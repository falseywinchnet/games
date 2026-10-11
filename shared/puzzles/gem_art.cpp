#include "gem_art.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace games {
namespace {
namespace r3d = render::r3d;
using render::Color;

Color jewel(int color) {
    const Color colors[] = {{220, 230, 240}, {228, 49, 86},   {59, 181, 255},
                            {255, 182, 48},  {126, 224, 111}, {168, 101, 249},
                            {53, 220, 208},  {244, 119, 44},  {233, 127, 203}};
    return colors[std::clamp(color, 0, 8)];
}
Color shade(Color c, double n) {
    c.r = static_cast<float>(std::clamp(c.r * n, 0.0, 255.0));
    c.g = static_cast<float>(std::clamp(c.g * n, 0.0, 255.0));
    c.b = static_cast<float>(std::clamp(c.b * n, 0.0, 255.0));
    return c;
}

struct Point {
    double x = 0, y = 0;
};

// Triangles, discs and strokes in the layer. Facets are shaded across from their corners;
// everything else is one colour, laid premultiplied, so it costs no interpolation.
struct Brush {
    r3d::Pass& pass;
    float lift = 0;
    void facet(Point a, Color ca, Point b, Color cb, Point c, Color cc, float z) const {
        const float va[3] = {ca.r, ca.g, ca.b}, vb[3] = {cb.r, cb.g, cb.b}, vc[3] = {cc.r, cc.g, cc.b};
        r3d::triangle(pass, corner(a, z), corner(b, z), corner(c, z), va, vb, vc,
                      r3d::Smooth{pass.target.order});
    }
    void solid(Point a, Point b, Point c, Color color, float z) const {
        r3d::triangle(pass, corner(a, z), corner(b, z), corner(c, z),
                      r3d::Flat{render::pack(pass.target.order, color)});
    }
    void disc(Point c, double radius, Color color, float z) const {
        for (int i = 0; i < 32; ++i) {
            const double a = i * 6.283185307 / 32, b = (i + 1) * 6.283185307 / 32;
            solid(c, {c.x + radius * std::cos(a), c.y + radius * std::sin(a)},
                  {c.x + radius * std::cos(b), c.y + radius * std::sin(b)}, color, z);
        }
    }
    void line(Point a, Point b, double thickness, Color color, float z) const {
        const double length = std::hypot(b.x - a.x, b.y - a.y);
        if (length < 1e-6)
            return;
        const double dx = (b.y - a.y) / length * thickness * .5,
                     dy = -(b.x - a.x) / length * thickness * .5;
        const Point p{a.x + dx, a.y + dy}, q{b.x + dx, b.y + dy}, r{b.x - dx, b.y - dy},
            s{a.x - dx, a.y - dy};
        solid(p, q, r, color, z);
        solid(p, r, s, color, z);
    }
    r3d::Corner corner(Point p, float z) const {
        return {static_cast<float>(p.x), static_cast<float>(p.y), z + lift};
    }
};

// Light falls from the upper left.
struct FacetLight {
    double light = -2.356;
    double operator()(double a, double strength) const {
        return .5 + strength * std::max(0.0, std::cos(a - light)) +
               .12 * std::cos(a - light + 3.14159) * -1;
    }
};

struct GemCorner {
    int n;
    double spin;
    Point center;
    double tilt;
    Point operator()(int i, double r) const {
        const double a = 6.283185307 * i / n + spin;
        return Point{center.x + std::cos(a) * r * tilt, center.y + std::sin(a) * r};
    }
};

// The mean of a 2 x 2 block of premultiplied pixels, two channels to a sum.
std::uint32_t average(const std::uint32_t* top, const std::uint32_t* bottom) {
    const std::uint32_t a = top[0], b = top[1], c = bottom[0], d = bottom[1];
    if ((a | b | c | d) == 0)
        return 0;
    const std::uint32_t rb = (a & 0x00ff00ffU) + (b & 0x00ff00ffU) + (c & 0x00ff00ffU) +
                             (d & 0x00ff00ffU) + 0x00020002U;
    const std::uint32_t ag = ((a >> 8) & 0x00ff00ffU) + ((b >> 8) & 0x00ff00ffU) +
                             ((c >> 8) & 0x00ff00ffU) + ((d >> 8) & 0x00ff00ffU) + 0x00020002U;
    return ((rb >> 2) & 0x00ff00ffU) | (((ag >> 2) & 0x00ff00ffU) << 8);
}

} // namespace

void draw_gem(r3d::Pass& pass, double x, double y, double radius, int value, double angle,
              double glisten, float lift) {
    const Brush brush{pass, lift};
    const Point center{x, y};
    const int color = value % 16;
    int n = color == 1   ? 6
            : color == 2 ? 4
            : color == 3 ? 8
            : color == 4 ? 5
            : color == 5 ? 6
            : color == 6 ? 4
                         : 8;
    if (value == 48)
        n = 4;
    const Color base = jewel(color);
    const double tilt = std::cos(angle) * .18 + .82;
    const FacetLight lit;
    const GemCorner corner{n, angle * .17 + .785398, center, tilt};
    // The stone first, nearest, so the depth test spares the girdle and shadow under it.
    for (int i = 0; i < n; ++i) {
        const double a = 6.283185307 * (i + .5) / n + corner.spin;
        const Point p = corner(i, radius), q = corner(i + 1, radius),
                    ip = corner(i, radius * .56), iq = corner(i + 1, radius * .56);
        const Color face = value == 48 ? jewel(1 + i % 6) : base;
        const double outer = lit(a, .62), inner = outer + .22;
        // Crown facets: two triangles from the girdle to the table.
        brush.facet(p, shade(face, outer), q, shade(face, outer), ip, shade(face, inner), 0);
        brush.facet(q, shade(face, outer * .94), iq, shade(face, inner), ip, shade(face, inner),
                    0);
        // Table: brightest at the center, shaded toward the far side.
        const double ta = lit(6.283185307 * i / n + corner.spin, .4) + .35,
                     tb = lit(6.283185307 * (i + 1) / n + corner.spin, .4) + .35;
        brush.facet(center, shade(value == 48 ? jewel(1 + (i + 3) % 6) : base, 1.42), ip,
                    shade(face, ta), iq, shade(face, tb), 0);
    }
    // Girdle: a dark outline just outside the stone, drawn only where it shows, as the band
    // between the stone's edge and its own.
    const Color edge = value == 48 ? Color{40, 40, 60} : shade(base, .28);
    for (int i = 0; i < n; ++i) {
        const Point p = corner(i, radius), q = corner(i + 1, radius);
        const Point op = corner(i, radius * 1.07), oq = corner(i + 1, radius * 1.07);
        brush.solid(p, op, oq, edge, 1);
        brush.solid(p, oq, q, edge, 1);
    }
    brush.disc({x + radius * .05, y + radius * .12}, radius * .92, {6, 2, 16, 110}, 3);
    // Specular glint on the table, upper left.
    {
        const double gx = x - radius * .22 * tilt, gy = y - radius * .26, g = radius * .13;
        brush.facet({gx - g, gy}, {255, 255, 255}, {gx, gy - g * .7}, {255, 255, 255},
                    {gx + g * .5, gy + g * .2}, {235, 240, 255}, -.5F);
    }
    // Strokes keep their width on screen whatever the layer's density: it is at least 1.5.
    const double stroke = std::max(1.0, radius / 30);
    if (glisten > .7) {
        const double r = radius * (glisten - .7) * 1.7;
        brush.line({x - radius * .3 - r, y - radius * .35}, {x - radius * .3 + r, y - radius * .35},
                   1.3 * stroke, {255, 255, 255}, -1);
        brush.line({x - radius * .3, y - radius * .35 - r}, {x - radius * .3, y - radius * .35 + r},
                   1.3 * stroke, {255, 255, 255}, -1);
    }
    if (value >= 16 && value < 32) {
        brush.disc(center, radius * .24, {32, 27, 52}, -2);
        brush.line({x + radius * .09, y - radius * .12}, {x + radius * .25, y - radius * .38},
                   2 * stroke, {255, 240, 192}, -3);
    }
    if (value >= 32 && value < 48)
        for (int i = 0; i < 5; ++i) {
            const double a = i * 6.283185307 / 5 - 1.5708, b = (i + 2) * 6.283185307 / 5 - 1.5708;
            brush.line({x + std::cos(a) * radius * .34, y + std::sin(a) * radius * .34},
                       {x + std::cos(b) * radius * .34, y + std::sin(b) * radius * .34}, 2 * stroke,
                       {255, 255, 225}, -2);
        }
}

void lay_layer(const render::Target& target, render::Rect area, const render::Target& layer,
               int left, int top, int scale) {
    const render::Rect cover{left, top, left + layer.width / scale, top + layer.height / scale};
    area = area.intersected(cover).intersected(target.bounds());
    for (int y = area.y0; y < area.y1; ++y) {
        std::uint32_t* out = target.row(y);
        const std::uint32_t* first = layer.row((y - top) * scale);
        const std::uint32_t* second = layer.row((y - top) * scale + scale - 1);
        for (int x = area.x0; x < area.x1; ++x) {
            const std::uint32_t source =
                scale == 1 ? first[x - left]
                           : average(first + (x - left) * 2, second + (x - left) * 2);
            const std::uint32_t alpha = source >> 24;
            if (alpha == 0)
                continue;
            if (alpha == 255) {
                out[x] = source;
                continue;
            }
            // Premultiplied over: the board keeps (255 - alpha) of itself.
            const std::uint32_t keep = 255 - alpha, d = out[x];
            const std::uint32_t rb = ((d & 0x00ff00ffU) * keep + 0x00800080U) >> 8 & 0x00ff00ffU;
            const std::uint32_t ag = ((d >> 8) & 0x00ff00ffU) * keep + 0x00800080U & 0xff00ff00U;
            out[x] = source + (rb | ag);
        }
    }
}

void rounded_outline(const render::Target& target, render::Rect clip, double x, double y,
                     double w, double h, double radius, Color color, float alpha,
                     double line_width) {
    const render::Rect area = render::pixel_bounds(x - line_width, y - line_width,
                                                   x + w + line_width, y + h + line_width)
                                  .intersected(clip)
                                  .intersected(target.bounds());
    const std::uint32_t value = target.color(Color{color.r, color.g, color.b, 255});
    for (int py = area.y0; py < area.y1; ++py) {
        std::uint32_t* row = target.row(py);
        const double cy = std::clamp(py + .5, y + radius, y + h - radius);
        for (int px = area.x0; px < area.x1; ++px) {
            const double cx = std::clamp(px + .5, x + radius, x + w - radius);
            const double distance = std::hypot(px + .5 - cx, py + .5 - cy) - radius;
            // Centred on the edge, as a stroke is.
            const double edge = std::clamp(line_width * .5 - std::fabs(distance) + .5, 0.0, 1.0);
            if (edge <= 0)
                continue;
            const std::uint32_t k = static_cast<std::uint32_t>(edge * alpha * 255 + .5);
            const std::uint32_t d = row[px];
            const std::uint32_t rb = (((value & 0x00ff00ffU) * k + (d & 0x00ff00ffU) * (255 - k) +
                                       0x00800080U) >> 8) & 0x00ff00ffU;
            const std::uint32_t ag = ((((value >> 8) & 0x00ff00ffU) * k +
                                       ((d >> 8) & 0x00ff00ffU) * (255 - k) + 0x00800080U)) &
                                     0xff00ff00U;
            row[px] = rb | ag;
        }
    }
}

} // namespace games
