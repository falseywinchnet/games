#include "thieves_cues.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// audio_src/garden_music.py, "Pumpkin Patrol": one garden theme in scale degrees,
// arranged five ways (one seamless 24-bar loop per season), and three stingers, ported
// line for line in numpy's order of operations. Notes go through synth.py's render_note
// cache exactly as in the script, so a repeated note draws nothing from synth._rng; no
// cue reuses a note an earlier cue cached. The states are what
// shared/voice/tools/score_reference.py printed for the script (synth._rng seeded 12345,
// the script's rng seeded 2002, used only for the tune's velocities).
namespace ct {
namespace {

using voice::Signal;
using voice::Stereo;

constexpr double two_pi = 2 * voice::pi;
constexpr double sr = voice::sample_rate;

constexpr voice::Word128 synth_increment{0xc9c7353e6e2b1f28ull, 0x7d761f2d4027fae7ull};
constexpr voice::Word128 script_increment{0x75163968aa438733ull, 0x1d83b29174a25ae5ull};  // default_rng(2002)

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

constexpr Start spring_start{synth_at(0x1905e0335aae9634ull, 0x9199b0d09775add5ull),  // as imported
                             script_at(0xd5ecd4ca26bddf24ull, 0xa43f87e62f9a5122ull)};
constexpr Start summer_start{synth_at(0x19efa0c92c5ca4e1ull, 0xbfcc0f6fe8c98d76ull),
                             script_at(0x558c98362469fa7dull, 0x4271092dfecb005dull)};
constexpr Start autumn_start{synth_at(0x42573940a1974cbbull, 0xb04021a975086760ull),
                             script_at(0x9562402e2c865315ull, 0xe90798d25057984cull)};
constexpr Start winter_start{synth_at(0xecd0435e8a3c1294ull, 0xdd83ec1d6bb77680ull),
                             script_at(0x652fb3c7d5113c32ull, 0x0476b1e71c5a8b9full)};
constexpr Start night_start{synth_at(0xf908551678bc4ab6ull, 0x15948f21c9f7cad8ull),
                            script_at(0x1a25ba2bc8e7544aull, 0x368e68135027fc46ull)};
constexpr voice::GeneratorState win_start = synth_at(0x34379f86ffb1f522ull, 0x407da46c3f150e81ull);
constexpr voice::GeneratorState stuck_start = synth_at(0x61863bfeb3e443abull, 0x29d4cab9397332b3ull);
constexpr voice::GeneratorState book_start = synth_at(0x7e3bd23a946ea037ull, 0xc009ff4163fa5652ull);

using Scale = std::array<int, 7>;
constexpr Scale major = {0, 2, 4, 5, 7, 9, 11};
constexpr Scale minor = {0, 2, 3, 5, 7, 8, 10};

// deg(d, scale, tonic): a scale degree (any integer, 0 = tonic) to MIDI.
int deg(int d, const Scale& scale, int tonic) {
    const long long octave = static_cast<long long>(std::floor(d / 7.0));
    const int result = tonic + scale[static_cast<std::size_t>(voice::python_modulo(d, 7))] + 12 * static_cast<int>(octave);
    return result;
}

constexpr int a_chords[8] = {0, 5, 3, 4, 0, 5, 1, 4};
constexpr int b_chords[8] = {3, 0, 1, 4, 3, 2, 1, 4};

struct Note {
    std::optional<int> degree;  // none rests
    double beats;
};
using Bar = std::vector<Note>;

const std::array<Bar, 8>& a_tune() {
    static const std::array<Bar, 8> tune = {{
        {{4, .75}, {4, .25}, {5, .5}, {4, .5}, {2, 1}, {0, 1}},
        {{2, .75}, {2, .25}, {4, .5}, {2, .5}, {0, 1}, {-2, 1}},
        {{3, .5}, {4, .5}, {5, .5}, {7, .5}, {6, 1}, {5, 1}},
        {{4, .5}, {std::nullopt, .5}, {1, .5}, {std::nullopt, .5}, {4, .5}, {3, .5}, {1, 1}},
        {{4, .75}, {4, .25}, {5, .5}, {4, .5}, {7, 1}, {9, .5}, {7, .5}},
        {{8, .75}, {7, .25}, {5, .5}, {4, .5}, {2, 1}, {4, 1}},
        {{3, .5}, {5, .5}, {8, .5}, {7, .5}, {5, .5}, {3, .5}, {1, 1}},
        {{4, 1.5}, {6, .5}, {7, 2}},
    }};
    return tune;
}

const std::array<Bar, 8>& b_tune() {
    static const std::array<Bar, 8> tune = {{
        {{7, 1.5}, {8, .5}, {9, 1}, {7, 1}},
        {{7, 1}, {4, 1}, {2, 1}, {4, 1}},
        {{5, 1.5}, {4, .5}, {3, 1}, {1, 1}},
        {{4, 3}, {std::nullopt, 1}},
        {{7, 1.5}, {8, .5}, {9, 1}, {11, 1}},
        {{9, 1}, {8, 1}, {6, 1}, {4, 1}},
        {{8, 1}, {7, .5}, {5, .5}, {3, 1}, {1, 1}},
        {{4, .5}, {3, .5}, {1, .5}, {-1, .5}, {-3, 2}},
    }};
    return tune;
}

const std::array<Bar, 8>& counter_line() {
    static const std::array<Bar, 8> counter = {{
        {{2, 2}, {0, 2}},
        {{0, 2}, {-2, 2}},
        {{0, 2}, {2, 2}},
        {{1, 2}, {-1, 2}},
        {{2, 2}, {4, 2}},
        {{5, 2}, {2, 2}},
        {{1, 2}, {3, 2}},
        {{1, 2}, {2, 2}},
    }};
    return counter;
}

enum class Drums { none, spring, summer, autumn, winter };

struct Season {
    int bpm;
    const Scale* scale;
    int tonic;
    const char* lead;
    int lead_oct;
    const char* counter;
    const char* chords;
    const char* bass;
    Drums drums;
    bool pad;
    double swing;
};

constexpr Season spring{112, &major, 67, "marimba", 0, "flute", "pluck", "pizz", Drums::spring, false, 0.0};
constexpr Season summer{120, &major, 67, "slide", 0, "marimba", "pluck", "bass", Drums::summer, false, 0.08};
constexpr Season autumn{104, &minor, 64, "clarinet", 0, "reed", "reed", "tuba", Drums::autumn, false, 0.12};
constexpr Season winter{96, &major, 67, "musicbox", 12, "celesta", "glass", "pizz", Drums::winter, true, 0.0};
constexpr Season night{84, &minor, 64, "vibes", 0, "flute", "felt", "felt", Drums::none, true, 0.1};

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

// finish(x, target_rms_db, ceiling_db): level to a loudness, then a soft ceiling.
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

double level_of(const std::string& name) {
    if (name == "lead") return -16;
    if (name == "counter") return -21;
    if (name == "chords") return -22;
    if (name == "bass") return -17;
    if (name == "drums") return -22;
    if (name == "pad") return -25;
    if (name == "fx") return -28;
    return -20;
}

double send_of(const std::string& name) {
    if (name == "lead") return .3;
    if (name == "counter") return .35;
    if (name == "chords") return .25;
    if (name == "pad") return .4;
    if (name == "drums") return .1;
    if (name == "bass") return .05;
    return .1;
}

// arrange(name) and master(mix, L) with finish(): one season's loop.
Stereo season(const Season& a, const Start& start) {
    voice::Synth synth(start.synth);
    voice::Generator rng(start.rng);
    const long long spb = static_cast<long long>(voice::python_round(sr * 60 / a.bpm));
    const int bars = 24;
    const std::size_t length = static_cast<std::size_t>(bars * 4 * spb);
    std::optional<voice::LoopMix> mix(std::in_place, length, true);
    const Scale& sc = *a.scale;
    const int ton = a.tonic;
    // at(bar, beat): the off-beat eighths swing
    const auto at = [&](int bar, double beat) {
        const double sw = voice::remainder(beat * 2, 2) == 1 ? a.swing * static_cast<double>(spb) : 0.0;
        const long long result =
            static_cast<long long>(voice::python_round((bar * 4 + beat) * static_cast<double>(spb) + sw));
        return result;
    };
    const auto note = [&](const char* inst, int m, double dur, double vel) -> const Signal& {
        return synth.render_note(inst, m, dur, vel);
    };
    const std::string chords_kind = a.chords;
    const std::string bass_kind = a.bass;
    for (int bar = 0; bar < bars; ++bar) {
        const int part = bar / 8;  // A, B, A2
        const int i = bar % 8;
        const int root = (part != 1 ? a_chords : b_chords)[i];
        const Bar& tune = (part != 1 ? a_tune() : b_tune())[static_cast<std::size_t>(i)];
        const int triad[3] = {root, root + 2, root + 4};
        double beat = 0.0;
        for (const Note& n : tune) {
            if (n.degree.has_value()) {
                const int m = deg(*n.degree, sc, ton) + a.lead_oct;
                const double vel = .72 + rng.uniform(-.05, .05);
                const Signal& y = note(a.lead, m, n.beats * static_cast<double>(spb) / sr * .92, vel);
                mix->add("lead", y, at(bar, beat), .05);
            }
            beat += n.beats;
        }
        if (part == 2) {
            beat = 0.0;
            for (const Note& n : counter_line()[static_cast<std::size_t>(i)]) {
                const Signal& y = note(a.counter, deg(*n.degree, sc, ton), n.beats * static_cast<double>(spb) / sr * .95, .55);
                mix->add("counter", y, at(bar, beat), -.35);
                beat += n.beats;
            }
        }
        int chord[3];
        for (int k = 0; k < 3; ++k) {
            chord[k] = deg(triad[k], sc, ton - 12);
        }
        if (chords_kind == "pluck" || chords_kind == "reed") {
            const bool reed = chords_kind == "reed";
            const std::vector<double> offbeats = reed ? std::vector<double>{1, 3} : std::vector<double>{.5, 1.5, 2.5, 3.5};
            for (const double b2 : offbeats) {
                for (int k = 0; k < 3; ++k) {
                    const Signal& y = note(a.chords, chord[k], .3 * static_cast<double>(spb) / sr * (reed ? 2 : 1), .5);
                    mix->add("chords", y, at(bar, b2) + k * 90, .3);
                }
            }
        } else {
            for (int k = 0; k < 3; ++k) {
                const Signal& y =
                    note(a.chords, chord[k] + 12 * (chords_kind == "glass" ? 1 : 0), 3.8 * static_cast<double>(spb) / sr, .45);
                mix->add("chords", y, at(bar, 0) + k * 200, .25 - .25 * k);
            }
        }
        const int r = deg(root, sc, ton - 24);
        const int fifth = deg(root + 4, sc, ton - 24);
        const double bdur = .9 * static_cast<double>(spb) / sr;
        mix->add("bass", note(a.bass, r, bdur, .8), at(bar, 0), -.1);
        mix->add("bass", note(a.bass, bass_kind != "felt" ? fifth : r, bdur, .7), at(bar, 2), -.1);
        if (bar % 4 == 3 && bass_kind != "felt") {
            const int all[24] = {0, 5, 3, 4, 0, 5, 1, 4, 3, 0, 1, 4, 3, 2, 1, 4, 0, 5, 3, 4, 0, 5, 1, 4};
            const int target = deg(all[(bar + 1) % 24], sc, ton - 24);
            const int steps[3] = {-3, -2, -1};
            for (int k = 0; k < 3; ++k) {
                mix->add("bass", note(a.bass, target + steps[k], .4 * static_cast<double>(spb) / sr, .6),
                        at(bar, 3 + k / 3.0), -.1);
            }
        }
        if (a.pad) {
            for (int k = 0; k < 3; ++k) {
                mix->add("pad", note("pad", chord[k], 4 * static_cast<double>(spb) / sr, .4), at(bar, 0), (k - 1) * .4);
            }
        }
        switch (a.drums) {
            case Drums::spring:
                for (int b2 = 0; b2 < 8; ++b2) {
                    mix->add("drums", synth.render_drum("shaker", b2 % 2 ? .35 : .2, b2), at(bar, b2 / 2.0), .3);
                }
                mix->add("drums", synth.render_drum("kick", .5, 0), at(bar, 0), 0);
                mix->add("drums", synth.render_drum("wbhi", .45, bar), at(bar, 1), -.3);
                mix->add("drums", synth.render_drum("wblo", .45, bar), at(bar, 3), -.3);
                break;
            case Drums::summer:
                for (int b2 = 0; b2 < 16; ++b2) {
                    mix->add("drums", synth.render_drum("shaker", b2 % 2 ? .3 : .18, b2), at(bar, b2 / 4.0), .3);
                }
                mix->add("drums", synth.render_drum("kick", .6, 0), at(bar, 0), 0);
                mix->add("drums", synth.render_drum("kick", .45, 1), at(bar, 2.5), 0);
                for (const int b2 : {1, 3}) {
                    mix->add("drums", synth.render_drum("clap", .5, bar * 2 + b2), at(bar, b2), 0);
                }
                break;
            case Drums::autumn:
                for (const double b2 : {0.0, 1.5, 2.0, 3.0}) {
                    const bool high = voice::remainder(b2, 1) != 0;
                    mix->add("drums", synth.render_drum(high ? "wbhi" : "wblo", .45, static_cast<long long>(b2 * 2)),
                            at(bar, b2), -.25);
                }
                mix->add("drums", synth.render_drum("tap", .35, bar), at(bar, 1), .25);
                mix->add("drums", synth.render_drum("tap", .35, bar + 1), at(bar, 3), .25);
                break;
            case Drums::winter:
                for (int b2 = 0; b2 < 8; ++b2) {
                    mix->add("drums", synth.render_drum("shaker", b2 % 2 ? .3 : .2, b2), at(bar, b2 / 2.0), .35);
                }
                if (bar % 2 == 0) {
                    mix->add("drums", synth.render_drum("triangle", .35, bar), at(bar, 0), -.35);
                }
                break;
            case Drums::none:
                break;
        }
    }
    // master(mix, L, wet=.22)
    Stereo out{Signal(length, 0.0), Signal(length, 0.0)};
    Stereo send{Signal(length, 0.0), Signal(length, 0.0)};
    for (const std::pair<std::string, Stereo>& bus : mix->buses()) {
        const double g = std::pow(10.0, (level_of(bus.first) - rms_db(bus.second)) / 20);
        const double s = send_of(bus.first);
        for (std::size_t i = 0; i < length; ++i) {
            const double left = bus.second.left[i] * g;
            const double right = bus.second.right[i] * g;
            out.left[i] += left;
            out.right[i] += right;
            send.left[i] += left * s;
            send.right[i] += right * s;
        }
    }
    mix.reset();  // the buses are folded in: free them before the reverb and the filter
    {
        const Stereo wet = voice::conv_reverb(send, voice::make_ir(1.8, 1.4, 1.2, .6), true);
        for (std::size_t i = 0; i < length; ++i) {
            out.left[i] += wet.left[i] * .22;
            out.right[i] += wet.right[i] * .22;
        }
    }
    send = Stereo{};
    out.left = voice::fft_filter(out.left, 35, 0, 2, 4096, true);
    out.right = voice::fft_filter(out.right, 35, 0, 2, 4096, true);
    return finish(std::move(out));
}

// stinger(kind): 3.6 s at 120 BPM into one stereo buffer, a short room and a fade.
class Stinger final {
  public:
    explicit Stinger(const voice::GeneratorState& start) : synth(start), mix_(length, false) {}
    void put(std::span<const double> y, double beat, double pan = 0.0, double g = 1.0) {
        mix_.add("out", y, static_cast<long long>(beat * spb), pan, g);
    }
    Stereo finish_mix() {
        Stereo out = mix_.bus("out");
        Stereo send{Signal(length), Signal(length)};
        for (std::size_t i = 0; i < length; ++i) {
            send.left[i] = out.left[i] * .3;
            send.right[i] = out.right[i] * .3;
        }
        const Stereo tail = voice::conv_reverb(send, voice::make_ir(1.8), false);
        for (std::size_t i = 0; i < length; ++i) {
            out.left[i] = out.left[i] + tail.left[i] * .3;
            out.right[i] = out.right[i] + tail.right[i] * .3;
        }
        const std::size_t fade = voice::samples(.3);
        const Signal down = voice::linspace(1, 0, fade);
        for (std::size_t i = 0; i < fade; ++i) {
            out.left[length - fade + i] *= down[i];
            out.right[length - fade + i] *= down[i];
        }
        return finish(std::move(out), -18, -2.5);
    }
    static constexpr std::size_t length = 172800;  // int(3.6 * SR)
    static constexpr double spb = 48000 * 60 / 120.0;
    voice::Synth synth;

  private:
    voice::LoopMix mix_;
};

}  // namespace

Stereo music_spring() {
    return season(spring, spring_start);
}

Stereo music_summer() {
    return season(summer, summer_start);
}

Stereo music_autumn() {
    return season(autumn, autumn_start);
}

Stereo music_winter() {
    return season(winter, winter_start);
}

Stereo music_night() {
    return season(night, night_start);
}

// all caught: a bright run up and a "ta-da"
Stereo stinger_win() {
    Stinger st(win_start);
    voice::Synth& synth = st.synth;
    static constexpr int run[5] = {67, 71, 74, 79, 83};
    for (int i = 0; i < 5; ++i) {
        st.put(synth.render_note("marimba", run[i], .25, .8), i * .25, .3 * std::sin(static_cast<double>(i)));
    }
    for (const int m : {55, 67, 71, 74, 79}) {
        st.put(synth.render_note("pluck", m, 1.6, .7), 1.5, 0, .7);
    }
    st.put(synth.render_note("slide", 86, 1.2, .8), 1.25, .1);
    st.put(synth.render_drum("kick", .8, 0), 1.5);
    st.put(synth.render_drum("clap", .6, 1), 1.5);
    st.put(synth.render_drum("triangle", .5, 2), 1.5, -.3);
    return st.finish_mix();
}

// the sad trombone: wah, wah, wah, waaah
Stereo stinger_stuck() {
    Stinger st(stuck_start);
    voice::Synth& synth = st.synth;
    static constexpr int notes[4] = {58, 57, 56, 55};
    static constexpr double lengths[4] = {.45, .45, .45, 1.6};
    for (int i = 0; i < 4; ++i) {
        const int m = notes[i];
        const double d = lengths[i];
        const Signal y = synth.render_note("slide", m - 12, d, .8);
        const std::size_t n = y.size();
        const Signal t = voice::tarr(n);
        const Signal f(n, voice::mtof(m - 12));
        const Signal reed = synth.wt(voice::Wave::reed, f, 10);
        const Signal g = voice::gate(n, d, .03, .08);
        const double w5 = two_pi * 5;
        Signal x(n);
        for (std::size_t k = 0; k < n; ++k) {
            const double buzz = reed[k] * g[k] * .5;
            const double wah = i == 3 ? .6 + .4 * std::sin(w5 * t[k]) : 1.0;
            x[k] = (y[k] * .5 + buzz) * wah;
        }
        st.put(x, i * .55, 0);
    }
    st.put(synth.render_note("tuba", 31, 1.6, .7), 1.65);
    return st.finish_mix();
}

// the whole book: a fanfare on the theme, in the major, with everything
Stereo stinger_book() {
    Stinger st(book_start);
    voice::Synth& synth = st.synth;
    static constexpr int degrees[4] = {4, 5, 7, 11};
    static constexpr double beats[4] = {.5, .5, .5, 2};
    double beat = 0;
    for (int i = 0; i < 4; ++i) {
        const double dur = beats[i] * Stinger::spb / sr;
        st.put(synth.render_note("slide", deg(degrees[i], major, 67), dur, .85), beat, .1);
        st.put(synth.render_note("marimba", deg(degrees[i], major, 67) - 12, dur, .7), beat, -.2, .6);
        beat += beats[i];
    }
    for (const int m : {43, 55, 59, 62, 67}) {
        st.put(synth.render_note("pluck", m, 1.8, .7), 1.5, 0, .7);
    }
    st.put(synth.render_drum("kick", .9, 0), 1.5);
    st.put(synth.render_drum("clap", .7, 2), 1.5);
    return st.finish_mix();
}

}  // namespace ct
