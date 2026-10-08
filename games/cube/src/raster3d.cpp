#include "raster3d.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ps_cube {
namespace {

double edge(const Vertex& a, const Vertex& b, double x, double y) {
    const double value = (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
    return value;
}

std::uint8_t channel(double value) {
    const double clamped = std::clamp(value, 0.0, 255.0) + .5;
    return static_cast<std::uint8_t>(clamped);
}

}  // namespace

void Raster3D::resize(int w, int h) {
    width = std::max(1, w);
    height = std::max(1, h);
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    pixels.assign(count * 4, 0);
    ids.assign(count, -1);
    depth.assign(count, std::numeric_limits<float>::infinity());
}

void Raster3D::clear() {
    std::fill(pixels.begin(), pixels.end(), static_cast<std::uint8_t>(0));
    std::fill(ids.begin(), ids.end(), -1);
    std::fill(depth.begin(), depth.end(), std::numeric_limits<float>::infinity());
}

void Raster3D::sample(double u, double v, double out[3]) const {
    if (draft) {
        double px = u * env_width_;
        px -= std::floor(px / env_width_) * env_width_;
        const int sx = std::clamp(static_cast<int>(px), 0, env_width_ - 1);
        const int sy = std::clamp(static_cast<int>(v * env_height_), 0, env_height_ - 1);
        const std::size_t at = (static_cast<std::size_t>(sy) * static_cast<std::size_t>(env_width_) +
                                static_cast<std::size_t>(sx)) * 4;
        out[0] = environment_[at] * .82 + 35;
        out[1] = environment_[at + 1] * .82 + 35;
        out[2] = environment_[at + 2] * .82 + 35;
        return;
    }
    // Longitude wraps around the panorama; latitude clamps at the poles.
    double px = u * env_width_ - .5;
    px -= std::floor(px / env_width_) * env_width_;
    const double py = std::clamp(v * env_height_ - .5, 0.0, static_cast<double>(env_height_ - 1));
    const int sx = static_cast<int>(px);
    const int sy = static_cast<int>(py);
    const int ex = (sx + 1) % env_width_;
    const int ey = std::min(sy + 1, env_height_ - 1);
    const double fx = px - sx;
    const double fy = py - sy;
    const std::size_t row0 = static_cast<std::size_t>(sy) * static_cast<std::size_t>(env_width_);
    const std::size_t row1 = static_cast<std::size_t>(ey) * static_cast<std::size_t>(env_width_);
    for (int c = 0; c < 3; ++c) {
        const std::size_t k = static_cast<std::size_t>(c);
        const double top = environment_[(row0 + static_cast<std::size_t>(sx)) * 4 + k] * (1 - fx) +
                           environment_[(row0 + static_cast<std::size_t>(ex)) * 4 + k] * fx;
        const double bottom = environment_[(row1 + static_cast<std::size_t>(sx)) * 4 + k] * (1 - fx) +
                              environment_[(row1 + static_cast<std::size_t>(ex)) * 4 + k] * fx;
        out[c] = (top * (1 - fy) + bottom * fy) * .82 + 35;
    }
}

void Raster3D::triangle(const Vertex& a, const Vertex& b, const Vertex& c, int id) {
    const double area = edge(a, b, c.x, c.y);
    if (std::fabs(area) < 1e-8 || width <= 0) {
        return;
    }
    const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
    const int x1 = std::min(width - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
    const int y1 = std::min(height - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
    if (x0 > x1 || y0 > y1) {
        return;
    }
    const double inverse = 1 / area;
    const bool overlay = a.z <= overlay_depth * .5;
    const bool reflect = a.env > 0 && !environment_.empty();
    const Vertex* edges[3][2] = {{&b, &c}, {&c, &a}, {&a, &b}};
    const bool same_color = a.color.r == b.color.r && a.color.r == c.color.r && a.color.g == b.color.g &&
                            a.color.g == c.color.g && a.color.b == b.color.b && a.color.b == c.color.b;
    const bool flat = same_color && !overlay && !reflect && a.color.a >= 255 && b.color.a >= 255 &&
                      c.color.a >= 255;
    const std::uint8_t flat_bgra[4] = {channel(a.color.b), channel(a.color.g), channel(a.color.r), 255};
    for (int y = y0; y <= y1; ++y) {
        const double yc = y + .5;
        const std::size_t row_start = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        double lo = x0;
        double hi = x1 + 1;
        bool empty = false;
        // Each edge function is linear in x along the row: visit only where all three
        // can be non-negative, with a pixel of slack, then test exactly.
        for (int e = 0; e < 3; ++e) {
            const Vertex& p = *edges[e][0];
            const Vertex& q = *edges[e][1];
            const double slope = -(q.y - p.y) * inverse;
            const double offset = ((q.x - p.x) * (yc - p.y) + (q.y - p.y) * p.x) * inverse;
            if (std::fabs(slope) < 1e-12) {
                empty = empty || offset < -1e-9;
                continue;
            }
            const double root = -offset / slope - .5;
            if (slope > 0) {
                lo = std::max(lo, std::floor(root) - 1);
            } else {
                hi = std::min(hi, std::ceil(root) + 1);
            }
        }
        if (empty || lo > hi) {
            continue;
        }
        const int xs = std::max(x0, static_cast<int>(lo));
        const int xe = std::min(x1, static_cast<int>(hi));
        // The weights are linear along the row: evaluate once, then step. A hair of
        // tolerance keeps rounding from opening a crack along a shared edge.
        double wa = edge(b, c, xs + .5, yc) * inverse;
        double wb = edge(c, a, xs + .5, yc) * inverse;
        double wc = edge(a, b, xs + .5, yc) * inverse;
        const double step_a = -(c.y - b.y) * inverse;
        const double step_b = -(a.y - c.y) * inverse;
        const double step_c = -(b.y - a.y) * inverse;
        for (int x = xs; x <= xe; ++x, wa += step_a, wb += step_b, wc += step_c) {
            if (wa < -1e-9 || wb < -1e-9 || wc < -1e-9) {
                continue;
            }
            const std::size_t n = row_start + static_cast<std::size_t>(x);
            const double z = wa * a.z + wb * b.z + wc * c.z;
            if (!overlay && z > depth[n]) {
                continue;
            }
            if (flat) {
                // One colour, opaque, no reflection: the common case for stones and lines.
                depth[n] = static_cast<float>(z);
                ids[n] = id;
                std::uint8_t* target = pixels.data() + n * 4;
                target[0] = flat_bgra[0];
                target[1] = flat_bgra[1];
                target[2] = flat_bgra[2];
                target[3] = flat_bgra[3];
                continue;
            }
            double r = wa * a.color.r + wb * b.color.r + wc * c.color.r;
            double g = wa * a.color.g + wb * b.color.g + wc * c.color.g;
            double bl = wa * a.color.b + wb * b.color.b + wc * c.color.b;
            const double alpha = (wa * a.color.a + wb * b.color.a + wc * c.color.a) / 255;
            if (reflect) {
                const double u = wa * a.u + wb * b.u + wc * c.u;
                const double v = wa * a.v + wb * b.v + wc * c.v;
                double mirror[3] = {0, 0, 0};
                sample(u, v, mirror);
                const double k = a.env;
                r = mirror[0] * k + r * (1 - k);
                g = mirror[1] * k + g * (1 - k);
                bl = mirror[2] * k + bl * (1 - k);
            }
            std::uint8_t* out = pixels.data() + n * 4;
            if (overlay) {
                // Straight colour over premultiplied destination.
                const double keep = 1 - alpha;
                out[0] = channel(bl * alpha + out[0] * keep);
                out[1] = channel(g * alpha + out[1] * keep);
                out[2] = channel(r * alpha + out[2] * keep);
                out[3] = channel(255 * alpha + out[3] * keep);
                if (alpha > .5) {
                    ids[n] = id;
                }
                continue;
            }
            depth[n] = static_cast<float>(z);
            ids[n] = id;
            out[0] = channel(bl * alpha);
            out[1] = channel(g * alpha);
            out[2] = channel(r * alpha);
            out[3] = channel(255 * alpha);
        }
    }
}

void Raster3D::line(double ax, double ay, double bx, double by, double thickness, Rgba color, int id,
                    double za, double zb) {
    const double length = std::hypot(bx - ax, by - ay);
    if (length < 1e-6) {
        return;
    }
    const double dx = (by - ay) / length * thickness * .5;
    const double dy = -(bx - ax) / length * thickness * .5;
    const Vertex p{ax + dx, ay + dy, za, color};
    const Vertex q{bx + dx, by + dy, zb, color};
    const Vertex r{bx - dx, by - dy, zb, color};
    const Vertex s{ax - dx, ay - dy, za, color};
    triangle(p, q, r, id);
    triangle(p, r, s, id);
}

void Raster3D::disc(double x, double y, double radius, Rgba color, int id, double z, int segments) {
    const Vertex center{x, y, z, color};
    for (int i = 0; i < segments; ++i) {
        const double a0 = i * 6.283185307179586 / segments;
        const double a1 = (i + 1) * 6.283185307179586 / segments;
        const Vertex p{x + radius * std::cos(a0), y + radius * std::sin(a0), z, color};
        const Vertex q{x + radius * std::cos(a1), y + radius * std::sin(a1), z, color};
        triangle(center, p, q, id);
    }
}

bool Raster3D::load_environment(const std::vector<std::uint8_t>& file) {
    const std::size_t bytes = 1024 * 512 * 4;
    if (file.size() != bytes + 16) {
        return false;
    }
    const std::uint8_t expected[16] = {'G', 'P', 'I', 'X', 1, 0, 0, 0, 0, 4, 0, 0, 0, 2, 0, 0};
    for (std::size_t index = 0; index < 16; ++index) {
        if (file[index] != expected[index]) {
            return false;
        }
    }
    environment_.assign(file.begin() + 16, file.end());
    env_width_ = 1024;
    env_height_ = 512;
    return true;
}

void composite(const Raster3D& raster, std::uint8_t* frame, int frame_width, int frame_height,
               std::size_t row_bytes, bool rgba, double x, double y, double w, double h, double opacity) {
    if (raster.width <= 0 || w <= 0 || h <= 0 || opacity <= 0) {
        return;
    }
    const int left = std::max(0, static_cast<int>(std::floor(x)));
    const int top = std::max(0, static_cast<int>(std::floor(y)));
    const int right = std::min(frame_width, static_cast<int>(std::ceil(x + w)));
    const int bottom = std::min(frame_height, static_cast<int>(std::ceil(y + h)));
    if (left >= right || top >= bottom) {
        return;
    }
    const double sx = raster.width / w;
    const double sy = raster.height / h;
    const int last_x = raster.width - 1;
    const int last_y = raster.height - 1;
    const std::size_t source_row = static_cast<std::size_t>(raster.width) * 4;
    const std::size_t red = rgba ? 0 : 2;
    const std::size_t blue = rgba ? 2 : 0;
    // Column lookups once per call: source columns and an 8-bit weight for the right one.
    const std::size_t columns = static_cast<std::size_t>(right - left);
    std::vector<std::uint32_t> column_left(columns);
    std::vector<std::uint32_t> column_right(columns);
    std::vector<std::uint32_t> column_weight(columns);
    for (std::size_t index = 0; index < columns; ++index) {
        const double fx = std::clamp((left + static_cast<double>(index) + .5 - x) * sx - .5, 0.0,
                                     static_cast<double>(last_x));
        const int x0 = static_cast<int>(fx);
        column_left[index] = static_cast<std::uint32_t>(x0) * 4;
        column_right[index] = static_cast<std::uint32_t>(std::min(x0 + 1, last_x)) * 4;
        column_weight[index] = static_cast<std::uint32_t>((fx - x0) * 256 + .5);
    }
    const std::uint32_t fade = static_cast<std::uint32_t>(std::clamp(opacity, 0.0, 1.0) * 256 + .5);
    // Bilinear in every state: a moving cube is drawn smaller, then smoothly enlarged.
    for (int py = top; py < bottom; ++py) {
        const double fy = std::clamp((py + .5 - y) * sy - .5, 0.0, static_cast<double>(last_y));
        const int y0 = static_cast<int>(fy);
        const int y1 = std::min(y0 + 1, last_y);
        const std::uint32_t wy = static_cast<std::uint32_t>((fy - y0) * 256 + .5);
        const std::uint8_t* row0 = raster.pixels.data() + static_cast<std::size_t>(y0) * source_row;
        const std::uint8_t* row1 = raster.pixels.data() + static_cast<std::size_t>(y1) * source_row;
        std::uint8_t* out = frame + static_cast<std::size_t>(py) * row_bytes + static_cast<std::size_t>(left) * 4;
        for (std::size_t index = 0; index < columns; ++index) {
            const std::uint8_t* a0 = row0 + column_left[index];
            const std::uint8_t* b0 = row0 + column_right[index];
            const std::uint8_t* a1 = row1 + column_left[index];
            const std::uint8_t* b1 = row1 + column_right[index];
            if ((a0[3] | b0[3] | a1[3] | b1[3]) == 0) {
                continue;
            }
            const std::uint32_t wx = column_weight[index];
            const std::uint32_t ix = 256 - wx;
            const std::uint32_t iy = 256 - wy;
            std::uint32_t value[4] = {0, 0, 0, 0};
            for (std::size_t c = 0; c < 4; ++c) {
                const std::uint32_t upper = a0[c] * ix + b0[c] * wx;
                const std::uint32_t lower = a1[c] * ix + b1[c] * wx;
                value[c] = (((upper * iy + lower * wy) >> 16) * fade) >> 8;
            }
            std::uint8_t* pixel = out + index * 4;
            if (value[3] >= 255) {
                // Opaque glass: a straight write.
                pixel[blue] = static_cast<std::uint8_t>(value[0]);
                pixel[1] = static_cast<std::uint8_t>(value[1]);
                pixel[red] = static_cast<std::uint8_t>(value[2]);
                pixel[3] = 255;
                continue;
            }
            // Premultiplied "over"; (x * 257 + 32896) >> 16 divides by 255 with rounding.
            const std::uint32_t keep = 255 - value[3];
            const std::uint32_t old_blue = pixel[blue];
            const std::uint32_t old_green = pixel[1];
            const std::uint32_t old_red = pixel[red];
            const std::uint32_t old_alpha = pixel[3];
            pixel[blue] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255, value[0] + ((old_blue * keep * 257 + 32896) >> 16)));
            pixel[1] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255, value[1] + ((old_green * keep * 257 + 32896) >> 16)));
            pixel[red] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255, value[2] + ((old_red * keep * 257 + 32896) >> 16)));
            pixel[3] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255, value[3] + ((old_alpha * keep * 257 + 32896) >> 16)));
        }
    }
}

}  // namespace ps_cube
