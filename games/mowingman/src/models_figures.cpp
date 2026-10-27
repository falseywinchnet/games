// The lounger and whoever is asleep on it.
//
// An aluminium frame on four legs, its sling woven in stripes, the back propped up at
// the head end. On it, someone dead to the world: bare legs crossed at the ankle, board
// shorts, a loud flowered shirt over a comfortable belly, a paperback face down on the
// chest under one hand, the other arm hanging over the side with its fingers in the
// grass, and a wide straw hat over the face. A glass of lemonade waits on the lawn.
//
// None of it moves, so it is drawn once and kept.
//
// Lounger metres: x across, y along (the head end at -y), z up.
#include "models.hpp"

#include <algorithm>
#include <cmath>

namespace mm {
namespace {

constexpr double pi = 3.14159265358979323846;

enum LoungerMaterial : std::uint16_t {
    aluminium,
    foot_cap,
    sling,
    skin,
    shorts,
    shirt,
    button,
    straw,
    ribbon,
    book_cover,
    pages,
    glass,
    lemonade,
    lemon,
    straw_tube,
    sole,
    nails,
    lounger_material_count
};

// The sling's surface: flat along the bed, then up the back; a little sag across.
V3 sling_point(double across, double along) {
    // along: 0 at the foot (+y) .. 1 at the top of the back (-y)
    const double hinge = 0.62;
    double y = 0;
    double z = 0;
    if (along < hinge) {
        y = 0.78 - along / hinge * 1.0;
        z = 0.37;
    } else {
        const double t = (along - hinge) / (1 - hinge);
        y = -0.22 - t * 0.5;
        z = 0.37 + t * 0.5;
    }
    const double sag = 0.035 * (1 - across * across) * (along < hinge ? 1.0 : 0.5);
    // up the back the sag is into the back, towards +y
    if (along < hinge)
        z -= sag;
    else
        y += sag * 0.7;
    return V3{across * 0.27, y, z};
}

void build_lounger(Mesh& mesh, double ppm, bool occupied) {
    Builder b(mesh, ppm);
    // The frame: two rails along the sides, bending up the back; the legs; the end bars.
    b.material = aluminium;
    for (int side = -1; side <= 1; side += 2) {
        const double x = side * 0.295;
        b.tube({V3{x, 0.8, 0.35}, V3{x, 0.0, 0.35}, V3{x, -0.2, 0.35}, V3{x, -0.24, 0.38}, V3{x, -0.48, 0.62}, V3{x, -0.72, 0.86}}, {0.016, 0.016, 0.016, 0.016, 0.016, 0.016});
        // legs at the foot and the head, and a prop under the back
        b.tube({V3{x, 0.72, 0.35}, V3{x * 1.05, 0.74, 0.0}}, {0.014, 0.014}, false);
        b.tube({V3{x, -0.18, 0.35}, V3{x * 1.05, -0.2, 0.0}}, {0.014, 0.014}, false);
        b.tube({V3{x, -0.08, 0.35}, V3{x, -0.42, 0.56}}, {0.011, 0.011}, false);
        b.material = foot_cap;
        b.cylinder(V3{x * 1.05, 0.74, -0.005}, V3{x * 1.05, 0.74, 0.025}, 0.02, 0.018, true);
        b.cylinder(V3{x * 1.05, -0.2, -0.005}, V3{x * 1.05, -0.2, 0.025}, 0.02, 0.018, true);
        b.material = aluminium;
    }
    b.tube({V3{-0.295, 0.8, 0.35}, V3{0.295, 0.8, 0.35}}, {0.016, 0.016}, false);
    b.tube({V3{-0.295, -0.72, 0.86}, V3{0.295, -0.72, 0.86}}, {0.016, 0.016}, false);
    b.tube({V3{-0.295, 0.72, 0.1}, V3{0.295, 0.72, 0.1}}, {0.01, 0.01}, false);

    // The sling.
    b.material = sling;
    {
        const int rows = 28;
        const int cols = 9;
        std::vector<V3> points{};
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
                points.push_back(sling_point(-1 + 2.0 * c / (cols - 1), static_cast<double>(r) / (rows - 1)));
        b.grid(points, rows, cols, false);
    }

    if (!occupied) {
        bake_occlusion(mesh, 32);
        return;
    }

    // Legs, crossed at the ankle: hips by the hinge, knees a little raised, feet at the end.
    const V3 hip[2] = {V3{-0.09, -0.12, 0.43}, V3{0.09, -0.12, 0.43}};
    const V3 knee[2] = {V3{-0.075, 0.27, 0.47}, V3{0.085, 0.27, 0.5}};
    const V3 ankle[2] = {V3{-0.03, 0.66, 0.405}, V3{0.0, 0.64, 0.46}};
    for (int k = 0; k < 2; ++k) {
        b.material = skin;
        b.tube({hip[k], lerp(hip[k], knee[k], 0.5), knee[k]}, {0.085, 0.078, 0.06}, true);
        b.tube({knee[k], lerp(knee[k], ankle[k], 0.35), ankle[k]}, {0.058, 0.055, 0.038}, true);
        // a bare foot, toes up, with its sole
        const V3 toe = ankle[k] + V3{k == 0 ? -0.01 : 0.02, 0.07, 0.07};
        b.tube({ankle[k] + V3{0, -0.02, 0}, ankle[k] + V3{0, 0.04, 0.03}, toe}, {0.042, 0.044, 0.036}, true);
        for (int t = 0; t < 5; ++t) {
            const double spread = (t - 2) * 0.016 + (k == 0 ? -0.004 : 0.004);
            b.sphere(toe + V3{spread, 0.02 - std::abs(t - 1.5) * 0.004, 0.028 - t * 0.003}, 0.011 - t * 0.0008);
        }
        b.material = sole;
        b.ellipsoid(ankle[k] + V3{0, 0.065, 0.02}, V3{0.044, 0.02, 0.07});
    }
    // board shorts over the hips and thighs, with a drawstring
    b.material = shorts;
    for (int k = 0; k < 2; ++k)
        b.tube({hip[k] + V3{0, -0.03, 0}, lerp(hip[k], knee[k], 0.55)}, {0.1, 0.092}, true);
    b.ellipsoid(V3{0, -0.15, 0.44}, V3{0.19, 0.12, 0.09});

    // The body, lying back up the back of the lounger: belly, chest, shoulders; then the shirt.
    const Xform torso_frame = aimed(V3{0, -0.15, 0.45}, V3{0, -0.42, 0.33});
    {
        const Xform keep = b.at;
        b.at = torso_frame;
        b.material = shirt;
        b.loft({Builder::Section{0.0, 0.18, 0.1, 0.02, 2.6}, Builder::Section{0.08, 0.195, 0.14, 0.05, 2.4}, Builder::Section{0.18, 0.2, 0.15, 0.06, 2.3},
                Builder::Section{0.3, 0.205, 0.12, 0.045, 2.5}, Builder::Section{0.42, 0.215, 0.095, 0.03, 2.8}, Builder::Section{0.5, 0.2, 0.085, 0.02, 2.8},
                Builder::Section{0.54, 0.1, 0.05, 0.02, 2.4}});
        // the open collar showing the throat, the buttons down the placket
        b.material = skin;
        b.ellipsoid(V3{0.5, 0, 0.07}, V3{0.07, 0.07, 0.04});
        b.material = button;
        for (int k = 0; k < 4; ++k) {
            const double x = 0.1 + k * 0.1;
            const double hump = k == 1 ? 0.155 : k == 0 ? 0.135 : k == 2 ? 0.145 : 0.115;
            b.sphere(V3{x, 0, hump + 0.045}, 0.009);
        }
        b.at = keep;
    }
    // neck, and the head lying back with the hat over the face
    const V3 neck = torso_frame.point(V3{0.56, 0, 0.03});
    const V3 head = torso_frame.point(V3{0.68, 0, 0.06});
    b.material = skin;
    b.tube({torso_frame.point(V3{0.48, 0, 0.03}), neck}, {0.055, 0.05}, true);
    b.ellipsoid(head, V3{0.1, 0.095, 0.11});
    for (int s = -1; s <= 1; s += 2)
        b.ellipsoid(head + V3{s * 0.092, 0.0, -0.01}, V3{0.014, 0.024, 0.032});
    // The hat, tipped over the face: a broad woven brim, the crown, a ribbon round it.
    {
        const Xform keep = b.at;
        b.at = translation(head + V3{0, 0.03, 0.105}) * rotation_x(-0.75);
        b.material = straw;
        b.lathe({0.0, 0.2, 0.235, 0.25, 0.24, 0.1, 0.098, 0.09, 0.06, 0.0}, {0.0, 0.0, -0.01, -0.025, -0.03, 0.005, 0.06, 0.1, 0.115, 0.118});
        b.material = ribbon;
        b.lathe({0.101, 0.1, 0.1, 0.101}, {0.004, 0.006, 0.035, 0.037});
        b.at = keep;
    }

    // The left arm across the belly, its hand on the paperback face down on the chest.
    b.material = shirt;
    const V3 left_shoulder = torso_frame.point(V3{0.46, -0.21, 0.03});
    const V3 left_elbow = torso_frame.point(V3{0.22, -0.24, 0.11});
    const V3 left_wrist = torso_frame.point(V3{0.3, -0.03, 0.2});
    b.tube({left_shoulder, lerp(left_shoulder, left_elbow, 0.5)}, {0.068, 0.062}, true);
    b.material = skin;
    b.tube({lerp(left_shoulder, left_elbow, 0.45), left_elbow, left_wrist}, {0.05, 0.045, 0.036}, true);
    b.ellipsoid(left_wrist + V3{0.03, -0.02, 0.01}, V3{0.05, 0.035, 0.022});
    {
        const Xform keep = b.at;
        b.at = torso_frame * translation(V3{0.33, 0.04, 0.185}) * rotation_z(0.25) * rotation_x(0.0);
        b.material = book_cover;
        b.rounded_box(V3{0, -0.06, 0.008}, V3{0.07, 0.058, 0.005}, 0.003);
        b.rounded_box(V3{0, 0.06, 0.008}, V3{0.07, 0.058, 0.005}, 0.003);
        b.material = pages;
        b.rounded_box(V3{0, -0.06, -0.002}, V3{0.066, 0.054, 0.006}, 0.002);
        b.rounded_box(V3{0, 0.06, -0.002}, V3{0.066, 0.054, 0.006}, 0.002);
        b.at = keep;
    }
    // The right arm over the side, hanging down, the fingers brushing the grass.
    b.material = shirt;
    const V3 right_shoulder = torso_frame.point(V3{0.46, 0.21, 0.03});
    const V3 right_elbow = V3{0.37, -0.32, 0.3};
    const V3 right_wrist = V3{0.42, -0.25, 0.08};
    b.tube({right_shoulder, lerp(right_shoulder, right_elbow, 0.45)}, {0.068, 0.062}, true);
    b.material = skin;
    b.tube({lerp(right_shoulder, right_elbow, 0.4), right_elbow, right_wrist}, {0.05, 0.045, 0.036}, true);
    b.ellipsoid(right_wrist + V3{0.005, 0.01, -0.045}, V3{0.024, 0.042, 0.05});
    for (int f = 0; f < 4; ++f)
        b.tube({right_wrist + V3{0.0, -0.015 + f * 0.012, -0.08}, right_wrist + V3{0.004, -0.018 + f * 0.013, -0.12}}, {0.008, 0.007}, true, 6);

    // The lemonade on the grass by the hand: a tall glass, the drink, a slice of lemon, a straw.
    const V3 cup{0.5, -0.05, 0.0};
    b.material = lemonade;
    b.cylinder(cup + V3{0, 0, 0.008}, cup + V3{0, 0, 0.12}, 0.033, 0.037, true);
    b.material = glass;
    b.lathe({0.0, 0.036, 0.038, 0.042, 0.043, 0.04}, {0.0, 0.0, 0.006, 0.15, 0.152, 0.15});
    b.material = lemon;
    b.at = translation(cup + V3{0.038, 0, 0.145}) * rotation_y(pi / 2 - 0.2);
    b.cylinder(V3{0, 0, -0.004}, V3{0, 0, 0.004}, 0.028, 0.028, true);
    b.at = Xform{};
    b.material = straw_tube;
    b.tube({cup + V3{-0.01, 0.0, 0.03}, cup + V3{-0.02, 0.01, 0.17}, cup + V3{-0.045, 0.02, 0.2}}, {0.0045, 0.0045, 0.0045}, true, 6);
    bake_occlusion(mesh, 40);
}

std::vector<Material> lounger_materials(std::uint64_t seed) {
    std::uint64_t random = seed | 1U;
    const unsigned slings[4] = {0x2F7FC1, 0xD9483B, 0x3F9A6A, 0xE7A33C};
    const unsigned skins[5] = {0xF1C7A5, 0xE0A882, 0xC68863, 0x9C6644, 0x6E4630};
    const unsigned shirts[4] = {0x1F78B4, 0xD33F49, 0x1B998B, 0xF29E4C};
    const unsigned prints[4] = {0xF6D2E0, 0xF7E27A, 0xFFFFFF, 0xE85D75};
    const unsigned shorts_colours[4] = {0x2D3E50, 0x7A8B5A, 0xC2B280, 0x5B4E8C};
    const unsigned covers[3] = {0x8E2C2C, 0x2C5D8E, 0x3D6B3D};
    std::vector<Material> mat(lounger_material_count);
    mat[aluminium] = paint_material(0xC9CDD1, 0.7F, 0.5F);
    mat[aluminium].metal = 0.9F;
    mat[aluminium].pattern = Pattern::metal_brushed;
    mat[foot_cap] = paint_material(0x222222, 0.4F, 0.04F);
    mat[sling] = paint_material(0xF6F2E6, 0.25F, 0.03F);
    mat[sling].tint = hex(slings[next_random(random) % 4U]);
    mat[sling].pattern = Pattern::weave;
    mat[sling].level = 9;
    mat[sling].scale = 6;
    mat[sling].two_sided = true;
    mat[sling].wrap = 0.3F;
    const std::size_t shirt_pick = next_random(random) % 4U;
    mat[skin] = paint_material(skins[next_random(random) % 5U], 0.45F, 0.035F);
    mat[skin].pattern = Pattern::skin;
    mat[skin].wrap = 0.4F;
    mat[shorts] = paint_material(shorts_colours[next_random(random) % 4U], 0.3F, 0.035F);
    mat[shorts].wrap = 0.25F;
    mat[shirt] = paint_material(shirts[shirt_pick], 0.3F, 0.035F);
    mat[shirt].tint = hex(prints[shirt_pick]);
    mat[shirt].pattern = Pattern::blossom;
    mat[shirt].scale = 5;
    mat[shirt].wrap = 0.3F;
    mat[button] = paint_material(0xF4EFE2, 0.8F, 0.06F);
    mat[straw] = paint_material(0xE3C98C, 0.3F, 0.035F);
    mat[straw].pattern = Pattern::weave;
    mat[straw].tint = hex(0xD4B572);
    mat[straw].level = 0;
    mat[straw].scale = 9;
    mat[straw].wrap = 0.25F;
    mat[ribbon] = paint_material(0x9A2E2E, 0.5F, 0.045F);
    mat[book_cover] = paint_material(covers[next_random(random) % 3U], 0.6F, 0.05F);
    mat[pages] = paint_material(0xF2EAD4, 0.2F, 0.03F);
    mat[glass] = paint_material(0xE8F4F8, 0.96F, 0.08F);
    mat[glass].opacity = 0.28F;
    mat[lemonade] = paint_material(0xF5E27A, 0.85F, 0.05F);
    mat[lemonade].opacity = 0.8F;
    mat[lemon] = paint_material(0xF2D33A, 0.6F, 0.05F);
    mat[straw_tube] = paint_material(0xE8434F, 0.7F, 0.05F);
    mat[sole] = paint_material(0xD9A688, 0.4F, 0.035F);
    mat[nails] = paint_material(0xF0D8CC, 0.7F, 0.05F);
    return mat;
}

} // namespace

void draw_lounger_model(Canvas& canvas, const Frame& frame, const Prop& prop, double heading) {
    const StillKey key = still_key(prop.occupied ? 21 : 20, prop.seed * 31 + static_cast<std::uint64_t>(std::lround(heading * 100)), frame);
    const Shot* kept = find_still(key);
    if (kept == nullptr) {
        Mesh mesh{};
        build_lounger(mesh, detail_for(frame.ppm), prop.occupied);
        const std::vector<Material> materials = lounger_materials(prop.seed);
        std::vector<Part> parts{};
        // The lounger is drawn along y with its head end at -y; turned, it lies along x.
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
