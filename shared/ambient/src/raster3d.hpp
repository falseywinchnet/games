#pragma once
// The ambient engine's camera: a perspective projection onto a pixel grid, points in
// camera space, and the light's orthographic frame for shadow maps.
//
// Triangles are scanned by the shared renderer's camera-space scan (shared/render,
// r3d_scan.hpp): near-plane clipping, 8-bit subpixel fixed-point edges with the
// top-left rule and each row's run solved exactly, so shared edges are covered exactly
// once. What happens at a covered pixel is a small named policy struct (depth only,
// visibility buffer, Gouraud colour, fragment list), chosen at compile time. The
// functions here are that scan, taking this engine's projection.
#include "ambient_math.hpp"
#include "archive.hpp"
#include "r3d_scan.hpp"

#include <cstdint>
#include <vector>

namespace ambient {

// Camera-space position (lateral x, y up, depth along the view), a projected vertex
// (pixel coordinates, y down, and reciprocal depth), what a policy receives at a
// covered pixel, and the culling choice: the renderer's own.
using ViewPoint = render::r3d::ViewPoint;
using ScreenPoint = render::r3d::ScreenPoint;
using Coverage = render::r3d::Coverage;
using Cull = render::r3d::Cull;

// A perspective camera mapped to a pixel grid of `width` x `height`.
struct Projection {
    int width{};
    int height{};
    Vec3 eye{};
    Vec3 right{};
    Vec3 up{};
    Vec3 forward{};
    float focal{};  // pixels per unit of lateral offset at unit depth
    float near_plane{0.1F};
    // The same grid as the renderer's scan takes it.
    [[nodiscard]] render::r3d::Lens lens() const {
        const render::r3d::Lens result{width, height, focal, near_plane};
        return result;
    }
};
Projection make_projection(const CameraSetup& camera, int width, int height);

inline ViewPoint to_view(const Projection& projection, Vec3 world) {
    const Vec3 d = subtract(world, projection.eye);
    const ViewPoint result{dot(d, projection.right), dot(d, projection.up), dot(d, projection.forward)};
    return result;
}
inline ScreenPoint to_screen(const Projection& projection, ViewPoint v) {
    const ScreenPoint result = render::r3d::to_screen(projection.lens(), v);
    return result;
}

// Rasterizes one triangle given in camera space. Back faces are rejected when
// `cull` is Cull::back. Geometry in front of the near plane is clipped.
template <typename Policy>
void rasterize(Policy& policy, const Projection& projection, ViewPoint a, ViewPoint b, ViewPoint c, Cull cull) {
    render::r3d::rasterize(policy, projection.lens(), a, b, c, cull);
}

// Rasterizes a triangle whose vertices are all in front of the near plane and
// already projected: the fast path for meshes that project each vertex once.
template <typename Policy>
void rasterize_projected(Policy& policy, const Projection& projection, ScreenPoint a, ScreenPoint b, ScreenPoint c,
                         Cull cull) {
    render::r3d::rasterize_projected(policy, projection.lens(), a, b, c, cull);
}

// An orthographic light camera for shadow maps: world -> (u, v) in [0, 1] and depth
// along the light, smaller nearer the light.
struct LightFrame {
    Vec3 origin{};
    Vec3 right{};
    Vec3 up{};
    Vec3 toward{};  // direction toward the light
    float half_width{};
    float half_height{};
};

// A depth map seen from the light, `size` x `size` texels. Thin leaves (ribbons) are
// told apart: each texel keeps the nearest caster with its ribbon, and the nearest
// caster of any other ribbon, so a blade is lit as if it cast no shadow on itself
// while every other blade and object still shadows it (the native Stillwater's policy:
// a blade's own shadow read as broad dark bands travelling up it).
struct ShadowMap {
    static constexpr std::uint16_t no_ribbon = 0xFFFFU;
    LightFrame frame{};
    int size{};
    std::vector<float> depth{};           // size * size, row-major: the nearest caster's nearness to the light
    std::vector<std::uint16_t> ribbon{};  // its ribbon, or no_ribbon (fixed scenery, broad leaves)
    std::vector<float> depth_behind{};    // the nearest caster of a different ribbon
    // 3 x 3 percentage closer; a `receiver` ribbon is not shadowed by itself.
    [[nodiscard]] float visibility(Vec3 world, float bias, std::uint16_t receiver = no_ribbon) const;
};

} // namespace ambient
