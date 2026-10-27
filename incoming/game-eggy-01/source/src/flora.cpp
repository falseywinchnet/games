#include "flora.hpp"

#include "ground.hpp"
#include "raster.hpp"

#include <algorithm>
#include <cmath>

namespace eggy {

namespace {
struct R {
    std::uint64_t s;
    explicit R(std::uint64_t seed) : s(mix64(seed) | 1) {}
    double uni() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return static_cast<double>(s >> 11) * (1.0 / 9007199254740992.0); }
    double in(double a, double b) { return a + (b - a) * uni(); }
    int below(int n) { return static_cast<int>(uni() * n); }
};
M34 at(double x, double y, double z) { return M34::translate(x, y, z); }
M34 sc(double x, double y, double z) { return M34::scale(x, y, z); }
const Col kW{1, 1, 1, 1};
Col mulc(Col a, Col b) { return {a.r * b.r, a.g * b.g, a.b * b.b, a.a * b.a}; }

// Convert a premultiplied canvas to a cut-out texture with mips.
void to_tex(const Canvas& c, Tex& t) {
    t.make(c.w, c.h);
    for (int y = 0; y < c.h; ++y)
        for (int x = 0; x < c.w; ++x) {
            const std::uint8_t* p = c.px.data() + (static_cast<size_t>(y) * c.w + x) * 4;
            const unsigned a = p[3];
            std::uint32_t v = 0;
            if (a > 0) {
                const unsigned r = std::min(255u, p[2] * 255u / a), g = std::min(255u, p[1] * 255u / a), b = std::min(255u, p[0] * 255u / a);
                v = (a << 24) | (r << 16) | (g << 8) | b;
            }
            t.at(x, y) = v;
        }
    t.build_mips();
}

void noise_tex(Tex& t, int n, Col dark, Col light, std::uint64_t seed, int cell, float contrast, int marks, Col mark) {
    t.make(n, n);
    R g(seed);
    std::vector<float> h(static_cast<size_t>(n) * n);
    auto lat = [&](int x, int y, int p) {
        x = ((x % p) + p) % p; y = ((y % p) + p) % p;
        return static_cast<double>(mix64(seed ^ (static_cast<std::uint64_t>(x) * 73856093ULL) ^ (static_cast<std::uint64_t>(y) * 19349663ULL)) >> 11) / 9007199254740992.0;
    };
    auto vn = [&](double x, double y, int c) {
        const int p = std::max(1, n / c);
        const double fx = x / c, fy = y / c;
        const int ix = static_cast<int>(std::floor(fx)), iy = static_cast<int>(std::floor(fy));
        double tx = fx - ix, ty = fy - iy;
        tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
        const double a = lat(ix, iy, p), b = lat(ix + 1, iy, p), cc = lat(ix, iy + 1, p), d = lat(ix + 1, iy + 1, p);
        return (a + (b - a) * tx) + ((cc + (d - cc) * tx) - (a + (b - a) * tx)) * ty;
    };
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const double v = std::clamp((.6 * vn(x, y, cell) + .4 * vn(x, y, std::max(2, cell / 3)) - .5) * contrast + .5, 0.0, 1.0);
            h[static_cast<size_t>(y * n + x)] = static_cast<float>(v);
        }
    for (int i = 0; i < marks; ++i) {  // blobs of the mark colour (leaf clusters, spots, lenticels)
        const int x = g.below(n), y = g.below(n), rad = 1 + g.below(3);
        for (int dy = -rad; dy <= rad; ++dy)
            for (int dx = -rad; dx <= rad; ++dx)
                if (dx * dx + dy * dy <= rad * rad) h[static_cast<size_t>(((y + dy + n) % n) * n + (x + dx + n) % n)] += 2.f;
    }
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float v = h[static_cast<size_t>(y * n + x)];
            Col c = v > 1.5f ? mark : mix(dark, light, v);
            const float dh = h[static_cast<size_t>(((y + n - 1) % n) * n + (x + n - 1) % n)] - h[static_cast<size_t>(((y + 1) % n) * n + (x + 1) % n)];
            c = shade(c, std::clamp(1.f + .35f * dh, .65f, 1.4f));
            t.at(x, y) = 0xFF000000u | (static_cast<std::uint32_t>(std::clamp(c.r, 0.f, 1.f) * 255) << 16) |
                         (static_cast<std::uint32_t>(std::clamp(c.g, 0.f, 1.f) * 255) << 8) | static_cast<std::uint32_t>(std::clamp(c.b, 0.f, 1.f) * 255);
        }
    t.build_mips();
}

const Col kPetals[24] = {hex(0xE8323A), hex(0xFF6F91), hex(0xF59AC7), hex(0xC04BC8), hex(0x9B59D0), hex(0x6A5ACD), hex(0x3F7FE0),
                         hex(0x6EC6FF), hex(0xFFFFFF), hex(0xFFF2C0), hex(0xFFD23F), hex(0xFFB03B), hex(0xFF7A2B), hex(0xD4E04A),
                         hex(0xB0E0FF), hex(0xE0B0FF), hex(0xFFC0CB), hex(0xC71585), hex(0x8B1A4A), hex(0x4B3FA8), hex(0xF0E68C),
                         hex(0xFA8072), hex(0x98FB98), hex(0xFFDAB9)};

void paint_flower(Canvas& c, R& g, double& size) {
    c.resize(32, 32);
    c.clear({0, 0, 0, 0});
    const int type = g.below(5);
    const Col pc = kPetals[g.below(24)], pc2 = g.uni() < .3 ? kPetals[g.below(24)] : pc;
    const Col center = std::array<Col, 4>{hex(0xFFE14D), hex(0xC9651F), hex(0x3A2410), hex(0xFFFFFF)}[static_cast<size_t>(g.below(4))];
    const Col stem = mix(hex(0x2F7A2A), hex(0x6AA040), static_cast<float>(g.uni()));
    const int heads = 1 + (g.uni() < .45 ? g.below(4) : 0);
    size = g.in(.32, .62);
    for (int hh = 0; hh < heads; ++hh) {
        const double hx = 16 + (heads > 1 ? (hh - (heads - 1) * .5) * 6 + g.in(-1.5, 1.5) : 0), hy = g.in(5, 12) + (hh % 2) * 3;
        c.begin(); c.move(16, 32); c.quad((16 + hx) / 2 + g.in(-2, 2), (32 + hy) / 2, hx, hy + 2); c.stroke(stem, 1.3);
        if (g.uni() < .6) { c.fill_ellipse(16 + g.in(-4, 4), g.in(22, 28), 3, 1.3, shade(stem, 1.1f)); }
        const double pr = type == 3 ? 1.6 : g.in(2.2, 4.2);
        switch (type) {
            case 0: case 4: {  // radial petals / cup
                const int n = 4 + g.below(5);
                for (int i = 0; i < n; ++i) {
                    const double a = i * 2 * M_PI / n + g.in(0, .2);
                    c.save(); c.translate(hx, hy); c.rotate(a);
                    c.fill_ellipse(pr * (type == 4 ? .5 : .9), 0, pr * (type == 4 ? .7 : 1.0), pr * .55, i % 2 ? pc2 : pc);
                    c.restore();
                }
                c.fill_circle(hx, hy, pr * .45, center);
                break;
            }
            case 1: {  // daisy: many thin rays
                const int n = 10 + g.below(6);
                for (int i = 0; i < n; ++i) {
                    const double a = i * 2 * M_PI / n;
                    c.stroke_line(hx, hy, hx + std::cos(a) * pr * 1.3, hy + std::sin(a) * pr * 1.3, pc, 1.1);
                }
                c.fill_circle(hx, hy, pr * .5, center);
                break;
            }
            case 2: {  // nodding bells
                for (int i = 0; i < 3; ++i) {
                    const double bx = hx + (i - 1) * 3, by = hy + 2 + i % 2 * 2;
                    c.begin(); c.move(bx - 1.6, by); c.quad(bx, by - 3, bx + 1.6, by); c.line(bx + 2, by + 2.5); c.line(bx - 2, by + 2.5); c.close(); c.fill(pc);
                }
                break;
            }
            case 3: {  // spike of tiny florets
                for (int i = 0; i < 7; ++i) c.fill_circle(hx + g.in(-1, 1), hy + i * 1.6, 1.6 - i * .1, i % 2 ? pc : shade(pc, 1.15f));
                break;
            }
        }
    }
}

void paint_fern(Canvas& c, R& g, double& size) {
    c.resize(64, 64);
    c.clear({0, 0, 0, 0});
    const int fronds = 3 + g.below(7);
    const Col base = std::array<Col, 6>{hex(0x3E7A2A), hex(0x5FA33C), hex(0x2E6A4A), hex(0x7A9A3A), hex(0x8A5A2A), hex(0x4E8A5A)}[static_cast<size_t>(g.below(6))];
    const double curl = g.in(-.04, .04), len = g.in(22, 34), leaf = g.in(2, 4.5), droop = g.in(0, .05);
    size = g.in(.45, .9);
    for (int f = 0; f < fronds; ++f) {
        double a = -M_PI / 2 + (f - (fronds - 1) * .5) * g.in(.25, .4);
        double x = 32, y = 63;
        const Col fc = shade(base, static_cast<float>(g.in(.85, 1.15)));
        for (int k = 0; k < static_cast<int>(len); ++k) {
            const double nx = x + std::cos(a), ny = y + std::sin(a);
            c.stroke_line(x, y, nx, ny, shade(fc, .7f), .9);
            if (k > 3 && k % 2 == 0) {
                const double ls = leaf * (1 - k / len * .8);
                for (int s = -1; s <= 1; s += 2) {
                    const double la = a + s * 1.3;
                    c.fill_ellipse(nx + std::cos(la) * ls * .6, ny + std::sin(la) * ls * .6, ls * .65, ls * .3, fc);
                }
            }
            x = nx; y = ny;
            a += curl + droop * (a > -M_PI / 2 ? 1 : -1);
        }
    }
}

void paint_tuft(Canvas& c, R& g) {
    c.resize(32, 32);
    c.clear({0, 0, 0, 0});
    const Col base = std::array<Col, 6>{hex(0x6FB43E), hex(0x9AD45A), hex(0xC9C46A), hex(0x7A9A6A), hex(0xA86A4A), hex(0x5E9E36)}[static_cast<size_t>(g.below(6))];
    const int n = 8 + g.below(14);
    const bool seeds = g.uni() < .35;
    for (int i = 0; i < n; ++i) {
        const double x0 = 16 + g.in(-6, 6), h = g.in(8, 28), lean = g.in(-7, 7);
        c.begin(); c.move(x0, 32); c.quad(x0 + lean * .3, 32 - h * .6, x0 + lean, 32 - h); c.stroke(shade(base, static_cast<float>(g.in(.75, 1.2))), 1.1);
        if (seeds) c.fill_ellipse(x0 + lean, 32 - h, 1.1, 2, hex(0xD8C89A));
    }
}
}  // namespace

const Flora& flora() {
    static Flora F = [] {
        Flora f;
        R g(20261001);
        Canvas c;
        for (int i = 0; i < kFlowers; ++i) { paint_flower(c, g, f.flower_size[static_cast<size_t>(i)]); to_tex(c, f.flower[static_cast<size_t>(i)]); }
        for (int i = 0; i < kFerns; ++i) { paint_fern(c, g, f.fern_size[static_cast<size_t>(i)]); to_tex(c, f.fern[static_cast<size_t>(i)]); }
        for (int i = 0; i < kTufts; ++i) { paint_tuft(c, g); to_tex(c, f.tuft[static_cast<size_t>(i)]); }
        // bark: brown, dark, grey, birch, reddish pine, mossy, silver, banded cherry
        const unsigned bark[8][3] = {{0x3E2A1A, 0x7A5636, 0x24170D}, {0x2A1E14, 0x4E3A28, 0x140E08}, {0x5E5A54, 0x8E8880, 0x3A3632},
                                     {0xD8D4CC, 0xF4F2EE, 0x2A2826}, {0x6A3420, 0xA85A34, 0x3A1A10}, {0x3A4426, 0x5E6A3A, 0x22281A},
                                     {0x8A8E92, 0xB8BCC0, 0x5A5E62}, {0x5A2A22, 0x8A4A3A, 0xC08A70}};
        for (int i = 0; i < 8; ++i) noise_tex(f.bark[static_cast<size_t>(i)], 32, hex(bark[i][0]), hex(bark[i][1]), 300 + i, 8, 1.6f, i == 3 ? 14 : 4, hex(bark[i][2]));
        const unsigned leaf[12][3] = {{0x1F4A1E, 0x5E9E3A, 0x8CC653}, {0x174018, 0x3E7A2E, 0x6AA040}, {0x3A4A1A, 0x7A8A3A, 0xA8B45A},
                                      {0x1A4A3A, 0x3E7A62, 0x6AA48A}, {0x3A6A1A, 0x8AC63A, 0xC4E46A}, {0x0F3A1A, 0x2A5E2E, 0x4E8A44},
                                      {0x8A3A14, 0xD9822B, 0xF0A030}, {0x6A1A10, 0xC93A1F, 0xE86A3A}, {0x8A6A14, 0xE8B23A, 0xF8D85A},
                                      {0x5A2A1A, 0x9A4A2A, 0xC87040}, {0x173A28, 0x2F6447, 0x4E8C5E}, {0x2A4A5A, 0x4E7A8A, 0x8AB0C0}};
        for (int i = 0; i < 12; ++i) noise_tex(f.leaf[static_cast<size_t>(i)], 64, hex(leaf[i][0]), hex(leaf[i][1]), 400 + i, 8, 1.8f, 90, hex(leaf[i][2]));
        const unsigned cap[8][3] = {{0xB01E1E, 0xE8413A, 0xFFF4E0}, {0x6A4424, 0x9A6A3E, 0x5A3418}, {0xDCD4C4, 0xF8F4EC, 0xC8BEA8},
                                    {0xC8A01E, 0xF0D040, 0xFFF0A0}, {0x5A2A7A, 0x8A4AAA, 0xD8B8F0}, {0xC8501E, 0xF08A3A, 0xFFF0D8},
                                    {0x4A5A7A, 0x7A8AAA, 0xC8D4E8}, {0x8A6A44, 0xC4A070, 0x6A4A2A}};
        for (int i = 0; i < 8; ++i) noise_tex(f.cap[static_cast<size_t>(i)], 32, hex(cap[i][0]), hex(cap[i][1]), 500 + i, 8, 1.2f, i == 0 || i == 5 ? 10 : 3, hex(cap[i][2]));
        for (int i = 0; i < 24; ++i) f.rock_meshes.push_back(rock_mesh(900 + static_cast<std::uint64_t>(i), 7, 5, .3 + .25 * (i % 4) / 3.0));
        for (int i = 0; i < 6; ++i) f.blob_meshes.push_back(rock_mesh(950 + static_cast<std::uint64_t>(i), 7, 5, .4));
        // trees
        for (int i = 0; i < kTrees; ++i) {
            TreeSpecies& t = f.trees[static_cast<size_t>(i)];
            const int k = i % 25;
            t.canopy = k < 8 ? Canopy::blobs : k < 15 ? Canopy::conifer : k < 17 ? Canopy::column : k < 18 ? Canopy::umbrella
                     : k < 20 ? Canopy::weeping : k < 23 ? Canopy::birch : Canopy::snag;
            if (i >= 25 && t.canopy == Canopy::blobs) t.canopy = (i % 2) ? Canopy::blobs : Canopy::conifer;
            t.height = g.in(.75, 1.35);
            t.trunk = g.in(.06, .12);
            t.spread = g.in(.8, 1.3);
            t.blobs = 2 + g.below(4);
            t.tiers = 2 + g.below(4);
            t.tint = {static_cast<float>(g.in(.9, 1.1)), static_cast<float>(g.in(.9, 1.1)), static_cast<float>(g.in(.9, 1.1)), 1};
            const bool autumn = i % 5 == 1 || i % 7 == 3;
            switch (t.canopy) {
                case Canopy::blobs: t.bark = g.below(3) == 0 ? 2 : g.below(2); t.leaf = autumn ? 6 + g.below(4) : g.below(6);
                    t.biomes = autumn ? (1u << static_cast<int>(Biome::autumn)) : (1u << static_cast<int>(Biome::meadow)) | (1u << static_cast<int>(Biome::forest)) | (1u << static_cast<int>(Biome::pond));
                    break;
                case Canopy::conifer: t.bark = g.uni() < .5 ? 4 : 1; t.leaf = g.uni() < .7 ? 10 : 11; t.height *= 1.15;
                    t.biomes = (1u << static_cast<int>(Biome::forest)) | (1u << static_cast<int>(Biome::alpine)) | (1u << static_cast<int>(Biome::snow)) |
                               (1u << static_cast<int>(Biome::ice)) | (1u << static_cast<int>(Biome::ridge)) | (1u << static_cast<int>(Biome::summit)) | (i % 3 == 0 ? (1u << static_cast<int>(Biome::meadow)) : 0u);
                    break;
                case Canopy::column: t.bark = 0; t.leaf = 5; t.height *= 1.3;
                    t.biomes = (1u << static_cast<int>(Biome::meadow)) | (1u << static_cast<int>(Biome::pond)) | (1u << static_cast<int>(Biome::alpine)); break;
                case Canopy::umbrella: t.bark = 4; t.leaf = 2; t.biomes = (1u << static_cast<int>(Biome::meadow)) | (1u << static_cast<int>(Biome::ravine)); break;
                case Canopy::weeping: t.bark = 2; t.leaf = 4; t.biomes = (1u << static_cast<int>(Biome::pond)) | (1u << static_cast<int>(Biome::meadow)); break;
                case Canopy::birch: t.bark = 3; t.leaf = autumn ? 8 : 4; t.trunk *= .7;
                    t.biomes = (1u << static_cast<int>(Biome::forest)) | (1u << static_cast<int>(Biome::alpine)) | (1u << static_cast<int>(Biome::meadow)) | (1u << static_cast<int>(Biome::autumn)); break;
                case Canopy::snag: t.bark = 6; t.leaf = 2; t.blobs = g.below(2);
                    t.biomes = (1u << static_cast<int>(Biome::ridge)) | (1u << static_cast<int>(Biome::ravine)) | (1u << static_cast<int>(Biome::alpine)) | (1u << static_cast<int>(Biome::snow)); break;
            }
        }
        for (int i = 0; i < kLogs; ++i) {
            LogSpecies& l = f.logs[static_cast<size_t>(i)];
            l.radius = g.in(.13, .24); l.bark = g.below(8); l.mossy = g.uni() < .35; l.hollow = g.uni() < .2; l.brackets = g.uni() < .4 ? 1 + g.below(3) : 0;
            l.tint = l.mossy ? Col{.8f, 1.f, .75f, 1} : Col{static_cast<float>(g.in(.85, 1.1)), static_cast<float>(g.in(.85, 1.05)), static_cast<float>(g.in(.8, 1.0)), 1};
        }
        const Col berries[6] = {hex(0xD0202A), hex(0x3A4AC8), hex(0xFFFFFF), hex(0x7A2A8A), hex(0xF08A2A), hex(0xFF8AC0)};
        for (int i = 0; i < kBushes; ++i) {
            BushSpecies& b = f.bushes[static_cast<size_t>(i)];
            b.blobs = 2 + g.below(4); b.radius = g.in(.16, .3); b.height = g.in(.18, .42); b.leaf = i % 6 == 5 ? 6 + g.below(3) : g.below(6);
            b.tint = {static_cast<float>(g.in(.85, 1.1)), static_cast<float>(g.in(.9, 1.1)), static_cast<float>(g.in(.85, 1.05)), 1};
            b.berries = g.uni() < .6 ? 4 + g.below(9) : 0; b.berry = berries[g.below(6)];
        }
        for (int i = 0; i < kMushrooms; ++i) {
            MushroomSpecies& m = f.mushrooms[static_cast<size_t>(i)];
            m.shape = g.below(4); m.cap_r = g.in(.035, .1); m.cap_h = g.in(.4, 1.2); m.stem_h = g.in(.03, .13); m.stem_r = g.in(.15, .35);
            m.cap_tex = g.below(8); m.cluster = 1 + g.below(5);
            m.cap = {static_cast<float>(g.in(.85, 1.1)), static_cast<float>(g.in(.85, 1.1)), static_cast<float>(g.in(.85, 1.1)), 1};
            m.stem = std::array<Col, 4>{hex(0xF2E8D8), hex(0xE8DCC0), hex(0xC8B490), hex(0xFFFFFF)}[static_cast<size_t>(g.below(4))];
        }
        const int rock_grounds[10] = {g_granite, g_slate, g_sandstone, g_basalt, g_lichen_rock, g_limestone, g_shale, g_snow_rock, g_granite, g_lichen_rock};
        for (int i = 0; i < kRocks; ++i) {
            RockSpecies& k = f.rocks[static_cast<size_t>(i)];
            k.mesh = i % 24; k.sx = g.in(.75, 1.3); k.sy = g.in(.7, 1.2); k.sz = g.in(.45, 1.15); k.ground = rock_grounds[i % 10];
            k.tint = {static_cast<float>(g.in(.88, 1.1)), static_cast<float>(g.in(.88, 1.08)), static_cast<float>(g.in(.88, 1.08)), 1};
            k.moss = g.uni() < .25;
        }
        return f;
    }();
    return F;
}

int tree_for(Biome b, unsigned hash) {
    static std::vector<int> lists[static_cast<int>(Biome::count)];
    static bool init = false;
    if (!init) {
        const Flora& f = flora();
        for (int bi = 0; bi < static_cast<int>(Biome::count); ++bi) {
            for (int i = 0; i < kTrees; ++i)
                if (f.trees[static_cast<size_t>(i)].biomes & (1u << bi)) lists[bi].push_back(i);
            if (lists[bi].empty()) lists[bi].push_back(8);
        }
        init = true;
    }
    const auto& l = lists[static_cast<int>(b)];
    return l[hash % l.size()];
}

int rock_for(Biome b, unsigned hash) {
    // rocks whose stone suits the biome
    static std::vector<int> lists[static_cast<int>(Biome::count)];
    static bool init = false;
    if (!init) {
        const Flora& f = flora();
        for (int bi = 0; bi < static_cast<int>(Biome::count); ++bi)
            for (int i = 0; i < kRocks; ++i) {
                const int gr = f.rocks[static_cast<size_t>(i)].ground;
                const Biome bb = static_cast<Biome>(bi);
                bool ok;
                if (bb == Biome::snow || bb == Biome::ice || bb == Biome::summit) ok = gr == g_snow_rock || gr == g_slate || gr == g_granite;
                else if (bb == Biome::ridge) ok = gr == g_basalt || gr == g_slate || gr == g_snow_rock;
                else if (bb == Biome::ravine) ok = gr == g_granite || gr == g_sandstone || gr == g_lichen_rock || gr == g_shale;
                else ok = gr != g_snow_rock && gr != g_basalt;
                if (ok) lists[bi].push_back(i);
            }
        init = true;
    }
    const auto& l = lists[static_cast<int>(b)];
    return l.empty() ? 0 : l[hash % l.size()];
}

M34 g_tree_pre;  // identity unless a tree is toppling

void draw_tree(R3D& r, const TreeSpecies& t, double x, double y, double z, double sway, double s, bool snowy, std::uint8_t mat, Col fade) {
    const Flora& F = flora();
    const Tex* bark = &F.bark[static_cast<size_t>(t.bark)];
    const Tex* lf = &F.leaf[static_cast<size_t>(t.leaf)];
    const Col tint = mulc(t.tint, fade);
    const double h = t.height * s, tr = t.trunk * s;
    switch (t.canopy) {
        case Canopy::conifer: {
            draw_mesh(r, cylinder_mesh(5), g_tree_pre * at(x, y, z) * sc(tr, tr, h * .4), bark, fade, mat);
            for (int i = 0; i < t.tiers; ++i) {
                const double f = static_cast<double>(i) / t.tiers;
                const double rad = (.55 - .35 * f) * s * t.spread, zz = z + (.22 + .62 * f) * h, sw = sway * (i + 1) * .35;
                draw_mesh(r, cone_mesh(7), g_tree_pre * at(x + sw, y, zz) * M34::rot_z(i * .7) * sc(rad, rad, h * (.62 - .2 * f)), lf, tint, mat, 1.5);
                if (snowy) draw_mesh(r, cone_mesh(7), g_tree_pre * at(x + sw, y, zz + h * (.62 - .2 * f) * .5) * sc(rad * .52, rad * .52, h * .3 * (.62 - .2 * f) / .62 * 1.6), nullptr, mulc(hex(0xF4F8FF), fade), mat);
            }
            break;
        }
        case Canopy::column:
            draw_mesh(r, cylinder_mesh(5), g_tree_pre * at(x, y, z) * sc(tr, tr, h * .3), bark, fade, mat);
            draw_mesh(r, F.blob_meshes[1], g_tree_pre * at(x + sway, y, z + h * .7) * sc(.26 * s * t.spread, .26 * s * t.spread, h * .55), lf, tint, mat, 2);
            break;
        case Canopy::umbrella:
            draw_mesh(r, cylinder_mesh(5), g_tree_pre * at(x, y, z) * M34::rot_y(.12) * sc(tr, tr, h * .95), bark, fade, mat);
            draw_mesh(r, F.blob_meshes[2], g_tree_pre * at(x + sway + h * .1, y, z + h) * sc(.7 * s * t.spread, .6 * s * t.spread, .2 * s), lf, tint, mat, 2);
            break;
        case Canopy::snag: {
            draw_mesh(r, cylinder_mesh(5), g_tree_pre * at(x, y, z) * sc(tr, tr, h * .85), bark, fade, mat);
            for (int i = 0; i < 3; ++i)
                draw_mesh(r, cylinder_mesh(4), g_tree_pre * at(x, y, z + h * (.4 + .15 * i)) * M34::rot_z(i * 2.1) * M34::rot_y(.9) * sc(tr * .5, tr * .5, h * .3), bark, fade, mat);
            if (t.blobs) draw_mesh(r, F.blob_meshes[3], g_tree_pre * at(x + sway, y, z + h * .85) * sc(.18 * s, .18 * s, .14 * s), lf, tint, mat, 2);
            break;
        }
        default: {  // blobs, weeping, birch
            const bool weeping = t.canopy == Canopy::weeping, birch = t.canopy == Canopy::birch;
            draw_mesh(r, cylinder_mesh(6), g_tree_pre * at(x, y, z) * sc(tr, tr, h * (birch ? 1.0 : .85)), bark, fade, mat);
            const double cz = z + h * (birch ? .95 : .85);
            const double rr = (birch ? .3 : .45) * s * t.spread;
            for (int i = 0; i < t.blobs; ++i) {
                const double a = i * 2.4 + t.height * 7, d = i ? rr * .55 : 0;
                const double bz = cz + (weeping ? -.12 * i * s : (i ? -.05 : .18) * s);
                const double k = i ? .72 : 1.0;
                draw_mesh(r, F.blob_meshes[static_cast<size_t>(i % 6)], g_tree_pre * at(x + std::cos(a) * d + sway, y + std::sin(a) * d, bz) *
                          sc(rr * k, rr * k, rr * k * (weeping ? 1.25 : .9)), lf, mulc(tint, i % 2 ? Col{.92f, .95f, .92f, 1} : kW), mat, 2);
            }
            break;
        }
    }
}

void draw_log(R3D& r, const LogSpecies& l, double x0, double x1, double y, double z, bool cl, bool cr) {
    const Flora& F = flora();
    const Tex* bark = &F.bark[static_cast<size_t>(l.bark)];
    draw_mesh(r, cylinder_mesh(8), at(x0, y, z) * M34::rot_y(M_PI / 2) * sc(l.radius, l.radius, x1 - x0), bark, l.tint, opaque, 1);
    static const Tex* end = nullptr;
    (void)end;
    auto cap = [&](double x, double dir) {
        draw_mesh(r, disc_mesh(8), at(x, y, z) * M34::rot_y(dir * M_PI / 2) * sc(l.radius, l.radius, 1), l.hollow ? nullptr : bark,
                  l.hollow ? hex(0x2A1A10) : hex(0xD8B080), opaque | double_sided);
    };
    if (cr) cap(x1, 1);
    if (cl) cap(x0, -1);
    for (int i = 0; i < l.brackets; ++i)
        draw_mesh(r, hemisphere_mesh(6, 2), at(x0 + (x1 - x0) * (.25 + .25 * i), y - l.radius * .9, z + l.radius * .1) * M34::rot_x(M_PI / 2) *
                  sc(.06, .06, .035), &F.cap[1], hex(0xE0C8A0), opaque | double_sided);
}

void draw_bush(R3D& r, const BushSpecies& b, double x, double y, double z, double sway, unsigned seed, std::uint8_t mat, Col fade) {
    const Flora& F = flora();
    const Tex* lf = &F.leaf[static_cast<size_t>(b.leaf)];
    for (int i = 0; i < b.blobs; ++i) {
        const double a = i * 2.3 + seed, d = i ? b.radius * .6 : 0;
        const double k = i ? .75 : 1;
        draw_mesh(r, F.blob_meshes[static_cast<size_t>((i + seed) % 6)], at(x + std::cos(a) * d + sway * .5, y + std::sin(a) * d, z + b.height * .45 * k) *
                  sc(b.radius * k, b.radius * k, b.height * .55 * k), lf, mulc(b.tint, fade), mat, 2);
    }
    for (int i = 0; i < b.berries; ++i) {
        const double a = i * 2.39996 + seed, rr = b.radius * (.5 + .45 * ((i * 7 + seed) % 10) / 10.0);
        draw_mesh(r, sphere_mesh(4, 3), at(x + std::cos(a) * rr, y + std::sin(a) * rr, z + b.height * (.3 + .5 * ((i * 3) % 5) / 5.0)) * sc(.025, .025, .025),
                  nullptr, mulc(b.berry, fade), mat | unlit);
    }
}

void draw_mushrooms(R3D& r, const MushroomSpecies& m, double x, double y, double z, unsigned seed) {
    const Flora& F = flora();
    const Tex* ct = &F.cap[static_cast<size_t>(m.cap_tex)];
    for (int i = 0; i < m.cluster; ++i) {
        const double a = i * 2.4 + seed, d = i ? .06 + .03 * (i % 3) : 0;
        const double k = i ? .65 + .1 * (i % 3) : 1.0;
        const double mx = x + std::cos(a) * d, my = y + std::sin(a) * d;
        const double sh = m.stem_h * k, cr = m.cap_r * k;
        draw_mesh(r, cylinder_mesh(5), at(mx, my, z) * sc(cr * m.stem_r, cr * m.stem_r, sh), nullptr, m.stem, opaque);
        switch (m.shape) {
            case 0: draw_mesh(r, hemisphere_mesh(8, 3), at(mx, my, z + sh) * sc(cr, cr, cr * m.cap_h), ct, m.cap, opaque, 1); break;
            case 1: draw_mesh(r, hemisphere_mesh(8, 3), at(mx, my, z + sh) * sc(cr * 1.2, cr * 1.2, cr * .3), ct, m.cap, opaque, 1); break;
            case 2: draw_mesh(r, cone_mesh(7), at(mx, my, z + sh * .8) * sc(cr * .8, cr * .8, cr * (1.2 + m.cap_h)), ct, m.cap, opaque, 1); break;
            default: draw_mesh(r, hemisphere_mesh(8, 3), at(mx, my, z + sh * .7) * sc(cr * .8, cr * .8, cr * (1.1 + m.cap_h)), ct, m.cap, opaque, 1); break;
        }
    }
}

void draw_rock(R3D& r, const RockSpecies& k, double x, double y, double z, double size, bool snowy, std::uint8_t mat, Col fade) {
    const Flora& F = flora();
    const Ground& G = ground();
    const double sx = k.sx * size, sy = k.sy * size, sz = k.sz * size;
    draw_mesh(r, F.rock_meshes[static_cast<size_t>(k.mesh)], at(x, y, z + sz * .55) * M34::rot_z(k.mesh * .37) * sc(sx, sy, sz),
              &G.tex[static_cast<size_t>(k.ground)], mulc(k.tint, fade), mat, 1);
    if (snowy) draw_mesh(r, hemisphere_mesh(7, 3), at(x, y, z + sz * 1.3) * sc(sx * .75, sy * .7, sz * .25), &G.tex[g_snow], fade, mat, 1);
    else if (k.moss) draw_mesh(r, hemisphere_mesh(7, 3), at(x, y, z + sz * 1.32) * sc(sx * .55, sy * .5, sz * .14), &G.tex[g_moss], fade, mat, 1);
}

}  // namespace eggy
