#include "thieves_cues.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

// Each effect follows audio_src/make_sfx.py line for line, in numpy's order of
// operations. The states are what tools/numpy_reference.py (shared/voice) printed for
// that script: synth._rng (seeded 12345) is shared by every cue in script order, and
// the script's own rng (seeded 1999) roughens every animal voice: chirp_voice draws its
// rng.normal(0, 1, n) even when it multiplies it by zero.
namespace ct {
namespace {

using voice::Signal;

constexpr double two_pi = 2 * voice::pi;

constexpr voice::Word128 synth_increment{0xc9c7353e6e2b1f28ull, 0x7d761f2d4027fae7ull};
constexpr voice::Word128 script_increment{0x947316d6bb5ff62bull, 0xda3c616049569929ull};  // default_rng(1999)

constexpr voice::GeneratorState synth_at(std::uint64_t high, std::uint64_t low) {
    const voice::GeneratorState state{{high, low}, synth_increment, false, 0u};
    return state;
}

constexpr voice::GeneratorState script_at(std::uint64_t high, std::uint64_t low) {
    const voice::GeneratorState state{{high, low}, script_increment, false, 0u};
    return state;
}

constexpr voice::GeneratorState step_starts[3] = {
    synth_at(0x1905e0335aae9634ull, 0x9199b0d09775add5ull),  // as imported
    synth_at(0x766aedb121464448ull, 0xaba3d14f12182849ull), synth_at(0x4bc6dd458473ff51ull, 0xb0411396cf63f17dull)};
constexpr voice::GeneratorState push_starts[3] = {synth_at(0x6d5dc4950f6cf6c6ull, 0x5773ae05b2d0de02ull),
                                                  synth_at(0x182c8a3f572a059eull, 0xc815d839d41b71d4ull),
                                                  synth_at(0xcc2d01f5aba146f2ull, 0xa2a3990c06a5c6c7ull)};
constexpr voice::GeneratorState push_rng_starts[3] = {script_at(0x71813e2c256938acull, 0x3f6f492b5e9892eaull),  // fresh
                                                      script_at(0x6676240b0297b634ull, 0x04bebfb25e0e3461ull),
                                                      script_at(0x8a060329c1d2a4f2ull, 0x3906203de754741cull)};
constexpr voice::GeneratorState trap_start = synth_at(0x6e6a96a5312c55c4ull, 0xc3d323f4db4a1635ull);
constexpr voice::GeneratorState trap_rng_start = script_at(0xf68e80d9c47a8e07ull, 0xeaf6fb1c07e09f27ull);
constexpr voice::GeneratorState muffle_rng_starts[2] = {script_at(0xadace03b7a8b3ef1ull, 0x1c5f3ed4952b5cc3ull),
                                                        script_at(0xeca78fa7d00eb2f9ull, 0xc5427b33afc812c8ull)};
constexpr voice::GeneratorState raspberry_start = synth_at(0x169019263658bfe4ull, 0xbc5970fc5da5608bull);
constexpr voice::GeneratorState crunch_start = synth_at(0xecdbd9f81e8e1ea7ull, 0x8b78898a3876b0e9ull);
constexpr voice::GeneratorState laugh_rng_start = script_at(0x31cd4e91ad8fa93full, 0x7b1df25cf70ea1d5ull);
constexpr voice::GeneratorState bump_start = synth_at(0x8c930224360e686bull, 0x608c4cde055e922full);
constexpr voice::GeneratorState undo_start = synth_at(0x42593eb39169c590ull, 0x03c191cf97a3a118ull);
constexpr voice::GeneratorState restart_start = synth_at(0xf6ea9a2c42072bfbull, 0x2c583f067dde5193ull);

double sign(double x) {
    const double result = x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0);
    return result;
}

// numpy's `array ** curve` for a scalar exponent: 1 copies, 0.5 is a square root.
double power(double x, double curve) {
    if (curve == 1) {
        return x;
    }
    if (curve == .5) {
        return std::sqrt(x);
    }
    const double result = std::pow(x, curve);
    return result;
}

Signal sweep(double f0, double f1, double dur, double curve = 1.0) {
    const std::size_t n = voice::samples(dur);
    const Signal u = voice::linspace(0, 1, n);
    const double ratio = f1 / f0;
    Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        f[i] = f0 * std::pow(ratio, power(u[i], curve));
    }
    const Signal cs = voice::cumulative_sum(f);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = std::sin(two_pi * cs[i] / voice::sample_rate);
    }
    return y;
}

// chirp_voice: a small animal voice, a pulse-ish tone gliding f0 -> f1 through one formant.
Signal chirp_voice(voice::Generator& rng, double f0, double f1, double dur, double formant = 1400,
                   double rough = 0.0) {
    const std::size_t n = voice::samples(dur);
    const Signal t = voice::tarr(n);
    const double ratio = f1 / f0;
    Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double glide = f0 * std::pow(ratio, t[i] / dur);
        const double jitter = rough * rng.normal(0, 1) * .02;
        f[i] = glide * (1 + jitter);
    }
    const Signal cs = voice::cumulative_sum(f);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double ph = two_pi * cs[i] / voice::sample_rate;
        y[i] = sign(std::sin(ph)) * .4 + std::sin(ph) * .6;
    }
    y = voice::fft_filter(y, formant * .5, formant * 2.2, 2);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * std::pow(std::sin(voice::pi * std::clamp(t[i] / dur, 0.0, 1.0)), .6);
    }
    return y;
}

Signal joined(std::span<const Signal> parts) {
    Signal y{};
    for (const Signal& part : parts) {
        y.insert(y.end(), part.begin(), part.end());
    }
    return y;
}

// steps on soft soil
template <int I>
Signal step() {
    voice::Synth synth(step_starts[I]);
    const std::size_t n = voice::samples(.12);
    const Signal t = voice::tarr(n);
    const Signal grit = voice::fft_filter(synth.noise(n), 600, 4000);
    const double w = two_pi * (90 + 30 * I);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = std::sin(w * t[i]) * std::exp(-t[i] / .03) + grit[i] * std::exp(-t[i] / .02) * .5;
    }
    const Signal result = voice::master(x, -20);
    return result;
}

// pushing a pumpkin: a scrape across the soil and a small "hup"
template <int I>
Signal push() {
    voice::Synth synth(push_starts[I]);
    voice::Generator rng(push_rng_starts[I]);
    const std::size_t n = voice::samples(.32);
    const Signal t = voice::tarr(n);
    Signal y = voice::fft_filter(synth.noise(n), 300 + 80 * I, 2200);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * std::sin(voice::pi * std::clamp(t[i] / .3, 0.0, 1.0)) * .6;
    }
    const Signal hup = chirp_voice(rng, 240 + 25 * I, 200 + 20 * I, .12, 900);
    for (std::size_t i = 0; i < hup.size(); ++i) {
        y[i] += hup[i] * .7;
    }
    const Signal result = voice::master(y, -15);
    return result;
}

// a pumpkin drops onto a burrow: a hollow thunk, a puff of soil, a tiny "eep"
Signal trap() {
    voice::Synth synth(trap_start);
    voice::Generator rng(trap_rng_start);
    const std::size_t n = voice::samples(.6);
    const Signal t = voice::tarr(n);
    const Signal puff = voice::fft_filter(synth.noise(n), 400, 3000);
    const Signal e = chirp_voice(rng, 900, 1300, .1, 2200);
    const std::size_t s = voice::samples(.12);
    const double w = two_pi * 180;
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double thunk = std::sin(two_pi * (70 + 80 * std::exp(-t[i] / .03)) * t[i]) * std::exp(-t[i] / .12) +
                             .5 * std::sin(w * t[i]) * std::exp(-t[i] / .05);
        const double eep = i >= s && i - s < e.size() ? e[i - s] * .35 : 0.0;
        x[i] = thunk + puff[i] * std::exp(-t[i] / .06) * .4 + eep;
    }
    const Signal result = voice::master(x, -10);
    return result;
}

// a raccoon pops up, and ducks back down
Signal pop() {
    Signal y = sweep(300, 900, .09, .6);
    const Signal t = voice::tarr(y.size());
    for (std::size_t i = 0; i < y.size(); ++i) {
        y[i] = y[i] * std::exp(-t[i] / .05);
    }
    y.resize(y.size() + voice::samples(.02), 0.0);
    const Signal result = voice::master(y, -16);
    return result;
}

Signal duck() {
    Signal y = sweep(900, 260, .12, 1.4);
    const Signal t = voice::tarr(y.size());
    for (std::size_t i = 0; i < y.size(); ++i) {
        y[i] = y[i] * std::exp(-t[i] / .06);
    }
    const Signal result = voice::master(y, -18);
    return result;
}

// muffled grumbling from under a pumpkin
template <int I>
Signal muffle() {
    voice::Generator rng(muffle_rng_starts[I]);
    std::vector<Signal> parts{};
    for (int k = 0; k < 3 + I; ++k) {
        const double f0 = rng.uniform(200, 320);
        const double f1 = rng.uniform(160, 280);
        const double dur = rng.uniform(.08, .16);
        parts.push_back(chirp_voice(rng, f0, f1, dur, 500, 1));
        parts.push_back(Signal(voice::samples(rng.uniform(.02, .06)), 0.0));
    }
    const Signal result = voice::master(voice::fft_filter(joined(parts), 80, 900, 2), -17);
    return result;
}

// a raspberry: a buzzing tongue
Signal raspberry() {
    voice::Synth synth(raspberry_start);
    const std::size_t n = voice::samples(.55);
    const Signal t = voice::tarr(n);
    const double w7 = two_pi * 7;
    const double w31 = two_pi * 31;
    Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        f[i] = 95 + 25 * std::sin(w7 * t[i]);
    }
    const Signal cs = voice::cumulative_sum(f);
    Signal buzz(n);
    for (std::size_t i = 0; i < n; ++i) {
        buzz[i] = sign(std::sin(two_pi * cs[i] / voice::sample_rate)) * (1 + .4 * std::sin(w31 * t[i]));
    }
    const Signal tone = voice::fft_filter(buzz, 120, 2400, 2);
    const Signal hiss = voice::fft_filter(synth.noise(n), 800, 4000);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double b = tone[i] + hiss[i] * .2;
        x[i] = b * std::min(1.0, t[i] / .03) * std::exp(-std::max(0.0, t[i] - .35) / .06);
    }
    const Signal result = voice::master(x, -14);
    return result;
}

// a carrot crunch
Signal crunch() {
    voice::Synth synth(crunch_start);
    const std::size_t n = voice::samples(.18);
    const Signal t = voice::tarr(n);
    const Signal bite = voice::fft_filter(synth.noise(n), 1500, 9000);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double second = .6 * std::exp(-std::max(0.0, t[i] - .05) / .015) * (t[i] > .05 ? 1.0 : 0.0);
        const double third = .4 * std::exp(-std::max(0.0, t[i] - .1) / .015) * (t[i] > .1 ? 1.0 : 0.0);
        x[i] = bite[i] * (std::exp(-t[i] / .02) + second + third);
    }
    const Signal result = voice::master(x, -18);
    return result;
}

// the raccoons' laugh: a quick run of chittering "hee"s, falling
Signal laugh() {
    voice::Generator rng(laugh_rng_start);
    std::vector<Signal> parts{};
    for (int k = 0; k < 6; ++k) {
        const double f0 = 1200 - k * 70 + rng.uniform(-40, 40);
        parts.push_back(chirp_voice(rng, f0, f0 * .82, .075, 2600));
        parts.push_back(Signal(voice::samples(.035), 0.0));
    }
    const Signal result = voice::master(joined(parts), -15);
    return result;
}

// walking into a hedge: a leafy rustle and a soft bonk
Signal bump() {
    voice::Synth synth(bump_start);
    const std::size_t n = voice::samples(.25);
    const Signal t = voice::tarr(n);
    const Signal rustle = voice::fft_filter(synth.noise(n), 1500, 7000);
    const double w = two_pi * 150;
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = rustle[i] * std::exp(-t[i] / .06) * .5 + std::sin(w * t[i]) * std::exp(-t[i] / .04);
    }
    const Signal result = voice::master(x, -20);
    return result;
}

// undo: a soft backwards swish; start over: a quick shuffle of soil
Signal undo() {
    voice::Synth synth(undo_start);
    const Signal s = sweep(1400, 700, .12, 1);
    const std::size_t n = voice::samples(.12);
    const Signal t = voice::tarr(n);
    const Signal swish = voice::fft_filter(synth.noise(n), 2000, 6000);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = s[i] * std::exp(-t[i] / .05) * .6 + swish[i] * .2;
    }
    const Signal result = voice::master(x, -22);
    return result;
}

Signal restart() {
    voice::Synth synth(restart_start);
    const std::size_t n = voice::samples(.4);
    const Signal t = voice::tarr(n);
    const Signal soil = voice::fft_filter(synth.noise(n), 300, 3000);
    const Signal s = sweep(500, 250, .4, 1);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double hump = std::sin(voice::pi * t[i] / .4);
        x[i] = soil[i] * (hump * hump) + .4 * s[i];
    }
    const Signal result = voice::master(x, -17);
    return result;
}

// a hint: a little chime; a click
Signal hint() {
    const std::size_t n = voice::samples(.6);
    const Signal t = voice::tarr(n);
    static constexpr double bells[3][3] = {{1568, 1, .25}, {2349, .5, .2}, {3136, .25, .12}};
    Signal x(n, 0.0);
    for (const double* bell : bells) {
        const double w = two_pi * bell[0];
        for (std::size_t i = 0; i < n; ++i) {
            x[i] = x[i] + bell[1] * std::sin(w * t[i]) * std::exp(-t[i] / bell[2]);
        }
    }
    const Signal result = voice::master(x, -16);
    return result;
}

Signal click() {
    const std::size_t n = voice::samples(.04);
    const Signal t = voice::tarr(n);
    const double w = two_pi * 1800;
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = std::sin(w * t[i]) * std::exp(-t[i] / .006);
    }
    const Signal result = voice::master(x, -22);
    return result;
}

constexpr voice::Cue cues[] = {
    {.name = "ct_step_01", .mono = step<0>},
    {.name = "ct_step_02", .mono = step<1>},
    {.name = "ct_step_03", .mono = step<2>},
    {.name = "ct_push_01", .mono = push<0>},
    {.name = "ct_push_02", .mono = push<1>},
    {.name = "ct_push_03", .mono = push<2>},
    {.name = "ct_trap", .mono = trap},
    {.name = "ct_pop", .mono = pop},
    {.name = "ct_duck", .mono = duck},
    {.name = "ct_muffle_01", .mono = muffle<0>},
    {.name = "ct_muffle_02", .mono = muffle<1>},
    {.name = "ct_raspberry", .mono = raspberry},
    {.name = "ct_crunch", .mono = crunch},
    {.name = "ct_laugh", .mono = laugh},
    {.name = "ct_bump", .mono = bump},
    {.name = "ct_undo", .mono = undo},
    {.name = "ct_restart", .mono = restart},
    {.name = "ct_hint", .mono = hint},
    {.name = "ct_click", .mono = click},
    {.name = "ct_music_spring", .stereo = music_spring},
    {.name = "ct_music_summer", .stereo = music_summer},
    {.name = "ct_music_autumn", .stereo = music_autumn},
    {.name = "ct_music_winter", .stereo = music_winter},
    {.name = "ct_music_night", .stereo = music_night},
    {.name = "ct_stinger_win", .stereo = stinger_win},
    {.name = "ct_stinger_stuck", .stereo = stinger_stuck},
    {.name = "ct_stinger_book", .stereo = stinger_book},
};

}  // namespace

std::span<const voice::Cue> sound_cues() {
    const std::span<const voice::Cue> result(cues);
    return result;
}

void register_sound_cues() {
    voice::Bank::shared().add(cues);
}

}  // namespace ct
