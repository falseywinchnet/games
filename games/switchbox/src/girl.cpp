#include <numbers>
#include "girl.hpp"

#include "platform/mesh.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace sbx {

namespace {
const Col kWhite{1, 1, 1, 1};
const Col kSweater = hex(0xA6D9B8), kCream = hex(0xF4ECD9), kHair = hex(0x8E5836), kStar = hex(0xF8D44E);
const Col kInkHair = hex(0x3E2216), kInkSkin = hex(0x8A4E3E), kInkCloth = hex(0x2F5A48), kInkCream = hex(0x8C7A5C);
constexpr double kInk = .016;  // outline width in world units (about one game pixel)
constexpr double kHeadRx = .46, kHeadRy = .42, kHeadRz = .43;
constexpr double kUpper = .26, kFore = .25, kHandOff = .07;

M34 at(V3 v) { return M34::translate(v.x, v.y, v.z); }
M34 sc(double x, double y, double z) { return M34::scale(x, y, z); }
V3 lerp(V3 a, V3 b, double t) { return a + (b - a) * t; }

// A frame whose z axis runs along `dir`, x as close as possible to `xhint`.
M34 basis(V3 origin, V3 dir, V3 xhint, double sx, double sy, double sz) {
    const V3 ez = norm(dir);
    V3 ex = xhint - ez * dot(xhint, ez);
    if (len(ex) < 1e-6) ex = std::fabs(ez.z) < .9 ? cross(V3{0, 0, 1}, ez) : cross(V3{1, 0, 0}, ez);
    ex = norm(ex);
    const V3 ey = cross(ez, ex);
    M34 m;
    m.m[0] = ex.x * sx; m.m[1] = ey.x * sy; m.m[2] = ez.x * sz; m.m[3] = origin.x;
    m.m[4] = ex.y * sx; m.m[5] = ey.y * sy; m.m[6] = ez.y * sz; m.m[7] = origin.y;
    m.m[8] = ex.z * sx; m.m[9] = ey.z * sy; m.m[10] = ez.z * sz; m.m[11] = origin.z;
    return m;
}

void part(R3D& r, const Mesh& m, const M34& model, Col c, Col ink, const Tex* tex = nullptr, double ink_w = kInk) {
    draw_mesh(r, m, model, tex, c, toon);
    if (ink_w > 0) draw_outline(r, m, model, ink_w, ink);
}

// The face patch: the front of the unit sphere, textured by planar projection.
const Mesh& face_mesh() {
    static Mesh m;
    if (!m.empty()) return m;
    const int sl = 36, st = 24;
    auto vtx = [&](int i, int j) {
        const double th = 2 * std::numbers::pi * i / sl, ph = -std::numbers::pi / 2 + std::numbers::pi * j / st;
        const V3 p{std::cos(ph) * std::cos(th), std::cos(ph) * std::sin(th), std::sin(ph)};
        return Vtx{p * 1.004, p, .5 + p.x * kFaceK, .5 - (p.z - kFaceZ) * kFaceK};
    };
    for (int j = 0; j < st; ++j)
        for (int i = 0; i < sl; ++i) {
            const Vtx a = vtx(i, j), b = vtx(i + 1, j), c = vtx(i + 1, j + 1), d = vtx(i, j + 1);
            auto inside = [](const Vtx& v) { return v.p.y < -.05 && v.p.z > -.98 && v.p.z < .8 && std::fabs(v.p.x) < .82; };
            if (!(inside(a) && inside(b) && inside(c) && inside(d))) continue;
            m.push_back(a); m.push_back(b); m.push_back(c);
            m.push_back(a); m.push_back(c); m.push_back(d);
        }
    return m;
}

// Hair cap: a sphere with the face window cut out of the front.
const Mesh& cap_mesh() {
    static Mesh m;
    if (!m.empty()) return m;
    const Mesh& s = sphere_mesh(28, 18);
    for (size_t i = 0; i + 2 < s.size(); i += 3) {
        const V3 c = (s[i].p + s[i + 1].p + s[i + 2].p) * (1.0 / 3);
        if (c.y < -.2 && c.z < .5) continue;
        m.push_back(s[i]); m.push_back(s[i + 1]); m.push_back(s[i + 2]);
    }
    return m;
}

// The back of the bob: a sphere with the whole front removed, so it frames
// the face from behind and at the sides but never crosses the cheeks or chin.
const Mesh& shell_mesh() {
    static Mesh m;
    if (!m.empty()) return m;
    const Mesh& s = sphere_mesh(24, 16);
    for (size_t i = 0; i + 2 < s.size(); i += 3) {
        const V3 c = (s[i].p + s[i + 1].p + s[i + 2].p) * (1.0 / 3);
        if (c.y < -.05 + .25 * std::max(0.0, c.z)) continue;
        m.push_back(s[i]); m.push_back(s[i + 1]); m.push_back(s[i + 2]);
    }
    return m;
}

// A hair clump: a tapered, flattened tube from z=0 to a point at z=1 whose
// centreline curls by `bend` along y (local +y is the clump's outer side).
const Mesh& strand_mesh(int bend_q) {
    static std::map<int, Mesh> cache;
    Mesh& m = cache[bend_q];
    if (!m.empty()) return m;
    const double bend = bend_q / 100.0;
    const int rings = 7, sides = 7;
    auto ring = [&](int j, int i) {
        const double t = static_cast<double>(j) / rings, a = 2 * std::numbers::pi * i / sides;
        const double rad = std::pow(1 - t, .75) * (1 + .25 * std::sin(t * std::numbers::pi));
        const V3 c{0, bend * t * t, t};
        const V3 n = norm({std::cos(a), std::sin(a), .25 - .5 * bend * t});
        return Vtx{c + V3{std::cos(a) * rad, std::sin(a) * rad, 0}, n, static_cast<double>(i) / sides, t};
    };
    for (int j = 0; j < rings; ++j)
        for (int i = 0; i < sides; ++i) {
            const Vtx a = ring(j, i), b = ring(j, i + 1), c = ring(j + 1, i + 1), d = ring(j + 1, i);
            m.push_back(a); m.push_back(b); m.push_back(c);
            m.push_back(a); m.push_back(c); m.push_back(d);
        }
    return m;
}

// Hair texture: warm brown with the glossy "angel ring" band near the crown.
const Tex& hair_tex() {
    static Tex t;
    if (!t.px.empty()) return t;
    t.make(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const double v = y / 63.0;  // 0 top of the sphere .. 1 bottom
            const double wave = .025 * std::sin(x * 2 * std::numbers::pi / 64 * 6);
            const double band = std::exp(-std::pow((v - .33 - wave) / .03, 2));
            double k = .95 + .08 * (1 - v);
            Col c = shade(kHair, static_cast<float>(k));
            c = mix(c, hex(0xC98E66), static_cast<float>(band * .55));
            t.at(x, y) = 0xFF000000u | static_cast<std::uint32_t>(std::clamp(c.r, 0.f, 1.f) * 255) << 16 |
                         static_cast<std::uint32_t>(std::clamp(c.g, 0.f, 1.f) * 255) << 8 | static_cast<std::uint32_t>(std::clamp(c.b, 0.f, 1.f) * 255);
        }
    t.build_mips();
    return t;
}

// A clump of hair hanging from `root` (head-local) toward `dir`; `bend` curls
// the tip toward the clump's outer side (negative hugs the head).
void lock(R3D& r, const M34& head, V3 root, V3 dir, V3 across, double w, double thick, double length, double bend = -.15) {
    const M34 m = head * basis(root, dir, across, w, thick, length);
    // the mesh's y is scaled by `thick`, so express the curl in those units
    const int q = std::clamp(static_cast<int>(std::lround(bend * length / thick * 20)) * 5, -600, 600);
    part(r, strand_mesh(q), m, kHair, kInkHair, nullptr, kInk * .8);
}
}  // namespace

M34 body_frame(const GirlPose& p) {
    const double sq = std::max(.5, p.squash);
    return at(p.root) * M34::rot_z(p.yaw) * M34::rot_y(p.side) * M34::rot_x(p.lean) * sc(p.scale, p.scale, p.scale) *
           sc(1 / std::sqrt(sq), 1 / std::sqrt(sq), sq);
}

M34 head_frame(const GirlPose& p) {
    // the head does not squash with the body: rebuild the neck point without the scale
    const M34 b = body_frame(p);
    const V3 neck = b.apply({0, 0, .78});
    const M34 rot = M34::rot_z(p.yaw) * M34::rot_y(p.side) * M34::rot_x(p.lean);
    return at(neck) * rot * M34::rot_z(p.head_yaw) * M34::rot_x(p.head_pitch) * M34::rot_y(p.head_roll) * sc(p.scale, p.scale, p.scale) *
           at({0, 0, .4});
}

V3 head_center(const GirlPose& p) { return head_frame(p).apply({0, 0, 0}); }

V3 shoulder_world(const GirlPose& p, int side) { return body_frame(p).apply({side ? .27 : -.27, -.02, .6}); }

ArmPose solve_arm(const GirlPose& p, int side) {
    ArmPose a;
    const M34 b = body_frame(p);
    a.shoulder = shoulder_world(p, side);
    const V3 rest = b.apply(p.rest[static_cast<size_t>(side)]);
    V3 target = lerp(rest, p.hand[static_cast<size_t>(side)], std::clamp(p.reach[static_cast<size_t>(side)], 0.0, 1.0));
    // two-bone IK to the wrist; the hand sits a little past it along the forearm
    const double l1 = kUpper * p.scale, l2 = (kFore + kHandOff) * p.scale;
    V3 d = target - a.shoulder;
    double dist = std::clamp(len(d), .08, l1 + l2 - 1e-4);
    const V3 dir = norm(d);
    a.hand = a.shoulder + dir * dist;
    const double cosa = std::clamp((l1 * l1 + dist * dist - l2 * l2) / (2 * l1 * dist), -1.0, 1.0);
    const double sina = std::sqrt(1 - cosa * cosa);
    // elbows bend out to the side, down, and slightly back
    const double sx = side ? 1 : -1;
    V3 pole = b.dir({sx * .9, .35, -.8});
    pole = norm(pole - dir * dot(pole, dir));
    a.elbow = a.shoulder + dir * (cosa * l1) + pole * (sina * l1);
    a.wrist = a.elbow + norm(a.hand - a.elbow) * (kFore * p.scale);
    return a;
}

void draw_girl(R3D& r, const GirlPose& p, double t) {
    const M34 b = body_frame(p);
    const M34 h = head_frame(p);
    // ---- body: an oversized sweater
    part(r, sphere_mesh(14, 10), b * at({0, .01, .36}) * sc(.32, .27, .4), kSweater, kInkCloth);
    part(r, sphere_mesh(14, 8), b * at({0, 0, .56}) * sc(.35, .26, .17), kSweater, kInkCloth);
    part(r, torus_mesh(16, 6, .32), b * at({0, -.01, .72}) * sc(.15, .135, .15), kCream, kInkCream);
    draw_mesh(r, sphere_mesh(8, 6), b * at({0, 0, .77}) * sc(.085, .085, .09), nullptr, skin_col(), toon);

    // ---- arms: puffy sleeves, big cream cuffs, mitten hands
    for (int side = 0; side < 2; ++side) {
        const ArmPose a = solve_arm(p, side);
        const V3 xh = b.dir({1, 0, 0});
        const V3 up = a.elbow - a.shoulder, fo = a.wrist - a.elbow;
        const double k = p.scale;
        part(r, sphere_mesh(10, 7), basis((a.shoulder + a.elbow) * .5, up, xh, .1 * k, .1 * k, len(up) * .5 + .07 * k), kSweater, kInkCloth);
        part(r, sphere_mesh(10, 7), basis((a.elbow + a.wrist) * .5, fo, xh, .095 * k, .095 * k, len(fo) * .5 + .05 * k), kSweater, kInkCloth);
        part(r, sphere_mesh(10, 6), basis(a.wrist, fo, xh, .105 * k, .105 * k, .06 * k), kCream, kInkCream);
        const double f = std::clamp(p.fist[static_cast<size_t>(side)], 0.0, 1.0);
        const V3 hd = norm(a.hand - a.wrist);
        part(r, sphere_mesh(10, 7), basis(a.hand, hd, xh, .072 * k, (.062 + .006 * f) * k, (.08 - .015 * f) * k), skin_col(), kInkSkin, nullptr, kInk * .8);
        const double pt = std::clamp(p.point[static_cast<size_t>(side)], 0.0, 1.0);
        if (pt > .05) {
            const V3 tip = a.hand + hd * (.07 * k);
            part(r, sphere_mesh(8, 5), basis(tip, hd, xh, .024 * k, .024 * k, .07 * k * pt), skin_col(), kInkSkin, nullptr, kInk * .7);
        }
    }

    // ---- head and face
    part(r, sphere_mesh(20, 14), h * sc(kHeadRx, kHeadRy, kHeadRz), skin_col(), kInkSkin);
    draw_mesh(r, face_mesh(), h * sc(kHeadRx, kHeadRy, kHeadRz), &face_texture(p.face), {1, 1, 1, 1}, toon);

    // ---- hair: cap, bob volume, bangs, side locks, flared ends, ahoge, star clips
    // secondary motion: the hanging hair trails the head (lag is in world units)
    const V3 lag_w = p.hair_lag;
    const M34 inv_rot = M34::rot_x(-p.lean) * M34::rot_y(-p.side) * M34::rot_z(-p.yaw);
    const V3 lag = inv_rot.dir(lag_w);
    const double swing_x = std::clamp(lag.x * 2.5, -.5, .5), swing_y = std::clamp(lag.y * 2.5, -.5, .5);
    auto hang = [&](V3 d, double k) { return norm(d + V3{swing_x * k, swing_y * k, 0}); };

    // open meshes get their ink from the closed sphere, so no inside shows through the face window
    const M34 capm = h * at({0, .015, .03}) * sc(kHeadRx * 1.07, kHeadRy * 1.08, kHeadRz * 1.06);
    const M34 shellm = h * at({0, .05, -.06}) * sc(.53, .47, .47);
    part(r, cap_mesh(), capm, kWhite, kInkHair, &hair_tex(), 0);
    part(r, shell_mesh(), shellm, kWhite, kInkHair, &hair_tex(), 0);
    draw_outline(r, sphere_mesh(20, 14), capm, kInk, kInkHair);
    draw_outline(r, sphere_mesh(20, 14), shellm, kInk, kInkHair);
    // bangs: soft pointed clumps across the forehead, tips curling in toward the brow
    const int nb = 6;
    for (int i = 0; i < nb; ++i) {
        const double u = (i - (nb - 1) * .5) / ((nb - 1) * .5);  // -1..1
        const double th = u * .95;
        const V3 root{std::sin(th) * .41, -std::cos(th) * .36, .4 - .05 * std::fabs(u)};
        const double length = .27 + .07 * std::fabs(u) + .03 * std::sin(i * 2.3 + 1);
        const V3 d = hang({std::sin(th) * .2 + u * .12, -std::cos(th) * .42, -1}, .35);
        lock(r, h, root, d, {std::cos(th), std::sin(th), 0}, .14, .06, length, -.22);
    }
    // a wispy middle strand between the eyes
    lock(r, h, {.03, -.38, .36}, hang({-.05, -.45, -1}, .35), {1, 0, 0}, .07, .04, .33, -.18);
    // side locks framing the cheeks, falling to the jaw
    for (int s2 = -1; s2 <= 1; s2 += 2) {
        lock(r, h, {s2 * .4, -.2, .2}, hang({-s2 * .02, -.18, -1}, .8), {0, s2 * -1.0, 0}, .13, .07, .58, -.18);
        lock(r, h, {s2 * .47, -.04, .12}, hang({s2 * .1, .02, -1}, .8), {0, s2 * -1.0, 0}, .14, .08, .56, .12);
    }
    // the bob's ends: chunky clumps around the back and sides, flicking outward
    for (int i = 0; i < 9; ++i) {
        const double th = std::numbers::pi * (.42 + 1.16 * i / 8.0);
        const V3 out{std::sin(th), -std::cos(th), 0};
        const V3 root = V3{out.x * .44, out.y * .4 + .06, -.14};
        lock(r, h, root, hang(out * .3 + V3{0, 0, -1}, 1.0), {out.y, -out.x, 0}, .15, .08, .3 + .04 * std::sin(i * 1.7), .3);
    }
    // ahoge: two thin segments that sway
    {
        const double a = p.ahoge;
        const V3 base{0, -.02, .47};
        const V3 d1 = norm({std::sin(a) * .6, .25, 1});
        const M34 s1 = h * basis(base, d1, {1, 0, 0}, .03, .022, .24);
        const Mesh& sm = strand_mesh(static_cast<int>(std::lround((.5 + .15 * std::sin(a)) * .24 / .022 * 20)) * 5);
        draw_mesh(r, sm, s1, nullptr, kHair, toon);
        draw_outline(r, sm, s1, kInk * .7, kInkHair);
    }
    // star hair clips
    for (int s = -1; s <= 1; s += 2) {
        const M34 m = h * at({s * .33, -.33, .27}) * M34::rot_z(s * -.55) * M34::rot_y(s * .35) * sc(.065, .065, .065);
        part(r, star_mesh(.45, .35), m, kStar, hex(0x9A6A10), nullptr, kInk * .7);
    }
    static_cast<void>(t);
}

}  // namespace sbx
