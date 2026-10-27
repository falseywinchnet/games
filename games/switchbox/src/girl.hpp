#pragma once
// The girl in the box: a chibi figure built from soft primitives with toon
// light and ink outlines, a painted face, and IK arms. Everything she does is
// a GirlPose; the Actor decides the pose, this file only draws it.
#include "face.hpp"
#include "platform/render.hpp"

#include <array>

namespace sbx {

struct GirlPose {
    V3 root{0, .5, .8};     // hip centre in the world
    double scale = 1.4;      // overall size (the rig is modelled at 1)
    double yaw = 0;          // turn about z; 0 faces the player (-y)
    double lean = 0;         // forward lean (radians), + toward the player
    double side = 0;         // sideways lean (radians), + toward screen right
    double squash = 1;       // >1 stretched tall, <1 squashed (volume kept)
    double head_yaw = 0, head_pitch = 0, head_roll = 0;  // pitch + nods toward the player
    std::array<V3, 2> hand{};          // world hand targets: 0 screen-left hand, 1 screen-right hand
    std::array<double, 2> reach{};     // 0 rest pose .. 1 hand at its target
    std::array<V3, 2> rest{V3{-.13, -.33, .5}, V3{.13, -.33, .5}};  // body-local rest hands (paws up)
    std::array<double, 2> fist{};      // 0 open mitten .. 1 balled up
    std::array<double, 2> point{};     // 0 .. 1 index finger out
    Face face;
    V3 hair_lag{};           // secondary motion: hair offset opposite recent head motion (world units)
    double ahoge = 0;        // the antenna strand's sway (radians)
    double shake = 0;        // a quick angry trembling amount
};

struct ArmPose {
    V3 shoulder, elbow, wrist, hand;
};

M34 body_frame(const GirlPose& p);
M34 head_frame(const GirlPose& p);   // origin at the head centre, unit = world units
V3 head_center(const GirlPose& p);
V3 shoulder_world(const GirlPose& p, int side);
ArmPose solve_arm(const GirlPose& p, int side);
constexpr double kArmReach = .58;    // shoulder to hand centre, fully straight, at scale 1

void draw_girl(R3D& r, const GirlPose& p, double t);

}  // namespace sbx
