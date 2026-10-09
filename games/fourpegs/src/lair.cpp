#include "lair.hpp"

#include "platform/render.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace fp {

namespace {
const Col kWhite{1, 1, 1, 1};
const Col kNeon = hex(0x3FE6FF), kNeonDim = hex(0x1A5C78), kRed = hex(0xFF2B3A), kWall = hex(0x241626), kMetal = hex(0x3A3346);
const Col kWood = hex(0x2A1420), kGoldTrim = hex(0xC9963A);
// peg colours, each with its own symbol so colour is never required
const Col kPeg[kColors] = {hex(0xF0324A), hex(0xFFC93A), hex(0x46D46A), hex(0x2EA8FF), hex(0xB063FF), hex(0xF4F1EA)};
const Col kInkPeg = hex(0x10061A);

M34 at(V3 v) { return M34::translate(v.x, v.y, v.z); }
M34 sc(double x, double y, double z) { return M34::scale(x, y, z); }

Tex tex_from(const Canvas& c) {
    Tex t;
    t.make(c.w, c.h);
    for (size_t i = 0; i < t.px.size(); ++i) {
        const std::uint8_t* p = &c.px[i * 4];
        const unsigned a = p[3];
        auto un = [&](unsigned v) { return a ? std::min(255u, v * 255 / a) : 0u; };
        t.px[i] = a << 24 | un(p[2]) << 16 | un(p[1]) << 8 | un(p[0]);
    }
    t.build_mips();
    return t;
}

void symbol(Canvas& c, int color, double cx, double cy, double r, Col col) {
    c.begin();
    switch (color) {
        case 0: c.circle(cx, cy, r * .62); break;                                                    // dot
        case 1: c.move(cx, cy - r * .85); c.line(cx + r * .8, cy + r * .6); c.line(cx - r * .8, cy + r * .6); c.close(); break;  // triangle
        case 2: c.rect(cx - r * .62, cy - r * .62, r * 1.24, r * 1.24); break;                       // square
        case 3: c.move(cx, cy - r * .9); c.line(cx + r * .7, cy); c.line(cx, cy + r * .9); c.line(cx - r * .7, cy); c.close(); break;  // diamond
        case 4:                                                                                       // star
            for (int i = 0; i < 10; ++i) {
                const double a = -M_PI / 2 + i * M_PI / 5, rr = i % 2 ? r * .4 : r * .92;
                i == 0 ? c.move(cx + std::cos(a) * rr, cy + std::sin(a) * rr) : c.line(cx + std::cos(a) * rr, cy + std::sin(a) * rr);
            }
            c.close();
            break;
        default:                                                                                      // cross
            c.rect(cx - r * .8, cy - r * .22, r * 1.6, r * .44);
            c.rect(cx - r * .22, cy - r * .8, r * .44, r * 1.6);
            break;
    }
    c.fill(col);
}

struct Textures {
    Tex space, glass, glow, sym[kColors], check, check_lit, shine, rim, pin_exact, pin_near;
    Textures() {
        Canvas c;
        // space through the porthole: deep nebula, stars, a ringed planet, a small moon
        c.resize(256, 256);
        c.clear(hex(0x05030C));
        c.begin(); c.rect(0, 0, 256, 256); c.fill(Paint::rad(170, 90, 200, {{0, hex(0x3A1450)}, {.5f, hex(0x160A2A)}, {1, hex(0x05030C)}}));
        c.begin(); c.rect(0, 0, 256, 256); c.fill(Paint::rad(60, 190, 120, {{0, alpha(hex(0x8A1A3A), .55f)}, {1, alpha(hex(0x8A1A3A), 0)}}));
        std::uint32_t h = 12345;
        for (int i = 0; i < 260; ++i) {
            h = h * 1664525u + 1013904223u;
            const double x = (h >> 8) % 256, y = (h >> 16) % 256, b = ((h >> 4) % 100) / 100.0;
            c.fill_circle(x, y, b > .93 ? 1.3 : .6, alpha(hex(0xFFFFFF), static_cast<float>(.35 + .65 * b)));
        }
        // the planet he will doubtless threaten
        c.begin(); c.circle(150, 140, 46); c.fill(Paint::rad(135, 122, 60, {{0, hex(0x7FD0FF)}, {.55f, hex(0x2C6FB8)}, {1, hex(0x0B1A40)}}));
        c.begin(); c.ellipse(150, 140, 78, 14); c.stroke(alpha(hex(0xE8C98A), .8f), 3);
        c.begin(); c.ellipse(150, 140, 46, 46); c.stroke(alpha(hex(0xBFE6FF), .35f), 2);
        c.begin(); c.circle(70, 70, 14); c.fill(Paint::rad(66, 66, 16, {{0, hex(0xE8E2D4)}, {1, hex(0x6A6474)}}));
        space = tex_from(c);
        // the console glass: a dark panel with a fine neon grid and circuit traces
        c.resize(128, 128);
        c.clear(hex(0x070A18));
        for (int i = 0; i <= 128; i += 16) {
            c.fill_rect(i, 0, 1, 128, alpha(kNeonDim, .55f));
            c.fill_rect(0, i, 128, 1, alpha(kNeonDim, .55f));
        }
        for (int i = 0; i < 6; ++i) {
            c.begin(); c.move(8 + i * 20, 128); c.line(8 + i * 20, 96 - i * 6); c.line(28 + i * 16, 80 - i * 6); c.stroke(alpha(kNeon, .35f), 1);
        }
        glass = tex_from(c);
        // soft glow, a gloss highlight, a ring
        c.resize(32, 32);
        c.clear({0, 0, 0, 0});
        c.begin(); c.circle(16, 16, 16); c.fill(Paint::rad(16, 16, 16, {{0, {1, 1, 1, 1}}, {.3f, {1, 1, 1, .5f}}, {1, {1, 1, 1, 0}}}));
        glow = tex_from(c);
        c.clear({0, 0, 0, 0});
        c.fill_ellipse(12, 10, 6, 4, hex(0xFFFFFF, .95f));
        shine = tex_from(c);
        c.clear({0, 0, 0, 0});
        c.begin(); c.circle(16, 16, 13); c.stroke({1, 1, 1, 1}, 3);
        rim = tex_from(c);
        // feedback pins: a solid gold dot for "exact", a hollow white ring for "elsewhere"
        c.clear({0, 0, 0, 0});
        c.fill_circle(16, 16, 12, hex(0xFFD24A));
        c.begin(); c.circle(16, 16, 12); c.stroke(hex(0x6A4A00), 2);
        pin_exact = tex_from(c);
        c.clear({0, 0, 0, 0});
        c.begin(); c.circle(16, 16, 10); c.stroke(hex(0xFFFFFF), 5);
        pin_near = tex_from(c);
        // peg symbols, white with a dark rim, on transparent
        for (int k = 0; k < kColors; ++k) {
            c.clear({0, 0, 0, 0});
            symbol(c, k, 16, 16, 13, hex(0x10061A, .9f));
            symbol(c, k, 16, 16, 10.5, k == 5 ? hex(0x2A2030) : hex(0xFFFFFF));
            sym[k] = tex_from(c);
        }
        // the CHECK plate, idle and lit
        for (int lit = 0; lit < 2; ++lit) {
            c.resize(64, 32);
            c.clear({0, 0, 0, 0});
            c.begin(); c.rrect(1, 1, 62, 30, 6); c.fill(lit ? alpha(kNeon, .35f) : alpha(kNeonDim, .3f));
            c.begin(); c.rrect(1.5, 1.5, 61, 29, 6); c.stroke(lit ? kNeon : kNeonDim, 2);
            // CHECK in blocky segment letters
            const Col tc = lit ? hex(0xE8FDFF) : alpha(kNeon, .6f);
            const double x0 = 9, y0 = 9, w = 7, hh = 14, gap = 3;
            auto bar = [&](double x, double y, double ww, double h2) { c.fill_rect(x, y, ww, h2, tc); };
            double x = x0;
            bar(x, y0, w, 2); bar(x, y0, 2, hh); bar(x, y0 + hh - 2, w, 2); x += w + gap;                                  // C
            bar(x, y0, 2, hh); bar(x + w - 2, y0, 2, hh); bar(x, y0 + hh / 2 - 1, w, 2); x += w + gap;                      // H
            bar(x, y0, w, 2); bar(x, y0, 2, hh); bar(x, y0 + hh / 2 - 1, w - 1, 2); bar(x, y0 + hh - 2, w, 2); x += w + gap; // E
            bar(x, y0, w, 2); bar(x, y0, 2, hh); bar(x, y0 + hh - 2, w, 2); x += w + gap;                                  // C
            bar(x, y0, 2, hh); bar(x + 2, y0 + hh / 2 - 1, 2, 2); bar(x + 4, y0 + 2, 2, hh / 2 - 3); bar(x + 4, y0 + hh / 2 + 1, 2, hh / 2 - 1); bar(x + w - 1, y0, 2, 3); bar(x + w - 1, y0 + hh - 3, 2, 3);  // K
            (lit ? check_lit : check) = tex_from(c);
        }
    }
};
const Textures& tx() {
    static Textures t;
    return t;
}

// The turn readout: "TURN" and a big segment number, redrawn when it changes.
const Tex& readout_tex(int turn, int turns, double alarm) {
    static std::map<int, Tex> cache;
    const int key = turn * 100 + turns * 2 + (alarm > .5 ? 1 : 0);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    Canvas c;
    c.resize(64, 32);
    c.clear({0, 0, 0, 0});
    const Col col = alarm > .5 ? hex(0xFF5A5A) : kNeon;
    c.begin(); c.rrect(1, 1, 62, 30, 6); c.fill(alpha(col, .18f));
    c.begin(); c.rrect(1.5, 1.5, 61, 29, 6); c.stroke(alpha(col, .8f), 1.5);
    // a seven-segment number
    static const int seg[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    auto digit = [&](int d, double x, double y, double w, double h) {
        const int m = seg[d % 10];
        const double t = 2.5;
        if (m & 1) c.fill_rect(x + t, y, w - 2 * t, t, col);
        if (m & 2) c.fill_rect(x + w - t, y + t, t, h / 2 - t, col);
        if (m & 4) c.fill_rect(x + w - t, y + h / 2, t, h / 2 - t, col);
        if (m & 8) c.fill_rect(x + t, y + h - t, w - 2 * t, t, col);
        if (m & 16) c.fill_rect(x, y + h / 2, t, h / 2 - t, col);
        if (m & 32) c.fill_rect(x, y + t, t, h / 2 - t, col);
        if (m & 64) c.fill_rect(x + t, y + h / 2 - t / 2, w - 2 * t, t, col);
    };
    const int left = std::max(0, turns - turn + 1);
    // turns left, large: the number that matters
    if (left >= 10) { digit(1, 18, 6, 11, 20); digit(0, 33, 6, 11, 20); }
    else digit(left, 26, 6, 11, 20);
    // a small row of ticks underneath: used turns
    for (int i = 0; i < turns; ++i) c.fill_rect(6 + i * 5.3, 28, 3, 2, i < turn - 1 ? alpha(col, .25f) : col);
    return cache.emplace(key, tex_from(c)).first->second;
}

void quad(R3D& r, V3 a, V3 b, V3 c, V3 d, const Tex* tex, Col col, std::uint16_t mat, double s1 = 1, double t1 = 1) {
    const V3 n = norm(cross(b - a, d - a));
    Vtx v[6] = {{a, n, 0, t1, col}, {b, n, s1, t1, col}, {c, n, s1, 0, col}, {a, n, 0, t1, col}, {c, n, s1, 0, col}, {d, n, 0, 0, col}};
    r.draw(v, 6, tex, mat);
}

void slab(R3D& r, V3 lo, V3 hi, Col c, std::uint16_t mat = opaque, double ink = 0, Col ink_col = kInkPeg) {
    const V3 ctr{(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z};
    const M34 m = at(ctr) * sc((hi.x - lo.x) / 2, (hi.y - lo.y) / 2, hi.z - lo.z);
    draw_mesh(r, box_mesh(), m, nullptr, c, mat);
    if (ink > 0) draw_outline(r, box_mesh(), m, ink, ink_col);
}

// a flat decal lying on the desk, centred at p
void decal(R3D& r, V3 p, double w, double h, const Tex* tex, Col col, std::uint16_t mat) {
    quad(r, p + V3{-w / 2, -h / 2, 0}, p + V3{w / 2, -h / 2, 0}, p + V3{w / 2, h / 2, 0}, p + V3{-w / 2, h / 2, 0}, tex, col, mat);
}
}  // namespace

void Lair::resize(int w, int h) {
    // Drawn at kSamples x kSamples per game pixel and averaged down in present: edges get
    // in-between shades instead of stairs. Everything outside speaks in game pixels.
    r.resize(w * kSamples, h * kSamples);
    w *= kSamples;
    h *= kSamples;
    r.yaw = 0;
    r.pitch = .3;
    r.persp = 7;
    r.scale = std::min(w / 6.3, h / 4.15);
    r.ax = .5;
    r.ay = .47;
    r.target = {0, -.1, 2.0};
    r.light.sun = norm({-.5, -.75, .7});
    r.light.sun_col = {.52f, .5f, .48f, 1};
    r.light.amb_col = {.56f, .5f, .52f, 1};
    r.light.fog_near = 1e9;
    r.light.fog_far = 2e9;
    r.light.toon_edge = .1f;
    r.light.toon_soft = .1f;
    r.set_camera();
}

void Lair::to_screen(V3 p, double& sx, double& sy) const {
    double sz;
    r.project(p, sx, sy, sz);
    sx /= kSamples;
    sy /= kSamples;
}

void Lair::present(Canvas& out) const {
    r.present_supersampled(out, kSamples, true);
}

namespace {
// how far a piece has fallen: it lets go at `start` (of the doom timeline) and drops under gravity
double fall(double doom, double start, double len = .35) {
    const double u = std::clamp((doom - start) / len, 0.0, 1.0);
    return u * u;
}
}  // namespace

void Lair::draw_room(const LairState& s, double t) {
    // fill the backdrop with the wall colour (everything else is drawn in 3D)
    const double d = s.doom;
    const float alarm = static_cast<float>(std::max(s.console.alarm, d) * (.6 + .4 * std::sin(t * (6 + 10 * d))));
    for (int y = 0; y < r.H; ++y) {
        const Col c = mix(hex(0x1A0E1E), hex(0x2C1424), static_cast<float>(y) / r.H);
        const Col c2 = mix(mix(c, hex(0x5A0E18), alarm * .5f), hex(0x3A0606), static_cast<float>(d));
        float* o = r.rgb.data() + static_cast<size_t>(y) * r.W * 3;
        for (int x = 0; x < r.W; ++x, o += 3) { o[0] = c2.r; o[1] = c2.g; o[2] = c2.b; }
    }
    const double wy = 3.6, cz = 2.55, R = 2.05;
    // back wall with the porthole cut-out look: wall quad, then space disc, then frame
    quad(r, {-7, wy + .05, -1}, {7, wy + .05, -1}, {7, wy + .05, 6}, {-7, wy + .05, 6}, nullptr, mix(kWall, hex(0x4A0E1A), alarm * .6f),
         static_cast<std::uint16_t>(unlit | double_sided));
    {
        // the view of space: a disc facing the room
        const M34 m = at({0, wy, cz}) * M34::rot_x(M_PI / 2) * sc(R, R, 1);
        Mesh d = disc_mesh(40);
        for (Vtx& v : d) { v.s = .5 + v.p.x * .5; v.t = .5 - v.p.y * .5; }
        // as his plan goes through, the sky outside burns red
        draw_mesh(r, d, m, &tx().space, mix(kWhite, hex(0xFF5030), static_cast<float>(s.doom * .7)), static_cast<std::uint16_t>(unlit | double_sided));
        // slow drifting stars in front of the texture? keep it calm: a faint glass sheen
        draw_mesh(r, disc_mesh(40), at({0, wy - .02, cz}) * M34::rot_x(M_PI / 2) * sc(R, R, 1), nullptr, alpha(hex(0x8AC8FF), .06f),
                  static_cast<std::uint16_t>(translucent | unlit | double_sided));
    }
    // a heavy riveted frame with six spokes, like a vault (it tears loose and falls in the collapse)
    {
        const double f = fall(d, .55, .4);
        const M34 ring = at({0, wy - .05 - f * 1.5, cz - f * 7}) * M34::rot_y(f * .8) * M34::rot_x(M_PI / 2 + f * .6) * sc(R + .1, R + .1, R + .1);
        draw_mesh(r, torus_mesh(40, 8, .07), ring, nullptr, kMetal, toon);
        draw_outline(r, torus_mesh(40, 8, .07), ring, .03, hex(0x0A050E));
    }
    for (int i = 0; i < 6; ++i) {
        const double a = i * M_PI / 3 + M_PI / 6;
        const double f = fall(d, .35 + .04 * i, .35);
        const V3 dir{std::cos(a), 0, std::sin(a)};
        const M34 sp = at(V3{0, wy - .08 - f, cz - f * 6} + dir * (R * .5 + .05)) * M34::rot_y(-a + M_PI / 2 + f * (i % 2 ? 2.0 : -2.5)) * sc(.05, .05, R * .5 - .05);
        draw_mesh(r, box_mesh(), sp, nullptr, kMetal, toon);
    }
    // the porthole glass cracks, spreading from one point
    if (d > .12) {
        const V3 c0{.6, wy - .03, cz + .5};
        const int n = static_cast<int>(std::min(9.0, (d - .12) * 30));
        std::uint32_t hsh = 77;
        for (int i = 0; i < n; ++i) {
            V3 p = c0;
            double ang = i * 2.4;
            for (int k = 0; k < 4; ++k) {
                hsh = hsh * 1664525u + 1013904223u;
                ang += ((hsh >> 16) % 100 - 50) / 120.0;
                const V3 q = p + V3{std::cos(ang), 0, std::sin(ang)} * (.25 + .1 * k);
                const V3 side = norm(cross(q - p, V3{0, 1, 0})) * .012;
                quad(r, p - side, q - side, q + side, p + side, nullptr, hex(0xF0F4FF), static_cast<std::uint16_t>(unlit | double_sided));
                p = q;
            }
        }
    }
    // red accent strips that pulse when things get critical
    const Col strip = mix(hex(0x8A1424), kRed, .4f + .6f * alarm);
    for (int s2 = -1; s2 <= 1; s2 += 2) {
        quad(r, {s2 * 3.2 - .06, wy - .1, -.5}, {s2 * 3.2 + .06, wy - .1, -.5}, {s2 * 3.2 + .06, wy - .1, 5.5}, {s2 * 3.2 - .06, wy - .1, 5.5}, nullptr, strip,
             static_cast<std::uint16_t>(unlit | double_sided));
        r.billboard({s2 * 3.2, wy - .2, .5}, .9, 5, &tx().glow, alpha(kRed, .12f + .3f * alarm), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
        // side pillars: they lean in, then topple
        const double f = fall(d, s2 < 0 ? .3 : .42, .45);
        const M34 pm = at({s2 * 3.9, wy - .3, -1}) * M34::rot_y(-s2 * f * 1.2) * M34::rot_x(-f * .3) * sc(.45, .3, 7);
        draw_mesh(r, box_mesh(), pm, nullptr, hex(0x1C1020), opaque);
    }
}

void Lair::draw_collapse(const LairState& s, double t) {
    const double d = s.doom;
    if (d <= 0) return;
    // debris raining from the ceiling: chunks of masonry tumbling past the camera
    std::uint32_t h = 4242;
    for (int i = 0; i < 26; ++i) {
        h = h * 1664525u + 1013904223u;
        const double x = ((h >> 8) % 1000) / 1000.0 * 9 - 4.5;
        const double y = ((h >> 18) % 1000) / 1000.0 * 4.5 - .5;
        const double start = .08 + .7 * ((h >> 4) % 1000) / 1000.0;
        const double u = (d - start) * 2.2;
        if (u <= 0) continue;
        const double z = 7 - 9 * u * u;
        if (z < -2) continue;
        const double sz = .08 + .18 * ((h >> 12) % 100) / 100.0;
        const M34 m = at({x, y, z}) * M34::rot_x(u * 7 + i) * M34::rot_z(u * 5) * sc(sz, sz * .8, sz * 1.4);
        draw_mesh(r, box_mesh(), m, nullptr, i % 3 ? hex(0x3A2A34) : hex(0x5A4048), toon);
        draw_outline(r, box_mesh(), m, .015, hex(0x050307));
    }
    // dust hanging in the air, and the red of the alarm everywhere
    for (int i = 0; i < 8; ++i) {
        const double ph = std::fmod(t * .2 + i * .13, 1.0);
        r.billboard({-4 + i * 1.15, 1.5, -.5 + 4 * ph}, 2.4, 1.6, &tx().glow, alpha(hex(0x8A6A60), static_cast<float>(.18 * d)),
                    static_cast<std::uint16_t>(translucent | unlit | no_depth_write));
    }
}

void Lair::draw_peg(int color, V3 base, double scale, double t, bool glow) {
    const double rr = .17 * scale;
    const V3 c = base + V3{0, 0, rr * .95};
    const M34 m = at(c) * sc(rr, rr, rr * .9);
    draw_mesh(r, sphere_mesh(14, 10), m, nullptr, kPeg[color], toon);
    draw_outline(r, sphere_mesh(14, 10), m, .018, kInkPeg);
    // the symbol on the face toward the player, and a gloss highlight
    const V3 face = c + V3{0, -rr * .92, rr * .25};
    quad(r, face + V3{-rr * .55, 0, -rr * .55}, face + V3{rr * .55, 0, -rr * .55}, face + V3{rr * .55, 0, rr * .55}, face + V3{-rr * .55, 0, rr * .55},
         &tx().sym[color], kWhite, static_cast<std::uint16_t>(cutout | unlit | double_sided));
    r.billboard(c + V3{-rr * .35, -rr * .5, rr * .25}, rr * .7, rr * .5, &tx().shine, alpha(kWhite, .8f), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
    if (glow) r.billboard(base + V3{0, 0, -.05}, rr * 5, rr * 3, &tx().glow, alpha(kPeg[color], .35f), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
    static_cast<void>(t);
}

void Lair::draw_desk(const LairState& s, double t) {
    const ConsoleState& cs = s.console;
    // the desk: dark lacquer, gold trim, the console glass inset across its top
    slab(r, {-kDeskX, kDeskFront, -1.5}, {kDeskX, kDeskBack, kDeskZ - .02}, kWood, opaque, .03, hex(0x050208));
    slab(r, {-kDeskX - .05, kDeskFront - .05, kDeskZ - .1}, {kDeskX + .05, kDeskBack + .05, kDeskZ - .02}, kGoldTrim, opaque);
    {
        const double x0 = -kDeskX + .12, x1 = kDeskX - .12, y0 = kDeskFront + .1, y1 = kDeskBack - .15;
        quad(r, {x0, y0, kDeskZ - .015}, {x1, y0, kDeskZ - .015}, {x1, y1, kDeskZ - .015}, {x0, y1, kDeskZ - .015}, &tx().glass, kWhite, unlit,
             (x1 - x0) / 1.2, (y1 - y0) / 1.2);
        // a new game sweeps a bright scanline from the front of the console to the back
        if (cs.reboot > 0 && cs.reboot < 1) {
            const double y = y0 + (y1 - y0) * cs.reboot;
            quad(r, {x0, y - .08, kDeskZ - .01}, {x1, y - .08, kDeskZ - .01}, {x1, y + .08, kDeskZ - .01}, {x0, y + .08, kDeskZ - .01}, nullptr,
                 alpha(kNeon, .7f), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
        }
    }
    const float on = static_cast<float>(std::clamp(cs.reboot <= 0 ? 1.0 : cs.reboot * 1.4 - .2, 0.0, 1.0));
    // sockets: glowing rings around dark wells
    for (int i = 0; i < kPegs; ++i) {
        const V3 p = socket(i);
        const bool hot = cs.hover_socket == i;
        draw_mesh(r, disc_mesh(18), at(p + V3{0, 0, .002}) * sc(.2, .2, 1), nullptr, hex(0x02030A), unlit);
        decal(r, p + V3{0, 0, .004}, .5, .5, &tx().rim, alpha(hot ? hex(0xCFFBFF) : kNeon, on), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
        r.billboard(p + V3{0, 0, -.08}, hot ? .9 : .6, .3, &tx().glow, alpha(kNeon, (hot ? .3f : .15f) * on), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
        const int c = cs.draft[static_cast<size_t>(i)];
        if (c >= 0) draw_peg(c, p, 1 + .25 * cs.pop[static_cast<size_t>(i)], t);
    }
    // the palette: one of each peg in a lit cradle along the front edge
    for (int c = 0; c < kColors; ++c) {
        const V3 p = palette(c);
        const bool hot = cs.hover_palette == c;
        decal(r, p + V3{0, 0, .004}, .42, .42, &tx().rim, alpha(kPeg[c], (hot ? .9f : .45f) * on), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
        const double hop = s.doom > .15 ? std::fabs(std::sin(t * (9 + c) + c)) * .25 * s.doom : 0;
        const double slide = fall(s.doom, .6 + .05 * c, .4);
        draw_peg(c, p + V3{slide * (c - 2.5) * .6, -slide * 2.5, (hot ? .04 : 0) + hop - slide * 3}, hot ? 1.12 : 1.0, t, hot);
    }
    // the CHECK button: a big red mushroom cap in a steel collar, glowing when a guess is ready
    {
        const V3 p = check_button();
        const bool lit = cs.can_check;
        const float pulse = lit ? static_cast<float>(.7 + .3 * std::sin(t * 5)) : 0.f;
        const double travel = .07 * std::clamp(cs.press, 0.0, 1.0);
        const M34 housing = at(p) * sc(.27, .27, .05);
        draw_mesh(r, cylinder_mesh(18), housing, nullptr, hex(0x24202A), toon);
        draw_mesh(r, disc_mesh(18), at(p + V3{0, 0, .05}) * sc(.27, .27, 1), nullptr, hex(0x2E2A34), toon);
        draw_outline(r, cylinder_mesh(18), housing, .02, hex(0x050407));
        draw_mesh(r, torus_mesh(20, 6, .2), at(p + V3{0, 0, .055}) * sc(.23, .23, .23), nullptr, hex(0x9AA0AC), toon);
        const Col cap = lit ? mix(hex(0xC81E2E), hex(0xFF5060), pulse * .5f) : hex(0x7A1A22);
        const M34 stem = at(p + V3{0, 0, .05 - travel}) * sc(.19, .19, .08);
        draw_mesh(r, cylinder_mesh(18), stem, nullptr, cap, toon);
        const M34 dome = at(p + V3{0, 0, .13 - travel}) * sc(.19, .19, .07);
        draw_mesh(r, hemisphere_mesh(16, 5), dome, nullptr, cap, toon);
        draw_outline(r, hemisphere_mesh(16, 5), dome, .018, hex(0x2A0508));
        draw_outline(r, cylinder_mesh(18), stem, .018, hex(0x2A0508));
        r.billboard(p + V3{-.06, -.08, .17 - travel}, .1, .06, &tx().shine, alpha(kWhite, .7f), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
        if (lit) r.billboard(p + V3{0, 0, -.05}, .9, .45, &tx().glow, alpha(hex(0xFF3040), .3f * pulse), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
        // its engraved label, just in front
        decal(r, p + V3{0, -.36, .004}, .56, .28, &tx().check_lit, alpha(lit ? hex(0xFFB0B0) : hex(0x9A6A70), on), static_cast<std::uint16_t>(cutout | unlit));
        decal(r, readout() + V3{0, 0, .004}, .8, .4, &readout_tex(cs.turn, cs.turns, cs.alarm), alpha(kWhite, on), static_cast<std::uint16_t>(cutout | unlit));
    }
    // feedback pins for the latest guess: a 2x2 cluster beside the sockets, lighting one by one
    {
        const V3 base = socket(0) + V3{-.45, 0, .004};
        const int lit = static_cast<int>(std::floor(cs.pins * kPegs + 1e-6));
        for (int k = 0; k < kPegs; ++k) {
            const V3 p = base + V3{(k % 2) * .16 - .08, (k / 2) * .16 - .08, 0};
            const bool shown = k < lit;
            const Tex* tex = nullptr;
            if (shown && k < cs.last.exact) tex = &tx().pin_exact;
            else if (shown && k < cs.last.exact + cs.last.near) tex = &tx().pin_near;
            if (tex) decal(r, p, .15, .15, tex, kWhite, static_cast<std::uint16_t>(cutout | unlit));
            else draw_mesh(r, disc_mesh(10), at(p) * sc(.05, .05, 1), nullptr, alpha(kNeonDim, on), unlit);
        }
    }
    // the dragged peg, lifted above the glass with a shadow beneath
    if (cs.drag_color >= 0) {
        decal(r, V3{cs.drag_at.x, cs.drag_at.y, kDeskZ + .003}, .3, .3, &tx().glow, alpha(hex(0x000000), .5f), static_cast<std::uint16_t>(translucent | unlit | no_depth_write));
        draw_peg(cs.drag_color, cs.drag_at + V3{0, 0, .22}, 1.15, t, true);
    }
    // at the end, the secret rises from a slot behind the sockets
    if (cs.reveal > 0) {
        for (int i = 0; i < kPegs; ++i) {
            const double u = std::clamp(cs.reveal * 1.6 - i * .15, 0.0, 1.0);
            if (u <= 0) continue;
            const V3 p = socket(i) + V3{0, .62, -.3 + .3 * u};
            draw_peg(cs.secret[static_cast<size_t>(i)], p, 1.0, t, true);
        }
        quad(r, {-1.2, -.55, kDeskZ + .003}, {1.2, -.55, kDeskZ + .003}, {1.2, -.38, kDeskZ + .003}, {-1.2, -.38, kDeskZ + .003}, nullptr,
             alpha(kNeon, static_cast<float>(.3 * cs.reveal)), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
    }
}

void Lair::render(const LairState& s, double t) {
    r.time = t;
    r.clear_depth();
    r.tris_drawn = 0;
    // the light goes red as his plan succeeds
    r.light.amb_col = {static_cast<float>(.56 + .25 * s.doom), static_cast<float>(.5 - .3 * s.doom), static_cast<float>(.52 - .3 * s.doom), 1};
    draw_room(s, t);
    draw_villain(r, s.villain, t);
    draw_desk(s, t);
    draw_collapse(s, t);
}

int Lair::pick_socket(double sx, double sy) const {
    int best = -1;
    double bd = r.scale / kSamples * .3;
    for (int i = 0; i < kPegs; ++i) {
        double x, y;
        to_screen(socket(i) + V3{0, 0, .12}, x, y);
        const double d = std::hypot(sx - x, sy - y);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

int Lair::pick_palette(double sx, double sy) const {
    int best = -1;
    double bd = r.scale / kSamples * .3;
    for (int c = 0; c < kColors; ++c) {
        double x, y;
        to_screen(palette(c) + V3{0, 0, .12}, x, y);
        const double d = std::hypot(sx - x, sy - y);
        if (d < bd) { bd = d; best = c; }
    }
    return best;
}

bool Lair::pick_check(double sx, double sy) const {
    double x, y, lx, ly;
    to_screen(check_button() + V3{0, 0, .12}, x, y);
    to_screen(check_button() + V3{0, -.38, 0}, lx, ly);
    return std::hypot(sx - x, sy - y) < r.scale / kSamples * .3 || std::hypot(sx - lx, sy - ly) < r.scale / kSamples * .2;
}

bool Lair::pick_villain(double sx, double sy, const VillainPose& v) const {
    double x, y;
    to_screen(villain_head_center(v), x, y);
    return std::hypot(sx - x, sy - y) < r.scale / kSamples * .55;
}

bool Lair::console_point(double sx, double sy, V3& out) const {
    double wx, wy;
    if (!r.unproject_plane(sx * kSamples, sy * kSamples, kDeskZ, wx, wy)) return false;
    out = {wx, wy, kDeskZ};
    return true;
}

}  // namespace fp
