// The ground library: 40 procedurally painted 128x128 terrain textures, each
// with a baked noise-bump relief (lit from the upper left) and a mip chain,
// plus the world-locked noise used to splat neighbouring grounds organically.
#include "ground.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace eggy {

namespace {
struct G {
    std::uint64_t s;
    explicit G(std::uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ULL + 7) {}
    double uni() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return static_cast<double>(s >> 11) * (1.0 / 9007199254740992.0); }
    int below(int n) { return static_cast<int>(uni() * n); }
};
double lat(int x, int y, int p, std::uint64_t seed) {
    x = ((x % p) + p) % p; y = ((y % p) + p) % p;
    std::uint64_t h = seed ^ (static_cast<std::uint64_t>(x) * 0x9E3779B185EBCA87ULL) ^ (static_cast<std::uint64_t>(y) * 0xC2B2AE3D27D4EB4FULL);
    h ^= h >> 33; h *= 0xff51afd7ed558ccdULL; h ^= h >> 33;
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}
double vn(double x, double y, int size, int cell, std::uint64_t seed) {
    const int p = std::max(1, size / cell);
    const double fx = x / cell, fy = y / cell;
    const int ix = static_cast<int>(std::floor(fx)), iy = static_cast<int>(std::floor(fy));
    double tx = fx - ix, ty = fy - iy;
    tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
    const double a = lat(ix, iy, p, seed), b = lat(ix + 1, iy, p, seed), c = lat(ix, iy + 1, p, seed), d = lat(ix + 1, iy + 1, p, seed);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}
double fbm(double x, double y, int size, int cell, std::uint64_t seed) {
    return .5 * vn(x, y, size, cell, seed) + .3 * vn(x, y, size, std::max(2, cell / 2), seed + 1) + .2 * vn(x, y, size, std::max(2, cell / 4), seed + 2);
}

enum Ov : unsigned {
    blades = 1, flowers = 2, clover = 4, pebbles = 8, cracks = 16, litter = 32, needles = 64, ripples = 128,
    streaks = 256, strata = 512, moss = 1024, roots = 2048, sparkle = 4096, wet = 8192, tufts = 16384, scales = 32768
};
struct Recipe {
    unsigned dark, light;
    int cell;
    float contrast;
    unsigned ov;
    float bump;
    unsigned c1, c2, c3;  // overlay palette
    int density;          // overlay amount scale
};

constexpr int N = 128;

// 40 grounds. Index order is part of the world's look ids (see ground.hpp).
const Recipe kRecipes[kGroundCount] = {
    {0x3B7A28, 0x88C64C, 32, 1.4f, blades | flowers, 1.0f, 0x9FDA5E, 0x5A9A34, 0xFFFFFF, 7},          // 0 lush meadow
    {0x46822E, 0x93CC55, 32, 1.4f, blades | flowers, 1.0f, 0xA6DE66, 0xF2E14D, 0xF59AC7, 14},         // 1 flowery meadow
    {0x3E7E2C, 0x7DBE4A, 16, 1.2f, clover | blades, .9f, 0x5FB044, 0x4E9C38, 0xFFFFFF, 6},           // 2 clover lawn
    {0x8C8A3E, 0xCBBE6A, 32, 1.4f, blades, 1.0f, 0xE0D088, 0x9C8E40, 0x7A6E30, 8},                  // 3 dry golden grass
    {0x5E7E3C, 0xA3BC70, 32, 1.4f, blades | pebbles, 1.1f, 0xC4D38A, 0x9C9A93, 0x7E7A70, 5},          // 4 alpine turf
    {0x6A7E4A, 0xA8B37A, 32, 1.6f, pebbles | blades, 1.3f, 0xB8B4A8, 0x8E8A80, 0xC4D38A, 9},          // 5 alpine stony turf
    {0x2F6828, 0x70A442, 16, 1.8f, moss | blades, 1.0f, 0xA9CF5E, 0x3E7A2A, 0x5FA33C, 6},             // 6 mossy grass
    {0x2C5A26, 0x5E8E3A, 32, 1.3f, blades | wet, .9f, 0x6FA848, 0x244A20, 0x86B8C8, 7},               // 7 wet grass
    {0x4A3A22, 0x7A5E34, 16, 1.4f, needles | litter, 1.0f, 0x9A7444, 0x6A4A26, 0x3E5A2A, 9},         // 8 pine needles
    {0x3A3E22, 0x66603A, 16, 1.4f, litter | moss, 1.1f, 0x7A6A38, 0x5E7A34, 0x8A7442, 9},            // 9 leaf litter
    {0x4A2E16, 0x7A4E22, 16, 1.3f, litter, 1.2f, 0xD9822B, 0xE8B23A, 0xB8401F, 14},                  // 10 autumn litter
    {0x3E2014, 0x6A3420, 16, 1.3f, litter, 1.2f, 0xC93A1F, 0xD96A2B, 0x8A2E14, 14},                  // 11 red autumn litter
    {0x2E5E22, 0x6FA040, 8, 2.0f, moss, 1.3f, 0x9CCB58, 0x2A4E1C, 0x7DB845, 10},                     // 12 moss carpet
    {0x24361A, 0x4A5A2C, 16, 1.4f, litter | moss | roots, 1.1f, 0x3E6A28, 0x5A4A2A, 0x2A1E12, 7},    // 13 dark fern floor
    {0x4A3220, 0x7A5634, 32, 1.4f, roots | pebbles, 1.2f, 0x3A2412, 0x8F8577, 0x5A3E22, 6},          // 14 roots and soil
    {0x8A6A40, 0xC2A06A, 32, 1.3f, pebbles | blades, 1.0f, 0x9E8C66, 0xB59F7C, 0x7FA848, 5},         // 15 trodden trail
    {0x4A3A26, 0x6E5638, 32, 1.2f, wet | pebbles, .8f, 0x3A2E1E, 0x8A7A5E, 0x7A8A94, 4},             // 16 mud
    {0x8A4A2A, 0xB8704A, 32, 1.3f, cracks | pebbles, 1.0f, 0x5E2E18, 0xC48A64, 0x9A5A3A, 5},          // 17 red clay
    {0xC4AA74, 0xEAD9A6, 32, 1.1f, ripples | pebbles, .7f, 0xB89C68, 0xF4E8C4, 0xA89070, 3},          // 18 sand
    {0x9A8A6A, 0xC8B894, 16, 1.4f, pebbles, 1.4f, 0x8A8A86, 0xB0A890, 0x6E6A60, 14},                 // 19 riverbed pebbles
    {0x6E6B67, 0xACA8A1, 32, 1.6f, cracks | sparkle, 1.4f, 0x3E3B38, 0xD8D4CC, 0x8A8680, 8},          // 20 granite
    {0x464C56, 0x707884, 32, 1.4f, cracks | strata, 1.3f, 0x2A2E36, 0x8A92A0, 0x5A6270, 6},          // 21 slate
    {0xA8805A, 0xCCAA80, 32, 1.2f, cracks | pebbles, 1.2f, 0x8A5A34, 0xD8B890, 0xB88A5A, 3},          // 22 sandstone
    {0x2E2E30, 0x585454, 16, 1.5f, cracks | pebbles, 1.4f, 0x1A1A1C, 0x6E6A66, 0x46423F, 6},         // 23 basalt
    {0x7A7670, 0xA8A49C, 32, 1.5f, cracks | moss, 1.3f, 0x3E3B38, 0xD99A2B, 0x9FB35A, 9},            // 24 lichen rock
    {0xA8A498, 0xD8D4C8, 32, 1.3f, cracks | strata, 1.1f, 0x7A766C, 0xEEEAE0, 0xB8B4A8, 5},          // 25 limestone
    {0x7A746C, 0x9E9688, 8, 1.6f, pebbles, 1.6f, 0x8E8A82, 0xB4AC9C, 0x625E58, 20},                  // 26 scree
    {0x7F786C, 0xA89E8C, 8, 1.3f, pebbles, 1.4f, 0x9C978E, 0xB8AE98, 0x847D72, 16},                  // 27 gravel
    {0x4E4A46, 0x6E6862, 16, 1.4f, strata | cracks, 1.4f, 0x3A3632, 0x7E7870, 0x5A5650, 7},          // 28 shale
    {0xD8E4F2, 0xFCFDFF, 32, 1.0f, sparkle, .6f, 0xFFFFFF, 0xBFE2FF, 0xE8F0FA, 6},                   // 29 fresh snow
    {0xC2D2E8, 0xFAFCFF, 32, 1.4f, ripples | sparkle, 1.0f, 0xB4C7E2, 0xFFFFFF, 0x9FD0FF, 5},        // 30 wind-carved snow
    {0xBCC8D8, 0xE8EEF4, 16, 1.6f, sparkle | pebbles, .9f, 0xFFFFFF, 0xA8B4C4, 0xC8D2DE, 4},         // 31 crusty old snow
    {0xD0DCEA, 0xF6FAFF, 32, 1.2f, tufts | sparkle, .8f, 0x8EA868, 0xFFFFFF, 0x6E8A4E, 5},           // 32 snow with grass
    {0x9EA6B2, 0xE6ECF4, 32, 1.8f, cracks | sparkle, 1.2f, 0x5E6670, 0xFFFFFF, 0x7A828E, 6},         // 33 snow-dusted rock
    {0xA8C4E4, 0xE4F0FC, 32, 1.3f, streaks | sparkle, .8f, 0xFFFFFF, 0x8EB4DE, 0xC8DCF2, 5},          // 34 blue firn
    {0x7FC4E0, 0xD4F0FA, 32, 1.4f, streaks | cracks, .7f, 0xFFFFFF, 0x5FA9C9, 0xBFEAF8, 6},          // 35 clear ice
    {0x4E96C4, 0xA4D4EC, 32, 1.6f, cracks | streaks, .8f, 0x2E6E9A, 0xDDF4FF, 0x6AB0D8, 9},          // 36 blue cracked ice
    {0xB8DCEA, 0xEEF8FC, 16, 1.3f, sparkle | streaks, .7f, 0xFFFFFF, 0xCCE8F4, 0x9ACCE0, 8},         // 37 frosted ice
    {0x6A6250, 0x988C72, 16, 1.4f, pebbles | wet, 1.2f, 0x8A8476, 0x5E5648, 0x6A7A80, 12},          // 38 pebble stream bed
    {0x3A3A2E, 0x5A5844, 32, 1.2f, wet | pebbles, .8f, 0x2E2E24, 0x6E6A58, 0x4A5A5E, 4},             // 39 dark silt
};

void paint(Tex& t, std::vector<float>& h, const Recipe& r, std::uint64_t seed) {
    G g(seed);
    t.make(N, N);
    h.assign(static_cast<size_t>(N) * N, 0.f);
    const Col d = hex(r.dark), l = hex(r.light), c1 = hex(r.c1), c2 = hex(r.c2), c3 = hex(r.c3);
    std::vector<Col> col(static_cast<size_t>(N) * N);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const double n = fbm(x, y, N, r.cell, seed * 31);
            const double v = std::clamp((n - .5) * r.contrast + .5, 0.0, 1.0);
            col[static_cast<size_t>(y * N + x)] = mix(d, l, static_cast<float>(v));
            h[static_cast<size_t>(y * N + x)] = static_cast<float>(v);
        }
    auto C = [&](int x, int y) -> Col& { return col[static_cast<size_t>(((y % N + N) % N) * N + ((x % N + N) % N))]; };
    auto H = [&](int x, int y) -> float& { return h[static_cast<size_t>(((y % N + N) % N) * N + ((x % N + N) % N))]; };
    auto blend = [&](int x, int y, Col c, float a, float dh) { C(x, y) = mix(C(x, y), c, a); H(x, y) += dh; };
    const int D = r.density;
    if (r.ov & moss)
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                const double m = vn(x, y, N, 16, seed + 77);
                if (m > .55) blend(x, y, m > .7 ? c1 : c3, static_cast<float>((m - .55) * 2.4), .2f);
            }
    if (r.ov & strata)
        for (int y = 0; y < N; ++y) {
            const float band = static_cast<float>(lat(0, y / 3, 1, seed + 5));
            for (int x = 0; x < N; ++x) {
                // gently warped, irregular bedding planes (not regular stripes)
                const int yy = y + static_cast<int>(6 * vn(x, y, N, 32, seed + 3) - 3 + 2 * std::sin(x * .07 + seed));
                const double bed = lat(0, (yy + 512) / 5, 64, seed + 8);
                if (bed > .82) blend(x, y, c2, .18f, .12f); else if (bed < .12) blend(x, y, c1, .2f, -.15f);
                C(x, y) = shade(C(x, y), .95f + .1f * band);
            }
        }
    if (r.ov & ripples)
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                const double rr = std::sin((x * .5 + y * 1.0) * 2 * M_PI / 16 + 3 * vn(x, y, N, 32, seed + 9));
                if (rr > .8) blend(x, y, c1, .3f, -.25f); else if (rr > .5) blend(x, y, c2, .3f, .2f);
            }
    if (r.ov & streaks)
        for (int i = 0; i < 3 * D; ++i) {
            const int x0 = g.below(N), y0 = g.below(N), len = 8 + g.below(24);
            for (int k = 0; k < len; ++k) blend(x0 + k, y0 - k / 2, c1, .45f, .1f);
        }
    if (r.ov & wet)
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                const double m = vn(x, y, N, 32, seed + 55);
                if (m > .62) blend(x, y, c3, .35f, -.3f);
            }
    if (r.ov & cracks)
        for (int i = 0; i < D * 2; ++i) {
            int x = g.below(N), y = g.below(N);
            for (int k = 0; k < 10 + g.below(20); ++k) {
                blend(x, y, c1, .85f, -.6f);
                blend(x, y - 1, c2, .25f, .1f);
                x += g.below(3) - 1;
                y += g.uni() < .6 ? 1 : 0;
            }
        }
    if (r.ov & roots)
        for (int i = 0; i < D; ++i) {
            double x = g.below(N), y = g.below(N), a = g.uni() * 6.283;
            for (int k = 0; k < 30; ++k) {
                blend(static_cast<int>(x), static_cast<int>(y), c1, .8f, .35f);
                a += (g.uni() - .5) * .6; x += std::cos(a); y += std::sin(a);
            }
        }
    if (r.ov & litter)
        for (int i = 0; i < D * 30; ++i) {
            const int x = g.below(N), y = g.below(N);
            const Col lc = std::array<Col, 3>{c1, c2, c3}[static_cast<size_t>(g.below(3))];
            const int s = 1 + g.below(2);
            for (int dy = 0; dy < s; ++dy) for (int dx = 0; dx <= s; ++dx) blend(x + dx, y + dy, shade(lc, .8f + .3f * static_cast<float>(g.uni())), .9f, .35f);
            blend(x + s + 1, y + s, hex(0x000000), .35f, -.2f);
        }
    if (r.ov & needles)
        for (int i = 0; i < D * 30; ++i) {
            const int x = g.below(N), y = g.below(N), dir = g.below(4);
            for (int k = 0; k < 4; ++k) blend(x + (dir == 0 ? k : dir == 1 ? -k : 0), y + (dir >= 2 ? k : k / 2), c1, .75f, .2f);
        }
    if (r.ov & pebbles)
        for (int i = 0; i < D * 8; ++i) {
            const int x = g.below(N), y = g.below(N), rad = 1 + g.below(3);
            const Col pc = std::array<Col, 3>{c1, c2, c3}[static_cast<size_t>(g.below(3))];
            for (int dy = -rad; dy <= rad; ++dy)
                for (int dx = -rad; dx <= rad; ++dx)
                    if (dx * dx + dy * dy <= rad * rad + rad) {
                        const float dome = 1.f - static_cast<float>(dx * dx + dy * dy) / static_cast<float>(rad * rad + rad + 1);
                        blend(x + dx, y + dy, pc, .95f, .9f * dome);
                    }
        }
    if (r.ov & blades)
        for (int i = 0; i < D * 70; ++i) {
            const int x = g.below(N), y = g.below(N), len = 2 + g.below(5), lean = g.below(3) - 1;
            const Col bc = g.uni() < .5 ? c1 : c2;
            for (int k = 0; k < len; ++k) blend(x + (k * lean) / std::max(1, len - 1), y - k, shade(bc, .78f + .45f * k / len), .85f, .25f + .1f * k);
        }
    if (r.ov & tufts)
        for (int i = 0; i < D * 4; ++i) {
            const int x = g.below(N), y = g.below(N);
            for (int b = 0; b < 6; ++b)
                for (int k = 0; k < 4; ++k) blend(x + b - 3 + (k * (b - 3)) / 4, y - k, c1, .9f, .4f);
        }
    if (r.ov & clover)
        for (int i = 0; i < D * 16; ++i) {
            const int x = g.below(N), y = g.below(N);
            for (int lf = 0; lf < 3; ++lf) {
                const int ox = static_cast<int>(std::lround(std::cos(lf * 2.094) * 1.6)), oy = static_cast<int>(std::lround(std::sin(lf * 2.094) * 1.6));
                blend(x + ox, y + oy, lf ? c2 : c1, .9f, .3f);
                blend(x + ox + 1, y + oy, c2, .7f, .3f);
            }
        }
    if (r.ov & flowers)
        for (int i = 0; i < D * 3; ++i) {
            const int x = g.below(N), y = g.below(N);
            const Col fc = g.uni() < .5 ? c2 : c3;
            blend(x, y, fc, 1, .5f); blend(x + 1, y, shade(fc, .85f), 1, .4f); blend(x, y + 1, shade(fc, .8f), 1, .4f); blend(x + 1, y + 1, hex(0xFFE14D), .6f, .4f);
        }
    if (r.ov & sparkle)
        for (int i = 0; i < D * 12; ++i) blend(g.below(N), g.below(N), g.uni() < .7 ? c1 : c2, .9f, .1f);
    // bake the noise bump: light from the upper left, depth from the height map
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const float dh = H(x - 1, y - 1) - H(x + 1, y + 1);
            const float k = std::clamp(1.f + r.bump * .55f * dh, .6f, 1.45f);
            const Col c = shade(C(x, y), k);
            t.at(x, y) = (0xFFu << 24) | (static_cast<std::uint32_t>(std::clamp(c.r, 0.f, 1.f) * 255) << 16) |
                         (static_cast<std::uint32_t>(std::clamp(c.g, 0.f, 1.f) * 255) << 8) | static_cast<std::uint32_t>(std::clamp(c.b, 0.f, 1.f) * 255);
        }
    t.build_mips();
}
}  // namespace

const Ground& ground() {
    static Ground g = [] {
        Ground out;
        std::vector<float> h;
        for (int i = 0; i < kGroundCount; ++i) paint(out.tex[static_cast<size_t>(i)], h, kRecipes[i], 1000 + static_cast<std::uint64_t>(i) * 17);
        // splat noise: smooth, world-locked, organic threshold field
        out.splat.make(64, 64);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const double n = .65 * vn(x, y, 64, 16, 4242) + .35 * vn(x, y, 64, 4, 4243);
                out.splat.at(x, y) = 0xFF000000u | static_cast<std::uint32_t>(std::clamp(n, 0.0, 1.0) * 255);
            }
        return out;
    }();
    return g;
}

}  // namespace eggy
