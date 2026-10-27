#include "room.hpp"

#include "minerals.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace stones {
namespace {

constexpr double pi = 3.14159265358979323846;

void octahedral(double x, double y, double z, double& u, double& v) {
    const double sum = std::abs(x) + std::abs(y) + std::abs(z);
    double px = x / sum;
    double py = y / sum;
    if (z < 0) {
        const double fx = (1 - std::abs(py)) * (px >= 0 ? 1 : -1);
        const double fy = (1 - std::abs(px)) * (py >= 0 ? 1 : -1);
        px = fx;
        py = fy;
    }
    u = px * .5 + .5;
    v = py * .5 + .5;
}

Light3 lookup(const float* map, int side, double x, double y, double z) {
    double u = 0;
    double v = 0;
    octahedral(x, y, z, u, v);
    const double fx = std::clamp(u * side - .5, 0.0, side - 1.0);
    const double fy = std::clamp(v * side - .5, 0.0, side - 1.0);
    const int x0 = std::min(static_cast<int>(fx), side - 2);
    const int y0 = std::min(static_cast<int>(fy), side - 2);
    const float tx = static_cast<float>(fx - x0);
    const float ty = static_cast<float>(fy - y0);
    const float* a = map + static_cast<std::size_t>(y0 * side + x0) * 3;
    const float* b = a + 3;
    const float* c = a + static_cast<std::size_t>(side) * 3;
    const float* d = c + 3;
    Light3 out;
    out.r = (a[0] + (b[0] - a[0]) * tx) * (1 - ty) + (c[0] + (d[0] - c[0]) * tx) * ty;
    out.g = (a[1] + (b[1] - a[1]) * tx) * (1 - ty) + (c[1] + (d[1] - c[1]) * tx) * ty;
    out.b = (a[2] + (b[2] - a[2]) * tx) * (1 - ty) + (c[2] + (d[2] - c[2]) * tx) * ty;
    return out;
}

Light3 blend(Light3 a, Light3 b, double t) {
    const float k = static_cast<float>(std::clamp(t, 0.0, 1.0));
    return {a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k};
}

double luminance(Light3 c) {
    return .2126 * c.r + .7152 * c.g + .0722 * c.b;
}

Sun make_sun() {
    Sun sun;
    const float* s = room_sun_bake;
    sun.x = s[0];
    sun.y = s[1];
    sun.z = s[2];
    sun.light = {s[3] * s[6], s[4] * s[6], s[5] * s[6]};
    return sun;
}

std::vector<LightDirection> gather_lights() {
    // Bands of elevation over the upper half, each cut into sectors of azimuth.
    constexpr int bands = 4;
    constexpr int sectors[bands] = {12, 10, 6, 1};
    constexpr double top[bands + 1] = {0, 18, 40, 66, 90};
    struct Cell {
        double x = 0, y = 0, z = 0, weight = 0;
    };
    std::vector<Cell> cells;
    for (int band = 0; band < bands; ++band)
        for (int s = 0; s < sectors[band]; ++s)
            cells.push_back({});
    constexpr int side = 128;
    for (int row = 0; row < side; ++row) {
        for (int column = 0; column < side; ++column) {
            double px = (column + .5) / side * 2 - 1;
            double py = (row + .5) / side * 2 - 1;
            const double pz = 1 - std::abs(px) - std::abs(py);
            if (pz <= 0)
                continue;
            const double length = std::sqrt(px * px + py * py + pz * pz);
            const double x = px / length;
            const double y = py / length;
            const double z = pz / length;
            // The texel's solid angle on the octahedron's upper face: (2/side)^2 projected.
            const double solid = (4.0 / (side * side)) * pz / (length * length * length) * 2;
            const float* t = room_mirror_texels + static_cast<std::size_t>(row * side + column) * 3;
            const double light = .2126 * t[0] + .7152 * t[1] + .0722 * t[2];
            const double w = light * z * solid;
            const double elevation = std::asin(std::clamp(z, 0.0, 1.0)) * 180 / pi;
            int band = 0;
            while (band < bands - 1 && elevation >= top[band + 1])
                ++band;
            double azimuth = std::atan2(y, x);
            if (azimuth < 0)
                azimuth += 2 * pi;
            const int sector = std::min(sectors[band] - 1, static_cast<int>(azimuth / (2 * pi) * sectors[band]));
            int index = sector;
            for (int b = 0; b < band; ++b)
                index += sectors[b];
            Cell& cell = cells[static_cast<std::size_t>(index)];
            cell.x += x * w;
            cell.y += y * w;
            cell.z += z * w;
            cell.weight += w;
        }
    }
    std::vector<LightDirection> lights;
    double total = 0;
    for (const Cell& cell : cells) {
        if (cell.weight <= 0)
            continue;
        const double length = std::sqrt(cell.x * cell.x + cell.y * cell.y + cell.z * cell.z);
        lights.push_back({cell.x / length, cell.y / length, cell.z / length, cell.weight});
        total += cell.weight;
    }
    const Sun& sun = room_sun();
    const double sun_weight = luminance(sun.light) * std::max(0.0, sun.z) / pi;
    if (sun_weight > 0) {
        lights.push_back({sun.x, sun.y, sun.z, sun_weight});
        total += sun_weight;
    }
    for (LightDirection& light : lights)
        light.weight /= total;
    return lights;
}

// White paper facing up comes to 0.85 on the screen (0.69 in linear light).
double measure_exposure() {
    const Light3 up = room_diffuse(0, 0, 1);
    const Sun& sun = room_sun();
    const double lit = luminance(up) + luminance(sun.light) * std::max(0.0, sun.z) / pi;
    return lit > 0 ? .69 / lit : 1.0;
}

}  // namespace

Light3 room_reflection(double x, double y, double z, double roughness) {
    if (roughness <= .05)
        return lookup(room_mirror_texels, 128, x, y, z);
    if (roughness <= .2)
        return blend(lookup(room_mirror_texels, 128, x, y, z), lookup(room_gloss_texels, 32, x, y, z),
                     (roughness - .05) / .15);
    if (roughness <= .45)
        return blend(lookup(room_gloss_texels, 32, x, y, z), lookup(room_satin_texels, 16, x, y, z),
                     (roughness - .2) / .25);
    return blend(lookup(room_satin_texels, 16, x, y, z), lookup(room_diffuse_texels, 16, x, y, z),
                 (roughness - .45) / .55);
}

Light3 room_diffuse(double x, double y, double z) {
    return lookup(room_diffuse_texels, 16, x, y, z);
}

const Sun& room_sun() {
    static const Sun sun = make_sun();
    return sun;
}

const std::vector<LightDirection>& room_lights() {
    static const std::vector<LightDirection> lights = gather_lights();
    return lights;
}

double exposure() {
    static const double value = measure_exposure();
    return value;
}

float screen_value(double light) {
    double v = light;
    if (v > .75)
        v = .75 + .25 * (1 - std::exp(-(v - .75) / .25));
    return srgb_from_linear(static_cast<float>(v));
}

}  // namespace stones
