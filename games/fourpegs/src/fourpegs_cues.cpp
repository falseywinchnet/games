#include "fourpegs_cues.hpp"

#include "voice_fft.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

// Each effect follows audio_src/make_sfx.py line for line, in numpy's order of
// operations. Its write() fades out the last 4 ms and has no fade-in. The states are
// what tools/numpy_reference.py (shared/voice, `cues --fade-in 0`) printed for the
// script: synth._rng (seeded 12345) is shared by every cue in script order, and the
// script's own rng (seeded 1888) drives the butler's glottal pulses and the rubble.
namespace fp {
namespace {

using voice::Signal;

constexpr double two_pi = 2 * voice::pi;
constexpr double sr = voice::sample_rate;

constexpr voice::Word128 synth_increment{0xc9c7353e6e2b1f28ull, 0x7d761f2d4027fae7ull};
constexpr voice::Word128 script_increment{0x0df8ffb4a300b4d5ull, 0x9e4802c569fccccdull};  // default_rng(1888)

constexpr voice::GeneratorState synth_at(std::uint64_t high, std::uint64_t low) {
    const voice::GeneratorState state{{high, low}, synth_increment, false, 0u};
    return state;
}

constexpr voice::GeneratorState script_at(std::uint64_t high, std::uint64_t low) {
    const voice::GeneratorState state{{high, low}, script_increment, false, 0u};
    return state;
}

struct Start {
    voice::GeneratorState synth;
    voice::GeneratorState rng;
};

constexpr Start voice_starts[8] = {
    {synth_at(0x1905e0335aae9634ull, 0x9199b0d09775add5ull), script_at(0x68f1cfd5e5f34695ull, 0x69d298396ede4987ull)},
    {synth_at(0xe74628592338bea2ull, 0xfbd8355fe1749006ull), script_at(0x06cfa03028c3e686ull, 0xb314ee80e342f9aaull)},
    {synth_at(0xacbe29c384173c57ull, 0xe161b59af6f8c234ull), script_at(0xefd1767dd048ca99ull, 0xbeb88e75cc2a0eb7ull)},
    {synth_at(0x5a0822b287776e0eull, 0xd925ea9cb71b94dfull), script_at(0x674375f1570c38eaull, 0x47e73e5b75cafc96ull)},
    {synth_at(0xe31c3f69d90937f4ull, 0x9b70cc7062927ae4ull), script_at(0xac2e36992d340bc8ull, 0xaf4ec8d5b5ad6631ull)},
    {synth_at(0xc6f0254bf96f7d70ull, 0x33d1dc85bdebfd14ull), script_at(0xf36c63d40668e831ull, 0x1cc5d2c3cf3228ddull)},
    {synth_at(0x68d494f27b234b84ull, 0x11ec9872aab74ea9ull), script_at(0x05e55b1b0416a287ull, 0x8085cda87a160573ull)},
    {synth_at(0x49e7f035774c0e43ull, 0xbd022633fd90fba9ull), script_at(0x40f52e3e3aac4896ull, 0x3388655aaf77640full)},
};
constexpr voice::GeneratorState hm_rng_start = script_at(0x8bd452634a2ecae9ull, 0xf07c39e29d3079bcull);
constexpr Start chuckle_start{synth_at(0xb16be77e024ced59ull, 0x9f840cb4d2165f88ull),
                              script_at(0x0f27d35d0191a9edull, 0xb1815d12d518e166ull)};
constexpr Start laugh_start{synth_at(0x8eee69616db98029ull, 0x06ddaf734b0418c3ull),
                            script_at(0x31a376011cd79b75ull, 0xaeaa64c0fc433cbcull)};
constexpr voice::GeneratorState rumble_start = synth_at(0xbd5fb5b663549b9aull, 0x19c9010d434a9529ull);
constexpr Start crash_start{synth_at(0x86e11d317ac4954eull, 0x054aeb6ec17a14e0ull),
                            script_at(0xf684bb3e0720448cull, 0xd1673043dcc67710ull)};
constexpr voice::GeneratorState console_start = synth_at(0x6c2fd76dbf26231dull, 0x4238005254460affull);
constexpr voice::GeneratorState pick_start = synth_at(0xbd5ac930c6e221f4ull, 0x7b4c7c8d6def8c2dull);
constexpr voice::GeneratorState desk_start = synth_at(0x4585c3221b6ba512ull, 0xb94de6defd7f428eull);
constexpr voice::GeneratorState clap_start = synth_at(0x746c312e49b0c24aull, 0x22f1113eb45c432full);

// write(name, x, peak_db): no fade-in in this script
Signal write(std::span<const double> x, double peak_db) {
    const Signal result = voice::master(x, peak_db, .004, 0);
    return result;
}

double sign(double x) {
    const double result = x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0);
    return result;
}

struct Vowel {
    double f[3];
};
constexpr Vowel vowel_a{{730, 1090, 2440}};
constexpr Vowel vowel_o{{570, 840, 2410}};
constexpr Vowel vowel_e{{530, 1840, 2480}};
constexpr Vowel vowel_u{{440, 1020, 2240}};
constexpr Vowel vowel_i{{390, 1990, 2550}};
constexpr Vowel vowel_uh{{520, 1190, 2390}};

// glottal(f0_t, fry): a pulse train with jitter, shimmer and occasional fry pulses.
Signal glottal(voice::Generator& rng, const Signal& f0_t, double fry = .15) {
    const std::size_t n = f0_t.size();
    Signal y(n, 0.0);
    double t = 0.0;
    while (t < static_cast<double>(n) - 1) {
        const std::size_t i = static_cast<std::size_t>(t);
        const double f = f0_t[i] * (1 + rng.normal(0, .02));
        double period = sr / (f > 40 ? f : 40.0);
        if (rng.random() < fry) {
            period *= rng.uniform(1.6, 2.4);  // a creaky, gravelly skip
        }
        const double amp = 1 + rng.normal(0, .12);
        const std::size_t length = static_cast<std::size_t>(period * .6);
        const double divisor = static_cast<double>(std::max<std::size_t>(1, length));
        for (std::size_t k = 0; k < length && i + k < n; ++k) {
            y[i + k] += std::pow(std::sin(voice::pi * static_cast<double>(k) / divisor), 3.0) * amp;
        }
        t += period;
    }
    Signal d(n);
    for (std::size_t i = 0; i < n; ++i) {
        d[i] = i == 0 ? y[0] - 0 : y[i] - y[i - 1];
    }
    return d;
}

// formants(src, vowel, bw, gains): three Gaussian vowel resonances in the spectrum.
Signal formants(const Signal& src, const Vowel& vowel, const double (&bw)[3] = {90, 110, 160},
                const double (&gains)[3] = {1, .55, .25}) {
    const std::size_t n = src.size();
    std::vector<voice::Complex> spectrum{};
    voice::rfft(src, n, spectrum);
    const double unit = 1.0 / (static_cast<double>(n) * (1.0 / sr));
    for (std::size_t k = 0; k < spectrum.size(); ++k) {
        const double f = static_cast<double>(k) * unit;
        double h = 0;
        for (int j = 0; j < 3; ++j) {
            const double z = (f - vowel.f[j]) / bw[j];
            h = h + gains[j] * std::exp(-.5 * (z * z));
        }
        h = h + .02;
        spectrum[k] = voice::Complex(spectrum[k].real() * h, spectrum[k].imag() * h);
    }
    Signal out{};
    voice::irfft(spectrum, n, out);
    return out;
}

Signal syllable(voice::Synth& synth, voice::Generator& rng, const Vowel& vowel, double f0, double contour, double dur,
                double fry = .18, double breath = .05) {
    const std::size_t n = voice::samples(dur);
    const Signal t = voice::tarr(n);
    Signal f0_t(n);
    const double bend = .6 * contour;
    for (std::size_t i = 0; i < n; ++i) {
        const double u = t[i] / dur;
        f0_t[i] = f0 * (1 + contour * u - bend * (u * u));
    }
    Signal v = formants(glottal(rng, f0_t, fry), vowel);
    const Signal hiss = voice::fft_filter(synth.noise(n), 400, 3000);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = v[i] + breath * hiss[i];
        const double env = std::min(1.0, t[i] / .018) * std::min(1.0, (dur - t[i]) / .05);
        v[i] = v[i] * std::clamp(env, 0.0, 1.0);
    }
    return v;
}

// tone(f, dur, decay, partials): a struck tone of a few partials.
struct Partial {
    double ratio;
    double amplitude;
};

Signal tone(double f, double dur, double decay, std::span<const Partial> partials) {
    const std::size_t n = voice::samples(dur);
    const Signal t = voice::tarr(n);
    Signal y(n, 0.0);
    for (const Partial& p : partials) {
        const double w = two_pi * f * p.ratio;
        for (std::size_t i = 0; i < n; ++i) {
            y[i] = y[i] + p.amplitude * std::sin(w * t[i]);
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * std::exp(-t[i] / decay);
    }
    return y;
}

constexpr Partial pure[] = {{1, 1}};

// his voice: eight syllables, played as text types out
template <int I>
Signal voice_syllable() {
    struct Spoken {
        const Vowel* vowel;
        double f0;
        double contour;
    };
    static constexpr Spoken words[8] = {{&vowel_a, 104, -.08}, {&vowel_o, 98, -.12},  {&vowel_e, 112, -.05},
                                        {&vowel_u, 96, -.1},   {&vowel_i, 116, -.15}, {&vowel_uh, 100, .05},
                                        {&vowel_o, 108, -.2},  {&vowel_a, 94, -.04}};
    voice::Synth synth(voice_starts[I].synth);
    voice::Generator rng(voice_starts[I].rng);
    const Signal y = syllable(synth, rng, *words[I].vowel, words[I].f0, words[I].contour, .13 + .03 * (I % 3));
    return write(y, -13);
}

// a closed-mouth "hm", nasal and low
Signal voice_hm() {
    voice::Generator rng(hm_rng_start);
    const double f0 = 96;
    const double dur = .5;
    const std::size_t n = voice::samples(dur);
    const Signal t = voice::tarr(n);
    Signal f0_t(n);
    for (std::size_t i = 0; i < n; ++i) {
        f0_t[i] = f0 * (1 + .12 * std::sin(voice::pi * t[i] / dur));
    }
    static constexpr double bw[3] = {60, 80, 120};
    static constexpr double gains[3] = {1, .15, .05};
    Signal v = formants(glottal(rng, f0_t, .1), vowel_u, bw, gains);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = v[i] * std::min(1.0, t[i] / .03) * std::clamp((dur - t[i]) / .1, 0.0, 1.0);
    }
    return write(v, -14);
}

Signal chuckle() {
    voice::Synth synth(chuckle_start.synth);
    voice::Generator rng(chuckle_start.rng);
    Signal out(voice::samples(.75), 0.0);
    static constexpr double parts[3][2] = {{118, .11}, {110, .11}, {102, .14}};
    for (int i = 0; i < 3; ++i) {
        const Signal s = syllable(synth, rng, vowel_uh, parts[i][0], -.1, parts[i][1], .3, .25);
        const std::size_t at = voice::samples(i * .2);
        const double g = 1 - .15 * i;
        for (std::size_t k = 0; k < s.size() && at + k < out.size(); ++k) {
            out[at + k] += s[k] * g;
        }
    }
    return write(out, -12);
}

// his maniacal laugh: slow, then faster and higher, then a falling cackle
Signal laugh() {
    voice::Synth synth(laugh_start.synth);
    voice::Generator rng(laugh_start.rng);
    static constexpr double beats[14][3] = {{.0, 108, .2},   {.42, 112, .2},  {.8, 120, .16},  {1.08, 130, .14},
                                            {1.3, 142, .13}, {1.5, 152, .13}, {1.69, 160, .13}, {1.88, 168, .14},
                                            {2.08, 172, .15}, {2.3, 166, .15}, {2.52, 154, .15}, {2.76, 140, .16},
                                            {3.02, 124, .2}, {3.34, 110, .3}};
    Signal out(voice::samples(3.9), 0.0);
    for (const double* b : beats) {
        const Signal s = syllable(synth, rng, vowel_a, b[1], -.08, b[2], .25, .18);
        const std::size_t at = voice::samples(b[0]);
        const double g = .7 + .3 * std::min(1.0, b[0] / 1.5);
        for (std::size_t k = 0; k < s.size() && at + k < out.size(); ++k) {
            out[at + k] += s[k] * g;
        }
    }
    return write(out, -8);
}

// the lair coming down: a long swelling rumble, and crashes of falling masonry
Signal rumble() {
    voice::Synth synth(rumble_start);
    const std::size_t n = voice::samples(7.5);
    const Signal t = voice::tarr(n);
    const Signal low = voice::fft_filter(synth.noise(n), 25, 160);
    const Signal mid = voice::fft_filter(synth.noise(n), 200, 900);
    const double w31 = two_pi * 3.1;
    const double w07 = two_pi * .7;
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        double r = low[i] * std::min(1.0, t[i] / 3.0) * (1 + .3 * std::sin(w31 * t[i]));
        const double swell = .5 + .5 * std::sin(w07 * t[i]);
        r = r + .4 * mid[i] * std::min(1.0, t[i] / 5.0) * (swell * swell);
        x[i] = r * std::clamp((7.5 - t[i]) / 1.0, 0.0, 1.0);
    }
    return write(x, -7);
}

Signal crash() {
    voice::Synth synth(crash_start.synth);
    voice::Generator rng(crash_start.rng);
    const std::size_t m = voice::samples(1.4);
    const Signal tt = voice::tarr(m);
    const Signal dust = voice::fft_filter(synth.noise(m), 300, 5000);
    Signal y(m);
    for (std::size_t i = 0; i < m; ++i) {
        y[i] = std::sin(two_pi * (55 + 30 * std::exp(-tt[i] / .05)) * tt[i]) * std::exp(-tt[i] / .25);
        y[i] = y[i] + .8 * dust[i] * std::exp(-tt[i] / .12);
    }
    const std::size_t l = voice::samples(.05);
    const Signal tl = voice::tarr(l);
    for (int k = 0; k < 9; ++k) {  // rattling rubble
        const std::size_t at = voice::samples(.08 + .1 * k + .03 * rng.random());
        const Signal rubble = voice::fft_filter(synth.noise(l), 1500, 7000);
        const double fall = 1 - k / 10.0;
        for (std::size_t i = 0; i < l && at + i < m; ++i) {
            y[at + i] += .25 * rubble[i] * std::exp(-tl[i] / .01) * fall;
        }
    }
    return write(y, -8);
}

// the console
Signal console_boot() {
    voice::Synth synth(console_start);
    const std::size_t n = voice::samples(.9);
    const Signal t = voice::tarr(n);
    Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double u = t[i] / .9;
        f[i] = 200 + 1600 * (u * u);
    }
    const Signal cs = voice::cumulative_sum(f);
    Signal chirps(n, 0.0);
    for (int k = 0; k < 4; ++k) {
        const Signal c = tone(1800 + 300 * k, .9, .03, pure);
        const double from = .45 + .08 * k;
        for (std::size_t i = 0; i < n; ++i) {
            chirps[i] = chirps[i] + c[i] * (t[i] > from ? 1.0 : 0.0);
        }
    }
    const Signal hiss = voice::fft_filter(synth.noise(n), 2000, 8000);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double z = (t[i] - .45) / .3;
        const double sweep = std::sin(two_pi * cs[i] / sr) * std::exp(-(z * z));
        x[i] = sweep * .6 + .2 * chirps[i] + .05 * hiss[i] * std::exp(-t[i] / .3);
    }
    return write(x, -12);
}

Signal pick() {
    voice::Synth synth(pick_start);
    static constexpr Partial partials[] = {{1, 1}, {2.7, .3}};
    const Signal y = tone(2600, .12, .02, partials);
    const std::size_t n = voice::samples(.12);
    const Signal t = voice::tarr(n);
    const Signal hiss = voice::fft_filter(synth.noise(n), 3000, 9000);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = y[i] + .05 * hiss[i] * std::exp(-t[i] / .01);
    }
    return write(x, -18);
}

template <int I>
Signal place() {
    const std::size_t m = voice::samples(.28);
    const Signal tt = voice::tarr(m);
    Signal f(m);
    for (std::size_t i = 0; i < m; ++i) {
        f[i] = 1200 + 900 * tt[i] / .28 + 80 * I;
    }
    const Signal cs = voice::cumulative_sum(f);
    Signal x(m);
    for (std::size_t i = 0; i < m; ++i) {
        const double thunk = std::sin(two_pi * (150 - 60 * tt[i] / .28) * tt[i]) * std::exp(-tt[i] / .04);
        const double zing = std::sin(two_pi * cs[i] / sr) * std::exp(-tt[i] / .09) * .35;
        x[i] = thunk + zing;
    }
    return write(x, -15);
}

Signal remove() {
    const std::size_t m = voice::samples(.25);
    const Signal tt = voice::tarr(m);
    Signal f(m);
    for (std::size_t i = 0; i < m; ++i) {
        f[i] = 1500 - 1100 * tt[i] / .25;
    }
    const Signal cs = voice::cumulative_sum(f);
    Signal x(m);
    for (std::size_t i = 0; i < m; ++i) {
        x[i] = std::sin(two_pi * cs[i] / sr) * std::exp(-tt[i] / .07);
    }
    return write(x, -18);
}

Signal check() {
    const std::size_t m = voice::samples(.6);
    const Signal tt = voice::tarr(m);
    Signal f(m);
    for (std::size_t i = 0; i < m; ++i) {
        f[i] = 600 + 2400 * tt[i] / .6;
    }
    const Signal cs = voice::cumulative_sum(f);
    const double w = two_pi * 110;
    Signal x(m);
    for (std::size_t i = 0; i < m; ++i) {
        const double press = std::sin(w * tt[i]) * std::exp(-tt[i] / .03);
        const double z = (tt[i] - .3) / .15;
        const double scan = std::sin(two_pi * cs[i] / sr) * std::exp(-(z * z)) * .3;
        x[i] = press + scan;
    }
    return write(x, -13);
}

Signal pin_exact() {
    static constexpr Partial partials[] = {{1, 1}, {2.76, .25}, {5.4, .08}};
    return write(tone(1760, .7, .25, partials), -15);
}

Signal pin_near() {
    static constexpr Partial partials[] = {{1, 1}, {2.0, .15}};
    return write(tone(1175, .45, .12, partials), -18);
}

Signal pin_none() {
    static constexpr Partial partials[] = {{1, 1}, {3, .4}, {5, .2}};
    Signal y = tone(98, .35, .1, partials);
    const Signal t = voice::tarr(voice::samples(.35));
    const double w = two_pi * 30;
    for (std::size_t i = 0; i < y.size(); ++i) {
        y[i] = y[i] * (1 + .5 * sign(std::sin(w * t[i])));
    }
    return write(y, -20);
}

Signal invalid() {
    static constexpr Partial partials[] = {{1, 1}, {1.5, .5}};
    return write(tone(220, .18, .06, partials), -20);
}

// the desk: a gloved palm on old wood, and restrained applause
Signal desk() {
    voice::Synth synth(desk_start);
    const std::size_t m = voice::samples(.4);
    const Signal tt = voice::tarr(m);
    const Signal wood = voice::fft_filter(synth.noise(m), 200, 1800);
    Signal x(m);
    for (std::size_t i = 0; i < m; ++i) {
        x[i] = std::sin(two_pi * (85 + 40 * std::exp(-tt[i] / .02)) * tt[i]) * std::exp(-tt[i] / .08) +
               .3 * wood[i] * std::exp(-tt[i] / .025);
    }
    return write(x, -9);
}

Signal clap() {
    voice::Synth synth(clap_start);
    const std::size_t m = voice::samples(.14);
    const Signal tt = voice::tarr(m);
    Signal x = voice::fft_filter(synth.noise(m), 300, 2500);
    for (std::size_t i = 0; i < m; ++i) {
        x[i] = x[i] * std::exp(-tt[i] / .018);
    }
    return write(x, -14);
}

constexpr voice::Cue cues[] = {
    {.name = "fp_voice_01", .mono = voice_syllable<0>},
    {.name = "fp_voice_02", .mono = voice_syllable<1>},
    {.name = "fp_voice_03", .mono = voice_syllable<2>},
    {.name = "fp_voice_04", .mono = voice_syllable<3>},
    {.name = "fp_voice_05", .mono = voice_syllable<4>},
    {.name = "fp_voice_06", .mono = voice_syllable<5>},
    {.name = "fp_voice_07", .mono = voice_syllable<6>},
    {.name = "fp_voice_08", .mono = voice_syllable<7>},
    {.name = "fp_voice_hm", .mono = voice_hm},
    {.name = "fp_chuckle", .mono = chuckle},
    {.name = "fp_laugh", .mono = laugh},
    {.name = "fp_rumble", .mono = rumble},
    {.name = "fp_crash", .mono = crash},
    {.name = "fp_console_boot", .mono = console_boot},
    {.name = "fp_pick", .mono = pick},
    {.name = "fp_place_01", .mono = place<0>},
    {.name = "fp_place_02", .mono = place<1>},
    {.name = "fp_place_03", .mono = place<2>},
    {.name = "fp_remove", .mono = remove},
    {.name = "fp_check", .mono = check},
    {.name = "fp_pin_exact", .mono = pin_exact},
    {.name = "fp_pin_near", .mono = pin_near},
    {.name = "fp_pin_none", .mono = pin_none},
    {.name = "fp_invalid", .mono = invalid},
    {.name = "fp_desk", .mono = desk},
    {.name = "fp_clap", .mono = clap},
    {.name = "fp_music_t1", .stereo = music_tier1},
    {.name = "fp_music_t2", .stereo = music_tier2},
    {.name = "fp_music_t3", .stereo = music_tier3},
    {.name = "fp_stinger_defeat", .stereo = stinger_defeat},
    {.name = "fp_stinger_triumph", .stereo = stinger_triumph},
    {.name = "fp_stinger_topscore", .stereo = stinger_topscore},
};

}  // namespace

std::span<const voice::Cue> sound_cues() {
    const std::span<const voice::Cue> result(cues);
    return result;
}

void register_sound_cues() {
    voice::Bank::shared().add(cues);
}

}  // namespace fp
