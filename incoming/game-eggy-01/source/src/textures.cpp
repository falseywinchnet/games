#include "textures.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace eggy {

namespace {
std::uint32_t pack(Col c) {
    auto b = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.f, 1.f) * 255 + .5f); };
    return (b(c.a) << 24) | (b(c.r) << 16) | (b(c.g) << 8) | b(c.b);
}
Col unpack(std::uint32_t v) {
    return {((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f, ((v >> 24) & 255) / 255.f};
}

struct Gen {
    std::uint64_t s;
    explicit Gen(std::uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ULL + 1) {}
    double uni() {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return static_cast<double>(s >> 11) * (1.0 / 9007199254740992.0);
    }
    int below(int n) { return static_cast<int>(uni() * n); }
};

double lattice(int x, int y, int px, int py, std::uint64_t seed) {
    x = ((x % px) + px) % px;
    y = ((y % py) + py) % py;
    std::uint64_t h = seed ^ (static_cast<std::uint64_t>(x) * 0x9E3779B185EBCA87ULL) ^ (static_cast<std::uint64_t>(y) * 0xC2B2AE3D27D4EB4FULL);
    h ^= h >> 33; h *= 0xff51afd7ed558ccdULL; h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ULL; h ^= h >> 33;
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}
// tileable value noise: (x,y) in texture pixels, `cell` pixels per lattice step
double vnoise(double x, double y, int size, int cell, std::uint64_t seed) {
    const int p = std::max(1, size / cell);
    const double fx = x / cell, fy = y / cell;
    const int ix = static_cast<int>(std::floor(fx)), iy = static_cast<int>(std::floor(fy));
    double tx = fx - ix, ty = fy - iy;
    tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
    const double a = lattice(ix, iy, p, p, seed), b = lattice(ix + 1, iy, p, p, seed);
    const double c = lattice(ix, iy + 1, p, p, seed), d = lattice(ix + 1, iy + 1, p, p, seed);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}
double fbm(double x, double y, int size, std::uint64_t seed) {
    return .5 * vnoise(x, y, size, size / 4, seed) + .3 * vnoise(x, y, size, size / 8, seed + 1) +
           .2 * vnoise(x, y, size, std::max(2, size / 16), seed + 2);
}
Col ramp(Col a, Col b, double t) { return mix(a, b, static_cast<float>(std::clamp(t, 0.0, 1.0))); }

void base_fill(Tex& t, int size, Col dark, Col light, std::uint64_t seed, double contrast = 1.4) {
    t.make(size, size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            t.at(x, y) = pack(ramp(dark, light, (fbm(x, y, size, seed) - .5) * contrast + .5));
}
void put(Tex& t, int x, int y, Col c) { t.at(x, y) = pack(c); }
void blend(Tex& t, int x, int y, Col c, float a) { t.at(x, y) = pack(mix(unpack(t.at(x, y)), c, a)); }
void speckle(Tex& t, int n, Col c, Gen& g, float a = 1) {
    for (int i = 0; i < n; ++i) blend(t, g.below(t.w), g.below(t.h), c, a);
}
void pebble(Tex& t, int x, int y, int r, Col c) {
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            if (dx * dx + dy * dy <= r * r + r) {
                const float k = 1.f + .12f * static_cast<float>(-dx - dy) / std::max(1, r);
                put(t, x + dx, y + dy, shade(c, k));
            }
    put(t, x - r / 2, y - r / 2, shade(c, 1.35f));
    blend(t, x + r, y + r, hex(0x000000), .35f);
}
void crack(Tex& t, int x, int y, int steps, Col c, Gen& g) {
    for (int i = 0; i < steps; ++i) {
        blend(t, x, y, c, .85f);
        blend(t, x, y - 1, hex(0xFFFFFF), .18f);
        x += g.below(3) - 1;
        y += g.uni() < .6 ? 1 : 0;
        if (g.uni() < .4) x += g.uni() < .5 ? 1 : -1;
    }
}
void blade(Tex& t, int x, int y, int h, Col c, Gen& g, bool alpha_sprite = false) {
    const int lean = g.below(3) - 1;
    for (int i = 0; i < h; ++i) {
        const int xx = x + (i * lean) / std::max(1, h - 1);
        const Col cc = shade(c, .78f + .5f * i / std::max(1, h));
        if (alpha_sprite) put(t, xx, y - i, cc); else blend(t, xx, y - i, cc, .85f);
    }
}
void clear(Tex& t, int w, int h) { t.make(w, h); std::fill(t.px.begin(), t.px.end(), 0u); }
void disc(Tex& t, double cx, double cy, double r, Col c, bool soft = false) {
    for (int y = 0; y < t.h; ++y)
        for (int x = 0; x < t.w; ++x) {
            const double d = std::hypot(x + .5 - cx, y + .5 - cy);
            if (soft) {
                const double a = std::clamp(1 - d / r, 0.0, 1.0);
                if (a > 0) put(t, x, y, alpha(c, static_cast<float>(a * a)));
            } else if (d <= r) put(t, x, y, c);
        }
}
}  // namespace

void Textures::build() {
    Gen g(4242);
    // ---- ground
    base_fill(grass, 64, hex(0x3F7A2A), hex(0x86C24A), 11);
    for (int i = 0; i < 420; ++i) blade(grass, g.below(64), g.below(64), 2 + g.below(3), g.uni() < .5 ? hex(0x9BD65A) : hex(0x5E9E36), g);
    speckle(grass, 90, hex(0x2E5A1F), g, .6f);
    for (int i = 0; i < 7; ++i) {
        const int x = g.below(64), y = g.below(64);
        const Col pc = (i % 3 == 0) ? hex(0xFFFFFF) : (i % 3 == 1) ? hex(0xFFE14D) : hex(0xF59AC7);
        put(grass, x, y, pc); put(grass, x + 1, y, shade(pc, .85f)); put(grass, x, y + 1, shade(pc, .8f));
    }
    // a faint, trodden trail: mostly grass, only a little worn
    trail = grass;
    for (auto& p : trail.px) p = pack(mix(unpack(p), hex(0x9C8A55), .22f));
    for (int i = 0; i < 40; ++i) pebble(trail, g.below(64), g.below(64), 1, hex(0x9E8C66));
    base_fill(grass_alpine, 64, hex(0x6F8B44), hex(0xAFC27A), 12);
    for (int i = 0; i < 260; ++i) blade(grass_alpine, g.below(64), g.below(64), 2 + g.below(2), hex(0xC4D38A), g);
    for (int i = 0; i < 20; ++i) pebble(grass_alpine, g.below(64), g.below(64), 1, hex(0x9C9A93));

    base_fill(forest, 64, hex(0x2F3F21), hex(0x5B5A31), 13);
    for (int i = 0; i < 160; ++i) {
        const int x = g.below(64), y = g.below(64);
        const Col lc = g.uni() < .5 ? hex(0x6E5A2E) : hex(0x4F6B2C);
        put(forest, x, y, lc); put(forest, x + 1, y, shade(lc, .85f));
    }
    for (int i = 0; i < 120; ++i) { const int x = g.below(64), y = g.below(64); blend(forest, x, y, hex(0x8A7442), .7f); blend(forest, x + 1, y + 1, hex(0x8A7442), .7f); }
    speckle(forest, 80, hex(0x1C2614), g, .7f);

    base_fill(autumn, 64, hex(0x4A3018), hex(0x7A5126), 14);
    for (int i = 0; i < 260; ++i) {
        const int x = g.below(64), y = g.below(64);
        const Col lc = std::array<Col, 4>{hex(0xD9822B), hex(0xB8401F), hex(0xE8B23A), hex(0x9C5A1E)}[static_cast<size_t>(g.below(4))];
        put(autumn, x, y, lc); put(autumn, x + 1, y, shade(lc, .9f)); put(autumn, x, y + 1, shade(lc, .75f));
    }
    base_fill(moss, 64, hex(0x3D6B2B), hex(0x7DAA45), 15, 1.8);
    speckle(moss, 160, hex(0xA9CF5E), g, .5f);

    base_fill(path, 64, hex(0x8C6A3F), hex(0xC9A46B), 16);
    for (int i = 0; i < 26; ++i) pebble(path, g.below(64), g.below(64), 1 + g.below(2), g.uni() < .5 ? hex(0x9A948A) : hex(0xB59F7C));
    speckle(path, 120, hex(0x6E5230), g, .5f);

    base_fill(rock, 64, hex(0x6E6B67), hex(0xACA8A1), 17, 1.6);
    for (int i = 0; i < 9; ++i) crack(rock, g.below(64), g.below(64), 8 + g.below(14), hex(0x3E3B38), g);
    for (int i = 0; i < 14; ++i) { const int x = g.below(64), y = g.below(64); const Col lc = g.uni() < .5 ? hex(0xD8A23A) : hex(0x9FB35A); blend(rock, x, y, lc, .8f); blend(rock, x + 1, y, lc, .6f); }
    speckle(rock, 140, hex(0xC9C5BE), g, .35f);

    base_fill(gravel, 64, hex(0x7F786C), hex(0xA89E8C), 18);
    for (int i = 0; i < 110; ++i) pebble(gravel, g.below(64), g.below(64), 1, std::array<Col, 3>{hex(0x9C978E), hex(0xB8AE98), hex(0x847D72)}[static_cast<size_t>(g.below(3))]);

    base_fill(snow, 64, hex(0xB9CCE4), hex(0xFAFCFF), 19, 1.9);
    for (int y = 0; y < 64; ++y)  // wind-carved ripples (sastrugi)
        for (int x = 0; x < 64; ++x) {
            const double rr = std::sin((x * .5 + y * 1.0) * 2 * M_PI / 16 + 2.5 * vnoise(x, y, 64, 16, 191));
            if (rr > .85) blend(snow, x, y, hex(0xB4C7E2), .3f);
            else if (rr > .6) blend(snow, x, y, hex(0xFFFFFF), .35f);
        }
    for (int i = 0; i < 90; ++i) put(snow, g.below(64), g.below(64), hex(0xFFFFFF));
    for (int i = 0; i < 40; ++i) put(snow, g.below(64), g.below(64), hex(0x9FD0FF));

    base_fill(ice, 64, hex(0x6FB9DA), hex(0xD9F3FB), 20, 1.7);
    for (int i = 0; i < 14; ++i) {
        int x = g.below(64), y = g.below(64);
        for (int k = 0; k < 10 + g.below(12); ++k) { blend(ice, x + k, y - k / 2, hex(0xFFFFFF), .55f); }
    }
    for (int i = 0; i < 5; ++i) crack(ice, g.below(64), g.below(64), 10, hex(0x5FA9C9), g);

    base_fill(sand, 64, hex(0xC9B07A), hex(0xEAD9A6), 21);
    base_fill(cliff, 32, hex(0x5E5A55), hex(0x948E86), 22, 1.3);
    for (int y = 0; y < 32; ++y) {
        const float band = .85f + .3f * static_cast<float>(lattice(0, y / 3, 1, 32, 23));
        for (int x = 0; x < 32; ++x) put(cliff, x, y, shade(unpack(cliff.at(x, y)), band));
    }
    for (int i = 0; i < 6; ++i) crack(cliff, g.below(32), g.below(32), 7, hex(0x34312E), g);
    base_fill(dirt, 32, hex(0x5E3F22), hex(0x8D6438), 24);
    for (int x = 0; x < 32; ++x) {  // grass fringe on top rows
        const int h = 2 + g.below(3);
        for (int y = 0; y < h; ++y) put(dirt, x, y, g.uni() < .5 ? hex(0x5E9E36) : hex(0x7DB845));
    }
    for (int i = 0; i < 6; ++i) crack(dirt, g.below(32), 4 + g.below(20), 6, hex(0x3A2412), g);
    for (int i = 0; i < 8; ++i) pebble(dirt, g.below(32), 6 + g.below(24), 1, hex(0x8F8577));

    for (int f = 0; f < 4; ++f) {
        base_fill(water[static_cast<size_t>(f)], 64, hex(0x23679E), hex(0x4AA3D8), 25, 1.1);
        Tex& w = water[static_cast<size_t>(f)];
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const double ph = (x + f * 4) * 2 * M_PI / 32 + 3 * std::sin((y + f * 2) * 2 * M_PI / 64);
                const double wv = std::sin(ph) * std::sin((y * 2 * M_PI / 16) + f * M_PI / 2);
                Col c = unpack(w.at(x, y));
                if (wv > .82) c = mix(c, hex(0xBDE9FF), .8f);
                else if (wv > .6) c = mix(c, hex(0x8FD0F2), .45f);
                c.a = .82f;
                w.at(x, y) = pack(c);
            }
    }

    // ---- materials
    base_fill(bark, 32, hex(0x3E2A1A), hex(0x7A5636), 30, 1.3);
    for (int x = 0; x < 32; x += 3 + g.below(3))
        for (int y = 0; y < 32; ++y) if (g.uni() < .85) blend(bark, x + (y / 9) % 2, y, hex(0x24170D), .8f);
    clear(wood_end, 32, 32);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            const double d = std::hypot(x - 15.5, y - 15.5);
            if (d > 16) { put(wood_end, x, y, hex(0x5A3A20)); continue; }
            const double ring = std::sin(d * 1.5 + lattice(x / 4, y / 4, 8, 8, 31) * 1.2);
            put(wood_end, x, y, ring > .3 ? hex(0xC99A60) : hex(0xA87A45));
        }
    base_fill(leaves, 64, hex(0x1F4A1E), hex(0x3E7A2E), 32, 1.2);
    for (int i = 0; i < 200; ++i) {
        const int x = g.below(64), y = g.below(64), r = 1 + g.below(2);
        const Col lc = g.uni() < .6 ? hex(0x5E9E3A) : hex(0x8CC653);
        for (int dy = -r; dy <= r; ++dy) for (int dx = -r; dx <= r; ++dx) if (dx * dx + dy * dy <= r * r) blend(leaves, x + dx, y + dy, lc, .8f);
        put(leaves, x - 1, y - 1, hex(0xB5E27A));
    }
    speckle(leaves, 120, hex(0x0F2A10), g, .8f);
    base_fill(leaves_autumn, 64, hex(0x8A3A14), hex(0xC4651F), 33, 1.2);
    for (int i = 0; i < 220; ++i) {
        const int x = g.below(64), y = g.below(64);
        const Col lc = std::array<Col, 3>{hex(0xF0A030), hex(0xE8C64A), hex(0xC93A1F)}[static_cast<size_t>(g.below(3))];
        put(leaves_autumn, x, y, lc); put(leaves_autumn, x + 1, y, shade(lc, .85f)); put(leaves_autumn, x, y + 1, shade(lc, .75f));
    }
    speckle(leaves_autumn, 90, hex(0x4A1A08), g, .8f);
    base_fill(pine, 32, hex(0x173A28), hex(0x2F6447), 34, 1.2);
    for (int i = 0; i < 140; ++i) {
        const int x = g.below(32), y = g.below(32);
        for (int k = 0; k < 3; ++k) blend(pine, x + k, y + k / 2, hex(0x4E8C5E), .7f);
    }
    base_fill(fluff, 32, hex(0xF7C531), hex(0xFFE36B), 35, 1.0);
    for (int i = 0; i < 60; ++i) { const int x = g.below(32), y = g.below(32); blend(fluff, x, y, hex(0xFFF2A8), .7f); blend(fluff, x + 1, y + 1, hex(0xE9A923), .35f); }
    base_fill(belly, 32, hex(0xFFE58A), hex(0xFFF6CF), 36, .8);
    clear(acorn, 32, 32);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            const int row = y / 5, sx = (x + (row % 2) * 3) % 6, sy = y % 5;
            const double edge = std::hypot(sx - 3, (sy - 4) * 1.3);
            Col c = hex(0x8A5426);
            if (edge < 2.4) c = hex(0xA9703A);
            if (edge > 3.0) c = hex(0x5A3214);
            if (sy == 0) c = hex(0x4A2810);
            put(acorn, x, y, c);
        }
    base_fill(beak, 16, hex(0xF0811F), hex(0xFFAA3D), 37, .9);
    eye.make(8, 8);
    std::fill(eye.px.begin(), eye.px.end(), pack(hex(0x1E120A)));

    // ---- sprites (cut-out)
    clear(tuft, 32, 32);
    for (int i = 0; i < 26; ++i) {
        const int x = 8 + g.below(16);
        blade(tuft, x, 31, 6 + g.below(14), g.uni() < .5 ? hex(0x6FB43E) : hex(0x9AD45A), g, true);
    }
    clear(fern, 32, 32);
    for (int f = 0; f < 5; ++f) {
        const double ang = -M_PI / 2 + (f - 2) * .42;
        for (int k = 0; k < 15; ++k) {
            const double x = 16 + std::cos(ang) * k * 1.0 + (f - 2) * k * .06 * k / 5, y = 31 + std::sin(ang) * k * 1.0 + k * k * .02;
            put(fern, static_cast<int>(x), static_cast<int>(y), hex(0x3E7A2A));
            for (int s = -1; s <= 1; s += 2) {
                const int lx = static_cast<int>(x + std::cos(ang + s * 1.2) * (3 - k / 6)), ly = static_cast<int>(y + std::sin(ang + s * 1.2) * (3 - k / 6));
                put(fern, lx, ly, hex(0x5FA33C));
                put(fern, (lx + static_cast<int>(x)) / 2, (ly + static_cast<int>(y)) / 2, hex(0x4E9035));
            }
        }
    }
    clear(clover, 16, 16);
    for (int i = 0; i < 3; ++i) {
        const double a = -M_PI / 2 + i * 2 * M_PI / 3;
        disc(clover, 8 + std::cos(a) * 3.2, 9 + std::sin(a) * 3.2, 2.8, i == 0 ? hex(0x5FB044) : hex(0x4E9C38));
    }
    for (int y = 9; y < 16; ++y) put(clover, 8, y, hex(0x3E7A2A));
    const Col fc[4] = {hex(0xE8323A), hex(0xFFD23F), hex(0x9B59D0), hex(0xFFFFFF)};
    for (int k = 0; k < 4; ++k) {
        clear(flower[k], 16, 16);
        for (int y = 7; y < 16; ++y) put(flower[k], 8 + (y > 12 ? 1 : 0), y, hex(0x3F8A2E));
        put(flower[k], 6, 12, hex(0x4E9C38)); put(flower[k], 7, 11, hex(0x4E9C38));
        for (int i = 0; i < 5; ++i) {
            const double a = i * 2 * M_PI / 5;
            disc(flower[k], 8 + std::cos(a) * 2.6, 5 + std::sin(a) * 2.6, 1.9, fc[k]);
        }
        disc(flower[k], 8, 5, 1.4, k == 1 ? hex(0xC9651F) : hex(0xFFE14D));
    }
    clear(mushroom_cap, 32, 32);
    base_fill(mushroom_cap, 32, hex(0xB01E1E), hex(0xE8413A), 38, 1.0);
    for (int i = 0; i < 9; ++i) { const int x = g.below(32), y = g.below(32); disc(mushroom_cap, x, y, 1.7, hex(0xFFF4E0)); }
    clear(star, 16, 16);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const double dx = x + .5 - 8, dy = y + .5 - 8.5, a = std::atan2(dy, dx) + M_PI / 2, r = std::hypot(dx, dy);
            const double lim = 4.0 + 3.6 * std::pow(std::fabs(std::cos(a * 2.5)), 3.0);
            if (r < lim) put(star, x, y, r < lim - 1.3 ? (dx + dy < -1 ? hex(0xFFF7B0) : hex(0xFFD23F)) : hex(0xB8860B));
        }
    clear(glow, 32, 32); disc(glow, 16, 16, 16, hex(0xFFFFFF), true);
    clear(puff, 16, 16); disc(puff, 8, 8, 8, hex(0xFFFFFF), true);
    clear(shadow, 32, 32); disc(shadow, 16, 16, 16, hex(0x000000), true);
    clear(sparkle, 8, 8);
    for (int i = 0; i < 8; ++i) { put(sparkle, i, 4, alpha(hex(0xFFFFFF), 1.f - std::fabs(i - 3.5f) / 4.5f)); put(sparkle, 4, i, alpha(hex(0xFFFFFF), 1.f - std::fabs(i - 3.5f) / 4.5f)); }
    const Col lc[4] = {hex(0x5FA33C), hex(0xE07B28), hex(0xC93A1F), hex(0xE8B23A)};
    for (int k = 0; k < 4; ++k) {
        clear(leaf_sprite[k], 8, 8);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                const double dx = x - 3.5, dy = y - 3.5;
                if (std::fabs(dx + dy) < 1.2 + 2.2 * (1 - std::fabs(dx - dy) / 8.0)) put(leaf_sprite[k], x, y, x == y ? shade(lc[k], .7f) : lc[k]);
            }
    }
    clear(lily, 32, 32);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            const double dx = x + .5 - 16, dy = y + .5 - 16, a = std::atan2(dy, dx);
            if (std::hypot(dx, dy) < 15 && !(a > -.25 && a < .25)) put(lily, x, y, shade(hex(0x4E9C38), static_cast<float>(.85 + .3 * vnoise(x, y, 32, 8, 39))));
        }
    flagcloth.make(16, 16);
    canvas.make(32, 32);
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) put(canvas, x, y, (x / 4) % 2 ? hex(0xE9DFC4) : hex(0xC0392B));
}

const Textures& textures() {
    static Textures t = [] { Textures x; x.build(); return x; }();
    return t;
}

}  // namespace eggy
