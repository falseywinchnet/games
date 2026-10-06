#pragma once
// The tank's own sound, synthesized as it plays: the filter's low pump hum, moving
// water (smoothed noise that slowly swells) and an air stone's small bubbles, each a
// short rising resonance at irregular intervals. The same recipe as the 64-second
// loop audio_src/make_audio.py renders for players without a live voice, but never
// repeating, and with nothing to decode or hold in memory.
#include <cstdint>
#include <span>

namespace sw {

class TankVoice final {
  public:
    static constexpr int sample_rate = 48000;
    explicit TankVoice(std::uint32_t seed = 71137);
    // Adds the tank to interleaved stereo frames at `gain`.
    void render_add(std::span<float> stereo, double gain);

  private:
    struct Bubble {
        double age{-1};  // seconds since it began; negative when idle
        double frequency{};
        double gain{};
        double left{};
        double right{};
        double phase{};
    };
    double uniform();
    void start_bubble();

    std::uint32_t state_;
    double time_{};
    double pump_phase_{};
    double water_left_{};
    double water_right_{};
    double next_bubble_{0.13};
    Bubble bubbles_[8]{};
};

} // namespace sw
