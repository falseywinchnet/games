#pragma once
// The pasture in 3D: a hex meadow of grass patches on a hillside, the sheep,
// fences (old weathered ones already standing, fresh ones you put up; every
// post joins its neighbours with rails), clover in flower, trees and hills beyond, clouds and
// birds overhead, butterflies over the grass. Seen from a little south and
// above, sunny and soft.
#include "field.hpp"
#include "platform/r3d.hpp"

#include <vector>

namespace sh {

struct SheepPose {
    V3 pos;               // where it stands
    double yaw = 0;       // facing (radians; 0 = toward the camera)
    double hop = 0;       // 0..1 height of a hop
    double chew = 0;      // 0..1 jaw
    double head_down = 0; // 0..1 grazing
    double head_turn = 0; // radians
    double blink = 0;
    double ear = 0;       // an ear flick
    double sit = 0;       // 0..1 sat down sulking
    double sleep = 0;     // 0..1 eyes closed, lying lower
    double startle = 0;   // 0..1 a little jump of surprise
    double run = 0;       // legs scampering
    double fade = 1;      // 1 visible
};

struct PastureState {
    const Meadow* meadow = nullptr;
    SheepPose sheep;
    int hover = -1;            // the patch under the pointer
    bool hover_ok = false;     // a stone could go there
    int hint = -1;             // a suggested patch, pulsing
    std::vector<double> drop;  // per cell: 0..1 a stone just placed (falls in)
    std::vector<int> exit_path; // the sheep's shortest way out (shown faintly when asked)
    double celebrate = 0;      // 0..1 a win
};

class Pasture {
public:
    R3D r;
    void resize(int w, int h, int top_bar, int bottom_bar);
    void frame(int size);                       // fit the camera to a size x size meadow
    V3 cell_pos(int cell) const;                // the centre of a patch, on the ground
    int pick(double sx, double sy) const;       // the patch under a screen point (-1 none)
    void render(const PastureState& s, double t);
    void to_screen(V3 p, double& sx, double& sy) const;

private:
    int w_ = 0, h_ = 0, top_ = 0, bottom_ = 0, n_ = 11;
    void draw_land(double t);
    void draw_patches(const PastureState& s, double t);
    void draw_sheep(const SheepPose& p, double t);
    void draw_fence(const Meadow& m, int cell, V3 c, bool old, float ghost);
};

}  // namespace sh
