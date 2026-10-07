// Garden sheds, made up from the prop's seed: an apex (gable) roof or a pent (single
// slope), shiplap boards in one of the colours sheds are painted or stained, corner and
// door trims, a ledged-and-braced door with T-hinges (or a pair of them), none to two
// windows with their glazing bars, sometimes a window box, a felted roof with fascias,
// and now and then a water butt or a spade left against the wall.
//
// Shed metres: x along its length, y across it (the door on the +y side, facing the
// lawn), z up, the origin on the ground at its middle. Drawn once and kept.
#include "models.hpp"

#include <algorithm>
#include <cmath>

namespace mm {
namespace {


enum ShedMaterial : std::uint16_t { boards, trim, felt, fascia, door_boards, iron, glass, sill, soil, flower, leaf, butt, base, handle, blade, shed_material_count };

struct ShedStyle {
    bool apex{};
    double eaves{};
    double ridge{};
    int doors{};       // 1 or 2
    double door_at{};  // the door's middle along x, as a share of the half length
    int windows{};
    bool window_box{};
    bool water_butt{};
    bool spade{};
    unsigned wall{};
    unsigned trim_colour{};
    unsigned felt_colour{};
    unsigned flower_colour{};
};

ShedStyle style_for(std::uint64_t seed) {
    std::uint64_t random = seed * 0x9E3779B97F4A7C15ULL + 5;
    ShedStyle s{};
    s.apex = random_unit(random) < 0.62;
    s.eaves = random_range(random, 1.85, 2.05);
    s.ridge = s.apex ? s.eaves + random_range(random, 0.45, 0.65) : s.eaves + random_range(random, 0.25, 0.35);
    s.doors = random_unit(random) < 0.3 ? 2 : 1;
    s.door_at = s.doors == 2 ? 0.0 : (random_unit(random) < 0.5 ? random_range(random, -0.55, -0.3) : random_range(random, 0.3, 0.55));
    const double w = random_unit(random);
    s.windows = w < 0.2 ? 0 : w < 0.75 ? 1 : 2;
    s.window_box = s.windows > 0 && random_unit(random) < 0.4;
    s.water_butt = random_unit(random) < 0.35;
    s.spade = random_unit(random) < 0.3;
    const unsigned walls[7] = {0xB0814F, 0x9A5B34, 0x8A9A7B, 0x9DB7B5, 0x3E5B3A, 0x8E3A2E, 0x3E4246};
    const unsigned trims[7] = {0x7A5634, 0xEDE6D6, 0xEDE6D6, 0xF2EFE6, 0xD9D2BE, 0xEDE6D6, 0xC9B98E};
    const int pick = static_cast<int>(next_random(random) % 7U);
    s.wall = walls[pick];
    s.trim_colour = trims[pick];
    const unsigned felts[3] = {0x1E2022, 0x262C24, 0x33241F};
    s.felt_colour = felts[next_random(random) % 3U];
    const unsigned flowers[4] = {0xE8434F, 0xF2C230, 0xB65FD0, 0xF07AA0};
    s.flower_colour = flowers[next_random(random) % 4U];
    return s;
}

std::vector<Material> shed_materials(const ShedStyle& s) {
    std::vector<Material> mat(shed_material_count);
    mat[boards] = paint_material(s.wall, 0.28F, 0.035F);
    mat[boards].pattern = Pattern::wood;
    mat[boards].scale = 2;
    mat[trim] = paint_material(s.trim_colour, 0.35F, 0.04F);
    mat[felt] = paint_material(s.felt_colour, 0.2F, 0.03F);
    mat[felt].pattern = Pattern::rubber;
    mat[felt].scale = 0.4F;
    mat[fascia] = paint_material(s.trim_colour, 0.35F, 0.04F);
    mat[door_boards] = mat[boards];
    mat[iron] = paint_material(0x1D1D1F, 0.45F, 0.05F);
    mat[glass] = paint_material(0x26343C, 0.95F, 0.08F);
    mat[sill] = paint_material(s.trim_colour, 0.4F, 0.04F);
    mat[soil] = paint_material(0x3A2618, 0.15F, 0.03F);
    mat[flower] = paint_material(s.flower_colour, 0.4F, 0.04F);
    mat[flower].wrap = 0.3F;
    mat[leaf] = paint_material(0x3E7A2E, 0.35F, 0.035F);
    mat[leaf].wrap = 0.3F;
    mat[butt] = paint_material(0x2F5A33, 0.55F, 0.05F);
    mat[base] = paint_material(0x8E8A82, 0.2F, 0.03F);
    mat[base].pattern = Pattern::stone;
    mat[handle] = paint_material(0xB98A56, 0.45F, 0.04F);
    mat[handle].pattern = Pattern::wood;
    mat[blade] = paint_material(0x9EA3A8, 0.7F, 0.5F);
    mat[blade].metal = 1;
    return mat;
}

void build_shed(Mesh& mesh, double ppm, const ShedStyle& s, double hx, double hy, std::uint64_t seed) {
    Builder b(mesh, ppm);
    std::uint64_t random = seed + 99;
    const double hz = s.eaves;
    // a low base of slabs it stands on
    b.material = base;
    b.rounded_box(V3{0, 0, 0.04}, V3{hx + 0.06, hy + 0.06, 0.04}, 0.01);
    // the walls: a core, then overlapping boards stepped out course by course
    b.material = boards;
    b.rounded_box(V3{0, 0, 0.08 + (hz - 0.08) / 2}, V3{hx, hy, (hz - 0.08) / 2}, 0.01);
    const double course = 0.125;
    for (double z = 0.1; z < hz - 0.05; z += course) {
        // each board's lower edge stands proud of the one below
        b.rounded_box(V3{0, 0, z + course * 0.3}, V3{hx + 0.012, hy + 0.012, course * 0.3}, 0.006);
    }
    // the gable ends (apex) or the rising sides (pent), boarded the same: a flat shape in
    // (across, up) stood on its edge at each end
    for (int side = -1; side <= 1; side += 2) {
        Xform end{};
        end.m[0] = 0; end.m[1] = 0; end.m[2] = 1; end.m[3] = side * hx;
        end.m[4] = 1; end.m[5] = 0; end.m[6] = 0; end.m[7] = 0;
        end.m[8] = 0; end.m[9] = 1; end.m[10] = 0; end.m[11] = 0;
        b.at = end;
        if (s.apex)
            b.prism({-hy, hy, 0.0}, {hz, hz, s.ridge}, -0.012, 0.012);
        else
            b.prism({-hy, hy, hy}, {hz, hz, s.ridge}, -0.012, 0.012);
        b.at = Xform{};
    }
    // corner trims
    b.material = trim;
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2)
            b.rounded_box(V3{sx * (hx + 0.012), sy * (hy + 0.012), 0.08 + (hz - 0.08) / 2}, V3{0.035, 0.035, (hz - 0.08) / 2}, 0.008);
    // The door(s): ledged and braced boards in a trim frame, T-hinges, a latch.
    const double door_w = s.doors == 2 ? 0.55 : 0.4;
    const double door_h = std::min(hz - 0.12, 1.78);
    const double front = hy + 0.02;
    for (int d = 0; d < s.doors; ++d) {
        const double cx = s.doors == 2 ? (d == 0 ? -door_w * 0.5 : door_w * 0.5) : s.door_at * hx;
        const double half = s.doors == 2 ? door_w * 0.5 : door_w;
        b.material = door_boards;
        b.rounded_box(V3{cx, front, 0.1 + door_h / 2}, V3{half - 0.01, 0.018, door_h / 2}, 0.006);
        // the board joints
        b.material = iron;
        for (double x = cx - half + 0.11; x < cx + half - 0.05; x += 0.11)
            b.rounded_box(V3{x, front + 0.019, 0.1 + door_h / 2}, V3{0.003, 0.002, door_h / 2 - 0.02}, 0.001);
        // ledges and the brace
        b.material = door_boards;
        for (double z : {0.3, 0.1 + door_h / 2, 0.1 + door_h - 0.2})
            b.rounded_box(V3{cx, front + 0.03, z}, V3{half - 0.04, 0.012, 0.05}, 0.006);
        b.at = translation(V3{cx, front + 0.03, 0.1 + door_h * 0.38}) * rotation_y(-std::atan2(door_h * 0.48, half * 2 - 0.12) * (d == 0 ? 1 : -1));
        b.rounded_box(V3{0, 0, 0}, V3{std::hypot(half * 2 - 0.12, door_h * 0.48) / 2, 0.011, 0.045}, 0.006);
        b.at = Xform{};
        // T-hinges on the hanging side
        b.material = iron;
        const double hinge_side = s.doors == 2 ? (d == 0 ? -1.0 : 1.0) : (s.door_at < 0 ? -1.0 : 1.0);
        for (double z : {0.3, 0.1 + door_h - 0.2}) {
            b.rounded_box(V3{cx + hinge_side * (half - 0.2), front + 0.045, z}, V3{0.18, 0.004, 0.012}, 0.003);
            b.rounded_box(V3{cx + hinge_side * (half - 0.03), front + 0.045, z}, V3{0.01, 0.004, 0.05}, 0.003);
        }
        // the latch and its handle
        b.rounded_box(V3{cx - hinge_side * (half - 0.08), front + 0.045, 0.1 + door_h * 0.52}, V3{0.06, 0.004, 0.01}, 0.003);
        b.tube({V3{cx - hinge_side * (half - 0.07), front + 0.045, 0.1 + door_h * 0.45}, V3{cx - hinge_side * (half - 0.07), front + 0.07, 0.1 + door_h * 0.47},
                V3{cx - hinge_side * (half - 0.07), front + 0.07, 0.1 + door_h * 0.57}, V3{cx - hinge_side * (half - 0.07), front + 0.045, 0.1 + door_h * 0.59}},
               {0.007, 0.007, 0.007, 0.007}, true, 6);
    }
    // the door frame
    b.material = trim;
    {
        const double cx = s.doors == 2 ? 0.0 : s.door_at * hx;
        const double half = door_w;
        b.rounded_box(V3{cx, front + 0.005, 0.1 + door_h + 0.03}, V3{half + 0.04, 0.025, 0.03}, 0.006);
        for (int side = -1; side <= 1; side += 2)
            b.rounded_box(V3{cx + side * (half + 0.025), front + 0.005, 0.1 + door_h / 2}, V3{0.025, 0.025, door_h / 2}, 0.006);
    }
    // Windows: dark glass behind glazing bars in a trim frame, a sill; sometimes a box of flowers.
    for (int w = 0; w < s.windows; ++w) {
        double cx = 0;
        if (s.doors == 2)
            cx = (w == 0 ? -1 : 1) * hx * 0.62;
        else
            cx = s.windows == 1 ? -s.door_at * hx * 0.9 : (w == 0 ? -1 : 1) * hx * 0.25 - s.door_at * hx * 0.5;
        if (std::abs(cx - (s.doors == 2 ? 0.0 : s.door_at * hx)) < door_w + 0.4)
            cx = (s.door_at < 0 ? 1 : -1) * hx * 0.55;
        const double ww = 0.32;
        const double wh = 0.3;
        const double wz = hz - 0.65;
        b.material = glass;
        b.rounded_box(V3{cx, front - 0.005, wz}, V3{ww, 0.012, wh}, 0.004);
        b.material = trim;
        for (int side = -1; side <= 1; side += 2) {
            b.rounded_box(V3{cx + side * (ww + 0.02), front + 0.01, wz}, V3{0.025, 0.02, wh + 0.04}, 0.005);
            b.rounded_box(V3{cx, front + 0.01, wz + side * (wh + 0.02)}, V3{ww + 0.04, 0.02, 0.025}, 0.005);
        }
        b.rounded_box(V3{cx, front + 0.012, wz}, V3{0.012, 0.014, wh}, 0.003);
        b.rounded_box(V3{cx, front + 0.012, wz}, V3{ww, 0.014, 0.012}, 0.003);
        b.material = sill;
        b.rounded_box(V3{cx, front + 0.04, wz - wh - 0.05}, V3{ww + 0.08, 0.05, 0.015}, 0.005);
        if (s.window_box) {
            b.material = boards;
            b.rounded_box(V3{cx, front + 0.1, wz - wh - 0.15}, V3{ww + 0.04, 0.07, 0.07}, 0.01);
            b.material = soil;
            b.rounded_box(V3{cx, front + 0.1, wz - wh - 0.08}, V3{ww + 0.01, 0.055, 0.01}, 0.004);
            for (int k = 0; k < 9; ++k) {
                const double fx = cx - ww + (2 * ww) * (k + 0.5) / 9 + random_range(random, -0.02, 0.02);
                const double fy = front + 0.1 + random_range(random, -0.03, 0.03);
                b.material = leaf;
                b.ellipsoid(V3{fx, fy, wz - wh - 0.05}, V3{0.045, 0.035, 0.035});
                b.material = flower;
                b.sphere(V3{fx + random_range(random, -0.02, 0.02), fy + 0.02, wz - wh - 0.02 + random_range(random, 0, 0.03)}, 0.022);
            }
        }
    }
    // The roof: felt over boards, overhanging, with fascias; a ridge cap on an apex.
    const double over = 0.12;
    if (s.apex) {
        const double rise = s.ridge - hz;
        const double slope = std::atan2(rise, hy);
        const double run = std::hypot(hy + over, rise * (hy + over) / hy) / 2;
        const double low = hz - over * rise / hy;  // the eave edge, past the wall
        for (int side = -1; side <= 1; side += 2) {
            const V3 mid{0, side * (hy + over) / 2, (s.ridge + low) / 2 + 0.03};
            b.at = translation(mid) * rotation_x(-side * slope);
            b.material = felt;
            b.rounded_box(V3{0, 0, 0}, V3{hx + over, run, 0.025}, 0.008);
            for (double x = -hx; x <= hx + 1e-6; x += hx / 2)
                b.rounded_box(V3{x, 0, 0.028}, V3{0.015, run, 0.008}, 0.004);
            b.material = fascia;
            b.rounded_box(V3{0, side * (run - 0.01), -0.035}, V3{hx + over + 0.01, 0.012, 0.05}, 0.004);
            for (int sx = -1; sx <= 1; sx += 2)
                b.rounded_box(V3{sx * (hx + over), 0, -0.02}, V3{0.012, run, 0.06}, 0.004);
            b.at = Xform{};
        }
        b.material = felt;
        b.cylinder(V3{-hx - over, 0, s.ridge + 0.05}, V3{hx + over, 0, s.ridge + 0.05}, 0.035, 0.035, true);
    } else {
        const double rise = s.ridge - hz;
        const double slope = std::atan2(rise, 2 * hy);
        const double run = (hy + over) / std::cos(slope);
        b.at = translation(V3{0, 0, hz + rise / 2 + 0.04}) * rotation_x(slope);
        b.material = felt;
        b.rounded_box(V3{0, 0, 0}, V3{hx + over, run, 0.025}, 0.008);
        for (double x = -hx; x <= hx + 1e-6; x += hx / 2)
            b.rounded_box(V3{x, 0, 0.028}, V3{0.015, run, 0.008}, 0.004);
        b.material = fascia;
        for (int side = -1; side <= 1; side += 2)
            b.rounded_box(V3{0, side * (run - 0.01), -0.03}, V3{hx + over + 0.01, 0.012, 0.05}, 0.004);
        b.at = Xform{};
        // the front is high, so the gutter runs along the low back edge
        b.material = iron;
        b.cylinder(V3{-hx - over, -(hy + over), hz - over * std::tan(slope) - 0.01}, V3{hx + over, -(hy + over), hz - over * std::tan(slope) - 0.01}, 0.035, 0.035, true);
    }
    // A water butt at a front corner, and a spade against the wall.
    if (s.water_butt) {
        const double sx = s.door_at < 0 ? 1.0 : -1.0;
        b.at = translation(V3{sx * (hx - 0.25), hy + 0.3, 0});
        b.material = butt;
        b.lathe({0.0, 0.23, 0.26, 0.27, 0.26, 0.24, 0.0}, {0.0, 0.0, 0.08, 0.42, 0.78, 0.86, 0.87});
        b.torus(V3{0, 0, 0.3}, 0.268, 0.012);
        b.torus(V3{0, 0, 0.6}, 0.266, 0.012);
        b.material = iron;
        b.cylinder(V3{0.2, 0.12, 0.12}, V3{0.3, 0.17, 0.12}, 0.015, 0.012, true, 8);
        b.at = Xform{};
    }
    if (s.spade) {
        const double sx = s.door_at < 0 ? 0.75 : -0.75;
        b.at = translation(V3{sx * hx, hy + 0.12, 0}) * rotation_x(-0.22);
        b.material = handle;
        b.cylinder(V3{0, 0, 0.25}, V3{0, 0, 1.05}, 0.016, 0.016, true, 8);
        b.rounded_box(V3{0, 0, 1.08}, V3{0.07, 0.016, 0.016}, 0.008);
        b.material = blade;
        b.rounded_box(V3{0, 0, 0.14}, V3{0.095, 0.006, 0.13}, 0.02);
        b.at = Xform{};
    }
}

} // namespace

void draw_shed_model(Canvas& canvas, const Frame& frame, const Prop& prop, double heading, double hx, double hy) {
    const StillKey key = still_key(50, prop.seed * 17 + static_cast<std::uint64_t>(std::lround(heading * 100)), frame);
    const Shot* kept = find_still(key);
    if (kept == nullptr) {
        const ShedStyle style = style_for(prop.seed);
        Mesh mesh{};
        build_shed(mesh, detail_for(frame.ppm), style, hx, hy, prop.seed);
        bake_occlusion(mesh, 32);
        const std::vector<Material> materials = shed_materials(style);
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

} // namespace mm
