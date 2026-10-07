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

// ------------------------------------------------------------------ tree building
// Models are built in tree space: base at the origin, z up, in render units
// (a mature broadleaf stands about 1.3 high; a tile is 1 wide).

void tri(Mesh& m, const Vtx& a, const Vtx& b, const Vtx& c) { m.push_back(a); m.push_back(b); m.push_back(c); }
Col scol(Col c, double k) { return {static_cast<float>(c.r * k), static_cast<float>(c.g * k), static_cast<float>(c.b * k), c.a}; }
Col jitter_col(R& g, double amount) {
    const double k = g.in(1 - amount, 1 + amount * .6);
    return {static_cast<float>(k * g.in(.96, 1.04)), static_cast<float>(k * g.in(.97, 1.03)), static_cast<float>(k * g.in(.95, 1.05)), 1};
}
V3 polar(double a, double from_vertical) {
    return {std::sin(from_vertical) * std::cos(a), std::sin(from_vertical) * std::sin(a), std::cos(from_vertical)};
}
void basis(V3 d, V3& e1, V3& e2) {
    const V3 ref = std::fabs(d.z) < .92 ? V3{0, 0, 1} : V3{1, 0, 0};
    e1 = norm(cross(d, ref));
    e2 = cross(d, e1);
}

// A tapered limb from a to b (radii ra, rb), bark wrapped once around it.
void add_limb(Mesh& m, V3 a, V3 b, double ra, double rb, int sides, Col ca, Col cb) {
    V3 e1, e2;
    basis(norm(b - a), e1, e2);
    const double along = len(b - a) / (M_PI * (ra + rb) + 1e-6);
    for (int i = 0; i < sides; ++i) {
        const double t0 = 2 * M_PI * i / sides, t1 = 2 * M_PI * (i + 1) / sides;
        const V3 n0 = e1 * std::cos(t0) + e2 * std::sin(t0), n1 = e1 * std::cos(t1) + e2 * std::sin(t1);
        const double s0 = static_cast<double>(i) / sides, s1 = static_cast<double>(i + 1) / sides;
        const Vtx p{a + n0 * ra, n0, s0, along, ca}, q{a + n1 * ra, n1, s1, along, ca};
        const Vtx w{b + n1 * rb, n1, s1, 0, cb}, x{b + n0 * rb, n0, s0, 0, cb};
        tri(m, p, q, w);
        tri(m, p, w, x);
    }
}

// A leaf clump: a lumpy ellipsoid with a flatter underside, darker below and
// toward the heart of the crown, so a crown reads as masses of leaves in
// light and shade. Each clump also lays a cap on `snow` for wintry places.
void add_clump(Mesh& m, Mesh& snow, R& g, V3 c, double rx, double ry, double rz, double rough, V3 heart, Col col, int sl = 7, int st = 4) {
    V3 p[6][8];
    V3 d[6][8];
    const double stagger = M_PI / sl;
    for (int j = 0; j <= st; ++j) {
        const double ph = -M_PI / 2 + M_PI * j / st;
        for (int i = 0; i < sl; ++i) {
            const double th = 2 * M_PI * i / sl + (j % 2) * stagger;
            const V3 dir{std::cos(ph) * std::cos(th), std::cos(ph) * std::sin(th), std::sin(ph)};
            const double k = (j == 0 || j == st) ? 1 : 1 + rough * (g.uni() - .5);
            const double flat = dir.z < 0 ? .68 : 1;
            p[j][i] = c + V3{dir.x * rx * k, dir.y * ry * k, dir.z * rz * k * flat};
            d[j][i] = dir;
        }
    }
    const double su = std::max(.6, (rx + ry) * 2.2), tv = std::max(.4, rz * 2.6);
    Vtx v[6][8];
    for (int j = 0; j <= st; ++j)
        for (int i = 0; i < sl; ++i) {
            const V3 dir = d[j][i];
            const V3 out = norm(p[j][i] - heart);
            const double k = .56 + .26 * (.5 + .5 * dir.z) + .2 * (.5 + .5 * dot(dir, out));
            v[j][i] = Vtx{p[j][i], norm({dir.x / rx, dir.y / ry, dir.z / rz}), su * i / sl, tv * (1 - static_cast<double>(j) / st), scol(col, k)};
        }
    for (int j = 0; j < st; ++j)
        for (int i = 0; i < sl; ++i) {
            const int i1 = (i + 1) % sl;
            if (j > 0) tri(m, v[j][i], v[j][i1], v[j + 1][i1]);
            if (j < st - 1) tri(m, v[j][i], v[j + 1][i1], v[j + 1][i]);
        }
    // snow: the top of the clump, lifted a little proud of the leaves
    // rings: halfway between the two rings below the pole, the last ring, the pole
    const Col white = hex(0xF4F8FF);
    const int j1 = st - 1;
    Vtx sv[3][8];
    for (int i = 0; i < sl; ++i) {
        const V3 low = (p[j1 - 1][i] + p[j1][i]) * .5;
        sv[0][i] = Vtx{c + (low - c) * 1.09, d[j1][i], 0, 0, scol(white, .84)};
        sv[1][i] = Vtx{c + (p[j1][i] - c) * 1.07 + V3{0, 0, rz * .05}, d[j1][i], 0, 0, scol(white, .93)};
        sv[2][i] = Vtx{c + (p[st][i] - c) * 1.06 + V3{0, 0, rz * .04}, d[st][i], 0, 0, white};
    }
    for (int i = 0; i < sl; ++i) {
        const int i1 = (i + 1) % sl;
        tri(snow, sv[0][i], sv[0][i1], sv[1][i1]);
        tri(snow, sv[0][i], sv[1][i1], sv[1][i]);
        tri(snow, sv[1][i], sv[1][i1], sv[2][i1]);
    }
}

// A whorl of conifer branches: a jagged, drooping cone with a dark underside.
void add_tier(Mesh& m, Mesh& snow, R& g, V3 c, double r, double h, double droop, int tips, double turn, double ragged, Col col, double notch = .56) {
    const int n = tips * 2;
    V3 rim[24];
    double rr[24];
    for (int k = 0; k < n; ++k) {
        const bool tip = k % 2 == 0;
        const double th = turn + M_PI * k / tips + (tip ? g.in(-.2, .2) * ragged : 0);
        rr[k] = tip ? r * g.in(1 - .22 * ragged, 1 + .14 * ragged) : r * notch * g.in(.9, 1.1);
        const double z = c.z - (tip ? droop * rr[k] * g.in(.8, 1.2) : droop * rr[k] * .3);
        rim[k] = {c.x + std::cos(th) * rr[k], c.y + std::sin(th) * rr[k], z};
    }
    const V3 apex = c + V3{0, 0, h};
    const V3 hub = c + V3{0, 0, h * .12};
    const V3 up{0, 0, 1};
    for (int k = 0; k < n; ++k) {
        const int k1 = (k + 1) % n;
        const V3 ra = norm({rim[k].x - c.x, rim[k].y - c.y, 0}), rb = norm({rim[k1].x - c.x, rim[k1].y - c.y, 0});
        const Col ca = scol(col, k % 2 == 0 ? 1.0 : .7), cb = scol(col, k1 % 2 == 0 ? 1.0 : .7);
        const double sa = static_cast<double>(k) / n * 2, sb = static_cast<double>(k + 1) / n * 2;
        tri(m, Vtx{rim[k], norm(ra * h + up * r), sa, 1.2, ca}, Vtx{rim[k1], norm(rb * h + up * r), sb, 1.2, cb}, Vtx{apex, up, (sa + sb) * .5, 0, scol(col, .92)});
        const Col under = scol(col, .42);
        tri(m, Vtx{rim[k1], norm(rb * .4 - up), sb, 1.2, under}, Vtx{rim[k], norm(ra * .4 - up), sa, 1.2, under}, Vtx{hub, V3{0, 0, -1}, (sa + sb) * .5, .6, scol(col, .3)});
        // snow lies on the upper part of each whorl and leaves the green tips showing
        const double f = k % 2 == 0 ? .66 : .74;
        const V3 sa3 = rim[k] + (apex - rim[k]) * (1 - f) + V3{0, 0, .022};
        const V3 sb3 = rim[k1] + (apex - rim[k1]) * (1 - (k1 % 2 == 0 ? .66 : .74)) + V3{0, 0, .022};
        const Col wt = hex(0xF4F8FF);
        tri(snow, Vtx{sa3, norm(ra * h + up * r), 0, 0, scol(wt, .9)}, Vtx{sb3, norm(rb * h + up * r), 0, 0, scol(wt, .9)}, Vtx{apex + V3{0, 0, .02}, up, 0, 0, wt});
    }
}

// A small bead (berries): an octahedron.
void add_bead(Mesh& m, V3 c, double r, Col col) {
    const V3 ax[6] = {{1, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int i = 0; i < 4; ++i) {
        const V3 a = ax[i], b = ax[(i + 1) % 4];
        tri(m, Vtx{c + a * r, a, 0, 0, col}, Vtx{c + b * r, b, 0, 0, col}, Vtx{c + ax[4] * r, ax[4], 0, 0, shade(col, 1.25f)});
        tri(m, Vtx{c + b * r, b, 0, 0, col}, Vtx{c + a * r, a, 0, 0, col}, Vtx{c + ax[5] * r, ax[5], 0, 0, scol(col, .6)});
    }
}

const Col kBarkLow{.62f, .6f, .58f, 1}, kWhite{1, 1, 1, 1};

// A crooked stem through n points, tapering from r0 to r1.
void add_stem(Mesh& m, const V3* pts, int n, double r0, double r1, int sides, Col c0, Col c1) {
    for (int i = 0; i + 1 < n; ++i) {
        const double f0 = static_cast<double>(i) / (n - 1), f1 = static_cast<double>(i + 1) / (n - 1);
        add_limb(m, pts[i], pts[i + 1], r0 + (r1 - r0) * f0, r0 + (r1 - r0) * f1, sides, mix(c0, c1, static_cast<float>(f0)), mix(c0, c1, static_cast<float>(f1)));
    }
}

// Oak: a short, thick bole that forks into a few spreading limbs, each
// carrying heavy clumps; the crown is broader than it is tall.
void build_oak(TreeModel& t, R& g) {
    const double bole = g.in(.24, .34), tr = g.in(.07, .09);
    const V3 top{g.in(-.04, .04), g.in(-.04, .04), bole};
    add_limb(t.wood, {0, 0, -.05}, top, tr * 1.4, tr, 6, kBarkLow, kWhite);
    const V3 heart = top + V3{0, 0, .3};
    const int limbs = 3 + g.below(2);
    const double a0 = g.in(0, 2 * M_PI);
    for (int k = 0; k < limbs; ++k) {
        const double a = a0 + k * 2 * M_PI / limbs + g.in(-.4, .4), el = g.in(.7, 1.15), L = g.in(.32, .5);
        const V3 mid = top + polar(a, el) * (L * .55);
        const V3 end = mid + polar(a + g.in(-.4, .4), el - g.in(.05, .35)) * (L * .5);
        add_limb(t.wood, top, mid, tr * .62, tr * .42, 5, kWhite, kWhite);
        add_limb(t.wood, mid, end, tr * .42, tr * .2, 4, kWhite, kWhite);
        const double cr = g.in(.22, .3);
        add_clump(t.leaf, t.snow, g, end + V3{0, 0, cr * .3}, cr * g.in(1, 1.2), cr * g.in(.9, 1.1), cr * .78, .4, heart, jitter_col(g, .07));
        if (g.uni() < .65) add_clump(t.leaf, t.snow, g, mid + V3{0, 0, cr * .5}, cr * .82, cr * .78, cr * .66, .4, heart, jitter_col(g, .07));
    }
    add_clump(t.leaf, t.snow, g, top + V3{g.in(-.05, .05), g.in(-.05, .05), g.in(.3, .38)}, .3, .28, .24, .35, heart, jitter_col(g, .05));
}

// Beech, lime and maple: a clean trunk under a dense oval (beech) or round
// (maple) crown, with limbs showing through the gaps.
void build_dome(TreeModel& t, R& g, bool round) {
    const double bole = round ? g.in(.2, .28) : g.in(.26, .36), tr = g.in(.055, .072);
    const V3 top{g.in(-.04, .04), g.in(-.04, .04), bole};
    const V3 bend{top.x * .4 + g.in(-.02, .02), top.y * .4 + g.in(-.02, .02), bole * .5};
    add_limb(t.wood, {0, 0, -.05}, bend, tr * 1.25, tr * 1.05, 6, kBarkLow, kWhite);
    add_limb(t.wood, bend, top, tr * 1.05, tr * .85, 6, kWhite, kWhite);
    const double ax = round ? g.in(.4, .48) : g.in(.33, .4), az = round ? ax * g.in(.85, 1.0) : g.in(.44, .54);
    const V3 heart = top + V3{0, 0, az * .62};
    for (int k = 0; k < 3; ++k) {
        const double a = g.in(0, 2 * M_PI);
        add_limb(t.wood, top, heart + V3{std::cos(a) * ax * .55, std::sin(a) * ax * .55, g.in(-.1, .15)}, tr * .5, tr * .2, 4, kWhite, kWhite);
    }
    add_clump(t.leaf, t.snow, g, heart, ax * .78, ax * .76, az * .72, .3, heart + V3{0, 0, -az}, jitter_col(g, .05));
    const int n = (round ? 7 : 6) + g.below(3);
    const double a0 = g.in(0, 2 * M_PI);
    for (int k = 0; k < n; ++k) {
        const double a = a0 + k * 2.39996 + g.in(-.3, .3), e = std::asin(g.in(-.45, .92));
        const V3 at{heart.x + std::cos(e) * std::cos(a) * ax * .66, heart.y + std::cos(e) * std::sin(a) * ax * .66, heart.z + std::sin(e) * az * .62};
        const double cr = g.in(.18, .25);
        add_clump(t.leaf, t.snow, g, at, cr * g.in(1, 1.15), cr, cr * .85, .4, heart, jitter_col(g, .08));
    }
}

// Birch: one to three slender, leaning white stems with small, airy clumps
// hung along their upper halves.
void build_birch(TreeModel& t, R& g) {
    const int stems = g.uni() < .42 ? 2 + g.below(2) : 1;
    const double a0 = g.in(0, 2 * M_PI);
    for (int s = 0; s < stems; ++s) {
        const double a = a0 + s * 2 * M_PI / stems + g.in(-.4, .4);
        const double lean = stems > 1 ? g.in(.12, .3) : g.in(0, .12), H = g.in(1.0, 1.35) * (s ? g.in(.75, .92) : 1);
        V3 pts[4];
        pts[0] = {std::cos(a) * .03 * (stems > 1), std::sin(a) * .03 * (stems > 1), -.04};
        for (int k = 1; k < 4; ++k) {
            const double bend = lean * (1 - .25 * k) + g.in(-.06, .06);
            pts[k] = pts[k - 1] + polar(a + g.in(-.3, .3), bend) * (H / 3);
        }
        const double r0 = (s ? .034 : .042);
        add_stem(t.wood, pts, 4, r0, .014, 5, scol(kBarkLow, 1.1), kWhite);
        const V3 heart = (pts[2] + pts[3]) * .5;
        const int clumps = 7 + g.below(3);
        for (int k = 0; k < clumps; ++k) {
            const double f = .42 + .56 * k / (clumps - 1.0);
            const double seg = f * 3;
            const int i = std::min(2, static_cast<int>(seg));
            const V3 on = pts[i] + (pts[i + 1] - pts[i]) * (seg - i);
            const double side = a + k * 2.39996 + g.in(-.5, .5), off = g.in(.06, .14) * (1.25 - .6 * f), cr = g.in(.09, .13) * (1.2 - .4 * f);
            add_clump(t.leaf, t.snow, g, on + V3{std::cos(side) * off, std::sin(side) * off, -cr * .5}, cr, cr * g.in(.8, 1), cr * 1.3, .5, heart, jitter_col(g, .09), 6, 3);
        }
        add_clump(t.leaf, t.snow, g, pts[3] + V3{0, 0, .03}, .09, .085, .13, .4, heart, jitter_col(g, .06), 6, 4);
    }
}

// Lombardy poplar: a tall, narrow column of upswept clumps.
void build_poplar(TreeModel& t, R& g) {
    const double H = g.in(1.55, 1.95), wide = g.in(.17, .22);
    add_limb(t.wood, {0, 0, -.05}, {0, 0, H * .8}, .058, .02, 5, kBarkLow, kWhite);
    const V3 heart{0, 0, H * .5};
    const int n = 6 + g.below(2);
    for (int k = 0; k < n; ++k) {
        const double f = (k + .5) / n;
        const double r = std::max(.06, wide * std::pow(std::sin(M_PI * std::min(.97, .12 + f * .9)), .7));
        const V3 at{g.in(-.03, .03), g.in(-.03, .03), .22 + f * (H - .34)};
        add_clump(t.leaf, t.snow, g, at, r * g.in(.9, 1.1), r * g.in(.85, 1.05), r * 1.55, .3, heart, jitter_col(g, .06), 7, 4);
    }
}

// Willow: a leaning trunk forking low, a rounded head, and long hanging
// shoots that fall almost to the ground in ragged, separate strands.
void build_willow(TreeModel& t, R& g) {
    const double bole = g.in(.3, .4), tr = g.in(.08, .1), a = g.in(0, 2 * M_PI);
    const V3 top = polar(a, g.in(.12, .28)) * bole;
    add_limb(t.wood, {0, 0, -.05}, top, tr * 1.3, tr, 6, kBarkLow, kWhite);
    const int limbs = 2 + g.below(2);
    for (int k = 0; k < limbs; ++k)
        add_limb(t.wood, top, top + polar(a + k * 2.4, g.in(.35, .7)) * g.in(.24, .32), tr * .6, tr * .3, 5, kWhite, kWhite);
    const V3 heart = top + V3{0, 0, .3};
    const double R0 = g.in(.38, .46);
    for (int k = 0; k < 5; ++k) {
        const double b = k * 2 * M_PI / 5 + g.in(-.3, .3), d = k ? R0 * .42 : 0;
        const double cr = k ? g.in(.2, .25) : .26;
        add_clump(t.leaf, t.snow, g, heart + V3{std::cos(b) * d, std::sin(b) * d, k ? g.in(-.04, .04) : .1}, cr * 1.1, cr, cr * .8, .3, heart, jitter_col(g, .05));
    }
    // strands: narrow hanging blades around the head, each its own length
    const int N = 20;
    const double top_z = heart.z + .02;
    for (int k = 0; k < N; ++k) {
        const double b = 2 * M_PI * (k + g.in(-.15, .15)) / N, half = M_PI / N * g.in(.8, 1.0);
        const double reach = R0 * g.in(.92, 1.08), low = std::max(.04, top_z - g.in(.4, .62) * (k % 3 == 0 ? .72 : 1));
        const double b0 = b - half, b1 = b + half;
        const V3 t0{heart.x + std::cos(b0) * reach * .82, heart.y + std::sin(b0) * reach * .82, top_z};
        const V3 t1{heart.x + std::cos(b1) * reach * .82, heart.y + std::sin(b1) * reach * .82, top_z};
        const V3 tip{heart.x + std::cos(b) * reach * 1.06, heart.y + std::sin(b) * reach * 1.06, low};
        const V3 m0 = (t0 + tip) * .5 + V3{std::cos(b) * .03, std::sin(b) * .03, 0}, m1 = (t1 + tip) * .5 + V3{std::cos(b) * .03, std::sin(b) * .03, 0};
        const V3 n0 = norm({std::cos(b0), std::sin(b0), .35}), n1 = norm({std::cos(b1), std::sin(b1), .35}), nt = norm({std::cos(b), std::sin(b), .2});
        const double s0 = 3.0 * k / N, s1 = s0 + .15;
        const Vtx vt0{t0, n0, s0, 0, scol(kWhite, .98)}, vt1{t1, n1, s1, 0, scol(kWhite, .98)};
        const Vtx vm0{m0, n0, s0, 1.1, scol(kWhite, .85)}, vm1{m1, n1, s1, 1.1, scol(kWhite, .85)};
        const Vtx vtip{tip, nt, (s0 + s1) * .5, 2.2, scol(kWhite, .7)};
        tri(t.leaf, vm0, vm1, vt1);
        tri(t.leaf, vm0, vt1, vt0);
        tri(t.leaf, vtip, vm1, vm0);
    }
}

// Rowan: a small, often many-stemmed tree with a light crown and bunches of
// orange-red berries.
void build_rowan(TreeModel& t, R& g) {
    const int stems = 1 + g.below(3);
    const double a0 = g.in(0, 2 * M_PI);
    V3 heart{0, 0, .7};
    std::vector<V3> tops;
    for (int s = 0; s < stems; ++s) {
        const double a = a0 + s * 2.2;
        V3 pts[3];
        pts[0] = {0, 0, -.04};
        pts[1] = pts[0] + polar(a + g.in(-.4, .4), g.in(.1, .32)) * g.in(.22, .3);
        pts[2] = pts[1] + polar(a + g.in(-.6, .6), g.in(0, .35)) * g.in(.22, .3);
        add_stem(t.wood, pts, 3, .04, .016, 5, kBarkLow, kWhite);
        tops.push_back(pts[2]);
    }
    heart = tops[0] + V3{0, 0, -.05};
    const Col berry[2] = {hex(0xE0461E), hex(0xC8301A)};
    for (size_t s = 0; s < tops.size(); ++s) {
        const int n = 3 + g.below(2);
        for (int k = 0; k < n; ++k) {
            const double b = g.in(0, 2 * M_PI), d = k ? g.in(.08, .15) : 0;
            const double cr = g.in(.14, .19);
            const V3 at = tops[s] + V3{std::cos(b) * d, std::sin(b) * d, k ? g.in(-.1, .05) : .04};
            add_clump(t.leaf, t.snow, g, at, cr * 1.1, cr, cr * .82, .4, heart, jitter_col(g, .07), 6, 4);
            if (g.uni() < .7) {
                const V3 dir = norm(V3{std::cos(b), std::sin(b), g.in(-.2, .6)});
                const V3 bunch = at + dir * (cr * .95);
                const Col bc = berry[g.below(2)];
                for (int q = 0; q < 5; ++q) add_bead(t.fruit, bunch + V3{g.in(-.03, .03), g.in(-.03, .03), g.in(-.03, .01)}, .02, bc);
            }
        }
    }
}

// Norway spruce, silver fir: whorled tiers on a straight leader. Spruce is
// narrow with drooping skirts; fir is broader, flatter and more regular.
void build_spire(TreeModel& t, R& g, bool fir) {
    const double H = fir ? g.in(1.3, 1.7) : g.in(1.55, 2.05), R0 = fir ? g.in(.36, .46) : g.in(.29, .38), z0 = g.in(.08, .18);
    add_limb(t.wood, {0, 0, -.04}, {0, 0, H * .9}, .055, .015, 5, kBarkLow, kWhite);
    const int n = (fir ? 8 : 9) + g.below(4);
    const Col col = kWhite;
    for (int i = 0; i < n; ++i) {
        const double f = static_cast<double>(i) / (n - 1);
        const double z = z0 + (H - z0 - (fir ? .26 : .3)) * f;
        const double r = std::max(.07, R0 * std::pow(1 - f * .93, fir ? .8 : .95) * g.in(.88, 1.08));
        const double th = (H - z0) / n * (fir ? 2.1 : 2.5);
        const double droop = fir ? g.in(.06, .18) : g.in(.38, .58) * (1.1 - .4 * f);
        add_tier(t.leaf, t.snow, g, {g.in(-.01, .01), g.in(-.01, .01), z}, r, th, droop, 7, g.in(0, 2 * M_PI), fir ? .5 : 1.0, scol(col, .9 + .1 * f));
    }
    add_tier(t.leaf, t.snow, g, {0, 0, H - (fir ? .22 : .3)}, .055, fir ? .2 : .3, 0, 4, 0, .3, kWhite);
}

// Scots pine: a tall, bending, orange-barked trunk, bare below, with a flat,
// irregular crown of a few dense cushions held out on upswept branches.
void build_pine(TreeModel& t, R& g) {
    const double H = g.in(1.35, 1.75);
    V3 pts[4];
    pts[0] = {0, 0, -.04};
    for (int k = 1; k < 4; ++k) pts[k] = pts[k - 1] + V3{g.in(-.07, .07), g.in(-.07, .07), H * (k == 3 ? .22 : .34)};
    add_stem(t.wood, pts, 4, .065, .026, 6, scol(kWhite, .62), scol(kWhite, 1.15));
    const V3 heart = pts[3] + V3{0, 0, -.05};
    const int n = 3 + g.below(3);
    const double a0 = g.in(0, 2 * M_PI);
    for (int k = 0; k < n; ++k) {
        const double f = g.in(.62, .95);
        const double seg = f * 3;
        const int i = std::min(2, static_cast<int>(seg));
        const V3 from = pts[i] + (pts[i + 1] - pts[i]) * (seg - i);
        const V3 end = from + polar(a0 + k * 2.39996 + g.in(-.4, .4), g.in(.75, 1.2)) * (g.in(.16, .3) * (1.3 - f));
        add_limb(t.wood, from, end, .024, .011, 4, kWhite, kWhite);
        const double cr = g.in(.17, .25);
        add_clump(t.leaf, t.snow, g, end + V3{0, 0, .05}, cr * g.in(1, 1.2), cr, cr * .52, .32, heart, jitter_col(g, .06));
    }
    add_clump(t.leaf, t.snow, g, pts[3] + V3{0, 0, .06}, .2, .18, .11, .3, heart, jitter_col(g, .05));
    if (g.uni() < .6) {  // a dead stub lower down
        const V3 from = pts[1] + (pts[2] - pts[1]) * g.in(0, .8);
        add_limb(t.wood, from, from + polar(g.in(0, 2 * M_PI), g.in(1.1, 1.5)) * g.in(.08, .15), .016, .006, 3, kWhite, kWhite);
    }
}

// Larch: a straight stem with sparse, star-shaped whorls of fine branches,
// gaps between them; soft green in summer, gold in autumn.
void build_larch(TreeModel& t, R& g) {
    const double H = g.in(1.4, 1.8);
    const V3 tip{g.in(-.04, .04), g.in(-.04, .04), H};
    add_limb(t.wood, {0, 0, -.04}, tip, .05, .012, 5, kBarkLow, kWhite);
    const int n = 7 + g.below(3);
    const double z0 = g.in(.24, .34);
    for (int i = 0; i < n; ++i) {
        const double f = static_cast<double>(i) / (n - 1);
        const double z = z0 + (H - z0 - .12) * f;
        const double r = std::max(.07, .42 * std::pow(1 - f * .9, 1.1) * g.in(.8, 1.12));
        const double gap = (H - z0) / n;
        add_tier(t.leaf, t.snow, g, tip * (z / H) + V3{0, 0, -gap * .2}, r, gap * g.in(1.0, 1.25), g.in(.15, .3), 6 + g.below(2), g.in(0, 2 * M_PI), 1.0, kWhite, .5);
    }
}

// Wind-shaped pine near the treeline (krummholz): short, crooked, leaning
// downwind (+x here), its foliage flagged to the lee and its windward limbs
// bare.
void build_stone_pine(TreeModel& t, R& g) {
    const double H = g.in(.55, .85);
    V3 pts[3];
    pts[0] = {0, 0, -.04};
    pts[1] = {g.in(.02, .08), g.in(-.04, .04), H * .42};
    pts[2] = pts[1] + V3{g.in(.06, .14), g.in(-.05, .05), H * .45};
    add_stem(t.wood, pts, 3, .08, .035, 6, kBarkLow, kWhite);
    for (int k = 0; k < 1 + g.below(2); ++k) {  // bare limbs on the windward side
        const V3 from = pts[1] + (pts[2] - pts[1]) * g.in(0, .8);
        add_limb(t.wood, from, from + V3{-g.in(.12, .22), g.in(-.08, .08), g.in(.0, .08)}, .02, .007, 3, kWhite, kWhite);
    }
    const V3 heart = pts[2] + V3{.05, 0, -.08};
    const int n = 4 + g.below(3);
    for (int k = 0; k < n; ++k) {
        const double f = g.in(.25, 1.0);
        const V3 on = f < .5 ? pts[0] + (pts[1] - pts[0]) * (f * 2) : pts[1] + (pts[2] - pts[1]) * (f * 2 - 1);
        const double cr = g.in(.15, .22);
        add_clump(t.leaf, t.snow, g, on + V3{g.in(.02, .22), g.in(-.12, .12), g.in(.0, .06)}, cr * 1.15, cr * .85, cr * .6, .35, heart, jitter_col(g, .06));
    }
}

// Juniper: a low, many-stemmed, spreading shrub-tree.
void build_juniper(TreeModel& t, R& g) {
    const int stems = 4 + g.below(3);
    const V3 heart{0, 0, .22};
    for (int s = 0; s < stems; ++s) {
        const double a = s * 2 * M_PI / stems + g.in(-.4, .4);
        V3 pts[3];
        pts[0] = {0, 0, -.03};
        pts[1] = pts[0] + polar(a, g.in(.45, .9)) * g.in(.14, .24);
        pts[2] = pts[1] + polar(a + g.in(-.5, .5), g.in(.3, 1.0)) * g.in(.12, .22);
        add_stem(t.wood, pts, 3, .03, .012, 4, kBarkLow, kWhite);
        const double cr = g.in(.1, .15);
        add_clump(t.leaf, t.snow, g, pts[2], cr * 1.1, cr, cr * .85, .45, heart, jitter_col(g, .07), 6, 4);
        if (g.uni() < .6) add_clump(t.leaf, t.snow, g, (pts[1] + pts[2]) * .5 + V3{0, 0, .03}, cr * .8, cr * .75, cr * .7, .45, heart, jitter_col(g, .07), 6, 3);
    }
}

// Dead snag: a bleached, crooked trunk with a broken top and a few stubs.
void build_snag(TreeModel& t, R& g) {
    const double H = g.in(.8, 1.35);
    V3 pts[4];
    pts[0] = {0, 0, -.04};
    for (int k = 1; k < 4; ++k) pts[k] = pts[k - 1] + V3{g.in(-.07, .07), g.in(-.07, .07), H / 3};
    add_stem(t.wood, pts, 4, .09, .042, 6, kBarkLow, kWhite);
    add_limb(t.wood, pts[3], pts[3] + V3{g.in(-.04, .04), g.in(-.04, .04), g.in(.08, .16)}, .038, .006, 4, kWhite, kWhite);  // the broken tip
    const int stubs = 2 + g.below(3);
    for (int k = 0; k < stubs; ++k) {
        const double f = g.in(.35, .92);
        const double seg = f * 3;
        const int i = std::min(2, static_cast<int>(seg));
        const V3 from = pts[i] + (pts[i + 1] - pts[i]) * (seg - i);
        const double a = g.in(0, 2 * M_PI);
        const V3 end = from + polar(a, g.in(.55, 1.25)) * g.in(.14, .32);
        add_limb(t.wood, from, end, .03, .009, 4, kWhite, kWhite);
        if (g.uni() < .4) add_limb(t.wood, (from + end) * .5, (from + end) * .5 + polar(a + g.in(-.9, .9), g.in(.2, .6)) * g.in(.06, .12), .01, .004, 3, kWhite, kWhite);
    }
}

void finish(TreeModel& t) {
    double crown = .12, height = .2;
    for (const Vtx& v : t.leaf.empty() ? t.wood : t.leaf) crown = std::max(crown, std::hypot(v.p.x, v.p.y));
    for (const Vtx& v : t.wood) height = std::max(height, v.p.z);
    for (const Vtx& v : t.leaf) height = std::max(height, v.p.z);
    t.crown = crown;
    t.height = height;
}

TreeModel build_tree(TreeKind kind, std::uint64_t seed) {
    TreeModel t;
    R g(seed);
    switch (kind) {
        case TreeKind::oak: build_oak(t, g); break;
        case TreeKind::beech: build_dome(t, g, false); break;
        case TreeKind::maple: build_dome(t, g, true); break;
        case TreeKind::birch: build_birch(t, g); break;
        case TreeKind::poplar: build_poplar(t, g); break;
        case TreeKind::willow: build_willow(t, g); break;
        case TreeKind::rowan: build_rowan(t, g); break;
        case TreeKind::spruce: build_spire(t, g, false); break;
        case TreeKind::fir: build_spire(t, g, true); break;
        case TreeKind::pine: build_pine(t, g); break;
        case TreeKind::larch: build_larch(t, g); break;
        case TreeKind::stone_pine: build_stone_pine(t, g); break;
        case TreeKind::juniper: build_juniper(t, g); break;
        case TreeKind::snag: case TreeKind::count: build_snag(t, g); break;
    }
    finish(t);
    return t;
}

// a few dark and light needle strokes over a mottled base
void needle_tex(Tex& t, Col dark, Col light, Col tip, std::uint64_t seed) {
    noise_tex(t, 64, dark, light, seed, 4, 1.5f, 0, tip);
    R g(seed * 7 + 1);
    for (int i = 0; i < 420; ++i) {
        const int x = g.below(64), y = g.below(64), len = 2 + g.below(3);
        const bool lit = g.uni() < .45;
        const Col c = lit ? tip : shade(dark, .7f);
        const std::uint32_t v = 0xFF000000u | (static_cast<std::uint32_t>(c.r * 255) << 16) | (static_cast<std::uint32_t>(c.g * 255) << 8) | static_cast<std::uint32_t>(c.b * 255);
        for (int k = 0; k < len; ++k) t.at(x + k * (i % 2 ? 1 : -1), y + k) = v;
    }
    t.build_mips();
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
        // bark: brown, dark, grey, birch, reddish pine, mossy, silver, banded cherry, smooth beech, larch
        const unsigned bark[kBarks][3] = {{0x3E2A1A, 0x7A5636, 0x24170D}, {0x2A1E14, 0x4E3A28, 0x140E08}, {0x5E5A54, 0x8E8880, 0x3A3632},
                                          {0xD8D4CC, 0xF4F2EE, 0x2A2826}, {0x6A3420, 0xA85A34, 0x3A1A10}, {0x3A4426, 0x5E6A3A, 0x22281A},
                                          {0x8A8E92, 0xB8BCC0, 0x5A5E62}, {0x5A2A22, 0x8A4A3A, 0xC08A70}, {0x6E665A, 0x968C7E, 0x564C42},
                                          {0x4A2C1E, 0x7E4E34, 0x2A170F}};
        for (int i = 0; i < kBarks; ++i) noise_tex(f.bark[static_cast<size_t>(i)], 32, hex(bark[i][0]), hex(bark[i][1]), 300 + i, 8, i == 8 ? .8f : 1.6f, i == 3 ? 14 : 4, hex(bark[i][2]));
        // tree foliage: summer broadleaves 0-6, needles 7-12, autumn 13-19
        const unsigned fol[kFoliage][3] = {
            {0x1C3A16, 0x4A7628, 0x7EA448}, {0x28501A, 0x6A9A34, 0xA2CA5E}, {0x3A5A1A, 0x88AA3E, 0xC6DC6E}, {0x1E4218, 0x527E2C, 0x86AE4A},
            {0x46601E, 0x8EAA44, 0xC6D678}, {0x2A4A16, 0x5E8C2C, 0x96C04E}, {0x2C4C1C, 0x668C34, 0x9CBE5C},
            {0x0E2418, 0x22442C, 0x3E6A42}, {0x10262A, 0x284A4A, 0x4E7872}, {0x1C3224, 0x3C5C44, 0x6C8C6A}, {0x31501C, 0x6A9036, 0x9CBC5C},
            {0x142A1C, 0x2C4A32, 0x4A6C4A}, {0x1C3430, 0x3C5A54, 0x7C9C94},
            {0x6A1010, 0xC42A1C, 0xF0663A}, {0x7A2E0A, 0xD4701E, 0xF8AC44}, {0x7A5A0A, 0xD6A82A, 0xF8E274}, {0x5A2A10, 0xA4582A, 0xD88E4C},
            {0x3E2A10, 0x7A5A24, 0xAA8640}, {0x6A4610, 0xB48428, 0xD8AC4C}, {0x4A5A14, 0x9EA830, 0xD8D262}};
        for (int i = 0; i < kFoliage; ++i) {
            if ((i >= 7 && i <= 12) || i == 18) needle_tex(f.foliage[static_cast<size_t>(i)], hex(fol[i][0]), hex(fol[i][1]), hex(fol[i][2]), 700 + static_cast<std::uint64_t>(i));
            else noise_tex(f.foliage[static_cast<size_t>(i)], 64, hex(fol[i][0]), hex(fol[i][1]), 700 + static_cast<std::uint64_t>(i), 8, 1.7f, 110, hex(fol[i][2]));
        }
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
        // trees: every species in kTreeForms grown forms, each its own skeleton
        f.tree_models.reserve(static_cast<size_t>(kTreeKinds * kTreeForms));
        for (int k = 0; k < kTreeKinds; ++k)
            for (int i = 0; i < kTreeForms; ++i)
                f.tree_models.push_back(build_tree(static_cast<TreeKind>(k), 0x7EE5000ULL + static_cast<std::uint64_t>(k * 64 + i)));
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
            const double lum = g.in(.86, 1.08), warm = g.in(-.03, .03);
            k.tint = {static_cast<float>(lum * (1 + warm)), static_cast<float>(lum), static_cast<float>(lum * (1 - warm)), 1};
            k.moss = g.uni() < .25;
        }
        return f;
    }();
    return F;
}

namespace {
// How common each species is (oak, beech, birch, poplar, willow, maple, rowan,
// spruce, fir, pine, larch, stone pine, juniper, snag) by biome, for the
// broadleaf and conifer sites on the path. The mountainside mixes both.
// Broadleaves thin out with altitude; above the treeline only wind-shaped
// pines, junipers and dead snags remain.
const int kBroadWeights[static_cast<int>(Biome::count)][kTreeKinds] = {
    {5, 2, 2, 1, 0, 1, 2, 0, 0, 0, 0, 0, 0, 0},   // meadow
    {3, 4, 2, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},   // forest
    {2, 3, 2, 0, 0, 4, 1, 0, 0, 0, 1, 0, 0, 0},   // autumn wood
    {1, 0, 2, 2, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0},   // pond country
    {0, 0, 1, 0, 0, 0, 1, 0, 0, 2, 0, 0, 2, 1},   // rocky ravine
    {0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0},   // alpine slopes
    {0, 0, 0, 0, 0, 0, 0, 4, 1, 0, 0, 2, 0, 1},   // snowfield
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 2},   // ice falls
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 1, 2},   // windy ridge
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 1}};  // summit
const int kConiferWeights[static_cast<int>(Biome::count)][kTreeKinds] = {
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 3, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 4, 2, 2, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 2, 0, 1, 3, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 2, 2},
    {0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 3, 2, 0, 1},
    {0, 0, 0, 0, 0, 0, 0, 4, 1, 0, 0, 2, 0, 1},
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 2},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 1, 2},
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 1}};
constexpr double kWindYaw = 2.5;  // prevailing wind on the heights: wind-shaped trees all flag this way

TreeKind pick_kind(Biome b, TreeSite site, double x) {
    const int bi = static_cast<int>(b);
    int w[kTreeKinds];
    int total = 0;
    for (int k = 0; k < kTreeKinds; ++k) {
        w[k] = (site == TreeSite::conifer ? 0 : kBroadWeights[bi][k]) + (site == TreeSite::broadleaf ? 0 : kConiferWeights[bi][k]);
        total += w[k];
    }
    if (total == 0) return TreeKind::spruce;
    int pick = std::min(total - 1, static_cast<int>(x * total));
    for (int k = 0; k < kTreeKinds; ++k) {
        if (pick < w[k]) return static_cast<TreeKind>(k);
        pick -= w[k];
    }
    return TreeKind::spruce;
}

int summer_foliage(TreeKind k) {
    switch (k) {
        case TreeKind::oak: return 0;
        case TreeKind::beech: return 1;
        case TreeKind::birch: return 2;
        case TreeKind::poplar: return 3;
        case TreeKind::willow: return 4;
        case TreeKind::maple: return 5;
        case TreeKind::rowan: return 6;
        case TreeKind::spruce: return 7;
        case TreeKind::fir: return 8;
        case TreeKind::pine: return 9;
        case TreeKind::larch: return 10;
        case TreeKind::stone_pine: return 11;
        case TreeKind::juniper: return 12;
        case TreeKind::snag: case TreeKind::count: return 0;
    }
    return 0;
}

int autumn_foliage(TreeKind k, R& g) {
    switch (k) {
        case TreeKind::maple: { const int c[4] = {13, 14, 13, 15}; return c[g.below(4)]; }
        case TreeKind::beech: { const int c[3] = {16, 14, 15}; return c[g.below(3)]; }
        case TreeKind::birch: case TreeKind::poplar: return g.uni() < .75 ? 15 : 19;
        case TreeKind::oak: return g.uni() < .6 ? 17 : 16;
        case TreeKind::rowan: return g.uni() < .6 ? 13 : 14;
        case TreeKind::willow: return 19;
        case TreeKind::larch: return 18;
        default: return summer_foliage(k);
    }
}

int bark_for(TreeKind k, R& g) {
    switch (k) {
        case TreeKind::oak: return 0;
        case TreeKind::beech: case TreeKind::maple: case TreeKind::rowan: return 8;
        case TreeKind::birch: return 3;
        case TreeKind::poplar: case TreeKind::willow: case TreeKind::fir: return 2;
        case TreeKind::spruce: case TreeKind::stone_pine: return 1;
        case TreeKind::pine: return 4;
        case TreeKind::larch: return 9;
        case TreeKind::juniper: return 7;
        case TreeKind::snag: case TreeKind::count: return g.uni() < .7 ? 6 : 2;
    }
    return 0;
}

}  // namespace

TreeLook tree_look(Biome b, TreeSite site, double u, double v, unsigned hash) {
    TreeLook t;
    R g(static_cast<std::uint64_t>(hash) * 0x9E3779B97F4A7C15ULL + 0x51ED);
    // stands: within a few tiles most trees share a species, as real woods do
    const std::int64_t cu = static_cast<std::int64_t>(std::floor((u + 64) / 3.5)), cv = static_cast<std::int64_t>(std::floor(v / 4.5));
    const std::uint64_t cell = mix64(static_cast<std::uint64_t>(cu) * 0x9E3779B97F4A7C15ULL ^ static_cast<std::uint64_t>(cv) * 0xC2B2AE3D27D4EB4FULL ^
                                     (static_cast<std::uint64_t>(b) << 40) ^ (static_cast<std::uint64_t>(site) << 48));
    const double stand = static_cast<double>(cell >> 11) * (1.0 / 9007199254740992.0);
    t.kind = pick_kind(b, site, g.uni() < .6 ? stand : g.uni());
    t.form = g.below(kTreeForms);
    // age: most trees are grown, some young, a few saplings; trees shrink toward the treeline
    t.size = .86 + .5 * std::sqrt(g.uni());
    const bool sapling = g.uni() < .09;
    if (sapling) t.size = g.in(.5, .7);
    double alt = 1;
    switch (b) {
        case Biome::ravine: case Biome::alpine: alt = .9; break;
        case Biome::snow: alt = .86; break;
        case Biome::ice: case Biome::ridge: case Biome::summit: alt = .74; break;
        default: break;
    }
    t.size *= alt;
    t.girth = g.in(.86, 1.16) * (sapling ? .9 : 1);
    t.yaw = g.in(0, 2 * M_PI);
    t.lean_dir = g.in(0, 2 * M_PI);
    t.lean = g.uni() < .18 ? g.in(.08, .17) : g.in(0, .06);
    if (t.kind == TreeKind::birch) t.lean += .04;
    const bool windy = b == Biome::alpine || b == Biome::snow || b == Biome::ice || b == Biome::ridge || b == Biome::summit;
    if (windy) {
        t.lean_dir = kWindYaw + g.in(-.45, .45);
        t.lean = g.in(.04, .12);
    }
    if (t.kind == TreeKind::stone_pine) {
        t.yaw = kWindYaw + g.in(-.35, .35);
        t.lean_dir = kWindYaw;
    }
    const double k = g.in(.86, 1.1);
    t.tint = {static_cast<float>(k * g.in(.94, 1.06)), static_cast<float>(k * g.in(.96, 1.04)), static_cast<float>(k * g.in(.93, 1.06)), 1};
    t.bark = bark_for(t.kind, g);
    t.foliage = summer_foliage(t.kind);
    // autumn colours: the autumn wood, and the first turning leaves up on the alpine slopes
    if (b == Biome::autumn) t.foliage = g.uni() < .12 ? (g.uni() < .5 ? 19 : summer_foliage(t.kind)) : autumn_foliage(t.kind, g);
    else if (b == Biome::alpine && g.uni() < .35) t.foliage = autumn_foliage(t.kind, g);
    return t;
}

double tree_crown(const TreeLook& t, double scale) {
    const TreeModel& m = flora().tree_models[static_cast<size_t>(static_cast<int>(t.kind) * kTreeForms + t.form)];
    return m.crown * t.size * t.girth * scale;
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

void draw_tree(R3D& r, const TreeLook& t, double x, double y, double z, double sway, double scale, bool snowy, std::uint8_t mat, Col fade) {
    const Flora& F = flora();
    const TreeModel& m = F.tree_models[static_cast<size_t>(static_cast<int>(t.kind) * kTreeForms + t.form)];
    const double s = t.size * scale, w = s * t.girth;
    // the wind bends the whole tree about its foot; lean and turn are its own
    const M34 model = g_tree_pre * at(x, y, z) * M34::rot_y(sway / std::max(.4, m.height * s)) * M34::rot_z(t.lean_dir) * M34::rot_y(t.lean) *
                      M34::rot_z(t.yaw - t.lean_dir) * sc(w, w, s);
    draw_mesh(r, m.wood, model, &F.bark[static_cast<size_t>(t.bark)], fade, mat, 1);
    if (!m.leaf.empty()) draw_mesh(r, m.leaf, model, &F.foliage[static_cast<size_t>(t.foliage)], mulc(t.tint, fade), mat, 1);
    if (!m.fruit.empty()) draw_mesh(r, m.fruit, model, nullptr, fade, mat);
    if (snowy && !m.snow.empty()) draw_mesh(r, m.snow, model, nullptr, fade, mat);
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
    // bedded: about a fifth of the stone is below ground, as stones lie on a real slope
    const double cz = z + sz * .42;
    draw_mesh(r, F.rock_meshes[static_cast<size_t>(k.mesh)], at(x, y, cz) * M34::rot_z(k.mesh * .37) * sc(sx, sy, sz),
              &G.tex[static_cast<size_t>(k.ground)], mulc(k.tint, fade), mat, 1);
    // snow and moss settle into the top of the stone instead of sitting on it like a lid
    if (snowy) draw_mesh(r, F.blob_meshes[static_cast<size_t>(k.mesh % 6)], at(x, y, cz + sz * .62) * M34::rot_z(k.mesh * .9) * sc(sx * .62, sy * .58, sz * .34), &G.tex[g_snow], fade, mat, 1);
    else if (k.moss) draw_mesh(r, F.blob_meshes[static_cast<size_t>(k.mesh % 6)], at(x, y, cz + sz * .7) * M34::rot_z(k.mesh * .9) * sc(sx * .5, sy * .45, sz * .26), &G.tex[g_moss], fade, mat, 1);
}

}  // namespace eggy
