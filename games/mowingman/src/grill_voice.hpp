#pragma once
// A kettle grill knocked over, and the coals it spills burning. Synthesized as it plays:
// the bowl and its lid are banks of ringing sheet-steel partials, struck in the order a
// grill falls (the bowl hit, the lid flying off, the bowl landing, the lid bouncing and
// rattling to rest) while the coals scatter in a spray of small ticks. Then, as long as
// they burn, a low flutter of flame with dry pops through it, dying with the fire.
#include <array>
#include <cstddef>
#include <atomic>
#include <cstdint>
#include <span>

namespace mm {

class GrillVoice final {
  public:
    static constexpr int sample_rate = 48000;
    explicit GrillVoice(std::uint32_t seed = 13);
    // From the window thread. A grill goes over at `pan` (-1 left .. 1 right).
    void knock(double pan);
    // From the window thread: how hard the fire burns (0 out .. 1 full), and where.
    void set_fire(double strength, double pan);
    // Adds the grill to interleaved stereo frames (on the audio thread).
    void render_add(std::span<float> stereo);

  private:
    // A two-pole resonator: one partial of a struck piece of steel.
    struct Mode {
        double c1{}, c2{}, gain{}, y1{}, y2{};
        double run(double x) {
            const double y = c1 * y1 - c2 * y2 + x;
            y2 = y1;
            y1 = y;
            return y * gain;
        }
    };
    // Something to strike, in a few samples from now.
    struct Strike {
        double at{};          // seconds from the knock
        double strength{};    // 0..1
        double brightness{};  // 0 dull .. 1 bright
        int what{};           // 0 bowl, 1 lid, 2 a coal, 3 the ground
        bool live{};
    };
    struct Exciter {
        double left{};   // samples of noise burst still to give
        double level{};
        double k{0.5};   // its one-pole colour
        double a{};
        double run(double noise) {
            if (left <= 0)
                return 0;
            left -= 1;
            a += (noise * level - a) * k;
            return a;
        }
    };

    double uniform();
    double normal();
    void schedule(double pan);
    void add(std::size_t& n, double at, double strength, double brightness, int what);
    void strike(const Strike& strike);

    std::uint32_t state_;
    std::atomic<int> knocks_{0};
    std::atomic<float> knock_pan_{0};
    std::atomic<float> fire_{0};
    std::atomic<float> fire_pan_{0};
    int heard_{};
    double pan_{};
    double clock_{};  // seconds since the last knock
    std::array<Strike, 96> strikes_{};
    std::array<Mode, 8> bowl_{};
    std::array<Mode, 6> lid_{};
    std::array<Mode, 2> coal_{};
    Exciter bowl_hit_{}, lid_hit_{}, coal_hit_{}, ground_hit_{};
    double ground_a_{}, ground_b_{};
    // the fire
    double fire_level_{}, fire_pan_now_{};
    double roar_a_{}, roar_b_{}, flutter_{}, flutter_target_{}, flutter_clock_{};
    double pop_left_{}, pop_level_{}, pop_prev_{};
    Mode pop_ring_{};
};

} // namespace mm
