#pragma once
// The ambient engine's triangle rasteriser: perspective projection, near-plane
// clipping, and scan conversion with 8-bit subpixel fixed-point edge functions and
// the top-left fill rule, so shared edges are covered exactly once.
//
// One template, `rasterize`, does the scanning; what happens at a covered pixel is
// a small named policy struct (depth only, visibility buffer, Gouraud colour,
// fragment list). Choosing the policy at compile time keeps one plain loop per case.
#include "ambient_math.hpp"
#include "archive.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ambient {

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
};
Projection make_projection(const CameraSetup& camera, int width, int height);

// Camera-space position: lateral x, y (up) and depth along `forward`.
struct ViewPoint {
    float x{};
    float y{};
    float depth{};
};
inline ViewPoint to_view(const Projection& projection, Vec3 world) {
    const Vec3 d = subtract(world, projection.eye);
    const ViewPoint result{dot(d, projection.right), dot(d, projection.up), dot(d, projection.forward)};
    return result;
}

// A projected vertex: pixel coordinates (y down) and reciprocal depth.
struct ScreenPoint {
    float x{};
    float y{};
    float inverse_depth{};
};
inline ScreenPoint to_screen(const Projection& projection, ViewPoint v) {
    const float inverse = 1.0F / v.depth;
    const ScreenPoint result{static_cast<float>(projection.width) * 0.5F + v.x * projection.focal * inverse,
                             static_cast<float>(projection.height) * 0.5F - v.y * projection.focal * inverse,
                             inverse};
    return result;
}

enum class Cull : std::uint8_t { none, back };

// What a policy receives at each covered pixel centre: the pixel, its reciprocal
// depth, the perspective-correct barycentric weights of the *original* triangle's
// three vertices, and whether the triangle faces the camera (counter-clockwise).
struct Coverage {
    int x{};
    int y{};
    float inverse_depth{};
    float weight0{};
    float weight1{};
    float weight2{};
};

namespace detail {

struct ClipVertex {
    ViewPoint view{};
    float weight0{};
    float weight1{};
    float weight2{};
};

// Scans one projected triangle. `weights` maps each screen vertex back to the
// original triangle's barycentric weights. `Identity` promises they are the unit
// weights (an unclipped triangle), so the mapping is skipped per pixel.
template <typename Policy, bool Identity = false>
void scan(Policy& policy, int width, int height, const ScreenPoint* s, const ClipVertex* weights, bool front) {
    constexpr float subpixel = 256.0F;
    const float limit = 4.0e6F;  // keeps 8-bit subpixel products inside 64 bits
    // Most triangles of a detailed mesh seen small hold no pixel centre at all. Find
    // those from the float bounds first, widened by more than the fixed-point
    // rounding, so a triangle is only skipped when the exact test below would be empty.
    {
        const float min_x = std::min({s[0].x, s[1].x, s[2].x});
        const float max_x = std::max({s[0].x, s[1].x, s[2].x});
        const float min_y = std::min({s[0].y, s[1].y, s[2].y});
        const float max_y = std::max({s[0].y, s[1].y, s[2].y});
        constexpr float slack = 1.0F / 128.0F;
        if (std::floor(max_x - 0.5F + slack) < std::ceil(min_x - 0.5F - slack) ||
            std::floor(max_y - 0.5F + slack) < std::ceil(min_y - 0.5F - slack) || max_x + slack < 0.5F ||
            max_y + slack < 0.5F || min_x - slack > static_cast<float>(width) - 0.5F ||
            min_y - slack > static_cast<float>(height) - 0.5F)
            return;
    }
    std::int64_t fx[3]{};
    std::int64_t fy[3]{};
    for (int k = 0; k < 3; ++k) {
        fx[k] = static_cast<std::int64_t>(std::lrint(std::clamp(s[k].x, -limit, limit) * subpixel));
        fy[k] = static_cast<std::int64_t>(std::lrint(std::clamp(s[k].y, -limit, limit) * subpixel));
    }
    // Orient counter-clockwise on screen (y down) so every edge function is
    // non-negative inside; remember the original orientation as `front`.
    const std::int64_t area = (fx[1] - fx[0]) * (fy[2] - fy[0]) - (fy[1] - fy[0]) * (fx[2] - fx[0]);
    if (area == 0)
        return;
    int order[3] = {0, 1, 2};
    const bool swapped = area < 0;
    if (swapped) {
        order[1] = 2;
        order[2] = 1;
    }
    std::int64_t x0 = fx[order[0]];
    std::int64_t y0 = fy[order[0]];
    std::int64_t x1 = fx[order[1]];
    std::int64_t y1 = fy[order[1]];
    std::int64_t x2 = fx[order[2]];
    std::int64_t y2 = fy[order[2]];
    const std::int64_t min_x = std::min({x0, x1, x2});
    const std::int64_t max_x = std::max({x0, x1, x2});
    const std::int64_t min_y = std::min({y0, y1, y2});
    const std::int64_t max_y = std::max({y0, y1, y2});
    // Pixel centres at (i + 0.5) * subpixel inside the bounds.
    const int start_x = std::max(0, static_cast<int>((min_x - 128 + 255) >> 8));
    const int end_x = std::min(width - 1, static_cast<int>((max_x - 128) >> 8));
    const int start_y = std::max(0, static_cast<int>((min_y - 128 + 255) >> 8));
    const int end_y = std::min(height - 1, static_cast<int>((max_y - 128) >> 8));
    if (start_x > end_x || start_y > end_y)
        return;
    // Edge function for edge (a -> b) at p: (b - a) x (p - a) = A (px - ax) + B (py - ay)
    // with A = ay - by and B = bx - ax. With positive area it is >= 0 inside, and
    // (A, B) is the edge's inward normal.
    const std::int64_t a01 = y0 - y1;
    const std::int64_t b01 = x1 - x0;
    const std::int64_t a12 = y1 - y2;
    const std::int64_t b12 = x2 - x1;
    const std::int64_t a20 = y2 - y0;
    const std::int64_t b20 = x0 - x2;
    // Top-left rule (y down): a centre exactly on an edge belongs to the triangle
    // only when the edge is a left edge (inward normal +x) or a top edge (+y).
    const std::int64_t bias0 = (a12 > 0 || (a12 == 0 && b12 > 0)) ? 0 : -1;
    const std::int64_t bias1 = (a20 > 0 || (a20 == 0 && b20 > 0)) ? 0 : -1;
    const std::int64_t bias2 = (a01 > 0 || (a01 == 0 && b01 > 0)) ? 0 : -1;
    const std::int64_t px = static_cast<std::int64_t>(start_x) * 256 + 128;
    const std::int64_t py = static_cast<std::int64_t>(start_y) * 256 + 128;
    // w0 is opposite vertex 0 (edge 1 -> 2), w1 opposite vertex 1, w2 opposite vertex 2.
    std::int64_t row0 = a12 * (px - x1) + b12 * (py - y1) + bias0;
    std::int64_t row1 = a20 * (px - x2) + b20 * (py - y2) + bias1;
    std::int64_t row2 = a01 * (px - x0) + b01 * (py - y0) + bias2;
    const std::int64_t step_x0 = a12 * 256;
    const std::int64_t step_x1 = a20 * 256;
    const std::int64_t step_x2 = a01 * 256;
    const std::int64_t step_y0 = b12 * 256;
    const std::int64_t step_y1 = b20 * 256;
    const std::int64_t step_y2 = b01 * 256;
    const float inverse_area = 1.0F / static_cast<float>(std::llabs(area));
    const float iz0 = s[order[0]].inverse_depth;
    const float iz1 = s[order[1]].inverse_depth;
    const float iz2 = s[order[2]].inverse_depth;
    // Reciprocal depth is affine in screen space: a plane, stepped per pixel and
    // re-anchored per row so rounding cannot accumulate along long spans.
    const float z_at_start = (static_cast<float>(row0 - bias0) * iz0 + static_cast<float>(row1 - bias1) * iz1 +
                              static_cast<float>(row2 - bias2) * iz2) * inverse_area;
    const float z_step_x = (static_cast<float>(step_x0) * iz0 + static_cast<float>(step_x1) * iz1 +
                            static_cast<float>(step_x2) * iz2) * inverse_area;
    const float z_step_y = (static_cast<float>(step_y0) * iz0 + static_cast<float>(step_y1) * iz1 +
                            static_cast<float>(step_y2) * iz2) * inverse_area;
    const ClipVertex& c0 = weights[order[0]];
    const ClipVertex& c1 = weights[order[1]];
    const ClipVertex& c2 = weights[order[2]];
    const std::int64_t row_steps[3] = {step_x0, step_x1, step_x2};
    const std::int64_t span_limit = static_cast<std::int64_t>(end_x - start_x) + 1;
    for (int y = start_y; y <= end_y; ++y) {
        // The row's covered run, solved from the three edge functions: an edge rising
        // along x bounds the run on the left, a falling one on the right. Exact integer
        // arithmetic, so it covers precisely the pixels the per-pixel test would; long
        // thin triangles (grass) no longer pay for the empty part of their bounds.
        const std::int64_t row_values[3] = {row0, row1, row2};
        std::int64_t first = 0;
        std::int64_t last = span_limit - 1;
        for (int k = 0; k < 3; ++k) {
            const std::int64_t w = row_values[k];
            const std::int64_t s = row_steps[k];
            if (s > 0) {
                if (w < 0)
                    first = std::max(first, std::min(span_limit, (-w + s - 1) / s));
            } else if (s < 0) {
                if (w < 0)
                    last = -1;
                else
                    last = std::min(last, w / (-s));
            } else if (w < 0) {
                last = -1;
            }
        }
        if (first <= last) {
            std::int64_t w0 = row0 + step_x0 * first;
            std::int64_t w1 = row1 + step_x1 * first;
            std::int64_t w2 = row2 + step_x2 * first;
            const float z_row = z_at_start + z_step_y * static_cast<float>(y - start_y);
            const int x_end = start_x + static_cast<int>(last);
            for (int x = start_x + static_cast<int>(first); x <= x_end; ++x) {
                if ((w0 | w1 | w2) >= 0) {
                    const float inverse_depth = z_row + z_step_x * static_cast<float>(x - start_x);
                    if (policy.test(x, y, inverse_depth)) {
                        // Screen-space weights, perspective-corrected where the policy asks.
                        const float l0 = static_cast<float>(w0 - bias0) * inverse_area;
                        const float l1 = static_cast<float>(w1 - bias1) * inverse_area;
                        const float l2 = static_cast<float>(w2 - bias2) * inverse_area;
                        float p0 = l0;
                        float p1 = l1;
                        float p2 = l2;
                        if constexpr (Policy::perspective) {
                            const float inverse = 1.0F / inverse_depth;
                            p0 = l0 * iz0 * inverse;
                            p1 = l1 * iz1 * inverse;
                            p2 = l2 * iz2 * inverse;
                        }
                        if constexpr (Identity) {
                            // Screen order is the original order, or with the last two swapped.
                            const Coverage coverage{x, y, inverse_depth, p0, swapped ? p2 : p1, swapped ? p1 : p2};
                            policy.cover(coverage, front);
                        } else {
                            const Coverage coverage{x,
                                                    y,
                                                    inverse_depth,
                                                    p0 * c0.weight0 + p1 * c1.weight0 + p2 * c2.weight0,
                                                    p0 * c0.weight1 + p1 * c1.weight1 + p2 * c2.weight1,
                                                    p0 * c0.weight2 + p1 * c1.weight2 + p2 * c2.weight2};
                            policy.cover(coverage, front);
                        }
                    }
                }
                w0 += step_x0;
                w1 += step_x1;
                w2 += step_x2;
            }
        }
        row0 += step_y0;
        row1 += step_y1;
        row2 += step_y2;
    }
}

} // namespace detail

// Rasterizes one triangle given in camera space. Back faces are rejected when
// `cull` is Cull::back. Geometry in front of the near plane is clipped.
template <typename Policy>
void rasterize(Policy& policy, const Projection& projection, ViewPoint a, ViewPoint b, ViewPoint c, Cull cull) {
    const float near_plane = projection.near_plane;
    const bool in_a = a.depth >= near_plane;
    const bool in_b = b.depth >= near_plane;
    const bool in_c = c.depth >= near_plane;
    if (!in_a && !in_b && !in_c)
        return;
    detail::ClipVertex polygon[4]{};
    int count = 0;
    if (in_a && in_b && in_c) {
        polygon[0] = {a, 1, 0, 0};
        polygon[1] = {b, 0, 1, 0};
        polygon[2] = {c, 0, 0, 1};
        count = 3;
    } else {
        const detail::ClipVertex input[3] = {{a, 1, 0, 0}, {b, 0, 1, 0}, {c, 0, 0, 1}};
        for (int k = 0; k < 3; ++k) {
            const detail::ClipVertex& current = input[k];
            const detail::ClipVertex& next = input[(k + 1) % 3];
            const bool current_in = current.view.depth >= near_plane;
            const bool next_in = next.view.depth >= near_plane;
            if (current_in) {
                polygon[count] = current;
                ++count;
            }
            if (current_in != next_in) {
                const float t = (near_plane - current.view.depth) / (next.view.depth - current.view.depth);
                const detail::ClipVertex cut{
                    {mix(current.view.x, next.view.x, t), mix(current.view.y, next.view.y, t), near_plane},
                    mix(current.weight0, next.weight0, t),
                    mix(current.weight1, next.weight1, t),
                    mix(current.weight2, next.weight2, t)};
                polygon[count] = cut;
                ++count;
            }
        }
    }
    ScreenPoint screen[4]{};
    for (int k = 0; k < count; ++k)
        screen[k] = to_screen(projection, polygon[k].view);
    // Facing from the whole (projected) polygon: counter-clockwise in a y-up frame.
    float signed_area = 0;
    for (int k = 0; k < count; ++k) {
        const ScreenPoint& p = screen[k];
        const ScreenPoint& q = screen[(k + 1) % count];
        signed_area += p.x * q.y - q.x * p.y;
    }
    const bool front = signed_area < 0;
    if (cull == Cull::back && !front)
        return;
    if (count == 3) {
        detail::scan<Policy, true>(policy, projection.width, projection.height, screen, polygon, front);
        return;
    }
    for (int k = 1; k + 1 < count; ++k) {
        const ScreenPoint fan[3] = {screen[0], screen[k], screen[k + 1]};
        const detail::ClipVertex weights[3] = {polygon[0], polygon[k], polygon[k + 1]};
        detail::scan(policy, projection.width, projection.height, fan, weights, front);
    }
}

// Rasterizes a triangle whose vertices are all in front of the near plane and
// already projected: the fast path for meshes that project each vertex once.
template <typename Policy>
void rasterize_projected(Policy& policy, const Projection& projection, ScreenPoint a, ScreenPoint b, ScreenPoint c,
                         Cull cull) {
    const float signed_area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    const bool front = signed_area < 0;
    if (cull == Cull::back && !front)
        return;
    const ScreenPoint points[3] = {a, b, c};
    const detail::ClipVertex weights[3] = {{{}, 1, 0, 0}, {{}, 0, 1, 0}, {{}, 0, 0, 1}};
    detail::scan<Policy, true>(policy, projection.width, projection.height, points, weights, front);
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

// A depth map seen from the light, `size` x `size` texels.
struct ShadowMap {
    LightFrame frame{};
    int size{};
    std::vector<float> depth{};  // size * size, row-major; distance below the light plane
    [[nodiscard]] float visibility(Vec3 world, float bias) const;  // 3 x 3 percentage closer
};

} // namespace ambient
