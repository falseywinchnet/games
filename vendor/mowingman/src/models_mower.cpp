// The zero-turn mower: big turf tyres at the back, casters at the front, the cutting
// deck slung between with its discharge chute on the right, the body over the drive
// with its two fender consoles, a high-backed seat, the two lap bars, the roll bar,
// and the V-twin under its hood behind. Eggy rides in his acorn-cap helmet.
//
// Mower metres: x forward, y to the right, z up, the origin on the ground under the
// middle of the deck.
#include "models.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace mm {
namespace {

constexpr double pi = 3.14159265358979323846;

enum MowerMaterial : std::uint16_t {
    body_paint,
    deck_steel,
    frame_black,
    plastic,
    tyre,
    caster_tyre,
    rim,
    chrome,
    seat,
    grip,
    plate,
    grille,
    badge,
    badge_disc,
    engine_alu,
    engine_black,
    lens,
    muffler,
    knob,
    wire,
    ceramic,
    decal,
    down,
    beak,
    eye,
    cup,
    stem,
    strap,
    belt,
    material_count
};

struct MowerModel {
    Mesh body{};
    Mesh wings{};
    Mesh head{};
    Mesh shade{};  // the body made coarsely, to throw shadows
};

// A wheel with its axle along y, centred at `c`; `outward` is +1 for a wheel on the right.
void wheel(Builder& b, const Xform& base, V3 c, double radius, double width, double rim_radius, double outward, bool drive) {
    const double hw = width / 2;
    // Turn the lathe's z (the axle) to point outwards along y.
    b.at = base * translation(c) * rotation_x(outward > 0 ? -pi / 2 : pi / 2);
    const double R = radius;
    const double rb = rim_radius;
    b.material = drive ? tyre : caster_tyre;
    // The tyre's section from the inner bead over the tread to the outer bead.
    const std::vector<double> tr{rb, R * 0.80, R * 0.93, R * 0.985, R, R, R, R, R, R * 0.985, R * 0.93, R * 0.80, rb};
    const std::vector<double> tz{-hw * 0.92, -hw, -hw * 0.96, -hw * 0.84, -hw * 0.62, -hw * 0.3, 0, hw * 0.3, hw * 0.62, hw * 0.84, hw * 0.96, hw, hw * 0.92};
    b.lathe(tr, tz, b.sides(R, 24, 120));
    // The rim: its barrel, the outer lip, the dish stepping in to the hub, the cap.
    b.material = rim;
    const std::vector<double> rr{rb + 0.008, rb - 0.004, rb - 0.004, rb + 0.01, rb + 0.01, rb - 0.012, rb * 0.72, rb * 0.5, rb * 0.42, rb * 0.36, rb * 0.24, 0.0};
    const std::vector<double> rz{-hw * 0.86, -hw * 0.8, hw * 0.72, hw * 0.84, hw * 0.92, hw * 0.88, hw * 0.55, hw * 0.48, hw * 0.66, hw * 0.7, hw * 0.84, hw * 0.88};
    b.lathe(rr, rz);
    // Lug nuts round the hub, and the valve stem.
    b.material = chrome;
    const int lugs = drive ? 5 : 4;
    for (int k = 0; k < lugs; ++k) {
        const double a = 2 * pi * k / lugs + 0.3;
        const double rl = rb * 0.46;
        b.cylinder(V3{std::cos(a) * rl, std::sin(a) * rl, hw * 0.6}, V3{std::cos(a) * rl, std::sin(a) * rl, hw * 0.74}, rb * 0.075, rb * 0.075, true, 6);
    }
    b.cylinder(V3{rb * 0.8, 0, hw * 0.6}, V3{rb * 0.86, 0, hw * 0.8}, 0.004, 0.003, true, 6);
}

void build_mower(MowerModel& model, double ppm, bool bake) {
    Mesh& m = model.body;
    Builder b(m, ppm);
    const Xform base{};

    // Drive tyres and casters.
    for (int side = -1; side <= 1; side += 2) {
        wheel(b, base, V3{-0.55, side * 0.47, 0.22}, 0.22, 0.17, 0.125, side, true);
        wheel(b, base, V3{0.555, side * 0.42, 0.11}, 0.11, 0.075, 0.062, side, false);
    }
    b.at = base;

    // Caster forks: two plates down to the axle, the yoke across the top, the swivel
    // stem up into its housing on the frame.
    for (int side = -1; side <= 1; side += 2) {
        const double y = side * 0.42;
        b.material = frame_black;
        for (int plate_side = -1; plate_side <= 1; plate_side += 2) {
            b.sweep({V3{0.555, y + plate_side * 0.05, 0.11}, V3{0.575, y + plate_side * 0.05, 0.19}, V3{0.6, y + plate_side * 0.05, 0.245}},
                    {0.022, 0.024, 0.026}, {0.005, 0.005, 0.005}, 6, V3{1, 0, 0});
        }
        b.rounded_box(V3{0.6, y, 0.255}, V3{0.035, 0.06, 0.012}, 0.008);
        b.material = chrome;
        b.cylinder(V3{0.555, y - 0.058, 0.11}, V3{0.555, y + 0.058, 0.11}, 0.009, 0.009, true, 8);
        b.cylinder(V3{0.6, y, 0.26}, V3{0.6, y, 0.33}, 0.012, 0.012, true, 10);
        b.material = deck_steel;
        b.cylinder(V3{0.6, y, 0.3}, V3{0.6, y, 0.43}, 0.034, 0.034, true);
        b.material = plastic;
        b.cylinder(V3{0.6, y, 0.43}, V3{0.6, y, 0.445}, 0.03, 0.026, true);
    }

    // The frame: two rails from bumper to bumper, rising to the caster housings in front.
    b.material = frame_black;
    for (int side = -1; side <= 1; side += 2) {
        b.rounded_box(V3{-0.2, side * 0.3, 0.285}, V3{0.72, 0.024, 0.036}, 0.01);
        b.tube({V3{0.48, side * 0.3, 0.29}, V3{0.55, side * 0.33, 0.33}, V3{0.6, side * 0.39, 0.36}}, {0.022, 0.022, 0.022}, true);
    }
    b.tube({V3{0.6, -0.42, 0.37}, V3{0.68, -0.36, 0.37}, V3{0.71, -0.26, 0.37}, V3{0.71, 0.26, 0.37}, V3{0.68, 0.36, 0.37}, V3{0.6, 0.42, 0.37}},
           {0.021, 0.021, 0.021, 0.021, 0.021, 0.021}, true);
    // rear bumper and the hitch plate
    b.tube({V3{-0.9, -0.3, 0.29}, V3{-0.99, -0.26, 0.29}, V3{-1.0, 0, 0.29}, V3{-0.99, 0.26, 0.29}, V3{-0.9, 0.3, 0.29}}, {0.02, 0.02, 0.02, 0.02, 0.02}, true);
    b.rounded_box(V3{-1.01, 0, 0.25}, V3{0.03, 0.06, 0.006}, 0.004);

    // The footplate: diamond plate, tipped up towards the toe, with its turned-up lip.
    b.material = plate;
    b.at = base * translation(V3{0.33, 0, 0.335}) * rotation_y(-0.12);
    b.rounded_box(V3{0, 0, 0}, V3{0.24, 0.28, 0.01}, 0.008);
    b.at = base * translation(V3{0.575, 0, 0.39}) * rotation_y(-1.1);
    b.rounded_box(V3{0, 0, 0}, V3{0.05, 0.28, 0.008}, 0.006);
    b.at = base;

    // The deck: a pressed steel shell, its rolled front lip, spindle housings with their
    // pulleys, the belt shields, anti-scalp rollers, the chute.
    b.material = deck_steel;
    b.rounded_box(V3{0, 0, 0.11}, V3{0.36, 0.53, 0.07}, 0.06);
    b.tube({V3{0.34, -0.5, 0.075}, V3{0.37, -0.42, 0.075}, V3{0.38, 0, 0.075}, V3{0.37, 0.42, 0.075}, V3{0.34, 0.5, 0.075}}, {0.016, 0.016, 0.016, 0.016, 0.016});
    // a pressed rib round the top
    b.at = base * translation(V3{0, 0, 0.176});
    b.rounded_box(V3{0.02, 0, 0}, V3{0.27, 0.44, 0.012}, 0.03);
    b.at = base;
    const double spindles[3][2] = {{0.07, -0.3}, {0.13, 0.0}, {0.07, 0.3}};
    for (const double* s : spindles) {
        b.material = deck_steel;
        b.cylinder(V3{s[0], s[1], 0.17}, V3{s[0], s[1], 0.215}, 0.055, 0.05, true);
        b.material = plastic;
        b.cylinder(V3{s[0], s[1], 0.215}, V3{s[0], s[1], 0.226}, 0.066, 0.066, true);
        b.material = belt;
        b.cylinder(V3{s[0], s[1], 0.226}, V3{s[0], s[1], 0.236}, 0.06, 0.06, false);
        b.material = plastic;
        b.cylinder(V3{s[0], s[1], 0.236}, V3{s[0], s[1], 0.246}, 0.066, 0.066, true);
        b.material = chrome;
        b.cylinder(V3{s[0], s[1], 0.246}, V3{s[0], s[1], 0.262}, 0.014, 0.012, true, 6);
    }
    // the drive belt from the pulleys back towards the engine
    b.material = belt;
    b.sweep({V3{0.07, -0.3, 0.231}, V3{0.13, 0.0, 0.231}, V3{0.07, 0.3, 0.231}}, {0.006, 0.006, 0.006}, {0.005, 0.005, 0.005}, 4, V3{0, 0, 1});
    b.sweep({V3{0.07, 0.0, 0.231}, V3{-0.15, 0.0, 0.26}, V3{-0.4, 0.0, 0.3}}, {0.006, 0.006, 0.006}, {0.005, 0.005, 0.005}, 4, V3{0, 0, 1});
    // the belt shields over the outer spindles
    b.material = plastic;
    for (int side = -1; side <= 1; side += 2) {
        b.at = base * translation(V3{0.05, side * 0.31, 0.215}) * rotation_z(side * 0.25);
        b.rounded_box(V3{0, 0, 0}, V3{0.1, 0.11, 0.035}, 0.03);
        b.at = base;
    }
    // anti-scalp rollers at the deck's front corners
    for (int side = -1; side <= 1; side += 2) {
        b.material = plastic;
        b.cylinder(V3{0.33, side * 0.535, 0.045}, V3{0.33, side * 0.565, 0.045}, 0.04, 0.04, true);
        b.material = rim;
        b.cylinder(V3{0.33, side * 0.533, 0.045}, V3{0.33, side * 0.567, 0.045}, 0.016, 0.016, true, 8);
        b.material = deck_steel;
        b.rounded_box(V3{0.3, side * 0.54, 0.075}, V3{0.05, 0.008, 0.03}, 0.006);
    }
    // the discharge chute on the right, sloping down and out
    b.material = plastic;
    b.sweep({V3{0.04, 0.5, 0.125}, V3{0.05, 0.58, 0.112}, V3{0.06, 0.66, 0.096}, V3{0.065, 0.72, 0.082}}, {0.17, 0.165, 0.155, 0.145},
            {0.05, 0.045, 0.04, 0.034}, 5, V3{1, 0, 0});
    for (int rib = -1; rib <= 1; ++rib)
        b.sweep({V3{0.05 + rib * 0.08, 0.52, 0.177}, V3{0.06 + rib * 0.075, 0.66, 0.138}, V3{0.065 + rib * 0.07, 0.715, 0.118}}, {0.006, 0.006, 0.006},
                {0.008, 0.008, 0.006}, 4, V3{1, 0, 0});
    // the deck lift lever by the right console
    b.material = frame_black;
    b.tube({V3{0.0, 0.34, 0.2}, V3{-0.05, 0.36, 0.38}, V3{-0.1, 0.4, 0.56}, V3{-0.12, 0.41, 0.6}}, {0.009, 0.009, 0.009, 0.009});
    b.material = grip;
    b.ellipsoid(V3{-0.125, 0.41, 0.615}, V3{0.022, 0.02, 0.028});

    // The drive: the transaxles by the rear wheels, with their cooling fans.
    for (int side = -1; side <= 1; side += 2) {
        b.material = engine_alu;
        b.rounded_box(V3{-0.55, side * 0.27, 0.24}, V3{0.08, 0.06, 0.075}, 0.02);
        b.material = engine_black;
        b.cylinder(V3{-0.55, side * 0.27, 0.315}, V3{-0.55, side * 0.27, 0.335}, 0.05, 0.05, true);
        b.material = chrome;
        b.cylinder(V3{-0.55, side * 0.27, 0.22}, V3{-0.55, side * 0.4, 0.22}, 0.018, 0.018, true, 8);
    }

    // The body: the tub under the seat, fenders arched over the tyres, the console
    // bases, painted.
    b.material = body_paint;
    b.rounded_box(V3{-0.44, 0, 0.43}, V3{0.36, 0.3, 0.115}, 0.07);
    for (int side = -1; side <= 1; side += 2) {
        std::vector<V3> arc{};
        std::vector<double> wide{};
        std::vector<double> thick{};
        for (int k = 0; k <= 16; ++k) {
            const double a = (12.0 + (170.0 - 12.0) * k / 16) * pi / 180;
            arc.push_back(V3{-0.55 + 0.27 * std::cos(a), side * 0.47, 0.22 + 0.27 * std::sin(a)});
            wide.push_back(0.118);
            thick.push_back(0.016);
        }
        b.sweep(arc, wide, thick, 6, V3{0, 1, 0});
        // a rolled edge on the fender's outer side
        std::vector<V3> lip{};
        std::vector<double> lip_r{};
        for (int k = 0; k <= 16; ++k) {
            const double a = (12.0 + (170.0 - 12.0) * k / 16) * pi / 180;
            lip.push_back(V3{-0.55 + 0.27 * std::cos(a), side * 0.588, 0.22 + 0.27 * std::sin(a)});
            lip_r.push_back(0.012);
        }
        b.tube(lip, lip_r, true);
        // the console base over each fender, and the tank under it
        b.rounded_box(V3{-0.42, side * 0.45, 0.5}, V3{0.21, 0.135, 0.045}, 0.035);
    }
    // the fuel caps
    for (int side = -1; side <= 1; side += 2) {
        b.material = engine_black;
        b.cylinder(V3{-0.57, side * 0.47, 0.54}, V3{-0.57, side * 0.47, 0.565}, 0.034, 0.032, true);
        b.material = plastic;
        b.rounded_box(V3{-0.57, side * 0.47, 0.57}, V3{0.012, 0.03, 0.008}, 0.005);
    }
    // stripe decal along each fender console base
    b.material = decal;
    for (int side = -1; side <= 1; side += 2)
        b.rounded_box(V3{-0.42, side * 0.586, 0.5}, V3{0.17, 0.002, 0.012}, 0.002);

    // The consoles: black mouldings with the controls.
    for (int side = -1; side <= 1; side += 2) {
        b.material = plastic;
        b.rounded_box(V3{-0.3, side * 0.45, 0.56}, V3{0.15, 0.105, 0.03}, 0.025);
        // a headlight in the nose of each console
        b.material = chrome;
        b.cylinder(V3{-0.165, side * 0.45, 0.555}, V3{-0.148, side * 0.45, 0.555}, 0.027, 0.027, true);
        b.material = lens;
        b.cylinder(V3{-0.15, side * 0.45, 0.555}, V3{-0.144, side * 0.45, 0.555}, 0.021, 0.019, true);
    }
    // left console: the key, the throttle in its slot, the choke
    b.material = engine_black;
    b.rounded_box(V3{-0.28, -0.47, 0.592}, V3{0.07, 0.012, 0.004}, 0.003);
    b.material = chrome;
    b.cylinder(V3{-0.4, -0.41, 0.588}, V3{-0.4, -0.41, 0.598}, 0.018, 0.016, true);
    b.material = engine_black;
    b.rounded_box(V3{-0.4, -0.41, 0.606}, V3{0.004, 0.014, 0.01}, 0.003);
    b.material = chrome;
    b.tube({V3{-0.27, -0.47, 0.59}, V3{-0.25, -0.47, 0.63}}, {0.005, 0.005});
    b.material = engine_black;
    b.ellipsoid(V3{-0.247, -0.47, 0.64}, V3{0.016, 0.014, 0.014});
    b.material = plastic;
    b.cylinder(V3{-0.36, -0.5, 0.588}, V3{-0.36, -0.5, 0.605}, 0.011, 0.011, true);
    // right console: the yellow blade knob, the hour meter, a cup holder
    b.material = knob;
    b.cylinder(V3{-0.37, 0.42, 0.588}, V3{-0.37, 0.42, 0.608}, 0.02, 0.018, false);
    b.ellipsoid(V3{-0.37, 0.42, 0.608}, V3{0.02, 0.02, 0.01});
    b.material = chrome;
    b.cylinder(V3{-0.27, 0.41, 0.588}, V3{-0.27, 0.41, 0.594}, 0.024, 0.024, true);
    b.material = lens;
    b.cylinder(V3{-0.27, 0.41, 0.594}, V3{-0.27, 0.41, 0.596}, 0.019, 0.019, true);
    b.material = plastic;
    b.torus(V3{-0.22, 0.49, 0.592}, 0.042, 0.008);
    b.material = engine_black;
    b.cylinder(V3{-0.22, 0.49, 0.55}, V3{-0.22, 0.49, 0.592}, 0.04, 0.04, false);

    // The seat: its pan, the cushion, the high back with its pad, the armrests.
    b.material = plastic;
    b.rounded_box(V3{-0.33, 0, 0.562}, V3{0.19, 0.205, 0.018}, 0.012);
    b.material = seat;
    b.loft({Builder::Section{-0.505, 0.18, 0.025, 0.618, 3.0}, Builder::Section{-0.49, 0.205, 0.042, 0.622, 3.2},
            Builder::Section{-0.4, 0.212, 0.048, 0.626, 3.4}, Builder::Section{-0.25, 0.214, 0.05, 0.628, 3.4},
            Builder::Section{-0.16, 0.208, 0.046, 0.626, 3.2}, Builder::Section{-0.13, 0.19, 0.03, 0.62, 3.0},
            Builder::Section{-0.122, 0.16, 0.012, 0.616, 2.6}});
    b.at = base * translation(V3{-0.5, 0, 0.62}) * rotation_y(-0.2);
    b.material = plastic;
    b.rounded_box(V3{-0.02, 0, 0.26}, V3{0.04, 0.2, 0.25}, 0.04);
    b.material = seat;
    b.rounded_box(V3{0.03, 0, 0.27}, V3{0.024, 0.175, 0.22}, 0.022);
    // a head pad at the top
    b.rounded_box(V3{0.035, 0, 0.47}, V3{0.026, 0.12, 0.045}, 0.02);
    b.at = base;
    for (int side = -1; side <= 1; side += 2) {
        b.material = seat;
        b.rounded_box(V3{-0.34, side * 0.24, 0.71}, V3{0.13, 0.028, 0.022}, 0.018);
        b.material = frame_black;
        b.tube({V3{-0.42, side * 0.25, 0.69}, V3{-0.43, side * 0.27, 0.6}}, {0.01, 0.01});
    }

    // The lap bars: up from their pivots beside the seat, forward, and in across the lap.
    for (int side = -1; side <= 1; side += 2) {
        b.material = frame_black;
        b.rounded_box(V3{-0.2, side * 0.365, 0.575}, V3{0.035, 0.022, 0.03}, 0.01);
        b.tube({V3{-0.2, side * 0.365, 0.58}, V3{-0.18, side * 0.36, 0.7}, V3{-0.13, side * 0.345, 0.79}, V3{-0.07, side * 0.31, 0.83}, V3{-0.045, side * 0.25, 0.84},
                V3{-0.04, side * 0.09, 0.84}},
               {0.012, 0.012, 0.012, 0.012, 0.012, 0.012});
        b.material = grip;
        b.tube({V3{-0.042, side * 0.235, 0.84}, V3{-0.04, side * 0.16, 0.84}, V3{-0.04, side * 0.085, 0.84}}, {0.019, 0.02, 0.019});
        // the damper below each pivot
        b.material = chrome;
        b.tube({V3{-0.2, side * 0.39, 0.55}, V3{-0.3, side * 0.39, 0.5}}, {0.007, 0.007});
    }

    // The roll bar: a bent tube over the back of the seat with its folding knuckles.
    {
        std::vector<V3> path{};
        std::vector<double> radius{};
        path.push_back(V3{-0.64, -0.41, 0.46});
        path.push_back(V3{-0.64, -0.41, 0.9});
        path.push_back(V3{-0.65, -0.41, 1.24});
        for (int k = 1; k <= 6; ++k) {
            const double a = pi / 2 * k / 6;
            path.push_back(V3{-0.65, -0.33 - 0.08 * std::cos(a), 1.24 + 0.08 * std::sin(a)});
        }
        for (int k = 0; k <= 6; ++k) {
            const double a = pi / 2 - pi / 2 * k / 6;
            path.push_back(V3{-0.65, 0.33 + 0.08 * std::cos(a), 1.24 + 0.08 * std::sin(a)});
        }
        path.push_back(V3{-0.64, 0.41, 0.9});
        path.push_back(V3{-0.64, 0.41, 0.46});
        // remove the two middle points that coincide
        std::vector<V3> clean{};
        for (const V3& p : path)
            if (clean.empty() || length(p - clean.back()) > 1e-4)
                clean.push_back(p);
        radius.assign(clean.size(), 0.024);
        b.material = frame_black;
        b.tube(clean, radius, true);
        for (int side = -1; side <= 1; side += 2) {
            b.rounded_box(V3{-0.64, side * 0.41, 0.95}, V3{0.035, 0.035, 0.05}, 0.01);
            b.material = chrome;
            b.cylinder(V3{-0.64, side * 0.37, 0.95}, V3{-0.64, side * 0.45, 0.95}, 0.009, 0.009, true, 8);
            b.material = frame_black;
            b.rounded_box(V3{-0.64, side * 0.41, 0.46}, V3{0.05, 0.05, 0.012}, 0.006);
        }
    }

    // The engine: on its plate behind the seat, the crankcase, two finned cylinders
    // leaning out to each side, their heads and plugs, the muffler and its shield.
    b.material = deck_steel;
    b.rounded_box(V3{-0.82, 0, 0.335}, V3{0.17, 0.27, 0.014}, 0.008);
    b.material = engine_alu;
    b.rounded_box(V3{-0.81, 0, 0.42}, V3{0.11, 0.12, 0.075}, 0.03);
    for (int side = -1; side <= 1; side += 2) {
        const V3 root{-0.81, side * 0.07, 0.47};
        const V3 axis = normalized(V3{0, side * 0.72, 0.69});
        b.material = engine_alu;
        b.cylinder(root, root + axis * 0.15, 0.048, 0.046, false);
        for (int fin = 0; fin < 8; ++fin) {
            const V3 at = root + axis * (0.03 + fin * 0.015);
            b.cylinder(at, at + axis * 0.005, 0.075 - fin * 0.0012, 0.075 - fin * 0.0012, true);
        }
        b.material = engine_black;
        b.at = base * aimed(root + axis * 0.17, axis);
        b.rounded_box(V3{0, 0, 0}, V3{0.035, 0.07, 0.065}, 0.02);
        b.at = base;
        // spark plug, its boot and lead
        const V3 plug = root + axis * 0.2 + V3{0.04, 0, 0};
        b.material = ceramic;
        b.cylinder(plug, plug + axis * 0.03, 0.007, 0.007, true, 8);
        b.material = wire;
        b.cylinder(plug + axis * 0.03, plug + axis * 0.055, 0.01, 0.009, true, 8);
        b.tube({plug + axis * 0.055, plug + axis * 0.065 + V3{0.02, -side * 0.02, 0.02}, V3{-0.72, side * 0.12, 0.6}, V3{-0.74, side * 0.06, 0.58}},
               {0.004, 0.004, 0.004, 0.004}, true, 6);
    }
    // the blower housing over the flywheel, under the hood
    b.material = engine_black;
    b.lathe({0.0, 0.13, 0.135, 0.13, 0.0}, {0.5, 0.5, 0.53, 0.56, 0.565});
    // muffler across the back, its heat shield and tailpipe
    b.material = muffler;
    b.cylinder(V3{-0.98, -0.2, 0.43}, V3{-0.98, 0.2, 0.43}, 0.055, 0.055, true);
    b.material = grille;
    b.at = base * translation(V3{-0.98, 0, 0.43}) * rotation_x(-pi / 2);
    b.lathe({0.068, 0.068}, {-0.17, 0.17}, 0, pi * 0.55, pi * 1.45);
    b.at = base;
    b.material = chrome;
    b.tube({V3{-0.98, 0.18, 0.4}, V3{-1.02, 0.22, 0.36}, V3{-1.04, 0.24, 0.33}}, {0.016, 0.016, 0.016}, true);
    b.material = muffler;
    b.tube({V3{-0.92, -0.06, 0.47}, V3{-0.96, -0.1, 0.46}, V3{-0.98, -0.12, 0.44}}, {0.015, 0.015, 0.015});

    // The hood over the engine, painted, with vents and the badge on top.
    b.material = body_paint;
    b.loft({Builder::Section{-1.0, 0.2, 0.05, 0.66, 2.6}, Builder::Section{-0.985, 0.25, 0.09, 0.665, 3.0}, Builder::Section{-0.92, 0.27, 0.105, 0.668, 3.2},
            Builder::Section{-0.76, 0.27, 0.11, 0.668, 3.2}, Builder::Section{-0.68, 0.26, 0.1, 0.665, 3.0}, Builder::Section{-0.655, 0.22, 0.06, 0.66, 2.6}});
    b.material = grille;
    for (int side = -1; side <= 1; side += 2)
        b.quad(V3{-0.92, side * 0.12, 0.7785}, V3{-0.92, side * 0.21, 0.7735}, V3{-0.74, side * 0.21, 0.7735}, V3{-0.74, side * 0.12, 0.7785});
    b.material = badge_disc;
    b.cylinder(V3{-0.83, 0, 0.775}, V3{-0.83, 0, 0.781}, 0.07, 0.07, true);
    b.material = badge;
    b.quad(V3{-0.77, -0.06, 0.7825}, V3{-0.77, 0.06, 0.7825}, V3{-0.89, 0.06, 0.7825}, V3{-0.89, -0.06, 0.7825});
    // tail lights on the hood's back face
    b.material = lens;
    for (int side = -1; side <= 1; side += 2)
        b.rounded_box(V3{-1.003, side * 0.15, 0.68}, V3{0.004, 0.035, 0.012}, 0.003);

    // Eggy: his body down on the seat, tail, feet.
    b.material = down;
    b.ellipsoid(V3{-0.3, 0, 0.73}, V3{0.15, 0.132, 0.112});
    b.at = base * translation(V3{-0.44, 0, 0.78}) * rotation_y(0.5);
    b.ellipsoid(V3{0, 0, 0}, V3{0.045, 0.034, 0.024});
    b.at = base;
    b.material = beak;
    for (int side = -1; side <= 1; side += 2) {
        b.ellipsoid(V3{-0.15, side * 0.055, 0.648}, V3{0.045, 0.028, 0.011});
        for (int toe = -1; toe <= 1; ++toe)
            b.ellipsoid(V3{-0.115, side * 0.055 + toe * 0.016, 0.646}, V3{0.016, 0.008, 0.008});
    }
    if (bake)
        bake_occlusion(m);

    // His wings, which reach forward to the lap bars.
    Builder w(model.wings, ppm);
    w.material = down;
    for (int side = -1; side <= 1; side += 2) {
        w.at = base * translation(V3{-0.25, side * 0.128, 0.765}) * rotation_z(side * 0.12) * rotation_y(0.32);
        w.ellipsoid(V3{0, 0, 0}, V3{0.09, 0.026, 0.05});
        // the wing's tip, a little curl of down
        w.ellipsoid(V3{0.07, 0, -0.012}, V3{0.035, 0.02, 0.026});
    }
    if (bake)
        bake_occlusion(model.wings, 24);

    // His head, about the neck: the beak, the eyes, the acorn cap, its stalk and chin strap.
    Builder h(model.head, ppm);
    h.material = down;
    h.ellipsoid(V3{0.03, 0, 0.11}, V3{0.097, 0.094, 0.092});
    h.ellipsoid(V3{0.075, 0, 0.068}, V3{0.05, 0.07, 0.04});
    h.material = beak;
    h.ellipsoid(V3{0.128, 0, 0.096}, V3{0.052, 0.04, 0.016});
    h.ellipsoid(V3{0.118, 0, 0.078}, V3{0.042, 0.032, 0.011});
    h.material = eye;
    for (int side = -1; side <= 1; side += 2) {
        h.sphere(V3{0.097, side * 0.05, 0.128}, 0.018);
        h.sphere(V3{0.164, side * 0.012, 0.104}, 0.004);
    }
    h.at = translation(V3{0.0, 0, 0.175}) * rotation_y(-0.28);
    h.material = cup;
    h.lathe({0.086, 0.091, 0.09, 0.084, 0.07, 0.048, 0.022, 0.0}, {-0.006, 0.004, 0.018, 0.034, 0.052, 0.066, 0.074, 0.077});
    h.material = stem;
    h.tube({V3{0, 0, 0.07}, V3{-0.008, 0, 0.1}, V3{-0.025, 0, 0.115}}, {0.01, 0.009, 0.007}, true, 10);
    h.at = Xform{};
    h.material = strap;
    h.tube({V3{0.008, -0.088, 0.17}, V3{0.04, -0.092, 0.1}, V3{0.07, -0.05, 0.04}, V3{0.075, 0, 0.032}, V3{0.07, 0.05, 0.04}, V3{0.04, 0.092, 0.1},
            V3{0.008, 0.088, 0.17}},
           {0.005, 0.005, 0.005, 0.005, 0.005, 0.005, 0.005}, true, 8);
    if (bake)
        bake_occlusion(model.head, 24);
}

std::vector<Material> livery_materials(Livery livery) {
    std::vector<Material> mat(material_count);
    unsigned body = 0xF26722;
    unsigned deck = 0x2F3133;
    unsigned rims = 0x9A9EA2;
    unsigned letter = 0xFFFFFF;
    unsigned disc = 0x1E1E1E;
    unsigned stripe = 0x2B2B2B;
    if (livery == Livery::red_t) {
        body = 0xCC2027;
        deck = 0x3A3A3C;
        rims = 0x2A2A2C;
        stripe = 0xF2F2F2;
    } else if (livery == Livery::green_jd) {
        body = 0x367C2B;
        deck = 0x367C2B;
        rims = 0xFFDE00;
        letter = 0xFFDE00;
        disc = 0x367C2B;
        stripe = 0xFFDE00;
    }
    mat[body_paint] = paint_material(body, 0.82F, 0.055F);
    mat[body_paint].pattern = Pattern::none;
    mat[deck_steel] = paint_material(deck, 0.58F, 0.045F);
    mat[frame_black] = paint_material(0x1D1E20, 0.62F, 0.045F);
    mat[plastic] = paint_material(0x1A1B1D, 0.42F, 0.04F);
    mat[plastic].pattern = Pattern::rubber;
    mat[plastic].scale = 0.5F;
    mat[tyre] = paint_material(0x1C1C1C, 0.3F, 0.035F);
    mat[tyre].pattern = Pattern::tread;
    mat[tyre].scale = 26;
    mat[tyre].level = 0.2F;
    mat[caster_tyre] = mat[tyre];
    mat[caster_tyre].scale = 0;
    mat[rim] = paint_material(rims, 0.7F, 0.06F);
    mat[chrome] = paint_material(0xD4D6D8, 0.88F, 0.6F);
    mat[chrome].metal = 1;
    mat[seat] = paint_material(0x161617, 0.55F, 0.045F);
    mat[seat].pattern = Pattern::stripes;
    mat[seat].tint = hex(0x232325);
    mat[seat].scale = 7;
    mat[grip] = paint_material(0x141414, 0.2F, 0.03F);
    mat[grip].pattern = Pattern::rubber;
    mat[plate] = paint_material(0x5E6267, 0.5F, 0.3F);
    mat[plate].metal = 0.6F;
    mat[plate].pattern = Pattern::tread_plate;
    mat[grille] = paint_material(0x26272A, 0.5F, 0.05F);
    mat[grille].pattern = Pattern::grille;
    mat[grille].scale = 9;
    mat[badge] = paint_material(disc, 0.8F, 0.06F);
    mat[badge].tint = hex(letter);
    mat[badge].pattern = Pattern::glyph;
    mat[badge].level = livery == Livery::red_t ? 'T' : livery == Livery::green_jd ? 1 : 'H';  // 1: the two letters JD
    mat[badge_disc] = paint_material(livery == Livery::green_jd ? 0xFFDE00 : 0xD4D6D8, 0.85F, 0.5F);
    mat[badge_disc].metal = 1;
    mat[engine_alu] = paint_material(0x6E7276, 0.45F, 0.3F);
    mat[engine_alu].metal = 0.7F;
    mat[engine_black] = paint_material(0x202124, 0.5F, 0.045F);
    mat[lens] = paint_material(0xF4F1E6, 0.95F, 0.08F);
    mat[lens].emit = 0.25F;
    mat[muffler] = paint_material(0x3B3631, 0.35F, 0.25F);
    mat[muffler].metal = 0.6F;
    mat[knob] = paint_material(0xF2C230, 0.7F, 0.05F);
    mat[wire] = paint_material(0x1A1A1A, 0.4F, 0.04F);
    mat[ceramic] = paint_material(0xF2F0EA, 0.8F, 0.06F);
    mat[decal] = paint_material(stripe, 0.75F, 0.05F);
    mat[down] = paint_material(0xF2CF55, 0.15F, 0.02F);
    mat[down].pattern = Pattern::down;
    mat[down].wrap = 0.45F;
    mat[down].sheen = 0.7F;
    mat[beak] = paint_material(0xE8892E, 0.55F, 0.05F);
    mat[beak].wrap = 0.2F;
    mat[eye] = paint_material(0x0E1114, 0.97F, 0.07F);
    mat[cup] = paint_material(0x7A5A34, 0.35F, 0.04F);
    mat[cup].pattern = Pattern::scales;
    mat[cup].scale = 6;
    mat[stem] = paint_material(0x4A3320, 0.3F, 0.04F);
    mat[strap] = paint_material(0x5A3A20, 0.45F, 0.04F);
    mat[belt] = paint_material(0x111111, 0.25F, 0.03F);
    return mat;
}

// Pictures of the mower body at many headings, made as they are needed: the body is rigid,
// so only Eggy's head and wings are drawn afresh each frame.
constexpr int headings = 240;
constexpr std::size_t kept_headings = 120;

struct HeadingShot {
    int bucket{};
    std::uint64_t used{};
    Shot shot{};
};

struct MowerKit {
    std::mutex guard{};
    std::map<long long, std::unique_ptr<MowerModel>> models{};
    std::vector<Material> liveries[livery_count]{};
    bool painted{};
    // the kept pictures, for one livery and one frame scale at a time
    int livery{-1};
    long long scale{};
    std::uint64_t clock{};
    std::vector<std::unique_ptr<HeadingShot>> shots{};
};

MowerKit& mower_kit() {
    static MowerKit kit{};
    return kit;
}

const MowerModel& mower_model(MowerKit& kit, double ppm, const std::vector<Material>*& materials, Livery livery) {
    if (!kit.painted) {
        for (int k = 0; k < livery_count; ++k)
            kit.liveries[k] = livery_materials(static_cast<Livery>(k));
        kit.painted = true;
    }
    materials = &kit.liveries[static_cast<int>(livery)];
    const double level = detail_for(ppm);
    std::unique_ptr<MowerModel>& slot = kit.models[std::llround(level)];
    if (!slot) {
        slot = std::make_unique<MowerModel>();
        build_mower(*slot, level, true);
        MowerModel coarse{};
        build_mower(coarse, level / 3, false);
        (*slot).shade = std::move(coarse.body);
    }
    return *slot;
}

} // namespace

void draw_mower(Canvas& canvas, const Frame& frame, const MowerPose& pose, Livery livery, double time) {
    MowerKit& kit = mower_kit();
    const std::lock_guard<std::mutex> lock(kit.guard);
    const std::vector<Material>* materials = nullptr;
    const MowerModel& model = mower_model(kit, frame.ppm, materials, livery);
    // The pictures are made about the mower's own ground point, then laid at its place.
    Frame local = frame;
    local.ox = 0;
    local.oy = 0;
    const long long scale = std::llround(frame.ppm * 1000) * 7919 + std::llround(frame.tilt * 1000) * 31 + std::llround(frame.rise * 1000);
    if (kit.livery != static_cast<int>(livery) || kit.scale != scale) {
        kit.shots.clear();
        kit.livery = static_cast<int>(livery);
        kit.scale = scale;
    }
    double turn = std::fmod(pose.heading, 2 * pi);
    if (turn < 0)
        turn += 2 * pi;
    const int bucket = static_cast<int>(std::lround(turn / (2 * pi) * headings)) % headings;
    const double heading = 2 * pi * bucket / headings;
    HeadingShot* found = nullptr;
    for (std::unique_ptr<HeadingShot>& kept : kit.shots)
        if ((*kept).bucket == bucket)
            found = kept.get();
    if (found == nullptr) {
        if (kit.shots.size() >= kept_headings) {
            std::size_t oldest = 0;
            for (std::size_t k = 1; k < kit.shots.size(); ++k)
                if ((*kit.shots[k]).used < (*kit.shots[oldest]).used)
                    oldest = k;
            kit.shots.erase(kit.shots.begin() + static_cast<std::ptrdiff_t>(oldest));
        }
        std::unique_ptr<HeadingShot> made = std::make_unique<HeadingShot>();
        (*made).bucket = bucket;
        std::vector<Part> parts{};
        parts.push_back(Part{&model.body, materials, rotation_z(heading), false, &model.shade});
        ShotOptions options{};
        options.time = 0;
        options.shadow_strength = 0.55F;
        shoot(local, parts, options, (*made).shot);
        found = made.get();
        kit.shots.push_back(std::move(made));
    }
    (*found).used = ++kit.clock;
    const int dx = static_cast<int>(std::lround(frame.px(pose.x)));
    const int dy = static_cast<int>(std::lround(frame.py(pose.y)));
    paint_shadow(canvas, (*found).shot, dx, dy);
    paint(canvas, (*found).shot, dx, dy);
    // Eggy's head and wings, alive, hidden where the mower stands in front of them.
    const Xform place = rotation_z(heading);
    const double bob = 0.006 * std::sin(time * 11);
    const double reach = pose.held ? 0.05 : 0.0;
    std::vector<Part> parts{};
    parts.push_back(Part{&model.wings, materials, place * translation(V3{reach, 0, bob * 0.5}), false, nullptr});
    parts.push_back(Part{&model.head, materials, place * translation(V3{-0.2, 0, 0.82 + bob}) * rotation_z(pose.eggy_look), false, nullptr});
    thread_local Shot eggy{};
    ShotOptions options{};
    options.time = time;
    options.ground_shadow = false;
    options.occluder = &(*found).shot;
    shoot(local, parts, options, eggy);
    paint(canvas, eggy, dx, dy);
    static_cast<void>(pose.shake);
}

} // namespace mm
