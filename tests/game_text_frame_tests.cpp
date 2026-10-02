#include "game_text.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void require(const bool condition, const char* const message) {
    if (!condition) { throw std::runtime_error(message); }
}
void verify_blend(const games::TextImage& image) {
    constexpr int width = 80;
    constexpr int height = 50;
    constexpr std::size_t stride = 91;
    constexpr std::uint32_t paper = 0xFFFFFFFFU;
    constexpr std::uint32_t sentinel = 0x12345678U;
    std::vector<std::uint32_t> pixels(stride * height, paper);
    for (int row = 0; row < height; ++row) {
        for (std::size_t column = width; column < stride; ++column) {
            pixels[static_cast<std::size_t>(row) * stride + column] = sentinel;
        }
    }
    games::blit_game_text(pixels, width, height, stride, image, -3, -2, 0, 0, 0, 1);
    const gui_forms::TextMaskMetrics metrics = image.mask.metrics();
    const std::span<const std::uint8_t> coverage = image.mask.coverage();
    std::size_t ink = 0;
    for (int row = 0; row < height; ++row) {
        for (std::size_t column = 0; column < stride; ++column) {
            const std::uint32_t pixel = pixels[static_cast<std::size_t>(row) * stride + column];
            if (column >= width) { require(pixel == sentinel, "Stride padding is untouched"); continue; }
            const std::int64_t mx = static_cast<std::int64_t>(column) + 3 - metrics.ink_left_px;
            const std::int64_t my = row + 2 - static_cast<std::int64_t>(metrics.ink_top_px);
            std::uint32_t expected = paper;
            if (mx >= 0 && my >= 0 && mx < metrics.width_px && my < metrics.height_px) {
                const std::uint32_t gray = 255U - coverage[static_cast<std::size_t>(my) * metrics.stride_bytes + static_cast<std::size_t>(mx)];
                expected = 0xFF000000U | gray | (gray << 8) | (gray << 16);
            }
            require(pixel == expected, "Blending follows signed native bearings and coverage exactly");
            if (pixel != paper) { ++ink; }
        }
    }
    require(ink > 0, "Clipped native mask remains visible");
    const std::vector<std::uint32_t> before = pixels;
    bool rejected = false;
    try { games::blit_game_text(pixels, width, height + 1, stride, image, 0, 0, 1, 0, 0, 1); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && pixels == before, "Invalid destination leaves the entire frame unchanged");
}
}
int main(const int argc, char** const argv) {
    try {
        require(argc == 2, "Pass approved provider font directory");
        games::GameText text(argv[1], "Carlito");
        std::array<std::string, 25> source{};
        std::array<games::TextImage, 25> masks{};
        for (std::size_t index = 0; index < source.size(); ++index) {
            source[index] = "Stable frame label " + std::to_string(index);
        }
        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(10);
        bool complete = false;
        while (!complete && std::chrono::steady_clock::now() < deadline) {
            text.begin();
            for (std::size_t index = 0; index < source.size(); ++index) {
                masks[index] = text.get(source[index], false, 14, 180, 1.5);
            }
            complete = text.ready();
            if (!complete) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        }
        require(complete, "A frame larger than the nine-slot queue completes without waiting inside the adapter");
        for (std::size_t index = 0; index < source.size(); ++index) {
            require(masks[index].mask.has_value() && masks[index].mask.source_utf8() == source[index],
                "Every successful frame label owns exactly its requested source");
        }
        verify_blend(masks[0]);
        text.cancel();
        require(!text.ready(), "Cancellation revokes publication readiness");
        require(masks[0].mask.has_value(), "Cancellation preserves retained frame masks");
        std::cout << "Bounded whole-frame text preparation, native bearings and clipped BGRA blending passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
