#pragma once
// The chamber: a sealed box of smoked glass and drifting luminous fog in a
// dark lab, ringed by 32 beam emitters, with a side console holding the score
// readout, the atom markers and the lever that opens it. Draws the scene
// (with a soft bloom on everything bright) and answers screen-space picking.
#include "box.hpp"
#include "platform/render.hpp"

#include <array>
#include <vector>

namespace ap {

// A streak of light travelling along a polyline.
struct Bolt {
    std::vector<V3> pts;
    double head = 0;       // distance travelled by the head (world units)
    double speed = 28;     // world units per second
    double tail = 1.4;     // length of the bright streak
    Col col{1, 1, 1, 1};
    bool keep = false;     // leaves a faint line along everything it has travelled (the reveal's replays)
    double fade = 1;       // overall opacity
    double length() const;
    bool arrived() const { return head >= length(); }
};

struct PortView {
    int kind = 0;          // 0 unknown, 1 absorbed, 2 reflected, 3 detour
    int pair = 0;          // the detour's number
    double flash = 0;      // 1 -> 0 after it lights
    double charge = 0;     // 0..1 the barrel powering up
};

Col port_color(const PortView& p);  // the colour of a port's answer

struct ChamberState {
    std::array<PortView, kPorts> ports{};
    int hover_port = -1, hover_cell = -1, partner = -1;
    std::uint64_t marks = 0, empties = 0;
    std::array<double, kN * kN> pop{};  // a marker just placed (1 -> 0)
    double fog = 1;          // fog density (the reveal vents it)
    double glow = 0;         // a beam inside lights the fog from within
    double hit = 0;          // an absorption: a deep violet flash
    double ripple = -1;      // seconds since a beam entered (-1: none)
    V3 ripple_at{};
    double lid = 0;          // 0 sealed .. 1 lifted clear
    double atoms_on = 0;     // 0..1 the atoms appearing
    double judge = 0;        // 0..1 the markers sinking to meet them
    Atoms atoms = 0;
    std::vector<Bolt> bolts;
    double lever = 0;        // 0 up .. 1 pulled
    bool lever_ready = false, lever_hover = false;
    int points = 0, markers_left = kAtoms;
    double boot = 0;         // 0..1 a new box powering up (0 = idle)
    double tray_flash = 0;   // the tray blinks when you try a fifth marker
    double shake = 0;
    double look_x = 0, look_y = 0;  // the camera drifts a little with the pointer (-1..1)
};

class Chamber {
public:
    R3D r;
    void resize(int w, int h);
    void render(const ChamberState& s, double t);

    static constexpr double kBeamZ = .5, kGlassZ = .98, kWall = 4.0, kWallOut = 4.3, kTop = 1.0;
    static V3 cell(double cx, double cy, double z = kBeamZ) { return {cx - 3.5, 3.5 - cy, z}; }
    static V3 outward(int port);
    static V3 hole(int port);     // where a beam passes through the wall
    static V3 muzzle(int port);   // the emitter's barrel tip
    static V3 plate(int port);    // the emitter's status plate (on top of its housing)
    static V3 lever_pivot() { return {7.5, -2.0, .75}; }
    static V3 lever_knob(double pull);
    static V3 tray(int i) { return {6.75 + .5 * i, .45, .75}; }
    static V3 readout() { return {7.5, 2.1, .75}; }
    static constexpr double kConsoleX0 = 6.1, kConsoleX1 = 8.9, kConsoleY0 = -3.6, kConsoleY1 = 3.3;

    void to_screen(V3 p, double& sx, double& sy) const;
    int pick_port(double sx, double sy) const;
    int pick_cell(double sx, double sy) const;
    bool pick_lever(double sx, double sy, double pull) const;
    void look(double x, double y);  // camera parallax

private:
    double base_yaw_ = 0, base_pitch_ = .95;
    V3 base_target_{};
    void sprite(V3 c, double size, const Tex* tex, Col col, std::uint16_t extra = 0);
    void streak(V3 a, V3 b, double width, Col col);
    void draw_room(const ChamberState& s, double t);
    void draw_box(const ChamberState& s, double t);
    void draw_fog(const ChamberState& s, double t);
    void draw_emitters(const ChamberState& s, double t);
    void draw_glass(const ChamberState& s, double t);
    void draw_atoms(const ChamberState& s, double t);
    void draw_console(const ChamberState& s, double t);
    void draw_bolts(const ChamberState& s);
    void bloom();
};

}  // namespace ap
