#include "garden.hpp"

#include "platform/render.hpp"

#include <algorithm>
#include <cmath>

namespace ct {

namespace {

M34 at(V3 v) { return M34::translate(v.x, v.y, v.z); }
M34 sc(double x, double y, double z) { return M34::scale(x, y, z); }

struct Palette {
    Col lawn, lawn2, soil, soil2, hedge, hedge_dark, hedge_ink, pumpkin, pumpkin_ink, stem, burrow, mound, bg;
    Col sun, amb;
};
Palette palette(Season s) {
    switch (s) {
        case Season::spring: return {hex(0x8ED05C), hex(0x7CBE4E), hex(0xC0905E), hex(0xA87A4C), hex(0x58B04E), hex(0x3A8A3C), hex(0x1E4A22), hex(0xF59A2E), hex(0x8A4210), hex(0x6A8A2A), hex(0x2A1A10), hex(0x9A6A3E), hex(0x8ED05C), {.55f, .53f, .48f, 1}, {.6f, .6f, .62f, 1}};
        case Season::summer: return {hex(0x7CC046), hex(0x6AAE3C), hex(0xB8844E), hex(0x9E6E40), hex(0x3E9A3E), hex(0x2A7430), hex(0x163E1A), hex(0xF58E24), hex(0x8A3C0E), hex(0x5A7A22), hex(0x281810), hex(0x946238), hex(0x7CC046), {.62f, .58f, .48f, 1}, {.56f, .56f, .58f, 1}};
        case Season::autumn: return {hex(0xC2B054), hex(0xAE9C46), hex(0xA8764C), hex(0x8E623E), hex(0xD07A34), hex(0xA2502A), hex(0x4E2410), hex(0xF08A22), hex(0x7A3A0C), hex(0x6A5A22), hex(0x241610), hex(0x8A5A34), hex(0xC2B054), {.6f, .52f, .42f, 1}, {.58f, .54f, .54f, 1}};
        case Season::winter: return {hex(0xEEF4FA), hex(0xDCE6F0), hex(0xB4A49C), hex(0xA09088), hex(0x3E7A5A), hex(0x2A5A42), hex(0x14301E), hex(0xF0902A), hex(0x7A3A0C), hex(0x5A6A3A), hex(0x2A2026), hex(0x9C8C8C), hex(0xEEF4FA), {.5f, .52f, .56f, 1}, {.62f, .64f, .7f, 1}};
        case Season::night: return {hex(0x6A7EA8), hex(0x5E7098), hex(0x6A5E70), hex(0x5E5266), hex(0x2A5048), hex(0x1C3A36), hex(0x0A1418), hex(0xD8782A), hex(0x5A2A0C), hex(0x3A4A3A), hex(0x120E18), hex(0x5A5068), hex(0x34405E), {.32f, .36f, .5f, 1}, {.36f, .4f, .56f, 1}};
    }
    return palette(Season::spring);
}

unsigned unpremultiply(unsigned v, unsigned a) { return a ? std::min(255u, v * 255 / a) : 0u; }

Tex tex_from(const Canvas& c) {
    Tex t;
    t.make(c.w, c.h);
    for (size_t i = 0; i < t.px.size(); ++i) {
        const std::uint8_t* p = &c.px[i * 4];
        const unsigned a = p[3];
        t.px[i] = a << 24 | unpremultiply(p[2], a) << 16 | unpremultiply(p[1], a) << 8 | unpremultiply(p[0], a);
    }
    t.build_mips();
    return t;
}

std::uint32_t hsh(std::uint32_t x) {
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return x;
}
double h01(std::uint32_t x) { return (hsh(x) & 0xFFFF) / 65535.0; }

struct Textures {
    Tex grass, leaves, glow, puff, petal, leaf, flake, ring, chevron, cross;
    Textures() {
        Canvas c;
        // grass: white-based so the season tints it; tufts of lighter and darker strokes
        c.resize(64, 64);
        c.clear(hex(0xF0F0F0));
        for (int i = 0; i < 160; ++i) {
            const double x = h01(i * 3 + 1) * 64, y = h01(i * 3 + 2) * 64, k = h01(i * 3 + 3);
            c.fill_rect(x, y, 1, 2, k > .55 ? hex(0xFFFFFF) : k < .3 ? hex(0xD6D6D6) : hex(0xE6E6E6));
        }
        grass = tex_from(c);
        // hedge leaves: clusters of light and dark leaf blobs
        c.clear(hex(0xE0E0E0));
        for (int i = 0; i < 120; ++i) {
            const double x = h01(i + 1000) * 64, y = h01(i + 1200) * 64, k = h01(i + 1400);
            c.fill_ellipse(x, y, 2.5 + k * 2, 1.8 + k, k > .55 ? hex(0xFFFFFF) : k < .25 ? hex(0xA8A8A8) : hex(0xD0D0D0));
        }
        leaves = tex_from(c);
        c.resize(32, 32);
        c.clear({0, 0, 0, 0});
        c.begin(); c.circle(16, 16, 16); c.fill(Paint::rad(16, 16, 16, {{0, {1, 1, 1, 1}}, {.35f, {1, 1, 1, .5f}}, {1, {1, 1, 1, 0}}}));
        glow = tex_from(c);
        c.clear({0, 0, 0, 0});
        for (int i = 0; i < 5; ++i) c.fill_circle(10 + h01(i + 40) * 12, 10 + h01(i + 60) * 12, 6 + h01(i + 80) * 4, {1, 1, 1, .9f});
        puff = tex_from(c);
        c.clear({0, 0, 0, 0});
        c.fill_ellipse(16, 16, 9, 6, {1, 1, 1, 1});
        petal = tex_from(c);
        c.clear({0, 0, 0, 0});
        c.begin(); c.move(16, 3); c.quad(29, 14, 16, 29); c.quad(3, 14, 16, 3); c.fill({1, 1, 1, 1});
        c.stroke_line(16, 6, 16, 27, {.75f, .75f, .75f, 1}, 1.2);
        leaf = tex_from(c);
        c.clear({0, 0, 0, 0});
        c.fill_circle(16, 16, 6, {1, 1, 1, 1});
        flake = tex_from(c);
        c.clear({0, 0, 0, 0});
        c.begin(); c.circle(16, 16, 12); c.stroke({1, 1, 1, 1}, 2.5);
        ring = tex_from(c);
        c.clear({0, 0, 0, 0});
        c.begin(); c.move(6, 22); c.line(16, 10); c.line(26, 22); c.line(21, 22); c.line(16, 16); c.line(11, 22); c.close(); c.fill({1, 1, 1, 1});
        chevron = tex_from(c);
        c.clear({0, 0, 0, 0});
        c.stroke_line(8, 8, 24, 24, {1, 1, 1, 1}, 4);
        c.stroke_line(24, 8, 8, 24, {1, 1, 1, 1}, 4);
        cross = tex_from(c);
    }
};
const Textures& tx() {
    static Textures t;
    return t;
}

// a ribbed pumpkin: a squashed sphere with eight lobes
const Mesh& pumpkin_mesh() {
    static Mesh m;
    if (!m.empty()) return m;
    m = sphere_mesh(24, 14);
    for (Vtx& v : m) {
        const double th = std::atan2(v.p.y, v.p.x);
        const double rib = 1 - .09 * (1 - std::cos(8 * th)) * .5 * (1 - std::fabs(v.p.z));
        v.p = {v.p.x * rib, v.p.y * rib, v.p.z};
        // pull the top and bottom in, the way a pumpkin dimples at the stem
        if (std::fabs(v.p.z) > .85) v.p.z *= .92;
    }
    return m;
}

void quad(R3D& r, V3 a, V3 b, V3 c, V3 d, const Tex* tex, Col col, std::uint16_t mat, double s0 = 0, double t0 = 0, double s1 = 1, double t1 = 1) {
    const V3 n = norm(cross(b - a, d - a));
    Vtx v[6] = {{a, n, s0, t1, col}, {b, n, s1, t1, col}, {c, n, s1, t0, col}, {a, n, s0, t1, col}, {c, n, s1, t0, col}, {d, n, s0, t0, col}};
    r.draw(v, 6, tex, mat);
}
void decal(R3D& r, V3 p, double w, double h, const Tex* tex, Col col, std::uint16_t mat) {
    quad(r, p + V3{-w / 2, -h / 2, 0}, p + V3{w / 2, -h / 2, 0}, p + V3{w / 2, h / 2, 0}, p + V3{-w / 2, h / 2, 0}, tex, col, mat);
}
constexpr std::uint16_t kGlow = additive | unlit | no_depth_write | double_sided;
constexpr std::uint16_t kSoft = translucent | unlit | no_depth_write | double_sided;
}  // namespace

Season season_for(const std::string& section_in) {
    std::string section = section_in;
    if (section.rfind("Endless ", 0) == 0) section = section.substr(8);  // "Endless Autumn" is autumn
    if (section == "Night") return Season::night;
    if (section.rfind("Summer", 0) == 0) return Season::summer;
    if (section.rfind("Autumn", 0) == 0) return Season::autumn;
    if (section == "Winter Night") return Season::night;
    if (section.rfind("Winter", 0) == 0) return Season::winter;
    return Season::spring;
}

void Garden::resize(int w, int h, int hud_w, int top) {
    r.resize(w, h);
    hud_w_ = hud_w;
    top_ = top;
    r.yaw = 0;
    r.pitch = .82;
    r.persp = 24;
    r.light.sun = norm({-.45, -.6, .8});
    r.light.fog_near = 1e9;
    r.light.fog_far = 2e9;
    r.light.toon_edge = .12f;
    r.light.toon_soft = .1f;
    fit_camera();
}

void Garden::set_level(const Level& lv) {
    lv_ = lv;
    // decorations on the lawn just outside the hedges
    deco_.assign(static_cast<size_t>(lv_.w * lv_.h), 0);
    for (int i = 0; i < lv_.w * lv_.h; ++i) {
        if (lv_.tiles[static_cast<size_t>(i)] != Tile::outside) continue;
        bool near = false;
        for (int d = 0; d < 4; ++d) near = near || (lv_.step(i, d) >= 0 && lv_.tiles[static_cast<size_t>(lv_.step(i, d))] == Tile::wall);
        if (near && h01(static_cast<std::uint32_t>(i) * 31 + 7) < .45) deco_[static_cast<size_t>(i)] = 1 + static_cast<int>(h01(static_cast<std::uint32_t>(i) * 17 + 3) * 3);
    }
    fit_camera();
}

V3 Garden::cell_pos(double x, double y) const { return {x - (lv_.w - 1) / 2.0, (lv_.h - 1) / 2.0 - y, 0}; }
V3 Garden::cell_pos(int cell) const { return cell_pos(cell % std::max(1, lv_.w), cell / std::max(1, lv_.w)); }

void Garden::fit_camera() {
    if (r.W <= 0 || lv_.w <= 0) return;
    const double avail_w = r.W - hud_w_ - 16, avail_h = r.H - 34 - top_;
    const double gw = lv_.w + .4, gh = lv_.h * std::sin(r.pitch) + 1.4;
    r.scale = std::min(avail_w / gw, avail_h / gh);
    r.ax = (r.W - hud_w_) / 2.0 / r.W;
    r.ay = (top_ + .54 * (r.H - top_)) / r.H;
    r.target = {0, .15, .3};
    r.set_camera();
}

void Garden::to_screen(V3 p, double& sx, double& sy) const {
    double sz;
    r.project(p, sx, sy, sz);
}

int Garden::pick_cell(double sx, double sy) const {
    double wx, wy;
    if (!r.unproject_plane(sx, sy, 0, wx, wy)) return -1;
    const int x = static_cast<int>(std::floor(wx + (lv_.w - 1) / 2.0 + .5)), y = static_cast<int>(std::floor((lv_.h - 1) / 2.0 - wy + .5));
    if (!lv_.in(x, y)) return -1;
    return lv_.idx(x, y);
}

void Garden::sprite(V3 c, double size, const Tex* tex, Col col, std::uint16_t mat) {
    const V3 rx = r.right() * (size * .5), uy = r.up() * (size * .5);
    const V3 n = r.fwd() * -1.0;
    Vtx q[6] = {{c - rx - uy, n, 0, 1, col}, {c + rx - uy, n, 1, 1, col}, {c + rx + uy, n, 1, 0, col},
                {c - rx - uy, n, 0, 1, col}, {c + rx + uy, n, 1, 0, col}, {c - rx + uy, n, 0, 0, col}};
    r.draw(q, 6, tex, static_cast<std::uint16_t>(mat | double_sided));
}

// ------------------------------------------------------------------ ground
void Garden::draw_ground(const GardenState& s, double t) {
    const Palette P = palette(s.season);
    for (int y = 0; y < r.H; ++y) {
        float* o = r.rgb.data() + static_cast<size_t>(y) * r.W * 3;
        for (int x = 0; x < r.W; ++x, o += 3) { o[0] = P.bg.r; o[1] = P.bg.g; o[2] = P.bg.b; }
    }
    // the field (field.cpp); until it has grown, a plain lawn, wide enough to fill the view
    // in small tiles: textures here are mapped without perspective correction, so one huge quad would warp.
    // It leaves the depth alone: the burrows' pits go down below it, and everything else stands on top.
    const int L = draw_field(s) ? 0 : 14;
    for (int gy = -L; gy < L; gy += 2)
        for (int gx = -L; gx < L; gx += 2)
            quad(r, {double(gx), double(gy), -.01}, {double(gx + 2), double(gy), -.01}, {double(gx + 2), double(gy + 2), -.01}, {double(gx), double(gy + 2), -.01}, &tx().grass, P.lawn, static_cast<std::uint16_t>(unlit | no_depth_write), 0, 0, 1.8, 1.8);
    // the soil beds (burrow squares with their holes cut) and the spoil thrown out of the burrows
    ground_.draw_beds(r, lv_, static_cast<std::uint32_t>(lv_.player * 131 + lv_.w * 17 + lv_.h));
    ground_.draw_spoil(r, lv_);
    // little flowers and things on the lawn outside the hedges
    for (int i = 0; i < lv_.w * lv_.h; ++i) {
        const int k = deco_.empty() ? 0 : deco_[static_cast<size_t>(i)];
        if (!k) continue;
        const V3 c = cell_pos(i) + V3{(h01(i * 7 + 1) - .5) * .5, (h01(i * 7 + 2) - .5) * .5, 0};
        Col petal = hex(0xF8F0FF);
        switch (s.season) {
            case Season::spring: petal = k == 1 ? hex(0xFFB0D0) : k == 2 ? hex(0xFFF2A0) : hex(0xC8B0FF); break;
            case Season::summer: petal = k == 1 ? hex(0xFFD23A) : k == 2 ? hex(0xFF6A5A) : hex(0xFFFFFF); break;
            case Season::autumn: petal = k == 1 ? hex(0xD8502A) : k == 2 ? hex(0xE8A030) : hex(0x9A5A2A); break;
            case Season::winter: petal = hex(0xFFFFFF); break;
            case Season::night: petal = hex(0xFFE8A0); break;
        }
        if (s.season == Season::winter && k == 1) {
            // a tiny snowman
            draw_mesh(r, sphere_mesh(10, 7), at(c + V3{0, 0, .11}) * sc(.12, .12, .11), nullptr, hex(0xFFFFFF), toon);
            draw_mesh(r, sphere_mesh(10, 7), at(c + V3{0, 0, .27}) * sc(.08, .08, .08), nullptr, hex(0xFFFFFF), toon);
            draw_outline(r, sphere_mesh(10, 7), at(c + V3{0, 0, .11}) * sc(.12, .12, .11), .015, hex(0x5A6A7A));
            draw_outline(r, sphere_mesh(10, 7), at(c + V3{0, 0, .27}) * sc(.08, .08, .08), .015, hex(0x5A6A7A));
            continue;
        }
        if (s.season == Season::night && k == 1) {
            // a lantern on a post
            draw_mesh(r, cylinder_mesh(6), at(c) * sc(.025, .025, .45), nullptr, hex(0x3A2A20), toon);
            draw_mesh(r, box_mesh(), at(c + V3{0, 0, .45}) * sc(.07, .07, .12), nullptr, hex(0xFFD070), unlit);
            sprite(c + V3{0, 0, .5}, 1.4 + .1 * std::sin(t * 3 + i), &tx().glow, hex(0xFFC060, .45f), kGlow);
            decal(r, c + V3{0, 0, .005}, 1.6, 1.6, &tx().glow, hex(0xFFB050, .25f), kGlow);
            continue;
        }
        for (int f = 0; f < 3; ++f) {
            const V3 p = c + V3{(h01(i * 13 + f) - .5) * .4, (h01(i * 13 + f + 5) - .5) * .4, 0};
            draw_mesh(r, cylinder_mesh(5), at(p) * sc(.012, .012, .12), nullptr, hex(0x3A7A2A), toon);
            draw_mesh(r, sphere_mesh(8, 5), at(p + V3{0, 0, .13}) * sc(.05, .05, .03), nullptr, petal, toon);
            draw_mesh(r, sphere_mesh(6, 4), at(p + V3{0, 0, .145}) * sc(.02, .02, .015), nullptr, hex(0xF0B030), unlit);
        }
    }
    static_cast<void>(t);
}

// ------------------------------------------------------------------ hedges: hedge.cpp

// ------------------------------------------------------------------ burrows
void Garden::draw_burrows(const GardenState& s, double t) {
    int k = 0;
    for (int i = 0; i < lv_.w * lv_.h; ++i) {
        if (!lv_.goal[static_cast<size_t>(i)]) continue;
        // the pit, its raccoon (hidden, peeking, out and taunting), then the dark over what is still below
        ground_.draw_pit(r, lv_, i);
        if (k < static_cast<int>(s.coons.size())) draw_raccoon(r, s.coons[static_cast<size_t>(k)], t);
        ground_.shade_pit(r, lv_, i);
        ++k;
    }
}

// ------------------------------------------------------------------ pumpkins
void Garden::draw_pumpkins(const GardenState& s, double t) {
    const Palette P = palette(s.season);
    for (const PumpkinView& pv : s.pumpkins) {
        const double R = .36, H = .3;
        const double wob = std::sin(t * 23 + pv.seed) * .08 * pv.wobble;
        const V3 base = pv.pos + V3{0, 0, pv.hop * .25};
        const M34 m = at(base + V3{0, 0, H}) * M34::rot_x(pv.roll_x + wob) * M34::rot_y(pv.roll_y + wob * .6) * M34::rot_z(pv.seed * .7);
        const Col body = mix(P.pumpkin, hex(0xFFD040), static_cast<float>(pv.glow * .35));
        // a shadow, then the pumpkin, its stem and a leaf
        decal(r, pv.pos + V3{.04, -.02, .008}, .82, .66, &tx().glow, hex(0x000000, .35f), kSoft);
        draw_mesh(r, pumpkin_mesh(), m * sc(R, R, H), nullptr, body, toon);
        draw_outline(r, pumpkin_mesh(), m * sc(R, R, H), .02, P.pumpkin_ink);
        const M34 stem = m * at({0, 0, H * .85}) * M34::rot_y(.25) * sc(.045, .045, .14);
        draw_mesh(r, cylinder_mesh(6), stem, nullptr, P.stem, toon);
        draw_outline(r, cylinder_mesh(6), stem, .012, hex(0x1E2A0A));
        draw_mesh(r, sphere_mesh(8, 5), m * at({.08, .02, H * .95}) * M34::rot_z(.6) * sc(.11, .06, .02), nullptr, hex(0x4C9A34), toon);
        // a highlight
        sprite(base + V3{-.12, -.15, H * 1.55}, .16, &tx().glow, hex(0xFFFFFF, .45f), kGlow);
        if (pv.glow > 0) sprite(base + V3{0, 0, H}, 1.4 * pv.glow + .4, &tx().glow, hex(0xFFE080, static_cast<float>(.5 * pv.glow)), kGlow);
    }
    // trapped raccoons: a paw poking out from under each covered burrow
    int b = 0;
    for (int i = 0; i < lv_.w * lv_.h; ++i) {
        if (!lv_.goal[static_cast<size_t>(i)]) continue;
        if (b < static_cast<int>(s.trapped.size()) && s.trapped[static_cast<size_t>(b)]) {
            const double w = b < static_cast<int>(s.paw_wiggle.size()) ? s.paw_wiggle[static_cast<size_t>(b)] : 0;
            if (std::sin(w * .37) > -.2) draw_trapped_paw(r, cell_pos(i) + V3{.16, -.28, 0}, .3, w);
        }
        ++b;
    }
}

// ------------------------------------------------------------------ marks: hover, path, hint, stuck
void Garden::draw_marks(const GardenState& s, double t) {
    if (s.hover_cell >= 0 && lv_.floor(s.hover_cell)) {
        decal(r, cell_pos(s.hover_cell) + V3{0, 0, .012}, .96, .96, &tx().ring, hex(0xFFFFFF, .55f), kGlow);
    }
    for (size_t i = 0; i < s.path.size(); ++i)
        decal(r, cell_pos(s.path[i]) + V3{0, 0, .012}, .16, .16, &tx().glow, hex(0xFFFFFF, .6f), kGlow);
    if (s.hint_from >= 0 && s.hint_dir >= 0) {
        // chevrons marching the way the pumpkin should go
        const V3 c = cell_pos(s.hint_from);
        const V3 d{static_cast<double>(kDX[s.hint_dir]), static_cast<double>(-kDY[s.hint_dir]), 0};
        for (int k = 0; k < 3; ++k) {
            const double u = std::fmod(s.hint_t * 1.2 + k / 3.0, 1.0);
            const V3 p = c + d * (.45 + .6 * u) + V3{0, 0, .03};
            const double ang = std::atan2(d.x, d.y);
            const V3 ex{std::cos(ang) * .3, -std::sin(ang) * .3, 0}, ey{std::sin(ang) * .3, std::cos(ang) * .3, 0};
            Vtx q[6] = {{p - ex - ey, {0, 0, 1}, 0, 1, hex(0xFFF2A0, static_cast<float>(1 - u))}, {p + ex - ey, {0, 0, 1}, 1, 1, hex(0xFFF2A0, static_cast<float>(1 - u))},
                        {p + ex + ey, {0, 0, 1}, 1, 0, hex(0xFFF2A0, static_cast<float>(1 - u))}, {p - ex - ey, {0, 0, 1}, 0, 1, hex(0xFFF2A0, static_cast<float>(1 - u))},
                        {p + ex + ey, {0, 0, 1}, 1, 0, hex(0xFFF2A0, static_cast<float>(1 - u))}, {p - ex + ey, {0, 0, 1}, 0, 0, hex(0xFFF2A0, static_cast<float>(1 - u))}};
            r.draw(q, 6, &tx().chevron, kGlow);
            r.draw(q, 6, &tx().chevron, kGlow);  // twice: bright enough on pale soil
        }
        // and the pumpkin itself pulses
        decal(r, c + V3{0, 0, .015}, 1.1, 1.1, &tx().ring, hex(0xFFF2A0, static_cast<float>(.5 + .4 * std::sin(s.hint_t * 6))), kGlow);
    }
    if (s.stuck_cell >= 0) {
        const V3 c = cell_pos(s.stuck_cell) + V3{0, 0, .95 + .05 * std::sin(t * 4)};
        sprite(c, .38, &tx().cross, hex(0xE83A3A), static_cast<std::uint16_t>(cutout | unlit));
    }
    for (const Puff& p : s.puffs) {
        const double u = std::clamp(p.age / p.life, 0.0, 1.0);
        sprite(p.pos + V3{0, 0, u * .25}, p.size * (.6 + u), &tx().puff, alpha(p.col, static_cast<float>(.7 * (1 - u))), kSoft);
    }
}

// ------------------------------------------------------------------ weather
void Garden::draw_weather(const GardenState& s, double t) {
    const double W = lv_.w / 2.0 + 2, H = lv_.h / 2.0 + 2;
    const int n = 28;
    for (int i = 0; i < n; ++i) {
        const double ph = h01(static_cast<std::uint32_t>(i) * 3 + 11);
        const double x0 = (h01(static_cast<std::uint32_t>(i) * 3 + 12) * 2 - 1) * W, y0 = (h01(static_cast<std::uint32_t>(i) * 3 + 13) * 2 - 1) * H;
        switch (s.season) {
            case Season::spring: {
                const double u = std::fmod(t * .08 + ph, 1.0);
                const V3 p{x0 + std::sin(t * .7 + i) * .6 + u * 2, y0 - u * 1.5, 3.2 - u * 3.4};
                if (p.z > 0) sprite(p, .16, &tx().petal, hex(0xFFC2DA, .9f), static_cast<std::uint16_t>(translucent | unlit));
                break;
            }
            case Season::summer: {
                if (i % 4) break;  // a few butterflies
                const double a = t * (.4 + ph * .3) + i;
                const V3 p{x0 * .7 + std::sin(a) * 1.5, y0 * .7 + std::cos(a * .8) * 1.2, .9 + .3 * std::sin(a * 2.3)};
                const double flap = std::fabs(std::sin(t * 14 + i)) * .14 + .04;
                sprite(p + r.right() * (-.06), flap, &tx().petal, hex(i % 8 ? 0xFFE04A : 0x8AC8FF), static_cast<std::uint16_t>(cutout | unlit));
                sprite(p + r.right() * (.06), flap, &tx().petal, hex(i % 8 ? 0xFFE04A : 0x8AC8FF), static_cast<std::uint16_t>(cutout | unlit));
                break;
            }
            case Season::autumn: {
                const double u = std::fmod(t * .06 + ph, 1.0);
                const V3 p{x0 + std::sin(t * 1.3 + i) * .5, y0 - u, 3 - u * 3.2};
                if (p.z > 0) sprite(p, .2, &tx().leaf, i % 3 ? hex(0xE07A2A) : hex(0xC8452A), static_cast<std::uint16_t>(cutout | unlit));
                break;
            }
            case Season::winter: case Season::night: {
                const double u = std::fmod(t * .05 + ph, 1.0);
                const V3 p{x0 + std::sin(t * .9 + i * 2) * .3, y0, 3 - u * 3.2};
                if (p.z > 0) sprite(p, .1, &tx().flake, hex(0xFFFFFF, .9f), static_cast<std::uint16_t>(translucent | unlit));
                if (s.season == Season::night && i % 5 == 0) {
                    // fireflies in the cold? no: a few drifting lights from the lanterns
                    const V3 f{x0 * .6 + std::sin(t * .5 + i) * .8, y0 * .6 + std::cos(t * .4 + i) * .8, .5 + .2 * std::sin(t + i)};
                    sprite(f, .35, &tx().glow, hex(0xFFE090, static_cast<float>(.5 + .5 * std::sin(t * 3 + i))), kGlow);
                }
                break;
            }
        }
    }
}

void Garden::render(const GardenState& s, double t) {
    const Palette P = palette(s.season);
    r.time = t;
    r.clear_depth();
    r.tris_drawn = 0;
    r.light.sun_col = P.sun;
    r.light.amb_col = P.amb;
    ground_.prepare(s.season, r.scale);
    draw_ground(s, t);
    draw_marks(s, t);
    draw_burrows(s, t);
    draw_hedges(s, t);
    draw_pumpkins(s, t);
    if (s.season == Season::night) decal(r, s.bear.pos + V3{0, 0, .01}, 3.2, 3.2, &tx().glow, hex(0xFFD080, .3f), kGlow);  // his lantern's pool of light
    draw_bear(r, s.bear, t);
    draw_weather(s, t);
    if (s.fade > 0) r.fill_rect2(0, 0, r.W, r.H, P.bg, static_cast<float>(std::min(1.0, s.fade)));
}

}  // namespace ct
