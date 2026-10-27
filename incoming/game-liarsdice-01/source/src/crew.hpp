#pragma once
// The crew in 3D: eight kinds of drowned gambler (crab, octopus, skeleton,
// grouper, eel, turtle, shark, ghost), four looks of each, built from toon-
// shaded primitives like the rest of the cabinet's figures. A pose drives
// them: mouth, blink, head turn, a glance down at their own cup, leaning
// back, arms raised or tapping, swaying, a flush of colour, a twitch. A tell
// is just one of those, played small and briefly, after a bid.
#include "brain.hpp"
#include "platform/r3d.hpp"

namespace ld {

struct CrewPose {
    V3 pos;               // the seat, on the floor
    double yaw = 0;       // turned toward the table's centre
    int who = 0;          // cast index
    double mouth = 0;     // 0..1 open (talking)
    double blink = 0;     // 0..1 closed
    double head_turn = 0; // radians, + to their left
    double head_down = 0; // 0..1 a glance down at their own cup
    double lean = 0;      // -1..1, + back in the chair, - forward over the table
    double arm_l = 0, arm_r = 0;  // 0..1 raised
    double tap = 0;       // 0..1 a tapping hand (oscillates with time)
    double sway = 0;      // 0..1 swaying side to side
    double flush = 0;     // 0..1 colour darkening toward red
    double twitch = 0;    // 0..1 a twitch of something kind-specific (an eyestalk, a fin, the jaw)
    double bob = 0;       // 0..1 a little hop (a cheer)
    double slump = 0;     // 0..1 dejected
    double fade = 0;      // 0..1 out of the game: they drift back and dim
    double hover = 0;     // highlighted
};

void apply_tell(CrewPose& p, Tell tell, double amount, double t);  // add a tell, `amount` 0..1
void draw_crew(R3D& r, const CrewPose& p, double t);
V3 crew_head(const CrewPose& p);   // where speech comes from
V3 crew_mouth(const CrewPose& p);  // where bubbles come from

}  // namespace ld
