#include "tray.hpp"

#include "noise.hpp"
#include "room.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace stones {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double socket_half = .476;  // of the pitch: narrow walls between sockets
constexpr double socket_round = .2;
constexpr double cove = .085;         // the wall's run from the edge to the floor
constexpr double lip = .018;          // the rounded top edge

// Signed distance to the socket's rounded square, in pitches: negative inside.
double socket_distance(double x, double y) {
    const double qx = std::abs(x) - (socket_half - socket_round);
    const double qy = std::abs(y) - (socket_half - socket_round);
    const double outside = std::hypot(std::max(qx, 0.0), std::max(qy, 0.0));
    const double inside = std::min(std::max(qx, qy), 0.0);
    return outside + inside - socket_round;
}

// The wood's height about a socket centred at the origin, in pitches (0 the tray's top).
double socket_height(double x, double y) {
    const double d = socket_distance(x, y);
    if (d >= lip)
        return 0;
    if (d > 0) {
        const double t = 1 - d / lip;
        return -.012 * t * t;
    }
    const double t = std::min(1.0, -d / cove);
    const double fall = 1 - (1 - t) * (1 - t);
    return -.012 - (socket_depth - .012) * fall;
}

// Any point's height on an endless grid of sockets.
double grid_height(double x, double y) {
    const double lx = x - std::floor(x + .5);
    const double ly = y - std::floor(y + .5);
    return socket_height(lx, ly);
}

// The room's light and the sun's reaching each point of one socket, worked out once: every
// socket is lit alike.
struct SocketLight {
    static constexpr int side = 72;  // points across a pitch
    std::vector<float> room;         // side x side, the share of the room's light
    std::vector<float> sun;          // the share of the sun's
    SocketLight() {
        room.assign(side * side, 1.0F);
        sun.assign(side * side, 1.0F);
        const std::vector<LightDirection>& lights = room_lights();
        const Sun& s = room_sun();
        const bool has_sun = s.light.r + s.light.g + s.light.b > 0;
        const std::size_t room_count = has_sun ? lights.size() - 1 : lights.size();
        const double step = .012;
        for (int row = 0; row < side; ++row) {
            for (int column = 0; column < side; ++column) {
                const double x = (column + .5) / side - .5;
                const double y = .5 - (row + .5) / side;
                const double h0 = socket_height(x, y);
                if (h0 >= 0)
                    continue;
                const double e = .002;
                const double gx = (socket_height(x + e, y) - socket_height(x - e, y)) / (2 * e);
                const double gy = (socket_height(x, y + e) - socket_height(x, y - e)) / (2 * e);
                const double nl = std::sqrt(gx * gx + gy * gy + 1);
                const double nx = -gx / nl;
                const double ny = -gy / nl;
                const double nz = 1 / nl;
                double lit = 0;
                double total = 0;
                for (std::size_t k = 0; k < lights.size(); ++k) {
                    const LightDirection& l = lights[k];
                    const double facing = std::max(0.0, nx * l.x + ny * l.y + nz * l.z);
                    const bool is_sun = k >= room_count;
                    if (facing <= 0 && !is_sun)
                        continue;
                    const double flat = std::sqrt(l.x * l.x + l.y * l.y);
                    double seen = 1;
                    if (flat > 1e-6) {
                        const double dx = l.x / flat * step;
                        const double dy = l.y / flat * step;
                        const double rise = l.z / flat * step;
                        double px = x;
                        double py = y;
                        double ray = h0 + .002;
                        while (ray < 0) {
                            px += dx;
                            py += dy;
                            ray += rise;
                            if (grid_height(px, py) > ray) {
                                seen = 0;
                                break;
                            }
                        }
                    }
                    if (is_sun) {
                        sun[static_cast<std::size_t>(row * side + column)] = static_cast<float>(seen);
                        continue;
                    }
                    lit += l.weight * facing * seen;
                    total += l.weight * facing;
                }
                room[static_cast<std::size_t>(row * side + column)] = total > 0 ? static_cast<float>(lit / total) : 1.0F;
            }
        }
    }
};

const SocketLight& socket_light() {
    static const SocketLight light;
    return light;
}

float table_at(const std::vector<float>& table, double x, double y) {
    const int side = SocketLight::side;
    const double fx = std::clamp((x + .5) * side - .5, 0.0, side - 1.001);
    const double fy = std::clamp((.5 - y) * side - .5, 0.0, side - 1.001);
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    const float tx = static_cast<float>(fx - x0);
    const float ty = static_cast<float>(fy - y0);
    const std::size_t at = static_cast<std::size_t>(y0 * side + x0);
    const float a = table[at] + (table[at + 1] - table[at]) * tx;
    const float b = table[at + side] + (table[at + side + 1] - table[at + side]) * tx;
    return a + (b - a) * ty;
}

struct Rgb {
    double r = 0, g = 0, b = 0;
};

Rgb from_hex(std::uint32_t rgb) {
    return {std::pow(((rgb >> 16) & 0xffU) / 255.0, 2.2), std::pow(((rgb >> 8) & 0xffU) / 255.0, 2.2),
            std::pow((rgb & 0xffU) / 255.0, 2.2)};
}

Rgb mixed(Rgb a, Rgb b, double t) {
    const double k = std::clamp(t, 0.0, 1.0);
    return {a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k};
}

double smoothstep(double edge0, double edge1, double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

// Dark walnut, its grain running across the tray, at a point in points.
Rgb wood_at(double u, double v) {
    static const Rgb base = from_hex(0x2c1a0f);
    static const Rgb late = from_hex(0x150b06);
    static const Rgb light = from_hex(0x4a2c18);
    const double figure = fbm2(u / 260, v / 70, 71, 3);
    const double rings = v / 7.5 + 2.2 * figure + .25 * noise2(u / 34, v / 9, 72);
    const double r = rings - std::floor(rings);
    const double latewood = smoothstep(.6, .82, r) * (1 - smoothstep(.9, 1.0, r));
    const double fibre = noise2(u / 46, v / .8, 73);
    const double broad = fbm2(u / 400, v / 220, 74, 2);
    Rgb c = mixed(base, late, latewood * .75);
    c = mixed(c, light, std::max(0.0, broad) * .55 + std::max(0.0, fibre) * .18);
    const double k = 1 + .1 * fibre;
    return {c.r * k, c.g * k, c.b * k};
}

double unit_hash(int a, int b, std::uint32_t key) {
    return (hash_signed(a, b, 3, key) + 1) * .5;
}

// Walnut made as a cabinet maker would: boards glued edge to edge, running across, each
// with its own tone and its own grain. Plain-sawn, the face cuts across the growth rings
// at a slant, so they rise into arches along the board; quartersawn, it cuts through
// them, so they run straight. Fine pores lie along the grain, darkest in each year's
// early wood; a board now and then carries a streak of pale sapwood.
//   style 1 oiled black walnut, plain-sawn; 2 quartersawn, darker; 3 lighter, figured.
Rgb walnut_at(double u, double v, int style) {
    const Rgb base = from_hex(style == 3 ? 0x5e3c24 : style == 2 ? 0x2e1c12 : 0x3a2517);
    const Rgb late = from_hex(style == 3 ? 0x34200f : style == 2 ? 0x160c07 : 0x1d110a);
    const Rgb light = from_hex(style == 3 ? 0x8a5e3a : style == 2 ? 0x4a2e1c : 0x5c3c26);
    const Rgb sap = from_hex(0xb8956a);
    const double width = style == 2 ? 78 : 128;
    const double offset = 37;
    const int board = static_cast<int>(std::floor((v + offset) / width));
    const double vb = v + offset - board * width;
    const std::uint32_t key = 911U + static_cast<std::uint32_t>(style);
    const double tone = .82 + .34 * unit_hash(board, 1, key);
    double rings = 0;
    if (style == 2) {
        // Straight stripes, a little wavy, closer here and wider there.
        rings = (vb + 4 * noise2(u / 300, board * 3.1, key + 2)) / (2.6 + 1.4 * unit_hash(board, 2, key)) +
                .6 * fbm2(u / 420, vb / 30, key + 3, 2);
    } else {
        // The pith lies below the face; where the face comes nearest it the rings close
        // into arches, the cathedrals of plain-sawn wood.
        const double centre = width * (.25 + .5 * unit_hash(board, 3, key));
        const double depth = width * (.35 + .4 * unit_hash(board, 4, key)) +
                             width * .3 * std::sin(u / (520 + 380 * unit_hash(board, 5, key)) + 6.28 * unit_hash(board, 6, key)) +
                             18 * fbm2(u / 260, board * 1.7, key + 4, 3);
        const double dv = vb - centre;
        rings = std::sqrt(dv * dv + depth * depth) / (5.5 + 2.5 * unit_hash(board, 7, key)) +
                1.2 * fbm2(u / 220, vb / 45 + board, key + 5, 3);
    }
    const double within = rings - std::floor(rings);
    const double early = 1 - smoothstep(0, .22, within);
    Rgb c = mixed(base, light, smoothstep(.35, .95, within) * .35);
    c = mixed(c, late, early * .5);
    // Long streaks of colour along the board.
    const double streak = fbm2(u / 520, vb / 26 + board * 5.3, key + 6, 3);
    c = mixed(c, late, smoothstep(0, .5, streak) * .35);
    c = mixed(c, light, smoothstep(0, .5, -streak) * .25);
    // Pores: fine dark dashes along the grain, thick in the early wood.
    const double pore = noise2(u / 7.5, v / .55, key + 7);
    if (pore > .55 - .25 * early)
        c = mixed(c, late, .5);
    // A curly figure: bands across the grain that the light picks out.
    if (style == 3) {
        const double curl = std::sin(u / 2.6 + 3 * fbm2(u / 90, v / 40, key + 8, 2));
        const double k = 1 + .09 * curl;
        c = {c.r * k, c.g * k, c.b * k};
    }
    // Pale sapwood at one edge of an odd board.
    if (style == 1 && unit_hash(board, 8, key) > .8) {
        const double sapwood = smoothstep(width * .78, width * .9, vb + 10 * noise2(u / 80, board, key + 9));
        c = mixed(c, sap, sapwood * .8);
    }
    c = {c.r * tone, c.g * tone, c.b * tone};
    // The glue line between boards.
    const double seam = std::min(vb, width - vb);
    if (seam < .7)
        c = {c.r * .35, c.g * .35, c.b * .35};
    return c;
}

// Dark felt lining a socket.
Rgb felt_at(double u, double v) {
    static const Rgb base = from_hex(0x1b2420);
    const double fuzz = noise2(u / 1.1, v / 1.1, 81) * .5 + noise2(u / .45, v / .45, 82) * .5;
    const double k = 1 + .12 * fuzz;
    return {base.r * k, base.g * k, base.b * k};
}

double fresnel(double c) {
    const double m = 1 - std::clamp(c, 0.0, 1.0);
    return .04 + .96 * m * m * m * m * m;
}

}  // namespace

void draw_tray(const TrayPlan& plan, std::uint32_t* pixels, int stride, bool bgra, int row0, int row1) {
    const SocketLight& socket = socket_light();
    const Sun& sun = room_sun();
    const bool has_sun = sun.light.r + sun.light.g + sun.light.b > 0;
    const double expose = exposure();
    const Light3 room_up = room_diffuse(0, 0, 1);
    const double cell = plan.cell;
    const double wall = plan.wall;
    const int red_shift = bgra ? 16 : 0;
    const int blue_shift = bgra ? 0 : 16;
    for (int y = std::max(0, row0); y < std::min(plan.height, row1); ++y) {
        std::uint32_t* row = pixels + static_cast<std::ptrdiff_t>(y) * stride;
        for (int x = 0; x < plan.width; ++x) {
            const double px = x + .5;
            const double py = y + .5;
            // Where in the grid, in pitches; the tray's y runs up the screen.
            const double gx = (px - plan.grid_x) / cell;
            const double gy = (py - plan.grid_y) / cell;
            double nx = 0;
            double ny = 0;
            double nz = 1;
            double room_seen = 1;
            double sun_seen = 1;
            bool felt = false;
            const bool in_grid = gx > -.02 && gy > -.02 && gx < plan.columns + .02 && gy < plan.rows + .02;
            if (in_grid) {
                const double lx = gx - std::floor(gx) - .5;
                const double ly = .5 - (gy - std::floor(gy));
                const double d = socket_distance(lx, ly);
                if (d < lip) {
                    const double e = .5 / cell;
                    const double hx = (socket_height(lx + e, ly) - socket_height(lx - e, ly)) / (2 * e);
                    const double hy = (socket_height(lx, ly + e) - socket_height(lx, ly - e)) / (2 * e);
                    const double nl = std::sqrt(hx * hx + hy * hy + 1);
                    nx = -hx / nl;
                    ny = -hy / nl;
                    nz = 1 / nl;
                    room_seen = table_at(socket.room, lx, ly);
                    sun_seen = table_at(socket.sun, lx, ly);
                    felt = plan.lining == Lining::felt && d < -cove * .7;
                }
            }
            // The raised wall round the picture's edge: a rounded bead of wood.
            if (wall > 0) {
                const double edge = std::min({px, py, plan.width - px, plan.height - py});
                if (edge < wall) {
                    const double t = edge / wall;
                    const double slope = std::cos(t * pi) * .9;
                    const bool horizontal = std::min(px, plan.width - px) < std::min(py, plan.height - py);
                    double sx = 0;
                    double sy = 0;
                    if (horizontal)
                        sx = px < plan.width * .5 ? -slope : slope;
                    else
                        sy = py < plan.height * .5 ? slope : -slope;
                    const double nl = std::sqrt(sx * sx + sy * sy + 1);
                    nx = sx / nl;
                    ny = sy / nl;
                    nz = 1 / nl;
                }
            }
            const double u = px / plan.scale;
            const double v = py / plan.scale;
            const Rgb albedo = felt ? felt_at(u, v) : plan.wood == 0 ? wood_at(u, v) : walnut_at(u, v, plan.wood);
            const Light3 room = (nz > .9999) ? room_up : room_diffuse(nx, ny, nz);
            const double sun_dot = has_sun ? std::max(0.0, nx * sun.x + ny * sun.y + nz * sun.z) : 0.0;
            const double sun_k = sun_dot / pi * sun_seen;
            double out_r = albedo.r * (room.r * room_seen + sun.light.r * sun_k);
            double out_g = albedo.g * (room.g * room_seen + sun.light.g * sun_k);
            double out_b = albedo.b * (room.b * room_seen + sun.light.b * sun_k);
            if (!felt) {
                // The varnish mirrors the room, softly.
                const double nv = std::max(nz, .02);
                const double f = fresnel(nv);
                const Light3 mirrored = room_reflection(2 * nv * nx, 2 * nv * ny, 2 * nv * nz - 1, .3);
                const double seen = .4 + .6 * room_seen;
                out_r += f * mirrored.r * seen;
                out_g += f * mirrored.g * seen;
                out_b += f * mirrored.b * seen;
            }
            const std::uint32_t r = static_cast<std::uint32_t>(std::lround(screen_value(out_r * expose) * 255));
            const std::uint32_t g = static_cast<std::uint32_t>(std::lround(screen_value(out_g * expose) * 255));
            const std::uint32_t b = static_cast<std::uint32_t>(std::lround(screen_value(out_b * expose) * 255));
            row[x] = (r << red_shift) | (g << 8) | (b << blue_shift) | 0xff000000U;
        }
    }
}

}  // namespace stones
