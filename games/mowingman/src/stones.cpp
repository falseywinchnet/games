#include "stones.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mm {
namespace {

constexpr double pi = 3.14159265358979323846;

// ---------------------------------------------------------------- Rock Stack's noise

std::uint32_t hash3(std::int32_t ix, std::int32_t iy, std::int32_t iz, std::uint32_t seed) {
    std::uint32_t h = static_cast<std::uint32_t>(ix) * 374761393U;
    h = h ^ (static_cast<std::uint32_t>(iy) * 668265263U);
    h = h ^ (static_cast<std::uint32_t>(iz) * 2147483647U);
    h = h ^ (seed * 1274126177U);
    h = (h ^ (h >> 13U)) * 1274126177U;
    h = h ^ (h >> 16U);
    return h;
}

double lattice(std::int32_t ix, std::int32_t iy, std::int32_t iz, std::uint32_t seed, double fx, double fy, double fz) {
    const std::uint32_t h = hash3(ix, iy, iz, seed) & 15U;
    const double u = h < 8 ? fx : fy;
    double w = fz;
    if (h < 4)
        w = fy;
    else if (h == 12 || h == 14)
        w = fx;
    const double first = (h & 1U) == 0 ? u : -u;
    const double second = (h & 2U) == 0 ? w : -w;
    return first + second;
}

double fade(double t) {
    return t * t * t * (t * (t * 6 - 15) + 10);
}

double gradient_noise(double x, double y, double z, std::uint32_t seed) {
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
    const double x00 = n000 + u * (n100 - n000);
    const double x10 = n010 + u * (n110 - n010);
    const double x01 = n001 + u * (n101 - n001);
    const double x11 = n011 + u * (n111 - n011);
    const double y0 = x00 + v * (x10 - x00);
    const double y1 = x01 + v * (x11 - x01);
    return y0 + w * (y1 - y0);
}

struct Random {
    std::uint32_t state{};
};

double random_unit(Random& random) {
    random.state = random.state + 0x6d2b79f5U;
    std::uint32_t t = random.state;
    t = (t ^ (t >> 15U)) * (t | 1U);
    t = t ^ (t + (t ^ (t >> 7U)) * (t | 61U));
    const std::uint32_t mixed = t ^ (t >> 14U);
    return static_cast<double>(mixed) / 4294967296.0;
}

// ---------------------------------------------------------------- Rock Stack's classes and stones

enum class StoneKind : std::uint8_t { pink_granite, grey_granite, sandstone, slate, basalt, quartzite, limestone, greywacke };

struct ClassSpec {
    StoneClass kind;
    double weight;
    double aspect[3];
    double amplitude;
    double hurst;
    int fractures;
    double fracture_depth;
    double bedding;
    double squareness;
    bool ridged;
    StoneKind stones[4];
    double vein_chance;
    double lichen_chance;
};

// Rock Stack's five, weighted for a garden edging: more rounded cobbles and river stones,
// fewer jagged shards.
const ClassSpec classes[5] = {
    {StoneClass::river_disc, 0.30, {1.0, 0.86, 0.42}, 0.04, 1.1, 0, 0.0, 0.0, 2.4, false,
     {StoneKind::grey_granite, StoneKind::quartzite, StoneKind::basalt, StoneKind::greywacke}, 0.30, 0.10},
    {StoneClass::cobble, 0.30, {1.0, 0.80, 0.60}, 0.06, 1.0, 2, 0.15, 0.0, 2.0, false,
     {StoneKind::pink_granite, StoneKind::grey_granite, StoneKind::quartzite, StoneKind::sandstone}, 0.35, 0.20},
    {StoneClass::block, 0.20, {1.0, 0.80, 0.62}, 0.08, 0.85, 9, 0.30, 0.0, 3.4, true,
     {StoneKind::pink_granite, StoneKind::grey_granite, StoneKind::sandstone, StoneKind::limestone}, 0.25, 0.35},
    {StoneClass::slab, 0.12, {1.0, 0.75, 0.32}, 0.06, 0.9, 6, 0.30, 0.8, 3.6, false,
     {StoneKind::slate, StoneKind::sandstone, StoneKind::limestone, StoneKind::greywacke}, 0.15, 0.40},
    {StoneClass::shard, 0.08, {1.0, 0.62, 0.48}, 0.12, 0.7, 11, 0.45, 0.0, 2.8, true,
     {StoneKind::basalt, StoneKind::slate, StoneKind::quartzite, StoneKind::greywacke}, 0.10, 0.10},
};

// The stones' average colours (sRGB), after Rock Stack's stone textures.
const double stone_colours[8][3] = {{0.72, 0.58, 0.54}, {0.62, 0.62, 0.60}, {0.78, 0.66, 0.49}, {0.37, 0.39, 0.43},
                                    {0.27, 0.27, 0.28}, {0.83, 0.81, 0.77}, {0.80, 0.78, 0.70}, {0.46, 0.46, 0.43}};

constexpr int octaves = 7;

struct Recipe {
    std::uint32_t seed{};
    double axes[3]{};
    double radius{};
    std::vector<V3> fracture_normals{};
    std::vector<double> fracture_offsets{};
    double amplitude{};
    double hurst{};
    double squareness{2};
    bool ridged{};
    int cutoff{octaves};
    double offset[3]{};
    StoneKind stone{};
    double tone[3]{1, 1, 1};
    bool vein{};
    V3 vein_normal{};
    double vein_offset{};
    double vein_width{};
    double lichen{};
    double lichen_colour[3]{0.62, 0.66, 0.5};
};

double octave_sum(const Recipe& r, V3 d, int first, int last) {
    double sum = 0;
    for (int k = first; k < last; ++k) {
        const double frequency = std::pow(2.0, k);
        const double amplitude = r.amplitude * std::pow(2.0, -r.hurst * k);
        double n = gradient_noise(d.x * frequency + r.offset[0], d.y * frequency + r.offset[1], d.z * frequency + r.offset[2], r.seed + static_cast<std::uint32_t>(k) * 7919U);
        if (r.ridged && k >= 1)
            n = 0.5 - std::abs(n) * 1.6;
        sum += amplitude * n;
    }
    return sum;
}

double radius_along(const Recipe& r, V3 d) {
    const double p = r.squareness;
    const double qx = std::pow(std::abs(d.x / r.axes[0]), p);
    const double qy = std::pow(std::abs(d.y / r.axes[1]), p);
    const double qz = std::pow(std::abs(d.z / r.axes[2]), p);
    double radius = std::pow(qx + qy + qz, -1.0 / p);
    radius *= 1 + octave_sum(r, d, 0, octaves);  // the garden sees all of it: no hull to keep to
    for (std::size_t k = 0; k < r.fracture_normals.size(); ++k) {
        const double facing = dot(d, r.fracture_normals[k]);
        if (facing > 1e-6)
            radius = std::min(radius, r.fracture_offsets[k] / facing);
    }
    return radius;
}

Recipe make_recipe(std::uint32_t seed, double size) {
    Random look{seed * 2246822519U + 7U};
    double pick = random_unit(look);
    int c = 0;
    while (c < 4 && pick > classes[c].weight) {
        pick -= classes[c].weight;
        ++c;
    }
    const ClassSpec& spec = classes[c];
    Recipe r{};
    r.seed = seed;
    r.amplitude = spec.amplitude;
    r.hurst = spec.hurst;
    r.squareness = spec.squareness * (0.9 + 0.2 * random_unit(look));
    r.ridged = spec.ridged;
    Random state{seed * 2654435761U};
    const double mean = (spec.aspect[0] + spec.aspect[1] + spec.aspect[2]) / 3;
    for (int k = 0; k < 3; ++k)
        r.axes[k] = 0.5 * size * spec.aspect[k] / mean * (0.85 + 0.3 * random_unit(state));
    r.radius = std::cbrt(r.axes[0] * r.axes[1] * r.axes[2]);
    for (int k = 0; k < spec.fractures; ++k) {
        const double z = 2 * random_unit(state) - 1;
        const double angle = 2 * pi * random_unit(state);
        const double ring = std::sqrt(std::max(0.0, 1 - z * z));
        const V3 normal{ring * std::cos(angle), ring * std::sin(angle), z};
        const double qx = normal.x / r.axes[0];
        const double qy = normal.y / r.axes[1];
        const double qz = normal.z / r.axes[2];
        const double reach = 1 / std::sqrt(qx * qx + qy * qy + qz * qz);
        r.fracture_normals.push_back(normal);
        r.fracture_offsets.push_back(reach * (1 - spec.fracture_depth * random_unit(state)));
    }
    if (spec.bedding > 0) {
        const double t1 = 0.06 * (2 * random_unit(state) - 1);
        const double t2 = 0.06 * (2 * random_unit(state) - 1);
        r.fracture_normals.push_back(normalized(V3{t1, t2, 1}));
        r.fracture_offsets.push_back(r.axes[2] * spec.bedding);
        const double t3 = 0.06 * (2 * random_unit(state) - 1);
        const double t4 = 0.06 * (2 * random_unit(state) - 1);
        r.fracture_normals.push_back(normalized(V3{t3, t4, -1}));
        r.fracture_offsets.push_back(r.axes[2] * spec.bedding);
    }
    for (double& o : r.offset)
        o = random_unit(state) * 64;
    r.stone = spec.stones[static_cast<int>(random_unit(look) * 4) % 4];
    const double tone = 1.0 + 0.16 * (random_unit(look) - 0.5);
    const double warmth = 0.06 * (random_unit(look) - 0.5);
    r.tone[0] = tone * (1 + warmth);
    r.tone[1] = tone;
    r.tone[2] = tone * (1 - warmth);
    r.vein = random_unit(look) < spec.vein_chance;
    {
        const double z = 2 * random_unit(look) - 1;
        const double angle = 2 * pi * random_unit(look);
        const double ring = std::sqrt(std::max(0.0, 1 - z * z));
        r.vein_normal = V3{ring * std::cos(angle), ring * std::sin(angle), z};
        r.vein_offset = (random_unit(look) - 0.5) * r.radius;
        r.vein_width = r.radius * (0.06 + 0.06 * random_unit(look));
    }
    if (random_unit(look) < spec.lichen_chance) {
        r.lichen = 0.25 + 0.5 * random_unit(look);
        if (random_unit(look) < 0.2) {
            r.lichen_colour[0] = 0.80;
            r.lichen_colour[1] = 0.58;
            r.lichen_colour[2] = 0.28;
        } else if (random_unit(look) < 0.5) {
            r.lichen_colour[0] = 0.72;
            r.lichen_colour[1] = 0.74;
            r.lichen_colour[2] = 0.62;
        }
    }
    return r;
}

double to_linear(double c) {
    return std::pow(std::max(0.0, c), 2.2);
}

// Rock Stack's stone colouring: shading in the hollows, mottling, bedding bands in
// sandstone and greywacke, a vein of quartz, lichen in patches.
V3 colour_at(const Recipe& r, V3 d, double hollow) {
    const double* mean = stone_colours[static_cast<int>(r.stone)];
    const double shade = std::clamp(0.94 + 0.16 * hollow, 0.72, 1.1);
    double c[3] = {mean[0] * r.tone[0] * shade, mean[1] * r.tone[1] * shade, mean[2] * r.tone[2] * shade};
    const double mottle = gradient_noise(d.x * 5 + 7, d.y * 5 + 3, d.z * 5 + 11, r.seed ^ 0x5bd1e995U) +
                          0.5 * gradient_noise(d.x * 13 + 1, d.y * 13 + 9, d.z * 13 + 4, r.seed ^ 0x27d4eb2dU);
    const double m = std::clamp(1 + 0.13 * mottle, 0.82, 1.18);
    for (double& v : c)
        v *= m;
    if (r.stone == StoneKind::sandstone || r.stone == StoneKind::greywacke) {
        const double across = dot(d, r.vein_normal) * r.radius;
        const double band = std::sin(across / 0.006 + 2.5 * gradient_noise(d.x * 3, d.y * 3, d.z * 3, r.seed ^ 0x165667b1U));
        const double k = std::clamp(1 + 0.07 * band, 0.9, 1.1);
        c[0] *= k;
        c[1] *= k * 0.99;
        c[2] *= k * 0.97;
    }
    if (r.vein) {
        const double distance = std::abs(dot(d, r.vein_normal) * r.radius - r.vein_offset);
        if (distance < r.vein_width) {
            const double k = std::clamp(1 - distance / r.vein_width, 0.0, 1.0) * 0.65;
            const double white[3] = {0.9, 0.88, 0.84};
            for (int i = 0; i < 3; ++i)
                c[i] += (white[i] - c[i]) * k;
        }
    }
    if (r.lichen > 0) {
        const double patch = gradient_noise(d.x * 3.1 + 19, d.y * 3.1 + 5, d.z * 3.1 + 2, r.seed ^ 0x9e3779b9U);
        const double speck = gradient_noise(d.x * 11, d.y * 11, d.z * 11, r.seed ^ 0x85ebca6bU);
        const double threshold = 0.18 - 0.35 * r.lichen;
        if (patch > threshold && speck > -0.15) {
            const double k = std::clamp((patch - threshold) * 4, 0.0, 0.8);
            for (int i = 0; i < 3; ++i)
                c[i] += (r.lichen_colour[i] - c[i]) * k;
        }
    }
    return V3{to_linear(c[0]), to_linear(c[1]), to_linear(c[2])};
}

} // namespace

void build_stone(Mesh& mesh, std::uint32_t seed, double size, double ppm, const Xform& place) {
    const Recipe r = make_recipe(seed, size);
    Builder b(mesh, ppm);
    b.at = place;
    const int around = b.sides(r.radius, 10, 40);
    const int rows = around / 2 + 1;
    std::vector<V3> points{};
    std::vector<V3> directions{};
    std::vector<double> hollows{};
    points.reserve(static_cast<std::size_t>(rows * around));
    for (int row = 0; row < rows; ++row) {
        const double lat = -pi / 2 + pi * row / (rows - 1);
        for (int col = 0; col < around; ++col) {
            const double a = 2 * pi * col / around;
            const V3 d{std::cos(lat) * std::cos(a), std::cos(lat) * std::sin(a), std::sin(lat)};
            const double rad = radius_along(r, d);
            points.push_back(d * rad);
            directions.push_back(d);
            hollows.push_back(octave_sum(r, d, 3, octaves) / std::max(1e-6, r.amplitude * 0.3));
        }
    }
    const std::size_t first = mesh.vertices.size();
    b.grid(points, rows, around, true);
    for (std::size_t k = 0; k < directions.size() && first + k < mesh.vertices.size(); ++k)
        mesh.vertices[first + k].colour = colour_at(r, directions[k], hollows[k]);
}

} // namespace mm
