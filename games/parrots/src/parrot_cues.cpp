#include "parrot_cues.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

// Each cue follows its script line for line, in numpy's order of operations. The states
// are what shared/voice/tools/numpy_reference.py (make_sfx.py) and
// games/penthesheep/tools/music_reference.py (parlor_music.py) printed: synth._rng
// (seeded 12345) is shared by every cue of a script in script order; make_sfx.py's own
// rng (seeded 1803) voices the syllables and places the cheer, parlor_music.py's (seeded
// 1877) shades the tune's velocities.
namespace pt {
namespace {

constexpr double two_pi = 2 * voice::pi;

constexpr voice::Word128 synth_increment{0xc9c7353e6e2b1f28ull, 0x7d761f2d4027fae7ull};
constexpr voice::Word128 sfx_increment{0x720c0d9518f2c817ull, 0x4b950037b914e56bull};
constexpr voice::Word128 music_increment{0x85ca2d6245a06c18ull, 0x244bc974156e4bc9ull};

constexpr voice::GeneratorState synth_at(std::uint64_t high, std::uint64_t low) {
    const voice::GeneratorState state{{high, low}, synth_increment, false, 0u};
    return state;
}

constexpr voice::GeneratorState sfx_at(std::uint64_t high, std::uint64_t low) {
    const voice::GeneratorState state{{high, low}, sfx_increment, false, 0u};
    return state;
}

struct Start {
    voice::GeneratorState synth;
    voice::GeneratorState rng;
};

constexpr Start voice_starts[6] = {
    {synth_at(0x1905e0335aae9634ull, 0x9199b0d09775add5ull), sfx_at(0xc6fa5224383b0cb8ull, 0xb40c5859bfb5b061ull)},
    {synth_at(0xfff6e8fa8bab8f0aull, 0x63b8fba8aa8e2915ull), sfx_at(0x4367938c7eb649baull, 0x4bae4bb9908653a8ull)},
    {synth_at(0x2dbf0cd32bf4108dull, 0x56113ad8d97e7c9cull), sfx_at(0x2e4a0a0ceea71813ull, 0xa2fe112cf854c3beull)},
    {synth_at(0xb76dcdc7bb8a66fdull, 0x8ffe7d47ea76b0f3ull), sfx_at(0x8c23efaaccb89fafull, 0x276be20eda0da78cull)},
    {synth_at(0x3b865a01d7a62333ull, 0xcdb913452211b188ull), sfx_at(0xec3d7c337b4d52abull, 0x1d33d37bbad7f510ull)},
    {synth_at(0xa759c736aa2e38b4ull, 0xa27162378868ae55ull), sfx_at(0x1eb472d356995d6cull, 0xc1d241b712861160ull)},
};
constexpr voice::GeneratorState liar_start = synth_at(0x3f7ad2a0af9ab9a5ull, 0xfb52403901d61a51ull);
constexpr Start contradiction_start{synth_at(0x32849cf26bbcfad3ull, 0x8078fc70d05548eeull),
                                    sfx_at(0xe25d3c6f7f2af421ull, 0x79347ad653dd9914ull)};
constexpr Start ask_start{synth_at(0xa24bcb93e018f74aull, 0x2a7af435df87ead2ull),
                          sfx_at(0x8a4041d2024ec11cull, 0xf14894a5cfe7f5d8ull)};
constexpr voice::GeneratorState gavel_start = synth_at(0xcba74efd9612ad60ull, 0x28c16c13db6fef1bull);
constexpr voice::GeneratorState flap_start = synth_at(0x8f56011ac729bf3bull, 0x75b121397294bbc4ull);
constexpr Start cheer_start{synth_at(0x42a4b3201599cf6eull, 0x358d9c491aaae8ddull),
                            sfx_at(0x5d277c7eeb5629cdull, 0x94b0f8bf933be6beull)};

constexpr Start music_start{synth_at(0x1905e0335aae9634ull, 0x9199b0d09775add5ull),
                            {{0x8242d8efc51176c4ull, 0x543e78a8aba9ce37ull}, music_increment, false, 0u}};
constexpr voice::GeneratorState stinger_win_start = synth_at(0xb3d633a0f5492dddull, 0x2a20fd26df8dd3bfull);
constexpr voice::GeneratorState stinger_lose_start = synth_at(0x7da73076a0dacc31ull, 0xbcc48f1205e17749ull);

// ------------------------------------------------------------------ make_sfx.py

struct Vowel {
    double f1;
    double f2;
};

constexpr Vowel vowels[6] = {{900, 1500}, {600, 2100}, {520, 900}, {380, 2500}, {420, 1000}, {780, 1250}};

// syllable(f0, hook, vowel, dur, rasp): a buzzy pulse with a pitch hook, through two formants.
voice::Signal syllable(voice::Synth& synth, voice::Generator& rng, double f0, double hook, Vowel vowel, double dur,
                       double rasp = .25) {
    const std::size_t n = voice::samples(dur);
    const voice::Signal t = voice::tarr(n);
    voice::Signal z(n);
    for (double& value : z) {
        value = rng.normal(0, 1);
    }
    const voice::Signal walk = voice::cumulative_sum(z);
    const double root_n = std::sqrt(static_cast<double>(n));
    voice::Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double u = t[i] / dur;
        const double hooked = 1 + hook * std::sin(voice::pi * std::clamp(u * 1.3, 0.0, 1.0)) - .25 * u;
        const double drift = 1 + .03 * walk[i] / root_n;
        f[i] = f0 * hooked * drift;
    }
    const voice::Signal cs = voice::cumulative_sum(f);
    voice::Signal ph(n);
    voice::Signal src(n);
    for (std::size_t i = 0; i < n; ++i) {
        ph[i] = two_pi * cs[i] / voice::sample_rate;
        src[i] = (voice::remainder(ph[i] / two_pi, 1) < .3 ? 1.0 : 0.0) - .3;
    }
    const voice::Signal grit = voice::fft_filter(synth.noise(n), 1500, 6000);
    for (std::size_t i = 0; i < n; ++i) {
        src[i] += rasp * grit[i] * (1 + std::sin(ph[i]));
    }
    const voice::Signal first = voice::resonator(src, vowel.f1, 160);
    const voice::Signal second = voice::resonator(src, vowel.f2, 220);
    const voice::Signal third = voice::resonator(src, vowel.f2 * 1.5, 400);
    voice::Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double env = std::min(1.0, t[i] / .012) * std::exp(-std::max(0.0, t[i] - dur * .55) / (dur * .18));
        y[i] = first[i] * 1.0 + second[i] * .7 + third[i] * .25;
        y[i] = y[i] * env;
    }
    return y;
}

// babble: six vowels, varied pitch hooks
template <int I>
voice::Signal babble() {
    voice::Synth synth(voice_starts[I].synth);
    voice::Generator rng(voice_starts[I].rng);
    const voice::Signal y =
        syllable(synth, rng, 520 + 40 * (I % 3), .35 + .1 * (I % 2), vowels[I], .095 + .015 * (I % 3), .2 + .05 * I);
    const voice::Signal result = voice::master(voice::fft_filter(y, 250, 7000, 2), -16);
    return result;
}

// the host's bell: a small brass table bell
voice::Signal bell() {
    const std::size_t n = voice::samples(1.6);
    const voice::Signal t = voice::tarr(n);
    static constexpr double partials[4][3] = {{1320, 1, .7}, {3170, .5, .35}, {4900, .3, .2}, {2640, .25, .5}};
    voice::Signal x(n, 0.0);
    for (const double* p : partials) {
        const double w = two_pi * p[0];
        for (std::size_t i = 0; i < n; ++i) {
            x[i] += p[1] * std::sin(w * t[i]) * std::exp(-t[i] / p[2]);
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = x[i] * std::min(1.0, t[i] / .002);
    }
    const voice::Signal result = voice::master(x, -12);
    return result;
}

// marks: honest a bright tink up, liar a low wooden tonk, unmark a soft tick down
voice::Signal mark_honest() {
    const std::size_t n = voice::samples(.25);
    const voice::Signal t = voice::tarr(n);
    const double w1 = two_pi * 1760;
    const double w2 = two_pi * 2640;
    voice::Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = (std::sin(w1 * t[i]) + .4 * std::sin(w2 * t[i])) * std::exp(-t[i] / .07);
    }
    const voice::Signal result = voice::master(x, -16);
    return result;
}

voice::Signal mark_liar() {
    voice::Synth synth(liar_start);
    const std::size_t n = voice::samples(.25);
    const voice::Signal t = voice::tarr(n);
    const voice::Signal knock = voice::fft_filter(synth.noise(n), 400, 2000);
    voice::Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = (std::sin(two_pi * (330 + 60 * std::exp(-t[i] / .01)) * t[i]) + .3 * knock[i]) * std::exp(-t[i] / .06);
    }
    const voice::Signal result = voice::master(x, -14);
    return result;
}

voice::Signal unmark() {
    const std::size_t n = voice::samples(.08);
    const voice::Signal t = voice::tarr(n);
    voice::Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = std::sin(two_pi * (900 - 2000 * t[i]) * t[i]) * std::exp(-t[i] / .02);
    }
    const voice::Signal result = voice::master(x, -22);
    return result;
}

// a contradiction: a feather ruffle and a disapproving low "hmm"
voice::Signal contradiction() {
    voice::Synth synth(contradiction_start.synth);
    voice::Generator rng(contradiction_start.rng);
    const std::size_t n = voice::samples(.5);
    const voice::Signal t = voice::tarr(n);
    const voice::Signal feathers = voice::fft_filter(synth.noise(n), 2000, 8000);
    const double w = two_pi * 28;
    voice::Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = feathers[i] * (.5 + .5 * std::sin(w * t[i])) * std::exp(-t[i] / .1) * .6;
    }
    const voice::Signal hmm = syllable(synth, rng, 240, -.15, {350, 900}, .38, .05);
    const std::size_t s = voice::samples(.08);
    for (std::size_t i = 0; i < hmm.size() && s + i < n; ++i) {
        y[s + i] += hmm[i] * .8;
    }
    const voice::Signal result = voice::master(y, -14);
    return result;
}

// a question asked: a rising "hm?" chirp with a little pen scratch
voice::Signal ask() {
    voice::Synth synth(ask_start.synth);
    voice::Generator rng(ask_start.rng);
    const voice::Signal q = syllable(synth, rng, 560, .6, {600, 2000}, .16, .1);
    voice::Signal x = voice::fft_filter(synth.noise(voice::samples(.12)), 3000, 9000);
    for (double& value : x) {
        value = value * .3;
    }
    x.resize(x.size() + voice::samples(.03), 0.0);
    x.insert(x.end(), q.begin(), q.end());
    const voice::Signal result = voice::master(x, -15);
    return result;
}

// the gavel: two raps on wood
voice::Signal rap(voice::Synth& synth, std::size_t n) {
    const voice::Signal t = voice::tarr(n);
    const voice::Signal wood = voice::fft_filter(synth.noise(n), 800, 4000);
    const double w1 = two_pi * 220;
    const double w2 = two_pi * 610;
    voice::Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = std::sin(w1 * t[i]) * std::exp(-t[i] / .03) + std::sin(w2 * t[i]) * std::exp(-t[i] / .015) * .6 +
               wood[i] * std::exp(-t[i] / .008) * .6;
    }
    return y;
}

voice::Signal gavel() {
    voice::Synth synth(gavel_start);
    voice::Signal x = rap(synth, voice::samples(.18));
    for (double& value : x) {
        value = value * .7;
    }
    const voice::Signal second = rap(synth, voice::samples(.5));
    x.insert(x.end(), second.begin(), second.end());
    const voice::Signal result = voice::master(x, -9);
    return result;
}

// wings flapping off: a run of feathery whumps, receding
voice::Signal flap() {
    voice::Synth synth(flap_start);
    voice::Signal parts{};
    for (int k = 0; k < 9; ++k) {
        const std::size_t m = voice::samples(.09);
        const voice::Signal t = voice::tarr(m);
        const voice::Signal whump = voice::fft_filter(synth.noise(m), 300, 3000);
        const double fall = 1 - k / 11.0;
        for (std::size_t i = 0; i < m; ++i) {
            const double s = std::sin(voice::pi * t[i] / (.09));
            parts.push_back(whump[i] * (s * s) * fall);
        }
    }
    const voice::Signal result = voice::master(voice::fft_filter(parts, 200, 5000), -13);
    return result;
}

// the table cheers: a cluster of happy squawks
voice::Signal cheer() {
    voice::Synth synth(cheer_start.synth);
    voice::Generator rng(cheer_start.rng);
    const std::size_t n = voice::samples(1.1);
    voice::Signal y(n, 0.0);
    for (int k = 0; k < 10; ++k) {
        const double f0 = rng.uniform(500, 900);
        const double hook = rng.uniform(.4, .7);
        const Vowel vowel = vowels[rng.integers(0, 6)];
        const double dur = rng.uniform(.09, .16);
        const voice::Signal s = syllable(synth, rng, f0, hook, vowel, dur, .25);
        const std::size_t at = voice::samples(rng.uniform(0, .85));
        const std::size_t e = std::min(n, at + s.size());
        const double loudness = rng.uniform(.5, 1);
        for (std::size_t i = at; i < e; ++i) {
            y[i] += s[i - at] * loudness;
        }
    }
    const voice::Signal result = voice::master(voice::fft_filter(y, 250, 7000), -12);
    return result;
}

voice::Signal decaying_tone(double seconds, double frequency, double decay, double peak_db) {
    const std::size_t n = voice::samples(seconds);
    const voice::Signal t = voice::tarr(n);
    const double w = two_pi * frequency;
    voice::Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = std::sin(w * t[i]) * std::exp(-t[i] / decay);
    }
    const voice::Signal result = voice::master(x, peak_db);
    return result;
}

// a tick and a click
voice::Signal tick() {
    const voice::Signal result = decaying_tone(.05, 2400, .008, -20);
    return result;
}

voice::Signal click() {
    const voice::Signal result = decaying_tone(.04, 1800, .006, -22);
    return result;
}

// ------------------------------------------------------------------ parlor_music.py: "Tea for Seven"

constexpr int major[7] = {0, 2, 4, 5, 7, 9, 11};
constexpr int tonic = 62;  // D
constexpr int bpm = 126;

int deg(int d, int base = tonic) {
    const long long step = voice::python_modulo(d, 7);
    const int octave = static_cast<int>((d - step) / 7);  // d // 7
    const int result = base + major[step] + 12 * octave;
    return result;
}

struct Step {
    int degree;  // rest_step for a rest
    double beats;
};
constexpr int rest_step = -100;
using Bar = std::vector<Step>;

const std::vector<Bar>& a_tune() {
    static const std::vector<Bar> tune = {
        {{2, 1}, {4, 1}, {7, 1}},          {{6, 2}, {4, 1}},          {{5, 1}, {4, .5}, {2, .5}, {0, 1}},
        {{1, 2}, {rest_step, 1}},          {{3, 1}, {5, 1}, {8, 1}},  {{7, 1.5}, {6, .5}, {4, 1}},
        {{5, 1}, {3, 1}, {1, 1}},          {{4, 2}, {rest_step, 1}},
    };
    return tune;
}

std::vector<Bar> make_a2_tune() {
    std::vector<Bar> tune(a_tune().begin(), a_tune().begin() + 6);
    tune.push_back({{5, .5}, {4, .5}, {3, 1}, {1, 1}});
    tune.push_back({{0, 2}, {rest_step, 1}});
    return tune;
}

const std::vector<Bar>& a2_tune() {
    static const std::vector<Bar> tune = make_a2_tune();
    return tune;
}

const std::vector<Bar>& b_tune() {
    static const std::vector<Bar> tune = {
        {{5, .5}, {rest_step, .5}, {4, .5}, {rest_step, .5}, {5, 1}},
        {{4, .5}, {rest_step, .5}, {3, .5}, {rest_step, .5}, {2, 1}},
        {{5, .5}, {rest_step, .5}, {7, .5}, {rest_step, .5}, {9, 1}},
        {{8, 2}, {rest_step, 1}},
        {{3, .5}, {rest_step, .5}, {5, .5}, {rest_step, .5}, {8, 1}},
        {{7, .5}, {6, .5}, {5, 1}, {3, 1}},
        {{4, 1}, {6, 1}, {8, 1}},
        {{7, 1}, {6, 1}, {5, 1}},
    };
    return tune;
}

constexpr int a_chords[8] = {0, 4, 0, 4, 3, 0, 1, 4};
constexpr int a2_chords[8] = {0, 4, 0, 5, 3, 0, 4, 0};
constexpr int b_chords[8] = {5, 2, 5, 2, 3, 1, 4, 4};
constexpr int counter_line[8] = {0, -1, 0, -2, 1, 0, -3, -1};  // each (d, 3)

struct Level {
    const char* bus;
    double db;
    double send;
};
// LEVELS and master()'s send amounts
constexpr Level levels[] = {{"lead", -16, .35},   {"flute", -18, .35}, {"counter", -22, .4}, {"chords", -22, .3},
                            {"bass", -18, .08},   {"sparkle", -26, .5}, {"pad", -27, .45},    {"drums", -28, .1}};

const Level& level_of(const std::string& name) {
    for (const Level& l : levels) {
        if (name == l.bus) {
            return l;
        }
    }
    static constexpr Level other{"", -20, .1};
    return other;
}

// bus_rms_db(b): the RMS of the channels' mean, in dB
double bus_rms_db(const voice::Stereo& b) {
    const std::size_t n = b.left.size();
    voice::Signal squares(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double m = (b.left[i] + b.right[i]) / 2;
        squares[i] = m * m;
    }
    const double r = std::sqrt(voice::sum(squares) / static_cast<double>(n));
    const double result = 20 * std::log10(std::max(r, 1e-9));
    return result;
}

// finish(x, target_rms_db, ceiling_db) and write()'s clip
voice::Stereo finish(voice::Stereo x, double target_rms_db, double ceiling_db) {
    const double g = std::pow(10.0, (target_rms_db - bus_rms_db(x)) / 20);
    const double c = std::pow(10.0, ceiling_db / 20);
    for (voice::Signal* channel : {&x.left, &x.right}) {
        for (double& value : *channel) {
            value = value * g;
            value = std::tanh(value / c) * c;
            value = std::clamp(value, -1.0, 1.0);
        }
    }
    return x;
}

// at(bar, beat) of arrange(): a hint of Viennese lilt, the second beat a touch early
long long at(int spb, int bar, double beat) {
    const double lilt = voice::remainder(beat, 3) == 1 ? -.06 * spb : 0;
    const long long result = static_cast<long long>(voice::python_round((bar * 3 + beat) * spb + lilt));
    return result;
}

voice::Stereo music() {
    voice::Synth synth(music_start.synth);
    voice::Generator rng(music_start.rng);
    const int spb = static_cast<int>(voice::python_round(voice::sample_rate * 60.0 / bpm));
    const int bars = 32;
    const std::size_t length = static_cast<std::size_t>(bars) * 3 * static_cast<std::size_t>(spb);
    voice::LoopMix mix(length, true);
    const double sr = voice::sample_rate;
    for (int bar = 0; bar < bars; ++bar) {
        const int part = bar / 8;  // A, A2, B, A3
        const int i = bar % 8;
        const int* chords = part == 0 ? a_chords : part == 2 ? b_chords : a2_chords;
        const Bar& tune = part == 0 ? a_tune()[i] : part == 2 ? b_tune()[i] : a2_tune()[i];
        const int root = chords[i];
        const int triad[3] = {root, root + 2, root + 4};
        const bool b_part = part == 2;
        const char* lead_inst = b_part ? "flute" : "clarinet";
        const char* bus = b_part ? "flute" : "lead";
        double beat = 0.0;
        for (const Step& s : tune) {
            if (s.degree != rest_step) {
                const double dur = s.beats * spb / sr * (b_part && s.beats <= .5 ? .55 : .9);
                const double vel = .7 + rng.uniform(-.05, .05);
                mix.add(bus, synth.render_note(lead_inst, deg(s.degree), dur, vel), at(spb, bar, beat), .1);
            }
            beat += s.beats;
        }
        // the last A gets a celesta doubling an octave up, and a clarinet counter-line
        if (part == 3) {
            beat = 0.0;
            for (const Step& s : tune) {
                if (s.degree != rest_step) {
                    mix.add("sparkle", synth.render_note("celesta", deg(s.degree) + 12, s.beats * spb / sr * .8, .45),
                            at(spb, bar, beat), -.4);
                }
                beat += s.beats;
            }
            mix.add("counter", synth.render_note("clarinet", deg(counter_line[i]), 3 * spb / sr * .95, .45), at(spb, bar, 0),
                    -.35);
        }
        if (part == 1) {
            mix.add("counter", synth.render_note("flute", deg(counter_line[i]) + 12, 3 * spb / sr * .9, .4), at(spb, bar, 0),
                    -.35);
        }
        // oom-pah-pah: pizz root on one, harp chords on two and three
        const int r = deg(root, tonic - 24);
        mix.add("bass", synth.render_note("pizz", r, .8 * spb / sr, .85), at(spb, bar, 0), -.1);
        if (bar % 2 == 1) {
            mix.add("bass", synth.render_note("pizz", deg(root + 4, tonic - 24), .5 * spb / sr, .5), at(spb, bar, 2.5), -.1);
        }
        int chord[3]{};
        for (int k = 0; k < 3; ++k) {
            chord[k] = deg(triad[k], tonic - 12);
        }
        for (const int b2 : {1, 2}) {
            for (int k = 0; k < 3; ++k) {
                mix.add("chords", synth.render_note("harp", chord[k], .5 * spb / sr, b2 == 1 ? .5 : .42),
                        at(spb, bar, b2) + k * 60, .3 - .15 * k);
            }
        }
        // sparkles at phrase ends
        if (i % 4 == 3) {
            for (int k = 0; k < 3; ++k) {
                mix.add("sparkle", synth.render_note("celesta", chord[k] + 24, .6, .35),
                        at(spb, bar, 1.5) + k * static_cast<long long>(voice::samples(.08)), .5 - .3 * k);
            }
        }
        // pad: soft sustain under the A sections; thinner in B
        for (int k = 0; k < 3; ++k) {
            mix.add("pad", synth.render_note("pad", chord[k], 3 * spb / sr, b_part ? .22 : .35), at(spb, bar, 0),
                    (k - 1) * .5);
        }
        // the faintest brush and triangle, keeping time
        mix.add("drums", synth.render_drum("tap", .25, bar), at(spb, bar, 0), .2);
        if (bar % 4 == 0) {
            mix.add("drums", synth.render_drum("triangle", .22, bar), at(spb, bar, 0), -.35);
        }
        if (b_part) {
            mix.add("drums", synth.render_drum("wbhi", .25, bar), at(spb, bar, 1), -.3);
            mix.add("drums", synth.render_drum("wblo", .25, bar), at(spb, bar, 2), -.3);
        }
    }
    // master(mix, L, wet=.28)
    voice::Stereo out{voice::Signal(length, 0.0), voice::Signal(length, 0.0)};
    voice::Stereo send{voice::Signal(length, 0.0), voice::Signal(length, 0.0)};
    for (const std::pair<std::string, voice::Stereo>& entry : mix.buses()) {
        const voice::Stereo& b = entry.second;
        const Level& l = level_of(entry.first);
        const double g = std::pow(10.0, (l.db - bus_rms_db(b)) / 20);
        for (std::size_t j = 0; j < length; ++j) {
            const double left = b.left[j] * g;
            const double right = b.right[j] * g;
            out.left[j] += left;
            out.right[j] += right;
            send.left[j] += left * l.send;
            send.right[j] += right * l.send;
        }
    }
    const voice::Stereo ir = voice::make_ir(2.0, 1.5, 1.3, .7);
    const voice::Stereo wet = voice::conv_reverb(send, ir, true);
    for (std::size_t j = 0; j < length; ++j) {
        out.left[j] += wet.left[j] * .28;
        out.right[j] += wet.right[j] * .28;
    }
    out.left = voice::fft_filter(out.left, 40, 0, 2, 4096, true);
    out.right = voice::fft_filter(out.right, 40, 0, 2, 4096, true);
    voice::Stereo result = finish(std::move(out), -20.0, -3.0);
    return result;
}

// stinger(kind): put() places a note by beats at the stingers' unrounded beat length
class Stinger final {
  public:
    explicit Stinger(const voice::GeneratorState& state) : synth(state) {}
    voice::Synth synth;
    voice::Stereo out{voice::Signal(n, 0.0), voice::Signal(n, 0.0)};

    void put(std::span<const double> y, double beat, double pan = 0.0, double g = 1.0) {
        const std::size_t s = static_cast<std::size_t>(beat * spb);
        const std::size_t e = std::min(n, s + y.size());
        const double gl = std::cos((pan + 1) * voice::pi / 4);
        const double gr = std::sin((pan + 1) * voice::pi / 4);
        for (std::size_t i = s; i < e; ++i) {
            out.left[i] += gl * g * y[i - s];
            out.right[i] += gr * g * y[i - s];
        }
    }
    const voice::Signal& note(const char* inst, int m, double dur, double vel) {
        return synth.render_note(inst, m, dur, vel);
    }
    voice::Stereo finish_stinger() {
        voice::Stereo send{out.left, out.right};
        for (voice::Signal* channel : {&send.left, &send.right}) {
            for (double& value : *channel) {
                value = value * .3;
            }
        }
        const voice::Stereo ir = voice::make_ir(2.0);
        const voice::Stereo wet = voice::conv_reverb(send, ir, false);
        for (std::size_t i = 0; i < n; ++i) {
            out.left[i] = out.left[i] + wet.left[i] * .3;
            out.right[i] = out.right[i] + wet.right[i] * .3;
        }
        const std::size_t fade = voice::samples(.4);
        const voice::Signal down = voice::linspace(1, 0, fade);
        for (std::size_t i = 0; i < fade; ++i) {
            out.left[n - fade + i] *= down[i];
            out.right[n - fade + i] *= down[i];
        }
        voice::Stereo result = finish(std::move(out), -18, -2.5);
        return result;
    }

  private:
    static inline const std::size_t n = voice::samples(3.4);
    static constexpr double spb = voice::sample_rate * 60.0 / bpm;
};

// case closed: a run up the arpeggio, then a full cadence with a celesta flourish
voice::Stereo stinger_win() {
    Stinger s(stinger_win_start);
    const int run[5] = {0, 2, 4, 7, 9};
    for (int k = 0; k < 5; ++k) {
        s.put(s.note("clarinet", deg(run[k]), .3, .75), k * .4, .2);
    }
    for (const int m : {deg(-7) - 12, deg(0) - 12, deg(2) - 12, deg(4) - 12, deg(7)}) {
        s.put(s.note("harp", m, 2.0, .7), 2.0, 0, .7);
    }
    s.put(s.note("flute", deg(11), 1.4, .8), 2.0, -.1);
    const int flourish[4] = {7, 9, 11, 14};
    for (int k = 0; k < 4; ++k) {
        s.put(s.note("celesta", deg(flourish[k]) + 12, .5, .5), 2.0 + k * .25, .4);
    }
    s.put(s.synth.render_drum("triangle", .5, 1), 2.0, -.3);
    voice::Stereo result = s.finish_stinger();
    return result;
}

// the wrong bird: a sagging clarinet, a sour little harp chord, and wings flapping away
voice::Stereo stinger_lose() {
    Stinger s(stinger_lose_start);
    const std::pair<int, double> sag[3] = {{deg(4), .5}, {deg(3), .5}, {deg(2) - 1, 1.6}};
    for (int k = 0; k < 3; ++k) {
        s.put(s.note("clarinet", sag[k].first, sag[k].second, .75), k * .7, 0);
    }
    for (const int m : {deg(-1) - 12, deg(1) - 12, deg(3) - 12 - 1}) {
        s.put(s.note("harp", m, 1.6, .55), 1.4, .2, .7);
    }
    s.put(s.note("pizz", deg(0) - 24, .8, .8), 1.4);
    voice::Stereo result = s.finish_stinger();
    return result;
}

constexpr voice::Cue cues[] = {
    {.name = "pt_voice_01", .mono = babble<0>},
    {.name = "pt_voice_02", .mono = babble<1>},
    {.name = "pt_voice_03", .mono = babble<2>},
    {.name = "pt_voice_04", .mono = babble<3>},
    {.name = "pt_voice_05", .mono = babble<4>},
    {.name = "pt_voice_06", .mono = babble<5>},
    {.name = "pt_bell", .mono = bell},
    {.name = "pt_mark_honest", .mono = mark_honest},
    {.name = "pt_mark_liar", .mono = mark_liar},
    {.name = "pt_unmark", .mono = unmark},
    {.name = "pt_contradiction", .mono = contradiction},
    {.name = "pt_ask", .mono = ask},
    {.name = "pt_gavel", .mono = gavel},
    {.name = "pt_flap", .mono = flap},
    {.name = "pt_cheer", .mono = cheer},
    {.name = "pt_tick", .mono = tick},
    {.name = "pt_click", .mono = click},
    {.name = "pt_music", .stereo = music},
    {.name = "pt_stinger_win", .stereo = stinger_win},
    {.name = "pt_stinger_lose", .stereo = stinger_lose},
};

}  // namespace

std::span<const voice::Cue> sound_cues() {
    const std::span<const voice::Cue> result(cues);
    return result;
}

void register_sound_cues() {
    voice::Bank::shared().add(cues);
}

}  // namespace pt
