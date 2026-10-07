// The gnome: a garden gnome of the cheeky kind, a little larger than life so he reads
// from up here. Red pointed cap flopping back, a big white curly beard over a blue
// tunic, a brown belt with a brass buckle, brown breeches and curl-toed boots, rosy
// cheeks and nose, and a wide open grin under his moustache.
//
// He is jointed so he can dance: boots and legs from the hips, the body over them,
// the head on its neck, each arm at the shoulder and the elbow. The sim says which
// move he is in and how far into it; the steps are worked out here.
//
// Gnome metres: x where he faces, y to his right, z up, his feet on the origin.
#include "models.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace mm {
namespace {

constexpr double pi = 3.14159265358979323846;

enum GnomeMaterial : std::uint16_t {
    cap_red,
    tunic,
    breeches,
    boot,
    sole,
    belt,
    buckle,
    skin,
    rosy,
    beard,
    eye_white,
    iris,
    pupil,
    mouth,
    teeth,
    tongue,
    gnome_material_count
};

struct GnomeModel {
    Mesh boot[2]{};
    Mesh leg[2]{};
    Mesh body{};
    Mesh head{};
    Mesh upper_arm{};
    Mesh forearm[2]{};
    std::vector<Material> materials{};
    std::unique_ptr<GnomeModel> coarse{};  // the same gnome made roughly, to throw his shadows
};

// Joints, in gnome metres.
constexpr double hip_z = 0.27;
constexpr double hip_y = 0.105;
constexpr double shoulder_x = 0.02;
constexpr double shoulder_y = 0.215;
constexpr double shoulder_z = 0.655;
constexpr double neck_x = 0.03;
constexpr double neck_z = 0.73;
constexpr double upper_arm_length = 0.15;
constexpr double forearm_length = 0.12;

void build_gnome(GnomeModel& model, double ppm, bool bake) {
    // Boots, each about its ankle: the upper, the curled toe, the sole.
    for (int side = 0; side < 2; ++side) {
        Builder b(model.boot[side], ppm);
        b.material = boot;
        b.ellipsoid(V3{0.045, 0, 0.05}, V3{0.125, 0.072, 0.06});
        b.cylinder(V3{0, 0, 0.05}, V3{0, 0, 0.11}, 0.068, 0.072, true);
        b.tube({V3{0.13, 0, 0.05}, V3{0.175, 0, 0.065}, V3{0.2, 0, 0.1}, V3{0.19, 0, 0.125}}, {0.04, 0.03, 0.02, 0.012}, true);
        b.material = sole;
        b.ellipsoid(V3{0.045, 0, 0.012}, V3{0.13, 0.075, 0.014});
        b.material = breeches;
        b.torus(V3{0, 0, 0.115}, 0.07, 0.014);
        if (bake)
            bake_occlusion(model.boot[side], 24);
    }
    // Legs, hanging from the hip: breeches down to the boot tops.
    for (int side = 0; side < 2; ++side) {
        Builder b(model.leg[side], ppm);
        b.material = breeches;
        b.tube({V3{0, 0, 0.02}, V3{0.005, 0, -0.07}, V3{0, 0, -0.17}}, {0.08, 0.076, 0.07}, true);
        if (bake)
            bake_occlusion(model.leg[side], 16);
    }
    // The body, about the hips: tunic, belt and buckle, the beard and the moustache's
    // lower curls hang from the head instead.
    {
        Builder b(model.body, ppm);
        b.at = translation(V3{0.012, 0, -hip_z});
        b.material = tunic;
        b.lathe({0.0, 0.215, 0.248, 0.262, 0.274, 0.286, 0.285, 0.272, 0.25, 0.215, 0.16, 0.09, 0.0},
                {0.275, 0.275, 0.29, 0.33, 0.39, 0.45, 0.51, 0.57, 0.62, 0.665, 0.705, 0.728, 0.735});
        // a hem: the tunic's skirt edge rolled a little
        b.torus(V3{0, 0, 0.29}, 0.245, 0.016);
        b.material = belt;
        b.lathe({0.284, 0.293, 0.296, 0.294, 0.283}, {0.425, 0.43, 0.45, 0.47, 0.476});
        b.at = translation(V3{0.012, 0, -hip_z});
        b.material = buckle;
        b.at = translation(V3{0.305, 0, 0.45 - hip_z});
        b.rounded_box(V3{0, 0, 0}, V3{0.012, 0.05, 0.035}, 0.008);
        b.material = belt;
        b.rounded_box(V3{0.006, 0, 0}, V3{0.008, 0.032, 0.019}, 0.004);
        b.at = Xform{};
        // buttons down the front, above the belt
        b.material = buckle;
        for (int k = 0; k < 2; ++k)
            b.sphere(V3{0.29 - k * 0.035, 0, 0.53 + k * 0.07 - hip_z}, 0.014);
        if (bake)
            bake_occlusion(model.body, 32);
    }
    // The head about the neck: skull, ears, cheeks, nose, eyes and brows, the grin, the
    // moustache and the beard, the cap.
    {
        Builder b(model.head, ppm);
        const V3 head{0.04 - neck_x, 0, 0.88 - neck_z};
        b.material = skin;
        b.ellipsoid(head, V3{0.16, 0.162, 0.152});
        for (int s = -1; s <= 1; s += 2)
            b.ellipsoid(head + V3{-0.025, s * 0.158, 0.015}, V3{0.035, 0.024, 0.05});
        b.material = rosy;
        for (int s = -1; s <= 1; s += 2)
            b.ellipsoid(head + V3{0.105, s * 0.085, -0.035}, V3{0.062, 0.058, 0.05});
        b.ellipsoid(head + V3{0.162, 0, -0.008}, V3{0.058, 0.054, 0.054});
        // eyes, a little sly: whites, blue irises, pupils, heavy lids
        for (int s = -1; s <= 1; s += 2) {
            const V3 eye = head + V3{0.13, s * 0.06, 0.03};
            b.material = eye_white;
            b.ellipsoid(eye, V3{0.032, 0.031, 0.034});
            b.material = iris;
            b.sphere(eye + V3{0.024, -s * 0.006, 0.0}, 0.017);
            b.material = pupil;
            b.sphere(eye + V3{0.034, -s * 0.007, 0.0}, 0.0085);
            b.material = skin;
            b.at = translation(eye + V3{0.0, 0, 0.03}) * rotation_x(s * 0.22);
            b.ellipsoid(V3{0, 0, 0}, V3{0.034, 0.036, 0.011});
            b.at = Xform{};
            // brows, white and bushy, cocked up at the outside
            b.material = beard;
            b.tube({eye + V3{0.024, -s * 0.024, 0.088}, eye + V3{0.022, s * 0.01, 0.084}, eye + V3{0.01, s * 0.046, 0.062}}, {0.01, 0.014, 0.009}, true);
        }
        // the grin, wide open and laughing: a smile-shaped mouth curving up at the corners,
        // the top teeth along its upper edge, the tongue in the bottom of it
        {
            std::vector<V3> smile{};
            std::vector<V3> upper{};
            std::vector<double> wide{};
            std::vector<double> tall{};
            std::vector<double> tooth_w{};
            std::vector<double> tooth_h{};
            for (int k = 0; k <= 12; ++k) {
                const double a = -1 + 2.0 * k / 12;
                const double y = a * 0.105;
                const double dip = 1 - a * a;
                const double front = 0.152 * std::sqrt(std::max(0.0, 1 - a * a * 0.55));
                smile.push_back(head + V3{front, y, -0.082 - 0.05 * dip});
                upper.push_back(head + V3{front + 0.016, y * 0.9, -0.076 - 0.03 * dip});
                wide.push_back(0.006 + 0.026 * std::sqrt(dip));
                tall.push_back(0.012 + 0.032 * dip);
                tooth_w.push_back(0.005 + 0.014 * std::sqrt(dip));
                tooth_h.push_back(0.007 + 0.015 * dip);
            }
            b.material = mouth;
            b.sweep(smile, wide, tall, 2.6, V3{1, 0, 0});
            b.material = teeth;
            b.sweep(upper, tooth_w, tooth_h, 3.0, V3{1, 0, 0});
            b.material = tongue;
            b.ellipsoid(head + V3{0.162, 0, -0.142}, V3{0.024, 0.045, 0.016});
        }
        // lips at the corners, turned up
        b.material = rosy;
        for (int s = -1; s <= 1; s += 2)
            b.sphere(head + V3{0.122, s * 0.106, -0.078}, 0.014);
        // the moustache, two curled lobes swept up at the ends like a smile
        b.material = beard;
        for (int s = -1; s <= 1; s += 2) {
            b.at = translation(head + V3{0.18, s * 0.05, -0.036}) * rotation_x(-s * 0.3);
            b.ellipsoid(V3{0, 0, 0}, V3{0.03, 0.056, 0.018});
            b.at = Xform{};
            b.tube({head + V3{0.17, s * 0.094, -0.035}, head + V3{0.16, s * 0.12, -0.014}, head + V3{0.15, s * 0.116, 0.006}}, {0.015, 0.011, 0.007}, true);
        }
        // the beard: sideburns from the ears, the bush under the grin, and curls down the belly
        for (int s = -1; s <= 1; s += 2)
            b.ellipsoid(head + V3{0.03, s * 0.138, -0.09}, V3{0.085, 0.065, 0.11});
        b.ellipsoid(head + V3{0.075, 0, -0.255}, V3{0.1, 0.16, 0.07});
        // white hair round the back of his head, under the cap
        b.ellipsoid(head + V3{-0.07, 0, -0.01}, V3{0.12, 0.168, 0.12});
        b.ellipsoid(head + V3{0.165, 0, -0.28}, V3{0.11, 0.18, 0.17});
        std::uint64_t curls = 91;
        for (int k = 0; k < 15; ++k) {
            const double a = -1.25 + 2.5 * k / 14;
            const double drop = 0.13 * (1 - std::cos(a * 0.9));
            const V3 at = head + V3{0.25 - 0.06 * std::abs(std::sin(a)), std::sin(a) * 0.17, -0.43 + drop};
            const double r = 0.04 + 0.012 * hash01(curls);
            b.ellipsoid(at, V3{r * 0.9, r, r * 1.1});
        }
        for (int k = 0; k < 9; ++k) {
            const double a = -1.0 + 2.0 * k / 8;
            b.ellipsoid(head + V3{0.235 - 0.05 * std::abs(a), a * 0.13, -0.3 + 0.05 * std::abs(a)}, V3{0.045, 0.05, 0.055});
        }
        // the cap: a cone that rises, leans back and flops over at the tip, on a rolled brim
        b.material = cap_red;
        b.tube({head + V3{-0.035, 0, 0.085}, head + V3{-0.06, 0, 0.19}, head + V3{-0.095, 0, 0.295}, head + V3{-0.145, 0, 0.385}, head + V3{-0.21, 0, 0.435},
                head + V3{-0.285, 0, 0.435}, head + V3{-0.34, 0, 0.395}, head + V3{-0.36, 0, 0.35}},
               {0.178, 0.148, 0.112, 0.078, 0.05, 0.032, 0.02, 0.009}, false);
        b.at = translation(head + V3{-0.032, 0, 0.08}) * rotation_y(-0.3);
        b.torus(V3{0, 0, 0}, 0.165, 0.03);
        b.cylinder(V3{0, 0, -0.005}, V3{0, 0, 0.02}, 0.168, 0.165, true);
        b.at = Xform{};
        if (bake)
            bake_occlusion(model.head, 40);
    }
    // An upper arm hanging from the shoulder: a puffed sleeve.
    {
        Builder b(model.upper_arm, ppm);
        b.material = tunic;
        b.tube({V3{0, 0, 0.02}, V3{0, 0, -0.05}, V3{0, 0, -upper_arm_length}}, {0.066, 0.064, 0.056}, true);
        if (bake)
            bake_occlusion(model.upper_arm, 16);
    }
    // A forearm from the elbow: the sleeve, then a mitten of a hand with its thumb
    // (the thumb towards the body when he hangs his arms).
    for (int side = 0; side < 2; ++side) {
        Builder b(model.forearm[side], ppm);
        const double s = side == 0 ? -1.0 : 1.0;
        b.material = tunic;
        b.tube({V3{0, 0, 0.01}, V3{0, 0, -forearm_length}}, {0.055, 0.05}, true);
        b.torus(V3{0, 0, -forearm_length}, 0.048, 0.012);
        b.material = skin;
        b.ellipsoid(V3{0.005, 0, -forearm_length - 0.055}, V3{0.042, 0.036, 0.055});
        b.ellipsoid(V3{0.03, -s * 0.03, -forearm_length - 0.03}, V3{0.016, 0.014, 0.03});
        if (bake)
            bake_occlusion(model.forearm[side], 16);
    }

    std::vector<Material>& mat = model.materials;
    mat.assign(gnome_material_count, Material{});
    mat[cap_red] = paint_material(0xD62828, 0.62F, 0.05F);
    mat[cap_red].pattern = Pattern::cap_print;
    mat[cap_red].scale = 1;
    mat[tunic] = paint_material(0x2F6CC4, 0.5F, 0.045F);
    mat[tunic].pattern = Pattern::cap_print;
    mat[breeches] = paint_material(0x6A4426, 0.4F, 0.04F);
    mat[breeches].pattern = Pattern::cap_print;
    mat[boot] = paint_material(0x3A2618, 0.68F, 0.05F);
    mat[sole] = paint_material(0x1E140D, 0.35F, 0.04F);
    mat[belt] = paint_material(0x4A2C17, 0.55F, 0.045F);
    mat[buckle] = paint_material(0xD9AE3A, 0.82F, 0.7F);
    mat[buckle].metal = 1;
    mat[skin] = paint_material(0xF4C4A2, 0.5F, 0.04F);
    mat[skin].pattern = Pattern::skin;
    mat[skin].wrap = 0.35F;
    mat[rosy] = paint_material(0xEE9A86, 0.6F, 0.045F);
    mat[rosy].wrap = 0.35F;
    mat[beard] = paint_material(0xF6F3EC, 0.4F, 0.04F);
    mat[beard].pattern = Pattern::hair;
    mat[beard].scale = 1.4F;
    mat[beard].wrap = 0.3F;
    mat[beard].sheen = 0.35F;
    mat[eye_white] = paint_material(0xFAFAF4, 0.85F, 0.05F);
    mat[iris] = paint_material(0x3A86D8, 0.9F, 0.05F);
    mat[pupil] = paint_material(0x0C0C0E, 0.96F, 0.06F);
    mat[mouth] = paint_material(0x4A1214, 0.4F, 0.04F);
    mat[teeth] = paint_material(0xFFFDF4, 0.75F, 0.06F);
    mat[tongue] = paint_material(0xD9505A, 0.7F, 0.05F);
}

struct GnomeKit {
    std::mutex guard{};
    std::map<long long, std::unique_ptr<GnomeModel>> models{};
};

GnomeKit& gnome_kit() {
    static GnomeKit kit{};
    return kit;
}

const GnomeModel& gnome_model(double ppm) {
    GnomeKit& kit = gnome_kit();
    const std::lock_guard<std::mutex> lock(kit.guard);
    // He is small (a quarter the size he is modelled at) and drawn every frame he is up.
    const double level = detail_for(ppm * 0.35);
    std::unique_ptr<GnomeModel>& slot = kit.models[std::llround(level)];
    if (!slot) {
        slot = std::make_unique<GnomeModel>();
        build_gnome(*slot, level, true);
        (*slot).coarse = std::make_unique<GnomeModel>();
        build_gnome(*(*slot).coarse, level / 3, false);
    }
    return *slot;
}

// How he stands at a moment of a move.
struct Stance {
    double hop{};       // metres off the ground
    double spin{};      // extra turn of the whole gnome, radians
    double roll{};      // the body rocking to his right, about the hips
    double pitch{};     // the body leaning forward
    double head_tilt{};
    double head_nod{};
    double forward[2]{};  // each arm swung forward from hanging (left, right)
    double out[2]{};      // and raised out to the side
    double twist[2]{};    // the upper arm turned about itself
    double elbow[2]{};    // bend at the elbow
    double kick[2]{};     // each leg swung forward from the hip
    double squat{};       // knees in: the whole body dropped
};

double ease(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

// The moves. `beat` is seconds into the move; a beat of his little dance is 0.42 s.
Stance stance_for(const Gnome& gnome) {
    Stance s{};
    const double beat = 0.42;
    const double t = gnome.beat;
    const double phase = t / beat * 2 * pi;
    const double bounce = std::abs(std::sin(phase * 0.5));
    // At rest the arms hang a little out from his round belly.
    s.out[0] = s.out[1] = 0.35;
    s.forward[0] = s.forward[1] = 0.15;
    s.elbow[0] = s.elbow[1] = 0.35;
    switch (gnome.state == GnomeState::looking || gnome.state == GnomeState::frozen ? gnome.dance : -1) {
    case 0:
        // Bouncing on the spot, both arms up and waving.
        s.hop = 0.09 * bounce;
        s.squat = 0.03 * (1 - bounce);
        for (int k = 0; k < 2; ++k) {
            s.out[k] = 2.55 + 0.25 * std::sin(phase + k * pi);
            s.elbow[k] = 0.5 + 0.35 * std::sin(phase * 2 + k);
            s.forward[k] = 0.2;
        }
        s.head_tilt = 0.18 * std::sin(phase);
        break;
    case 1:
        // A jig: kicking out one boot then the other, rocking, arms swinging.
        s.hop = 0.035 * bounce;
        s.roll = 0.14 * std::sin(phase * 0.5);
        s.kick[0] = 0.75 * std::max(0.0, std::sin(phase * 0.5));
        s.kick[1] = 0.75 * std::max(0.0, -std::sin(phase * 0.5));
        s.forward[0] = 0.8 * std::sin(phase * 0.5);
        s.forward[1] = -0.8 * std::sin(phase * 0.5);
        s.elbow[0] = s.elbow[1] = 1.1;
        s.out[0] = s.out[1] = 0.45;
        s.head_tilt = -0.2 * std::sin(phase * 0.5);
        break;
    case 2: {
        // A pirouette on one toe, arms flung out, and back round to face you.
        const double turn = ease(t / 1.1);
        s.spin = 2 * pi * turn;
        s.hop = 0.1 * std::sin(std::min(1.0, t / 1.1) * pi);
        s.out[0] = s.out[1] = 1.55;
        s.elbow[0] = s.elbow[1] = 0.15;
        s.kick[1] = 0.5 * std::sin(std::min(1.0, t / 1.1) * pi);
        s.head_nod = -0.15;
        break;
    }
    case 3: {
        // Nyah: thumb on his nose, fingers waggling, leaning in at you.
        const double up = ease(t / 0.3);
        s.pitch = 0.16 * up;
        s.hop = 0.02 * bounce;
        s.forward[1] = 1.95 * up;
        s.out[1] = 0.2;
        s.elbow[1] = 2.3 * up + 0.35 * (1 - up);
        s.twist[1] = 0.2 * std::sin(phase * 3) * up;
        s.forward[0] = 1.6 * up;
        s.out[0] = 0.15;
        s.elbow[0] = 1.2 * up + 0.35 * (1 - up);
        s.twist[0] = 0.35 * std::sin(phase * 3 + 1) * up;
        s.head_tilt = 0.22 * std::sin(phase * 0.5);
        s.head_nod = -0.1 * up;
        break;
    }
    case 4: {
        // The cheek of it: he turns his back, bends over and wiggles.
        const double away = ease(t / 0.35) * (1 - ease((t - 1.75) / 0.35));
        s.spin = pi * away;
        s.pitch = 0.42 * away;
        s.roll = 0.22 * std::sin(phase * 2) * away;
        s.squat = 0.03 * away;
        s.forward[0] = s.forward[1] = -0.5 * away + 0.15;
        s.out[0] = s.out[1] = 0.55;
        s.elbow[0] = s.elbow[1] = 1.6 * away + 0.35;
        // looking back over his shoulder at you
        s.head_nod = -0.25 * away;
        break;
    }
    case 5:
        // The chicken: hands in his armpits, elbows flapping, knees bobbing.
        s.squat = 0.04 * bounce;
        s.hop = 0.03 * std::max(0.0, std::sin(phase));
        for (int k = 0; k < 2; ++k) {
            s.out[k] = 0.7 + 0.55 * std::max(0.0, std::sin(phase * 2));
            s.forward[k] = 0.45;
            s.elbow[k] = 2.5;
            s.twist[k] = 0.5;
        }
        s.head_nod = 0.15 * std::sin(phase * 2);
        s.pitch = 0.08;
        break;
    default:
        break;
    }
    return s;
}

} // namespace

void draw_gnome(Canvas& canvas, const Frame& frame, const Gnome& gnome, double time) {
    if (gnome.state == GnomeState::hidden || gnome.state == GnomeState::shattered || gnome.height <= 0.02)
        return;
    const GnomeModel& model = gnome_model(frame.ppm);
    const Stance s = stance_for(gnome);
    // He is garden-gnome size, about 37 cm to the tip of his cap; coming out of the
    // grass he rises through it, and the jump up overshoots into the air.
    const double size = 0.28;
    const double lift = (gnome.height - 1) * 1.55 * size + s.hop * size;
    const double facing = gnome.facing + s.spin;
    const Xform root = translation(V3{gnome.x, gnome.y, std::max(lift, -1.6 * size)}) * rotation_z(facing) * scaling(size, size, size);
    std::vector<Part> parts{};
    // Legs and boots, from the hips.
    for (int side = 0; side < 2; ++side) {
        const double y = side == 0 ? -hip_y : hip_y;
        const double sign = side == 0 ? -1.0 : 1.0;
        const Xform hip = root * translation(V3{0, y, hip_z - s.squat}) * rotation_x(sign * 0.04) * rotation_y(-s.kick[side]);
        parts.push_back(Part{&model.leg[side], &model.materials, hip, false, &(*model.coarse).leg[side]});
        const Xform ankle = hip * translation(V3{0, 0, -0.165}) * rotation_y(s.kick[side] * 0.6);
        parts.push_back(Part{&model.boot[side], &model.materials, ankle * translation(V3{0, 0, -0.105}), false, &(*model.coarse).boot[side]});
    }
    // The body, rocking on the hips.
    const Xform body = root * translation(V3{0, 0, hip_z - s.squat}) * rotation_x(s.roll) * rotation_y(s.pitch);
    parts.push_back(Part{&model.body, &model.materials, body, false, &(*model.coarse).body});
    // The head, turning to look and tilting with the music.
    // He holds his chin up at you, so his grin shows from up here.
    const Xform head = body * translation(V3{neck_x, 0, neck_z - hip_z}) * rotation_z(gnome.look) * rotation_x(s.head_tilt) * rotation_y(s.head_nod - 0.3) *
                       scaling(1.1, 1.1, 1.1);
    parts.push_back(Part{&model.head, &model.materials, head, false, &(*model.coarse).head});
    // The arms.
    for (int side = 0; side < 2; ++side) {
        const double sign = side == 0 ? -1.0 : 1.0;
        const Xform shoulder = body * translation(V3{shoulder_x, sign * shoulder_y, shoulder_z - hip_z}) * rotation_y(-s.forward[side]) *
                               rotation_x(sign * s.out[side]) * rotation_z(sign * s.twist[side]);
        parts.push_back(Part{&model.upper_arm, &model.materials, shoulder, false, &(*model.coarse).upper_arm});
        const Xform elbow = shoulder * translation(V3{0, 0, -upper_arm_length}) * rotation_y(-s.elbow[side]);
        parts.push_back(Part{&model.forearm[side], &model.materials, elbow, false, &(*model.coarse).forearm[side]});
    }
    thread_local Shot shot{};
    ShotOptions options{};
    options.time = time;
    options.clip_ground = true;
    options.shadow_strength = 0.5F;
    shoot(frame, parts, options, shot);
    paint_shadow(canvas, shot);
    paint(canvas, shot);

    if (gnome.state == GnomeState::frozen) {
        const double pulse = 0.5 + 0.5 * std::sin(time * 6);
        canvas.begin();
        canvas.ellipse(frame.px(gnome.x), frame.py(gnome.y) - frame.up(0.7 * size), frame.ppm * 0.36 * size, frame.up(0.85 * size));
        canvas.stroke(rgb(200, 235, 255, static_cast<float>(0.35 + 0.35 * pulse)), std::max(1.0, frame.ppm * 0.012));
    }
    // While he is low, the tall grass still closes over him.
    if (gnome.height < 0.6) {
        std::uint64_t random = static_cast<std::uint64_t>(gnome.x * 1000 + gnome.y * 7);
        for (int k = 0; k < 18; ++k) {
            const double a = random_range(random, 0, 2 * pi);
            const double r = random_range(random, 0.0, 0.2);
            const double x = frame.px(gnome.x + std::cos(a) * r);
            const double y = frame.py(gnome.y + std::sin(a) * r);
            const double lean_a = random_range(random, 0, 2 * pi);
            canvas.stroke_line(x, y, x + std::cos(lean_a) * frame.ppm * 0.1, y - std::abs(std::sin(lean_a)) * frame.up(0.18),
                               rgb(70, 110, 40, static_cast<float>(0.9 * (1 - gnome.height / 0.6))), std::max(1.0, frame.ppm * 0.01));
        }
    }
}

} // namespace mm
