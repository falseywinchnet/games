#include "zen_cues.hpp"

#include "voice_fft.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

// Each cue follows audio_src/make_sfx.py or audio_src/stone_garden.py line for line, in
// numpy's order of operations. The states are what music_reference.py printed for the
// scripts: make_sfx.py's own rng (seeded 2604) is shared by every modes() call in script
// order, so each effect that strikes modes starts from the state the script had reached;
// everything else draws from generators seeded on the spot. synth._rng is never used by
// make_sfx.py; stone_garden.py's pads draw from it from the start.
namespace zc {
namespace {

using voice::Signal;
using voice::Stereo;

constexpr double two_pi = 2 * voice::pi;  // Python's 2 * np.pi
constexpr double sr = voice::sample_rate;

constexpr voice::Word128 script_increment{0xc8b275173436fb43ull, 0x47f351903078390bull};

constexpr voice::GeneratorState script_at(std::uint64_t high, std::uint64_t low) {
    const voice::GeneratorState state{{high, low}, script_increment, false, 0u};
    return state;
}

// rng = np.random.default_rng(2604) where make_sfx.py began each sound that uses it
constexpr voice::GeneratorState clip_start = script_at(0x7b540aaa03486885ull, 0x27e4f53cd42487b2ull);
constexpr voice::GeneratorState unclip_start = script_at(0xc594adc0e28d5613ull, 0x3f4c53347ca3e08dull);
constexpr voice::GeneratorState knock_starts[3] = {script_at(0x10250d58dacab784ull, 0x8884d1381270e08bull),
                                                   script_at(0x43aaa46766c820aeull, 0x53266b5b3eb7bf79ull),
                                                   script_at(0xd392a7495bff4ef7ull, 0x6cf017b705b5ecd7ull)};
constexpr voice::GeneratorState tumble_start = script_at(0xe7f28dd640585878ull, 0x32044542378dfce3ull);
constexpr voice::GeneratorState whoosh_start = script_at(0x207b96fe3e91e90full, 0xee120805ba2668b7ull);
constexpr voice::GeneratorState land_start = script_at(0x2f0d9081d1e9a881ull, 0x466a7bd4c8d421bdull);
constexpr voice::GeneratorState reset_start = script_at(0x114c388d73fb53e4ull, 0x331d144161a393cdull);
constexpr voice::GeneratorState click_start = script_at(0xb2ed8337737c707cull, 0x189a7ee6594cbca1ull);
constexpr voice::GeneratorState relay_start = script_at(0x68204731b7e90d76ull, 0xb56b74458d2b3545ull);
constexpr voice::GeneratorState clunk_start = script_at(0x89c1b0c0a60cf175ull, 0x3604b4789b500198ull);
constexpr voice::GeneratorState best_start = script_at(0xbec35fc80e7ed8fcull, 0x38f1a9129798974cull);
// the squeak, the birds, the motor, the brook and the rush leave it untouched, so the
// reeds (rendered before the rush is written) begin where the squeak did
constexpr voice::GeneratorState reeds_start = script_at(0x1f4762b995f2f506ull, 0x41b06800e530d510ull);
constexpr voice::GeneratorState bed_starts[3] = {script_at(0x9584fc71464b032dull, 0x88b21734cf87ba6cull),
                                                 script_at(0xa6d9e045e3297446ull, 0x12e46b5bd5ab2e64ull),
                                                 script_at(0xe08ea5bbe89c3b52ull, 0x3eb06b181cc3b096ull)};
// stone_garden.py's rng = np.random.default_rng(1964), as imported
constexpr voice::GeneratorState garden_start{
    {0x407a550428096cecull, 0x7f84beb71f89528aull}, {0xcedf282e7d5088f5ull, 0x8ffd09b802b56e93ull}, false, 0u};

// ---------------------------------------------------------------- the script's helpers

std::size_t secs(double n) {
    const std::size_t result = voice::samples(n);
    return result;
}

double peak(const Stereo& x) {
    const double result = std::max(voice::peak(x.left), voice::peak(x.right));
    return result;
}

Signal normals(voice::Generator& r, std::size_t n) {
    Signal x(n);
    r.fill_standard_normal(x);
    return x;
}

// modes(n, freqs, amps, taus): decaying sines with phases from the script's rng
Signal modes(voice::Generator& rng, std::size_t n, const std::vector<double>& freqs, const std::vector<double>& amps,
             const std::vector<double>& taus) {
    const Signal t = voice::tarr(n);
    Signal y(n, 0.0);
    const std::size_t count = std::min(freqs.size(), std::min(amps.size(), taus.size()));
    for (std::size_t i = 0; i < count; ++i) {
        const double f = freqs[i];
        if (f > 20000) {
            continue;
        }
        const double ph = rng.uniform(0, 6.28);
        const double a = amps[i];
        const double tau = taus[i];
        const double w = two_pi * f;
        for (std::size_t j = 0; j < n; ++j) {
            y[j] += a * std::exp(-t[j] / tau) * std::sin(w * t[j] + ph);
        }
    }
    return y;
}

// burst(n, lo, hi, tau, seed): a breath of band-limited noise, decaying
Signal burst(std::size_t n, double lo, double hi, double tau, std::uint64_t seed) {
    voice::Generator r(seed);
    const Signal noise = normals(r, n);
    Signal x = voice::fft_filter(noise, lo, hi, 2);
    const Signal t = voice::tarr(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = x[i] * std::exp(-t[i] / tau);
    }
    return x;
}

void place(Signal& dst, std::span<const double> src, double at) {
    const std::size_t s = secs(at);
    for (std::size_t i = 0; i < src.size() && s + i < dst.size(); ++i) {
        dst[s + i] += src[i];
    }
}

// a * x + b * y, numpy's (a * x) + (b * y)
Signal mixed(double a, const Signal& x, double b, const Signal& y) {
    Signal result(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        result[i] = a * x[i] + b * y[i];
    }
    return result;
}

Signal times(double a, const Signal& x) {
    Signal result(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        result[i] = a * x[i];
    }
    return result;
}

// roomy(x, wet, length, rt): a little outdoor air round a mono sound; stereo
Stereo roomy(const Signal& x, double wet = 0.18, double length = 0.9, double rt = 0.5) {
    const Stereo ir = voice::make_ir(length, rt * 1.2, rt, rt * 0.5, 0.006, 11);
    const std::size_t n = x.size() + ir.left.size();
    Stereo dry{Signal(n, 0.0), Signal(n, 0.0)};
    std::copy(x.begin(), x.end(), dry.left.begin());
    std::copy(x.begin(), x.end(), dry.right.begin());
    Stereo wetx = voice::conv_reverb(dry, ir, false);
    wetx.left.resize(n);
    wetx.right.resize(n);
    const double top = peak(wetx) + 1e-12;
    const double level = voice::peak(x);
    Stereo out{Signal(n), Signal(n)};
    for (std::size_t i = 0; i < n; ++i) {
        out.left[i] = dry.left[i] + wet * wetx.left[i] / top * level;
        out.right[i] = dry.right[i] + wet * wetx.right[i] / top * level;
    }
    return out;
}

// finish(x, peak_db) and write_wav's clip
void finish_channel(Signal& y, double top, double gain, std::size_t fade) {
    for (double& v : y) {
        v = v / top * gain;
    }
    const std::size_t k = std::min(y.size(), fade);
    const Signal ramp = voice::linspace(1, 0, k);
    for (std::size_t i = 0; i < k; ++i) {
        y[y.size() - k + i] *= ramp[i];
    }
    const Signal rise = voice::linspace(0, 1, 16);
    for (std::size_t i = 0; i < 16 && i < y.size(); ++i) {
        y[i] *= rise[i];
    }
    for (double& v : y) {
        v = std::clamp(v, -1.0, 1.0);
    }
}

Signal finish(Signal x, double peak_db) {
    const double top = voice::peak(x) + 1e-12;
    finish_channel(x, top, std::pow(10.0, peak_db / 20), secs(0.004));
    return x;
}

Stereo finish(Stereo x, double peak_db) {
    const double top = peak(x) + 1e-12;
    const double gain = std::pow(10.0, peak_db / 20);
    finish_channel(x.left, top, gain, secs(0.004));
    finish_channel(x.right, top, gain, secs(0.004));
    return x;
}

// main()'s beds and score: x / max|x| * 10 ** (-3 / 20), then write_wav's clip
Stereo level_bed(Stereo x) {
    const double top = peak(x);
    const double gain = std::pow(10.0, -3.0 / 20);
    for (Signal* channel : {&x.left, &x.right}) {
        for (double& v : *channel) {
            v = std::clamp(v / top * gain, -1.0, 1.0);
        }
    }
    return x;
}

Signal phase_of(const Signal& f) {
    const Signal cs = voice::cumulative_sum(f);
    Signal ph(cs.size());
    for (std::size_t i = 0; i < cs.size(); ++i) {
        ph[i] = two_pi * cs[i] / sr;
    }
    return ph;
}

double clip01(double x) {
    const double result = std::min(std::max(x, 0.0), 1.0);
    return result;
}

// ---------------------------------------------------------------- stone and steel

// stone_knock(f0, vel, seed): a click of contact, a few inharmonic modes, a dull body
Signal stone_knock(voice::Generator& rng, double f0, double vel, std::uint64_t seed) {
    voice::Generator r(seed);
    const std::size_t n = secs(0.16);
    const double r1 = 1.58 * r.uniform(.97, 1.03);
    const double r2 = 2.31 * r.uniform(.96, 1.04);
    const double r3 = 3.9 * r.uniform(.95, 1.05);
    const std::vector<double> freqs{f0 * 1.0, f0 * r1, f0 * r2, f0 * r3, f0 * 5.2};
    Signal y = modes(rng, n, freqs, {1, .7, .45, .25, .12}, {.016, .011, .008, .005, .003});
    const Signal b = burst(n, 1800, 9000, 0.0016, seed);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] += 0.9 * b[i];
    }
    const double body = r.uniform(140, 210);
    const Signal low = modes(rng, n, {body}, {1}, {.022});
    for (std::size_t i = 0; i < n; ++i) {
        y[i] += 0.8 * low[i];
        y[i] = y[i] * vel;
    }
    return y;
}

Signal wood_thock(voice::Generator& rng, double f0, std::uint64_t seed) {
    const std::size_t n = secs(0.35);
    Signal y = modes(rng, n, {f0, f0 * 2.03, f0 * 3.4, f0 * 5.6}, {1, .45, .2, .08}, {.09, .05, .025, .012});
    const Signal b = burst(n, 300, 3000, 0.004, seed);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] += 0.4 * b[i];
    }
    return y;
}

Stereo clip() {
    voice::Generator rng(clip_start);
    Signal y(secs(0.32), 0.0);
    const Signal a = modes(rng, secs(.2), {2950, 4720, 6910, 8840}, {1, .7, .45, .25}, {.028, .018, .011, .007});
    place(y, mixed(1, a, 1, burst(secs(.2), 3000, 12000, .0012, 1)), 0.0);
    const Signal b = modes(rng, secs(.15), {3320, 5600, 7700}, {1, .5, .3}, {.02, .012, .007});
    place(y, mixed(0.55, b, 0.5, burst(secs(.15), 3000, 12000, .001, 2)), 0.045);
    place(y, times(0.5, modes(rng, secs(.12), {175, 410}, {1, .4}, {.035, .02})), 0.0);
    return finish(roomy(y, 0.12), -5);
}

Stereo unclip() {
    voice::Generator rng(unclip_start);
    Signal y(secs(0.45), 0.0);
    const Signal a = modes(rng, secs(.2), {2700, 4400, 6300}, {1, .6, .3}, {.022, .014, .008});
    place(y, mixed(0.8, a, 0.6, burst(secs(.2), 3000, 11000, .001, 3)), 0.0);
    place(y, times(0.6, burst(secs(.12), 250, 1800, .025, 4)), 0.03);
    place(y, times(0.35, modes(rng, secs(.3), {3900, 5150, 6600}, {1, .8, .5}, {.06, .045, .03})), 0.07);
    return finish(roomy(y, 0.14), -6);
}

template <int Variant>
Stereo knock() {
    voice::Generator rng(knock_starts[Variant]);
    voice::Generator r(100 + Variant);
    static constexpr double pitches[3] = {1250, 1520, 1080};
    const double f0 = pitches[Variant];
    Signal y = stone_knock(rng, f0, 1.0, 200 + Variant);
    if (Variant == 2) {
        Signal y2(secs(.25), 0.0);
        place(y2, y, 0);
        const double f = f0 * r.uniform(.97, 1.03);
        place(y2, times(0.45, stone_knock(rng, f, 1, 300)), 0.075);
        y = std::move(y2);
    }
    return finish(roomy(y, 0.1, 0.6, 0.35), -4);
}

Stereo tumble() {
    voice::Generator rng(tumble_start);
    const std::size_t n = secs(2.2);
    Signal y(n, 0.0);
    double t = 0.0;
    std::uint64_t k = 0;
    while (t < 1.7) {
        const double vel = (0.5 + 0.5 * std::sin(voice::pi * std::min(1.0, t / 1.2))) * rng.uniform(.4, 1);
        const double f = rng.uniform(800, 1700);
        place(y, times(vel, stone_knock(rng, f, 1, 400 + k)), t);
        if (rng.random() < .35) {
            const double scale = 0.4 * vel;
            const double low = rng.uniform(90, 150);
            place(y, times(scale, modes(rng, secs(.2), {low}, {1}, {.05})), t);
        }
        t += rng.uniform(.03, .16) * (1 + t);
        ++k;
    }
    const Signal rumble = voice::fft_filter(normals(rng, n), 30, 260, 2);
    const Signal tt = voice::tarr(n);
    const double rumble_top = voice::peak(rumble);
    const double y_top = voice::peak(y);
    for (std::size_t i = 0; i < n; ++i) {
        const double env = clip01(tt[i] / 0.15) * std::exp(-tt[i] / 0.7);
        y[i] += 0.6 * rumble[i] / rumble_top * env * y_top * 0.5;
    }
    for (int g = 0; g < 90; ++g) {
        const double at = rng.uniform(0.1, 1.9);
        const double f = rng.uniform(3500, 8000);
        place(y, times(0.08, modes(rng, secs(.02), {f}, {1}, {.002})), at);
    }
    return finish(roomy(y, 0.2, 1.0, 0.5), -4);
}

Stereo whoosh() {
    voice::Generator rng(whoosh_start);
    const std::size_t n = secs(0.9);
    const Signal t = voice::tarr(n);
    const Signal x = normals(rng, n);
    Signal y(n, 0.0);
    static constexpr double centres[5] = {350, 550, 850, 1300, 1900};
    Signal pos(n);
    for (std::size_t i = 0; i < n; ++i) {
        pos[i] = std::sin(voice::pi * clip01(t[i] / 0.8));
    }
    for (int i = 0; i < 5; ++i) {
        const double c = centres[i];
        const Signal band = voice::fft_filter(x, c * .75, c * 1.33, 2);
        for (std::size_t j = 0; j < n; ++j) {
            const double d = pos[j] * (5 - 1) - i;
            const double w = std::exp(-(d * d) / 0.8);
            y[j] += band[j] * w;
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        const double env = std::pow(std::sin(voice::pi * clip01(t[i] / 0.85)), 1.5);
        y[i] = y[i] * env;
    }
    return finish(roomy(y, 0.15), -8);
}

Stereo land() {
    voice::Generator rng(land_start);
    Signal y(secs(0.5), 0.0);
    place(y, wood_thock(rng, 196, 7), 0);
    place(y, times(0.6, stone_knock(rng, 1180, 1, 8)), 0.0);
    place(y, times(0.25, stone_knock(rng, 1400, 1, 9)), 0.09);
    return finish(roomy(y, 0.14, 0.8, 0.45), -5);
}

Stereo reset() {
    voice::Generator rng(reset_start);
    Signal y(secs(2.4), 0.0);
    double t = 0.02;
    std::uint64_t k = 0;
    while (t < 1.9) {
        const double density = std::exp(-std::pow((t - 0.45) / 0.55, 2.0));
        const double vel = rng.uniform(.25, 1) * (0.4 + 0.6 * density);
        const double f = rng.uniform(850, 1800);
        place(y, times(vel, stone_knock(rng, f, 1, 600 + k)), t);
        if (rng.random() < 0.3) {
            const double scale = 0.5 * vel;
            const double f_wood = rng.uniform(170, 230);
            place(y, times(scale, wood_thock(rng, f_wood, 700 + k)), t);
        }
        t += rng.uniform(.012, .05) / (0.25 + density);
        ++k;
    }
    return finish(roomy(y, 0.18, 1.0, 0.5), -5);
}

Signal click() {
    voice::Generator rng(click_start);
    const std::size_t n = secs(0.08);
    const Signal a = modes(rng, n, {1750, 2900, 4300}, {1, .4, .15}, {.009, .005, .003});
    const Signal b = modes(rng, n, {420}, {1}, {.012});
    Signal y = mixed(1, a, 0.6, b);
    const Signal c = burst(n, 2000, 9000, .0008, 12);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] += 0.3 * c[i];
    }
    return finish(y, -10);
}

Stereo relay() {
    voice::Generator rng(relay_start);
    Signal y(secs(0.3), 0.0);
    const Signal a = modes(rng, secs(.08), {3400, 1250, 5600}, {1, .6, .3}, {.01, .014, .005});
    place(y, mixed(1, a, 0.6, burst(secs(.08), 2500, 10000, .001, 13)), 0);
    place(y, times(0.6, modes(rng, secs(.08), {3000, 1100}, {1, .5}, {.008, .012})), 0.014);
    const Signal t = voice::tarr(secs(.22));
    Signal f(t.size());
    for (std::size_t i = 0; i < t.size(); ++i) {
        f[i] = 180 + 260 * clip01(t[i] / .2);
    }
    const Signal ph = phase_of(f);
    Signal spin(t.size());
    for (std::size_t i = 0; i < t.size(); ++i) {
        spin[i] = 0.18 * std::sin(ph[i]) * std::sin(voice::pi * clip01(t[i] / .22));
    }
    place(y, spin, 0.03);
    return finish(roomy(y, 0.08), -9);
}

Stereo clunk() {
    voice::Generator rng(clunk_start);
    Signal y(secs(0.9), 0.0);
    const Signal a = modes(rng, secs(.4), {92, 238, 410, 1300}, {1, .6, .3, .2}, {.12, .06, .03, .01});
    place(y, mixed(1, a, 0.5, burst(secs(.4), 200, 3000, .006, 14)), 0);
    const Signal t = voice::tarr(secs(.7));
    Signal f(t.size());
    for (std::size_t i = 0; i < t.size(); ++i) {
        f[i] = 620 - 280 * clip01(t[i] / .6);
    }
    const Signal ph = phase_of(f);
    Signal sag(t.size());
    for (std::size_t i = 0; i < t.size(); ++i) {
        sag[i] = (std::sin(ph[i]) + .5 * std::sin(2 * ph[i]) + .2 * std::sin(3 * ph[i])) * std::exp(-t[i] / .3) * 0.22;
    }
    place(y, sag, 0.03);
    return finish(roomy(y, 0.12), -6);
}

Stereo best() {
    voice::Generator rng(best_start);
    const std::size_t n = secs(3.2);
    Signal left(n, 0.0);
    Signal right(n, 0.0);
    static constexpr int notes[4] = {74, 78, 81, 86};
    for (int i = 0; i < 4; ++i) {
        const double f = voice::mtof(notes[i]);
        Signal y = modes(rng, secs(2.4), {f, f * 2.76, f * 5.40, f * 8.93}, {1, .38, .16, .06}, {.55, .22, .08, .03});
        const Signal b = burst(secs(2.4), 2000, 9000, .0015, static_cast<std::uint64_t>(20 + i));
        for (std::size_t j = 0; j < y.size(); ++j) {
            y[j] += 0.25 * b[j];
        }
        const double p = -0.5 + i / 3.0;
        place(left, times(std::cos((p + 1) * voice::pi / 4), y), 0.11 * i);
        place(right, times(std::sin((p + 1) * voice::pi / 4), y), 0.11 * i);
    }
    static constexpr int high[2] = {93, 98};
    for (int i = 0; i < 2; ++i) {
        const double f = voice::mtof(high[i]);
        const Signal y = times(0.25, modes(rng, secs(1.6), {f, f * 5.9}, {1, .1}, {.5, .05}));
        place(left, times(i == 0 ? 0.8 : 0.4, y), 0.5 + 0.16 * i);
        place(right, times(i == 0 ? 0.4 : 0.8, y), 0.5 + 0.16 * i);
    }
    const Stereo x{std::move(left), std::move(right)};
    const Stereo ir = voice::make_ir(2.2, 1.8, 1.4, 0.7, 0.02, 21);
    Stereo wet = voice::conv_reverb(x, ir, false);
    wet.left.resize(n);
    wet.right.resize(n);
    const double wet_top = peak(wet);
    const double x_top = peak(x);
    Stereo out{Signal(n), Signal(n)};
    for (std::size_t i = 0; i < n; ++i) {
        out.left[i] = x.left[i] + 0.35 * wet.left[i] / wet_top * x_top;
        out.right[i] = x.right[i] + 0.35 * wet.right[i] / wet_top * x_top;
    }
    return finish(out, -4);
}

Stereo squeak() {
    Signal y(secs(0.6), 0.0);
    static constexpr double calls[2][4] = {{0.0, 0.17, 1050, 1420}, {0.24, 0.20, 1120, 1520}};
    for (int k = 0; k < 2; ++k) {
        const double at = calls[k][0];
        const double length = calls[k][1];
        const double f0 = calls[k][2];
        const double f1 = calls[k][3];
        const std::size_t m = secs(length);
        const Signal t = voice::tarr(m);
        Signal f(m);
        for (std::size_t i = 0; i < m; ++i) {
            f[i] = f0 + (f1 - f0) * std::sin(voice::pi / 2 * clip01(t[i] / (length * 0.6)));
            f[i] = f[i] * (1 + 0.012 * std::sin(two_pi * 31 * t[i]));
        }
        const Signal ph = phase_of(f);
        Signal tone(m, 0.0);
        for (int h = 1; h < 9; ++h) {
            const double a = std::pow(0.85, h);
            for (std::size_t i = 0; i < m; ++i) {
                tone[i] += a * std::sin(h * ph[i]);
            }
        }
        const Signal b1 = voice::fft_filter(tone, 700, 3600, 2);
        const Signal b2 = voice::fft_filter(tone, 2400, 3200, 4);
        voice::Generator r(static_cast<std::uint64_t>(30 + k));
        const Signal air = voice::fft_filter(normals(r, m), 3000, 9000, 2);
        Signal part(m);
        for (std::size_t i = 0; i < m; ++i) {
            const double body = b1[i] + 0.3 * b2[i];
            const double env = clip01(t[i] / 0.015) * clip01((length - t[i]) / 0.03);
            part[i] = (body + 0.15 * air[i]) * env;
        }
        place(y, part, at);
    }
    return finish(roomy(y, 0.1), -7);
}

template <int Kind>
Stereo bird() {
    Signal y{};
    if (Kind == 0) {  // tee-oo, tee-oo: a falling two-note whistle
        y.assign(secs(1.6), 0.0);
        static constexpr double notes[2][3] = {{0.16, 3600, 3450}, {0.24, 2950, 2700}};
        for (const double at : {0.0, 0.62}) {
            for (int j = 0; j < 2; ++j) {
                const double length = notes[j][0];
                const double fa = notes[j][1];
                const double fb = notes[j][2];
                const std::size_t m = secs(length);
                const Signal t = voice::tarr(m);
                Signal f(m);
                for (std::size_t i = 0; i < m; ++i) {
                    f[i] = fa + (fb - fa) * t[i] / length;
                }
                const Signal ph = phase_of(f);
                Signal part(m);
                for (std::size_t i = 0; i < m; ++i) {
                    const double env = std::pow(std::sin(voice::pi * clip01(t[i] / length)), 1.5);
                    part[i] = (std::sin(ph[i]) + 0.08 * std::sin(2 * ph[i])) * env;
                }
                place(y, part, at + 0.2 * j);
            }
        }
    } else if (Kind == 1) {  // a quick trill
        y.assign(secs(1.2), 0.0);
        for (int i = 0; i < 11; ++i) {
            const std::size_t m = secs(0.045);
            const Signal t = voice::tarr(m);
            Signal f(m);
            for (std::size_t j = 0; j < m; ++j) {
                f[j] = 4300 + 900 * (t[j] / 0.045) - 40 * i;
            }
            const Signal ph = phase_of(f);
            const double swell = 0.6 + 0.4 * std::sin(voice::pi * i / 10);
            Signal part(m);
            for (std::size_t j = 0; j < m; ++j) {
                const double s = std::sin(voice::pi * t[j] / 0.045);
                part[j] = std::sin(ph[j]) * (s * s) * swell;
            }
            place(y, part, 0.07 * i);
        }
    } else {  // a warble: up, wavering, down
        const std::size_t m = secs(0.8);
        const Signal t = voice::tarr(m);
        Signal f(m);
        for (std::size_t i = 0; i < m; ++i) {
            f[i] = 2600 + 900 * std::sin(voice::pi * t[i] / 0.8) + 260 * std::sin(two_pi * 14 * t[i]);
        }
        const Signal ph = phase_of(f);
        y.assign(secs(1.3), 0.0);
        Signal part(m);
        for (std::size_t i = 0; i < m; ++i) {
            const double s = std::sin(voice::pi * t[i] / 0.8);
            part[i] = std::sin(ph[i]) * (s * s);
        }
        place(y, part, 0.05);
    }
    y = voice::fft_filter(y, 1500, 9000, 1);
    return finish(roomy(y, 0.35, 1.4, 0.9), -6);
}

// ---------------------------------------------------------------- beds

// The crane's little electric motor, a seamless 2-second loop (the game varies its rate).
Stereo motor() {
    const std::size_t L = secs(2.0);
    const Signal t = voice::tarr(L);
    const double f1 = 280.0;
    Signal y(L);
    voice::Generator rnd(50);
    Signal brush = voice::fft_filter(normals(rnd, L), 2500, 7000, 2, 4096, true);
    for (std::size_t i = 0; i < L; ++i) {
        const double c = 0.5 + 0.5 * std::cos(two_pi * 140.0 * t[i]);
        brush[i] *= std::pow(c, 4.0);
    }
    const double brush_top = voice::peak(brush);
    for (std::size_t i = 0; i < L; ++i) {
        const double ph = two_pi * f1 * t[i] - f1 * 0.004 * std::cos(two_pi * 8.0 * t[i]) / 8.0;
        double tone = 0.55 * std::sin(ph) + 1.0 * std::sin(2 * ph + 1.0) + 0.32 * std::sin(3 * ph + 2.0) +
                      0.22 * std::sin(4 * ph + 0.4) + 0.10 * std::sin(6 * ph + 1.7) + 0.05 * std::sin(9 * ph + 0.3);
        tone *= 1 + 0.12 * std::sin(two_pi * 140.0 * t[i]);
        const double gears = 0.07 * std::sin(two_pi * 1690.0 * t[i]) * (1 + 0.5 * std::sin(two_pi * 56.0 * t[i]));
        const double hum = 0.25 * std::sin(two_pi * 70.0 * t[i]);
        y[i] = tone + gears + hum + 0.10 * brush[i] / brush_top * 3;
    }
    y = voice::fft_filter(y, 50, 4200, 2, 4096, true);
    Stereo stereo{y, Signal(L)};
    for (std::size_t i = 0; i < L; ++i) {
        stereo.right[i] = y[(i + L - 37) % L];  // np.roll(y, 37)
    }
    return level_bed(std::move(stereo));
}

// np.fft.rfftfreq(L, 1 / SR)
Signal rfft_frequencies(std::size_t length) {
    const double unit = 1.0 / (static_cast<double>(length) * (1.0 / sr));
    Signal f(length / 2 + 1);
    for (std::size_t k = 0; k < f.size(); ++k) {
        f[k] = static_cast<double>(k) * unit;
    }
    return f;
}

// np.fft.irfft(np.fft.rfft(x) * shape, L) / max|.|
Signal shaped_noise(voice::Generator& rnd, std::size_t length, const Signal& shape) {
    const Signal x = normals(rnd, length);
    std::vector<voice::Complex> spectrum{};
    voice::rfft(x, length, spectrum);
    for (std::size_t k = 0; k < spectrum.size(); ++k) {
        spectrum[k] = voice::Complex(spectrum[k].real() * shape[k], spectrum[k].imag() * shape[k]);
    }
    Signal y{};
    voice::irfft(spectrum, length, y);
    const double top = voice::peak(y);
    for (double& v : y) {
        v /= top;
    }
    return y;
}

// The brook, a seamless 40-second loop: a wash of water and thousands of ringing bubbles.
Stereo brook() {
    const double loop = 40.0;
    const std::size_t L = secs(loop);
    voice::LoopMix mix(L, true);
    voice::Generator rnd(77);
    const Signal t = voice::tarr(L);
    const Signal f = rfft_frequencies(L);
    Signal shape(f.size());
    for (std::size_t k = 0; k < f.size(); ++k) {
        shape[k] = 1 / std::sqrt(std::max(f[k], 20.0)) / std::sqrt(1 + std::pow(f[k] / 1800, 4.0)) /
                   std::sqrt(1 + std::pow(160 / std::max(f[k], 1.0), 4.0));
    }
    for (int ch = 0; ch < 2; ++ch) {
        const double pan = ch == 0 ? -0.6 : 0.6;
        Signal wash = shaped_noise(rnd, L, shape);
        for (std::size_t i = 0; i < L; ++i) {
            const double swell = 1 + 0.22 * std::sin(two_pi * 3 * t[i] / loop + ch) +
                                 0.15 * std::sin(two_pi * 11 * t[i] / loop + 2 * ch) +
                                 0.1 * std::sin(two_pi * 29 * t[i] / loop + 1);
            wash[i] = wash[i] * swell;
        }
        mix.add("wash", wash, 0, pan, 0.12);
    }
    // the babble's pace: busy runs and quieter spells
    Signal pace(L, 0.0);
    for (int k = 1; k < 40; ++k) {
        const double a = rnd.uniform(0, 1) / std::pow(static_cast<double>(k), 0.7);
        const double w = two_pi * k;
        const double phase = rnd.uniform(0, 6.28);
        for (std::size_t i = 0; i < L; ++i) {
            pace[i] += a * std::sin(w * t[i] / loop + phase);
        }
    }
    const double low = *std::min_element(pace.begin(), pace.end());
    const double high = *std::max_element(pace.begin(), pace.end());
    for (double& v : pace) {
        v = (v - low) / (high - low);
        v = 0.25 + 0.75 * std::pow(v, 1.5);
    }
    double time = 0.0;
    const double log_low = std::log(420.0);
    const double log_high = std::log(2600.0);
    while (time < loop) {
        const double rate = 120 * pace[std::min(L - 1, secs(time))];
        time += rnd.exponential(1 / rate);
        const double f0 = std::exp(rnd.uniform(log_low, log_high));
        const double q = rnd.uniform(12, 30);
        const double tau = q / (voice::pi * f0);
        const std::size_t m = secs(std::min(0.12, 5 * tau));
        const Signal tt = voice::tarr(m);
        const double rise = rnd.uniform(0.04, 0.16);
        Signal fr(m);
        for (std::size_t i = 0; i < m; ++i) {
            fr[i] = f0 * (1 + rise * tt[i] / tau);
        }
        const Signal ph = phase_of(fr);
        const double amp = rnd.uniform(0.2, 1.0) * std::pow(f0 / 1000, -0.4);
        Signal bubble(m);
        for (std::size_t i = 0; i < m; ++i) {
            bubble[i] = amp * std::sin(ph[i]) * std::exp(-tt[i] / tau) * clip01(tt[i] / 0.0006);
        }
        const long long start = static_cast<long long>(secs(time));
        mix.add("babble", bubble, start, rnd.uniform(-0.75, 0.75), 0.13);
    }
    // the odd deep gulp, in twos and threes
    time = 1.0;
    while (time < loop) {
        const std::int64_t count = rnd.integers(2, 4);
        for (std::int64_t j = 0; j < count; ++j) {
            const double f0 = rnd.uniform(160, 340);
            const double tau = rnd.uniform(18, 30) / (voice::pi * f0);
            const std::size_t m = secs(6 * tau);
            const Signal tt = voice::tarr(m);
            Signal fr(m);
            for (std::size_t i = 0; i < m; ++i) {
                fr[i] = f0 * (1 + 0.1 * tt[i] / tau);
            }
            const Signal ph = phase_of(fr);
            Signal gulp(m);
            for (std::size_t i = 0; i < m; ++i) {
                gulp[i] = std::sin(ph[i]) * std::exp(-tt[i] / tau) * clip01(tt[i] / .002);
            }
            const long long start = static_cast<long long>(secs(time + 0.07 * static_cast<double>(j)));
            mix.add("babble", gulp, start, rnd.uniform(-.4, .4), 0.16);
        }
        time += rnd.uniform(2.0, 5.0);
    }
    const Stereo& wash = mix.bus("wash");
    const Stereo& babble = mix.bus("babble");
    Stereo x{Signal(L), Signal(L)};
    for (std::size_t i = 0; i < L; ++i) {
        x.left[i] = wash.left[i] + babble.left[i];
        x.right[i] = wash.right[i] + babble.right[i];
    }
    x.left = voice::fft_filter(x.left, 70, 0, 2, 4096, true);
    x.right = voice::fft_filter(x.right, 70, 0, 2, 4096, true);
    const Stereo ir = voice::make_ir(1.2, 0.9, 0.7, 0.35, 0.01, 31);
    const Stereo wet = voice::conv_reverb(x, ir, true);
    const double x_top = peak(x);
    const double wet_top = peak(wet);
    for (std::size_t i = 0; i < L; ++i) {
        x.left[i] = x.left[i] + 0.25 * wet.left[i] * x_top / wet_top;
        x.right[i] = x.right[i] + 0.25 * wet.right[i] * x_top / wet_top;
    }
    return level_bed(std::move(x));
}

// The stream's steady rush, a seamless 37-second loop.
Stereo rush() {
    const double loop = 37.0;
    const std::size_t L = secs(loop);
    const Signal t = voice::tarr(L);
    voice::Generator rnd(91);
    Stereo out{Signal(L), Signal(L)};
    const Signal f = rfft_frequencies(L);
    Signal shape(f.size());
    for (std::size_t k = 0; k < f.size(); ++k) {
        shape[k] = 1 / std::pow(std::max(f[k], 30.0), 0.35) / std::sqrt(1 + std::pow(f[k] / 2600, 4.0)) /
                   std::sqrt(1 + std::pow(110 / std::max(f[k], 1.0), 4.0));
    }
    for (int ch = 0; ch < 2; ++ch) {
        const Signal body = shaped_noise(rnd, L, shape);
        Signal swell(L, 1.0);
        for (const int k : {2, 5, 9, 17, 31}) {
            const double a = rnd.uniform(0.04, 0.12);
            const double phase = rnd.uniform(0, 6.28);
            const double w = two_pi * k;
            for (std::size_t i = 0; i < L; ++i) {
                swell[i] += a * std::sin(w * t[i] / loop + phase);
            }
        }
        Signal& channel = ch == 0 ? out.left : out.right;
        for (std::size_t i = 0; i < L; ++i) {
            channel[i] = body[i] * swell[i];
        }
    }
    // gurgles: narrow bands that come and go
    for (int g = 0; g < 6; ++g) {
        const double centre = rnd.uniform(300, 1400);
        Signal band = voice::fft_filter(normals(rnd, L), centre * 0.85, centre * 1.18, 3, 4096, true);
        const double top = voice::peak(band);
        for (double& v : band) {
            v /= top;
        }
        const double k = static_cast<double>(rnd.integers(3, 12));
        const double phase = rnd.uniform(0, 6.28);
        const double pan = rnd.uniform(-0.7, 0.7);
        const double gl = std::cos((pan + 1) * voice::pi / 4);
        const double gr = std::sin((pan + 1) * voice::pi / 4);
        const double w = two_pi * k;
        for (std::size_t i = 0; i < L; ++i) {
            const double env = std::pow(std::max(0.0, std::sin(w * t[i] / loop + phase)), 3.0);
            out.left[i] += 0.35 * band[i] * env * gl;
            out.right[i] += 0.35 * band[i] * env * gr;
        }
    }
    return level_bed(std::move(out));
}

// Wind in the reeds and grass, a seamless 53-second loop.
Stereo reeds() {
    voice::Generator rng(reeds_start);
    const double loop = 53.0;
    const std::size_t L = secs(loop);
    const Signal t = voice::tarr(L);
    voice::Generator rnd(92);
    Stereo out{Signal(L), Signal(L)};
    Signal gust(L, 0.0);
    for (const int k : {1, 2, 3, 5, 8, 13}) {
        const double a = rnd.uniform(0.3, 1.0) / k;
        const double phase = rnd.uniform(0, 6.28);
        const double w = two_pi * k;
        for (std::size_t i = 0; i < L; ++i) {
            gust[i] += a * std::sin(w * t[i] / loop + phase);
        }
    }
    const double low = *std::min_element(gust.begin(), gust.end());
    const double high = *std::max_element(gust.begin(), gust.end());
    for (double& v : gust) {
        v = (v - low) / (high - low);
        v = 0.15 + 0.85 * (v * v);
    }
    for (int ch = 0; ch < 2; ++ch) {
        const Signal hiss = voice::fft_filter(normals(rnd, L), 1400, 7500, 2, 4096, true);
        const Signal flutter = voice::fft_filter(normals(rnd, L), 2, 14, 1, 4096, true);
        const double top = voice::peak(hiss);
        Signal& channel = ch == 0 ? out.left : out.right;
        for (std::size_t i = 0; i < L; ++i) {
            const double fl = 1 + 0.4 * flutter[i] * 8;
            channel[i] = hiss[i] / top * gust[i] * std::min(std::max(fl, 0.2), 2.0);
        }
    }
    voice::LoopMix mix(L, true);
    for (int j = 0; j < 140; ++j) {
        const double at = rnd.uniform(0, loop);
        const double g = gust[std::min(L - 1, secs(at))];
        const double f = rnd.uniform(2500, 6000);
        const Signal tick = modes(rng, secs(0.02), {f}, {1}, {0.002});
        mix.add("ticks", times(g, tick), static_cast<long long>(secs(at)), rnd.uniform(-0.9, 0.9), 0.25);
    }
    const Stereo& ticks = mix.bus("ticks");
    for (std::size_t i = 0; i < L; ++i) {
        out.left[i] = out.left[i] + ticks.left[i];
        out.right[i] = out.right[i] + ticks.right[i];
    }
    return level_bed(std::move(out));
}

// ---------------------------------------------------------------- the creek's effects

template <int Variant>
Stereo splash() {
    voice::Generator rnd(300 + Variant);
    Signal y(secs(0.6), 0.0);
    const double tau_slap = 0.018 + 0.01 * rnd.random();
    place(y, burst(secs(0.12), 500 + 300 * Variant, 4500, tau_slap, 310 + Variant), 0.0);
    const std::int64_t bubbles = rnd.integers(4, 10);
    const double log_low = std::log(500.0);
    const double log_high = std::log(2600.0);
    for (std::int64_t b = 0; b < bubbles; ++b) {
        const double f0 = std::exp(rnd.uniform(log_low, log_high));
        const double tau = rnd.uniform(14, 26) / (voice::pi * f0);
        const std::size_t m = secs(std::min(0.1, 6 * tau));
        const Signal tt = voice::tarr(m);
        const double rise = rnd.uniform(0.05, 0.14);
        Signal f(m);
        for (std::size_t i = 0; i < m; ++i) {
            f[i] = f0 * (1 + rise * tt[i] / tau);
        }
        const Signal ph = phase_of(f);
        const double amp = rnd.uniform(0.2, 0.7);
        Signal bubble(m);
        for (std::size_t i = 0; i < m; ++i) {
            bubble[i] = amp * std::sin(ph[i]) * std::exp(-tt[i] / tau) * clip01(tt[i] / 0.0005);
        }
        place(y, bubble, rnd.uniform(0.01, 0.3));
    }
    if (Variant == 3) {  // a plop: a deeper bubble, something heavier dropped in
        const double f0 = 340.0;
        const double tau = 22 / (voice::pi * f0);
        const std::size_t m = secs(6 * tau);
        const Signal tt = voice::tarr(m);
        Signal f(m);
        for (std::size_t i = 0; i < m; ++i) {
            f[i] = f0 * (1 + 0.12 * tt[i] / tau);
        }
        const Signal ph = phase_of(f);
        Signal plop(m);
        for (std::size_t i = 0; i < m; ++i) {
            plop[i] = 1.2 * std::sin(ph[i]) * std::exp(-tt[i] / tau);
        }
        place(y, plop, 0.0);
    }
    return finish(roomy(y, 0.2, 0.8, 0.4), -5);
}

template <int Variant>
Stereo bed() {
    voice::Generator rng(bed_starts[Variant]);
    voice::Generator rnd(400 + Variant);
    Signal y(secs(0.9), 0.0);
    double at = 0.0;
    const std::int64_t count = rnd.integers(2, 5);
    for (std::int64_t k = 0; k < count; ++k) {
        const double f = rnd.uniform(900, 1600);
        const double vel = rnd.uniform(0.4, 1.0);
        place(y, stone_knock(rng, f, vel, static_cast<std::uint64_t>(410 + Variant * 7 + k)), at);
        at += rnd.uniform(0.05, 0.22);
    }
    y = voice::fft_filter(y, 120, 1300, 2);
    return finish(roomy(y, 0.15, 0.7, 0.35), -6);
}

template <int Kind>
Stereo frog() {
    Signal y{};
    if (Kind == 0) {  // a two-note ribbit
        y.assign(secs(0.6), 0.0);
        static constexpr double croaks[2][2] = {{0.11, 420.0}, {0.13, 470.0}};
        for (int j = 0; j < 2; ++j) {
            const double length = croaks[j][0];
            const double f0 = croaks[j][1];
            const std::size_t m = secs(length);
            const Signal t = voice::tarr(m);
            Signal f(m);
            for (std::size_t i = 0; i < m; ++i) {
                f[i] = f0 * (1 - 0.08 * t[i] / length);
            }
            const Signal ph = phase_of(f);
            Signal tone(m, 0.0);
            for (int h = 1; h < 10; ++h) {
                const double a = std::pow(0.75, h);
                for (std::size_t i = 0; i < m; ++i) {
                    tone[i] += a * std::sin(h * ph[i]);
                }
            }
            const Signal b1 = voice::fft_filter(tone, 600, 2600, 2);
            const Signal b2 = voice::fft_filter(tone, 1100, 1500, 3);
            Signal part(m);
            for (std::size_t i = 0; i < m; ++i) {
                const double pulses = std::pow(0.5 + 0.5 * std::sin(two_pi * 70 * t[i]), 3.0);
                const double env = std::sin(voice::pi * clip01(t[i] / length));
                part[i] = (b1[i] + 0.5 * b2[i]) * pulses * env;
            }
            place(y, part, 0.17 * j);
        }
    } else if (Kind == 1) {  // a green frog's twanging gunk
        y.assign(secs(0.6), 0.0);
        const std::size_t m = secs(0.35);
        const Signal t = voice::tarr(m);
        Signal f(m);
        for (std::size_t i = 0; i < m; ++i) {
            f[i] = 175 * (1 - 0.06 * t[i] / 0.35);
        }
        const Signal ph = phase_of(f);
        Signal tone(m, 0.0);
        for (int h = 1; h < 12; ++h) {
            const double a = std::pow(0.7, h);
            for (std::size_t i = 0; i < m; ++i) {
                tone[i] += a * std::sin(h * ph[i] + h);
            }
        }
        for (std::size_t i = 0; i < m; ++i) {
            tone[i] = tone[i] * std::exp(-t[i] / 0.09);
        }
        place(y, mixed(1, voice::fft_filter(tone, 150, 1800, 2), 0.6, voice::fft_filter(tone, 520, 720, 3)), 0.0);
    } else {  // a deep croaking rattle
        y.assign(secs(0.9), 0.0);
        const std::size_t m = secs(0.55);
        const Signal t = voice::tarr(m);
        Signal f(m);
        for (std::size_t i = 0; i < m; ++i) {
            f[i] = 118 * (1 + 0.04 * std::sin(voice::pi * t[i] / 0.55));
        }
        const Signal ph = phase_of(f);
        Signal tone(m, 0.0);
        for (int h = 1; h < 14; ++h) {
            const double a = std::pow(0.8, h);
            for (std::size_t i = 0; i < m; ++i) {
                tone[i] += a * std::sin(h * ph[i]);
            }
        }
        const Signal b1 = voice::fft_filter(tone, 90, 1200, 2);
        const Signal b2 = voice::fft_filter(tone, 280, 380, 3);
        Signal part(m);
        for (std::size_t i = 0; i < m; ++i) {
            const double pulses = std::pow(0.5 + 0.5 * std::sin(two_pi * 28 * t[i]), 6.0);
            part[i] = (b1[i] + 0.5 * b2[i]) * pulses * std::sqrt(std::sin(voice::pi * clip01(t[i] / 0.55)));
        }
        place(y, part, 0.0);
    }
    return finish(roomy(y, 0.3, 1.2, 0.7), -6);
}

// ---------------------------------------------------------------- "Stone Garden"

constexpr double beat = 1.0;
constexpr double bar_length = 4 * beat;
constexpr int bars = 16;
constexpr double garden_loop = bars * bar_length;

struct Chord {
    int bass;
    int upper[4];
};
constexpr Chord chords[bars] = {
    {38, {57, 61, 64, 66}}, {38, {57, 61, 64, 66}}, {35, {54, 57, 61, 62}}, {35, {54, 57, 61, 62}},
    {31, {54, 57, 59, 62}}, {31, {54, 57, 61, 62}}, {33, {57, 62, 64, 69}}, {33, {57, 59, 61, 64}},
    {42, {57, 61, 64, 69}}, {43, {54, 59, 62, 66}}, {40, {55, 59, 62, 66}}, {33, {57, 62, 64, 67}},
    {35, {54, 57, 61, 64}}, {31, {54, 57, 59, 64}}, {40, {55, 59, 62, 66}}, {33, {57, 62, 64, 69}},
};
constexpr int scale[11] = {62, 64, 66, 69, 71, 74, 76, 78, 81, 83, 86};

// stone_bar(m, vel): a struck slate bar
Signal stone_bar(voice::Generator& rng, double m, double vel) {
    const double f = voice::mtof(m);
    const std::size_t n = secs(2.6);
    const Signal t = voice::tarr(n);
    Signal y(n, 0.0);
    const double rows[4][3] = {{1.0, 1.0, 1.1}, {2.76, 0.32 * vel, 0.32}, {5.40, 0.14 * vel, 0.10}, {8.93, 0.05 * vel, 0.04}};
    for (const double* row : rows) {
        const double fr = f * row[0];
        if (fr < 16000) {
            const double a = row[1];
            const double tau = row[2];
            const double c = 1 + 0.15 * (f / 600);
            const double ph = rng.uniform(0, 6.28);
            const double w = two_pi * fr;
            for (std::size_t i = 0; i < n; ++i) {
                y[i] += a * std::exp(-t[i] / tau * c) * std::sin(w * t[i] + ph);
            }
        }
    }
    const std::size_t k = secs(0.003);
    const Signal tap = voice::fft_filter(normals(rng, k), 1500, 8000);
    const Signal down = voice::linspace(1, 0, k);
    for (std::size_t i = 0; i < k; ++i) {
        y[i] += 0.35 * vel * tap[i] * down[i];
    }
    const std::size_t att = secs(0.0015);
    const Signal up = voice::linspace(0, 1, att);
    for (std::size_t i = 0; i < att; ++i) {
        y[i] *= up[i];
    }
    const double level = 0.35 + 0.65 * vel;
    for (double& v : y) {
        v = v * level;
    }
    return y;
}

bool in_chord(const int (&chord)[5], int pitch) {
    const bool result = std::find(std::begin(chord), std::end(chord), pitch % 12) != std::end(chord);
    return result;
}

struct Struck {
    double when;
    int m;
    double vel;
};

// tune(): two-bar phrases on the pentatonic, nearest chord tones on long notes
std::vector<Struck> tune(voice::Generator& rng) {
    using Cell = std::vector<std::pair<double, double>>;
    const Cell cells[5] = {
        {{0.0, 1.5}, {1.5, 0.5}, {2.0, 2.0}, {5.0, 1.0}},
        {{0.0, 1.0}, {1.0, 1.0}, {2.0, 3.0}, {6.0, 1.5}},
        {{0.5, 1.0}, {1.5, 2.5}, {4.5, 0.5}, {5.0, 2.0}},
        {{0.0, 3.0}, {4.0, 1.0}, {5.0, 1.0}, {6.0, 2.0}},
        {{0.0, 2.0}, {3.0, 1.0}, {4.0, 3.0}},
    };
    static constexpr int steps[7] = {-2, -1, -1, 1, 1, 2, 0};
    std::vector<Struck> notes{};
    int degree = 5;
    for (int phrase = 0; phrase < bars / 2; ++phrase) {
        if (phrase == 3 || phrase == 7) {
            notes.push_back({phrase * 2 * bar_length, phrase == 3 ? 74 : 76, 0.45});
            continue;
        }
        const Cell& cell = phrase != 0 && phrase != 4 ? cells[rng.integers(0, 5)] : cells[0];
        for (std::size_t k = 0; k < cell.size(); ++k) {
            const double at = cell[k].first;
            const double length = cell[k].second;
            const double when = phrase * 2 * bar_length + at * beat;
            const int bar = static_cast<int>(std::floor(when / bar_length)) % bars;
            int chord[5]{};
            for (int c = 0; c < 4; ++c) {
                chord[c] = chords[bar].upper[c] % 12;
            }
            chord[4] = chords[bar].bass % 12;
            const int step = steps[rng.choice(7)];
            degree = std::clamp(degree + step, 2, 11 - 2);
            if (length >= 2 && !in_chord(chord, scale[degree])) {
                for (const int d : {degree - 1, degree + 1}) {
                    if (0 <= d && d < 11 && in_chord(chord, scale[d])) {
                        degree = d;
                        break;
                    }
                }
            }
            const double vel = 0.42 + 0.2 * rng.random() - (k != 0 ? 0.08 : 0.0);
            notes.push_back({when, scale[degree], vel});
        }
        degree = std::clamp(degree + (phrase < 4 ? 1 : -1), 3, 8);
    }
    return notes;
}

long long at_sample(double seconds) {
    const long long result = static_cast<long long>(seconds * sr);  // int(x * SR), toward zero
    return result;
}

// "Stone Garden": one seamless 64-second loop
Stereo music() {
    voice::Synth synth;
    voice::Generator rng(garden_start);
    const std::size_t L = secs(garden_loop);
    voice::LoopMix mix(L, true);
    for (int bar = 0; bar < bars; ++bar) {
        const long long start = at_sample(bar * bar_length);
        for (int i = 0; i < 4; ++i) {
            const Signal y = synth.pad(chords[bar].upper[i], bar_length + 0.6, 0.32, 1.4, 2.2, 1500);
            mix.add("pad", y, start - at_sample(0.3), -0.5 + i / 3.0, 0.20);
        }
    }
    for (int bar = 0; bar < bars; ++bar) {
        const long long start = at_sample(bar * bar_length);
        const int bass = chords[bar].bass;
        mix.add("piano", synth.felt_piano(bass + 12, 3.6, 0.42), start, -0.1, 0.9);
        if (bar % 4 == 1) {
            mix.add("piano", synth.felt_piano(bass + 19, 1.8, 0.30), start + at_sample(2 * beat), 0.1, 0.7);
        }
        if (bar % 2 == 0) {
            for (int i = 0; i < 3; ++i) {
                mix.add("piano", synth.felt_piano(chords[bar].upper[i], 2.6, 0.26), start + at_sample(1.0 + 0.09 * i),
                        -0.25 + 0.25 * i, 0.55);
            }
        }
    }
    for (const Struck& s : tune(rng)) {
        const Signal y = stone_bar(rng, s.m, s.vel);
        const double pan = std::clamp((s.m - 76) / 14.0, -0.6, 0.6);
        mix.add("stone", y, at_sample(s.when), pan, 0.55);
    }
    for (int k = 0; k < 7; ++k) {
        const double when = rng.uniform(0, garden_loop);
        const int bar = static_cast<int>(std::floor(when / bar_length)) % bars;
        const int m = chords[bar].upper[rng.integers(0, 4)] + 24;
        const Signal y = synth.kalimba(m, 1.0, 0.35);
        mix.add("glint", y, at_sample(when), rng.uniform(-0.8, 0.8), 0.22);
    }
    const Stereo stone = mix.bus("stone");
    const Stereo echo = voice::pingpong(stone, at_sample(0.75), 0.32, 4, 3200);
    const Stereo& pad = mix.bus("pad");
    const Stereo& piano = mix.bus("piano");
    const Stereo& glint = mix.bus("glint");
    Stereo dry{Signal(L), Signal(L)};
    for (std::size_t i = 0; i < L; ++i) {
        dry.left[i] = pad.left[i] + piano.left[i] + stone.left[i] + 0.55 * echo.left[i] + glint.left[i];
        dry.right[i] = pad.right[i] + piano.right[i] + stone.right[i] + 0.55 * echo.right[i] + glint.right[i];
    }
    const Stereo ir = voice::make_ir(3.4, 2.8, 2.2, 1.0, 0.028, 64);
    const Stereo wet = voice::conv_reverb(dry, ir, true);
    const double dry_top = peak(dry);
    const double wet_top = peak(wet);
    Stereo out{Signal(L), Signal(L)};
    for (std::size_t i = 0; i < L; ++i) {
        out.left[i] = dry.left[i] + 0.42 * wet.left[i] * dry_top / wet_top;
        out.right[i] = dry.right[i] + 0.42 * wet.right[i] * dry_top / wet_top;
    }
    out.left = voice::fft_filter(out.left, 35, 9000, 1, 4096, true);
    out.right = voice::fft_filter(out.right, 35, 9000, 1, 4096, true);
    return level_bed(std::move(out));
}

constexpr voice::Cue cues[] = {
    {.name = "zc_clip", .stereo = clip},
    {.name = "zc_unclip", .stereo = unclip},
    {.name = "zc_knock_1", .stereo = knock<0>},
    {.name = "zc_knock_2", .stereo = knock<1>},
    {.name = "zc_knock_3", .stereo = knock<2>},
    {.name = "zc_tumble", .stereo = tumble},
    {.name = "zc_whoosh", .stereo = whoosh},
    {.name = "zc_land", .stereo = land},
    {.name = "zc_reset", .stereo = reset},
    {.name = "zc_click", .mono = click},
    {.name = "zc_relay", .stereo = relay},
    {.name = "zc_clunk", .stereo = clunk},
    {.name = "zc_best", .stereo = best},
    {.name = "zc_squeak", .stereo = squeak},
    {.name = "zc_bird_1", .stereo = bird<0>},
    {.name = "zc_bird_2", .stereo = bird<1>},
    {.name = "zc_bird_3", .stereo = bird<2>},
    {.name = "zc_motor", .stereo = motor},
    {.name = "zc_brook", .stereo = brook},
    {.name = "zc_rush", .stereo = rush},
    {.name = "zc_reeds", .stereo = reeds},
    {.name = "zc_splash_1", .stereo = splash<0>},
    {.name = "zc_splash_2", .stereo = splash<1>},
    {.name = "zc_splash_3", .stereo = splash<2>},
    {.name = "zc_splash_4", .stereo = splash<3>},
    {.name = "zc_bed_1", .stereo = bed<0>},
    {.name = "zc_bed_2", .stereo = bed<1>},
    {.name = "zc_bed_3", .stereo = bed<2>},
    {.name = "zc_frog_1", .stereo = frog<0>},
    {.name = "zc_frog_2", .stereo = frog<1>},
    {.name = "zc_frog_3", .stereo = frog<2>},
    {.name = "zc_music", .stereo = music},
};

}  // namespace

std::span<const voice::Cue> sound_cues() {
    const std::span<const voice::Cue> result(cues);
    return result;
}

void register_sound_cues() {
    voice::Bank::shared().add(cues);
}

}  // namespace zc
