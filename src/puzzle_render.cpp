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
    const double inv = 1 / area;
    // Edge functions are linear in x along a row; each row visits only the span where
    // all three can be non-negative (plus a pixel of slack), then tests exactly.
    const RasterVertex* edges[3][2] = {{&b, &c}, {&c, &a}, {&a, &b}};
    for (int y = y0; y <= y1; ++y) {
        const double yc = y + .5;
        double lo = x0, hi = x1 + 1;
        bool empty = false;
        for (const RasterVertex* const(&e)[2] : edges) {
            const RasterVertex &p = *e[0], &q = *e[1];
            // side(p, q, x, yc) * inv = slope * x + offset, required >= 0.
            const double slope = -(q.y - p.y) * inv,
                         offset = ((q.x - p.x) * (yc - p.y) + (q.y - p.y) * p.x) * inv;
            if (std::abs(slope) < 1e-12) {
                if (offset < -1e-9)
                    empty = true;
                continue;
            }
            const double root = -offset / slope - .5;
            if (slope > 0)
                lo = std::max(lo, std::floor(root) - 1);
            else
                hi = std::min(hi, std::ceil(root) + 1);
        }
        if (empty || lo > hi)
            continue;
        const int xs = std::max(x0, static_cast<int>(lo)), xe = std::min(x1, static_cast<int>(hi));
        for (int x = xs; x <= xe; ++x) {
            // Evaluate all three edges directly: subtraction can make an exact
            // shared-edge pixel slightly negative and open a crack in the mesh.
            double wa = side(b, c, x + .5, yc) * inv, wb = side(c, a, x + .5, yc) * inv,
                   wc = side(a, b, x + .5, yc) * inv;
            if (wa < 0 || wb < 0 || wc < 0)
                continue;
            int n = y * width + x;
            double z = wa * a.z + wb * b.z + wc * c.z;
            if (z > -1e8)
                z += z_bias;
            // Overlay strokes use a huge negative depth and paint in submission order.
            if (z > depth[n] && z > -1e8)
                continue;
            depth[n] = z;
            ids[n] = id;
            PixelColor col{wa * a.color.r + wb * b.color.r + wc * c.color.r,
                           wa * a.color.g + wb * b.color.g + wc * c.color.g,
                           wa * a.color.b + wb * b.color.b + wc * c.color.b,
                           wa * a.color.a + wb * b.color.a + wc * c.color.a};
            if (a.u >= 0 && !environment_.empty()) {
                double u = wa * a.u + wb * b.u + wc * c.u, v = wa * a.v + wb * b.v + wc * c.v;
                // Longitude wraps around the panorama; latitude clamps at the poles.
                double px = u * env_width_ - .5;
                px -= std::floor(px / env_width_) * env_width_;
                double py =
                    std::clamp(v * env_height_ - .5, 0.0, static_cast<double>(env_height_ - 1));
                int sx = static_cast<int>(px), sy = static_cast<int>(py),
                    ex = (sx + 1) % env_width_, ey = std::min(sy + 1, env_height_ - 1);
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
                const double k = a.env;
                col = {channels[0] * k + col.r * (1 - k), channels[1] * k + col.g * (1 - k),
                       channels[2] * k + col.b * (1 - k), 255};
            }
            pixels[n * 4] =
                static_cast<std::byte>(std::clamp(col.b * col.a / 255, 0.0, 255.0) + .5);
            pixels[n * 4 + 1] =
                static_cast<std::byte>(std::clamp(col.g * col.a / 255, 0.0, 255.0) + .5);
            pixels[n * 4 + 2] =
                static_cast<std::byte>(std::clamp(col.r * col.a / 255, 0.0, 255.0) + .5);
            pixels[n * 4 + 3] = static_cast<std::byte>(std::clamp(col.a, 0.0, 255.0) + .5);
        }
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
    line(a, b, thickness, color, id, z, z);
}
void PuzzleRaster::line(Point2 a, Point2 b, double thickness, PixelColor color, int id, double za,
                        double zb) {
    double length = std::hypot(b.x - a.x, b.y - a.y);
    if (length < 1e-6)
        return;
    double dx = (b.y - a.y) / length * thickness * .5, dy = -(b.x - a.x) / length * thickness * .5;
    RasterVertex p{a.x + dx, a.y + dy, za, color}, q{b.x + dx, b.y + dy, zb, color},
        r{b.x - dx, b.y - dy, zb, color}, s{a.x - dx, a.y - dy, za, color};
    triangle(p, q, r, id);
    triangle(p, r, s, id);
}
static PixelColor jewel(int color) {
    const PixelColor colors[] = {{220, 230, 240}, {228, 49, 86},   {59, 181, 255},
                                 {255, 182, 48},  {126, 224, 111}, {168, 101, 249},
                                 {53, 220, 208},  {244, 119, 44},  {233, 127, 203}};
    return colors[std::clamp(color, 0, 8)];
}
// Nature Cube pair colors: nine hues that stay apart on the glass.
static PixelColor pair_color(int pair) {
    const PixelColor colors[] = {{220, 230, 240}, {232, 58, 90},   {52, 170, 255}, {255, 186, 40},
                                 {98, 214, 92},   {170, 104, 250}, {40, 222, 208}, {255, 118, 36},
                                 {250, 120, 205}, {176, 120, 70}};
    return colors[std::clamp(pair, 0, 9)];
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
    const double light = -2.356; // light falls from the upper left
    struct FacetLight {
        double light;
        double operator()(double a, double strength) const {
            return .5 + strength * std::max(0.0, std::cos(a - light)) +
                   .12 * std::cos(a - light + 3.14159) * -1;
        }
    };
    FacetLight lit{light};
    disc({center.x + radius * .05, center.y + radius * .12}, radius * .92, {6, 2, 16, 110}, id, 3);
    const double spin = angle * .17 + .785398;
    struct GemCorner {
        int n;
        double spin;
        Point2 center;
        double tilt;
        Point2 operator()(int i, double r) const {
            const double a = 6.283185307 * i / n + spin;
            return Point2{center.x + std::cos(a) * r * tilt, center.y + std::sin(a) * r};
        }
    };
    GemCorner corner{n, spin, center, tilt};
    // Girdle: a dark outline just outside the stone.
    for (int i = 0; i < n; ++i) {
        Point2 p = corner(i, radius * 1.07), q = corner(i + 1, radius * 1.07);
        PixelColor edge = shade(base, .28);
        if (value == 48)
            edge = {40, 40, 60};
        triangle({center.x, center.y, 1, edge}, {p.x, p.y, 1, edge}, {q.x, q.y, 1, edge}, id);
    }
    for (int i = 0; i < n; ++i) {
        const double a = 6.283185307 * (i + .5) / n + spin;
        Point2 p = corner(i, radius), q = corner(i + 1, radius), ip = corner(i, radius * .56),
               iq = corner(i + 1, radius * .56);
        PixelColor face = value == 48 ? jewel(1 + i % 6) : base;
        const double outer = lit(a, .62), inner = outer + .22;
        // Crown facets: two triangles from the girdle to the table.
        triangle({p.x, p.y, 0, shade(face, outer)}, {q.x, q.y, 0, shade(face, outer)},
                 {ip.x, ip.y, 0, shade(face, inner)}, id);
        triangle({q.x, q.y, 0, shade(face, outer * .94)}, {iq.x, iq.y, 0, shade(face, inner)},
                 {ip.x, ip.y, 0, shade(face, inner)}, id);
        // Table: brightest at the center, shaded toward the far side.
        const double ta = lit(6.283185307 * i / n + spin, .4) + .35,
                     tb = lit(6.283185307 * (i + 1) / n + spin, .4) + .35;
        triangle({center.x, center.y, 0, shade(value == 48 ? jewel(1 + (i + 3) % 6) : base, 1.42)},
                 {ip.x, ip.y, 0, shade(face, ta)}, {iq.x, iq.y, 0, shade(face, tb)}, id);
    }
    // Specular glint on the table, upper left.
    {
        const double gx = center.x - radius * .22 * tilt, gy = center.y - radius * .26,
                     g = radius * .13;
        triangle({gx - g, gy, -.5, {255, 255, 255}}, {gx, gy - g * .7, -.5, {255, 255, 255}},
                 {gx + g * .5, gy + g * .2, -.5, {235, 240, 255}}, id);
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
namespace {
Point3 add(Point3 a, Point3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Point3 scaled(Point3 a, double k) {
    return {a.x * k, a.y * k, a.z * k};
}
double dot3(Point3 a, Point3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
Point3 face_normal(int face) {
    return face == 0   ? Point3{0, 0, 1}
           : face == 1 ? Point3{1, 0, 0}
           : face == 2 ? Point3{0, 0, -1}
           : face == 3 ? Point3{-1, 0, 0}
           : face == 4 ? Point3{0, -1, 0}
                       : Point3{0, 1, 0};
}
// Two in-plane axes for a face, matching PuzzleGame::cube_center's (u, v) layout.
void face_axes(int face, Point3& u, Point3& v) {
    switch (face) {
    case 0:
        u = {1, 0, 0};
        v = {0, 1, 0};
        break;
    case 1:
        u = {0, 0, -1};
        v = {0, 1, 0};
        break;
    case 2:
        u = {-1, 0, 0};
        v = {0, 1, 0};
        break;
    case 3:
        u = {0, 0, 1};
        v = {0, 1, 0};
        break;
    case 4:
        u = {1, 0, 0};
        v = {0, 0, 1};
        break;
    default:
        u = {1, 0, 0};
        v = {0, 0, -1};
        break;
    }
}
} // namespace
// The cube is a block of dark glass with sixteen mirrored tiles on each playable face.
// Reflections are computed on subdivided tiles so the panorama never smears across a
// seam, the grout shows the glass body rather than the scenery behind, and traced
// paths run over the surface, folding across the cube's edges.
void PuzzleRaster::cube(const PuzzleGame& game, double yaw, double pitch, int hover) {
    clear();
    const double scale = std::min(width, height) * 1.28;
    struct Projected {
        double x, y, depth;
    };
    struct ProjectCube {
        double yaw, pitch, scale;
        int width, height;
        Projected operator()(Point3 world) const {
            Point3 p = rotate(world, yaw, pitch);
            return Projected{width * .5 + p.x * scale / (5 - p.z),
                             height * .5 + p.y * scale / (5 - p.z), -p.z};
        }
    };
    ProjectCube project{yaw, pitch, scale, width, height};
    struct ReflectCube {
        double yaw, pitch;
        void operator()(Point3 world, Point3 n_world, double& u, double& v) const {
            Point3 p = rotate(world, yaw, pitch), n = rotate(n_world, yaw, pitch);
            Point3 view{p.x, p.y, p.z - 5};
            const double length = std::sqrt(dot3(view, view));
            view = scaled(view, 1 / length);
            const double d = dot3(view, n);
            Point3 r{view.x - 2 * d * n.x, view.y - 2 * d * n.y, view.z - 2 * d * n.z};
            u = .5 + std::atan2(r.x, r.z) / 6.283185307;
            v = .5 - std::asin(std::clamp(r.y, -1.0, 1.0)) / 3.141592654;
        }
    };
    ReflectCube env_uv{yaw, pitch};
    struct VisibleCubeFace {
        double yaw, pitch;
        bool operator()(int face) const {
            Point3 n = rotate(face_normal(face), yaw, pitch),
                   c = rotate(face_normal(face), yaw, pitch);
            return n.x * (-c.x) + n.y * (-c.y) + n.z * (5 - c.z) > 0;
        }
    };
    VisibleCubeFace face_visible{yaw, pitch};
    // Emits a quad, subdivided so interpolated reflection coordinates stay accurate.
    struct CubeQuad {
        PuzzleRaster& raster;
        const ProjectCube& project;
        const ReflectCube& env_uv;
        void operator()(Point3 origin, Point3 du, Point3 dv, Point3 normal, PixelColor tint,
                        double env, int steps, double bias, int id) const {
            for (int j = 0; j < steps; ++j)
                for (int i = 0; i < steps; ++i) {
                    RasterVertex corner[4];
                    for (int k = 0; k < 4; ++k) {
                        const double a = (i + (k == 1 || k == 2)) / static_cast<double>(steps),
                                     b = (j + (k >= 2)) / static_cast<double>(steps);
                        Point3 w = add(origin, add(scaled(du, a), scaled(dv, b)));
                        Projected q = project(w);
                        double u = -1, v = -1;
                        if (env > 0)
                            env_uv(w, normal, u, v);
                        corner[k] = {q.x, q.y, q.depth + bias, tint, u, v, env};
                    }
                    if (env > 0) {
                        double lo = 1, hi = 0;
                        for (const RasterVertex& c : corner) {
                            lo = std::min(lo, c.u);
                            hi = std::max(hi, c.u);
                        }
                        if (hi - lo > .5)
                            for (RasterVertex& c : corner)
                                if (c.u < .5)
                                    c.u += 1;
                    }
                    raster.triangle(corner[0], corner[1], corner[2], id);
                    raster.triangle(corner[0], corner[2], corner[3], id);
                }
        }
    };
    CubeQuad quad{*this, project, env_uv};
    std::array<bool, 6> visible{};
    for (int face : {0, 3, 4})
        visible[face] = face_visible(face);
    for (int cell = 0; cell < 96; ++cell) {
        const int face = cell / 16;
        if (!PuzzleGame::cube_playable(cell) || !visible[face])
            continue;
        Point3 n = face_normal(face), u, v;
        face_axes(face, u, v);
        const Point3 center = PuzzleGame::cube_center(cell);
        const double half = .224;
        const Point3 origin = add(center, add(scaled(u, -half), scaled(v, -half)));
        const int mark = game.state.marks[cell];
        if (game.cube_rock(cell)) {
            // A mossy pebble resting in a bed of earth: a domed fan of triangles, lit from
            // the upper left, with moss on its shoulders.
            quad(origin, scaled(u, 2 * half), scaled(v, 2 * half), n, {40, 54, 40}, .1, 3, 0, cell);
            const int sides = 14;
            const Point3 lift = scaled(n, .06);
            const Projected top = project(add(center, lift));
            for (int k = 0; k < sides; ++k) {
                const double a0 = k * 6.283185307 / sides, a1 = (k + 1) * 6.283185307 / sides;
                const double r0 = .2 * (1 + .1 * std::sin(a0 * 3 + cell)),
                             r1 = .2 * (1 + .1 * std::sin(a1 * 3 + cell));
                const Point3 e0 = add(
                    center, add(scaled(u, std::cos(a0) * r0), scaled(v, std::sin(a0) * r0 * .86)));
                const Point3 e1 = add(
                    center, add(scaled(u, std::cos(a1) * r1), scaled(v, std::sin(a1) * r1 * .86)));
                const Projected p0 = project(add(e0, scaled(n, .012))),
                                p1 = project(add(e1, scaled(n, .012)));
                const Projected screen_a = project(add(center, scaled(u, .1))),
                                screen_b = project(add(center, scaled(v, .1)));
                // Light by the rim direction on screen: brighter toward the upper left.
                const double sx = (screen_a.x - top.x) * std::cos(a0 + .22) +
                                  (screen_b.x - top.x) * std::sin(a0 + .22),
                             sy = (screen_a.y - top.y) * std::cos(a0 + .22) +
                                  (screen_b.y - top.y) * std::sin(a0 + .22);
                const double len = std::max(1e-6, std::hypot(sx, sy));
                const double lit = std::clamp(.55 - .45 * (sx + sy) / len * .7071, .15, 1.0);
                const bool moss = std::sin(a0 * 2 + cell * 1.7) > .35;
                const PixelColor rim =
                    moss ? PixelColor{90 * lit + 40, 140 * lit + 46, 70 * lit + 34}
                         : PixelColor{132 * lit + 44, 134 * lit + 44, 120 * lit + 40};
                const PixelColor crown{150, 156, 140};
                triangle({top.x, top.y, top.depth - .02, crown}, {p0.x, p0.y, p0.depth - .02, rim},
                         {p1.x, p1.y, p1.depth - .02, rim}, cell);
            }
        } else if (mark) {
            PixelColor jewel_color = pair_color(mark);
            quad(origin, scaled(u, 2 * half), scaled(v, 2 * half), n,
                 shade(jewel_color, cell == hover ? 1.2 : 1.0), .2, 3, 0, cell);
            // A pale socket marks where the color starts and ends.
            const double inner = .07;
            quad(add(center, add(scaled(u, -inner), scaled(v, -inner))), scaled(u, 2 * inner),
                 scaled(v, 2 * inner), n, {250, 248, 232}, 0, 1, -.006, cell);
        } else {
            const bool hot = cell == hover;
            quad(origin, scaled(u, 2 * half), scaled(v, 2 * half), n,
                 hot ? PixelColor{255, 236, 170} : PixelColor{190, 226, 222}, hot ? .55 : .86, 3, 0,
                 cell);
        }
    }
    // Glass body under the tiles, drawn after them so hidden pixels fail the depth test early.
    for (int face : {0, 3, 4}) {
        if (!visible[face])
            continue;
        Point3 n = face_normal(face), u, v;
        face_axes(face, u, v);
        const double facing = std::max(0.0, -rotate(n, yaw, pitch).z);
        PixelColor body = shade({16, 44, 50}, .7 + .5 * facing);
        quad(add(n, add(scaled(u, -1), scaled(v, -1))), scaled(u, 2), scaled(v, 2), n, body, 0, 1,
             .02, -1);
    }
    // Bright bevels along the visible edges of the block.
    const double edge_width = std::max(1.0, scale * .006);
    for (int face : {0, 3, 4}) {
        if (!visible[face])
            continue;
        Point3 n = face_normal(face), u, v;
        face_axes(face, u, v);
        Point3 corners[4] = {
            add(n, add(scaled(u, -1), scaled(v, -1))), add(n, add(scaled(u, 1), scaled(v, -1))),
            add(n, add(scaled(u, 1), scaled(v, 1))), add(n, add(scaled(u, -1), scaled(v, 1)))};
        for (int k = 0; k < 4; ++k) {
            Projected a = project(corners[k]), b = project(corners[(k + 1) % 4]);
            line({a.x, a.y}, {b.x, b.y}, edge_width, {214, 240, 236}, -1, a.depth - .01,
                 b.depth - .01);
        }
    }
    // Paths ride on the surface; a step between faces folds through the shared edge.
    // The visible surface of a convex block never hides itself, so strokes are painted
    // in order (every rim, then every fill) rather than depth tested.
    const double thickness = std::max(3.0, scale * .042), front = -1e9;
    for (int pass = 0; pass < 2; ++pass)
        for (int pair = 0; pair < game.cube_pairs(); ++pair) {
            const std::vector<int>& path = game.state.paths[pair];
            const PixelColor color = pass ? pair_color(pair + 1) : shade(pair_color(pair + 1), .5);
            const double width = pass ? thickness : thickness * 1.3;
            struct CubeSegment {
                PuzzleRaster& raster;
                const ProjectCube& project;
                double width;
                PixelColor color;
                double front;
                void operator()(Point3 a3, Point3 b3, int id) const {
                    Projected a = project(a3), b = project(b3);
                    raster.line({a.x, a.y}, {b.x, b.y}, width, color, id, front, front);
                }
            };
            CubeSegment segment{*this, project, width, color, front};
            for (std::size_t j = 0; j < path.size(); ++j) {
                const int b = path[j];
                if (!visible[b / 16])
                    continue;
                const Point3 cb = PuzzleGame::cube_center(b);
                Projected pb = project(cb);
                disc({pb.x, pb.y}, width * .5, color, b, front);
                if (j == 0)
                    continue;
                const int a = path[j - 1];
                const Point3 ca = PuzzleGame::cube_center(a);
                if (a / 16 == b / 16) {
                    if (visible[a / 16])
                        segment(ca, cb, b);
                } else {
                    const Point3 nb = face_normal(b / 16);
                    const Point3 fold = add(ca, scaled(nb, 1 - dot3(ca, nb)));
                    Projected pf = project(fold);
                    if (visible[a / 16])
                        segment(ca, fold, b);
                    segment(fold, cb, b);
                    disc({pf.x, pf.y}, width * .5, color, b, front);
                }
            }
        }
}
} // namespace games
