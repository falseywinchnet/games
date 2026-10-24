#include "mowing_cues.hpp"

#include "gnome_voice.hpp"
#include "voice_fft.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

// audio_src/make_audio.py's one-shots, line for line in numpy's order of operations.
// One generator, default_rng(20261005), runs through the shots in the order main()
// builds them (the two formant "hoo"s, which the gnome's performed voice replaced, still
// draw first); each cue starts from the state the script had reached when it began
// that shot, as captured from the script itself. Each master is the float stereo the
// script handed to write_wav, which truncates (x * 32767) to 16 bits.
namespace mm {
namespace {

constexpr double sr = voice::sample_rate;
constexpr double tau = 2 * voice::pi;  // TAU = 2 * np.pi

using Spectrum = std::vector<voice::Complex>;

constexpr voice::Word128 script_increment{0xa5ba4327088fe212ull, 0xa23152b3367af769ull};

constexpr voice::GeneratorState script_at(std::uint64_t high, std::uint64_t low) {
    const voice::GeneratorState state{{high, low}, script_increment, false, 0u};
    return state;
}

constexpr voice::GeneratorState freeze_start = script_at(0x0b73b8c41a5664f7ull, 0x0e4fa527b25a8007ull);
constexpr voice::GeneratorState shatter_start = script_at(0x747809beaa02954cull, 0xbe7b93e8f78f383eull);
constexpr voice::GeneratorState scream_start = script_at(0x3f2d17561f0d80deull, 0x5a315f1e86cee7cfull);
constexpr voice::GeneratorState puff_start = script_at(0x6aa50213a19b2decull, 0x3eae4b353fe9d07bull);
constexpr voice::GeneratorState mushroom_start = script_at(0xa47116878f923ddcull, 0x09184c3ab49f74a2ull);
constexpr voice::GeneratorState bump_start = script_at(0x83be63d7984280afull, 0x1e73817f85b8c041ull);
constexpr voice::GeneratorState bonk_start = script_at(0xa486deaf48ed250full, 0xa526692c9f1fb559ull);

// np.fft.rfftfreq(n, 1 / SR) with f[0] = 1e-3, as resonances() and band() set it.
std::vector<double> frequencies(std::size_t n) {
    const double spacing = 1.0 / sr;
    const double unit = 1.0 / (static_cast<double>(n) * spacing);
    std::vector<double> f(n / 2 + 1);
    for (std::size_t k = 0; k < f.size(); ++k) {
        f[k] = static_cast<double>(k) * unit;
    }
    f[0] = 1e-3;
    return f;
}

// numpy's complex division (Smith's method, as its loops compute it).
voice::Complex divide(double ar, double ai, double br, double bi) {
    if (std::fabs(br) >= std::fabs(bi)) {
        const double rat = bi / br;
        const double scl = 1.0 / (br + bi * rat);
        return {(ar + ai * rat) * scl, (ai - ar * rat) * scl};
    }
    const double rat = br / bi;
    const double scl = 1.0 / (bi + br * rat);
    return {(ar * rat + ai) * scl, (ai * rat - ar) * scl};
}

voice::Complex multiply(voice::Complex a, voice::Complex b) {
    return {a.real() * b.real() - a.imag() * b.imag(), a.real() * b.imag() + a.imag() * b.real()};
}

struct Mode {
    double frequency;
    double q;
    double gain;
};

// resonances(n, modes, lowpass, highpass): parallel resonators' rfft response.
Spectrum resonances(std::size_t n, std::span<const Mode> modes, double lowpass = 0, double highpass = 0) {
    const std::vector<double> f = frequencies(n);
    Spectrum h(f.size(), voice::Complex(0.0, 0.0));
    for (const Mode& mode : modes) {
        for (std::size_t k = 0; k < f.size(); ++k) {
            const double x = f[k] / mode.frequency - mode.frequency / f[k];
            // 1j * q is (0, q); times the real array and plus 1 it is (1, q x)
            const voice::Complex z = multiply(voice::Complex(0.0, mode.q), voice::Complex(x, 0.0));
            const voice::Complex term = divide(mode.gain, 0.0, 1 + z.real(), 0 + z.imag());
            h[k] = voice::Complex(h[k].real() + term.real(), h[k].imag() + term.imag());
        }
    }
    if (lowpass != 0) {
        for (std::size_t k = 0; k < f.size(); ++k) {
            const double d = std::sqrt(1 + std::pow(f[k] / lowpass, 4.0));
            h[k] = divide(h[k].real(), h[k].imag(), d, 0.0);
        }
    }
    if (highpass != 0) {
        for (std::size_t k = 0; k < f.size(); ++k) {
            const double r = 1 / std::sqrt(1 + std::pow(highpass / f[k], 4.0));
            h[k] = multiply(h[k], voice::Complex(r, 0.0));
        }
    }
    return h;
}

// band(n, low, high): a real response, multiplied in as numpy's complex (b, 0).
Spectrum band(std::size_t n, double low, double high) {
    const std::vector<double> f = frequencies(n);
    Spectrum h(f.size());
    for (std::size_t k = 0; k < f.size(); ++k) {
        h[k] = voice::Complex(1 / std::sqrt(1 + std::pow(f[k] / high, 4.0)) / std::sqrt(1 + std::pow(low / f[k], 4.0)),
                              0.0);
    }
    return h;
}

// filtered(signal, response): circular, irfft(rfft(signal) * response, n)
voice::Signal filtered(std::span<const double> signal, const Spectrum& response) {
    Spectrum spectrum{};
    voice::rfft(signal, signal.size(), spectrum);
    for (std::size_t k = 0; k < spectrum.size(); ++k) {
        spectrum[k] = multiply(spectrum[k], response[k]);
    }
    voice::Signal out{};
    voice::irfft(spectrum, signal.size(), out);
    return out;
}

// pad_filter(signal, response_maker): a second of silence after, filtered, trimmed
template <class Maker>
voice::Signal pad_filter(std::span<const double> signal, Maker&& maker) {
    voice::Signal padded(signal.begin(), signal.end());
    padded.resize(signal.size() + voice::sample_rate, 0.0);
    voice::Signal out = filtered(padded, maker(padded.size()));
    out.resize(signal.size());
    return out;
}

voice::Signal normals(voice::Generator& rng, double scale, std::size_t n) {
    voice::Signal x(n);
    for (double& value : x) {
        value = rng.normal(0, scale);
    }
    return x;
}

// garden_space(mono, loop=False): early reflections from a house wall and fences
voice::Stereo garden_space(const voice::Signal& mono) {
    voice::Stereo out{mono, mono};
    const Spectrum soft = band(mono.size(), 60, 3500);
    struct Echo {
        double delay_ms;
        double gain;
        int side;
    };
    constexpr std::array<Echo, 5> echoes{{{9.5, 0.22, 0}, {13.0, 0.2, 1}, {21.0, 0.13, 0}, {27.5, 0.12, 1}, {41.0, 0.07, 0}}};
    for (const Echo& e : echoes) {
        const auto shift = static_cast<std::size_t>(sr * e.delay_ms / 1000);  // int(SR * delay_ms / 1000)
        voice::Signal delayed(mono.size(), 0.0);
        for (std::size_t i = shift; i < mono.size(); ++i) {
            delayed[i] = mono[i - shift];
        }
        voice::Signal echo = filtered(delayed, soft);
        for (double& value : echo) {
            value = value * e.gain;
        }
        voice::Signal& near = e.side == 0 ? out.left : out.right;
        voice::Signal& far = e.side == 0 ? out.right : out.left;
        for (std::size_t i = 0; i < mono.size(); ++i) {
            near[i] += echo[i];
            far[i] += echo[i] * 0.6;
        }
    }
    return out;
}

// finish(mono, peak): a 400-sample fade, the garden's space, scaled to the peak
voice::Stereo finish(const voice::Signal& input, double peak) {
    const std::size_t n = input.size();
    voice::Signal mono(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double fade = std::min(1.0, static_cast<double>(n - 1 - i) / 400);
        mono[i] = input[i] * fade;
    }
    voice::Stereo stereo = garden_space(mono);
    const double top = std::max(voice::peak(stereo.left), voice::peak(stereo.right));
    const double scale = peak / top;
    for (std::size_t i = 0; i < n; ++i) {
        stereo.left[i] = stereo.left[i] * scale;
        stereo.right[i] = stereo.right[i] * scale;
    }
    return stereo;
}

// formant_voice(f0, formants, breath, rng): a glottal pulse train through formant resonators
voice::Signal formant_voice(const voice::Signal& f0, std::span<const Mode> formants, double breath,
                            voice::Generator& rng) {
    const std::size_t n = f0.size();
    voice::Signal step(n);
    for (std::size_t i = 0; i < n; ++i) {
        step[i] = f0[i] / sr;
    }
    const voice::Signal phase = voice::cumulative_sum(step);
    voice::Signal source(n, 0.0);
    for (int k = 1; k < 30; ++k) {
        const double amplitude = 1.0 / std::pow(static_cast<double>(k), 1.9);
        const double w = tau * k;
        for (std::size_t i = 0; i < n; ++i) {
            const double harmonic = k * f0[i];
            source[i] += amplitude * std::sin(w * phase[i]) * (harmonic < 9000 ? 1.0 : 0.0);
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        source[i] += rng.normal(0, breath);
    }
    return pad_filter(source, [&](std::size_t m) { return resonances(m, formants); });
}

voice::Signal times(std::size_t n) {
    return voice::tarr(n);
}

voice::Stereo freeze() {
    voice::Generator rng(freeze_start);
    const std::size_t n = voice::samples(0.6);
    const voice::Signal t = times(n);
    voice::Signal sweep(n);
    for (std::size_t i = 0; i < n; ++i) {
        sweep[i] = 2600 * std::exp(-t[i] * 3.2) + 700;
    }
    const voice::Signal cs = voice::cumulative_sum(sweep);
    voice::Signal sparkle(n, 0.0);
    for (int k = 0; k < 18; ++k) {
        const double at = rng.uniform(0, 0.45);
        const double f = rng.uniform(4000, 8000);
        const double w = tau * f;
        voice::Signal ring(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double s = t[i] - at;
            ring[i] = s > 0 ? std::sin(w * s) * std::exp(-std::max(s, 0.0) * 60) : 0.0;
        }
        const double a = rng.uniform(0.2, 0.5);
        for (std::size_t i = 0; i < n; ++i) {
            sparkle[i] += ring[i] * a;
        }
    }
    const voice::Signal hiss = normals(rng, 1, n);
    const voice::Signal ice = pad_filter(hiss, [](std::size_t m) { return band(m, 4000, 11000); });
    voice::Signal mono(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double chirp = std::sin(tau * cs[i] / sr) * std::exp(-t[i] * 4);
        const double frost = ice[i] * std::exp(-t[i] * 6) * 0.15;
        mono[i] = chirp * 0.6 + sparkle[i] * 0.4 + frost;
    }
    return finish(mono, 0.5);
}

voice::Stereo shatter() {
    voice::Generator rng(shatter_start);
    const std::size_t n = voice::samples(1.2);
    const voice::Signal t = times(n);
    voice::Signal out(n, 0.0);
    const std::size_t c = voice::samples(0.004);
    const voice::Signal crack = normals(rng, 1, c);
    const voice::Signal ramp = voice::linspace(1, 0, c);
    for (std::size_t i = 0; i < c; ++i) {
        out[i] += crack[i] * ramp[i] * 0.9;
    }
    for (std::size_t i = 0; i < n; ++i) {
        out[i] += std::sin(tau * 120 * t[i]) * std::exp(-t[i] * 35) * 0.5;
    }
    for (int k = 0; k < 70; ++k) {
        const double at = 0.003 + rng.exponential(0.16);
        if (at > 1.0) {
            continue;
        }
        const bool big = rng.uniform(0, 1) < 0.2;
        voice::Signal piece(n, 0.0);
        for (int j = 0; j < 3; ++j) {
            const double f = big ? rng.uniform(1100, 2600) : rng.uniform(2500, 9500);
            const double decay = big ? rng.uniform(40, 90) : rng.uniform(70, 220);
            const double w = tau * f;
            const double phase = rng.uniform(0, tau);
            for (std::size_t i = 0; i < n; ++i) {
                const double s = t[i] - at;
                piece[i] += s > 0 ? std::sin(w * s + phase) * std::exp(-std::max(s, 0.0) * decay) : 0.0;
            }
        }
        const double level = rng.uniform(0.15, 0.5);
        const double fade = std::exp(-at * 1.5);
        for (std::size_t i = 0; i < n; ++i) {
            out[i] += piece[i] * level * fade;
        }
    }
    return finish(out, 0.75);
}

voice::Stereo scream() {
    voice::Generator rng(scream_start);
    const std::size_t n = voice::samples(0.85);
    const voice::Signal t = times(n);
    const double last = t[n - 1];
    voice::Signal f0(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double s = t[i] / last;
        const double contour = s < 0.25 ? 1050 + 450 * s / 0.25 : 1500 - 700 * (s - 0.25) / 0.75;
        f0[i] = contour * (1 + 0.04 * std::sin(tau * 7 * t[i]));
    }
    constexpr std::array<Mode, 3> formants{{{1000, 6, 0.9}, {2500, 9, 0.7}, {3400, 12, 0.35}}};
    const voice::Signal sound = formant_voice(f0, formants, 0.08, rng);
    voice::Signal shaped(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double envelope = std::min(1.0, t[i] / 0.04) * std::min(1.0, (last - t[i]) / 0.25);
        shaped[i] = sound[i] * envelope;
    }
    const voice::Signal distant = pad_filter(shaped, [](std::size_t m) { return band(m, 500, 3800); });
    return finish(distant, 0.35);
}

voice::Stereo puff() {
    voice::Generator rng(puff_start);
    const std::size_t n = voice::samples(0.45);
    const voice::Signal t = times(n);
    const voice::Signal breath = pad_filter(normals(rng, 1, n), [](std::size_t m) { return band(m, 700, 3200); });
    voice::Signal mono(n);
    for (std::size_t i = 0; i < n; ++i) {
        mono[i] = breath[i] * std::min(1.0, t[i] / 0.05) * std::exp(-t[i] * 7);
    }
    return finish(mono, 0.3);
}

voice::Stereo mushroom() {
    voice::Generator rng(mushroom_start);
    const std::size_t n = voice::samples(0.25);
    const voice::Signal t = times(n);
    constexpr std::array<Mode, 2> modes{{{620, 4, 1.0}, {1300, 6, 0.4}}};
    const voice::Signal wet = pad_filter(normals(rng, 1, n), [&](std::size_t m) { return resonances(m, modes); });
    voice::Signal squelch(n);
    for (std::size_t i = 0; i < n; ++i) {
        squelch[i] = wet[i] * std::exp(-t[i] * 25);
    }
    const double top = voice::peak(squelch) + 1e-9;
    voice::Signal mono(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double thock = std::sin(tau * 185 * t[i]) * std::exp(-t[i] * 40);
        mono[i] = thock * 0.7 + squelch[i] / top * 0.35;
    }
    return finish(mono, 0.45);
}

voice::Stereo bump() {
    voice::Generator rng(bump_start);
    const std::size_t n = voice::samples(0.45);
    const voice::Signal t = times(n);
    voice::Signal rattle(n, 0.0);
    for (const double f : {640.0, 930.0, 1350.0, 1780.0}) {
        const double w = tau * f;
        const double phase = rng.uniform(0, tau);
        const double decay = rng.uniform(14, 30);
        for (std::size_t i = 0; i < n; ++i) {
            rattle[i] += std::sin(w * t[i] + phase) * std::exp(-t[i] * decay);
        }
    }
    voice::Signal mono(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double thud = std::sin(tau * 88 * t[i]) * std::exp(-t[i] * 18);
        mono[i] = thud * 0.8 + rattle[i] * 0.25;
    }
    return finish(mono, 0.5);
}

// granny_bonk: three whacks of a rolling pin on the mower, a boing, and birdies circling
voice::Stereo granny_bonk() {
    voice::Generator rng(bonk_start);
    const std::size_t n = voice::samples(2.6);
    voice::Signal out(n, 0.0);
    const auto whack = [&](double at, double size) {
        const std::size_t i = voice::samples(at);
        const std::size_t m = voice::samples(0.5);
        const voice::Signal s = times(m);
        const voice::Signal noise = normals(rng, 1, m);
        constexpr std::array<std::array<double, 2>, 3> woods{{{520, 60}, {1130, 80}, {1870, 110}}};
        constexpr std::array<std::array<double, 3>, 5> clangs{
            {{310, 9, 0.6}, {742, 7, 0.5}, {1385, 6, 0.35}, {2290, 8, 0.25}, {3510, 11, 0.15}}};
        // sum() of a generator starts from the integer 0
        voice::Signal wood(m, 0.0);
        for (const auto& [f, d] : woods) {
            const double w = tau * f;
            for (std::size_t j = 0; j < m; ++j) {
                wood[j] = wood[j] + std::sin(w * s[j]) * std::exp(-s[j] * d);
            }
        }
        voice::Signal clang(m, 0.0);
        for (const auto& [f, d, a] : clangs) {
            const double w = tau * f;
            const double phase = rng.uniform(0, tau);
            for (std::size_t j = 0; j < m; ++j) {
                clang[j] = clang[j] + std::sin(w * s[j] + phase) * std::exp(-s[j] * d) * a;
            }
        }
        const std::size_t count = std::min(m, n - i);
        for (std::size_t j = 0; j < count; ++j) {
            const double crack = noise[j] * std::exp(-s[j] * 160);
            const double thud = std::sin(tau * 95 * s[j]) * std::exp(-s[j] * 30);
            const double hit = (crack * 0.5 + wood[j] * 0.5 + clang[j] * 0.55 + thud * 0.8) * size;
            out[i + j] += hit;
        }
    };
    whack(0.02, 1.0);
    whack(0.2, 0.8);
    whack(0.36, 1.1);
    {
        const std::size_t i = voice::samples(0.5);
        const std::size_t m = voice::samples(1.1);
        const voice::Signal s = times(m);
        voice::Signal f(m);
        for (std::size_t j = 0; j < m; ++j) {
            f[j] = 150 * (1 + 0.35 * std::exp(-s[j] * 3) * std::sin(tau * 11 * s[j])) * (1 + 0.6 * std::exp(-s[j] * 12));
        }
        const voice::Signal cs = voice::cumulative_sum(f);
        for (std::size_t j = 0; j < m; ++j) {
            const double boing = std::sin(tau * cs[j] / sr) + 0.35 * std::sin(2 * tau * cs[j] / sr);
            out[i + j] += boing * std::exp(-s[j] * 2.6) * 0.7;
        }
    }
    for (int k = 0; k < 7; ++k) {
        const double at = 1.25 + 0.17 * k;
        const std::size_t j0 = voice::samples(at);
        const std::size_t q = voice::samples(0.09);
        const voice::Signal u = times(q);
        voice::Signal f(q);
        for (std::size_t j = 0; j < q; ++j) {
            f[j] = 3200 + 1800 * u[j] / 0.09 + 300 * std::sin(tau * 40 * u[j]);
        }
        const voice::Signal cs = voice::cumulative_sum(f);
        const double level = 1 - k / 9.0;
        for (std::size_t j = 0; j < q; ++j) {
            const double chirp = std::sin(tau * cs[j] / sr) * std::sin(voice::pi * u[j] / 0.09);
            out[j0 + j] += chirp * 0.18 * level;
        }
    }
    return finish(out, 0.75);
}

constexpr voice::Cue cues[] = {
    {.name = "mm_freeze", .stereo = freeze},
    {.name = "mm_shatter", .stereo = shatter},
    {.name = "mm_scream", .stereo = scream},
    {.name = "mm_puff", .stereo = puff},
    {.name = "mm_mushroom", .stereo = mushroom},
    {.name = "mm_bump", .stereo = bump},
    {.name = "mm_granny_bonk", .stereo = granny_bonk},
};

}  // namespace

std::span<const voice::Cue> sound_cues() {
    const std::span<const voice::Cue> result(cues);
    return result;
}

void register_sound_cues() {
    voice::Bank::shared().add(cues);
    voice::Bank::shared().add(gnome_cues());
}

}  // namespace mm
