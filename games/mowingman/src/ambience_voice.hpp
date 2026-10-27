#pragma once
// The garden's own sounds, synthesized as they play: bees droning as they come and go,
// four kinds of bird singing from the trees, and, once the engine is off, the fountain,
// wind in the trees, crickets and a frog. Driven by a few numbers the scene posts each
// frame; everything else is this voice's own.
#include <cstdint>
#include <span>

namespace mm {

// One bee as the garden hears it: which bee (its id keeps its own voice), whether its
// wings are going, how near it is and where, and how its pitch is bent by its speed
// (working harder) and by the Doppler shift of its coming and going.
struct BeeSound {
    std::uint32_t id{};     // 0: no bee
    bool flying{};
    double gain{};          // 0 .. 1, from its distance
    double pan{};           // -1 .. 1
    double pitch{1};        // multiplies its wingbeat
};

struct AmbienceControls {
    bool engine_on{true};
    static constexpr int max_bees = 4;
    BeeSound bees[max_bees]{};
    bool singing[4]{};      // a bird of each kind is singing: robin, blue tit, blackbird, sparrow
    double bird_pan[4]{};
    bool fountain{};
    double trees{};         // 0 .. 1: how much canopy there is for the wind
    bool water{};           // somewhere for a frog: a pool, pond or bath
    bool dusk{true};        // crickets
};

class AmbienceVoice final {
  public:
    static constexpr int sample_rate = 48000;
    explicit AmbienceVoice(std::uint32_t seed = 11);
    void set(const AmbienceControls& controls);
    // Adds the garden to interleaved stereo frames.
    void render_add(std::span<float> stereo);

  private:
    struct Phrase {
        double wait{};      // seconds until the next phrase
        int note{-1};       // the note being sung, -1 between notes
        double note_time{};
        double level{};
        double phase{};
    };
    double uniform();
    void sing(int kind, Phrase& phrase, double dt, double& left, double& right);
    std::uint32_t state_;
    AmbienceControls want_{};
    double quiet_{};        // 0 engine running .. 1 engine off, glided
    // A bee's voice: its wingbeat, the harmonics the wing stroke makes, flutter noise.
    struct BeeVoice {
        std::uint32_t id{};
        double wingbeat{};      // Hz, its own: a bumblebee's is lower than a honeybee's
        double phase{};
        double gain{};          // glided
        double pan{};
        double pitch{1};
        double wings{};         // 0 landed .. 1 flying, glided
        double burst{};         // seconds left of a short buzz while it works a flower
        double burst_wait{};
        double wander{};        // slow random walk of its wingbeat, -1..1
        double harmonic[20]{};  // each harmonic's own slow wobble
        double cycle_gain{1};   // stroke-to-stroke variation
        double flutter_y1{};
        double flutter_y2{};
        double dc{};
    };
    void buzz(BeeVoice& voice, const BeeSound& bee, double dt, double& left, double& right);
    BeeVoice bee_voices_[AmbienceControls::max_bees]{};
    Phrase birds_[4]{};
    double cricket_phase_{};
    double cricket_wait_{};
    double cricket_left_{};
    double frog_wait_{};
    double frog_left_{};
    double frog_phase_{};
    // A two-pole band on noise: one small turbulent source.
    struct Band {
        double y1{}, y2{}, a1{}, a2{}, g{1};
        void tune(double hz, double q);
        double run(double x);
    };
    struct Drop {
        double env{};
        double decay{0.999};
        Band band{};
    };
    double normal();
    // wind
    double gust_{0.2};
    double gust_target_{0.3};
    double gust_wait_{};
    double gust_level_{0.2};
    double hiss_hi_{};
    double hiss_lo_{};
    double push_{};
    double wind_left_{};
    double wind_right_{};
    Band flick_{};
    // the fountain: a fine spray of drops, a little rain on the pool, a faint gurgle
    Drop drops_[24]{};
    int drop_slot_{};
    Band gurgle_a_{};
    Band gurgle_b_{};
    double gurgle_walk_{0.5};
    double gurgle_target_{0.5};
    double gurgle_wait_{};
    Band rain_{};
    double hiss_{};
};

} // namespace mm
