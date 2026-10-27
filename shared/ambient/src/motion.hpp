#pragma once
// Creature routes and the current that sways foliage, as analytic functions of time.
// Nothing here integrates state per frame: a paused scene resumes exactly, and the
// pose at any time can be evaluated directly (for drawing, picking or tests).
//
// Routes follow Stillwater's: a slow trigonometric loop around a centre, and after
// a startle a cubic ease-out escape, then a fresh loop from the escape point.
#include "ambient_math.hpp"
#include "archive.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ambient {

struct Point3 {
    double x{};
    double y{};
    double z{};
};

enum class CreatureKind : std::uint8_t { swimmer, walker };

struct Creature {
    Point3 center{};
    double scale{1};
    double radius_horizontal{};
    double radius_vertical{};
    double angular_speed{};
    double phase{};
    CreatureKind kind{CreatureKind::swimmer};
    // Startle state; escape_duration is zero until the first startle.
    Point3 startled_from{};
    double startled_at{};
    Point3 escape_to{};
    double escape_duration{};
};

struct Pose {
    Point3 position{};
    double heading{};  // rotation about the vertical axis, radians
};

std::vector<Creature> creatures_from(const std::vector<ActorRecord>& records);
[[nodiscard]] Pose creature_pose(const Creature& creature, double time);

// Limits within which startled creatures stay.
struct Bounds {
    double min_x{-9};
    double max_x{9};
    double min_y{0.8};
    double max_y{7.5};
};

// A tap at normalized screen coordinates (0..1, y down) startles creatures whose
// projected position lies within `reach` (fraction of the view height). Returns the
// number startled; their routes change from `time` on.
struct ScreenTap {
    double x{};
    double y{};
    double aspect{1};  // width / height
    double reach{0.2};
};
struct Projection;
std::size_t startle(std::vector<Creature>& creatures, const Projection& projection, ScreenTap tap, double time,
                    const Bounds& bounds);

// The Riverscape current: displacement of a foliage vertex along its bend
// direction, and the slope used to tilt its normal. Per-root terms are computed
// once per update; per-vertex constants once per scene.
struct CurrentRoot {
    float phase{};     // static: seeded from the root position
    float strength{};  // this update
    float sin_theta{};
    float cos_theta{};
    float sin_ripple{};
    float cos_ripple{};
};
struct CurrentVertex {
    float drag{};           // compliance * alignment of the bend with the current
    float compliance{};
    float amount_scale{};   // 0.09 d^2 / (1 + 0.06 d^2)
    float slope_scale{};    // 0.18 d / (1 + 0.06 d^2)^2
    float envelope_scale{}; // d^1.3
    float slope_power{};    // 1.3 d^0.3
    float sin_wave{};       // sin(1.05 d), cos(1.05 d)
    float cos_wave{};
    float sin_ripple{};     // sin(1.7 d), cos(1.7 d)
    float cos_ripple{};
};
void prepare_current(const SceneData& scene, std::vector<CurrentRoot>& roots, std::vector<CurrentVertex>& vertices);
void update_current_roots(const std::vector<Vec3>& positions, double time, std::vector<CurrentRoot>& roots);
// The largest displacement the current can give a vertex, in world units: a bound
// over every time, from |strength| <= 0.55 and |shape| <= 1.3.
inline float current_reach(const CurrentVertex& v) {
    constexpr float strength = 0.55F;
    const float drift = std::abs(v.drag) * strength * v.amount_scale;
    const float wave = v.compliance * (0.012F + 0.02F * strength) * v.envelope_scale * 1.3F;
    const float result = drift + wave;
    return result;
}
// Displacement along the bend direction (x) and normal slope (y) for one vertex.
struct Sway {
    float amount{};
    float slope{};
};
inline Sway current_sway(const CurrentRoot& root, const CurrentVertex& v) {
    // sin(A - B) and cos(A - B) from the per-root angle A and per-vertex offset B.
    const float sin_theta = root.sin_theta * v.cos_wave - root.cos_theta * v.sin_wave;
    const float cos_theta = root.cos_theta * v.cos_wave + root.sin_theta * v.sin_wave;
    const float sin_ripple = root.sin_ripple * v.cos_ripple - root.cos_ripple * v.sin_ripple;
    const float cos_ripple = root.cos_ripple * v.cos_ripple + root.sin_ripple * v.sin_ripple;
    const float drag = v.drag * root.strength;
    const float gain = v.compliance * (0.012F + 0.02F * root.strength);
    const float envelope = gain * v.envelope_scale;
    const float envelope_slope = gain * v.slope_power;
    const float shape = sin_theta + 0.3F * sin_ripple;
    const Sway result{drag * v.amount_scale + envelope * shape,
                      drag * v.slope_scale + envelope_slope * shape -
                          envelope * (1.05F * cos_theta + 0.51F * cos_ripple)};
    return result;
}

} // namespace ambient
