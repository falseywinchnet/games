#pragma once
// The deck chewing through a flower bed: chips and stems through the blades. Nothing
// rings. A dense clatter of chips against the sheet metal of the deck, duller clods and
// stems going through under it, and a shredding rasp that swells with every blade pass.
// Added on top of the mower's own voice, scaled by how hard it is chewing.
#include <cstdint>
#include <span>

namespace mm {

class ChipperVoice final {
  public:
    static constexpr int sample_rate = 48000;
    explicit ChipperVoice(std::uint32_t seed = 7);
    // Adds `intensity` (0..1, glided) worth of chewing to interleaved stereo frames.
    void render_add(std::span<float> stereo, double intensity);

  private:
    // A short burst of noise coloured by a one-pole band: its own brightness and length.
    struct Burst {
        double env{}, k{0.3}, decay{0.99}, a{}, b{};
        double run(double noise) {
            env *= decay;
            a += (noise - a) * k;
            b += (a - b) * k * 0.35;
            return (a - b) * env;
        }
    };
    double uniform();
    std::uint32_t state_;
    double level_{};
    Burst clatter_[8]{};
    Burst clods_[3]{};
    int clatter_slot_{};
    int clod_slot_{};
    double rasp_a_{};
    double rasp_b_{};
    double pass_phase_{};
};

} // namespace mm
