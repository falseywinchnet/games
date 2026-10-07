// The fountain and the trees' trunks.
//
// The fountain is cast stone in two tiers: a round basin with a moulded coping on its
// plinth, a baluster pedestal, a broad lower bowl with a lobed rim, a slender stem, a
// small upper bowl, and a pineapple finial. The stone is wet and mossed at the water
// line. The stone is drawn once and kept; the water is drawn every frame: the pools'
// rippled surfaces, the curtains falling from each lip, and the jet that bells over
// from the finial, hidden wherever the stone stands in front of it.
//
// A tree's trunk flares into its roots, its bark is furrowed and lichened, and it forks
// into limbs that go up into the canopy. Its shade on the grass is dappled.
#include "models.hpp"

#include <algorithm>
#include <cmath>

namespace mm {
namespace {

constexpr double pi = 3.14159265358979323846;
const V3 sun_ground{0.22, 0.30, 0};  // where a metre of height throws its shadow, as in art.cpp

enum GardenMaterial : std::uint16_t { stone, wet_stone, finial, pool_water, bowl_water, curtain, jet, bark, garden_material_count };

std::vector<Material> garden_materials() {
    std::vector<Material> mat(garden_material_count);
    mat[stone] = paint_material(0xB3AB99, 0.3F, 0.035F);
    mat[stone].pattern = Pattern::stone;
    mat[stone].scale = 1.0F;
    mat[wet_stone] = mat[stone];
    mat[wet_stone].pattern = Pattern::wet_stone;
    mat[finial] = mat[stone];
    mat[finial].pattern = Pattern::scales;
    mat[finial].scale = 5;
    mat[pool_water] = paint_material(0x1E4A52, 0.95F, 0.02F);
    mat[pool_water].pattern = Pattern::water;
    mat[pool_water].opacity = 0.93F;
    mat[bowl_water] = mat[pool_water];
    mat[curtain] = paint_material(0xD8ECF2, 0.9F, 0.03F);
    mat[curtain].pattern = Pattern::curtain;
    mat[curtain].opacity = 0.62F;
    mat[curtain].two_sided = true;
    mat[curtain].scale = 1.0F;
    mat[jet] = mat[curtain];
    mat[jet].opacity = 0.75F;
    mat[bark] = paint_material(0x5E4632, 0.25F, 0.03F);
    mat[bark].pattern = Pattern::bark;
    mat[bark].scale = 1.0F;
    return mat;
}

const std::vector<Material>& garden_palette() {
    static const std::vector<Material> palette = garden_materials();
    return palette;
}

// Heights of the fountain's parts for a basin of radius 0.62; others are scaled.
constexpr double basin_water = 0.36;
constexpr double lower_lip_r = 0.36;
constexpr double lower_lip_z = 0.97;
constexpr double lower_water = 0.955;
constexpr double upper_lip_r = 0.19;
constexpr double upper_lip_z = 1.3;
constexpr double upper_water = 1.288;
constexpr double finial_top = 1.44;

void build_fountain_stone(Mesh& mesh, double ppm, double k) {
    Builder b(mesh, ppm);
    b.at = scaling(k, k, k);
    // The basin: plinth, wall, a bullnosed coping, the inner wall and floor.
    b.material = wet_stone;
    b.lathe({0.0, 0.64, 0.645, 0.6, 0.6, 0.6, 0.625, 0.645, 0.648, 0.638, 0.6, 0.535, 0.52, 0.52, 0.5, 0.0},
            {0.0, 0.0, 0.05, 0.065, 0.2, 0.33, 0.345, 0.365, 0.39, 0.415, 0.428, 0.428, 0.41, 0.12, 0.1, 0.1});
    // a band of moulding round the wall
    b.torus(V3{0, 0, 0.2}, 0.605, 0.014);
    // The pedestal: square-ish block, a ring, the swelling vase, its collar.
    b.lathe({0.0, 0.15, 0.15, 0.13, 0.115, 0.1, 0.122, 0.13, 0.115, 0.075, 0.062, 0.085, 0.09, 0.07},
            {0.1, 0.1, 0.17, 0.19, 0.2, 0.24, 0.32, 0.42, 0.52, 0.62, 0.68, 0.72, 0.76, 0.79});
    b.material = stone;
    // The lower bowl: its underside sweeping out to a rolled lip, the lobes under it, the inside.
    b.lathe({0.06, 0.12, 0.2, 0.28, 0.33, 0.355, 0.366, 0.365, 0.352, 0.33, 0.24, 0.12, 0.0},
            {0.78, 0.8, 0.84, 0.885, 0.92, 0.945, 0.96, 0.975, 0.982, 0.97, 0.935, 0.91, 0.905});
    const int lobes = 16;
    for (int j = 0; j < lobes; ++j) {
        const double a = 2 * pi * j / lobes;
        b.ellipsoid(V3{std::cos(a) * 0.31, std::sin(a) * 0.31, 0.9}, V3{0.05, 0.05, 0.03});
    }
    // The stem between the bowls.
    b.lathe({0.06, 0.045, 0.035, 0.05, 0.04, 0.032, 0.045, 0.05}, {0.9, 0.95, 1.0, 1.06, 1.12, 1.16, 1.19, 1.21});
    // The upper bowl.
    b.lathe({0.04, 0.09, 0.14, 0.175, 0.192, 0.195, 0.186, 0.17, 0.09, 0.0}, {1.2, 1.215, 1.245, 1.275, 1.292, 1.305, 1.31, 1.302, 1.272, 1.268});
    // The pineapple finial with its leaves.
    b.material = finial;
    b.lathe({0.0, 0.03, 0.048, 0.052, 0.046, 0.03, 0.012, 0.0}, {1.27, 1.27, 1.3, 1.34, 1.38, 1.41, 1.43, 1.44});
    b.material = stone;
    for (int j = 0; j < 6; ++j) {
        const double a = 2 * pi * j / 6 + 0.3;
        b.tube({V3{std::cos(a) * 0.012, std::sin(a) * 0.012, 1.42}, V3{std::cos(a) * 0.03, std::sin(a) * 0.03, 1.45}, V3{std::cos(a) * 0.045, std::sin(a) * 0.045, 1.44}},
               {0.008, 0.007, 0.003}, true, 6);
    }
    bake_occlusion(mesh, 40);
}

// Water falling from a lip at (r0, z0) to a surface at z1, thrown out a little as it goes.
void curtain_lathe(Builder& b, double r0, double z0, double z1, double throw_out) {
    std::vector<double> radius{};
    std::vector<double> height{};
    for (int j = 0; j <= 9; ++j) {
        const double t = j / 9.0;
        radius.push_back(r0 + throw_out * t * (1.6 - 0.6 * t));
        height.push_back(z0 - (z0 - z1) * t * t);
    }
    // the lathe's rows run bottom to top; the pattern streams down v
    std::reverse(radius.begin(), radius.end());
    std::reverse(height.begin(), height.end());
    b.lathe(radius, height);
}

void build_fountain_water(Mesh& mesh, double ppm, double k) {
    Builder b(mesh, ppm);
    b.at = scaling(k, k, k);
    b.material = pool_water;
    b.lathe({0.0, 0.522}, {basin_water, basin_water});
    b.material = bowl_water;
    b.lathe({0.0, 0.34}, {lower_water, lower_water});
    b.lathe({0.0, 0.178}, {upper_water, upper_water});
    b.material = curtain;
    curtain_lathe(b, lower_lip_r + 0.008, lower_lip_z, basin_water, 0.05);
    curtain_lathe(b, upper_lip_r + 0.006, upper_lip_z, lower_water, 0.04);
    // The jet: a column from the finial, belling over into the top bowl.
    b.material = jet;
    b.lathe({0.006, 0.01, 0.014, 0.022, 0.045, 0.08, 0.11, 0.135, 0.15}, {finial_top, 1.52, 1.58, 1.615, 1.625, 1.6, 1.53, 1.42, 1.32});
}

// The birdbath, for a dish of radius 0.38: a low pebble-edged catch pool, a turned
// pedestal, a wide shallow dish with a bubbler at its heart, and the dish brimming over
// all round its lip in a thin falling veil into the pool below.
constexpr double bath_pool = 0.09;
constexpr double bath_lip_r = 0.27;
constexpr double bath_lip_z = 0.8;
constexpr double bath_water = 0.79;

void build_birdbath_stone(Mesh& mesh, double ppm, double k) {
    Builder b(mesh, ppm);
    b.at = scaling(k, k, k);
    b.material = wet_stone;
    // the catch pool's kerb and floor
    b.lathe({0.0, 0.4, 0.405, 0.39, 0.37, 0.355, 0.35, 0.0}, {0.0, 0.0, 0.06, 0.11, 0.12, 0.11, 0.04, 0.04});
    b.torus(V3{0, 0, 0.115}, 0.375, 0.012);
    // the pedestal: foot, waist, collar
    b.lathe({0.0, 0.13, 0.13, 0.1, 0.07, 0.052, 0.048, 0.056, 0.07, 0.085, 0.06},
            {0.04, 0.04, 0.12, 0.15, 0.22, 0.32, 0.46, 0.58, 0.63, 0.67, 0.7});
    b.material = stone;
    // the dish: underside sweeping out to a rolled lip, then the shallow inside
    b.lathe({0.05, 0.12, 0.2, 0.25, 0.272, 0.28, 0.276, 0.266, 0.2, 0.1, 0.0},
            {0.68, 0.7, 0.735, 0.77, 0.79, 0.8, 0.806, 0.8, 0.78, 0.768, 0.765});
    b.torus(V3{0, 0, 0.74}, 0.215, 0.01);
    // the bubbler's stone boss in the middle of the dish
    b.material = wet_stone;
    b.lathe({0.0, 0.03, 0.028, 0.016, 0.0}, {0.77, 0.77, 0.8, 0.812, 0.815});
    bake_occlusion(mesh, 40);
}

void build_birdbath_water(Mesh& mesh, double ppm, double k) {
    Builder b(mesh, ppm);
    b.at = scaling(k, k, k);
    b.material = pool_water;
    b.lathe({0.0, 0.352}, {bath_pool, bath_pool});
    b.material = bowl_water;
    b.lathe({0.0, 0.262}, {bath_water, bath_water});
    // It spills from three worn notches in the lip, each a ribbon of water arcing out and
    // down into the pool.
    b.material = jet;
    for (int j = 0; j < 3; ++j) {
        const double a = 2 * pi * j / 3 + 0.5;
        std::vector<V3> path{};
        std::vector<double> width{};
        for (int i = 0; i <= 8; ++i) {
            const double t = i / 8.0;
            const double r = bath_lip_r + 0.006 + 0.07 * t * (1.4 - 0.4 * t);
            const double z = bath_lip_z + 0.002 - (bath_lip_z - bath_pool) * t * t;
            path.push_back(V3{std::cos(a) * r, std::sin(a) * r, z});
            width.push_back(0.024 - 0.011 * t);
        }
        b.tube(path, width, false, 8);
    }
    // the bubbler: a low dome of water welling up
    b.material = jet;
    b.lathe({0.004, 0.012, 0.02, 0.03, 0.04}, {0.815, 0.85, 0.858, 0.845, 0.8});
}

struct FountainKit {
    std::mutex guard{};
    long long level{};
    long long scale{};
    Mesh water{};
};

FountainKit& fountain_kit() {
    static FountainKit kit{};
    return kit;
}

} // namespace

void draw_fountain_model(Canvas& canvas, const Frame& frame, const Prop& prop) {
    const StillKey key = still_key(40, prop.seed * 13 + static_cast<std::uint64_t>(std::lround(prop.rx * 1000)), frame);
    const Shot* kept = find_still(key);
    if (kept == nullptr) {
        Mesh mesh{};
        build_fountain_stone(mesh, detail_for(frame.ppm), prop.rx / 0.62);
        std::vector<Part> parts{};
        parts.push_back(Part{&mesh, &garden_palette(), translation(V3{prop.x, prop.y, 0}), false, nullptr});
        Shot shot{};
        ShotOptions options{};
        options.shadow_strength = 0.5F;
        shoot(frame, parts, options, shot);
        kept = &keep_still(key, std::move(shot));
    }
    paint_shadow(canvas, *kept);
    paint(canvas, *kept);
}

void draw_fountain_water(Canvas& canvas, const Frame& frame, const Prop& prop, double time) {
    const StillKey key = still_key(40, prop.seed * 13 + static_cast<std::uint64_t>(std::lround(prop.rx * 1000)), frame);
    const Shot* stone_shot = find_still(key);
    if (stone_shot == nullptr)
        return;
    FountainKit& kit = fountain_kit();
    const std::lock_guard<std::mutex> lock(kit.guard);
    const long long level = std::llround(detail_for(frame.ppm));
    const long long scale = std::llround(prop.rx * 1000);
    if (kit.level != level || kit.scale != scale || kit.water.vertices.empty()) {
        kit.water = Mesh{};
        build_fountain_water(kit.water, static_cast<double>(level), prop.rx / 0.62);
        kit.level = level;
        kit.scale = scale;
    }
    std::vector<Material> palette = garden_palette();
    // where each fall lands, for the rings it makes
    palette[pool_water].level = static_cast<float>((lower_lip_r + 0.05) * prop.rx / 0.62);
    palette[bowl_water].level = static_cast<float>(0.15 * prop.rx / 0.62);
    std::vector<Part> parts{};
    parts.push_back(Part{&kit.water, &palette, translation(V3{prop.x, prop.y, 0}), false, nullptr});
    thread_local Shot water{};
    ShotOptions options{};
    options.time = time;
    options.ground_shadow = false;
    options.occluder = stone_shot;
    shoot(frame, parts, options, water);
    paint(canvas, water);
    // Spray: bright drops thrown off the top of the jet, and sparkle where the falls land.
    const double k = prop.rx / 0.62;
    for (int j = 0; j < 14; ++j) {
        const double life = std::fmod(time * 1.4 + j * 0.173, 1.0);
        const double a = j * 2.3999 + time * 0.3;
        const double reach = (0.04 + 0.16 * life) * k;
        const double z = (1.62 + 0.05 * std::sin(life * pi) - 0.3 * life * life) * k;
        canvas.fill_circle(frame.px(prop.x + std::cos(a) * reach), frame.py(prop.y + std::sin(a) * reach) - frame.up(z), std::max(0.7, frame.ppm * 0.008),
                           rgb(255, 255, 255, static_cast<float>(0.75 * (1 - life))));
    }
    for (int j = 0; j < 18; ++j) {
        const double a = j * 2 * pi / 18 + std::sin(time * 3 + j) * 0.1;
        const double flick = 0.5 + 0.5 * std::sin(time * 13 + j * 1.7);
        const double r = (lower_lip_r + 0.055) * k;
        canvas.fill_circle(frame.px(prop.x + std::cos(a) * r), frame.py(prop.y + std::sin(a) * r) - frame.up(basin_water * k + 0.015), std::max(0.7, frame.ppm * 0.007),
                           rgb(255, 255, 255, static_cast<float>(0.5 * flick)));
    }
}

void draw_birdbath_model(Canvas& canvas, const Frame& frame, const Prop& prop) {
    const StillKey key = still_key(41, prop.seed * 13 + static_cast<std::uint64_t>(std::lround(prop.rx * 1000)), frame);
    const Shot* kept = find_still(key);
    if (kept == nullptr) {
        Mesh mesh{};
        build_birdbath_stone(mesh, detail_for(frame.ppm), prop.rx / 0.38);
        std::vector<Part> parts{};
        parts.push_back(Part{&mesh, &garden_palette(), translation(V3{prop.x, prop.y, 0}), false, nullptr});
        Shot shot{};
        ShotOptions options{};
        options.shadow_strength = 0.5F;
        shoot(frame, parts, options, shot);
        kept = &keep_still(key, std::move(shot));
    }
    paint_shadow(canvas, *kept);
    paint(canvas, *kept);
}

void draw_birdbath_water(Canvas& canvas, const Frame& frame, const Prop& prop, double time) {
    const StillKey key = still_key(41, prop.seed * 13 + static_cast<std::uint64_t>(std::lround(prop.rx * 1000)), frame);
    const Shot* stone_shot = find_still(key);
    if (stone_shot == nullptr)
        return;
    static std::mutex guard{};
    static Mesh water{};
    static long long kept_level = 0;
    static long long kept_scale = 0;
    const std::lock_guard<std::mutex> lock(guard);
    const long long level = std::llround(detail_for(frame.ppm));
    const long long scale = std::llround(prop.rx * 1000);
    if (kept_level != level || kept_scale != scale || water.vertices.empty()) {
        water = Mesh{};
        build_birdbath_water(water, static_cast<double>(level), prop.rx / 0.38);
        kept_level = level;
        kept_scale = scale;
    }
    const double k = prop.rx / 0.38;
    std::vector<Material> palette = garden_palette();
    palette[pool_water].level = static_cast<float>((bath_lip_r + 0.04) * k);
    palette[bowl_water].level = static_cast<float>(0.02 * k);
    std::vector<Part> parts{};
    parts.push_back(Part{&water, &palette, translation(V3{prop.x, prop.y, 0}), false, nullptr});
    thread_local Shot shot{};
    ShotOptions options{};
    options.time = time;
    options.ground_shadow = false;
    options.occluder = stone_shot;
    shoot(frame, parts, options, shot);
    paint(canvas, shot);
    // Splashes where the three streams land in the pool.
    for (int j = 0; j < 15; ++j) {
        const int stream = j % 3;
        const double life = std::fmod(time * 2.2 + j * 0.271, 1.0);
        const double a = 2 * pi * stream / 3 + 0.5 + std::sin(j * 4.1) * 0.12;
        const double r = (bath_lip_r + 0.076 + 0.03 * std::sin(j * 2.7) * life) * k;
        canvas.fill_circle(frame.px(prop.x + std::cos(a) * r), frame.py(prop.y + std::sin(a) * r) - frame.up((bath_pool + 0.04 * std::sin(life * pi)) * k), std::max(0.6, frame.ppm * 0.006),
                           rgb(255, 255, 255, static_cast<float>(0.6 * (1 - life))));
    }
}

void draw_tree_foot(Canvas& canvas, const Frame& frame, const Tree& tree) {
    // The canopy's shade: dappled where the sun gets through the leaves.
    const StillKey shade_key = still_key(31, tree.seed, frame);
    const Shot* shade = find_still(shade_key);
    if (shade == nullptr) {
        Shot shot{};
        const double cx = tree.x + sun_ground.x * 1.9;
        const double cy = tree.y + sun_ground.y * 1.9;
        const double r = tree.crown * 1.05;
        shot.sx0 = static_cast<int>(std::floor(frame.px(cx - r))) - 1;
        shot.sy0 = static_cast<int>(std::floor(frame.py(cy - r))) - 1;
        shot.sw = static_cast<int>(std::ceil(r * 2 * frame.ppm)) + 3;
        shot.sh = static_cast<int>(std::ceil(r * 2 * frame.ppm * frame.tilt)) + 3;
        shot.shadow.assign(static_cast<std::size_t>(shot.sw * shot.sh), 0);
        const V3 offset{static_cast<double>(tree.seed % 97U), static_cast<double>(tree.seed % 89U), 0.5};
        for (int y = 0; y < shot.sh; ++y)
            for (int x = 0; x < shot.sw; ++x) {
                const double wx = (shot.sx0 + x + 0.5 - frame.ox) / frame.ppm;
                const double wy = (shot.sy0 + y + 0.5 - frame.oy) / (frame.ppm * frame.tilt);
                // a lumpy outline, like the canopy's
                const double a = std::atan2(wy - cy, wx - cx);
                const double edge = r * (0.9 + 0.1 * noise3(V3{std::cos(a) * 2 + offset.x, std::sin(a) * 2 + offset.y, 0.3}));
                const double d = std::hypot(wx - cx, wy - cy) / edge;
                if (d >= 1)
                    continue;
                const double body = 1 - std::pow(d, 4);
                const double fleck = fbm3(V3{wx * 5.5 + offset.x, wy * 5.5 + offset.y, 0.7}, 3);
                const double dapple = fleck > 0.62 ? 0.35 : 1.0;
                shot.shadow[static_cast<std::size_t>(y * shot.sw + x)] = static_cast<std::uint8_t>(std::lround(255 * 0.46 * body * dapple));
            }
        shade = &keep_still(shade_key, std::move(shot));
    }
    paint_shadow(canvas, *shade);
    // The trunk itself.
    const StillKey key = still_key(30, tree.seed, frame);
    const Shot* kept = find_still(key);
    if (kept == nullptr) {
        Mesh mesh{};
        Builder b(mesh, detail_for(frame.ppm));
        b.material = bark;
        std::uint64_t random = tree.seed | 1U;
        const double r = tree.trunk;
        const double lean_a = hash01(random) * 2 * pi;
        const double lean = 0.04 + 0.05 * hash01(random);
        std::vector<V3> spine{};
        std::vector<double> radii{};
        for (int j = 0; j <= 10; ++j) {
            const double t = j / 10.0;
            const double z = (trunk_height - 0.04) * t;
            const double bend = lean * (t * t) + 0.015 * std::sin(t * 7 + lean_a);
            spine.push_back(V3{std::cos(lean_a) * bend, std::sin(lean_a) * bend, z - 0.03});
            radii.push_back(r * (1.0 + 0.85 * std::exp(-t * 14) - 0.18 * t));
        }
        b.tube(spine, radii, false);
        // roots flaring into the mulch
        const int roots = 5;
        for (int j = 0; j < roots; ++j) {
            const double a = 2 * pi * j / roots + hash01(random) * 0.6;
            const double reach = r * (2.3 + 0.8 * hash01(random));
            b.sweep({V3{std::cos(a) * r * 0.55, std::sin(a) * r * 0.55, 0.2}, V3{std::cos(a) * r * 1.25, std::sin(a) * r * 1.25, 0.07},
                     V3{std::cos(a) * r * 1.9, std::sin(a) * r * 1.9, 0.02}, V3{std::cos(a) * reach, std::sin(a) * reach, -0.01}},
                    {r * 0.42, r * 0.36, r * 0.26, r * 0.1}, {r * 0.5, r * 0.3, r * 0.18, r * 0.06}, 2.4, V3{-std::sin(a), std::cos(a), 0});
        }
        // limbs forking up into the canopy
        const V3 top = spine.back();
        const int limbs = 3 + static_cast<int>(hash01(random) * 2);
        for (int j = 0; j < limbs; ++j) {
            const double a = 2 * pi * j / limbs + hash01(random) * 0.8;
            const double out = tree.crown * (0.32 + 0.18 * hash01(random));
            const V3 from = top + V3{0, 0, -0.12 - 0.15 * hash01(random)};
            const V3 mid = from + V3{std::cos(a) * out * 0.5, std::sin(a) * out * 0.5, 0.25};
            const V3 end = from + V3{std::cos(a) * out, std::sin(a) * out, 0.42 + 0.15 * hash01(random)};
            b.tube({from, mid, end}, {r * 0.62, r * 0.42, r * 0.18}, true);
            // a twig or two off each limb
            const V3 twig = mid + V3{std::cos(a + 0.9) * out * 0.35, std::sin(a + 0.9) * out * 0.35, 0.22};
            b.tube({mid, twig}, {r * 0.25, r * 0.08}, true);
        }
        bake_occlusion(mesh, 32);
        std::vector<Part> parts{};
        parts.push_back(Part{&mesh, &garden_palette(), translation(V3{tree.x, tree.y, 0}), false, nullptr});
        Shot shot{};
        ShotOptions options{};
        options.shadow_strength = 0.4F;
        shoot(frame, parts, options, shot);
        kept = &keep_still(key, std::move(shot));
    }
    paint_shadow(canvas, *kept);
    paint(canvas, *kept);
}

} // namespace mm
