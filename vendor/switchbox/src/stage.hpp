#pragma once
// The diorama: a toy box on a table with six lamped toggle switches and a
// hinged lid, the girl inside, a fixed camera, and screen-space picking.
#include "girl.hpp"
#include "puzzle.hpp"
#include "platform/r3d.hpp"

#include <array>

namespace sbx {

struct SwitchVis {
    double on = 0;        // lever position: 0 down (toward the player) .. 1 up (flipped)
    double lamp = 0;      // lamp brightness 0..1
    double sink = 0;      // 0 in its socket .. 1 yanked down into the box (ripped out)
    double wiggle = 0;    // a little rattle (a refused click, or about to be yanked)
};

struct Fx {               // cartoon marks around her head, each 0..1
    double anger = 0;     // the red cross-vein mark
    double steam = 0;     // puffs from the top of her head
    double sweat = 0;     // a drop at her temple
    double hearts = 0;    // hearts floating up
    double sparkle = 0;   // stars twinkling around her
};

struct StageState {
    std::array<SwitchVis, kSwitches> sw{};
    double lid = 0;       // 0 closed .. 1 fully open
    GirlPose girl;
    bool girl_visible = true;
    // a switch held by her (stolen or being reinserted): drawn at her hand
    int held = -1;
    int held_hand = 1;
    Fx fx;
    // the in-game pointer, drawn under her hand while the player holds a switch
    bool cursor = false;
    V3 cursor_at{};
};

class Stage {
public:
    R3D r;
    void resize(int w, int h);
    void render(const StageState& s, double t);

    // geometry (world units, z up, the player looks toward +y)
    static constexpr double kBoxX = 2.3, kBoxY = 1.25, kBoxZ = 1.45;
    static constexpr double kOpenY0 = 0, kOpenY1 = 1.1, kOpenX = 2.15;  // the hatch she lives under
    static double switch_x(int i) { return -1.7 + .68 * i; }
    static constexpr double kSwitchY = -.36, kPivotZ = kBoxZ + .05, kLever = .36, kLampY = -.88;
    static V3 knob(int i, double on);   // knob centre for a lever position
    static V3 lamp(int i) { return {switch_x(i), kLampY, kBoxZ + .02}; }
    static M34 lid_frame(double open);  // the lid's model frame

    void to_screen(V3 p, double& sx, double& sy) const;
    int pick_switch(double sx, double sy, const StageState& s) const;  // -1: none
    int pick_socket(double sx, double sy) const;  // the nearest socket, present or not; -1: none
    bool pick_head(double sx, double sy, const GirlPose& g) const;

private:
    void draw_room();
    void draw_box(const StageState& s);
    void draw_switch(int i, const SwitchVis& v, double t);
    void draw_fx(const StageState& s, double t);
};

// Draw a loose switch (lever, knob and its little plug) in an arbitrary frame.
void draw_loose_switch(R3D& r, int i, const M34& frame);

}  // namespace sbx
