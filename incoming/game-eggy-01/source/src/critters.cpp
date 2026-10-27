// Rare background life (seen, never met): a resting deer, a preening cat, frogs
// on logs and leaping fish - plus the base camp set pieces. Low-poly models.
#include "critters.hpp"

#include "mesh.hpp"
#include "textures.hpp"

#include <cmath>

namespace eggy {

namespace {
M34 at(double x, double y, double z) { return M34::translate(x, y, z); }
M34 sc(double x, double y, double z) { return M34::scale(x, y, z); }
void blob(R3D& r, const M34& m, Col c, std::uint8_t mat = opaque) { draw_mesh(r, sphere_mesh(8, 5), m, nullptr, c, mat); }
}  // namespace

void draw_deer(R3D& r, double x, double y, double z, double heading, double t, int variant) {
    const bool fawn = variant % 3 == 0;
    const double s = (fawn ? .75 : 1.0) * 3.0;  // deer dwarf a duckling
    const Col coat = fawn ? hex(0xB8763A) : hex(0x9A6236), belly = hex(0xF0E2C8), dark = hex(0x2A1A10);
    const M34 root = at(x, y, z) * M34::rot_z(heading) * sc(s, s, s);
    blob(r, root * at(0, 0, .16) * sc(.3, .16, .13), coat);                    // body, lying down
    blob(r, root * at(.05, 0, .1) * sc(.22, .13, .07), belly);                 // belly
    for (int side = -1; side <= 1; side += 2) {                               // folded legs
        blob(r, root * at(.18, side * .12, .05) * sc(.12, .04, .04), coat);
        blob(r, root * at(-.18, side * .12, .05) * sc(.12, .05, .05), coat);
    }
    blob(r, root * at(-.3, 0, .24) * sc(.04, .03, .05), belly);               // white tail
    if (fawn)
        for (int k = 0; k < 6; ++k) blob(r, root * at(-.18 + k * .07, (k % 2 ? .06 : -.05), .28) * sc(.02, .02, .008), belly);
    // neck and head, turning slowly; ears flick now and then
    const double look = .7 * std::sin(t * .35 + variant) * (std::sin(t * .13 + variant) > 0 ? 1 : .2);
    const M34 neck = root * at(.22, 0, .22) * M34::rot_z(look) * M34::rot_y(-.9);
    draw_mesh(r, cylinder_mesh(6), neck * sc(.05, .05, .2), nullptr, coat, opaque);
    const M34 head = root * at(.22, 0, .22) * M34::rot_z(look) * at(.12, 0, .16);
    blob(r, head * sc(.09, .055, .06), coat);
    blob(r, head * at(.08, 0, -.015) * sc(.05, .035, .03), coat);
    blob(r, head * at(.125, 0, -.01) * sc(.015, .015, .015), dark);
    for (int side = -1; side <= 1; side += 2) {
        blob(r, head * at(.04, side * .045, .02) * sc(.012, .012, .014), dark);
        const double flick = std::max(0.0, std::sin(t * 5 + side + variant) - .92) * 6;
        draw_mesh(r, cone_mesh(5), head * at(-.02, side * .05, .04) * M34::rot_x(side * (.9 + flick * .3)) * sc(.025, .012, .08), nullptr, coat, opaque);
    }
}

void draw_cat(R3D& r, double x, double y, double z, double heading, double t, int variant) {
    const Col coats[4] = {hex(0xE0913A), hex(0x8A8A90), hex(0x2A2A2E), hex(0xF2E6D8)};
    const Col coat = coats[variant % 4], patch = variant % 4 == 3 ? hex(0xE0913A) : coat;
    const M34 root = at(x, y, z) * M34::rot_z(heading) * sc(2.5, 2.5, 2.5);  // a cat is big to a duckling
    blob(r, root * at(0, 0, .1) * sc(.1, .085, .11), coat);                   // sitting body
    blob(r, root * at(-.04, 0, .06) * sc(.09, .09, .06), patch);
    // preening: head dips to a raised paw, licks, then looks up
    const double cyc = std::fmod(t * .45 + variant, 6.0);
    const double lick = cyc < 3.5 ? std::sin(std::min(1.0, cyc) * M_PI / 2) : 0;
    const double paw = lick * (.04 + .015 * std::sin(t * 9));
    draw_mesh(r, cylinder_mesh(5), root * at(.07, .04, .02) * M34::rot_y(-.3 - lick * 1.1) * sc(.018, .018, .1 + paw), nullptr, coat, opaque);
    draw_mesh(r, cylinder_mesh(5), root * at(.07, -.04, .02) * sc(.018, .018, .1), nullptr, coat, opaque);
    const M34 head = root * at(.06 + lick * .02, lick * .03, .22 - lick * .05) * M34::rot_z(lick * .5) * M34::rot_x(lick * .3);
    blob(r, head * sc(.065, .07, .06), coat);
    for (int side = -1; side <= 1; side += 2) {
        draw_mesh(r, cone_mesh(4), head * at(0, side * .04, .04) * sc(.022, .022, .05), nullptr, coat, opaque);
        blob(r, head * at(.055, side * .025, .012) * sc(.008, .012, lick > .3 ? .003 : .012), hex(0x2A3A10));
    }
    blob(r, head * at(.065, 0, -.012) * sc(.008, .008, .006), hex(0xE88A9A));
    // tail curls and swishes
    const double sw = .5 * std::sin(t * 1.7 + variant);
    draw_mesh(r, cylinder_mesh(4), root * at(-.09, 0, .03) * M34::rot_z(sw) * M34::rot_y(1.3) * sc(.014, .014, .12), nullptr, coat, opaque);
    draw_mesh(r, cylinder_mesh(4), root * at(-.09, 0, .03) * M34::rot_z(sw) * at(-.11, 0, .02) * M34::rot_z(sw * .8) * M34::rot_y(.4) * sc(.014, .014, .1), nullptr, coat, opaque);
}

void draw_frog(R3D& r, double x, double y, double z, double heading, double t, int variant) {
    const Col skin = variant % 3 ? hex(0x4E9A2E) : hex(0x6AAE3A), belly = hex(0xD8E8A0);
    const M34 root = at(x, y, z) * M34::rot_z(heading) * sc(1.4, 1.4, 1.4);
    // an occasional hop along the log
    const double cyc = std::fmod(t * .3 + variant * .37, 7.0);
    const double hop = cyc < .5 ? std::sin(cyc / .5 * M_PI) * .08 : 0;
    const M34 b = root * at(0, 0, hop);
    blob(r, b * at(0, 0, .03) * sc(.055, .045, .03), skin);
    for (int side = -1; side <= 1; side += 2) {
        blob(r, b * at(-.03, side * .045, .015) * sc(.035, .015, .015), skin);
        blob(r, b * at(.03, side * .03, .055) * sc(.014, .014, .014), skin);
        blob(r, b * at(.036, side * .032, .062) * sc(.007, .007, .008), hex(0x101008));
    }
    const double croak = std::max(0.0, std::sin(t * 2.2 + variant));
    blob(r, b * at(.045, 0, .015) * sc(.018 + croak * .014, .018 + croak * .016, .012 + croak * .012), belly);
}

void draw_fish(R3D& r, double x, double y, double z, double angle, double tilt, int variant) {
    const Col c = variant % 2 ? hex(0xC8D4E0) : hex(0xE0803A);
    const M34 root = at(x, y, z) * M34::rot_z(angle) * M34::rot_y(tilt) * sc(1.6, 1.6, 1.6);
    blob(r, root * sc(.07, .022, .03), c);
    draw_mesh(r, cone_mesh(3), root * at(-.06, 0, 0) * M34::rot_y(-M_PI / 2) * sc(.02, .03, .04), nullptr, shade(c, .85f), opaque);
}

void draw_campfire(R3D& r, const Textures& T, double x, double y, double z, double t) {
    for (int k = 0; k < 7; ++k) {
        const double a = k * 2 * M_PI / 7;
        blob(r, at(x + std::cos(a) * .2, y + std::sin(a) * .2, z + .02) * sc(.05, .045, .035), hex(0x7A7670));
    }
    for (int k = 0; k < 3; ++k)
        draw_mesh(r, cylinder_mesh(5), at(x, y, z + .03) * M34::rot_z(k * 2.1) * M34::rot_y(1.35) * at(0, 0, -.13) * sc(.025, .025, .26), &T.bark, hex(0x8A6A4A), opaque);
    for (int k = 0; k < 4; ++k) {
        const double f = .55 + .45 * std::sin(t * (9 + k * 3) + k * 1.7);
        const Col fc = k == 0 ? hex(0xFFE08A) : k == 1 ? hex(0xFFB040) : hex(0xFF6A20);
        r.billboard({x + (k - 1.5) * .03, y, z / r.height_scale + .02}, .1 + .06 * f - k * .01, .18 + .14 * f - k * .03, &T.glow, alpha(fc, .9f), additive | unlit | no_fog);
    }
    r.billboard({x, y, z / r.height_scale - .1}, .9, .9, &T.glow, hex(0xFF8A30, .25f), additive | unlit | no_fog);
}

void draw_camp_tent(R3D& r, const Textures& T, double x, double y, double z, double facing, double size, Col tint) {
    // simple A-frame of canvas with a dark door, door facing `facing` (world angle)
    const double c = std::cos(facing), s = std::sin(facing);
    auto L = [&](double fwd, double side, double up) { return V3{x + (fwd * c - side * s) * size, y + (fwd * s + side * c) * size, z + up * size}; };
    const V3 A = L(.6, -.55, 0), B = L(.6, .55, 0), C = L(-.7, -.55, 0), D = L(-.7, .55, 0), T0 = L(.6, 0, .9), T1 = L(-.7, 0, .9);
    auto quad = [&](V3 a, V3 b, V3 c2, V3 d) {
        const V3 n = norm(cross(b - a, d - a));
        Vtx q[6] = {{a, n, 0, 1, tint}, {b, n, 1, 1, tint}, {c2, n, 1, 0, tint}, {a, n, 0, 1, tint}, {c2, n, 1, 0, tint}, {d, n, 0, 0, tint}};
        r.draw(q, 6, &T.canvas, double_sided);
    };
    quad(A, C, T1, T0);
    quad(D, B, T0, T1);
    const V3 nb{-c, -s, 0};
    Vtx back[3] = {{C, nb, 0, 0, shade(tint, .8f)}, {D, nb, 0, 0, shade(tint, .8f)}, {T1, nb, 0, 0, shade(tint, .8f)}};
    r.draw(back, 3, nullptr, double_sided);
    Vtx door[3] = {{A, {c, s, 0}, 0, 0, hex(0x3A2A18)}, {B, {c, s, 0}, 0, 0, hex(0x3A2A18)}, {T0, {c, s, 0}, 0, 0, hex(0x3A2A18)}};
    r.draw(door, 3, nullptr, double_sided | unlit);
}

}  // namespace eggy
