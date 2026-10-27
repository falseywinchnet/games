#include "motion.hpp"

#include "raster3d.hpp"

#include <algorithm>
#include <cmath>

namespace ambient {
namespace {

Point3 cruise_position(const Creature& creature, double elapsed) {
    const double angle = elapsed * creature.angular_speed + creature.phase;
    const Point3 result{creature.center.x + creature.radius_horizontal * std::sin(angle),
                        creature.center.y + creature.radius_vertical * std::sin(angle * 2),
                        creature.center.z + creature.radius_horizontal * 0.25 * (std::cos(angle) - 1)};
    return result;
}

} // namespace

std::vector<Creature> creatures_from(const std::vector<ActorRecord>& records) {
    std::vector<Creature> result{};
    result.reserve(records.size());
    for (const ActorRecord& record : records) {
        Creature creature{};
        creature.center = {record.center[0], record.center[1], record.center[2]};
        creature.scale = record.center[3];
        creature.radius_horizontal = record.cruise[0];
        creature.radius_vertical = record.cruise[1];
        creature.angular_speed = record.cruise[2];
        creature.phase = record.cruise[3];
        creature.kind = record.kind == 1 ? CreatureKind::walker : CreatureKind::swimmer;
        result.push_back(creature);
    }
    return result;
}

Pose creature_pose(const Creature& creature, double time) {
    Pose pose{};
    const bool escaped = creature.escape_duration > 0;
    const double since = time - creature.startled_at;
    if (escaped && since < creature.escape_duration) {
        const double f = std::clamp(since / creature.escape_duration, 0.0, 1.0);
        const double ease = 1.0 - std::pow(1.0 - f, 3.0);
        pose.position = {creature.startled_from.x + (creature.escape_to.x - creature.startled_from.x) * ease,
                         creature.startled_from.y + (creature.escape_to.y - creature.startled_from.y) * ease,
                         creature.startled_from.z + (creature.escape_to.z - creature.startled_from.z) * ease};
        pose.heading = std::atan2(-(creature.escape_to.z - creature.startled_from.z),
                                  creature.escape_to.x - creature.startled_from.x);
    } else {
        const double elapsed = escaped ? since - creature.escape_duration : time;
        pose.position = cruise_position(creature, elapsed);
        const double angle = elapsed * creature.angular_speed + creature.phase;
        pose.heading = std::atan2(0.25 * std::sin(angle), std::cos(angle));
    }
    if (creature.kind == CreatureKind::walker)
        pose.heading = 0;
    return pose;
}

std::size_t startle(std::vector<Creature>& creatures, const Projection& projection, ScreenTap tap, double time,
                    const Bounds& bounds) {
    if (!std::isfinite(tap.x) || !std::isfinite(tap.y) || !std::isfinite(time) || !std::isfinite(tap.aspect) ||
        tap.aspect <= 0 || projection.width <= 0 || projection.height <= 0)
        return 0;
    std::size_t count = 0;
    for (Creature& creature : creatures) {
        const Pose pose = creature_pose(creature, time);
        const Vec3 world{static_cast<float>(pose.position.x), static_cast<float>(pose.position.y),
                         static_cast<float>(pose.position.z)};
        const ViewPoint view = to_view(projection, world);
        if (view.depth <= projection.near_plane)
            continue;
        const ScreenPoint screen = to_screen(projection, view);
        const double sx = static_cast<double>(screen.x) / projection.width;
        const double sy = static_cast<double>(screen.y) / projection.height;
        const double dx = (sx - tap.x) * tap.aspect;
        const double dy = sy - tap.y;
        const double separation = std::hypot(dx, dy);
        if (separation > tap.reach)
            continue;
        const double intensity = 1 - separation / tap.reach;
        const double sign = dx >= 0 ? 1.0 : -1.0;
        const bool walker = creature.kind == CreatureKind::walker;
        const double flight = (walker ? 0.5 : 2.5) * (0.5 + intensity);
        Point3 destination{std::clamp(pose.position.x + sign * flight, bounds.min_x, bounds.max_x),
                           pose.position.y, pose.position.z};
        if (!walker) {
            destination.y = std::clamp(pose.position.y + 0.5 * intensity, bounds.min_y, bounds.max_y);
            destination.z -= 0.8 * intensity;
        }
        creature.startled_from = pose.position;
        creature.startled_at = time;
        creature.escape_to = destination;
        creature.escape_duration = 0.6 + 0.5 * (1 - intensity);
        // The new loop starts at the escape point (phase zero puts it at the centre).
        creature.center = destination;
        creature.phase = 0;
        const double room = std::max(0.3, bounds.max_x + 0.5 - std::abs(creature.center.x));
        creature.radius_horizontal = std::min(creature.radius_horizontal, room);
        ++count;
    }
    return count;
}

void prepare_current(const SceneData& scene, std::vector<CurrentRoot>& roots, std::vector<CurrentVertex>& vertices) {
    roots.assign(scene.sway_roots.size(), CurrentRoot{});
    for (std::size_t index = 0; index < roots.size(); ++index) {
        const Vec3 root = scene.sway_roots[index];
        // The shader hashed in single precision; double here keeps the seed stable.
        const double hashed = std::sin(static_cast<double>(root.x) * 12.9898 + static_cast<double>(root.z) * 78.233) *
                              43758.5453;
        const double seed = hashed - std::floor(hashed);
        roots[index].phase = static_cast<float>(seed * 6.2832 + root.x * 0.9);
    }
    const Vec3 current = normalize({1, 0, 0.22F});
    vertices.resize(scene.sway.vertices.size());
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        const SwayVertex& source = scene.sway.vertices[index];
        const Vec3 bend{source.bend[0] / 127.0F, source.bend[1] / 127.0F, source.bend[2] / 127.0F};
        const float compliance = source.compliance * (1.2F / 255.0F);
        const float d = source.distance;
        const float saturation = 1 + 0.06F * d * d;
        const float safe = std::max(d, 0.0001F);
        const float power = std::pow(safe, 0.3F);
        CurrentVertex& v = vertices[index];
        v.compliance = compliance;
        v.drag = compliance * dot(current, bend);
        v.amount_scale = 0.09F * d * d / saturation;
        v.slope_scale = 0.18F * d / (saturation * saturation);
        v.envelope_scale = safe * power;
        v.slope_power = 1.3F * power;
        v.sin_wave = std::sin(1.05F * d);
        v.cos_wave = std::cos(1.05F * d);
        v.sin_ripple = std::sin(1.7F * d);
        v.cos_ripple = std::cos(1.7F * d);
    }
}

void update_current_roots(const std::vector<Vec3>& positions, double time, std::vector<CurrentRoot>& roots) {
    const double global = 0.34 * std::sin(time * 0.031);
    for (std::size_t index = 0; index < roots.size(); ++index) {
        const Vec3 p = positions[index];
        CurrentRoot& root = roots[index];
        const double strength = global + 0.15 * std::sin(time * 0.055 - p.x * 0.34 - p.z * 0.19) +
                                0.03 * std::sin(time * 0.235 + p.x * 1.7 + p.z * 1.1) +
                                0.03 * std::sin(time * 0.155 + p.x * 0.6 - p.z * 2.3);
        const double theta = time * 0.95 + root.phase;
        const double ripple = time * 1.55 + root.phase * 2.3;
        root.strength = static_cast<float>(strength);
        root.sin_theta = static_cast<float>(std::sin(theta));
        root.cos_theta = static_cast<float>(std::cos(theta));
        root.sin_ripple = static_cast<float>(std::sin(ripple));
        root.cos_ripple = static_cast<float>(std::cos(ripple));
    }
}

} // namespace ambient
