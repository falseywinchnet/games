#include "turf.hpp"

#include "lawn.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sh {

namespace {

constexpr double kMetresPerUnit = 1.0;  // a sheep is about a unit long

// Grows `side` + `fold` texels each way, then lays the extra margin back over the start,
// fading across it, so the right edge runs on into the left and the bottom into the top.
Tex folded(const grass::Layer& layer, int side, int fold, double lift, double gain) {
    Tex t;
    if (layer.width < side + fold || layer.height < side + fold)
        return t;
    float grade[256];
    for (int i = 0; i < 256; ++i)
        grade[i] = static_cast<float>(std::min(255.0, 255.0 * gain * std::pow(i / 255.0, lift)));
    t.make(side, side);
    const std::size_t lw = static_cast<std::size_t>(layer.width);
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            const double wx = x < fold ? (x + .5) / fold : 1.0, wy = y < fold ? (y + .5) / fold : 1.0;
            const double sx = wx * wx * (3 - 2 * wx), sy = wy * wy * (3 - 2 * wy);
            const int xs[2] = {x, x + side}, ys[2] = {y, y + side};
            const double fx[2] = {sx, 1 - sx}, fy[2] = {sy, 1 - sy};
            double acc[3] = {0, 0, 0};
            for (int j = 0; j < 2; ++j)
                for (int i = 0; i < 2; ++i) {
                    const double k = fx[i] * fy[j];
                    if (k <= 0)
                        continue;
                    const std::uint32_t p = layer.px[static_cast<std::size_t>(ys[j]) * lw + static_cast<std::size_t>(xs[i])];
                    acc[0] += k * ((p >> 16U) & 255U);
                    acc[1] += k * ((p >> 8U) & 255U);
                    acc[2] += k * (p & 255U);
                }
            std::uint32_t out = 0xFF000000U;
            for (int c = 0; c < 3; ++c) {
                const int level = static_cast<int>(std::clamp(acc[c] + .5, 0.0, 255.0));
                out |= static_cast<std::uint32_t>(grade[level] + .5F) << (16 - 8 * c);
            }
            t.px[static_cast<std::size_t>(y) * static_cast<std::size_t>(side) + static_cast<std::size_t>(x)] = out;
        }
    t.build_mips();
    return t;
}

grass::Layer grow(grass::LawnLook look, int side, int fold, double units, std::uint64_t seed, bool flowers) {
    const double ppm = side / (units * kMetresPerUnit);
    const double metres = (side + fold) / ppm, square = side / ppm;
    grass::LawnScene scene{};
    scene.width = 2;
    scene.height = 2;
    scene.distance.assign(4, 1000.0F);  // nothing stops the grass: the patches' edges are drawn over it
    if (flowers) {
        // A few drifts of daisies and buttercups, copied over the fold so they wrap too.
        std::uint32_t state = 0x5EEDu;
        for (int clump = 0; clump < 10; ++clump) {
            state = state * 1664525U + 1013904223U;
            const double cx = (state >> 8U) / 16777216.0 * square;
            state = state * 1664525U + 1013904223U;
            const double cy = (state >> 8U) / 16777216.0 * square;
            for (int k = 0; k < 14; ++k) {
                state = state * 1664525U + 1013904223U;
                const double a = (state >> 8U) / 16777216.0 * 6.2831853;
                state = state * 1664525U + 1013904223U;
                const double r = .9 * std::sqrt((state >> 8U) / 16777216.0);
                const double x = std::fmod(cx + std::cos(a) * r + square, square), y = std::fmod(cy + std::sin(a) * r + square, square);
                const int kind = clump % 3 == 0 ? 1 : 3;
                scene.flowers.push_back({kind, x, y});
                if (x < fold / ppm)
                    scene.flowers.push_back({kind, x + square, y});
                if (y < fold / ppm)
                    scene.flowers.push_back({kind, x, y + square});
            }
        }
    }
    return grass::render_lawn(look, scene, seed, metres, metres, ppm);
}

}  // namespace

GroundArt grow_ground() {
    GroundArt art;
    // The game is painted brighter and softer than the lawn engine's daylight: lift it to sit with the sheep.
    art.turf = folded(grow(grass::LawnLook::mown_light, 512, 48, kTurfUnits, 17U, false), 512, 48, .74, 1.16);
    art.field = folded(grow(grass::LawnLook::tall, 1024, 64, kFieldUnits, 23U, true), 1024, 64, .78, 1.12);
    return art;
}

}  // namespace sh
