#pragma once
// The game's music: a bluegrass band making up fiddle tunes as it plays. A five-string
// banjo in open G (gDGBD) leads. In the Scruggs styles it picks steady sixteenth-note
// rolls with three fingers (forward, backward, alternating thumb, forward-reverse), the
// melody accented inside the roll on the 3-3-2 beats, the short fifth string droning
// its high g, the fretting hand hammering on, pulling off and sliding, and every part
// closing on the G lick. In the clawhammer style it frails "bum-ditty": a melody note
// struck down with the nail, a brush, the thumb on the drone. Tunes are fiddle-tune
// shaped, AABB in eight-bar parts over I, IV and V; between tunes the band rests or
// plays a quieter passage. An upright bass and a guitar's boom-chuck sit underneath,
// and in the full bluegrass style a mandolin chops the off-beat; all of them softer
// than the banjo.
//
// The banjo's strings are plucked delay lines (Karplus-Strong) with a bright loop, a
// little stiffness (an allpass in the loop sharpens the upper partials), a pitch that
// starts a touch sharp and settles (tension), and a loud start that falls away fast.
// They drive the head: a bank of resonators for the drum's modes, which gives the
// nasal honk, with a click of the pick or nail at each note. A resonator banjo is
// brighter and punchier; an open-back one is plunkier and darker.
#include <cstdint>
#include <span>
#include <vector>

namespace mm {

// Which band plays. Scruggs is full bluegrass on a resonator banjo; clawhammer is a
// mellower open-back banjo frailing old-time tunes; porch (the game's) is Scruggs
// rolling at an easier tempo with a quieter band and gentler passages between tunes.
enum class BanjoStyle { scruggs = 0, clawhammer = 1, porch = 2 };

class BanjoVoice final {
  public:
    static constexpr int sample_rate = 48000;
    // Parts, for solo(): the banjo, the upright bass, the guitar, the mandolin.
    static constexpr int part_banjo = 0;
    static constexpr int part_bass = 1;
    static constexpr int part_guitar = 2;
    static constexpr int part_mandolin = 3;

    explicit BanjoVoice(std::uint32_t seed = 21, BanjoStyle style = BanjoStyle::porch);
    // Adds the band to interleaved stereo frames at the given gain (glided).
    void render_add(std::span<float> stereo, double gain);
    // For tools: hear one part alone, or -1 for the whole band.
    void solo(int part) {
        solo_ = part;
    }
    // For tools and tests.
    [[nodiscard]] int tunes_played() const {
        return tunes_;
    }
    [[nodiscard]] long notes_picked(int part) const {
        return picked_[part];
    }
    [[nodiscard]] double tempo() const {
        return tempo_;
    }

    struct String {
        std::vector<float> line{};
        int write{};
        double delay{100};       // samples, glides on a slide or a hammer
        double delay_to{100};
        double glide{};          // per-sample approach to delay_to, 0 when settled
        double bend{};           // the tension's sharpening, fraction of pitch
        float last{};
        float ap_x{};            // the stiffness allpass
        float ap_y{};
        float loss{0.996F};      // per-period damping
        float bright{0.5F};      // loop low-pass: higher is brighter
        float stiff{};           // allpass coefficient (negative sharpens)
        float punch{};           // the extra loudness of a fresh pluck, decaying
        float punch_decay{0.999F};
        double mute_in{-1};      // seconds until a damp, -1 none
        float mute_loss{0.93F};
        int sounding{-1};        // the note it rings, for damping on a chord change
        int quiet{1 << 20};      // samples it has been silent
        int pending{-1};         // samples until a scheduled pluck (a strum's spread)
        double pending_midi{};
        double pending_strength{};
        double pending_hardness{};
        double pending_where{};
    };

    struct Chord {
        int root{};  // semitones above the key's tonic
        int kind{};  // 0 major, 1 minor, 2 dominant seventh
    };

    // One sixteenth of a bar of banjo: what is played on which string.
    struct Slot {
        int string{};       // 1 first .. 5 drone, 0 rest
        int midi{};
        int pinch{};        // a second string picked at once (0 none)
        int pinch_midi{};
        int how{};          // see banjo_voice.cpp: pick, hammer, slide, pull, brush
        double strength{};
    };

  private:
    double uniform();
    int pick(int n);
    [[nodiscard]] double part_gain(int part) const {
        return solo_ < 0 || solo_ == part ? 1.0 : 0.0;
    }
    void pluck_banjo(int string, int midi, double strength, double hardness_scale);
    void pluck(String& s, double midi, double strength, double hardness, double where, double keep);
    void set_pitch(String& s, double midi, double seconds);
    float run(String& s);
    void step_slot();
    void plan_tune();
    void plan_bar();
    void plan_lick(int half);
    void plan_scruggs_bar(bool gentle);
    void plan_clawhammer_bar();
    void place_melody(int slot, int midi, double strength, bool ornament);
    int fret_note(int string, const Chord& chord) const;
    int drone() const;
    bool tone_in(int midi, const Chord& chord) const;
    bool in_scale(int midi) const;
    int nearest_chord_tone(int midi, const Chord& chord) const;
    int step_scale(int midi, int steps) const;
    void play_slot(const Slot& slot);
    void play_band(int slot);

    std::uint32_t state_;
    BanjoStyle style_;
    int solo_{-1};
    String banjo_[5]{};
    String bass_{};
    String guitar_[6]{};
    String mandolin_[4]{};
    std::vector<float> burst_{};  // scratch for a pluck, kept so the audio thread never allocates

    // the head: modal resonators under the bridge
    static constexpr int head_modes = 7;
    double head_y1_[head_modes]{};
    double head_y2_[head_modes]{};
    double head_a1_[head_modes]{};
    double head_a2_[head_modes]{};
    double head_g_[head_modes]{};
    double head_direct_{};
    double head_top_{};          // the head's high end: one-pole coefficient
    double head_lift_{};         // its radiation's rise to the top
    double head_last_{};
    double head_tone_{};
    // the pick or nail on the string, and the hand on the head
    double click_env_{};
    double click_y1_{};
    double click_y2_{};
    double click_a1_{};
    double click_a2_{};
    double thump_env_{};
    double thump_y1_{};
    double thump_y2_{};
    // the guitar's and the bass's warmth, and a little room
    double guitar_body_{};
    double bass_tone_{};
    std::vector<float> room_{};
    int room_write_{};
    double hp_left_{};
    double hp_right_{};
    double gain_{};

    // the score
    Slot bar_plan_[8]{};
    Chord chord_{};
    Chord progression_[2][8] = {};  // the A and B parts
    int melody_[2][8][3] = {};   // the melody's three accents in each bar of each part
    int key_{};                  // semitones above G
    int part_{};                 // 0 A, 1 B
    int pass_{};                 // first or second time through the part
    int bar_{};
    int slot_{};
    int section_{};              // 0 a tune, 1 a quiet passage, 2 a rest between
    int rest_bars_{};
    int hand_{};                 // the fretting hand's position
    bool licking_{};             // this pass ends on the G lick
    double clock_{};             // seconds until the next sixteenth
    double tempo_{116};
    double tune_tempo_{116};
    double loud_{0.9};
    double tune_loud_{0.9};
    int tunes_{};
    long picked_[4]{};
    double swing_{};             // the on-beat sixteenth's share of each pair
};

} // namespace mm
