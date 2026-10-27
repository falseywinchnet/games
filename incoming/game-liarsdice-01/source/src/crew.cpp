#include "crew.hpp"

#include "platform/mesh.hpp"

#include <algorithm>
#include <cmath>

namespace ld {

namespace {
const Col kInk = hex(0x0E1418);
constexpr double kOutline = .014;
M34 at(V3 v) { return M34::translate(v.x, v.y, v.z); }
M34 at(double x, double y, double z) { return M34::translate(x, y, z); }
M34 sc(double x, double y, double z) { return M34::scale(x, y, z); }
M34 sc(double s) { return M34::scale(s, s, s); }
void part(R3D& r, const Mesh& m, const M34& model, Col c, double ink = kOutline) {
    draw_mesh(r, m, model, nullptr, c, toon);
    if (ink > 0) draw_outline(r, m, model, ink, kInk);
}
void flat(R3D& r, const Mesh& m, const M34& model, Col c) { draw_mesh(r, m, model, nullptr, c, unlit); }
const Mesh& ball() { return sphere_mesh(12, 9); }
const Mesh& small_ball() { return sphere_mesh(8, 6); }

enum Hat { none, tricorn, bandana, captain, bowler, crown, veil, plumed, turban, sailor };
// what each of the 32 wears, and its colour
struct Dress { Hat hat; std::uint32_t col; };
const Dress kDress[32] = {
    {bandana, 0xC82A2A}, {none, 0}, {crown, 0xE8C040}, {sailor, 0xF4F4F0},           // crabs
    {bowler, 0x2A2A30}, {sailor, 0x2A4A8A}, {turban, 0x8A2AA8}, {none, 0},             // octopuses
    {tricorn, 0x2A2026}, {bandana, 0x2A60C0}, {plumed, 0x6A2A5A}, {captain, 0x1E2A48}, // skeletons
    {none, 0}, {plumed, 0x2A7A5A}, {bowler, 0x1E1E22}, {sailor, 0xF0F0EC},             // groupers
    {bandana, 0x8A2AC0}, {none, 0}, {none, 0}, {tricorn, 0x5A1E1E},                    // eels
    {bowler, 0x4A3A2A}, {none, 0}, {bandana, 0xD86A2A}, {none, 0},                     // turtles
    {bandana, 0x303030}, {tricorn, 0x1E1A1A}, {none, 0}, {captain, 0xF0F0F0},          // sharks
    {sailor, 0xE8F0F8}, {veil, 0x1A1A24}, {plumed, 0x9AC8E8}, {none, 0},               // ghosts
};
// main, second, accent colours: four looks of each kind
const std::uint32_t kPal[8][4][3] = {
    {{0xD8402E, 0xF0A070, 0x2A2A2A}, {0x8A6A5A, 0xC8B0A0, 0x6A7A5A}, {0x6A4AB0, 0xC8B0E8, 0x2A2A2A}, {0xE88A30, 0xF8D090, 0x2A2A2A}},  // crab
    {{0x9A4AB8, 0xD8A0E8, 0x2A2A2A}, {0xD86A3A, 0xF0B090, 0x2A2A2A}, {0x3A9AA0, 0x9AD8D8, 0x2A2A2A}, {0xE88AB0, 0xF8C8D8, 0x2A2A2A}},  // octopus
    {{0xEDE6D2, 0x7A2A2A, 0x1A1A1A}, {0xE6DECA, 0x3A3A6A, 0x1A1A1A}, {0xF0EADA, 0x5A2A5A, 0x1A1A1A}, {0xE0D8C2, 0x2A3A5A, 0x1A1A1A}},  // skeleton
    {{0x8A6A48, 0xD8C8A0, 0x5A4030}, {0x8A929A, 0xD0D4D8, 0x5A6068}, {0xA8B048, 0xE8E8A0, 0x6A7030}, {0xD08A8A, 0xF0D0C8, 0x8A5A5A}},  // grouper
    {{0x4A8A3A, 0xC8D870, 0x2A4A2A}, {0xE8C830, 0xF8F0A0, 0x8A6A1A}, {0x3A5AD8, 0x9AD8F8, 0x1A2A6A}, {0x6A4A30, 0xB89A70, 0x3A2A1A}},  // eel
    {{0x4A8A4A, 0xD8D090, 0x3A5A2A}, {0x7A7A3A, 0xD0C890, 0x4A4A2A}, {0x8A6A3A, 0xE0C8A0, 0x5A402A}, {0x5A7A5A, 0xC8C8B0, 0x9A9A8A}},  // turtle
    {{0x6A7A8A, 0xF0F0EC, 0x3A4A5A}, {0x4A6A8A, 0xE8ECF0, 0x2A3A5A}, {0x8A8A7A, 0xF0EEE6, 0x5A5A4A}, {0xB8C0C8, 0xF8F8F8, 0x6A7A8A}},  // shark
    {{0xC8E8F0, 0xF0FAFF, 0x60D0F0}, {0xA8B8D0, 0xE0E8F4, 0x9070E0}, {0xD0F0E8, 0xF4FFFA, 0x60F0C0}, {0xC0C8C8, 0xE8ECEC, 0xB0C0F0}},  // ghost
};

// a soft round glow for eyes and sparks
const Tex& soft() {
    static Tex t = [] {
        Tex d;
        d.make(32, 32);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                const double dx = (x + .5) / 16 - 1, dy = (y + .5) / 16 - 1;
                const double k = std::clamp(1 - std::sqrt(dx * dx + dy * dy), 0.0, 1.0);
                const auto v = static_cast<std::uint32_t>(std::lround(255 * k * k));
                d.at(x, y) = v << 24 | v << 16 | v << 8 | v;
            }
        d.build_mips();
        return d;
    }();
    return t;
}

Col shade(Col c, double flush) { return mix(c, hex(0xA81820), static_cast<float>(flush * .55)); }

void eye(R3D& r, const M34& m, double size, double blink, double down, Col white = hex(0xF8F4EC), bool horizontal_pupil = false) {
    flat(r, small_ball(), m * sc(size, size * .7, size), white);
    const double open = 1 - std::clamp(blink, 0.0, 1.0);
    if (open > .12) {
        const M34 pm = m * at(0, -size * .62, -size * .35 * down);
        if (horizontal_pupil) flat(r, small_ball(), pm * sc(size * .6, size * .3, size * .25 * open), kInk);
        else flat(r, small_ball(), pm * sc(size * .45, size * .3, size * .5 * open), kInk);
    } else {
        flat(r, small_ball(), m * at(0, -size * .66, 0) * sc(size * .8, size * .2, size * .12), kInk);
    }
}

void hat(R3D& r, const M34& top, Hat h, Col c, double t) {
    switch (h) {
        case none: break;
        case tricorn:
            part(r, cylinder_mesh(12), top * sc(.13, .13, .08), c);
            for (int k = 0; k < 3; ++k) {
                const double a = k * 2.094 + 1.57;
                part(r, sphere_mesh(10, 6), top * at(std::cos(a) * .11, std::sin(a) * .11, .04) * M34::rot_z(a) * sc(.09, .18, .035), c);
            }
            part(r, small_ball(), top * at(0, -.14, .05) * sc(.025), hex(0xE8C040), .006);
            break;
        case bandana:
            part(r, hemisphere_mesh(12, 6), top * at(0, 0, -.02) * sc(.15, .15, .1), c);
            part(r, small_ball(), top * at(0, .14, 0) * sc(.05, .05, .04), c, .008);
            for (int s = -1; s <= 1; s += 2) part(r, small_ball(), top * at(s * .04, .19, -.04) * M34::rot_x(.6) * sc(.025, .02, .06), c, .006);
            break;
        case captain:
            part(r, cylinder_mesh(14), top * sc(.13, .13, .07), c);
            part(r, cylinder_mesh(14), top * at(0, .0, .07) * sc(.16, .15, .04), c);
            draw_mesh(r, disc_mesh(14), top * at(0, .0, .11) * sc(.16, .15, 1), nullptr, c, toon);
            part(r, sphere_mesh(10, 4), top * at(0, -.12, .0) * sc(.12, .07, .015), hex(0x1A1A1E), .006);  // the peak
            part(r, small_ball(), top * at(0, -.14, .05) * sc(.03), hex(0xE8C040), .006);
            break;
        case bowler:
            part(r, hemisphere_mesh(12, 6), top * at(0, 0, .0) * sc(.12, .12, .12), c);
            draw_mesh(r, disc_mesh(16), top * at(0, 0, .0) * sc(.18, .18, 1), nullptr, c, toon);
            draw_outline(r, disc_mesh(16), top * at(0, 0, .0) * sc(.18, .18, 1), .008, kInk);
            break;
        case crown:
            part(r, cylinder_mesh(10), top * sc(.1, .1, .06), c);
            for (int k = 0; k < 5; ++k) {
                const double a = k * 1.2566;
                part(r, cone_mesh(5), top * at(std::cos(a) * .09, std::sin(a) * .09, .05) * sc(.025, .025, .06), c, .006);
            }
            part(r, small_ball(), top * at(0, -.1, .03) * sc(.02), hex(0xD02040), .004);
            break;
        case veil:
            part(r, hemisphere_mesh(12, 6), top * at(0, 0, -.01) * sc(.14, .14, .09), c);
            draw_mesh(r, sphere_mesh(12, 8), top * at(0, .02, -.12) * sc(.17, .17, .16), nullptr, alpha(c, .45f), static_cast<std::uint16_t>(translucent | unlit));
            break;
        case plumed:
            part(r, hemisphere_mesh(12, 6), top * at(0, 0, -.01) * sc(.13, .13, .08), c);
            draw_mesh(r, disc_mesh(14), top * at(0, 0, .0) * M34::rot_x(-.2) * sc(.2, .17, 1), nullptr, c, toon);
            part(r, sphere_mesh(8, 5), top * at(.08, .04, .12) * M34::rot_y(-.6 + .05 * std::sin(t * 2)) * M34::rot_x(.3) * sc(.03, .015, .16), hex(0xF0F0F0), .006);
            break;
        case turban:
            part(r, sphere_mesh(12, 8), top * at(0, 0, .03) * sc(.15, .15, .1), c);
            part(r, sphere_mesh(10, 6), top * at(0, 0, .09) * sc(.11, .11, .07), c);
            part(r, small_ball(), top * at(0, -.14, .06) * sc(.03), hex(0x30C0A0), .006);
            break;
        case sailor:
            part(r, cylinder_mesh(14), top * sc(.13, .13, .05), c);
            draw_mesh(r, disc_mesh(14), top * at(0, 0, .05) * sc(.13, .13, 1), nullptr, c, toon);
            draw_mesh(r, torus_mesh(14, 4, .15), top * at(0, 0, .015) * sc(.135, .135, .1), nullptr, hex(0x2A3A6A), unlit);
            break;
    }
}

// the common frame: on the floor at the seat, turned to the table, leaning, swaying, hopping
M34 frame_of(const CrewPose& p, double t) {
    const double hop = std::fabs(std::sin(p.bob * M_PI)) * .08;
    const double sw = std::sin(t * 3.1) * .07 * p.sway;
    return at(p.pos + V3{0, p.fade * .35, hop - p.fade * .05}) * M34::rot_z(p.yaw) * M34::rot_y(sw) * M34::rot_x(-p.lean * .22 + p.slump * .18);
}
double kind_head_z(Species s) {
    switch (s) {
        case Species::crab: return 1.08;
        case Species::octopus: return 1.3;
        case Species::skeleton: return 1.42;
        case Species::grouper: return 1.25;
        case Species::eel: return 1.38;
        case Species::turtle: return 1.25;
        case Species::shark: return 1.4;
        case Species::ghost: return 1.32;
    }
    return 1.3;
}
M34 head_of(const CrewPose& p, const M34& f) {
    const Species s = cast()[static_cast<size_t>(p.who)].species;
    const double y = s == Species::turtle ? -.18 + .1 * std::clamp(p.lean, 0.0, 1.0) : s == Species::eel ? -.12 : 0;
    return f * at(0, y, kind_head_z(s)) * M34::rot_z(p.head_turn) * M34::rot_x(p.head_down * .5 + p.slump * .3);
}
}  // namespace

V3 crew_head(const CrewPose& p) { return head_of(p, frame_of(p, 0)).apply({0, 0, 0}); }
V3 crew_mouth(const CrewPose& p) { return head_of(p, frame_of(p, 0)).apply({0, -.2, -.06}); }

void apply_tell(CrewPose& p, Tell tell, double a, double t) {
    switch (tell) {
        case Tell::glance: p.head_down = std::max(p.head_down, a * .8); break;
        case Tell::blink: if (std::fmod(t * 7, 1.0) < .5) p.blink = std::max(p.blink, a); break;
        case Tell::lean: p.lean += a * .7; break;
        case Tell::tap: p.tap = std::max(p.tap, a); break;
        case Tell::flush: p.flush = std::max(p.flush, a * .8); break;
        case Tell::bubbles: break;  // the scene lets a few bubbles slip from their mouth
        case Tell::sway: p.sway = std::max(p.sway, a); break;
        case Tell::twitch: p.twitch = std::max(p.twitch, a); break;
    }
}

void draw_crew(R3D& r, const CrewPose& p, double t) {
    const Character& ch = cast()[static_cast<size_t>(p.who)];
    const int k = static_cast<int>(ch.species), lk = ch.look;
    Col A = hex(kPal[k][lk][0]), B = hex(kPal[k][lk][1]), C = hex(kPal[k][lk][2]);
    A = shade(A, p.flush);
    B = shade(B, p.flush * .6);
    if (p.fade > 0) { A = mix(A, hex(0x203040), static_cast<float>(p.fade * .5)); B = mix(B, hex(0x203040), static_cast<float>(p.fade * .5)); }
    if (p.hover > 0) { A = mix(A, hex(0xFFFFFF), static_cast<float>(p.hover * .12)); }
    const M34 f = frame_of(p, t);
    const M34 h = head_of(p, f);
    const Dress dr = kDress[p.who];
    const double tapz = p.tap > 0 ? std::fabs(std::sin(t * 14)) * .06 * p.tap : 0;
    const double tw = p.twitch > 0 ? std::sin(t * 31) * p.twitch : 0;
    const double mo = std::clamp(p.mouth, 0.0, 1.0);
    auto arm_lift = [&](int s) { return (s < 0 ? p.arm_l : p.arm_r); };

    switch (ch.species) {
        case Species::crab: {
            // the shell, wide and low behind the table edge; legs tucked beneath
            part(r, ball(), f * at(0, .05, .98) * sc(.42, .3, .2), A);
            draw_mesh(r, ball(), f * at(0, -.12, .94) * sc(.32, .16, .12), nullptr, B, toon);
            for (int s = -1; s <= 1; s += 2)
                for (int l = 0; l < 3; ++l) part(r, cylinder_mesh(6), f * at(s * (.3 + l * .06), .05 + l * .08, .9) * M34::rot_y(s * 2.3) * sc(.025, .025, .3), A, .008);
            // eyestalks: a twitch bends one; a glance tips them both
            for (int s = -1; s <= 1; s += 2) {
                const double bend = (s > 0 ? tw * .35 : 0) + p.head_down * .55;
                const M34 st = f * at(s * .1, -.14, 1.08) * M34::rot_z(p.head_turn * .5) * M34::rot_x(bend) * M34::rot_y(s * -.15);
                part(r, cylinder_mesh(6), st * sc(.025, .025, .2), A, .008);
                eye(r, st * at(0, 0, .22), .055, p.blink, p.head_down);
            }
            flat(r, small_ball(), f * at(0, -.29, 1.0) * sc(.06, .02, .015 + .03 * mo), kInk);  // the mouth
            // claws on the table; a tap raises the right one and knocks it down
            for (int s = -1; s <= 1; s += 2) {
                const double lift = arm_lift(s) * .35 + (s > 0 ? tapz : 0);
                const M34 cl = f * at(s * .3, -.48, .86 + lift) * M34::rot_z(s * .3);
                part(r, cylinder_mesh(6), f * at(s * .32, -.15, .9) * M34::rot_x(1.2 - lift) * sc(.04, .04, .35), A, .01);
                part(r, ball(), cl * sc(.11, .09, .07), A);
                part(r, sphere_mesh(8, 5), cl * at(-s * .04, -.1, .02) * M34::rot_z(-s * .25) * sc(.05, .1, .04), A, .01);
                part(r, sphere_mesh(8, 5), cl * at(s * .04, -.1, -.01 - mo * .02) * M34::rot_z(s * .25) * sc(.04, .09, .03), B, .01);
            }
            hat(r, f * at(0, .02, 1.16), dr.hat, hex(dr.col), t);
            if (lk == 1) for (int b = 0; b < 5; ++b) part(r, cone_mesh(6), f * at(-.25 + b * .12, .1, 1.12 + .03 * (b % 2)) * sc(.03, .03, .05), hex(0xD8D0C0), .006);  // barnacles
            break;
        }
        case Species::octopus: {
            // tentacles: two on the table's edge (its hands), the rest curling down to the floor
            for (int i = 0; i < 6; ++i) {
                const bool front = i == 2 || i == 3;
                const double x0 = -.2 + i * .08, side = x0 < 0 ? -1 : 1;
                const double lift = front ? arm_lift(i == 2 ? -1 : 1) * .3 + (i == 3 ? tapz : 0) : 0;
                const int segs = 12;
                for (int sgi = 0; sgi < segs; ++sgi) {
                    const double u = sgi / (segs - 1.0), wave = std::sin(t * 1.6 + i * 1.3 + u * 4) * .025 * u;
                    V3 c;
                    if (front) c = {x0 * (1 + u * .6) + wave, -.12 - u * .32, .98 - u * .1 + lift * u};
                    else c = {x0 + side * u * .28 + wave, -.08 - u * .12, .95 - u * .85 + std::sin(u * 3) * .06};
                    const double rad = .055 * (1 - u * .7);
                    part(r, small_ball(), f * at(c) * M34::scale(rad, rad, rad), sgi % 2 ? A : mix(A, B, .15f), .006);
                }
                if (front) part(r, small_ball(), f * at(x0 * 1.6, -.46, .89 + lift) * M34::rot_z(side * .8) * sc(.03, .05, .015), B, .004);  // a curled tip
            }
            // the mantle (a big soft head) and its eyes
            part(r, ball(), h * at(0, .05, .12) * sc(.3, .28, .34), A);
            draw_mesh(r, ball(), h * at(0, -.16, -.08) * sc(.2, .12, .12), nullptr, B, toon);
            for (int s = -1; s <= 1; s += 2) eye(r, h * at(s * .13, -.22, .02 + (s > 0 ? tw * .02 : 0)), .07, p.blink, p.head_down, hex(0xF8F0D0), true);
            flat(r, small_ball(), h * at(0, -.27, -.1) * sc(.03 + .02 * mo, .02, .02 + .03 * mo), kInk);
            for (int d = 0; d < 6; ++d) flat(r, small_ball(), h * at(-.15 + d * .06, -.18 + std::fabs(d - 2.5) * .02, .3 - std::fabs(d - 2.5) * .03) * sc(.02), mix(A, B, .6f));  // spots
            hat(r, h * at(0, .05, .4), dr.hat, hex(dr.col), t);
            break;
        }
        case Species::skeleton: {
            // a coat, a ribcage at the open front, bony arms to the table
            part(r, cylinder_mesh(12), f * at(0, .05, .62) * sc(.22, .17, .55), B);
            for (int i = 0; i < 4; ++i) draw_mesh(r, torus_mesh(12, 4, .12), f * at(0, -.02, .82 + i * .07) * sc(.15 - i * .01, .12, .1), nullptr, A, toon);
            part(r, cylinder_mesh(8), f * at(0, .02, .78) * sc(.025, .025, .45), A, .008);  // the spine
            for (int s = -1; s <= 1; s += 2) {
                const double lift = arm_lift(s) * .3 + (s > 0 ? tapz : 0);
                part(r, cylinder_mesh(6), f * at(s * .2, .0, 1.12) * M34::rot_x(2.2 - lift) * M34::rot_y(s * .2) * sc(.025, .025, .32), A, .008);
                const V3 hand{s * .24, -.42, .84 + lift};
                part(r, sphere_mesh(8, 5), f * at(hand) * sc(.055, .06, .025), A, .008);
                for (int fi = 0; fi < 4; ++fi) part(r, cylinder_mesh(5), f * at(hand + V3{-.03 + fi * .02, -.04, 0}) * M34::rot_x(1.5 + (s > 0 ? tapz * 8 * (fi % 2) : 0)) * sc(.008, .008, .06), A, .004);
            }
            // the skull, the jaw that chatters when it twitches
            part(r, ball(), h * sc(.15, .14, .15), A);
            for (int s = -1; s <= 1; s += 2) {
                flat(r, small_ball(), h * at(s * .06, -.12, .02) * sc(.045, .03, .045), hex(0x101014));
                if (p.blink < .5) flat(r, small_ball(), h * at(s * .06, -.145, .02 - p.head_down * .015) * sc(.012), hex(0xF0E070));  // the glints in the sockets
            }
            flat(r, small_ball(), h * at(0, -.14, -.04) * sc(.018, .01, .025), hex(0x101014));
            const double jaw = mo * .5 + std::fabs(tw) * .35;
            part(r, sphere_mesh(10, 6), h * at(0, -.04, -.1) * M34::rot_x(jaw) * at(0, -.04, -.03) * sc(.11, .09, .05), A, .01);
            for (int tth = 0; tth < 5; ++tth) flat(r, small_ball(), h * at(-.05 + tth * .025, -.135, -.07) * sc(.009, .005, .012), hex(0xFFFFF0));
            hat(r, h * at(0, 0, .12), dr.hat, hex(dr.col), t);
            break;
        }
        case Species::grouper: {
            // a big upright fish: belly, lips, goggle eyes, fins for hands
            part(r, ball(), f * at(0, .05, 1.0) * sc(.36, .3, .4), A);
            draw_mesh(r, ball(), f * at(0, -.1, .92) * sc(.26, .2, .28), nullptr, B, toon);
            for (int d = 0; d < 7; ++d) flat(r, small_ball(), f * at(-.25 + (d * 37 % 50) / 100.0, -.18, .85 + (d * 13 % 40) / 100.0) * sc(.025), C);  // spots
            for (int sp = 0; sp < 5; ++sp) part(r, cone_mesh(5), f * at(0, .15 + sp * .04, 1.3 - sp * .05) * M34::rot_x(.4) * sc(.015, .04, .14), C, .006);  // dorsal spines
            const double lips = .06 + mo * .05;
            part(r, sphere_mesh(10, 6), h * at(0, -.3, -.04) * sc(.13, .06, lips), mix(A, hex(0xD08070), .4f), .01);
            flat(r, small_ball(), h * at(0, -.34, -.04) * sc(.08, .02, lips * .5), kInk);
            for (int s = -1; s <= 1; s += 2) eye(r, h * at(s * .2, -.18, .06), .075, p.blink, p.head_down);
            for (int s = -1; s <= 1; s += 2) {
                const double lift = arm_lift(s) * .3 + (s > 0 ? tapz : 0) + (tw * .1 * (s > 0));
                part(r, sphere_mesh(8, 5), f * at(s * .3, -.35, .87 + lift) * M34::rot_z(s * .4) * sc(.09, .14, .03), C, .008);
            }
            hat(r, f * at(0, .05, 1.38), dr.hat, hex(dr.col), t);
            break;
        }
        case Species::eel: {
            // a long body rising out of the dark in an S, a fin ridge down its back
            for (int s = 0; s < 9; ++s) {
                const double u = s / 8.0;
                const V3 c{std::sin(u * 3.2 + t * .8) * .12 * (1 - u * .5), .3 - u * .42, .55 + u * .82};
                part(r, ball(), f * at(c) * sc(.12 - u * .02), s % 2 ? A : mix(A, C, .2f), .01);
                if (s % 2 == 0 && s < 8) part(r, cone_mesh(5), f * at(c + V3{0, .1, .05}) * M34::rot_x(-.5) * sc(.02, .05, .09), C, .006);
            }
            // the head: long, with a lower jaw that drops to speak
            part(r, ball(), h * at(0, -.05, 0) * sc(.12, .2, .11), A);
            draw_mesh(r, ball(), h * at(0, -.08, -.05) * sc(.09, .17, .06), nullptr, B, toon);
            part(r, sphere_mesh(10, 6), h * at(0, -.06, -.07) * M34::rot_x(mo * .4) * at(0, -.08, -.02) * sc(.08, .14, .035), B, .008);
            for (int s = -1; s <= 1; s += 2) eye(r, h * at(s * .08, -.16, .05), .04, p.blink, p.head_down, hex(0xF8E070));
            if (lk == 1) for (int z = 0; z < 3; ++z) r.billboard(h.apply({std::sin(t * 9 + z) * .15, -.1, .1 + z * .05}), .08, .08, &soft(), hex(0xF8F080, .6f), static_cast<std::uint16_t>(additive | unlit | no_depth_write));  // Zap's sparks
            hat(r, h * at(0, -.02, .1), dr.hat, hex(dr.col), t);
            break;
        }
        case Species::turtle: {
            // the shell, a dome behind; a pale plastron; flippers on the table; the head on a neck that retracts when it leans back
            part(r, ball(), f * at(0, .12, 1.0) * sc(.38, .2, .38), A);
            for (int s = 0; s < 6; ++s) flat(r, sphere_mesh(6, 4), f * at(-.2 + (s % 3) * .2, -.04, .85 + (s / 3) * .25) * sc(.08, .02, .08), mix(A, C, .5f));
            draw_mesh(r, ball(), f * at(0, -.03, .95) * sc(.27, .1, .3), nullptr, B, toon);
            part(r, cylinder_mesh(8), f * at(0, -.08, 1.12) * M34::rot_x(1.2) * sc(.07, .07, .14 * (1 - .5 * std::clamp(p.lean, 0.0, 1.0))), mix(A, B, .4f), .008);
            part(r, ball(), h * sc(.13, .15, .12), mix(A, B, .3f));
            part(r, cone_mesh(8), h * at(0, -.13, -.03) * M34::rot_x(1.57) * sc(.07, .05, .06), mix(A, hex(0x3A3A2A), .5f), .008);
            for (int s = -1; s <= 1; s += 2) {
                eye(r, h * at(s * .08, -.1, .04), .035, std::max(p.blink, .25), p.head_down);  // heavy-lidded
                part(r, sphere_mesh(8, 4), h * at(s * .08, -.1, .065) * sc(.04, .03, .012), mix(A, B, .3f), .004);  // the lid
            }
            flat(r, small_ball(), h * at(0, -.15, -.05) * sc(.05, .015, .008 + .02 * mo), kInk);
            for (int s = -1; s <= 1; s += 2) {
                const double lift = arm_lift(s) * .3 + (s > 0 ? tapz : 0);
                part(r, sphere_mesh(8, 5), f * at(s * .3, -.38, .86 + lift) * M34::rot_z(s * .5) * sc(.08, .17, .035), mix(A, B, .3f), .008);
            }
            if (lk == 3) for (int b = 0; b < 7; ++b) part(r, cone_mesh(6), f * at(-.28 + b * .09, .2, 1.2 + .06 * std::sin(b)) * sc(.035, .035, .05), hex(0xD8D0C0), .006);
            hat(r, h * at(0, 0, .11), dr.hat, hex(dr.col), t);
            break;
        }
        case Species::shark: {
            // torso, white belly, fins; a long pointed snout over a toothy grin; a hammer for Hal
            part(r, ball(), f * at(0, .05, .95) * sc(.3, .24, .42), A);
            draw_mesh(r, ball(), f * at(0, -.08, .9) * sc(.22, .18, .34), nullptr, B, toon);
            part(r, cone_mesh(6), f * at(0, .16, 1.2) * M34::rot_x(-.6 + tw * .2) * sc(.035, .16, .34), A, .01);  // the dorsal fin, tall
            for (int g = 0; g < 3; ++g) for (int s2 = -1; s2 <= 1; s2 += 2) flat(r, small_ball(), f * at(s2 * (.2 - g * .015), -.12, 1.15 - g * .05) * M34::rot_z(s2 * .5) * sc(.012, .03, .03), mix(A, kInk, .5f));  // gill slits
            const bool hammer = lk == 2;
            part(r, ball(), h * at(0, .02, 0) * sc(.17, .17, .14), A);
            if (hammer) {
                part(r, ball(), h * at(0, -.12, .04) * sc(.36, .1, .06), A);
                for (int s = -1; s <= 1; s += 2) eye(r, h * at(s * .34, -.13, .04), .04, p.blink, p.head_down, hex(0x101010));
            } else {
                part(r, cone_mesh(10), h * at(0, -.1, .03) * M34::rot_x(1.75) * sc(.13, .1, .26), A, .01);  // the snout, pointing out over the table
                for (int s = -1; s <= 1; s += 2) eye(r, h * at(s * .12, -.12, .06), .035, p.blink, p.head_down, hex(0x101010));
            }
            draw_mesh(r, ball(), h * at(0, -.12, -.08) * sc(.15, .13, .05), nullptr, B, toon);  // the pale under-jaw
            const double grin = .025 + mo * .05 + (tw > 0 ? tw * .02 : 0);
            flat(r, small_ball(), h * at(0, -.2, -.07) * sc(.13, .05, grin), hex(0x401018));
            for (int tth = 0; tth < 9; ++tth) {
                const double a = -1.2 + tth * .3;
                flat(r, cone_mesh(4), h * at(std::sin(a) * .12, -.2 - std::cos(a) * .03, -.07 + grin * .7) * M34::rot_x(3.14) * sc(.013, .008, .03), hex(0xFFFFF4));
            }
            for (int s = -1; s <= 1; s += 2) {
                const double lift = arm_lift(s) * .3 + (s > 0 ? tapz : 0);
                part(r, sphere_mesh(8, 5), f * at(s * .27, -.32, .87 + lift) * M34::rot_z(s * .3) * sc(.07, .16, .03), A, .008);
            }
            hat(r, h * at(0, .04, .12), dr.hat, hex(dr.col), t);
            break;
        }
        case Species::ghost: {
            // a glowing, see-through sailor: a rounded head-and-body trailing into wisps, glowing eyes
            const std::uint16_t ghostly = static_cast<std::uint16_t>(translucent | unlit);
            const float a = static_cast<float>(.62 - p.fade * .3 - .1 * std::sin(t * 1.3));
            for (int s = 0; s < 5; ++s) {
                const double u = s / 4.0;
                draw_mesh(r, ball(), f * at(std::sin(t * 1.5 + u * 3) * .05 * u, .05 + u * .1, 1.0 - u * .4) * sc(.28 - u * .17), nullptr, alpha(A, a * (1 - static_cast<float>(u) * .4f)), ghostly);
            }
            draw_mesh(r, ball(), h * sc(.22, .2, .22), nullptr, alpha(B, a), ghostly);
            for (int s = -1; s <= 1; s += 2) {
                if (p.blink < .5) {
                    flat(r, small_ball(), h * at(s * .08, -.17, .03 - p.head_down * .03) * sc(.035, .02, .045), hex(kPal[7][lk][2]));
                    r.billboard(h.apply({s * .08, -.22, -.01}), .16, .14, &soft(), alpha(hex(kPal[7][lk][2]), .25f), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
                }
            }
            flat(r, small_ball(), h * at(0, -.19, -.08) * sc(.035, .02, .02 + .05 * mo), hex(0x203040));
            for (int s = -1; s <= 1; s += 2) {
                const double lift = arm_lift(s) * .3 + (s > 0 ? tapz : 0);
                draw_mesh(r, ball(), f * at(s * .26, -.3, .9 + lift) * M34::rot_z(s * .4) * sc(.07, .2, .06), nullptr, alpha(A, a), ghostly);
            }
            hat(r, h * at(0, 0, .17), dr.hat, hex(dr.col), t);
            break;
        }
    }
}

}  // namespace ld
