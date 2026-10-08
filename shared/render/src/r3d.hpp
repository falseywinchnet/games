#pragma once
// r3d: the suite's triangle rasteriser. Triangles arrive in device pixels with a depth
// (smaller is nearer) and are scanned with 8-bit subpixel fixed-point edge functions and
// the top-left fill rule, so shared edges are covered exactly once. Each row's covered
// run is solved exactly from the edges, so the pixel loop does no inside test: it steps
// depth, tests it before any shading, and hands the pixel to a shader chosen at compile
// time. A scissor rectangle bounds every write; a triangle outside it costs its bounds
// test and nothing else.
//
// Work is skipped, not sped up: callers draw near things first so depth rejects what
// they hide before it is shaded, and repair only the rectangle that changed.
#include "target.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace render::r3d {

// A projected vertex: device pixels, and a depth that is affine in screen space
// (camera depth for an orthographic or mildly perspective view, -1/w for a steep one).
struct Corner {
    float x = 0;
    float y = 0;
    float z = 0;
};

// Per-pixel state kept beside a target, the same size: depth, and triangle ids when a
// game picks on the picture.
class Buffers {
public:
    void resize(int width, int height, bool ids);
    // Back to empty inside `area`: depth to the far plane, ids to -1.
    void clear(Rect area);
    [[nodiscard]] int width() const {
        return width_;
    }
    [[nodiscard]] int height() const {
        return height_;
    }
    [[nodiscard]] bool has_ids() const {
        return !ids_.empty();
    }
    // -1 where nothing claimed the pixel, or outside.
    [[nodiscard]] int id_at(int x, int y) const {
        if (ids_.empty() || x < 0 || y < 0 || x >= width_ || y >= height_) {
            return -1;
        }
        return ids_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)];
    }
    float* depth_row(int y) {
        return depth_.data() + static_cast<std::ptrdiff_t>(y) * width_;
    }
    std::int32_t* id_row(int y) {
        return ids_.empty() ? nullptr : ids_.data() + static_cast<std::ptrdiff_t>(y) * width_;
    }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<float> depth_;
    std::vector<std::int32_t> ids_;
};

// One frame's drawing: where pixels go, the buffers beside them, and the scissor.
struct Pass {
    Target target;
    Buffers* buffers = nullptr;
    Rect clip;
};

// An equirectangular picture of the surroundings for mirrored surfaces, prepared once:
// in the target's byte order, with the darkening and lift the surface wants baked in.
class Panorama {
public:
    // GPIX version 1 (16-byte header, RGBA rows). False leaves it empty.
    bool load_gpix(std::span<const std::uint8_t> file, Order order, float gain, float lift);
    [[nodiscard]] bool empty() const {
        return texels_.empty();
    }
    [[nodiscard]] Order order() const {
        return order_;
    }
    // u wraps around, v clamps at the poles; both 0..1. Bilinear, or nearest.
    [[nodiscard]] std::uint32_t bilinear(float u, float v) const;
    [[nodiscard]] std::uint32_t nearest(float u, float v) const;

private:
    std::vector<std::uint32_t> texels_;
    int width_ = 0;
    int height_ = 0;
    int wrap_mask_ = 0;  // width - 1 when the width is a power of two
    Order order_ = Order::bgra;
};

// --- Shaders. Each names its attribute count N, whether it tests and writes depth, and
// shade(pixel, attributes), which returns true when it claims the pixel (depth and id).

// One opaque colour.
struct Flat {
    static constexpr int N = 0;
    static constexpr bool depth = true;
    std::uint32_t color = 0;
    bool shade(std::uint32_t* pixel, const float*) const {
        *pixel = color;
        return true;
    }
};

// Opaque colour interpolated from the corners: attributes r, g, b (0..255).
struct Smooth {
    static constexpr int N = 3;
    static constexpr bool depth = true;
    Order order = Order::bgra;
    bool shade(std::uint32_t* pixel, const float* a) const {
        *pixel = pack(order, a[0], a[1], a[2], 255);
        return true;
    }
};

// A mirror over a tint: attributes u, v into the panorama; `keep` of the tint (0..256)
// shows through, premultiplied in `tint_part` as two 8-bit lanes.
template <bool Nearest>
struct Mirror {
    static constexpr int N = 2;
    static constexpr bool depth = true;
    const Panorama* panorama = nullptr;
    std::uint32_t tint_rb = 0;  // tint * keep / 256, red-blue lanes (0x00ff00ff)
    std::uint32_t tint_g = 0;   // and green, in the low lane
    std::uint32_t mirror = 256; // share of the reflection, 0..256
    bool shade(std::uint32_t* pixel, const float* a) const {
        const std::uint32_t seen = Nearest ? (*panorama).nearest(a[0], a[1]) : (*panorama).bilinear(a[0], a[1]);
        const std::uint32_t rb = ((((seen & 0x00ff00ffU) * mirror) >> 8) & 0x00ff00ffU) + tint_rb;
        const std::uint32_t g = ((((seen >> 8) & 0xffU) * mirror) >> 8) + tint_g;
        *pixel = rb | (g << 8) | 0xff000000U;
        return true;
    }
    static Mirror make(const Panorama& panorama, Color tint, float share);
};

// A straight-alpha colour laid over what is there, ignoring depth: strokes on top.
// It claims the pixel's id when mostly opaque.
struct Over {
    static constexpr int N = 0;
    static constexpr bool depth = false;
    std::uint32_t premultiplied = 0;  // in the target's order
    std::uint32_t keep = 0;           // 255 - alpha
    bool claims = false;
    bool shade(std::uint32_t* pixel, const float*) const;
    static Over make(Order order, Color color);
};

namespace detail {

constexpr float far_depth = std::numeric_limits<float>::infinity();

// A value given at three corners as a plane over the screen: its value at the first
// pixel centre scanned, and its step per pixel along x and y.
struct Plane {
    float dx1 = 0;
    float dy1 = 0;
    float dx2 = 0;
    float dy2 = 0;
    float inverse = 0;  // 1 / the corners' cross product
    float offset_x = 0;  // from corner 0 to the first pixel centre
    float offset_y = 0;
    void solve(float f0, float f1, float f2, float& at, float& per_x, float& per_y) const {
        per_x = ((f1 - f0) * dy2 - (f2 - f0) * dy1) * inverse;
        per_y = ((f2 - f0) * dx1 - (f1 - f0) * dx2) * inverse;
        at = f0 + per_x * offset_x + per_y * offset_y;
    }
};

// Scans one triangle into the pass. `values[k][i]` is attribute i at corner k.
template <typename Shader>
void scan(Pass& pass, const Corner* c, const float (*values)[Shader::N > 0 ? Shader::N : 1], const Shader& shader,
          int id) {
    const Rect clip = pass.clip.intersected(pass.target.bounds());
    if (clip.empty()) {
        return;
    }
    // Reject from float bounds first, widened past the fixed-point rounding, so a
    // triangle is skipped only when the exact scan would be empty.
    {
        constexpr float slack = 1.0F / 128.0F;
        const float min_x = std::min({c[0].x, c[1].x, c[2].x});
        const float max_x = std::max({c[0].x, c[1].x, c[2].x});
        const float min_y = std::min({c[0].y, c[1].y, c[2].y});
        const float max_y = std::max({c[0].y, c[1].y, c[2].y});
        if (!(max_x + slack >= static_cast<float>(clip.x0) + .5F && min_x - slack <= static_cast<float>(clip.x1) - .5F &&
              max_y + slack >= static_cast<float>(clip.y0) + .5F && min_y - slack <= static_cast<float>(clip.y1) - .5F)) {
            return;
        }
    }
    constexpr float limit = 4.0e6F;
    std::int64_t fx[3];
    std::int64_t fy[3];
    for (int k = 0; k < 3; ++k) {
        fx[k] = static_cast<std::int64_t>(std::lrint(std::clamp(c[k].x, -limit, limit) * 256.0F));
        fy[k] = static_cast<std::int64_t>(std::lrint(std::clamp(c[k].y, -limit, limit) * 256.0F));
    }
    const std::int64_t area = (fx[1] - fx[0]) * (fy[2] - fy[0]) - (fy[1] - fy[0]) * (fx[2] - fx[0]);
    if (area == 0) {
        return;
    }
    // Wind so each edge function is non-negative inside.
    int o1 = 1;
    int o2 = 2;
    if (area < 0) {
        o1 = 2;
        o2 = 1;
    }
    const std::int64_t x0 = fx[0];
    const std::int64_t y0 = fy[0];
    const std::int64_t x1 = fx[o1];
    const std::int64_t y1 = fy[o1];
    const std::int64_t x2 = fx[o2];
    const std::int64_t y2 = fy[o2];
    const int start_x = std::max(clip.x0, static_cast<int>((std::min({x0, x1, x2}) - 128 + 255) >> 8));
    const int end_x = std::min(clip.x1 - 1, static_cast<int>((std::max({x0, x1, x2}) - 128) >> 8));
    const int start_y = std::max(clip.y0, static_cast<int>((std::min({y0, y1, y2}) - 128 + 255) >> 8));
    const int end_y = std::min(clip.y1 - 1, static_cast<int>((std::max({y0, y1, y2}) - 128) >> 8));
    if (start_x > end_x || start_y > end_y) {
        return;
    }
    // Edge (a -> b) at p: A (px - ax) + B (py - ay), A = ay - by, B = bx - ax. Top-left
    // rule (y down): a centre exactly on an edge belongs only to a left or top edge.
    const std::int64_t a[3] = {y1 - y2, y2 - y0, y0 - y1};
    const std::int64_t b[3] = {x2 - x1, x0 - x2, x1 - x0};
    const std::int64_t ox[3] = {x1, x2, x0};
    const std::int64_t oy[3] = {y1, y2, y0};
    const std::int64_t px = static_cast<std::int64_t>(start_x) * 256 + 128;
    const std::int64_t py = static_cast<std::int64_t>(start_y) * 256 + 128;
    std::int64_t row[3];
    std::int64_t step_x[3];
    std::int64_t step_y[3];
    for (int k = 0; k < 3; ++k) {
        const std::int64_t bias = (a[k] > 0 || (a[k] == 0 && b[k] > 0)) ? 0 : -1;
        row[k] = a[k] * (px - ox[k]) + b[k] * (py - oy[k]) + bias;
        step_x[k] = a[k] * 256;
        step_y[k] = b[k] * 256;
    }
    // Depth and attributes are planes over the float corners: value at the first pixel
    // centre of each row, then a step per pixel.
    const float dx1 = c[1].x - c[0].x;
    const float dy1 = c[1].y - c[0].y;
    const float dx2 = c[2].x - c[0].x;
    const float dy2 = c[2].y - c[0].y;
    const float det = dx1 * dy2 - dx2 * dy1;
    if (det == 0) {
        return;
    }
    const float inverse = 1.0F / det;
    const Plane setup{dx1, dy1, dx2, dy2, inverse, static_cast<float>(start_x) + .5F - c[0].x,
                      static_cast<float>(start_y) + .5F - c[0].y};
    float z_at = 0;
    float z_x = 0;
    float z_y = 0;
    setup.solve(c[0].z, c[1].z, c[2].z, z_at, z_x, z_y);
    constexpr int n = Shader::N > 0 ? Shader::N : 1;
    float v_at[n] = {};
    float v_x[n] = {};
    float v_y[n] = {};
    if constexpr (Shader::N > 0) {
        for (int i = 0; i < Shader::N; ++i) {
            setup.solve(values[0][i], values[1][i], values[2][i], v_at[i], v_x[i], v_y[i]);
        }
    }
    const std::int64_t span = static_cast<std::int64_t>(end_x - start_x) + 1;
    Buffers* buffers = pass.buffers;
    for (int y = start_y; y <= end_y; ++y) {
        // The covered run, exact: an edge rising along x bounds it on the left, a
        // falling one on the right.
        std::int64_t first = 0;
        std::int64_t last = span - 1;
        for (int k = 0; k < 3; ++k) {
            const std::int64_t w = row[k];
            const std::int64_t s = step_x[k];
            if (s > 0) {
                if (w < 0) {
                    first = std::max(first, (-w + s - 1) / s);
                }
            } else if (s < 0) {
                last = w < 0 ? -1 : std::min(last, w / (-s));
            } else if (w < 0) {
                last = -1;
            }
        }
        const float dy = static_cast<float>(y - start_y);
        if (first <= last) {
            const float offset = static_cast<float>(first);
            float z = z_at + z_y * dy + z_x * offset;
            float v[n];
            if constexpr (Shader::N > 0) {
                for (int i = 0; i < Shader::N; ++i) {
                    v[i] = v_at[i] + v_y[i] * dy + v_x[i] * offset;
                }
            }
            const int xs = start_x + static_cast<int>(first);
            const int xe = start_x + static_cast<int>(last);
            std::uint32_t* out = pass.target.row(y) + xs;
            float* depth = buffers != nullptr ? (*buffers).depth_row(y) + xs : nullptr;
            std::int32_t* ids = buffers != nullptr ? (*buffers).id_row(y) : nullptr;
            for (int x = xs; x <= xe; ++x, ++out) {
                bool visible = true;
                if constexpr (Shader::depth) {
                    visible = depth == nullptr || z <= *depth;
                }
                if (visible && shader.shade(out, v)) {
                    if constexpr (Shader::depth) {
                        if (depth != nullptr) {
                            *depth = z;
                        }
                    }
                    if (ids != nullptr) {
                        ids[x] = id;
                    }
                }
                z += z_x;
                if constexpr (Shader::depth) {
                    if (depth != nullptr) {
                        ++depth;
                    }
                }
                if constexpr (Shader::N > 0) {
                    for (int i = 0; i < Shader::N; ++i) {
                        v[i] += v_x[i];
                    }
                }
            }
        }
        for (int k = 0; k < 3; ++k) {
            row[k] += step_y[k];
        }
    }
}

}  // namespace detail

// One triangle with no attributes (Flat, Over).
template <typename Shader>
void triangle(Pass& pass, const Corner& a, const Corner& b, const Corner& c, const Shader& shader, int id = -1) {
    static_assert(Shader::N == 0);
    const Corner corners[3] = {a, b, c};
    const float none[3][1] = {{0}, {0}, {0}};
    detail::scan(pass, corners, none, shader, id);
}

// One triangle with N attributes per corner.
template <typename Shader>
void triangle(Pass& pass, const Corner& a, const Corner& b, const Corner& c, const float (&va)[Shader::N],
              const float (&vb)[Shader::N], const float (&vc)[Shader::N], const Shader& shader, int id = -1) {
    const Corner corners[3] = {a, b, c};
    float values[3][Shader::N];
    for (int i = 0; i < Shader::N; ++i) {
        values[0][i] = va[i];
        values[1][i] = vb[i];
        values[2][i] = vc[i];
    }
    detail::scan(pass, corners, values, shader, id);
}

}  // namespace render::r3d
