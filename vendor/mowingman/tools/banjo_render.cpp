// The band, as the game plays it: `mm_banjo_render out.wav [seconds [seed]]`.
#include "banjo_voice.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s out.wav [seconds [seed]]\n", argv[0]);
        return 2;
    }
    const double seconds = argc > 2 ? std::atof(argv[2]) : 60;
    const std::uint32_t seed = argc > 3 ? static_cast<std::uint32_t>(std::atoi(argv[3])) : 21;
    mm::BanjoVoice band(seed);
    const int rate = mm::BanjoVoice::sample_rate;
    std::vector<float> all(static_cast<std::size_t>(seconds * rate) * 2, 0.0F);
    std::vector<float> block(2 * 480);
    for (std::size_t at = 0; at + block.size() <= all.size(); at += block.size()) {
        std::fill(block.begin(), block.end(), 0.0F);
        band.render_add(block, 1.0);
        std::copy(block.begin(), block.end(), all.begin() + static_cast<long>(at));
    }
    float peak = 1e-9F;
    for (float s : all)
        peak = std::max(peak, std::abs(s));
    std::FILE* file = std::fopen(argv[1], "wb");
    const std::uint32_t data = static_cast<std::uint32_t>(all.size() * 2);
    const std::uint32_t header[] = {0x46464952U, 36 + data, 0x45564157U, 0x20746d66U, 16, 0x00020001U, static_cast<std::uint32_t>(rate), static_cast<std::uint32_t>(rate * 4), 0x00100004U, 0x61746164U, data};
    std::fwrite(header, 4, 11, file);
    for (float s : all) {
        const std::int16_t v = static_cast<std::int16_t>(std::lround(std::clamp(s, -1.0F, 1.0F) * 32767));
        std::fwrite(&v, 2, 1, file);
    }
    std::fclose(file);
    std::printf("peak %.3f\n", peak);
    return 0;
}
