// Renders the koto garden to a WAV file to listen to:
//   sudoku_music_render out.wav [seconds [day|night [seed]]]
#include "sudoku_music.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <span>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s out.wav [seconds [day|night [seed]]]\n", argv[0]);
        return 2;
    }
    const double seconds = argc > 2 ? std::atof(argv[2]) : 90;
    const bool night = argc > 3 && std::string(argv[3]) == "night";
    const std::uint32_t seed = argc > 4 ? static_cast<std::uint32_t>(std::strtoul(argv[4], nullptr, 10)) : 7;
    ps_sudoku::GardenMusic music(seed, night);
    const int rate = ps_sudoku::GardenMusic::sample_rate;
    const int frames = static_cast<int>(seconds * rate);
    std::vector<float> all(static_cast<std::size_t>(frames) * 2, 0.0F);
    for (int at = 0; at < frames; at += 1024) {
        const int n = std::min(1024, frames - at);
        music.render_add(std::span<float>(all.data() + static_cast<std::size_t>(at) * 2, static_cast<std::size_t>(n) * 2), 1.0);
    }
    FILE* out = std::fopen(argv[1], "wb");
    if (out == nullptr)
        return 1;
    const std::uint32_t data = static_cast<std::uint32_t>(all.size() * 2);
    const auto u32 = [&](std::uint32_t v) { std::fwrite(&v, 4, 1, out); };
    const auto u16 = [&](std::uint16_t v) { std::fwrite(&v, 2, 1, out); };
    std::fwrite("RIFF", 1, 4, out);
    u32(36 + data);
    std::fwrite("WAVEfmt ", 1, 8, out);
    u32(16);
    u16(1);
    u16(2);
    u32(static_cast<std::uint32_t>(rate));
    u32(static_cast<std::uint32_t>(rate * 4));
    u16(4);
    u16(16);
    std::fwrite("data", 1, 4, out);
    u32(data);
    for (float v : all) {
        const float c = v > 1 ? 1 : (v < -1 ? -1 : v);
        const std::int16_t s = static_cast<std::int16_t>(c * 32767);
        std::fwrite(&s, 2, 1, out);
    }
    std::fclose(out);
    std::printf("%s: %.0f s, %d phrases, %ld notes, %ld breaths\n", argv[1], seconds, music.phrases(), music.notes_plucked(),
                music.breaths());
    return 0;
}
