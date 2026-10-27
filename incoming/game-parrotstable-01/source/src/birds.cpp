#include "birds.hpp"

#include "platform/mesh.hpp"

#include <algorithm>
#include <cmath>

namespace pt {

namespace {
// a soft round glow: white fading to nothing at the rim (colour and alpha both, so it works additively)
const Tex& soft_disc() {
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
}  // namespace

namespace {
struct Palette {
    Col body, belly, head, wing, wing_tip, tail, face, beak_top, beak_bot, crest;
    bool face_patch;   // the bare white cheeks of the big macaws
    int crest_kind;    // 0 none, 1 cockatoo fan, 2 a little tuft
};
const Palette& palette(int s) {
    static const Palette p[kSpecies] = {
        {hex(0xD8262A), hex(0xD8262A), hex(0xE0302E), hex(0x2A5AC8), hex(0xF0C020), hex(0xD8262A), hex(0xF4F0EA), hex(0xEDE6D6), hex(0x2A2A2A), hex(0), true, 0},  // scarlet macaw
        {hex(0x2A7AD0), hex(0xF2B820), hex(0x3A9A50), hex(0x2A6AC8), hex(0x1A3A90), hex(0x2A6AC8), hex(0xF4F0EA), hex(0x1E1E22), hex(0x1E1E22), hex(0), true, 0},  // blue-and-gold
        {hex(0x2A3AA8), hex(0x2E40B0), hex(0x2A3AA8), hex(0x24308E), hex(0x1C2678), hex(0x24308E), hex(0xF8D030), hex(0x2A2A30), hex(0x2A2A30), hex(0), false, 0},  // hyacinth
        {hex(0x3AA040), hex(0x58B850), hex(0x48B048), hex(0x2E8A36), hex(0xD8302A), hex(0x48A040), hex(0xF0D030), hex(0x6A6A6A), hex(0x5A5A5A), hex(0), false, 2},  // amazon
        {hex(0xF4F4EE), hex(0xF8F8F4), hex(0xF4F4EE), hex(0xE8E8E0), hex(0xF0E080), hex(0xEEEEE6), hex(0xF4F4EE), hex(0x2A2A2A), hex(0x2A2A2A), hex(0xF8D030), false, 1},  // cockatoo
        {hex(0x8E9298), hex(0xA0A4AA), hex(0x9A9EA4), hex(0x7A7E84), hex(0x6A6E74), hex(0xD02A2A), hex(0xE8E8EA), hex(0x1E1E22), hex(0x1E1E22), hex(0), false, 0},  // African grey
        {hex(0x5AC850), hex(0x6AD860), hex(0xF0E040), hex(0x4AA040), hex(0x2A2A2A), hex(0x2A6AC8), hex(0xF0E040), hex(0xC8B890), hex(0xC8B890), hex(0), false, 0},  // budgie
        {hex(0x58B048), hex(0x68C058), hex(0xF08870), hex(0x48A040), hex(0x2A80C8), hex(0x48A040), hex(0xF08870), hex(0xE8C860), hex(0xE8C860), hex(0), false, 0},  // lovebird
        {hex(0x48B040), hex(0x58C050), hex(0x50B848), hex(0x38983A), hex(0x2A60C0), hex(0x48A040), hex(0x50B848), hex(0xF0A030), hex(0x2A2A2A), hex(0), false, 0},  // eclectus
        {hex(0xA0A4AE), hex(0xE87AA0), hex(0xF0A8C0), hex(0x8E929C), hex(0x7A7E88), hex(0x8E929C), hex(0xF0C8D8), hex(0xE8E0D8), hex(0xE8E0D8), hex(0xF4D8E4), false, 2},  // galah
    };
    return p[((s % kSpecies) + kSpecies) % kSpecies];
}
const Col kInk = hex(0x16121A);
constexpr double kOutline = .016;
M34 at(V3 v) { return M34::translate(v.x, v.y, v.z); }
M34 sc(double x, double y, double z) { return M34::scale(x, y, z); }
void part(R3D& r, const Mesh& m, const M34& model, Col c, double ink = kOutline) {
    draw_mesh(r, m, model, nullptr, c, toon);
    if (ink > 0) draw_outline(r, m, model, ink, kInk);
}
void flat_part(R3D& r, const Mesh& m, const M34& model, Col c) { draw_mesh(r, m, model, nullptr, c, unlit); }

M34 frame_of(const BirdPose& p) {
    const double hop = std::fabs(std::sin(p.bob * M_PI)) * .06;
    const double up = p.fly * p.fly * 2.4;
    return at(p.pos + V3{p.fly * 1.6, p.fly * .8, hop + up}) * M34::rot_z(p.yaw + p.fly * 1.2);
}
M34 head_of(const BirdPose& p, const M34& f) {
    const double shake = std::sin(p.shake * 26) * .35 * p.shake;
    return f * at({0, -.04, .64}) * M34::rot_z(p.head_turn + shake) * M34::rot_y(p.head_tilt) * M34::rot_x(p.head_nod - .15);
}
}  // namespace

const char* species_name(int s) {
    static const char* n[kSpecies] = {"scarlet macaw", "blue-and-gold macaw", "hyacinth macaw", "amazon", "cockatoo", "African grey", "budgie", "lovebird", "eclectus", "galah"};
    return n[((s % kSpecies) + kSpecies) % kSpecies];
}

V3 bird_head(const BirdPose& p) { return head_of(p, frame_of(p)).apply({0, 0, 0}); }
V3 bird_beak(const BirdPose& p) { return head_of(p, frame_of(p)).apply({0, -.2, -.03}); }

void draw_bird(R3D& r, const BirdPose& p, double t) {
    const Palette& P = palette(p.species);
    const M34 f = frame_of(p);
    const double puff = 1 + .12 * p.ruffle;
    // the tail: long feathers hanging down behind
    for (int k = -1; k <= 1; ++k) {
        const M34 tail = f * at({k * .05, .14, .16}) * M34::rot_x(.35 + 1.1 * p.fly + .05 * std::sin(t * 2 + k)) * M34::rot_y(k * .12) * sc(.045, .03, .32);
        part(r, sphere_mesh(8, 6), tail * at({0, 0, -.5}), k == 0 ? P.tail : mix(P.tail, P.wing_tip, .4f));
    }
    // the body, leaning forward a touch, and its belly
    const M34 body = f * at({0, 0, .34}) * M34::rot_x(-.12) * sc(.17 * puff, .16 * puff, .27 * puff);
    part(r, sphere_mesh(14, 10), body, P.body);
    draw_mesh(r, sphere_mesh(12, 8), f * at({0, -.06, .3}) * M34::rot_x(-.12) * sc(.13 * puff, .11, .2 * puff), nullptr, P.belly, toon);
    // wings at the sides; raised, they gesture
    for (int s = -1; s <= 1; s += 2) {
        const double up = s < 0 ? p.wing_l : p.wing_r;
        const M34 w = f * at({s * .17 * puff, .02, .48}) * M34::rot_y(s * (.15 + 1.6 * up)) * M34::rot_x(-.1 - .4 * up) * sc(.07, .13, .24);
        part(r, sphere_mesh(10, 7), w * at({0, 0, -.6}), P.wing);
        draw_mesh(r, sphere_mesh(8, 5), w * at({0, .2, -1.25}) * sc(.8, .7, .5), nullptr, P.wing_tip, toon);
    }
    // feet gripping the perch
    for (int s = -1; s <= 1; s += 2) part(r, sphere_mesh(6, 4), f * at({s * .07, -.05, .06}) * sc(.05, .07, .035), hex(0x8A8A90), .01);
    // the head
    const M34 h = head_of(p, f);
    part(r, sphere_mesh(14, 10), h * sc(.17, .16, .16), P.head);
    if (P.face_patch) for (int s = -1; s <= 1; s += 2) flat_part(r, sphere_mesh(8, 6), h * at({s * .1, -.1, -.01}) * sc(.07, .05, .07), P.face);
    // eyes at the sides of the head (parrots look out sideways), blinking
    for (int s = -1; s <= 1; s += 2) {
        const M34 e = h * at({s * .115, -.08, .035});
        flat_part(r, sphere_mesh(8, 6), e * sc(.042, .03, .042), (P.face.r == P.head.r && P.face.g == P.head.g && P.face.b == P.head.b) ? hex(0xF4F0E8) : P.face);
        const double open = 1 - std::clamp(p.blink, 0.0, 1.0);
        if (open > .1) flat_part(r, sphere_mesh(8, 6), e * at({s * .008, -.018, 0}) * sc(.022, .02, .024 * open), kInk);
        else flat_part(r, sphere_mesh(6, 4), e * at({0, -.02, 0}) * sc(.03, .01, .006), kInk);
    }
    // the hooked beak: an upper bill curving down over a lower one; it opens to speak
    const double gape = std::clamp(p.beak, 0.0, 1.0) * .35;
    const M34 bk = h * at({0, -.13, -.02});
    // the player's mark colours the beak itself: green for honest, red for a liar (and a glow while it's fresh)
    const Col mc = p.mark == 1 ? hex(0x48E070) : p.mark == 2 ? hex(0xF03848) : hex(0xFFF0A0);
    const Col top = p.mark ? mix(P.beak_top, mc, .85f) : p.hover ? mix(P.beak_top, mc, .4f) : P.beak_top;
    const Col bot = p.mark ? mix(P.beak_bot, mc, .7f) : P.beak_bot;
    const M34 upper = bk * M34::rot_x(1.95 - gape * .3) * sc(.075, .07, .19);
    part(r, cone_mesh(10), upper, top);
    part(r, sphere_mesh(8, 6), bk * sc(.08, .065, .075), top);
    const M34 lower = bk * at({0, .0, -.05}) * M34::rot_x(1.25 + gape) * sc(.055, .05, .09);
    part(r, cone_mesh(8), lower, bot);
    if (p.mark || p.hover)
        r.billboard(bird_beak(p) - V3{0, 0, .17}, .42 + .3 * p.glow, .34 + .25 * p.glow, &soft_disc(), alpha(mc, static_cast<float>(.12 + .3 * p.glow)), static_cast<std::uint16_t>(additive | unlit | no_depth_write));
    // crests
    if (P.crest_kind == 1) {
        for (int k = -2; k <= 2; ++k)
            part(r, cone_mesh(6), h * at({k * .02, .03, .12}) * M34::rot_x(.6 + .1 * std::sin(t + k)) * M34::rot_y(k * .25) * sc(.03, .02, .17 + .02 * (2 - std::abs(k))), P.crest, .01);
    } else if (P.crest_kind == 2) {
        part(r, sphere_mesh(8, 5), h * at({0, -.02, .14}) * sc(.07, .07, .04), P.crest_kind == 2 && P.crest.r + P.crest.g + P.crest.b > 0 ? P.crest : hex(0xF0D030), .01);
    }
    // what they wear
    switch (p.manner) {
        case Manner::posh:  // a monocle on a chain
            draw_mesh(r, torus_mesh(14, 4, .12), h * at({.11, -.11, .035}) * M34::rot_z(.9) * M34::rot_x(1.57) * sc(.045, .045, .045), nullptr, hex(0xD8B048), unlit);
            break;
        case Manner::salty:  // an eyepatch and a red bandana
            flat_part(r, sphere_mesh(8, 6), h * at({-.12, -.085, .035}) * sc(.04, .025, .04), hex(0x101010));
            draw_mesh(r, torus_mesh(16, 5, .1), h * at({0, .0, .06}) * M34::rot_x(-.3) * sc(.16, .155, .1), nullptr, hex(0xC8242A), toon);
            part(r, sphere_mesh(8, 5), h * at({0, .1, .14}) * sc(.12, .08, .05), hex(0xC8242A), .01);
            break;
        case Manner::gossip:  // a big pink bow
            for (int s = -1; s <= 1; s += 2) part(r, sphere_mesh(8, 5), h * at({s * .06, .02, .15}) * M34::rot_y(s * .5) * sc(.06, .025, .04), hex(0xF070B0), .01);
            part(r, sphere_mesh(6, 4), h * at({0, .02, .155}) * sc(.025, .025, .025), hex(0xF070B0), .01);
            break;
        case Manner::scholar:  // round spectacles
            for (int s = -1; s <= 1; s += 2) draw_mesh(r, torus_mesh(12, 4, .15), h * at({s * .1, -.105, .04}) * M34::rot_z(s * .7) * M34::rot_x(1.57) * sc(.04, .04, .04), nullptr, hex(0x3A3A40), unlit);
            break;
        case Manner::drama:  // a feather boa
            for (int k = 0; k < 9; ++k) {
                const double a = -1.5 + k * .38;
                part(r, sphere_mesh(6, 4), f * at({std::sin(a) * .17, -std::cos(a) * .12 + .02, .54 - .02 * std::fabs(k - 4)}) * sc(.05, .05, .05), k % 2 ? hex(0xB040D0) : hex(0xD060F0), .008);
            }
            break;
        case Manner::sunny:  // a flower behind the ear
            for (int k = 0; k < 5; ++k) part(r, sphere_mesh(6, 4), h * at({.12 + .03 * std::cos(k * 1.26), .03, .1 + .03 * std::sin(k * 1.26)}) * sc(.025, .015, .025), hex(0xF8F0F8), .006);
            part(r, sphere_mesh(6, 4), h * at({.12, .025, .1}) * sc(.02, .02, .02), hex(0xF8C020), .006);
            break;
        case Manner::nervous:  // a woolly scarf
            draw_mesh(r, torus_mesh(16, 6, .25), f * at({0, -.01, .53}) * sc(.15, .14, .12), nullptr, hex(0x60A0E0), toon);
            part(r, box_mesh(), f * at({.06, -.13, .38}) * sc(.035, .015, .12), hex(0x60A0E0), .008);
            break;
        case Manner::grump:  // heavy scowling brows
            for (int s = -1; s <= 1; s += 2) part(r, box_mesh(), h * at({s * .1, -.1, .08}) * M34::rot_y(s * .45) * sc(.045, .015, .012), mix(P.head, kInk, .5f), .006);
            break;
    }
    // a silent bird wears a little padlock on its beak until asked
    if (p.tight_beaked) {
        part(r, box_mesh(), bk * at({0, -.1, -.08}) * sc(.035, .02, .04), hex(0xD8B048), .01);
        draw_mesh(r, torus_mesh(10, 4, .3), bk * at({0, -.1, -.03}) * M34::rot_x(1.57) * sc(.025, .025, .025), nullptr, hex(0xA8A8B0), unlit);
    }
}

}  // namespace pt
