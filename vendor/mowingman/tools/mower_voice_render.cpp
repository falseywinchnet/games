// Renders the mower's voice through a short working day to a WAV, driving it at
// 60 Hz the way the game does: start, idle, throttle up, blades on, short grass, a
// lull, heavy grass with surges, clear, blades off, throttle down, key off.
//
//   mower_voice_render out.wav [seed]
#include "mower_voice.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

double grass_at(double seconds) {
    constexpr std::array<double, 17> at{0, 8.6, 9.2, 12.6, 13.2, 14.2, 15, 16, 16.6, 18.4, 18.9, 19.5, 21, 21.6, 23.6, 24.3, 32};
    constexpr std::array<double, 17> load{0, 0, .3, .32, .1, .1, .3, .35, .7, .7, .3, .75, .75, .3, .3, 0, 0};
    for (std::size_t index = 1; index < at.size(); ++index) {
        if (seconds <= at[index])
            return load[index - 1] + (load[index] - load[index - 1]) * (seconds - at[index - 1]) / (at[index] - at[index - 1]);
    }
    return 0;
}

void put(std::FILE* file, std::uint32_t value, int bytes) {
    for (int index = 0; index < bytes; ++index)
        std::fputc(static_cast<int>((value >> (8 * index)) & 0xFF), file);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: mower_voice_render out.wav [seed]\n");
        return 2;
    }
    const std::uint64_t seed = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1;
    mm::MowerVoice voice(seed);
    constexpr int rate = mm::MowerVoice::sample_rate;
    constexpr int frame = rate / 60;
    constexpr double length = 32;
    std::vector<float> sound;
    std::vector<float> block(static_cast<std::size_t>(frame) * 2);
    const std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
    for (int index = 0; index * frame < static_cast<int>(length * rate); ++index) {
        const double seconds = static_cast<double>(index) / 60;
        mm::MowerControls controls{};
        controls.ignition = seconds < 30;
        controls.governed_rpm = seconds >= 2.6 && seconds < 27.3 ? 3600 : 1500;
        controls.blades = seconds > 6.5 && seconds < 25.5;
        controls.load = grass_at(seconds);
        voice.control(controls);
        voice.render(block);
        sound.insert(sound.end(), block.begin(), block.end());
    }
    const double spent = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    std::FILE* file = std::fopen(argv[1], "wb");
    if (file == nullptr) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    const std::uint32_t bytes = static_cast<std::uint32_t>(sound.size() * 2);
    std::fputs("RIFF", file);
    put(file, 36 + bytes, 4);
    std::fputs("WAVEfmt ", file);
    put(file, 16, 4);
    put(file, 1, 2);
    put(file, 2, 2);
    put(file, rate, 4);
    put(file, rate * 4, 4);
    put(file, 4, 2);
    put(file, 16, 2);
    std::fputs("data", file);
    put(file, bytes, 4);
    for (const float sample : sound)
        put(file, static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(sample * 32767.0f))) & 0xFFFF, 2);
    std::fclose(file);
    std::printf("%.1f s of sound in %.2f s (%.0fx real time)\n", length, spent, length / spent);
    return 0;
}
