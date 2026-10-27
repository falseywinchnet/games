// The old lady whose flowers they were, a little larger than life as the gnome is:
// sensible black shoes and tan stockings, a full lilac skirt printed with flowers under
// a frilled white apron, a knitted cardigan buttoned over her blouse with its lace
// collar, a string of pearls, a silver-grey perm with a bun on top, round spectacles,
// a fierce frown, and the rolling pin up in her right hand.
//
// She is jointed like the gnome: hips, knees, the body leaning over them, the head on
// its neck, the arms at the shoulder and elbow, and the pin in her fist. Her stride
// (metres walked) drives the walk; her state says whether she marches, glares, jumps
// or runs.
//
// Her metres: x where she faces, y to her right, z up, feet on the origin.
#include "models.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace mm {
namespace {

constexpr double pi = 3.14159265358979323846;

enum GrannyMaterial : std::uint16_t {
    shoe,
    stocking,
    skirt,
    apron,
    cardigan,
    blouse,
    button,
    pearl,
    skin,
    blush,
    hair,
    eye,
    brow,
    lips,
    glasses_wire,
    lens,
    pin_wood,
    pin_handle,
    granny_material_count
};

struct GrannyModel {
    Mesh shoe[2]{};
    Mesh shin{};
    Mesh thigh{};
    Mesh pelvis{};
    Mesh torso{};
    Mesh head{};
    Mesh upper_arm{};
    Mesh forearm[2]{};
    Mesh pin{};
    std::vector<Material> materials{};
    std::unique_ptr<GrannyModel> coarse{};
};

constexpr double hip_z = 0.86;
constexpr double hip_y = 0.085;
constexpr double thigh_length = 0.42;
constexpr double shin_length = 0.38;
constexpr double waist_z = 0.96;
constexpr double shoulder_y = 0.19;
constexpr double shoulder_z = 1.33;
constexpr double neck_z = 1.4;
constexpr double upper_arm_length = 0.27;
constexpr double forearm_length = 0.24;

void build_granny(GrannyModel& model, double ppm, bool bake) {
    // Shoes about the ankle: a low black lace-up with a little heel and a strap.
    for (int side = 0; side < 2; ++side) {
        Builder b(model.shoe[side], ppm);
        b.material = shoe;
        b.ellipsoid(V3{0.05, 0, 0.035}, V3{0.115, 0.05, 0.038});
        b.rounded_box(V3{-0.045, 0, 0.022}, V3{0.03, 0.035, 0.022}, 0.01);
        b.cylinder(V3{0, 0, 0.03}, V3{0, 0, 0.07}, 0.047, 0.044, true);
        b.material = stocking;
        b.torus(V3{0.06, 0, 0.06}, 0.03, 0.004);
        if (bake)
            bake_occlusion(model.shoe[side], 20);
    }
    // Shin from the knee, thigh from the hip, in stockings (the skirt hides most of it).
    {
        Builder b(model.shin, ppm);
        b.material = stocking;
        b.tube({V3{0, 0, 0.01}, V3{0.012, 0, -0.12}, V3{0.004, 0, -shin_length + 0.02}, V3{0, 0, -shin_length - 0.01}}, {0.055, 0.058, 0.042, 0.04}, true);
        if (bake)
            bake_occlusion(model.shin, 12);
    }
    {
        Builder b(model.thigh, ppm);
        b.material = stocking;
        b.tube({V3{0, 0, 0.02}, V3{0, 0, -thigh_length}}, {0.075, 0.058}, true);
        if (bake)
            bake_occlusion(model.thigh, 12);
    }
    // About the hips: the full skirt, its print and hem, and the apron over the front.
    {
        Builder b(model.pelvis, ppm);
        b.at = translation(V3{0, 0, -hip_z});
        b.material = skirt;
        b.lathe({0.0, 0.31, 0.335, 0.33, 0.305, 0.26, 0.21, 0.17, 0.155, 0.0}, {0.37, 0.37, 0.39, 0.43, 0.55, 0.7, 0.84, 0.94, 0.985, 0.99});
        b.torus(V3{0, 0, 0.385}, 0.33, 0.012);
        // the apron: a curved white panel down the front with a frilled hem, and its ties
        b.material = apron;
        b.lathe({0.338, 0.336, 0.31, 0.265, 0.215, 0.175}, {0.44, 0.47, 0.58, 0.72, 0.85, 0.94}, 0, -0.75, 0.75);
        for (int k = 0; k < 13; ++k) {
            const double a = -0.72 + 1.44 * k / 12;
            b.sphere(V3{std::cos(a) * 0.345, std::sin(a) * 0.345, 0.44}, 0.022);
        }
        b.lathe({0.162, 0.168, 0.168, 0.162}, {0.925, 0.93, 0.955, 0.96});
        b.at = translation(V3{-0.165, 0, 0.94 - hip_z});
        b.ellipsoid(V3{0, -0.03, 0}, V3{0.02, 0.035, 0.022});
        b.ellipsoid(V3{0, 0.03, 0}, V3{0.02, 0.035, 0.022});
        b.tube({V3{-0.01, -0.02, -0.01}, V3{-0.02, -0.035, -0.12}}, {0.01, 0.008}, true);
        b.tube({V3{-0.01, 0.02, -0.01}, V3{-0.02, 0.04, -0.14}}, {0.01, 0.008}, true);
        if (bake)
            bake_occlusion(model.pelvis, 32);
    }
    // About the waist: the cardigan over her bust and shoulders, its buttons and pockets,
    // the blouse's lace collar, the pearls.
    {
        Builder b(model.torso, ppm);
        b.material = cardigan;
        b.at = translation(V3{0, 0, -waist_z}) * scaling(1.12, 1, 1);
        b.lathe({0.0, 0.158, 0.168, 0.178, 0.188, 0.19, 0.185, 0.17, 0.12, 0.06, 0.0},
                {0.93, 0.93, 0.99, 1.07, 1.15, 1.22, 1.28, 1.335, 1.375, 1.395, 1.4});
        b.at = translation(V3{0, 0, -waist_z});
        // ribbed hem of the cardigan
        b.torus(V3{0, 0, 0.94}, 0.165, 0.014);
        b.material = button;
        for (int k = 0; k < 5; ++k)
            b.sphere(V3{0.188 + (k == 1 || k == 2 ? 0.018 : 0.0), 0, 0.99 + k * 0.075}, 0.012);
        b.material = cardigan;
        for (int s = -1; s <= 1; s += 2) {
            b.at = translation(V3{0.13, s * 0.12, 1.02 - waist_z}) * rotation_z(s * 0.75);
            b.rounded_box(V3{0, 0, 0}, V3{0.012, 0.055, 0.045}, 0.01);
            b.at = translation(V3{0, 0, -waist_z});
        }
        b.material = blouse;
        for (int k = 0; k < 14; ++k) {
            const double a = -1.75 + 3.5 * k / 13;
            b.ellipsoid(V3{std::cos(a) * 0.066 + 0.006, std::sin(a) * 0.062, 1.378}, V3{0.016, 0.016, 0.009});
        }
        b.cylinder(V3{0, 0, 1.37}, V3{0, 0, 1.43}, 0.058, 0.052, true);
        b.material = pearl;
        for (int k = 0; k < 17; ++k) {
            const double a = -1.5 + 3.0 * k / 16;
            const double sag = 0.05 * std::cos(a * 1.05);
            b.sphere(V3{std::cos(a) * 0.1 + 0.03 + sag * 0.5, std::sin(a) * 0.1, 1.355 - sag}, 0.011);
        }
        if (bake)
            bake_occlusion(model.torso, 32);
    }
    // The head about the neck: face, ears and pearl drops, nose, eyes, a fierce frown,
    // pursed lips, spectacles; then the perm and the bun with its pin.
    {
        Builder b(model.head, ppm);
        const V3 c{0.005, 0, 0.13};
        b.material = skin;
        b.ellipsoid(c, V3{0.1, 0.092, 0.118});
        b.cylinder(V3{0, 0, -0.04}, V3{0, 0, 0.05}, 0.045, 0.045, false);
        for (int s = -1; s <= 1; s += 2) {
            b.ellipsoid(c + V3{-0.005, s * 0.09, 0.0}, V3{0.018, 0.014, 0.03});
            b.material = pearl;
            b.sphere(c + V3{0.0, s * 0.095, -0.035}, 0.011);
            b.material = skin;
        }
        b.ellipsoid(c + V3{0.1, 0, 0.0}, V3{0.03, 0.022, 0.035});
        b.material = blush;
        for (int s = -1; s <= 1; s += 2)
            b.ellipsoid(c + V3{0.075, s * 0.05, -0.025}, V3{0.03, 0.03, 0.025});
        for (int s = -1; s <= 1; s += 2) {
            const V3 at = c + V3{0.083, s * 0.036, 0.02};
            b.material = eye;
            b.ellipsoid(at, V3{0.01, 0.012, 0.009});
            // brows drawn down hard towards the nose
            b.material = brow;
            b.tube({at + V3{0.008, -s * 0.012, 0.012}, at + V3{0.004, s * 0.008, 0.024}, at + V3{-0.004, s * 0.026, 0.03}}, {0.007, 0.008, 0.006}, true);
            // spectacles: a wire rim and a lens, an arm back to the ear
            b.material = glasses_wire;
            b.at = translation(at + V3{0.016, 0, 0.0}) * rotation_y(pi / 2 - 0.1);
            b.torus(V3{0, 0, 0}, 0.024, 0.0025, 0, 6);
            b.at = Xform{};
            b.tube({at + V3{0.012, s * 0.024, 0.002}, c + V3{0.0, s * 0.092, 0.025}}, {0.0022, 0.0022}, false, 5);
            b.material = lens;
            b.at = translation(at + V3{0.017, 0, 0.0}) * rotation_y(pi / 2 - 0.1);
            b.cylinder(V3{0, 0, -0.001}, V3{0, 0, 0.001}, 0.023, 0.023, true);
            b.at = Xform{};
        }
        b.material = glasses_wire;
        b.tube({c + V3{0.1, -0.013, 0.025}, c + V3{0.106, 0, 0.028}, c + V3{0.1, 0.013, 0.025}}, {0.0025, 0.0025, 0.0025}, false, 5);
        // pursed lips, turned down at the corners
        b.material = lips;
        b.sweep({c + V3{0.083, -0.03, -0.062}, c + V3{0.094, -0.012, -0.056}, c + V3{0.097, 0, -0.055}, c + V3{0.094, 0.012, -0.056}, c + V3{0.083, 0.03, -0.062}},
                {0.004, 0.007, 0.008, 0.007, 0.004}, {0.004, 0.006, 0.007, 0.006, 0.004}, 2.5, V3{1, 0, 0});
        // the perm: tight curls over the crown and round the back, and the bun on top
        b.material = hair;
        std::uint64_t random = 17;
        for (int k = 0; k < 46; ++k) {
            const double u = (k + 0.5) / 46;
            const double z = 1 - 1.55 * u;  // from the crown down the back
            const double a = k * 2.399963;
            const double ring = std::sqrt(std::max(0.0, 1 - z * z));
            V3 d{std::cos(a) * ring, std::sin(a) * ring, z};
            if (d.x > 0.35 && d.z < 0.55)
                d = V3{-d.x * 0.6, d.y, d.z};  // keep the curls off her face
            const double r = 0.032 + 0.01 * hash01(random);
            b.sphere(c + V3{d.x * 0.1, d.y * 0.095, d.z * 0.12 + 0.02}, r);
        }
        b.sphere(c + V3{-0.035, 0, 0.165}, 0.062);
        b.torus(c + V3{-0.035, 0, 0.13}, 0.05, 0.016);
        b.material = glasses_wire;
        b.tube({c + V3{-0.09, -0.04, 0.18}, c + V3{0.02, 0.05, 0.15}}, {0.003, 0.003}, true, 5);
        if (bake)
            bake_occlusion(model.head, 32);
    }
    // An arm in its cardigan sleeve, and each forearm with its hand. The right hand is a
    // fist round the pin; the left an open hand.
    {
        Builder b(model.upper_arm, ppm);
        b.material = cardigan;
        b.tube({V3{0, 0, 0.03}, V3{0, 0, -upper_arm_length}}, {0.058, 0.048}, true);
        if (bake)
            bake_occlusion(model.upper_arm, 12);
    }
    for (int side = 0; side < 2; ++side) {
        Builder b(model.forearm[side], ppm);
        b.material = cardigan;
        b.tube({V3{0, 0, 0.01}, V3{0, 0, -forearm_length}}, {0.046, 0.04}, true);
        b.torus(V3{0, 0, -forearm_length}, 0.038, 0.01);
        b.material = skin;
        if (side == 1) {
            b.ellipsoid(V3{0.005, 0, -forearm_length - 0.045}, V3{0.035, 0.03, 0.045});
        } else {
            b.ellipsoid(V3{0, 0, -forearm_length - 0.055}, V3{0.018, 0.04, 0.06});
            b.ellipsoid(V3{0.022, -0.03, -forearm_length - 0.04}, V3{0.012, 0.012, 0.028});
        }
        if (bake)
            bake_occlusion(model.forearm[side], 12);
    }
    // The rolling pin, across her fist: the turned barrel and its two handles.
    {
        Builder b(model.pin, ppm);
        b.material = pin_wood;
        b.at = rotation_x(pi / 2);
        b.lathe({0.0, 0.03, 0.036, 0.036, 0.03, 0.0}, {-0.2, -0.2, -0.19, 0.19, 0.2, 0.2});
        b.material = pin_handle;
        b.lathe({0.0, 0.012, 0.016, 0.018, 0.012, 0.0}, {0.2, 0.2, 0.24, 0.27, 0.3, 0.3});
        b.lathe({0.0, 0.012, 0.018, 0.016, 0.012, 0.0}, {-0.3, -0.3, -0.27, -0.24, -0.2, -0.2});
        if (bake)
            bake_occlusion(model.pin, 12);
    }

    std::vector<Material>& mat = model.materials;
    mat.assign(granny_material_count, Material{});
    mat[shoe] = paint_material(0x161210, 0.72F, 0.05F);
    mat[stocking] = paint_material(0xC8A07E, 0.45F, 0.04F);
    mat[stocking].sheen = 0.4F;
    mat[stocking].wrap = 0.2F;
    mat[skirt] = paint_material(0x9C7FCE, 0.25F, 0.035F);
    mat[skirt].pattern = Pattern::floral;
    mat[skirt].tint = hex(0xF4D3E6);
    mat[skirt].scale = 14;
    mat[skirt].wrap = 0.2F;
    mat[apron] = paint_material(0xFFFDF8, 0.3F, 0.035F);
    mat[apron].pattern = Pattern::stripes;
    mat[apron].tint = hex(0xF1E6E8);
    mat[apron].scale = 9;
    mat[apron].two_sided = true;
    mat[apron].wrap = 0.3F;
    mat[cardigan] = paint_material(0xC9A062, 0.15F, 0.03F);
    mat[cardigan].pattern = Pattern::knit;
    mat[cardigan].scale = 40;
    mat[cardigan].wrap = 0.35F;
    mat[cardigan].sheen = 0.3F;
    mat[blouse] = paint_material(0xFBF8F0, 0.3F, 0.035F);
    mat[blouse].wrap = 0.3F;
    mat[button] = paint_material(0x6B4A2A, 0.75F, 0.06F);
    mat[pearl] = paint_material(0xF3EEE4, 0.92F, 0.09F);
    mat[pearl].sheen = 0.6F;
    mat[skin] = paint_material(0xEDC6A8, 0.42F, 0.035F);
    mat[skin].pattern = Pattern::skin;
    mat[skin].wrap = 0.4F;
    mat[blush] = paint_material(0xE9A592, 0.4F, 0.035F);
    mat[blush].wrap = 0.4F;
    mat[hair] = paint_material(0xC9CBD4, 0.5F, 0.05F);
    mat[hair].pattern = Pattern::hair;
    mat[hair].scale = 3;
    mat[hair].sheen = 0.4F;
    mat[eye] = paint_material(0x1E2228, 0.9F, 0.06F);
    mat[brow] = paint_material(0x9EA0A8, 0.3F, 0.04F);
    mat[lips] = paint_material(0xB8576A, 0.6F, 0.05F);
    mat[glasses_wire] = paint_material(0xC8A85A, 0.85F, 0.6F);
    mat[glasses_wire].metal = 1;
    mat[lens] = paint_material(0xDDEEFF, 0.97F, 0.06F);
    mat[lens].opacity = 0.22F;
    mat[pin_wood] = paint_material(0xD6A86C, 0.45F, 0.04F);
    mat[pin_wood].pattern = Pattern::wood;
    mat[pin_wood].scale = 3;
    mat[pin_handle] = paint_material(0xA8743E, 0.55F, 0.045F);
    mat[pin_handle].pattern = Pattern::wood;
}

struct GrannyKit {
    std::mutex guard{};
    std::map<long long, std::unique_ptr<GrannyModel>> models{};
};

GrannyKit& granny_kit() {
    static GrannyKit kit{};
    return kit;
}

const GrannyModel& granny_model(double ppm) {
    GrannyKit& kit = granny_kit();
    const std::lock_guard<std::mutex> lock(kit.guard);
    const double level = detail_for(ppm * 0.7);
    std::unique_ptr<GrannyModel>& slot = kit.models[std::llround(level)];
    if (!slot) {
        slot = std::make_unique<GrannyModel>();
        build_granny(*slot, level, true);
        (*slot).coarse = std::make_unique<GrannyModel>();
        build_granny(*(*slot).coarse, level / 3, false);
    }
    return *slot;
}

struct Stance {
    double bob{};
    double lean{};       // forward, radians
    double sway{};       // to her right
    double twist{};      // hips turning with the step
    double head_nod{};
    double head_turn{};
    double thigh[2]{};   // swung forward from the hip
    double knee[2]{};    // bend
    double forward[2]{};
    double out[2]{};
    double elbow[2]{};
    double twist_arm[2]{};
    double pin_roll{};   // the pin turned in her fist
};

Stance stance_for(const Granny& granny) {
    Stance s{};
    const bool running = granny.state == GrannyState::fleeing;
    const double step = running ? 1.1 : 0.9;
    const double phase = granny.stride / step * 2 * pi;
    const double swing = std::sin(phase);
    const double lift = std::max(0.0, std::cos(phase));
    const double t = granny.clock;
    if (granny.state == GrannyState::walking) {
        // A brisk, furious march: legs swinging, the left fist pumping, the pin shaken over her head.
        s.thigh[0] = 0.38 * swing;
        s.thigh[1] = -0.38 * swing;
        s.knee[0] = 0.55 * std::max(0.0, -std::cos(phase + 0.6));
        s.knee[1] = 0.55 * std::max(0.0, std::cos(phase + 0.6));
        s.bob = 0.018 * std::abs(std::cos(phase));
        s.lean = 0.12;
        s.twist = 0.08 * swing;
        s.sway = 0.03 * swing;
        s.forward[0] = -0.55 * swing + 0.2;
        s.out[0] = 0.25;
        s.elbow[0] = 1.25;
        s.forward[1] = 2.55 + 0.25 * std::sin(phase * 2);
        s.out[1] = 0.25;
        s.elbow[1] = 0.55 + 0.3 * std::sin(phase * 2);
        s.pin_roll = 0.2 * std::sin(phase * 2);
        s.head_nod = 0.1;
    } else if (granny.state == GrannyState::turning) {
        // Stopped to look for it: glaring, the pin wagging at it.
        s.lean = 0.05;
        s.forward[0] = 0.7;
        s.out[0] = 0.55;
        s.elbow[0] = 2.2;  // hand on her hip
        s.twist_arm[0] = 1.0;
        s.forward[1] = 2.2;
        s.out[1] = 0.35;
        s.elbow[1] = 0.9 + 0.35 * std::sin(t * 9);
        s.pin_roll = 0.3 * std::sin(t * 9);
        s.head_turn = 0.25 * std::sin(t * 2.5);
        s.head_nod = 0.12;
    } else if (granny.state == GrannyState::startled) {
        // A gnome! She jumps back, arms flung up.
        const double jump = std::sin(std::min(1.0, t / 0.35) * pi);
        s.bob = 0.06 * jump;
        s.lean = -0.22;
        s.forward[0] = s.forward[1] = 0.6;
        s.out[0] = s.out[1] = 2.5;
        s.elbow[0] = s.elbow[1] = 0.5;
        s.pin_roll = 1.2 * std::min(1.0, t / 0.3);
        s.thigh[0] = 0.25 * jump;
        s.knee[0] = 0.5 * jump;
        s.head_nod = -0.2;
    } else if (running) {
        // Off she goes, flapping.
        s.thigh[0] = 0.6 * swing;
        s.thigh[1] = -0.6 * swing;
        s.knee[0] = 1.0 * std::max(0.0, -std::cos(phase + 0.5));
        s.knee[1] = 1.0 * std::max(0.0, std::cos(phase + 0.5));
        s.bob = 0.04 * lift;
        s.lean = 0.25;
        s.sway = 0.06 * swing;
        for (int k = 0; k < 2; ++k) {
            s.forward[k] = 1.2 + 0.5 * std::sin(phase * 2 + k * pi);
            s.out[k] = 1.9 + 0.4 * std::sin(phase * 2 + k * 2);
            s.elbow[k] = 0.4;
        }
        s.pin_roll = phase;
        s.head_nod = -0.1;
    }
    return s;
}

} // namespace

void draw_granny(Canvas& canvas, const Frame& frame, const Granny& granny) {
    if (granny.state == GrannyState::away)
        return;
    const GrannyModel& model = granny_model(frame.ppm);
    const GrannyModel& coarse = *model.coarse;
    const Stance s = stance_for(granny);
    const double size = 1.1;
    const Xform root = translation(V3{granny.x, granny.y, s.bob * size}) * rotation_z(granny.facing) * scaling(size, size, size);
    std::vector<Part> parts{};
    const Xform hips = root * translation(V3{0, 0, hip_z}) * rotation_z(s.twist) * rotation_x(s.sway);
    for (int side = 0; side < 2; ++side) {
        const double y = side == 0 ? -hip_y : hip_y;
        const Xform thigh = hips * translation(V3{0, y, 0}) * rotation_y(-s.thigh[side]);
        parts.push_back(Part{&model.thigh, &model.materials, thigh, false, &coarse.thigh});
        const Xform knee = thigh * translation(V3{0, 0, -thigh_length}) * rotation_y(s.knee[side]);
        parts.push_back(Part{&model.shin, &model.materials, knee, false, &coarse.shin});
        const Xform ankle = knee * translation(V3{0, 0, -shin_length}) * rotation_y(s.thigh[side] - s.knee[side]);
        parts.push_back(Part{&model.shoe[side], &model.materials, ankle * translation(V3{0, 0, -0.06}), false, &coarse.shoe[side]});
    }
    parts.push_back(Part{&model.pelvis, &model.materials, hips, false, &coarse.pelvis});
    const Xform torso = hips * translation(V3{0, 0, waist_z - hip_z}) * rotation_y(s.lean) * rotation_z(-s.twist * 0.6);
    parts.push_back(Part{&model.torso, &model.materials, torso, false, &coarse.torso});
    // Her chin up, glaring: it shows her face from up here.
    const Xform head = torso * translation(V3{0.01, 0, neck_z - waist_z}) * rotation_z(s.head_turn) * rotation_y(s.head_nod - 0.22) * scaling(1.12, 1.12, 1.12);
    parts.push_back(Part{&model.head, &model.materials, head, false, &coarse.head});
    for (int side = 0; side < 2; ++side) {
        const double sign = side == 0 ? -1.0 : 1.0;
        const Xform shoulder = torso * translation(V3{0, sign * shoulder_y, shoulder_z - waist_z}) * rotation_y(-s.forward[side]) * rotation_x(sign * s.out[side]) *
                               rotation_z(sign * s.twist_arm[side]);
        parts.push_back(Part{&model.upper_arm, &model.materials, shoulder, false, &coarse.upper_arm});
        const Xform elbow = shoulder * translation(V3{0, 0, -upper_arm_length}) * rotation_y(-s.elbow[side]);
        parts.push_back(Part{&model.forearm[side], &model.materials, elbow, false, &coarse.forearm[side]});
        if (side == 1) {
            const Xform fist = elbow * translation(V3{0.005, 0, -forearm_length - 0.045}) * rotation_y(s.pin_roll);
            parts.push_back(Part{&model.pin, &model.materials, fist, false, &coarse.pin});
        }
    }
    thread_local Shot shot{};
    ShotOptions options{};
    options.shadow_strength = 0.5F;
    shoot(frame, parts, options, shot);
    paint_shadow(canvas, shot);
    paint(canvas, shot);
    if (granny.state == GrannyState::startled) {
        // the jolt: little strokes about her head
        for (int k = 0; k < 5; ++k) {
            const double a = -0.9 + k * 0.45 + granny.facing;
            const double x = frame.px(granny.x) + std::cos(a) * frame.ppm * 0.35 * size;
            const double y = frame.py(granny.y) - frame.up(1.75 * size) + std::sin(a) * frame.ppm * 0.2 * size;
            canvas.stroke_line(x, y, x + std::cos(a) * frame.ppm * 0.1, y + std::sin(a) * frame.ppm * 0.08, hex(0xFFF2A0), std::max(1.0, frame.ppm * 0.02));
        }
    }
}

} // namespace mm
