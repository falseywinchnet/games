// Eggy as a low-poly, texture-mapped 3D duckling (and the summit officer).
#include "render.hpp"
#include "pose.hpp"
#include "textures.hpp"

#include <algorithm>
#include <cmath>

namespace eggy {

namespace {
M34 at(V3 p) { return M34::translate(p.x, p.y, p.z); }
M34 sc(double x, double y, double z) { return M34::scale(x, y, z); }
const Col kWhite{1, 1, 1, 1};
}  // namespace

void draw_duck3d(R3D& r, double x, double y, double z, double heading, const Pose& p, double t) {
    const Textures& T = textures();
    const double S = .36 * p.scale_mul;  // overall size
    const double sit = std::clamp(p.sit, 0.0, 1.0), lie = std::clamp(p.lie, 0.0, 1.0), swim = std::clamp(p.swim, 0.0, 1.0);
    // root: world position, facing, then pose
    M34 root = at({x, y, z}) * M34::rot_z(heading) * sc(S, S, S);
    root = root * at({0, 0, -sit * .07 - swim * .16 + p.bob * -.012});
    root = root * at({0, 0, .3}) * M34::rot_y(-lie * 1.75 + p.lean * .6) * M34::rot_x(p.tilt) * at({0, 0, -.3});
    root = root * sc(p.sx, p.sx, p.sy);

    // feet and legs
    if (swim < .6) {
        const double amp = p.stepamp * (1 - sit);
        for (int side = -1; side <= 1; side += 2) {
            const double ph = p.step + (side > 0 ? 0 : M_PI);
            const double fx = .04 + .07 * std::cos(ph) * amp + sit * .06;
            double lift = std::max(0.0, std::sin(ph)) * .07 * amp;
            if (lie > .3) lift += .05 * (1 + std::sin(t * 18 + side));
            const M34 foot = root * at({fx, side * .1, lift + .012});
            draw_mesh(r, box_mesh(), foot * at({.04, 0, -.012}) * sc(.085, .055, .024), &T.beak, kWhite, opaque);
            if (sit < .5) draw_mesh(r, cylinder_mesh(5), foot * sc(.022, .022, .16), nullptr, hex(0xE07A1E), opaque);
        }
    }
    // tail tuft
    draw_mesh(r, cone_mesh(6), root * at({-.27, 0, .4}) * M34::rot_y(-.95) * sc(.08, .1, .17), &T.fluff, kWhite, opaque, .5);
    // body and belly
    const double puff = p.puff;
    draw_mesh(r, sphere_mesh(12, 8), root * at({0, 0, .32}) * sc(.33 * puff, .29 * puff, .27 * puff), &T.fluff, kWhite, opaque, 2);
    draw_mesh(r, sphere_mesh(10, 6), root * at({.1, 0, .26}) * sc(.255 * puff, .24 * puff, .205 * puff), &T.belly, kWhite, opaque, 1);
    if (p.medal) {
        const M34 m = root * at({.33, 0, .3}) * M34::rot_y(M_PI / 2);
        draw_mesh(r, disc_mesh(8), m * at({0, 0, .005}) * sc(.06, .06, 1), nullptr, hex(0xF7C948), opaque);
        draw_mesh(r, box_mesh(), root * at({.32, 0, .37}) * sc(.01, .03, .05), nullptr, hex(0xC0303A), opaque);
    }
    // wings (hinged at the shoulders)
    for (int side = -1; side <= 1; side += 2) {
        const double raise = (side > 0 ? p.wing_far : p.wing_near);
        const double fwd = side < 0 ? p.wing_fwd : 0;
        M34 w = root * at({-.02, side * .25, .44}) * M34::rot_x(side * raise) * M34::rot_y(-fwd * 1.5);
        draw_mesh(r, sphere_mesh(8, 5), w * at({-.1, side * .035, -.07}) * sc(.18, .055, .12), &T.fluff, hex(0xF2D070), opaque, 1);
    }
    // head
    M34 head = root * at({.14 + p.head_dx * .006, 0, .74 - p.head_dy * .006}) * M34::rot_z(p.head_yaw) * M34::rot_x(p.head_tilt);
    const double R = .25;
    draw_mesh(r, sphere_mesh(12, 8), head * sc(R, R, R), &T.fluff, kWhite, opaque, 2);
    // cheeks
    for (int side = -1; side <= 1; side += 2) {
        const double az = side * .95, el = -.18;
        const V3 q{R * .93 * std::cos(el) * std::cos(az), R * .93 * std::cos(el) * std::sin(az), R * std::sin(el)};
        draw_mesh(r, sphere_mesh(6, 4), head * at(q) * sc(.04, .05, .028), nullptr, hex(0xFF8FA0), opaque);
    }
    // eyes with a glint
    for (int side = -1; side <= 1; side += 2) {
        const double az = side * .62, el = .02;
        const V3 q{R * .95 * std::cos(el) * std::cos(az), R * .95 * std::cos(el) * std::sin(az), R * .95 * std::sin(el)};
        double ez = .066, lift = 0;
        bool glint = true;
        switch (p.eyes) {
            case Eyes::blink: ez = .012; glint = false; break;
            case Eyes::happy: ez = .016; lift = .012; glint = false; break;
            case Eyes::sleepy: ez = .012; lift = -.012; glint = false; break;
            case Eyes::squint: ez = .02; glint = false; break;
            case Eyes::wide: ez = .07; break;
            default: break;
        }
        const double ex = p.eyes == Eyes::wide ? .05 : .042;
        M34 e = head * at({q.x, q.y, q.z + lift});
        if (p.eyes == Eyes::dizzy) e = e * M34::rot_x(t * 9 * side);
        draw_mesh(r, sphere_mesh(6, 4), e * sc(ex, ex, ez), nullptr, hex(0x1A1008), opaque);
        if (glint) draw_mesh(r, sphere_mesh(4, 3), e * at({.026, 0, .024}) * sc(.017, .017, .017), nullptr, hex(0xFFFFFF), opaque | unlit);
    }
    // beak: upper and hinged lower
    const M34 bk = head * at({R * .88, 0, -.06});
    draw_mesh(r, box_mesh(), bk * at({.07, 0, .0}) * sc(.075, .075, .045), &T.beak, kWhite, opaque);
    draw_mesh(r, box_mesh(), bk * M34::rot_y(std::clamp(p.beak, 0.0, 1.0) * .55) * at({.06, 0, -.03}) * sc(.065, .065, .03),
              &T.beak, hex(0xE58A3A), opaque);
    // helmet (acorn cap) or the officer's cap
    M34 hm = head * at({-.03, 0, .115 + p.helmet_lift * .01}) * M34::rot_x(p.helmet_tilt) * M34::rot_y(.08);
    if (!p.officer) {
        draw_mesh(r, hemisphere_mesh(12, 5), hm * sc(.25, .25, .2), &T.acorn, kWhite, opaque, 2);
        draw_mesh(r, cylinder_mesh(12), hm * at({0, 0, -.012}) * sc(.255, .255, .04), &T.acorn, hex(0x9A6A40), opaque, 1);
        draw_mesh(r, cylinder_mesh(5), hm * at({.0, 0, .19}) * M34::rot_y(-.55) * sc(.022, .022, .1), &T.bark, hex(0xC08A5A), opaque);
    } else {
        draw_mesh(r, cylinder_mesh(12), hm * sc(.25, .25, .13), nullptr, hex(0x56633A), opaque);
        draw_mesh(r, disc_mesh(12), hm * at({0, 0, .13}) * sc(.27, .27, 1), nullptr, hex(0x6C7A48), opaque);
        draw_mesh(r, box_mesh(), hm * at({.24, 0, 0}) * sc(.1, .17, .02), nullptr, hex(0x2A2F1A), opaque);
        draw_mesh(r, sphere_mesh(6, 4), hm * at({.25, 0, .07}) * sc(.035, .035, .035), nullptr, hex(0xF7C948), opaque);
    }
}

}  // namespace eggy
