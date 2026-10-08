#include "pasture.hpp"

#include "platform/mesh.hpp"
#include "platform/raster.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace sh {

namespace {
const Col kWhite{1, 1, 1, 1};
const Col kInk = hex(0x2A2620);
constexpr double kR = .5;             // hex radius (centre to corner)
constexpr double kDX = 0.8660254 * 2 * kR, kDY = 1.5 * kR;
M34 at(V3 v) { return M34::translate(v.x, v.y, v.z); }
M34 at(double x, double y, double z) { return M34::translate(x, y, z); }
M34 sc(double x, double y, double z) { return M34::scale(x, y, z); }
M34 sc(double s) { return M34::scale(s, s, s); }
std::uint32_t hsh(std::uint32_t x) { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x; }
double h01(std::uint32_t x) { return (hsh(x) & 0xFFFF) / 65535.0; }
void part(R3D& r, const Mesh& m, const M34& model, Col c, double ink = .02) {
    draw_mesh(r, m, model, nullptr, c, toon);
    if (ink > 0) draw_outline(r, m, model, ink, kInk);
}
void flat(R3D& r, const Mesh& m, const M34& model, Col c) { draw_mesh(r, m, model, nullptr, c, unlit); }

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
    Tex grass, field, sky, glow, cloud, hills;
    Textures() {
        Canvas c;
        c.resize(64, 64);
        // a patch of grass: soft blades and a few daisies
        c.clear(hex(0x7CC060));
        for (int k = 0; k < 160; ++k) { const double x = h01(k) * 64, y = h01(k + 300) * 64; c.begin(); c.move(x, y); c.line(x + (h01(k + 600) - .5) * 2, y - 2 - h01(k + 900) * 3); c.stroke(h01(k + 50) < .5 ? hex(0x6AB050) : hex(0x92D070), .8); }
        grass = tex_from(c);
        // the wider field beyond: longer grass, flowers
        c.clear(hex(0x6FB25A));
        for (int k = 0; k < 120; ++k) { const double x = h01(k + 77) * 64, y = h01(k + 377) * 64; c.begin(); c.move(x, y); c.line(x + (h01(k + 677) - .5) * 3, y - 3 - h01(k + 977) * 4); c.stroke(h01(k + 51) < .5 ? hex(0x5EA04C) : hex(0x88C868), .9); }
        for (int k = 0; k < 14; ++k) c.fill_circle(h01(k + 1200) * 64, h01(k + 1300) * 64, 1.1, k % 3 ? hex(0xFFFFFF) : hex(0xF8E070));
        field = tex_from(c);
        // the sky: deep blue above, pale at the horizon
        c.begin(); c.rect(0, 0, 64, 64); c.fill(Paint::lin(0, 0, 0, 64, {{0, hex(0x5AA0E8)}, {.7f, hex(0xA8D4F4)}, {1, hex(0xE0F0F8)}}));
        sky = tex_from(c);
        // distant hills
        c.clear({0, 0, 0, 0});
        c.begin(); c.move(0, 64); c.line(0, 40); for (int x = 0; x <= 64; x += 4) c.line(x, 34 + 8 * std::sin(x * .1) + 4 * std::sin(x * .27)); c.line(64, 64); c.close(); c.fill(hex(0x8EC28A));
        c.begin(); c.move(0, 64); c.line(0, 50); for (int x = 0; x <= 64; x += 4) c.line(x, 46 + 6 * std::sin(x * .14 + 1) + 3 * std::sin(x * .31)); c.line(64, 64); c.close(); c.fill(hex(0x72B06A));
        hills = tex_from(c);
        c.resize(32, 32);
        c.clear({0, 0, 0, 0});
        c.begin(); c.circle(16, 16, 16); c.fill(Paint::rad(16, 16, 16, {{0, {1, 1, 1, 1}}, {.35f, {1, 1, 1, .5f}}, {1, {1, 1, 1, 0}}}));
        glow = tex_from(c);
        c.resize(64, 32);
        c.clear({0, 0, 0, 0});
        for (int k = 0; k < 7; ++k) c.fill_circle(10 + k * 7.5, 20 - 6 * std::sin(k * .45), 7 + 3 * std::sin(k * 1.3), hex(0xFFFFFF));
        c.fill_rect(8, 20, 50, 7, hex(0xFFFFFF));
        cloud = tex_from(c);
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
// a hexagonal patch: a flat top and short sides
void hex_patch(R3D& r, V3 c, double rad, double height, Col top, Col side, const Tex* tex) {
    Vtx v[18 + 36];
    int n = 0;
    for (int k = 0; k < 6; ++k) {
        const double a0 = M_PI / 6 + k * M_PI / 3, a1 = a0 + M_PI / 3;
        const V3 p0{c.x + std::cos(a0) * rad, c.y + std::sin(a0) * rad, c.z + height}, p1{c.x + std::cos(a1) * rad, c.y + std::sin(a1) * rad, c.z + height};
        const V3 up{0, 0, 1};
        v[n++] = {c + V3{0, 0, height}, up, (c.x) * .5, (c.y) * .5, top};
        v[n++] = {p0, up, p0.x * .5, p0.y * .5, top};
        v[n++] = {p1, up, p1.x * .5, p1.y * .5, top};
    }
    r.draw(v, static_cast<size_t>(n), tex, toon);
    n = 0;
    for (int k = 0; k < 6; ++k) {
        const double a0 = M_PI / 6 + k * M_PI / 3, a1 = a0 + M_PI / 3;
        const V3 p0{c.x + std::cos(a0) * rad, c.y + std::sin(a0) * rad, c.z}, p1{c.x + std::cos(a1) * rad, c.y + std::sin(a1) * rad, c.z};
        const V3 nn = norm(V3{std::cos(a0 + M_PI / 6), std::sin(a0 + M_PI / 6), 0});
        const V3 h{0, 0, height};
        v[n++] = {p1, nn, 0, 0, side}; v[n++] = {p0, nn, 0, 0, side}; v[n++] = {p0 + h, nn, 0, 0, side};
        v[n++] = {p1, nn, 0, 0, side}; v[n++] = {p0 + h, nn, 0, 0, side}; v[n++] = {p1 + h, nn, 0, 0, side};
    }
    r.draw(v, static_cast<size_t>(n), nullptr, toon);
}
}  // namespace

void Pasture::resize(int w, int h, int top_bar, int bottom_bar) {
    land_valid_ = false;
    r.resize(w, h);
    w_ = w;
    h_ = h;
    top_ = top_bar;
    bottom_ = bottom_bar;
    frame(n_);
}

void Pasture::frame(int size) {
    land_valid_ = false;
    n_ = size;
    r.yaw = 0;
    r.pitch = .78;
    r.persp = 14;
    const double bw = (n_ + .5) * kDX + .6, bh = (n_ - 1) * kDY + 1.6;
    r.scale = std::min((w_ - 8) / bw, (h_ - top_ - bottom_) / (bh * std::sin(r.pitch) + .9));
    r.ax = .5;
    r.ay = (top_ + (h_ - top_ - bottom_) * .5) / h_;
    r.target = {0, 0, 0};
    r.light.sun = norm({-.5, -.35, .8});
    r.light.sun_col = {.75f, .7f, .58f, 1};
    r.light.amb_col = {.58f, .62f, .68f, 1};
    r.light.fog_col = {.82f, .9f, .95f, 1};
    r.light.fog_near = 9;
    r.light.fog_far = 30;
    r.light.focus = {0, 0, 0};
    r.light.toon_edge = .1f;
    r.light.toon_soft = .14f;
    r.set_camera();
}

V3 Pasture::cell_pos(int cell) const {
    const int c = cell % n_, row = cell / n_;
    const double x = (c + .5 * (row & 1) - (n_ - 1) / 2.0 - .25) * kDX;
    const double y = ((n_ - 1) / 2.0 - row) * kDY;
    return {x, y, 0};
}

void Pasture::to_screen(V3 p, double& sx, double& sy) const {
    double sz;
    r.project(p, sx, sy, sz);
}

int Pasture::pick(double sx, double sy) const {
    double wx, wy;
    if (!r.unproject_plane(sx, sy, .12, wx, wy)) return -1;
    int best = -1;
    double bd = kR * kR * .95;
    for (int i = 0; i < n_ * n_; ++i) {
        const V3 c = cell_pos(i);
        const double d = (c.x - wx) * (c.x - wx) + (c.y - wy) * (c.y - wy);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

void Pasture::draw_land(double t, bool animated) {
    // the sky, the hills, clouds drifting, birds far off
    const double far = 16;
    if (!animated) {
    quad(r, {-40, far, -1}, {40, far, -1}, {40, far, 26}, {-40, far, 26}, &tx().sky, kWhite, static_cast<std::uint16_t>(unlit | no_fog));
    quad(r, {-40, far - .5, -1}, {40, far - .5, -1}, {40, far - .5, 5}, {-40, far - .5, 5}, &tx().hills, kWhite, static_cast<std::uint16_t>(cutout | unlit), 4, 1);
    }
    if (animated) {
    for (int k = 0; k < 6; ++k) {
        const double x = std::fmod(h01(k) * 60 + t * (.15 + .05 * k), 60.0) - 30;
        const double z = 6 + h01(k + 10) * 10, s = 2 + h01(k + 20) * 2.5;
        quad(r, {x - s, far - 1 - k * .1, z}, {x + s, far - 1 - k * .1, z}, {x + s, far - 1 - k * .1, z + s}, {x - s, far - 1 - k * .1, z + s}, &tx().cloud, hex(0xFFFFFF, .9f), static_cast<std::uint16_t>(translucent | unlit | no_fog));
    }
    for (int k = 0; k < 3; ++k) {
        const double u = std::fmod(t * .03 + k * .37, 1.0);
        const V3 b{-20 + u * 40, far - 2, 9 + 2 * k + std::sin(t * .7 + k)};
        const double flap = std::sin(t * 8 + k) * .25;
        for (int s = -1; s <= 1; s += 2) quad(r, b, b + V3{s * .35, 0, .12 + flap}, b + V3{s * .35, 0, .1 + flap}, b + V3{0, 0, -.03}, nullptr, hex(0x3A3A40), static_cast<std::uint16_t>(unlit | double_sided));
    }
    }
    if (!animated) {
    // the field all around
    // (in tiles: one huge quad would warp, and reach behind the camera)
    for (int gy = 0; gy < 12; ++gy)
        for (int gx = 0; gx < 16; ++gx) {
            const double x0 = -24 + gx * 3, y0 = -8 + gy * 2;
            if (y0 > far - .6) continue;
            const double y1 = std::min(y0 + 2, far - .6);
            if (grown_) {
                // The grown field: world-placed, so it runs on from tile to tile.
                const double k = 1 / kFieldUnits;
                const V3 n{0, 0, 1};
                Vtx v[6] = {{{x0, y0, -.02}, n, x0 * k, y0 * k, kWhite}, {{x0 + 3, y0, -.02}, n, (x0 + 3) * k, y0 * k, kWhite},
                            {{x0 + 3, y1, -.02}, n, (x0 + 3) * k, y1 * k, kWhite}, {{x0, y0, -.02}, n, x0 * k, y0 * k, kWhite},
                            {{x0 + 3, y1, -.02}, n, (x0 + 3) * k, y1 * k, kWhite}, {{x0, y1, -.02}, n, x0 * k, y1 * k, kWhite}};
                r.draw(v, 6, &ground_.field, toon);
            } else
                quad(r, {x0, y0, -.02}, {x0 + 3, y0, -.02}, {x0 + 3, y1, -.02}, {x0, y1, -.02}, &tx().field, kWhite, toon, 1.2, .8 * (y1 - y0) / 2);
        }
    // no fence round the meadow: the sheep can walk off any side (the fences are what you build)
    const double fy = (n_ * kDY) / 2 + 1.2, fx = (n_ * kDX) / 2 + 1.2;
    // trees beyond the meadow, a few bushes
    for (int k = 0; k < 7; ++k) {
        const double x = -fx - 1 + h01(k + 40) * (2 * fx + 2), y = fy + 1.2 + h01(k + 50) * 3;
        const double s = .8 + h01(k + 60) * .7;
        part(r, cylinder_mesh(8), at(x, y, 0) * sc(.12 * s, .12 * s, 1.1 * s), hex(0x8A6440), .015);
        for (int b = 0; b < 3; ++b)
            part(r, sphere_mesh(10, 7), at(x + (b - 1) * .35 * s, y, 1.3 * s + (b == 1 ? .35 * s : 0)) * sc(.6 * s, .55 * s, .5 * s), b == 1 ? hex(0x5AA048) : hex(0x4E9440), .02);
    }
    for (int k = 0; k < 6; ++k) {
        const double side = k % 2 ? 1 : -1;
        const double x = side * (fx + .3 + h01(k + 70) * 1.5), y = -2 + h01(k + 80) * 5;
        part(r, sphere_mesh(10, 6), at(x, y, .15) * sc(.55, .45, .4), hex(0x5EA84E), .02);
    }
    }
    if (animated) {
    const double fy = (n_ * kDY) / 2 + 1.2, fx = (n_ * kDX) / 2 + 1.2;
    // butterflies
    for (int k = 0; k < 4; ++k) {
        const double u = t * (.12 + .03 * k) + k * 1.7;
        const V3 b{std::sin(u) * (fx * .9) , std::sin(u * 1.37 + k) * (fy * .8), .8 + .3 * std::sin(u * 3.1)};
        const double flap = std::fabs(std::sin(t * 14 + k)) * .9;
        const Col wc = k % 2 ? hex(0xF8E060) : hex(0xF8F8F8);
        for (int s = -1; s <= 1; s += 2) quad(r, b, b + V3{s * .12 * std::cos(flap), .05, .12 * std::sin(flap)}, b + V3{s * .1 * std::cos(flap), -.08, .1 * std::sin(flap)}, b + V3{0, -.03, 0}, nullptr, wc, static_cast<std::uint16_t>(unlit | double_sided));
    }
    }
}

void Pasture::draw_patches(const PastureState& s, double t) {
    if (!s.meadow) return;
    const Meadow& m = *s.meadow;
    for (int i = 0; i < m.w * m.h; ++i) {
        const V3 c = cell_pos(i);
        const double shade = .92 + .12 * h01(static_cast<std::uint32_t>(i) * 7 + 3);
        // Over the grown turf the patch's colour is a light: near white, with each patch a touch different.
        Col top = grown_ ? mix(hex(0xE8F0DC), hex(0xFFFFF4), static_cast<float>(shade - .92) * 4)
                         : mix(hex(0x88C868), hex(0xA8DC80), static_cast<float>(shade - .92) * 4);
        if (m.edge(i)) top = mix(top, hex(0xC8D890), .35f);  // the edge: where it would hop off
        bool on_path = false;
        for (int p : s.exit_path) on_path = on_path || p == i;
        if (on_path) top = mix(top, hex(0xF8F0B0), .35f + .1f * static_cast<float>(std::sin(t * 4)));
        if (i == s.hover) top = mix(top, s.hover_ok ? hex(0xFFFFFF) : hex(0xE07060), .3f);
        hex_patch(r, c, kR * .96, .12, top, hex(0x8A6A48), grown_ ? &ground_.turf : &tx().grass);
        const Cell k = m.cells[static_cast<size_t>(i)];
        const double drop = s.drop.size() > static_cast<size_t>(i) ? s.drop[static_cast<size_t>(i)] : 0;
        if (k == Cell::rock || k == Cell::stone) {
            // a fence post; rails run to every neighbouring post, old or new, so walls join up as you build them
            const double z = k == Cell::stone ? (1 - std::min(1.0, (1 - drop) * 1.6)) * 1.2 + (drop > 0 ? std::fabs(std::sin(drop * 9)) * .05 * drop : 0) : 0;
            draw_fence(m, i, c + V3{0, 0, z}, k == Cell::rock, 1.f);
        } else if (k == Cell::clover) {
            // a clump of clover, three leaves to a stem, a pink flower nodding
            for (int q = 0; q < 5; ++q) {
                const double a = q * 1.26 + h01(i) * 3, rr = .16 + .08 * (q % 2);
                const V3 p = c + V3{std::cos(a) * rr, std::sin(a) * rr, .14};
                for (int l = 0; l < 3; ++l) {
                    const double la = l * 2.094 + a;
                    draw_mesh(r, sphere_mesh(6, 4), at(p + V3{std::cos(la) * .08, std::sin(la) * .08, .05}) * sc(.08, .08, .02), nullptr, hex(0x2E8A34), toon);
                }
            }
            const double nod = std::sin(t * 1.5 + i) * .03;
            part(r, cylinder_mesh(5), at(c + V3{0, 0, .14}) * sc(.02, .02, .3), hex(0x3E8A3A), 0);
            part(r, sphere_mesh(8, 6), at(c + V3{nod, 0, .46}) * sc(.11, .11, .12), hex(0xF080B8), .012);
            part(r, sphere_mesh(8, 6), at(c + V3{nod + .1, .05, .38}) * sc(.08, .08, .09), hex(0xF8A8D0), .01);
        }
        if (i == s.hint) {
            const float a = static_cast<float>(.35 + .3 * std::sin(t * 6));
            r.billboard(c + V3{0, 0, .1}, .9, .9, &tx().glow, hex(0xFFF0A0, a), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
        }
        if (i == s.hover && s.hover_ok) {
            // a ghost of the fence you'd put up, already joined to its neighbours
            draw_fence(m, i, c, false, .45f);
        }
    }
}

void Pasture::draw_fence(const Meadow& m, int i, V3 c, bool old, float ghost) {
    const bool solid = ghost >= 1;
    const Col post = old ? hex(0x8C8472) : hex(0xC89E68), rail = old ? hex(0x9A9078) : hex(0xD8B07A);
    const std::uint16_t mat = solid ? static_cast<std::uint16_t>(toon) : static_cast<std::uint16_t>(translucent | unlit);
    auto piece = [&](const M34& model, Col col, double ink) {
        draw_mesh(r, box_mesh(), model, nullptr, solid ? col : hex(0xFFFFFF, ghost), mat);
        if (solid && ink > 0) draw_outline(r, box_mesh(), model, ink, kInk);
    };
    auto rails = [&](V3 a, V3 b) {
        const V3 d = b - a;
        const double len = std::sqrt(d.x * d.x + d.y * d.y), ang = std::atan2(d.y, d.x);
        for (double h : {.28, .5}) piece(at(a + V3{0, 0, h}) * M34::rot_z(ang) * M34::rot_y(1.5708) * sc(.03, .045, len), rail, .012);
    };
    int nb[6];
    const int n = m.neighbours(i, nb);
    int joined = 0;
    for (int j = 0; j < n; ++j) {
        const Cell k = m.cells[static_cast<size_t>(nb[j])];
        if (k != Cell::rock && k != Cell::stone) continue;
        // half a rail each way: the neighbour draws the other half, and they meet at the patch edge
        rails(c, c + (cell_pos(nb[j]) - cell_pos(i)) * .5);
        ++joined;
    }
    if (joined == 0) {
        // on its own: a short panel across the patch, turned one of three ways
        const double ang = (hsh(static_cast<std::uint32_t>(i) * 3 + 1) % 3) * M_PI / 3 + M_PI / 6;
        const V3 u{std::cos(ang) * .3, std::sin(ang) * .3, 0};
        for (int sgn = -1; sgn <= 1; sgn += 2) piece(at(c + u * sgn + V3{0, 0, .12}) * sc(.05, .05, .52), post, .014);
        rails(c - u, c + u);
    } else {
        piece(at(c + V3{0, 0, .12}) * sc(.06, .06, .56), post, .016);
        if (old && solid) part(r, sphere_mesh(8, 5), at(c + V3{.05, -.06, .13}) * sc(.12, .1, .05), hex(0x6A9A4A), .01);  // moss at its foot
    }
}

void Pasture::draw_sheep(const SheepPose& p, double t) {
    if (p.fade <= 0) return;
    const double sit = std::clamp(p.sit, 0.0, 1.0);
    const double lift = std::fabs(std::sin(p.hop * M_PI)) * .45 + p.startle * .15;
    const M34 f = at(p.pos + V3{0, 0, .12 + lift - sit * .16}) * M34::rot_z(p.yaw) * sc(1.3);
    const Col wool = hex(0xF8F6EE), wool2 = hex(0xEAE6DA), face = hex(0x3A322C), pink = hex(0xE8A0A0);
    // legs: dark, scampering when it runs, tucked when it sits
    for (int s = -1; s <= 1; s += 2)
        for (int fb = -1; fb <= 1; fb += 2) {
            const double swing = p.run > 0 ? std::sin(t * 18 + (s * fb > 0 ? 0 : M_PI)) * .5 * p.run : 0;
            const double len = .2 * (1 - sit * .8);
            part(r, cylinder_mesh(6), f * at(s * .12, fb * .16, .02 + (1 - sit) * .0) * M34::rot_x(swing) * sc(.04, .04, len + .02), face, .01);
        }
    // the woolly body: a cloud of puffs
    const M34 body = f * at(0, 0, .38 - sit * .12);
    for (int k = 0; k < 11; ++k) {
        const double a = k * 2.4, z = (k % 3) * .07;
        const V3 o{std::cos(a) * .17, std::sin(a) * .22, z};
        part(r, sphere_mesh(10, 7), body * at(o) * sc(.15 + .02 * (k % 2)), k % 2 ? wool : wool2, .016);
    }
    part(r, sphere_mesh(12, 8), body * at(0, 0, .06) * sc(.26, .32, .2), wool, .018);
    // a stubby tail
    part(r, sphere_mesh(8, 6), body * at(0, .32, .06) * sc(.07), wool2, .012);
    // the head: dark face, ears, eyes, a mouth that chews
    const M34 h = body * at(0, -.32, .12 - p.head_down * .22) * M34::rot_z(p.head_turn) * M34::rot_x(p.head_down * .7 - .1);
    part(r, sphere_mesh(12, 8), h * sc(.12, .15, .12), face, .016);
    part(r, sphere_mesh(10, 7), h * at(0, .03, .1) * sc(.13, .1, .07), wool, .012);  // the woolly cap
    for (int s = -1; s <= 1; s += 2) {
        const double flick = (s > 0 ? p.ear : 0) * std::sin(t * 30) * .4;
        part(r, sphere_mesh(8, 5), h * at(s * .13, .02, .04) * M34::rot_y(s * (1.1 + flick)) * sc(.03, .06, .1), face, .008);
        draw_mesh(r, sphere_mesh(6, 4), h * at(s * .135, .0, .04) * M34::rot_y(s * (1.1 + flick)) * sc(.015, .04, .07), nullptr, pink, unlit);
        // eyes: white with a dark pupil; closed when blinking or asleep; heavy-lidded when sulking
        const double closed = std::max({p.blink, p.sleep, sit * .5});
        const M34 e = h * at(s * .07, -.1, .04);
        flat(r, sphere_mesh(8, 6), e * sc(.035, .02, .035 * (1 - closed * .85)), hex(0xFFFFFF));
        if (closed < .7) flat(r, sphere_mesh(6, 4), e * at(0, -.012, -.004) * sc(.018, .012, .02 * (1 - closed)), hex(0x101010));
        if (sit > .5 && p.sleep < .5) part(r, box_mesh(), e * at(0, -.01, .03) * M34::rot_y(s * -.4) * sc(.035, .006, .006), hex(0x1A1410), 0);  // a sulky brow
    }
    flat(r, sphere_mesh(6, 4), h * at(0, -.15, -.06) * sc(.03, .01, .006 + .012 * p.chew), hex(0x1A1410));
}

void Pasture::take_ground() {
    if (grown_)
        return;
    if (!growing_.valid()) {
        growing_ = std::async(std::launch::async, grow_ground);
        return;
    }
    if (growing_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    GroundArt art = growing_.get();
    if (art.turf.px.empty() || art.field.px.empty())
        return;
    ground_ = std::move(art);
    grown_ = true;
    land_valid_ = false;
}

void Pasture::render(const PastureState& s, double t) {
    take_ground();
    r.time = t;
    r.clear_depth();
    r.tris_drawn = 0;
    if (!land_valid_ || land_rgb_.size() != r.rgb.size()) {
        draw_land(t, false);
        land_rgb_ = r.rgb;
        land_depth_ = r.depth;
        land_valid_ = true;
    } else {
        r.rgb = land_rgb_;
        r.depth = land_depth_;
    }
    draw_land(t, true);
    draw_patches(s, t);
    draw_sheep(s.sheep, t);
    // stars of celebration rising round the penned sheep
    if (s.celebrate > 0) {
        for (int k = 0; k < 14; ++k) {
            const double u = std::fmod(t * .5 + h01(k + 3) , 1.0);
            const double a = k * .45 + t * .3;
            const V3 p = s.sheep.pos + V3{std::cos(a) * (.4 + u * .6), std::sin(a) * (.4 + u * .6), .3 + u * 1.4};
            r.billboard(p, .16, .16, &tx().glow, hex(k % 2 ? 0xFFE070 : 0xFFFFFF, static_cast<float>(s.celebrate * (1 - u))), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
        }
    }
}

}  // namespace sh
