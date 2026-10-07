#include "field.hpp"

#include "lawn.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ct {

namespace {

constexpr int kSide = 1024;              // texels each way
constexpr int kFold = 64;                // grown beyond the edge, then folded back over it so it wraps
constexpr double kUnits = 24.0;          // world units the field covers each way
constexpr double kMetresPerUnit = .6;    // the grass drawn large, to read in the garden's big pixels

struct Rng {
    std::uint32_t state;
    double next() {
        state = state * 1664525U + 1013904223U;
        return static_cast<double>(state >> 8U) / 16777216.0;
    }
};

// Wildflowers grow in drifts: a few clumps, each a scatter of one kind.
void sow(std::vector<grass::LawnFlower>& flowers, Rng& rng, int kind, int clumps, int per_clump, double spread, double metres) {
    for (int c = 0; c < clumps; ++c) {
        const double cx = rng.next() * metres, cy = rng.next() * metres;
        const int count = per_clump / 2 + static_cast<int>(rng.next() * per_clump);
        for (int i = 0; i < count; ++i) {
            const double a = rng.next() * 6.2831853, r = spread * std::sqrt(rng.next());
            const double x = std::fmod(cx + std::cos(a) * r + metres, metres), y = std::fmod(cy + std::sin(a) * r + metres, metres);
            flowers.push_back(grass::LawnFlower{kind, x, y});
        }
    }
}

grass::LawnWeather weather_for(Season season) {
    grass::LawnWeather w{};
    switch (season) {
        case Season::spring: w.fresh = .85; break;
        case Season::summer: break;
        case Season::autumn: w.dry = 1; break;
        case Season::winter: w.frost = 1; break;
        case Season::night: w.frost = 1; break;
    }
    return w;
}

std::vector<grass::LawnFlower> flowers_for(Season season, double metres) {
    std::vector<grass::LawnFlower> flowers;
    Rng rng{0x5EED0000U + static_cast<std::uint32_t>(season)};
    switch (season) {
        case Season::spring:
            sow(flowers, rng, 3, 14, 22, 1.6, metres);   // daisies
            sow(flowers, rng, 2, 10, 10, 1.4, metres);   // dandelions
            sow(flowers, rng, 4, 6, 12, 1.0, metres);    // speedwell blue
            break;
        case Season::summer:
            sow(flowers, rng, 1, 14, 20, 1.8, metres);   // buttercups
            sow(flowers, rng, 0, 10, 16, 1.2, metres);   // clover
            sow(flowers, rng, 3, 6, 12, 1.2, metres);    // daisies
            break;
        case Season::autumn:
            sow(flowers, rng, 5, 9, 7, .6, metres);      // mushrooms, in rings and clusters
            sow(flowers, rng, 6, 8, 6, 1.4, metres);     // dandelion clocks
            break;
        case Season::winter: case Season::night: break;
    }
    return flowers;
}

}  // namespace

FieldArt make_field(Season season, const std::atomic<bool>* cancel) {
    FieldArt field;
    if (cancel != nullptr && (*cancel).load()) return field;
    const int grown = kSide + kFold;
    const double ppm = kSide / (kUnits * kMetresPerUnit);
    const double metres = grown / ppm;
    grass::LawnScene scene{};
    scene.width = 2;
    scene.height = 2;
    scene.distance.assign(4, 1000.0F);  // nothing stands in the field: the hedge is drawn over it
    // flowers are sown over the folded square, and their wrap copies over the fold
    const double square = kSide / ppm;
    for (const grass::LawnFlower& f : flowers_for(season, square)) {
        scene.flowers.push_back(f);
        if (f.x < kFold / ppm) scene.flowers.push_back(grass::LawnFlower{f.kind, f.x + square, f.y});
        if (f.y < kFold / ppm) scene.flowers.push_back(grass::LawnFlower{f.kind, f.x, f.y + square});
        if (f.x < kFold / ppm && f.y < kFold / ppm) scene.flowers.push_back(grass::LawnFlower{f.kind, f.x + square, f.y + square});
    }
    const grass::Layer layer = grass::render_lawn(grass::LawnLook::tall, scene, 1009U + static_cast<std::uint64_t>(season), metres, metres, ppm, weather_for(season));
    if (cancel != nullptr && (*cancel).load()) return field;
    if (layer.width < grown || layer.height < grown) return field;
    // Fold the margin back over the start, fading across it, so the right edge runs on into the left and the
    // bottom into the top. The picture's top row (the lawn's far edge, sun upper left) is laid nearest the
    // viewer, so its sun comes from the front left like the garden's.
    std::vector<float> folded(static_cast<std::size_t>(kSide) * kSide * 3);
    const std::size_t lw = static_cast<std::size_t>(layer.width);
    for (int y = 0; y < kSide; ++y)
        for (int x = 0; x < kSide; ++x) {
            float acc[3] = {0, 0, 0};
            const double wx = x < kFold ? (x + .5) / kFold : 1.0, wy = y < kFold ? (y + .5) / kFold : 1.0;
            const double sx = wx * wx * (3 - 2 * wx), sy = wy * wy * (3 - 2 * wy);
            const int xs[2] = {x, x + kSide}, ys[2] = {y, y + kSide};
            const double fx[2] = {sx, 1 - sx}, fy[2] = {sy, 1 - sy};
            for (int j = 0; j < 2; ++j)
                for (int i = 0; i < 2; ++i) {
                    const double k = fx[i] * fy[j];
                    if (k <= 0 || xs[i] >= layer.width || ys[j] >= layer.height) continue;
                    const std::uint32_t p = layer.px[static_cast<std::size_t>(ys[j]) * lw + static_cast<std::size_t>(xs[i])];
                    acc[0] += static_cast<float>(k * ((p >> 16U) & 255U));
                    acc[1] += static_cast<float>(k * ((p >> 8U) & 255U));
                    acc[2] += static_cast<float>(k * (p & 255U));
                }
            float* o = &folded[(static_cast<std::size_t>(y) * kSide + static_cast<std::size_t>(x)) * 3];
            o[0] = acc[0]; o[1] = acc[1]; o[2] = acc[2];
        }
    // the garden is painted brighter than the lawn engine's daylight: lift the grass to sit with it
    // autumn's straw is warmer than the engine's dry grass alone
    const float warmth[3] = {season == Season::autumn ? 1.08F : 1.F, season == Season::autumn ? .98F : 1.F, season == Season::autumn ? .82F : 1.F};
    float grade[256];
    const bool snowy = season == Season::winter || season == Season::night;
    const double lift = snowy ? .62 : season == Season::spring ? .74 : .78, gain = snowy ? 1.30 : season == Season::spring ? 1.24 : 1.18;
    for (int i = 0; i < 256; ++i) grade[i] = static_cast<float>(std::min(255.0, 255.0 * gain * std::pow(i / 255.0, lift)));
    field.tex.make(kSide, kSide);
    for (int y = 0; y < kSide; ++y)
        for (int x = 0; x < kSide; ++x) {
            const float* c = &folded[(static_cast<std::size_t>(y) * kSide + static_cast<std::size_t>(x)) * 3];
            const std::uint32_t r = static_cast<std::uint32_t>(std::min(255.F, grade[static_cast<int>(std::clamp(c[0] + .5F, 0.F, 255.F))] * warmth[0] + .5F));
            const std::uint32_t g = static_cast<std::uint32_t>(std::min(255.F, grade[static_cast<int>(std::clamp(c[1] + .5F, 0.F, 255.F))] * warmth[1] + .5F));
            const std::uint32_t b = static_cast<std::uint32_t>(std::min(255.F, grade[static_cast<int>(std::clamp(c[2] + .5F, 0.F, 255.F))] * warmth[2] + .5F));
            field.tex.px[static_cast<std::size_t>(y) * kSide + static_cast<std::size_t>(x)] = 0xFF000000U | r << 16U | g << 8U | b;
        }
    // halved copies for small windows, where a texel would otherwise sparkle
    field.tex.build_mips();
    field.units = kUnits;
    field.season = season;
    return field;
}

bool Garden::draw_field(const GardenState& s) {
    if (!field_ || !(*field_).ready() || (*field_).season != s.season) return false;
    const FieldArt& f = *field_;
    // the part of the ground the view can see, in two-unit tiles (the texture is mapped affinely, so no huge quads)
    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    const double corners[4][2] = {{0, 0}, {static_cast<double>(r.W), 0}, {0, static_cast<double>(r.H)}, {static_cast<double>(r.W), static_cast<double>(r.H)}};
    for (int k = 0; k < 4; ++k) {
        double wx = 0, wy = 0;
        if (!r.unproject_plane(corners[k][0], corners[k][1], -.01, wx, wy)) continue;
        x0 = std::min(x0, wx); x1 = std::max(x1, wx); y0 = std::min(y0, wy); y1 = std::max(y1, wy);
    }
    if (x0 > x1 || y0 > y1) return false;
    const int gx0 = static_cast<int>(std::floor(std::max(x0, -60.0) / 2)) * 2 - 2, gx1 = static_cast<int>(std::ceil(std::min(x1, 60.0) / 2)) * 2 + 2;
    const int gy0 = static_cast<int>(std::floor(std::max(y0, -60.0) / 2)) * 2 - 2, gy1 = static_cast<int>(std::ceil(std::min(y1, 60.0) / 2)) * 2 + 2;
    // moonlight on the snow at night; daylight is in the picture already
    const Col tint = s.season == Season::night ? Col{.50f, .60f, .86f, 1} : Col{1, 1, 1, 1};
    const V3 n{0, 0, 1};
    const double k = 1 / f.units;
    // one level of detail for the whole field, from the view's scale, so no seam runs between tiles
    int level = 0;
    for (double q = f.tex.w / f.units / std::max(1.0, r.scale); q > 1.5 && level < 4; q *= .5) ++level;
    const Tex& tex = f.tex.level(level);
    for (int gy = gy0; gy < gy1; gy += 2)
        for (int gx = gx0; gx < gx1; gx += 2) {
            const double ax = gx, ay = gy, bx = gx + 2, by = gy + 2;
            Vtx q[6] = {{{ax, ay, -.01}, n, ax * k, ay * k, tint}, {{bx, ay, -.01}, n, bx * k, ay * k, tint}, {{bx, by, -.01}, n, bx * k, by * k, tint},
                        {{ax, ay, -.01}, n, ax * k, ay * k, tint}, {{bx, by, -.01}, n, bx * k, by * k, tint}, {{ax, by, -.01}, n, ax * k, by * k, tint}};
            r.draw(q, 6, &tex, unlit);
        }
    return true;
}

}  // namespace ct
