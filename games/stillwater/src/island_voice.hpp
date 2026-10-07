#pragma once
// Stillwater's music: an island band on an old library record, making up lazy
// Hawaiian tunes as it plays. A lap steel guitar sings the melody, sliding up into
// its notes with a wide slow vibrato and now and then a harmony a sixth below; a
// ukulele strums the island rhythm; an upright bass walks root and fifth; a
// vibraphone (motor on) and a marimba answer at the ends of phrases; a shaker
// keeps the swing, and a few bubbles rise past. The chords are the old island ones,
// sixths, major sevenths and chains of dominant sevenths, and every section is
// joined to the next by the Hawaiian vamp (II7, V7), which is also where the key
// moves. The whole band goes through a spring reverb and a worn tape: a narrow,
// warm, wobbling, nearly mono sound. Everything is synthesized here, far faster
// than real time; the tunes are its own.
#include <cstdint>
#include <span>
#include <vector>

namespace sw {

class IslandVoice final {
  public:
    static constexpr int sample_rate = 48000;
    explicit IslandVoice(std::uint32_t seed = 1720);
    // Adds the band to interleaved stereo frames at `gain`.
    void render_add(std::span<float> stereo, double gain);
    // Sections played so far (for tools and tests).
    [[nodiscard]] int verses_played() const {
        return verses_;
    }
    // For tools: hear one part alone (0 steel, 1 mallets, 2 ukulele, 3 bass,
    // 4 shaker and bubbles), or -1 for the whole band.
    void solo(int part) {
        solo_ = part;
    }

    // A struck or plucked tone made of a few partials, each with its own ratio,
    // level and decay: the steel guitar, the vibraphone, the marimba and the bass.
    struct Tone {
        double hz{};
        double target_hz{};
        double glide{};          // per-sample approach of hz to target_hz (a slide)
        double phase[6]{};
        double amp[6]{};
        double ratio[6]{};
        double decay[6]{};       // per-sample multiplier
        int partials{};
        double attack{};         // per-sample rise of the envelope
        double env{};
        double age{};
        double vibrato_phase{};
        double vibrato_depth{};  // fraction of pitch, reached after vibrato_delay
        double vibrato_delay{};
        double tremolo_phase{};
        double tremolo_depth{};  // the vibraphone's motor
        double release_in{-1};   // seconds until damped, -1 to ring
        double damp{1};          // per-sample multiplier once released
        double level{};
        float pan{};
    };

    // A plucked string (Karplus-Strong): the ukulele.
    struct Pluck {
        std::vector<float> line{};
        int write{};
        double delay{100};
        float last{};
        float loss{0.995F};
        float bright{0.5F};
        double mute_in{-1};
        int pending{-1};         // samples until a scheduled pluck (a strum's spread)
        double pending_midi{};
        double pending_strength{};
        float pan{};
    };

    struct Chord {
        int root{};  // semitones above the key
        int kind{};  // 0 major sixth, 1 major seventh, 2 dominant seventh, 3 minor seventh, 4 minor sixth
    };

    struct Section {
        const char* name{};
        Chord bars[8]{};
        int lead{};    // 0 steel, 1 vibraphone, 2 steel in sixths
        double tempo{92};
        double loud{1};
    };

  private:
    double uniform();
    [[nodiscard]] double part_gain(int part) const {
        return solo_ < 0 || solo_ == part ? 1.0 : 0.0;
    }
    int pick(int n);
    void plan_section();
    void step_eighth();
    bool chord_tone(int midi, const Chord& chord) const;
    int chord_pc(const Chord& chord, int index) const;
    int lead_note(int previous, const Chord& chord, bool strong);
    void steel(Tone& tone, int midi, double seconds, double strength, double slide, bool swell);
    void vibes(Tone& tone, int midi, double strength, bool marimba);
    float run_tone(Tone& tone);
    void pluck_now(Pluck& string, double midi, double strength);
    float run_pluck(Pluck& string);
    void strum(const Chord& chord, bool down, double strength);
    void upright(int midi, double seconds);
    void bubble();

    std::uint32_t state_;
    Tone steel_[2]{};      // the melody and its harmony a sixth below
    Tone mallets_[6]{};    // vibraphone and marimba notes, round robin
    int mallet_next_{};
    Pluck uke_[4]{};
    int uke_pos_{};            // the fretting hand's position this section
    double uke_body_[2][2]{};  // the body's two resonances
    double uke_tone_{};
    double brush_env_{};       // the fingers brushing the strings
    double brush_hz_{2600};
    double brush_y1_{};
    double brush_y2_{};
    Tone bass_{};
    double thump_env_{};
    double thump_low_{};
    std::vector<float> scratch_{};
    // shaker
    double shake_env_{};
    double shake_hp_{};
    double shake_prev_{};
    // bubbles
    double bubble_wait_{2};
    double bubble_hz_{};
    double bubble_rise_{};
    double bubble_env_{};
    double bubble_phase_{};
    double bubble_pan_{};
    int bubble_more_{};
    double bubble_gap_{};
    // the spring reverb: dispersion allpasses, then two damped feedback delays
    std::vector<float> allpass_[4]{};
    int allpass_at_[4]{};
    std::vector<float> spring_[2]{};
    int spring_at_[2]{};
    double spring_low_[2]{};
    // the tape: wow and flutter through a moving delay, its band and its hiss
    std::vector<float> tape_[2]{};
    int tape_at_{};
    double wow_phase_{};
    double flutter_phase_{};
    double hp_[2]{};
    double lp1_[2]{};
    double lp2_[2]{};
    double hiss_low_{};

    // the score
    Section section_{};
    int verses_{};
    int last_section_{-1};
    int key_{0};           // semitones above C
    int next_key_{0};
    bool vamp_{};          // the two-bar vamp into the next section
    int bar_{};
    int eighth_{};
    double clock_{};
    double tempo_{92};
    double loud_{0.9};
    int melody_{72};
    int strum_pattern_{};
    int solo_{-1};
    int lead_len_[8]{};    // this bar's melody: eighths each note lasts, 0 for none
};

} // namespace sw
