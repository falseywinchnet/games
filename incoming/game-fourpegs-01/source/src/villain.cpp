#include "villain.hpp"

#include "platform/mesh.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace fp {

namespace {
const Col kWhite{1, 1, 1, 1};
// formal dress: black tailcoat, satin lapels, charcoal waistcoat, white linen, white gloves
const Col kCoat = hex(0x1E1C24), kLapel = hex(0x34313C), kVest = hex(0x4A4752), kGold = hex(0xC9A24A), kShirt = hex(0xF4F2EE);
const Col kTie = hex(0x111014), kGlove = hex(0xF6F4F0), kHair = hex(0x2B2729), kSilver = hex(0xB6B3B8);
const Col kInkCloth = hex(0x050407), kInkSkin = hex(0x6A4A3E), kInkGold = hex(0x5E4612);
constexpr double kInk = .022;
constexpr double kHeadRx = .31, kHeadRy = .33, kHeadRz = .45;
constexpr double kUpper = .46, kFore = .44, kHandOff = .1;

M34 at(V3 v) { return M34::translate(v.x, v.y, v.z); }
M34 sc(double x, double y, double z) { return M34::scale(x, y, z); }
V3 lerp(V3 a, V3 b, double t) { return a + (b - a) * t; }

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

const Mesh& face_mesh() {
    static Mesh m;
    if (!m.empty()) return m;
    const int sl = 36, st = 24;
    auto vtx = [&](int i, int j) {
        const double th = 2 * M_PI * i / sl, ph = -M_PI / 2 + M_PI * j / st;
        const V3 p{std::cos(ph) * std::cos(th), std::cos(ph) * std::sin(th), std::sin(ph)};
        return Vtx{p * 1.004, p, .5 + p.x * kVFaceK, .5 - (p.z - kVFaceZ) * kVFaceK};
    };
    for (int j = 0; j < st; ++j)
        for (int i = 0; i < sl; ++i) {
            const Vtx a = vtx(i, j), b = vtx(i + 1, j), c = vtx(i + 1, j + 1), d = vtx(i, j + 1);
            auto inside = [](const Vtx& v) { return v.p.y < -.05 && v.p.z > -.98 && v.p.z < .78 && std::fabs(v.p.x) < .84; };
            if (!(inside(a) && inside(b) && inside(c) && inside(d))) continue;
            m.push_back(a); m.push_back(b); m.push_back(c);
            m.push_back(a); m.push_back(c); m.push_back(d);
        }
    return m;
}

// Slicked-back hair: a cap with a sharp widow's peak cut into the front.
const Mesh& hair_mesh() {
    static Mesh m;
    if (!m.empty()) return m;
    const Mesh& s = sphere_mesh(32, 20);
    for (size_t i = 0; i + 2 < s.size(); i += 3) {
        const V3 c = (s[i].p + s[i + 1].p + s[i + 2].p) * (1.0 / 3);
        const double hairline = .74 - .3 * std::fabs(c.x);  // high, combed back, receding at the temples
        if (c.y < -.1 && c.z < hairline) continue;
        if (c.z < -.1 && c.y < .3) continue;  // above the ears only
        m.push_back(s[i]); m.push_back(s[i + 1]); m.push_back(s[i + 2]);
    }
    return m;
}

// Hair texture: glossy black, swept back, with one dramatic white streak.
const Tex& hair_tex() {
    static Tex t;
    if (!t.px.empty()) return t;
    t.make(64, 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const double u = x / 64.0, v = y / 63.0;
            const double part = std::exp(-std::pow((u - .7) / .008, 2)) * (v < .45 ? 1 : 0);  // a crisp side parting
            const double sheen = std::exp(-std::pow((v - .32) / .07, 2)) * .3;                 // brushed, not greasy
            const double temples = std::clamp((v - .5) / .2, 0.0, 1.0);                         // silver at the sides
            Col c = kHair;
            c = mix(c, hex(0x6E6870), static_cast<float>(sheen));
            c = mix(c, kSilver, static_cast<float>(temples * .8));
            c = mix(c, hex(0x0E0C0E), static_cast<float>(part * .8));
            t.at(x, y) = 0xFF000000u | static_cast<std::uint32_t>(std::clamp(c.r, 0.f, 1.f) * 255) << 16 |
                         static_cast<std::uint32_t>(std::clamp(c.g, 0.f, 1.f) * 255) << 8 | static_cast<std::uint32_t>(std::clamp(c.b, 0.f, 1.f) * 255);
        }
    t.build_mips();
    return t;
}

}  // namespace

M34 villain_body(const VillainPose& p) {
    const double sq = std::max(.5, p.squash);
    return at(p.root) * M34::rot_z(p.yaw) * M34::rot_y(p.side) * M34::rot_x(p.lean) * sc(p.scale, p.scale, p.scale) *
           sc(1 / std::sqrt(sq), 1 / std::sqrt(sq), sq);
}

M34 villain_head(const VillainPose& p) {
    const M34 b = villain_body(p);
    const V3 neck = b.apply({0, 0, 1.22});
    const M34 rot = M34::rot_z(p.yaw) * M34::rot_y(p.side) * M34::rot_x(p.lean);
    const double hs = p.scale * .86;  // an adult's proportions: a smaller head on broad shoulders
    return at(neck) * rot * M34::rot_z(p.head_yaw) * M34::rot_x(p.head_pitch) * M34::rot_y(p.head_roll) * sc(hs, hs, hs) *
           at({0, -.02, .44});
}

V3 villain_head_center(const VillainPose& p) { return villain_head(p).apply({0, 0, 0}); }

V3 villain_shoulder(const VillainPose& p, int side) { return villain_body(p).apply({side ? .5 : -.5, .02, 1.02 + .03 * p.breathe}); }

VArm villain_arm(const VillainPose& p, int side) {
    VArm a;
    const M34 b = villain_body(p);
    a.shoulder = villain_shoulder(p, side);
    const V3 rest = b.apply(p.rest[static_cast<size_t>(side)]);
    const V3 target = lerp(rest, p.hand[static_cast<size_t>(side)], std::clamp(p.reach[static_cast<size_t>(side)], 0.0, 1.0));
    const double l1 = kUpper * p.scale, l2 = (kFore + kHandOff) * p.scale;
    const V3 d = target - a.shoulder;
    const double dist = std::clamp(len(d), .1, l1 + l2 - 1e-4);
    const V3 dir = norm(d);
    a.hand = a.shoulder + dir * dist;
    const double cosa = std::clamp((l1 * l1 + dist * dist - l2 * l2) / (2 * l1 * dist), -1.0, 1.0);
    const double sina = std::sqrt(1 - cosa * cosa);
    const double sx = side ? 1 : -1;
    V3 pole = b.dir({sx * 1.0, .3, -.6});  // elbows out and down: a schemer's posture
    pole = norm(pole - dir * dot(pole, dir));
    a.elbow = a.shoulder + dir * (cosa * l1) + pole * (sina * l1);
    a.wrist = a.elbow + norm(a.hand - a.elbow) * (kFore * p.scale);
    return a;
}

void draw_villain(R3D& r, const VillainPose& p, double t) {
    const M34 b = villain_body(p);
    const M34 h = villain_head(p);
    const double k = p.scale;
    // ---- torso: tailcoat over a charcoal waistcoat, white shirt front, wing collar, bow tie
    part(r, sphere_mesh(16, 12), b * at({0, .02, .58}) * sc(.44, .3, .6 + .02 * p.breathe), kCoat, kInkCloth);
    part(r, sphere_mesh(16, 10), b * at({0, .03, .96}) * sc(.56, .32, .17), kCoat, kInkCloth);  // squared shoulders
    draw_mesh(r, sphere_mesh(14, 10), b * at({0, -.12, .66}) * sc(.27, .2, .46), nullptr, kVest, toon);   // waistcoat
    draw_mesh(r, sphere_mesh(12, 8), b * at({0, -.2, .95}) * sc(.13, .12, .17), nullptr, kShirt, toon);  // shirt front
    for (int sd = -1; sd <= 1; sd += 2) {
        // satin lapels forming the V
        const M34 lap = b * at({sd * .15, -.23, .78}) * M34::rot_y(sd * .38) * sc(.07, .05, .26);
        draw_mesh(r, sphere_mesh(10, 6), lap, nullptr, kLapel, toon);
        draw_outline(r, sphere_mesh(10, 6), lap, kInk * .6, kInkCloth);
        // wing collar tips
        draw_mesh(r, cone_mesh(5), b * at({sd * .05, -.27, 1.06}) * M34::rot_y(sd * 1.9) * sc(.035, .02, .06), nullptr, kShirt, toon);
        // the bow tie's two loops
        part(r, sphere_mesh(8, 6), b * at({sd * .07, -.29, 1.04}) * sc(.07, .035, .045), kTie, kInkCloth, nullptr, kInk * .5);
    }
    part(r, sphere_mesh(8, 6), b * at({0, -.3, 1.04}) * sc(.03, .03, .035), kTie, kInkCloth, nullptr, kInk * .5);
    for (int i = 0; i < 4; ++i)  // waistcoat buttons
        draw_mesh(r, sphere_mesh(6, 4), b * at({0, -.31, .8 - .13 * i}) * sc(.025, .02, .025), nullptr, hex(0x7A7680), toon);
    {
        // a gold watch chain across the waistcoat
        V3 prev = b.apply({0, -.31, .54});
        for (int i = 1; i <= 8; ++i) {
            const double u = i / 8.0;
            const V3 q = b.apply({-.2 * u, -.3 + .02 * std::sin(u * M_PI), .54 - .06 * std::sin(u * M_PI)});
            draw_mesh(r, cylinder_mesh(4), basis(prev, q - prev, {0, 0, 1}, .008, .008, len(q - prev)), nullptr, kGold, unlit);
            prev = q;
        }
    }
    // tails of the coat behind, and the neck
    part(r, sphere_mesh(14, 8), b * at({0, .22, .45}) * sc(.48, .22, .5), kCoat, kInkCloth);
    draw_mesh(r, cylinder_mesh(10), b * at({0, 0, 1.04}) * sc(.12, .12, .22), nullptr, villain_skin(), toon);
    draw_mesh(r, cylinder_mesh(10), b * at({0, -.005, 1.0}) * sc(.135, .135, .07), nullptr, kShirt, toon);  // collar band

    // ---- arms: coat sleeves with gold cuffs, white gloves
    for (int side = 0; side < 2; ++side) {
        const VArm a = villain_arm(p, side);
        const V3 xh = b.dir({1, 0, 0});
        const V3 up = a.elbow - a.shoulder, fo = a.wrist - a.elbow;
        part(r, sphere_mesh(10, 7), basis((a.shoulder + a.elbow) * .5, up, xh, .15 * k, .15 * k, len(up) * .5 + .1 * k), kCoat, kInkCloth);
        part(r, sphere_mesh(10, 7), basis((a.elbow + a.wrist) * .5, fo, xh, .13 * k, .13 * k, len(fo) * .5 + .07 * k), kCoat, kInkCloth);
        part(r, cylinder_mesh(10), basis(a.wrist - norm(fo) * (.05 * k), fo, xh, .11 * k, .11 * k, .07 * k), kShirt, kInkSkin, nullptr, kInk * .6);  // shirt cuff
        // the gloved hand: palm, a block of fingers that curls, a thumb, and an index finger for pointing
        const size_t si = static_cast<size_t>(side);
        const double curl = std::clamp(p.curl[si], 0.0, 1.0), pt = std::clamp(p.point[si], 0.0, 1.0);
        const V3 fd = norm(p.palm[si] * .6 + norm(a.hand - a.wrist) * .4);
        // the hand's broad axis: level for a palm-down hand, vertical when the palms face each other
        const V3 flat = cross(fd, V3{0, 0, 1}), facing = cross(fd, V3{1, 0, 0});
        const double pin = std::clamp(p.palms_in[si], 0.0, 1.0);
        V3 across = flat * (1 - pin) + facing * pin + xh * .01;
        if (len(across) < 1e-4) across = xh;
        across = norm(across);
        part(r, sphere_mesh(10, 7), basis(a.hand, fd, across, .09 * k, .055 * k, .1 * k), kGlove, kInkSkin, nullptr, kInk * .8);
        const V3 fbase = a.hand + fd * (.08 * k);
        const V3 fdir = norm(fd * (1 - curl) + V3{0, 0, -1} * curl * .6 + norm(a.wrist - a.hand) * curl * .5);
        part(r, sphere_mesh(10, 6), basis(fbase + fdir * (.07 * k * (1 - .5 * curl)), fdir, across, .085 * k, .045 * k, (.1 - .05 * curl) * k), kGlove, kInkSkin, nullptr, kInk * .7);
        part(r, sphere_mesh(8, 5), basis(a.hand + across * (.08 * k * (side ? -1 : 1)) + fd * (.03 * k), fd, across, .03 * k, .03 * k, .07 * k), kGlove, kInkSkin, nullptr, kInk * .6);
        if (pt > .05) {
            const V3 idx = a.hand + fd * (.1 * k);
            part(r, sphere_mesh(8, 5), basis(idx + fd * (.08 * k * pt), fd, across, .03 * k, .03 * k, .1 * k * pt), kGlove, kInkSkin, nullptr, kInk * .6);
        }
    }

    // ---- head: an oval face with a firm jaw, small ears, neatly combed hair
    part(r, sphere_mesh(22, 16), h * sc(kHeadRx, kHeadRy, kHeadRz), villain_skin(), kInkSkin);
    part(r, sphere_mesh(16, 10), h * at({0, .05, -.24}) * sc(.255, .25, .25), villain_skin(), kInkSkin);  // a firm jaw
    draw_mesh(r, face_mesh(), h * sc(kHeadRx, kHeadRy, kHeadRz), &vface_texture(p.face), kWhite, toon);
    for (int sd = -1; sd <= 1; sd += 2)
        part(r, sphere_mesh(8, 6), h * at({sd * .31, .03, .0}) * sc(.045, .06, .09), villain_skin(), kInkSkin, nullptr, kInk * .6);
    {
        const M34 hm = h * at({0, .02, .04}) * sc(kHeadRx * 1.06, kHeadRy * 1.08, kHeadRz * 1.03);
        part(r, hair_mesh(), hm, kWhite, kInkCloth, &hair_tex(), 0);
        draw_outline(r, sphere_mesh(22, 16), hm, kInk, kInkCloth);
        // combed-back volume above the parting, and neat silver sideburns
        part(r, sphere_mesh(14, 8), h * at({-.04, .06, .3}) * M34::rot_x(-.35) * sc(.28, .3, .15), kHair, kInkCloth, nullptr, kInk * .8);
        for (int sd = -1; sd <= 1; sd += 2)
            draw_mesh(r, sphere_mesh(8, 6), h * at({sd * .285, -.04, .02}) * sc(.025, .05, .1), nullptr, mix(kHair, kSilver, .55f), toon);
    }
    // a monocle over his right eye (screen left), on a fine gold chain to the waistcoat
    {
        const V3 c{-.33 * kHeadRx, -.335, .04 * kHeadRz};
        const M34 mm = h * at(c) * M34::rot_x(M_PI / 2) * sc(.072, .072, .072);
        draw_mesh(r, torus_mesh(16, 5, .14), mm, nullptr, kGold, toon);
        draw_outline(r, torus_mesh(16, 5, .14), mm, kInk * .5, kInkGold);
        draw_mesh(r, disc_mesh(16), h * at(c + V3{0, -.004, 0}) * M34::rot_x(M_PI / 2) * sc(.068, .068, 1), nullptr, alpha(hex(0xDDEEFF), .22f),
                  static_cast<std::uint16_t>(translucent | unlit | double_sided));
        r.billboard(h.apply(c + V3{.02, -.02, .01}), .035, .025, nullptr, alpha(kWhite, .5f), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
        const V3 a0 = h.apply(c + V3{-.05, 0, -.05}), a1 = b.apply({-.22, -.3, .66});
        for (int i = 0; i < 8; ++i) {
            const double u0 = i / 8.0, u1 = (i + 1) / 8.0;
            const V3 p0 = lerp(a0, a1, u0) + V3{0, -.03 * std::sin(u0 * M_PI), -.1 * std::sin(u0 * M_PI)};
            const V3 p1 = lerp(a0, a1, u1) + V3{0, -.03 * std::sin(u1 * M_PI), -.1 * std::sin(u1 * M_PI)};
            draw_mesh(r, cylinder_mesh(4), basis(p0, p1 - p0, {1, 0, 0}, .007, .007, len(p1 - p0)), nullptr, kGold, unlit);
        }
    }
    // the pocket watch, when he consults it
    if (p.watch > .02) {
        const VArm a = villain_arm(p, 0);
        const M34 wm = at(a.hand + V3{0, -.06, .07}) * M34::rot_x(1.2) * sc(.1 * p.watch, .1 * p.watch, .02);
        draw_mesh(r, cylinder_mesh(14), wm, nullptr, kGold, toon);
        draw_mesh(r, disc_mesh(14), wm * at({0, 0, 1.01}), nullptr, hex(0xF2EEE2), unlit);
        draw_outline(r, cylinder_mesh(14), wm, kInk * .6, kInkGold);
    }
    static_cast<void>(t);
}

}  // namespace fp
