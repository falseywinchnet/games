#pragma once
// The cast, rigged from primitives with toon light and ink outlines: the
// gardener bear (straw hat, blue overalls) and the raccoon bandits (grey,
// masked, ringed tails). Their faces are painted textures over the front of
// the head, so an expression is a drawing swapped instantly; each combination
// is painted once and cached. Local space: z up, the critter faces -y.
#include "platform/render.hpp"

#include <cstdint>

namespace ct {

enum class BEyes : std::uint8_t { open, happy, closed, wide, squint, worried, shut_tight, dizzy };
enum class BMouth : std::uint8_t { smile, grin, open, flat, o, frown, effort, gasp };
enum class CEyes : std::uint8_t { open, sly, laugh, wide, teary, dizzy, closed, angry };
enum class CMouth : std::uint8_t { smirk, grin, laugh, raspberry, o, frown, wobble, grit };

struct BearFace {
    BEyes eyes = BEyes::open;
    BMouth mouth = BMouth::smile;
    int brow = 0;        // 0 none, 1 raised, 2 worried, 3 determined
    int blush = 0;
    std::uint32_t key() const { return static_cast<std::uint32_t>(eyes) | static_cast<std::uint32_t>(mouth) << 4 | static_cast<std::uint32_t>(brow) << 8 | static_cast<std::uint32_t>(blush) << 12; }
};
struct CoonFace {
    CEyes eyes = CEyes::open;
    CMouth mouth = CMouth::smirk;
    int brow = 0;        // 0 none, 1 cocky, 2 worried, 3 angry
    std::uint32_t key() const { return static_cast<std::uint32_t>(eyes) | static_cast<std::uint32_t>(mouth) << 4 | static_cast<std::uint32_t>(brow) << 8; }
};

struct Limb {
    V3 hand{};   // where the paw is, relative to the body origin (local space, before facing)
};

struct BearPose {
    V3 pos{};            // feet, world
    double yaw = 0;      // facing: 0 toward the camera (-y), then +pi/2 toward +x...
    double bob = 0;      // vertical bounce
    double lean = 0;     // forward lean (radians), e.g. when pushing
    double squash = 0;   // -1..1 squash (landing) / stretch (hop)
    double walk = 0;     // walk cycle phase (radians); 0 standing
    double head_tilt = 0, head_turn = 0, head_nod = 0;
    double hat_tilt = 0, hat_lift = 0;
    Limb left, right;    // paw targets (local); the defaults hang at the sides
    bool facepalm = false;
    BearFace face;
};

struct CoonPose {
    V3 pos{};            // the burrow centre, world (ground level)
    double rise = 0;     // how far out of the burrow: 0 hidden, 1 shoulders out, 1.6 standing on the rim
    double yaw = 0;
    double sway = 0, bounce = 0, head_tilt = 0, head_turn = 0;
    Limb left, right;
    int prop = 0;        // 0 none, 1 a stolen carrot, 2 juggling carrots, 3 a white flag, 4 a turnip
    double prop_t = 0;   // animation clock for the prop
    double tail = 0;     // tail wag phase
    CoonFace face;
};

void draw_bear(R3D& r, const BearPose& p, double t);
void draw_raccoon(R3D& r, const CoonPose& p, double t);
// A paw poking out from under a pumpkin (a trapped raccoon), and its muffled wiggle.
void draw_trapped_paw(R3D& r, V3 at, double yaw, double wiggle);

V3 bear_head_center(const BearPose& p);
V3 coon_head_center(const CoonPose& p);

}  // namespace ct
