#include "puzzle_render.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace games {
void PuzzleRaster::resize(int w, int h) {
    width = w;
    height = h;
    pixels.resize(w * h * 4);
    ids.resize(w * h);
    depth.resize(w * h);
    clear();
}
void PuzzleRaster::clear() {
    std::fill(pixels.begin(), pixels.end(), std::byte{0});
    std::fill(ids.begin(), ids.end(), -1);
    std::fill(depth.begin(), depth.end(), std::numeric_limits<double>::infinity());
}
static double side(RasterVertex a, RasterVertex b, double x, double y) {
    return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
}
void PuzzleRaster::triangle(RasterVertex a, RasterVertex b, RasterVertex c, int id) {
    double area = side(a, b, c.x, c.y);
    if (std::abs(area) < 1e-8)
        return;
    int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x})))),
        x1 = std::min(width - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y})))),
        y1 = std::min(height - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            double wa = side(b, c, x + .5, y + .5) / area, wb = side(c, a, x + .5, y + .5) / area,
                   wc = 1 - wa - wb;
            if (wa < 0 || wb < 0 || wc < 0)
                continue;
            int n = y * width + x;
            double z = wa * a.z + wb * b.z + wc * c.z;
            if (z > depth[n])
                continue;
            depth[n] = z;
            ids[n] = id;
            PixelColor col{wa * a.color.r + wb * b.color.r + wc * c.color.r,
                           wa * a.color.g + wb * b.color.g + wc * c.color.g,
                           wa * a.color.b + wb * b.color.b + wc * c.color.b,
                           wa * a.color.a + wb * b.color.a + wc * c.color.a};
            if (a.u >= 0 && !environment_.empty()) {
                double u = wa * a.u + wb * b.u + wc * c.u, v = wa * a.v + wb * b.v + wc * c.v;
                double px = std::clamp(u * env_width_ - .5, 0.0,
                                       static_cast<double>(env_width_ - 1)),
                       py = std::clamp(v * env_height_ - .5, 0.0,
                                       static_cast<double>(env_height_ - 1));
                int sx = static_cast<int>(px), sy = static_cast<int>(py),
                    ex = std::min(sx + 1, env_width_ - 1), ey = std::min(sy + 1, env_height_ - 1);
                double fx = px - sx, fy = py - sy;
                double channels[3]{};
                for (int channel = 0; channel < 3; ++channel)
                    channels[channel] =
                        (environment_[(sy * env_width_ + sx) * 4 + channel] * (1 - fx) * (1 - fy) +
                         environment_[(sy * env_width_ + ex) * 4 + channel] * fx * (1 - fy) +
                         environment_[(ey * env_width_ + sx) * 4 + channel] * (1 - fx) * fy +
                         environment_[(ey * env_width_ + ex) * 4 + channel] * fx * fy) *
                            .82 +
                        35;
                col = {channels[0], channels[1], channels[2], 255};
            }
            pixels[n * 4] = static_cast<std::byte>(std::clamp(col.b * col.a / 255, 0.0, 255.0));
            pixels[n * 4 + 1] = static_cast<std::byte>(std::clamp(col.g * col.a / 255, 0.0, 255.0));
            pixels[n * 4 + 2] = static_cast<std::byte>(std::clamp(col.r * col.a / 255, 0.0, 255.0));
            pixels[n * 4 + 3] = static_cast<std::byte>(std::clamp(col.a, 0.0, 255.0));
        }
}
void PuzzleRaster::disc(Point2 c, double radius, PixelColor color, int id, double z) {
    for (int i = 0; i < 32; ++i) {
        double a = i * 6.283185307 / 32, b = (i + 1) * 6.283185307 / 32;
        triangle({c.x, c.y, z, color},
                 {c.x + radius * std::cos(a), c.y + radius * std::sin(a), z, color},
                 {c.x + radius * std::cos(b), c.y + radius * std::sin(b), z, color}, id);
    }
}
void PuzzleRaster::line(Point2 a, Point2 b, double thickness, PixelColor color, int id, double z) {
    double length = std::hypot(b.x - a.x, b.y - a.y);
    if (length < 1e-6)
        return;
    double dx = (b.y - a.y) / length * thickness * .5, dy = -(b.x - a.x) / length * thickness * .5;
    RasterVertex p{a.x + dx, a.y + dy, z, color}, q{b.x + dx, b.y + dy, z, color},
        r{b.x - dx, b.y - dy, z, color}, s{a.x - dx, a.y - dy, z, color};
    triangle(p, q, r, id);
    triangle(p, r, s, id);
}
static PixelColor jewel(int color) {
    const PixelColor colors[] = {{220, 230, 240}, {228, 49, 86},   {59, 181, 255},
                                 {255, 182, 48},  {126, 224, 111}, {168, 101, 249},
                                 {53, 220, 208},  {244, 119, 44},  {233, 127, 203}};
    return colors[std::clamp(color, 0, 8)];
}
static PixelColor shade(PixelColor c, double n) {
    c.r = std::clamp(c.r * n, 0.0, 255.0);
    c.g = std::clamp(c.g * n, 0.0, 255.0);
    c.b = std::clamp(c.b * n, 0.0, 255.0);
    return c;
}
void PuzzleRaster::gem(Point2 center, double radius, int value, double angle, double glisten,
                       int id) {
    int color = value % 16, n = color == 1   ? 6
                                : color == 2 ? 4
                                : color == 3 ? 8
                                : color == 4 ? 5
                                : color == 5 ? 6
                                : color == 6 ? 4
                                             : 8;
    if (value == 48)
        n = 4;
    PixelColor base = jewel(color);
    double tilt = std::cos(angle) * .18 + .82;
    disc({center.x + 2, center.y + 5}, radius * .91, {2, 5, 15, 180}, id, 2);
    for (int i = 0; i < n; ++i) {
        double a = 6.283185307 * i / n + angle * .17 + .785398,
               b = 6.283185307 * (i + 1) / n + angle * .17 + .785398;
        Point2 p{center.x + std::cos(a) * radius * tilt, center.y + std::sin(a) * radius},
            q{center.x + std::cos(b) * radius * tilt, center.y + std::sin(b) * radius};
        Point2 ip{center.x + (p.x - center.x) * .56, center.y + (p.y - center.y) * .56},
            iq{center.x + (q.x - center.x) * .56, center.y + (q.y - center.y) * .56};
        PixelColor face = shade(base, .52 + .58 * (.5 + .5 * std::sin(a - 1)));
        if (value == 48)
            face = jewel(1 + i % 6);
        triangle({p.x, p.y, 0, face}, {q.x, q.y, 0, face}, {ip.x, ip.y, 0, shade(face, 1.35)}, id);
        triangle({q.x, q.y, 0, face}, {iq.x, iq.y, 0, shade(face, 1.35)},
                 {ip.x, ip.y, 0, shade(face, 1.35)}, id);
        triangle({center.x, center.y, 0, shade(base, 1.25)}, {ip.x, ip.y, 0, shade(base, 1.25)},
                 {iq.x, iq.y, 0, shade(base, .96)}, id);
    }
    if (glisten > .7) {
        double r = radius * (glisten - .7) * 1.7;
        line({center.x - radius * .3 - r, center.y - radius * .35},
             {center.x - radius * .3 + r, center.y - radius * .35}, 1.3, {255, 255, 255}, id, -1);
        line({center.x - radius * .3, center.y - radius * .35 - r},
             {center.x - radius * .3, center.y - radius * .35 + r}, 1.3, {255, 255, 255}, id, -1);
    }
    if (value >= 16 && value < 32) {
        disc(center, radius * .24, {32, 27, 52}, id, -2);
        line({center.x + radius * .09, center.y - radius * .12},
             {center.x + radius * .25, center.y - radius * .38}, 2, {255, 240, 192}, id, -3);
    }
    if (value >= 32 && value < 48) {
        for (int i = 0; i < 5; ++i) {
            double a = i * 6.283185307 / 5 - 1.5708, b = (i + 2) * 6.283185307 / 5 - 1.5708;
            line({center.x + std::cos(a) * radius * .34, center.y + std::sin(a) * radius * .34},
                 {center.x + std::cos(b) * radius * .34, center.y + std::sin(b) * radius * .34}, 2,
                 {255, 255, 225}, id, -2);
        }
    }
}
static Point3 rotate(Point3 p, double yaw, double pitch) {
    double x = p.x * std::cos(yaw) + p.z * std::sin(yaw),
           z = -p.x * std::sin(yaw) + p.z * std::cos(yaw);
    return {x, p.y * std::cos(pitch) - z * std::sin(pitch),
            p.y * std::sin(pitch) + z * std::cos(pitch)};
}
PixelColor PuzzleRaster::reflection(Point3 normal, Point3 position) const {
    Point3 v{position.x, position.y, position.z - 5};
    double length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    v.x /= length;
    v.y /= length;
    v.z /= length;
    double dot = v.x * normal.x + v.y * normal.y + v.z * normal.z;
    Point3 r{v.x - 2 * dot * normal.x, v.y - 2 * dot * normal.y, v.z - 2 * dot * normal.z};
    if (environment_.empty())
        return {125 + 70 * r.y, 158 + 55 * r.y, 161 + 65 * r.y};
    double u = .5 + std::atan2(r.x, r.z) / 6.283185307,
           vv = .5 - std::asin(std::clamp(r.y, -1.0, 1.0)) / 3.141592654;
    int x = std::clamp(static_cast<int>(u * env_width_), 0, env_width_ - 1),
        y = std::clamp(static_cast<int>(vv * env_height_), 0, env_height_ - 1);
    int i = (y * env_width_ + x) * 4;
    return {environment_[i] * .76 + 52, environment_[i + 1] * .76 + 52,
            environment_[i + 2] * .76 + 52};
}
void PuzzleRaster::cube(const PuzzleGame& game, double yaw, double pitch, int hover) {
    clear();
    double scale = std::min(width, height) * 1.28;
    std::array<Point2, 96> projected{};
    std::array<double, 96> z{};
    std::array<bool, 96> visible{};
    for (int cell = 0; cell < 96; ++cell) {
        if (!PuzzleGame::cube_playable(cell))
            continue;
        int face = cell / 16;
        Point3 center = PuzzleGame::cube_center(cell);
        Point3 normal = face == 0   ? Point3{0, 0, 1}
                        : face == 1 ? Point3{1, 0, 0}
                        : face == 2 ? Point3{0, 0, -1}
                        : face == 3 ? Point3{-1, 0, 0}
                        : face == 4 ? Point3{0, -1, 0}
                                    : Point3{0, 1, 0};
        Point3 n = rotate(normal, yaw, pitch), c = rotate(center, yaw, pitch);
        visible[cell] = n.x * (-c.x) + n.y * (-c.y) + n.z * (5 - c.z) > 0;
        if (!visible[cell])
            continue;
        projected[cell] = {width * .5 + c.x * scale / (5 - c.z),
                           height * .5 + c.y * scale / (5 - c.z)};
        z[cell] = -c.z;
        RasterVertex corners[4];
        for (int k = 0; k < 4; ++k) {
            double u = (k == 0 || k == 3) ? -.236 : .236, v = k < 2 ? -.236 : .236;
            Point3 p = center;
            if (face == 0 || face == 2) {
                p.x += u;
                p.y += v;
            } else if (face == 1 || face == 3) {
                p.z += u;
                p.y += v;
            } else {
                p.x += u;
                p.z += v;
            }
            p = rotate(p, yaw, pitch);
            PixelColor color = reflection(n, p);
            if (cell == hover)
                color = shade(color, 1.15);
            Point3 view{p.x, p.y, p.z - 5};
            double length = std::sqrt(view.x * view.x + view.y * view.y + view.z * view.z);
            view.x /= length;
            view.y /= length;
            view.z /= length;
            double dot = view.x * n.x + view.y * n.y + view.z * n.z;
            Point3 reflected{view.x - 2 * dot * n.x, view.y - 2 * dot * n.y,
                             view.z - 2 * dot * n.z};
            double env_u = .5 + std::atan2(reflected.x, reflected.z) / 6.283185307,
                   env_v = .5 - std::asin(std::clamp(reflected.y, -1.0, 1.0)) / 3.141592654;
            if (game.state.marks[cell]) {
                color = shade(jewel(game.state.marks[cell]),
                              .72 + .28 * std::max(0.0, n.z) + (k < 2 ? .15 : 0));
                env_u = -1;
                env_v = -1;
            }
            corners[k] = {width * .5 + p.x * scale / (5 - p.z),
                          height * .5 + p.y * scale / (5 - p.z),
                          -p.z,
                          color,
                          env_u,
                          env_v};
        }
        triangle(corners[0], corners[1], corners[2], cell);
        triangle(corners[0], corners[2], corners[3], cell);
        if (game.state.marks[cell]) {
            RasterVertex badge[4];
            for (int k = 0; k < 4; ++k)
                badge[k] = {projected[cell].x + (corners[k].x - projected[cell].x) * .17,
                            projected[cell].y + (corners[k].y - projected[cell].y) * .17,
                            z[cell] + (corners[k].z - z[cell]) * .17 - .008,
                            {246, 245, 227}};
            triangle(badge[0], badge[1], badge[2], cell);
            triangle(badge[0], badge[2], badge[3], cell);
        }
    }
    for (int pair = 0; pair < 6; ++pair) {
        const std::vector<int>& path = game.state.paths[pair];
        for (std::size_t j = 1; j < path.size(); ++j) {
            int a = path[j - 1], b = path[j];
            if (!visible[a] || !visible[b])
                continue;
            line(projected[a], projected[b], 8, jewel(pair + 1), b, std::min(z[a], z[b]) - .03);
        }
    }
}
} // namespace games
