#pragma once
// r3d's scan for triangles given in camera space: clipped at the near plane, projected,
// and scanned with the same 8-bit subpixel fixed-point edges and top-left rule as the
// screen-space scan in r3d.hpp, so shared edges are covered exactly once. Each covered
// pixel centre goes to a policy chosen at compile time, with its reciprocal depth and
// the barycentric weights of the *original* triangle's corners, perspective-correct
// when the policy asks (textures and tints on walls rushing past a first-person
// camera; affine would swim). The policy owns the buffers and the shading:
//
//   struct Policy {
//       static constexpr bool perspective = true;         // correct the weights for depth
//       bool test(int x, int y, float inverse_depth);     // true to cover the pixel
//       void cover(const Coverage& at, bool front);       // shade it; `front` is the facing
//   };
//
// This is the ambient engine's rasteriser, the core the renderer study chose, with the
// maze's perspective correction as the policy option; the first-person games draw
// through it. Each row's covered run is solved exactly from the edges, so the pixel
// loop does no inside test; a triangle much taller than it is wide (a blade of grass)
// is walked down its columns instead, each column's run solved once, which covers the
// same pixels with the same weights in another order.
#include "target.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace render::r3d {

// Camera space: lateral x, y (up) and depth along the view direction.
struct ViewPoint {
    float x = 0;
    float y = 0;
    float depth = 0;
};

// A projected point: device pixels (y down) and reciprocal depth.
struct ScreenPoint {
    float x = 0;
    float y = 0;
    float inverse_depth = 0;
};

// The pixel grid a view point projects onto: `focal` pixels per unit of lateral offset
// at unit depth, and the near plane in front of which nothing is drawn.
struct Lens {
    int width = 0;
    int height = 0;
    float focal = 1;
    float near_plane = 0.1F;
};

[[nodiscard]] inline ScreenPoint to_screen(const Lens& lens, ViewPoint v) {
    const float inverse = 1.0F / v.depth;
    const ScreenPoint result{static_cast<float>(lens.width) * 0.5F + v.x * lens.focal * inverse,
                             static_cast<float>(lens.height) * 0.5F - v.y * lens.focal * inverse, inverse};
    return result;
}

enum class Cull : std::uint8_t { none, back };

// What a policy receives at a covered pixel centre: the pixel, its reciprocal depth and
// the weights of the original triangle's three corners.
struct Coverage {
    int x = 0;
    int y = 0;
    float inverse_depth = 0;
    float weight0 = 0;
    float weight1 = 0;
    float weight2 = 0;
};

// What a policy that shades by planes receives once per unclipped triangle: each
// original corner's weight at the first pixel centre of the triangle's bounds and its
// change per pixel across and down. The weights are affine in screen space, so any
// attribute interpolated from the corners is a plane too; the policy forms its planes
// here, then at the start of each covered run (`run`) and per pixel (`cover_at`, the
// pixel's offset along the run) it pays one multiply-add per attribute instead of
// re-forming them from three weights. A policy opts in with `planes = true`.
struct WeightPlanes {
    int x = 0;  // the pixel the planes are given at
    int y = 0;
    float at[3] = {};
    float across[3] = {};
    float down[3] = {};
    bool front = false;
};

// A projected corner in the scan's fixed point (1/256 pixel), as the scan rounds it.
// A policy that sets `cache_quantized = true` hands the scan its unclipped triangle's
// corners already rounded (`quantized[3]`, in the triangle's order), so a corner shared
// by several triangles is rounded once.
struct QuantizedPoint {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

// The scan's rounding of a projected coordinate to fixed point.
[[nodiscard]] inline std::int32_t quantize(float value) {
    constexpr float subpixel = 256.0F;
    constexpr float limit = 4.0e6F;  // keeps 8-bit subpixel products inside 64 bits
    const std::int32_t result = static_cast<std::int32_t>(std::lrint(std::clamp(value, -limit, limit) * subpixel));
    return result;
}

namespace detail {

template <typename Policy> constexpr bool cached_quantized() {
    if constexpr (requires { Policy::cache_quantized; }) {
        return Policy::cache_quantized;
    } else {
        return false;
    }
}

template <typename Policy> constexpr bool shades_by_planes() {
    if constexpr (requires { Policy::planes; }) {
        return Policy::planes;
    } else {
        return false;
    }
}

// A vertex of the clipped polygon, and where it sits in the original triangle.
struct ClipVertex {
    ViewPoint view;
    float weight0 = 0;
    float weight1 = 0;
    float weight2 = 0;
};

// The covered run along one line of pixels (a row, or a column), solved exactly from
// the three edge functions: `values` are the biased edge functions at the line's first
// pixel and `steps` their change per pixel along it. An edge rising along the line
// bounds the run at its start, a falling one at its end; every pixel of the run passes
// all three. Returns false when the line holds none.
inline bool solve_run(const std::int64_t* values, const std::int64_t* steps, std::int64_t span_limit,
                      std::int64_t& first, std::int64_t& last) {
    first = 0;
    last = span_limit - 1;
    for (int k = 0; k < 3; ++k) {
        const std::int64_t w = values[k];
        const std::int64_t s_k = steps[k];
        if (s_k > 0) {
            if (w < 0) {
                first = std::max(first, std::min(span_limit, (-w + s_k - 1) / s_k));
            }
        } else if (s_k < 0) {
            if (w < 0) {
                last = -1;
            } else {
                last = std::min(last, w / (-s_k));
            }
        } else if (w < 0) {
            last = -1;
        }
    }
    const bool result = first <= last;
    return result;
}

// What every covered pixel of one triangle is shaded from, fixed for the sweep.
struct SweepShading {
    std::int64_t bias0 = 0;
    std::int64_t bias1 = 0;
    std::int64_t bias2 = 0;
    float inverse_area = 0;
    float iz0 = 0;
    float iz1 = 0;
    float iz2 = 0;
    const ClipVertex* c0 = nullptr;
    const ClipVertex* c1 = nullptr;
    const ClipVertex* c2 = nullptr;
    bool swapped = false;
    bool front = false;
};

// One covered pixel to the policy: its weights from the edge functions, corrected for
// perspective where the policy asks, mapped to the original corners unless `Identity`.
template <typename Policy, bool Identity>
inline void deliver(Policy& policy, int x, int y, float inverse_depth, std::int64_t w0, std::int64_t w1,
                    std::int64_t w2, const SweepShading& k) {
    const float l0 = static_cast<float>(w0 - k.bias0) * k.inverse_area;
    const float l1 = static_cast<float>(w1 - k.bias1) * k.inverse_area;
    const float l2 = static_cast<float>(w2 - k.bias2) * k.inverse_area;
    float p0 = l0;
    float p1 = l1;
    float p2 = l2;
    if constexpr (Policy::perspective) {
        const float inverse = 1.0F / inverse_depth;
        p0 = l0 * k.iz0 * inverse;
        p1 = l1 * k.iz1 * inverse;
        p2 = l2 * k.iz2 * inverse;
    }
    if constexpr (Identity) {
        // Screen order is the original order, or with the last two swapped.
        const Coverage coverage{x, y, inverse_depth, p0, k.swapped ? p2 : p1, k.swapped ? p1 : p2};
        policy.cover(coverage, k.front);
    } else {
        const Coverage coverage{x,
                                y,
                                inverse_depth,
                                p0 * (*k.c0).weight0 + p1 * (*k.c1).weight0 + p2 * (*k.c2).weight0,
                                p0 * (*k.c0).weight1 + p1 * (*k.c1).weight1 + p2 * (*k.c2).weight1,
                                p0 * (*k.c0).weight2 + p1 * (*k.c1).weight2 + p2 * (*k.c2).weight2};
        policy.cover(coverage, k.front);
    }
}

// Scans one projected triangle. `weights` maps each screen vertex back to the original
// triangle's corners. `Identity` promises they are the unit weights (an unclipped
// triangle), so the mapping is skipped per pixel.
template <typename Policy, bool Identity = false>
void sweep(Policy& policy, int width, int height, const ScreenPoint* s, const ClipVertex* weights, bool front) {
    constexpr float subpixel = 256.0F;
    constexpr float limit = 4.0e6F;  // keeps 8-bit subpixel products inside 64 bits
    // Reject from float bounds first, widened past the fixed-point rounding, so a
    // triangle is skipped only when the exact scan would be empty.
    {
        const float min_x = std::min({s[0].x, s[1].x, s[2].x});
        const float max_x = std::max({s[0].x, s[1].x, s[2].x});
        const float min_y = std::min({s[0].y, s[1].y, s[2].y});
        const float max_y = std::max({s[0].y, s[1].y, s[2].y});
        constexpr float slack = 1.0F / 128.0F;
        if (std::floor(max_x - 0.5F + slack) < std::ceil(min_x - 0.5F - slack) ||
            std::floor(max_y - 0.5F + slack) < std::ceil(min_y - 0.5F - slack) || max_x + slack < 0.5F ||
            max_y + slack < 0.5F || min_x - slack > static_cast<float>(width) - 0.5F ||
            min_y - slack > static_cast<float>(height) - 0.5F) {
            return;
        }
    }
    std::int64_t fx[3] = {};
    std::int64_t fy[3] = {};
    if constexpr (Identity && cached_quantized<Policy>()) {
        for (int k = 0; k < 3; ++k) {
            fx[k] = (*policy.quantized[k]).x;
            fy[k] = (*policy.quantized[k]).y;
        }
    } else {
        for (int k = 0; k < 3; ++k) {
            fx[k] = static_cast<std::int64_t>(std::lrint(std::clamp(s[k].x, -limit, limit) * subpixel));
            fy[k] = static_cast<std::int64_t>(std::lrint(std::clamp(s[k].y, -limit, limit) * subpixel));
        }
    }
    // Orient counter-clockwise on screen (y down) so every edge function is non-negative
    // inside; the original orientation is `front`.
    const std::int64_t area = (fx[1] - fx[0]) * (fy[2] - fy[0]) - (fy[1] - fy[0]) * (fx[2] - fx[0]);
    if (area == 0) {
        return;
    }
    int order[3] = {0, 1, 2};
    const bool swapped = area < 0;
    if (swapped) {
        order[1] = 2;
        order[2] = 1;
    }
    const std::int64_t x0 = fx[order[0]];
    const std::int64_t y0 = fy[order[0]];
    const std::int64_t x1 = fx[order[1]];
    const std::int64_t y1 = fy[order[1]];
    const std::int64_t x2 = fx[order[2]];
    const std::int64_t y2 = fy[order[2]];
    const std::int64_t min_x = std::min({x0, x1, x2});
    const std::int64_t max_x = std::max({x0, x1, x2});
    const std::int64_t min_y = std::min({y0, y1, y2});
    const std::int64_t max_y = std::max({y0, y1, y2});
    // Pixel centres at (i + 0.5) * subpixel inside the bounds.
    const int start_x = std::max(0, static_cast<int>((min_x - 128 + 255) >> 8));
    const int end_x = std::min(width - 1, static_cast<int>((max_x - 128) >> 8));
    const int start_y = std::max(0, static_cast<int>((min_y - 128 + 255) >> 8));
    const int end_y = std::min(height - 1, static_cast<int>((max_y - 128) >> 8));
    if (start_x > end_x || start_y > end_y) {
        return;
    }
    // Edge function for edge (a -> b) at p: (b - a) x (p - a) = A (px - ax) + B (py - ay)
    // with A = ay - by and B = bx - ax. With positive area it is >= 0 inside, and (A, B)
    // is the edge's inward normal.
    const std::int64_t a01 = y0 - y1;
    const std::int64_t b01 = x1 - x0;
    const std::int64_t a12 = y1 - y2;
    const std::int64_t b12 = x2 - x1;
    const std::int64_t a20 = y2 - y0;
    const std::int64_t b20 = x0 - x2;
    // Top-left rule (y down): a centre exactly on an edge belongs to the triangle only
    // when the edge is a left edge (inward normal +x) or a top edge (+y).
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
    const SweepShading shading{bias0,
                               bias1,
                               bias2,
                               inverse_area,
                               iz0,
                               iz1,
                               iz2,
                               &weights[order[0]],
                               &weights[order[1]],
                               &weights[order[2]],
                               swapped,
                               front};
    const std::int64_t row_steps[3] = {step_x0, step_x1, step_x2};
    const std::int64_t column_steps[3] = {step_y0, step_y1, step_y2};
    const int rows = end_y - start_y + 1;
    const int columns = end_x - start_x + 1;
    constexpr bool planar = Identity && !Policy::perspective && shades_by_planes<Policy>();
    if constexpr (planar) {
        // The weights' planes, in the original corner order.
        WeightPlanes planes{};
        planes.x = start_x;
        planes.y = start_y;
        planes.front = front;
        const std::int64_t at[3] = {row0 - bias0, row1 - bias1, row2 - bias2};
        for (int k = 0; k < 3; ++k) {
            planes.at[order[k]] = static_cast<float>(at[k]) * inverse_area;
            planes.across[order[k]] = static_cast<float>(row_steps[k]) * inverse_area;
            planes.down[order[k]] = static_cast<float>(column_steps[k]) * inverse_area;
        }
        policy.triangle(planes);
    }
    // A triangle much taller than it is wide is walked down its columns: one run solved
    // per column instead of one per row, most of which would hold no pixel centre. A
    // solve costs about as much as ten pixel steps, and a column's pixel step does one
    // more multiply-add for its depth, which decides the shape where the two meet.
    if (static_cast<std::int64_t>(rows - columns) * 22 > static_cast<std::int64_t>(rows) * columns) {
        const std::int64_t span_limit = rows;
        for (int x = start_x; x <= end_x; ++x) {
            const std::int64_t column_values[3] = {row0, row1, row2};
            std::int64_t first = 0;
            std::int64_t last = 0;
            if (solve_run(column_values, column_steps, span_limit, first, last)) {
                std::int64_t w0 = row0 + step_y0 * first;
                std::int64_t w1 = row1 + step_y1 * first;
                std::int64_t w2 = row2 + step_y2 * first;
                // The depth as the row walk forms it: the row's start, then the column's step.
                const float z_column = z_step_x * static_cast<float>(x - start_x);
                const int y_first = start_y + static_cast<int>(first);
                const int y_end = start_y + static_cast<int>(last);
                if constexpr (planar) {
                    policy.run(x, y_first, true);
                }
                for (int y = y_first; y <= y_end; ++y) {
                    const float z_row = z_at_start + z_step_y * static_cast<float>(y - start_y);
                    const float inverse_depth = z_row + z_column;
                    if (policy.test(x, y, inverse_depth)) {
                        if constexpr (planar) {
                            policy.cover_at(x, y, inverse_depth, static_cast<float>(y - y_first));
                        } else {
                            deliver<Policy, Identity>(policy, x, y, inverse_depth, w0, w1, w2, shading);
                        }
                    }
                    w0 += step_y0;
                    w1 += step_y1;
                    w2 += step_y2;
                }
            }
            row0 += step_x0;
            row1 += step_x1;
            row2 += step_x2;
        }
        return;
    }
    const std::int64_t span_limit = columns;
    for (int y = start_y; y <= end_y; ++y) {
        // The row's covered run; every pixel of it passes all three edges, so the loop
        // tests none.
        const std::int64_t row_values[3] = {row0, row1, row2};
        std::int64_t first = 0;
        std::int64_t last = 0;
        if (solve_run(row_values, row_steps, span_limit, first, last)) {
            std::int64_t w0 = row0 + step_x0 * first;
            std::int64_t w1 = row1 + step_x1 * first;
            std::int64_t w2 = row2 + step_x2 * first;
            const float z_row = z_at_start + z_step_y * static_cast<float>(y - start_y);
            const int x_first = start_x + static_cast<int>(first);
            const int x_end = start_x + static_cast<int>(last);
            if constexpr (planar) {
                policy.run(x_first, y, false);
            }
            for (int x = x_first; x <= x_end; ++x) {
                const float inverse_depth = z_row + z_step_x * static_cast<float>(x - start_x);
                if (policy.test(x, y, inverse_depth)) {
                    if constexpr (planar) {
                        policy.cover_at(x, y, inverse_depth, static_cast<float>(x - x_first));
                    } else {
                        deliver<Policy, Identity>(policy, x, y, inverse_depth, w0, w1, w2, shading);
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

}  // namespace detail

// Scans one triangle given in camera space. Back faces are rejected when `cull` is
// Cull::back. Geometry in front of the near plane is clipped away.
template <typename Policy>
void rasterize(Policy& policy, const Lens& lens, ViewPoint a, ViewPoint b, ViewPoint c, Cull cull) {
    const float near_plane = lens.near_plane;
    const bool in_a = a.depth >= near_plane;
    const bool in_b = b.depth >= near_plane;
    const bool in_c = c.depth >= near_plane;
    if (!in_a && !in_b && !in_c) {
        return;
    }
    detail::ClipVertex polygon[4] = {};
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
                const detail::ClipVertex cut{{current.view.x + (next.view.x - current.view.x) * t,
                                              current.view.y + (next.view.y - current.view.y) * t, near_plane},
                                             current.weight0 + (next.weight0 - current.weight0) * t,
                                             current.weight1 + (next.weight1 - current.weight1) * t,
                                             current.weight2 + (next.weight2 - current.weight2) * t};
                polygon[count] = cut;
                ++count;
            }
        }
    }
    ScreenPoint screen[4] = {};
    for (int k = 0; k < count; ++k) {
        screen[k] = to_screen(lens, polygon[k].view);
    }
    // Facing from the whole projected polygon: counter-clockwise in a y-up frame.
    float signed_area = 0;
    for (int k = 0; k < count; ++k) {
        const ScreenPoint& p = screen[k];
        const ScreenPoint& q = screen[(k + 1) % count];
        signed_area += p.x * q.y - q.x * p.y;
    }
    const bool front = signed_area < 0;
    if (cull == Cull::back && !front) {
        return;
    }
    // Only an untouched triangle carries the unit weights the identity sweep assumes: a
    // triangle clipped down to one original corner and two cuts is three vertices too,
    // and goes through the general mapping.
    if (count == 3 && in_a && in_b && in_c) {
        detail::sweep<Policy, true>(policy, lens.width, lens.height, screen, polygon, front);
        return;
    }
    if (count == 3) {
        detail::sweep(policy, lens.width, lens.height, screen, polygon, front);
        return;
    }
    for (int k = 1; k + 1 < count; ++k) {
        const ScreenPoint fan[3] = {screen[0], screen[k], screen[k + 1]};
        const detail::ClipVertex weights[3] = {polygon[0], polygon[k], polygon[k + 1]};
        detail::sweep(policy, lens.width, lens.height, fan, weights, front);
    }
}

// Scans a triangle whose corners are all in front of the near plane and already
// projected: the fast path for meshes that project each vertex once.
template <typename Policy>
void rasterize_projected(Policy& policy, const Lens& lens, ScreenPoint a, ScreenPoint b, ScreenPoint c, Cull cull) {
    const float signed_area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    const bool front = signed_area < 0;
    if (cull == Cull::back && !front) {
        return;
    }
    const ScreenPoint points[3] = {a, b, c};
    const detail::ClipVertex weights[3] = {{{}, 1, 0, 0}, {{}, 0, 1, 0}, {{}, 0, 0, 1}};
    detail::sweep<Policy, true>(policy, lens.width, lens.height, points, weights, front);
}

}  // namespace render::r3d
