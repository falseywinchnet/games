// Stillwater's live voices rendered to 48 kHz 16-bit WAV, with the time they took.
//
//   sw_music_render shanty out.wav [seconds [seed]]   the band, as the game plays it
//   sw_music_render loop out.wav [seconds [seed]]     a seamless shanty loop (the fallback
//                                                     where the toolkit has no live voices)
//   sw_music_render tank out.wav [seconds [seed]]     the tank's water
#include "shanty_voice.hpp"
#include "tank_voice.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

namespace {

constexpr int rate = 48000;
// The level the game plays the band at (stillwater_view.cpp), so the loop matches it.
constexpr double shanty_gain = 0.25;

bool write_wav(const char* path, const std::vector<float>& all) {
    std::FILE* file = std::fopen(path, "wb");
    if (file == nullptr)
        return false;
    const std::uint32_t data = static_cast<std::uint32_t>(all.size() * 2);
    const std::uint32_t header[] = {0x46464952U, 36 + data, 0x45564157U, 0x20746d66U, 16, 0x00020001U,
                                    static_cast<std::uint32_t>(rate), static_cast<std::uint32_t>(rate * 4),
                                    0x00100004U, 0x61746164U, data};
    std::fwrite(header, 4, 11, file);
    for (const float s : all) {
        const std::int16_t v = static_cast<std::int16_t>(std::lround(std::clamp(s, -1.0F, 1.0F) * 32767));
        std::fwrite(&v, 2, 1, file);
    }
    std::fclose(file);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s shanty|loop|tank out.wav [seconds [seed]]\n", argv[0]);
        return 2;
    }
    const bool tank = std::strcmp(argv[1], "tank") == 0;
    const bool loop = std::strcmp(argv[1], "loop") == 0;
    const double seconds = argc > 3 ? std::atof(argv[3]) : 60;
    const std::uint32_t seed = argc > 4 ? static_cast<std::uint32_t>(std::atoi(argv[4])) : 1720;
    // A loop is rendered with a tail that is crossfaded over its own beginning.
    const double fade = loop ? 6.0 : 0.0;
    const std::size_t frames = static_cast<std::size_t>((seconds + fade) * rate);
    std::vector<float> all(frames * 2, 0.0F);
    sw::ShantyVoice band(seed);
    sw::TankVoice water(seed);
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    // The device asks for 256 frames at a time; render in the same blocks.
    const std::size_t block = 256 * 2;
    for (std::size_t at = 0; at < all.size(); at += block) {
        const std::size_t count = std::min(block, all.size() - at);
        std::span<float> part(all.data() + at, count);
        if (tank)
            water.render_add(part, 1.0);
        else
            band.render_add(part, shanty_gain);
    }
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (loop) {
        const std::size_t body = static_cast<std::size_t>(seconds * rate);
        const std::size_t tail = frames - body;
        for (std::size_t i = 0; i < tail; ++i) {
            const double t = (static_cast<double>(i) + 0.5) / static_cast<double>(tail);
            const float in = static_cast<float>(std::sin(t * 1.5707963267948966));
            const float out = static_cast<float>(std::cos(t * 1.5707963267948966));
            for (std::size_t c = 0; c < 2; ++c)
                all[i * 2 + c] = all[i * 2 + c] * in + all[(body + i) * 2 + c] * out;
        }
        all.resize(body * 2);
    }
    float peak = 1e-9F;
    double square = 0;
    for (const float s : all) {
        peak = std::max(peak, std::abs(s));
        square += static_cast<double>(s) * s;
    }
    if (!write_wav(argv[2], all)) {
        std::fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    std::printf("%s: %.1f s, peak %.3f, rms %.4f, rendered in %.3f s (%.0fx real time)", argv[1],
                static_cast<double>(all.size() / 2) / rate, static_cast<double>(peak),
                std::sqrt(square / static_cast<double>(std::max<std::size_t>(1, all.size()))), took,
                (seconds + fade) / std::max(took, 1e-9));
    if (!tank)
        std::printf(", %d verses", band.verses_played());
    std::printf("\n");
    return 0;
}
