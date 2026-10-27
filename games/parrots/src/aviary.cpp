#include "aviary.hpp"

#include "platform/render.hpp"
#include "soil.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace pt {

namespace aviary_detail {
const Col kWhite{1, 1, 1, 1};
const Col kInk = hex(0x1A1418);
constexpr double kPi = 3.14159265358979323846;
// Where the player stands: low near the front, looking up into the dome.
const V3 kEye = eye_at();
constexpr double kPitch = -.2, kPersp = 10;

M34 at(V3 v) { return M34::translate(v.x, v.y, v.z); }
M34 sc(double x, double y, double z) { return M34::scale(x, y, z); }
// The unit cylinder (z in [0, 1], radius 1) stood from a to b with radius `rad`.
M34 along(V3 a, V3 b, double rad) {
    const V3 w = b - a, d = norm(w);
    const V3 u = norm(cross(std::fabs(d.z) < .9 ? V3{0, 0, 1} : V3{1, 0, 0}, d)), v = cross(d, u);
    M34 m;
    m.m[0] = u.x * rad; m.m[4] = u.y * rad; m.m[8] = u.z * rad;
    m.m[1] = v.x * rad; m.m[5] = v.y * rad; m.m[9] = v.z * rad;
    m.m[2] = w.x; m.m[6] = w.y; m.m[10] = w.z;
    m.m[3] = a.x; m.m[7] = a.y; m.m[11] = a.z;
    return m;
}

std::uint32_t hsh(std::uint32_t x) { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x; }
double h01(std::uint32_t x) { return (hsh(x) & 0xFFFF) / 65535.0; }
// Smooth value noise that tiles every `period` cells, and its sum over octaves.
double vnoise(double x, double y, int period, std::uint32_t seed) {
    const int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y));
    const double fx = x - xi, fy = y - yi;
    auto v = [&](int a, int b) {
        a = ((a % period) + period) % period;
        b = ((b % period) + period) % period;
        return h01(seed + static_cast<std::uint32_t>(a * 7919 + b * 104729));
    };
    const double sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    const double a = v(xi, yi) + (v(xi + 1, yi) - v(xi, yi)) * sx, b = v(xi, yi + 1) + (v(xi + 1, yi + 1) - v(xi, yi + 1)) * sx;
    return a + (b - a) * sy;
}
double fbm(double x, double y, int period, std::uint32_t seed, int octaves = 5) {
    double sum = 0, amp = .5, total = 0;
    for (int o = 0; o < octaves; ++o) {
        sum += vnoise(x, y, period, seed + static_cast<std::uint32_t>(o) * 131) * amp;
        total += amp;
        x *= 2;
        y *= 2;
        period *= 2;
        amp *= .5;
    }
    return sum / total;
}
double smooth(double a, double b, double x) {
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

// A texture painted pixel by pixel: f(u, v) gives straight colour and alpha.
template <class F> Tex paint(int w, int h, F f) {
    Tex t;
    t.make(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const Col c = f((x + .5) / w, (y + .5) / h);
            auto b = [](float v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.f, 1.f) * 255)); };
            t.at(x, y) = b(c.a) << 24 | b(c.r) << 16 | b(c.g) << 8 | b(c.b);
        }
    t.build_mips();
    return t;
}
Col lerp(Col a, Col b, double t) { return mix(a, b, static_cast<float>(std::clamp(t, 0.0, 1.0))); }
Col scale(Col c, double k) { return {static_cast<float>(c.r * k), static_cast<float>(c.g * k), static_cast<float>(c.b * k), c.a}; }

const AviaryLook kLooks[] = {
    // A: white-painted iron, clear glass, a high summer afternoon.
    {"A  white iron, summer afternoon", hex(0xEDE8DC), hex(0x9A968C), hex(0xDCEEE8, .2f), hex(0x3E7CCC), hex(0xA8CCE8), hex(0xFFFFFF), hex(0x9AA8BC), hex(0x3A6038),
     {1.0f, .95f, .84f, 1}, {.3f, .36f, .32f, 1}, sun_dir()},
    // B: dark green iron, warm glass, golden hour.
    {"B  green iron, golden hour", hex(0x2C5240), hex(0x18301F), hex(0xFFEFD0, .2f), hex(0x5A78B4), hex(0xF4C08A), hex(0xFFE6C4), hex(0xC88870), hex(0x3C4A2C),
     {1.0f, .84f, .62f, 1}, {.46f, .4f, .4f, 1}, sun_dir()},
    // C: verdigris copper, a soft morning.
    {"C  verdigris, soft morning", hex(0x64A290), hex(0x3A6A5C), hex(0xE6F6EE, .18f), hex(0x84AADC), hex(0xE4D8E6), hex(0xFFF4F4), hex(0xB8A2BC), hex(0x46644A),
     {.98f, .93f, .88f, 1}, {.46f, .47f, .52f, 1}, sun_dir()},
};

struct Textures {
    Tex sky, sand, gravel, brick, bark, palm, bamboo, frond, banana, fern, stone, soil, rope, water, leaves, vine;
    Textures(const AviaryLook& L, std::uint64_t seed) {
        const auto s = static_cast<std::uint32_t>(seed * 7919 + 17);
        // The sky beyond the glass: blue to haze, soft cumulus from noise, the garden's
        // trees along the bottom. s runs half way round (one repeat); t from z = 40 to -5.
        sky = paint(512, 256, [&](double u, double v) {
            const double z = 40 - 45 * v;
            Col c = lerp(L.sky_top, L.sky_low, smooth(28, 0, z));
            const double n = fbm(u * 8, v * 8, 8, s, 6);
            const double cloud = smooth(.52, .7, n) * smooth(0, 6, z) * smooth(38, 20, z);
            const double lit = fbm(u * 8 + .05, v * 8 - .08, 8, s, 6);
            c = lerp(c, lerp(L.cloud_dark, L.cloud_lit, smooth(.45, .62, lit)), cloud * .95);
            // the trees beyond, their tops ragged against the sky
            const double top = 3.2 + 2.6 * fbm(u * 24, 0, 24, s + 9, 4);
            if (z < top) c = lerp(L.trees, scale(L.trees, .6), fbm(u * 64, v * 32, 64, s + 11, 3) + smooth(top, top - 3, z) * .4);
            return c;
        });
        // Sand from the soil engine: drifts, damp patches and a grain-by-grain speckle,
        // lit by this house's sun (u runs with +x, v with -y; the light is baked in).
        {
            soil::Parameters sp = soil::preset(soil::Preset::sand);
            const int side = 256;
            sp.texels_per_metre = side / 5.0;
            sp.light_azimuth = std::atan2(-sun_dir().y, sun_dir().x);
            sp.light_elevation = std::asin(sun_dir().z);
            sp.seed = s;
            soil::Image img;
            if (soil::generate_surface(sp, side, side, img)) {
                sand.make(side, side);
                for (int i = 0; i < side * side; ++i) {
                    const std::uint8_t* q = &img.rgba[static_cast<size_t>(i) * 4];
                    sand.px[static_cast<size_t>(i)] = 0xFF000000u | std::uint32_t{q[0]} << 16 | std::uint32_t{q[1]} << 8 | q[2];
                }
                sand.build_mips();
            } else {
                sand = paint(4, 4, [](double, double) { return hex(0xD8C8A0); });
            }
        }
        // The path's gravel: the same sand with rounded stones packed into it.
        {
            soil::Parameters sp = soil::preset(soil::Preset::river_gravel);
            const int side = 256;
            sp.texels_per_metre = side / 2.0;
            sp.gravel_size = .016;
            sp.light_azimuth = std::atan2(-sun_dir().y, sun_dir().x);
            sp.light_elevation = std::asin(sun_dir().z);
            sp.seed = s + 5;
            soil::Image img;
            if (soil::generate_surface(sp, side, side, img)) {
                gravel.make(side, side);
                for (int i = 0; i < side * side; ++i) {
                    const std::uint8_t* q = &img.rgba[static_cast<size_t>(i) * 4];
                    gravel.px[static_cast<size_t>(i)] = 0xFF000000u | std::uint32_t{q[0]} << 16 | std::uint32_t{q[1]} << 8 | q[2];
                }
                gravel.build_mips();
            } else {
                gravel = paint(4, 4, [](double, double) { return hex(0xA89878); });
            }
        }
        brick = paint(64, 32, [&](double u, double v) {
            const int row = static_cast<int>(v * 4);
            const double x = u * 4 + (row % 2) * .5;
            const double fu = x - std::floor(x), fv = v * 4 - row;
            Col c = lerp(hex(0x8A4030), hex(0xB0604A), h01(s + 500 + static_cast<std::uint32_t>(row * 8 + static_cast<int>(x))));
            c = scale(c, .85 + .3 * fbm(u * 16, v * 8, 16, s + 41, 4));
            if (fu < .06 || fv < .12) c = hex(0xB8AC98);
            return c;
        });
        // Driftwood bark: grey-brown, long fibres, a few dark cracks.
        bark = paint(64, 128, [&](double u, double v) {
            const double fib = fbm(u * 16, v * 2, 16, s + 51, 5), crack = fbm(u * 8, v * 1.5, 8, s + 53, 3);
            Col c = lerp(hex(0x5A4A3C), hex(0x9A8A76), fib);
            if (crack < .32) c = scale(c, .55);
            return c;
        });
        // Palm trunk: rings of old leaf bases.
        palm = paint(64, 64, [&](double u, double v) {
            const double rv = v * 6 + .3 * fbm(u * 8, v * 6, 8, s + 61, 3);
            const double ring = rv - std::floor(rv);
            Col c = lerp(hex(0x6A5644), hex(0x9C8466), smooth(.0, .7, ring));
            c = scale(c, .8 + .35 * fbm(u * 32, v * 16, 32, s + 63, 4));
            if (ring > .88) c = scale(c, .6);
            return c;
        });
        bamboo = paint(32, 64, [&](double u, double v) {
            Col c = lerp(hex(0x8A9A40), hex(0xC8B868), .5 + .25 * std::sin(u * 2 * kPi) + .3 * fbm(u * 4, v * 16, 4, s + 71, 3));
            if (v > .94) c = scale(c, .7);
            return c;
        });
        // A palm frond: a midrib and leaflets angled toward the tip, gaps between them.
        frond = paint(64, 128, [&](double u, double v) {
            const double dx = std::fabs(u - .5) * 2;
            const double env = std::sin(kPi * std::min(1.0, v * 1.05)) * .95 + .05;
            const double phase = v * 38 + dx * 5;
            const double f = phase - std::floor(phase);
            const bool leaf = dx < env && f < .62 - .2 * dx;
            if (dx < .05) return lerp(hex(0x9AA850), hex(0x6A7838), v);
            if (!leaf) return Col{0, 0, 0, 0};
            Col c = lerp(hex(0x3E8A3A), hex(0x1E5A26), dx * .7 + .3 * fbm(u * 8, v * 16, 8, s + 81, 3));
            if (f < .08) c = scale(c, 1.25);
            return Col{c.r, c.g, c.b, 1};
        });
        // A banana leaf: broad, a pale midrib, veins running out, a few tears.
        banana = paint(64, 128, [&](double u, double v) {
            const double dx = std::fabs(u - .5) * 2, env = std::pow(std::sin(kPi * v), .6);
            if (dx > env) return Col{0, 0, 0, 0};
            const double tear = fbm(v * 24, u < .5 ? 0 : 3, 24, s + 91, 2);
            const double vein = v * 30 - std::floor(v * 30);
            if (dx > .45 && tear > .72 && vein < .08) return Col{0, 0, 0, 0};
            Col c = lerp(hex(0x5EA848), hex(0x2E7032), dx * .6 + .3 * fbm(u * 4, v * 8, 4, s + 93, 3));
            if (dx < .05) c = hex(0xB8C878);
            const double lat = v * 40 - dx * 6;
            if (lat - std::floor(lat) < .12) c = scale(c, .85);
            return Col{c.r, c.g, c.b, 1};
        });
        fern = paint(32, 64, [&](double u, double v) {
            const double dx = std::fabs(u - .5) * 2, env = (1 - v) * .9 + .1;
            const double ph = v * 24 + dx * 3;
            const double f = ph - std::floor(ph);
            if (dx > env || (dx > .06 && f > .55)) return Col{0, 0, 0, 0};
            const Col c = lerp(hex(0x6AB050), hex(0x3A7A34), dx);
            return Col{c.r, c.g, c.b, 1};
        });
        stone = paint(64, 64, [&](double u, double v) {
            Col c = lerp(hex(0x9C968A), hex(0xCEC6B4), fbm(u * 8, v * 8, 8, s + 101, 6));
            if (fbm(u * 16, v * 16, 16, s + 103, 3) < .3) c = lerp(c, hex(0x5A7040), .35);  // moss in the hollows
            return c;
        });
        soil = paint(64, 64, [&](double u, double v) {
            return lerp(hex(0x3A2C20), hex(0x5E4632), fbm(u * 16, v * 16, 16, s + 111, 5));
        });
        rope = paint(16, 32, [&](double u, double v) {
            const double ph = (u + v * 2) * 2;
            return lerp(hex(0x8A6E48), hex(0xD0B488), std::sin((ph - std::floor(ph)) * kPi));
        });
        // A spray of broad leaves for a tree's crown: overlapping pointed ovals in many
        // greens, darker toward the middle of the spray, gaps of sky between.
        leaves = [&] {
            Canvas c;
            c.resize(128, 128);
            c.clear({0, 0, 0, 0});
            for (int k = 0; k < 70; ++k) {
                const double x = 10 + 108 * h01(s + 1300 + k), y = 10 + 108 * h01(s + 1400 + k);
                const double a = 2 * kPi * h01(s + 1500 + k), l = 9 + 9 * h01(s + 1600 + k), w = l * .42;
                const double edge = std::hypot(x - 64, y - 64) / 64;
                const Col g = lerp(lerp(hex(0x5E9A3E), hex(0x2A5E2A), h01(s + 1700 + k)), hex(0x1A3A1E), .6 * (1 - edge));
                const double ca = std::cos(a), sa = std::sin(a);
                c.begin();
                c.move(x - ca * l, y - sa * l);
                c.quad(x - sa * w, y + ca * w, x + ca * l, y + sa * l);
                c.quad(x + sa * w, y - ca * w, x - ca * l, y - sa * l);
                c.fill(g);
                c.begin();
                c.move(x - ca * l * .9, y - sa * l * .9);
                c.line(x + ca * l * .9, y + sa * l * .9);
                c.stroke(lerp(g, hex(0xA8C878), .35), .6);
            }
            Tex t;
            t.make(128, 128);
            for (size_t i = 0; i < t.px.size(); ++i) {
                const std::uint8_t* q = &c.px[i * 4];
                const unsigned a = q[3] > 100 ? 255u : 0u;
                auto un = [&](unsigned v) { return q[3] ? std::min(255u, v * 255 / q[3]) : 0u; };
                t.px[i] = a << 24 | un(q[2]) << 16 | un(q[1]) << 8 | un(q[0]);
            }
            t.build_mips();
            return t;
        }();
        // A liana's small leaves along a hanging stem.
        vine = paint(32, 64, [&](double u, double v) {
            const double dx = std::fabs(u - .5) * 2;
            if (dx < .08) return hex(0x5A6A38);
            const double ph = v * 7 + (u < .5 ? 0 : .5);
            const double f = ph - std::floor(ph);
            const double leaf = std::sin(kPi * f) * (1 - dx * .2);
            if (dx > leaf * .95 || f < .1) return Col{0, 0, 0, 0};
            const Col c = lerp(hex(0x5AA040), hex(0x2E6A2C), dx);
            return Col{c.r, c.g, c.b, 1};
        });
        water = paint(64, 64, [&](double u, double v) {
            const double n = fbm(u * 8, v * 8, 8, s + 121, 4);
            return lerp(hex(0x3A6A70, .7f), hex(0xA8D0D0, .7f), smooth(.45, .75, n));
        });
    }
};

// What the still part is made of: meshes in the world, drawn, and some casting shade.
struct Piece {
    const Mesh* mesh = nullptr;
    Mesh own;                 // built in world space when mesh is null
    M34 model;
    Col col = kWhite;
    std::uint16_t mat = 0;
    const Tex* tex = nullptr;
    bool cast = false;
};

struct Builder {
    std::vector<Piece> pieces;
    Piece& mesh(const Mesh& m, const M34& model, Col c, const Tex* tex = nullptr, bool cast = false, std::uint16_t mat = 0) {
        Piece p;
        p.mesh = &m;
        p.model = model;
        p.col = c;
        p.tex = tex;
        p.cast = cast;
        p.mat = mat;
        pieces.push_back(std::move(p));
        return pieces.back();
    }
    Piece& own(Col c, std::uint16_t mat, bool cast) {
        Piece p;
        p.col = c;
        p.mat = mat;
        p.cast = cast;
        pieces.push_back(std::move(p));
        return pieces.back();
    }
    // A rod from a to b in world space; its texture runs once round and `per_unit` times a
    // world unit along it.
    void rod(V3 a, V3 b, double rad, Col c, const Tex* tex = nullptr, bool cast = false, int slices = 8, double per_unit = 1) {
        Piece& p = own(kWhite, 0, cast);
        p.tex = tex;
        const M34 m = along(a, b, rad);
        const double length = len(b - a) * per_unit;
        const Mesh& cyl = cylinder_mesh(slices);
        for (const Vtx& v : cyl) {
            Vtx w = v;
            w.p = m.apply(v.p);
            w.n = norm(m.apply(v.n) - m.apply({0, 0, 0}));
            w.s = std::atan2(v.p.y, v.p.x) / (2 * kPi) + .5;
            w.t = v.p.z * length;
            w.c = c;
            p.own.push_back(w);
        }
        // keep each triangle's s from wrapping across the seam
        for (size_t i = 0; i + 2 < p.own.size(); i += 3) {
            const double hi = std::max({p.own[i].s, p.own[i + 1].s, p.own[i + 2].s});
            for (size_t k = 0; k < 3; ++k)
                if (hi - p.own[i + k].s > .5) p.own[i + k].s += 1;
        }
    }
    static void tri(Mesh& m, V3 a, V3 b, V3 c, Col col, V3 n) {
        m.push_back({a, n, 0, 0, col});
        m.push_back({b, n, 0, 0, col});
        m.push_back({c, n, 0, 0, col});
    }
    static void quad(Mesh& m, V3 a, V3 b, V3 c, V3 d, Col col, double s0 = 0, double s1 = 1, double t0 = 0, double t1 = 1) {
        const V3 n = norm(cross(b - a, d - a));
        m.push_back({a, n, s0, t1, col});
        m.push_back({b, n, s1, t1, col});
        m.push_back({c, n, s1, t0, col});
        m.push_back({a, n, s0, t1, col});
        m.push_back({c, n, s1, t0, col});
        m.push_back({d, n, s0, t0, col});
    }
};

// The house's surfaces: the wall at angle th and height z, the dome at th and phi.
V3 wall_at(double th, double z, double r = kDomeR) { return {r * std::cos(th), r * std::sin(th), z}; }
V3 dome_at(double th, double phi) {
    return {kDomeR * std::cos(phi) * std::cos(th), kDomeR * std::cos(phi) * std::sin(th), kWallH + kDomeRise * std::sin(phi)};
}
// The height of the dome's glass over a floor point.
double roof_over(double x, double y) {
    const double rho = std::hypot(x, y) / kDomeR;
    return rho >= 1 ? kWallH : kWallH + kDomeRise * std::sqrt(1 - rho * rho);
}
bool behind_player(double th) { return std::sin(th) < -.55; }

void build_house(Builder& B, const AviaryLook& L, const Textures& T) {
    const int bays = 28;
    const double step = 2 * kPi / bays;
    // the sky beyond, on a great drum
    {
        Piece& p = B.own(kWhite, static_cast<std::uint16_t>(unlit | no_fog), false);
        p.tex = &T.sky;
        const double R = 45;
        const int n = 48, rows = 8;
        for (int k = 0; k < n; ++k)
            for (int r = 0; r < rows; ++r) {
                const double a0 = -.35 + (kPi + .7) * k / n, a1 = -.35 + (kPi + .7) * (k + 1) / n;
                const double z0 = -5 + 45.0 * r / rows, z1 = -5 + 45.0 * (r + 1) / rows;
                const V3 p0{R * std::cos(a0), R * std::sin(a0), z0}, p1{R * std::cos(a1), R * std::sin(a1), z0};
                Builder::quad(p.own, p1, p0, V3{p0.x, p0.y, z1}, V3{p1.x, p1.y, z1}, kWhite, (k + 1.0) / n * 1.4, k * 1.4 / n, (40 - z1) / 45, (40 - z0) / 45);
            }
        // and the sky overhead, its colour at the drum's top
        for (int k = 0; k < n; ++k) {
            const double a0 = 2 * kPi * k / n, a1 = 2 * kPi * (k + 1) / n;
            p.own.push_back({{0, 0, 40}, {0, 0, -1}, .5, .004, kWhite});
            p.own.push_back({{R * std::cos(a1), R * std::sin(a1), 40}, {0, 0, -1}, .5, .004, kWhite});
            p.own.push_back({{R * std::cos(a0), R * std::sin(a0), 40}, {0, 0, -1}, .5, .004, kWhite});
        }
    }
    // the floor: stone flags, cut into small cells so the texture keeps its perspective
    {
        Piece& p = B.own(kWhite, 0, false);
        p.tex = &T.sand;
        for (int y = -9; y < 13; ++y)
            for (int x = -13; x < 13; ++x) {
                if (std::hypot(x + .5, y + .5) > kDomeR + .8) continue;
                Builder::quad(p.own, {double(x), double(y), 0}, {x + 1.0, double(y), 0}, {x + 1.0, y + 1.0, 0}, {double(x), y + 1.0, 0}, kWhite,
                              x / 5.0, (x + 1) / 5.0, (y + 1) / 5.0, y / 5.0);
            }
    }
    // the wall: a brick plinth, then glass to the eaves; and the dome's glass
    {
        Piece& brick = B.own(kWhite, 0, false);
        brick.tex = &T.brick;
        for (int k = 0; k < bays; ++k) {
            const double t0 = k * step, t1 = (k + 1) * step;
            if (behind_player((t0 + t1) / 2)) continue;
            Builder::quad(brick.own, wall_at(t1, 0), wall_at(t0, 0), wall_at(t0, .9), wall_at(t1, .9), kWhite, 0, 1.5, 0, 1.6);
        }
    }
    {
        Piece& glass = B.own(kWhite, static_cast<std::uint16_t>(translucent | unlit | no_depth_write | double_sided), false);
        for (int k = 0; k < bays; ++k) {
            const double t0 = k * step, t1 = (k + 1) * step;
            if (!behind_player((t0 + t1) / 2))
                for (int r = 0; r < 4; ++r) {
                    const double z0 = .9 + (kWallH - .9) * r / 4, z1 = .9 + (kWallH - .9) * (r + 1) / 4;
                    const float k2 = static_cast<float>(.75 + .5 * h01(static_cast<std::uint32_t>(k * 13 + r)));
                    const Col c{L.glass.r * k2, L.glass.g * k2, L.glass.b, L.glass.a * (r == 0 ? 1.8f : 1)};
                    Builder::quad(glass.own, wall_at(t1, z0), wall_at(t0, z0), wall_at(t0, z1), wall_at(t1, z1), c);
                }
            for (int r = 0; r < 6; ++r) {
                const double p0 = r * kPi / 2 / 6, p1 = (r + 1) * kPi / 2 / 6;
                for (int h = 0; h < 2; ++h) {
                    const double a = t0 + step * h / 2, b = t0 + step * (h + 1) / 2;
                    const float k2 = static_cast<float>(.8 + .45 * h01(static_cast<std::uint32_t>(k * 31 + r * 7 + h)));
                    const Col c{L.glass.r * k2, L.glass.g * k2, L.glass.b, L.glass.a};
                    Builder::quad(glass.own, dome_at(b, p0), dome_at(a, p0), dome_at(a, p1), dome_at(b, p1), c);
                }
            }
        }
    }
    // the ironwork: ribs up the wall and over the dome, glazing bars, rings, scroll brackets
    const Col iron = L.iron;
    for (int k = 0; k < bays; ++k) {
        const double th = k * step;
        if (!behind_player(th)) {
            B.rod(wall_at(th, .9), wall_at(th, kWallH), .07, iron, nullptr, false, 8);
            B.rod(wall_at(th + step / 2, .9), wall_at(th + step / 2, kWallH), .025, iron, nullptr, false, 5);
            B.mesh(torus_mesh(14, 4, .16), at(wall_at(th, kWallH - .45, kDomeR - .35)) * M34::rot_z(th + kPi / 2) * M34::rot_x(kPi / 2) * sc(.35, .35, .35), iron);
        }
        const int segs = 10;
        for (int s = 0; s < segs; ++s) B.rod(dome_at(th, s * kPi / 2 / segs), dome_at(th, (s + 1) * kPi / 2 / segs), .065, iron, nullptr, false, 7);
        for (int s = 0; s < segs; ++s) B.rod(dome_at(th + step / 2, s * kPi / 2 / segs), dome_at(th + step / 2, (s + 1) * kPi / 2 / segs), .022, iron, nullptr, false, 4);
    }
    for (int k = 0; k < bays; ++k) {
        const double t0 = k * step, t1 = (k + 1) * step;
        if (!behind_player((t0 + t1) / 2))
            for (double z : {.9, 3.2, kWallH}) B.rod(wall_at(t0, z), wall_at(t1, z), z == kWallH ? .09 : .04, iron, nullptr, false, 6);
        for (int r = 1; r <= 5; ++r) B.rod(dome_at(t0, r * kPi / 12), dome_at(t1, r * kPi / 12), .035, iron, nullptr, false, 5);
    }
    // the gallery round the far wall: a deck on brackets, balusters, and the rail
    {
        const double z = kGalleryZ, rr = kDomeR - 1.25, rail = z + .95;
        Piece& deck = B.own(kWhite, 0, false);
        for (int k = 0; k < 40; ++k) {
            const double a = .25 + (kPi - .5) * k / 40, b = .25 + (kPi - .5) * (k + 1) / 40;
            Builder::quad(deck.own, wall_at(b, z, kDomeR - .05), wall_at(a, z, kDomeR - .05), wall_at(a, z, rr - .05), wall_at(b, z, rr - .05), hex(0x7A6650));
            Builder::quad(deck.own, wall_at(a, z - .14, rr - .05), wall_at(b, z - .14, rr - .05), wall_at(b, z, rr - .05), wall_at(a, z, rr - .05), L.iron_dark);
            B.rod(wall_at(a, rail, rr), wall_at(b, rail, rr), .045, iron, nullptr, false, 6);
            B.rod(wall_at(a, z + .12, rr), wall_at(b, z + .12, rr), .025, iron, nullptr, false, 5);
            for (int q = 0; q < 3; ++q) {
                const double t = a + (b - a) * q / 3;
                B.rod(wall_at(t, z, rr), wall_at(t, rail, rr), .014, iron, nullptr, false, 4);
            }
            if (k % 4 == 0) B.rod(wall_at(a, z - 1.1, kDomeR - .1), wall_at(a, z - .1, rr + .1), .05, iron, nullptr, false, 6);
        }
    }
}

// A palm: a ringed trunk leaning from its foot to the crown, and a crown of fronds.
void build_palm(Builder& B, const Canopy& c, const Textures& T, std::uint32_t seed) {
    const int segs = 16;
    auto pt = [&](double u) { return c.foot + (c.crown - c.foot) * u + V3{.4 * std::sin(u * 2.8), .2 * std::sin(u * 2.1), 0} * (1 - u); };
    for (int s = 0; s < segs; ++s) {
        const double u0 = static_cast<double>(s) / segs, u1 = static_cast<double>(s + 1) / segs;
        B.rod(pt(u0), pt(u1), .3 - .1 * u0, kWhite, &T.palm, true, 10, .7);
    }
    const int fronds = 22;
    for (int f = 0; f < fronds; ++f) {
        const double az = 2 * kPi * f / fronds + .35 * h01(seed + f);
        const V3 dir{std::cos(az), std::sin(az), 0};
        const double length = c.radius * (.9 + .25 * h01(seed + 40 + f)), lift = .3 + .6 * h01(seed + 80 + f);
        auto rach = [&](double u) { return c.crown + dir * (length * u) + V3{0, 0, lift * u * 2.2 - (lift + 1.6) * u * u}; };
        const V3 side = norm(cross(dir, {0, 0, 1}));
        Piece& p = B.own(kWhite, static_cast<std::uint16_t>(cutout | double_sided), true);
        p.tex = &T.frond;
        const Col tint = lerp(kWhite, hex(0xC8D890), h01(seed + 120 + f) * .5);
        const double w = .55 * length / 3, droop = .18 * w;
        const int st = 10;
        for (int k = 0; k < st; ++k) {
            const double u0 = static_cast<double>(k) / st, u1 = static_cast<double>(k + 1) / st;
            const V3 a = rach(u0), b = rach(u1);
            Builder::quad(p.own, a - side * w - V3{0, 0, droop}, a, b, b - side * w - V3{0, 0, droop}, tint, 0, .5, u1, u0);
            Builder::quad(p.own, a, a + side * w - V3{0, 0, droop}, b + side * w - V3{0, 0, droop}, b, tint, .5, 1, u1, u0);
        }
    }
}

// A broadleaf jungle tree: a buttressed trunk, limbs, and a crown of leaf sprays that
// fills its canopy's disc, with lianas hanging from it.
void build_broadleaf(Builder& B, const Canopy& c, const Textures& T, std::uint32_t seed) {
    const int segs = 12;
    auto pt = [&](double u) { return c.foot + (c.crown - c.foot) * u + V3{.3 * std::sin(u * 3.1 + seed % 5), .2 * std::cos(u * 2.3), 0} * (1 - u); };
    for (int s = 0; s < segs; ++s) {
        const double u0 = static_cast<double>(s) / segs, u1 = static_cast<double>(s + 1) / segs;
        B.rod(pt(u0), pt(u1) + V3{0, 0, .03}, .42 - .2 * u0, kWhite, &T.bark, true, 10, .5);
    }
    for (int k = 0; k < 5; ++k) {  // buttress roots
        const double a = 2 * kPi * k / 5 + h01(seed + k);
        B.rod(pt(.12), c.foot + V3{std::cos(a) * 1.3, std::sin(a) * 1.3, 0}, .16, kWhite, &T.bark, true, 7);
    }
    // limbs out into the crown
    for (int k = 0; k < 6; ++k) {
        const double a = 2 * kPi * k / 6 + h01(seed + 20 + k), r = c.radius * (.45 + .3 * h01(seed + 30 + k));
        const V3 from = pt(.65 + .05 * k), to = c.crown + V3{std::cos(a) * r, std::sin(a) * r, -.6 + h01(seed + 40 + k)};
        B.rod(from, to, .14, kWhite, &T.bark, true, 7);
    }
    // the crown: leaf sprays as crossed cards, through an flattened ball over the disc
    Piece& p = B.own(kWhite, static_cast<std::uint16_t>(cutout | double_sided), true);
    p.tex = &T.leaves;
    const int n = static_cast<int>(28 * c.radius * c.radius / 9);
    for (int k = 0; k < n; ++k) {
        const double a = 2 * kPi * h01(seed + 100 + k), rr = c.radius * std::sqrt(h01(seed + 200 + k));
        const double z = c.crown.z + (h01(seed + 300 + k) - .35) * 1.8 * (1.1 - rr / c.radius);
        const V3 m = c.crown + V3{std::cos(a) * rr, std::sin(a) * rr, z - c.crown.z};
        const double size = .9 + .6 * h01(seed + 400 + k), turn = kPi * h01(seed + 500 + k);
        const Col tint = lerp(kWhite, hex(0xB8C890), h01(seed + 600 + k) * .6);
        for (int q = 0; q < 2; ++q) {
            const double t = turn + q * kPi / 2;
            const V3 d{std::cos(t) * size, std::sin(t) * size, 0}, up{0, 0, size * .8};
            Builder::quad(p.own, m - d - up, m + d - up, m + d + up, m - d + up, tint);
        }
        Builder::quad(p.own, m + V3{-size, -size, 0}, m + V3{size, -size, 0}, m + V3{size, size, 0}, m + V3{-size, size, 0}, tint);  // a flat one, to shade below
    }
    // lianas hanging from the crown's edge
    Piece& v = B.own(kWhite, static_cast<std::uint16_t>(cutout | double_sided), false);
    v.tex = &T.vine;
    for (int k = 0; k < 7; ++k) {
        const double a = 2 * kPi * h01(seed + 700 + k), rr = c.radius * (.5 + .45 * h01(seed + 710 + k));
        const V3 top = c.crown + V3{std::cos(a) * rr, std::sin(a) * rr, -.6};
        const double drop = 2.5 + (top.z - 3) * .6 * h01(seed + 720 + k);
        const V3 side{-std::sin(a) * .22, std::cos(a) * .22, 0};
        for (int q = 0; q < 6; ++q) {
            const double u0 = q / 6.0, u1 = (q + 1) / 6.0;
            const V3 p0 = top + V3{.2 * std::sin(u0 * 5 + k), 0, -drop * u0}, p1 = top + V3{.2 * std::sin(u1 * 5 + k), 0, -drop * u1};
            Builder::quad(v.own, p1 - side, p1 + side, p0 + side, p0 - side, kWhite, 0, 1, u1 * drop / 1.2, u0 * drop / 1.2);
        }
    }
}

// Lianas looping between two points: a sagging stem with leaves along it.
void build_liana(Builder& B, V3 a, V3 b, double sag, const Textures& T) {
    Piece& v = B.own(kWhite, static_cast<std::uint16_t>(cutout | double_sided), false);
    v.tex = &T.vine;
    const V3 side = norm(cross(b - a, {0, 0, 1})) * .2;
    const int n = 12;
    const double length = len(b - a);
    for (int q = 0; q < n; ++q) {
        const double u0 = static_cast<double>(q) / n, u1 = static_cast<double>(q + 1) / n;
        const V3 p0 = a + (b - a) * u0 - V3{0, 0, sag * 4 * u0 * (1 - u0)}, p1 = a + (b - a) * u1 - V3{0, 0, sag * 4 * u1 * (1 - u1)};
        Builder::quad(v.own, p0 - side + V3{0, 0, .1}, p1 - side + V3{0, 0, .1}, p1 + side - V3{0, 0, .1}, p0 + side - V3{0, 0, .1}, kWhite, 0, 1, u1 * length / 1.2, u0 * length / 1.2);
    }
}

// The bare perch tree: a weathered trunk, and a branch out to each bar.
void build_tree(Builder& B, const Stand& S, const std::vector<const Bar*>& bars, const Textures& T, std::uint32_t seed) {
    const double H = S.reach;
    auto trunk = [&](double z) { return S.base + V3{.18 * std::sin(z * .9 + seed % 7), .12 * std::cos(z * .7), z}; };
    for (int k = 0; k < 14; ++k) {
        const double z0 = H * k / 14, z1 = H * (k + 1) / 14;
        B.rod(trunk(z0), trunk(z1) + V3{0, 0, .03}, .26 - .17 * z0 / H, kWhite, &T.bark, true, 10, .8);
    }
    for (int k = 0; k < 5; ++k) {  // roots spreading on the floor
        const double a = 2 * kPi * k / 5 + h01(seed + k);
        B.rod(trunk(.6), S.base + V3{std::cos(a) * .9, std::sin(a) * .9, 0}, .1, kWhite, &T.bark, true, 7);
    }
    for (const Bar* b : bars) {
        B.rod(trunk(b->a.z - .35), b->a, .1, kWhite, &T.bark, true, 8);
        B.rod(b->a - (b->b - b->a) * .05, b->b + (b->b - b->a) * .12, .075, kWhite, &T.bark, true, 8, 1.2);
        const V3 e = b->b + (b->b - b->a) * .12;  // a twig off the end
        B.rod(e, e + V3{0, 0, .35} + norm(b->b - b->a) * .25, .03, kWhite, &T.bark, false, 5);
    }
    for (int k = 0; k < 4; ++k) {  // bare branches that hold no places, for the tree's shape
        const double z = H * (.45 + .14 * k), a = 2 * kPi * h01(seed + 30 + k);
        const V3 from = trunk(z), to = from + V3{std::cos(a), std::sin(a), .7};
        B.rod(from, to, .06, kWhite, &T.bark, true, 6);
        B.rod(to, to + V3{std::cos(a + .6) * .5, std::sin(a + .6) * .5, .5}, .03, kWhite, &T.bark, false, 5);
    }
}

// A clump of banana leaves rising from the soil.
void build_banana(Builder& B, V3 base, double size, const Textures& T, std::uint32_t seed) {
    const int n = 6 + static_cast<int>(h01(seed) * 4);
    for (int f = 0; f < n; ++f) {
        const double az = 2 * kPi * f / n + .5 * h01(seed + f), length = (1.6 + .8 * h01(seed + 20 + f)) * size;
        const V3 dir{std::cos(az), std::sin(az), 0};
        const V3 side = norm(cross(dir, {0, 0, 1}));
        const V3 root = base + V3{0, 0, (.6 + .6 * h01(seed + 40 + f)) * size};
        B.rod(base, root, .05 * size, hex(0x7A8A4A), nullptr, false, 5);
        Piece& p = B.own(kWhite, static_cast<std::uint16_t>(cutout | double_sided), false);
        p.tex = &T.banana;
        auto mid = [&](double u) { return root + dir * (length * .7 * u) + V3{0, 0, length * (.8 * u - .75 * u * u)}; };
        const double w = .32 * length;
        for (int s = 0; s < 8; ++s) {
            const double u0 = s / 8.0, u1 = (s + 1) / 8.0;
            const V3 a = mid(u0), b = mid(u1), sag{0, 0, -.06 * size};
            Builder::quad(p.own, a - side * w + sag, a + side * w + sag, b + side * w + sag, b - side * w + sag, kWhite, 0, 1, u1, u0);
        }
    }
}

// A tree fern: a dark shaggy stem and a rosette of fronds.
void build_tree_fern(Builder& B, V3 base, double h, const Textures& T, std::uint32_t seed) {
    B.rod(base, base + V3{0, 0, h}, .18, hex(0x6A5040), &T.bark, false, 8);
    Piece& p = B.own(kWhite, static_cast<std::uint16_t>(cutout | double_sided), false);
    p.tex = &T.fern;
    const V3 top = base + V3{0, 0, h};
    for (int f = 0; f < 14; ++f) {
        const double az = 2 * kPi * f / 14 + .3 * h01(seed + f), length = 1.6 + .6 * h01(seed + 10 + f);
        const V3 dir{std::cos(az), std::sin(az), 0};
        const V3 side = norm(cross(dir, {0, 0, 1}));
        auto rr = [&](double u) { return top + dir * (length * u) + V3{0, 0, .7 * u - 1.3 * u * u}; };
        for (int s = 0; s < 6; ++s) {
            const V3 a = rr(s / 6.0), b = rr((s + 1) / 6.0);
            Builder::quad(p.own, a - side * .3, a + side * .3, b + side * .3, b - side * .3, kWhite, 0, 1, (s + 1) / 6.0, s / 6.0);
        }
    }
}

// Low planting on the sand: fern rosettes, grass tufts, small broad leaves, bromeliads.
void build_ground_cover(Builder& B, V3 p, int kind, double size, const Textures& T, std::uint32_t seed) {
    switch (kind) {
    case 0: {  // a fern rosette, fronds arching out low
        Piece& f = B.own(kWhite, static_cast<std::uint16_t>(cutout | double_sided), false);
        f.tex = &T.fern;
        const int n = 7 + static_cast<int>(h01(seed) * 5);
        for (int k = 0; k < n; ++k) {
            const double az = 2 * kPi * k / n + .5 * h01(seed + k), length = size * (.7 + .4 * h01(seed + 10 + k));
            const V3 dir{std::cos(az), std::sin(az), 0}, side = norm(cross(dir, {0, 0, 1}));
            auto at_u = [&](double u) { return p + dir * (length * u) + V3{0, 0, length * (.9 * u - .8 * u * u)}; };
            for (int q = 0; q < 4; ++q) {
                const V3 a = at_u(q / 4.0), b = at_u((q + 1) / 4.0);
                const double w = .16 * size;
                Builder::quad(f.own, a - side * w, a + side * w, b + side * w, b - side * w, lerp(kWhite, hex(0xD8E8A0), h01(seed + 30 + k) * .6), 0, 1, (q + 1) / 4.0, q / 4.0);
            }
        }
        break;
    }
    case 1: {  // a tuft of grass blades
        Piece& g = B.own(kWhite, static_cast<std::uint16_t>(double_sided), false);
        const int n = 14 + static_cast<int>(h01(seed) * 10);
        for (int k = 0; k < n; ++k) {
            const double az = 2 * kPi * h01(seed + k), lean = .2 + .5 * h01(seed + 40 + k), h = size * (.35 + .4 * h01(seed + 80 + k));
            const V3 base = p + V3{(h01(seed + 120 + k) - .5) * .2 * size, (h01(seed + 160 + k) - .5) * .2 * size, 0};
            const V3 dir{std::cos(az), std::sin(az), 0}, side = norm(cross(dir, {0, 0, 1})) * (.018 * size);
            const V3 tip = base + dir * (h * lean) + V3{0, 0, h};
            const Col c = lerp(hex(0x5A8A38), hex(0xB8B060), h01(seed + 200 + k) * .7);
            g.own.push_back({base - side, {0, -.3, 1}, 0, 0, scale(c, .6)});
            g.own.push_back({base + side, {0, -.3, 1}, 0, 0, scale(c, .6)});
            g.own.push_back({tip, {0, -.3, 1}, 0, 0, c});
        }
        break;
    }
    case 2: {  // small broad leaves, like a hosta
        Piece& l = B.own(kWhite, static_cast<std::uint16_t>(cutout | double_sided), false);
        l.tex = &T.banana;
        const int n = 5 + static_cast<int>(h01(seed) * 4);
        for (int k = 0; k < n; ++k) {
            const double az = 2 * kPi * k / n + .6 * h01(seed + k), length = size * (.45 + .25 * h01(seed + 10 + k));
            const V3 dir{std::cos(az), std::sin(az), 0}, side = norm(cross(dir, {0, 0, 1}));
            auto at_u = [&](double u) { return p + dir * (length * (.2 + .8 * u)) + V3{0, 0, length * (.7 * u - .5 * u * u) + .05}; };
            for (int q = 0; q < 3; ++q) {
                const V3 a = at_u(q / 3.0), b = at_u((q + 1) / 3.0);
                const double w = .28 * length;
                Builder::quad(l.own, a - side * w, a + side * w, b + side * w, b - side * w, lerp(kWhite, hex(0x88B070), h01(seed + 20) * .8), 0, 1, (q + 1) / 3.0, q / 3.0);
            }
        }
        break;
    }
    default: {  // a bromeliad: stiff strap leaves round a red heart
        Piece& l = B.own(kWhite, static_cast<std::uint16_t>(double_sided), false);
        const int n = 10;
        for (int k = 0; k < n; ++k) {
            const double az = 2 * kPi * k / n + .3 * h01(seed + k), length = size * (.45 + .2 * h01(seed + 10 + k));
            const V3 dir{std::cos(az), std::sin(az), 0}, side = norm(cross(dir, {0, 0, 1})) * (.06 * size);
            const V3 tip = p + dir * length + V3{0, 0, length * .55};
            const Col c = lerp(hex(0x3A7A3A), hex(0x7AA050), h01(seed + 30 + k));
            Builder::tri(l.own, p - side, p + side, tip, c, {0, -.3, 1});
        }
        B.mesh(sphere_mesh(8, 5), at(p + V3{0, 0, .12 * size}) * sc(.1 * size, .1 * size, .14 * size), h01(seed + 50) < .5 ? hex(0xD8283A) : hex(0xF07820));
        break;
    }
    }
}

// An orchid on a trunk: a few strap leaves and an arching spray of flowers, petals of one
// colour and a lip of another.
void build_orchid(Builder& B, V3 p, V3 out, Col petal, Col lip, std::uint32_t seed) {
    Piece& g = B.own(kWhite, static_cast<std::uint16_t>(double_sided), false);
    const V3 side = norm(cross(out, {0, 0, 1}));
    for (int k = 0; k < 4; ++k) {  // leaves
        const double a = -.6 + .4 * k;
        const V3 d = norm(out + side * a + V3{0, 0, -.3});
        Builder::tri(g.own, p - side * .05, p + side * .05, p + d * .45, hex(0x3A7A34), {0, -.4, 1});
        Builder::tri(g.own, p + side * .05, p - side * .05, p + d * .45 + V3{0, 0, .03}, hex(0x2E6A2C), {0, -.4, 1});
    }
    auto stem = [&](double u) { return p + out * (.7 * u) + V3{0, 0, .55 * u - .5 * u * u} + side * (.15 * std::sin(u * 3 + seed % 5)); };
    for (int k = 0; k < 6; ++k) B.rod(stem(k / 6.0), stem((k + 1) / 6.0), .008, hex(0x4A6A30), nullptr, false, 3);
    Piece& f = B.own(kWhite, static_cast<std::uint16_t>(double_sided), false);
    const int n = 5 + static_cast<int>(h01(seed) * 4);
    for (int k = 0; k < n; ++k) {
        const V3 c = stem(.35 + .65 * k / n) + V3{0, 0, -.02};
        const double r = .07;
        for (int q = 0; q < 5; ++q) {
            const double a = 2 * kPi * q / 5 + h01(seed + k) * .5;
            const V3 dir = side * std::cos(a) + V3{0, 0, std::sin(a)}, perp = side * -std::sin(a) + V3{0, 0, std::cos(a)};
            const V3 m = c + dir * (r * .5) + V3{0, -.01, 0};
            Builder::tri(f.own, c, m + perp * (r * .3), c + dir * r, petal, {0, -1, 0});
            Builder::tri(f.own, c, c + dir * r, m - perp * (r * .3), petal, {0, -1, 0});
        }
        B.mesh(sphere_mesh(6, 4), at(c + V3{0, -.03, -.02}) * sc(.03, .025, .03), lip);
    }
}

// A banana bunch hanging from a limb: a stalk and hands of curved fingers.
void build_banana_bunch(Builder& B, V3 top, std::uint32_t seed) {
    B.rod(top, top - V3{0, 0, .9}, .025, hex(0x6A6A30), nullptr, false, 5);
    for (int h = 0; h < 4; ++h)
        for (int k = 0; k < 7; ++k) {
            const double a = 2 * kPi * k / 7 + h * .4, z = -.25 - h * .14;
            const V3 base = top + V3{std::cos(a) * .06, std::sin(a) * .06, z};
            const V3 tip = base + V3{std::cos(a) * .16, std::sin(a) * .16, .12};
            B.rod(base, tip, .028, lerp(hex(0xC8D040), hex(0x88B030), h01(seed + h * 7 + k)), nullptr, false, 5);
        }
    B.mesh(cone_mesh(8), at(top - V3{0, 0, .9}) * M34::rot_x(kPi) * sc(.06, .06, .22), hex(0x7A2A48));  // the flower's purple bract
}

// A feeding table: a tray on a post, laid with fruit; husks scattered round its foot.
void build_feeder(Builder& B, V3 c, const Textures& T, std::uint32_t seed) {
    const double h = 1.15;
    B.rod(c, c + V3{0, 0, h}, .06, kWhite, &T.bark, true, 7);
    B.mesh(box_mesh(), at(c + V3{0, 0, h}) * sc(.42, .32, .05), hex(0x7A5A3C), nullptr, true);
    for (int s = -1; s <= 1; s += 2) {
        B.mesh(box_mesh(), at(c + V3{s * .42, 0, h}) * sc(.02, .32, .1), hex(0x6A4A30));
        B.mesh(box_mesh(), at(c + V3{0, s * .32, h}) * sc(.42, .02, .1), hex(0x6A4A30));
    }
    const V3 tray = c + V3{0, 0, h + .05};
    B.mesh(sphere_mesh(10, 7), at(tray + V3{-.2, .05, .07}) * M34::rot_z(.5) * sc(.13, .09, .08), hex(0xF0A030));  // a mango
    B.mesh(sphere_mesh(8, 5), at(tray + V3{-.22, .05, .1}) * sc(.07, .05, .03), hex(0xE8C040));
    for (int k = 0; k < 2; ++k) {  // orange halves, cut side up
        const V3 o = tray + V3{.1 + k * .17, -.12 + k * .08, 0};
        B.mesh(hemisphere_mesh(10, 4), at(o + V3{0, 0, .07}) * M34::rot_x(kPi) * sc(.08, .08, .07), hex(0xF08A20));
        B.mesh(disc_mesh(10), at(o + V3{0, 0, .071}) * sc(.075, .075, 1), hex(0xF8C060));
    }
    for (int k = 0; k < 14; ++k)  // grapes
        B.mesh(sphere_mesh(5, 3), at(tray + V3{.12 + .1 * h01(seed + k), .14 + .08 * h01(seed + 20 + k), .03 + .05 * h01(seed + 40 + k)}) * sc(.025, .025, .025), hex(0x6A2A5A));
    Piece& husk = B.own(kWhite, 0, false);
    for (int k = 0; k < 120; ++k) {
        const double a = 2 * kPi * h01(seed + 100 + k), r = .9 * std::sqrt(h01(seed + 200 + k));
        const V3 q = c + V3{std::cos(a) * r, std::sin(a) * r, .008};
        const double t = 2 * kPi * h01(seed + 300 + k);
        const V3 d{std::cos(t) * .018, std::sin(t) * .018, 0}, e{-std::sin(t) * .008, std::cos(t) * .008, 0};
        Builder::tri(husk.own, q - d, q + e, q + d, h01(seed + 400 + k) < .5 ? hex(0x4A3A2A) : hex(0xC8B890), {0, 0, 1});
    }
}

// A dropped feather lying on the sand.
void build_feather(Builder& B, V3 p, Col c, double turn, double size) {
    Piece& f = B.own(kWhite, static_cast<std::uint16_t>(double_sided), false);
    const V3 d{std::cos(turn), std::sin(turn), 0}, e{-std::sin(turn), std::cos(turn), 0};
    const V3 base = p - d * (.12 * size), tip = p + d * (.12 * size), mid = p + d * (.03 * size), z{0, 0, .01};
    Builder::tri(f.own, base + z, mid + e * (.035 * size) + z, tip + z, c, {0, 0, 1});
    Builder::tri(f.own, base + z, tip + z, mid - e * (.03 * size) + z, scale(c, .85), {0, 0, 1});
    B.rod(base - d * (.04 * size) + z, tip + z * 1.2, .003, hex(0xE8E0D0), nullptr, false, 3);
}

// A butterfly resting with its wings half open.
void build_butterfly(Builder& B, V3 p, Col wing, double turn) {
    Piece& b = B.own(kWhite, static_cast<std::uint16_t>(double_sided | unlit), false);
    const V3 d{std::cos(turn), std::sin(turn), 0}, e{-std::sin(turn), std::cos(turn), 0};
    for (int s = -1; s <= 1; s += 2) {
        const V3 out = e * (s * .07) + V3{0, 0, .05};
        Builder::tri(b.own, p - d * .01, p + d * .05 + out, p + out * 1.1 - d * .01, wing, {0, -1, 0});
        Builder::tri(b.own, p - d * .01, p + out * .9 - d * .02, p - d * .05 + out * .6, scale(wing, .8), {0, -1, 0});
    }
    B.rod(p - d * .03, p + d * .03, .006, hex(0x201810), nullptr, false, 3);
}

// A gardener's galvanised watering can and a trowel left by a bed.
void build_watering_can(Builder& B, V3 p, double turn) {
    const Col zinc = hex(0x9AA0A0), wood = hex(0x8A5A30);
    const M34 m = at(p) * M34::rot_z(turn);
    B.mesh(cylinder_mesh(14), m * sc(.17, .17, .3), zinc, nullptr, true, gloss);
    B.mesh(disc_mesh(14), m * at({0, 0, .3}) * sc(.17, .17, 1), scale(zinc, .8), nullptr, false, gloss);
    B.mesh(torus_mesh(14, 4, .1), m * at({0, 0, .3}) * sc(.17, .17, .17), zinc, nullptr, false, gloss);
    B.mesh(torus_mesh(14, 4, .12), m * at({0, 0, .32}) * M34::rot_x(kPi / 2) * sc(.14, .14, .14), zinc, nullptr, false, gloss);
    const V3 a = m.apply({.14, 0, .06}), b = m.apply({.42, 0, .32});
    B.rod(a, b, .022, zinc, nullptr, false, 6);
    B.mesh(cone_mesh(10), along(b, m.apply({.47, 0, .36}), 1) * sc(.05, .05, 1), zinc);
    // the trowel, lying beside it
    const V3 t0 = p + V3{-.32, -.18, .02}, t1 = t0 + V3{std::cos(turn + .8) * .16, std::sin(turn + .8) * .16, 0};
    B.rod(t0, t1, .02, wood, nullptr, false, 6);
    B.mesh(sphere_mesh(8, 4), at(t1 + V3{std::cos(turn + .8) * .12, std::sin(turn + .8) * .12, 0}) * M34::rot_z(turn + .8) * sc(.12, .06, .012), hex(0x8A8E90), nullptr, false, gloss);
}

void build_stands(Builder& B, const Layout& Ly, const Textures& T, const AviaryLook& L, std::uint32_t seed) {
    const Col brass = hex(0xC8A048);
    for (size_t si = 0; si < Ly.stands.size(); ++si) {
        const Stand& S = Ly.stands[si];
        std::vector<const Bar*> bars;
        for (const Bar& b : Ly.bars)
            if (b.stand == static_cast<int>(si)) bars.push_back(&b);
        switch (S.kind) {
        case StandKind::tree: build_tree(B, S, bars, T, seed + 7); break;
        case StandKind::bamboo:
            for (const Bar* b : bars) {
                const V3 top = (b->a + b->b) * .5;
                for (int k = 0; k < 6; ++k) {
                    const V3 p0 = S.base + (top - S.base) * (k / 6.0), p1 = S.base + (top - S.base) * ((k + 1) / 6.0);
                    B.rod(p0, p1, .07, kWhite, &T.bamboo, true, 8);
                    B.mesh(torus_mesh(10, 4, .2), at(p1) * sc(.075, .075, .075), hex(0x8A8A40));
                }
                B.rod(b->a - (b->b - b->a) * .08, b->b + (b->b - b->a) * .08, .055, kWhite, &T.bamboo, true, 8, .5);
                B.rod(top - V3{0, 0, .6}, top + (b->a - top) * .6, .022, hex(0x8A7048), nullptr, false, 4);  // lashed struts
                B.rod(top - V3{0, 0, .6}, top + (b->b - top) * .6, .022, hex(0x8A7048), nullptr, false, 4);
            }
            break;
        case StandKind::lamp:
            for (const Bar* b : bars) {
                const V3 top = (b->a + b->b) * .5;
                const Col iron = hex(0x2A2A28);
                B.mesh(cylinder_mesh(12), at(S.base) * sc(.22, .22, .3), iron, nullptr, true);
                B.mesh(cone_mesh(12), at(S.base + V3{0, 0, .3}) * sc(.16, .16, .5), iron, nullptr, true);
                B.rod(S.base, top + V3{0, 0, .25}, .06, iron, nullptr, true, 8);
                for (int k = 0; k < 3; ++k) B.mesh(torus_mesh(12, 4, .3), at(S.base + V3{0, 0, .9 + k * .9}) * sc(.09, .09, .09), iron);
                B.rod(b->a - (b->b - b->a) * .12, b->b + (b->b - b->a) * .12, .04, iron, nullptr, true, 7);
                for (V3 e : {b->a - (b->b - b->a) * .12, b->b + (b->b - b->a) * .12}) {
                    // a lantern hanging from each end of the arm, a scroll above it
                    B.mesh(torus_mesh(12, 4, .2), at(e + V3{0, 0, .12}) * M34::rot_x(kPi / 2) * sc(.12, .12, .12), iron);
                    B.rod(e, e - V3{0, 0, .2}, .012, iron, nullptr, false, 4);
                    B.mesh(box_mesh(), at(e - V3{0, 0, .62}) * sc(.12, .12, .42), hex(0xF8E8B0, .7f), nullptr, false, static_cast<std::uint16_t>(translucent | unlit));
                    B.mesh(cone_mesh(8), at(e - V3{0, 0, .2}) * M34::rot_x(kPi) * sc(.16, .16, .0) * sc(1, 1, 1), iron);
                    B.mesh(cone_mesh(8), at(e - V3{0, 0, .22}) * sc(.16, .16, .12), iron);
                }
            }
            break;
        case StandKind::swing:
            for (const Bar* b : bars) {
                for (V3 e : {b->a + V3{.04, 0, 0}, b->b - V3{.04, 0, 0}}) B.rod(e, {e.x, e.y, roof_over(e.x, e.y) - .1}, .02, kWhite, &T.rope, false, 5, 3);
                B.rod(b->a - V3{.08, 0, 0}, b->b + V3{.08, 0, 0}, .05, kWhite, &T.rope, true, 8, 3);
            }
            break;
        case StandKind::hoop:
            for (const Bar* b : bars) {
                const V3 c = (b->a + b->b) * .5 + V3{0, 0, .38};
                B.mesh(torus_mesh(28, 6, .09), at(c) * M34::rot_z(.3) * M34::rot_x(kPi / 2) * sc(.38, .38, .38), brass, nullptr, true, gloss);
                B.rod(c + V3{0, 0, .38}, {c.x, c.y, roof_over(c.x, c.y) - .1}, .015, L.iron_dark, nullptr, false, 4);
            }
            break;
        case StandKind::fountain: {
            const V3 c = S.base;
            Piece& rim = B.own(kWhite, 0, true);
            rim.tex = &T.stone;
            const int n = 32;
            auto ring = [&](double a, double r, double z) { return c + V3{r * std::cos(a), r * std::sin(a), z}; };
            for (int k = 0; k < n; ++k) {
                const double a0 = 2 * kPi * k / n, a1 = 2 * kPi * (k + 1) / n;
                Builder::quad(rim.own, ring(a0, 1.5, 0), ring(a1, 1.5, 0), ring(a1, 1.5, .7), ring(a0, 1.5, .7), kWhite, k / 8.0, (k + 1) / 8.0, .5, 0);
                Builder::quad(rim.own, ring(a0, 1.5, .7), ring(a1, 1.5, .7), ring(a1, 1.25, .7), ring(a0, 1.25, .7), kWhite, k / 8.0, (k + 1) / 8.0, .6, .5);
                Builder::quad(rim.own, ring(a1, 1.25, .3), ring(a0, 1.25, .3), ring(a0, 1.25, .7), ring(a1, 1.25, .7), kWhite, k / 8.0, (k + 1) / 8.0, .8, .6);
            }
            Piece& w = B.own(kWhite, static_cast<std::uint16_t>(translucent | gloss), false);
            w.tex = &T.water;
            for (int k = 0; k < n; ++k) {
                const double a0 = 2 * kPi * k / n, a1 = 2 * kPi * (k + 1) / n;
                w.own.push_back({c + V3{0, 0, .55}, {0, 0, 1}, .5, .5, kWhite});
                w.own.push_back({ring(a0, 1.25, .55), {0, 0, 1}, .5 + .5 * std::cos(a0), .5 + .5 * std::sin(a0), kWhite});
                w.own.push_back({ring(a1, 1.25, .55), {0, 0, 1}, .5 + .5 * std::cos(a1), .5 + .5 * std::sin(a1), kWhite});
            }
            B.rod(c, c + V3{0, 0, 1.6}, .2, kWhite, &T.stone, true, 10);
            B.mesh(hemisphere_mesh(16, 5), at(c + V3{0, 0, 1.85}) * M34::rot_x(kPi) * sc(.6, .6, .3), hex(0xC8C0B0), &T.stone, true);
            B.mesh(disc_mesh(16), at(c + V3{0, 0, 1.84}) * sc(.55, .55, 1), hex(0x6A9AA0), nullptr, false, gloss);
            B.mesh(cone_mesh(10), at(c + V3{0, 0, 1.84}) * sc(.06, .06, .6), hex(0xE8F4F8, .55f), nullptr, false, static_cast<std::uint16_t>(translucent | unlit));
            break;
        }
        case StandKind::bath: {
            // a baluster pedestal, a wide shallow bowl, water with a few floating petals
            const V3 c = S.base;
            B.mesh(cylinder_mesh(14), at(c) * sc(.42, .42, .12), hex(0xC8C0B0), &T.stone, true);
            B.mesh(cone_mesh(14), at(c + V3{0, 0, .12}) * sc(.3, .3, .5), hex(0xC8C0B0), &T.stone, true);
            B.rod(c + V3{0, 0, .3}, c + V3{0, 0, .8}, .13, kWhite, &T.stone, true, 12);
            B.mesh(sphere_mesh(12, 6), at(c + V3{0, 0, .55}) * sc(.2, .2, .14), hex(0xC8C0B0), &T.stone, true);
            B.mesh(hemisphere_mesh(20, 5), at(c + V3{0, 0, 1.0}) * M34::rot_x(kPi) * sc(.95, .95, .22), hex(0xD0C8B8), &T.stone, true);
            B.mesh(torus_mesh(28, 5, .06), at(c + V3{0, 0, .99}) * sc(.95, .95, .95), hex(0xD8D0C0), &T.stone, true);
            B.mesh(disc_mesh(20), at(c + V3{0, 0, .96}) * sc(.88, .88, 1), kWhite, &T.water, false, static_cast<std::uint16_t>(translucent | gloss));
            for (int k = 0; k < 5; ++k) {
                const double a = 2 * kPi * h01(seed + 1200 + k), rr = .6 * h01(seed + 1210 + k);
                B.mesh(sphere_mesh(5, 3), at(c + V3{std::cos(a) * rr, std::sin(a) * rr, .97}) * sc(.05, .035, .01), k % 2 ? hex(0xF06A88) : hex(0xF8F0E8));
            }
            break;
        }
        case StandKind::log:
            for (const Bar* b : bars) {
                const V3 d = norm(b->b - b->a);
                B.rod(b->a - d * .3 - V3{0, 0, .14}, b->b + d * .3 - V3{0, 0, .14}, .16, kWhite, &T.bark, true, 10, .8);
                B.rod(b->b + d * .1 - V3{0, 0, .08}, b->b + d * .1 + V3{.1, -.2, .35}, .05, kWhite, &T.bark, true, 6);
            }
            break;
        }
    }
}
}  // namespace aviary_detail
using namespace aviary_detail;

int aviary_looks() { return static_cast<int>(sizeof kLooks / sizeof kLooks[0]); }
const AviaryLook& aviary_look(int i) { return kLooks[std::clamp(i, 0, aviary_looks() - 1)]; }

void Aviary::resize(int w, int h, int top_band, int bottom) {
    if (w == w_ && h == h_ && top_band == top_ && bottom == bottom_ && still_ok_) return;
    w_ = w;
    h_ = h;
    top_ = top_band;
    bottom_ = bottom;
    r.resize(w, h);
    frame_camera();
    if (still_ok_ && still_w_ > 0 && still_h_ > 0) {
        // the old picture stretched to the new size, nearest texel, until the size settles
        std::vector<float> rgb(static_cast<size_t>(w) * h * 3), depth(static_cast<size_t>(w) * h);
        for (int y = 0; y < h; ++y) {
            const int sy = std::min(still_h_ - 1, y * still_h_ / h);
            for (int x = 0; x < w; ++x) {
                const int sx = std::min(still_w_ - 1, x * still_w_ / w);
                const size_t d = static_cast<size_t>(y) * w + x, src = static_cast<size_t>(sy) * still_w_ + sx;
                for (int c = 0; c < 3; ++c) rgb[d * 3 + c] = still_rgb_[src * 3 + c];
                depth[d] = 1e30f;  // nothing in the stretched picture hides a bird
            }
        }
        still_rgb_ = std::move(rgb);
        still_depth_ = std::move(depth);
        still_w_ = w;
        still_h_ = h;
        stale_frames_ = 0;
    } else {
        still_ok_ = false;
    }
}

void Aviary::frame_camera() {
    r.yaw = 0;
    r.pitch = kPitch;
    r.persp = kPersp;
    r.set_camera();
    // the eye stays put; the target is persp ahead of it
    r.target = kEye + V3{0, std::cos(kPitch), -std::sin(kPitch)} * kPersp;
    const double room = std::max(20, h_ - top_ - bottom_);
    r.scale = std::min(w_ / (2 * .8 * kPersp), room / (2 * .52 * kPersp));
    r.ax = .5;
    r.ay = (top_ + room * .5) / std::max(1, h_);
    const AviaryLook& L = aviary_look(look_);
    r.light.sun = L.sun;
    r.light.sun_col = L.sun_col;
    r.light.amb_col = L.amb_col;
    r.light.hemisphere = true;
    r.light.amb_ground = mix(L.amb_col, hex(0x6A5A44), .45f);
    r.light.focus = kEye;
    r.light.fog_near = 4;
    r.light.fog_far = 55;
    r.light.fog_col = mix(mix(L.sky_low, L.cloud_lit, .3f), hex(0x6A8A60), .45f);
    r.light.toon_edge = .12f;
    r.light.toon_soft = .1f;
    r.light.gloss_strength = .35;
}

void Aviary::set(const Layout& layout, int look, std::uint64_t seed) {
    layout_ = layout;
    look_ = look;
    seed_ = seed;
    frame_camera();
    built_.reset();
    still_ok_ = false;
    stale_frames_ = -1;
}

struct Aviary::Built {
    Textures T;
    Builder B;
    Built(const AviaryLook& L, std::uint64_t seed) : T(L, seed) {}
};

void Aviary::draw_still() {
    const AviaryLook& L = aviary_look(look_);
    if (!built_) {
        built_ = std::make_shared<Built>(L, seed_);
        build_scene(built_->B, built_->T, L);
    }
    const Builder& B = built_->B;
    rasterise(B);
}

void Aviary::build_scene(Builder& B, const Textures& T, const AviaryLook& L) {
    B.pieces.reserve(8000);  // pieces are filled through references to the last one added
    const auto sd = static_cast<std::uint32_t>(seed_ * 2654435761u);
    build_house(B, L, T);
    for (size_t i = 0; i < layout_.canopies.size(); ++i) {
        const Canopy& c = layout_.canopies[i];
        if (c.palm) build_palm(B, c, T, sd + static_cast<std::uint32_t>(i) * 101);
        else build_broadleaf(B, c, T, sd + static_cast<std::uint32_t>(i) * 977);
    }
    // lianas strung between neighbouring crowns and down from the dome's ribs
    for (size_t i = 0; i < layout_.canopies.size(); ++i)
        for (size_t k = i + 1; k < layout_.canopies.size(); ++k) {
            const Canopy &a = layout_.canopies[i], &b = layout_.canopies[k];
            const double d = std::hypot(a.crown.x - b.crown.x, a.crown.y - b.crown.y);
            if (d < 7.5 && h01(sd + 900 + static_cast<std::uint32_t>(i * 31 + k)) < .7)
                build_liana(B, a.crown - V3{0, 0, .8}, b.crown - V3{0, 0, .8}, 1.5 + d * .25, T);
        }
    // epiphytes: ferns and bromeliads riding the limbs of the trees
    for (size_t i = 0; i < layout_.canopies.size(); ++i) {
        const Canopy& c = layout_.canopies[i];
        for (int k = 0; k < 3; ++k) {
            const double u = .45 + .15 * k;
            const V3 p = c.foot + (c.crown - c.foot) * u + V3{.35, -.3, 0};
            build_ground_cover(B, p, k % 2 ? 3 : 0, .9, T, sd + 950 + static_cast<std::uint32_t>(i * 7 + k));
        }
    }
    // A gravel path from the player's feet through the clearing to the fountain, bent
    // to keep clear of the stands.
    std::vector<V3> path;
    {
        V3 end{0, 4, 0};
        for (const Stand& st : layout_.stands)
            if (st.kind == StandKind::fountain) end = st.base - V3{0, 1.9, 0};
        // waypoints row by row toward the fountain, each where the stands leave most room,
        // swinging from side to side; then a smooth curve through them
        std::vector<V3> way{{-.6, -10.5, 0}};
        const double rows[] = {-7.0, -4.6, -2.4, -.3, 1.6};
        for (int r = 0; r < 5; ++r) {
            double best = -1e9, bx = 0;
            for (int k = 0; k <= 24; ++k) {
                const double x = -3.6 + 7.2 * k / 24;
                double room = 1e9;
                for (const Stand& st : layout_.stands)
                    if (st.kind != StandKind::hoop && st.kind != StandKind::swing) room = std::min(room, std::hypot(x - st.base.x, rows[r] - st.base.y));
                for (const Canopy& c : layout_.canopies) room = std::min(room, std::hypot(x - c.foot.x, rows[r] - c.foot.y) - .6);
                const double swing = (r % 2 ? 1 : -1) * x * .35 - std::fabs(x - way.back().x) * .25 - std::fabs(x) * .1;
                const double score = std::min(room, 2.2) + swing;
                if (score > best) { best = score; bx = x; }
            }
            way.push_back({bx, rows[r], 0});
        }
        way.push_back(end);
        way.push_back(end + V3{0, 1, 0});
        for (size_t k = 1; k + 2 < way.size(); ++k)
            for (int q = 0; q < 12; ++q) {
                const double t = q / 12.0, t2 = t * t, t3 = t2 * t;
                const V3 &p0 = way[k - 1], &p1 = way[k], &p2 = way[k + 1], &p3 = way[k + 2];
                path.push_back((p1 * 2 + (p2 - p0) * t + (p0 * 2 - p1 * 5 + p2 * 4 - p3) * t2 + (p1 * 3 - p0 - p2 * 3 + p3) * t3) * .5);
            }
        path.push_back(end);
        Piece& p = B.own(kWhite, static_cast<std::uint16_t>(translucent), false);
        p.tex = &T.gravel;
        double along_path = 0;
        for (size_t k = 0; k + 1 < path.size(); ++k) {
            const V3 a = path[k], b = path[k + 1];
            const V3 n = norm(cross(b - a, {0, 0, 1}));
            const double w = .85, step = len(b - a);
            const double t0 = along_path / 2, t1 = (along_path + step) / 2;
            along_path += step;
            const V3 lift{0, 0, .006};
            for (int side = -1; side <= 1; side += 2) {
                const V3 a0 = a + lift, b0 = b + lift, a1 = a + n * (side * w * .6) + lift, b1 = b + n * (side * w * .6) + lift;
                const V3 a2 = a + n * (side * w) + lift, b2 = b + n * (side * w) + lift;
                const Col solid{1, 1, 1, 1}, clear{1, 1, 1, 0};
                const double s0 = .5, s1 = .5 + side * .3, s2 = .5 + side * .5;
                const V3 up{0, 0, 1};
                p.own.push_back({a0, up, s0, t0, solid}); p.own.push_back({b0, up, s0, t1, solid}); p.own.push_back({b1, up, s1, t1, solid});
                p.own.push_back({a0, up, s0, t0, solid}); p.own.push_back({b1, up, s1, t1, solid}); p.own.push_back({a1, up, s1, t0, solid});
                p.own.push_back({a1, up, s1, t0, solid}); p.own.push_back({b1, up, s1, t1, solid}); p.own.push_back({b2, up, s2, t1, clear});
                p.own.push_back({a1, up, s1, t0, solid}); p.own.push_back({b2, up, s2, t1, clear}); p.own.push_back({a2, up, s2, t0, clear});
            }
        }
    }
    auto path_dist = [&](V3 q) {
        double d = 1e9;
        for (const V3& pp : path) d = std::min(d, std::hypot(q.x - pp.x, q.y - pp.y));
        return d;
    };
    // planting beds round the wall: banana clumps and tree ferns, and two near the front
    {
        Piece& p = B.own(kWhite, 0, false);
        p.tex = &T.soil;
        for (int k = 0; k < 40; ++k) {
            const double a = -.2 + (kPi + .4) * k / 40, b = -.2 + (kPi + .4) * (k + 1) / 40;
            Builder::quad(p.own, wall_at(a, .02, kDomeR - 2.6), wall_at(b, .02, kDomeR - 2.6), wall_at(b, .02, kDomeR - .05), wall_at(a, .02, kDomeR - .05), kWhite, k / 4.0, (k + 1) / 4.0, 0, 1);
        }
    }
    // the understorey: banana clumps and tree ferns all round, two rings deep, hiding the
    // wall; never across the line to a bird sitting low
    auto in_the_way = [&](V3 p, double reach) {
        const V3 e = eye_at();
        for (const Place& pl : layout_.places) {
            if (pl.pos.z > 3.2) continue;
            const double dx = pl.pos.x - e.x, dy = pl.pos.y - e.y, l2 = dx * dx + dy * dy;
            const double t = ((p.x - e.x) * dx + (p.y - e.y) * dy) / l2;
            if (t <= 0 || t >= 1) continue;
            if (std::hypot(e.x + dx * t - p.x, e.y + dy * t - p.y) < reach) return true;
        }
        return false;
    };
    for (int ring = 0; ring < 2; ++ring)
        for (int k = 0; k < 26; ++k) {
            const auto q = static_cast<std::uint32_t>(ring * 100 + k);
            const double th = -.45 + (kPi + .9) * (k + h01(sd + 500 + q)) / 26;
            const double rr = ring == 0 ? kDomeR - 1.2 - 1.2 * h01(sd + 520 + q) : 7.2 + 1.6 * h01(sd + 520 + q);
            const V3 p{rr * std::cos(th), rr * std::sin(th), 0};
            if (p.y < -8.5 || in_the_way(p, ring ? 2.0 : 1.4)) continue;
            bool clear = true;
            for (const Stand& st : layout_.stands)
                if (std::hypot(p.x - st.base.x, p.y - st.base.y) < 1.5) clear = false;
            if (!clear) continue;
            if (h01(sd + 540 + q) < .4) build_tree_fern(B, p, 1.8 + 2.2 * h01(sd + 560 + q), T, sd + 580 + q * 7);
            else build_banana(B, p, 1.1 + .7 * h01(sd + 600 + q), T, sd + 620 + q * 11);
        }
    // ferns round the palms' feet and the stands, and clumps across the floor
    for (const Canopy& c : layout_.canopies) build_tree_fern(B, c.foot + V3{.6, -.5, 0}, .25, T, sd + static_cast<std::uint32_t>(c.foot.x * 100));
    for (const Stand& st : layout_.stands)
        if (st.kind == StandKind::tree || st.kind == StandKind::bamboo) build_tree_fern(B, st.base + V3{-.5, -.4, 0}, .2, T, sd + static_cast<std::uint32_t>(st.base.y * 100 + 7));
    for (int k = 0; k < 6; ++k) {
        const V3 p{-7 + 14 * h01(sd + 800 + k), -1 + 7 * h01(sd + 810 + k), 0};
        bool clear = std::fabs(p.x) > 1.6 && path_dist(p) > 1.4;
        for (const Stand& st : layout_.stands)
            if (std::hypot(p.x - st.base.x, p.y - st.base.y) < 1.6) clear = false;
        if (clear) build_banana(B, p, .8 + .4 * h01(sd + 820 + k), T, sd + 830 + k * 13);
    }
    // low planting over the sand: denser toward the beds and round the stands, clear of
    // the fountain and of each stand's foot
    for (int k = 0, placed = 0; k < 900 && placed < 170; ++k) {
        const V3 p{-11 + 22 * h01(sd + 2000 + k), -7.5 + 19 * h01(sd + 3000 + k), 0};
        const double rho = std::hypot(p.x, p.y);
        if (rho > kDomeR - .6) continue;
        double want = .25 + .6 * smooth(6, 10, rho);
        bool clear = path_dist(p) > 1.05;
        if (path_dist(p) < 1.7) want += .4;  // the path's edges are planted
        for (const Stand& st : layout_.stands) {
            if (st.kind == StandKind::hoop) continue;
            const double d = std::hypot(p.x - st.base.x, p.y - st.base.y);
            if (d < (st.kind == StandKind::fountain ? 1.8 : st.kind == StandKind::log ? 1.0 : .55)) clear = false;
            else if (d < 2.2) want += .35;
        }
        for (const Canopy& c : layout_.canopies)
            if (std::hypot(p.x - c.foot.x, p.y - c.foot.y) < 2.2) want += .3;
        if (!clear || h01(sd + 4000 + k) > want) continue;
        const int kind = static_cast<int>(h01(sd + 5000 + k) * 4);
        build_ground_cover(B, p, kind, .8 + .8 * h01(sd + 6000 + k), T, sd + 7000 + k * 17);
        ++placed;
    }
    build_banana(B, {-6.2, -5.4, 0}, 1.3, T, sd + 700);
    build_banana(B, {6.6, -4.8, 0}, 1.2, T, sd + 710);
    build_stands(B, layout_, T, L, sd);
    // a feeding table at the clearing's edge, beside the path
    auto free_spot = [&](V3 q, double room) {
        if (path_dist(q) < .9 || in_the_way(q, .9)) return false;
        for (const Stand& st : layout_.stands)
            if (st.kind != StandKind::hoop && std::hypot(q.x - st.base.x, q.y - st.base.y) < room) return false;
        for (const Canopy& c : layout_.canopies)
            if (std::hypot(q.x - c.foot.x, q.y - c.foot.y) < 1.2) return false;
        return true;
    };
    V3 feeder{1e9, 0, 0};
    // ...seen whole: no trunk between it and the player
    auto seen = [&](V3 q) {
        const V3 e = eye_at();
        for (int k = 1; k < 30; ++k) {
            const V3 m = e + (q - e) * (k / 30.0);
            for (const Stand& st : layout_.stands)
                if ((st.kind == StandKind::tree || st.kind == StandKind::bamboo || st.kind == StandKind::lamp) && std::hypot(m.x - st.base.x, m.y - st.base.y) < .7) return false;
            for (const Canopy& c : layout_.canopies)
                if (std::hypot(m.x - c.foot.x, m.y - c.foot.y) < 1.0) return false;
        }
        return true;
    };
    for (V3 q : {V3{2.3, .2, 0}, V3{-2.3, .4, 0}, V3{2.4, 1.4, 0}, V3{-2.4, 1.6, 0}, V3{1.9, -.8, 0}, V3{-1.9, -.6, 0}, V3{2.8, 3.0, 0}, V3{-2.8, 3.2, 0}})
        if (free_spot(q, 1.4) && seen(q)) { feeder = q; break; }
    if (feeder.x < 1e8) build_feeder(B, feeder, T, sd + 1500);
    if (std::getenv("PT_AVIARY_TRACE")) std::fprintf(stderr, "feeder at %.1f %.1f\n", feeder.x, feeder.y);
    // orchids and bromeliads on the trunks, on the side toward the player, at eye height
    const Col orchid_petal[] = {hex(0xF8F4F0), hex(0xE860B0), hex(0xF8E070), hex(0xC8A0E8)};
    const Col orchid_lip[] = {hex(0xD02878), hex(0xF8F0F8), hex(0xC83020), hex(0x7A3AA0)};
    int n_orchid = 0;
    for (size_t i = 0; i < layout_.canopies.size(); ++i) {
        const Canopy& c = layout_.canopies[i];
        const V3 toward = norm(V3{eye_at().x - c.foot.x, eye_at().y - c.foot.y, 0});
        for (int k = 0; k < 3; ++k) {
            const double z = 1.4 + 1.1 * k + .4 * h01(sd + 1600 + static_cast<std::uint32_t>(i * 5 + k));
            const double u = z / std::max(1.0, c.crown.z);
            const V3 axis = c.foot + (c.crown - c.foot) * u;
            const V3 side = norm(cross(toward, {0, 0, 1})) * ((k % 2 ? 1 : -1) * .3);
            const V3 at_bark = axis + toward * (c.palm ? .3 : .38) + side;
            const auto q = sd + 1700 + static_cast<std::uint32_t>(i * 11 + k);
            if (k == 1) build_ground_cover(B, at_bark, 3, .7, T, q);
            else {
                build_orchid(B, at_bark, norm(toward + side), orchid_petal[n_orchid % 4], orchid_lip[n_orchid % 4], q);
                ++n_orchid;
            }
        }
    }
    // a bunch of bananas hanging from the broadleaf limb nearest the clearing
    {
        const Canopy* nearest = nullptr;
        double nd = 1e9;
        for (const Canopy& c : layout_.canopies)
            if (!c.palm && std::hypot(c.crown.x, c.crown.y - 1) < nd) { nd = std::hypot(c.crown.x, c.crown.y - 1); nearest = &c; }
        if (nearest) {
            const V3 in = norm(V3{-nearest->crown.x, 1 - nearest->crown.y, 0});
            build_banana_bunch(B, nearest->crown + in * (nearest->radius * .75) - V3{0, 0, .9}, sd + 1800);
        }
    }
    // feathers dropped under the perches and on the path; butterflies on the flowers
    const Col plumes[] = {hex(0xD8262A), hex(0x2A7AD0), hex(0x3AA040), hex(0xF2B820), hex(0xF4F4EE), hex(0x8E9298), hex(0xE87AA0)};
    for (int k = 0; k < 26; ++k) {
        const auto q = sd + 1900 + static_cast<std::uint32_t>(k);
        V3 at_floor;
        if (k < 18) {
            const Place& pl = layout_.places[static_cast<size_t>(k) % layout_.places.size()];
            at_floor = {pl.pos.x + (h01(q) - .5) * 1.4, pl.pos.y + (h01(q + 50) - .5) * 1.4, 0};
        } else {
            const V3& pp = path[static_cast<size_t>(h01(q) * (path.size() - 1))];
            at_floor = {pp.x + (h01(q + 50) - .5) * .8, pp.y + (h01(q + 60) - .5) * .5, 0};
        }
        build_feather(B, at_floor, plumes[k % 7], 2 * kPi * h01(q + 100), .8 + .6 * h01(q + 150));
    }
    const Col wings[] = {hex(0x2A7AF0), hex(0xF08A20), hex(0xF8E040), hex(0xE83A6A)};
    for (int k = 0; k < 5; ++k) {
        const auto q = sd + 2100 + static_cast<std::uint32_t>(k);
        const V3 c{-6 + 12 * h01(q), -2 + 6 * h01(q + 10), .4 + 1.6 * h01(q + 20)};
        if (in_the_way(c, .8)) continue;
        build_butterfly(B, c, wings[k % 4], 2 * kPi * h01(q + 30));
    }
    // the gardener's watering can and trowel, by a bed near the front
    for (V3 q : {V3{-4.4, -3.8, 0}, V3{4.6, -3.6, 0}, V3{-5.2, -1.6, 0}, V3{5.2, -1.2, 0}})
        if (free_spot(q, 1.0)) {
            build_watering_can(B, q, q.x < 0 ? .5 : 2.6);
            break;
        }

}

void Aviary::rasterise(const Builder& B) {
    const AviaryLook& L = aviary_look(look_);
    const auto sd = static_cast<std::uint32_t>(seed_ * 2654435761u);
    // Shade first: the palms, the trees and the stands cast it.
    r.clear_depth();
    std::fill(r.rgb.begin(), r.rgb.end(), .5f);
    r.shadow_begin({0, 2, 4}, 16, 2048);
    for (const Piece& p : B.pieces)
        if (p.cast) {
            if (p.mesh) r.shadow_cast(p.mesh->data(), p.mesh->size(), &p.model);
            else r.shadow_cast(p.own.data(), p.own.size(), nullptr);
        }
    r.shadow_use(false);
    for (int pass = 0; pass < 2; ++pass)
        for (const Piece& p : B.pieces) {
            const bool late = (p.mat & translucent) != 0;
            if (late != (pass == 1)) continue;
            if (p.mesh) draw_mesh(r, *p.mesh, p.model, p.tex, p.col, p.mat);
            else if (!p.own.empty()) r.draw(p.own.data(), p.own.size(), p.tex, p.mat, nullptr, nullptr, nullptr, p.col);
        }
    r.shadow_darkness = .8f;
    r.shadow_screen_pass(.78f);
    // shafts of sun slanting down into the clearing through the haze
    {
        std::vector<Vtx> shaft;
        const V3 sun = L.sun;
        for (int k = 0; k < 6; ++k) {
            const V3 floor_at{-2.5 + 5 * h01(sd + 1100 + k), -1 + 5 * h01(sd + 1110 + k), 0};
            const V3 top = floor_at + sun * (12 / sun.z);
            const double w = .35 + .6 * h01(sd + 1120 + k);
            const V3 side = norm(cross(sun, V3{0, 1, 0})) * w;
            const Col c = alpha(L.sun_col, .05f + .05f * static_cast<float>(h01(sd + 1130 + k)));
            const Col c0 = alpha(L.sun_col, 0);
            const V3 n{0, -1, 0};
            shaft.push_back({floor_at - side, n, 0, 0, c0}); shaft.push_back({floor_at + side, n, 0, 0, c0}); shaft.push_back({top + side, n, 0, 0, c});
            shaft.push_back({floor_at - side, n, 0, 0, c0}); shaft.push_back({top + side, n, 0, 0, c}); shaft.push_back({top - side, n, 0, 0, c});
        }
        r.draw(shaft.data(), shaft.size(), nullptr, static_cast<std::uint16_t>(additive | unlit | no_depth_write | double_sided));
    }
    still_rgb_ = r.rgb;
    still_depth_ = r.depth;
    still_w_ = r.W;
    still_h_ = r.H;
    still_ok_ = true;
    birds_whole_ = false;
}

Aviary::Drawn Aviary::bird_box(const BirdPose& b) const {
    // the screen box round a bird: a block about its feet, wider in flight or flapping
    const double sz = b.size, wide = (b.wing_l > 0 || b.wing_r > 0 || b.fly > 0 || b.ruffle > 0) ? .95 : .55;
    Drawn d;
    d.x0 = d.y0 = 1 << 30;
    d.x1 = d.y1 = -(1 << 30);
    const V3 base = b.pos + V3{0, 0, std::fabs(std::sin(b.bob * 3.14159)) * .06 * sz + b.fly * b.fly * 2.4};
    for (int k = 0; k < 8; ++k) {
        const V3 c = base + V3{(k & 1 ? 1 : -1) * wide * sz + b.fly * 1.6, (k & 2 ? 1 : -1) * wide * sz + b.fly * .8, k & 4 ? 1.05 * sz : -.25 * sz};
        double x, y, z;
        r.project(c, x, y, z);
        d.x0 = std::min(d.x0, static_cast<int>(std::floor(x)) - 3);
        d.y0 = std::min(d.y0, static_cast<int>(std::floor(y)) - 3);
        d.x1 = std::max(d.x1, static_cast<int>(std::ceil(x)) + 3);
        d.y1 = std::max(d.y1, static_cast<int>(std::ceil(y)) + 3);
    }
    d.x0 = std::clamp(d.x0, 0, r.W);
    d.x1 = std::clamp(d.x1, 0, r.W);
    d.y0 = std::clamp(d.y0, 0, r.H);
    d.y1 = std::clamp(d.y1, 0, r.H);
    return d;
}

bool Aviary::render(const AviaryState& s, double t) {
    r.time = t;
    if (stale_frames_ >= 0 && ++stale_frames_ > 4) {
        stale_frames_ = -1;
        still_ok_ = false;
    }
    if (!still_ok_) {
        draw_still();
        birds_whole_ = false;
    }
    // each bird's look, and the birds that changed since the last frame
    const size_t n = s.birds.size();
    std::vector<Drawn> now(n);
    for (size_t i = 0; i < n; ++i) {
        const BirdPose& b = s.birds[i];
        std::uint64_t key = 1469598103934665603ULL;
        auto mix = [&](double v) { key = (key ^ static_cast<std::uint64_t>(std::llround(v * 4096))) * 1099511628211ULL; };
        for (double v : {b.pos.x, b.pos.y, b.pos.z, b.yaw, b.size, b.bob, b.head_tilt, b.head_turn, b.head_nod, b.beak, b.wing_l, b.wing_r, b.blink, b.ruffle, b.shake, b.fly})
            mix(v);
        mix(b.species);
        mix(b.mark);
        mix(b.hover);
        mix(b.glow);
        mix(i < s.shaded.size() && s.shaded[i]);
        now[i] = bird_box(b);
        now[i].key = key;
    }
    int rx0 = r.W, ry0 = r.H, rx1 = 0, ry1 = 0;
    bool whole = !birds_whole_ || drawn_.size() != n;
    if (!whole)
        for (size_t i = 0; i < n; ++i)
            if (now[i].key != drawn_[i].key) {
                for (const Drawn& d : {drawn_[i], now[i]}) {
                    rx0 = std::min(rx0, d.x0);
                    ry0 = std::min(ry0, d.y0);
                    rx1 = std::max(rx1, d.x1);
                    ry1 = std::max(ry1, d.y1);
                }
            }
    if (!whole && (rx0 >= rx1 || ry0 >= ry1)) return false;  // nothing changed
    if (whole) {
        r.rgb = still_rgb_;
        r.depth = still_depth_;
    } else {
        // the still put back under what changed
        for (int y = ry0; y < ry1; ++y) {
            const size_t row = static_cast<size_t>(y) * r.W;
            std::copy(still_rgb_.begin() + static_cast<std::ptrdiff_t>((row + rx0) * 3), still_rgb_.begin() + static_cast<std::ptrdiff_t>((row + rx1) * 3),
                      r.rgb.begin() + static_cast<std::ptrdiff_t>((row + rx0) * 3));
            std::copy(still_depth_.begin() + static_cast<std::ptrdiff_t>(row + rx0), still_depth_.begin() + static_cast<std::ptrdiff_t>(row + rx1),
                      r.depth.begin() + static_cast<std::ptrdiff_t>(row + rx0));
        }
    }
    const Col sun = r.light.sun_col, amb = r.light.amb_col;
    for (size_t i = 0; i < n; ++i) {
        // a bird away from what changed is left as it was drawn: drawn again, it would
        // write exactly its own pixels, and the depth test keeps them
        if (!whole && (now[i].x1 <= rx0 || now[i].x0 >= rx1 || now[i].y1 <= ry0 || now[i].y0 >= ry1)) continue;
        const bool shade = i < s.shaded.size() && s.shaded[i];
        r.light.sun_col = shade ? Col{sun.r * .12f, sun.g * .14f, sun.b * .18f, 1} : sun;
        r.light.amb_col = shade ? Col{amb.r * .75f, amb.g * .8f, amb.b * .86f, 1} : amb;
        // drawn from its pose alone (no sway by the clock), so a bird drawn again over itself
        // writes exactly what is there
        draw_bird(r, s.birds[i], 0);
    }
    r.light.sun_col = sun;
    r.light.amb_col = amb;
    drawn_ = std::move(now);
    birds_whole_ = true;
    return true;
}

V3 Aviary::eye() { return kEye; }

void Aviary::to_screen(V3 p, double& sx, double& sy) const {
    double sz;
    r.project(p, sx, sy, sz);
}

int Aviary::pick_bird(double sx, double sy, const AviaryState& s) const {
    int best = -1;
    double bd = 1e9;
    for (size_t i = 0; i < s.birds.size(); ++i) {
        double hx, hy, fx, fy;
        to_screen(bird_head(s.birds[i]), hx, hy);
        to_screen(s.birds[i].pos, fx, fy);
        const double reach = std::max(8.0, std::hypot(hx - fx, hy - fy) * .6);
        const double d = std::hypot(sx - (hx + fx) / 2, (sy - (hy + fy) / 2) * .7);
        if (d < reach && d < bd) { bd = d; best = static_cast<int>(i); }
    }
    return best;
}

}  // namespace pt
