#include "scene.hpp"

#include "r2d.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace ps_cube {
namespace {

namespace r3d = render::r3d;

constexpr double tau = 6.283185307179586;

Point3 add(Point3 a, Point3 b) {
    const Point3 sum{a.x + b.x, a.y + b.y, a.z + b.z};
    return sum;
}

Point3 scaled(Point3 a, double k) {
    const Point3 product{a.x * k, a.y * k, a.z * k};
    return product;
}

double dot(Point3 a, Point3 b) {
    const double value = a.x * b.x + a.y * b.y + a.z * b.z;
    return value;
}

Rgba shade(Rgba c, double k) {
    const Rgba out{std::clamp(c.r * k, 0.0, 255.0), std::clamp(c.g * k, 0.0, 255.0),
                   std::clamp(c.b * k, 0.0, 255.0), c.a};
    return out;
}

Rgba mix(Rgba a, Rgba b, double t) {
    const Rgba out{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
                   a.a + (b.a - a.a) * t};
    return out;
}

// All six faces of the block: the three playable ones first, then their opposites.
Point3 body_normal(int face) {
    if (face < faces) {
        return face_normal(face);
    }
    const Point3 n = face_normal(face - faces);
    return scaled(n, -1);
}

void body_axes(int face, Point3& u, Point3& v) {
    if (face < faces) {
        face_axes(face, u, v);
        return;
    }
    face_axes(face - faces, u, v);
    u = scaled(u, -1);
}

struct Projected {
    double x = 0;
    double y = 0;
    double depth = 0;
};

struct Camera {
    double yaw = 0;
    double pitch = 0;
    double size = 1;     // cube scale
    double focal = 1;    // pixels
    double cx = 0;
    double cy = 0;

    double cos_yaw = 1;
    double sin_yaw = 0;
    double cos_pitch = 1;
    double sin_pitch = 0;

    Point3 rotate(Point3 p) const {
        const double x = p.x * cos_yaw + p.z * sin_yaw;
        const double z = -p.x * sin_yaw + p.z * cos_yaw;
        const Point3 out{x, p.y * cos_pitch - z * sin_pitch, p.y * sin_pitch + z * cos_pitch};
        return out;
    }
    Projected project(Point3 world) const {
        const Point3 p = scaled(rotate(world), size);
        const Projected out{cx + p.x * focal / (5 - p.z), cy + p.y * focal / (5 - p.z), -p.z};
        return out;
    }
    bool facing(Point3 normal) const {
        const Point3 n = rotate(normal);
        return 5 * n.z - size > 0;
    }
    // Where the mirror at a surface point sees the panorama.
    void reflect(Point3 world, Point3 normal, double& u, double& v) const {
        const Point3 p = scaled(rotate(world), size);
        const Point3 n = rotate(normal);
        Point3 view{p.x, p.y, p.z - 5};
        view = scaled(view, 1 / std::sqrt(dot(view, view)));
        const double d = dot(view, n);
        const Point3 r{view.x - 2 * d * n.x, view.y - 2 * d * n.y, view.z - 2 * d * n.z};
        u = .5 + std::atan2(r.x, r.z) / tau;
        v = .5 - std::asin(std::clamp(r.y, -1.0, 1.0)) / 3.141592653589793;
    }
};

Camera camera_for(const Motion& motion, const Box& board) {
    const Pose look = pose(motion);
    Camera camera;
    camera.yaw = look.yaw;
    camera.pitch = look.pitch;
    camera.cos_yaw = std::cos(look.yaw);
    camera.sin_yaw = std::sin(look.yaw);
    camera.cos_pitch = std::cos(look.pitch);
    camera.sin_pitch = std::sin(look.pitch);
    camera.size = look.scale;
    camera.focal = std::min(board.w, board.h) * 1.28;
    camera.cx = board.x + board.w * (.5 + look.drift);
    camera.cy = board.y + board.h * (.5 + look.lift);
    return camera;
}

r3d::Corner corner(const Projected& p, double bias) {
    const r3d::Corner c{static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.depth + bias)};
    return c;
}

render::Color color_of(Rgba c) {
    const render::Color out{static_cast<float>(c.r), static_cast<float>(c.g), static_cast<float>(c.b),
                            static_cast<float>(c.a)};
    return out;
}

bool same(Rgba a, Rgba b) {
    const bool equal = a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    return equal;
}

// One opaque colour.
void flat(Drawing& drawing, r3d::Corner a, r3d::Corner b, r3d::Corner c, Rgba color, int id) {
    const r3d::Flat shader{drawing.pass.target.color(color_of(color))};
    r3d::triangle(drawing.pass, a, b, c, shader, id);
}

// A colour at the first corner shading to another at the other two.
void fan(Drawing& drawing, r3d::Corner a, r3d::Corner b, r3d::Corner c, Rgba middle, Rgba rim, int id) {
    if (same(middle, rim)) {
        flat(drawing, a, b, c, middle, id);
        return;
    }
    const float ca[3] = {static_cast<float>(middle.r), static_cast<float>(middle.g), static_cast<float>(middle.b)};
    const float cb[3] = {static_cast<float>(rim.r), static_cast<float>(rim.g), static_cast<float>(rim.b)};
    r3d::triangle(drawing.pass, a, b, c, ca, cb, cb, r3d::Smooth{drawing.pass.target.order}, id);
}

// A stroke laid over the surface in submission order, ignoring depth.
void stroke(Drawing& drawing, double ax, double ay, double bx, double by, double thickness, Rgba color, int id) {
    const double length = std::hypot(bx - ax, by - ay);
    if (length < 1e-6) {
        return;
    }
    const double dx = (by - ay) / length * thickness * .5;
    const double dy = -(bx - ax) / length * thickness * .5;
    const r3d::Corner p{static_cast<float>(ax + dx), static_cast<float>(ay + dy), 0};
    const r3d::Corner q{static_cast<float>(bx + dx), static_cast<float>(by + dy), 0};
    const r3d::Corner r{static_cast<float>(bx - dx), static_cast<float>(by - dy), 0};
    const r3d::Corner s{static_cast<float>(ax - dx), static_cast<float>(ay - dy), 0};
    const r3d::Over shader = r3d::Over::make(drawing.pass.target.order, color_of(color));
    r3d::triangle(drawing.pass, p, q, r, shader, id);
    r3d::triangle(drawing.pass, p, r, s, shader, id);
}

// A flat quad on the surface, subdivided so the reflection stays accurate.
void quad(Drawing& drawing, const Camera& camera, Point3 origin, Point3 du, Point3 dv, Point3 normal,
          Rgba tint, double env, int requested_steps, double bias, int id) {
    const bool mirrored = env > 0 && drawing.panorama != nullptr && !drawing.panorama->empty();
    const int steps = drawing.draft ? 1 : requested_steps;
    const auto sharp = mirrored ? r3d::Mirror<false>::make(*drawing.panorama, color_of(tint), static_cast<float>(env))
                                : r3d::Mirror<false>{};
    const auto draft = mirrored ? r3d::Mirror<true>::make(*drawing.panorama, color_of(tint), static_cast<float>(env))
                                : r3d::Mirror<true>{};
    for (int j = 0; j < steps; ++j) {
        for (int i = 0; i < steps; ++i) {
            r3d::Corner corners[4];
            float uv[4][2] = {};
            for (int k = 0; k < 4; ++k) {
                const double a = (i + ((k == 1 || k == 2) ? 1 : 0)) / static_cast<double>(steps);
                const double b = (j + (k >= 2 ? 1 : 0)) / static_cast<double>(steps);
                const Point3 w = add(origin, add(scaled(du, a), scaled(dv, b)));
                corners[k] = corner(camera.project(w), bias);
                if (mirrored) {
                    double u = 0;
                    double v = 0;
                    camera.reflect(w, normal, u, v);
                    uv[k][0] = static_cast<float>(u);
                    uv[k][1] = static_cast<float>(v);
                }
            }
            if (!mirrored) {
                flat(drawing, corners[0], corners[1], corners[2], tint, id);
                flat(drawing, corners[0], corners[2], corners[3], tint, id);
                continue;
            }
            // Keep a quad from spanning the panorama's seam the long way round.
            float lo = 1;
            float hi = 0;
            for (const auto& c : uv) {
                lo = std::min(lo, c[0]);
                hi = std::max(hi, c[0]);
            }
            if (hi - lo > .5F) {
                for (auto& c : uv) {
                    c[0] += c[0] < .5F ? 1 : 0;
                }
            }
            if (drawing.draft) {
                r3d::triangle(drawing.pass, corners[0], corners[1], corners[2], uv[0], uv[1], uv[2], draft, id);
                r3d::triangle(drawing.pass, corners[0], corners[2], corners[3], uv[0], uv[2], uv[3], draft, id);
            } else {
                r3d::triangle(drawing.pass, corners[0], corners[1], corners[2], uv[0], uv[1], uv[2], sharp, id);
                r3d::triangle(drawing.pass, corners[0], corners[2], corners[3], uv[0], uv[2], uv[3], sharp, id);
            }
        }
    }
}

// A filled ring (or disc, when inner is 0) lying on a face, raised by `rise`.
void ring(Drawing& drawing, const Camera& camera, Point3 center, Point3 u, Point3 v, Point3 normal,
          double inner, double outer, double rise, Rgba color, double bias, int id, bool lit_rim = true) {
    const int segments = drawing.draft ? 12 : (inner <= 0 && outer < .06) ? 16 : 28;
    const Point3 lifted = add(center, scaled(normal, rise));
    for (int k = 0; k < segments; ++k) {
        const double a0 = k * tau / segments;
        const double a1 = (k + 1) * tau / segments;
        const Point3 o0 = add(lifted, add(scaled(u, std::cos(a0) * outer), scaled(v, std::sin(a0) * outer)));
        const Point3 o1 = add(lifted, add(scaled(u, std::cos(a1) * outer), scaled(v, std::sin(a1) * outer)));
        const Projected p0 = camera.project(o0);
        const Projected p1 = camera.project(o1);
        // A ring lit from the upper left: brighter where its rim faces the light.
        const double light = lit_rim ? .82 + .18 * std::cos(a0 + 2.3) : 1;
        const Rgba lit = shade(color, light);
        if (inner <= 0) {
            const Projected c = camera.project(lifted);
            fan(drawing, corner(c, bias), corner(p0, bias), corner(p1, bias), color, lit, id);
            continue;
        }
        const Point3 i0 = add(lifted, add(scaled(u, std::cos(a0) * inner), scaled(v, std::sin(a0) * inner)));
        const Point3 i1 = add(lifted, add(scaled(u, std::cos(a1) * inner), scaled(v, std::sin(a1) * inner)));
        const Projected q0 = camera.project(i0);
        const Projected q1 = camera.project(i1);
        const r3d::Corner a = corner(p0, bias);
        const r3d::Corner b = corner(p1, bias);
        const r3d::Corner c = corner(q1, bias);
        const r3d::Corner d = corner(q0, bias);
        flat(drawing, a, b, c, lit, id);
        flat(drawing, a, c, d, lit, id);
    }
}

// One face of a stone block: a quad from four corners, flat-shaded.
void facet(Drawing& drawing, const Camera& camera, const Point3 corners[4], Rgba color, double bias, int id) {
    r3d::Corner p[4];
    for (int k = 0; k < 4; ++k) {
        p[k] = corner(camera.project(corners[k]), bias);
    }
    flat(drawing, p[0], p[1], p[2], color, id);
    flat(drawing, p[0], p[2], p[3], color, id);
}

bool stone_at(const Puzzle& puzzle, int cell) {
    const bool stone = cell >= 0 && puzzle.tiles[static_cast<std::size_t>(cell)] == Tile::stone;
    return stone;
}

// A block of mossy stone standing out of the glass. Neighbouring stones on one face
// join into a wall: the block fills its whole cell and drops no side where another stone
// stands. It rises out of the glass when the board arrives.
void draw_stone(Drawing& drawing, const Camera& camera, const Puzzle& puzzle, Point3 center, Point3 u,
                Point3 v, Point3 n, double half, double rise, int cell) {
    if (rise <= .02) {
        return;
    }
    const int side = puzzle.side;
    const int face = cell / (side * side);
    const int row = (cell % (side * side)) / side;
    const int column = cell % side;
    const double height = half * .42 * rise;
    const Point3 lift = scaled(n, height);
    // Which neighbours on this face are stone too (left, right, up, down in u and v).
    const int steps[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    bool joined[4] = {false, false, false, false};
    for (int k = 0; k < 4; ++k) {
        const int r = row + steps[k][0];
        const int c = column + steps[k][1];
        if (r >= 0 && c >= 0 && r < side && c < side) {
            joined[k] = stone_at(puzzle, face * side * side + r * side + c);
        }
    }
    const double inset = half * .1;
    const double left = joined[0] ? half : half - inset;
    const double right = joined[1] ? half : half - inset;
    const double up = joined[2] ? half : half - inset;
    const double down = joined[3] ? half : half - inset;
    const Point3 base[4] = {add(center, add(scaled(u, -left), scaled(v, -up))),
                            add(center, add(scaled(u, right), scaled(v, -up))),
                            add(center, add(scaled(u, right), scaled(v, down))),
                            add(center, add(scaled(u, -left), scaled(v, down)))};
    Point3 top[4];
    for (int k = 0; k < 4; ++k) {
        top[k] = add(base[k], lift);
    }
    // Weathered colour: each block a little different, moss on some.
    const double grain = .5 + .5 * std::sin(cell * 12.9898 + face * 4.1);
    const Rgba granite{118 + 22 * grain, 124 + 18 * grain, 112 + 10 * grain};
    const Rgba moss{82 + 20 * grain, 128 + 22 * grain, 70 + 8 * grain};
    const Rgba crown = mix(granite, moss, grain > .55 ? .65 : .2);
    const int sides_of[4][2] = {{3, 0}, {1, 2}, {0, 1}, {2, 3}};  // corner pairs: left, right, up, down
    const double light[4] = {.62, .88, 1.0, .5};
    for (int k = 0; k < 4; ++k) {
        if (joined[k]) {
            continue;
        }
        const int i = sides_of[k][0];
        const int j = sides_of[k][1];
        const Point3 wall[4] = {base[i], base[j], top[j], top[i]};
        facet(drawing, camera, wall, shade(granite, light[k] * .8), -.002, cell);
    }
    facet(drawing, camera, top, crown, -.002, cell);
    // A worn bevel: a lighter rim on the top's upper and left edges.
    const double bevel = half * .12;
    const Point3 rim_up[4] = {top[0], top[1], add(top[1], scaled(v, bevel)), add(top[0], scaled(v, bevel))};
    const Point3 rim_left[4] = {top[0], add(top[0], scaled(u, bevel)), add(top[3], scaled(u, bevel)), top[3]};
    if (!joined[2]) {
        facet(drawing, camera, rim_up, shade(crown, 1.22), -.004, cell);
    }
    if (!joined[0]) {
        facet(drawing, camera, rim_left, shade(crown, 1.12), -.004, cell);
    }
}

void draw_portal(Drawing& drawing, const Camera& camera, Point3 center, Point3 u, Point3 v, Point3 n,
                 double half, double bloom, int index, int claimed, int cell) {
    // A well in the glass ringed with pale stone: a swirl inside, and one, two or three
    // studs on the rim so a portal can be matched with its partner. A claimed pair takes
    // the colour of the line that runs through it.
    const double size = half * std::clamp(bloom, 0.0, 1.1);
    if (size <= .001) {
        return;
    }
    const Rgba colour = claimed >= 0 ? pair_color(claimed) : Rgba{236, 230, 206};
    const Rgba well = claimed >= 0 ? shade(pair_color(claimed), .32) : Rgba{16, 30, 40};
    const Rgba arm = claimed >= 0 ? mix(pair_color(claimed), Rgba{255, 255, 255}, .35) : Rgba{126, 206, 222};
    ring(drawing, camera, center, u, v, n, 0, size * .7, .003, well, -.004, cell);
    // Three arms of the swirl, each a thin curved strip.
    for (int a = 0; a < 3; ++a) {
        const int steps = 7;
        for (int s = 0; s < steps; ++s) {
            const double t0 = s / static_cast<double>(steps);
            const double t1 = (s + 1) / static_cast<double>(steps);
            const double ang0 = a * tau / 3 + t0 * 2.4;
            const double ang1 = a * tau / 3 + t1 * 2.4;
            const double r0 = size * (.08 + .52 * t0);
            const double r1 = size * (.08 + .52 * t1);
            const double w0 = size * (.03 + .07 * t0);
            const double w1 = size * (.03 + .07 * t1);
            const Point3 lifted = add(center, scaled(n, .004));
            const Point3 c0 = add(lifted, add(scaled(u, std::cos(ang0) * r0), scaled(v, std::sin(ang0) * r0)));
            const Point3 c1 = add(lifted, add(scaled(u, std::cos(ang1) * r1), scaled(v, std::sin(ang1) * r1)));
            const Point3 d0 = add(scaled(u, std::cos(ang0) * w0), scaled(v, std::sin(ang0) * w0));
            const Point3 d1 = add(scaled(u, std::cos(ang1) * w1), scaled(v, std::sin(ang1) * w1));
            const Projected pa = camera.project(add(c0, d0));
            const Projected pb = camera.project(add(c1, d1));
            const Projected pc = camera.project(add(c1, scaled(d1, -1)));
            const Projected pd = camera.project(add(c0, scaled(d0, -1)));
            const Rgba tone = shade(arm, .55 + .45 * t1);
            flat(drawing, corner(pa, -.006), corner(pb, -.006), corner(pc, -.006), tone, cell);
            flat(drawing, corner(pa, -.006), corner(pc, -.006), corner(pd, -.006), tone, cell);
        }
    }
    ring(drawing, camera, center, u, v, n, size * .68, size * .96, .006, colour, -.008, cell);
    const Rgba stud = claimed >= 0 ? Rgba{250, 250, 245} : Rgba{52, 64, 70};
    for (int k = 0; k <= index; ++k) {
        const double angle = -1.5708 + (k - index * .5) * .62;
        const Point3 at = add(center, add(scaled(u, std::cos(angle) * size * .77), scaled(v, std::sin(angle) * size * .77)));
        ring(drawing, camera, at, u, v, n, 0, size * .085, .008, stud, -.012, cell);
    }
}

}  // namespace

Rgba pair_color(int pair) {
    const Rgba colors[] = {{232, 58, 90},  {52, 170, 255}, {255, 186, 40}, {98, 214, 92},
                           {170, 104, 250}, {40, 222, 208}, {255, 118, 36}, {250, 120, 205},
                           {176, 120, 70}, {235, 235, 240}};
    const int index = std::clamp(pair, 0, 9);
    return colors[index];
}

bool cell_point(const Puzzle& puzzle, const Motion& motion, const Box& board, int cell, double& x, double& y) {
    if (cell < 0 || cell >= puzzle.geometry.cells) {
        return false;
    }
    const Camera camera = camera_for(motion, board);
    const int face = cell / (puzzle.side * puzzle.side);
    if (!camera.facing(face_normal(face))) {
        return false;
    }
    const Projected p = camera.project(cell_center(puzzle.side, cell));
    x = p.x;
    y = p.y;
    return true;
}

render::Rect cube_bounds(const Motion& motion, const Box& board) {
    const Pose look = pose(motion);
    if (look.opacity <= 0 || board.w <= 0 || board.h <= 0) {
        return render::Rect{};
    }
    // A box a little larger than the cube holds the standing stones and the lines
    // lifted off the faces; the projection of its corners bounds what lies inside.
    const Camera camera = camera_for(motion, board);
    const double reach = 1.16;
    double left = 1e30;
    double top = 1e30;
    double right = -1e30;
    double bottom = -1e30;
    for (int k = 0; k < 8; ++k) {
        const Point3 c{(k & 1) ? reach : -reach, (k & 2) ? reach : -reach, (k & 4) ? reach : -reach};
        const Projected p = camera.project(c);
        left = std::min(left, p.x);
        top = std::min(top, p.y);
        right = std::max(right, p.x);
        bottom = std::max(bottom, p.y);
    }
    const double bevel = std::max(1.0, camera.focal * camera.size * .006);
    return render::pixel_bounds(left, top, right, bottom).inflated(static_cast<int>(std::ceil(bevel)) + 2);
}

render::Rect cell_bounds(const Puzzle& puzzle, const Motion& motion, const Box& board, const std::vector<int>& cells) {
    const Camera camera = camera_for(motion, board);
    const int side = puzzle.side;
    const double half = 1.0 / side;
    render::Rect bounds;
    for (const int cell : cells) {
        if (cell < 0 || cell >= puzzle.geometry.cells) {
            continue;
        }
        const int face = cell / (side * side);
        const Point3 n = face_normal(face);
        Point3 u;
        Point3 v;
        face_axes(face, u, v);
        const Point3 center = cell_center(side, cell);
        // The cell's square, from the glass up to the top of a standing stone.
        double left = 1e30;
        double top = 1e30;
        double right = -1e30;
        double bottom = -1e30;
        for (int k = 0; k < 8; ++k) {
            const Point3 across = add(scaled(u, (k & 1) ? half : -half), scaled(v, (k & 2) ? half : -half));
            const Projected p = camera.project(add(add(center, across), scaled(n, (k & 4) ? half * .45 : 0)));
            left = std::min(left, p.x);
            top = std::min(top, p.y);
            right = std::max(right, p.x);
            bottom = std::max(bottom, p.y);
        }
        bounds = bounds.united(render::pixel_bounds(left, top, right, bottom));
    }
    // The block's bevels run over cells at its edges.
    const double bevel = std::max(1.0, camera.focal * camera.size * .006);
    return bounds.inflated(static_cast<int>(std::ceil(bevel)) + 2);
}

int pick(const r3d::Buffers& buffers, const Box& board, double x, double y) {
    if (!buffers.has_ids() || x < 0 || y < 0 || x >= buffers.width() || y >= buffers.height()) {
        return -1;
    }
    const int px = static_cast<int>(x);
    const int py = static_cast<int>(y);
    const int direct = buffers.id_at(px, py);
    if (direct >= 0) {
        return direct;
    }
    const int reach = std::max(2, static_cast<int>(board.w / 90));
    int best = -1;
    int best_distance = reach * reach + 1;
    for (int dy = -reach; dy <= reach; ++dy) {
        for (int dx = -reach; dx <= reach; ++dx) {
            const int id = buffers.id_at(px + dx, py + dy);
            if (id >= 0 && dx * dx + dy * dy < best_distance) {
                best = id;
                best_distance = dx * dx + dy * dy;
            }
        }
    }
    return best;
}

void draw_cube(Drawing& drawing, const Puzzle& puzzle, const Play& play, const Motion& motion, int hover) {
    const Pose look = pose(motion);
    if (look.opacity <= 0 || puzzle.tiles.empty()) {
        return;
    }
    const Camera camera = camera_for(motion, drawing.board);
    const int side = puzzle.side;
    const double half = 1.0 / side;          // half a cell, in cube units
    const double tile = half * .9;            // half a tile; the rest is grout
    std::array<bool, 6> visible{};
    for (int face = 0; face < 6; ++face) {
        visible[static_cast<std::size_t>(face)] = camera.facing(body_normal(face));
    }
    // Tiles.
    for (int cell = 0; cell < puzzle.geometry.cells; ++cell) {
        const int face = cell / (side * side);
        if (!visible[static_cast<std::size_t>(face)]) {
            continue;
        }
        const Point3 n = face_normal(face);
        Point3 u;
        Point3 v;
        face_axes(face, u, v);
        const Point3 center = cell_center(side, cell);
        const Point3 origin = add(center, add(scaled(u, -tile), scaled(v, -tile)));
        const Tile kind = puzzle.tiles[static_cast<std::size_t>(cell)];
        const bool hot = cell == hover;
        // Each cell nearest first: what stands on a tile hides it before it is shaded.
        if (kind == Tile::stone) {
            draw_stone(drawing, camera, puzzle, center, u, v, n, half, stone_rise(motion, puzzle, cell), cell);
            quad(drawing, camera, origin, scaled(u, 2 * tile), scaled(v, 2 * tile), n, Rgba{30, 40, 32}, .1, 1, 0,
                 cell);
            continue;
        }
        if (kind == Tile::endpoint) {
            const int pair = puzzle.pair_of[static_cast<std::size_t>(cell)];
            const double bloom = std::clamp(tile_bloom(motion, puzzle, cell), 0.0, 1.12);
            if (bloom > .01) {
                const double socket = half * .28 * bloom;
                quad(drawing, camera, add(center, add(scaled(u, -socket), scaled(v, -socket))), scaled(u, 2 * socket),
                     scaled(v, 2 * socket), n, Rgba{250, 248, 232}, 0, 1, -.004, cell);
                const double size = tile * bloom;
                const Rgba color = shade(pair_color(pair), hot ? 1.18 : 1.0);
                quad(drawing, camera, add(center, add(scaled(u, -size), scaled(v, -size))), scaled(u, 2 * size),
                     scaled(v, 2 * size), n, color, .2, 3, -.002, cell);
            }
            quad(drawing, camera, origin, scaled(u, 2 * tile), scaled(v, 2 * tile), n, Rgba{190, 226, 222}, .86, 3,
                 .002, cell);
            continue;
        }
        if (kind == Tile::portal) {
            const int portal = puzzle.portal_of[static_cast<std::size_t>(cell)];
            draw_portal(drawing, camera, center, u, v, n, half, tile_bloom(motion, puzzle, cell), portal,
                        portal_owner(puzzle, play, portal), cell);
            quad(drawing, camera, origin, scaled(u, 2 * tile), scaled(v, 2 * tile), n,
                 hot ? Rgba{150, 190, 196} : Rgba{70, 104, 112}, .45, 3, 0, cell);
            continue;
        }
        quad(drawing, camera, origin, scaled(u, 2 * tile), scaled(v, 2 * tile), n,
             hot ? Rgba{255, 236, 170} : Rgba{190, 226, 222}, hot ? .55 : .86, 3, 0, cell);
    }
    // The glass body under the tiles, and the blank faces seen while the cube turns.
    for (int face = 0; face < 6; ++face) {
        if (!visible[static_cast<std::size_t>(face)]) {
            continue;
        }
        const Point3 n = body_normal(face);
        Point3 u;
        Point3 v;
        body_axes(face, u, v);
        if (face >= faces) {
            quad(drawing, camera, add(n, add(scaled(u, -1), scaled(v, -1))), scaled(u, 2), scaled(v, 2), n,
                 Rgba{30, 70, 76}, .38, 4, .02, -1);
            continue;
        }
        // A playable face's glass shows only in the grout: every cell lays a tile over the
        // rest. The strips reach a little under the tiles so no seam opens between them.
        const double facing = std::max(0.0, camera.rotate(n).z);
        const Rgba body = shade(Rgba{16, 44, 50}, .7 + .5 * facing);
        const double gap = (half - tile) * 1.3;
        for (int line = 0; line <= side; ++line) {
            const double at = -1 + 2 * half * line;
            const double g0 = std::max(-1.0, at - gap);
            const double g1 = std::min(1.0, at + gap);
            quad(drawing, camera, add(n, add(scaled(u, g0), scaled(v, -1))), scaled(u, g1 - g0), scaled(v, 2), n, body,
                 0, 1, .02, -1);
            quad(drawing, camera, add(n, add(scaled(u, -1), scaled(v, g0))), scaled(u, 2), scaled(v, g1 - g0), n, body,
                 0, 1, .02, -1);
        }
    }
    // Bright bevels along the visible edges of the block.
    const double focal_scale = camera.focal * camera.size;
    const double bevel = std::max(1.0, focal_scale * .006);
    for (int face = 0; face < 6; ++face) {
        if (!visible[static_cast<std::size_t>(face)]) {
            continue;
        }
        const Point3 n = body_normal(face);
        Point3 u;
        Point3 v;
        body_axes(face, u, v);
        const Point3 corners[4] = {add(n, add(scaled(u, -1), scaled(v, -1))), add(n, add(scaled(u, 1), scaled(v, -1))),
                                   add(n, add(scaled(u, 1), scaled(v, 1))), add(n, add(scaled(u, -1), scaled(v, 1)))};
        for (int k = 0; k < 4; ++k) {
            const Projected a = camera.project(corners[k]);
            const Projected b = camera.project(corners[(k + 1) % 4]);
            stroke(drawing, a.x, a.y, b.x, b.y, bevel, Rgba{214, 240, 236, 235}, -1);
        }
    }
    // The lines ride on the surface; a step between faces folds through the shared edge,
    // and a hop through a portal leaves a gap between the two wells. They are laid in the
    // faces' own planes, so stone blocks beside them hide them where they should.
    const double width3d = half * .82;
    // The fill (nearer) before the dark edge under it.
    for (int pass = 1; pass >= 0; --pass) {
        for (std::size_t pair = 0; pair < play.paths.size(); ++pair) {
            const std::vector<int>& path = play.paths[pair];
            const int count = static_cast<int>(path.size());
            if (count == 0) {
                continue;
            }
            const double grow = growth(motion, static_cast<int>(pair));
            int last_step = count - 1;
            if (count >= 2 && !adjacent(puzzle.geometry, path[static_cast<std::size_t>(count - 2)],
                                        path[static_cast<std::size_t>(count - 1)])) {
                last_step = count - 2;  // the newest step went into a portal
            }
            for (int j = 0; j < count; ++j) {
                const int b = path[static_cast<std::size_t>(j)];
                const int face_b = b / (side * side);
                const double along = count > 1 ? j / static_cast<double>(count - 1) : 0;
                const double glow = line_glow(motion, static_cast<int>(pair), along);
                const Rgba base = pair_color(static_cast<int>(pair));
                const Rgba fill = mix(base, Rgba{255, 255, 250}, glow * .62);
                const Rgba color = pass == 0 ? shade(base, .45) : fill;
                const double radius = (pass == 0 ? width3d * .62 : width3d * .5) * (1 + .3 * glow);
                const double bias = pass == 0 ? -.004 : -.014;
                const Point3 cb = cell_center(side, b);
                const bool partial = j == last_step && grow < 1 && j > 0;
                const bool after_partial = j > last_step && grow < 1;
                Point3 ub;
                Point3 vb;
                face_axes(face_b, ub, vb);
                const Point3 nb = face_normal(face_b);
                if (after_partial || j == 0 || !adjacent(puzzle.geometry, path[static_cast<std::size_t>(j - 1)], b)) {
                    // A start gets a round end; the far side of a portal shows its well, which
                    // has taken the line's colour, so nothing is drawn over it.
                    if (j == 0 && visible[static_cast<std::size_t>(face_b)]) {
                        ring(drawing, camera, cb, ub, vb, nb, 0, radius, .004, color, bias, b, false);
                    }
                    continue;
                }
                const int a = path[static_cast<std::size_t>(j - 1)];
                const int face_a = a / (side * side);
                const Point3 ca = cell_center(side, a);
                // Points along the step: through the fold when it crosses an edge.
                Point3 points[3] = {ca, cb, cb};
                int faces_of[2] = {face_a, face_b};
                int point_count = 2;
                if (face_a != face_b) {
                    points[1] = add(ca, scaled(nb, 1 - dot(ca, nb)));
                    points[2] = cb;
                    point_count = 3;
                }
                const double reach = partial ? grow * (point_count - 1) : point_count - 1;
                for (int k = 0; k + 1 < point_count; ++k) {
                    if (reach <= k) {
                        break;
                    }
                    const double t = std::min(1.0, reach - k);
                    const int face = point_count == 2 ? face_b : faces_of[k];
                    if (!visible[static_cast<std::size_t>(face)]) {
                        continue;
                    }
                    const Point3 n = face_normal(face);
                    Point3 u;
                    Point3 v;
                    face_axes(face, u, v);
                    Point3 from = points[k];
                    const Point3 delta = add(points[k + 1], scaled(from, -1));
                    Point3 to = add(from, scaled(delta, t));
                    // A line meets a portal at its rim, leaving the well in view.
                    const double step = std::sqrt(dot(delta, delta));
                    const bool into_portal = k + 2 == point_count && t >= 1 &&
                                             puzzle.tiles[static_cast<std::size_t>(b)] == Tile::portal;
                    const bool out_of_portal = k == 0 && puzzle.tiles[static_cast<std::size_t>(a)] == Tile::portal &&
                                               j >= 2 && !adjacent(puzzle.geometry, path[static_cast<std::size_t>(j - 2)], a);
                    if (into_portal && step > 1e-9) {
                        to = add(to, scaled(delta, -half * .78 / step));
                    }
                    if (out_of_portal && step > 1e-9) {
                        from = add(from, scaled(delta, half * .78 / step));
                    }
                    // Across the line, in the face's plane.
                    Point3 across{n.y * delta.z - n.z * delta.y, n.z * delta.x - n.x * delta.z,
                                  n.x * delta.y - n.y * delta.x};
                    const double length = std::sqrt(dot(across, across));
                    if (length < 1e-9) {
                        continue;
                    }
                    across = scaled(across, radius / length);
                    const Point3 lift = scaled(n, .004);
                    const Point3 corners[4] = {add(add(from, across), lift), add(add(to, across), lift),
                                               add(add(to, scaled(across, -1)), lift),
                                               add(add(from, scaled(across, -1)), lift)};
                    facet(drawing, camera, corners, color, bias, b);
                    // No round joint on the fold itself: half of it would hang past the
                    // cube's edge, and the two halves already meet there.
                    const bool at_fold = point_count == 3 && k == 0 && t >= 1;
                    if (!into_portal && !at_fold) {
                        ring(drawing, camera, to, u, v, n, 0, radius, .004, color, bias, b, false);
                    }
                    if (k == 0 && !out_of_portal) {
                        ring(drawing, camera, from, u, v, n, 0, radius, .004, color, bias, b, false);
                    }
                }
            }
        }
    }
}

void show_cube(Drawing& drawing, std::vector<std::uint32_t>& scratch, const Puzzle& puzzle, const Play& play,
               const Motion& motion, int hover) {
    const double opacity = pose(motion).opacity;
    if (opacity >= 1) {
        draw_cube(drawing, puzzle, play, motion, hover);
        return;
    }
    if (opacity <= 0) {
        return;
    }
    const render::Target target = drawing.pass.target;
    const render::Rect area = drawing.pass.clip.intersected(target.bounds());
    const std::size_t count = static_cast<std::size_t>(target.width) * static_cast<std::size_t>(target.height);
    if (scratch.size() != count) {
        scratch.assign(count, 0);
    }
    const render::Target alone{scratch.data(), target.width, target.width, target.height, target.order};
    for (int y = area.y0; y < area.y1; ++y) {
        std::fill(alone.row(y) + area.x0, alone.row(y) + area.x1, 0U);
    }
    drawing.pass.target = alone;
    draw_cube(drawing, puzzle, play, motion, hover);
    drawing.pass.target = target;
    render::r2d::over(target, alone, area, static_cast<float>(opacity));
}

}  // namespace ps_cube
