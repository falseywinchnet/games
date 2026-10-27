#include "r2d.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace render::r2d {
namespace {

// Two 8-bit lanes (0x00ff00ff) times k / 255, rounded.
std::uint32_t scale_lanes(std::uint32_t lanes, std::uint32_t k) {
    const std::uint32_t t = lanes * k + 0x00800080U;
    return ((t + ((t >> 8) & 0x00ff00ffU)) >> 8) & 0x00ff00ffU;
}

std::uint32_t scale_pixel(std::uint32_t p, std::uint32_t k) {
    return scale_lanes(p & 0x00ff00ffU, k) | (scale_lanes((p >> 8) & 0x00ff00ffU, k) << 8);
}

// A straight colour at coverage k (0..255) over a premultiplied pixel.
std::uint32_t blend(std::uint32_t destination, std::uint32_t opaque_color, std::uint32_t k) {
    return scale_pixel(opaque_color, k) + scale_pixel(destination, 255 - k);
}

std::uint32_t coverage(double amount) {
    return static_cast<std::uint32_t>(std::clamp(amount, 0.0, 1.0) * 255.0 + .5);
}

}  // namespace

void restore(const Target& target, const Target& source, Rect area) {
    area = area.intersected(target.bounds()).intersected(source.bounds());
    if (area.empty()) {
        return;
    }
    const std::size_t bytes = static_cast<std::size_t>(area.x1 - area.x0) * 4;
    for (int y = area.y0; y < area.y1; ++y) {
        std::memcpy(target.row(y) + area.x0, source.row(y) + area.x0, bytes);
    }
}

void fill(const Target& target, Rect area, Color color) {
    area = area.intersected(target.bounds());
    const std::uint32_t value = target.color(Color{color.r, color.g, color.b, 255});
    for (int y = area.y0; y < area.y1; ++y) {
        std::fill(target.row(y) + area.x0, target.row(y) + area.x1, value);
    }
}

void tint(const Target& target, Rect area, Color color, float alpha) {
    area = area.intersected(target.bounds());
    const std::uint32_t value = target.color(Color{color.r, color.g, color.b, 255});
    const std::uint32_t k = coverage(alpha);
    for (int y = area.y0; y < area.y1; ++y) {
        std::uint32_t* row = target.row(y);
        for (int x = area.x0; x < area.x1; ++x) {
            row[x] = blend(row[x], value, k);
        }
    }
}

void mask(const Target& target, Rect clip, int x, int y, int width, int height, std::span<const std::uint8_t> coverage_mask,
          Color color) {
    const Rect area = Rect{x, y, x + width, y + height}.intersected(clip).intersected(target.bounds());
    if (area.empty() || coverage_mask.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        return;
    }
    const std::uint32_t value = target.color(Color{color.r, color.g, color.b, 255});
    for (int py = area.y0; py < area.y1; ++py) {
        const std::uint8_t* cover = coverage_mask.data() + static_cast<std::size_t>(py - y) * static_cast<std::size_t>(width);
        std::uint32_t* row = target.row(py);
        for (int px = area.x0; px < area.x1; ++px) {
            const std::uint32_t k = cover[px - x];
            if (k == 255) {
                row[px] = value;
            } else if (k != 0) {
                row[px] = blend(row[px], value, k);
            }
        }
    }
}

void rounded(const Target& target, Rect clip, double x, double y, double w, double h, double radius, Color fill_color,
             float alpha, Color line, double line_width) {
    const Rect area = pixel_bounds(x, y, x + w, y + h).intersected(clip).intersected(target.bounds());
    const std::uint32_t fill_value = target.color(Color{fill_color.r, fill_color.g, fill_color.b, 255});
    const std::uint32_t line_value = target.color(Color{line.r, line.g, line.b, 255});
    // Between the corner arcs, a pixel centre is its own nearest point on the centre
    // line (cx == px + .5), so its distance from the edge, and the fill and line
    // coverages it gets, are the same across the row: solved once per row, and the
    // distance (a hypot) is evaluated only in the corners.
    const double inner_left = x + radius;
    const double inner_right = x + w - radius;
    for (int py = area.y0; py < area.y1; ++py) {
        std::uint32_t* row = target.row(py);
        const double cy = std::clamp(py + .5, y + radius, y + h - radius);
        // The columns whose centre lies between the arcs, found exactly as the clamp
        // below decides them.
        int first = std::max(area.x0, floor_int(inner_left - .5));
        while (first < area.x1 && !(first + .5 >= inner_left && first + .5 <= inner_right)) {
            ++first;
        }
        int last = std::min(area.x1, floor_int(inner_right - .5) + 2);
        while (last > first && !(last - 1 + .5 >= inner_left && last - 1 + .5 <= inner_right)) {
            --last;
        }
        const double between_distance = std::hypot(0.0, py + .5 - cy) - radius;
        const double between_inside = std::clamp(.5 - between_distance, 0.0, 1.0);
        const std::uint32_t between_fill = coverage(between_inside * alpha);
        const double between_edge =
            line_width > 0 ? std::clamp(line_width - std::fabs(between_distance + line_width * .5) + .5, 0.0, 1.0) : 0.0;
        const std::uint32_t between_line = coverage(between_edge * .75);
        for (int px = area.x0; px < area.x1; ++px) {
            if (px >= first && px < last) {
                if (between_inside <= 0) {
                    continue;
                }
                row[px] = blend(row[px], fill_value, between_fill);
                if (between_edge > 0) {
                    row[px] = blend(row[px], line_value, between_line);
                }
                continue;
            }
            const double cx = std::clamp(px + .5, inner_left, inner_right);
            const double distance = std::hypot(px + .5 - cx, py + .5 - cy) - radius;
            const double inside = std::clamp(.5 - distance, 0.0, 1.0);
            if (inside <= 0) {
                continue;
            }
            row[px] = blend(row[px], fill_value, coverage(inside * alpha));
            if (line_width > 0) {
                const double edge = std::clamp(line_width - std::fabs(distance + line_width * .5) + .5, 0.0, 1.0);
                if (edge > 0) {
                    row[px] = blend(row[px], line_value, coverage(edge * .75));
                }
            }
        }
    }
}

void over(const Target& target, const Target& layer, Rect area, float opacity) {
    area = area.intersected(target.bounds()).intersected(layer.bounds());
    const std::uint32_t fade = coverage(opacity);
    if (area.empty() || fade == 0) {
        return;
    }
    for (int y = area.y0; y < area.y1; ++y) {
        std::uint32_t* out = target.row(y);
        const std::uint32_t* in = layer.row(y);
        for (int x = area.x0; x < area.x1; ++x) {
            const std::uint32_t source = fade == 255 ? in[x] : scale_pixel(in[x], fade);
            const std::uint32_t alpha = source >> 24;
            if (alpha == 0) {
                continue;
            }
            out[x] = alpha == 255 ? source : source + scale_pixel(out[x], 255 - alpha);
        }
    }
}

}  // namespace render::r2d
