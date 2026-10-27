// The things left out on the lawn, as solid models: two barbecues, two sandboxes and a
// deckchair, and the parasol some loungers stand under.
//
// - A kettle barbecue: an enamelled bowl and domed lid on three legs, two of them on
//   wheels, an ash pan slung between them, a wooden handle on the lid and a vent on top.
// - A long barrel barbecue on a cart: the firebox, its half-round lid with a thermometer
//   and a long handle, three knobs, slatted wooden shelves either side, a gas bottle
//   underneath and two wheels at one end.
// - A plank sandbox: four boards with corner seats round a bed of sand that has been
//   dug, heaped and wetted, with a bucket, a spade and now and then a sandcastle.
// - A turtle sandbox: the moulded green turtle's basin with its head, feet and tail,
//   full of sand, its shell-shaped lid lying beside it on the grass.
// - A wooden deckchair, its canvas in bold stripes, with a towel and a drink.
//
// None of it moves, so each is drawn once and kept.
//
// Metres: x along the thing's length, y across, z up; turned props are rotated about z.
#include "models.hpp"

#include <algorithm>
#include <cmath>

namespace mm {
namespace {

constexpr double pi = 3.14159265358979323846;

enum YardMaterial : std::uint16_t {
    enamel,      // the barbecue's body and lid
    steel,       // legs, grates, the cart
    chrome,      // handles' brackets, the thermometer's rim
    handle_wood, // handles and shelves
    rubber,      // wheels
    gas,         // the gas bottle
    plank,       // the sandbox's boards
    sand,
    plastic_red,
    plastic_blue,
    turtle,      // the turtle's moulded body
    turtle_shell,
    eye,
    pupil,
    canvas,      // the deckchair's sling
    towel,
    glass,
    drink,
    dial,
    yard_material_count
};

std::vector<Material> yard_materials(std::uint64_t seed) {
    std::uint64_t random = seed | 1U;
    const unsigned enamels[4] = {0x1B1D20, 0x8E1B1F, 0x1F4A35, 0x23395B};
    const unsigned canvases[4] = {0x2F6FB8, 0xD8443A, 0x2E8A5E, 0xE9A23B};
    const unsigned towels[4] = {0xF2B8C6, 0x7FC4E0, 0xF4E38A, 0xFFFFFF};
    std::vector<Material> mat(yard_material_count);
    mat[enamel] = paint_material(enamels[next_random(random) % 4U], 0.82F, 0.06F);
    mat[steel] = paint_material(0x55595E, 0.5F, 0.4F);
    mat[steel].metal = 0.8F;
    mat[chrome] = paint_material(0xD7DADD, 0.9F, 0.6F);
    mat[chrome].metal = 1.0F;
    mat[chrome].pattern = Pattern::metal_brushed;
    mat[handle_wood] = paint_material(0x9A6A3C, 0.45F, 0.04F);
    mat[handle_wood].pattern = Pattern::wood;
    mat[handle_wood].scale = 3;
    mat[rubber] = paint_material(0x222326, 0.2F, 0.03F);
    mat[rubber].pattern = Pattern::rubber;
    mat[gas] = paint_material(next_random(random) % 2U ? 0x2C6FB5 : 0xC9C9C2, 0.6F, 0.05F);
    mat[plank] = paint_material(0xA8794A, 0.25F, 0.03F);
    mat[plank].pattern = Pattern::wood;
    mat[plank].scale = 2;
    mat[sand] = paint_material(0xE5CF98, 0.1F, 0.02F);
    mat[sand].pattern = Pattern::sand;
    mat[plastic_red] = paint_material(0xD8342B, 0.7F, 0.05F);
    mat[plastic_blue] = paint_material(0x2F72CF, 0.7F, 0.05F);
    mat[turtle] = paint_material(0x4FA548, 0.62F, 0.05F);
    mat[turtle_shell] = paint_material(0x2F8A3C, 0.66F, 0.05F);
    mat[turtle_shell].tint = hex(0x7CC864);
    mat[turtle_shell].pattern = Pattern::shell;
    mat[turtle_shell].scale = 1;
    mat[eye] = paint_material(0xFFFFFF, 0.8F, 0.05F);
    mat[pupil] = paint_material(0x15171A, 0.9F, 0.06F);
    mat[canvas] = paint_material(0xF6F1E3, 0.25F, 0.03F);
    mat[canvas].tint = hex(canvases[next_random(random) % 4U]);
    mat[canvas].pattern = Pattern::stripes;
    mat[canvas].scale = 7;
    mat[canvas].two_sided = true;
    mat[canvas].wrap = 0.3F;
    mat[towel] = paint_material(towels[next_random(random) % 4U], 0.15F, 0.03F);
    mat[towel].pattern = Pattern::knit;
    mat[towel].wrap = 0.35F;
    mat[towel].two_sided = true;
    mat[glass] = paint_material(0xE8F4F8, 0.96F, 0.08F);
    mat[glass].opacity = 0.28F;
    mat[drink] = paint_material(0xE85A3C, 0.85F, 0.05F);
    mat[drink].opacity = 0.8F;
    mat[dial] = paint_material(0xF4F1E8, 0.7F, 0.05F);
    return mat;
}

void wheel(Builder& b, V3 centre, double radius, double width, V3 axle) {
    b.material = rubber;
    b.cylinder(centre - axle * (width * 0.5), centre + axle * (width * 0.5), radius, radius, true);
    b.material = chrome;
    b.cylinder(centre - axle * (width * 0.55), centre + axle * (width * 0.55), radius * 0.35, radius * 0.35, true);
}

void build_kettle_grill(Mesh& mesh, double ppm) {
    Builder b(mesh, ppm);
    // Three legs from under the bowl to the grass; the two at the back end on wheels.
    const double feet[3] = {pi * 1.5, pi * 0.17, pi * 0.83};  // one leg at the back, the wheels in front
    for (int k = 0; k < 3; ++k) {
        const V3 top{std::cos(feet[k]) * 0.16, std::sin(feet[k]) * 0.16, 0.5};
        const V3 foot{std::cos(feet[k]) * 0.3, std::sin(feet[k]) * 0.3, k == 0 ? 0.0 : 0.07};
        b.material = steel;
        b.tube({top, foot}, {0.016, 0.014}, true);
        if (k > 0)
            wheel(b, foot, 0.07, 0.035, V3{-std::sin(feet[k]), std::cos(feet[k]), 0});
    }
    // the ash pan, slung between the legs
    b.material = steel;
    b.lathe({0.0, 0.17, 0.18, 0.17}, {0.2, 0.2, 0.23, 0.235});
    // the bowl and its lid: one enamelled sphere, split at the rim
    b.material = enamel;
    b.lathe({0.0, 0.09, 0.17, 0.225, 0.258, 0.272, 0.276}, {0.42, 0.43, 0.47, 0.52, 0.57, 0.62, 0.66});
    b.material = chrome;
    b.torus(V3{0, 0, 0.665}, 0.277, 0.008);
    b.material = enamel;
    b.lathe({0.278, 0.272, 0.255, 0.222, 0.17, 0.1, 0.0}, {0.665, 0.71, 0.76, 0.81, 0.85, 0.875, 0.882});
    // the vent on the lid and the lid's wooden handle on its brackets
    b.material = chrome;
    b.cylinder(V3{0.06, 0, 0.87}, V3{0.06, 0, 0.89}, 0.05, 0.05, true);
    b.cylinder(V3{-0.06, 0, 0.865}, V3{-0.06, 0, 0.905}, 0.008, 0.008, true);
    b.cylinder(V3{-0.06, 0.05, 0.86}, V3{-0.06, 0.05, 0.9}, 0.008, 0.008, true);
    b.material = handle_wood;
    b.tube({V3{-0.06, -0.06, 0.905}, V3{-0.06, 0.1, 0.905}}, {0.018, 0.018}, true);
    // side handles on the bowl
    for (int side = -1; side <= 1; side += 2) {
        b.material = chrome;
        b.tube({V3{0, side * 0.27, 0.6}, V3{0, side * 0.31, 0.6}}, {0.007, 0.007}, true);
        b.material = handle_wood;
        b.tube({V3{-0.06, side * 0.315, 0.6}, V3{0.06, side * 0.315, 0.6}}, {0.014, 0.014}, true);
    }
    bake_occlusion(mesh, 32);
}

void build_barrel_grill(Mesh& mesh, double ppm) {
    Builder b(mesh, ppm);
    // The cart: four legs, a slatted lower shelf, wheels at one end.
    b.material = steel;
    for (int ex = -1; ex <= 1; ex += 2)
        for (int ey = -1; ey <= 1; ey += 2)
            b.tube({V3{ex * 0.36, ey * 0.2, 0.6}, V3{ex * 0.36, ey * 0.2, ex < 0 ? 0.08 : 0.0}}, {0.016, 0.016}, true);
    b.rounded_box(V3{0, 0, 0.16}, V3{0.37, 0.21, 0.01}, 0.006);
    for (int ey = -1; ey <= 1; ey += 2) {
        b.tube({V3{-0.36, ey * 0.2, 0.16}, V3{0.36, ey * 0.2, 0.16}}, {0.012, 0.012}, true);
        wheel(b, V3{-0.36, ey * 0.235, 0.08}, 0.08, 0.04, V3{0, 1, 0});
    }
    // the gas bottle on the shelf
    b.material = gas;
    b.lathe({0.0, 0.1, 0.115, 0.115, 0.1, 0.04, 0.0}, {0.17, 0.17, 0.2, 0.36, 0.4, 0.42, 0.42});
    b.material = chrome;
    b.cylinder(V3{0.12, 0, 0.42}, V3{0.12, 0, 0.45}, 0.018, 0.018, true);
    // the firebox, and its half-round lid along x
    b.material = enamel;
    b.rounded_box(V3{0, 0, 0.72}, V3{0.38, 0.24, 0.12}, 0.03);
    std::vector<Builder::Section> lid{};
    for (int k = 0; k <= 8; ++k) {
        const double x = -0.38 + 0.76 * k / 8;
        lid.push_back(Builder::Section{x, 0.24, 0.17, 0.84, 2.0, 0.0});
    }
    b.loft(lid, 0, true);
    // the lid's long handle, the thermometer, the knobs on the front
    b.material = chrome;
    b.tube({V3{-0.24, 0.27, 0.86}, V3{-0.24, 0.31, 0.88}}, {0.008, 0.008}, true);
    b.tube({V3{0.24, 0.27, 0.86}, V3{0.24, 0.31, 0.88}}, {0.008, 0.008}, true);
    b.material = handle_wood;
    b.tube({V3{-0.27, 0.315, 0.885}, V3{0.27, 0.315, 0.885}}, {0.017, 0.017}, true);
    b.material = chrome;
    b.at = aimed(V3{0, 0.2, 0.97}, V3{0, 0.6, 0.8});
    b.cylinder(V3{0, 0, 0}, V3{0, 0, 0.012}, 0.04, 0.04, true);
    b.material = dial;
    b.cylinder(V3{0, 0, 0.012}, V3{0, 0, 0.014}, 0.032, 0.032, true);
    b.at = Xform{};
    for (int k = -1; k <= 1; ++k) {
        b.material = chrome;
        b.cylinder(V3{k * 0.12, 0.24, 0.68}, V3{k * 0.12, 0.27, 0.68}, 0.025, 0.025, true);
        b.material = steel;
        b.rounded_box(V3{k * 0.12, 0.275, 0.68}, V3{0.006, 0.006, 0.02}, 0.004);
    }
    // the side shelves: slats on brackets
    for (int side = -1; side <= 1; side += 2) {
        b.material = steel;
        b.tube({V3{side * 0.38, -0.18, 0.78}, V3{side * 0.72, -0.18, 0.8}}, {0.01, 0.01}, true);
        b.tube({V3{side * 0.38, 0.18, 0.78}, V3{side * 0.72, 0.18, 0.8}}, {0.01, 0.01}, true);
        b.material = handle_wood;
        for (int slat = 0; slat < 5; ++slat)
            b.rounded_box(V3{side * (0.43 + slat * 0.065), 0, 0.815}, V3{0.027, 0.21, 0.012}, 0.006);
    }
    bake_occlusion(mesh, 32);
}

// A bed of sand, `rx` by `ry` (an ellipse when `round`), heaped and dug, its top at about `z`.
void sand_bed(Builder& b, double rx, double ry, double z, bool round, std::uint64_t seed) {
    std::uint64_t random = seed | 1U;
    struct Heap {
        double x, y, r, h;
    };
    std::vector<Heap> heaps{};
    for (int k = 0; k < 7; ++k)
        heaps.push_back(Heap{random_range(random, -rx * 0.7, rx * 0.7), random_range(random, -ry * 0.7, ry * 0.7),
                             random_range(random, 0.08, 0.2), random_range(random, -0.05, 0.06)});
    const int rows = 40, cols = 40;
    std::vector<V3> points{};
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            double u = -1 + 2.0 * c / (cols - 1), v = -1 + 2.0 * r / (rows - 1);
            if (round) {
                // squeeze the square grid into the ellipse
                const double m = std::max(std::abs(u), std::abs(v));
                const double len = std::hypot(u, v);
                if (len > 1e-9) {
                    u *= m / len;
                    v *= m / len;
                }
            }
            const double x = u * rx, y = v * ry;
            double h = 0;
            for (const Heap& heap : heaps) {
                const double d = std::hypot(x - heap.x, y - heap.y) / heap.r;
                h += heap.h * std::exp(-d * d * 1.6);
            }
            // the sand runs up the sides a little
            const double edge = round ? std::hypot(u, v) : std::max(std::abs(u), std::abs(v));
            h += 0.02 * std::pow(edge, 6);
            if (round)
                h = std::max(h, 0.006);  // the turtle's tub is shallow: dug no deeper than its floor
            points.push_back(V3{x, y, z + h});
        }
    b.material = sand;
    b.grid(points, rows, cols, false);
}

void bucket_and_spade(Builder& b, V3 at, double turn) {
    b.material = plastic_red;
    b.at = translation(at);
    b.lathe({0.0, 0.05, 0.052, 0.07, 0.072}, {0.0, 0.0, 0.004, 0.13, 0.13});
    b.material = sand;
    b.cylinder(V3{0, 0, 0.1}, V3{0, 0, 0.115}, 0.066, 0.066, true);
    b.material = plastic_red;
    b.tube({V3{-0.07, 0, 0.12}, V3{-0.04, 0, 0.19}, V3{0.04, 0, 0.19}, V3{0.07, 0, 0.12}}, {0.005, 0.005, 0.005, 0.005}, false);
    b.at = translation(at + V3{std::cos(turn) * 0.2, std::sin(turn) * 0.2, 0}) * rotation_z(turn);
    b.material = plastic_blue;
    b.tube({V3{0, 0, 0.03}, V3{0.2, 0, 0.05}}, {0.011, 0.011}, true);
    b.rounded_box(V3{0.25, 0, 0.05}, V3{0.05, 0.04, 0.006}, 0.004);
    b.tube({V3{-0.02, -0.03, 0.03}, V3{-0.02, 0.03, 0.03}}, {0.01, 0.01}, true);
    b.at = Xform{};
}

void sandcastle(Builder& b, V3 at) {
    b.material = sand;
    b.at = translation(at);
    b.lathe({0.11, 0.1, 0.09, 0.0}, {0.0, 0.05, 0.1, 0.1});
    for (int k = 0; k < 4; ++k) {
        const double a = k * pi / 2 + pi / 4;
        b.cylinder(V3{std::cos(a) * 0.07, std::sin(a) * 0.07, 0.08}, V3{std::cos(a) * 0.07, std::sin(a) * 0.07, 0.16}, 0.03, 0.026, true);
    }
    b.cylinder(V3{0, 0, 0.1}, V3{0, 0, 0.2}, 0.04, 0.035, true);
    b.at = Xform{};
}

void build_plank_sandbox(Mesh& mesh, double ppm, double h, std::uint64_t seed) {
    Builder b(mesh, ppm);
    std::uint64_t random = seed | 7U;
    const double t = 0.07;  // board thickness
    b.material = plank;
    b.rounded_box(V3{0, -h + t * 0.5, 0.13}, V3{h, t * 0.5, 0.13}, 0.01);
    b.rounded_box(V3{0, h - t * 0.5, 0.13}, V3{h, t * 0.5, 0.13}, 0.01);
    b.rounded_box(V3{-h + t * 0.5, 0, 0.13}, V3{t * 0.5, h - t, 0.13}, 0.01);
    b.rounded_box(V3{h - t * 0.5, 0, 0.13}, V3{t * 0.5, h - t, 0.13}, 0.01);
    // corner seats: triangular boards across two corners
    for (int corner = 0; corner < 2; ++corner) {
        const double sx = corner == 0 ? -1 : 1, sy = corner == 0 ? -1 : 1;
        const double a = h - 0.02, c = h - 0.3;
        b.prism({sx * a, sx * c, sx * a}, {sy * a, sy * a, sy * c}, 0.24, 0.27);
    }
    sand_bed(b, h - t, h - t, 0.17, false, seed);
    const double bx = random_range(random, -h * 0.4, h * 0.4), by = random_range(random, -h * 0.4, h * 0.4);
    bucket_and_spade(b, V3{bx, by, 0.17}, random_range(random, 0, 2 * pi));
    if (random_unit(random) < 0.6)
        sandcastle(b, V3{-bx * 0.8, -by * 0.8 + 0.05, 0.16});
    bake_occlusion(mesh, 32);
}

void build_turtle_sandbox(Mesh& mesh, double ppm, std::uint64_t seed) {
    Builder b(mesh, ppm);
    std::uint64_t random = seed | 11U;
    // The basin: a rounded oval tub, its rim rolled over, its body at +x.
    const V3 body{0.32, 0, 0};
    b.at = translation(body);
    b.material = turtle;
    std::vector<Builder::Section> tub{};
    for (int k = 0; k <= 12; ++k) {
        const double u = -1 + 2.0 * k / 12;
        const double w = 0.5 * std::sqrt(std::max(0.02, 1 - u * u));
        tub.push_back(Builder::Section{u * 0.62, w, 0.085, 0.075, 3.0, 0.0});
    }
    b.loft(tub, 0, true);
    sand_bed(b, 0.55, 0.43, 0.165, true, seed);
    // the head, raised and smiling, with two round eyes; the feet; a little tail
    b.material = turtle;
    b.ellipsoid(V3{0.74, 0, 0.16}, V3{0.16, 0.13, 0.12});
    b.ellipsoid(V3{0.62, 0, 0.12}, V3{0.1, 0.11, 0.08});
    for (int side = -1; side <= 1; side += 2) {
        b.material = eye;
        b.sphere(V3{0.8, side * 0.065, 0.24}, 0.04);
        b.material = pupil;
        b.sphere(V3{0.835, side * 0.07, 0.25}, 0.018);
        b.material = turtle;
        for (int end = -1; end <= 1; end += 2)
            b.ellipsoid(V3{end * 0.38, side * 0.47, 0.04}, V3{0.12, 0.08, 0.045});
    }
    b.ellipsoid(V3{-0.66, 0, 0.06}, V3{0.08, 0.04, 0.035});
    b.at = Xform{};
    // The shell, lifted off and laid on the grass beside: a shallow dome of plates.
    b.at = translation(V3{-0.55, 0.05, 0}) * rotation_z(random_range(random, -0.4, 0.4));
    b.material = turtle_shell;
    b.lathe({0.46, 0.46, 0.43, 0.36, 0.25, 0.12, 0.0}, {0.0, 0.03, 0.08, 0.13, 0.17, 0.19, 0.2});
    b.at = translation(V3{-0.55, 0.05, 0}) * scaling(1.0, 0.82, 1.0);
    b.material = turtle;
    b.torus(V3{0, 0, 0.025}, 0.455, 0.022);
    b.at = Xform{};
    bucket_and_spade(b, V3{body.x + random_range(random, -0.25, 0.15), random_range(random, -0.2, 0.2), 0.165}, random_range(random, 0, 2 * pi));
    bake_occlusion(mesh, 32);
}

void build_deckchair(Mesh& mesh, double ppm) {
    Builder b(mesh, ppm);
    // A beech frame: the back legs leaning back, the front legs, the seat rail; canvas hung between.
    b.material = handle_wood;
    for (int side = -1; side <= 1; side += 2) {
        const double x = side * 0.25;
        b.tube({V3{x, 0.42, 0.0}, V3{x, -0.36, 0.86}}, {0.02, 0.02}, true);   // the back legs, the long rails
        b.tube({V3{x * 1.05, -0.12, 0.0}, V3{x * 1.05, 0.3, 0.42}}, {0.018, 0.018}, true);  // the front legs
        b.tube({V3{x * 1.05, 0.3, 0.42}, V3{x * 1.05, 0.48, 0.44}}, {0.016, 0.016}, true);  // the arm
    }
    b.tube({V3{-0.27, 0.48, 0.44}, V3{0.27, 0.48, 0.44}}, {0.016, 0.016}, true);
    b.tube({V3{-0.25, -0.36, 0.86}, V3{0.25, -0.36, 0.86}}, {0.016, 0.016}, true);
    b.tube({V3{-0.25, 0.42, 0.0}, V3{0.25, 0.42, 0.0}}, {0.016, 0.016}, true);
    // the canvas: from the top rail down to the front, sagging deep in the seat
    b.material = canvas;
    const int rows = 24, cols = 8;
    std::vector<V3> points{};
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            const double across = -1 + 2.0 * c / (cols - 1), along = static_cast<double>(r) / (rows - 1);
            const V3 top{0, -0.36, 0.86}, front{0, 0.46, 0.44};
            V3 p = top + (front - top) * along;
            p.z -= 0.36 * std::sin(pi * along) * (0.9 + 0.1 * (1 - across * across));
            p.y += 0.06 * std::sin(pi * along);
            p.x = across * 0.235;
            points.push_back(p);
        }
    b.grid(points, rows, cols, false);
    // a towel over the top rail, hanging down the back
    b.material = towel;
    std::vector<V3> cloth{};
    for (int r = 0; r < 10; ++r)
        for (int c = 0; c < 6; ++c) {
            const double across = -1 + 2.0 * c / 5, down = r / 9.0;
            const double y = down < 0.3 ? -0.36 - 0.02 + down * 0.1 : -0.37 - (down - 0.3) * 0.08;
            const double z = down < 0.3 ? 0.88 - down * 0.2 : 0.82 - (down - 0.3) * 0.5;
            cloth.push_back(V3{across * 0.2 + 0.02 * std::sin(down * 7), y - 0.015 * std::cos(across * 3), z});
        }
    b.grid(cloth, 10, 6, false);
    // a drink on the grass by the arm
    b.at = translation(V3{0.38, 0.46, 0});
    b.material = drink;
    b.cylinder(V3{0, 0, 0.008}, V3{0, 0, 0.1}, 0.031, 0.034, true);
    b.material = glass;
    b.lathe({0.0, 0.034, 0.036, 0.04, 0.041, 0.038}, {0.0, 0.0, 0.006, 0.13, 0.132, 0.13});
    b.at = Xform{};
    bake_occlusion(mesh, 32);
}

void build_parasol(Mesh& mesh, double ppm) {
    Builder b(mesh, ppm);
    // a pole in a weighted base, leaning a little over the lounger, and a canopy of eight panels
    b.material = steel;
    b.lathe({0.0, 0.18, 0.18, 0.12, 0.03}, {0.0, 0.0, 0.05, 0.08, 0.1});
    const V3 foot{0, 0, 0.05}, top{0.18, 0.12, 1.85};
    b.material = chrome;
    b.tube({foot, top}, {0.02, 0.02}, true);
    b.material = canvas;
    const int ribs = 8, rings = 6;
    std::vector<V3> points{};
    for (int r = 0; r < rings; ++r)
        for (int k = 0; k < ribs * 4; ++k) {
            const double a = 2 * pi * k / (ribs * 4);
            const double t = static_cast<double>(r) / (rings - 1);
            // between the ribs each panel sags a touch
            const double scallop = 1 - 0.05 * t * std::pow(std::abs(std::sin(a * ribs / 2)), 1.0);
            const double radius = 0.8 * t * scallop;
            points.push_back(top + V3{std::cos(a) * radius, std::sin(a) * radius, 0.14 - 0.42 * t * t});
        }
    b.grid(points, rings, ribs * 4, true);
    b.material = chrome;
    b.sphere(top + V3{0, 0, 0.16}, 0.03);
    bake_occlusion(mesh, 24);
}

void draw_kept(Canvas& canvas, const Frame& frame, int kind, std::uint64_t seed, double heading, const Prop& prop,
               void (*build)(Mesh&, double, const Prop&)) {
    const StillKey key = still_key(kind, seed * 31 + static_cast<std::uint64_t>(std::lround(heading * 100)), frame);
    const Shot* kept = find_still(key);
    if (kept == nullptr) {
        Mesh mesh{};
        build(mesh, detail_for(frame.ppm), prop);
        const std::vector<Material> materials = yard_materials(prop.seed);
        std::vector<Part> parts{};
        parts.push_back(Part{&mesh, &materials, translation(V3{prop.x, prop.y, 0}) * rotation_z(heading), false, nullptr});
        Shot shot{};
        ShotOptions options{};
        options.shadow_strength = 0.5F;
        shoot(frame, parts, options, shot);
        kept = &keep_still(key, std::move(shot));
    }
    paint_shadow(canvas, *kept);
    paint(canvas, *kept);
}

// A thing knocked over: laid on its side the way it fell, what would be under the
// grass cut away.
void draw_fallen(Canvas& canvas, const Frame& frame, int kind, const Prop& prop, void (*build)(Mesh&, double, const Prop&)) {
    const StillKey key = still_key(kind, prop.seed * 31 + static_cast<std::uint64_t>(std::lround((prop.fall + 10) * 100)), frame);
    const Shot* kept = find_still(key);
    if (kept == nullptr) {
        Mesh mesh{};
        build(mesh, detail_for(frame.ppm), prop);
        const std::vector<Material> materials = yard_materials(prop.seed);
        std::vector<Part> parts{};
        // Over about the foot it was standing on, top toward the way it fell.
        const Xform place = translation(V3{prop.x, prop.y, 0.18}) * rotation_z(prop.fall) * rotation_y(1.35);
        parts.push_back(Part{&mesh, &materials, place, false, nullptr});
        Shot shot{};
        ShotOptions options{};
        options.shadow_strength = 0.5F;
        options.clip_ground = true;
        shoot(frame, parts, options, shot);
        kept = &keep_still(key, std::move(shot));
    }
    paint_shadow(canvas, *kept);
    paint(canvas, *kept);
}

void kettle(Mesh& mesh, double ppm, const Prop&) {
    build_kettle_grill(mesh, ppm);
}
void barrel(Mesh& mesh, double ppm, const Prop&) {
    build_barrel_grill(mesh, ppm);
}
void planks(Mesh& mesh, double ppm, const Prop& prop) {
    build_plank_sandbox(mesh, ppm, prop.rx, prop.seed);
}
void turtle_box(Mesh& mesh, double ppm, const Prop& prop) {
    build_turtle_sandbox(mesh, ppm, prop.seed);
}
void deckchair(Mesh& mesh, double ppm, const Prop&) {
    build_deckchair(mesh, ppm);
}
void parasol(Mesh& mesh, double ppm, const Prop&) {
    build_parasol(mesh, ppm);
}

} // namespace

void draw_grill_model(Canvas& canvas, const Frame& frame, const Prop& prop, double heading) {
    if (prop.toppled) {
        draw_fallen(canvas, frame, prop_style(prop) == 2 ? 67 : 66, prop, prop_style(prop) == 2 ? barrel : kettle);
        return;
    }
    if (prop_style(prop) == 2)
        draw_kept(canvas, frame, 61, prop.seed, heading, prop, barrel);
    else
        draw_kept(canvas, frame, 60, prop.seed, heading, prop, kettle);
}

void draw_sandbox_model(Canvas& canvas, const Frame& frame, const Prop& prop, double heading) {
    if (prop_style(prop) == 0)
        draw_kept(canvas, frame, 62, prop.seed, 0, prop, planks);
    else
        draw_kept(canvas, frame, 63, prop.seed, heading, prop, turtle_box);
}

bool draw_chair_model(Canvas& canvas, const Frame& frame, const Prop& prop, double heading) {
    // One chair in three is a deckchair, never with a sleeper; the rest are loungers.
    if (prop_style(prop) != 1 || prop.occupied)
        return false;
    draw_kept(canvas, frame, 64, prop.seed, heading, prop, deckchair);
    return true;
}

void draw_parasol_model(Canvas& canvas, const Frame& frame, const Prop& prop, double heading) {
    draw_kept(canvas, frame, 65, prop.seed, heading, prop, parasol);
}

} // namespace mm
