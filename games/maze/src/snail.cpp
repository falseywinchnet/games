#include "snail.hpp"

#include "paints.hpp"

#include <algorithm>
#include <cmath>

namespace mz {
namespace {

constexpr double kH = 1.0;  // wall height (one floor)
constexpr double kPi = 3.14159265358979323846;

std::uint32_t hsh(std::uint32_t x) {
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return x;
}
double h01(std::uint32_t x) { return (hsh(x) & 0xFFFFFF) / 16777215.0; }
double next01(std::uint32_t& s) {
    s = hsh(s + 0x9E3779B9u);
    return (s & 0xFFFFFF) / 16777215.0;
}

// a square it climbs the walls of (rock, or beyond the edge), and one it may crawl into;
// doors, the reward room and the elevator are neither, and it turns back from them
bool solid(const Level& lv, int f, int x, int y) {
    const Floor& fl = lv.floors[static_cast<size_t>(f)];
    return !fl.in(x, y) || fl.at(x, y).block == Block::wall;
}
bool passable(const Level& lv, int f, int x, int y) {
    const Floor& fl = lv.floors[static_cast<size_t>(f)];
    if (!fl.in(x, y)) return false;
    const Cell& c = fl.at(x, y);
    return c.block == Block::open && !c.goal && !c.elevator;
}

// The surface across one edge of `s` (0 where u is 0, 1 where u is 1, 2 where v is 0, 3 where v is 1);
// false where the snail must turn back.
bool across(const Level& lv, const Surface& s, int edge, Surface& out) {
    const int f = s.f, x = s.x, y = s.y;
    if (s.side < 2) {  // the floor or the ceiling: on to the next square's, or up (down) its wall
        const int d = edge == 0 ? kW : edge == 1 ? kE : edge == 2 ? kS : kN;
        const int nx = x + kDX[d], ny = y + kDY[d];
        if (solid(lv, f, nx, ny)) { out = {f, x, y, 2 + d}; return true; }
        if (!passable(lv, f, nx, ny)) return false;
        out = {f, nx, ny, s.side};
        return true;
    }
    const int d = s.side - 2;
    if (edge == 2) { out = {f, x, y, 0}; return true; }
    if (edge == 3) { out = {f, x, y, 1}; return true; }
    const int side = edge == 1 ? (d + 1) % 4 : (d + 3) % 4;  // u runs toward the right-hand side
    const int x2 = x + kDX[side], y2 = y + kDY[side];
    if (solid(lv, f, x2, y2)) { out = {f, x, y, 2 + side}; return true; }  // into the corner and on up the next wall
    if (!passable(lv, f, x2, y2)) return false;
    const int x3 = x2 + kDX[d], y3 = y2 + kDY[d];
    if (solid(lv, f, x3, y3)) { out = {f, x2, y2, 2 + d}; return true; }  // the wall runs on
    if (!passable(lv, f, x3, y3)) return false;
    out = {f, x3, y3, 2 + (side + 2) % 4};                                  // round the end of the wall
    return true;
}

// paint and a line of slime under the snail: a brush a little wider than it is, ragged at the edge,
// its bristles leaving streaks along the way it goes
void stamp(Patch& p, const Surface& on, double u, double v, double heading, int paint, double clock, std::uint32_t stroke) {
    const double cx = u * kPatch, cy = (1 - v) * kPatch, r = .15 * kPatch;
    const double sx = -std::sin(heading), sy = -std::cos(heading);  // across the stroke, in texels (rows run down as v runs up)
    const std::uint32_t salt = hsh(static_cast<std::uint32_t>(on.x * 73856093 ^ on.y * 19349663 ^ on.side * 83492791 ^ on.f));
    for (int y = std::max(0, static_cast<int>(cy - r - 1)); y <= std::min(kPatch - 1, static_cast<int>(cy + r + 1)); ++y)
        for (int x = std::max(0, static_cast<int>(cx - r - 1)); x <= std::min(kPatch - 1, static_cast<int>(cx + r + 1)); ++x) {
            const double dx = x + .5 - cx, dy = y + .5 - cy, d = std::hypot(dx, dy);
            const double edge = r * (.84 + .16 * h01(salt ^ static_cast<std::uint32_t>((x / 2) * 31 + (y / 2) * 977)));
            if (d >= edge) continue;
            const double lateral = dx * sx + dy * sy;
            if (d > r * .45 && h01(stroke * 131 + static_cast<std::uint32_t>(std::floor(lateral * .8) + 64)) < .14) continue;  // a dry bristle
            const size_t i = static_cast<size_t>(y) * kPatch + x;
            p.paint[i] = static_cast<std::uint8_t>(paint + 1);
            if (std::fabs(lateral) < 1.6) p.slime[i] = static_cast<float>(clock);
        }
}

void compose(const Patch& p, const Surface& s, double t, double clock) {
    if (p.shown_t == t && !p.shown.px.empty()) return;
    p.shown_t = t;
    if (p.shown.px.empty()) p.shown.make(kPatch, kPatch);
    const std::uint32_t phase = static_cast<std::uint32_t>(s.x * 7919 + s.y * 104729 + s.side * 131 + s.f * 31);
    const Tex32* frame[256] = {};
    std::uint32_t flags[256] = {};
    const std::uint32_t sparkle = static_cast<std::uint32_t>(t * 5);
    for (int y = 0; y < kPatch; ++y)
        for (int x = 0; x < kPatch; ++x) {
            const size_t i = static_cast<size_t>(y) * kPatch + x;
            const int id = p.paint[i];
            if (!id) { p.shown.px[i] = 0; continue; }
            if (!frame[id]) { frame[id] = &tex_paint_at(id - 1, t, phase, 0); flags[id] = paint_flags(id - 1); }
            const Tex32& tex = *frame[id];
            const int tx = x * tex.w / kPatch;
            int ty = y * tex.h / kPatch;
            if (flags[id] & kRain) ty -= static_cast<int>(t * (10 + ((tx >> 3) & 15) * 2.5));
            if (flags[id] & kSlide) ty += static_cast<int>(t * 5);
            const std::uint32_t c = tex.at(tx, ty);
            double r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
            // the stroke's edge, where the paint lies thicker
            const bool rim = (x > 0 && !p.paint[i - 1]) || (x < kPatch - 1 && !p.paint[i + 1]) || (y > 0 && !p.paint[i - kPatch]) ||
                             (y < kPatch - 1 && !p.paint[i + kPatch]);
            if (rim) { r *= .72; g *= .72; b *= .72; }
            // slime: wet and glistening, drying over half a minute
            const double wet = std::clamp(1 - (clock - p.slime[i]) / 30, 0.0, 1.0);
            if (wet > 0) {
                r += (232 - r) * .4 * wet; g += (240 - g) * .4 * wet; b += (255 - b) * .45 * wet;
                if (h01(static_cast<std::uint32_t>(i) * 31 + sparkle * 7919) > .97) { r = g = b = 255 * wet + r * (1 - wet); }
            }
            p.shown.px[i] = 0xFF000000u | static_cast<std::uint32_t>(r) << 16 | static_cast<std::uint32_t>(g) << 8 | static_cast<std::uint32_t>(b);
        }
}

// ---------------------------------------------------------------- the snail in the round
const Tex32& tex_skin() {
    static const Tex32 t = [] {
        Tex32 s;
        s.make(16, 16);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                const double k = .86 + .2 * h01(static_cast<std::uint32_t>(x * 16 + y + 300)) - (h01(static_cast<std::uint32_t>(x * 3 + y * 51)) > .9 ? .2 : 0);
                s.px[static_cast<size_t>(y) * 16 + x] = 0xFF000000u | static_cast<std::uint32_t>(200 * k) << 16 | static_cast<std::uint32_t>(216 * k) << 8 |
                                                         static_cast<std::uint32_t>(144 * k);
            }
        return s;
    }();
    return t;
}
const Tex32& tex_shell() {  // pink, with a dark spiral wound into it
    static const Tex32 t = [] {
        Tex32 s;
        s.make(64, 64);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const double dx = x + .5 - 32, dy = y + .5 - 32, r = std::hypot(dx, dy), a = std::atan2(dy, dx);
                const double turn = r / 5.2 - a / (2 * kPi);  // whole numbers fall on the spiral's groove
                const double groove = std::fabs(turn - std::floor(turn + .5));
                double k = .78 + .22 * (1 - r / 46);
                std::uint32_t rgb = 0xE07AB0;
                if (groove < .1) rgb = 0x7A2A6A;
                else if (groove < .18) k *= .85;
                const double cr = ((rgb >> 16) & 255) * k, cg = ((rgb >> 8) & 255) * k, cb = (rgb & 255) * k;
                s.px[static_cast<size_t>(y) * 64 + x] = 0xFF000000u | static_cast<std::uint32_t>(cr) << 16 | static_cast<std::uint32_t>(cg) << 8 |
                                                         static_cast<std::uint32_t>(cb);
            }
        return s;
    }();
    return t;
}
const Tex32& tex_ink() {
    static const Tex32 t = [] {
        Tex32 s;
        s.make(1, 1);
        s.px[0] = 0xFF141418u;
        return s;
    }();
    return t;
}

// a triangle lit from the viewer's side, so the snail reads in the round whichever way it faces
void lit_tri(Soft3D& r, const V3& a, const V3& b, const V3& c, float ua, float va, float ub, float vb, float uc, float vc, const Tex32* tex) {
    const V3 n = norm(cross(b - a, c - a));
    const V3 to = norm(r.eye - (a + b + c) * (1.0 / 3));
    const float k = static_cast<float>(.42 + .62 * std::fabs(dot(n, to)));
    r.tri({a, ua, va, k, k, k}, {b, ub, vb, k, k, k}, {c, uc, vc, k, k, k}, tex, kTwoSided);
}

}  // namespace

Frame surface_frame(const Surface& s) {
    const double fz = s.f * kH, x = s.x, y = s.y;
    if (s.side == 0) return {{x, y, fz}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    if (s.side == 1) return {{x, y, fz + kH}, {1, 0, 0}, {0, 1, 0}, {0, 0, -1}};
    const int d = s.side - 2;
    V3 a, b;  // as the wall is drawn: `a` the right-hand end seen from the square
    switch (d) {
        case kN: a = {x + 1, y + 1, fz}; b = {x, y + 1, fz}; break;
        case kS: a = {x, y, fz}; b = {x + 1, y, fz}; break;
        case kE: a = {x + 1, y, fz}; b = {x + 1, y + 1, fz}; break;
        default: a = {x, y + 1, fz}; b = {x, y, fz}; break;
    }
    return {b, a - b, {0, 0, 1}, {-static_cast<double>(kDX[d]), -static_cast<double>(kDY[d]), 0}};
}

V3 Snail::world() const {
    const Frame f = surface_frame(on);
    return f.o + f.u * u + f.v * v;
}
V3 Snail::forward() const {
    const Frame f = surface_frame(on);
    return f.u * std::cos(heading) + f.v * std::sin(heading);
}
V3 Snail::normal() const { return surface_frame(on).n; }

void snail_place(Snail& s, Pos at, std::uint32_t seed) {
    s = Snail{};
    s.alive = true;
    s.on = {at.f, at.x, at.y, 0};
    s.rng = hsh(seed + 17);
    s.heading = next01(s.rng) * 2 * kPi;
    s.turn1 = next01(s.rng) * 2 * kPi;
    s.turn2 = next01(s.rng) * 2 * kPi;
    s.paint = static_cast<int>(next01(s.rng) * paint_count());
    s.paint_t = 6 + 8 * next01(s.rng);
}

SnailNews snail_step(Snail& s, const Level& lv, Patches& patches, V3 viewer, double dt) {
    SnailNews news;
    if (!s.alive) return news;
    s.clock += dt;
    // shy: into its shell when you come close, and out again a while after you have gone
    const double dist = len(viewer - s.world());
    if (dist < 1.25) {
        if (s.hide == 0) news.hid = true;
        s.away = 0;
        s.hide = std::min(1.0, s.hide + dt / .22);
    } else if (dist > 1.6) {
        s.away += dt;
        if (s.away > 1.6 && s.hide > 0) {
            if (s.hide == 1) news.peeked = true;
            s.hide = std::max(0.0, s.hide - dt / 1.3);
        }
    } else {
        s.away = 0;
    }
    if (s.hide > 0) return news;
    // never straight: its heading wanders on two slow swells and a quick wobble
    const double c = s.clock;
    s.heading += (.55 * std::sin(.37 * c + s.turn1) + .35 * std::sin(1.1 * c + s.turn2) + .12 * std::sin(2.9 * c)) * dt;
    const double speed = .2 * (.85 + .15 * std::sin(c * 5));  // in pulses, as it ripples along
    s.u += std::cos(s.heading) * speed * dt;
    s.v += std::sin(s.heading) * speed * dt;
    // over an edge: on to the next surface, folding round onto a wall or the ceiling, or back from a door
    for (int guard = 0; guard < 2; ++guard) {
        int edge = -1;
        double over = 0;
        if (-s.u > over) { edge = 0; over = -s.u; }
        if (s.u - 1 > over) { edge = 1; over = s.u - 1; }
        if (-s.v > over) { edge = 2; over = -s.v; }
        if (s.v - 1 > over) { edge = 3; over = s.v - 1; }
        if (edge < 0) break;
        Surface next;
        if (!across(lv, s.on, edge, next)) {
            if (edge < 2) { s.heading = kPi - s.heading; s.u = edge ? 1 - over : over; }
            else { s.heading = -s.heading; s.v = edge == 3 ? 1 - over : over; }
            continue;
        }
        const Frame a = surface_frame(s.on), b = surface_frame(next);
        const V3 p = a.o + a.u * std::clamp(s.u, 0.0, 1.0) + a.v * std::clamp(s.v, 0.0, 1.0);
        const V3 t = a.u * std::cos(s.heading) + a.v * std::sin(s.heading);
        const V3 e = edge < 2 ? a.v : a.u;                                  // along the edge
        const V3 out = (edge < 2 ? a.u : a.v) * (edge % 2 ? 1.0 : -1.0);   // off this surface
        V3 in = b.o + b.u * .5 + b.v * .5 - p;                              // onto the next, away from the edge
        in = norm(in - e * dot(in, e));
        const V3 t2 = e * dot(t, e) + in * std::max(dot(t, out), .05);
        const V3 p2 = p + in * over;
        s.on = next;
        s.u = dot(p2 - b.o, b.u);
        s.v = dot(p2 - b.o, b.v);
        s.heading = std::atan2(dot(t2, b.v), dot(t2, b.u));
    }
    s.u = std::clamp(s.u, 0.0, 1.0);
    s.v = std::clamp(s.v, 0.0, 1.0);
    if (s.clock >= s.paint_t) {  // a new colour now and then
        s.paint = static_cast<int>(next01(s.rng) * paint_count()) % paint_count();
        s.paint_t = s.clock + 7 + 7 * next01(s.rng);
    }
    stamp(patches[s.on], s.on, s.u, s.v, s.heading, s.paint, s.clock, static_cast<std::uint32_t>(s.paint_t * 10));
    news.painted = true;
    if (s.clock >= s.squelch_t) {
        news.squelched = true;
        s.squelch_t = s.clock + .8 + .5 * next01(s.rng);
    }
    return news;
}

void draw_snail(Soft3D& r, const Snail& s) {
    if (!s.alive) return;
    const V3 P = s.world(), F = s.forward(), N = s.normal(), S = cross(N, F);
    const double e = s.hide * s.hide * (3 - 2 * s.hide);       // eased into the shell
    const double moving = s.hide > 0 ? 0 : 1, c = s.clock;
    // the body, tail to head: along it, its half width and its height; it ripples as it goes
    const double fs[] = {-.16, -.11, -.04, .04, .1, .145, .17};
    const double ws[] = {.004, .03, .042, .044, .04, .032, .014};
    const double hs[] = {.004, .02, .03, .036, .055, .066, .05};
    constexpr int kSections = 7, kRound = 6;
    V3 ring[kSections][kRound];
    for (int k = 0; k < kSections; ++k) {
        double f = fs[k] + moving * .012 * std::sin(c * 5 - fs[k] * 25);
        f = -.02 + (f + .02) * (1 - .82 * e);
        const double w = ws[k] * (1 - .5 * e), h = (hs[k] + moving * .005 * std::sin(c * 5 + k)) * (1 - .65 * e);
        for (int j = 0; j < kRound; ++j) {
            const double a = j * kPi / (kRound - 1);
            ring[k][j] = P + F * f + S * (std::cos(a) * w) + N * (std::sin(a) * h + .002);
        }
    }
    const Tex32* skin = &tex_skin();
    for (int k = 0; k + 1 < kSections; ++k) {
        const float u0 = static_cast<float>(k) / kSections, u1 = static_cast<float>(k + 1) / kSections;
        for (int j = 0; j + 1 < kRound; ++j) {
            const float v0 = static_cast<float>(j) / kRound, v1 = static_cast<float>(j + 1) / kRound;
            lit_tri(r, ring[k][j], ring[k + 1][j], ring[k + 1][j + 1], u0, v0, u1, v0, u1, v1, skin);
            lit_tri(r, ring[k][j], ring[k + 1][j + 1], ring[k][j + 1], u0, v0, u1, v1, u0, v1, skin);
        }
        lit_tri(r, ring[k][0], ring[k + 1][0], ring[k + 1][kRound - 1], u0, 0, u1, 0, u1, 1, skin);  // the sole
        lit_tri(r, ring[k][0], ring[k + 1][kRound - 1], ring[k][kRound - 1], u0, 0, u1, 1, u0, 1, skin);
    }
    // the shell, its spiral on both flanks
    const V3 C = P + F * (-.03) + N * (.074 - .022 * e);
    const double rf = .078, rs = .052, rn = .072;
    constexpr int kLat = 7, kLon = 12;
    const Tex32* shell = &tex_shell();
    for (int i = 0; i < kLat; ++i)
        for (int j = 0; j < kLon; ++j) {
            V3 q[4];
            float qu[4], qv[4];
            for (int k = 0; k < 4; ++k) {
                const double th = (i + k / 2) * kPi / kLat, ph = (j + ((k + 1) / 2) % 2) * 2 * kPi / kLon;
                q[k] = C + S * (std::cos(th) * rs) + (F * (std::cos(ph) * rf) + N * (std::sin(ph) * rn)) * std::sin(th);
                qu[k] = static_cast<float>(.5 + .5 * std::cos(ph) * std::sin(th));
                qv[k] = static_cast<float>(.5 - .5 * std::sin(ph) * std::sin(th));
            }
            lit_tri(r, q[0], q[1], q[2], qu[0], qv[0], qu[1], qv[1], qu[2], qv[2], shell);
            lit_tri(r, q[0], q[2], q[3], qu[0], qv[0], qu[2], qv[2], qu[3], qv[3], shell);
        }
    // eye stalks: they draw in first when it hides, and come out last
    const double out = 1 - std::clamp(s.hide * 2.5, 0.0, 1.0);
    if (out <= 0) return;
    const V3 head = P + F * (.15 * (1 - .82 * e)) + N * (.06 * (1 - .65 * e));
    for (int side = -1; side <= 1; side += 2) {
        const double sway = .012 * std::sin(c * 2.3 + side * 1.7);
        const V3 base = head + S * (side * .013);
        const V3 tip = base + (F * (.03 + sway) + N * .075 + S * (side * .014)) * out;
        const V3 w = norm(cross(tip - base, r.eye - base)) * .0045;
        lit_tri(r, base - w, base + w, tip + w, 0, 0, 1, 0, 1, 1, skin);
        lit_tri(r, base - w, tip + w, tip - w, 0, 0, 1, 1, 0, 1, skin);
        const double er = .011 * out;  // the eye, a little dark bead
        const V3 ax[3] = {F * er, S * er, N * er};
        for (int k = 0; k < 8; ++k) {
            const V3 a = ax[0] * (k & 1 ? 1.0 : -1.0), b = ax[1] * (k & 2 ? 1.0 : -1.0), d = ax[2] * (k & 4 ? 1.0 : -1.0);
            lit_tri(r, tip + a, tip + b, tip + d, 0, 0, 0, 0, 0, 0, &tex_ink());
        }
    }
}

void draw_patches(Soft3D& r, const Patches& patches, int f, double t, double snail_clock) {
    for (const std::pair<const Surface, Patch>& sp : patches) {
        const Surface& s = sp.first;
        if (s.f != f) continue;
        const Frame fr = surface_frame(s);
        const V3 mid = fr.o + fr.u * .5 + fr.v * .5, d = mid - r.eye;
        if (len(d) > 15 || (dot(d, r.fwd()) < -1.0 && len(d) > 1.5)) continue;
        compose(sp.second, s, t, snail_clock);
        const float k = s.side == 0 ? .92f : s.side == 1 ? .86f : s.side - 2 == kN || s.side - 2 == kS ? 1.f : .8f;
        const V3 o = fr.o + fr.n * .005;  // over the posters too
        r.quad({o, 0, 1, k, k, k}, {o + fr.u, 1, 1, k, k, k}, {o + fr.u + fr.v, 1, 0, k, k, k}, {o + fr.v, 0, 0, k, k, k}, &sp.second.shown,
               kAlphaTest | kTwoSided);
    }
}

}  // namespace mz
