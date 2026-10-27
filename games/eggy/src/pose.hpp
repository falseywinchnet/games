#pragma once
// Animation pose for Eggy and the summit officer (shared by animation and the 3D model).
#include "render.hpp"

namespace eggy {

enum class Eyes { open, blink, happy, dizzy, squint, sleepy, wide };

struct Pose {
    double bob = 0, sx = 1, sy = 1, tilt = 0, lean = 0;
    double yaw = -.35;          // 0 = profile facing right, + toward camera, - away
    double head_yaw = 0;        // extra head turn
    double head_dx = 0, head_dy = 0, head_tilt = 0;
    double beak = 0;            // 0 closed .. 1 wide open
    Eyes eyes = Eyes::open;
    double wing_near = 0, wing_far = 0;  // raise, radians-ish 0..1.8
    double wing_fwd = 0;        // near wing reaching forward (fan, salute)
    double step = 0, stepamp = 0;
    double sit = 0, lie = 0, swim = 0;
    double puff = 1;
    double blush = .55;
    double helmet_tilt = 0, helmet_lift = 0;
    double look_up = 0;         // eye gaze offset
    bool medal = false;
    bool officer = false;       // the summit duck: cap instead of acorn, slightly taller
    double scale_mul = 1;
};

void draw_duck3d(R3D& r, double x, double y, double z, double heading, const Pose& p, double t);

}  // namespace eggy
