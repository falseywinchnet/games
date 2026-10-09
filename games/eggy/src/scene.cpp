#include "scene.hpp"

#include "critters.hpp"
#include "flora.hpp"
#include "ground.hpp"
#include "render.hpp"
#include "textures.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace eggy {

namespace {
M34 at(double x, double y, double z) { return M34::translate(x, y, z); }
thread_local double g_zscale = 1;
thread_local bool g_fade = false;  // prop stands between camera and Eggy
std::uint8_t M(std::uint8_t m) { return g_fade ? static_cast<std::uint8_t>(translucent) : m; }
Col FD(Col c) { return g_fade ? alpha(c, .38f) : c; }
double Z(double z) { return z * g_zscale; }
M34 sc(double x, double y, double z) { return M34::scale(x, y, z); }
const Col kW{1, 1, 1, 1};
double h01(std::uint64_t a, std::uint64_t b) {
    std::uint64_t h = mix64(a * 0x9E3779B97F4A7C15ULL ^ mix64(b + 0x51));
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}
Col lerpc(Col a, Col b, double t) { return mix(a, b, static_cast<float>(std::clamp(t, 0.0, 1.0))); }
double approach(double cur, double target, double rate, double dt) { return cur + (target - cur) * std::min(1.0, dt * rate); }

// A soft contact shadow under a tree, bush or stone: tilted to the slope and
// pushed a little away from the sun, so props stand on the mountain instead
// of floating over it.
void ground_shadow(R3D& r, const World& w, double x, double y, double radius, float strength) {
    const double e = .3;
    const double gz = w.ground(x, y);
    const double dx = (w.ground(x + e, y) - w.ground(x - e, y)) / (2 * e) * g_zscale, dy = (w.ground(x, y + e) - w.ground(x, y - e)) / (2 * e) * g_zscale;
    const double sx = r.light.sun.x, sy = r.light.sun.y, sl = std::max(.2, std::hypot(sx, sy));
    const double ox = -sx / sl * radius * .22, oy = -sy / sl * radius * .22;
    M34 tilt;
    tilt.m[8] = dx;
    tilt.m[9] = dy;
    draw_mesh(r, disc_mesh(10), at(x + ox, y + oy, Z(gz) + .03 + dx * ox + dy * oy) * tilt * sc(radius, radius * .92, 1), &textures().shadow, hex(0x000000, strength),
              translucent | unlit);
}
}  // namespace

void Scene::resize(int w, int h) {
    r.resize(w, h);
    // Flowers and grasses are lit mostly from above, like the ground they stand on,
    // rather than edge-on to the sun, which leaves them dull and dark.
    r.billboard_lift = 2;
}

void Scene::spawn(Particle::Kind k, V3 p, int n, double spread, Col c) {
    if (reduced_motion && k != Particle::sparkle && k != Particle::confetti) n = (n + 2) / 3;
    for (int i = 0; i < n; ++i) {
        Particle q;
        q.kind = k;
        q.p = p + V3{(h01(parts.size() + i, 1 + static_cast<std::uint64_t>(time_ * 977)) - .5) * spread,
                     (h01(parts.size() + i, 2 + static_cast<std::uint64_t>(time_ * 977)) - .5) * spread, 0};
        const double a = h01(i, 3 + static_cast<std::uint64_t>(time_ * 1000)) * 6.283, s = .3 + h01(i, 4 + static_cast<std::uint64_t>(time_ * 1000));
        q.col = c;
        switch (k) {
            case Particle::dust: q.vel = {std::cos(a) * s * .6, std::sin(a) * s * .6, .3}; q.life = .6; q.size = .16; break;
            case Particle::splash: q.vel = {std::cos(a) * s, std::sin(a) * s, 1.6 + s}; q.life = .7; q.size = .07; break;
            case Particle::ripple: q.vel = {}; q.life = 1.2; q.size = .2; break;
            case Particle::sparkle: q.vel = {std::cos(a) * s * .8, std::sin(a) * s * .8, .6 + s}; q.life = 1.0; q.size = .14; break;
            case Particle::feather: q.vel = {std::cos(a) * .25, std::sin(a) * .25, .5}; q.life = 2.2; q.size = .09; break;
            case Particle::note: q.vel = {.15, .1, .7}; q.life = 1.4; q.size = .16; break;
            case Particle::sweat: q.vel = {std::cos(a) * .4, std::sin(a) * .4, 1.0}; q.life = .7; q.size = .06; break;
            case Particle::breath: q.vel = {.2, -.1, .25}; q.life = 1.3; q.size = .1; break;
            case Particle::snowpuff: q.vel = {std::cos(a) * s * .5, std::sin(a) * s * .5, .4}; q.life = .6; q.size = .12; break;
            case Particle::confetti: q.vel = {std::cos(a) * s, std::sin(a) * s, 2.5 + 2 * s}; q.life = 3.5; q.size = .07;
                q.col = std::array<Col, 5>{hex(0xE8323A), hex(0xFFD23F), hex(0x3FA9F5), hex(0x5FD068), hex(0xF59AC7)}[static_cast<size_t>(i % 5)]; break;
            case Particle::heart: q.vel = {0, 0, .6}; q.life = 1.2; q.size = .12; break;
            case Particle::starburst: q.vel = {std::cos(a) * 1.5 * s, std::sin(a) * 1.5 * s, 1.2 * s}; q.life = .9; q.size = .12; break;
        }
        q.spin = a;
        parts.push_back(q);
    }
    if (parts.size() > 600) parts.erase(parts.begin(), parts.begin() + static_cast<long>(parts.size() - 600));
}

void Scene::on_event(const Event& e, const Sim& s) {
    const V3 duck{s.d.u, s.d.v, s.d.z};
    switch (e.type) {
        case Ev::land:
            squash = .16;
            spawn(static_cast<Surface>(e.a) == Surface::snow ? Particle::snowpuff : Particle::dust, duck, 5, .3,
                  static_cast<Surface>(e.a) == Surface::snow ? hex(0xFFFFFF) : hex(0xC9B08A));
            break;
        case Ev::step:
            if (static_cast<Surface>(e.a) == Surface::snow && h01(static_cast<std::uint64_t>(time_ * 100), 7) < .5) spawn(Particle::snowpuff, duck, 1, .15, hex(0xFFFFFF));
            else if ((static_cast<Surface>(e.a) == Surface::path || static_cast<Surface>(e.a) == Surface::gravel) && h01(static_cast<std::uint64_t>(time_ * 100), 8) < .3)
                spawn(Particle::dust, duck, 1, .15, hex(0xC9B08A));
            break;
        case Ev::splash: spawn(Particle::splash, duck + V3{0, 0, .1}, 10, .2, hex(0x9ED8F5)); spawn(Particle::ripple, duck, 1, 0, hex(0xBDE9FF)); break;
        case Ev::swim_stroke: spawn(Particle::ripple, duck, 1, 0, hex(0xBDE9FF)); break;
        case Ev::refreshing: spawn(Particle::sparkle, duck + V3{0, 0, .7}, 12, .4, hex(0xBFE8FF)); spawn(Particle::heart, duck + V3{0, 0, 1.0}, 1, 0, hex(0xFF8FA0)); cheer = 1.2; break;
        case Ev::drink: spawn(Particle::ripple, duck + V3{std::cos(s.d.heading) * .5, std::sin(s.d.heading) * .5, 0}, 1, 0, hex(0xBDE9FF)); break;
        case Ev::star: spawn(Particle::starburst, duck + V3{0, 0, .6}, 18, .2, hex(0xFFD23F)); spawn(Particle::sparkle, duck + V3{0, 0, .8}, 10, .5, hex(0xFFF2A0)); cheer = 2.0; break;
        case Ev::preen: spawn(Particle::feather, duck + V3{-.1, 0, .5}, 2, .2, hex(0xFFE36B)); break;
        case Ev::flap: spawn(Particle::feather, duck + V3{0, 0, .5}, 1, .3, hex(0xFFE36B)); break;
        case Ev::chirp: spawn(Particle::note, duck + V3{0, 0, 1.0}, 1, 0, hex(0x6B4AC8)); break;
        case Ev::knocked: spawn(Particle::starburst, duck + V3{0, 0, .9}, 6, .1, hex(0xFFE14D)); break;
        case Ev::helped: helped_flash = 1.0; break;
        case Ev::lightning: flash_t_ = 0; flash_dist_ = e.x; bolt_seed_ = static_cast<unsigned>(time_ * 1000) | 1; break;
        case Ev::medal: spawn(Particle::sparkle, duck + V3{.3, 0, .4}, 14, .3, hex(0xFFE14D)); break;
        case Ev::title: spawn(Particle::confetti, duck + V3{.6, .6, 1.4}, 90, 1.4, kW); cheer = 8; break;
        default: break;
    }
}

// ------------------------------------------------------------------ animation
void Scene::animate(const Sim& s, double dt) {
    const Duck& d = s.d;
    Pose tg;
    const double t = rt_;
    tg.puff = 1 + d.cold * .1;
    const double sp = std::clamp(d.speed / 1.6, 0.0, 1.0);
    tg.step = d.walk_phase;
    tg.stepamp = d.air ? 0 : sp;
    tg.bob = -std::fabs(std::sin(d.walk_phase)) * 4 * sp;
    tg.tilt = std::sin(d.walk_phase) * .09 * sp;
    tg.eyes = Eyes::open;
    tg.swim = d.swim ? 1 : 0;
    if (d.swim) tg.bob = std::sin(t * 3) * 1.5;
    tg.beak = d.breath < .25 ? .35 + .3 * std::sin(t * 9) : 0;
    // breathing while standing
    const double breathe = std::sin(t * (d.breath < .4 ? 7 : 2.4));
    tg.sy = 1 + .02 * breathe;
    tg.sx = 1 - .01 * breathe;
    if (d.air) {
        const double st = std::clamp(d.vz * .05, -.14, .14);
        tg.sy = 1 + st; tg.sx = 1 - st * .5;
        tg.wing_near = tg.wing_far = d.vz > 0 ? 1.0 : .7 + .5 * std::sin(t * 25);
        tg.eyes = Eyes::wide;
    }
    if (squash > 0) { tg.sy = .82; tg.sx = 1.1; }
    if (d.sliding) {
        tg.wing_near = tg.wing_far = .9 + .6 * std::sin(t * 20);
        tg.lean = -.35; tg.eyes = Eyes::wide; tg.beak = .7;
    }
    if (d.brace > .3) { tg.lean = .4 * d.brace; tg.eyes = Eyes::squint; tg.helmet_tilt = -.2 * d.brace; }
    const double a = d.act_t;
    switch (d.act) {
        case Act::rest:
            tg.sit = 1; tg.eyes = (std::fmod(a, 3) < 2.2) ? Eyes::sleepy : Eyes::blink;
            tg.beak = .4 + .35 * std::sin(t * 9); tg.sy = 1 + .05 * std::sin(t * 9); tg.wing_near = tg.wing_far = -.15;
            break;
        case Act::drink:
            if (a < 1.9) {
                tg.lean = .9; tg.head_dy = 14 + 4 * std::sin(a * 12); tg.head_dx = 10; tg.beak = .25 + .2 * std::sin(a * 12); tg.eyes = Eyes::blink;
            } else {
                tg.eyes = Eyes::happy; tg.wing_near = tg.wing_far = (a < 2.7) ? .8 + .6 * std::sin(a * 22) : 0; tg.beak = .5;
            }
            break;
        case Act::preen:
            tg.head_yaw = 2.3 * std::sin(std::min(1.0, a * 2.5) * M_PI / 2) * (a < d.act_len - .4 ? 1 : 0);
            tg.head_dy = 8; tg.head_dx = -6; tg.beak = .25 + .25 * std::sin(t * 16); tg.wing_far = .35;
            tg.eyes = std::sin(t * 3) > 0 ? Eyes::blink : Eyes::happy;
            break;
        case Act::flap:
            tg.wing_near = tg.wing_far = 1.0 + .7 * std::sin(t * 24); tg.bob = -6 * std::fabs(std::sin(t * 7));
            tg.eyes = Eyes::happy; tg.beak = .4;
            break;
        case Act::look:
            tg.head_yaw = std::sin(a * 1.9) * 1.0; tg.head_tilt = .15 * std::sin(a * 1.3);
            break;
        case Act::chirp:
            tg.beak = std::sin(std::min(1.0, a / .5) * M_PI) * .9; tg.head_tilt = .25; tg.eyes = Eyes::happy;
            break;
        case Act::knocked: {
            const double up = a > d.act_len - .6 ? (d.act_len - a) / .6 : 1;
            tg.lie = std::clamp(up, 0.0, 1.0); tg.eyes = Eyes::dizzy; tg.helmet_tilt = -.7 * tg.lie; tg.helmet_lift = 8 * tg.lie;
            tg.wing_near = tg.wing_far = .6 + .4 * std::sin(t * 14);
            break;
        }
        case Act::shiver:
            tg.tilt = std::sin(t * 60) * .05; tg.puff = 1.14; tg.eyes = Eyes::squint; tg.beak = std::sin(t * 40) > 0 ? .25 : 0;
            break;
        case Act::fan:
            tg.wing_fwd = .45 + .2 * std::sin(t * 14); tg.wing_near = .4; tg.eyes = Eyes::squint; tg.beak = .3;
            break;
        case Act::ceremony: {
            const double c = s.ceremony_t;
            if (c > 9 && c < 13.5) tg.wing_fwd = 1.0, tg.wing_near = .5;
            tg.eyes = c > 14.5 ? Eyes::happy : Eyes::open;
            tg.medal = c > 11.5;
            if (c > 15) { tg.wing_near = tg.wing_far = .9 + .6 * std::sin(t * 20); tg.bob = -8 * std::fabs(std::sin(t * 6)); }
            break;
        }
        default: break;
    }
    if (s.finished) { tg.medal = true; tg.eyes = Eyes::happy; }
    if (salute > 0) { tg.wing_fwd = 1.0; tg.wing_near = .5; tg.eyes = salute < 1.2 ? Eyes::happy : Eyes::open; }
    if (cheer > 0 && d.act == Act::none) { tg.eyes = Eyes::happy; tg.wing_near = tg.wing_far = std::max(tg.wing_near, .6 + .5 * std::sin(t * 18)); }
    if (helped_flash > 0 && d.act == Act::none && tg.eyes == Eyes::open) tg.eyes = Eyes::wide;
    // blinking
    blink_t_ -= dt;
    if (blink_t_ <= 0) { blink_left_ = .13; blink_t_ = 1.8 + h01(static_cast<std::uint64_t>(t * 10), 5) * 3.5; }
    if (blink_left_ > 0) { blink_left_ -= dt; if (tg.eyes == Eyes::open || tg.eyes == Eyes::wide) tg.eyes = Eyes::blink; }
    // smooth everything (eyes switch instantly)
    auto sm = [&](double& v, double target, double rate) { v = approach(v, target, rate, dt); };
    sm(pose.bob, tg.bob, 20); sm(pose.sx, tg.sx, 18); sm(pose.sy, tg.sy, 18); sm(pose.tilt, tg.tilt, 16); sm(pose.lean, tg.lean, 8);
    sm(pose.head_yaw, tg.head_yaw, 7); sm(pose.head_dx, tg.head_dx, 9); sm(pose.head_dy, tg.head_dy, 9); sm(pose.head_tilt, tg.head_tilt, 8);
    sm(pose.beak, tg.beak, 18); sm(pose.wing_near, tg.wing_near, 22); sm(pose.wing_far, tg.wing_far, 22); sm(pose.wing_fwd, tg.wing_fwd, 10);
    sm(pose.sit, tg.sit, 7); sm(pose.lie, tg.lie, 9); sm(pose.swim, tg.swim, 6); sm(pose.puff, tg.puff, 4); sm(pose.helmet_tilt, tg.helmet_tilt, 6);
    sm(pose.helmet_lift, tg.helmet_lift, 6);
    pose.step = tg.step; pose.stepamp = approach(pose.stepamp, tg.stepamp, 10, dt);
    pose.eyes = tg.eyes; pose.medal = tg.medal;
    squash = std::max(0.0, squash - dt);
    cheer = std::max(0.0, cheer - dt);
    helped_flash = std::max(0.0, helped_flash - dt);
    salute = std::max(0.0, salute - dt);
}

// ------------------------------------------------------------------ sky
void Scene::sky(const Sim& s) {
    const int W = r.W, H = r.H;
    const double ph = phase_, sun = sun_;
    const double dusk = std::clamp(1 - std::fabs(sun - .35) / .35, 0.0, 1.0) * (sun < .9 ? 1 : 0);
    const double alt = std::clamp(s.progress() * 3, 0.0, 1.0);
    Col top = lerpc(lerpc(hex(0x070B1E), hex(0x3E7FD0), sun), hex(0x2A4E9C), dusk * .6);
    Col hor = lerpc(lerpc(hex(0x1B2A52), hex(0xBFE3F7), sun), hex(0xF2A65A), dusk);
    top = lerpc(top, hex(0x15306E), alt * sun * .5);
    for (int y = 0; y < H; ++y) {
        const Col c = lerpc(top, hor, std::pow(static_cast<double>(y) / H, .8));
        for (int x = 0; x < W; ++x) {
            float* o = r.rgb.data() + (static_cast<size_t>(y) * W + x) * 3;
            o[0] = c.r; o[1] = c.g; o[2] = c.b;
        }
    }
    auto disc2 = [&](double cx, double cy, double rad, Col c, float a, bool soft) {
        for (int y = std::max(0, static_cast<int>(cy - rad)); y <= std::min(H - 1, static_cast<int>(cy + rad)); ++y)
            for (int x = std::max(0, static_cast<int>(cx - rad)); x <= std::min(W - 1, static_cast<int>(cx + rad)); ++x) {
                const double d = std::hypot(x + .5 - cx, y + .5 - cy);
                if (d > rad) continue;
                const float k = soft ? a * static_cast<float>(1 - d / rad) * static_cast<float>(1 - d / rad) : a;
                float* o = r.rgb.data() + (static_cast<size_t>(y) * W + x) * 3;
                o[0] += (c.r - o[0]) * k; o[1] += (c.g - o[1]) * k; o[2] += (c.b - o[2]) * k;
            }
    };
    // stars at night
    const double nightk = std::clamp(1 - sun * 2, 0.0, 1.0);
    for (int i = 0; i < 140 && nightk > 0; ++i) {
        const int x = static_cast<int>(h01(i, 11) * W), y = static_cast<int>(h01(i, 12) * H * .6);
        const float tw = static_cast<float>(nightk * (.5 + .5 * std::sin(sky_t_ * (1 + h01(i, 13) * 3) + i)));
        float* o = r.rgb.data() + (static_cast<size_t>(y) * W + x) * 3;
        o[0] += (1 - o[0]) * tw; o[1] += (1 - o[1]) * tw; o[2] += (1 - o[2]) * tw;
    }
    // sun / moon arc
    const double sa = (ph - .25) * 2 * M_PI;  // 0 at sunrise
    const double sx = W * (.85 - .7 * (std::fmod(ph + 1 - .25, 1.0) / .5)), sy = H * (.62 - .5 * std::sin(sa));
    if (sun > 0) { disc2(sx, sy, 22, hex(0xFFF0B0), .35f * static_cast<float>(sun), true); disc2(sx, sy, 7, hex(0xFFF7D0), 1, false); }
    else {
        const double mx = W * (.85 - .7 * (std::fmod(ph + 1 - .75, 1.0) / .5)), my = H * (.62 - .5 * std::sin((ph - .75) * 2 * M_PI));
        disc2(mx, my, 14, hex(0xC9D6FF), .25f, true);
        disc2(mx, my, 5.5, hex(0xEEF2FF), 1, false);
        disc2(mx + 1.5, my - 1, 1.3, hex(0xC9CFE0), 1, false);
    }
    // distant ranges (parallax) and The Peak
    const double prog = s.progress();
    const double peak_h = H * (.18 + .3 * prog);
    {
        const double px = W * .78;
        for (int x = 0; x < W; ++x) {
            const double d = std::fabs(x - px) / (W * .45);
            const int top_y = static_cast<int>(H * .62 - peak_h * std::max(0.0, 1 - d * d * 1.6) - 2 * std::sin(x * .3));
            for (int y = std::max(0, top_y); y < H; ++y) {
                const double snowline = H * .62 - peak_h * .55;
                Col c = y < snowline ? hex(0xE8F0FA) : hex(0x8FA3C4);
                c = lerpc(c, hor, .55);
                float* o = r.rgb.data() + (static_cast<size_t>(y) * W + x) * 3;
                o[0] = c.r * (.6f + .4f * static_cast<float>(sun)); o[1] = c.g * (.6f + .4f * static_cast<float>(sun)); o[2] = c.b * (.65f + .35f * static_cast<float>(sun));
            }
        }
    }
    for (int layer = 0; layer < 3; ++layer) {
        const double par = (layer + 1) * .9;
        const double base = H * (.58 + layer * .08);
        const Col lc = lerpc(lerpc(hex(0x5B6F99), hex(0x3E5A3E), layer / 2.0), hor, .55 - layer * .18);
        for (int x = 0; x < W; ++x) {
            const double X = x + sky_v_ * par + layer * 1000;
            const double hgt = 14 * std::sin(X * .021) + 9 * std::sin(X * .057 + 1) + 5 * std::sin(X * .13 + 2) + (layer == 0 ? 10 : 0);
            const int top_y = static_cast<int>(base - hgt);
            for (int y = std::max(0, top_y); y < H; ++y) {
                Col c = lc;
                if (layer == 0 && y < top_y + 4 && hgt > 18) c = lerpc(hex(0xF4F8FF), hor, .4);
                const float k = static_cast<float>(.55 + .45 * sun);
                float* o = r.rgb.data() + (static_cast<size_t>(y) * W + x) * 3;
                o[0] = c.r * k; o[1] = c.g * k; o[2] = c.b * (k + .08f);
            }
        }
    }
    // a sea of clouds far below (seen past the cliff edge)
    {
        const Col cs = lerpc(lerpc(hex(0x39406A), hex(0xF4F7FF), sun), hex(0xFFD2B0), dusk * .6);
        for (int i = 0; i < 46; ++i) {
            const double bx = std::fmod(h01(i, 61) * (W + 80) - sky_t_ * 1.5 - sky_v_ * 1.6, W + 80.0);
            const double by = H * (.74 + h01(i, 62) * .3);
            const double rad = 10 + h01(i, 63) * 14;
            disc2((bx < 0 ? bx + W + 80 : bx) - 40, by + 3, rad, shade(cs, .8f), .9f, false);
            disc2((bx < 0 ? bx + W + 80 : bx) - 40, by, rad, cs, .95f, false);
        }
    }
    // pixel clouds
    for (int i = 0; i < 7; ++i) {
        const double speed = 2 + h01(i, 21) * 3;
        double cx = std::fmod(h01(i, 22) * (W + 160) - sky_t_ * speed - sky_v_ * .6 * (1 + i % 3), W + 160.0);
        if (cx < 0) cx += W + 160;
        cx -= 80;
        const double cy = H * (.08 + h01(i, 23) * .32);
        const Col cc = lerpc(lerpc(hex(0x40476B), hex(0xFFFFFF), sun), hex(0xFFC9A0), dusk * .5);
        for (int k = 0; k < 6; ++k) {
            const double bx = cx + (k - 2.5) * 7, by = cy - 4 * std::sin(k * 1.3) - (k == 2 || k == 3 ? 5 : 0);
            disc2(bx, by + 2, 7 + (k % 3), shade(cc, .82f), .9f, false);
            disc2(bx, by, 7 + (k % 3), cc, .95f, false);
        }
    }
}

void Scene::storm_sky(const Sim& s) {
    const double st = s.storm;
    if (st < .02 && flash_t_ > .4) return;
    const int W = r.W, H = r.H;
    // heavy slate cloud deck
    for (int y = 0; y < H; ++y) {
        const float k = static_cast<float>(st * (.75 - .35 * y / H));
        for (int x = 0; x < W; ++x) {
            const double n = .5 + .5 * std::sin(x * .045 + y * .11 + sky_t_ * .3) * std::sin(x * .021 - y * .07 + 1.3);
            const Col c = lerpc(hex(0x2E333C), hex(0x59616E), n);
            float* o = r.rgb.data() + (static_cast<size_t>(y) * W + x) * 3;
            o[0] += (c.r - o[0]) * k; o[1] += (c.g - o[1]) * k; o[2] += (c.b - o[2]) * k;
        }
    }
    // lightning: a jagged bolt and a flash of the whole sky
    if (flash_t_ < .4) {
        const float fk = static_cast<float>((1 - flash_t_ / .4) * (1 - .55 * flash_dist_));
        for (size_t i = 0; i < r.rgb.size(); ++i) r.rgb[i] += (1 - r.rgb[i]) * fk * .45f;
        if (flash_t_ < .2) {
            std::uint64_t hs = mix64(bolt_seed_);
            auto rnd = [&]() { hs = mix64(hs); return static_cast<double>(hs >> 11) * (1.0 / 9007199254740992.0); };
            double x = W * (.15 + .7 * rnd()), y = 0;
            const double ground_y = H * (.35 + .25 * flash_dist_);
            while (y < ground_y) {
                const double nx = x + (rnd() - .5) * 10, ny = y + 3 + rnd() * 6;
                for (int k = 0; k <= 8; ++k) {
                    const int px = static_cast<int>(x + (nx - x) * k / 8), py = static_cast<int>(y + (ny - y) * k / 8);
                    for (int dx = 0; dx <= (flash_dist_ < .5 ? 1 : 0); ++dx)
                        if (px + dx >= 0 && px + dx < W && py >= 0 && py < H) {
                            float* o = r.rgb.data() + (static_cast<size_t>(py) * W + px + dx) * 3;
                            o[0] = 1; o[1] = 1; o[2] = 1;
                        }
                }
                if (rnd() < .12) {  // a little fork
                    double bx = nx, by = ny;
                    for (int j = 0; j < 4; ++j) {
                        const double cx = bx + (rnd() - .3) * 8, cy = by + 3 + rnd() * 4;
                        for (int k = 0; k <= 6; ++k) {
                            const int px = static_cast<int>(bx + (cx - bx) * k / 6), py = static_cast<int>(by + (cy - by) * k / 6);
                            if (px >= 0 && px < W && py >= 0 && py < H) { float* o = r.rgb.data() + (static_cast<size_t>(py) * W + px) * 3; o[0] = .85f; o[1] = .85f; o[2] = 1; }
                        }
                        bx = cx; by = cy;
                    }
                }
                x = nx; y = ny;
            }
        }
    }
}

// ------------------------------------------------------------------ terrain
namespace {
}  // namespace

void Scene::terrain(const Sim& s) {
    const Textures& T = textures();
    const Ground& G = ground();
    const World& w = s.world;
    const double span_back = 14 / zoom, span_fwd = 22 / zoom;
    const std::int64_t v0 = std::max<std::int64_t>(0, static_cast<std::int64_t>(cam_v - span_back));
    const std::int64_t v1 = static_cast<std::int64_t>(cam_v + span_fwd);
    const int u0 = -4, u1 = kWidth + 13;
    std::vector<Vtx> water_tris;
    water_spots_.clear();
    // ground ids for the visible area (one ring larger, for blending)
    const int gw = u1 - u0 + 2;
    const std::int64_t gv0 = v0 - 1;
    const int gh = static_cast<int>(v1 - v0 + 3);
    looks_.assign(static_cast<size_t>(gw * gh), 0);
    for (int gy = 0; gy < gh; ++gy) {
        const std::int64_t v = std::max<std::int64_t>(0, gv0 + gy);
        for (int gx = 0; gx < gw; ++gx) {
            const int u = u0 - 1 + gx;
            std::uint8_t lk;
            if (u >= 0 && u < kWidth) lk = w.tile(u, v).look;
            else {
                const double dz = std::fabs(w.ground(u + 1, static_cast<double>(v) + .5) - w.ground(u, static_cast<double>(v) + .5));
                lk = w.flank_look(u + .5, v, dz > .9);
            }
            looks_[static_cast<size_t>(gy * gw + gx)] = lk;
        }
    }
    auto look_at = [&](int u, std::int64_t v) -> int {
        const int gx = u - (u0 - 1);
        const int gy = static_cast<int>(v - gv0);
        if (gx < 0 || gy < 0 || gx >= gw || gy >= gh) return -1;
        return looks_[static_cast<size_t>(gy * gw + gx)];
    };
    // corner heights for the visible area (one ring larger) -> smooth vertex normals
    const int hw = gw + 1, hh = gh + 1;
    heights_.assign(static_cast<size_t>(hw * hh), 0.0);
    for (int gy = 0; gy < hh; ++gy) {
        const std::int64_t v = gv0 + gy;
        for (int gx = 0; gx < hw; ++gx) {
            const int u = u0 - 1 + gx;
            double z;
            if (v < 0) z = 0;
            else if (u >= 0 && u < kWidth) z = w.tile(u, v).z[0];
            else if (u == kWidth && v >= 0) z = w.tile(kWidth - 1, v).z[1];
            else z = w.ground(u, static_cast<double>(v));
            heights_[static_cast<size_t>(gy * hw + gx)] = z;
        }
    }
    auto hz_at = [&](int u, std::int64_t v) {
        const int gx = std::clamp(u - (u0 - 1), 0, hw - 1), gy = std::clamp(static_cast<int>(v - gv0), 0, hh - 1);
        return heights_[static_cast<size_t>(gy * hw + gx)];
    };
    auto normal_at = [&](int u, std::int64_t v) {
        const double dzu = (hz_at(u + 1, v) - hz_at(u - 1, v)) * .5 * r.height_scale;
        const double dzv = (hz_at(u, v + 1) - hz_at(u, v - 1)) * .5 * r.height_scale;
        return norm(V3{-dzu, -dzv, 1});
    };
    // near-to-far so hidden terrain fails the depth test before texturing
    for (std::int64_t v = v0; v <= v1; ++v) {
        const Row& row = w.row(v);
        const SegmentInfo seg = w.segment(v / kSegment);
        for (int u = u1 - 1; u >= u0; --u) {
            double z[4];
            bool water = false;
            std::uint8_t var;
            if (u >= 0 && u < kWidth) {
                const Tile& t = row.t[static_cast<size_t>(u)];
                for (int i = 0; i < 4; ++i) z[i] = t.z[i];
                var = t.variant;
                water = t.surface == Surface::water;
            } else {
                z[0] = w.ground(u, static_cast<double>(v));
                z[1] = w.ground(u + 1, static_cast<double>(v));
                z[2] = w.ground(u + 1, static_cast<double>(v) + .999);
                z[3] = w.ground(u, static_cast<double>(v) + .999);
                var = static_cast<std::uint8_t>(h01(static_cast<std::uint64_t>(v) * 31 + static_cast<std::uint64_t>(u + 40), 3) * 255);
            }
            // cull quickly: project corners
            double minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
            const double cu[4] = {0, 1, 1, 0}, cvv[4] = {0, 0, 1, 1};
            for (int i = 0; i < 4; ++i) {
                double sx, sy, sz;
                r.project({u + cu[i], static_cast<double>(v) + cvv[i], z[i]}, sx, sy, sz);
                minx = std::min(minx, sx); maxx = std::max(maxx, sx); miny = std::min(miny, sy); maxy = std::max(maxy, sy);
            }
            if (maxx < -2 || minx > r.W + 2 || maxy < -2 || miny > r.H + 40) continue;
            const V3 p0{static_cast<double>(u), static_cast<double>(v), z[0]}, p1{u + 1.0, static_cast<double>(v), z[1]},
                     p2{u + 1.0, v + 1.0, z[2]}, p3{static_cast<double>(u), v + 1.0, z[3]};
            const V3 n = norm(cross(p1 - p0, p3 - p0));
            const V3 n0 = normal_at(u, v), n1 = normal_at(u + 1, v), n2 = normal_at(u + 1, v + 1), n3 = normal_at(u, v + 1);
            // this tile's ground and the most common different neighbour, blended at the corners
            const int A = look_at(u, v);
            int B = -1, bestc = 0;
            {
                int ids[8], cnt[8], m = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (!dx && !dy) continue;
                        const int l = look_at(u + dx, v + dy);
                        if (l < 0 || l == A) continue;
                        int k = 0;
                        while (k < m && ids[k] != l) ++k;
                        if (k == m) { ids[m] = l; cnt[m] = 0; ++m; }
                        if (++cnt[k] > bestc) { bestc = cnt[k]; B = l; }
                    }
            }
            float cw[4] = {0, 0, 0, 0};
            if (B >= 0)
                for (int i = 0; i < 4; ++i) {
                    const int cx = u + static_cast<int>(cu[i]);
                    const std::int64_t cy = v + static_cast<std::int64_t>(cvv[i]);
                    int na = 0, nb = 0;
                    for (int k = 0; k < 4; ++k) {
                        const int l = look_at(cx - 1 + (k & 1), cy - 1 + (k >> 1));
                        if (l == A) ++na; else if (l == B) ++nb;
                    }
                    cw[i] = na + nb ? static_cast<float>(nb) / static_cast<float>(na + nb) : 0.f;
                }
            const float vk = .975f + .05f * static_cast<float>(var) / 255.f;
            Col c{vk, vk, vk, 1};
            if (water) c = {.55f, .58f, .52f, 1};
            const double ts = .5;
            Vtx q[6] = {{p0, n0, p0.x * ts, -p0.y * ts, c, cw[0]}, {p1, n1, p1.x * ts, -p1.y * ts, c, cw[1]}, {p2, n2, p2.x * ts, -p2.y * ts, c, cw[2]},
                        {p0, n0, p0.x * ts, -p0.y * ts, c, cw[0]}, {p2, n2, p2.x * ts, -p2.y * ts, c, cw[2]}, {p3, n3, p3.x * ts, -p3.y * ts, c, cw[3]}};
            r.draw(q, 6, &G.tex[static_cast<size_t>(std::max(0, A))], opaque, nullptr, B >= 0 ? &G.tex[static_cast<size_t>(B)] : nullptr, &G.splat);
            // water: soft organic shoreline from per-corner water fractions
            {
                float wf[4];
                bool any = false;
                for (int i = 0; i < 4; ++i) {
                    const int cx = u + static_cast<int>(cu[i]);
                    const std::int64_t cy = v + static_cast<std::int64_t>(cvv[i]);
                    int nw = 0;
                    for (int k = 0; k < 4; ++k) {
                        const int tu = cx - 1 + (k & 1);
                        const std::int64_t tv = cy - 1 + (k >> 1);
                        if (tu >= 0 && tu < kWidth && tv >= 0 && w.tile(tu, tv).surface == Surface::water) ++nw;
                    }
                    wf[i] = nw / 4.f;
                    any = any || nw > 0;
                }
                if (any && water && !(u >= 0 && u < kWidth && row.t[static_cast<size_t>(u)].lane))
                    water_spots_.push_back({u + .5, v + .5, (z[0] + z[2]) * .5 + .07});
                if (any) {
                    const double o = time_ * (seg.brook ? .35 : .05);
                    const V3 up{0, 0, 1};
                    auto W2 = [&](const V3& p, float f) {
                        const float a = .88f * std::clamp((f - .2f) / .5f, 0.f, 1.f);
                        return Vtx{{p.x, p.y, p.z + .07}, up, p.x * .5 + o, -p.y * .5, {1, 1, 1, a}};
                    };
                    Vtx q2[6] = {W2(p0, wf[0]), W2(p1, wf[1]), W2(p2, wf[2]), W2(p0, wf[0]), W2(p2, wf[2]), W2(p3, wf[3])};
                    water_tris.insert(water_tris.end(), q2, q2 + 6);
                }
            }
        }
    }
    // footprints in the snow
    for (const Footprint& f : s.prints) {
        const double fade = std::clamp(1 - f.age / 240, 0.0, 1.0);
        const double ca = std::cos(f.angle), sa = std::sin(f.angle);
        const double gz = w.ground(std::clamp(f.u, 0.0, kWidth - .01), f.v) + .015;
        auto P = [&](double x, double y) { return V3{f.u + ca * x - sa * y, f.v + sa * x + ca * y, gz}; };
        const Col fc{.55f, .62f, .78f, static_cast<float>(.55 * fade)};
        const V3 up{0, 0, 1};
        Vtx q[6] = {{P(-.05, -.03), up, 0, 0, fc}, {P(.06, -.03), up, 0, 0, fc}, {P(.06, .03), up, 0, 0, fc},
                    {P(-.05, -.03), up, 0, 0, fc}, {P(.06, .03), up, 0, 0, fc}, {P(-.05, .03), up, 0, 0, fc}};
        r.draw(q, 6, nullptr, translucent | unlit);
    }
    const int frame = static_cast<int>(time_ * 4) & 3;
    if (!water_tris.empty()) r.draw(water_tris.data(), water_tris.size(), &T.water[static_cast<size_t>(frame)], translucent);
}

// ------------------------------------------------------------------ props
namespace {
}  // namespace

void Scene::props(const Sim& s) {
    const Textures& T = textures();
    const Flora& F = flora();
    const World& w = s.world;
    const double wind = s.wind_vis;
    const float shadow_k = static_cast<float>(.3 + .18 * sun_);  // contact shadows: firmer in daylight
    const std::int64_t v0 = std::max<std::int64_t>(0, static_cast<std::int64_t>(cam_v - 14 / zoom));
    const std::int64_t v1 = static_cast<std::int64_t>(cam_v + 22 / zoom);
    auto hsh = [&](int u, std::int64_t v, unsigned salt) {
        return static_cast<unsigned>(mix64(static_cast<std::uint64_t>(v) * 0x9E3779B97F4A7C15ULL ^ static_cast<std::uint64_t>(u + 64) * 0xC2B2AE3D27D4EB4FULL ^ salt) >> 20);
    };
    auto visible = [&](double x, double y, double z) {
        double sx, sy, sz;
        r.project({x, y, z}, sx, sy, sz);
        return !(sx < -40 || sx > r.W + 40 || sy < -10 || sy > r.H + 80);
    };
    for (std::int64_t v = v1; v >= v0; --v) {
        const Row& row = w.row(v);
        // rare background life on this row (seen from afar, never met)
        const unsigned rv = hsh(0, v, 77);
        const Biome rb = row.biome;
        const bool grassy = rb == Biome::meadow || rb == Biome::pond || rb == Biome::forest || rb == Biome::autumn || rb == Biome::alpine;
        const bool deer_row = grassy && v > static_cast<std::int64_t>(kCampEnd) + 20 && rv % 420 == 3;
        const bool cat_row = (rb == Biome::meadow || rb == Biome::pond || rb == Biome::forest) && v > 60 && rv % 640 == 11;
        const bool fall_row = (rb == Biome::alpine || rb == Biome::ravine || rb == Biome::forest || rb == Biome::autumn) && v % 3 == 0 &&
                              hsh(1, v, 78) % 380 == 5;
        if (deer_row) {
            const double dx = kWidth + 2.2 + ((rv >> 9) % 10) * .12, dy = v + .5;
            if (visible(dx, dy, w.ground(dx, dy))) draw_deer(r, dx, dy, Z(w.ground(dx, dy)), .4 + ((rv >> 4) % 5) * .5, rt_, static_cast<int>(rv >> 12));
        }
        if (cat_row) {
            const bool near_side = (rv >> 7) % 2;
            const double cx = near_side ? kWidth + 1.6 : -.9, cy = v + .5;
            if (visible(cx, cy, w.ground(cx, cy))) draw_cat(r, cx, cy, Z(w.ground(cx, cy)), near_side ? 2.6 : .3, rt_, static_cast<int>(rv >> 10));
        }
        if (fall_row) {  // a waterfall tumbling down the far slope into a little pool
            const Textures& TT = textures();
            std::vector<Vtx> cascade;
            const double y0 = v + .1, y1 = v + .9, fo = -rt_ * 1.6;
            const double u_start = kWidth + .1, u_end = kWidth + 6.5;
            for (int i = 0; i < 13; ++i) {
                const double ua = u_start + i * .5, ub = ua + .5;
                const double za0 = w.ground(ua, y0) + .04, za1 = w.ground(ua, y1) + .04, zb0 = w.ground(ub, y0) + .04, zb1 = w.ground(ub, y1) + .04;
                const V3 n{0, 0, 1};
                const Col wc{1, 1, 1, .9f};
                Vtx q[6] = {{{ua, y0, za0}, n, 0, -ua * .5 + fo, wc}, {{ub, y0, zb0}, n, 0, -ub * .5 + fo, wc}, {{ub, y1, zb1}, n, .5, -ub * .5 + fo, wc},
                            {{ua, y0, za0}, n, 0, -ua * .5 + fo, wc}, {{ub, y1, zb1}, n, .5, -ub * .5 + fo, wc}, {{ua, y1, za1}, n, .5, -ua * .5 + fo, wc}};
                cascade.insert(cascade.end(), q, q + 6);
            }
            r.draw(cascade.data(), cascade.size(), &TT.water[static_cast<size_t>(static_cast<int>(rt_ * 6) & 3)], translucent);
            for (int k = 0; k < 6; ++k) {  // foam tumbling down, and mist at the pool
                const double fu = u_start + std::fmod(rt_ * 1.6 + k * 1.1, u_end - u_start), fz = w.ground(fu, v + .5) + .07;
                r.billboard({fu, v + .5, fz}, .24, .16, &TT.puff, hex(0xFFFFFF, .6f), translucent | unlit);
            }
            r.billboard({u_end + .2, v + .5, w.ground(u_end + .2, v + .5) + .02}, 1.0, .55, &TT.puff, hex(0xE8F4FF, .45f), translucent | unlit);
            draw_mesh(r, disc_mesh(12), at(u_end + .3, v + .5, Z(w.ground(u_end + .3, v + .5)) + .03) * sc(.7, .55, 1), &TT.water[static_cast<size_t>(static_cast<int>(rt_ * 3) & 3)], hex(0xFFFFFF, .9f), translucent, 1);
            near_waterfall = std::max(near_waterfall, std::clamp(1 - std::fabs(static_cast<double>(v) - s.d.v) / 8, 0.0, 1.0));
        }
        // the mountainside beyond the path: woods, bushes and rocks
        for (int u = -4; u < kWidth + 13; ++u) {
            if (u >= 0 && u < kWidth) continue;
            if ((deer_row && u >= kWidth + 1 && u <= kWidth + 4) || (fall_row && u >= kWidth && u <= kWidth + 7) || (cat_row && (u == -1 || u == kWidth + 1))) continue;
            if (v < static_cast<std::int64_t>(kCampEnd) + 4 && u >= -3 && u <= -1) continue;  // the camp's tents stand here
            const double hh = h01(static_cast<std::uint64_t>(v) * 131 + static_cast<std::uint64_t>(u + 50), 9);
            const double dens = u < 0 ? .34 : .16;
            if (hh > dens) continue;
            const double fx = u + .3 + .4 * h01(static_cast<std::uint64_t>(v), static_cast<std::uint64_t>(u + 70));
            const double fy = v + .3 + .4 * h01(static_cast<std::uint64_t>(u + 90), static_cast<std::uint64_t>(v));
            const double gz = w.ground(fx, fy);
            if (!visible(fx, fy, gz)) continue;
            const unsigned hv = hsh(u, v, 1);
            const double sway = std::sin(time_ * 1.5 + fx) * .05 * (wind + .1);
            g_fade = fx > s.d.u + .25 && std::fabs(fy - s.d.v) < 3 && fx - s.d.u < 7;
            const bool snowy = row.biome == Biome::snow || row.biome == Biome::ice || row.biome == Biome::ridge || row.biome == Biome::summit;
            const int kind = static_cast<int>(hv % 10);
            if (kind < 6) {
                const bool woods = rb == Biome::forest || rb == Biome::autumn || rb == Biome::alpine;
                if (fall_v_ < 0 && woods && u < 0 && rt_ > next_fall_ && std::fabs(fy - s.d.v) < 8) {
                    fall_v_ = v; fall_u_ = u; fall_start_ = rt_; fall_dir_ = (hv % 2 ? .5 : -.7);
                    sounds.push_back({"eggy_tree_fall", .7f});
                }
                const bool falling = fall_v_ == v && fall_u_ == u;
                if (falling) {
                    const double k = std::clamp((rt_ - fall_start_) / 2.4, 0.0, 1.0);
                    const double ang = k * k * 1.45;
                    if (k >= 1 && rt_ - fall_start_ < 2.45) spawn(Particle::dust, {fx + std::cos(fall_dir_) * 1.2, fy + std::sin(fall_dir_) * 1.2, gz}, 14, .9, hex(0xB8A07A));
                    g_tree_pre = at(fx, fy, Z(gz)) * M34::rot_z(fall_dir_) * M34::rot_x(-ang) * M34::rot_z(-fall_dir_) * at(-fx, -fy, -Z(gz));
                }
                const TreeLook look = tree_look(row.biome, TreeSite::flank, fx, fy, hv);
                if (!falling) ground_shadow(r, w, fx, fy, tree_crown(look, 1.15) * .85 + .12, shadow_k);
                draw_tree(r, look, fx, fy, Z(gz), falling ? 0 : sway, 1.15, snowy, M(opaque), FD(kW));
                g_tree_pre = M34{};
            }
            else if (kind < 8 && !snowy) {
                ground_shadow(r, w, fx, fy, F.bushes[hv % kBushes].radius * 1.5 + .06, shadow_k);
                draw_bush(r, F.bushes[hv % kBushes], fx, fy, Z(gz), sway, hv, M(opaque), FD(kW));
            } else {
                const RockSpecies& rk = F.rocks[static_cast<size_t>(rock_for(row.biome, hv))];
                const double size = .45 + .2 * (hv % 3);
                ground_shadow(r, w, fx, fy, size * std::max(rk.sx, rk.sy) * 1.05, shadow_k);
                draw_rock(r, rk, fx, fy, Z(gz), size, snowy, M(opaque), FD(kW));
            }
            g_fade = false;
        }
        for (int u = 0; u < kWidth; ++u) {
            const Tile& t = row.t[static_cast<size_t>(u)];
            const bool snowy = t.biome == Biome::snow || t.biome == Biome::ice || t.biome == Biome::ridge || t.biome == Biome::summit;
            const double fx = u + t.fx, fy = v + t.fy;
            if (t.feature == Feature::none && w.star_at(u, v) < 0) continue;
            const double gz = w.ground(fx, fy);
            if (!visible(fx, fy, gz)) continue;
            const unsigned hv = hsh(u, v, 2);
            const double sway = std::sin(time_ * 1.6 + fx * .7 + fy) * .06 * (wind + .15);
            g_fade = (t.feature == Feature::tree || t.feature == Feature::pine || t.feature == Feature::boulder || t.feature == Feature::bush) &&
                     fx > s.d.u + .25 && std::fabs(fy - s.d.v) < 2.2 && fx - s.d.u < 4.5;
            switch (t.feature) {
                case Feature::tree: case Feature::pine:
                {
                    const TreeLook look = tree_look(t.biome, t.feature == Feature::pine ? TreeSite::conifer : TreeSite::broadleaf, fx, fy, hv);
                    ground_shadow(r, w, fx, fy, tree_crown(look, 1.0) * .85 + .12, shadow_k);
                    draw_tree(r, look, fx, fy, Z(gz), sway, 1.0, snowy, M(opaque), FD(kW));
                }
                    break;
                case Feature::bush:
                    ground_shadow(r, w, fx, fy, F.bushes[hv % kBushes].radius * 1.5 + .06, shadow_k);
                    draw_bush(r, F.bushes[hv % kBushes], fx, fy, Z(gz), sway, hv, M(opaque), FD(kW));
                    break;
                case Feature::boulder: {
                    const RockSpecies& rk = F.rocks[static_cast<size_t>(rock_for(t.biome, hv))];
                    ground_shadow(r, w, fx, fy, .36 * std::max(rk.sx, rk.sy) * 1.05, shadow_k);
                    draw_rock(r, rk, fx, fy, Z(gz), .36, snowy, M(opaque), FD(kW));
                    break;
                }
                case Feature::rock: draw_rock(r, F.rocks[static_cast<size_t>(rock_for(t.biome, hv))], fx, fy, Z(gz), .2, false, opaque, kW); break;
                case Feature::pebble:
                    for (int k = 0; k < 2 + static_cast<int>(hv % 3); ++k) {
                        const double px = fx + (k - 1) * .13, py = fy + ((hv >> k) % 3 - 1) * .09;
                        draw_rock(r, F.rocks[(hv + k * 37) % kRocks], px, py, Z(w.ground(px, py)), .045 + .015 * k, false, opaque, kW);
                    }
                    break;
                case Feature::cairn:
                    for (int k = 0; k < 4; ++k) draw_rock(r, F.rocks[(hv + k) % kRocks], fx, fy, Z(gz) + k * .13, .14 - k * .025, snowy && k == 3, opaque, kW);
                    break;
                case Feature::stump:
                    draw_mesh(r, cylinder_mesh(8), at(fx, fy, Z(gz) - .05) * sc(.2, .2, .28), &F.bark[hv % 8], kW, opaque, 1);
                    draw_mesh(r, disc_mesh(8), at(fx, fy, Z(gz) + .23) * sc(.2, .2, 1), &T.wood_end, kW, opaque, 1);
                    break;
                case Feature::log: {
                    const LogSpecies& ls = F.logs[static_cast<size_t>(mix64(static_cast<std::uint64_t>(v) * 77) % kLogs)];
                    const bool left_end = u == 0 || row.t[static_cast<size_t>(u - 1)].feature != Feature::log;
                    const bool right_end = u == kWidth - 1 || row.t[static_cast<size_t>(u + 1)].feature != Feature::log;
                    const double z0 = Z(w.ground(u + .5, v + .5)) + ls.radius;
                    draw_log(r, ls, u + (left_end ? .08 : -.01), u + (right_end ? .92 : 1.01), v + .5, z0, left_end, right_end);
                    if (left_end && mix64(static_cast<std::uint64_t>(v) * 991) % 6 == 0) {  // now and then, a frog
                        draw_frog(r, u + .65, v + .5, z0 + ls.radius * .95, .4, rt_, static_cast<int>(v % 7));
                        if (std::fabs(static_cast<double>(v) - s.d.v) < 7 && rt_ > next_ribbit_) {
                            next_ribbit_ = rt_ + 4 + (static_cast<int>(rt_ * 10) % 5);
                            sounds.push_back({"eggy_ribbit", .45f});
                        }
                    }
                    break;
                }
                case Feature::mushroom: draw_mushrooms(r, F.mushrooms[hv % kMushrooms], fx, fy, Z(gz), hv); break;
                case Feature::crate:
                    draw_mesh(r, box_mesh(), at(fx, fy, Z(gz)) * M34::rot_z(hv * .3) * sc(.16, .16, .26), &F.bark[1], hex(0xE0B880), opaque, .5);
                    break;
                case Feature::campfire: {
                    draw_campfire(r, T, u + .5, v + .5, Z(w.ground(u + .5, v + .5)), rt_);
                    near_fire = std::max(near_fire, std::clamp(1 - std::hypot(u + .5 - s.d.u, v + .5 - s.d.v) / 7, 0.0, 1.0));
                    if (rt_ - smoke_t_ > .45) { smoke_t_ = rt_; spawn(Particle::breath, {u + .5, v + .5, w.ground(u + .5, v + .5) + .35}, 1, .1, hex(0xBFBFBF)); }
                    break;
                }
                case Feature::flower: {
                    const int sp = static_cast<int>(hv % kFlowers);
                    const double fsz = F.flower_size[static_cast<size_t>(sp)];
                    const int count = 1 + static_cast<int>((hv >> 8) % 3);
                    for (int k = 0; k < count; ++k) {
                        const double px = fx + (k ? ((hv >> (k * 3)) % 7 - 3) * .06 : 0), py = fy + (k ? ((hv >> (k * 5)) % 7 - 3) * .05 : 0);
                        r.billboard({px + sway * .3, py, w.ground(px, py)}, fsz, fsz, &F.flower[static_cast<size_t>(sp)], kW, cutout);
                    }
                    break;
                }
                case Feature::tuft:
                    r.billboard({fx + sway * .5, fy, gz}, .42, .38, &F.tuft[hv % kTufts], t.biome == Biome::alpine ? hex(0xE0E8B0) : kW, cutout);
                    break;
                case Feature::fern: {
                    const int sp = static_cast<int>(hv % kFerns);
                    const double fs = F.fern_size[static_cast<size_t>(sp)];
                    r.billboard({fx + sway * .3, fy, gz}, fs, fs, &F.fern[static_cast<size_t>(sp)], t.biome == Biome::autumn ? hex(0xE8C080) : kW, cutout);
                    break;
                }
                case Feature::clover: r.billboard({fx, fy, gz}, .22, .22, &T.clover, kW, cutout); break;
                case Feature::lichen: {
                    const Col lc = hv % 2 ? hex(0xD99A2B, .85f) : hex(0x9FB35A, .85f);
                    draw_mesh(r, disc_mesh(7), at(fx, fy, Z(gz) + .02) * sc(.18, .12, 1), nullptr, lc, translucent);
                    break;
                }
                case Feature::lily: {
                    const double wz = std::min({t.z[0], t.z[1], t.z[2], t.z[3]}) + .08;
                    draw_mesh(r, disc_mesh(9), at(fx, fy, Z(wz)) * M34::rot_z(hv * .1) * sc(.26, .26, 1), &T.lily, kW, cutout, 1);
                    if (hv % 3 == 0) r.billboard({fx + .05, fy, wz}, .18, .18, &F.flower[static_cast<size_t>(hv % 7)], hex(0xFFC0D8), cutout);
                    break;
                }
                case Feature::stone:
                    draw_rock(r, F.rocks[hv % kRocks], u + .5, v + .5, Z(std::min({t.z[0], t.z[1], t.z[2], t.z[3]})) - .05, .3, false, opaque, kW);
                    break;
                case Feature::puddle:
                    draw_mesh(r, disc_mesh(10), at(fx, fy, Z(gz) + .02) * sc(.34, .26, 1), &T.water[static_cast<size_t>(static_cast<int>(time_ * 3) & 3)], hex(0xFFFFFF, .85f), translucent, 1);
                    break;
                case Feature::ledge:
                    draw_mesh(r, box_mesh(), at(u + .5, v + .5, Z(w.ground(u + .5, v + .5)) - .05) * sc(.47, .47, .14), &ground().tex[g_slate], kW, opaque, 1);
                    break;
                case Feature::crystal:
                    for (int k = 0; k < 3; ++k)
                        draw_mesh(r, cone_mesh(4), at(fx + (k - 1) * .09, fy, Z(gz)) * M34::rot_y((k - 1) * .3) * sc(.06, .06, .3 - k * .06), nullptr, hex(0xBFF0FF), opaque);
                    break;
                case Feature::snowdrift:
                {   // a wind-carved drift: a long low ridge with a smaller lobe beside it
                    const double turn = 2.5 + ((hv >> 3) % 7) * .08;
                    draw_mesh(r, F.blob_meshes[hv % 6], at(fx, fy, Z(gz) - .03) * M34::rot_z(turn) * sc(.46, .26, .2), &ground().tex[g_snow], kW, opaque, 1);
                    draw_mesh(r, F.blob_meshes[(hv + 3) % 6], at(fx + std::cos(turn + 1.4) * .16, fy + std::sin(turn + 1.4) * .16, Z(gz) - .03) * M34::rot_z(turn + .4) *
                              sc(.24, .17, .12), &ground().tex[g_snow], kW, opaque, 1);
                }
                    break;
                case Feature::flags: {
                    const double z2 = Z(gz);
                    draw_mesh(r, cylinder_mesh(4), at(fx - .4, fy, z2) * sc(.02, .02, .55), &F.bark[0], kW, opaque);
                    draw_mesh(r, cylinder_mesh(4), at(fx + .4, fy + .3, z2) * sc(.02, .02, .5), &F.bark[0], kW, opaque);
                    const Col fc[5] = {hex(0x3FA9F5), hex(0xFFFFFF), hex(0xE8323A), hex(0x5FD068), hex(0xFFD23F)};
                    for (int k = 0; k < 5; ++k) {
                        const double a0 = (k + .1) / 5.0, a1 = (k + .9) / 5.0;
                        const V3 A{fx - .4 + .8 * a0, fy + .3 * a0, gz + (.52 - .08 * std::sin(a0 * M_PI)) / r.height_scale};
                        const V3 B{fx - .4 + .8 * a1, fy + .3 * a1, gz + (.52 - .08 * std::sin(a1 * M_PI)) / r.height_scale};
                        const double flut = std::sin(time_ * 6 + k) * .05 * (1 + wind * 2);
                        const V3 C{(A.x + B.x) / 2 + flut, (A.y + B.y) / 2 - .05, (A.z + B.z) / 2 - .12 / r.height_scale};
                        const V3 n = norm(cross(B - A, C - A));
                        Vtx q[3] = {{A, n, 0, 0, fc[k]}, {B, n, 0, 0, fc[k]}, {C, n, 0, 0, fc[k]}};
                        r.draw(q, 3, nullptr, double_sided);
                    }
                    break;
                }
                case Feature::signpost:
                    draw_mesh(r, box_mesh(), at(fx, fy, Z(gz)) * sc(.035, .035, .75), &F.bark[0], kW, opaque);
                    draw_mesh(r, box_mesh(), at(fx, fy, Z(gz) + .48) * M34::rot_z(-.6) * sc(.3, .03, .17), &T.wood_end, hex(0xD8B080), opaque, .3);
                    break;
                default: break;
            }
            g_fade = false;
            // floating star (only drawn while it is still out there)
            const int si = w.star_at(u, v);
            if (si >= 0 && !s.collected[static_cast<size_t>(si)]) {
                const Star& st = w.stars()[static_cast<size_t>(si)];
                const double bz = w.ground(st.u, v + .5) + .35 + .05 * std::sin(time_ * 2.2 + si);
                r.billboard({st.u, v + .5, bz - .2}, .7, .7, &T.glow, hex(0xFFE680, s.player_mode ? .55f : .3f), additive | no_fog);
                const double spin = std::cos(time_ * 2.5 + si);
                r.billboard({st.u, v + .5, bz - .12}, .3 * std::max(.15, std::fabs(spin)), .3, &T.star, kW, cutout | unlit);
            }
        }
    }
}

// ------------------------------------------------------------------ rare life
void Scene::life(const Sim& s, double /*dt*/) {
    const Textures& T = textures();
    const World& w = s.world;
    // a toppled tree lies where it fell for a while, then the woods forget
    if (fall_v_ >= 0 && rt_ - fall_start_ > 600) { fall_v_ = -1; next_fall_ = rt_ + 300 + std::fmod(rt_ * 37, 600); }
    if (fall_v_ >= 0 && std::fabs(static_cast<double>(fall_v_) - s.d.v) > 30) { fall_v_ = -1; next_fall_ = rt_ + 300 + std::fmod(rt_ * 37, 600); }
    // fish leap now and then from brooks and ponds
    if (!water_spots_.empty()) {
        const double period = 3.4;
        const int n = static_cast<int>(rt_ / period);
        const std::uint64_t hn = mix64(static_cast<std::uint64_t>(n) * 2654435761ULL);
        const double frac = (rt_ - n * period) / .85;
        if (hn % 3 != 0 && frac < 1) {
            const WaterSpot& sp = water_spots_[hn % water_spots_.size()];
            const double dir = (hn >> 8) % 2 ? 1 : -1;
            const double fx = sp.x + dir * (frac - .5) * .5, fy = sp.y + ((hn >> 12) % 3 - 1) * .1;
            const double fz = sp.z + std::sin(frac * M_PI) * .35;
            draw_fish(r, fx, fy, Z(fz), dir > 0 ? 0 : M_PI, -std::cos(frac * M_PI) * .9 * dir, static_cast<int>(hn >> 20));
            if (!fish_in_air_ || fish_cycle_ != n) {
                fish_in_air_ = true;
                fish_cycle_ = n;
                spawn(Particle::splash, {sp.x - dir * .25, fy, sp.z}, 5, .1, hex(0xBDE9FF));
                if (std::fabs(sp.y - s.d.v) < 8) sounds.push_back({"eggy_fish", .35f});
            }
        } else if (fish_in_air_ && fish_cycle_ == n) {
            fish_in_air_ = false;
            const WaterSpot& sp = water_spots_[hn % water_spots_.size()];
            spawn(Particle::ripple, {sp.x + ((hn >> 8) % 2 ? .25 : -.25), sp.y, sp.z - .07}, 1, 0, hex(0xBDE9FF));
        }
    }
    // base camp: tents and a pennant (drawn while the camp is in view)
    if (cam_v < kCampEnd + 30) {
        draw_camp_tent(r, T, -.7, 11.5, w.ground(-.7, 11.5), .2, .8, hex(0xFFFFFF));
        draw_camp_tent(r, T, -.9, 17.5, w.ground(-.9, 17.5), .5, .6, hex(0xD8E8C8));
        draw_camp_tent(r, T, kWidth + 1.4, 9.5, w.ground(kWidth + 1.4, 9.5), M_PI, .6, hex(0xF0E0C0));
        const double px = -.9, py = 28.5, pz = Z(w.ground(px, py));
        draw_mesh(r, cylinder_mesh(4), at(px, py, pz) * sc(.02, .02, .9), &T.bark, hex(0xFFFFFF), opaque);
        const double fl = std::sin(rt_ * 5) * .05;
        const double top = w.ground(px, py) + .88 / r.height_scale;
        Vtx flag[3] = {{{px, py, top}, {1, 0, 0}, 0, 0, hex(0xFFD23F)}, {{px, py, top - .18 / r.height_scale}, {1, 0, 0}, 0, 0, hex(0xFFD23F)},
                       {{px + .35, py + fl, top - .09 / r.height_scale}, {1, 0, 0}, 0, 0, hex(0xE8323A)}};
        r.draw(flag, 3, nullptr, double_sided);
    }
}

// ------------------------------------------------------------------ weather, particles
void Scene::weather(const Sim& s, double /*dt*/) {
    const SegmentInfo seg = s.world.segment_at(s.d.v);
    const Biome b = s.world.row(static_cast<std::int64_t>(s.d.v)).biome;
    const bool snowing = b == Biome::snow || b == Biome::ice || b == Biome::ridge || b == Biome::summit;
    const int W = r.W, H = r.H;
    auto px = [&](int x, int y, Col c, float a) {
        if (x < 0 || y < 0 || x >= W || y >= H) return;
        float* o = r.rgb.data() + (static_cast<size_t>(y) * W + x) * 3;
        o[0] += (c.r - o[0]) * a; o[1] += (c.g - o[1]) * a; o[2] += (c.b - o[2]) * a;
    };
    const double wind = std::round(s.wind_vis * 8) / 8;  // steps, so the screen stays still between changes
    const double st = s.storm;
    // storms: a low cloud deck rolling over the mountain, lit by lightning
    if (st > .04) {
        const int band = static_cast<int>(H * .34);
        const float lit = flash_t_ < .3 ? static_cast<float>((1 - flash_t_ / .3) * (1 - .5 * flash_dist_)) : 0.f;
        for (int y = 0; y < band; ++y) {
            const double fy = 1 - static_cast<double>(y) / band;
            for (int x = 0; x < W; ++x) {
                const double n = .5 + .25 * std::sin(x * .05 + time_ * .4 + std::sin(y * .13) * 2) + .25 * std::sin(x * .017 - time_ * .25 + y * .09);
                const float a = static_cast<float>(st * .78 * std::pow(fy, .6) * std::clamp(n * 1.3 - .1, 0.0, 1.0));
                const Col c = lerpc(hex(0x2A2F38), hex(0x5E6674), n);
                const Col cl = lerpc(c, hex(0xE8ECFF), lit * .8);
                px(x, y, cl, a);
            }
        }
        if (flash_t_ < .18) {  // the bolt forks down out of the clouds
            std::uint64_t hs = mix64(bolt_seed_ * 7 + 3);
            auto rnd = [&]() { hs = mix64(hs); return static_cast<double>(hs >> 11) * (1.0 / 9007199254740992.0); };
            double x = W * (.1 + .8 * rnd()), y = band * .2;
            const double end_y = band * (1.0 + .6 * (1 - flash_dist_));
            while (y < end_y) {
                const double nx = x + (rnd() - .5) * 9, ny = y + 3 + rnd() * 5;
                for (int k = 0; k <= 8; ++k) {
                    const int bx = static_cast<int>(x + (nx - x) * k / 8), by = static_cast<int>(y + (ny - y) * k / 8);
                    px(bx, by, hex(0xFFFFFF), 1.f);
                    if (flash_dist_ < .5) px(bx + 1, by, hex(0xD8DCFF), .8f);
                }
                x = nx; y = ny;
            }
        }
    }
    if (snowing) {
        for (int i = 0; i < static_cast<int>((reduced_motion ? 50 : 160) * (1 + 2.5 * st)); ++i) {
            const double sp = (12 + h01(i, 31) * 16) * (1 + st);
            const double x = std::fmod(h01(i, 32) * W * 1.5 + time_ * (6 + wind * 70 + st * 160) - cam_v * 3 + std::sin(time_ + i) * 4, W * 1.5);
            const double y = std::fmod(h01(i, 33) * H + time_ * sp, H);
            px(static_cast<int>(x - W * .25), static_cast<int>(y), hex(0xFFFFFF), .85f);
            if (i % 4 == 0) px(static_cast<int>(x - W * .25) + 1, static_cast<int>(y), hex(0xFFFFFF), .5f);
        }
    }
    const double rain = std::max(static_cast<double>(seg.rain), st);
    if (rain > 0 && !snowing) {
        const double slant = .4 + st * 1.4;
        for (int i = 0; i < static_cast<int>(120 * rain * (1 + 2 * st)); ++i) {
            const double x = std::fmod(h01(i, 41) * W + time_ * (30 + st * 140), W), y = std::fmod(h01(i, 42) * H + time_ * (220 + st * 120), H);
            const int len = 4 + static_cast<int>(st * 4);
            for (int k = 0; k < len; ++k) px(static_cast<int>(x - k * slant), static_cast<int>(y - k), hex(0xBFD7EE), .45f);
        }
    }
    if (wind > .3) {
        for (int i = 0; i < 14; ++i) {
            const double x0 = std::fmod(h01(i, 51) * W * 2 - time_ * 260 * wind, W * 2);
            const double y0 = h01(i, 52) * H * .9;
            for (int k = 0; k < 26; ++k) px(static_cast<int>(x0 + k), static_cast<int>(y0 + std::sin((x0 + k) * .05) * 3), hex(0xFFFFFF), static_cast<float>(.25 * (wind - .3) * std::sin(k / 26.0 * M_PI)));
        }
    }
}

void Scene::particles(double dt) {
    const Textures& T = textures();
    for (Particle& q : parts) {
        q.age += dt;
        const double g = (q.kind == Particle::splash || q.kind == Particle::sweat || q.kind == Particle::starburst) ? 6 :
                         q.kind == Particle::confetti ? 3.5 : q.kind == Particle::feather ? .35 : 0;
        q.vel.z -= g * dt;
        if (q.kind == Particle::confetti && q.vel.z < -.6) q.vel.z = -.6;
        q.p = q.p + q.vel * dt;
    }
    parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Particle& q) { return q.age >= q.life; }), parts.end());
    for (const Particle& q : parts) {
        const float life = static_cast<float>(1 - q.age / q.life);
        switch (q.kind) {
            case Particle::dust: case Particle::snowpuff: case Particle::breath:
                r.billboard(q.p, q.size * (1 + q.age * 2), q.size * (1 + q.age * 2), &T.puff, alpha(q.col, life * .7f), translucent);
                break;
            case Particle::ripple:
                draw_mesh(r, disc_mesh(10), at(q.p.x, q.p.y, Z(q.p.z) + .09) * sc(.15 + q.age * .5, .12 + q.age * .4, 1), &T.puff, alpha(q.col, life * .5f), translucent | unlit);
                break;
            case Particle::sparkle: case Particle::starburst:
                r.billboard(q.p, q.size * 2, q.size * 2, &T.sparkle, alpha(q.col, life), additive | unlit | no_fog);
                break;
            case Particle::feather: case Particle::confetti: case Particle::sweat: case Particle::splash:
                r.billboard(q.p, q.size * std::fabs(std::cos(q.age * 6 + q.spin)) + .02, q.size, nullptr, alpha(q.col, std::min(1.f, life * 2)), translucent | unlit);
                break;
            case Particle::note: case Particle::heart:
                r.billboard(q.p + V3{std::sin(q.age * 5) * .05, 0, 0}, q.size, q.size, &T.sparkle, alpha(q.col, life), additive | unlit);
                break;
        }
    }
}

// ------------------------------------------------------------------ summit
void Scene::ceremony(const Sim& s) {
    const Textures& T = textures();
    const World& w = s.world;
    const double sv = static_cast<double>(w.length()) - 8;
    // the tent stands just beyond Eggy (away from the camera), door facing him
    const double tu = w.lane(sv) - 2.4, tv = sv + .3;
    if (std::fabs(cam_v - tv) > 40) return;
    const double gz = w.ground(tu, tv);
    const double open = std::clamp((s.ceremony_t - 1.2) / 1.2, 0.0, 1.0);
    const double ts = .62;
    auto L = [&](double lx, double ly, double lz) { return V3{tu - ly * ts, tv + lx * ts, gz + lz * ts}; };  // local -y (door) -> world +u
    const V3 A = L(-.7, -.55, 0), B = L(.7, -.55, 0), C = L(-.7, .9, 0), D = L(.7, .9, 0);
    const V3 top0 = L(0, -.55, 1.15), top1 = L(0, .9, 1.15);
    auto quad = [&](V3 a, V3 b, V3 c, V3 d, const Tex* tx, Col col) {
        const V3 n = norm(cross(b - a, d - a));
        Vtx q[6] = {{a, n, 0, 1, col}, {b, n, 1, 1, col}, {c, n, 1, 0, col}, {a, n, 0, 1, col}, {c, n, 1, 0, col}, {d, n, 0, 0, col}};
        r.draw(q, 6, tx, double_sided);
    };
    quad(A, C, top1, top0, &T.canvas, kW);
    quad(D, B, top0, top1, &T.canvas, kW);
    {
        const V3 n{-1, 0, 0};
        Vtx back[3] = {{C, n, 0, 1, hex(0xC9B98F)}, {D, n, 1, 1, hex(0xC9B98F)}, {top1, n, .5, 0, hex(0xC9B98F)}};
        r.draw(back, 3, nullptr, double_sided);
        const Col dark = hex(0x3A2A18);
        Vtx door[3] = {{A, {1, 0, 0}, 0, 0, dark}, {B, {1, 0, 0}, 0, 0, dark}, {top0, {1, 0, 0}, 0, 0, dark}};
        r.draw(door, 3, nullptr, double_sided | unlit);
        for (int side = -1; side <= 1; side += 2) {
            const V3 edge = side < 0 ? A : B;
            const double sw = open * 1.2;
            const V3 tip = open > .05 ? L(side * .7 * std::cos(sw) * .9, -.55 - std::sin(sw) * .5, 0) : L(0, -.56, 0);
            Vtx flap[3] = {{edge, {1, 0, 0}, 0, 1, kW}, {top0, {1, 0, 0}, .5, 0, kW}, {tip, {1, 0, 0}, 0, 1, kW}};
            r.draw(flap, 3, &T.canvas, double_sided);
        }
        const V3 pole = L(0, -.55, 1.1);
        draw_mesh(r, cylinder_mesh(4), at(pole.x, pole.y, Z(pole.z)) * sc(.015, .015, .34), &T.bark, kW, opaque);
        const double fl = std::sin(rt_ * 7) * .05;
        Vtx flag[3] = {{L(0, -.55, 1.65), {1, 0, 0}, 0, 0, hex(0xE8323A)}, {L(0, -.55, 1.45), {1, 0, 0}, 0, 0, hex(0xE8323A)},
                       {L(.4, -.55 + fl, 1.57), {1, 0, 0}, 0, 0, hex(0xFFD23F)}};
        r.draw(flag, 3, nullptr, double_sided);
    }
    // the officer
    if (s.ceremony_t >= 2.6 || s.finished) {
        const double ct = s.finished ? 30 : s.ceremony_t;
        const V3 from{tu + .45, tv, 0}, to{s.d.u - .42, s.d.v + .12, 0};
        double k = std::clamp((ct - 3) / 3.0, 0.0, 1.0);
        if (ct > 10.4 && ct < 12.6) k = 1 + .3 * std::sin((ct - 10.4) / 2.2 * M_PI);  // steps in to pin the medal
        const double ou = from.x + (to.x - from.x) * k, ov = from.y + (to.y - from.y) * k;
        Pose op;
        op.officer = true;
        op.scale_mul = 1.12;
        op.medal = true;
        const bool walking = (ct > 3 && ct < 6) || (ct > 10.4 && ct < 12.6);
        op.step = rt_ * 9;
        op.stepamp = walking ? 1 : 0;
        op.bob = walking ? -3 * std::fabs(std::sin(rt_ * 9)) : 0;
        if (ct > 7 && ct < 9.6) { op.wing_fwd = 1; op.wing_near = .5; }
        if (ct > 11 && ct < 12.4) op.wing_fwd = .55;
        op.eyes = ct > 14.5 ? Eyes::happy : Eyes::open;
        if (ct > 15.5) op.wing_near = op.wing_far = .8 + .6 * std::sin(rt_ * 18);
        const double heading = walking && ct < 6 ? std::atan2(to.y - from.y, to.x - from.x) : std::atan2(s.d.v - ov, s.d.u - ou);
        
        draw_duck3d(r, ou, ov, Z(w.ground(ou, ov)), heading, op, rt_);
        officer_u = ou; officer_v = ov;
    }
}

// ------------------------------------------------------------------ frame
void Scene::render(const Sim& s, double t, double dt) {
    time_ = t;
    r.time = t;
    // Camera: Eggy stays near the centre; the camera leads a little in the
    // direction he is going and follows softly (a spring, not a leash).
    rt_ = t;
    time_ = t;
    {
        const double lead_u = std::clamp(s.d.vu, -1.5, 1.5) * .8;
        const double lead_v = std::clamp(s.d.vv, -1.0, 2.0) * 1.1 + 1.2;
        const double tu = s.d.u * .75 + kWidth * .5 * .25 + lead_u, tv = s.d.v + lead_v;
        const double tz = s.d.z + lead_v * kSlope;
        if (!cam_ready || std::fabs(cam_v - tv) > 25) { cam_u = tu; cam_v = tv; cam_z = tz; cam_ready = true; }
        const double k = 1 - std::exp(-dt * 2.0);
        cam_u += (tu - cam_u) * k;
        cam_v += (tv - cam_v) * k;
        cam_z += (tz - cam_z) * k;
    }
    r.scale = 36 * zoom * (r.W / 400.0);
    r.ax = .5; r.ay = .55;
    r.yaw = 110 * M_PI / 180;
    r.height_scale = 2.0;
    g_zscale = r.height_scale;
    r.target = {cam_u, cam_v, cam_z};
    r.set_camera();
    animate(s, dt);
    // lighting by time of day and altitude
    phase_ = std::floor(s.day_phase() * 240) / 240;  // daylight steps every ~5 s
    sun_ = std::clamp(std::sin((phase_ - .2) / .6 * M_PI) * 1.4, 0.0, 1.0);
    const double sun = sun_;
    const double dusk = std::clamp(1 - std::fabs(sun - .35) / .35, 0.0, 1.0) * (sun < .9 ? 1 : 0);
    night_ = 1 - sun;
    const double ph = phase_;
    const double az = (ph - .25) * 2 * M_PI;
    r.light.sun = norm({-std::cos(az) * .6 - .3, .35, .45 + .55 * std::max(0.0, std::sin(az))});
    r.light.sun_col = lerpc(lerpc(hex(0x2A3355), hex(0xFFF6E0), sun), hex(0xFFB070), dusk * .7);
    r.light.amb_col = lerpc(hex(0x2C3A66), hex(0x7C8AA6), sun);
    r.light.amb_col = {r.light.amb_col.r * .75f, r.light.amb_col.g * .75f, r.light.amb_col.b * .8f, 1};
    r.light.fog_col = lerpc(lerpc(hex(0x1B2A52), hex(0xBFE3F7), sun), hex(0xF2A65A), dusk);
    r.light.fog_near = 9 / zoom;
    r.light.fog_far = 21 / zoom;
    {   // storms darken the day; lightning briefly floods everything with light
        const float st = static_cast<float>(s.storm);
        const float dim = 1 - .5f * st;
        r.light.sun_col = {r.light.sun_col.r * dim, r.light.sun_col.g * dim, r.light.sun_col.b * dim, 1};
        r.light.amb_col = {r.light.amb_col.r * (1 - .3f * st), r.light.amb_col.g * (1 - .28f * st), r.light.amb_col.b * (1 - .2f * st), 1};
        r.light.fog_col = lerpc(r.light.fog_col, hex(0x4A5260), st * .7);
        r.light.fog_near *= 1 - .35 * st;
        r.light.fog_far *= 1 - .3 * st;
        flash_t_ += dt;
        const double fk = flash_t_ < .35 ? (1 - flash_t_ / .35) * (1 - .6 * flash_dist_) : 0;
        if (fk > 0) {
            const float f = static_cast<float>(fk);
            r.light.amb_col = {r.light.amb_col.r + .9f * f, r.light.amb_col.g + .9f * f, r.light.amb_col.b + 1.f * f, 1};
            r.light.fog_col = lerpc(r.light.fog_col, hex(0xE8ECFF), fk);
        }
    }
    r.light.focus = {cam_u, cam_v + 2, 0};
    r.clear_depth();
    // the sky only changes at the 2 Hz scenery tick or when the camera moves:
    // keep the last one and reuse it
    {
        sky_t_ = std::floor(t * 4) / 4;
        sky_v_ = std::floor(cam_v * 2) / 2;
        const double key[6] = {sky_t_, phase_, sky_v_, static_cast<double>(r.W) + std::floor(s.storm * 20) * 1e4 + (flash_t_ < .4 ? std::floor(flash_t_ * 30) + 1 : 0) * 1e6, static_cast<double>(r.H), std::floor(s.progress() * 2000)};
        if (sky_cache_.size() != r.rgb.size() || std::memcmp(key, sky_key_, sizeof key) != 0) {
            sky(s);
            storm_sky(s);
            sky_cache_ = r.rgb;
            std::memcpy(sky_key_, key, sizeof key);
        } else {
            std::memcpy(r.rgb.data(), sky_cache_.data(), sky_cache_.size() * sizeof(float));
        }
    }
    terrain(s);
    near_waterfall = 0;
    near_fire = 0;
    props(s);
    life(s, dt);
    if (s.ceremony_t >= 0 || s.d.v > static_cast<double>(s.world.length()) - 60) ceremony(s);
    // leaves (with their shadows so you can dodge them)
    const Textures& T = textures();
    for (const Leaf& l : s.leaves) {
        const double gz = s.world.ground(std::clamp(l.u, 0.0, kWidth - .01), std::max(0.0, l.v));
        if (l.landed < 0) {
            const float sh = static_cast<float>(std::clamp(.45 - (l.z - gz) * .05, .1, .45));
            draw_mesh(r, disc_mesh(6), at(l.u, l.v, Z(gz) + .03) * sc(.1, .08, 1), &T.shadow, hex(0x000000, sh), translucent | unlit);
        }
        const double w2 = .22 * std::fabs(std::cos(l.phase * 1.3 + l.spin));
        r.billboard({l.u, l.v, l.landed >= 0 ? gz + .02 : l.z}, l.landed >= 0 ? .2 : w2 + .03, l.landed >= 0 ? .06 : .2,
                    &T.leaf_sprite[l.color % 4], kW, cutout);
    }
    // Eggy and his shadow (a little extra light so he stays readable at night)
    {
        const double gz = s.world.ground(s.d.u, s.d.v);
        const double lift = std::clamp(s.d.z - gz, 0.0, 2.0);
        draw_mesh(r, disc_mesh(10), at(s.d.u, s.d.v, Z(gz) + .03) * sc(.11 - lift * .025, .09 - lift * .02, 1), &T.shadow, hex(0x000000, .5f), translucent | unlit);
        const Lighting saved = r.light;
        r.light.amb_col = {std::max(r.light.amb_col.r, .5f), std::max(r.light.amb_col.g, .5f), std::max(r.light.amb_col.b, .55f), 1};
        r.light.fog_far = 1e6; r.light.fog_near = 1e6 - 1;
        draw_duck3d(r, s.d.u, s.d.v, Z(s.d.z), s.d.heading, pose, t);
        r.light = saved;
    }
    particles(dt);
    weather(s, dt);
}

bool Scene::screen_to_ground(const Sim& s, double sx, double sy, double& u, double& v) const {
    double gz = s.d.z;
    for (int i = 0; i < 4; ++i) {
        if (!r.unproject_ground(sx, sy, gz, u, v)) return false;
        gz = s.world.ground(std::clamp(u, 0.0, kWidth - .01), std::max(0.0, v));
    }
    return true;
}

void Scene::duck_screen(const Sim& s, double& x, double& y) const {
    double z;
    r.project({s.d.u, s.d.v, s.d.z + .42 / r.height_scale}, x, y, z);
}

}  // namespace eggy
