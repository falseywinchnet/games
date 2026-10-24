#include "maze_cues.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

// Each cue follows its script line for line, in numpy's order of operations. The states
// are what tools/numpy_reference.py (shared/voice) printed for make_sfx.py, whose cues
// share synth._rng (seeded 12345) in script order; the snail and the residents draw from
// their own generators (seeded 1995 and 2026), recorded at the start of each cue that uses
// them. The jingles come from mall_music.py, as its masters were made: after its old
// "Atrium" loop had been arranged, so the start chime's pads draw from _rng where that
// arrangement left it (the shipped masters/mz_start.wav is that render, byte for byte).
namespace mz {
namespace {

using voice::Signal;

constexpr double two_pi = 2 * voice::pi;

constexpr voice::Word128 synth_increment{0xc9c7353e6e2b1f28ull, 0x7d761f2d4027fae7ull};
constexpr voice::Word128 snail_increment{0x66630da7872ae075ull, 0x7c1969437db41787ull};
constexpr voice::Word128 residents_increment{0xbec6782ecb0472d8ull, 0xdd766bd09854840bull};

constexpr voice::GeneratorState synth_at(std::uint64_t high, std::uint64_t low) {
    const voice::GeneratorState state{{high, low}, synth_increment, false, 0u};
    return state;
}

constexpr voice::GeneratorState step_starts[3] = {synth_at(0x1905e0335aae9634ull, 0x9199b0d09775add5ull),
                                                  synth_at(0xed5fb2ebd8801133ull, 0xc5a263fae74aacedull),
                                                  synth_at(0xfbdfdc7616d132bfull, 0x320fb020867c732cull)};
constexpr voice::GeneratorState turn_start = synth_at(0x5a0822b287776e0eull, 0xd925ea9cb71b94dfull);
constexpr voice::GeneratorState bump_start = synth_at(0x547feb34bf83d398ull, 0x08cb46e8e707341aull);
constexpr voice::GeneratorState locked_start = synth_at(0x843e14de3206a660ull, 0x1e94b2820383ebf2ull);
constexpr voice::GeneratorState pad_start = synth_at(0x716bb0e4fd080009ull, 0x57066715209a70e7ull);
constexpr voice::GeneratorState door_start = synth_at(0xed361852b30f9567ull, 0xe03bdc173fc75646ull);
constexpr voice::GeneratorState flip_start = synth_at(0xa39af9ff5cda57caull, 0x11302b13bdeedb4aull);
constexpr voice::GeneratorState bulb_off_start = synth_at(0x6781a4ce58c491f0ull, 0x1ee665c700e74dbeull);
constexpr voice::GeneratorState bulb_on_start = synth_at(0xb4c9fc0fdcc2a59bull, 0x5fba22b42fee5571ull);
constexpr voice::GeneratorState elevator_start = synth_at(0x68047197a8b0c6b4ull, 0x1e9ab3d5f51ade15ull);
constexpr voice::GeneratorState portal_start = synth_at(0xf21e1a8cf13ab56eull, 0x372e8416f2e20540ull);
constexpr voice::GeneratorState marble_roll_start = synth_at(0x14accd4750877dddull, 0xfe8b479d4af77e03ull);
constexpr voice::GeneratorState marble_bounce_start = synth_at(0x4468db0aba757cf1ull, 0x3c6896a7cf1a4dd8ull);
constexpr voice::GeneratorState marble_bump_start = synth_at(0x554d576aba808ccfull, 0x1521e963628f3cdaull);
constexpr voice::GeneratorState poof_start = synth_at(0x1dcce8affded058eull, 0xa7ea026777c9e7beull);
constexpr voice::GeneratorState keys_start = synth_at(0xc58034a1b4607e7bull, 0xe627621a6fbc5b10ull);
constexpr voice::GeneratorState thinker_start = synth_at(0xdf65f0d56bc0e92dull, 0x4da0b3d57ee0659bull);
// mall_music.py: _rng after arrange() and the win jingle (which draws nothing)
constexpr voice::GeneratorState start_chime_start = synth_at(0x1f4d2ce38bc4e207ull, 0x54282949ff720670ull);

constexpr std::uint64_t snail_seed = 1995;      // own = np.random.default_rng(1995) in snail()
constexpr std::uint64_t residents_seed = 2026;  // own = np.random.default_rng(2026) in residents()
// `own` where each later cue began
constexpr voice::GeneratorState hide_own{{0x83f418190d6f20cfull, 0x2c5bc710bb8fbc3cull}, snail_increment, false, 0xf52b41fcu};
constexpr voice::GeneratorState peek_own{{0xf25a8812d1adbe26ull, 0x6c4c8ac05f45ab33ull}, snail_increment, false, 0xf52b41fcu};
constexpr voice::GeneratorState burp_own{{0x65d12fc5fe0bbbb7ull, 0x5c286d5136b6fdc3ull}, residents_increment, false, 0u};
constexpr voice::GeneratorState thinker_own{{0xc3e4647780c1d943ull, 0x1bf7987f490280f1ull}, residents_increment, false, 0u};

constexpr double peak_fade = .005;  // Maze's write() fades the last 5 ms

double sign(double x) {
    const double result = x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0);
    return result;
}

double clip01(double x) {
    const double result = std::min(std::max(x, 0.0), 1.0);
    return result;
}

double square(double x) {
    const double result = x * x;
    return result;
}

// array ** p, with numpy's fast paths for p = 1, 0.5 and 2
double power(double x, double p) {
    if (p == 1) {
        return x;
    }
    if (p == .5) {
        return std::sqrt(x);
    }
    if (p == 2) {
        return x * x;
    }
    const double result = std::pow(x, p);
    return result;
}

Signal standard_normals(voice::Generator& g, std::size_t n) {
    Signal z(n);
    g.fill_standard_normal(z);
    return z;
}

// sweep(f0, f1, dur, curve)
Signal sweep(double f0, double f1, double dur, double curve) {
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

// env(n, att, tau)
Signal env(std::size_t n, double att, double tau) {
    const Signal t = voice::tarr(n);
    const double a = std::max(att, 1e-6);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = std::min(1.0, t[i] / a) * std::exp(-t[i] / tau);
    }
    return y;
}

// chime(freqs, dur, tau): (f, a) pairs
Signal chime(std::initializer_list<std::pair<double, double>> freqs, double dur, double tau) {
    const std::size_t n = voice::samples(dur);
    const Signal t = voice::tarr(n);
    Signal y(n, 0.0);
    for (const std::pair<double, double>& partial : freqs) {
        const double f = partial.first;
        const double a = partial.second;
        const double w = two_pi * f;
        for (std::size_t i = 0; i < n; ++i) {
            y[i] += a * std::sin(w * t[i]) * std::exp(-t[i] / tau);
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * std::min(1.0, t[i] / .002);
    }
    return y;
}

// place(out, x, at)
void place(Signal& out, const Signal& x, double at) {
    const std::size_t i = voice::samples(at);
    if (i >= out.size()) {
        return;
    }
    const std::size_t n = std::min(x.size(), out.size() - i);
    for (std::size_t k = 0; k < n; ++k) {
        out[i + k] += x[k];
    }
}

// formant(x, centre, width)
Signal formant(const Signal& x, double centre, double width) {
    const Signal result = voice::fft_filter(x, std::max(30.0, centre - width), centre + width);
    return result;
}

// sin(2 pi * cumsum(f) / SR)
Signal phase_of(const Signal& f) {
    const Signal cs = voice::cumulative_sum(f);
    Signal ph(f.size());
    for (std::size_t i = 0; i < f.size(); ++i) {
        ph[i] = two_pi * cs[i] / voice::sample_rate;
    }
    return ph;
}

Signal finish_effect(const Signal& x, double peak_db) {
    const Signal result = voice::master(x, peak_db, peak_fade);
    return result;
}

// ------------------------------------------------------------------ make_sfx.py

// carpet steps: a muffled pad of a footfall
template <int I>
Signal step() {
    voice::Synth synth(step_starts[I]);
    const std::size_t n = voice::samples(.16);
    const Signal t = voice::tarr(n);
    const Signal pad = voice::fft_filter(synth.noise(n), 80, 700 + 120 * I);
    const Signal e1 = env(n, .008, .035);
    const Signal e2 = env(n, .004, .04);
    const double w = two_pi * (70 + 8 * I);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = pad[i] * e1[i] + .5 * std::sin(w * t[i]) * e2[i];
    }
    return finish_effect(x, -19);
}

Signal turn() {
    voice::Synth synth(turn_start);
    const std::size_t n = voice::samples(.22);
    const Signal t = voice::tarr(n);
    const Signal swish = voice::fft_filter(synth.noise(n), 300, 2500);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = swish[i] * square(std::sin(voice::pi * t[i] / .22));
    }
    return finish_effect(x, -26);
}

Signal bump() {
    voice::Synth synth(bump_start);
    const std::size_t n = voice::samples(.25);
    const Signal t = voice::tarr(n);
    const Signal e1 = env(n, .002, .06);
    const Signal hit = voice::fft_filter(synth.noise(n), 200, 2000);
    const Signal e2 = env(n, .001, .02);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = std::sin(two_pi * (110 + 60 * std::exp(-t[i] / .02)) * t[i]) * e1[i] + .3 * hit[i] * e2[i];
    }
    return finish_effect(x, -15);
}

// locked: a heavy clunk and a short buzz
Signal locked() {
    voice::Synth synth(locked_start);
    const std::size_t n = voice::samples(.45);
    const Signal t = voice::tarr(n);
    const Signal e1 = env(n, .002, .08);
    const Signal hit = voice::fft_filter(synth.noise(n), 400, 3000);
    const Signal e2 = env(n, .001, .015);
    const Signal e3 = env(n, .15, .08);
    const double w90 = two_pi * 90;
    const double w110 = two_pi * 110;
    Signal clunk(n);
    Signal buzz(n);
    for (std::size_t i = 0; i < n; ++i) {
        clunk[i] = std::sin(w90 * t[i]) * e1[i] + .5 * hit[i] * e2[i];
        buzz[i] = sign(std::sin(w110 * t[i])) * e3[i] * (t[i] > .15 ? 1.0 : 0.0);
    }
    const Signal filtered = voice::fft_filter(buzz, 80, 1600);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = clunk[i] + filtered[i] * .4;
    }
    return finish_effect(x, -13);
}

// a pad pressed: a click and a bright two-note chime
Signal pad() {
    voice::Synth synth(pad_start);
    const std::size_t n = voice::samples(.6);
    const Signal click = voice::fft_filter(synth.noise(n), 2000, 9000);
    const Signal e = env(n, .0005, .004);
    const Signal bell = chime({{1318.5, 1}, {1975.5, .6}}, .6, .25);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = click[i] * e[i] + bell[i];
    }
    return finish_effect(x, -14);
}

// a door grinding down into the floor
Signal door() {
    voice::Synth synth(door_start);
    const std::size_t n = voice::samples(1.5);
    const Signal t = voice::tarr(n);
    const Signal noise = voice::fft_filter(synth.noise(n), 60, 600);
    const double w11 = two_pi * 11;
    Signal grind(n);
    Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        grind[i] = noise[i] * (1 + .5 * std::sin(w11 * t[i])) * std::min(1.0, t[i] / .1) *
                   std::exp(-std::max(0.0, t[i] - 1.2) / .08);
        f[i] = 55 - 15 * t[i] / 1.5;
    }
    const Signal ph = phase_of(f);
    Signal motor(n);
    for (std::size_t i = 0; i < n; ++i) {
        motor[i] = sign(std::sin(ph[i])) * .15;
    }
    Signal thud(n, 0.0);
    const std::size_t k = voice::samples(1.28);
    const Signal tt = voice::tarr(n - k);
    const double w60 = two_pi * 60;
    for (std::size_t i = 0; i < n - k; ++i) {
        thud[k + i] = std::sin(w60 * tt[i]) * std::exp(-tt[i] / .08);
    }
    const Signal hum = voice::fft_filter(motor, 40, 400);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = grind[i] + hum[i] + thud[i];
    }
    return finish_effect(x, -12);
}

// the flip stone: the world rolling over
Signal flip() {
    voice::Synth synth(flip_start);
    const std::size_t n = voice::samples(1.3);
    const Signal t = voice::tarr(n);
    const Signal air = voice::fft_filter(synth.noise(n), 200, 4000);
    const Signal roll = sweep(80, 320, 1.3, .7);
    const Signal bell = chime({{voice::mtof(85), .4}, {voice::mtof(92), .3}}, 1.3, .5);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double whoosh = air[i] * square(std::sin(voice::pi * clip01(t[i] / 1.2)));
        const double rolled = roll[i] * std::sin(voice::pi * clip01(t[i] / 1.3)) * .5;
        x[i] = whoosh + rolled + bell[i] * (t[i] > .9 ? 1.0 : 0.0);
    }
    return finish_effect(x, -12);
}

// bulbs: pop and fizz out, hum back on
Signal bulb_off() {
    voice::Synth synth(bulb_off_start);
    const std::size_t n = voice::samples(.6);
    const Signal t = voice::tarr(n);
    const Signal pop = voice::fft_filter(synth.noise(n), 1000, 9000);
    const Signal e1 = env(n, .0005, .01);
    const Signal fizz = voice::fft_filter(synth.noise(n), 3000, 9000);
    const Signal e2 = env(n, .01, .12);
    const double w47 = two_pi * 47;
    const double w120 = two_pi * 120;
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double popped = pop[i] * e1[i];
        const double fizzed = fizz[i] * e2[i] * (1 + sign(std::sin(w47 * t[i]))) * .2;
        const double hum = std::sin(w120 * t[i]) * std::exp(-t[i] / .2) * .3;
        x[i] = popped + fizzed + hum * (t[i] < .1 ? 1.0 : 0.0);
    }
    return finish_effect(x, -12);
}

Signal bulb_on() {
    voice::Synth synth(bulb_on_start);
    const std::size_t n = voice::samples(.7);
    const Signal t = voice::tarr(n);
    const double w120 = two_pi * 120;
    const double w240 = two_pi * 240;
    Signal hum(n);
    for (std::size_t i = 0; i < n; ++i) {
        hum[i] = (std::sin(w120 * t[i]) + .5 * std::sin(w240 * t[i])) * std::min(1.0, t[i] / .4) *
                 std::exp(-std::max(0.0, t[i] - .5) / .1) * .5;
    }
    const Signal spark = voice::fft_filter(synth.noise(n), 2000, 6000);
    const Signal e = env(n, .001, .02);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = hum[i] + spark[i] * e[i] * .3;
    }
    return finish_effect(x, -16);
}

// the elevator: a motor winding up, then the ding
Signal elevator() {
    voice::Synth synth(elevator_start);
    const std::size_t n = voice::samples(2.6);
    const Signal t = voice::tarr(n);
    Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        f[i] = 90 + 50 * std::min(1.0, t[i] / 2.2);
    }
    const Signal ph = phase_of(f);
    const Signal noise = voice::fft_filter(synth.noise(n), 100, 900);
    const std::size_t k = voice::samples(2.3);
    const Signal bell = chime({{voice::mtof(84), 1}, {voice::mtof(88), .5}},
                              static_cast<double>(n - k) / voice::sample_rate, .4);
    Signal ding(n, 0.0);
    for (std::size_t i = 0; i < n - k && i < bell.size(); ++i) {
        ding[k + i] = bell[i];
    }
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double motor = std::sin(ph[i]) * std::min(1.0, t[i] / .3) * std::exp(-std::max(0.0, t[i] - 2.2) / .1);
        const double rattle = noise[i] * .3 * std::min(1.0, t[i] / .3) * std::exp(-std::max(0.0, t[i] - 2.2) / .1);
        x[i] = motor * .6 + rattle + ding[i] * .8;
    }
    return finish_effect(x, -12);
}

// a portal: a shimmer that falls and rises
Signal portal() {
    voice::Synth synth(portal_start);
    const std::size_t n = voice::samples(.8);
    const Signal t = voice::tarr(n);
    const double w3 = two_pi * 3;
    Signal shim(n, 0.0);
    for (const double f : {880.0, 1320.0, 1760.0}) {
        const double w = two_pi * f;
        for (std::size_t i = 0; i < n; ++i) {
            shim[i] += std::sin(w * t[i] * (1 + .3 * std::sin(w3 * t[i])));
        }
    }
    const Signal e1 = env(n, .01, .3);
    const Signal sparkle = voice::fft_filter(synth.noise(n), 3000, 10000);
    const Signal e2 = env(n, .001, .05);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = shim[i] * e1[i] + sparkle[i] * e2[i] * .5;
    }
    return finish_effect(x, -13);
}

// the marble: a rumble as it rolls a square, a knock off a wall, a thud face to face
Signal marble_roll() {
    voice::Synth synth(marble_roll_start);
    const std::size_t n = voice::samples(.75);
    const Signal t = voice::tarr(n);
    const Signal rumble = voice::fft_filter(synth.noise(n), 30, 220);
    const double w9 = two_pi * 9;
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = rumble[i] * std::sin(voice::pi * t[i] / .75) * (1 + .3 * std::sin(w9 * t[i]));
    }
    return finish_effect(x, -12);
}

Signal marble_bounce() {
    voice::Synth synth(marble_bounce_start);
    const std::size_t n = voice::samples(.4);
    const Signal t = voice::tarr(n);
    const Signal e1 = env(n, .001, .07);
    const Signal knock = voice::fft_filter(synth.noise(n), 500, 4000);
    const Signal e2 = env(n, .0005, .01);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = std::sin(two_pi * (140 + 80 * std::exp(-t[i] / .01)) * t[i]) * e1[i] + .3 * knock[i] * e2[i];
    }
    return finish_effect(x, -14);
}

Signal marble_bump() {
    voice::Synth synth(marble_bump_start);
    const std::size_t n = voice::samples(.7);
    const Signal t = voice::tarr(n);
    const Signal e1 = env(n, .002, .25);
    const Signal thud = voice::fft_filter(synth.noise(n), 60, 400);
    const Signal e2 = env(n, .002, .1);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = std::sin(two_pi * (45 + 40 * std::exp(-t[i] / .03)) * t[i]) * e1[i] + .3 * thud[i] * e2[i];
    }
    return finish_effect(x, -10);
}

// someone appears (a sparkle), and vanishes (a poof)
Signal appear() {
    const std::size_t n = voice::samples(.6);
    Signal sparkle(n, 0.0);
    const double notes[4] = {88, 91, 95, 100};
    for (int i = 0; i < 4; ++i) {
        const Signal c = chime({{voice::mtof(notes[i]), .5}}, .6, .15);  // chime(...) * 0 + np.roll(chime(...), shift)
        const std::size_t shift = voice::samples(i * .05);
        for (std::size_t j = 0; j < c.size(); ++j) {
            sparkle[(j + shift) % c.size()] += c[j];
        }
    }
    return finish_effect(sparkle, -16);
}

Signal poof() {
    voice::Synth synth(poof_start);
    const std::size_t n = voice::samples(.4);
    const Signal puff = voice::fft_filter(synth.noise(n), 300, 3000);
    const Signal e1 = env(n, .005, .08);
    const Signal fall = sweep(600, 200, .4, 1);
    const Signal e2 = env(n, .001, .1);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = puff[i] * e1[i] + .3 * fall[i] * e2[i];
    }
    return finish_effect(x, -17);
}

Signal click() {
    const std::size_t n = voice::samples(.05);
    const Signal t = voice::tarr(n);
    const double w = two_pi * 2200;
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = std::sin(w * t[i]) * std::exp(-t[i] / .006);
    }
    return finish_effect(x, -22);
}

// ------------------------------------------------------------------ snail()

// the crawl: sticky crackle, slime pulled off a surface, over a soft wet hush
Signal snail_squelch() {
    voice::Generator own(snail_seed);
    const std::size_t n = voice::samples(.5);
    const Signal t = voice::tarr(n);
    std::vector<std::int64_t> ats(130);
    for (std::int64_t& at : ats) {
        at = own.integers(0, static_cast<std::int64_t>(n) - 400);
    }
    Signal pops(n, 0.0);
    for (const std::int64_t at : ats) {
        const std::size_t k = voice::samples(own.uniform(.0006, .0025));
        const Signal z = standard_normals(own, k);
        const double quarter = static_cast<double>(k) / 4;
        const double loud = std::pow(own.uniform(.2, 1), 2.0);
        for (std::size_t j = 0; j < k; ++j) {
            const double decay = std::exp(static_cast<double>(-static_cast<std::int64_t>(j)) / quarter);
            pops[static_cast<std::size_t>(at) + j] += z[j] * decay * loud;
        }
    }
    const Signal filtered = voice::fft_filter(pops, 1200, 8000);
    Signal crackle(n);
    for (std::size_t i = 0; i < n; ++i) {
        crackle[i] = filtered[i] * std::min(1.0, t[i] / .06) * std::exp(-std::max(0.0, t[i] - .25) / .12);
    }
    const Signal wet = voice::fft_filter(standard_normals(own, n), 300, 2500);
    Signal hush(n);
    for (std::size_t i = 0; i < n; ++i) {
        hush[i] = wet[i] * square(std::sin(voice::pi * clip01(t[i] / .5)));
    }
    const double crackle_peak = voice::peak(crackle);
    const double hush_peak = voice::peak(hush);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = crackle[i] / crackle_peak + .35 * hush[i] / hush_peak;
    }
    return finish_effect(x, -22);
}

// into the shell: a suck of air, then the shell's hollow knock on the floor
Signal snail_hide() {
    voice::Generator own(hide_own);
    const std::size_t n = voice::samples(.32);
    const Signal t = voice::tarr(n);
    const Signal suck = sweep(240, 70, .32, .5);
    const Signal e1 = env(n, .004, .07);
    const Signal hiss = voice::fft_filter(standard_normals(own, n), 600, 3500);
    const Signal e2 = env(n, .002, .03);
    const double w = two_pi * 420;
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double knock = t[i] > .09 ? std::sin(w * (t[i] - .09)) * std::exp(-std::max(0.0, t[i] - .09) / .035) : 0.0;
        x[i] = suck[i] * e1[i] + .3 * (hiss[i] * e2[i]) + .55 * knock;
    }
    return finish_effect(x, -17);
}

// peeking out: a tiny rising pip and a little wet
Signal snail_peek() {
    voice::Generator own(peek_own);
    const std::size_t n = voice::samples(.22);
    const Signal t = voice::tarr(n);
    const Signal pip = sweep(520, 1150, .22, 1.4);
    const Signal wet = voice::fft_filter(standard_normals(own, n), 400, 2000);
    const Signal e = env(n, .01, .05);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = pip[i] * std::min(1.0, t[i] / .02) * std::exp(-t[i] / .07) + .3 * wet[i] * e[i];
    }
    return finish_effect(x, -24);
}

// ------------------------------------------------------------------ residents()

// a meow: the pitch rising then falling through an 'ee-ow' of a mouth
Signal meow() {
    const std::size_t n = voice::samples(.62);
    const Signal t = voice::tarr(n);
    const double w6 = two_pi * 6;
    Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double f0 = 520 + 260 * std::pow(std::sin(voice::pi * clip01(t[i] / .5)), 1.5) - 120 * t[i];
        f[i] = f0 * (1 + .012 * std::sin(w6 * t[i]));
    }
    const Signal ph = phase_of(f);
    Signal voiced(n, 0.0);
    for (int k = 1; k < 9; ++k) {
        const double weight = std::pow(static_cast<double>(k), 1.2);
        for (std::size_t i = 0; i < n; ++i) {
            voiced[i] += std::sin(ph[i] * k) / weight;
        }
    }
    const Signal ee = formant(voiced, 900, 500);
    const Signal ow = formant(voiced, 2000, 700);
    const Signal nasal = formant(voiced, 2600, 500);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double shape = std::min(1.0, t[i] / .05) * std::exp(-std::max(0.0, t[i] - .32) / .1);
        const double opening = std::sin(voice::pi * clip01(t[i] / .55));
        const double mouth = ee[i] * (1 - opening) + ow[i] * opening + .5 * nasal[i];
        x[i] = mouth * shape;
    }
    return finish_effect(x, -15);
}

// a honk: harsh, nasal, a little flat at the end
Signal honk() {
    voice::Generator own(residents_seed);
    const std::size_t n = voice::samples(.36);
    const Signal t = voice::tarr(n);
    Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        f[i] = 400 - 70 * t[i] / .36;
    }
    const Signal ph = phase_of(f);
    Signal saw(n, 0.0);
    for (int k = 1; k < 24; ++k) {
        for (std::size_t i = 0; i < n; ++i) {
            saw[i] += std::sin(ph[i] * k) / k;
        }
    }
    const Signal low = formant(saw, 1300, 450);
    const Signal high = formant(saw, 2900, 600);
    const Signal rasp = voice::fft_filter(standard_normals(own, n), 900, 4000);
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double h = low[i] + .6 * high[i] + .25 * rasp[i];
        x[i] = std::tanh(h * 2.5) * std::min(1.0, t[i] / .015) * std::exp(-std::max(0.0, t[i] - .22) / .05);
    }
    return finish_effect(x, -12);
}

// the bicycle bell: ding-ding, struck twice
Signal bell() {
    const std::size_t n = voice::samples(1.0);
    Signal out(n, 0.0);
    static constexpr double partials[4][3] = {{1, 1, .5}, {2.76, .5, .25}, {5.4, .25, .1}, {1.5, .3, .35}};
    for (const double at : {0.0, .17}) {
        const std::size_t m = voice::samples(.8);
        const Signal tt = voice::tarr(m);
        Signal ding(m, 0.0);
        for (const double* p : partials) {
            const double w = two_pi * 2350 * p[0];
            for (std::size_t i = 0; i < m; ++i) {
                ding[i] += p[1] * std::sin(w * tt[i]) * std::exp(-tt[i] / p[2]);
            }
        }
        for (std::size_t i = 0; i < m; ++i) {
            ding[i] = ding[i] * std::min(1.0, tt[i] / .002);
        }
        place(out, ding, at);
    }
    return finish_effect(out, -14);
}

// a baby's burp: a short, wet, low croak
Signal burp() {
    voice::Generator own(burp_own);
    const std::size_t n = voice::samples(.42);
    const Signal t = voice::tarr(n);
    Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        f[i] = 140 - 50 * t[i] / .42;
    }
    const Signal ph = phase_of(f);
    Signal pulses(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double p = std::sin(ph[i]);
        pulses[i] = sign(p) * std::pow(std::fabs(p), .3);
    }
    const Signal low = formant(pulses, 420, 260);
    const Signal high = formant(pulses, 1100, 300);
    const Signal wet = voice::fft_filter(standard_normals(own, n), 200, 1600);
    const double w23 = two_pi * 23;
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double b = low[i] + .5 * high[i] + .15 * wet[i];
        const double shape = std::min(1.0, t[i] / .02) * std::exp(-std::max(0.0, t[i] - .18) / .09) *
                             (1 + .4 * std::sin(w23 * t[i]));
        x[i] = b * shape;
    }
    return finish_effect(x, -13);
}

// Keyboard Cat's riff: a jaunty little run on an electric piano (our own tune)
Signal keys() {
    voice::Synth synth(keys_start);
    const std::size_t n = voice::samples(1.9);
    Signal out(n, 0.0);
    const double beat = .16;
    const double run[9] = {67, 71, 74, 79, 78, 74, 76, 72, 67};
    for (int k = 0; k < 9; ++k) {
        place(out, synth.ep(run[k], beat * (k == 8 ? 2 : .9), .75), k * beat);
    }
    const double line[4] = {43, 47, 50, 43};
    for (int k = 0; k < 4; ++k) {
        place(out, synth.bass(line[k], beat * 2, .7), k * beat * 2.25);
    }
    return finish_effect(out, -14);
}

// Monke: ooh ooh
Signal ooh() {
    const std::size_t n = voice::samples(.9);
    Signal out(n, 0.0);
    const std::pair<double, double> calls[2] = {{0, 330}, {.32, 380}};
    for (const std::pair<double, double>& call : calls) {
        const double at = call.first;
        const double base = call.second;
        const std::size_t m = voice::samples(.34);
        const Signal tt = voice::tarr(m);
        Signal f0(m);
        for (std::size_t i = 0; i < m; ++i) {
            f0[i] = base + 160 * clip01(tt[i] / .2);
        }
        const Signal cs = voice::cumulative_sum(f0);
        const double four_pi = 4 * voice::pi;
        Signal v(m);
        for (std::size_t i = 0; i < m; ++i) {
            v[i] = std::sin(two_pi * cs[i] / voice::sample_rate) + .3 * std::sin(four_pi * cs[i] / voice::sample_rate);
        }
        const Signal mouth = formant(v, 500, 300);
        Signal y(m);
        for (std::size_t i = 0; i < m; ++i) {
            y[i] = mouth[i] * std::pow(std::sin(voice::pi * tt[i] / .34), .8);
        }
        place(out, y, at);
    }
    return finish_effect(out, -15);
}

Signal scaled(Signal y, double g) {
    for (double& v : y) {
        v = v * g;
    }
    return y;
}

// the Thinking Face's own music: two bars at 120, glam, peachy and upbeat, looping cleanly
Signal thinker() {
    voice::Synth synth(thinker_start);
    voice::Generator own(thinker_own);
    const double bar = 2.0;
    const std::size_t n = voice::samples(2 * bar);
    Signal out(n + voice::samples(2), 0.0);
    const double q = bar / 4;
    const double chords[2][4] = {{65, 69, 72, 76}, {64, 67, 71, 74}};  // Fmaj7, Em7
    const double basses[2][4] = {{41, 41, 53, 48}, {40, 40, 52, 47}};
    for (int b = 0; b < 2; ++b) {
        for (int k = 0; k < 4; ++k) {
            for (const double m : chords[b]) {
                place(out, scaled(synth.ep(m, q * .45, .55, 1.3), .5), b * bar + k * q + q * .5);  // off-beat stabs
            }
        }
        for (int k = 0; k < 4; ++k) {
            place(out, synth.bass(basses[b][k], q * .8, .8), b * bar + k * q);
        }
    }
    for (int k = 0; k < 8; ++k) {  // four on the floor, with handclaps on two and four
        const Signal tt = voice::tarr(voice::samples(.3));
        Signal f(tt.size());
        for (std::size_t i = 0; i < tt.size(); ++i) {
            f[i] = 50 + 110 * std::exp(-tt[i] / .03);
        }
        const Signal ph = phase_of(f);
        Signal kick(tt.size());
        for (std::size_t i = 0; i < tt.size(); ++i) {
            kick[i] = std::sin(ph[i]) * std::exp(-tt[i] / .14);
        }
        place(out, scaled(kick, .9), k * q);
        if (k % 2 != 0) {
            const Signal hit = voice::fft_filter(standard_normals(own, tt.size()), 900, 6000);
            Signal clap(tt.size());
            for (std::size_t i = 0; i < tt.size(); ++i) {
                clap[i] = hit[i] * std::exp(-tt[i] / .05);
            }
            place(out, scaled(clap, .45), k * q);
        }
    }
    const double up[4] = {77, 81, 84, 88};
    const double down[4] = {76, 79, 83, 86};
    for (int k = 0; k < 16; ++k) {  // sparkle: a vibraphone arpeggio in sixteenths
        const double m = k < 8 ? up[k % 4] : down[k % 4];
        place(out, scaled(synth.vibes(m, q * .4, .35), .35), k * q / 2);
    }
    Signal loop(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(n));
    for (std::size_t i = 0; i < out.size() - n; ++i) {
        loop[i] += out[n + i];  // the tails wrap round, so the loop has no seam
    }
    return finish_effect(loop, -10);
}

// ------------------------------------------------------------------ mall_music.py

constexpr int bpm = 78;
constexpr double root = 61;  // D-flat

long long spb() {
    const long long result = static_cast<long long>(voice::python_round(static_cast<double>(voice::sample_rate) * 60 / bpm));
    return result;
}

double bus_rms_db(const voice::Stereo& x) {
    Signal m2(x.left.size());
    for (std::size_t i = 0; i < m2.size(); ++i) {
        const double m = (x.left[i] + x.right[i]) / 2;
        m2[i] = m * m;
    }
    const double r = std::sqrt(voice::sum(m2) / static_cast<double>(m2.size()));
    const double result = 20 * std::log10(std::max(r, 1e-9));
    return result;
}

// finish(x, target_rms_db, ceiling_db), then write()'s clip
voice::Stereo finish_jingle(voice::Stereo x, double target_rms_db, double ceiling_db) {
    const double gain = std::pow(10.0, (target_rms_db - bus_rms_db(x)) / 20);
    const double c = std::pow(10.0, ceiling_db / 20);
    for (Signal* channel : {&x.left, &x.right}) {
        for (double& v : *channel) {
            v = std::clamp(std::tanh(v * gain / c) * c, -1.0, 1.0);
        }
    }
    return x;
}

// jingle()'s put(y, beat, pan, g)
void put(voice::Stereo& out, const Signal& y, double beat, double pan = 0.0, double g = 1.0) {
    const std::size_t n = out.left.size();
    const std::size_t s = static_cast<std::size_t>(beat * static_cast<double>(spb()));
    const std::size_t e = std::min(n, s + y.size());
    const double gl = std::cos((pan + 1) * voice::pi / 4);
    const double gr = std::sin((pan + 1) * voice::pi / 4);
    for (std::size_t i = s; i < e; ++i) {
        out.left[i] += gl * g * y[i - s];
        out.right[i] += gr * g * y[i - s];
    }
}

voice::Stereo jingle(bool win) {
    voice::Synth synth(win ? voice::GeneratorState(synth_at(0x1905e0335aae9634ull, 0x9199b0d09775add5ull)) : start_chime_start);
    const std::size_t n = voice::samples(3.2);
    voice::Stereo out{Signal(n, 0.0), Signal(n, 0.0)};
    if (win) {  // found it: a bright run and a ninth chord that hangs in the atrium
        const double run[6] = {0, 4, 7, 11, 14, 16};
        for (int i = 0; i < 6; ++i) {
            put(out, synth.render_note("vibes", root + 12 + run[i], .5, .7), i * .2, .4 * std::sin(static_cast<double>(i)));
        }
        for (const double m : {0.0, 4.0, 7.0, 11.0, 14.0}) {
            put(out, synth.render_note("ep", root + m, 2.2, .6), 1.2, 0, .8);
        }
        put(out, synth.render_note("synthbass", root - 24, 2, .7), 1.2);
    } else {  // start-up: a slow rising chord, the sound of a computer waking in 1995
        const double chord[5] = {0, 7, 11, 16, 19};
        for (int i = 0; i < 5; ++i) {
            put(out, synth.render_note("pad", root - 12 + chord[i], 2.6 - i * .2, .5), i * .25, -.4 + .2 * i);
        }
        put(out, synth.render_note("vibes", root + 16, 1.6, .5), 1.0, .3);
        put(out, synth.render_note("vibes", root + 23, 1.6, .45), 1.25, -.3);
    }
    voice::Stereo send{Signal(n), Signal(n)};
    for (std::size_t i = 0; i < n; ++i) {
        send.left[i] = out.left[i] * .4;
        send.right[i] = out.right[i] * .4;
    }
    const voice::Stereo wet = voice::conv_reverb(send, voice::make_ir(3.0), false);
    for (std::size_t i = 0; i < n; ++i) {
        out.left[i] = out.left[i] + wet.left[i] * .5;
        out.right[i] = out.right[i] + wet.right[i] * .5;
    }
    const std::size_t fade = voice::samples(.5);
    const Signal down = voice::linspace(1, 0, fade);
    for (std::size_t i = 0; i < fade; ++i) {
        out.left[n - fade + i] *= down[i];
        out.right[n - fade + i] *= down[i];
    }
    return finish_jingle(out, -18, -2.5);
}

voice::Stereo stinger_win() {
    return jingle(true);
}

voice::Stereo start_chime() {
    return jingle(false);
}

constexpr voice::Cue cues[] = {
    {.name = "mz_step_01", .mono = step<0>},
    {.name = "mz_step_02", .mono = step<1>},
    {.name = "mz_step_03", .mono = step<2>},
    {.name = "mz_turn", .mono = turn},
    {.name = "mz_bump", .mono = bump},
    {.name = "mz_locked", .mono = locked},
    {.name = "mz_pad", .mono = pad},
    {.name = "mz_door", .mono = door},
    {.name = "mz_flip", .mono = flip},
    {.name = "mz_bulb_off", .mono = bulb_off},
    {.name = "mz_bulb_on", .mono = bulb_on},
    {.name = "mz_elevator", .mono = elevator},
    {.name = "mz_portal", .mono = portal},
    {.name = "mz_marble_roll", .mono = marble_roll},
    {.name = "mz_marble_bounce", .mono = marble_bounce},
    {.name = "mz_marble_bump", .mono = marble_bump},
    {.name = "mz_appear", .mono = appear},
    {.name = "mz_poof", .mono = poof},
    {.name = "mz_click", .mono = click},
    {.name = "mz_snail_squelch", .mono = snail_squelch},
    {.name = "mz_snail_hide", .mono = snail_hide},
    {.name = "mz_snail_peek", .mono = snail_peek},
    {.name = "mz_meow", .mono = meow},
    {.name = "mz_honk", .mono = honk},
    {.name = "mz_bell", .mono = bell},
    {.name = "mz_burp", .mono = burp},
    {.name = "mz_keys", .mono = keys},
    {.name = "mz_ooh", .mono = ooh},
    {.name = "mz_thinker", .mono = thinker},
    {.name = "mz_stinger_win", .stereo = stinger_win},
    {.name = "mz_start", .stereo = start_chime},
};

}  // namespace

std::span<const voice::Cue> sound_cues() {
    const std::span<const voice::Cue> result(cues);
    return result;
}

void register_sound_cues() {
    voice::Bank::shared().add(cues);
}

}  // namespace mz
