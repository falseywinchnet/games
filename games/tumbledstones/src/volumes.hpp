#pragma once
// What grown shapes may share: hashed numbers, easing and broken faces. Each family's own
// solid is in its file.
#include "noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ts {

inline constexpr double volume_pi = 3.14159265358979323846;

// 0..1 from two integers and a key.
[[nodiscard]] inline double grown_unit(int a, int b, std::uint32_t key) {
    return (hash_signed(a, b, 37, key) + 1) * .5;
}

[[nodiscard]] inline double grown_ease(double edge0, double edge1, double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

inline void grown_normalise(double v[3]) {
    const double l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 0)
        for (int i = 0; i < 3; ++i)
            v[i] /= l;
}

// A broken face: a plane through `at` facing `n` (outside the piece), bowed about where
// the break began, rippled outwards from there if glassy, roughened if not.
struct BrokenFace {
    double n[3] = {1, 0, 0};
    double at[3] = {0, 0, 0};
    double bow = 0;
    double ripple = 0;
    double rough = 0;
    std::uint32_t key = 1;

    // How far outside a point lies, and how far across the face from where it began.
    [[nodiscard]] double value(double x, double y, double z, double& lateral) const {
        const double dx = x - at[0];
        const double dy = y - at[1];
        const double dz = z - at[2];
        const double q = dx * n[0] + dy * n[1] + dz * n[2];
        lateral = std::sqrt(std::max(0.0, dx * dx + dy * dy + dz * dz - q * q));
        double v = q + bow * lateral * lateral;
        if (ripple != 0)
            v += ripple * std::sin(lateral * 40) * std::exp(-lateral * 1.4);
        if (rough != 0)
            v += rough * fbm3(x * 4.5, y * 4.5, z * 4.5, key, 3);
        return v;
    }
};

// Breaks round `middle`: `count` faces turned round the horizon of `up` (each tilted a
// little), `size` and a bit away from it.
inline void grown_break_round(const double middle[3], const double up[3], int count, double size, double tilt_range,
                              std::uint32_t key, std::vector<BrokenFace>& faces) {
    double a[3] = {1, 0, 0};
    if (std::abs(up[0]) > .8) {
        a[0] = 0;
        a[1] = 1;
    }
    const double dot = a[0] * up[0] + a[1] * up[1] + a[2] * up[2];
    for (int k = 0; k < 3; ++k)
        a[k] -= dot * up[k];
    grown_normalise(a);
    const double b[3] = {up[1] * a[2] - up[2] * a[1], up[2] * a[0] - up[0] * a[2], up[0] * a[1] - up[1] * a[0]};
    const double start = 2 * volume_pi * grown_unit(0, 31, key);
    for (int k = 0; k < count; ++k) {
        const double angle = start + 2 * volume_pi * (k + .55 * (grown_unit(k, 32, key) - .5)) / count;
        const double tilt = tilt_range * (grown_unit(k, 33, key) - .5);
        BrokenFace face;
        for (int i = 0; i < 3; ++i)
            face.n[i] = std::cos(angle) * a[i] + std::sin(angle) * b[i] + tilt * up[i];
        grown_normalise(face.n);
        const double offset = size * (.7 + .55 * grown_unit(k, 34, key));
        for (int i = 0; i < 3; ++i)
            face.at[i] = middle[i] + face.n[i] * offset;
        face.key = key + 101U * static_cast<std::uint32_t>(k + 1);
        faces.push_back(face);
    }
}

}  // namespace ts
