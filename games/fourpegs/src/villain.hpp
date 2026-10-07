#pragma once
// The villain: an impeccable gentleman in a tailcoat, composed as a butler,
// seated behind his console. Soft primitives with toon light and ink outlines, a
// painted face, IK arms and gloved hands. The Actor decides the pose; this
// file only draws it.
#include "vface.hpp"
#include "platform/r3d.hpp"

#include <array>

namespace fp {

struct VillainPose {
    V3 root{0, 1.0, -.08};   // hip centre in the world (behind and below the desk top)
    double scale = 1.45;
    double yaw = 0;          // body turn about z; 0 faces the player (-y)
    double lean = 0;         // + leans toward the player
    double side = 0;         // + leans toward screen right
    double squash = 1;
    double breathe = 0;      // 0..1 chest rise
    double head_yaw = 0, head_pitch = 0, head_roll = 0;  // pitch + nods toward the player
    std::array<V3, 2> hand{};          // world targets: 0 screen-left hand (his right), 1 screen-right
    std::array<double, 2> reach{};     // 0 rest .. 1 at target
    std::array<V3, 2> rest{V3{-.34, -.6, .8}, V3{.34, -.6, .8}};  // body-local: hands resting on the desk
    std::array<double, 2> curl{};      // 0 flat hand .. 1 fist
    std::array<double, 2> point{};     // 0 .. 1 index finger out
    std::array<V3, 2> palm{V3{0, -1, 0}, V3{0, -1, 0}};  // world direction the fingers point
    std::array<double, 2> palms_in{};  // 0 palms down .. 1 palms facing each other (clapping, steepling)
    VFace face;
    double cape_sway = 0;    // secondary motion of the coat tails
    double watch = 0;        // 0..1 the pocket watch in his left hand
};

struct VArm {
    V3 shoulder, elbow, wrist, hand;
};

M34 villain_body(const VillainPose& p);
M34 villain_head(const VillainPose& p);  // origin at the head centre
V3 villain_head_center(const VillainPose& p);
V3 villain_shoulder(const VillainPose& p, int side);
VArm villain_arm(const VillainPose& p, int side);
constexpr double kVArmReach = 1.0;  // shoulder to hand centre, straight, at scale 1

void draw_villain(R3D& r, const VillainPose& p, double t);

}  // namespace fp
