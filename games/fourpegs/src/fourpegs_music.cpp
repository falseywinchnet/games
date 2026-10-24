#include "fourpegs_cues.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// audio_src/villain_music.py, "The Mastermind's Console": one lair theme in D minor,
// arranged in three tiers that share its 16-bar form (played twice per loop), and three
// stingers, ported line for line in numpy's order of operations. The script draws from
// synth._rng (seeded 12345: plucks, basses, kicks, hats, noise bursts) and its own rng
// (seeded 1966: every additive partial's phase, and a timing jitter on every note), so
// each note is synthesised before its start time is drawn, as Python evaluates
// mix.add's arguments. The states are what shared/voice/tools/score_reference.py printed.
namespace fp {
namespace {

using voice::Signal;
using voice::Stereo;

constexpr double two_pi = 2 * voice::pi;
constexpr double sr = voice::sample_rate;

constexpr voice::Word128 synth_increment{0xc9c7353e6e2b1f28ull, 0x7d761f2d4027fae7ull};
constexpr voice::Word128 script_increment{0x112d415d13b0a9d2ull, 0x84bbd8077f0b6f2full};  // default_rng(1966)

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

constexpr Start tier_starts[3] = {
    {synth_at(0x1905e0335aae9634ull, 0x9199b0d09775add5ull), script_at(0xf367834c21cbb628ull, 0x7dbb54a923278c33ull)},
    {synth_at(0x34d5c807990f3078ull, 0x1b9a58a2a4ffceedull), script_at(0xcebd4455d78281baull, 0xafeb2319829a3aa2ull)},
    {synth_at(0xb6e49356c1012225ull, 0xe0fd028eed813f04ull), script_at(0x2e793c160a3c127eull, 0x280f024e6d1e1771ull)},
};
constexpr Start defeat_start{synth_at(0x34cbdae59bcd7f63ull, 0xb931aea4851e15eeull),
                             script_at(0x24747a723dabf3e9ull, 0xddaa0835f051a286ull)};
constexpr Start triumph_start{synth_at(0xca44ca5acd5d5410ull, 0xd396e5da8442243bull),
                              script_at(0xce970ef889bb15a8ull, 0xcf9fd939477fdb6cull)};
constexpr Start topscore_start{synth_at(0x3db8d90bbec144fcull, 0xf794c4b521ccbe64ull),
                               script_at(0x6f1a84bd184a59f1ull, 0x866b9c8bc7ec479full)};

// One run of the script: its two generators.
struct Score {
    voice::Synth synth;
    voice::Generator rng;
};

// additive(f_t, n, amp_k, K): harmonics of a moving pitch, each at a random phase. A
// harmonic whose amplitude never reaches 1e-4 is skipped (and draws no phase).
// `amp_k(k, a)` fills a with the k-th harmonic's amplitude; `constant` says it is one
// value throughout (np.full), so only a[0] is read.
void additive(Score& s, Signal& y, const Signal& f_t, std::size_t n, int harmonics,
              const std::function<void(int, Signal&)>& amp_k, bool constant) {
    const Signal cs = voice::cumulative_sum(f_t);
    Signal ph(n);
    for (std::size_t i = 0; i < n; ++i) {
        ph[i] = two_pi * cs[i] / sr;
    }
    Signal a(constant ? 1 : n);
    for (int k = 1; k <= harmonics; ++k) {
        amp_k(k, a);
        double top = 0;
        for (const double value : a) {
            top = std::max(top, std::fabs(value));
        }
        if (n == 0 || top < 1e-4) {
            continue;
        }
        const double phase = s.rng.uniform(0, 6.28);
        for (std::size_t i = 0; i < n; ++i) {
            y[i] = y[i] + a[constant ? 0 : i] * std::sin(k * ph[i] + phase);
        }
    }
}

Signal harpsichord(Score& s, double m, double dur, double vel = .7) {
    const Signal y = s.synth.pluck(m, std::min(dur, .5), vel, .08, .55, 2.2, true);
    const Signal y2 = s.synth.pluck(m + 12, std::min(dur, .4), vel * .4, .11, .45, 2.0, false);
    Signal out(std::max(y.size(), y2.size()), 0.0);
    for (std::size_t i = 0; i < y.size(); ++i) {
        out[i] += y[i];
    }
    for (std::size_t i = 0; i < y2.size(); ++i) {
        out[i] += .5 * y2[i];
    }
    for (double& value : out) {
        value = value * .9;
    }
    return out;
}

Signal organ(double m, double dur, double vel = .6) {
    const double f = voice::mtof(m);
    const double rel = .25;
    const std::size_t n = voice::samples(dur + rel);
    const Signal t = voice::tarr(n);
    const double w = two_pi * 6.2;
    Signal lesl(n);
    for (std::size_t i = 0; i < n; ++i) {
        lesl[i] = 1 + .004 * std::sin(w * t[i]);
    }
    const Signal cs = voice::cumulative_sum(lesl);
    static constexpr double bars[6][2] = {{1, 1}, {2, .7}, {3, .45}, {4, .5}, {6, .25}, {8, .3}};
    Signal y(n, 0.0);
    for (const double* bar : bars) {
        const int k = static_cast<int>(bar[0]);
        if (k * f < 9000) {
            const double wk = two_pi * k * f;
            for (std::size_t i = 0; i < n; ++i) {
                y[i] = y[i] + bar[1] * std::sin(wk * cs[i] / sr);
            }
        }
    }
    const double w58 = two_pi * 5.8;
    const Signal g = voice::gate(n, dur, .05, rel);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * (1 + .12 * std::sin(w58 * t[i]));
        y[i] = y[i] * g[i] * vel * .35;
    }
    return y;
}

Signal brass(Score& s, double m, double dur, double vel = .7, bool stab = false) {
    const double f = voice::mtof(m);
    const double rel = stab ? .08 : .18;
    const std::size_t n = voice::samples(dur + rel);
    const Signal t = voice::tarr(n);
    const double att = stab ? .03 : .06;
    const double w = two_pi * 5.5;
    Signal bright(n);
    Signal f_t(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double env = std::min(1.0, t[i] / att);
        bright[i] = (900 + 2600 * vel) * (.55 + .45 * env) * (1 + .3 * std::exp(-t[i] / .12));
        const double vib = 1 + .005 * std::sin(w * t[i]) * std::min(1.0, t[i] / .35);
        f_t[i] = f * vib * (1 - .015 * std::exp(-t[i] / .04));
    }
    const int harmonics = static_cast<int>(std::min(30.0, 7000 / f));
    Signal y(n, 0.0);
    additive(
        s, y, f_t, n, harmonics,
        [&](int k, Signal& a) {
            const double top = -(k * f);
            const double fall = std::pow(static_cast<double>(k), .6);
            for (std::size_t i = 0; i < n; ++i) {
                a[i] = std::exp(top / bright[i]) / fall;
            }
        },
        false);
    const Signal breath = voice::fft_filter(s.synth.noise(n), 800, 3500);
    const Signal g = voice::gate(n, dur, att * .5, rel);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] + .06 * breath[i] * std::exp(-t[i] / .05);
        y[i] = y[i] * g[i] * (.3 + .6 * vel) * .4;
    }
    return y;
}

// detuned additive voices (strings and choir): f * 2^(det/1200) with a vibrato
Signal ensemble(Score& s, double f, std::size_t n, const Signal& t, std::span<const int> detunes, double vib_depth,
                double vib_rate, int harmonics, const std::function<double(int)>& amplitude) {
    Signal y(n, 0.0);
    for (const int det : detunes) {
        const double w = two_pi * (vib_rate + det * .05);
        const double base = f * std::pow(2.0, det / 1200.0);
        Signal f_t(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double vib = 1 + vib_depth * std::sin(w * t[i] + det);
            f_t[i] = base * vib;
        }
        Signal part(n, 0.0);
        additive(
            s, part, f_t, n, harmonics, [&](int k, Signal& a) { a[0] = amplitude(k); }, true);
        for (std::size_t i = 0; i < n; ++i) {
            y[i] = y[i] + part[i];
        }
    }
    return y;
}

Signal strings(Score& s, double m, double dur, double vel = .6, bool stacc = false) {
    const double f = voice::mtof(m);
    const double rel = stacc ? .07 : .3;
    const std::size_t n = voice::samples(dur + rel);
    const Signal t = voice::tarr(n);
    const int harmonics = static_cast<int>(std::min(24.0, 8000 / f));
    static constexpr int detunes[3] = {-6, 0, 7};
    const Signal y = ensemble(s, f, n, t, detunes, .004, 5.2, harmonics, [&](int k) {
        const double result = 1 / std::pow(static_cast<double>(k), 1.1) * std::exp(-k * f / 4500);
        return result;
    });
    const double att = stacc ? .012 : .12;
    const Signal bow = voice::fft_filter(s.synth.noise(n), 1500, 6000);
    const Signal g = voice::gate(n, dur, att, rel);
    Signal out(n);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = (y[i] / 3 + bow[i] * std::exp(-t[i] / .04) * .08) * g[i] * vel * .32;
    }
    return out;
}

Signal timpani(Score& s, double m, double vel = .8) {
    const double f = voice::mtof(m);
    const std::size_t n = voice::samples(1.6);
    const Signal t = voice::tarr(n);
    static constexpr double modes[4][3] = {{1, 1, .9}, {1.5, .5, .5}, {1.99, .35, .4}, {2.44, .2, .3}};
    Signal y(n, 0.0);
    for (const double* mode : modes) {
        const double w = two_pi * f * mode[0];
        for (std::size_t i = 0; i < n; ++i) {
            y[i] = y[i] + mode[1] * std::exp(-t[i] / mode[2]) * std::sin(w * t[i] * (1 + .01 * std::exp(-t[i] / .05)));
        }
    }
    const Signal skin = voice::fft_filter(s.synth.noise(n), 60, 800);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] + .5 * skin[i] * std::exp(-t[i] / .03);
        y[i] = y[i] * vel * .5;
    }
    return y;
}

Signal snare(Score& s, double vel = .7) {
    const std::size_t n = voice::samples(.25);
    const Signal t = voice::tarr(n);
    const Signal nz = voice::fft_filter(s.synth.noise(n), 1500, 9000);
    const double w = two_pi * 185;
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double body = std::sin(w * t[i]) * std::exp(-t[i] / .05);
        y[i] = (body * .6 + nz[i] * std::exp(-t[i] / .07)) * vel * .5;
    }
    return y;
}

Signal tom(double m, double vel = .7) {
    const std::size_t n = voice::samples(.4);
    const Signal t = voice::tarr(n);
    const double base = voice::mtof(m);
    Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        f[i] = base * (1 + .3 * std::exp(-t[i] / .03));
    }
    const Signal cs = voice::cumulative_sum(f);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = std::sin(two_pi * cs[i] / sr) * std::exp(-t[i] / .18) * vel * .6;
    }
    return y;
}

Signal choir(Score& s, double m, double dur, double vel = .5) {
    const double f = voice::mtof(m);
    const double rel = .5;
    const std::size_t n = voice::samples(dur + rel);
    const Signal t = voice::tarr(n);
    static constexpr double form[3][3] = {{700, 1, 110}, {1150, .5, 120}, {2700, .25, 180}};  // "ah"
    static constexpr int detunes[3] = {-9, 0, 8};
    const int harmonics = static_cast<int>(std::min(28.0, 4500 / f));
    const Signal y = ensemble(s, f, n, t, detunes, .006, 5, harmonics, [&](int k) {
        double sum = 0;
        for (const double* formant : form) {
            const double z = (k * f - formant[0]) / formant[2];
            sum = sum + formant[1] * std::exp(-std::pow(z, 2.0) / 8);
        }
        const double result = sum + .02 / k;
        return result;
    });
    const Signal g = voice::gate(n, dur, .25, rel);
    Signal out(n);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = y[i] / 3 * g[i] * vel * .5;
    }
    return out;
}

// alarm(m1, m2, dur): a two-tone klaxon, half the duration on each note.
Signal alarm(Score& s, double m1, double m2, double dur, double vel = .35) {
    const std::size_t n = voice::samples(dur);
    const Signal t = voice::tarr(n);
    Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        f[i] = t[i] < dur / 2 ? voice::mtof(m1) : voice::mtof(m2);
    }
    f = voice::fft_filter(f, 0, 30);  // soften the step
    Signal y(n, 0.0);
    additive(
        s, y, f, n, 9, [](int k, Signal& a) { a[0] = k % 2 ? 1.0 / k : 0.0; }, true);
    const Signal g = voice::gate(n, dur - .02, .01, .02);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * g[i] * vel * .3;
    }
    return y;
}

Signal pizz_bass(Score& s, double m, double dur, double vel = .7) {
    Signal y = s.synth.bass(m, std::min(dur, .35), vel);
    for (double& value : y) {
        value = value * .9;
    }
    return y;
}

// ------------------------------------------------------------------ the form
struct ChordBar {
    int root;
    std::vector<int> tones;
};

const std::vector<ChordBar>& chords() {
    static const std::vector<ChordBar> c = {
        {50, {0, 3, 7}},  {50, {0, 3, 8}},  {55, {0, 3, 7}},     {57, {0, 4, 7, 10}},
        {50, {0, 3, 7}},  {53, {0, 4, 7}},  {46, {0, 4, 7}},     {57, {0, 5, 7, 10}},
        {55, {0, 3, 7}},  {50, {0, 3, 7}},  {52, {0, 3, 6, 10}}, {57, {0, 4, 7, 10, 13}},
        {50, {0, 3, 7}},  {46, {0, 4, 7}},  {55, {0, 3, 7, 9}},  {57, {0, 4, 7, 10}},
    };
    return c;
}

struct MelodyNote {
    std::optional<int> midi;
    double beats;
};

const std::vector<std::vector<MelodyNote>>& melody() {
    static const std::vector<std::vector<MelodyNote>> mel = {
        {{74, 1}, {77, .5}, {76, .5}, {74, 1}, {69, 1}},
        {{70, 1.5}, {69, .5}, {68, 1}, {69, 1}},
        {{70, .5}, {72, .5}, {74, 1}, {79, 1}, {77, .5}, {76, .5}},
        {{76, 2}, {73, 1}, {69, 1}},
        {{74, 1}, {77, .5}, {81, .5}, {80, 1}, {81, 1}},
        {{77, 1.5}, {76, .5}, {74, 1}, {72, 1}},
        {{70, 1}, {74, 1}, {79, 1}, {77, .5}, {74, .5}},
        {{76, 3}, {std::nullopt, 1}},
        {{79, 1.5}, {77, .5}, {76, 1}, {74, 1}},
        {{77, 1.5}, {76, .5}, {74, 1}, {69, 1}},
        {{70, 1}, {79, 1}, {76, 1}, {74, .5}, {73, .5}},
        {{69, .5}, {70, .5}, {73, .5}, {76, .5}, {79, 1}, {82, 1}},
        {{81, 1.5}, {77, .5}, {74, 1}, {69, 1}},
        {{70, 1}, {74, 1}, {77, 1}, {74, 1}},
        {{76, 1}, {79, 1}, {82, 1}, {79, 1}},
        {{81, 2}, {73, 1}, {76, .5}, {79, .5}},
    };
    return mel;
}

double rms_db(const Stereo& b) {
    const std::size_t n = b.left.size();
    Signal squares(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double m = (b.left[i] + b.right[i]) / 2;
        squares[i] = m * m;
    }
    const double r = std::sqrt(voice::sum(squares) / static_cast<double>(n));
    const double result = 20 * std::log10(std::max(r, 1e-9));
    return result;
}

// finish(x, target_rms_db, ceiling_db): level to a loudness, then a gentle soft limit.
Stereo finish(Stereo x, double target_rms_db = -19.0, double ceiling_db = -3.0) {
    const double g = std::pow(10.0, (target_rms_db - rms_db(x)) / 20);
    const double c = std::pow(10.0, ceiling_db / 20);
    for (Signal* channel : {&x.left, &x.right}) {
        for (double& value : *channel) {
            value = value * g;
            value = std::tanh(value / c) * c;
        }
    }
    return x;
}

double level(int tier, const std::string& name) {
    struct Level {
        const char* name;
        double db;
    };
    static const std::vector<Level> levels[3] = {
        {{"bass", -15}, {"harp", -19}, {"lead", -15}, {"organ", -22}, {"drums", -20}},
        {{"bass", -14}, {"harp", -21}, {"lead", -17}, {"organ", -22}, {"drums", -17}, {"strings", -17},
         {"brass", -17}, {"tick", -24}, {"timp", -18}},
        {{"bass", -13}, {"lead", -16}, {"organ", -24}, {"drums", -15}, {"strings", -16}, {"brass", -14},
         {"tick", -23}, {"timp", -16}, {"choir", -21}, {"alarm", -27}},
    };
    for (const Level& l : levels[tier - 1]) {
        if (name == l.name) {
            return l.db;
        }
    }
    return -20;
}

// arrange(tier), master(mix, LEVELS[tier]) and finish(): one tier's loop.
Stereo tier_loop(int tier) {
    Score s{voice::Synth(tier_starts[tier - 1].synth), voice::Generator(tier_starts[tier - 1].rng)};
    const int bpm = tier == 1 ? 116 : tier == 2 ? 128 : 144;
    const double spb = sr * 60 / bpm;
    const int bars = 32;  // the form twice: the second pass hands the theme to other voices
    const std::size_t length = static_cast<std::size_t>(voice::python_round(bars * 4 * spb));
    std::optional<voice::LoopMix> mix(std::in_place, length, true);
    // at(beat): the beat's sample with a little timing jitter, drawn after the note
    const auto at = [&](double beat) {
        const double jitter = s.rng.normal(0, .002);
        const long long result = static_cast<long long>(voice::python_round(beat * spb + jitter * sr));
        return result;
    };
    const auto sec = [&](double beats) {
        const double result = beats * spb / sr;
        return result;
    };
    const auto add = [&](const std::string& bus, const Signal& y, double beat, double pan = 0.0) {
        const long long start = at(beat);
        mix->add(bus, y, start, pan);
    };
    const std::vector<ChordBar>& form = chords();
    for (int bi = 0; bi < bars; ++bi) {
        const ChordBar& chord = form[static_cast<std::size_t>(bi % 16)];
        const int root = chord.root;
        const std::vector<int>& tones = chord.tones;
        const double b0 = bi * 4;
        // ---- bass: a sneaky pizzicato walk; driving eighths later
        static constexpr int walk[8] = {0, 0, 7, 8, 7, 0, 12, 11};
        static constexpr int drive[8] = {0, 0, 12, 0, 7, 0, 10, 12};
        const int bass_root = root >= 50 ? root - 12 : root;
        for (int k = 0; k < 8; ++k) {
            if (tier == 1 && (k == 1 || k == 6) && bi % 2) {
                continue;
            }
            const int iv = tier == 1 ? walk[k] : drive[k];
            const Signal y = pizz_bass(s, bass_root + iv, sec(.45), k % 2 == 0 ? .75 : .55);
            add("bass", y, b0 + k * .5, 0);
        }
        // ---- harpsichord: arpeggiated chord tones (tier 1 and 2)
        if (tier <= 2) {
            std::vector<int> notes{};
            for (const int t : tones) {
                notes.push_back(root + 12 + t);
            }
            const int order[8] = {0, 1, 2, 1, notes.size() > 3 ? 3 : 2, 2, 1, 2};
            for (int k = 0; k < 8; ++k) {
                const int m = notes[static_cast<std::size_t>(order[k]) % notes.size()];
                const Signal y = harpsichord(s, m, sec(.4), .5 + (k % 2 == 0 ? .15 : 0.0));
                add("harp", y, b0 + k * .5, k % 2 ? .35 : -.15);
            }
        }
        // ---- strings: staccato ostinato (tier 2 eighths, tier 3 sixteenths)
        if (tier >= 2) {
            const double step = tier == 2 ? .5 : .25;
            const int notes[3] = {root + tones[0], root + tones[1], root + tones[2]};
            static constexpr int pick[4] = {0, 1, 2, 1};
            for (int k = 0; k < static_cast<int>(4 / step); ++k) {
                const int m = notes[pick[k % 4]] + (tier == 3 && k % 8 >= 4 ? 12 : 0);
                const Signal y = strings(s, m, sec(step * .6), .55 + (k % 2 == 0 ? .1 : 0.0), true);
                add("strings", y, b0 + k * step, -.3);
            }
        }
        // ---- organ swells at phrase ends; continuous in tier 3
        if (bi % 4 == 3 || tier == 3) {
            for (std::size_t j = 0; j < std::min<std::size_t>(4, tones.size()); ++j) {
                const Signal y = organ(root + tones[j], sec(4), tier < 3 ? .45 : .3);
                add("organ", y, b0, 0);
            }
        }
        // ---- brass stabs on the downbeat (tiers 2-3)
        if (tier >= 2 && bi % 2 == 0) {
            for (int j = 0; j < 3; ++j) {
                const Signal y = brass(s, root + tones[static_cast<std::size_t>(j)], sec(.3), .7, true);
                add("brass", y, b0, .15);
            }
            if (tier == 3) {
                for (int j = 0; j < 3; ++j) {
                    const Signal y = brass(s, root + tones[static_cast<std::size_t>(j)] + 12, sec(.25), .65, true);
                    add("brass", y, b0 + 2.5, -.1);
                }
            }
        }
        // ---- choir (tier 3): a dark "ah" on the chord
        if (tier == 3) {
            for (int j = 0; j < 3; ++j) {
                const Signal y = choir(s, root + 12 + tones[static_cast<std::size_t>(j)], sec(3.8), .45);
                add("choir", y, b0, 0);
            }
        }
        // ---- percussion
        for (int beat = 0; beat < 4; ++beat) {
            const double tb = b0 + beat;
            if (tier == 1) {
                if (beat == 0 || beat == 2) {
                    add("drums", s.synth.kick(.45), tb);
                }
                if (beat == 1 || beat == 3) {
                    add("drums", s.synth.rim(.45), tb, .2);
                }
                for (const double e : {0.0, .5}) {
                    add("drums", s.synth.shaker(.25 + (e == 0 ? .1 : 0.0)), tb + e, -.3);
                }
            } else {
                if (beat == 0 || beat == 2 || tier == 3) {
                    add("drums", s.synth.kick(beat == 0 || beat == 2 ? .7 : .5), tb);
                }
                if (tier == 3 && (beat == 1 || beat == 3)) {
                    add("drums", s.synth.kick(.45), tb + .5);
                }
                if (beat == 1 || beat == 3) {
                    add("drums", snare(s, .75), tb, .05);
                }
                const std::vector<double> hats = tier == 2 ? std::vector<double>{0, .5} : std::vector<double>{0, .25, .5, .75};
                for (const double e : hats) {
                    add("drums", s.synth.hat(e == 0 ? .35 : .22), tb + e, .3);
                }
                // the ticking clock: time is running out
                add("tick", s.synth.woodblock(tier == 2 ? .35 : .45, beat % 2 == 0), tb, beat % 2 ? .5 : -.5);
            }
        }
        if (tier >= 2 && bi % 4 == 0) {
            add("timp", timpani(s, 38, .75), b0);
        }
        if (tier == 3) {
            add("timp", timpani(s, bi % 2 == 0 ? 38 : 45, .6), b0 + 2);
            // the klaxon, low in the mix, every other bar
            if (bi % 2 == 1) {
                add("alarm", alarm(s, 81, 77, sec(2)), b0 + 2, 0);
            }
        }
        // fills at phrase ends
        if (bi % 4 == 3 && tier >= 2) {
            const std::vector<int> fill =
                tier == 2 ? std::vector<int>{50, 47, 45, 43} : std::vector<int>{52, 50, 47, 45, 43, 40, 38, 36};
            const double step = tier == 2 ? .25 : .125;
            for (std::size_t k = 0; k < fill.size(); ++k) {
                add("drums", tom(fill[k], .6), b0 + 3 + static_cast<double>(k) * step, -.2 + .1 * static_cast<double>(k));
            }
        }
    }
    // ---- melody. Pass 1: theremin (1), theremin over brass (2), brass in octaves (3).
    // Pass 2: organ with a theremin countermelody (1), strings and brass (2), brass and choir (3).
    double beat = 0;
    for (int pas = 0; pas < 2; ++pas) {
        for (int bi = 0; bi < 16; ++bi) {
            const ChordBar& chord = form[static_cast<std::size_t>(bi)];
            const int root = chord.root;
            const std::vector<int>& tones = chord.tones;
            if (pas == 1 && tier == 1) {  // a long-note countermelody on the chord's third
                const int high = root + 24 + tones[1];
                const Signal y = strings(s, high - (high > 80 ? 12 : 0), sec(3.8), .4);
                add("lead", y, bi * 4 + 64, .2);
            }
            for (const MelodyNote& note : melody()[static_cast<std::size_t>(bi)]) {
                const double d = note.beats;
                if (note.midi.has_value()) {
                    const int m = *note.midi;
                    if (tier == 1) {
                        if (pas == 0) {
                            add("lead", strings(s, m, sec(d * .98), .62), beat, .1);
                        } else {
                            add("organ", organ(m - 12, sec(d * .9), .55), beat, -.1);
                        }
                    } else if (tier == 2) {
                        if (pas == 0) {
                            add("lead", strings(s, m, sec(d * .98), .55), beat, .1);
                        } else {
                            add("strings", strings(s, m, sec(d * .95), .6), beat, .2);
                        }
                        add("brass", brass(s, m - 12, sec(d * .9), .55), beat, -.1);
                    } else {
                        add("brass", brass(s, m, sec(d * .9), .75), beat, 0);
                        add("brass", brass(s, m - 12, sec(d * .9), .65), beat, -.15);
                        if (pas == 1) {
                            add("choir", choir(s, m, sec(d * .95), .4), beat, .1);
                        }
                    }
                }
                beat += d;
            }
        }
    }
    // master(mix, LEVELS[tier], wet=.22)
    Stereo out{Signal(length, 0.0), Signal(length, 0.0)};
    Stereo send{Signal(length, 0.0), Signal(length, 0.0)};
    for (const std::pair<std::string, Stereo>& bus : mix->buses()) {
        const std::string& name = bus.first;
        const double g = std::pow(10.0, (level(tier, name) - rms_db(bus.second)) / 20);
        const bool wide = name == "lead" || name == "harp" || name == "brass" || name == "choir" || name == "strings";
        const double amount = wide ? .35 : .12;
        for (std::size_t i = 0; i < length; ++i) {
            const double left = bus.second.left[i] * g;
            const double right = bus.second.right[i] * g;
            out.left[i] += left;
            out.right[i] += right;
            send.left[i] += left * amount;
            send.right[i] += right * amount;
        }
    }
    mix.reset();  // the buses are folded in: free them before the reverb and the filter
    {
        const Stereo wet = voice::conv_reverb(send, voice::make_ir(2.2, 1.8, 1.4, .7), true);
        for (std::size_t i = 0; i < length; ++i) {
            out.left[i] += wet.left[i] * .22;
            out.right[i] += wet.right[i] * .22;
        }
    }
    send = Stereo{};
    out.left = voice::fft_filter(out.left, 30, 0, 2, 4096, true);
    out.right = voice::fft_filter(out.right, 30, 0, 2, 4096, true);
    return finish(std::move(out));
}

// stinger(kind): 4.2 s at 116 BPM; every bus straight into both channels.
class Stinger final {
  public:
    explicit Stinger(const Start& start) : s{voice::Synth(start.synth), voice::Generator(start.rng)} {}
    void put(const std::string& bus, const Signal& y, double beat) {
        Stereo* b = nullptr;
        for (std::pair<std::string, Stereo>& entry : buses_) {
            if (entry.first == bus) {
                b = &entry.second;
            }
        }
        if (b == nullptr) {
            buses_.emplace_back(bus, Stereo{Signal(length, 0.0), Signal(length, 0.0)});
            b = &buses_.back().second;
        }
        const std::size_t at = static_cast<std::size_t>(beat * spb);
        for (std::size_t i = 0; i < y.size() && at + i < length; ++i) {
            (*b).left[at + i] += y[i];
            (*b).right[at + i] += y[i];
        }
    }
    Stereo finish_mix() const {
        Stereo out{Signal(length, 0.0), Signal(length, 0.0)};
        Stereo send{Signal(length, 0.0), Signal(length, 0.0)};
        for (const std::pair<std::string, Stereo>& bus : buses_) {
            for (std::size_t i = 0; i < length; ++i) {
                out.left[i] += bus.second.left[i];
                out.right[i] += bus.second.right[i];
                send.left[i] += bus.second.left[i] * .3;
                send.right[i] += bus.second.right[i] * .3;
            }
        }
        const Stereo tail = voice::conv_reverb(send, voice::make_ir(2.0), false);
        for (std::size_t i = 0; i < length; ++i) {
            out.left[i] += tail.left[i] * .3;
            out.right[i] += tail.right[i] * .3;
        }
        const std::size_t fade = voice::samples(.3);
        const Signal down = voice::linspace(1, 0, fade);
        for (std::size_t i = 0; i < fade; ++i) {
            out.left[length - fade + i] *= down[i];
            out.right[length - fade + i] *= down[i];
        }
        return finish(std::move(out), -18, -2.5);
    }
    static constexpr std::size_t length = 201600;  // int(4.2 * SR)
    static constexpr double spb = 48000 * 60 / 116.0;
    Score s;

  private:
    std::vector<std::pair<std::string, Stereo>> buses_{};
};

}  // namespace

Stereo music_tier1() {
    return tier_loop(1);
}

Stereo music_tier2() {
    return tier_loop(2);
}

Stereo music_tier3() {
    return tier_loop(3);
}

// you won: his plan collapses, comically
Stereo stinger_defeat() {
    Stinger st(defeat_start);
    Score& s = st.s;
    static constexpr int fall[4] = {62, 61, 60, 59};
    for (int i = 0; i < 4; ++i) {
        const double d = i < 3 ? .75 : 2.5;
        st.put("brass", brass(s, fall[i], d * Stinger::spb / sr, .75), i * .75);
        st.put("brass", brass(s, fall[i] - 12, d * Stinger::spb / sr, .6), i * .75);
    }
    st.put("timp", timpani(s, 38, .9), 0);
    st.put("timp", timpani(s, 33, .9), 2.25);
    static constexpr int run[7] = {86, 81, 77, 74, 69, 65, 62};
    for (int i = 0; i < 7; ++i) {
        st.put("harp", harpsichord(s, run[i], .2, .6), 2.25 + i * .125);
    }
    return st.finish_mix();
}

// you lost: his triumph
Stereo stinger_triumph() {
    Stinger st(triumph_start);
    Score& s = st.s;
    for (const int t : {0, 3, 7, 12}) {
        st.put("organ", organ(38 + t + 12, 3.2, .7), 0);
    }
    static constexpr int rise[4] = {62, 65, 69, 74};
    static constexpr double lengths[4] = {.5, .5, .5, 2.5};
    static constexpr double beats[4] = {0, .5, 1, 1.5};
    for (int i = 0; i < 4; ++i) {
        st.put("brass", brass(s, rise[i], lengths[i] * Stinger::spb / sr, .85), beats[i]);
        st.put("brass", brass(s, rise[i] - 12, lengths[i] * Stinger::spb / sr, .7), beats[i]);
    }
    st.put("timp", timpani(s, 38, 1), 0);
    st.put("timp", timpani(s, 38, .8), 1.5);
    static constexpr int triad[3] = {0, 3, 7};
    for (int i = 0; i < 8; ++i) {
        st.put("choir", choir(s, 62 + triad[i % 3], .6, .5), 1.5 + i * .25);
    }
    return st.finish_mix();
}

// top score: the heroic turn to D major
Stereo stinger_topscore() {
    Stinger st(topscore_start);
    Score& s = st.s;
    static constexpr int rise[4] = {62, 66, 69, 74};
    static constexpr double lengths[4] = {.5, .5, .5, 2};
    static constexpr double beats[4] = {0, .5, 1, 1.5};
    for (int i = 0; i < 4; ++i) {
        st.put("brass", brass(s, rise[i], lengths[i] * Stinger::spb / sr, .8), beats[i]);
        st.put("strings", strings(s, rise[i] - 12, lengths[i] * Stinger::spb / sr, .6), beats[i]);
    }
    for (const int t : {0, 4, 7}) {
        st.put("organ", organ(50 + t, 2.5, .5), 1.5);
    }
    st.put("timp", timpani(s, 38, .8), 1.5);
    return st.finish_mix();
}

}  // namespace fp
