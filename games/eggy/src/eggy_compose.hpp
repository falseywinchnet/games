#pragma once
// The batch composer Eggy's soundtrack is written on (authoring/audio/work/src/compose.py,
// with the helpers eggy_music.py takes from songs.py and songs2.py): a song spec becomes
// seeded note events, and the events a seamless stereo loop through the shared synth
// engine. Ported expression by expression with numpy's random streams, so the same
// spec gives the same events and the same mix as the Python.
//
// Only what eggy_music.py uses is here (bass 'two' and 'walk', comp 'sustain' and
// 'chunk', no harmony line, no autopan or bus low-pass, no brush swishes).
// Not thread-safe per object; standard library only.
#include "eggy_synth.hpp"

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace eggy::compose {

struct Section {
    std::string name;
    std::vector<std::string> bars;
    std::set<std::string> layers;
    std::string lead{};
    int center = 72;
    double e = 1.0;
};

struct Bus {
    std::string name;
    std::optional<double> level{};
    double rev = .15;
    std::optional<std::pair<double, double>> delay{};  // (beats, feedback)
    double delay_mix = .5;
};

struct Room {
    double rt_low = 2.0;
    double rt_mid = 1.6;
    double rt_high = 0.8;
};

struct Song {
    std::string id;
    double bpm;
    int bpb;
    double swing;
    int key;
    std::vector<int> scale;
    int seed;
    std::string bass_inst = "bass";
    std::vector<Section> sections;
    std::vector<Bus> bus;
    double wet = .5;
    Room ir{};
    double presence_db = 3.0;
};

struct Chord {
    int root;
    std::vector<int> iv;
    std::string q;
    int bass;
    std::vector<int> pcs;  // sorted
};

struct ChordSpan {
    double start;
    double dur;
    Chord chord;
    int section;
};

struct Event {
    char kind;  // 'n' note, 'd' drum
    double t;
    std::string name;
    int m;
    double d;
    double v;
    double pan;
    std::string bus;
};

using Rhythm = std::vector<std::pair<double, double>>;  // (onset beat, duration beats)

class Ctx {
  public:
    explicit Ctx(const Song& song);  // songs2.new_ctx: rng = seed, mrng = mel_seed or seed + 1

    struct SectionSpan {
        double start;
        int nbars;
        const Section* spec;
    };

    const Song& s;
    int bpb;
    voice::Generator rng;
    voice::Generator mrng;
    std::vector<Event> ev{};
    std::vector<ChordSpan> chords{};
    std::vector<SectionSpan> secs{};
    double total = 0;
    std::vector<double> starts{};

    // chord_at(t): the index into `chords` (Python compares these dicts by identity).
    [[nodiscard]] std::size_t chord_index(double t) const;
    [[nodiscard]] const ChordSpan& chord_at(double t) const {
        return chords[chord_index(t)];
    }
    [[nodiscard]] std::vector<int> scale_at(double t) const;
    void note(double t, const std::string& inst, int m, double d, double v, double pan = 0.0, const std::string& bus = "");
    void drum(double t, const std::string& name, double v, double pan = 0.0, const std::string& bus = "drums");
};

[[nodiscard]] Chord parse_chord(const std::string& s);
[[nodiscard]] int near(int pc, int target);
[[nodiscard]] int scale_step(int m, int k, const std::vector<int>& sc);
[[nodiscard]] std::optional<std::vector<int>> voicing(const Chord& ch, const std::optional<std::vector<int>>& prev, int lo,
                                                      int hi, bool rootless = true, bool add9 = true, std::size_t maxn = 4);

void melody(Ctx& ctx, double sec_start, int nbars, const std::string& inst, int center, const std::vector<Rhythm>& rhythms,
            const std::vector<Rhythm>& cadences, double vel = 0.7, double art = 0.92);
void counter(Ctx& ctx, double s0, int nbars, const std::string& inst, int center, double vel, double pan,
             const std::string& bus, double delay = 0.0, double art = 0.95);
void bass_line(Ctx& ctx, double s0, int nbars, const std::string& kind, int lo = 33, double vel = 0.7,
               const std::string& bus = "bass");
void comp(Ctx& ctx, double s0, int nbars, const std::string& kind, const std::string& inst, int lo, int hi, double vel,
          double pan = 0.0, const std::string& bus = "comp", bool rootless = true);
void arp(Ctx& ctx, double s0, int nbars, const std::string& inst, int lo, int hi, double vel, const std::vector<int>& pattern,
         double step = 0.5, double pan = 0.0, const std::string& bus = "arp", double dur = 0);

// songs2.py
struct Hit {
    std::string name;
    double t;
    double v;
    double p;
};
using DrumBar = std::vector<Hit>;
void pdrums(Ctx& ctx, double s0, int nb, const std::vector<DrumBar>& bars, double e = 1.0, const DrumBar* fill = nullptr);
struct OctaveStep {
    double t;
    int iv;
    double d;
};
void bass_oct(Ctx& ctx, double s0, int nb, const std::string& inst, int lo, double vel, const std::vector<OctaveStep>& pat);
void scatter(Ctx& ctx, double s0, int nb, const std::string& inst, int lo, int hi, double rate, double vel,
             const std::string& bus = "scatter", const std::vector<int>* scale = nullptr);

// render(song, ctx) followed by master(song, mix, spb): the loop before eggy_music.py's
// levelling. See eggy_compose.cpp for how the work is arranged to keep it quick and
// small; the arithmetic is the script's.
[[nodiscard]] voice::Stereo mixdown(const Song& song, const Ctx& ctx, EggySynth& synth);

}  // namespace eggy::compose
