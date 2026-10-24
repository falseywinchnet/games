#include "batch_kit.hpp"
#include "batch_parts.hpp"
#include "voice_fft.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// music_switchbox_loop, "Peekaboo Switches": authoring/audio/work/src/songs2.py's
// SWITCHBOX spec and arr_switchbox, the composer in compose.py (chords, scales,
// voicings, melody, counter-line, arpeggio, comping, bass and drum patterns, drawing
// from the song's seeded generators in the script's order), compose.render and
// compose.master, and render_music.py's loudness step and soft limiter, line for line.
//
// The master was rendered by `render_music.py --batch2` with Sudoku (day and night),
// Nature Cube, Untangle, Atom Probe and Four Pegs before it in the same process, so
// synth._rng had moved on and some notes were already cached. notes_music.inc holds
// every note the loop plays with the state synth._rng had when that run first
// rendered it (tools/batch_reference.py), so the loop renders alone and equals the
// master.
namespace batch_cues {
namespace {

constexpr NoteStart script[] = {
#include "notes_music.inc"
};

// The integrated loudness ffmpeg's ebur128 measured (and printed to 0.1 LUFS) for the
// loop normalised to a 0.5 peak; render_music.py scales to -20 LUFS from it.
#include "music_loudness.inc"

constexpr int beats_per_bar = 4;
constexpr int key = 9;  // A major
constexpr int scale_major[7] = {0, 2, 4, 5, 7, 9, 11};
constexpr double bpm = 128;
constexpr std::uint64_t song_seed = 1701;

// ------------------------------------------------------------------ chords (compose.py)
struct Chord {
    int root{};
    std::vector<int> iv{};
    std::string q{};
    int bass{};
    std::vector<int> pcs{};  // sorted pitch classes
};

int pcname(std::string_view s) {
    static constexpr int note[7] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G
    int v = note[s[0] - 'A'];
    for (std::size_t i = 1; i < s.size(); ++i) {
        v += s[i] == '#' ? 1 : -1;
    }
    const int result = static_cast<int>(voice::python_modulo(v, 12));
    return result;
}

struct Quality {
    std::string_view name;
    std::array<int, 4> iv;
    std::size_t count;
};

// QUAL, the qualities the Switchbox chart uses.
constexpr Quality qualities[] = {{"", {0, 4, 7, 0}, 3},     {"7", {0, 4, 7, 10}, 4},   {"maj7", {0, 4, 7, 11}, 4},
                                 {"m7", {0, 3, 7, 10}, 4},  {"sus4", {0, 5, 7, 0}, 3}, {"7sus4", {0, 5, 7, 10}, 4}};

Chord parse_chord(std::string_view text) {
    std::string_view s = text;
    int bass = -1;
    const std::size_t slash = s.find('/');
    if (slash != std::string_view::npos) {
        bass = pcname(s.substr(slash + 1));
        s = s.substr(0, slash);
    }
    std::size_t i = 1;
    while (i < s.size() && (s[i] == '#' || s[i] == 'b')) {
        ++i;
    }
    Chord chord{};
    chord.root = pcname(s.substr(0, i));
    chord.q = std::string(s.substr(i));
    for (const Quality& quality : qualities) {
        if (quality.name == chord.q) {
            chord.iv.assign(quality.iv.begin(), quality.iv.begin() + static_cast<std::ptrdiff_t>(quality.count));
        }
    }
    chord.bass = bass < 0 ? chord.root : bass;
    bool present[12] = {};
    for (const int x : chord.iv) {
        present[voice::python_modulo(chord.root + x, 12)] = true;
    }
    for (int pc = 0; pc < 12; ++pc) {
        if (present[pc]) {
            chord.pcs.push_back(pc);
        }
    }
    return chord;
}

bool contains(const std::vector<int>& values, int value) {
    const bool result = std::find(values.begin(), values.end(), value) != values.end();
    return result;
}

int pitch_class(int m) {
    const int result = static_cast<int>(voice::python_modulo(m, 12));
    return result;
}

// near(pc, target): the midi note with pitch class pc nearest to target.
int near(int pc, int target) {
    const int m = target - pitch_class(target - pc);
    const int result = target - m <= 6 ? m : m + 12;
    return result;
}

std::vector<std::string_view> words(std::string_view text) {
    std::vector<std::string_view> result{};
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find(' ', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        result.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return result;
}

// ------------------------------------------------------------------ the song (songs2.py)
struct Section {
    std::array<std::string_view, 8> bars;
    std::size_t bar_count;
    std::string_view layers;  // space-separated, as S(name, bars, layers, ...) splits them
    std::string_view lead;    // empty for None
    int center;
    double e;
};

constexpr std::array<std::string_view, 8> chart_verse = {"A", "E/G#", "F#m7", "C#m7", "D", "A/C#", "Bm7", "E"};
constexpr std::array<std::string_view, 8> chart_chorus = {"Dmaj7", "E", "C#m7", "F#m7", "Dmaj7", "E", "A", "A7"};
constexpr std::array<std::string_view, 8> chart_bridge = {"Bm7", "C#m7", "Dmaj7", "E", "Bm7", "C#m7", "D", "E7sus4 E7"};

// intro, A1, A2, CH, BR, CH2, A3
constexpr Section sections[] = {
    {{"Dmaj7", "E", "C#m7", "F#m7"}, 4, "mbox", "", 72, 1.0},
    {chart_verse, 8, "mbox pizz sbass drums lead", "celesta", 81, .85},
    {chart_verse, 8, "mbox pizz sbass drums lead counter", "celesta", 81, 1.0},
    {chart_chorus, 8, "mbox pizz sbass drums lead pad", "analog", 81, 1.0},
    {chart_bridge, 8, "mbox pizz half lead", "musicbox", 84, .75},
    {chart_chorus, 8, "mbox pizz sbass drums lead pad counter", "analog", 81, 1.0},
    {chart_verse, 8, "mbox pizz sbass drums lead", "celesta", 81, 1.0},
};

bool has_layer(const Section& section, std::string_view layer) {
    const std::vector<std::string_view> layers = words(section.layers);
    const bool result = std::find(layers.begin(), layers.end(), layer) != layers.end();
    return result;
}

// A bus of the mix and its compose.master settings (SWITCHBOX['bus']).
struct BusSpec {
    std::string_view name;
    double level;
    double rev;
    double delay_beats;  // 0: no ping-pong
    double delay_feedback;
    double delay_mix;
    double lp;  // 0: no low-pass
};

constexpr BusSpec bus_specs[] = {{"lead", -17, .2, .75, .25, .3, 0},  {"counter", -25, .25, 0, 0, 0, 0},
                                 {"arp", -23, .3, 0, 0, 0, 6500},     {"pizz", -23, .15, 0, 0, 0, 0},
                                 {"bass", -19, .03, 0, 0, 0, 0},      {"drums", -24, .08, 0, 0, 0, 0},
                                 {"pad", -27, .35, 0, 0, 0, 0}};
constexpr double wet_level = .45;
constexpr double presence_db = 1.5;

// ANIME_R and ANIME_C: 2-bar lead rhythms and cadences, (onset beat, duration beats)
struct Step {
    double on;
    double du;
};
using Rhythm = std::vector<Step>;

const std::vector<Rhythm>& anime_rhythms() {
    static const std::vector<Rhythm> rhythms = {
        {{0, .5}, {.5, .25}, {.75, .25}, {1, .5}, {1.5, .5}, {2, 1}, {3, .5}, {3.5, .5}, {4, .5}, {4.5, .5}, {5, .5},
         {5.5, 1}, {6.5, 1}},
        {{0, .25}, {.25, .25}, {.5, .5}, {1, .5}, {1.5, 1}, {2.5, .5}, {3, 1}, {4, .5}, {4.5, .5}, {5, 1}, {6, .5},
         {6.5, .5}, {7, 1}},
        {{0, 1}, {1, .5}, {1.5, .5}, {2, .5}, {2.5, .5}, {3, .5}, {3.5, .5}, {4, 1.5}, {5.5, .5}, {6, 1.5}}};
    return rhythms;
}

const std::vector<Rhythm>& anime_cadences() {
    static const std::vector<Rhythm> cadences = {
        {{0, .5}, {.5, .5}, {1, .5}, {1.5, .5}, {2, .5}, {2.5, .5}, {3, 1}, {4, 3.5}}, {{0, 1}, {1, 1}, {2, 2}, {4, 4}}};
    return cadences;
}

// ------------------------------------------------------------------ the arrangement (compose.Ctx)
enum class Kind { note, drum };

struct Event {
    Kind kind{};
    double t{};
    std::string name{};
    int m{};
    double d{};
    double v{};
    double pan{};
    std::string bus{};
};

struct ChordSpan {
    double start{};
    double dur{};
    Chord chord{};
};

struct SectionSpan {
    double start{};
    int bars{};
    std::size_t section{};
};

class Ctx final {
  public:
    Ctx() : rng(song_seed), mrng(song_seed + 1) {
        double b = 0.0;
        for (std::size_t si = 0; si < std::size(sections); ++si) {
            const Section& section = sections[si];
            secs.push_back(SectionSpan{b, static_cast<int>(section.bar_count), si});
            for (std::size_t i = 0; i < section.bar_count; ++i) {
                const std::vector<std::string_view> parts = words(section.bars[i]);
                const double d = static_cast<double>(beats_per_bar) / static_cast<double>(parts.size());
                for (std::size_t k = 0; k < parts.size(); ++k) {
                    chords.push_back(ChordSpan{b + static_cast<double>(k) * d, d, parse_chord(parts[k])});
                }
                b += beats_per_bar;
            }
        }
        total = b;
        for (const ChordSpan& span : chords) {
            starts.push_back(span.start);
        }
    }

    [[nodiscard]] const ChordSpan& chord_at(double t) const {
        const double u = voice::remainder(t, total);
        const std::vector<double>::const_iterator found = std::upper_bound(starts.begin(), starts.end(), u + 1e-6);
        const std::ptrdiff_t i = (found - starts.begin()) - 1;
        const ChordSpan& result = chords[static_cast<std::size_t>(i)];
        return result;
    }

    [[nodiscard]] std::vector<int> scale_at(double t) const {
        const Chord& ch = chord_at(t).chord;
        bool sc[12] = {};
        for (const int x : scale_major) {
            sc[pitch_class(key + x)] = true;
        }
        for (const int c : ch.pcs) {
            if (sc[c]) {
                continue;
            }
            bool snapshot[12] = {};
            std::copy(sc, sc + 12, snapshot);
            for (int k = 0; k < 12; ++k) {
                if (snapshot[k] && std::min(pitch_class(k - c), pitch_class(c - k)) == 1 && !contains(ch.pcs, k)) {
                    sc[k] = false;
                }
            }
            sc[c] = true;
        }
        std::vector<int> result{};
        for (int k = 0; k < 12; ++k) {
            if (sc[k]) {
                result.push_back(k);
            }
        }
        return result;
    }

    void note(double t, std::string_view inst, int m, double d, double v, double pan, std::string_view bus) {
        ev.push_back(Event{Kind::note, t, std::string(inst), m, d, std::clamp(v, 0.05, 1.0), pan, std::string(bus)});
    }

    void drum(double t, std::string_view name, double v, double pan) {
        ev.push_back(Event{Kind::drum, t, std::string(name), 0, 0, std::clamp(v, 0.02, 1.0), pan, "drums"});
    }

    voice::Generator rng;
    voice::Generator mrng;
    std::vector<Event> ev{};
    std::vector<ChordSpan> chords{};
    std::vector<SectionSpan> secs{};
    double total{};
    std::vector<double> starts{};
};

int scale_step(int m, int k, const std::vector<int>& sc) {
    std::vector<int> notes{};
    for (int x = m - 30; x < m + 31; ++x) {
        if (contains(sc, pitch_class(x))) {
            notes.push_back(x);
        }
    }
    std::size_t best = 0;
    for (std::size_t i = 1; i < notes.size(); ++i) {
        if (std::abs(notes[i] - m) < std::abs(notes[best] - m)) {
            best = i;
        }
    }
    const long long last = static_cast<long long>(notes.size()) - 1;
    const long long index = std::clamp(static_cast<long long>(best) + k, 0LL, last);
    const int result = notes[static_cast<std::size_t>(index)];
    return result;
}

double mean_of(const std::vector<int>& v) {
    const std::vector<double> values(v.begin(), v.end());
    const double result = voice::sum(values) / static_cast<double>(values.size());
    return result;
}

// voicing(ch, prev, lo, hi, rootless, add9, maxn); an empty prev is None.
std::vector<int> voicing(const Chord& ch, const std::vector<int>& prev, int lo, int hi, bool rootless, bool add9,
                         std::size_t maxn) {
    std::vector<int> pcs{};
    for (const int x : ch.iv) {
        pcs.push_back(pitch_class(ch.root + x));
    }
    if (rootless && pcs.size() >= 4) {
        pcs.erase(pcs.begin());
    }
    const bool ninth = ch.q == "maj7" || ch.q == "m7" || ch.q == "6" || ch.q == "m6";
    if (add9 && ninth && pcs.size() < maxn) {
        pcs.push_back(pitch_class(ch.root + 2));
    }
    std::sort(pcs.begin(), pcs.end());
    pcs.erase(std::unique(pcs.begin(), pcs.end()), pcs.end());
    if (pcs.size() > maxn) {
        pcs.resize(maxn);
    }
    std::vector<int> best{};
    double bs = 1e9;
    const double center = static_cast<double>(lo + hi) / 2;
    for (std::size_t r = 0; r < pcs.size(); ++r) {
        std::vector<int> rot(pcs.begin() + static_cast<std::ptrdiff_t>(r), pcs.end());
        rot.insert(rot.end(), pcs.begin(), pcs.begin() + static_cast<std::ptrdiff_t>(r));
        for (int base = lo - 12; base < hi; ++base) {
            std::vector<int> v{};
            int m = base;
            for (const int p : rot) {
                m = m + pitch_class(p - m);
                v.push_back(m);
            }
            if (v.front() < lo || v.back() > hi) {
                continue;
            }
            const double mv = mean_of(v);
            double sc = std::fabs(mv - center) * 0.35;
            if (!prev.empty()) {
                const double move = std::fabs(mv - mean_of(prev)) + 0.3 * std::abs(v[0] - prev[0]);
                sc += move;
            }
            if (sc < bs) {
                best = v;
                bs = sc;
            }
        }
    }
    return best;
}

int sign(int x) {
    const int result = (x > 0) - (x < 0);
    return result;
}

// STEPW: melodic steps and their weights
constexpr int step_values[9] = {-1, 1, -2, 2, 0, 3, -3, 4, -4};
constexpr double step_weights[9] = {0.24, 0.24, 0.12, 0.12, 0.08, 0.07, 0.07, 0.03, 0.03};

std::vector<int> contour(voice::Generator& rng, std::size_t n) {
    std::vector<int> out{};
    int run = 0;
    for (std::size_t i = 0; i < n; ++i) {
        int s = step_values[rng.choice(step_weights)];
        if (run >= 2 && s * sign(out.back()) > 0) {
            s = -s;
        }
        run = !out.empty() && sign(s) == sign(out.back()) && s != 0 ? run + 1 : 0;
        out.push_back(s);
    }
    return out;
}

// The lead's instrument, articulation, pan and bus, as melody() passes them to realize().
struct Lead {
    std::string_view inst;
    double art;
};

int nearest_to(const std::vector<int>& pcs, int target) {
    int best = 0;
    for (std::size_t i = 0; i < pcs.size(); ++i) {
        const int x = near(pcs[i], target);
        if (i == 0 || std::abs(x - target) < std::abs(best - target)) {
            best = x;
        }
    }
    return best;
}

int realize(Ctx& ctx, double t0, const Rhythm& rhythm, const std::vector<int>& cont, int target, int lo, int hi,
            const Lead& lead, double vel, const std::vector<int>& end_on) {
    const double pan = 0.08;
    const int strong_div = 2;  // 4 beats to the bar
    int p = 0;
    for (std::size_t i = 0; i < rhythm.size(); ++i) {
        const double on = rhythm[i].on;
        const double du = rhythm[i].du;
        const double t = t0 + on;
        const Chord& ch = ctx.chord_at(t).chord;
        const std::vector<int> sc = ctx.scale_at(t);
        if (i == 0) {
            p = nearest_to(ch.pcs, target);
        } else {
            const int k = cont[(i - 1) % cont.size()];
            int q = scale_step(p, k, sc);
            if (q < lo || q > hi) {
                q = scale_step(p, -k, sc);
            }
            p = q;
        }
        const double rounded = voice::python_round(on);
        const bool on_strong_beat = std::fabs(on - rounded) < 1e-6 &&
                                    voice::python_modulo(static_cast<long long>(rounded), strong_div) == 0;
        const bool strong = on_strong_beat || du >= 1.5;
        if (strong && !contains(ch.pcs, pitch_class(p))) {
            // the nearest chord tone, the higher one on a tie
            int best = 0;
            for (std::size_t j = 0; j < ch.pcs.size(); ++j) {
                const int x = near(ch.pcs[j], p);
                const int distance = std::abs(x - p);
                const int best_distance = std::abs(best - p);
                if (j == 0 || distance < best_distance || (distance == best_distance && x > best)) {
                    best = x;
                }
            }
            p = best;
        }
        if (i == rhythm.size() - 1 && !end_on.empty()) {
            std::vector<int> pcs{};
            for (const int x : end_on) {
                pcs.push_back(pitch_class(ch.root + x));
            }
            p = nearest_to(pcs, p);
        }
        p = std::clamp(p, lo, hi);
        const double accent = std::fabs(voice::remainder(on, strong_div)) < 1e-6 ? 1.08 : 0.94;
        const double jitter = ctx.rng.uniform(-0.04, 0.04);
        const double v = vel * accent + jitter;
        ctx.note(t, lead.inst, p, du * lead.art, v, pan, "lead");
    }
    return p;
}

void melody(Ctx& ctx, double sec_start, int nbars, std::string_view inst, int center, double vel, double art) {
    voice::Generator& rng = ctx.mrng;
    const int unit = 2 * beats_per_bar;
    const int count = nbars / 2;
    const int lo = center - 9;
    const int hi = center + 10;
    const std::vector<Rhythm>& rhythms = anime_rhythms();
    const std::vector<Rhythm>& cadences = anime_cadences();
    const std::int64_t rhythm_count = static_cast<std::int64_t>(rhythms.size());
    const Rhythm& ra = rhythms[static_cast<std::size_t>(rng.integers(0, rhythm_count))];
    const Rhythm& rb = rhythms[static_cast<std::size_t>(rng.integers(0, rhythm_count))];
    const std::vector<int> ca = contour(rng, ra.size() + 2);
    const std::vector<int> cb = contour(rng, rb.size() + 2);
    const Lead lead{inst, art};
    const std::vector<int> none{};
    int last = center;
    static constexpr std::string_view roles[4] = {"A", "var", "shift", "half"};
    for (int u = 0; u < count; ++u) {
        const double t0 = sec_start + u * unit;
        std::string_view role = u == count - 1 ? std::string_view("cad") : roles[u % 4];
        if (count >= 8 && u % 8 >= 4 && (role == "A" || role == "shift")) {
            role = "B";
        }
        const int tgt = static_cast<int>(0.5 * center + 0.5 * last);
        if (role == "A") {
            last = realize(ctx, t0, ra, ca, tgt, lo, hi, lead, vel, none);
        } else if (role == "var") {
            std::vector<int> c(ca.begin(), ca.end() - 2);
            const std::vector<int> tail = contour(rng, 2);
            c.insert(c.end(), tail.begin(), tail.end());
            last = realize(ctx, t0, ra, c, tgt, lo, hi, lead, vel, none);
        } else if (role == "shift") {
            last = realize(ctx, t0, ra, ca, tgt + 3, lo, hi, lead, vel * 1.04, none);
        } else if (role == "B") {
            last = realize(ctx, t0, rb, cb, tgt + 2, lo, hi, lead, vel * 1.03, none);
        } else {
            const std::int64_t cadence_count = static_cast<std::int64_t>(cadences.size());
            const Rhythm& cad = cadences[static_cast<std::size_t>(rng.integers(0, cadence_count))];
            static constexpr int choices[5] = {-1, -1, -2, 1, 0};
            std::vector<int> c{};
            for (std::size_t i = 0; i < cad.size(); ++i) {
                c.push_back(choices[rng.choice(5)]);
            }
            const std::vector<int> end = role == "cad" ? std::vector<int>{0, 4, 3} : std::vector<int>{7, 4, 3, 2};
            last = realize(ctx, t0, cad, c, tgt, lo, hi, lead, vel * 0.96, end);
        }
    }
}

void counter(Ctx& ctx, double s0, int nbars, std::string_view inst, int center, double vel, double pan,
             std::string_view bus, double delay) {
    const double art = 0.95;
    const double end = s0 + nbars * beats_per_bar;
    int prev = center;
    double t = s0;
    while (t < end - 1e-6) {
        const ChordSpan& span = ctx.chord_at(t);
        const Chord& ch = span.chord;
        std::vector<int> tones{};
        for (std::size_t i = 1; i < ch.iv.size(); ++i) {
            tones.push_back(pitch_class(ch.root + ch.iv[i]));
        }
        const std::vector<int> pair = {tones[0], tones.size() > 2 ? tones[2] : tones.back()};
        int p = 0;
        double best = 0;
        for (std::size_t i = 0; i < pair.size(); ++i) {
            const int x = near(pair[i], prev);
            const double score = std::abs(x - prev) + 0.2 * std::abs(x - center);
            if (i == 0 || score < best) {
                p = x;
                best = score;
            }
        }
        const double dd = std::min(span.dur, end - t);
        const double v = vel + ctx.rng.uniform(-.04, .04);
        ctx.note(t + delay, inst, p, (dd - delay) * art, v, pan, bus);
        prev = p;
        t += dd;
    }
}

void arp(Ctx& ctx, double s0, int nbars, std::string_view inst, int lo, int hi, double vel,
         std::span<const int> pattern, double step, double pan, std::string_view bus, double dur) {
    std::vector<int> prev{};
    const int n_steps = static_cast<int>(nbars * beats_per_bar / step);
    for (int i = 0; i < n_steps; ++i) {
        const double t = s0 + i * step;
        const Chord& c = ctx.chord_at(t).chord;
        const std::vector<int> v = voicing(c, prev, lo, hi, false, true, 4);
        prev = v;
        std::vector<int> tones = v;
        tones.push_back(v[0] - 12 + pitch_class(c.root - v[0]));
        std::sort(tones.begin(), tones.end());
        tones.erase(std::unique(tones.begin(), tones.end()), tones.end());
        const int idx = pattern[static_cast<std::size_t>(i) % pattern.size()];
        const int count = static_cast<int>(tones.size());
        const int m = tones[static_cast<std::size_t>(idx % count)] + 12 * (idx / count);
        const double acc = std::fabs(voice::remainder(t - s0, beats_per_bar)) < 1e-6 ? 1.0 : 0.88;
        const double v_jitter = ctx.rng.uniform(-.05, .05);
        const double pan_jitter = ctx.rng.uniform(-.1, .1);
        ctx.note(t, inst, m, dur, vel * acc + v_jitter, pan + pan_jitter, bus);
    }
}

std::vector<ChordSpan> bar_chords(const Ctx& ctx, double b0) {
    std::vector<ChordSpan> result{};
    for (const ChordSpan& span : ctx.chords) {
        if (b0 - 1e-6 <= span.start && span.start < b0 + beats_per_bar - 1e-6) {
            result.push_back(span);
        }
    }
    return result;
}

// comp(kind='stab'): pizz on 1 and 2&, and on 4 in odd bars
void comp_stab(Ctx& ctx, double s0, int nbars, std::string_view inst, int lo, int hi, double vel, double pan,
               std::string_view bus) {
    static constexpr double odd_bar[3] = {0, 1.5, 3};
    static constexpr double even_bar[2] = {0, 1.5};
    std::vector<int> prev{};
    for (int bar = 0; bar < nbars; ++bar) {
        const double b0 = s0 + bar * beats_per_bar;
        const std::span<const double> hits = bar % 2 != 0 ? std::span<const double>(odd_bar) : std::span<const double>(even_bar);
        for (const double t : hits) {
            const Chord& c = ctx.chord_at(b0 + t).chord;
            const std::vector<int> v = voicing(c, prev, lo, hi, false, false, 3);
            for (std::size_t k = 0; k < v.size(); ++k) {
                const double jitter = ctx.rng.uniform(-.04, .04);
                const double value = vel * (t == 0 ? 1 : .8) + jitter;
                ctx.note(b0 + t + static_cast<double>(k) * 0.007, inst, v[k], 0.25, value, pan, bus);
            }
            prev = v;
        }
    }
}

// comp(kind='sustain'): each chord held
void comp_sustain(Ctx& ctx, double s0, int nbars, std::string_view inst, int lo, int hi, double vel, double pan,
                  std::string_view bus) {
    std::vector<int> prev{};
    for (int bar = 0; bar < nbars; ++bar) {
        const double b0 = s0 + bar * beats_per_bar;
        for (const ChordSpan& span : bar_chords(ctx, b0)) {
            const std::vector<int> v = voicing(span.chord, prev, lo, hi, true, true, 4);
            for (std::size_t k = 0; k < v.size(); ++k) {
                const double jitter = ctx.rng.uniform(-.05, .05);
                ctx.note(span.start + static_cast<double>(k) * 0.008, inst, v[k], span.dur * 0.97, vel + jitter, pan, bus);
            }
            prev = v;
        }
    }
}

struct BassHit {
    double t;
    int iv;
    double d;
};

void bass_oct(Ctx& ctx, double s0, int nb, std::string_view inst, int lo, double vel, std::span<const BassHit> pat) {
    for (int b = 0; b < nb; ++b) {
        const double b0 = s0 + b * beats_per_bar;
        for (const BassHit& hit : pat) {
            const Chord& c = ctx.chord_at(b0 + hit.t).chord;
            const int r = near(c.bass, lo + 6);
            const double jitter = ctx.rng.uniform(-.04, .04);
            const double v = vel * (hit.iv == 0 ? 1 : .8) + jitter;
            ctx.note(b0 + hit.t, inst, r + hit.iv, hit.d, v, 0, "bass");
        }
    }
}

// bass_line(kind='whole'): one note per chord on its bass note; it draws nothing.
void bass_whole(Ctx& ctx, double s0, int nbars, int lo, double vel) {
    int prev = lo + 7;
    for (int bar = 0; bar < nbars; ++bar) {
        const double b0 = s0 + bar * beats_per_bar;
        for (const ChordSpan& span : bar_chords(ctx, b0)) {
            int p = near(span.chord.bass, prev);
            p = p < lo ? p + 12 : (p > lo + 14 ? p - 12 : p);
            ctx.note(span.start, "bass", p, span.dur * 0.95, vel, 0, "bass");
            prev = p;
        }
    }
}

struct Hit {
    std::string_view name;
    double t;
    double v;
    double pan;
};

void pdrums(Ctx& ctx, double s0, int nb, std::span<const Hit> bar, double e, std::span<const Hit> fill) {
    for (int b = 0; b < nb; ++b) {
        const std::span<const Hit> pat = !fill.empty() && b == nb - 1 ? fill : bar;
        for (const Hit& hit : pat) {
            const double jitter = ctx.rng.uniform(-.02, .02);
            ctx.drum(s0 + b * beats_per_bar + hit.t, hit.name, hit.v * e + jitter, hit.pan);
        }
    }
}

// arr_switchbox
Ctx arrange() {
    Ctx ctx{};
    std::vector<Hit> bounce = {{"kick", 0, .38, 0}, {"kick", 2.5, .3, 0}, {"clap", 1, .3, .1}, {"clap", 3, .3, .1}};
    for (int k = 0; k < 8; ++k) {
        bounce.push_back(Hit{"hat", k / 2.0, k % 2 == 0 ? .14 : .1, .4});
    }
    for (int k = 0; k < 16; k += 2) {
        bounce.push_back(Hit{"shaker", k / 4.0 + .25, .06, -.4});
    }
    std::vector<Hit> fill{};
    for (const Hit& hit : bounce) {
        if (hit.t < 3) {
            fill.push_back(hit);
        }
    }
    fill.push_back(Hit{"wbhi", 3, .25, .3});
    fill.push_back(Hit{"wblo", 3.25, .25, -.3});
    fill.push_back(Hit{"wbhi", 3.5, .3, .3});
    fill.push_back(Hit{"clap", 3.75, .3, 0});
    std::vector<Hit> half = {{"kick", 0, .25, 0}, {"clap", 2, .2, 0}};
    for (int k = 0; k < 8; ++k) {
        half.push_back(Hit{"shaker", k / 2.0, .07, -.4});
    }
    static constexpr int mbox_pattern[8] = {0, 3, 2, 4, 1, 3, 2, 4};
    static constexpr BassHit sbass[8] = {{0, 0, .3}, {.5, 12, .2}, {1, 0, .3}, {1.5, 12, .2},
                                         {2, 0, .3}, {2.5, 12, .2}, {3, 7, .3}, {3.5, 12, .2}};
    const std::vector<SectionSpan> secs = ctx.secs;
    for (const SectionSpan& sec : secs) {
        const Section& spec = sections[sec.section];
        const double s0 = sec.start;
        const int nb = sec.bars;
        const double e = spec.e;
        if (has_layer(spec, "lead")) {
            const double vel = .68 * std::pow(e, .5);
            const double art = spec.lead == "analog" ? .8 : 1.0;
            melody(ctx, s0, nb, spec.lead, spec.center, vel, art);
        }
        if (has_layer(spec, "counter")) {
            counter(ctx, s0, nb, "vibes", spec.center - 9, .4, -.4, "counter", .5);
        }
        if (has_layer(spec, "mbox")) {
            arp(ctx, s0, nb, "musicbox", 69, 88, .32 * e, mbox_pattern, 0.5, .15, "arp", .4);
        }
        if (has_layer(spec, "pizz")) {
            comp_stab(ctx, s0, nb, "pizz", 57, 72, .5 * e, -.4, "pizz");
        }
        if (has_layer(spec, "sbass")) {
            bass_oct(ctx, s0, nb, "synthbass", 33, .65 * e, sbass);
        }
        if (has_layer(spec, "half")) {
            bass_whole(ctx, s0, nb, 33, .5);
            pdrums(ctx, s0, nb, half, e, {});
        }
        if (has_layer(spec, "drums")) {
            pdrums(ctx, s0, nb, bounce, e, fill);
        }
        if (has_layer(spec, "pad")) {
            comp_sustain(ctx, s0, nb, "pad", 57, 74, .4 * e, 0, "pad");
        }
    }
    return ctx;
}

// ------------------------------------------------------------------ rendering (compose.render, compose.master)
bool earlier(const Event& a, const Event& b) {
    const bool result = a.t < b.t;
    return result;
}

double swing_pos(double t, double swing) {
    const double beat = std::floor(t);
    const double p = t - beat;
    const double sp = 0.5 + swing / 6.0;
    const double q = p < 0.5 ? p * 2 * sp : sp + (p - 0.5) * 2 * (1 - sp);
    const double result = beat + q;
    return result;
}

// An event placed in the loop: its start sample and, for a drum, its round-robin.
struct Placed {
    std::size_t event{};
    long long start{};
    long long round_robin{};
};

// active_rms_db: the louder half of the one-second RMS windows of the mono sum.
double active_rms_db(const voice::Stereo& x) {
    const std::size_t length = x.left.size();
    const std::size_t w = voice::sample_rate;
    std::vector<double> rms{};
    voice::Signal squares(w);
    for (std::size_t i = 0; i + w < length; i += w) {
        for (std::size_t j = 0; j < w; ++j) {
            const double mono = (x.left[i + j] + x.right[i + j]) / 2;
            squares[j] = mono * mono;
        }
        const double value = std::sqrt(voice::sum(squares) / static_cast<double>(w));
        if (value > 1e-6) {
            rms.push_back(value);
        }
    }
    if (rms.empty()) {
        return -120;
    }
    std::sort(rms.begin(), rms.end());
    std::vector<double> top(rms.begin() + static_cast<std::ptrdiff_t>(rms.size() / 2), rms.end());
    for (double& value : top) {
        value = value * value;
    }
    const double result = 20 * std::log10(std::sqrt(voice::sum(top) / static_cast<double>(top.size())));
    return result;
}

// presence(x, db): a smooth high shelf (+db above about 2500 Hz), circular.
voice::Signal presence(const voice::Signal& x, double db) {
    const double f0 = 2500.0;
    const std::size_t n = x.size();
    std::vector<voice::Complex> spectrum{};
    voice::rfft(x, n, spectrum);
    // np.fft.rfftfreq(len(x), 1 / SR)
    const double spacing = 1.0 / static_cast<double>(voice::sample_rate);
    const double unit = 1.0 / (static_cast<double>(n) * spacing);
    const double slope = db / 20;
    for (std::size_t k = 0; k < spectrum.size(); ++k) {
        const double f = static_cast<double>(k) * unit;
        const double ratio = f0 / std::max(f, 1.0);
        const double g = std::pow(10.0, slope * (1 / (1 + ratio * ratio)));
        spectrum[k] = voice::Complex(spectrum[k].real() * g, spectrum[k].imag() * g);
    }
    voice::Signal y{};
    voice::irfft(spectrum, n, y);
    return y;
}

void filter_channels(voice::Stereo& x, double hi) {
    x.left = voice::fft_filter(x.left, 0, hi, 1, 4096, true);
    x.right = voice::fft_filter(x.right, 0, hi, 1, 4096, true);
}

const BusSpec* bus_spec(std::string_view name) {
    const BusSpec* result = nullptr;
    for (const BusSpec& spec : bus_specs) {
        if (spec.name == name) {
            result = &spec;
        }
    }
    return result;
}

// b + pingpong(b, delay, fb) * mix (synth.pingpong with n=5, lp=3500), one echo
// channel at a time to keep the loop's memory down.
void add_pingpong(voice::Stereo& b, long long delay, double fb, double mix_gain) {
    const std::size_t length = b.left.size();
    const long long size = static_cast<long long>(length);
    voice::Signal mono(length);
    for (std::size_t i = 0; i < length; ++i) {
        mono[i] = (b.left[i] + b.right[i]) / 2;
    }
    for (int channel = 0; channel < 2; ++channel) {
        voice::Signal echo(length, 0.0);
        double g = 1.0;
        for (int k = 1; k <= 5; ++k) {
            g *= fb;
            if ((k + 1) % 2 != channel) {
                continue;
            }
            const long long shift = voice::python_modulo(static_cast<long long>(k) * delay, size);
            for (long long i = 0; i < size; ++i) {
                const long long from = voice::python_modulo(i - shift, size);
                echo[static_cast<std::size_t>(i)] += g * mono[static_cast<std::size_t>(from)];
            }
        }
        echo = voice::fft_filter(echo, 250, 3500, 2, 4096, true);
        voice::Signal& target = channel == 0 ? b.left : b.right;
        for (std::size_t i = 0; i < length; ++i) {
            target[i] = target[i] + echo[i] * mix_gain;
        }
    }
}

// conv_reverb(x, ir, circular=True) for one channel.
voice::Signal circular_reverb(const voice::Signal& x, const voice::Signal& response) {
    const std::size_t n = x.size();
    voice::Signal h(n, 0.0);
    for (std::size_t i = 0; i < response.size() && i < n; ++i) {
        h[i] = response[i];
    }
    std::vector<voice::Complex> spectrum{};
    std::vector<voice::Complex> kernel{};
    voice::rfft(x, n, spectrum);
    voice::rfft(h, n, kernel);
    h = voice::Signal{};
    for (std::size_t k = 0; k < spectrum.size(); ++k) {
        const voice::Complex a = spectrum[k];
        const voice::Complex c = kernel[k];
        spectrum[k] = voice::Complex(a.real() * c.real() - a.imag() * c.imag(), a.real() * c.imag() + a.imag() * c.real());
    }
    kernel = std::vector<voice::Complex>{};
    voice::Signal wet{};
    voice::irfft(spectrum, n, wet);
    return wet;
}

// One bus mixed (its events summed in time order into a circular stereo loop) and
// processed as compose.master does: levelled, echoed, low-passed.
voice::Stereo master_bus(const std::vector<Event>& events, const std::vector<Placed>& placed, const std::string& name,
                         std::size_t length, double sec_beat, long long spb, double& rev) {
    voice::LoopMix mix(length);
    {
        Notes notes(script);  // the bus's notes; freed with it
        for (const Placed& p : placed) {
            const Event& event = events[p.event];
            if (event.bus != name) {
                continue;
            }
            if (event.kind == Kind::note) {
                const voice::Signal& y = notes.note(event.name, event.m, event.d * sec_beat, event.v);
                mix.add(name, y, p.start, event.pan, 1.0);
            } else {
                const voice::Signal& y = notes.synth().render_drum(event.name, event.v, p.round_robin);
                mix.add(name, y, p.start, event.pan, 1.0);
            }
        }
    }
    voice::Stereo b = std::move(mix.bus(name));
    rev = 0.15;
    const BusSpec* spec = bus_spec(name);
    if (spec == nullptr) {
        return b;
    }
    const double cur = active_rms_db(b);
    const double gain = std::pow(10.0, ((*spec).level - cur) / 20);
    for (std::size_t i = 0; i < length; ++i) {
        b.left[i] = b.left[i] * gain;
        b.right[i] = b.right[i] * gain;
    }
    if ((*spec).delay_beats != 0) {
        const long long delay = static_cast<long long>(voice::python_round((*spec).delay_beats * static_cast<double>(spb)));
        add_pingpong(b, delay, (*spec).delay_feedback, (*spec).delay_mix);
    }
    if ((*spec).lp != 0) {
        filter_channels(b, (*spec).lp);
    }
    rev = (*spec).rev;
    return b;
}

void soft_limit_channel(voice::Signal& x, double ceiling) {
    const double knee = 0.7 * ceiling;
    for (double& value : x) {
        const double a = std::fabs(value);
        if (a > knee) {
            const double direction = value > 0 ? 1.0 : (value < 0 ? -1.0 : 0.0);
            value = direction * (knee + (ceiling - knee) * std::tanh((a - knee) / (ceiling - knee)));
        }
    }
}

voice::Stereo switchbox_loop() {
    const Ctx ctx = arrange();
    std::vector<Event> events = ctx.ev;
    std::stable_sort(events.begin(), events.end(), earlier);
    const long long spb = static_cast<long long>(voice::python_round(voice::sample_rate * 60 / bpm));
    const std::size_t length = static_cast<std::size_t>(voice::python_round(ctx.total * static_cast<double>(spb)));
    const double sec_beat = static_cast<double>(spb) / voice::sample_rate;
    // where each event lands: timing jitter and drum round-robins, drawn in time order
    voice::Generator jitter_rng(song_seed + 99);
    std::vector<Placed> placed{};
    std::vector<std::pair<std::string, long long>> round_robins{};
    std::vector<std::string> bus_order{};
    for (std::size_t i = 0; i < events.size(); ++i) {
        const Event& event = events[i];
        const double jit = jitter_rng.normal(0, event.kind == Kind::note ? 0.0025 : 0.0015);
        const double tt = swing_pos(event.t, 0.0);
        const double at = tt * static_cast<double>(spb) + jit * voice::sample_rate;
        const long long s = static_cast<long long>(voice::python_round(at));
        long long rr = 0;
        if (event.kind == Kind::drum) {
            bool found = false;
            for (std::pair<std::string, long long>& entry : round_robins) {
                if (entry.first == event.name) {
                    entry.second += 1;
                    rr = entry.second;
                    found = true;
                }
            }
            if (!found) {
                round_robins.emplace_back(event.name, 1);
                rr = 1;
            }
        }
        placed.push_back(Placed{i, s, rr});
        if (std::find(bus_order.begin(), bus_order.end(), event.bus) == bus_order.end()) {
            bus_order.push_back(event.bus);
        }
    }
    // compose.master, one bus at a time in the order the mix created them
    voice::Stereo dry{voice::Signal(length, 0.0), voice::Signal(length, 0.0)};
    voice::Stereo send{voice::Signal(length, 0.0), voice::Signal(length, 0.0)};
    for (const std::string& name : bus_order) {
        double rev = 0.15;
        const voice::Stereo b = master_bus(events, placed, name, length, sec_beat, spb, rev);
        for (std::size_t i = 0; i < length; ++i) {
            dry.left[i] += b.left[i];
            dry.right[i] += b.right[i];
            send.left[i] += b.left[i] * rev;
            send.right[i] += b.right[i] * rev;
        }
    }
    // the reverb send, the 28 Hz high-pass and the presence shelf, one channel at a time
    const voice::Stereo ir = voice::make_ir(2.6, 1.6, 1.3, .6, .018, 7);
    for (int channel = 0; channel < 2; ++channel) {
        voice::Signal& d = channel == 0 ? dry.left : dry.right;
        voice::Signal& s = channel == 0 ? send.left : send.right;
        {
            const voice::Signal wet = circular_reverb(s, channel == 0 ? ir.left : ir.right);
            s = voice::Signal{};
            for (std::size_t i = 0; i < length; ++i) {
                d[i] = d[i] + wet[i] * wet_level;
            }
        }
        d = voice::fft_filter(d, 28, 0, 2, 4096, true);
        d = presence(d, presence_db);
    }
    voice::Stereo out = std::move(dry);
    // render_music.py: a 0.5 peak, then -20 LUFS from ffmpeg's measurement of that, then
    // the soft limiter above 70 % of a -3 dBFS ceiling
    const double top = std::max(voice::peak(out.left), voice::peak(out.right));
    const double loudness_gain = std::pow(10.0, (-20.0 - measured_loudness) / 20);
    for (std::size_t i = 0; i < length; ++i) {
        out.left[i] = out.left[i] / top * 0.5 * loudness_gain;
        out.right[i] = out.right[i] / top * 0.5 * loudness_gain;
    }
    const double ceiling = std::pow(10.0, -3.0 / 20);
    if (std::max(voice::peak(out.left), voice::peak(out.right)) > 0.7 * ceiling) {
        soft_limit_channel(out.left, ceiling);
        soft_limit_channel(out.right, ceiling);
    }
    return out;
}

constexpr voice::Cue cues[] = {
    {.name = "music_switchbox_loop", .stereo = switchbox_loop},
};

}  // namespace

std::span<const voice::Cue> music_cues() {
    const std::span<const voice::Cue> result(cues);
    return result;
}

}  // namespace batch_cues
