// A bee flying past, landing on a flower, working it, and leaving, as the garden hears it.
//
//   mm_bee_render out.wav
#include "ambience_voice.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s out.wav\n", argv[0]);
        return 2;
    }
    mm::AmbienceVoice voice(5);
    const int rate = mm::AmbienceVoice::sample_rate;
    const double seconds = 12;
    std::vector<float> all;
    std::vector<float> block(2 * 480);
    for (int b = 0; b < static_cast<int>(seconds * 100); ++b) {
        const double t = b / 100.0;
        mm::AmbienceControls controls{};
        controls.engine_on = true;  // the engine itself is not here: only the bees are heard
        mm::BeeSound& bee = controls.bees[0];
        bee.id = 7;
        // flies in from the left past the listener (t 0..4), lands (4..8), leaves to the right (8..12)
        double x = -8 + 2.0 * std::min(t, 4.0);
        if (t > 8)
            x = 0 + 2.4 * (t - 8);
        const double vx = t < 4 ? 2.0 : t > 8 ? 2.4 : 0.0;
        const double d = std::sqrt(x * x + 7.0 * 7.0);
        bee.flying = t < 4 || t > 8;
        bee.gain = std::min(1.0, std::pow(7.0 / d, 2.0));
        bee.pan = std::clamp(x / 8, -1.0, 1.0) * 0.8;
        bee.pitch = (1 + 0.03 * std::min(1.0, vx / 2.4)) / (1 + x * vx / d / 343.0);
        // a second bee, a bumblebee, working a flower nearby the whole time
        controls.bees[1].id = 9;
        controls.bees[1].flying = t > 2 && t < 3;
        controls.bees[1].gain = 0.6;
        controls.bees[1].pan = 0.3;
        voice.set(controls);
        std::fill(block.begin(), block.end(), 0.0F);
        voice.render_add(block);
        all.insert(all.end(), block.begin(), block.end());
    }
    float peak = 1e-9F;
    for (float s : all)
        peak = std::max(peak, std::abs(s));
    std::FILE* file = std::fopen(argv[1], "wb");
    const std::uint32_t data = static_cast<std::uint32_t>(all.size() * 2);
    const std::uint32_t header[] = {0x46464952U, 36 + data, 0x45564157U, 0x20746d66U, 16, 0x00020001U, static_cast<std::uint32_t>(rate), static_cast<std::uint32_t>(rate * 4), 0x00100004U, 0x61746164U, data};
    std::fwrite(header, 4, 11, file);
    for (float s : all) {
        const std::int16_t v = static_cast<std::int16_t>(std::lround(std::clamp(s / peak * 0.5F, -1.0F, 1.0F) * 32767));
        std::fwrite(&v, 2, 1, file);
    }
    std::fclose(file);
    std::printf("peak before normalising %.4f\n", peak);
    return 0;
}
