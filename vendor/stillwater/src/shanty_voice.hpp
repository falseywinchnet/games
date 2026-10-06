#pragma once
// Stillwater's music: a small pirate band below decks that never stops making up
// shanties. A concertina carries the tune and squeezes the off-beat chords, a fiddle
// answers it (or doubles it an octave up in a chorus), a bass fiddle and a boot on
// the boards keep the 6/8 lilt, and a low drone holds the key under the slow verses.
// The tune is D Dorian, the old sea mode: a minor third with a bright sixth.
//
// It plays in verses: each verse picks a chord progression, a tempo, a lead and a
// two-bar motif, then sings eight bars from it (question, answer, the motif again
// varied, home on the tonic). Between verses the tempo and loudness ease rather
// than jump. Everything is synthesized here, far faster than real time.
#include <cstdint>
#include <span>
#include <vector>

namespace sw {

class ShantyVoice final {
  public:
    static constexpr int sample_rate = 48000;
    explicit ShantyVoice(std::uint32_t seed = 1720);
    // Adds the band to interleaved stereo frames at `gain`.
    void render_add(std::span<float> stereo, double gain);
    // The verse being played (for tools and tests).
    [[nodiscard]] int verses_played() const {
        return verses_;
    }

    // One reed or string voice: a band-limited sawtooth pair, slightly apart in
    // pitch (a concertina's two reeds beat against each other), shaped by an envelope.
    struct Reed {
        double phase_a{};
        double phase_b{};
        double hz{};
        double target_hz{};
        double detune{1.0};     // ratio of the second reed
        double level{};         // envelope
        double peak{};          // envelope target while held
        double attack{0.004};   // per-sample approach while rising
        double release{0.0015}; // per-sample decay after the note
        double hold{};          // seconds left before the release
        double vibrato_phase{};
        double vibrato_depth{};  // fraction of pitch
        double vibrato_delay{};  // seconds before vibrato enters
        double age{};
        double low{};            // one-pole tone filter state
        double tone{0.25};       // filter coefficient: higher is brighter
        float pan{};
    };

    struct Chord {
        int root{};  // semitones above D
        int minor{};
    };

    struct Verse {
        Chord bars[8]{};
        double tempo{70};  // dotted quarters a minute
        double loud{1};
        int style{};       // 0 jig, 1 chorus (fiddle doubles), 2 slow verse (drone, no stomp)
    };

  private:
    double uniform();
    int pick(int n);
    void plan_verse();
    void make_motif();
    void step_eighth();
    int melody_note(int bar, int eighth, bool strong);
    bool chord_tone(int midi, const Chord& chord) const;
    int nearest_scale(int midi, int direction) const;
    void sound_note(Reed& reed, int midi, double seconds, double strength);
    float run_reed(Reed& reed);
    void stomp(double strength);

    std::uint32_t state_;
    Reed lead_{};        // concertina right hand
    Reed fiddle_{};
    Reed chord_[3]{};    // concertina left hand
    Reed bass_{};
    Reed drone_[2]{};
    // the boot on the boards
    double stomp_phase_{};
    double stomp_level_{};
    double stomp_hz_{};
    double knock_level_{};
    double knock_low_{};
    // fiddle body: two resonant bands
    double body_y1_[2]{};
    double body_y2_[2]{};
    double body_a1_[2]{};
    double body_a2_[2]{};
    double body_g_[2]{};
    // a small room: two feedback delays per side
    std::vector<float> room_{};
    int room_write_[4]{};
    double room_low_[4]{};
    double dc_left_{};
    double dc_right_{};
    double dc_in_left_{};
    double dc_in_right_{};

    // the score
    Verse verse_{};
    int verses_{};
    int last_style_{-1};
    int bar_{};
    int eighth_{};
    double clock_{};       // seconds until the next eighth
    double tempo_{70};
    double loud_{0.8};
    int lead_is_fiddle_{};
    int melody_{62};       // the last melody note
    int motif_[12]{};      // two bars of scale steps (relative), -99 for a held note
    int rhythm_[4]{};      // per half bar: 0 dotted quarter, 1 quarter and eighth, 2 three eighths, 3 eighth and quarter
    int phrase_rhythm_[16]{};
};

} // namespace sw
