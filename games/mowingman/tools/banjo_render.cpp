// The band, as the game plays it, written to a 48 kHz 16-bit WAV with the time it took:
//
//   mm_banjo_render out.wav [seconds [seed [style [part]]]]
//
// style: scruggs (the game's), porch or clawhammer. part: all, banjo, bass, guitar,
// mandolin or fiddle, to hear one alone (BanjoVoice::solo).
#include "banjo_voice.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s out.wav [seconds [seed [scruggs|porch|clawhammer [all|banjo|bass|guitar|mandolin|fiddle]]]]\n", argv[0]);
        return 2;
    }
    const double seconds = argc > 2 ? std::atof(argv[2]) : 60;
    const std::uint32_t seed = argc > 3 ? static_cast<std::uint32_t>(std::atoi(argv[3])) : 21;
    mm::BanjoStyle style = mm::BanjoStyle::scruggs;
    if (argc > 4 && std::strcmp(argv[4], "porch") == 0)
        style = mm::BanjoStyle::porch;
    else if (argc > 4 && std::strcmp(argv[4], "clawhammer") == 0)
        style = mm::BanjoStyle::clawhammer;
    int part = -1;
    if (argc > 5) {
        const char* names[5] = {"banjo", "bass", "guitar", "mandolin", "fiddle"};
        for (int k = 0; k < 5; ++k)
            if (std::strcmp(argv[5], names[k]) == 0)
                part = k;
    }
    mm::BanjoVoice band(seed, style);
    band.solo(part);
    const int rate = mm::BanjoVoice::sample_rate;
    std::vector<float> all(static_cast<std::size_t>(seconds * rate) * 2, 0.0F);
    // blocks of 10 ms, as the device asks for them
    std::vector<float> block(2 * 480);
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    for (std::size_t at = 0; at + block.size() <= all.size(); at += block.size()) {
        std::fill(block.begin(), block.end(), 0.0F);
        band.render_add(block, 1.0);
        std::copy(block.begin(), block.end(), all.begin() + static_cast<long>(at));
    }
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    float peak = 1e-9F;
    double power = 0;
    for (float s : all) {
        peak = std::max(peak, std::abs(s));
        power += static_cast<double>(s) * s;
    }
    std::FILE* file = std::fopen(argv[1], "wb");
    if (file == nullptr)
        return 1;
    const std::uint32_t data = static_cast<std::uint32_t>(all.size() * 2);
    const std::uint32_t header[] = {0x46464952U, 36 + data, 0x45564157U, 0x20746d66U, 16, 0x00020001U, static_cast<std::uint32_t>(rate), static_cast<std::uint32_t>(rate * 4), 0x00100004U, 0x61746164U, data};
    std::fwrite(header, 4, 11, file);
    for (float s : all) {
        const std::int16_t v = static_cast<std::int16_t>(std::lround(std::clamp(s, -1.0F, 1.0F) * 32767));
        std::fwrite(&v, 2, 1, file);
    }
    std::fclose(file);
    std::printf("peak %.3f rms %.4f tunes %d tempo %.1f, %.3f s for %.0f s of sound (%.2f%% of one core)\n", peak,
                std::sqrt(power / static_cast<double>(all.size())), band.tunes_played(), band.tempo(), took, seconds, 100 * took / seconds);
    return 0;
}
