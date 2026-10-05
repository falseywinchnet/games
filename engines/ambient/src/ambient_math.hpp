#pragma once
// Small single-precision vector and transform types for the ambient renderer.
// Scene geometry, pixels and shading are float because that is what the retained
// buffers store; scene time and route parameters are double (see motion.hpp).
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ambient {

struct Vec3 {
    float x{};
    float y{};
    float z{};
};

inline Vec3 add(Vec3 a, Vec3 b) {
    const Vec3 result{a.x + b.x, a.y + b.y, a.z + b.z};
    return result;
}
inline Vec3 subtract(Vec3 a, Vec3 b) {
    const Vec3 result{a.x - b.x, a.y - b.y, a.z - b.z};
    return result;
}
inline Vec3 scale(Vec3 a, float s) {
    const Vec3 result{a.x * s, a.y * s, a.z * s};
    return result;
}
inline Vec3 multiply(Vec3 a, Vec3 b) {
    const Vec3 result{a.x * b.x, a.y * b.y, a.z * b.z};
    return result;
}
inline float dot(Vec3 a, Vec3 b) {
    const float result = a.x * b.x + a.y * b.y + a.z * b.z;
    return result;
}
inline Vec3 cross(Vec3 a, Vec3 b) {
    const Vec3 result{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    return result;
}
inline float length(Vec3 a) {
    const float result = std::sqrt(dot(a, a));
    return result;
}
// A zero vector stays zero rather than becoming NaN.
inline Vec3 normalize(Vec3 a) {
    const float squared = dot(a, a);
    if (squared <= 1e-24F)
        return a;
    const float inverse = 1.0F / std::sqrt(squared);
    const Vec3 result{a.x * inverse, a.y * inverse, a.z * inverse};
    return result;
}
inline Vec3 mix(Vec3 a, Vec3 b, float t) {
    const Vec3 result{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
    return result;
}
inline float mix(float a, float b, float t) {
    const float result = a + (b - a) * t;
    return result;
}
inline float smoothstep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    const float result = t * t * (3.0F - 2.0F * t);
    return result;
}
inline float fract(float x) {
    const float result = x - std::floor(x);
    return result;
}
// Rotation about the vertical axis, matching the original shader's rotate_y.
inline Vec3 rotate_y(Vec3 p, float cosine, float sine) {
    const Vec3 result{cosine * p.x + sine * p.z, p.y, -sine * p.x + cosine * p.z};
    return result;
}

// A row-major 3 x 4 affine transform: world = M * (x, y, z, 1).
struct Affine {
    float m[12]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
};
inline Vec3 apply(const Affine& t, Vec3 p) {
    const Vec3 result{t.m[0] * p.x + t.m[1] * p.y + t.m[2] * p.z + t.m[3],
                      t.m[4] * p.x + t.m[5] * p.y + t.m[6] * p.z + t.m[7],
                      t.m[8] * p.x + t.m[9] * p.y + t.m[10] * p.z + t.m[11]};
    return result;
}
inline Vec3 apply_linear(const Affine& t, Vec3 p) {
    const Vec3 result{t.m[0] * p.x + t.m[1] * p.y + t.m[2] * p.z,
                      t.m[4] * p.x + t.m[5] * p.y + t.m[6] * p.z,
                      t.m[8] * p.x + t.m[9] * p.y + t.m[10] * p.z};
    return result;
}
// Normals transform by the inverse transpose. For the scaled rotations used by the
// archives this is the linear part with each column divided by its squared length,
// exactly as the original shader computed it.
inline Vec3 apply_normal(const Affine& t, Vec3 n) {
    const float sx = t.m[0] * t.m[0] + t.m[4] * t.m[4] + t.m[8] * t.m[8];
    const float sy = t.m[1] * t.m[1] + t.m[5] * t.m[5] + t.m[9] * t.m[9];
    const float sz = t.m[2] * t.m[2] + t.m[6] * t.m[6] + t.m[10] * t.m[10];
    const Vec3 divided{n.x / std::max(sx, 1e-5F), n.y / std::max(sy, 1e-5F), n.z / std::max(sz, 1e-5F)};
    const Vec3 result = normalize(apply_linear(t, divided));
    return result;
}

// A fast sine for repeated kernels: range reduction to [-pi, pi] and a seventh-order
// odd polynomial. Absolute error is below 2e-4 over all finite inputs of moderate size.
inline float fast_sin(float x) {
    constexpr float inverse_tau = 0.15915494309189535F;
    constexpr float tau = 6.283185307179586F;
    const float turns = std::floor(x * inverse_tau + 0.5F);
    const float r = x - turns * tau;
    // Fold into [-pi/2, pi/2] where the polynomial is accurate.
    constexpr float half_pi = 1.5707963267948966F;
    constexpr float pi = 3.141592653589793F;
    float folded = r;
    if (r > half_pi)
        folded = pi - r;
    if (r < -half_pi)
        folded = -pi - r;
    const float x2 = folded * folded;
    const float result = folded * (1.0F + x2 * (-0.16666667F + x2 * (0.0083333310F + x2 * -0.00019840874F)));
    return result;
}
inline float fast_cos(float x) {
    const float result = fast_sin(x + 1.5707963267948966F);
    return result;
}

} // namespace ambient
