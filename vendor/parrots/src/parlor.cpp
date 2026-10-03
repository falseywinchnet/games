#include "parlor.hpp"

#include "platform/mesh.hpp"
#include "platform/raster.hpp"

#include <algorithm>
#include <cmath>

namespace pt {

namespace {
const Col kWhite{1, 1, 1, 1};
const Col kInk = hex(0x1A1418);
constexpr double kTopZ = .75, kPerchZ = .66, kBirdY = .82;

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
std::uint32_t hsh(std::uint32_t x) { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x; }
double h01(std::uint32_t x) { return (hsh(x) & 0xFFFF) / 65535.0; }

struct Textures {
    Tex paper, cloth, wood, portrait, window, glow, crumbs;
    Textures() {
        Canvas c;
        // striped wallpaper with a little fleur
        c.resize(64, 64);
        c.clear(hex(0x2E5A4A));
        for (int x = 0; x < 64; x += 16) c.fill_rect(x, 0, 6, 64, hex(0x3A6E5A));
        for (int y = 8; y < 64; y += 32) for (int x = 11; x < 64; x += 32) { c.fill_circle(x, y, 2.5, hex(0xC8A858)); c.fill_circle(x, y + 16, 1.5, hex(0xA88A48)); }
        paper = tex_from(c);
        // the tablecloth: white damask with a lace hem along the front
        c.clear(hex(0xF4F0E6));
        for (int y = 0; y < 64; y += 16) for (int x = (y / 16 % 2) * 8; x < 64; x += 16) { c.begin(); c.move(x + 8, y + 2); c.line(x + 14, y + 8); c.line(x + 8, y + 14); c.line(x + 2, y + 8); c.close(); c.stroke(hex(0xE4DED0), 1.2); }
        cloth = tex_from(c);
        // dark polished wood
        c.clear(hex(0x5A3220));
        for (int k = 0; k < 14; ++k) { c.begin(); c.move(0, k * 4.6 + h01(k) * 3); c.quad(32, k * 4.6 + h01(k + 20) * 6, 64, k * 4.6 + h01(k + 40) * 3); c.stroke(hex(0x6E4028), 1); }
        wood = tex_from(c);
        // a portrait of a very grand parrot, in a gilt frame
        c.resize(64, 64);
        c.clear(hex(0xC8982A));
        c.fill_rect(5, 5, 54, 54, hex(0x2A1A10));
        c.begin(); c.rect(6, 6, 52, 52); c.fill(Paint::rad(32, 30, 34, {{0, hex(0x5A4030)}, {1, hex(0x1A100A)}}));
        c.fill_ellipse(32, 44, 14, 16, hex(0xC82828));
        c.fill_circle(32, 24, 10, hex(0xD83030));
        c.fill_ellipse(28, 23, 4, 4, hex(0xF0EAE0));
        c.fill_circle(28, 23, 1.5, hex(0x101010));
        c.begin(); c.move(38, 22); c.quad(46, 24, 42, 32); c.quad(39, 28, 38, 22); c.fill(hex(0xE8E0D0));
        for (int k = 0; k < 5; ++k) c.fill_circle(16 + k * 8, 60 - 3, 2, hex(0xF0D060));  // a ruff of gilt
        c.begin(); c.move(22, 14); c.line(26, 8); c.line(32, 12); c.line(38, 8); c.line(42, 14); c.close(); c.fill(hex(0xF0D040));  // a little crown
        portrait = tex_from(c);
        // the window: a deep evening sky with stars and a crescent
        c.clear(hex(0x1A2A4A));
        c.begin(); c.rect(0, 0, 64, 64); c.fill(Paint::lin(0, 0, 0, 64, {{0, hex(0x1A2A5A)}, {1, hex(0x6A4A7A)}}));
        for (int k = 0; k < 18; ++k) c.fill_rect(h01(k + 300) * 64, h01(k + 400) * 40, 1, 1, hex(0xFFFFFF));
        c.fill_circle(46, 16, 7, hex(0xF8F0C0));
        c.fill_circle(49, 14, 6, hex(0x1E2E5C));
        c.fill_rect(31, 0, 3, 64, hex(0xE8E0D0));
        c.fill_rect(0, 31, 64, 3, hex(0xE8E0D0));
        window = tex_from(c);
        c.resize(32, 32);
        c.clear({0, 0, 0, 0});
        c.begin(); c.circle(16, 16, 16); c.fill(Paint::rad(16, 16, 16, {{0, {1, 1, 1, 1}}, {.35f, {1, 1, 1, .5f}}, {1, {1, 1, 1, 0}}}));
        glow = tex_from(c);
        c.clear({0, 0, 0, 0});
        for (int k = 0; k < 30; ++k) c.fill_circle(4 + h01(k + 600) * 24, 4 + h01(k + 700) * 24, .8 + h01(k + 800) * 1.5, hex(0xD8A860));
        crumbs = tex_from(c);
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
void part(R3D& r, const Mesh& m, const M34& model, Col c, double ink = .014) {
    draw_mesh(r, m, model, nullptr, c, toon);
    if (ink > 0) draw_outline(r, m, model, ink, kInk);
}
}  // namespace

void Parlor::resize(int w, int h, int bottom_panel, int top_band) {
    r.resize(w, h);
    panel_ = bottom_panel;
    w_ = w;
    h_ = h;
    r.yaw = 0;
    r.pitch = .24;
    r.persp = 6.5;
    const double room = std::max(20, h - bottom_panel - top_band);
    r.scale = std::min(w / 5.3, room / 1.75);
    r.ax = .5;
    r.ay = (top_band + room * .6) / h;
    r.target = {0, .75, 1.02};
    r.light.sun = norm({-.4, -.6, .8});
    r.light.sun_col = {.5f, .46f, .4f, 1};
    r.light.amb_col = {.62f, .58f, .56f, 1};
    r.light.fog_near = 1e9;
    r.light.fog_far = 2e9;
    r.light.toon_edge = .1f;
    r.light.toon_soft = .1f;
    r.set_camera();
}

void Parlor::seat(int n, ParlorState& s) {
    s.birds.resize(static_cast<size_t>(n));
    const double gap = std::min(1.1, 6.9 / n);
    // frame the whole row, with a little room either side
    r.scale = std::min(w_ / ((n - 1) * gap + 1.6), (h_ - panel_) / 1.75);
    r.set_camera();
    for (int i = 0; i < n; ++i) {
        BirdPose& b = s.birds[static_cast<size_t>(i)];
        b.pos = {(i - (n - 1) / 2.0) * gap, kBirdY, kPerchZ};
        b.yaw = -(i - (n - 1) / 2.0) * .06;  // the row turns gently in, toward the middle of the table
    }
}

void Parlor::to_screen(V3 p, double& sx, double& sy) const {
    double sz;
    r.project(p, sx, sy, sz);
}

int Parlor::pick_bird(double sx, double sy, const ParlorState& s) const {
    int best = -1;
    double bd = r.scale * .42;
    for (size_t i = 0; i < s.birds.size(); ++i) {
        for (V3 q : {bird_beak(s.birds[i]), bird_head(s.birds[i]), s.birds[i].pos + V3{0, 0, .35}}) {
            double x, y;
            to_screen(q, x, y);
            const double d = std::hypot(sx - x, sy - y);
            if (d < bd) { bd = d; best = static_cast<int>(i); }
        }
    }
    return best;
}

void Parlor::draw_room(const ParlorState& s, double t) {
    const float lamp = static_cast<float>(s.lamp);
    for (int y = 0; y < r.H; ++y) {
        float* o = r.rgb.data() + static_cast<size_t>(y) * r.W * 3;
        const Col c = mix(hex(0x1A1418), hex(0x2A2026), static_cast<float>(y) / r.H);
        for (int x = 0; x < r.W; ++x, o += 3) { o[0] = c.r; o[1] = c.g; o[2] = c.b; }
    }
    // the back wall: wallpaper above a wooden dado
    const double wy = 2.4;
    for (int k = -5; k < 5; ++k) {
        quad(r, {k * 1.0, wy, 1.0}, {k * 1.0 + 1, wy, 1.0}, {k * 1.0 + 1, wy, 3.4}, {k * 1.0, wy, 3.4}, &tx().paper, mix(hex(0x6A6A6A), kWhite, .4f * lamp), unlit, 1, 2.4);
        quad(r, {k * 1.0, wy, -.2}, {k * 1.0 + 1, wy, -.2}, {k * 1.0 + 1, wy, 1.0}, {k * 1.0, wy, 1.0}, &tx().wood, mix(hex(0x6A6A6A), kWhite, .4f * lamp), unlit, 1, 1.2);
    }
    quad(r, {-5, wy - .02, .98}, {5, wy - .02, .98}, {5, wy - .02, 1.04}, {-5, wy - .02, 1.04}, nullptr, hex(0xC8A050), unlit);
    // a window, and a grand portrait
    quad(r, {1.6, wy - .03, 1.5}, {2.8, wy - .03, 1.5}, {2.8, wy - .03, 2.9}, {1.6, wy - .03, 2.9}, &tx().window, kWhite, unlit);
    for (V3 a : {V3{1.55, wy - .04, 1.45}}) {
        quad(r, a, a + V3{1.3, 0, 0}, a + V3{1.3, 0, .06}, a + V3{0, 0, .06}, nullptr, hex(0xE8E0D0), unlit);
        quad(r, a + V3{0, 0, 1.44}, a + V3{1.3, 0, 1.44}, a + V3{1.3, 0, 1.5}, a + V3{0, 0, 1.5}, nullptr, hex(0xE8E0D0), unlit);
    }
    quad(r, {-2.6, wy - .03, 1.4}, {-1.4, wy - .03, 1.4}, {-1.4, wy - .03, 2.7}, {-2.6, wy - .03, 2.7}, &tx().portrait, kWhite, unlit);
    // a pool of lamplight on the wall behind the table
    r.billboard({0, wy - .1, .6}, 7, 2.8, &tx().glow, hex(0xFFD890, .18f * lamp), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
    // the floor
    quad(r, {-6, -3, -.01}, {6, -3, -.01}, {6, wy, -.01}, {-6, wy, -.01}, &tx().wood, hex(0x8A7A70), unlit, 6, 3);
    static_cast<void>(t);
}

void Parlor::draw_table(const ParlorState& s, double t) {
    const double X = 3.7, Y0 = -.55, Y1 = .55, Z = kTopZ;
    // the cloth over the top, hanging down the front
    quad(r, {-X, Y0, Z}, {X, Y0, Z}, {X, Y1, Z}, {-X, Y1, Z}, &tx().cloth, kWhite, toon, 7, 1);
    quad(r, {-X, Y0, .28}, {X, Y0, .28}, {X, Y0, Z}, {-X, Y0, Z}, &tx().cloth, hex(0xF0ECE2), toon, 7, .5);
    for (int k = 0; k < 37; ++k) {  // a scalloped lace hem
        const double x = -X + k * (2 * X / 36);
        draw_mesh(r, disc_mesh(10), at({x, Y0 - .005, .28}) * M34::rot_x(1.57) * sc(.1, .07, 1), nullptr, hex(0xF8F4EC), unlit);
    }
    // perches behind the table, one for each bird
    for (const BirdPose& b : s.birds) {
        part(r, cylinder_mesh(8), at({b.pos.x, b.pos.y + .1, -.01}) * sc(.03, .03, kPerchZ - .02), hex(0x6A4428));
        part(r, cylinder_mesh(8), at({b.pos.x - .2, b.pos.y - .02, kPerchZ - .03}) * M34::rot_y(1.5708) * sc(.025, .025, .4), hex(0x7A5030));
    }
    // a place setting for each: saucer and cup
    for (const BirdPose& b : s.birds) {
        const V3 c{b.pos.x, -.2, Z + .005};
        draw_mesh(r, disc_mesh(14), at(c) * sc(.17, .12, 1), nullptr, hex(0xF8F8F8), toon);
        draw_outline(r, disc_mesh(14), at(c) * sc(.17, .12, 1), .01, hex(0x8A8A9A));
        part(r, cylinder_mesh(10), at(c + V3{.03, 0, 0}) * sc(.06, .06, .07), hex(0xF8F8F8), .01);
        draw_mesh(r, disc_mesh(10), at(c + V3{.03, 0, .072}) * sc(.05, .05, 1), nullptr, hex(0x8A5A30), unlit);
    }
    // the evidence, in the middle: between the two middle birds, never under one
    const size_t nb = s.birds.size();
    const double mx = nb % 2 == 1 && nb > 1 ? (s.birds[nb / 2].pos.x + s.birds[nb / 2 + 1].pos.x) / 2 : 0;
    const V3 m{mx, -.05, Z + .005};
    if (s.prop == "teapot") {
        // toppled on its side, a puddle of tea
        draw_mesh(r, disc_mesh(16), at(m + V3{.25, -.1, .002}) * sc(.4, .2, 1), nullptr, hex(0xA8703A, .9f), static_cast<std::uint16_t>(translucent | unlit));
        part(r, sphere_mesh(12, 8), at(m + V3{0, 0, .12}) * M34::rot_y(1.4) * sc(.16, .15, .14), hex(0xF0F0F8));
        part(r, cone_mesh(8), at(m + V3{.18, 0, .1}) * M34::rot_y(1.75) * sc(.03, .03, .16), hex(0xF0F0F8));
        part(r, torus_mesh(10, 4, .3), at(m + V3{-.15, 0, .14}) * M34::rot_x(1.57) * sc(.06, .06, .06), hex(0xF0F0F8));
    } else if (s.prop == "cake") {
        part(r, cylinder_mesh(16), at(m) * sc(.26, .26, .16), hex(0xF8E0E8));
        draw_mesh(r, disc_mesh(16), at(m + V3{0, 0, .16}) * sc(.26, .26, 1), nullptr, hex(0xF8F0F4), toon);
        draw_mesh(r, disc_mesh(10), at(m + V3{.08, -.06, .162}) * sc(.07, .05, 1), nullptr, hex(0x6A3A20), unlit);  // the pecked hole
        for (int k = 0; k < 5; ++k) part(r, cylinder_mesh(6), at(m + V3{std::cos(k * 1.26) * .15, std::sin(k * 1.26) * .12, .16}) * sc(.012, .012, .08), hex(0x60A0F0), .006);
    } else if (s.prop == "jar") {
        part(r, cylinder_mesh(12), at(m) * sc(.11, .11, .2), hex(0xD8E8F0, .8f));
        part(r, cylinder_mesh(12), at(m + V3{.2, .05, 0}) * M34::rot_y(1.4) * sc(.12, .12, .03), hex(0xD8B048));  // the lid, cast aside
    } else if (s.prop == "button") {
        part(r, cylinder_mesh(14), at(m) * sc(.14, .14, .03), hex(0x6A2A5A));
        draw_mesh(r, disc_mesh(14), at(m + V3{0, 0, .031}) * sc(.11, .11, 1), nullptr, hex(0x4A1A3A), unlit);
        draw_mesh(r, torus_mesh(12, 4, .25), at(m + V3{0, 0, .033}) * sc(.05, .05, .05), nullptr, hex(0x8A6A80), unlit);  // the empty ring where it sat
    } else if (s.prop == "mirror") {
        part(r, box_mesh(), at(m) * sc(.2, .03, .01), hex(0xC8A048));
        draw_mesh(r, disc_mesh(14), at(m + V3{0, 0, .012}) * sc(.16, .1, 1), nullptr, hex(0xE8E4D8), unlit);  // a pale patch where it stood
    } else {
        // the plate: empty but for crumbs
        draw_mesh(r, disc_mesh(16), at(m) * sc(.28, .2, 1), nullptr, hex(0xF8F8F8), toon);
        draw_outline(r, disc_mesh(16), at(m) * sc(.28, .2, 1), .012, hex(0x8A8A9A));
        r.billboard(m + V3{0, 0, .002}, .3, .05, &tx().crumbs, kWhite, static_cast<std::uint16_t>(cutout | unlit));
        for (int k = 0; k < 8; ++k) draw_mesh(r, sphere_mesh(4, 3), at(m + V3{(h01(k + 900) - .5) * .4, (h01(k + 950) - .5) * .25, .01}) * sc(.015, .015, .008), nullptr, hex(0xC8904A), unlit);
    }
    static_cast<void>(t);
}

void Parlor::render(const ParlorState& s, double t) {
    r.time = t;
    r.clear_depth();
    r.tris_drawn = 0;
    draw_room(s, t);
    draw_table(s, t);
    for (size_t i = 0; i < s.birds.size(); ++i) {
        draw_bird(r, s.birds[i], t);
        if (s.spot >= 0 && static_cast<size_t>(s.spot) == i)
            r.billboard(s.birds[i].pos + V3{0, -.05, -.1}, 1.4, 1.6, &tx().glow, hex(0xFFF0B0, .35f), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
    }
    if (s.confetti > 0) {
        for (int k = 0; k < 40; ++k) {
            const double u = std::fmod(t * .4 + h01(k) , 1.0);
            const V3 p{(h01(k + 50) - .5) * 7, .3 + h01(k + 90) * .6, 3 - u * 3};
            const Col c = k % 4 == 0 ? hex(0xF04060) : k % 4 == 1 ? hex(0xF0D040) : k % 4 == 2 ? hex(0x40C0F0) : hex(0x60E080);
            r.billboard(p, .06, .06, nullptr, alpha(c, static_cast<float>(s.confetti)), static_cast<std::uint16_t>(unlit));
        }
    }
}

}  // namespace pt
