#include "chipper_voice.hpp"

#include <algorithm>
#include <cmath>

namespace mm {

ChipperVoice::ChipperVoice(std::uint32_t seed) : state_(seed * 2654435761U + 1U) {}

double ChipperVoice::uniform() {
    state_ += 0x6d2b79f5U;
    std::uint32_t t = (state_ ^ (state_ >> 15)) * (1U | state_);
    t ^= t + (t ^ (t >> 7)) * (61U | t);
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
}

void ChipperVoice::render_add(std::span<float> stereo, double intensity) {
    const double target = std::clamp(intensity, 0.0, 1.0);
    if (target <= 0 && level_ < 1e-4)
        return;
    const double dt = 1.0 / sample_rate;
    for (std::size_t frame = 0; frame + 1 < stereo.size(); frame += 2) {
        level_ += (target - level_) * (target > level_ ? 0.0025 : 0.0004);
        const double noise = uniform() * 2 - 1;
        // Chips on the deck: a couple of hundred short bright bursts a second, each its own colour and length.
        if (uniform() < 180.0 / sample_rate) {
            Burst& burst = clatter_[clatter_slot_];
            burst.env = 0.4 + 0.8 * uniform() * uniform();
            burst.k = 0.35 + 0.5 * uniform();
            burst.decay = 0.985 + 0.012 * uniform();
            clatter_slot_ = (clatter_slot_ + 1) % 8;
        }
        double clatter = 0;
        for (Burst& burst : clatter_)
            clatter += burst.run(noise);
        // Clods and stems: fewer, duller, longer.
        if (uniform() < 45.0 / sample_rate) {
            Burst& burst = clods_[clod_slot_];
            burst.env = 0.5 + uniform();
            burst.k = 0.04 + 0.05 * uniform();
            burst.decay = 0.996;
            clod_slot_ = (clod_slot_ + 1) % 3;
        }
        double clod = 0;
        for (Burst& burst : clods_)
            clod += burst.run(noise);
        // The shredding rasp, swelling a little with every blade pass.
        pass_phase_ += 58.0 * dt;
        if (pass_phase_ >= 1)
            pass_phase_ -= 1;
        rasp_a_ += (noise - rasp_a_) * 0.5;
        rasp_b_ += (rasp_a_ - rasp_b_) * 0.08;
        const double rasp = (rasp_a_ - rasp_b_) * (0.5 + 0.5 * (1 - pass_phase_) * (1 - pass_phase_));
        const double mix = (clatter * 0.6 + clod * 1.0 + rasp * 0.5) * 1.3;
        const float out = static_cast<float>(std::tanh(mix) * 0.21 * level_);
        stereo[frame] += out * 0.85F;
        stereo[frame + 1] += out;
    }
}

} // namespace mm
