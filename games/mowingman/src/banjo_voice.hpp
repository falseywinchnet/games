#pragma once
// The game's music: a bluegrass band that never stops improvising. A five-string banjo
// in open G picks Scruggs rolls round the chords, finding a melody on the strong beats
// and throwing in hammer-ons, slides and the old G lick; an upright bass walks root and
// fifth; a flat-top guitar chops the off-beats. It plays in acts (a breakdown, a B part,
// a minor bridge, a quiet vamp, a turn in C or D) and moves from one to the next by a
// lick and a bass walk, easing its tempo and loudness rather than jumping.
//
// Every string is a plucked delay line (Karplus-Strong) with a fractional delay, so a
// note can slide; the banjo's strings ring through the twang of a tight drum head.
#include <cstdint>
#include <span>
#include <vector>

namespace mm {

class BanjoVoice final {
  public:
    static constexpr int sample_rate = 48000;
    explicit BanjoVoice(std::uint32_t seed = 21);
    // Adds the band to interleaved stereo frames at the given gain (glided).
    void render_add(std::span<float> stereo, double gain);

    struct String {
        std::vector<float> line{};
        int write{};
        double delay{100};       // samples, may glide (a slide)
        double delay_to{100};
        double glide{};          // per-sample approach to delay_to
        float last{};
        float loss{0.996F};      // per-period damping
        float bright{0.5F};      // loop low-pass mix: higher is brighter
        float pan{};
        float level{1};
        float ap_state{};
        double mute_in{-1};      // seconds until a damp, -1 none
    };

    struct Chord {
        int root{};  // semitones above G
        int kind{};  // 0 major, 1 minor, 2 dominant seventh
    };

    struct Act {
        const char* name{};
        Chord bars[16]{};
        int length{8};
        double tempo{124};
        double loud{1};
        int style{};  // 0 forward rolls, 1 mixed rolls, 2 melodic, 3 sparse vamp
        int bass{1};  // 0 none, 1 two-beat, 2 walking
        int guitar{1};
    };

  private:
    double uniform();
    int pick(int n);
    void pluck(String& s, double midi, double strength, double hardness);
    void slide_to(String& s, double midi, double seconds);
    float run(String& s);
    void step_eighth();
    void plan_act();
    int voice_on(int string, const Chord& chord, int pos) const;
    bool tone_in(int midi, const Chord& chord) const;
    int scale_degree_near(int midi, int dir, const Chord& chord, bool strong);

    std::uint32_t state_;
    String banjo_[5]{};
    String bass_{};
    String guitar_[6]{};
    std::vector<float> burst_{};  // scratch for a pluck, kept so the audio thread never allocates
    // the head: three resonant bands over the strings
    double head_y1_[3]{};
    double head_y2_[3]{};
    double head_a1_[3]{};
    double head_a2_[3]{};
    double head_g_[3]{};
    double lp_left_{};
    double lp_right_{};
    double dc_left_{};
    double dc_right_{};
    double gain_{};

    // the score
    Act act_{};
    int act_index_{-1};
    int key_{};           // semitones above G the act is played in
    int bar_{};
    int eighth_{};
    double clock_{};      // seconds until the next eighth
    double tempo_{124};
    double loud_{0.9};
    int roll_[8]{};
    int melody_{67};      // the last melody note
    int hand_{0};         // the fretting hand's position
    int lick_{-1};        // eighth within a lick, -1 none
    int lick_kind_{};
    int acts_played_{};
    int last_style_{-1};
};

} // namespace mm
