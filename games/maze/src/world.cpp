#include "world.hpp"

#include "platform/canvas.hpp"
#include "art.hpp"
#include "paints.hpp"
#include "textures.hpp"

#include <algorithm>
#include <cmath>

namespace mz {

namespace {
constexpr double kH = 1.0;  // wall height (one floor)

Tex32 to_tex(const Canvas& c) {
    Tex32 t;
    t.make(c.w, c.h);
    for (size_t i = 0; i < t.px.size(); ++i) {
        const std::uint8_t* p = &c.px[i * 4];
        const unsigned a = p[3];
        auto un = [&](unsigned v) { return a ? std::min(255u, v * 255 / a) : 0u; };
        t.px[i] = a << 24 | un(p[2]) << 16 | un(p[1]) << 8 | un(p[0]);
    }
    return t;
}

Vert vx(V3 p, float u, float v, float k) { return {p, u, v, k, k, k}; }

// a quad on the boundary of square (x,y) on side d, seen from inside the square
void wall_face(Soft3D& r, double fz, int x, int y, int d, double z0, double z1, const Tex32* tex, float k, std::uint32_t flags, double v0 = 0, double v1 = 1,
               int span = 1) {
    V3 a, b;
    switch (d) {
        case kN: a = {double(x + 1), double(y + 1), 0}; b = {double(x), double(y + 1), 0}; break;
        case kS: a = {double(x), double(y), 0}; b = {double(x + 1), double(y), 0}; break;
        case kE: a = {double(x + 1), double(y), 0}; b = {double(x + 1), double(y + 1), 0}; break;
        default: a = {double(x), double(y + 1), 0}; b = {double(x), double(y), 0}; break;
    }
    a.z = b.z = fz;
    const V3 up0{0, 0, z0}, up1{0, 0, z1};
    // `a` is the right-hand end as seen from inside the square, so u runs from b to a (left to right).
    // A tile spanning several faces takes u from the position along the wall, so it runs on.
    float ua = 1, ub = 0;
    if (span > 1) {
        const bool along_x = d == kN || d == kS;
        ua = static_cast<float>((along_x ? a.x : a.y) / span);
        ub = static_cast<float>((along_x ? b.x : b.y) / span);
    }
    r.quad(vx(a + up0, ua, static_cast<float>(v1), k), vx(b + up0, ub, static_cast<float>(v1), k), vx(b + up1, ub, static_cast<float>(v0), k), vx(a + up1, ua, static_cast<float>(v0), k), tex, flags);
}

void flat(Soft3D& r, double x0, double y0, double x1, double y1, double z, bool up, const Tex32* tex, float k, std::uint32_t flags,
          int span = 0) {
    // `up`: seen from above (a floor); otherwise from below (a ceiling). With a span, the tile
    // covers span x span squares and each square takes its part of it.
    float u0 = 0, u1 = 1, v0 = 1, v1 = 0;
    if (span > 0) {
        u0 = static_cast<float>(x0 / span); u1 = static_cast<float>(x1 / span);
        v0 = static_cast<float>(-y0 / span); v1 = static_cast<float>(-y1 / span);
    }
    const Vert a = vx({x0, y0, z}, u0, v0, k), b = vx({x1, y0, z}, u1, v0, k), c = vx({x1, y1, z}, u1, v1, k), d = vx({x0, y1, z}, u0, v1, k);
    if (up) r.quad(a, d, c, b, tex, flags);
    else r.quad(a, b, c, d, tex, flags);
}

// a lit sphere (the marble) and a faceted icosahedron (the flip stone)
void sphere(Soft3D& r, V3 c, double rad, double spin_x, double spin_y, const Tex32* tex) {
    const int sl = 14, st = 10;
    const V3 L = norm({-.4, -.5, .8});
    auto P = [&](int i, int j, Vert& out) {
        const double th = 2 * M_PI * i / sl, ph = -M_PI / 2 + M_PI * j / st;
        V3 n{std::cos(ph) * std::cos(th), std::cos(ph) * std::sin(th), std::sin(ph)};
        // roll it
        const double cy = std::cos(spin_y), sy = std::sin(spin_y), cx = std::cos(spin_x), sx = std::sin(spin_x);
        n = {n.x, n.y * cy - n.z * sy, n.y * sy + n.z * cy};
        n = {n.x * cx + n.z * sx, n.y, -n.x * sx + n.z * cx};
        const float k = static_cast<float>(.45 + .6 * std::max(0.0, dot(n, L)));
        out = {c + n * rad, static_cast<float>(i) / sl * 2, static_cast<float>(j) / st, k, k, k};
    };
    for (int j = 0; j < st; ++j)
        for (int i = 0; i < sl; ++i) {
            Vert a, b, cc, d;
            P(i, j, a); P(i + 1, j, b); P(i + 1, j + 1, cc); P(i, j + 1, d);
            r.quad(a, d, cc, b, tex, kTwoSided);
        }
}

void icosahedron(Soft3D& r, V3 c, double rad, double spin, double tilt) {
    static const double g = (1 + std::sqrt(5.0)) / 2;
    static const V3 v0[12] = {{-1, g, 0}, {1, g, 0}, {-1, -g, 0}, {1, -g, 0}, {0, -1, g}, {0, 1, g}, {0, -1, -g}, {0, 1, -g}, {g, 0, -1}, {g, 0, 1}, {-g, 0, -1}, {-g, 0, 1}};
    static const int f[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                                 {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    const V3 L = norm({-.4, -.5, .8});
    V3 v[12];
    for (int i = 0; i < 12; ++i) {
        V3 p = norm(v0[i]);
        const double cs = std::cos(spin), sn = std::sin(spin), ct = std::cos(tilt), st = std::sin(tilt);
        p = {p.x * cs - p.y * sn, p.x * sn + p.y * cs, p.z};
        p = {p.x, p.y * ct - p.z * st, p.y * st + p.z * ct};
        v[i] = p;
    }
    for (const auto& tr : f) {
        const V3 n = norm(cross(v[tr[1]] - v[tr[0]], v[tr[2]] - v[tr[0]]));
        const float k = static_cast<float>(.35 + .65 * std::max(0.0, dot(n, L)));
        r.tri({c + v[tr[0]] * rad, 0, 0, k * .78f, k * .8f, k * .84f}, {c + v[tr[2]] * rad, 0, 0, k * .78f, k * .8f, k * .84f}, {c + v[tr[1]] * rad, 0, 0, k * .78f, k * .8f, k * .84f}, nullptr, kTwoSided);
    }
}

// a picture hanging upside down from the ceiling, turned to face the camera
void hanging(Soft3D& r, V3 top, double w, double h, const Tex32* tex, float k = 1) {
    const V3 rh = norm(V3{std::cos(r.yaw), -std::sin(r.yaw), 0}) * (w * .5);
    const V3 down{0, 0, -h};
    // the texture's foot (its bottom row, v = 1) against the ceiling, its head hanging down
    r.quad({top - rh, 1, 1, k, k, k}, {top + rh, 0, 1, k, k, k}, {top + rh + down, 0, 0, k, k, k}, {top - rh + down, 1, 0, k, k, k}, tex, kAlphaTest | kTwoSided);
}

// a resident standing (or hovering) in its square; `face` mirrors it to look the other way
void standing(Soft3D& r, V3 base, double size, const Tex32* tex, bool face_left = true, float k = 1) {
    if (tex) r.sprite(base, size, size, tex, kAlphaTest, k, face_left ? 1 : -1);
}

void box(Soft3D& r, V3 lo, V3 hi, const Tex32* tex, float k) {
    // a pedestal: four sides and a top
    const Vert t[8] = {vx({lo.x, lo.y, lo.z}, 0, 1, k), vx({hi.x, lo.y, lo.z}, 1, 1, k), vx({hi.x, hi.y, lo.z}, 0, 1, k), vx({lo.x, hi.y, lo.z}, 1, 1, k),
                       vx({lo.x, lo.y, hi.z}, 0, 0, k), vx({hi.x, lo.y, hi.z}, 1, 0, k), vx({hi.x, hi.y, hi.z}, 0, 0, k), vx({lo.x, hi.y, hi.z}, 1, 0, k)};
    r.quad(t[0], t[1], t[5], t[4], tex, kTwoSided);
    r.quad(t[1], t[2], t[6], t[5], tex, kTwoSided);
    r.quad(t[2], t[3], t[7], t[6], tex, kTwoSided);
    r.quad(t[3], t[0], t[4], t[7], tex, kTwoSided);
    r.quad(t[4], t[5], t[6], t[7], tex, kTwoSided);
}
}  // namespace

const Tex32& tex_bulb() {
    static Tex32 t = [] {
        Canvas c;
        c.resize(32, 64);
        c.clear({0, 0, 0, 0});
        c.fill_circle(16, 22, 13, hex(0xFFF6B0));
        c.begin(); c.move(9, 30); c.line(23, 30); c.line(20, 42); c.line(12, 42); c.close(); c.fill(hex(0xFFF6B0));
        c.fill_circle(12, 17, 4, hex(0xFFFFFF));
        c.stroke_line(13, 24, 16, 34, hex(0xC89020), 1.2);
        c.stroke_line(19, 24, 16, 34, hex(0xC89020), 1.2);
        c.fill_rect(11, 42, 10, 9, hex(0xA8A8B0));
        for (int k = 0; k < 3; ++k) c.fill_rect(11, 44 + k * 3, 10, 1, hex(0x6A6A72));
        c.fill_rect(13, 51, 6, 3, hex(0x3A3A40));
        return to_tex(c);
    }();
    return t;
}

namespace {
// where a face's paint stands in its loop, so neighbouring faces don't move in step
std::uint32_t face_phase(int f, int x, int y, int d) { return static_cast<std::uint32_t>(x * 7919 + y * 104729 + d * 131 + f * 31); }

// where the viewer stands across a face: -1 toward its left end, 1 toward its right, seen from inside the square
double face_gaze(const Soft3D& r, double fz, int x, int y, int d) {
    const V3 centre{x + .5 + kDX[d] * .5, y + .5 + kDY[d] * .5, fz + kH * .5};
    const V3 along = d == kN ? V3{1, 0, 0} : d == kS ? V3{-1, 0, 0} : d == kE ? V3{0, 1, 0} : V3{0, -1, 0};  // the way u runs
    const V3 to = r.eye - centre;
    return std::clamp(dot(to, along) / std::max(len(to), 1e-6) * 1.5, -1.0, 1.0);
}

// one face's look between heights z0 and z1 (texture v0 at the top, v1 at the bottom): the wall,
// the wall with a poster or a framed piece of art inset over it
void face_look(Soft3D& r, const Level& lv, double fz, int f, int x, int y, int d, int look, double z0, double z1, double v0, double v1, float shade,
               double t, bool poster) {
    if (look >= 64 && look < kArtLook) {
        const int p = look - 64;
        wall_face(r, fz, x, y, d, z0, z1, &tex_paint_at(p, t, face_phase(f, x, y, d), face_gaze(r, fz, x, y, d)), shade, paint_flags(p), v0, v1);
        return;
    }
    wall_face(r, fz, x, y, d, z0, z1, &tex_wall(lv.wall, (x * 7 + y * 13 + d) % 4), shade, 0, v0, v1, wall_span(lv.wall));
    if (look < 1 || !poster) return;
    const Tex32* picture = look >= kArtLook ? &tex_art(look - kArtLook) : &tex_poster(look - 1);
    const V3 in{static_cast<double>(-kDX[d]) * .004, static_cast<double>(-kDY[d]) * .004, 0};
    V3 a, b;
    switch (d) {
        case kN: a = {x + .82, y + 1.0, 0}; b = {x + .18, y + 1.0, 0}; break;
        case kS: a = {x + .18, double(y), 0}; b = {x + .82, double(y), 0}; break;
        case kE: a = {x + 1.0, y + .18, 0}; b = {x + 1.0, y + .82, 0}; break;
        default: a = {double(x), y + .82, 0}; b = {double(x), y + .18, 0}; break;
    }
    a = a + in; b = b + in;
    const double p0 = fz + .22, p1 = fz + .86;
    r.quad(vx({a.x, a.y, p0}, 1, 1, shade), vx({b.x, b.y, p0}, 0, 1, shade), vx({b.x, b.y, p1}, 0, 0, shade), vx({a.x, a.y, p1}, 1, 0, shade),
           picture, kAlphaTest);
}
}  // namespace

V3 cell_center(Pos p, double z) { return {p.x + .5, p.y + .5, p.f * kH + z}; }

V3 Mover::world() const {
    const V3 a = cell_center(at), b = cell_center(to);
    return a + (b - a) * t;
}

void draw_world(Soft3D& r, const Level& lv, const WorldState& s, double t) {
    const int car = lv.carpet, ceil = lv.ceiling;
    // which floors are on show: the one being walked, and during an elevator ride both
    for (int f = 0; f < static_cast<int>(lv.floors.size()); ++f) {
        const bool riding = s.elevator_lift >= 0;
        if (f != s.floor && !riding) continue;
        const Floor& fl = lv.floors[static_cast<size_t>(f)];
        const double fz = f * kH;
        const int ex = static_cast<int>(std::floor(r.eye.x)), ey = static_cast<int>(std::floor(r.eye.y));
        const V3 fwd = r.fwd();
        const int R = 15;
        for (int y = std::max(0, ey - R); y <= std::min(fl.h - 1, ey + R); ++y)
            for (int x = std::max(0, ex - R); x <= std::min(fl.w - 1, ex + R); ++x) {
                const Cell& c = fl.at(x, y);
                if (c.block == Block::wall) continue;
                // behind the camera (with a margin, for the roll and the corners)
                const V3 d{x + .5 - r.eye.x, y + .5 - r.eye.y, 0};
                if (dot(d, V3{fwd.x, fwd.y, 0}) < -1.6 && len(d) > 1.5) continue;
                const Pos here{f, x, y};
                const bool lift_here = riding && s.elevator_at.x == x && s.elevator_at.y == y;
                // floor and ceiling
                if (!(lift_here && f == 1)) {
                    if (c.elevator) flat(r, x, y, x + 1, y + 1, fz, true, &tex_elevator(), .92f, 0);
                    else flat(r, x, y, x + 1, y + 1, fz, true, c.portal >= 0 ? &tex_floor_portal(car) : &tex_floor(car), .92f, 0, kFloorSpan);
                }
                if (!(lift_here && f == 0)) {
                    if (c.goal) flat(r, x, y, x + 1, y + 1, fz + kH, false, &tex_gold_wall(), .86f, 0);
                    else flat(r, x, y, x + 1, y + 1, fz + kH, false, &tex_ceiling_art(ceil), .86f, 0, kCeilingSpan);
                }
                // the elevator platform itself, riding up between the floors
                if (lift_here && f == 0) {
                    const double lz = std::clamp(s.elevator_lift, 0.0, 1.0) * kH;
                    flat(r, x, y, x + 1, y + 1, lz, true, &tex_elevator(), .95f, 0);
                    for (int dd = 0; dd < 4; ++dd) wall_face(r, 0, x, y, dd, kH, 2 * kH, &tex_wall(lv.wall, 0), .7f, kTwoSided, 0, 1, wall_span(lv.wall));
                }
                // pads
                if (c.pad >= 0) flat(r, x + .15, y + .15, x + .85, y + .85, fz + .004, true, &tex_pad(c.pad, s.pressed >> c.pad & 1), 1.f, kAlphaTest);
                if (c.cpad >= 0) flat(r, x + .15, y + .15, x + .85, y + .85, fz + kH - .004, false, &tex_pad(c.cpad, s.pressed >> c.cpad & 1), 1.f, kAlphaTest);
                // walls, and door slabs
                for (int dd = 0; dd < 4; ++dd) {
                    const int nx = x + kDX[dd], ny = y + kDY[dd];
                    const bool edge = !fl.in(nx, ny);
                    const Cell* n = edge ? nullptr : &fl.at(nx, ny);
                    const float shade = dd == kN || dd == kS ? 1.f : .8f;
                    if (edge || n->block == Block::wall) {
                        const int look = c.face[static_cast<size_t>(dd)];
                        if (c.goal) {
                            wall_face(r, fz, x, y, dd, 0, kH, &tex_gold_wall(), shade, 0);
                            continue;
                        }
                        face_look(r, lv, fz, f, x, y, dd, look, 0, kH, 0, 1, shade, t, true);
                    } else if (n->block == Block::door) {
                        const double open = n->door >= 0 && n->door < static_cast<int>(s.door_open.size()) ? s.door_open[static_cast<size_t>(n->door)] : 0;
                        const double top = kH * (1 - open);
                        if (top > .01) wall_face(r, fz, x, y, dd, 0, top, &tex_door(n->door), shade, 0, open, 1);
                    }
                }
                // a door square: its slab's top while it sinks
                if (c.block == Block::door) {
                    const double open = c.door >= 0 && c.door < static_cast<int>(s.door_open.size()) ? s.door_open[static_cast<size_t>(c.door)] : 0;
                    if (open < .99) flat(r, x, y, x + 1, y + 1, fz + kH * (1 - open), true, &tex_door(c.door), .75f, 0);
                }
                // things in this square
                const int th = lv.thing_at(here);
                if (th >= 0) {
                    const Thing& thing = lv.things[static_cast<size_t>(th)];
                    const V3 cc = cell_center(here, .5 + .05 * std::sin(t * 2 + x));
                    if (thing.kind == ThingKind::flip) {
                        icosahedron(r, cc, .2, t * 1.4 + x, .6 + .3 * std::sin(t * .7));
                    } else {
                        const bool spent = th < static_cast<int>(s.bulb_spent.size()) && s.bulb_spent[static_cast<size_t>(th)];
                        r.sprite(cc - V3{0, 0, .18}, .22, .44, &tex_bulb(), kAlphaTest | kUnlit, spent ? .5f : 1.f);
                        if (!spent) r.sprite(cc - V3{0, 0, .3}, .9, .7, &tex_glow(), kAdditive | kNoZWrite | kUnlit, .35f);
                    }
                }
                // the reward room is full of the thing: a few copies scattered about every square
                if (c.goal && s.reward && !(here == lv.goal)) {
                    for (int k = 0; k < 3; ++k) {
                        const double ox = .22 + .28 * k + .08 * std::sin(x * 3.1 + k), oy = .25 + .25 * ((k * 7 + x + y) % 3);
                        r.sprite({x + ox, y + oy, fz}, .28, .28, s.reward, kAlphaTest, .95f);
                    }
                }
                // the reward on its pedestal
                if (here == lv.goal && s.reward) {
                    box(r, cell_center(here) - V3{.22, .22, 0}, cell_center(here) + V3{.22, .22, .3}, &tex_gold_wall(), .9f);
                    const double spin = std::cos(t * 1.8);
                    r.sprite(cell_center(here, .3 + .03 * std::sin(t * 2.4)), .55, .55, s.reward, kAlphaTest, 1.f, std::fabs(spin) < .08 ? .08 : spin);
                    r.sprite(cell_center(here, .2), 1.2, .9, &tex_glow(), kAdditive | kNoZWrite | kUnlit, .25f);
                }
            }
        draw_patches(r, s.patches, f, t, s.snail.clock);  // the snail's paint, over floor, walls and ceiling
    }
    // the giant ball: the stone marble, or the frog on his penny-farthing, the Thinking Face, or Mazeball
    if (s.marble.alive && (s.marble.at.f == s.floor || s.elevator_lift >= 0)) {
        const V3 p = s.marble.world() + V3{0, 0, .42};
        const double rx = s.marble.dir == kE || s.marble.dir == kW ? s.marble.roll * (s.marble.dir == kE ? 1 : -1) : 0;
        const double ry = s.marble.dir == kN || s.marble.dir == kS ? s.marble.roll * (s.marble.dir == kN ? -1 : 1) : 0;
        // heading left across the view, or right: the frog is drawn riding to the left
        const double across = kDX[s.marble.dir] * std::cos(r.yaw) - kDY[s.marble.dir] * std::sin(r.yaw);
        const Resident wheel[] = {Resident::frog0, Resident::frog1, Resident::frog2, Resident::frog3};
        const Tex32* frog = resident_tex(wheel[static_cast<int>(s.marble.roll * 3) & 3]);
        const Tex32* thinker = resident_tex(Resident::thinker);
        const Tex32* eyes = resident_tex(Resident::googly);
        if (s.ball == Ball::frog && frog) {
            standing(r, s.marble.world(), 1.05, frog, across <= .1);
        } else if (s.ball == Ball::thinker && thinker) {
            const V3 at = s.marble.world() + V3{0, 0, .06 + .04 * std::sin(t * 2.2)};
            r.sprite(at, .92, .92, thinker, kAlphaTest | kUnlit, 1.f);  // he glows: lit from within, and the halo is his own
        } else if (s.ball == Ball::mazeball && eyes) {
            sphere(r, p, .42, rx, ry, &tex_wall(lv.wall, 0));
            const V3 to_eye = norm(V3{r.eye.x - p.x, r.eye.y - p.y, 0});
            r.sprite(p + to_eye * .43 + V3{0, 0, .02}, .5, .25, eyes, kAlphaTest, 1.f);  // googly eyes, always on you
        } else {
            sphere(r, p, .42, rx, ry, &tex_marble());
        }
    }
    // the stay-at-homes
    const auto here_floor = [&s](const Dweller& d) { return d.alive && d.at.f == s.floor; };
    if (here_floor(s.ceiling_cat)) hanging(r, cell_center(s.ceiling_cat.at, kH - .004), .36, .36, resident_tex(Resident::ceiling_cat));
    if (here_floor(s.goose)) hanging(r, cell_center(s.goose.at, kH - .004), .42, .42, resident_tex(Resident::goose));
    if (here_floor(s.keyboard_cat)) standing(r, cell_center(s.keyboard_cat.at), .62, resident_tex(Resident::keyboard_cat));
    if (here_floor(s.raptor)) {
        // he faces the trophy: drawn looking left, so mirrored when the trophy is to his right as you see it
        const V3 rp = cell_center(s.raptor.at), gp = cell_center(lv.goal);
        const bool trophy_left = dot(gp - rp, r.right()) < 0;
        standing(r, rp, .95, resident_tex(s.raptor.pose ? Resident::raptor_fold : Resident::raptor_think), trophy_left);
    }
    if (s.monke_rides && s.elevator_lift >= 0 && s.elevator_at.x >= 0) {
        // on the plate with you, just in front
        const V3 plate = cell_center({0, s.elevator_at.x, s.elevator_at.y}, std::clamp(s.elevator_lift, 0.0, 1.0) * kH);
        const V3 ahead = norm(V3{r.fwd().x, r.fwd().y, 0});
        standing(r, plate + ahead * .46, .7, resident_tex(Resident::monke));
    }
    // the snail, in the round
    if (s.snail.alive && s.snail.on.f == s.floor) draw_snail(r, s.snail);
    // a visitor
    if (s.visitor.alive && s.visitor.at.f == s.floor && s.visitor.tex) {
        const double a = std::clamp(s.visitor.appear, 0.0, 1.0);
        r.sprite(cell_center(s.visitor.at, s.visitor.lift + .02 * std::sin(s.visitor.bob)), s.visitor.h * s.visitor.tex->w / s.visitor.tex->h, s.visitor.h * a, s.visitor.tex, kAlphaTest, static_cast<float>(.6 + .4 * a));
    }
}

}  // namespace mz
