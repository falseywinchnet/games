#include "critters.hpp"

#include "platform/mesh.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace ct {

namespace {
const Col kWhite{1, 1, 1, 1};
// the bear
const Col kFur = hex(0xA0683A), kFurInk = hex(0x4A2A14), kMuzzle = hex(0xE6B884), kOveralls = hex(0x4C7CC8), kOverallsInk = hex(0x1E3462);
const Col kStraw = hex(0xEAC874), kStrawInk = hex(0x8A6A26), kBand = hex(0xD8443A), kButton = hex(0xF4D24A);
// the raccoons
const Col kCoon = hex(0x8E929C), kCoonDark = hex(0x3C3E46), kCoonInk = hex(0x24262C), kCoonLight = hex(0xE8E8EC);
const Col kCarrot = hex(0xF2862E), kLeaf = hex(0x5CB848), kTurnip = hex(0xE8DCEC), kTurnipTop = hex(0xA858B8);
constexpr double kInk = .018;
constexpr double kFaceK = .6, kFaceZ = -.08;

M34 at(V3 v) { return M34::translate(v.x, v.y, v.z); }
M34 sc(double x, double y, double z) { return M34::scale(x, y, z); }

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

// a capsule-ish limb from a to b
void limb(R3D& r, const M34& frame, V3 a, V3 b, double rad, Col c, Col ink) {
    const V3 wa = frame.apply(a), wb = frame.apply(b);
    const V3 d = wb - wa;
    const double l = len(d);
    if (l < 1e-5) return;
    const M34 m = basis(wa, d, {1, 0, 0}, rad, rad, l);
    part(r, cylinder_mesh(8), m, c, ink);
}

// the front of a unit sphere, textured by planar projection: the painted face
const Mesh& face_mesh() {
    static Mesh m;
    if (!m.empty()) return m;
    const int sl = 32, st = 22;
    auto vtx = [&](int i, int j) {
        const double th = 2 * M_PI * i / sl, ph = -M_PI / 2 + M_PI * j / st;
        const V3 p{std::cos(ph) * std::cos(th), std::cos(ph) * std::sin(th), std::sin(ph)};
        return Vtx{p * 1.006, p, .5 + p.x * kFaceK, .5 - (p.z - kFaceZ) * kFaceK};
    };
    for (int j = 0; j < st; ++j)
        for (int i = 0; i < sl; ++i) {
            const Vtx a = vtx(i, j), b = vtx(i + 1, j), c = vtx(i + 1, j + 1), d = vtx(i, j + 1);
            auto inside = [](const Vtx& v) { return v.p.y < -.08 && v.p.z > -.9 && v.p.z < .75 && std::fabs(v.p.x) < .8; };
            if (!(inside(a) && inside(b) && inside(c) && inside(d))) continue;
            m.push_back(a); m.push_back(b); m.push_back(c);
            m.push_back(a); m.push_back(c); m.push_back(d);
        }
    return m;
}

// the lower part of a sphere (the overalls over the bear's tummy)
const Mesh& lower_mesh() {
    static Mesh m;
    if (!m.empty()) return m;
    const Mesh& s = sphere_mesh(20, 14);
    for (size_t i = 0; i + 2 < s.size(); i += 3) {
        const V3 c = (s[i].p + s[i + 1].p + s[i + 2].p) * (1.0 / 3);
        if (c.z > .28 - .25 * std::max(0.0, -c.y)) continue;
        for (int k = 0; k < 3; ++k) { Vtx v = s[i + k]; v.p = v.p * 1.03; m.push_back(v); }
    }
    return m;
}

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

// --- painted eyes and mouths, shared shapes
void eye_open(Canvas& c, double x, double y, double rx, double ry, Col ink, bool white) {
    if (white) c.fill_ellipse(x, y, rx + 2.5, ry + 2.5, hex(0xFFFFFF));
    c.fill_ellipse(x, y, rx, ry, ink);
    c.fill_circle(x - rx * .35, y - ry * .4, std::max(1.6, rx * .42), hex(0xFFFFFF));
    c.fill_circle(x + rx * .3, y + ry * .35, std::max(1.0, rx * .2), hex(0xFFFFFF, .8f));
}
void arc_eye(Canvas& c, double x, double y, double w, double h, Col ink, double thick, bool up) {
    c.begin();
    c.move(x - w, y + (up ? h * .4 : -h * .4));
    c.quad(x, y + (up ? -h : h), x + w, y + (up ? h * .4 : -h * .4));
    c.stroke(ink, thick);
}

const Tex& bear_face(const BearFace& f) {
    static std::map<std::uint32_t, Tex> cache;
    auto it = cache.find(f.key());
    if (it != cache.end()) return it->second;
    Canvas c;
    c.resize(128, 128);
    c.clear(kFur);
    const Col ink = hex(0x1E140E);
    // the muzzle and nose
    c.fill_ellipse(64, 84, 27, 19, kMuzzle);
    c.begin(); c.move(55, 72); c.quad(64, 66, 73, 72); c.quad(70, 80, 64, 81); c.quad(58, 80, 55, 72); c.fill(hex(0x2A1810));
    c.fill_ellipse(61, 71, 3, 1.6, hex(0xFFFFFF, .7f));
    // mouth on the muzzle
    switch (f.mouth) {
        case BMouth::smile: c.begin(); c.move(64, 81); c.line(64, 86); c.stroke(ink, 2); c.begin(); c.move(54, 86); c.quad(64, 94, 74, 86); c.stroke(ink, 2.4); break;
        case BMouth::grin:
            c.begin(); c.move(52, 86); c.quad(64, 102, 76, 86); c.close(); c.fill(hex(0x6A1E1A));
            c.begin(); c.ellipse(64, 96, 6, 3.5); c.fill(hex(0xE86A6A));
            c.begin(); c.move(52, 86); c.quad(64, 102, 76, 86); c.close(); c.stroke(ink, 2); break;
        case BMouth::open: c.fill_ellipse(64, 91, 7, 7, hex(0x6A1E1A)); c.begin(); c.ellipse(64, 91, 7, 7); c.stroke(ink, 2); c.fill_ellipse(64, 95, 4, 2.5, hex(0xE86A6A)); break;
        case BMouth::flat: c.stroke_line(56, 89, 72, 89, ink, 2.4); break;
        case BMouth::o: c.fill_ellipse(64, 90, 4, 4.5, hex(0x6A1E1A)); c.begin(); c.ellipse(64, 90, 4, 4.5); c.stroke(ink, 1.8); break;
        case BMouth::frown: c.begin(); c.move(55, 93); c.quad(64, 85, 73, 93); c.stroke(ink, 2.4); break;
        case BMouth::effort:
            c.fill_rect(54, 86, 20, 7, hex(0xFFFFFF)); c.begin(); c.rect(54, 86, 20, 7); c.stroke(ink, 1.8);
            c.stroke_line(64, 86, 64, 93, ink, 1.2); break;
        case BMouth::gasp: c.fill_ellipse(64, 92, 6, 9, hex(0x6A1E1A)); c.begin(); c.ellipse(64, 92, 6, 9); c.stroke(ink, 2); break;
    }
    // eyes
    const double ex[2] = {40, 88}, ey = 50;
    for (double x : ex) {
        switch (f.eyes) {
            case BEyes::open: eye_open(c, x, ey, 8.5, 10.5, ink, false); break;
            case BEyes::wide: eye_open(c, x, ey, 9, 12, ink, true); break;
            case BEyes::happy: arc_eye(c, x, ey + 2, 10, 8, ink, 4.5, true); break;
            case BEyes::closed: arc_eye(c, x, ey + 1, 10, 5, ink, 4.2, false); break;
            case BEyes::squint: c.stroke_line(x - 10, ey, x + 10, ey, ink, 4.2); break;
            case BEyes::worried: eye_open(c, x, ey + 1, 7, 9, ink, false); break;
            case BEyes::shut_tight: {
                const double s = x < 64 ? 1 : -1;
                c.begin(); c.move(x - 8 * s, ey - 5); c.line(x + 6 * s, ey); c.line(x - 8 * s, ey + 5); c.stroke(ink, 3);
                break;
            }
            case BEyes::dizzy:
                c.begin();
                for (int k = 0; k <= 24; ++k) {
                    const double a = k * .55, rr = 1 + k * .32;
                    k == 0 ? c.move(x + std::cos(a) * rr, ey + std::sin(a) * rr) : c.line(x + std::cos(a) * rr, ey + std::sin(a) * rr);
                }
                c.stroke(ink, 1.8);
                break;
        }
    }
    // brows
    for (int k = 0; k < 2; ++k) {
        const double x = ex[k], s = k == 0 ? 1 : -1;
        switch (f.brow) {
            case 1: c.begin(); c.move(x - 8, ey - 14); c.quad(x, ey - 20, x + 8, ey - 14); c.stroke(ink, 2.6); break;
            case 2: c.stroke_line(x - 8 * s, ey - 12, x + 7 * s, ey - 18, ink, 2.6); break;
            case 3: c.stroke_line(x - 8 * s, ey - 18, x + 7 * s, ey - 12, ink, 3); break;
            default: break;
        }
    }
    if (f.blush) {
        c.fill_ellipse(24, 74, 9, 5, hex(0xF07A7A, .55f));
        c.fill_ellipse(104, 74, 9, 5, hex(0xF07A7A, .55f));
    }
    return cache.emplace(f.key(), tex_from(c)).first->second;
}

const Tex& coon_face(const CoonFace& f) {
    static std::map<std::uint32_t, Tex> cache;
    auto it = cache.find(f.key());
    if (it != cache.end()) return it->second;
    Canvas c;
    c.resize(128, 128);
    c.clear(kCoon);
    const Col ink = hex(0x121216);
    // the pale face: brow patch and muzzle
    c.begin(); c.move(20, 46); c.quad(64, 6, 108, 46); c.quad(116, 76, 64, 108); c.quad(12, 76, 20, 46); c.fill(kCoonLight);
    // the bandit's mask
    c.begin();
    c.move(10, 44); c.quad(34, 32, 58, 44); c.quad(64, 50, 70, 44); c.quad(94, 32, 118, 44);
    c.quad(122, 64, 98, 70); c.quad(80, 70, 70, 60); c.quad(64, 56, 58, 60); c.quad(48, 70, 30, 70); c.quad(6, 64, 10, 44);
    c.fill(hex(0x1E1E24));
    c.stroke_line(64, 18, 64, 40, hex(0x4A4C54), 4);  // the stripe down the forehead
    // nose and mouth
    c.fill_ellipse(64, 78, 8, 5.5, hex(0x141418));
    c.fill_ellipse(61, 76.5, 2.5, 1.4, hex(0xFFFFFF, .7f));
    switch (f.mouth) {
        case CMouth::smirk: c.begin(); c.move(56, 88); c.quad(66, 94, 76, 85); c.stroke(ink, 2.2); break;
        case CMouth::grin:
            c.begin(); c.move(50, 86); c.quad(64, 100, 78, 86); c.close(); c.fill(hex(0xFFFFFF));
            c.begin(); c.move(50, 86); c.quad(64, 100, 78, 86); c.close(); c.stroke(ink, 2);
            c.stroke_line(57, 87, 57, 93, ink, 1); c.stroke_line(71, 87, 71, 93, ink, 1); break;
        case CMouth::laugh:
            c.begin(); c.move(48, 85); c.quad(64, 108, 80, 85); c.close(); c.fill(hex(0x6A1820));
            c.fill_ellipse(64, 99, 7, 4, hex(0xF07080));
            c.begin(); c.move(48, 85); c.quad(64, 108, 80, 85); c.close(); c.stroke(ink, 2); break;
        case CMouth::raspberry:
            c.begin(); c.move(54, 88); c.quad(64, 92, 74, 88); c.stroke(ink, 2.2);
            c.fill_ellipse(64, 95, 6, 7, hex(0xF06A84)); c.begin(); c.ellipse(64, 95, 6, 7); c.stroke(ink, 1.6); c.stroke_line(64, 90, 64, 98, hex(0xB83A54), 1.2); break;
        case CMouth::o: c.fill_ellipse(64, 91, 4.5, 5.5, hex(0x6A1820)); c.begin(); c.ellipse(64, 91, 4.5, 5.5); c.stroke(ink, 1.8); break;
        case CMouth::frown: c.begin(); c.move(55, 94); c.quad(64, 86, 73, 94); c.stroke(ink, 2.2); break;
        case CMouth::wobble: c.begin(); c.move(52, 91); c.quad(56, 87, 60, 91); c.quad(64, 95, 68, 91); c.quad(72, 87, 76, 91); c.stroke(ink, 2); break;
        case CMouth::grit:
            c.fill_rect(52, 86, 24, 8, hex(0xFFFFFF)); c.begin(); c.rect(52, 86, 24, 8); c.stroke(ink, 1.8);
            c.stroke_line(58, 86, 58, 94, ink, 1); c.stroke_line(64, 86, 64, 94, ink, 1); c.stroke_line(70, 86, 70, 94, ink, 1); break;
    }
    // eyes, shining out of the mask
    const double ex[2] = {38, 90}, ey = 52;
    for (int k = 0; k < 2; ++k) {
        const double x = ex[k], s = k == 0 ? 1 : -1;
        const Col shine = hex(0xFFFFFF);
        switch (f.eyes) {
            case CEyes::open: c.fill_ellipse(x, ey, 7, 7.5, shine); c.fill_ellipse(x + 1, ey + 1, 4.2, 5, ink); c.fill_circle(x - .5, ey - 1.5, 1.6, shine); break;
            case CEyes::wide: c.fill_ellipse(x, ey, 8.5, 9.5, shine); c.fill_ellipse(x, ey, 3.2, 3.6, ink); break;
            case CEyes::sly:
                c.fill_ellipse(x, ey + 1, 7, 6, shine); c.fill_ellipse(x - 2 * s, ey + 2, 4, 4, ink);
                c.fill_rect(x - 9, ey - 8, 18, 7, hex(0x1E1E24)); c.stroke_line(x - 8, ey - 1, x + 8, ey - 1, shine, 1.4); break;
            case CEyes::laugh: arc_eye(c, x, ey + 2, 7, 6, shine, 3.2, true); break;
            case CEyes::closed: arc_eye(c, x, ey + 1, 7, 4, shine, 3, false); break;
            case CEyes::teary:
                c.fill_ellipse(x, ey, 7, 7.5, shine); c.fill_ellipse(x, ey + 1, 4.5, 5, ink); c.fill_circle(x - 1, ey - 1.5, 1.8, shine);
                c.fill_ellipse(x + 3 * s, ey + 12, 2.5, 4, hex(0x8AD8FF)); break;
            case CEyes::dizzy:
                c.begin();
                for (int q = 0; q <= 22; ++q) {
                    const double a = q * .6, rr = 1 + q * .3;
                    q == 0 ? c.move(x + std::cos(a) * rr, ey + std::sin(a) * rr) : c.line(x + std::cos(a) * rr, ey + std::sin(a) * rr);
                }
                c.stroke(shine, 1.8);
                break;
            case CEyes::angry:
                c.fill_ellipse(x, ey + 1, 7, 6.5, shine); c.fill_ellipse(x, ey + 2, 4, 4.5, ink);
                c.begin(); c.move(x - 10 * s, ey - 9); c.line(x + 9 * s, ey - 2); c.line(x + 9 * s, ey - 12); c.close(); c.fill(hex(0x1E1E24)); break;
        }
        switch (f.brow) {
            case 1: c.stroke_line(x - 8 * s, ey - 13, x + 7 * s, ey - 17, kCoonLight, 2.6); break;
            case 2: c.stroke_line(x - 8 * s, ey - 13, x + 7 * s, ey - 19, kCoonLight, 2.6); break;
            case 3: c.stroke_line(x - 8 * s, ey - 19, x + 7 * s, ey - 12, kCoonLight, 2.8); break;
            default: break;
        }
    }
    return cache.emplace(f.key(), tex_from(c)).first->second;
}

// a two-bone arm: shoulder to the paw target, elbow bent out and down
void arm(R3D& r, const M34& frame, V3 shoulder, V3 hand, double upper, double fore, double rad, Col c, Col ink, double paw, Col paw_col) {
    V3 d = hand - shoulder;
    double L = len(d);
    const double maxL = upper + fore - 1e-3;
    if (L > maxL) { hand = shoulder + d * (maxL / L); d = hand - shoulder; L = maxL; }
    const double a = std::clamp((upper * upper + L * L - fore * fore) / (2 * upper * L), -1.0, 1.0);
    const V3 dn = norm(d);
    V3 bend = V3{shoulder.x > 0 ? 1.0 : -1.0, .2, -.6};
    bend = norm(bend - dn * dot(bend, dn));
    const V3 elbow = shoulder + dn * (upper * a) + bend * (upper * std::sqrt(std::max(0.0, 1 - a * a)));
    limb(r, frame, shoulder, elbow, rad, c, ink);
    limb(r, frame, elbow, hand, rad * .92, c, ink);
    part(r, sphere_mesh(10, 7), frame * at(elbow) * sc(rad, rad, rad), c, ink, nullptr, 0);
    part(r, sphere_mesh(10, 7), frame * at(hand) * sc(paw, paw, paw), paw_col, ink);
}

void carrot(R3D& r, const M34& m) {
    part(r, cone_mesh(8), m * M34::rot_x(M_PI) * sc(.05, .05, .2), kCarrot, hex(0x8A3A10));
    for (int k = 0; k < 3; ++k)
        part(r, cone_mesh(5), m * M34::rot_z(k * 2.1) * M34::rot_x(.4) * sc(.018, .018, .1), kLeaf, hex(0x1E5A18), nullptr, .012);
}
}  // namespace

V3 bear_head_center(const BearPose& p) {
    const M34 f = at(p.pos + V3{0, 0, p.bob}) * M34::rot_z(p.yaw) * M34::rot_x(-p.lean);
    return f.apply({0, 0, .86 * (1 + p.squash * .12)});
}

void draw_bear(R3D& r, const BearPose& p, double t) {
    const double sq = 1 + p.squash * .12, wide = 1 - p.squash * .07;
    const M34 frame = at(p.pos + V3{0, 0, p.bob}) * M34::rot_z(p.yaw) * M34::rot_x(-p.lean) * sc(wide, wide, sq);
    // legs and feet, swinging with the walk
    for (int s = -1; s <= 1; s += 2) {
        const double ph = p.walk + (s > 0 ? M_PI : 0);
        const double swing = p.walk != 0 ? std::sin(ph) * .09 : 0, lift = p.walk != 0 ? std::max(0.0, std::cos(ph)) * .05 : 0;
        const V3 hip{s * .11, 0, .2}, foot{s * .12, -swing - .02, .05 + lift};
        limb(r, frame, hip, foot, .07, kOveralls, kOverallsInk);
        part(r, sphere_mesh(10, 7), frame * at(foot + V3{0, -.03, -.01}) * sc(.085, .11, .055), kFur, kFurInk);
    }
    // the body, in overalls
    const M34 body = frame * at({0, 0, .38}) * sc(.24, .21, .25);
    part(r, sphere_mesh(18, 12), body, kFur, kFurInk);
    part(r, lower_mesh(), body, kOveralls, kOverallsInk);
    for (int s = -1; s <= 1; s += 2) {
        // straps and buttons
        limb(r, frame, {s * .11, -.2, .5}, {s * .13, -.05, .66}, .022, kOveralls, kOverallsInk);
        part(r, sphere_mesh(6, 4), frame * at({s * .11, -.235, .5}) * sc(.028, .02, .028), kButton, kStrawInk, nullptr, .01);
    }
    part(r, sphere_mesh(8, 6), frame * at({0, .22, .3}) * sc(.06, .06, .06), kFur, kFurInk);  // a stub of a tail
    // arms
    const V3 shL{-.22, -.02, .54}, shR{.22, -.02, .54};
    V3 hL = p.left.hand, hR = p.right.hand;
    if (len(hL) < 1e-6) hL = {-.29, -.06, .32 + .03 * std::sin(t * 2)};
    if (len(hR) < 1e-6) hR = {.29, -.06, .32 + .03 * std::sin(t * 2 + 1)};
    arm(r, frame, shL, hL, .17, .16, .055, kFur, kFurInk, .07, kFur);
    arm(r, frame, shR, hR, .17, .16, .055, kFur, kFurInk, .07, kFur);
    // the head: ears, the painted face, the hat
    // the head tips back a little so the face meets the camera above
    const M34 head = frame * at({0, 0, .86}) * M34::rot_z(p.head_turn) * M34::rot_y(p.head_tilt) * M34::rot_x(p.head_nod - .3);
    const M34 skull = head * sc(.3, .27, .27);
    part(r, sphere_mesh(18, 12), skull, kFur, kFurInk);
    draw_mesh(r, face_mesh(), skull, &bear_face(p.face), kWhite, toon);
    for (int s = -1; s <= 1; s += 2) {
        part(r, sphere_mesh(10, 7), head * at({s * .21, .05, .19}) * sc(.09, .06, .09), kFur, kFurInk);
        part(r, sphere_mesh(8, 6), head * at({s * .21, .01, .19}) * sc(.055, .025, .055), kMuzzle, kFurInk, nullptr, 0);
    }
    // a straw hat pushed back on the head, brim up at the front
    const M34 hat = head * at({0, .12, .21 + p.hat_lift}) * M34::rot_y(p.hat_tilt) * M34::rot_x(.6);
    part(r, cylinder_mesh(18), hat * sc(.27, .27, .02), kStraw, kStrawInk);
    draw_mesh(r, disc_mesh(18), hat * at({0, 0, .02}) * sc(.27, .27, 1), nullptr, kStraw, toon);
    part(r, cylinder_mesh(16), hat * sc(.17, .17, .12), kStraw, kStrawInk);
    draw_mesh(r, disc_mesh(16), hat * at({0, 0, .12}) * sc(.17, .17, 1), nullptr, kStraw, toon);
    part(r, cylinder_mesh(16), hat * at({0, 0, .02}) * sc(.175, .175, .035), kBand, hex(0x6A1A14), nullptr, 0);
    // the facepalm: a paw over the eyes
    if (p.facepalm) part(r, sphere_mesh(10, 7), head * at({.06, -.26, .05}) * sc(.11, .06, .09), kFur, kFurInk);
}

V3 coon_head_center(const CoonPose& p) {
    const double bc = -.62 + p.rise * .55 + p.bounce;
    return p.pos + V3{0, 0, bc + .4};
}

void draw_raccoon(R3D& r, const CoonPose& p, double t) {
    if (p.rise <= .02) return;
    const double bc = -.62 + p.rise * .55 + p.bounce;  // body centre height
    const M34 frame = at(p.pos + V3{0, 0, bc}) * M34::rot_z(p.yaw) * M34::rot_y(p.sway);
    // the ringed tail, only once it is out of the burrow
    if (p.rise > 1.05) {
        // a plump ringed tail curling up behind, wagging
        for (int k = 0; k < 8; ++k) {
            const double u = k / 7.0, w = std::sin(p.tail - k * .45) * .1 * u;
            const V3 q{.06 + w + .1 * u, .17 + .08 * u, -.16 + .4 * u * u};
            const double rr = .085 - .02 * u;
            part(r, sphere_mesh(8, 6), frame * at(q) * sc(rr, rr, rr), k % 2 ? kCoonDark : kCoon, kCoonInk);
        }
    }
    part(r, sphere_mesh(16, 11), frame * sc(.21, .19, .25), kCoon, kCoonInk);
    part(r, sphere_mesh(12, 8), frame * at({0, -.1, -.02}) * sc(.14, .1, .17), kCoonLight, kCoonInk, nullptr, 0);
    // arms and paws
    V3 hL = p.left.hand, hR = p.right.hand;
    if (len(hL) < 1e-6) hL = {-.2, -.12, .02 + .02 * std::sin(t * 3)};
    if (len(hR) < 1e-6) hR = {.2, -.12, .02 + .02 * std::sin(t * 3 + 1)};
    arm(r, frame, {-.16, -.02, .12}, hL, .12, .12, .045, kCoon, kCoonInk, .055, kCoonDark);
    arm(r, frame, {.16, -.02, .12}, hR, .12, .12, .045, kCoon, kCoonInk, .055, kCoonDark);
    // the head
    const M34 head = frame * at({0, -.02, .4}) * M34::rot_z(p.head_turn) * M34::rot_y(p.head_tilt) * M34::rot_x(-.3);
    const M34 skull = head * sc(.26, .23, .22);
    part(r, sphere_mesh(16, 11), skull, kCoon, kCoonInk);
    draw_mesh(r, face_mesh(), skull, &coon_face(p.face), kWhite, toon);
    for (int s = -1; s <= 1; s += 2) {
        const M34 ear = head * at({s * .15, .03, .15}) * M34::rot_y(s * .45) * sc(.09, .05, .14);
        part(r, cone_mesh(8), ear, kCoon, kCoonInk);
        draw_mesh(r, cone_mesh(8), head * at({s * .15, -.01, .16}) * M34::rot_y(s * .45) * sc(.05, .02, .09), nullptr, kCoonDark, toon);
    }
    // what it is holding
    if (p.prop == 1 || p.prop == 4) {
        const M34 hm = frame * at(hR + V3{0, -.02, .06});
        if (p.prop == 1) carrot(r, hm * M34::rot_x(.6 + .3 * std::sin(p.prop_t * 3)));
        else {
            part(r, sphere_mesh(10, 7), hm * sc(.08, .08, .075), kTurnip, hex(0x6A4A70));
            for (int k = 0; k < 3; ++k) part(r, cone_mesh(5), hm * at({0, 0, .06}) * M34::rot_z(k * 2.1) * M34::rot_x(.3) * sc(.02, .02, .1), kLeaf, hex(0x1E5A18), nullptr, .01);
        }
    } else if (p.prop == 2) {
        // juggling: three carrots looping over its head
        for (int k = 0; k < 3; ++k) {
            const double a = p.prop_t * 5 + k * 2.094;
            const V3 c{std::cos(a) * .2, -.12, .5 + std::max(0.0, std::sin(a)) * .3};
            carrot(r, frame * at(c) * M34::rot_y(a * 2));
        }
    } else if (p.prop == 3) {
        // a white flag on a twig
        const V3 base = hR, top = hR + V3{0, 0, .4};
        limb(r, frame, base - V3{0, 0, .08}, top, .012, hex(0x8A5A2A), hex(0x3A2410));
        const V3 a = frame.apply(top), b = frame.apply(top + V3{.22, 0, 0}), c2 = frame.apply(top + V3{.22, 0, -.14}), d = frame.apply(top - V3{0, 0, .14});
        const double wv = std::sin(p.prop_t * 8) * .03;
        Vtx q[6] = {{a, {0, -1, 0}, 0, 0, kWhite}, {b + V3{0, wv, 0}, {0, -1, 0}, 1, 0, kWhite}, {c2 + V3{0, wv, 0}, {0, -1, 0}, 1, 1, kWhite},
                    {a, {0, -1, 0}, 0, 0, kWhite}, {c2 + V3{0, wv, 0}, {0, -1, 0}, 1, 1, kWhite}, {d, {0, -1, 0}, 0, 1, kWhite}};
        r.draw(q, 6, nullptr, static_cast<std::uint16_t>(double_sided | toon));
    }
}

void draw_trapped_paw(R3D& r, V3 at_, double yaw, double wiggle) {
    const M34 f = at(at_) * M34::rot_z(yaw);
    const V3 a{0, 0, .03}, b{std::sin(wiggle) * .05, -.13, .07 + .03 * std::cos(wiggle * 1.3)};
    limb(r, f, a, b, .035, kCoon, kCoonInk);
    part(r, sphere_mesh(8, 6), f * at(b) * sc(.05, .05, .04), kCoonDark, kCoonInk);
}

}  // namespace ct
