#pragma once
// The small arithmetic every part of the falls shares: vectors, linear colour, and
// deterministic noise. Everything here is a pure function of its arguments, so a
// picture made twice from the same seed is the same picture.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cf {

constexpr double pi = 3.14159265358979323846;

struct Vec3 {
    double x = 0, y = 0, z = 0;
};

inline Vec3 operator+(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 operator-(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 operator*(Vec3 a, double s) {
    return {a.x * s, a.y * s, a.z * s};
}
inline double dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline double length(Vec3 a) {
    return std::sqrt(dot(a, a));
}
inline Vec3 normalized(Vec3 a) {
    const double n = length(a);
    return n > 0 ? a * (1 / n) : Vec3{0, 0, 1};
}
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// Linear light, before exposure and the tone curve; may exceed 1.
struct Rgb {
    double r = 0, g = 0, b = 0;
};

inline Rgb operator+(Rgb a, Rgb b) {
    return {a.r + b.r, a.g + b.g, a.b + b.b};
}
inline Rgb operator*(Rgb a, double s) {
    return {a.r * s, a.g * s, a.b * s};
}
inline Rgb operator*(Rgb a, Rgb b) {
    return {a.r * b.r, a.g * b.g, a.b * b.b};
}
inline Rgb mix(Rgb a, Rgb b, double t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
inline double luminance(Rgb c) {
    return .2126 * c.r + .7152 * c.g + .0722 * c.b;
}
// An sRGB-encoded colour given as 0..255, in linear light.
inline Rgb srgb(int r, int g, int b) {
    const int channels[3] = {r, g, b};
    double out[3] = {};
    for (int i = 0; i < 3; ++i) {
        const double v = channels[i] / 255.0;
        out[i] = v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4);
    }
    return {out[0], out[1], out[2]};
}

inline double smoothstep(double edge0, double edge1, double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}
inline double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

// A 32-bit integer hash (lowbias32): well mixed, cheap, the same everywhere.
inline std::uint32_t hash32(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
inline std::uint32_t hash2(int x, int y, std::uint32_t seed) {
    const std::uint32_t a = hash32(static_cast<std::uint32_t>(x) * 0x9E3779B1U + seed);
    const std::uint32_t b = hash32(a ^ (static_cast<std::uint32_t>(y) * 0x85EBCA77U));
    return b;
}
inline std::uint32_t hash3(int x, int y, int z, std::uint32_t seed) {
    return hash2(x, y, hash32(static_cast<std::uint32_t>(z) * 0xC2B2AE3DU + seed));
}
// 0..1 from a hash.
inline double unit(std::uint32_t h) {
    return (h >> 8) * (1.0 / 16777216.0);
}

// The unit vectors of the gradient noise's directions, a full turn in 65536 steps with
// a guard, read between steps: the same directions as working out the sine and cosine,
// to within a billionth, at a fraction of the cost.
struct TurnTable {
    double cosine[65537]{};
    double sine[65537]{};
    TurnTable() {
        for (int i = 0; i <= 65536; ++i) {
            const double angle = 2 * pi * i / 65536.0;
            cosine[i] = std::cos(angle);
            sine[i] = std::sin(angle);
        }
    }
};
inline const TurnTable& turn_table() {
    static const TurnTable table;
    return table;
}

// The cosine of an angle in radians, from the table.
inline double turn_cos(double angle) {
    double turns = angle * (1 / (2 * pi));
    turns -= std::floor(turns);
    const double at = turns * 65536;
    const int step = std::min(65535, static_cast<int>(at));
    const double between = at - step;
    const TurnTable& t = turn_table();
    return t.cosine[step] + (t.cosine[step + 1] - t.cosine[step]) * between;
}

// Gradient noise in two dimensions, about -1..1, smooth (quintic fade).
inline double gradient(int ix, int iy, std::uint32_t seed, double fx, double fy) {
    const std::uint32_t h = hash2(ix, iy, seed);
    // unit(h) * 65536, as the table's step and the fraction between steps.
    const std::uint32_t bits = h >> 8;            // 24 bits of the turn
    const std::uint32_t step = bits >> 8;         // 16 of them pick the step
    const double between = (bits & 255U) * (1.0 / 256);
    const TurnTable& t = turn_table();
    const double c = t.cosine[step] + (t.cosine[step + 1] - t.cosine[step]) * between;
    const double s = t.sine[step] + (t.sine[step + 1] - t.sine[step]) * between;
    return c * fx + s * fy;
}
inline double noise2(double x, double y, std::uint32_t seed) {
    const double fx0 = std::floor(x);
    const double fy0 = std::floor(y);
    const int ix = static_cast<int>(fx0);
    const int iy = static_cast<int>(fy0);
    const double fx = x - fx0;
    const double fy = y - fy0;
    const double ux = fx * fx * fx * (fx * (fx * 6 - 15) + 10);
    const double uy = fy * fy * fy * (fy * (fy * 6 - 15) + 10);
    const double n00 = gradient(ix, iy, seed, fx, fy);
    const double n10 = gradient(ix + 1, iy, seed, fx - 1, fy);
    const double n01 = gradient(ix, iy + 1, seed, fx, fy - 1);
    const double n11 = gradient(ix + 1, iy + 1, seed, fx - 1, fy - 1);
    const double top = lerp(n00, n10, ux);
    const double bottom = lerp(n01, n11, ux);
    return lerp(top, bottom, uy) * 1.414;
}
// Fractal sum of `octaves` of noise2, each twice as fine and `gain` as strong.
inline double fbm(double x, double y, std::uint32_t seed, int octaves, double gain = .5) {
    double sum = 0;
    double amplitude = 1;
    double norm = 0;
    for (int o = 0; o < octaves; ++o) {
        sum += amplitude * noise2(x, y, seed + static_cast<std::uint32_t>(o) * 1013U);
        norm += amplitude;
        amplitude *= gain;
        x *= 2.03;
        y *= 2.03;
    }
    return sum / norm;
}
// Distance to the nearest of a jittered point per cell (cellular noise), and to the
// second nearest; their difference is small along the cracks between cells.
struct Cells {
    double nearest = 0, second = 0;
    std::uint32_t id = 0;
};
inline Cells cells(double x, double y, std::uint32_t seed) {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    Cells out{9, 9, 0};
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const std::uint32_t h = hash2(ix + dx, iy + dy, seed);
            const double px = ix + dx + unit(h);
            const double py = iy + dy + unit(hash32(h));
            const double d = std::sqrt((px - x) * (px - x) + (py - y) * (py - y));
            if (d < out.nearest) {
                out.second = out.nearest;
                out.nearest = d;
                out.id = h;
            } else if (d < out.second) {
                out.second = d;
            }
        }
    return out;
}

} // namespace cf
