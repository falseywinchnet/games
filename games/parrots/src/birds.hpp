#pragma once
// The parrots, rigged from primitives with toon light and ink outlines: ten
// species (scarlet and blue-and-gold macaws, a hyacinth, an amazon, a
// cockatoo, an African grey, a budgie, a lovebird, an eclectus, a galah), each
// wearing something that says who they are: a monocle, an eyepatch and
// bandana, a bow, spectacles, a feather boa, a flower, a scarf, a scowl.
// Local space: z up, the bird faces -y (toward the player).
#include "platform/render.hpp"
#include "script.hpp"

#include <cstdint>

namespace pt {

constexpr int kSpecies = 10;
const char* species_name(int s);

struct BirdPose {
    V3 pos{};              // feet, world
    double yaw = 0;        // 0 faces the player
    int species = 0;
    Manner manner = Manner::sunny;
    double bob = 0;        // a little hop
    double head_tilt = 0, head_turn = 0, head_nod = 0;
    double beak = 0;       // 0 shut .. 1 wide open (talking, squawking)
    double wing_l = 0, wing_r = 0;   // 0 folded .. 1 raised (gesturing)
    double blink = 0;      // 0 open .. 1 shut
    double ruffle = 0;     // feathers puffed out (indignant)
    double shake = 0;      // a quick head-shake (no!)
    int mark = 0;          // the player's mark on the beak: 0 none, 1 honest, 2 liar
    bool hover = false;    // the pointer is on the beak
    double glow = 0;       // 0..1 the beak's mark lighting up
    bool tight_beaked = false;  // waits to be asked (a little padlock of silence)
    double fly = 0;        // 0..1 fleeing the table (the culprit getting away)
};

void draw_bird(R3D& r, const BirdPose& p, double t);
V3 bird_beak(const BirdPose& p);   // where the beak is (for clicks and bubbles)
V3 bird_head(const BirdPose& p);

}  // namespace pt
