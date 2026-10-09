#include "cabin.hpp"

#include "platform/render.hpp"

#include <algorithm>
#include <cmath>

namespace ld {

namespace {
const Col kWhite{1, 1, 1, 1};
const Col kInk = hex(0x0E1418);
M34 at(V3 v) { return M34::translate(v.x, v.y, v.z); }
M34 at(double x, double y, double z) { return M34::translate(x, y, z); }
M34 sc(double x, double y, double z) { return M34::scale(x, y, z); }
std::uint32_t hsh(std::uint32_t x) { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x; }
double h01(std::uint32_t x) { return (hsh(x) & 0xFFFF) / 65535.0; }

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

struct Textures {
    Tex planks, floor, water, keeper, glow, ray, felt;
    Textures() {
        Canvas c;
        // old ship's planking: dark, waterlogged, with green slime at the seams
        c.resize(64, 64);
        c.clear(hex(0x3A2A1E));
        for (int y = 0; y < 64; y += 16) {
            c.fill_rect(0, y, 64, 1, hex(0x1A120C));
            c.fill_rect(0, y + 1, 64, 2, hex(0x2E4A32, .55f));
            for (int k = 0; k < 4; ++k) { c.begin(); c.move(0, y + 5 + k * 3 + h01(y + k) * 2); c.line(64, y + 5 + k * 3 + h01(y + k + 9) * 2); c.stroke(hex(0x48362A), .8); }
            c.fill_circle(10 + h01(y) * 44, y + 8, 1.2, hex(0x1A120C));
        }
        planks = tex_from(c);
        c.clear(hex(0x2E2218));
        for (int x = 0; x < 64; x += 13) c.fill_rect(x, 0, 1, 64, hex(0x160E08));
        for (int k = 0; k < 30; ++k) c.fill_circle(h01(k + 50) * 64, h01(k + 80) * 64, .8 + h01(k + 110) * 1.6, hex(0xC8B890, .25f));  // sand drifted in
        floor = tex_from(c);
        // deep water beyond the window: lighter above, black below, faint caustics
        c.clear(hex(0x06141E));
        c.begin(); c.rect(0, 0, 64, 64); c.fill(Paint::lin(0, 0, 0, 64, {{0, hex(0x1E5A6A)}, {.6f, hex(0x0A2A3A)}, {1, hex(0x041018)}}));
        for (int k = 0; k < 12; ++k) { c.begin(); c.move(h01(k) * 64, 0); c.line(h01(k + 30) * 64 + 6, 30); c.stroke(hex(0x6AC8D8, .12f), 2); }
        water = tex_from(c);
        // the Keeper: a vast anglerfish face filling the window, all teeth and glowing eyes
        c.resize(128, 128);
        c.clear({0, 0, 0, 0});
        c.begin(); c.ellipse(64, 78, 62, 46); c.fill(hex(0x0A1418));
        c.begin(); c.ellipse(64, 70, 58, 40); c.fill(Paint::rad(64, 60, 60, {{0, hex(0x223038)}, {1, hex(0x0A1418)}}));
        c.begin(); c.move(10, 92); c.quad(64, 126, 118, 92); c.quad(64, 108, 10, 92); c.fill(hex(0x02060A));  // the maw
        for (int k = 0; k < 13; ++k) { const double x = 16 + k * 7.6, y = 94 + std::sin(k * .5) * 4; c.begin(); c.move(x - 3, y); c.line(x, y + 8 + (k % 3) * 3); c.line(x + 3, y); c.close(); c.fill(hex(0xD8D8C8)); }
        for (int k = 0; k < 11; ++k) { const double x = 22 + k * 8.4, y = 116 - std::sin(k * .4) * 3; c.begin(); c.move(x - 3, y); c.line(x, y - 9); c.line(x + 3, y); c.close(); c.fill(hex(0xC8C8B8)); }
        for (int s = -1; s <= 1; s += 2) { c.fill_circle(64 + s * 30, 60, 9, hex(0xE8F070)); c.fill_circle(64 + s * 30, 60, 4, hex(0x101010)); }
        c.begin(); c.move(64, 30); c.quad(70, 8, 84, 4); c.stroke(hex(0x1A2830), 3);  // the lure's stalk
        keeper = tex_from(c);
        c.resize(32, 32);
        c.clear({0, 0, 0, 0});
        c.begin(); c.circle(16, 16, 16); c.fill(Paint::rad(16, 16, 16, {{0, {1, 1, 1, 1}}, {.35f, {1, 1, 1, .5f}}, {1, {1, 1, 1, 0}}}));
        glow = tex_from(c);
        // a shaft of light: bright in the middle, soft at the sides, fading toward the bottom
        c.clear({0, 0, 0, 0});
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                const double u = std::fabs(x - 15.5) / 16, v = y / 31.0;
                const float a = static_cast<float>((1 - u * u) * (1 - v) * .9);
                c.fill_rect(x, y, 1, 1, {1, 1, 1, a});
            }
        ray = tex_from(c);
        // the table top: green baize, worn, ringed by a brass band
        c.resize(64, 64);
        c.clear(hex(0x1E5A3E));
        for (int k = 0; k < 90; ++k) c.fill_rect(h01(k + 200) * 64, h01(k + 300) * 64, 1, 1, hex(0x2A6A4A));
        for (int k = 0; k < 40; ++k) c.fill_rect(h01(k + 400) * 64, h01(k + 500) * 64, 1, 1, hex(0x164A30));
        felt = tex_from(c);
    }
};
const Textures& tx() {
    static Textures t;
    return t;
}

void quad(R3D& r, V3 a, V3 b, V3 c, V3 d, const Tex* tex, Col col, std::uint16_t mat, double s1 = 1, double t1 = 1) {
    const V3 n = norm(cross(b - a, d - a));
    Vtx v[6] = {{a, n, 0, t1, col}, {b, n, s1, t1, col}, {c, n, s1, 0, col}, {a, n, 0, t1, col}, {c, n, s1, 0, col}, {d, n, 0, 0, col}};
    r.draw(v, 6, tex, mat);
}
void part(R3D& r, const Mesh& m, const M34& model, Col c, double ink = .012) {
    draw_mesh(r, m, model, nullptr, c, toon);
    if (ink > 0) draw_outline(r, m, model, ink, kInk);
}

// pips on a face, in the face's own [-1,1]^2 coordinates
const double kPips[7][6][2] = {
    {},
    {{0, 0}},
    {{-.5, -.5}, {.5, .5}},
    {{-.5, -.5}, {0, 0}, {.5, .5}},
    {{-.5, -.5}, {.5, -.5}, {-.5, .5}, {.5, .5}},
    {{-.5, -.5}, {.5, -.5}, {0, 0}, {-.5, .5}, {.5, .5}},
    {{-.5, -.5}, {.5, -.5}, {-.5, 0}, {.5, 0}, {-.5, .5}, {.5, .5}},
};
const int kPipCount[7] = {0, 1, 2, 3, 4, 5, 6};
// with a face up, which faces show to the front and the right (opposite faces sum to seven)
const int kFront[7] = {0, 2, 1, 1, 1, 1, 2};
const int kRight[7] = {0, 3, 4, 5, 2, 3, 4};
}  // namespace

void draw_die(R3D& r, V3 base, double size, int value, double spin, Col body, Col pip, double glow) {
    value = std::clamp(value, 1, 6);
    const double h = size / 2;
    const M34 m = at(base) * M34::rot_z(spin);
    if (glow > 0) r.billboard(base + V3{0, 0, -size * .2}, size * 3.2, size * 2.6, &tx().glow, alpha(hex(0xF8E070), static_cast<float>(.55 * glow)), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
    part(r, box_mesh(), m * sc(h, h, size), mix(body, hex(0xFFF4B0), static_cast<float>(glow * .5)), .006);
    auto face = [&](int v, const M34& fm) {
        for (int k = 0; k < kPipCount[v]; ++k)
            draw_mesh(r, disc_mesh(8), fm * at(kPips[v][k][0] * h * .95, kPips[v][k][1] * h * .95, 0) * sc(h * .2, h * .2, 1), nullptr, v == 1 ? hex(0xB02020) : pip, unlit);
    };
    face(value, m * at(0, 0, size + .001));
    face(kFront[value], m * at(0, -h - .001, h) * M34::rot_x(1.5708));
    face(kRight[value], m * at(h + .001, 0, h) * M34::rot_y(1.5708));
}

void Cabin::resize(int w, int h, int bottom_panel, int top_band) {
    r.resize(w, h);
    panel_ = bottom_panel;
    w_ = w;
    h_ = h;
    r.yaw = 0;
    r.pitch = .42;
    r.persp = 4.6;
    const double room = std::max(20, h - bottom_panel - top_band);
    r.scale = std::min(w / 3.0, room / 1.75);
    r.ax = .5;
    r.ay = (top_band + room * .5) / h;
    r.target = {0, .3, 1.1};
    r.light.sun = norm({.2, -.5, .9});
    r.light.sun_col = {.62f, .56f, .42f, 1};
    r.light.amb_col = {.36f, .5f, .55f, 1};
    r.light.fog_col = {.04f, .16f, .2f, 1};
    r.light.fog_near = 2.2;
    r.light.fog_far = 6.5;
    r.light.focus = {0, -1.6, 1.6};
    r.light.toon_edge = .12f;
    r.light.toon_soft = .12f;
    r.set_camera();
}

V3 Cabin::cup_pos(int seat) const {
    if (seat == 0) return {.42, -.5, kTop};
    const double a = n_ == 2 ? (seat == 1 ? -.6 : .6) : (seat - 2) * .95;
    return {std::sin(a) * .55, std::cos(a) * .55 - .02, kTop};
}

V3 Cabin::dice_pos(int seat, int i, int n) const {
    if (seat == 0) {
        // your own, in a row at the near edge
        return {-.16 + (i - (n - 1) / 2.0) * .15, -.62, kTop};
    }
    // a little cluster where the cup sits
    const V3 c = cup_pos(seat);
    const double a = i * 2.4 + seat;
    const double rr = i == 0 ? 0 : .06 + .015 * i;
    return c + V3{std::cos(a) * rr, std::sin(a) * rr * .8, 0};
}

void Cabin::seat(int opponents, CabinState& s) {
    n_ = std::clamp(opponents, 1, 3);
    s.crew.resize(static_cast<size_t>(n_ + 1));
    s.cups.resize(static_cast<size_t>(n_ + 1));
    s.dice.resize(static_cast<size_t>(n_ + 1));
    s.hidden.resize(static_cast<size_t>(n_ + 1));
    for (int i = 1; i <= n_; ++i) {
        const double a = n_ == 2 ? (i == 1 ? -.62 : .62) : (i - 2) * .98;
        const double R = 1.28;
        CrewPose& p = s.crew[static_cast<size_t>(i)];
        p.pos = {std::sin(a) * R, std::cos(a) * R, 0};
        p.yaw = std::atan2(-p.pos.x, p.pos.y);
    }
}

void Cabin::to_screen(V3 p, double& sx, double& sy) const {
    double sz;
    r.project(p, sx, sy, sz);
}

int Cabin::pick(double sx, double sy, const CabinState& s) const {
    int best = -1;
    double bd = 1e9;
    for (int i = 1; i < static_cast<int>(s.crew.size()); ++i) {
        double x, y;
        to_screen(crew_head(s.crew[static_cast<size_t>(i)]) - V3{0, 0, .25}, x, y);
        const double d = std::hypot(sx - x, (sy - y) * .7);
        if (d < r.scale * .45 && d < bd) { bd = d; best = i; }
    }
    return best;
}

void Cabin::draw_room(const CabinState& s, double t) {
    const double dark = 1 - .35 * s.tension;
    const Col wall = mix(hex(0x6A7A70), hex(0x2A3A40), static_cast<float>(s.tension * .5));
    // the curved hull behind: planking on a half-cylinder, ribs at intervals
    const int segs = 14;
    const double R = 3.0, z0 = -.05, z1 = 3.2;
    for (int i = 0; i < segs; ++i) {
        const double a0 = -1.9 + i * 3.8 / segs, a1 = a0 + 3.8 / segs;
        const V3 p0{std::sin(a0) * R, std::cos(a0) * R * .8 + .2, z0}, p1{std::sin(a1) * R, std::cos(a1) * R * .8 + .2, z0};
        quad(r, p1 + V3{0, 0, z1 - z0}, p0 + V3{0, 0, z1 - z0}, p0, p1, &tx().planks, wall, 0, 1, 3);
    }
    for (int i = 0; i <= 6; ++i) {
        const double a = -1.75 + i * .583;
        part(r, box_mesh(), at(std::sin(a) * (R - .08), std::cos(a) * (R - .08) * .8 + .2, 0) * M34::rot_z(-a) * sc(.07, .06, 3.2), mix(hex(0x2A1C12), hex(0x101418), static_cast<float>(s.tension * .4)), .015);
    }
    // the great stern window, and the Keeper in it
    const V3 wc{0, 2.55, 1.3};
    const double wr = .8;
    quad(r, wc + V3{-wr, -.02, -wr}, wc + V3{wr, -.02, -wr}, wc + V3{wr, -.02, wr}, wc + V3{-wr, -.02, wr}, &tx().water, kWhite, static_cast<std::uint16_t>(unlit | no_fog));
    const double kb = std::sin(t * .35) * .05;  // the Keeper drifts
    const double ks = .95 + .05 * std::sin(t * .21);
    {
        // high in the window so his eyes show over the crew
        const double zt = 1.95 + kb, zb = .78 + kb, xw = .78 * ks;
        quad(r, wc + V3{-xw, -.1, zb - wc.z}, wc + V3{xw, -.1, zb - wc.z}, wc + V3{xw, -.1, zt - wc.z}, wc + V3{-xw, -.1, zt - wc.z},
             &tx().keeper, alpha(kWhite, static_cast<float>(.6 + .4 * s.keeper)), static_cast<std::uint16_t>(translucent | unlit | no_fog));
    }
    // the lure: a lamp on a stalk, dangling at the top of the window, brighter when he speaks
    const V3 lure = wc + V3{.3 + .03 * std::sin(t * .9), -.12, .55 + kb + .03 * std::sin(t * 1.3)};
    r.billboard(lure - V3{0, 0, .25}, .5 + .4 * s.keeper, .5 + .4 * s.keeper, &tx().glow, hex(0x90F8E0, static_cast<float>(.45 + .5 * s.keeper)), static_cast<std::uint16_t>(additive | unlit | no_depth_write | no_fog));
    draw_mesh(r, sphere_mesh(8, 6), at(lure) * sc(.035, .035, .035), nullptr, hex(0xE0FFF0), static_cast<std::uint16_t>(unlit | no_fog));
    // the window's heavy frame and mullions
    draw_mesh(r, torus_mesh(24, 6, .07), at(wc) * M34::rot_x(1.5708) * sc(wr, wr, wr), nullptr, hex(0x4A3A22), toon);
    for (int k = 0; k < 2; ++k) part(r, box_mesh(), at(wc + V3{0, -.04, 0}) * M34::rot_y(k * 1.5708) * at(0, 0, -wr) * sc(.025, .02, wr * 2), hex(0x3A2A18), .01);
    // the floor
    quad(r, {-4, -3, 0}, {4, -3, 0}, {4, 3.2, 0}, {-4, 3.2, 0}, &tx().floor, mix(kWhite, hex(0x406070), static_cast<float>(1 - dark)), 0, 6, 5);
    // seaweed in the corners, swaying
    for (int k = 0; k < 9; ++k) {
        const double x = (k < 5 ? -1 : 1) * (1.7 + h01(k) * .8), y = .6 + h01(k + 20) * 1.6;
        const double hgt = 1.0 + h01(k + 40) * 1.4;
        for (int s2 = 0; s2 < 8; ++s2) {
            const double u = s2 / 7.0;
            const double sw = std::sin(t * .8 + k + u * 2.5) * .18 * u;
            draw_mesh(r, sphere_mesh(6, 4), at(x + sw, y, u * hgt) * M34::rot_y(sw) * sc(.07 * (1 - u * .5), .03, .14), nullptr, mix(hex(0x2A7A3A), hex(0x5A9A3A), static_cast<float>(u)), toon);
        }
    }
    // a treasure chest, open, spilling gold
    part(r, box_mesh(), at(1.55, .9, 0) * M34::rot_z(-.5) * sc(.28, .18, .26), hex(0x5A3A1E), .015);
    part(r, box_mesh(), at(1.55, .9, .26) * M34::rot_z(-.5) * at(0, .18, 0) * M34::rot_x(-1.1) * at(0, -.18, 0) * sc(.28, .18, .06), hex(0x6A4422), .015);
    for (int k = 0; k < 14; ++k)
        draw_mesh(r, disc_mesh(8), at(1.35 + h01(k + 70) * .45, .7 + h01(k + 90) * .35, .27 + h01(k + 110) * .05) * sc(.04, .04, 1), nullptr, hex(0xF0C040), toon);
    // fish passing beyond the window, small and dark
    for (int k = 0; k < 3; ++k) {
        const double u = std::fmod(t * (.04 + .02 * k) + h01(k + 7), 1.0);
        const V3 fp = wc + V3{-1 + u * 2, .02, -.4 + k * .35};
        if (std::fabs(fp.x) < wr * .8) draw_mesh(r, sphere_mesh(6, 4), at(fp) * sc(.06, .01, .025), nullptr, hex(0x0A2028, .8f), static_cast<std::uint16_t>(translucent | unlit | no_fog));
    }
    // shafts of light from above
    for (int k = 0; k < 4; ++k) {
        const double x = -1.4 + k * .95 + .1 * std::sin(t * .2 + k);
        const float a = static_cast<float>((.07 + .04 * std::sin(t * .5 + k * 1.7)) * dark);
        quad(r, {x - .25, 1.6, 0}, {x + .25, 1.6, 0}, {x + .7, 1.0, 3.2}, {x + .2, 1.0, 3.2}, &tx().ray, hex(0xB0F0E8, a), static_cast<std::uint16_t>(additive | unlit | no_depth_write | double_sided), 1, 1);
    }
    // bubbles drifting up everywhere
    for (int k = 0; k < 36; ++k) {
        const double sp = .12 + h01(k + 500) * .2;
        const double u = std::fmod(t * sp + h01(k + 600), 1.0);
        const V3 b{-2.2 + h01(k + 700) * 4.4 + std::sin(t * 2 + k) * .03, .2 + h01(k + 800) * 2.2, u * 3.3};
        const double rad = .015 + h01(k + 900) * .025;
        r.billboard(b, rad * 2, rad * 2, &tx().glow, hex(0xC8F8FF, .35f), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
    }
}

void Cabin::draw_table(const CabinState& s, double t) {
    // a great barrel for a table: staves and hoops, a baize top with a brass rim
    const double R = .95, Z = kTop;
    part(r, cylinder_mesh(28), at(0, 0, 0) * sc(R * .82, R * .82, Z - .04), hex(0x5A3A22), .02);
    for (int k = 0; k < 3; ++k) draw_mesh(r, torus_mesh(28, 4, .04), at(0, 0, .15 + k * .25) * sc(R * .84, R * .84, R * .84), nullptr, hex(0x6A6A6A), toon);
    part(r, cylinder_mesh(32), at(0, 0, Z - .06) * sc(R, R, .06), hex(0x4A2E1A), .02);
    draw_mesh(r, disc_mesh(32), at(0, 0, Z) * sc(R, R, 1), &tx().felt, kWhite, toon, 3);
    draw_mesh(r, torus_mesh(32, 5, .025), at(0, 0, Z) * sc(R, R, R), nullptr, hex(0xC8A048), toon);
    // the lantern overhead, swinging on its chain, high above everyone's heads
    const double sw = std::sin(t * .7) * .1;
    const V3 top{0, .3, 3.4};
    const V3 lamp = top + V3{std::sin(sw) * .75, 0, -std::cos(sw) * .75};
    for (int k = 0; k < 6; ++k) {
        const V3 c = top + (lamp - top) * (k / 6.0);
        draw_mesh(r, torus_mesh(8, 3, .3), at(c) * M34::rot_y(sw) * M34::rot_x(k % 2 ? 1.57 : 0) * sc(.03, .03, .03), nullptr, hex(0x3A3A3A), unlit);
    }
    const M34 lm = at(lamp) * M34::rot_y(sw);
    part(r, cone_mesh(8), lm * at(0, 0, -.06) * sc(.12, .12, .1), hex(0x2A2620), .01);             // the cap
    draw_mesh(r, cylinder_mesh(8), lm * at(0, 0, -.3) * sc(.08, .08, .24), nullptr, hex(0xFFE8A0), unlit);  // the glowing glass
    for (int k = 0; k < 4; ++k) part(r, box_mesh(), lm * M34::rot_z(k * 1.5708 + .785) * at(.08, 0, -.32) * sc(.012, .012, .27), hex(0x2A2620), .006);
    part(r, cylinder_mesh(8), lm * at(0, 0, -.34) * sc(.1, .1, .04), hex(0x2A2620), .01);
    r.billboard(lamp + V3{0, 0, -.6}, 1.0, .9, &tx().glow, hex(0xFFD070, static_cast<float>(.4 - .14 * s.tension)), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
    // a pool of lamplight on the baize
    r.billboard({std::sin(sw) * .4, .15, Z + .005}, 1.6, .9, &tx().glow, hex(0xFFD890, static_cast<float>(.12 - .05 * s.tension)), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
    // the Keeper's jar: lost dice sink into it
    const V3 jp = jar_pos();
    for (int k = 0; k < s.jar; ++k) {
        const double a = k * 2.1;
        const V3 dp = jp + V3{std::cos(a) * .045 * (k % 3), std::sin(a) * .04 * (k % 3), .01 + (k / 3) * .05};
        draw_die(r, dp, .05, 1 + (k * 5) % 6, a, hex(0xE8E0CC), hex(0x1A1A1A), 0);
    }
    for (const Sink& sk : s.sinking) {
        const V3 from = cup_pos(sk.seat) + V3{0, 0, .15};
        const double u = std::clamp(sk.t, 0.0, 1.0);
        const V3 p = from + (jp + V3{0, 0, .1} - from) * u + V3{0, 0, std::sin(u * M_PI) * .45};
        draw_die(r, p, .07 - .02 * u, 1 + sk.seat, u * 9, hex(0xF0E8D4), hex(0x1A1A1A), .5);
    }
    draw_mesh(r, cylinder_mesh(14), at(jp) * sc(.13, .13, .26), nullptr, hex(0xA8E0E8, .3f), static_cast<std::uint16_t>(translucent | unlit));
    draw_mesh(r, torus_mesh(14, 4, .08), at(jp + V3{0, 0, .26}) * sc(.13, .13, .13), nullptr, hex(0xC8F0F0), toon);
    // cups and dice
    for (int seat = 0; seat < static_cast<int>(s.cups.size()); ++seat) {
        const CupShow& cs = s.cups[static_cast<size_t>(seat)];
        const auto& dv = s.dice[static_cast<size_t>(seat)];
        const int n = static_cast<int>(dv.size());
        const bool own = seat == 0;
        // the dice show when the cup is up (or they're yours, set out in front of you)
        if (own || cs.lift > .3) {
            for (int i = 0; i < n; ++i) {
                const DieShow& d = dv[static_cast<size_t>(i)];
                const V3 p = dice_pos(seat, i, n) + V3{0, 0, std::sin(d.hop * M_PI) * .06};
                const double size = own ? .115 : .085;
                const Col body = mix(own ? hex(0xF4ECD8) : hex(0xE8DFC8), hex(0x60686A), static_cast<float>(d.fade * .6));
                draw_die(r, p, size, d.value, own ? .1 * (i - 2) : i * .7 + seat, body, hex(0x1A1A1A), d.glow);
            }
        }
        // the cup: leather, upside down; lifted and tipped back to reveal, rattled when shaken
        // lifted, it swings up and over to one side and is set down mouth-up beside the dice
        const V3 cp = cup_pos(seat);
        const double jit = cs.shake > 0 ? std::sin(t * 47 + seat) * .03 * cs.shake : 0;
        const V3 radial = seat == 0 ? V3{0, -1, 0} : norm(V3{cp.x, cp.y, 0});
        const V3 side{-radial.y, radial.x, 0};
        const double u = std::clamp(cs.lift, 0.0, 1.0);
        const V3 pos = cp + side * (.22 * u) + V3{jit, 0, std::sin(u * M_PI) * .22 + cs.shake * .08};
        const double flip = u * M_PI;  // turned over as it goes
        const M34 cm = at(pos + V3{0, 0, u > .5 ? .22 * std::sin((u - .5) * M_PI) : 0}) * M34::rot_z(std::atan2(side.y, side.x)) * M34::rot_y(jit * 3 + flip) * at(0, 0, u > .5 ? -.22 * std::sin((u - .5) * M_PI) : 0);
        part(r, cylinder_mesh(14), cm * sc(.105, .105, .22), hex(0x6A3A1E), .012);
        draw_mesh(r, disc_mesh(14), cm * at(0, 0, .22) * sc(.105, .105, 1), nullptr, hex(0x5A3018), toon);
        draw_mesh(r, torus_mesh(14, 4, .1), cm * at(0, 0, .02) * sc(.11, .11, .11), nullptr, hex(0xC8A048), toon);
        draw_mesh(r, torus_mesh(14, 4, .1), cm * at(0, 0, .2) * sc(.105, .105, .105), nullptr, hex(0xC8A048), toon);
    }
}

void Cabin::render(const CabinState& s, double t) {
    r.time = t;
    r.clear_depth();
    r.tris_drawn = 0;
    // the backdrop: dark water-green, deeper with the tension
    const Col bg = mix(hex(0x0C2A30), hex(0x061418), static_cast<float>(s.tension));
    for (size_t i = 0; i < r.rgb.size(); i += 3) { r.rgb[i] = bg.r * bg.r; r.rgb[i + 1] = bg.g * bg.g; r.rgb[i + 2] = bg.b * bg.b; }
    draw_room(s, t);
    for (size_t i = 1; i < s.crew.size(); ++i) draw_crew(r, s.crew[i], t);
    draw_table(s, t);
    // bubbles let slip at the table (a tell, or a sigh)
    for (const auto& [from, age] : s.puffs) {
        for (int k = 0; k < 5; ++k) {
            const double a = age - k * .12;
            if (a < 0 || a > 2.2) continue;
            const V3 b = from + V3{std::sin(a * 6 + k) * .03, -.02, a * .45};
            const double rad = .018 + .008 * (k % 3);
            r.billboard(b, rad * 2, rad * 2, &tx().glow, hex(0xE0FFFF, static_cast<float>(.7 * (1 - a / 2.2))), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
        }
    }
}

}  // namespace ld
