#include "puzzle_render.hpp"
#include <array>
#include <fstream>

namespace games {
void PuzzleRaster::load_environment(const std::string& file) {
    // Build-time PNG conversion produces a fixed top-left RGBA8 map. Runtime
    // accepts this one bounded asset layout and needs no platform image codec.
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    constexpr std::size_t bytes = 1024 * 512 * 4;
    if (!input || input.tellg() != static_cast<std::streamoff>(bytes + 16)) {
        return;
    }
    input.seekg(0);
    std::array<unsigned char, 16> header{};
    input.read(reinterpret_cast<char*>(header.data()), header.size());
    const std::array<unsigned char, 16> expected{'G', 'P', 'I', 'X', 1, 0, 0, 0,
                                               0, 4, 0, 0, 0, 2, 0, 0};
    if (!input || header != expected) { return; }
    std::vector<unsigned char> pixels(bytes);
    input.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    if (!input) { return; }
    environment_ = std::move(pixels);
    env_width_ = 1024;
    env_height_ = 512;
}
}
