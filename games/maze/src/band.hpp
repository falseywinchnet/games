#pragma once
// The maze's music, played live: a band that makes up its tunes as it plays, in one of
// three styles chosen for each level (after the late-90s action-game music the owner
// pointed to: major keys, a punchy bass, busy hats, a lead that runs and slides).
//
//   Maze Run     synth rock, A major, 98 BPM: octave bass, power chords, a sliding lead,
//                brass stabs, a D-C-G middle and an F-G-A climb back to the top.
//   Bonus Round  a romp, C major, 124 BPM: oom-pah pizzicato and marimba, celesta,
//                clarinet and flute passing the tune round, brass hits, woodblocks.
//   Turbo        a funk groove, B-flat, 112 BPM: slap bass, Rhodes stabs, a brass
//                section, vibraphone and lead trading phrases over a breakbeat.
//
// A tune runs through its style's form (intro, the tune, again with harmony, a middle, a
// break, the tune, the middle, a tag), about two minutes. The first tune of a level plays
// the style's own themes; later ones make new melodies over the same chords, in the
// themes' rhythms, and move the key. In a blackout the band is heard through a wall (a
// low-pass and a long, dull room); on the ceiling it plays backwards, a bar at a time.
//
// Everything is synthesized here, sample by sample, far faster than real time.
// render_add() runs on the audio thread; play(), muffle() and backwards() may be called
// from any thread and take effect at the next bar (the style) or at once (the rest).
#include <array>
#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

namespace mz {

enum class Style { maze_run, bonus_round, turbo, count };

class MazeBand final {
  public:
    static constexpr int sample_rate = 48000;
    MazeBand(std::uint32_t seed, Style style);

    // The style and a seed for the next level's tunes; the band moves to them at the next bar.
    void play(Style style, std::uint32_t seed);
    void muffle(bool on);      // the blackout
    void backwards(bool on);   // the ceiling
    // Adds the band to interleaved stereo frames at `gain`.
    void render_add(std::span<float> stereo, double gain);

    enum class Bus { bass, drums, hats, cymbals, guitar, guitar2, arp, brass, lead, lead2, keys, mallets, mallets2, count };
    static constexpr int bus_count = static_cast<int>(Bus::count);
    // For tools: bars and tunes played so far, and each bus's power since the start
    // (mean square of its mono signal before the mix), for calibrating the levels.
    [[nodiscard]] int bars_played() const { return bars_; }
    [[nodiscard]] int tunes_played() const { return tunes_; }
    [[nodiscard]] double bus_power(Bus bus) const;
    void measure_raw(bool on) { raw_ = on; }   // every bus at unity and no master gain

    enum class Kind {
        sqbass, lead, gtr, brass, bpluck, slap, ep, marimba, pizz, celesta, vibes, clarinet, flute,
        kick, snare, hat, ohat, crash, tom_hi, tom_mid, tom_lo, wood_hi, wood_lo, clap, shaker, clave
    };
    enum class Part { intro, verse, middle, breakdown, stop, tag };

    // A note of a tune: on a beat of its bar, semitones above the key, for so many beats.
    struct Note {
        double beat{};
        int step{};
        double beats{};
    };
    using Phrase = std::vector<std::vector<Note>>;  // one list of notes per bar

    struct Chord {
        int root{};               // semitones above the key
        std::array<int, 4> tones{};  // above the root
        int count{};
    };

    // How one voice of the tune is played in a section.
    struct Line {
        Kind kind{Kind::lead};
        Bus bus{Bus::lead};
        double vel{.7};
        double pan{};
        int octave{};
        double legato{.92};
        int mode{};   // 0 as written, 1 a third above, 2 only the notes on the beat, 3 long notes answered
    };

    struct Section {
        Part part{Part::verse};
        int time{};               // how many times this part has come round in the tune
        std::vector<Chord> bars{};
        Phrase tune{};            // empty: no melody
        std::vector<Line> lines{};
    };

    struct Event {
        int at{};          // samples into the bar
        Kind kind{};
        double midi{};
        int length{};      // samples the note is held
        double vel{};
        double pan{};
        Bus bus{};
        double slide{};    // semitones below, gliding up into the note
    };

    struct Voice {
        bool on{};
        Kind kind{};
        Bus bus{};
        double hz{};
        double vel{};
        double gain_l{}, gain_r{};
        int age{};
        int length{};      // samples held
        int life{};        // samples until it is done
        double slide{};
        double phase[8]{};
        double partial_hz[8]{};
        double s1{}, s2{}, s3{}, s4{}, s5{}, s6{};  // filter states
        std::uint32_t noise{};
    };

  private:
    double uniform();
    double jitter(double spread);
    void start_tune();
    void plan_bar();
    void arrange_run(const Section& section, int index, const Chord& chord, bool last_of_section);
    void arrange_bonus(const Section& section, int index, const Chord& chord, bool last_of_section);
    void arrange_turbo(const Section& section, int index, const Chord& chord, bool last_of_section);
    void rock_beat(bool half, bool open_hat);
    void fill();
    void play_lines(const Section& section, int index);
    Phrase make_tune(const std::vector<Chord>& bars, const Phrase& rhythms, int low, int high);
    void put(double beat, Kind kind, double midi, double beats, double vel, double pan, Bus bus, double slide = 0);
    void hit(double beat, Kind kind, double vel, double pan, Bus bus);
    void start_voice(const Event& event);
    double run_voice(Voice& voice);
    void reverb_setup();

    // the score
    std::uint32_t state_;
    Style style_;
    std::atomic<int> wanted_style_;
    std::atomic<std::uint32_t> wanted_seed_;
    std::atomic<int> wanted_change_{0};
    int change_seen_{0};
    std::vector<Section> form_{};
    int section_{};
    int section_bar_{};
    int key_{};
    int tunes_{};
    int bars_{};
    double bpm_{98};
    double swing_{};
    double loose_{};
    int beat_samples_{};
    int bar_samples_{};
    int bar_at_{};
    std::vector<Event> events_{};
    std::size_t next_event_{};

    // the players
    std::array<Voice, 96> voices_{};
    std::array<double, bus_count> bus_l_{}, bus_r_{};
    std::array<double, bus_count> bus_power_{};
    long long measured_{};
    bool raw_{};

    // the room, the echo, the wall and the master
    std::array<std::vector<double>, 4> room_{};
    std::array<int, 4> room_at_{};
    std::array<double, 4> room_low_{};
    double room_feedback_[4]{};
    double room_scale_{1};
    std::array<double, 3> room_scales_{};   // per style, for a unit-energy response
    std::array<std::vector<double>, 4> wall_{};
    std::array<int, 4> wall_at_{};
    std::array<double, 4> wall_low_{};
    double wall_feedback_[4]{};
    double wall_scale_{1};
    std::vector<double> echo_l_{}, echo_r_{};
    int echo_at_{};
    int echo_length_{1};
    double echo_low_l_{}, echo_low_r_{};
    double echo_hp_l_{}, echo_hp_r_{};

    // the blackout and the ceiling
    std::atomic<bool> wanted_muffle_{false};
    std::atomic<bool> wanted_backwards_{false};
    double muffle_{};
    double back_{};
    double dull_[4]{};
    double top_[4]{};      // the master's top end
    std::vector<float> history_{};   // the last few bars, stereo interleaved, a ring
    long long written_{};
    long long read_at_{};            // reading backwards: the next frame
    long long fade_at_{};            // the bar before, continuing backwards under the fade
    int chunk_left_{};
    int fade_left_{};
};

}  // namespace mz
