#include "atomprobe_cues.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

// Each effect follows audio_src/make_sfx.py line for line, in numpy's order of
// operations. The states are what tools/numpy_reference.py (shared/voice) printed for
// that script: synth._rng (seeded 12345) is shared by every cue in script order, and
// the script's own rng (seeded 2026) is used only by the fog's shimmer and the atoms'
// sparkle.
namespace ap {
namespace {

using voice::Signal;

constexpr double two_pi = 2 * voice::pi;

constexpr voice::Word128 synth_increment{0xc9c7353e6e2b1f28ull, 0x7d761f2d4027fae7ull};

constexpr voice::GeneratorState synth_at(std::uint64_t high, std::uint64_t low) {
    const voice::GeneratorState state{{high, low}, synth_increment, false, 0u};
    return state;
}

constexpr voice::GeneratorState hover_start = synth_at(0x1905e0335aae9634ull, 0x9199b0d09775add5ull);  // as imported
constexpr voice::GeneratorState fire_start = synth_at(0x0d002ffdcdaa0428ull, 0xebbc09107fd083c8ull);
constexpr voice::GeneratorState hum_start = synth_at(0xacbe29c384173c57ull, 0xe161b59af6f8c234ull);
constexpr voice::GeneratorState hit_start = synth_at(0xd385cc8266ee91e5ull, 0xb2ca7b04873649b7ull);
constexpr voice::GeneratorState mark_start = synth_at(0xcbc220adf6d1b82aull, 0x609318ee6f54ba3eull);
constexpr voice::GeneratorState cross_start = synth_at(0x15a21b470059be14ull, 0x3035e1e709d99167ull);
constexpr voice::GeneratorState lever_start = synth_at(0x22bca2a4fee6ef2cull, 0x61eddc38e094346eull);
constexpr voice::GeneratorState vent_start = synth_at(0xf2e34b30986bb255ull, 0x09a01a896beaeb11ull);
constexpr voice::GeneratorState replay_start = synth_at(0x70000d4558c8d29dull, 0x6e9244d97c8850b7ull);
constexpr std::uint64_t hum_rng_start = 2026;  // rng = np.random.default_rng(2026), first used by the hum
constexpr voice::GeneratorState atoms_rng_start{
    {0x7e3cdb979e4ba9b4ull, 0xd0fb2362a584dac6ull}, {0xbec6782ecb0472d8ull, 0xdd766bd09854840bull}, false, 0u};

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

// sweep(f0, f1, dur, curve): a sine gliding exponentially, and its frequencies.
struct Sweep {
    Signal y{};
    Signal f{};
};

Sweep sweep(double f0, double f1, double dur, double curve = 1.0) {
    const std::size_t n = voice::samples(dur);
    const Signal u = voice::linspace(0, 1, n);
    const double ratio = f1 / f0;
    Sweep s{Signal(n), Signal(n)};
    for (std::size_t i = 0; i < n; ++i) {
        s.f[i] = f0 * std::pow(ratio, power(u[i], curve));
    }
    const Signal cs = voice::cumulative_sum(s.f);
    for (std::size_t i = 0; i < n; ++i) {
        s.y[i] = std::sin(two_pi * cs[i] / voice::sample_rate);
    }
    return s;
}

// bell(f, dur, tau, partials): struck partials, the higher ones dying faster.
struct Partial {
    double ratio;
    double amplitude;
};
constexpr Partial bell_partials[] = {{1, 1}, {2.76, .3}, {5.4, .12}, {8.9, .05}};

Signal bell(double f, double dur, double tau, std::span<const Partial> partials = bell_partials) {
    const std::size_t n = voice::samples(dur);
    const Signal t = voice::tarr(n);
    Signal y(n, 0.0);
    for (std::size_t k = 0; k < partials.size(); ++k) {
        const double w = two_pi * f * partials[k].ratio;
        const double decay = tau / (1 + .6 * static_cast<double>(k));
        for (std::size_t i = 0; i < n; ++i) {
            y[i] = y[i] + partials[k].amplitude * std::sin(w * t[i]) * std::exp(-t[i] / decay);
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * std::min(1.0, t[i] / .001);
    }
    return y;
}

void add_at(Signal& y, std::size_t at, std::span<const double> x, double gain = 1.0) {
    for (std::size_t i = 0; i < x.size() && at + i < y.size(); ++i) {
        y[at + i] += gain == 1.0 ? x[i] : x[i] * gain;
    }
}

// hovering an emitter: a tiny relay tick
Signal hover() {
    voice::Synth synth(hover_start);
    const std::size_t n = voice::samples(.03);
    const Signal t = voice::tarr(n);
    const Signal hiss = voice::fft_filter(synth.noise(n), 3000, 9000);
    const double w = two_pi * 2600;
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = std::sin(w * t[i]) * std::exp(-t[i] / .006) + .3 * hiss[i] * std::exp(-t[i] / .003);
    }
    const Signal result = voice::master(x, -24);
    return result;
}

// charging: a rising whine with a capacitor flutter, ending on a click
Signal charge() {
    const Sweep s = sweep(320, 1500, .3, 1.6);
    const std::size_t n = s.y.size();
    const Signal t = voice::tarr(n);
    Signal doubled(n);
    for (std::size_t i = 0; i < n; ++i) {
        doubled[i] = s.f[i] * 2.01;
    }
    const Signal cs = voice::cumulative_sum(doubled);
    const double w = two_pi * 38;
    Signal y(n + voice::samples(.04), 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = (s.y[i] + .35 * std::sin(two_pi * cs[i] / voice::sample_rate)) * (.3 + .7 * t[i] / .3) *
               (1 + .25 * std::sin(w * t[i]));
    }
    const Signal result = voice::master(y, -17);
    return result;
}

// firing: a laser zap, a fast falling chirp over a crack of noise
Signal fire() {
    voice::Synth synth(fire_start);
    const Sweep s = sweep(2600, 260, .26, .45);
    const std::size_t n = s.y.size();
    const Signal t = voice::tarr(n);
    const Signal crack = voice::fft_filter(synth.noise(n), 1500, 12000);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double zap = (s.y[i] + .4 * sign(s.y[i]) * .5) * std::exp(-t[i] / .09);
        x[i] = zap + crack[i] * std::exp(-t[i] / .012) * .5;
    }
    const Signal result = voice::master(x, -12);
    return result;
}

// the fog taking the beam: a low resonant hum swelling and fading, with a shimmer
Signal hum() {
    voice::Synth synth(hum_start);
    voice::Generator rng(hum_rng_start);
    const std::size_t n = voice::samples(.75);
    const Signal t = voice::tarr(n);
    Signal e(n);
    for (std::size_t i = 0; i < n; ++i) {
        e[i] = std::pow(std::sin(voice::pi * std::clamp(t[i] / .75, 0.0, 1.0)), 1.5);
    }
    const Signal air = voice::fft_filter(synth.noise(n), 600, 3800);
    static constexpr double shimmers[3] = {1318, 1661, 1975};
    double phases[3];
    for (double& phase : phases) {
        phase = rng.uniform(0, 6);
    }
    const double w82 = two_pi * 82;
    const double w83 = two_pi * 83.5;
    const double w164 = two_pi * 164;
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double tone = (std::sin(w82 * t[i]) + .6 * std::sin(w83 * t[i]) + .3 * std::sin(w164 * t[i])) * e[i];
        double shimmer = 0;
        for (int k = 0; k < 3; ++k) {
            shimmer = shimmer + std::sin(two_pi * shimmers[k] * t[i] + phases[k]);
        }
        shimmer = shimmer * e[i] * std::exp(-t[i] / .3) * .08;
        x[i] = tone + air[i] * e[i] * .25 + shimmer;
    }
    const Signal result = voice::master(x, -15);
    return result;
}

// an exit: the beam bursts out the far side singing, a rising zing into a bright bell
Signal exit() {
    const Sweep s = sweep(700, 2400, .12, .7);
    const std::size_t n = s.y.size();
    const Signal t = voice::tarr(n);
    const Signal b = bell(1760, .6, .25);
    Signal out(b.size() + n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] += s.y[i] * std::exp(-t[i] / .06) * .8;
    }
    add_at(out, voice::samples(.06), b);
    const Signal result = voice::master(out, -13);
    return result;
}

// a reflection: a metallic ping and its quick echo back
Signal reflect() {
    static constexpr Partial metal[] = {{1, 1}, {2.32, .4}, {4.1, .2}};
    const Signal p1 = bell(1320, .5, .16, metal);
    const Signal p2 = bell(990, .5, .2, metal);
    Signal out(voice::samples(.65), 0.0);
    add_at(out, 0, p1);
    add_at(out, voice::samples(.09), p2, .8);
    const Signal result = voice::master(out, -13);
    return result;
}

// an absorption: a deep thump that swallows the light
Signal hit() {
    voice::Synth synth(hit_start);
    const std::size_t n = voice::samples(.9);
    const Signal t = voice::tarr(n);
    Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        f[i] = 38 + 70 * std::exp(-t[i] / .05);
    }
    const Signal cs = voice::cumulative_sum(f);
    const Signal mud = voice::fft_filter(synth.noise(n), 60, 500);
    const Sweep suck = sweep(900, 120, .35, .5);
    const Signal ts = voice::tarr(suck.y.size());
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double boom = std::sin(two_pi * cs[i] / voice::sample_rate) * std::exp(-t[i] / .3);
        const double dirt = mud[i] * std::exp(-t[i] / .12) * .4;
        const double pull = i < suck.y.size() ? suck.y[i] * std::exp(-ts[i] / .12) * .25 : 0.0;
        x[i] = boom + dirt + pull;
    }
    const Signal result = voice::master(x, -9);
    return result;
}

// already known: a soft double blip
Signal blip() {
    const std::size_t n = voice::samples(.16);
    const Signal t = voice::tarr(n);
    const double w = two_pi * 1500;
    Signal b(n);
    for (std::size_t i = 0; i < n; ++i) {
        b[i] = std::sin(w * t[i]) * std::exp(-t[i] / .02);
    }
    Signal out(voice::samples(.25), 0.0);
    add_at(out, 0, b);
    add_at(out, voice::samples(.07), b, .7);
    const Signal result = voice::master(out, -20);
    return result;
}

// markers: a glassy set-down, a lift, a scratch
Signal mark() {
    voice::Synth synth(mark_start);
    const std::size_t n = voice::samples(.35);
    const Signal t = voice::tarr(n);
    const Signal tap = voice::fft_filter(synth.noise(n), 2000, 9000);
    static constexpr Partial glass[] = {{1, 1}, {2.0, .3}, {3.0, .1}};
    const Signal b = bell(2093, .35, .12, glass);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = tap[i] * std::exp(-t[i] / .004) * .5 + b[i];
    }
    const Signal result = voice::master(x, -15);
    return result;
}

Signal unmark() {
    const Sweep s = sweep(1800, 900, .12, 1);
    const Signal t = voice::tarr(s.y.size());
    Signal x(s.y.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = s.y[i] * std::exp(-t[i] / .04);
    }
    const Signal result = voice::master(x, -20);
    return result;
}

Signal cross() {
    voice::Synth synth(cross_start);
    const std::size_t n = voice::samples(.1);
    const Signal t = voice::tarr(n);
    const Signal scratch = voice::fft_filter(synth.noise(n), 2500, 8000);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double second = .6 * std::exp(-std::max(0.0, t[i] - .045) / .01) * (t[i] > .045 ? 1.0 : 0.0);
        x[i] = scratch[i] * (std::exp(-t[i] / .01) + second);
    }
    const Signal result = voice::master(x, -21);
    return result;
}

// refused: a low double buzz
Signal full() {
    const std::size_t n = voice::samples(.28);
    const Signal t = voice::tarr(n);
    const double w = two_pi * 110;
    Signal buzz(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double second = std::exp(-std::max(0.0, t[i] - .13) / .05) * (t[i] > .13 ? 1.0 : 0.0);
        buzz[i] = sign(std::sin(w * t[i])) * (std::exp(-t[i] / .05) + second);
    }
    const Signal result = voice::master(voice::fft_filter(buzz, 80, 1500), -19);
    return result;
}

// the lever: a heavy ratcheting pull, then the clunk
Signal lever() {
    voice::Synth synth(lever_start);
    const std::size_t n = voice::samples(.6);
    Signal y(n, 0.0);
    const std::size_t m = voice::samples(.02);
    const Signal tm = voice::tarr(m);
    for (int k = 0; k < 5; ++k) {
        const std::size_t s = voice::samples(.02 + k * .045);
        const Signal click = voice::fft_filter(synth.noise(m), 1500, 6000);
        for (std::size_t i = 0; i < m; ++i) {
            y[s + i] += click[i] * std::exp(-tm[i] / .004) * .5;
        }
    }
    const std::size_t s = voice::samples(.27);
    const std::size_t rest = n - s;
    const Signal tt = voice::tarr(rest);
    const Signal clunk = voice::fft_filter(synth.noise(rest), 200, 3000);
    for (std::size_t i = 0; i < rest; ++i) {
        y[s + i] += std::sin(two_pi * (70 + 60 * std::exp(-tt[i] / .02)) * tt[i]) * std::exp(-tt[i] / .1) +
                    .5 * clunk[i] * std::exp(-tt[i] / .03);
    }
    const Signal result = voice::master(y, -9);
    return result;
}

// the vents: pressure released, a long hiss that thins out
Signal vent() {
    voice::Synth synth(vent_start);
    const std::size_t n = voice::samples(2.0);
    const Signal t = voice::tarr(n);
    const Signal h = voice::fft_filter(synth.noise(n), 1500, 11000);
    const Signal whoosh = voice::fft_filter(synth.noise(n), 200, 1200);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double e = std::min(1.0, t[i] / .05) * std::exp(-t[i] / .7);
        x[i] = h[i] * e + whoosh[i] * std::exp(-t[i] / .3) * .6;
    }
    const Signal result = voice::master(x, -12);
    return result;
}

// the atoms appear: a shimmering chord swells in (E major add 9)
Signal atoms() {
    voice::Generator rng(atoms_rng_start);
    const std::size_t n = voice::samples(2.0);
    const Signal t = voice::tarr(n);
    static constexpr int chord_notes[6] = {64, 68, 71, 75, 78, 83};
    const double w45 = two_pi * 4.5;
    Signal x(n, 0.0);
    for (int k = 0; k < 6; ++k) {
        const double w = two_pi * voice::mtof(chord_notes[k]);
        const double weight = 1 + .2 * k;
        for (std::size_t i = 0; i < n; ++i) {
            x[i] = x[i] + std::sin(w * t[i] * (1 + .002 * std::sin(w45 * t[i] + k))) / weight;
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        const double e = std::min(1.0, t[i] / .6) * std::exp(-std::max(0.0, t[i] - .8) / .45);
        x[i] = x[i] * e;
    }
    static constexpr int sparkle_notes[5] = {88, 92, 95, 99, 100};
    Signal sparkle(n, 0.0);
    for (int j = 0; j < 26; ++j) {
        const std::size_t s = voice::samples(rng.uniform(.1, 1.5));
        const Signal b = bell(voice::mtof(sparkle_notes[rng.choice(5)]), .3, .08);
        add_at(sparkle, s, b, .15);
    }
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = x[i] + sparkle[i];
    }
    const Signal result = voice::master(x, -13);
    return result;
}

// a beam replayed along its true path: a soft travelling zap
Signal replay() {
    voice::Synth synth(replay_start);
    const Sweep s = sweep(1800, 600, .35, .8);
    const std::size_t n = s.y.size();
    const Signal t = voice::tarr(n);
    const Signal hiss = voice::fft_filter(synth.noise(n), 2000, 7000);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double travel = std::sqrt(std::sin(voice::pi * std::clamp(t[i] / .35, 0.0, 1.0)));
        x[i] = s.y[i] * travel * std::exp(-t[i] / .2) + hiss[i] * std::exp(-t[i] / .03) * .2;
    }
    const Signal result = voice::master(x, -17);
    return result;
}

// a marker locked onto its atom, and one that found nothing
Signal lock() {
    const Signal a = bell(1568, .6, .25);
    const Signal b = bell(2349, .6, .3);
    Signal out(voice::samples(.7), 0.0);
    add_at(out, 0, a);
    add_at(out, voice::samples(.08), b, .9);
    const Signal result = voice::master(out, -13);
    return result;
}

Signal wrong() {
    const std::size_t n = voice::samples(.45);
    const Signal t = voice::tarr(n);
    const Sweep s = sweep(240, 120, .45, 1);
    Signal square(n);
    for (std::size_t i = 0; i < n; ++i) {
        square[i] = sign(s.y[i]);
    }
    const Signal buzz = voice::fft_filter(square, 60, 1200);
    const double w = two_pi * 60;
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = buzz[i] * std::exp(-t[i] / .12) + std::sin(w * t[i]) * std::exp(-t[i] / .1);
    }
    const Signal result = voice::master(x, -15);
    return result;
}

// a new box powering up: a rising sweep and the emitters coming online in a ripple of clicks
Signal boot() {
    const std::size_t n = voice::samples(1.2);
    const Signal t = voice::tarr(n);
    const Sweep s = sweep(90, 700, 1.0, 1.4);
    Signal y(n, 0.0);
    for (std::size_t i = 0; i < s.y.size(); ++i) {
        y[i] = s.y[i];
    }
    Signal driven(n);
    for (std::size_t i = 0; i < n; ++i) {
        driven[i] = y[i] + .5 * sign(y[i]);
    }
    const Signal rise = voice::fft_filter(driven, 40, 2500);
    const std::size_t m = voice::samples(.012);
    const Signal tm = voice::tarr(m);
    Signal clicks(n, 0.0);
    for (int k = 0; k < 16; ++k) {
        const std::size_t at = voice::samples(.15 + k * .055);
        const double w = two_pi * (1800 + 60 * k);
        for (std::size_t i = 0; i < m; ++i) {
            clicks[at + i] += std::sin(w * tm[i]) * std::exp(-tm[i] / .003) * .4;
        }
    }
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double shaped = rise[i] * std::min(1.0, t[i] / .2) * std::exp(-std::max(0.0, t[i] - .9) / .08) * .5;
        x[i] = shaped + clicks[i];
    }
    const Signal result = voice::master(x, -13);
    return result;
}

constexpr voice::Cue cues[] = {
    {.name = "ap_hover", .mono = hover},
    {.name = "ap_charge", .mono = charge},
    {.name = "ap_fire", .mono = fire},
    {.name = "ap_hum", .mono = hum},
    {.name = "ap_exit", .mono = exit},
    {.name = "ap_reflect", .mono = reflect},
    {.name = "ap_hit", .mono = hit},
    {.name = "ap_blip", .mono = blip},
    {.name = "ap_mark", .mono = mark},
    {.name = "ap_unmark", .mono = unmark},
    {.name = "ap_cross", .mono = cross},
    {.name = "ap_full", .mono = full},
    {.name = "ap_lever", .mono = lever},
    {.name = "ap_vent", .mono = vent},
    {.name = "ap_atoms", .mono = atoms},
    {.name = "ap_replay", .mono = replay},
    {.name = "ap_lock", .mono = lock},
    {.name = "ap_wrong", .mono = wrong},
    {.name = "ap_boot", .mono = boot},
    {.name = "ap_music", .stereo = music},
    {.name = "ap_stinger_win", .stereo = stinger_win},
    {.name = "ap_stinger_miss", .stereo = stinger_miss},
    {.name = "ap_stinger_topscore", .stereo = stinger_topscore},
};

}  // namespace

std::span<const voice::Cue> sound_cues() {
    const std::span<const voice::Cue> result(cues);
    return result;
}

void register_sound_cues() {
    voice::Bank::shared().add(cues);
}

}  // namespace ap
