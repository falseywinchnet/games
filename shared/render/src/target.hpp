#pragma once
// What r2d and r3d draw into: 32-bit premultiplied pixels the engine does not own (a
// LiveSurface buffer, a kept layer, a preview frame), in the byte order the window
// presents without conversion, and pixel rectangles.
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace render {

// A half-open pixel rectangle [x0, x1) x [y0, y1).
struct Rect {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;

    [[nodiscard]] bool empty() const {
        return x1 <= x0 || y1 <= y0;
    }
    [[nodiscard]] long long area() const {
        return empty() ? 0 : static_cast<long long>(x1 - x0) * (y1 - y0);
    }
    [[nodiscard]] bool contains(const Rect& other) const {
        return other.empty() || (other.x0 >= x0 && other.y0 >= y0 && other.x1 <= x1 && other.y1 <= y1);
    }
    // The smallest rectangle holding both; an empty one adds nothing.
    [[nodiscard]] Rect united(const Rect& other) const {
        if (other.empty()) {
            return *this;
        }
        if (empty()) {
            return other;
        }
        return Rect{std::min(x0, other.x0), std::min(y0, other.y0), std::max(x1, other.x1), std::max(y1, other.y1)};
    }
    [[nodiscard]] Rect intersected(const Rect& other) const {
        const Rect out{std::max(x0, other.x0), std::max(y0, other.y0), std::min(x1, other.x1), std::min(y1, other.y1)};
        return out.empty() ? Rect{} : out;
    }
    [[nodiscard]] Rect inflated(int by) const {
        return empty() ? Rect{} : Rect{x0 - by, y0 - by, x1 + by, y1 + by};
    }
};

// The largest integer not above v.
[[nodiscard]] inline int floor_int(double v) {
    const int i = static_cast<int>(v);
    return i > v ? i - 1 : i;
}

// The rectangle of whole pixels touched by a box in floating-point pixel units.
[[nodiscard]] inline Rect pixel_bounds(double left, double top, double right, double bottom) {
    const Rect out{floor_int(left), floor_int(top), floor_int(right) + 1, floor_int(bottom) + 1};
    return out;
}

// Where red and blue sit in a pixel. macOS and Linux Skia rasters are RGBA, Windows DIBs
// BGRA; writing the window's own order lets it present by copying.
enum class Order : std::uint8_t { bgra, rgba };

// A colour with straight alpha, channels 0..255.
struct Color {
    float r = 0;
    float g = 0;
    float b = 0;
    float a = 255;
};

// A channel (0..255, clamped) rounded to a byte.
[[nodiscard]] inline std::uint32_t byte_of(float v) {
    const float c = v < 0 ? 0 : (v > 255 ? 255 : v);
    return static_cast<std::uint32_t>(c + .5F);
}

// One 32-bit pixel from straight channels, premultiplied.
[[nodiscard]] inline std::uint32_t pack(Order order, float r, float g, float b, float a = 255) {
    const float k = a >= 255 ? 1 : (a <= 0 ? 0 : a / 255);
    const std::uint32_t red = byte_of(r * k);
    const std::uint32_t blue = byte_of(b * k);
    const std::uint32_t low = order == Order::bgra ? blue : red;
    const std::uint32_t high = order == Order::bgra ? red : blue;
    return low | (byte_of(g * k) << 8) | (high << 16) | (byte_of(a) << 24);
}
[[nodiscard]] inline std::uint32_t pack(Order order, Color c) {
    return pack(order, c.r, c.g, c.b, c.a);
}

// Pixels the engine writes. Memory is little-endian 32-bit words: byte 0 is the low byte.
struct Target {
    std::uint32_t* pixels = nullptr;
    int stride = 0;  // in pixels
    int width = 0;
    int height = 0;
    Order order = Order::bgra;

    [[nodiscard]] Rect bounds() const {
        return Rect{0, 0, width, height};
    }
    [[nodiscard]] std::uint32_t* row(int y) const {
        return pixels + static_cast<std::ptrdiff_t>(y) * stride;
    }
    [[nodiscard]] std::uint32_t color(Color c) const {
        return pack(order, c);
    }
};

}  // namespace render
