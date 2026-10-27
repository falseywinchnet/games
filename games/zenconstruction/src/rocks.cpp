#include "rocks.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <mutex>

namespace zc {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kInfinity = 1e300;

// ---------------------------------------------------------------- noise

// 32-bit lattice hash; unsigned arithmetic wraps exactly as the prototype's Math.imul does.
std::uint32_t hash3(std::int32_t ix, std::int32_t iy, std::int32_t iz, std::uint32_t seed) {
    std::uint32_t h = static_cast<std::uint32_t>(ix) * 374761393u;
    h = h ^ (static_cast<std::uint32_t>(iy) * 668265263u);
    h = h ^ (static_cast<std::uint32_t>(iz) * 2147483647u);
    h = h ^ (seed * 1274126177u);
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return h;
}

// A gradient from twelve edge directions, dotted with the offset.
double lattice(std::int32_t ix, std::int32_t iy, std::int32_t iz, std::uint32_t seed, double fx, double fy, double fz) {
    const std::uint32_t h = hash3(ix, iy, iz, seed) & 15u;
    const double u = h < 8 ? fx : fy;
    double w = fz;
    if (h < 4) {
        w = fy;
    } else if (h == 12 || h == 14) {
        w = fx;
    }
    const double first = (h & 1u) == 0 ? u : -u;
    const double second = (h & 2u) == 0 ? w : -w;
    return first + second;
}

double fade(double t) {
    return t * t * t * (t * (t * 6 - 15) + 10);
}

double interpolate(double a, double b, double t) {
    return a + t * (b - a);
}

// Gradient noise in about [-1, 1], one octave, lattice period 1.
double noise3(double x, double y, double z, std::uint32_t seed) {
    const double floor_x = std::floor(x);
    const double floor_y = std::floor(y);
    const double floor_z = std::floor(z);
    const std::int32_t ix = static_cast<std::int32_t>(floor_x);
    const std::int32_t iy = static_cast<std::int32_t>(floor_y);
    const std::int32_t iz = static_cast<std::int32_t>(floor_z);
    const double fx = x - floor_x;
    const double fy = y - floor_y;
    const double fz = z - floor_z;
    const double u = fade(fx);
    const double v = fade(fy);
    const double w = fade(fz);
    const double n000 = lattice(ix, iy, iz, seed, fx, fy, fz);
    const double n100 = lattice(ix + 1, iy, iz, seed, fx - 1, fy, fz);
    const double n010 = lattice(ix, iy + 1, iz, seed, fx, fy - 1, fz);
    const double n110 = lattice(ix + 1, iy + 1, iz, seed, fx - 1, fy - 1, fz);
    const double n001 = lattice(ix, iy, iz + 1, seed, fx, fy, fz - 1);
    const double n101 = lattice(ix + 1, iy, iz + 1, seed, fx - 1, fy, fz - 1);
    const double n011 = lattice(ix, iy + 1, iz + 1, seed, fx, fy - 1, fz - 1);
    const double n111 = lattice(ix + 1, iy + 1, iz + 1, seed, fx - 1, fy - 1, fz - 1);
    const double x00 = interpolate(n000, n100, u);
    const double x10 = interpolate(n010, n110, u);
    const double x01 = interpolate(n001, n101, u);
    const double x11 = interpolate(n011, n111, u);
    const double y0 = interpolate(x00, x10, v);
    const double y1 = interpolate(x01, x11, v);
    return interpolate(y0, y1, w);
}

// mulberry32 over a caller-owned state
struct Random {
    std::uint32_t state = 0;
};

double random_unit(Random& random) {
    random.state = random.state + 0x6d2b79f5u;
    std::uint32_t t = random.state;
    t = (t ^ (t >> 15)) * (t | 1u);
    t = t ^ (t + (t ^ (t >> 7)) * (t | 61u));
    const std::uint32_t mixed = t ^ (t >> 14);
    return static_cast<double>(mixed) / 4294967296.0;
}

phys::Vec3 unit(phys::Vec3 v) {
    return phys::normalized(v);
}

// ---------------------------------------------------------------- the classes

struct ClassSpec {
    RockClass kind;
    double weight;
    double size_low, size_high;      // mean diameter, metres
    double aspect[3];
    double amplitude, hurst;
    int fractures;
    double fracture_depth;
    double bedding;                  // 0, or the share of the thin semi-axis kept between two bedding planes
    double basic_friction_angle;
    double squareness;
    bool ridged;
    int stone_count;
    Stone stones[4];                 // what this kind of rock is likely to be made of
    double vein_chance, lichen_chance;
};

// After photographs of balanced stones, beach cobble and scree: flat river
// discs and plenty of slabs to build on, blocky fractured stones, jagged
// shards to make it hard; a few rounder cobbles.
const ClassSpec kClasses[5] = {
    {RockClass::river_disc, 0.20, 0.07, 0.20, {1.0, 0.86, 0.30}, 0.04, 1.1, 0, 0.0, 0.0, 29, 2.4, false, 4,
     {Stone::grey_granite, Stone::quartzite, Stone::basalt, Stone::greywacke}, 0.30, 0.10},
    {RockClass::cobble, 0.12, 0.06, 0.16, {1.0, 0.80, 0.60}, 0.06, 1.0, 2, 0.15, 0.0, 29, 2.0, false, 4,
     {Stone::pink_granite, Stone::grey_granite, Stone::quartzite, Stone::sandstone}, 0.35, 0.20},
    {RockClass::block, 0.18, 0.07, 0.18, {1.0, 0.80, 0.62}, 0.08, 0.85, 9, 0.30, 0.0, 31, 3.4, true, 4,
     {Stone::pink_granite, Stone::grey_granite, Stone::sandstone, Stone::limestone}, 0.25, 0.35},
    {RockClass::slab, 0.28, 0.09, 0.24, {1.0, 0.75, 0.22}, 0.06, 0.9, 6, 0.30, 0.8, 31, 3.6, false, 4,
     {Stone::slate, Stone::sandstone, Stone::limestone, Stone::greywacke}, 0.15, 0.40},
    {RockClass::shard, 0.22, 0.06, 0.14, {1.0, 0.62, 0.42}, 0.12, 0.7, 11, 0.45, 0.0, 32, 2.8, true, 4,
     {Stone::basalt, Stone::slate, Stone::quartzite, Stone::greywacke}, 0.10, 0.10},
};

constexpr int kVertexBudget = 48;     // collision hull vertices
constexpr int kOctaves = 7;
constexpr double kRoughnessGain = 0.5;
constexpr double kRoughnessLimit = 12;   // degrees

// ---------------------------------------------------------------- the radius field

double octave_sum(const RockRecipe& recipe, phys::Vec3 d, int first, int last) {
    double sum = 0;
    for (int k = first; k < last; k += 1) {
        const double frequency = std::pow(2.0, k);
        const double amplitude = recipe.amplitude * std::pow(2.0, -recipe.hurst * k);
        double n = noise3(d.x * frequency + recipe.noise_offset[0], d.y * frequency + recipe.noise_offset[1],
                          d.z * frequency + recipe.noise_offset[2], recipe.seed + static_cast<std::uint32_t>(k) * 7919u);
        if (recipe.ridged && k >= 1) {
            n = 0.5 - std::abs(n) * 1.6;   // sharp crests and creases, as broken rock has
        }
        sum += amplitude * n;
    }
    return sum;
}

// Radius of the geometric (below-cutoff) surface along unit direction d.
double low_radius(const RockRecipe& recipe, phys::Vec3 d) {
    // a superquadric: an ellipsoid at squareness 2, flatter faces and firmer
    // edges above it
    const double p = recipe.squareness;
    const double qx = std::pow(std::abs(d.x / recipe.axes[0]), p);
    const double qy = std::pow(std::abs(d.y / recipe.axes[1]), p);
    const double qz = std::pow(std::abs(d.z / recipe.axes[2]), p);
    double radius = std::pow(qx + qy + qz, -1.0 / p);
    radius *= 1 + octave_sum(recipe, d, 0, recipe.cutoff_octave);
    for (size_t k = 0; k < recipe.fracture_normals.size(); k += 1) {
        const double facing = phys::dot(d, recipe.fracture_normals[k]);
        if (facing > 1.0e-6) {
            radius = std::min(radius, recipe.fracture_offsets[k] / facing);
        }
    }
    return radius;
}

// Height of the texture (above-cutoff bands) along d, metres.
double texture_height(const RockRecipe& recipe, phys::Vec3 d) {
    return recipe.radius * octave_sum(recipe, d, recipe.cutoff_octave, recipe.octaves);
}

std::vector<phys::Vec3> golden_directions(int count) {
    std::vector<phys::Vec3> directions;
    directions.reserve(static_cast<size_t>(count));
    const double golden = kPi * (3 - std::sqrt(5.0));
    for (int i = 0; i < count; i += 1) {
        const double z = 1 - (2.0 * i + 1) / count;
        const double ring = std::sqrt(std::max(0.0, 1 - z * z));
        const double angle = golden * i;
        directions.push_back(phys::Vec3{ring * std::cos(angle), ring * std::sin(angle), z});
    }
    return directions;
}

// Distance from the recipe's origin to the cooked hull along unit direction d.
// The hull's planes are in the body frame (centre of mass at the origin); the
// recipe's origin sits at -com_offset there.
double hull_radius(const phys::Shape& shape, phys::Vec3 d) {
    const phys::HullData& hull = shape.hulls[0];
    double radius = kInfinity;
    for (size_t f = 0; f < hull.face_normals.size(); f += 1) {
        const phys::Vec3 n = hull.face_normals[f];
        const double facing = phys::dot(n, d);
        if (facing > 1.0e-9) {
            const double offset_input = hull.face_offsets[f] + phys::dot(n, shape.com_offset);
            radius = std::min(radius, offset_input / facing);
        }
    }
    return radius;
}

phys::Shape cook_points(const std::vector<phys::Vec3>& points, double friction) {
    phys::ShapeDesc desc;
    phys::HullDesc hull;
    hull.points = points;
    desc.hulls.push_back(hull);
    desc.density = 2600;
    desc.friction = friction;
    desc.restitution = 0.05;
    desc.rolling_resistance = 0.002;
    return phys::cook(desc);
}

RockRecipe make_recipe(std::uint32_t seed, const ClassSpec& spec, double size, const double aspect[3], double amplitude,
                       Random& look) {
    RockRecipe recipe;
    recipe.seed = seed;
    recipe.kind = spec.kind;
    recipe.amplitude = amplitude;
    recipe.hurst = spec.hurst;
    recipe.octaves = kOctaves;
    recipe.basic_friction_angle = spec.basic_friction_angle;
    recipe.squareness = spec.squareness * (0.9 + 0.2 * random_unit(look));
    recipe.ridged = spec.ridged;
    Random state;
    state.state = seed * 2654435761u;
    const double mean = (aspect[0] + aspect[1] + aspect[2]) / 3;
    for (int k = 0; k < 3; k += 1) {
        const double jitter = 0.85 + 0.3 * random_unit(state);
        recipe.axes[k] = 0.5 * size * aspect[k] / mean * jitter;
    }
    recipe.radius = std::cbrt(recipe.axes[0] * recipe.axes[1] * recipe.axes[2]);
    for (int k = 0; k < spec.fractures; k += 1) {
        const double z = 2 * random_unit(state) - 1;
        const double angle = 2 * kPi * random_unit(state);
        const double ring = std::sqrt(std::max(0.0, 1 - z * z));
        const phys::Vec3 normal{ring * std::cos(angle), ring * std::sin(angle), z};
        const double qx = normal.x / recipe.axes[0];
        const double qy = normal.y / recipe.axes[1];
        const double qz = normal.z / recipe.axes[2];
        const double reach = 1 / std::sqrt(qx * qx + qy * qy + qz * qz);
        recipe.fracture_normals.push_back(normal);
        recipe.fracture_offsets.push_back(reach * (1 - spec.fracture_depth * random_unit(state)));
    }
    if (spec.bedding > 0) {
        // a slab splits along two nearly parallel planes across its thin axis
        const double tilt = 0.06;
        const double t1 = tilt * (2 * random_unit(state) - 1);
        const double t2 = tilt * (2 * random_unit(state) - 1);
        recipe.fracture_normals.push_back(unit(phys::Vec3{t1, t2, 1}));
        recipe.fracture_offsets.push_back(recipe.axes[2] * spec.bedding);
        const double t3 = tilt * (2 * random_unit(state) - 1);
        const double t4 = tilt * (2 * random_unit(state) - 1);
        recipe.fracture_normals.push_back(unit(phys::Vec3{t3, t4, -1}));
        recipe.fracture_offsets.push_back(recipe.axes[2] * spec.bedding);
    }
    // Octave k's surface wavelength is about 2 radius / 2^k; the hull carries
    // waves longer than twice its mean vertex spacing.
    const double spacing = std::sqrt(4 * kPi * recipe.radius * recipe.radius / kVertexBudget);
    const double cutoff_wavelength = 2 * spacing;
    recipe.cutoff_octave = kOctaves;
    for (int k = 0; k < kOctaves; k += 1) {
        if (2 * recipe.radius / std::pow(2.0, k) < cutoff_wavelength) {
            recipe.cutoff_octave = k;
            break;
        }
    }
    for (int k = 0; k < 3; k += 1) {
        recipe.noise_offset[k] = random_unit(state) * 64;
    }
    // the look: the stone, and a gentle tint over it (a little warmer or cooler)
    const int pick = static_cast<int>(random_unit(look) * spec.stone_count) % spec.stone_count;
    recipe.stone = spec.stones[pick];
    const float tone = static_cast<float>(1.0 + 0.16 * (random_unit(look) - 0.5));
    const float warmth = static_cast<float>(0.06 * (random_unit(look) - 0.5));
    recipe.colour = Col{tone * (1 + warmth), tone, tone * (1 - warmth), 1};
    recipe.vein = random_unit(look) < spec.vein_chance;
    {
        const double z = 2 * random_unit(look) - 1;
        const double angle = 2 * kPi * random_unit(look);
        const double ring = std::sqrt(std::max(0.0, 1 - z * z));
        recipe.vein_normal = phys::Vec3{ring * std::cos(angle), ring * std::sin(angle), z};
        recipe.vein_offset = (random_unit(look) - 0.5) * recipe.radius;
        recipe.vein_width = recipe.radius * (0.06 + 0.06 * random_unit(look));
    }
    if (random_unit(look) < spec.lichen_chance) {
        recipe.lichen = 0.25 + 0.5 * random_unit(look);
        if (random_unit(look) < 0.2) {
            recipe.lichen_colour = Col{.80f, .58f, .28f, 1};   // orange crust, now and then
        } else if (random_unit(look) < 0.5) {
            recipe.lichen_colour = Col{.72f, .74f, .62f, 1};   // pale grey-green
        }
    }
    return recipe;
}

// ---------------------------------------------------------------- the icosphere

struct Icosphere {
    std::vector<phys::Vec3> directions;
    std::vector<int> triangles;
};

int midpoint(Icosphere& sphere, std::map<std::uint64_t, int>& midpoints, int a, int b) {
    const int low = std::min(a, b);
    const int high = std::max(a, b);
    const std::uint64_t key = (static_cast<std::uint64_t>(low) << 32) | static_cast<std::uint64_t>(high);
    const std::map<std::uint64_t, int>::const_iterator found = midpoints.find(key);
    if (found != midpoints.end()) {
        return (*found).second;
    }
    const phys::Vec3 sum = sphere.directions[static_cast<size_t>(a)] + sphere.directions[static_cast<size_t>(b)];
    sphere.directions.push_back(unit(sum));
    const int index = static_cast<int>(sphere.directions.size()) - 1;
    midpoints[key] = index;
    return index;
}

Icosphere build_icosphere(int levels) {
    Icosphere sphere;
    const double t = (1 + std::sqrt(5.0)) / 2;
    const double raw[12][3] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                               {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (int k = 0; k < 12; k += 1) {
        sphere.directions.push_back(unit(phys::Vec3{raw[k][0], raw[k][1], raw[k][2]}));
    }
    const int base[60] = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
                          3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
    for (int k = 0; k < 60; k += 1) {
        sphere.triangles.push_back(base[k]);
    }
    for (int level = 0; level < levels; level += 1) {
        std::map<std::uint64_t, int> midpoints;
        std::vector<int> next;
        next.reserve(sphere.triangles.size() * 4);
        for (size_t k = 0; k + 2 < sphere.triangles.size(); k += 3) {
            const int a = sphere.triangles[k];
            const int b = sphere.triangles[k + 1];
            const int c = sphere.triangles[k + 2];
            const int ab = midpoint(sphere, midpoints, a, b);
            const int bc = midpoint(sphere, midpoints, b, c);
            const int ca = midpoint(sphere, midpoints, c, a);
            const int quad[12] = {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca};
            for (int q = 0; q < 12; q += 1) {
                next.push_back(quad[q]);
            }
        }
        sphere.triangles = next;
    }
    return sphere;
}

void initialize_icosphere(int levels, Icosphere* sphere) {
    *sphere = build_icosphere(levels);
}

const Icosphere& icosphere(int levels) {
    static Icosphere cache[5];
    static std::once_flag initialized[5];
    // Rock generation starts on several workers. Publish each immutable mesh
    // only after its vectors are fully built.
    std::call_once(initialized[levels], initialize_icosphere, levels, &cache[levels]);
    return cache[levels];
}

// ---------------------------------------------------------------- the look

float clampf(double v, double low, double high) {
    return static_cast<float>(std::min(high, std::max(low, v)));
}

// The stone's colour along direction d: its base colour, darker in the
// texture's hollows, flecked with grains, crossed by a quartz vein now and
// then, with lichen on some.
Col stone_colour(const RockRecipe& recipe, phys::Vec3 d, double height, double rms_height) {
    const Col mean = stone_mean(recipe.stone);
    const double hollow = height / std::max(1.0e-9, 3 * rms_height);
    const float shade = clampf(0.94 + 0.16 * hollow, 0.72, 1.1);
    Col c{recipe.colour.r * shade, recipe.colour.g * shade, recipe.colour.b * shade, 1};
    // mottling: broad patches of lighter and darker stone, weathering
    const double mottle = noise3(d.x * 5 + 7, d.y * 5 + 3, d.z * 5 + 11, recipe.seed ^ 0x5bd1e995u) +
                          0.5 * noise3(d.x * 13 + 1, d.y * 13 + 9, d.z * 13 + 4, recipe.seed ^ 0x27d4eb2du);
    const float m = clampf(1 + 0.13 * mottle, 0.82, 1.18);
    c = Col{c.r * m, c.g * m, c.b * m, 1};
    // bedding in sandstone and greywacke: faint bands across the rock
    if (recipe.stone == Stone::sandstone || recipe.stone == Stone::greywacke) {
        const double across = phys::dot(d, recipe.vein_normal) * recipe.radius;
        const double band = std::sin(across / 0.006 + 2.5 * noise3(d.x * 3, d.y * 3, d.z * 3, recipe.seed ^ 0x165667b1u));
        const float k = clampf(1 + 0.07 * band, 0.9, 1.1);
        c = Col{c.r * k, c.g * k * 0.99f, c.b * k * 0.97f, 1};
    }
    // a band of milky quartz, as a tint that turns the stone white there
    if (recipe.vein) {
        const double distance = std::abs(phys::dot(d, recipe.vein_normal) * recipe.radius - recipe.vein_offset);
        if (distance < recipe.vein_width) {
            const float k = clampf(1 - distance / recipe.vein_width, 0, 1) * 0.65f;
            const Col white{std::min(2.2f, .9f / std::max(.05f, mean.r)), std::min(2.2f, .88f / std::max(.05f, mean.g)),
                            std::min(2.2f, .84f / std::max(.05f, mean.b)), 1};
            c = mix(c, white, k);
        }
    }
    // lichen in patches, its colour whatever the stone beneath
    if (recipe.lichen > 0) {
        const double patch = noise3(d.x * 3.1 + 19, d.y * 3.1 + 5, d.z * 3.1 + 2, recipe.seed ^ 0x9e3779b9u);
        const double speck = noise3(d.x * 11, d.y * 11, d.z * 11, recipe.seed ^ 0x85ebca6bu);
        const double threshold = 0.18 - 0.35 * recipe.lichen;
        if (patch > threshold && speck > -0.15) {
            const float k = clampf((patch - threshold) * 4, 0, 0.8);
            const Col lichen{recipe.lichen_colour.r / std::max(.05f, mean.r), recipe.lichen_colour.g / std::max(.05f, mean.g),
                             recipe.lichen_colour.b / std::max(.05f, mean.b), 1};
            c = mix(c, lichen, k);
        }
    }
    return c;
}

// texture coordinates for one triangle: planar projection along its dominant
// axis, in grain repeats per metre (the grain is noise, so the changes of
// projection between triangles don't show)
void project_grain(Vtx& v, int axis, double shift) {
    const double repeats = 20;   // a 128-texel tile every 5 cm: 0.4 mm a texel
    if (axis == 0) {
        v.s = v.p.y * repeats + shift;
        v.t = v.p.z * repeats + 0.37 * shift;
    } else if (axis == 1) {
        v.s = v.p.x * repeats + 0.61 * shift;
        v.t = v.p.z * repeats + shift;
    } else {
        v.s = v.p.x * repeats + 0.29 * shift;
        v.t = v.p.y * repeats + 0.83 * shift;
    }
}

RockMesh build_mesh(const RockRecipe& recipe, const phys::Shape& shape, int level, double rms_height) {
    const Icosphere& sphere = icosphere(level);
    const size_t count = sphere.directions.size();
    std::vector<V3> positions(count);
    std::vector<Col> colours(count);
    for (size_t k = 0; k < count; k += 1) {
        const phys::Vec3 d = sphere.directions[k];
        const double on_hull = hull_radius(shape, d);
        const double low = low_radius(recipe, d);
        const double height = texture_height(recipe, d);
        // halfway between the hull's flat facets and the designed surface: the
        // outline loses its corners
        // never outside the hull the physics collides, or the drawn rock
        // would sink into whatever it rests on; bumps that would rise past
        // it are shaved to the hull's facet
        // (and never more than 3 mm inside it, or it floats a hair above it)
        const double radius = std::min(on_hull, std::max(on_hull - 0.003, 0.5 * (on_hull + low) + height));
        positions[k] = V3{d.x * radius - shape.com_offset.x, d.y * radius - shape.com_offset.y, d.z * radius - shape.com_offset.z};
        colours[k] = stone_colour(recipe, d, height, rms_height);
    }
    // Normals: each corner averages the faces round its vertex that turn less
    // than 40 degrees from its own face, so the stone's rounded parts shade
    // smoothly and a fracture's edge stays sharp.
    const size_t faces = sphere.triangles.size() / 3;
    std::vector<V3> face_normals(faces);
    std::vector<std::vector<size_t>> around(count);
    for (size_t f = 0; f < faces; f += 1) {
        const size_t a = static_cast<size_t>(sphere.triangles[3 * f]);
        const size_t b = static_cast<size_t>(sphere.triangles[3 * f + 1]);
        const size_t c = static_cast<size_t>(sphere.triangles[3 * f + 2]);
        face_normals[f] = cross(positions[b] - positions[a], positions[c] - positions[a]);
        around[a].push_back(f);
        around[b].push_back(f);
        around[c].push_back(f);
    }
    const double crease = std::cos(40.0 * kPi / 180.0);
    // each rock's grain starts somewhere else in the tile
    const double shift = (recipe.seed % 1000u) / 1000.0 * 7.0;
    RockMesh mesh;
    mesh.level = level;
    mesh.triangles.reserve(sphere.triangles.size());
    for (size_t k = 0; k + 2 < sphere.triangles.size(); k += 3) {
        // in the icosphere's own order, which faces outward as the renderer
        // expects (reversed, the renderer kept only the inside of each rock)
        const size_t corner[3] = {static_cast<size_t>(sphere.triangles[k]), static_cast<size_t>(sphere.triangles[k + 1]),
                                  static_cast<size_t>(sphere.triangles[k + 2])};
        const V3 face = cross(positions[corner[1]] - positions[corner[0]], positions[corner[2]] - positions[corner[0]]);
        const double ax = std::abs(face.x);
        const double ay = std::abs(face.y);
        const double az = std::abs(face.z);
        int axis = 2;
        if (ax >= ay && ax >= az) {
            axis = 0;
        } else if (ay >= az) {
            axis = 1;
        }
        const V3 own = norm(face_normals[k / 3]);
        for (int n = 0; n < 3; n += 1) {
            Vtx v;
            v.p = positions[corner[n]];
            V3 sum{0, 0, 0};
            const std::vector<size_t>& ring = around[corner[n]];
            for (size_t r = 0; r < ring.size(); r += 1) {
                const V3 other = face_normals[ring[r]];
                if (dot(norm(other), own) >= crease) {
                    sum = sum + other;
                }
            }
            v.n = norm(sum);
            v.c = colours[corner[n]];
            project_grain(v, axis, shift);
            mesh.triangles.push_back(v);
        }
    }
    return mesh;
}

// The texture's statistics: root-mean-square height over the sphere, and the
// rms slope probed at the finest octave's scale.
void texture_statistics(const RockRecipe& recipe, double& rms_height, double& rms_slope) {
    const std::vector<phys::Vec3> probe = golden_directions(600);
    double height_squares = 0;
    double slope_squares = 0;
    const double step = 0.25 * recipe.radius / std::pow(2.0, kOctaves - 1);
    for (size_t k = 0; k < probe.size(); k += 1) {
        const phys::Vec3 d = probe[k];
        const phys::Vec3 helper = std::abs(d.z) < 0.9 ? phys::Vec3{0, 0, 1} : phys::Vec3{1, 0, 0};
        const phys::Vec3 side = unit(phys::cross(d, helper));
        const phys::Vec3 other = phys::cross(d, side);
        const double here = texture_height(recipe, d);
        const double along_side = texture_height(recipe, unit(d + side * (step / recipe.radius)));
        const double along_other = texture_height(recipe, unit(d + other * (step / recipe.radius)));
        const double slope_side = (along_side - here) / step;
        const double slope_other = (along_other - here) / step;
        height_squares += here * here;
        slope_squares += slope_side * slope_side + slope_other * slope_other;
    }
    rms_height = std::sqrt(height_squares / static_cast<double>(probe.size()));
    rms_slope = std::sqrt(slope_squares / static_cast<double>(probe.size()));
}

}  // namespace

// ---------------------------------------------------------------- public

const char* rock_class_name(RockClass kind) {
    if (kind == RockClass::river_disc) {
        return "river disc";
    }
    if (kind == RockClass::cobble) {
        return "cobble";
    }
    if (kind == RockClass::block) {
        return "block";
    }
    if (kind == RockClass::slab) {
        return "slab";
    }
    return "shard";
}

Rock make_rock(std::uint32_t run_seed, int index) {
    const std::uint32_t seed = (run_seed * 2246822519u) ^ (static_cast<std::uint32_t>(index) * 3266489917u + 374761393u);
    Random state;
    state.state = seed * 40503u + 977u;
    double pick = random_unit(state);
    const ClassSpec* chosen = &kClasses[4];
    for (int k = 0; k < 5; k += 1) {
        if (pick < kClasses[k].weight) {
            chosen = &kClasses[k];
            break;
        }
        pick -= kClasses[k].weight;
    }
    const ClassSpec& spec = *chosen;
    // sizes skew toward the small end of the class's range
    const double draw = random_unit(state);
    const double size = spec.size_low + (spec.size_high - spec.size_low) * draw * draw;
    double aspect[3];
    aspect[0] = spec.aspect[0];
    aspect[1] = spec.aspect[1] * (0.85 + 0.3 * random_unit(state));
    aspect[2] = spec.aspect[2] * (0.8 + 0.4 * random_unit(state));
    const double amplitude = spec.amplitude * (0.75 + 0.5 * random_unit(state));
    Rock rock;
    rock.index = index;
    rock.recipe = make_recipe(seed, spec, size, aspect, amplitude, state);
    const RockRecipe& recipe = rock.recipe;

    // Collision hull: the geometric surface sampled at the vertex budget. A
    // polyhedron with its corners on a convex surface lies inside it, so the
    // corners are pushed out until hull and surface agree on average.
    const std::vector<phys::Vec3> samples = golden_directions(kVertexBudget);
    std::vector<phys::Vec3> points;
    points.reserve(samples.size());
    for (size_t k = 0; k < samples.size(); k += 1) {
        points.push_back(samples[k] * low_radius(recipe, samples[k]));
    }
    const phys::Shape first = cook_points(points, 0.75);
    const std::vector<phys::Vec3> check = golden_directions(400);
    double ratio_sum = 0;
    for (size_t k = 0; k < check.size(); k += 1) {
        ratio_sum += low_radius(recipe, check[k]) / hull_radius(first, check[k]);
    }
    const double inflate = ratio_sum / static_cast<double>(check.size());
    for (size_t k = 0; k < points.size(); k += 1) {
        points[k] = points[k] * inflate;
    }
    double rms_height = 0;
    double rms_slope = 0;
    texture_statistics(recipe, rms_height, rms_slope);
    const double roughness = std::min(kRoughnessLimit, kRoughnessGain * std::atan(rms_slope) * 180 / kPi);
    rock.friction = std::tan((recipe.basic_friction_angle + roughness) * kPi / 180);
    rock.shape = cook_points(points, rock.friction);
    rock.diameter = 2 * recipe.radius;
    rock.far_mesh = build_mesh(recipe, rock.shape, 2, rms_height);
    return rock;
}

void ensure_near_mesh(Rock& rock) {
    if (!rock.near_mesh.triangles.empty()) {
        return;
    }
    double rms_height = 0;
    double rms_slope = 0;
    texture_statistics(rock.recipe, rms_height, rms_slope);
    rock.near_mesh = build_mesh(rock.recipe, rock.shape, 3, rms_height);
}

// The orientation that puts a rock's thinnest axis (its largest moment of
// inertia) vertical: Jacobi rotations on the inertia tensor.
phys::Quat flat_side_down(const Rock& rock) {
    double a[3][3];
    double v[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int i = 0; i < 3; i += 1) {
        for (int j = 0; j < 3; j += 1) {
            a[i][j] = rock.shape.inertia[static_cast<size_t>(i * 3 + j)];
        }
    }
    for (int sweep = 0; sweep < 30; sweep += 1) {
        for (int p = 0; p < 2; p += 1) {
            for (int q = p + 1; q < 3; q += 1) {
                if (std::abs(a[p][q]) < 1e-15) {
                    continue;
                }
                const double theta = 0.5 * std::atan2(2 * a[p][q], a[q][q] - a[p][p]);
                const double c = std::cos(theta);
                const double s = std::sin(theta);
                for (int k = 0; k < 3; k += 1) {
                    const double akp = a[k][p];
                    const double akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < 3; k += 1) {
                    const double apk = a[p][k];
                    const double aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < 3; k += 1) {
                    const double vkp = v[k][p];
                    const double vkq = v[k][q];
                    v[k][p] = c * vkp - s * vkq;
                    v[k][q] = s * vkp + c * vkq;
                }
            }
        }
    }
    int thin = 0;
    for (int k = 1; k < 3; k += 1) {
        if (a[k][k] > a[thin][thin]) {
            thin = k;
        }
    }
    // the rotation taking that body axis to world z
    const phys::Vec3 axis{v[0][thin], v[1][thin], v[2][thin]};
    const phys::Vec3 up{0, 0, 1};
    const phys::Vec3 turn = phys::cross(axis, up);
    const double sine = phys::length(turn);
    const double cosine = phys::dot(axis, up);
    if (sine < 1e-9) {
        return cosine > 0 ? phys::Quat{} : phys::from_axis_angle(phys::Vec3{1, 0, 0}, 3.14159265358979);
    }
    return phys::from_axis_angle(turn * (1 / sine), std::atan2(sine, cosine));
}

const Tex& rock_texture(const Rock& rock) {
    // ZC_ARROW_ROCKS=1 paints every rock with arrows fixed to its surface
    static const bool arrows = std::getenv("ZC_ARROW_ROCKS") != nullptr;
    if (arrows) {
        return arrow_texture();
    }
    return stone_texture(rock.recipe.stone);
}

const Tex& stone_grain() {
    static Tex grain;
    if (grain.w == 0) {
        grain.make(64, 64);
        Random specks;
        specks.state = 0x2545F491u;
        for (int y = 0; y < 64; y += 1) {
            for (int x = 0; x < 64; x += 1) {
                // a fine and a coarse scale of noise (the tile seam is lost in it), and
                // salt-and-pepper grains: dark mica, pale feldspar
                const double fine = noise3(x * 0.5, y * 0.5, 0.5, 977u);
                const double coarse = noise3(x / 8.0, y / 8.0, 3.5, 4421u);
                double value = 0.95 + 0.08 * fine + 0.05 * coarse;
                const double grain_draw = random_unit(specks);
                if (grain_draw < 0.06) {
                    value *= 0.62;
                } else if (grain_draw < 0.10) {
                    value *= 1.12;
                }
                const int level = static_cast<int>(std::lround(std::min(1.0, std::max(0.0, value)) * 255));
                grain.at(x, y) = 0xFF000000u | (static_cast<std::uint32_t>(level) << 16) | (static_cast<std::uint32_t>(level) << 8) |
                                 static_cast<std::uint32_t>(level);
            }
        }
        grain.build_mips();
    }
    return grain;
}

}  // namespace zc
