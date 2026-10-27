#pragma once
// The tank's own sound, synthesized as it plays. The recipe is the native Stillwater's
// refined ambience and glass tap (its src/sound.cpp after commit 424bea9, the version
// its owner listened to and kept), carried over to 48 kHz with every filter set by its
// frequency rather than its sample-rate coefficient:
//
// - the filter's motor: a quiet 60 Hz harmonic series weighted toward 120 Hz, its
//   amplitude stirred by filtered mechanical noise, with a little of that noise heard;
// - the return water: a very quiet, continuous band of noise (about 40 to 600 Hz),
//   separate in each ear;
// - the air stone: bubbles formed at exponential waiting times, each a short damped
//   resonance at the Minnaert frequency of its radius (f ~ 3.26 / r), with no pitch
//   sweep, softened by a low-pass as the water would;
// - a fingertip on the glass: a dull 200 ms contact, a damped body and two faint
//   glass modes, placed where the glass was touched.
//
// Beyond the original: the flow density that paces the bubbles really wanders (the
// original's wander was a few thousandths), each tank has its own mix (a reef's
// stronger circulation; a river pool's current, its far filter and the odd bubble of
// gas from the silt), the bubbles sound from where the tank's bubbles rise, and each
// tap is a little different from the last. Nothing repeats, nothing is decoded or
// held in memory, and the callback allocates nothing.
#include <atomic>
#include <cstdint>
#include <span>

namespace sw {

// One tank's mix, as multiples of the planted tank's (the original's) levels.
struct TankMix {
    double motor{1};          // the filter's hum
    double water{1};          // the return water (or the river's current)
    double bubbles{24};       // mean bubbles a second at an even flow
    double flow_bubbles{9};   // added (or taken) as the flow wanders a full step
    double radius_low{0.0007};  // bubble radii in metres: low + span * u^2
    double radius_span{0.0025};
    double bubble_gain{1};
    double pan_low{0.25};     // where bubbles sound, 0 left .. 1 right
    double pan_span{0.40};
};

[[nodiscard]] const TankMix& tank_mix(int tank);

class TankVoice final {
  public:
    static constexpr int sample_rate = 48000;
    // The level the suite plays the tank at, over the original's own digital level
    // (which sat at -49 dBFS RMS beside nothing else): about 10 dB, still well under
    // the band and the game's effects.
    static constexpr double suite_level = 3.2;
    explicit TankVoice(std::uint32_t seed = 71137, int tank = 0);
    // Adds the tank to interleaved stereo frames at `gain` (1 is the original's level).
    void render_add(std::span<float> stereo, double gain);
    // From any thread. The tank whose water this is (0 planted, 1 reef, 2 river pool);
    // a change glides over about a second.
    void set_tank(int tank);
    // From any thread: a fingertip on the glass, -1 left .. 1 right. A tap the device
    // has not played within a third of a second (the sound was off) is dropped.
    void knock(double pan);
    // For tests: the number of taps played so far.
    [[nodiscard]] std::uint32_t knocks_played() const {
        return knocks_played_;
    }

  private:
    struct Resonance {
        double previous{};
        double current{};
        double coefficient{};
        double decay_squared{};
        double gain{};
        double pan{};
        unsigned int remaining{};
    };
    struct Knock {
        unsigned int frame{};
        unsigned int length{};
        bool active{};
        double left{};
        double right{};
        double gain{};
        double body_step{};
        double glass_step{};
        double bright_step{};
        double contact{};
        double low{};
        std::uint32_t noise{};
    };
    struct PendingKnock {
        std::atomic<float> pan{0};
        std::atomic<std::int64_t> at{0};  // steady-clock nanoseconds when posted
    };
    double random();
    double knock_random();
    void bubble();
    void take_knocks();
    void start_knock(double pan);

    std::uint32_t state_;
    std::uint32_t knock_state_;  // the taps draw their own numbers, leaving the water's as they were
    unsigned int until_bubble_{};
    Resonance bubbles_[24]{};
    Knock knocks_[4]{};
    // The motor: the 60 Hz fundamental's rotation, from which the harmonics follow.
    double motor_cos_{1};
    double motor_sin_{0};
    double step_cos_{};
    double step_sin_{};
    unsigned int renormalize_{};
    double motor_air_{};
    double water_left_{};
    double water_right_{};
    double water_low_left_{};
    double water_low_right_{};
    double bubble_left_{};
    double bubble_right_{};
    double flow_{};
    double flow_target_{};
    unsigned int flow_hold_{};
    double fade_{};
    // The mix in force and the one it glides to.
    TankMix mix_{};
    TankMix target_{};
    std::atomic<int> wanted_tank_{0};
    int tank_{0};
    bool first_tank_{true};  // until the first block: a new tank is taken at once
    PendingKnock pending_[4]{};
    std::atomic<std::uint32_t> posted_{0};
    std::uint32_t taken_{0};
    std::uint32_t knocks_played_{0};
};

} // namespace sw
