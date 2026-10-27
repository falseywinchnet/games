#include "stage.hpp"

#include "platform/mesh.hpp"
#include "platform/raster.hpp"

#include <algorithm>
#include <cmath>

namespace sbx {

namespace {
const Col kWhite{1, 1, 1, 1};
const Col kBoxPink = hex(0xF3B5C4), kTrim = hex(0xFFF4E4), kDeck = hex(0xFBEFE2), kInside = hex(0x3C2A3E);
const Col kBase = hex(0x5E4263), kSteel = hex(0xD4D8E2), kLampOff = hex(0xE3C9A0), kLampOn = hex(0xFFF0A0);
const Col kKnob[kSwitches] = {hex(0xFF8F8F), hex(0xFFB870), hex(0xF7DE6A), hex(0x8CD9A6), hex(0x86C3F2), hex(0xB9A0EE)};
const Col kInkBox = hex(0x6E4A5A);
constexpr double kOn = -.55, kOff = .55;  // lever tilt about x: + leans toward the player

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

struct Textures {
    Tex front, wood, glow, shadow, wall, anger, sweat, puff, heart, sparkle, pointer;
    Textures() {
        Canvas c;
        // box front: pink with cream stars and dots
        c.resize(128, 64);
        c.clear(kBoxPink);
        auto star = [&](double x, double y, double r, Col col) {
            c.begin();
            for (int i = 0; i < 10; ++i) {
                const double a = -M_PI / 2 + i * M_PI / 5, rr = i % 2 ? r * .45 : r;
                i == 0 ? c.move(x + std::cos(a) * rr, y + std::sin(a) * rr) : c.line(x + std::cos(a) * rr, y + std::sin(a) * rr);
            }
            c.close();
            c.fill(col);
        };
        star(20, 20, 8, alpha(kTrim, .9f)); star(70, 40, 6, alpha(kTrim, .8f)); star(108, 16, 7, alpha(hex(0xFFE08A), .9f));
        star(44, 48, 5, alpha(hex(0xFFE08A), .8f));
        for (int i = 0; i < 9; ++i) c.fill_circle(10 + i * 14.5, 4 + (i % 3) * 26 + 6, 1.6, alpha(kTrim, .7f));
        front = tex_from(c);
        // table wood
        c.resize(64, 64);
        c.clear(hex(0xC99A6E));
        for (int y = 0; y < 64; ++y) {
            const double g = .5 + .5 * std::sin(y * .9 + std::sin(y * .23) * 3);
            c.fill_rect(0, y, 64, 1, alpha(hex(0x9E6E48), static_cast<float>(.25 * g)));
        }
        for (int k = 0; k < 4; ++k) c.fill_rect(0, k * 16, 64, 1, alpha(hex(0x7A5034), .55f));
        wood = tex_from(c);
        // soft glow and contact shadow
        c.resize(32, 32);
        c.clear({0, 0, 0, 0});
        c.begin(); c.circle(16, 16, 16); c.fill(Paint::rad(16, 16, 16, {{0, {1, 1, 1, 1}}, {.35f, {1, 1, 1, .45f}}, {1, {1, 1, 1, 0}}}));
        glow = tex_from(c);
        c.clear({0, 0, 0, 0});
        c.begin(); c.rect(0, 0, 32, 32); c.fill(Paint::rad(16, 16, 16, {{0, {0, 0, 0, .55f}}, {.7f, {0, 0, 0, .4f}}, {1, {0, 0, 0, 0}}}));
        shadow = tex_from(c);
        // anger mark: four red bulging strokes in a cross
        c.resize(32, 32);
        c.clear({0, 0, 0, 0});
        for (int q = 0; q < 4; ++q) {
            c.save();
            c.translate(16, 16);
            c.rotate(q * M_PI / 2);
            c.begin(); c.move(3, -12); c.quad(4, -4, 12, -3); c.stroke(hex(0xE8304A), 4.5);
            c.restore();
        }
        anger = tex_from(c);
        c.clear({0, 0, 0, 0});
        c.begin(); c.move(16, 3); c.quad(27, 18, 22, 25); c.quad(16, 31, 10, 25); c.quad(5, 18, 16, 3); c.close();
        c.fill(hex(0x8FD3FF)); c.begin(); c.move(16, 3); c.quad(27, 18, 22, 25); c.quad(16, 31, 10, 25); c.quad(5, 18, 16, 3); c.close(); c.stroke(hex(0x3C7FB8), 1.6);
        c.fill_ellipse(13, 19, 2.5, 4, hex(0xFFFFFF, .9f));
        sweat = tex_from(c);
        c.clear({0, 0, 0, 0});
        c.fill_circle(16, 18, 10, hex(0xFFFFFF, .9f)); c.fill_circle(10, 13, 6, hex(0xFFFFFF, .9f)); c.fill_circle(22, 12, 7, hex(0xFFFFFF, .9f));
        puff = tex_from(c);
        c.clear({0, 0, 0, 0});
        c.begin(); c.move(16, 28); c.cubic(2, 18, 4, 4, 16, 10); c.cubic(28, 4, 30, 18, 16, 28); c.close(); c.fill(hex(0xFF7FA6));
        c.begin(); c.move(16, 28); c.cubic(2, 18, 4, 4, 16, 10); c.cubic(28, 4, 30, 18, 16, 28); c.close(); c.stroke(hex(0xC23E6A), 1.5);
        c.fill_ellipse(10, 12, 2.5, 2, hex(0xFFFFFF, .85f));
        heart = tex_from(c);
        c.clear({0, 0, 0, 0});
        c.begin(); c.move(16, 1); c.quad(16, 16, 31, 16); c.quad(16, 16, 16, 31); c.quad(16, 16, 1, 16); c.quad(16, 16, 16, 1); c.close(); c.fill(hex(0xFFF2A0));
        sparkle = tex_from(c);
        // the pointer: a white arrow, tip at the top-left corner
        c.clear({0, 0, 0, 0});
        c.begin(); c.move(2, 1); c.line(2, 24); c.line(8, 18); c.line(12, 28); c.line(16, 26); c.line(12, 17); c.line(20, 17); c.close();
        c.fill(hex(0xFFFFFF)); c.begin(); c.move(2, 1); c.line(2, 24); c.line(8, 18); c.line(12, 28); c.line(16, 26); c.line(12, 17); c.line(20, 17); c.close();
        c.stroke(hex(0x1A1020), 1.6);
        pointer = tex_from(c);
    }
};
const Textures& tx() {
    static Textures t;
    return t;
}

// box_mesh() has no bottom; slabs seen from below (the open lid) need one
const Mesh& cube() {
    static Mesh m;
    if (m.empty()) {
        m = box_mesh();
        const V3 n{0, 0, -1};
        m.push_back({{-1, -1, 0}, n, 0, 0}); m.push_back({{1, 1, 0}, n, 1, 1}); m.push_back({{1, -1, 0}, n, 1, 0});
        m.push_back({{-1, -1, 0}, n, 0, 0}); m.push_back({{-1, 1, 0}, n, 0, 1}); m.push_back({{1, 1, 0}, n, 1, 1});
    }
    return m;
}

void slab(R3D& r, V3 lo, V3 hi, Col c, const Tex* tex = nullptr, double ts = 1) {
    const V3 ctr{(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z};
    draw_mesh(r, cube(), at(ctr) * sc((hi.x - lo.x) / 2, (hi.y - lo.y) / 2, hi.z - lo.z), tex, c, opaque, ts);
}
void slab_ink(R3D& r, V3 lo, V3 hi, Col c, const Tex* tex = nullptr) {
    const V3 ctr{(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z};
    const M34 m = at(ctr) * sc((hi.x - lo.x) / 2, (hi.y - lo.y) / 2, hi.z - lo.z);
    draw_mesh(r, cube(), m, tex, c, opaque);
    draw_outline(r, cube(), m, .02, kInkBox);
}

void quad(R3D& r, V3 a, V3 b, V3 c, V3 d, const Tex* tex, Col col, std::uint16_t mat, double s1 = 1, double t1 = 1) {
    const V3 n = norm(cross(b - a, d - a));
    Vtx v[6] = {{a, n, 0, t1, col}, {b, n, s1, t1, col}, {c, n, s1, 0, col}, {a, n, 0, t1, col}, {c, n, s1, 0, col}, {d, n, 0, 0, col}};
    r.draw(v, 6, tex, mat);
}
}  // namespace

V3 Stage::knob(int i, double on) {
    const double a = kOff + (kOn - kOff) * std::clamp(on, 0.0, 1.0);
    return {switch_x(i), kSwitchY - std::sin(a) * kLever, kPivotZ + std::cos(a) * kLever};
}

M34 Stage::lid_frame(double open) {
    const double a = std::clamp(open, 0.0, 1.0) * 1.72;  // hinge at the back edge, swings up and back
    return at({0, kOpenY1 + .06, kBoxZ + .02}) * M34::rot_x(-a);
}

void Stage::resize(int w, int h) {
    r.resize(w, h);
    r.yaw = -.14;
    r.pitch = .55;
    r.scale = std::min(w / 6.3, h / 4.9);
    r.ax = .5;
    r.ay = .6;
    r.target = {0, -.2, 1.15};
    r.light.sun = norm({-.55, -.7, .9});
    r.light.sun_col = {.42f, .4f, .37f, 1};
    r.light.amb_col = {.66f, .62f, .7f, 1};
    r.light.fog_near = 1e9;
    r.light.fog_far = 2e9;
    r.light.toon_edge = .12f;
    r.light.toon_soft = .12f;
    r.set_camera();
}

void Stage::to_screen(V3 p, double& sx, double& sy) const {
    double sz;
    r.project(p, sx, sy, sz);
}

void Stage::draw_room() {
    // wall: a warm gradient with soft polka dots
    for (int y = 0; y < r.H; ++y) {
        const float k = static_cast<float>(y) / r.H;
        const Col c = mix(hex(0xFBE3E6), hex(0xF6CDD6), k);
        float* o = r.rgb.data() + static_cast<size_t>(y) * r.W * 3;
        for (int x = 0; x < r.W; ++x, o += 3) {
            const int cx = (x + (y / 22 % 2) * 11) % 22 - 11, cy = y % 22 - 11;
            const bool dot = cx * cx + cy * cy < 7;
            o[0] = dot ? c.r * 1.03f : c.r; o[1] = dot ? c.g * 1.03f : c.g; o[2] = dot ? c.b * 1.05f : c.b;
        }
    }
    // table top: a wide wooden plane, and its front edge
    const double hw = 9, ty0 = -6, ty1 = 3.4;
    quad(r, {-hw, ty0, 0}, {hw, ty0, 0}, {hw, ty1, 0}, {-hw, ty1, 0}, &tx().wood, kWhite, opaque, 2, 6);
    quad(r, {-hw, ty1, 0}, {hw, ty1, 0}, {hw, ty1, -.25}, {-hw, ty1, -.25}, nullptr, hex(0x8E623F), opaque);
    // contact shadow under the box
    const double sx = kBoxX + .35, sy = kBoxY + .3;
    quad(r, {-sx + .15, -sy - .1, .004}, {sx + .15, -sy - .1, .004}, {sx + .15, sy, .004}, {-sx + .15, sy, .004}, &tx().shadow, kWhite,
         static_cast<std::uint16_t>(translucent | unlit));
}

void Stage::draw_box(const StageState& s) {
    const double t = .14;  // wall thickness
    // walls (outer pink with stars on the front; the inside is dark)
    slab_ink(r, {-kBoxX, -kBoxY, 0}, {kBoxX, -kBoxY + t, kBoxZ - .08}, kWhite, &tx().front);
    slab_ink(r, {-kBoxX, kBoxY - t, 0}, {kBoxX, kBoxY, kBoxZ - .08}, kBoxPink);
    slab_ink(r, {-kBoxX, -kBoxY, 0}, {-kBoxX + t, kBoxY, kBoxZ - .08}, kBoxPink);
    slab_ink(r, {kBoxX - t, -kBoxY, 0}, {kBoxX, kBoxY, kBoxZ - .08}, kBoxPink);
    // the dark inside of the hatch
    slab(r, {-kOpenX, kOpenY0, .1}, {kOpenX, kOpenY1, .12}, kInside);
    slab(r, {-kOpenX, kOpenY1 - .01, .1}, {kOpenX, kOpenY1, kBoxZ - .08}, kInside);
    slab(r, {-kOpenX, kOpenY0, .1}, {kOpenX, kOpenY0 + .01, kBoxZ - .08}, kInside);
    slab(r, {-kOpenX, kOpenY0, .1}, {-kOpenX + .01, kOpenY1, kBoxZ - .08}, kInside);
    slab(r, {kOpenX - .01, kOpenY0, .1}, {kOpenX, kOpenY1, kBoxZ - .08}, kInside);
    // cream trim band around the top
    slab_ink(r, {-kBoxX - .03, -kBoxY - .03, kBoxZ - .14}, {kBoxX + .03, -kBoxY + t, kBoxZ - .02}, kTrim);
    slab_ink(r, {-kBoxX - .03, kOpenY1, kBoxZ - .14}, {kBoxX + .03, kBoxY + .03, kBoxZ - .02}, kTrim);
    slab_ink(r, {-kBoxX - .03, -kBoxY, kBoxZ - .14}, {-kOpenX, kBoxY, kBoxZ - .02}, kTrim);
    slab_ink(r, {kOpenX, -kBoxY, kBoxZ - .14}, {kBoxX + .03, kBoxY, kBoxZ - .02}, kTrim);
    // the switch deck over the front half
    slab_ink(r, {-kOpenX, -kBoxY + t, kBoxZ - .1}, {kOpenX, kOpenY0, kBoxZ}, kDeck);
    // little bun feet
    for (int fx = -1; fx <= 1; fx += 2)
        for (int fy = -1; fy <= 1; fy += 2) {
            const M34 m = at({fx * (kBoxX - .35), fy * (kBoxY - .3), -.02}) * sc(.26, .2, .12);
            draw_mesh(r, sphere_mesh(10, 6), m, nullptr, kTrim, opaque);
        }
    // lid
    {
        const M34 L = lid_frame(s.lid);
        const double w = kOpenX + .05, d = kOpenY1 - kOpenY0 + .08;
        const M34 m = L * at({0, -d / 2, -.04}) * sc(w, d / 2, .1);
        draw_mesh(r, cube(), m, nullptr, kBoxPink, opaque);
        draw_outline(r, cube(), m, .02, kInkBox);
        const M34 m2 = L * at({0, -d / 2, .06}) * sc(w - .25, d / 2 - .2, .025);
        draw_mesh(r, cube(), m2, nullptr, kTrim, opaque);
        // the padded underside, seen when the lid is open
        const M34 m3 = L * at({0, -d / 2, -.045}) * sc(w - .12, d / 2 - .1, .01);
        draw_mesh(r, cube(), m3, &tx().front, hex(0xFFE6EC), opaque, 1);
        // a heart-shaped knob at the front of the lid
        const M34 k = L * at({0, -d + .12, .09}) * sc(.12, .08, .07);
        draw_mesh(r, sphere_mesh(10, 6), k, nullptr, hex(0xFF8FA8), opaque);
        draw_outline(r, sphere_mesh(10, 6), k, .015, kInkBox);
        // hinges
        for (int hx = -1; hx <= 1; hx += 2)
            draw_mesh(r, cylinder_mesh(8), at({hx * 1.3 - .15, kOpenY1 + .06, kBoxZ + .02}) * M34::rot_y(M_PI / 2) * sc(.05, .05, .3), nullptr, kSteel, opaque);
    }
}

void draw_loose_switch(R3D& r, int i, const M34& f) {
    // lever along +z from the frame origin, knob on top, a little plug at the bottom
    draw_mesh(r, cylinder_mesh(8), f * sc(.035, .035, Stage::kLever), nullptr, kSteel, opaque);
    const M34 k = f * at({0, 0, Stage::kLever}) * sc(.1, .1, .1);
    draw_mesh(r, sphere_mesh(12, 8), k, nullptr, kKnob[i], toon);
    draw_outline(r, sphere_mesh(12, 8), k, .016, shade(kKnob[i], .45f));
    const M34 b = f * at({0, 0, -.04}) * sc(.06, .06, .08);
    draw_mesh(r, cylinder_mesh(8), b, nullptr, kBase, opaque);
}

void Stage::draw_switch(int i, const SwitchVis& v, double t) {
    const double x = switch_x(i) + std::sin(t * 60) * .015 * v.wiggle;
    // base plate and socket
    const M34 plate = at({x, kSwitchY, kBoxZ - .005}) * sc(.18, .15, .05);
    draw_mesh(r, cube(), plate, nullptr, kBase, opaque);
    draw_outline(r, cube(), plate, .015, hex(0x2C1C30));
    if (v.sink < 1) {
        // sinking: the whole lever drops through its socket; the deck hides what is below
        const double drop = v.sink * v.sink * .62;
        if (v.sink <= 0) draw_mesh(r, hemisphere_mesh(10, 4), at({x, kSwitchY, kBoxZ + .04}) * sc(.07, .07, .05), nullptr, kSteel, opaque);
        const double a = kOff + (kOn - kOff) * std::clamp(v.on, 0.0, 1.0);
        draw_loose_switch(r, i, at({x, kSwitchY, kPivotZ - drop}) * M34::rot_x(a * (1 - v.sink)));
    }
    if (v.sink > 0) draw_mesh(r, disc_mesh(10), at({x, kSwitchY, kBoxZ + .051}) * sc(.065, .065, 1), nullptr, hex(0x150C16), unlit);
    // lamp: a glass dome in a steel bezel, with a glow when lit
    const V3 lp = lamp(i);
    draw_mesh(r, torus_mesh(12, 5, .3), at(lp + V3{0, 0, -.01}) * sc(.12, .12, .12), nullptr, kSteel, opaque);
    const float l = static_cast<float>(std::clamp(v.lamp, 0.0, 1.0));
    draw_mesh(r, hemisphere_mesh(12, 5), at(lp) * sc(.11, .11, .1), nullptr, mix(kLampOff, kLampOn, l), l > .05f ? unlit : opaque);
    draw_mesh(r, sphere_mesh(6, 4), at(lp + V3{-.035, -.03, .07}) * sc(.022, .022, .016), nullptr, mix(hex(0xF6E6CC), kWhite, l), unlit);
    if (l > .02f) r.billboard(lp + V3{0, 0, -.12}, .65, .5, &tx().glow, alpha(hex(0xFFD860), .55f * l), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
}

void Stage::render(const StageState& s, double t) {
    r.time = t;
    r.clear_depth();
    r.tris_drawn = 0;
    draw_room();
    draw_box(s);
    for (int i = 0; i < kSwitches; ++i) draw_switch(i, s.sw[static_cast<size_t>(i)], t);
    if (s.cursor) {
        // a camera-facing sprite whose top-left corner (the arrow's tip) sits on the knob
        const double w = .3, h = .3;
        const V3 p = s.cursor_at + r.right() * (w * .5) - V3{0, 0, h};
        r.billboard(p, w, h, &tx().pointer, kWhite, static_cast<std::uint16_t>(cutout | unlit | no_fog));
    }
    if (s.girl_visible) { draw_girl(r, s.girl, t); draw_fx(s, t); }
    if (s.held >= 0 && s.girl_visible) {
        const ArmPose a = solve_arm(s.girl, s.held_hand);
        const V3 up = norm(a.hand - a.wrist);
        const M34 f = M34::translate(a.hand.x, a.hand.y, a.hand.z) * M34::rot_x(std::atan2(-up.y, up.z) * .5) * at({0, 0, -.12});
        draw_loose_switch(r, s.held, f);
    }
}

void Stage::draw_fx(const StageState& s, double t) {
    const Fx& f = s.fx;
    const M34 h = head_frame(s.girl);
    const double k = s.girl.scale;
    const std::uint16_t spr = static_cast<std::uint16_t>(cutout | unlit | no_fog | no_depth_write);
    if (f.anger > .02) {
        const double pulse = 1 + .18 * std::sin(t * 14);
        const double sz = .2 * k * f.anger * pulse;
        r.billboard(h.apply({.34, -.1, .3}), sz, sz, &tx().anger, kWhite, spr);
    }
    if (f.sweat > .02) {
        const double slide = std::fmod(t * .6, 1.0);
        r.billboard(h.apply({-.42, -.2, .2 - .15 * slide}), .12 * k * f.sweat, .14 * k * f.sweat, &tx().sweat, kWhite, spr);
    }
    if (f.steam > .02)
        for (int i = 0; i < 4; ++i) {
            const double u = std::fmod(t * 1.3 + i * .25, 1.0);
            const double side = (i % 2 ? 1 : -1) * (.12 + .25 * u);
            const double sz = (.1 + .2 * u) * k * f.steam;
            r.billboard(h.apply({side, 0, .38 + .55 * u}), sz, sz, &tx().puff, alpha(kWhite, static_cast<float>(1 - u)), spr);
        }
    if (f.hearts > .02)
        for (int i = 0; i < 5; ++i) {
            const double u = std::fmod(t * .45 + i * .2, 1.0);
            const double x = std::sin(i * 2.4) * .75 + .08 * std::sin(t * 3 + i);
            const double sz = .17 * k * f.hearts * (1 - .4 * u);
            r.billboard(h.apply({x, -.3, -.2 + 1.1 * u}), sz, sz, &tx().heart, kWhite, spr);
        }
    if (f.sparkle > .02)
        for (int i = 0; i < 6; ++i) {
            const double tw = .5 + .5 * std::sin(t * 7 + i * 1.9);
            const double a = i * 1.047 + t * .4;
            const double sz = .14 * k * f.sparkle * tw;
            r.billboard(h.apply({std::cos(a) * .72, -.3, std::sin(a) * .62 - .05}), sz, sz, &tx().sparkle, kWhite,
                        static_cast<std::uint16_t>(additive | unlit | no_fog | no_depth_write));
        }
}

int Stage::pick_switch(double sx, double sy, const StageState& s) const {
    int best = -1;
    double bd = r.scale * .3;
    for (int i = 0; i < kSwitches; ++i) {
        if (s.sw[static_cast<size_t>(i)].sink > .05) continue;
        double ax, ay, bx, by;
        to_screen({switch_x(i), kSwitchY, kPivotZ}, ax, ay);
        to_screen(knob(i, s.sw[static_cast<size_t>(i)].on), bx, by);
        // distance to the lever segment on screen
        const double vx = bx - ax, vy = by - ay;
        const double u = std::clamp(((sx - ax) * vx + (sy - ay) * vy) / std::max(1e-9, vx * vx + vy * vy), 0.0, 1.0);
        const double d = std::hypot(sx - (ax + vx * u), sy - (ay + vy * u));
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

int Stage::pick_socket(double sx, double sy) const {
    int best = -1;
    double bd = r.scale * .3;
    for (int i = 0; i < kSwitches; ++i) {
        double ax, ay;
        to_screen({switch_x(i), kSwitchY, kBoxZ}, ax, ay);
        const double d = std::hypot(sx - ax, sy - ay);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

bool Stage::pick_head(double sx, double sy, const GirlPose& g) const {
    double hx, hy;
    to_screen(head_center(g), hx, hy);
    return std::hypot(sx - hx, (sy - hy) * 1.1) < r.scale * .5;
}

}  // namespace sbx
