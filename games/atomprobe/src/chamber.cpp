#include "chamber.hpp"

#include "platform/mesh.hpp"
#include "platform/raster.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace ap {

namespace {
const Col kWhite{1, 1, 1, 1};
const Col kCyan = hex(0x56F0FF), kTeal = hex(0x1A6E7A), kDeep = hex(0x08202A), kViolet = hex(0x8A4DFF);
const Col kMetal = hex(0x2A3440), kMetalLight = hex(0x56677A), kMetalDark = hex(0x161C24), kInk = hex(0x05080C);
const Col kAtom = hex(0xFFB347), kGood = hex(0x6CFF9A), kBad = hex(0xFF4A5A);

M34 at(V3 v) { return M34::translate(v.x, v.y, v.z); }
M34 sc(double x, double y, double z) { return M34::scale(x, y, z); }
constexpr std::uint16_t kGlowMat = additive | unlit | no_depth_write | double_sided;

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

// tileable value noise, a few octaves
double vnoise(double x, double y, int period, std::uint32_t seed) {
    auto h = [&](int ix, int iy) {
        ix = ((ix % period) + period) % period;
        iy = ((iy % period) + period) % period;
        std::uint32_t v = static_cast<std::uint32_t>(ix) * 374761393u + static_cast<std::uint32_t>(iy) * 668265263u + seed * 2246822519u;
        v = (v ^ (v >> 13)) * 1274126177u;
        return ((v ^ (v >> 16)) & 0xFFFF) / 65535.0;
    };
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    double fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    const double a = h(ix, iy), b = h(ix + 1, iy), c = h(ix, iy + 1), d = h(ix + 1, iy + 1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

struct Textures {
    Tex glow, ring, frame, fog[2], grid, cross, plate, streak, floor, socket, sheen;
    Textures() {
        Canvas c;
        c.resize(32, 32);
        c.clear({0, 0, 0, 0});
        c.begin(); c.circle(16, 16, 16); c.fill(Paint::rad(16, 16, 16, {{0, {1, 1, 1, 1}}, {.25f, {1, 1, 1, .55f}}, {1, {1, 1, 1, 0}}}));
        glow = tex_from(c);
        c.clear({0, 0, 0, 0});
        c.begin(); c.circle(16, 16, 13); c.stroke({1, 1, 1, 1}, 2.5);
        ring = tex_from(c);
        c.clear({0, 0, 0, 0});
        c.begin(); c.rect(1.5, 1.5, 29, 29); c.stroke({1, 1, 1, 1}, 2);
        frame = tex_from(c);
        c.clear({0, 0, 0, 0});
        c.stroke_line(8, 8, 24, 24, {1, 1, 1, 1}, 3);
        c.stroke_line(24, 8, 8, 24, {1, 1, 1, 1}, 3);
        cross = tex_from(c);
        c.clear({0, 0, 0, 0});
        c.begin(); c.rrect(2, 2, 28, 28, 5); c.fill({1, 1, 1, 1});
        c.begin(); c.rrect(2.5, 2.5, 27, 27, 5); c.stroke({1, 1, 1, .6f}, 1);
        plate = tex_from(c);
        c.clear({0, 0, 0, 0});
        c.begin(); c.circle(16, 16, 12); c.fill({0, 0, 0, .9f});
        c.begin(); c.circle(16, 16, 12); c.stroke({1, 1, 1, .8f}, 2);
        socket = tex_from(c);
        // a beam's cross-section: a white-hot core in a soft halo
        c.resize(8, 32);
        c.clear({0, 0, 0, 0});
        for (int y = 0; y < 32; ++y) {
            const double d = std::fabs(y + .5 - 16) / 16;
            const float a = static_cast<float>(std::exp(-d * d * 9) * .7 + (d < .14 ? .5 : 0));
            c.fill_rect(0, y, 8, 1, {1, 1, 1, std::min(1.f, a)});
        }
        streak = tex_from(c);
        // fog: two tileable noise fields, soft and wispy
        for (int k = 0; k < 2; ++k) {
            c.resize(128, 128);
            c.clear({0, 0, 0, 0});
            for (int y = 0; y < 128; ++y)
                for (int x = 0; x < 128; ++x) {
                    double v = 0, amp = .55, f = 4;
                    for (int o = 0; o < 4; ++o, amp *= .5, f *= 2) v += amp * vnoise(x / 128.0 * f, y / 128.0 * f, static_cast<int>(f), 17u + k * 31u + o);
                    v = std::clamp((v - .28) * 1.7, 0.0, 1.0);
                    c.fill_rect(x, y, 1, 1, {1, 1, 1, static_cast<float>(v)});
                }
            fog[k] = tex_from(c);
        }
        // the glass's etched grid: eight by eight, lines on the cell edges
        c.resize(256, 256);
        c.clear({0, 0, 0, 0});
        for (int i = 0; i <= 8; ++i) {
            const double p = std::min(255.0, i * 32.0);
            c.fill_rect(p - (i == 8 ? 1 : 0), 0, 1, 256, {1, 1, 1, i == 0 || i == 8 ? .9f : .55f});
            c.fill_rect(0, p - (i == 8 ? 1 : 0), 256, 1, {1, 1, 1, i == 0 || i == 8 ? .9f : .55f});
        }
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 8; ++j) c.fill_rect(i * 32 + 15, j * 32 + 15, 2, 2, {1, 1, 1, .35f});
        grid = tex_from(c);
        // a diagonal sheen across the glass
        c.resize(64, 64);
        c.clear({0, 0, 0, 0});
        c.begin(); c.move(10, 64); c.line(26, 64); c.line(54, 0); c.line(38, 0); c.close(); c.fill({1, 1, 1, .5f});
        c.begin(); c.move(30, 64); c.line(34, 64); c.line(62, 0); c.line(58, 0); c.close(); c.fill({1, 1, 1, .35f});
        sheen = tex_from(c);
        // lab floor tiles
        c.resize(64, 64);
        c.clear(hex(0x0C131B));
        c.fill_rect(0, 0, 64, 1, hex(0x1A2632));
        c.fill_rect(0, 0, 1, 64, hex(0x1A2632));
        c.fill_rect(0, 32, 64, 1, hex(0x121C26));
        c.fill_rect(32, 0, 1, 64, hex(0x121C26));
        floor = tex_from(c);
    }
};
const Textures& tx() {
    static Textures t;
    return t;
}

// The score readout: two big seven-segment digits.
const Tex& readout_tex(int value, bool hot) {
    static std::map<int, Tex> cache;
    const int key = value * 2 + (hot ? 1 : 0);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    Canvas c;
    c.resize(64, 32);
    c.clear(hex(0x02080A));
    const Col on = hot ? hex(0xFFE08A) : kCyan, off = alpha(kCyan, .08f);
    static const int seg[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    auto digit = [&](int d, double x, double y, double w, double h) {
        const double t = 3;
        for (int k = 0; k < 7; ++k) {
            const Col col = (seg[d % 10] >> k & 1) ? on : off;
            switch (k) {
                case 0: c.fill_rect(x + t, y, w - 2 * t, t, col); break;
                case 1: c.fill_rect(x + w - t, y + t, t, h / 2 - t, col); break;
                case 2: c.fill_rect(x + w - t, y + h / 2, t, h / 2 - t, col); break;
                case 3: c.fill_rect(x + t, y + h - t, w - 2 * t, t, col); break;
                case 4: c.fill_rect(x, y + h / 2, t, h / 2 - t, col); break;
                case 5: c.fill_rect(x, y + t, t, h / 2 - t, col); break;
                default: c.fill_rect(x + t, y + h / 2 - t / 2, w - 2 * t, t, col); break;
            }
        }
    };
    const int v = std::clamp(value, 0, 99);
    digit(v / 10, 12, 5, 16, 22);
    digit(v % 10, 34, 5, 16, 22);
    return cache.emplace(key, tex_from(c)).first->second;
}

void quad(R3D& r, V3 a, V3 b, V3 c, V3 d, const Tex* tex, Col col, std::uint16_t mat, double s0 = 0, double t0 = 0, double s1 = 1, double t1 = 1) {
    const V3 n = norm(cross(b - a, d - a));
    Vtx v[6] = {{a, n, s0, t1, col}, {b, n, s1, t1, col}, {c, n, s1, t0, col}, {a, n, s0, t1, col}, {c, n, s1, t0, col}, {d, n, s0, t0, col}};
    r.draw(v, 6, tex, mat);
}

// a flat decal lying at height p.z, centred at p
void decal(R3D& r, V3 p, double w, double h, const Tex* tex, Col col, std::uint16_t mat) {
    quad(r, p + V3{-w / 2, -h / 2, 0}, p + V3{w / 2, -h / 2, 0}, p + V3{w / 2, h / 2, 0}, p + V3{-w / 2, h / 2, 0}, tex, col, mat);
}

void slab(R3D& r, V3 lo, V3 hi, Col c, double ink = 0) {
    const V3 ctr{(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z};
    const M34 m = at(ctr) * sc((hi.x - lo.x) / 2, (hi.y - lo.y) / 2, hi.z - lo.z);
    draw_mesh(r, box_mesh(), m, nullptr, c, toon);
    if (ink > 0) draw_outline(r, box_mesh(), m, ink, kInk);
}

Col pair_col(int pair) {
    static const Col p[12] = {hex(0x4FC3FF), hex(0xFFD23F), hex(0x7CFF6B), hex(0xFF7AD9), hex(0xFF9A3C), hex(0xB18CFF),
                              hex(0x3FFFD2), hex(0xFF5E5E), hex(0xC6FF3F), hex(0x6F8BFF), hex(0xFFB3A0), hex(0x2EE6A0)};
    return p[(pair - 1 + 120) % 12];
}
}  // namespace

Col port_color(const PortView& p) {
    switch (p.kind) {
        case 1: return hex(0xFF3B5C);
        case 2: return hex(0xF4F7FF);
        case 3: return pair_col(p.pair);
        default: return hex(0x3A4A5A);
    }
}

double Bolt::length() const {
    double l = 0;
    for (size_t i = 1; i < pts.size(); ++i) l += len(pts[i] - pts[i - 1]);
    return l;
}

// ------------------------------------------------------------------ geometry
V3 Chamber::outward(int p) {
    const Port q = port(p);
    return {static_cast<double>(-q.dx), static_cast<double>(q.dy), 0};
}
V3 Chamber::hole(int p) {
    const Port q = port(p);
    return cell(q.x, q.y) - outward(p) * .2;
}
V3 Chamber::muzzle(int p) { return hole(p) + outward(p) * .2; }
V3 Chamber::plate(int p) {
    const V3 o = hole(p) + outward(p) * .95;
    return {o.x, o.y, .63};
}
V3 Chamber::lever_knob(double pull) {
    const double a = .35 - 1.75 * std::clamp(pull, 0.0, 1.0);  // tilted away; pulled down toward you
    return lever_pivot() + V3{0, std::sin(a), std::cos(a)} * 1.45;
}

void Chamber::resize(int w, int h) {
    r.resize(w, h);
    r.yaw = 0;
    r.pitch = base_pitch_ = .9;
    r.persp = 15;
    r.scale = std::min(w / 17.8, h / 12.6);
    r.ax = .5;
    r.ay = .5;
    r.target = base_target_ = {1.55, -.2, .2};
    r.light.sun = norm({-.45, -.55, .8});
    r.light.sun_col = {.55f, .62f, .7f, 1};
    r.light.amb_col = {.32f, .4f, .48f, 1};
    r.light.fog_near = 1e9;
    r.light.fog_far = 2e9;
    r.light.toon_edge = .12f;
    r.light.toon_soft = .12f;
    r.set_camera();
}

void Chamber::look(double x, double y) {
    r.yaw = base_yaw_ + std::clamp(x, -1.0, 1.0) * .035;
    r.pitch = base_pitch_ + std::clamp(y, -1.0, 1.0) * .025;
    r.set_camera();
}

void Chamber::to_screen(V3 p, double& sx, double& sy) const {
    double sz;
    r.project(p, sx, sy, sz);
}

int Chamber::pick_port(double sx, double sy) const {
    int best = -1;
    double bd = r.scale * .5;
    for (int p = 0; p < kPorts; ++p) {
        // the housing and the barrel both count
        for (V3 q : {plate(p), muzzle(p) + outward(p) * .25}) {
            double x, y;
            to_screen(q, x, y);
            const double d = std::hypot(sx - x, sy - y);
            if (d < bd) { bd = d; best = p; }
        }
    }
    return best;
}

int Chamber::pick_cell(double sx, double sy) const {
    double wx, wy;
    if (!r.unproject_plane(sx, sy, kGlassZ, wx, wy)) return -1;
    const double cx = wx + 4, cy = 4 - wy;
    if (cx < 0 || cx >= 8 || cy < 0 || cy >= 8) return -1;
    return static_cast<int>(cy) * kN + static_cast<int>(cx);
}

bool Chamber::pick_lever(double sx, double sy, double pull) const {
    double x, y, bx, by;
    to_screen(lever_knob(pull), x, y);
    to_screen(lever_pivot() + (lever_knob(pull) - lever_pivot()) * .5, bx, by);
    return std::hypot(sx - x, sy - y) < r.scale * .55 || std::hypot(sx - bx, sy - by) < r.scale * .4;
}

// ------------------------------------------------------------------ drawing helpers
void Chamber::sprite(V3 c, double size, const Tex* tex, Col col, std::uint16_t extra) {
    // a camera-facing quad (billboards in r3d stand upright; this one faces the eye)
    const V3 rx = r.right() * (size * .5), uy = r.up() * (size * .5);
    const V3 n = r.fwd() * -1.0;
    Vtx q[6] = {{c - rx - uy, n, 0, 1, col}, {c + rx - uy, n, 1, 1, col}, {c + rx + uy, n, 1, 0, col},
                {c - rx - uy, n, 0, 1, col}, {c + rx + uy, n, 1, 0, col}, {c - rx + uy, n, 0, 0, col}};
    r.draw(q, 6, tex, static_cast<std::uint16_t>(kGlowMat | extra));
}

void Chamber::streak(V3 a, V3 b, double width, Col col) {
    const V3 d = b - a;
    if (len(d) < 1e-6) return;
    const V3 side = norm(cross(d, r.fwd())) * (width * .5);
    Vtx q[6] = {{a - side, {0, 0, 1}, 0, 1, col}, {b - side, {0, 0, 1}, 1, 1, col}, {b + side, {0, 0, 1}, 1, 0, col},
                {a - side, {0, 0, 1}, 0, 1, col}, {b + side, {0, 0, 1}, 1, 0, col}, {a + side, {0, 0, 1}, 0, 0, col}};
    r.draw(q, 6, &tx().streak, kGlowMat);
}

// ------------------------------------------------------------------ the room
void Chamber::draw_room(const ChamberState& s, double t) {
    for (int y = 0; y < r.H; ++y) {
        const Col c = mix(hex(0x05080C), hex(0x0A1118), static_cast<float>(y) / r.H);
        float* o = r.rgb.data() + static_cast<size_t>(y) * r.W * 3;
        for (int x = 0; x < r.W; ++x, o += 3) { o[0] = c.r; o[1] = c.g; o[2] = c.b; }
    }
    // the lab floor, lit by the chamber's glow
    const double F = 16;
    quad(r, {-F, -F, -.02}, {F, -F, -.02}, {F, F, -.02}, {-F, F, -.02}, &tx().floor, hex(0x9AB0C0), unlit, 0, 0, F, F);
    const float pulse = static_cast<float>(.85 + .15 * std::sin(t * 1.3));
    decal(r, {0, 0, -.01}, 17, 17, &tx().glow, alpha(mix(kTeal, kViolet, static_cast<float>(s.hit * .6)), (.45f + .3f * static_cast<float>(s.glow)) * pulse * static_cast<float>(.4 + .6 * s.fog)),
          kGlowMat);
    // equipment along the back: racks of blinking lights and coolant columns
    const double by = 7.8;
    for (int i = 0; i < 7; ++i) {
        const double x = -9 + i * 3.1;
        slab(r, {x - 1.25, by, 0}, {x + 1.25, by + 1.2, 3.6 + (i % 3) * .5}, kMetalDark, .03);
        for (int k = 0; k < 12; ++k) {
            std::uint32_t h = static_cast<std::uint32_t>(i * 97 + k * 31);
            h = h * 2654435761u;
            const double rate = .3 + (h >> 24) / 255.0 * 1.5;
            const bool on = std::sin(t * rate * 3 + (h >> 8 & 255)) > .2;
            const Col lc = (h >> 4) % 5 == 0 ? hex(0xFF5A5A) : (h >> 4) % 3 == 0 ? hex(0xFFD24A) : kCyan;
            const V3 p{x - .9 + (k % 4) * .6, by - .02, .9 + (k / 4) * .75};
            if (on) sprite(p, .35, &tx().glow, alpha(lc, .8f));
        }
    }
    for (int sx = -1; sx <= 1; sx += 2) {
        // a coolant column: a glass tube of slowly rising bubbles
        const V3 base{sx * 10.5, 2.5, 0};
        draw_mesh(r, cylinder_mesh(14), at(base) * sc(.55, .55, 5), nullptr, alpha(kTeal, .5f), static_cast<std::uint16_t>(translucent | unlit));
        for (int k = 0; k < 6; ++k) {
            const double ph = std::fmod(t * (.12 + k * .03) + k * .17, 1.0);
            sprite(base + V3{std::sin(k * 2.0 + t) * .2, 0, .3 + ph * 4.6}, .3, &tx().glow, alpha(kCyan, .55f * static_cast<float>(1 - ph)));
        }
        sprite(base + V3{0, 0, 2.5}, 3.2, &tx().glow, alpha(kTeal, .25f));
    }
}

// ------------------------------------------------------------------ the chamber body
void Chamber::draw_box(const ChamberState& s, double t) {
    const double W = kWall, O = kWallOut, P = 5.75;
    // the plinth the emitters stand on
    slab(r, {-P, -P, -.02}, {P, P, .18}, kMetalDark, .03);
    slab(r, {-P - .08, -P - .08, .12}, {P + .08, P + .08, .16}, hex(0x0E141A));
    // the inner floor, faintly gridded (seen once the fog clears)
    decal(r, {0, 0, .19}, 8, 8, nullptr, hex(0x03060A), unlit);
    decal(r, {0, 0, .195}, 8, 8, &tx().grid, alpha(kTeal, .25f + .35f * static_cast<float>(1 - s.fog)), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
    // four walls with a bright rim
    slab(r, {-O, W, .18}, {O, O, kTop}, kMetal, .025);
    slab(r, {-O, -O, .18}, {O, -W, kTop}, kMetal, .025);
    slab(r, {-O, -W, .18}, {-W, W, kTop}, kMetal, .025);
    slab(r, {W, -W, .18}, {O, W, kTop}, kMetal, .025);
    const double rim = .05;
    slab(r, {-O, W, kTop}, {O, O, kTop + rim}, kMetalLight);
    slab(r, {-O, -O, kTop}, {O, -W, kTop + rim}, kMetalLight);
    slab(r, {-O, -W, kTop}, {-W, W, kTop + rim}, kMetalLight);
    slab(r, {W, -W, kTop}, {O, W, kTop + rim}, kMetalLight);
    // corner pylons with status lights
    for (int i = 0; i < 4; ++i) {
        const double cx = (i % 2 ? 1 : -1) * 5.05, cy = (i / 2 ? 1 : -1) * 5.05;
        slab(r, {cx - .55, cy - .55, .18}, {cx + .55, cy + .55, 1.15}, kMetal, .025);
        slab(r, {cx - .4, cy - .4, 1.15}, {cx + .4, cy + .4, 1.25}, kMetalLight);
        const float on = static_cast<float>(s.boot > 0 ? std::fmod(s.boot * 4 + i * .25, 1.0) > .5 : 1);
        const Col lc = s.lid > 0 ? kGood : mix(kCyan, kViolet, static_cast<float>(s.hit));
        sprite({cx, cy, 1.32}, .55, &tx().glow, alpha(lc, (.55f + .25f * static_cast<float>(std::sin(t * 2 + i))) * on));
    }
    // a port in the wall for every emitter: a dark aperture in a lit collar
    for (int p = 0; p < kPorts; ++p) {
        const V3 o = outward(p), h = hole(p);
        const double phi = std::atan2(o.y, o.x);
        const M34 m = at(h + o * .305) * M34::rot_z(phi) * M34::rot_y(M_PI / 2);
        draw_mesh(r, disc_mesh(12), m * sc(.13, .13, 1), nullptr, hex(0x010204), unlit);
        const PortView& pv = s.ports[static_cast<size_t>(p)];
        const bool hot = s.hover_port == p;
        Col rc = pv.kind ? port_color(pv) : kTeal;
        draw_mesh(r, torus_mesh(14, 5, .25), m * sc(.17, .17, .17), nullptr, alpha(rc, hot ? 1.f : pv.kind ? .8f : .45f), unlit);
    }
}

// ------------------------------------------------------------------ the fog
void Chamber::draw_fog(const ChamberState& s, double t) {
    if (s.fog <= .01) return;
    const double W = kWall - .02;
    const float glow = static_cast<float>(s.glow), hit = static_cast<float>(s.hit), dens = static_cast<float>(s.fog);
    // the reveal vents the fog upward and out
    const double rise = (1 - s.fog) * 1.2;
    const double zs[4] = {.28, .48, .68, .86};
    for (int k = 0; k < 4; ++k) {
        const double z = zs[k] + rise * (k + 1) * .3;
        const double sx = t * (.018 + .007 * k) * (k % 2 ? 1 : -1), sy = t * (.012 + .005 * k);
        Col c = mix(mix(kDeep, kTeal, .35f + .15f * k), kCyan, glow * .55f);
        c = mix(c, kViolet, hit * .8f);
        const float a = dens * (.42f - .05f * k) * (1 + glow * .3f);
        quad(r, {-W, -W, z}, {W, -W, z}, {W, W, z}, {-W, W, z}, &tx().fog[k % 2], alpha(c, a), static_cast<std::uint16_t>(translucent | unlit | no_depth_write | double_sided),
             sx, sy, sx + 1.3, sy + 1.3);
    }
    // light from within: brighter wisps that shimmer when a beam is inside
    const Col lit = mix(mix(kTeal, kCyan, glow), kViolet, hit);
    const double sx = -t * .03, sy = t * .021;
    quad(r, {-W, -W, .6 + rise}, {W, -W, .6 + rise}, {W, W, .6 + rise}, {-W, W, .6 + rise}, &tx().fog[1], alpha(lit, dens * (.16f + .5f * glow + .5f * hit)),
         kGlowMat, sx, sy, sx + .9, sy + .9);
    // a ripple spreading from where a beam went in (clipped to the box)
    if (s.ripple >= 0 && s.ripple < .9) {
        const double rad = .3 + s.ripple * 7, a = (1 - s.ripple / .9) * .6;
        const int n = 72;
        for (int i = 0; i < n; ++i) {
            const double a0 = i * 2 * M_PI / n, a1 = (i + 1) * 2 * M_PI / n;
            const V3 p0 = s.ripple_at + V3{std::cos(a0) * rad, std::sin(a0) * rad, 0}, p1 = s.ripple_at + V3{std::cos(a1) * rad, std::sin(a1) * rad, 0};
            if (std::fabs(p0.x) > W || std::fabs(p0.y) > W || std::fabs(p1.x) > W || std::fabs(p1.y) > W) continue;
            streak({p0.x, p0.y, .9}, {p1.x, p1.y, .9}, .22, alpha(kCyan, static_cast<float>(a) * dens));
        }
    }
}

// ------------------------------------------------------------------ emitters
void Chamber::draw_emitters(const ChamberState& s, double t) {
    for (int p = 0; p < kPorts; ++p) {
        const PortView& pv = s.ports[static_cast<size_t>(p)];
        const V3 o = outward(p), h = hole(p);
        const bool hot = s.hover_port == p, partner = s.partner == p;
        const double phi = std::atan2(o.y, o.x);
        // the housing: a round pod on the plinth, its cap the status lamp
        const V3 c = h + o * .95;
        const bool boot_on = s.boot <= 0 || s.boot * 32 > (p + 4) % kPorts;
        const M34 pod = at({c.x, c.y, .18}) * sc(.27, .27, .44);
        draw_mesh(r, cylinder_mesh(16), pod, nullptr, hot ? mix(kMetal, kMetalLight, .45f) : kMetal, toon);
        draw_outline(r, cylinder_mesh(16), pod, .022, kInk);
        draw_mesh(r, torus_mesh(16, 5, .14), at({c.x, c.y, .62}) * sc(.27, .27, .27), nullptr, kMetalLight, toon);
        // its cap: dark until the port has an answer, then lit in the answer's colour
        const float flash = static_cast<float>(pv.flash);
        Col pc = pv.kind ? port_color(pv) : hot ? hex(0x24384A) : hex(0x0E161E);
        if (pv.kind) pc = mix(pc, kWhite, flash * .6f);
        if (partner) pc = mix(pc, kWhite, static_cast<float>(.3 + .3 * std::sin(t * 9)));
        draw_mesh(r, disc_mesh(16), at({c.x, c.y, .625}) * sc(.24, .24, 1), nullptr, boot_on || pv.kind ? pc : hex(0x0A0E12), unlit);
        if (pv.kind) sprite(plate(p), .8 + flash * 1.2, &tx().glow, alpha(port_color(pv), .3f + .5f * flash));
        // the barrel, pointing into the wall
        const M34 bm = at(h + o * .62 + V3{0, 0, .5 - h.z}) * M34::rot_z(phi + M_PI) * M34::rot_y(M_PI / 2) * sc(.085, .085, .36);
        draw_mesh(r, cylinder_mesh(10), bm, nullptr, hex(0x3C4A58), toon);
        draw_outline(r, cylinder_mesh(10), bm, .015, kInk);
        // the muzzle ring: its lamp
        const V3 tip{h.x + o.x * .26, h.y + o.y * .26, .5};
        const Col lamp = pv.charge > 0 ? mix(kCyan, kWhite, static_cast<float>(pv.charge)) : hot ? kCyan : pv.kind ? port_color(pv) : kTeal;
        const M34 rm = at(tip) * M34::rot_z(phi) * M34::rot_y(M_PI / 2) * sc(.11, .11, .11);
        draw_mesh(r, torus_mesh(12, 5, .3), rm, nullptr, boot_on ? lamp : hex(0x1A2228), unlit);
        if (hot || pv.charge > 0) sprite(tip, .5 + pv.charge * 1.4, &tx().glow, alpha(kCyan, .35f + .6f * static_cast<float>(pv.charge)));
        // aiming: a faint sight line from the muzzle to the port
        if (hot && pv.charge <= 0) streak(tip, {h.x, h.y, .5}, .06, alpha(hex(0xFF4A5A), .6f));
    }
}

// ------------------------------------------------------------------ glass, markers
void Chamber::draw_glass(const ChamberState& s, double t) {
    const double W = kWall, lift = s.lid * s.lid * 6;
    const float vis = static_cast<float>(std::max(0.0, 1 - s.lid * 1.6));
    // markers: glowing beads sitting on the glass; at the end they sink to meet the atoms
    for (int cidx = 0; cidx < kN * kN; ++cidx) {
        const V3 base = cell(cidx % kN, cidx / kN, kGlassZ);
        if (s.marks >> cidx & 1) {
            const double pop = s.pop[static_cast<size_t>(cidx)];
            const double z = kGlassZ + .2 + pop * .35 - (kGlassZ + .2 - kBeamZ) * s.judge;
            const V3 c{base.x, base.y, z};
            Col col = kCyan;
            if (s.judge > .95) col = (s.atoms >> cidx & 1) ? kGood : kBad;
            const double rr = .17 * (1 + .3 * pop);
            draw_mesh(r, sphere_mesh(12, 8), at(c) * sc(rr, rr, rr), nullptr, mix(col, kWhite, .35f), unlit);
            draw_outline(r, sphere_mesh(12, 8), at(c) * sc(rr, rr, rr), .02, kInk);
            sprite(c, .85 + .2 * std::sin(t * 3 + cidx), &tx().glow, alpha(col, .55f));
            if (s.judge <= 0) decal(r, base + V3{0, 0, .006}, .55, .55, &tx().ring, alpha(col, .5f * vis), kGlowMat);
        }
        if ((s.empties >> cidx & 1) && vis > 0) decal(r, base + V3{0, 0, .008}, .7, .7, &tx().cross, alpha(hex(0x9FB4C8), .8f * vis), kGlowMat);
    }
    if (vis <= 0) return;
    const double z = kGlassZ + lift;
    // the hovered square, and faint guides out to the four ports that look along it
    if (s.hover_cell >= 0 && s.lid <= 0) {
        const int cx = s.hover_cell % kN, cy = s.hover_cell / kN;
        const V3 c = cell(cx, cy, z + .004);
        decal(r, c, .92, .92, nullptr, alpha(kCyan, .1f), kGlowMat);
        decal(r, c, 1.0, 1.0, &tx().frame, alpha(kCyan, .9f), kGlowMat);
        streak({-W, c.y, z + .003}, {W, c.y, z + .003}, .9, alpha(kCyan, .05f));
        streak({c.x, -W, z + .003}, {c.x, W, z + .003}, .9, alpha(kCyan, .05f));
    }
    // the glass: smoked, etched with the grid, a sheen across it
    quad(r, {-W, -W, z}, {W, -W, z}, {W, W, z}, {-W, W, z}, nullptr, alpha(hex(0x0A1A24), .32f * vis), static_cast<std::uint16_t>(translucent | unlit | no_depth_write | double_sided));
    decal(r, {0, 0, z + .002}, 2 * W, 2 * W, &tx().grid, alpha(kCyan, .3f * vis), kGlowMat);
    const double sh = std::fmod(t * .05, 1.0) * 2 - .5;
    quad(r, {-W, -W, z + .003}, {W, -W, z + .003}, {W, W, z + .003}, {-W, W, z + .003}, &tx().sheen, alpha(hex(0xBFEFFF), .07f * vis),
         static_cast<std::uint16_t>(additive | unlit | no_depth_write | double_sided), sh, 0, sh + 1, 1);
    // the lid's frame when it lifts away
    if (s.lid > 0) {
        const Col fc = alpha(kMetalLight, vis);
        slab(r, {-W - .1, -W - .1, z}, {W + .1, -W + .05, z + .06}, fc);
        slab(r, {-W - .1, W - .05, z}, {W + .1, W + .1, z + .06}, fc);
    }
}

// ------------------------------------------------------------------ atoms
void Chamber::draw_atoms(const ChamberState& s, double t) {
    if (s.atoms_on <= 0) return;
    const double k = std::clamp(s.atoms_on, 0.0, 1.0);
    const double grow = k < .6 ? k / .6 * 1.2 : 1.2 - (k - .6) / .4 * .2;  // overshoot then settle
    for (int cidx = 0; cidx < kN * kN; ++cidx) {
        if (!(s.atoms >> cidx & 1)) continue;
        const V3 c = cell(cidx % kN, cidx / kN, kBeamZ);
        const bool found = (s.marks >> cidx & 1) != 0;
        const Col core = s.judge > .95 ? (found ? kGood : kBad) : kAtom;
        const double rr = .21 * grow;
        const M34 m = at(c) * sc(rr, rr, rr);
        draw_mesh(r, sphere_mesh(14, 10), m, nullptr, mix(core, kWhite, .25f), unlit);
        draw_outline(r, sphere_mesh(14, 10), m, .02, hex(0x2A1200));
        sprite(c, 1.3 * grow + .15 * std::sin(t * 4 + cidx), &tx().glow, alpha(core, .65f));
        // three electron orbits in tilted planes
        for (int o = 0; o < 3; ++o) {
            const M34 rot = M34::rot_z(o * M_PI / 3 + t * .4) * M34::rot_x(1.15);
            const double R = .44 * grow;
            const int n = 28;
            for (int i = 0; i < n; ++i) {
                const double a0 = i * 2 * M_PI / n, a1 = (i + 1) * 2 * M_PI / n;
                const V3 p0 = c + rot.dir({std::cos(a0) * R, std::sin(a0) * R, 0}), p1 = c + rot.dir({std::cos(a1) * R, std::sin(a1) * R, 0});
                streak(p0, p1, .05, alpha(kCyan, .45f * static_cast<float>(k)));
            }
            const double ea = t * (2.6 + o * .7) + o * 2.1 + cidx;
            const V3 e = c + rot.dir({std::cos(ea) * R, std::sin(ea) * R, 0});
            sprite(e, .26, &tx().glow, alpha(kWhite, static_cast<float>(k)));
        }
        // the reckoning: a ring bursts outward from each atom when the markers arrive
        if (s.judge > .95) sprite(c, 1.6 + .2 * std::sin(t * 6), &tx().ring, alpha(found ? kGood : kBad, found ? .5f : .3f + .3f * static_cast<float>(std::sin(t * 7) > 0)));
    }
}

// ------------------------------------------------------------------ the side console
void Chamber::draw_console(const ChamberState& s, double t) {
    slab(r, {kConsoleX0, kConsoleY0, -.02}, {kConsoleX1, kConsoleY1, .72}, kMetalDark, .03);
    slab(r, {kConsoleX0 + .12, kConsoleY0 + .12, .72}, {kConsoleX1 - .12, kConsoleY1 - .12, .74}, hex(0x111820));
    // the points readout
    decal(r, readout() + V3{0, 0, .005}, 1.9, .95, &readout_tex(s.points, s.lid > 0), kWhite, unlit);
    decal(r, readout() + V3{0, 0, .004}, 2.05, 1.08, nullptr, kMetalLight, unlit);
    // the marker tray: one bead for each marker still to place
    for (int i = 0; i < kAtoms; ++i) {
        const V3 p = tray(i) + V3{0, 0, .005};
        const bool full = i < s.markers_left;
        const float blink = static_cast<float>(s.tray_flash > 0 && std::sin(t * 30) > 0);
        decal(r, p, .44, .44, &tx().socket, mix(kTeal, kBad, blink), static_cast<std::uint16_t>(cutout | unlit));
        if (full) {
            const V3 c = p + V3{0, 0, .14};
            draw_mesh(r, sphere_mesh(12, 8), at(c) * sc(.15, .15, .15), nullptr, mix(kCyan, kWhite, .35f), unlit);
            draw_outline(r, sphere_mesh(12, 8), at(c) * sc(.15, .15, .15), .02, kInk);
            sprite(c, .6, &tx().glow, alpha(kCyan, .4f));
        }
    }
    // the lever: a slot, a pivot, a long handle with a lit knob
    const V3 pv = lever_pivot();
    decal(r, pv + V3{0, -.45, .005}, .3, 2.0, nullptr, hex(0x02050A), unlit);
    const Col ready = s.lever_ready ? mix(kGood, kWhite, static_cast<float>(.2 + .2 * std::sin(t * 5))) : hex(0x6A2A30);
    decal(r, pv + V3{0, -1.6, .005}, .9, .22, &tx().plate, alpha(ready, s.lever_ready ? 1.f : .5f), static_cast<std::uint16_t>(cutout | unlit));
    {
        const M34 axle = at(pv + V3{-.35, 0, .12}) * M34::rot_y(M_PI / 2) * sc(.14, .14, .7);
        draw_mesh(r, cylinder_mesh(12), axle, nullptr, kMetalLight, toon);
        draw_outline(r, cylinder_mesh(12), axle, .015, kInk);
        const V3 knob = lever_knob(s.lever);
        const V3 d = knob - (pv + V3{0, 0, .12});
        const double a = std::atan2(d.y, d.z);
        const M34 shaft = at(pv + V3{0, 0, .12}) * M34::rot_x(-a) * sc(.07, .07, len(d));
        draw_mesh(r, cylinder_mesh(10), shaft, nullptr, hex(0x8A98A8), toon);
        draw_outline(r, cylinder_mesh(10), shaft, .015, kInk);
        const double kr = s.lever_hover ? .26 : .23;
        draw_mesh(r, sphere_mesh(14, 10), at(knob) * sc(kr, kr, kr), nullptr, s.lever_ready || s.lid > 0 ? kGood : hex(0xB8303C), toon);
        draw_outline(r, sphere_mesh(14, 10), at(knob) * sc(kr, kr, kr), .025, kInk);
        if (s.lever_ready) sprite(knob, 1.2 + .2 * std::sin(t * 5), &tx().glow, alpha(kGood, .5f));
    }
}

// ------------------------------------------------------------------ light
void Chamber::draw_bolts(const ChamberState& s) {
    for (const Bolt& b : s.bolts) {
        if (b.pts.size() < 2) continue;
        const double L = b.length();
        const double h1 = std::min(b.head, L), h0 = std::max(0.0, b.head - b.tail);
        double acc = 0;
        for (size_t i = 1; i < b.pts.size(); ++i) {
            const V3 a = b.pts[i - 1], c = b.pts[i];
            const double sl = len(c - a);
            if (sl <= 0) continue;
            // the faint record of everything it has crossed
            if (b.keep && acc < h1) {
                const double e = std::min(sl, h1 - acc);
                streak(a, a + (c - a) * (e / sl), .2, alpha(b.col, .75f * static_cast<float>(b.fade)));
            }
            // the bright streak
            const double s0 = std::max(h0, acc), s1 = std::min(h1, acc + sl);
            if (s1 > s0) {
                streak(a + (c - a) * ((s0 - acc) / sl), a + (c - a) * ((s1 - acc) / sl), .3, alpha(b.col, static_cast<float>(b.fade)));
                streak(a + (c - a) * ((s0 - acc) / sl), a + (c - a) * ((s1 - acc) / sl), .12, alpha(kWhite, .8f * static_cast<float>(b.fade)));
            }
            acc += sl;
        }
        if (b.head < L) {
            // the head
            double d = b.head, acc2 = 0;
            for (size_t i = 1; i < b.pts.size(); ++i) {
                const double sl = len(b.pts[i] - b.pts[i - 1]);
                if (acc2 + sl >= d) { sprite(b.pts[i - 1] + (b.pts[i] - b.pts[i - 1]) * ((d - acc2) / std::max(1e-9, sl)), .7, &tx().glow, alpha(b.col, static_cast<float>(b.fade))); break; }
                acc2 += sl;
            }
        }
    }
}

// Soft bloom: a blurred, quarter-size copy of everything bright, added back.
void Chamber::bloom() {
    const int w = r.W / 4, h = r.H / 4;
    if (w < 2 || h < 2) return;
    static std::vector<float> a, b;
    a.assign(static_cast<size_t>(w) * h * 3, 0.f);
    b.assign(a.size(), 0.f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float acc[3] = {0, 0, 0};
            for (int k = 0; k < 16; ++k) {
                const float* p = r.rgb.data() + (static_cast<size_t>(y * 4 + k / 4) * r.W + x * 4 + k % 4) * 3;
                for (int ch = 0; ch < 3; ++ch) acc[ch] += std::max(0.f, p[ch] - .55f);
            }
            for (int ch = 0; ch < 3; ++ch) a[(static_cast<size_t>(y) * w + x) * 3 + ch] = acc[ch] / 16;
        }
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                for (int ch = 0; ch < 3; ++ch) {
                    float s = 0;
                    for (int d = -2; d <= 2; ++d) s += a[(static_cast<size_t>(y) * w + std::clamp(x + d, 0, w - 1)) * 3 + ch];
                    b[(static_cast<size_t>(y) * w + x) * 3 + ch] = s / 5;
                }
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                for (int ch = 0; ch < 3; ++ch) {
                    float s = 0;
                    for (int d = -2; d <= 2; ++d) s += b[(static_cast<size_t>(std::clamp(y + d, 0, h - 1)) * w + x) * 3 + ch];
                    a[(static_cast<size_t>(y) * w + x) * 3 + ch] = s / 5;
                }
    }
    for (int y = 0; y < r.H; ++y) {
        const double fy = std::clamp((y + .5) / 4 - .5, 0.0, h - 1.001);
        const int y0 = static_cast<int>(fy);
        const float ty = static_cast<float>(fy - y0);
        for (int x = 0; x < r.W; ++x) {
            const double fx = std::clamp((x + .5) / 4 - .5, 0.0, w - 1.001);
            const int x0 = static_cast<int>(fx);
            const float tx_ = static_cast<float>(fx - x0);
            float* o = r.rgb.data() + (static_cast<size_t>(y) * r.W + x) * 3;
            for (int ch = 0; ch < 3; ++ch) {
                auto g = [&](int xx, int yy) { return a[(static_cast<size_t>(yy) * w + xx) * 3 + ch]; };
                const float v = (g(x0, y0) * (1 - tx_) + g(x0 + 1, y0) * tx_) * (1 - ty) + (g(x0, y0 + 1) * (1 - tx_) + g(x0 + 1, y0 + 1) * tx_) * ty;
                o[ch] += v * 2.2f;
            }
        }
    }
}

void Chamber::render(const ChamberState& s, double t) {
    r.time = t;
    if (s.shake > 0) {
        r.target = base_target_ + V3{std::sin(t * 71) * .05 * s.shake, 0, std::cos(t * 57) * .04 * s.shake};
        r.set_camera();
    } else if (r.target.x != base_target_.x || r.target.z != base_target_.z) {
        r.target = base_target_;
        r.set_camera();
    }
    r.clear_depth();
    r.tris_drawn = 0;
    draw_room(s, t);
    draw_box(s, t);
    draw_console(s, t);
    draw_emitters(s, t);
    draw_atoms(s, t);
    draw_fog(s, t);
    draw_bolts(s);
    draw_glass(s, t);
    bloom();
}

}  // namespace ap
