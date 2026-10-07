#pragma once
// The crane drawn: a yellow pressed-steel toy truck crane after the 1960s
// mobile cranes, built up from rounded parts. A cab-over truck with deep-dish
// hubs on lugged tyres, a duck at the wheel; outriggers down to the gravel
// (one not quite); a slewing house with a control cab, a winch drum,
// crank handles, stacks and a striped counterweight;
// a telescoping boom whose foot section is black and punched with round
// holes; a ram to luff it; the hoist line over the head sheave down to a
// striped hook block. The truck never moves and is built once; the rest
// is built each frame.
#include "platform/r3d.hpp"

#include <vector>

namespace zc {

// What the operator is doing, for her head in the cab.
enum class OperatorMood { idle, watching, focused, worried, cheering, oops };

// Where the truck stands, found once from the ground under it.
struct CraneSetup {
    M34 chassis;                 // the truck's frame: x forward, y left, z up
    double heading = 0;          // the truck's facing (radians from +x)
    double pad_ground[4] = {0, 0, 0, 0};   // ground height under each outrigger pad
};

// This frame's crane.
struct CranePose {
    V3 hook;                     // where the slings meet, in the bowl of the hook
    double wires = 0;            // 0 reeled in .. 1 let down to the rock
    bool moving = false;         // the motors are running
    double time = 0;
    OperatorMood mood = OperatorMood::idle;
    bool hook_follows = false;   // the hook turns on its swivel with the held rock
    double hook_turn = 0;        // the held rock's heading (radians from +x)
};

class CraneModel {
public:
    CraneModel();
    void set_setup(const CraneSetup& setup);
    const CraneSetup& setup() const { return setup_; }
    void build(const CranePose& pose);

    // this frame's triangles, by how they're drawn
    std::vector<Vtx> paint;      // glossy paint and chrome
    std::vector<Vtx> matte;      // rubber, black steel, cloth, skin
    std::vector<Vtx> striped;    // hazard stripes (stripe_tex)
    std::vector<Vtx> plates;     // the punched boom plates (plate_tex, cut out)
    std::vector<Vtx> glass;      // windows (translucent)
    std::vector<Vtx> lamps;      // lights (unlit)
    Tex stripe_tex, plate_tex;
    // how many vertices at the front of paint, matte and striped are the truck
    // (which never moves; its shadow can be kept)
    size_t truck_paint_count() const { return truck_paint_.size(); }
    size_t truck_matte_count() const { return truck_matte_.size(); }
    size_t truck_striped_count() const { return truck_striped_.size(); }
    V3 beacon_at;                // the beacon's lamp, and how brightly it's sweeping
    double beacon_glow = 0;

private:
    CraneSetup setup_;
    bool built_ = false;
    std::vector<Vtx> truck_paint_, truck_matte_, truck_striped_, truck_glass_, truck_lamps_;
    void build_truck();
    void add_wheel(const M34& frame);
    void add_cab();
    void add_outrigger(int index, double x, double side);
    void add_duck(const CranePose& pose);
    void add_house(const M34& turret, const CranePose& pose, double hoist_angle, double luff_angle);
    void add_operator(const M34& turret, const CranePose& pose);
    void add_hook_block(V3 hook, double slew, V3 line_top, double hook_turn);
};

}  // namespace zc
