// The room the stones lie in, path traced once: the card tables' sunlit room
// (authoring/table_light/trace.cpp, borrowed here with its walls, glass, sky and ceiling
// lights unchanged), with the cloth replaced by the dark wood of the tray. What a polished
// stone shows is the light arriving at it from every direction, so this bakes, at the
// tray's centre:
//
//   mirror   the light arriving from each direction, an octahedral map 128 x 128;
//   gloss    the same blurred for a glossy polish (a cosine lobe of power 90), 32 x 32;
//   satin    and for a satin one (power 14), 16 x 16;
//   diffuse  the irradiance a surface facing each direction receives, 16 x 16;
//   sun      the sun's direction and its irradiance face on, gathered apart because its
//            disc is far smaller than a texel.
//
// Directions are the tray's: x to the right, y away from the player (up the screen), z up.
// The room's own y runs towards the player, so it is turned round on the way out.
//
//   ts_bake_room <sun|window> <samples per texel> <out.cpp>
//
// "window" moves the side glass back, as the card tables' TABLE_ROOM=window does: the sun
// then falls behind the player and the tray is lit by the glass ahead and the room.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr double pi = 3.14159265358979323846;

struct V3 {
    double x = 0;
    double y = 0;
    double z = 0;
};
V3 operator+(V3 a, V3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
V3 operator-(V3 a, V3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
V3 operator*(V3 a, double s) {
    return {a.x * s, a.y * s, a.z * s};
}
V3 operator*(V3 a, V3 b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}
double dot(V3 a, V3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
V3 cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
V3 unit(V3 v) {
    const double length = std::sqrt(dot(v, v));
    return v * (1 / length);
}
double axis(V3 v, int k) {
    if (k == 0)
        return v.x;
    if (k == 1)
        return v.y;
    return v.z;
}

struct Random {
    std::uint64_t s;
    double next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return static_cast<double>(s >> 11) * (1.0 / 9007199254740992.0);
    }
};

// An axis-aligned rectangle, lit from both sides; `emit` glows only downwards.
struct Rect {
    int k;
    double at;
    double lo[2];
    double hi[2];
    V3 albedo;
    V3 emit;
};

struct Hit {
    double t;
    const Rect* rect;
};

constexpr double wall_x = -1.6, far_x = 6, back_y = -2.4, front_y = 7, floor_z = -.75,
                 ceiling_z = 3.25;
constexpr double glass_x0 = -1.4, glass_x1 = 5.4, glass_z0 = -.55, glass_z1 = 3.05;
constexpr double sun_up = 32 * pi / 180, sun_round = 80 * pi / 180;
constexpr double side_y1 = 4.5;
double side_y0 = -1.8;
constexpr double sun_radius = .267 * pi / 180;
const V3 sun_light{1.0, .96, .90};
const V3 sky_tint{.55, .70, 1.0};
const V3 lamp_glow = V3{1.0, .80, .58} * .025;
constexpr double lamp_side = .6, lamp_spacing = 2.4;

V3 sun_direction() {
    return {-std::cos(sun_up) * std::sin(sun_round), -std::cos(sun_up) * std::cos(sun_round),
            std::sin(sun_up)};
}

struct Scene {
    std::vector<Rect> rects;

    Scene(double w, double d) {
        const V3 plaster{.62, .58, .52};
        const V3 ceiling{.75, .75, .73};
        const V3 boards{.22, .14, .09};
        const V3 tray{.075, .045, .03};
        const V3 wood{.18, .10, .06};
        const V3 none{};
        rects.push_back({2, floor_z, {wall_x, back_y}, {far_x, front_y}, boards, none});
        rects.push_back({2, ceiling_z, {wall_x, back_y}, {far_x, front_y}, ceiling, none});
        rects.push_back({0, far_x, {back_y, floor_z}, {front_y, ceiling_z}, plaster, none});
        rects.push_back({0, wall_x, {back_y, floor_z}, {front_y, glass_z0}, plaster, none});
        rects.push_back({0, wall_x, {back_y, glass_z1}, {front_y, ceiling_z}, plaster, none});
        rects.push_back({0, wall_x, {back_y, glass_z0}, {side_y0, glass_z1}, plaster, none});
        rects.push_back({0, wall_x, {side_y1, glass_z0}, {front_y, glass_z1}, plaster, none});
        rects.push_back({1, front_y, {wall_x, floor_z}, {far_x, ceiling_z}, plaster, none});
        rects.push_back({1, back_y, {wall_x, floor_z}, {far_x, glass_z0}, plaster, none});
        rects.push_back({1, back_y, {wall_x, glass_z1}, {far_x, ceiling_z}, plaster, none});
        rects.push_back({1, back_y, {wall_x, glass_z0}, {glass_x0, glass_z1}, plaster, none});
        rects.push_back({1, back_y, {glass_x1, glass_z0}, {far_x, glass_z1}, plaster, none});
        const V3 frame{.70, .69, .66};
        for (double x = glass_x0 + 1.5; x < glass_x1 - .1; x += 1.5)
            rects.push_back({1, back_y + .01, {x - .06, glass_z0}, {x + .06, glass_z1}, frame, none});
        rects.push_back({1, back_y + .01, {glass_x0, 2.0}, {glass_x1, 2.1}, frame, none});
        // The table under the tray, and the tray's dark wood across the middle of it.
        const double x0 = -.45, x1 = w + .45, y0 = -.5, y1 = d + .25, under = -.04;
        rects.push_back({2, 0, {x0, y0}, {x1, y1}, tray, none});
        rects.push_back({2, under, {x0, y0}, {x1, y1}, wood, none});
        rects.push_back({0, x0, {y0, under}, {y1, 0}, wood, none});
        rects.push_back({0, x1, {y0, under}, {y1, 0}, wood, none});
        rects.push_back({1, y0, {x0, under}, {x1, 0}, wood, none});
        rects.push_back({1, y1, {x0, under}, {x1, 0}, wood, none});
        for (double x = wall_x + .8; x + lamp_side < far_x; x += lamp_spacing)
            for (double y = back_y + .9; y + lamp_side < front_y; y += lamp_spacing)
                rects.push_back({2, ceiling_z - .005, {x, y}, {x + lamp_side, y + lamp_side},
                                 V3{.8, .8, .8}, lamp_glow});
    }

    bool hit(V3 o, V3 d, double t_max, Hit& out) const {
        out.t = t_max;
        out.rect = nullptr;
        for (const Rect& r : rects) {
            const double dk = axis(d, r.k);
            if (std::abs(dk) < 1e-12)
                continue;
            const double t = (r.at - axis(o, r.k)) / dk;
            if (t <= 1e-7 || t >= out.t)
                continue;
            const V3 p = o + d * t;
            const int a = r.k == 0 ? 1 : 0;
            const int b = r.k == 2 ? 1 : 2;
            const double u = axis(p, a);
            const double v = axis(p, b);
            if (u < r.lo[0] || u > r.hi[0] || v < r.lo[1] || v > r.hi[1])
                continue;
            out.t = t;
            out.rect = &r;
        }
        return out.rect != nullptr;
    }

    V3 sky(V3 d) const {
        const V3 s = sun_direction();
        if (d.z < .12) {
            const double lit = (std::sin(sun_up) * .8 + .2) / pi * (d.z < 0 ? .30 : .45);
            return V3{.97, .90, .80} * lit;
        }
        const double horizon = 1 + 2 * std::pow(1 - d.z, 3);
        const double gamma = std::acos(std::clamp(dot(d, s), -1.0, 1.0));
        const double aureole = 1 + 3 * std::exp(-4 * gamma);
        const double white = std::pow(1 - d.z, 2) * .6;
        return (sky_tint * (1 - white) + V3{1, 1, 1} * white) * (.06 * horizon * aureole);
    }

    double sun_seen(V3 p, Random& random, int n) const {
        const V3 s = sun_direction();
        const V3 a = unit(cross(s, V3{0, 0, 1}));
        const V3 b = cross(a, s);
        int seen = 0;
        Hit h;
        for (int i = 0; i < n; ++i) {
            const double r = sun_radius * std::sqrt(random.next());
            const double phi = 2 * pi * random.next();
            const V3 d = unit(s + a * (r * std::cos(phi)) + b * (r * std::sin(phi)));
            if (!hit(p, d, 1e9, h))
                ++seen;
        }
        return static_cast<double>(seen) / n;
    }

    // The light arriving at o from direction d, all but the sun's disc.
    V3 arriving(V3 o, V3 d, Random& random, int depth) const {
        Hit h;
        if (!hit(o, d, 1e9, h))
            return sky(d);
        const Rect& r = *h.rect;
        V3 out{};
        if (d.z > 0 && (r.emit.x > 0 || r.emit.y > 0))
            out = r.emit;
        const V3 p = o + d * h.t;
        V3 n{};
        const double facing = axis(d, r.k) < 0 ? 1 : -1;
        if (r.k == 0)
            n.x = facing;
        else if (r.k == 1)
            n.y = facing;
        else
            n.z = facing;
        const V3 s = sun_direction();
        V3 gathered{};
        const double c = dot(n, s);
        if (c > 0)
            gathered = sun_light * (c * sun_seen(p + n * 1e-6, random, 1));
        double carry = 1;
        if (depth >= 3) {
            carry = std::max({r.albedo.x, r.albedo.y, r.albedo.z});
            if (random.next() > carry)
                return out + r.albedo * gathered * (1 / pi);
        }
        const double u1 = random.next();
        const double u2 = random.next();
        const double rr = std::sqrt(u1);
        const double phi = 2 * pi * u2;
        const V3 t = std::abs(n.x) > .5 ? V3{0, 1, 0} : V3{1, 0, 0};
        const V3 tu = unit(cross(n, t));
        const V3 tv = cross(n, tu);
        const V3 next = unit(tu * (rr * std::cos(phi)) + tv * (rr * std::sin(phi)) +
                             n * std::sqrt(std::max(0.0, 1 - u1)));
        const V3 bounce = arriving(p + n * 1e-6, next, random, depth + 1);
        return out + r.albedo * (gathered * (1 / pi) + bounce * (1 / carry));
    }
};

// The octahedral map: a direction to a point of [0, 1]^2 and back.
void octahedral_point(V3 d, double& u, double& v) {
    const double sum = std::abs(d.x) + std::abs(d.y) + std::abs(d.z);
    double px = d.x / sum;
    double py = d.y / sum;
    if (d.z < 0) {
        const double fx = (1 - std::abs(py)) * (px >= 0 ? 1 : -1);
        const double fy = (1 - std::abs(px)) * (py >= 0 ? 1 : -1);
        px = fx;
        py = fy;
    }
    u = px * .5 + .5;
    v = py * .5 + .5;
}

V3 octahedral_direction(double u, double v) {
    double px = u * 2 - 1;
    double py = v * 2 - 1;
    const double pz = 1 - std::abs(px) - std::abs(py);
    if (pz < 0) {
        const double fx = (1 - std::abs(py)) * (px >= 0 ? 1 : -1);
        const double fy = (1 - std::abs(px)) * (py >= 0 ? 1 : -1);
        px = fx;
        py = fy;
    }
    return unit(V3{px, py, pz});
}

// The tray's directions to the room's: the room's y runs towards the player.
V3 room_from_tray(V3 d) {
    return {d.x, -d.y, d.z};
}

struct Map {
    int side = 0;
    std::vector<V3> texels;  // side x side, row-major, v down the rows
};

Map trace_mirror(const Scene& scene, V3 at, int side, int samples) {
    Map map;
    map.side = side;
    map.texels.assign(static_cast<std::size_t>(side * side), V3{});
    for (int row = 0; row < side; ++row) {
        for (int column = 0; column < side; ++column) {
            Random random{0x9e3779b97f4a7c15ULL ^ (static_cast<std::uint64_t>(row) << 32) ^
                          static_cast<std::uint64_t>(column + 1) * 0xbf58476d1ce4e5b9ULL};
            V3 sum{};
            for (int s = 0; s < samples; ++s) {
                const double u = (column + random.next()) / side;
                const double v = (row + random.next()) / side;
                const V3 d = room_from_tray(octahedral_direction(u, v));
                sum = sum + scene.arriving(at, d, random, 0);
            }
            map.texels[static_cast<std::size_t>(row * side + column)] = sum * (1.0 / samples);
        }
        if (row % 16 == 15)
            std::fprintf(stderr, "  traced %d of %d rows\n", row + 1, side);
    }
    return map;
}

// Every texel's direction and solid angle, for the blurs.
struct Texel {
    V3 direction;
    double weight;
    V3 light;
};

std::vector<Texel> texels_of(const Map& map) {
    std::vector<Texel> out;
    const int side = map.side;
    for (int row = 0; row < side; ++row)
        for (int column = 0; column < side; ++column) {
            const double u = (column + .5) / side;
            const double v = (row + .5) / side;
            const V3 d = octahedral_direction(u, v);
            // The solid angle by finite difference of neighbouring directions.
            const V3 du = octahedral_direction(u + .5 / side, v) - octahedral_direction(u - .5 / side, v);
            const V3 dv = octahedral_direction(u, v + .5 / side) - octahedral_direction(u, v - .5 / side);
            const double area = std::sqrt(dot(cross(du, dv), cross(du, dv)));
            out.push_back({d, area, map.texels[static_cast<std::size_t>(row * side + column)]});
        }
    return out;
}

// A blur of the mirror by a lobe cos^power about each direction (power 1: the irradiance
// of a surface facing it, over pi).
Map blur(const std::vector<Texel>& source, int side, double power) {
    Map map;
    map.side = side;
    map.texels.assign(static_cast<std::size_t>(side * side), V3{});
    for (int row = 0; row < side; ++row)
        for (int column = 0; column < side; ++column) {
            const V3 r = octahedral_direction((column + .5) / side, (row + .5) / side);
            V3 sum{};
            double total = 0;
            for (const Texel& texel : source) {
                const double c = dot(texel.direction, r);
                if (c <= 0)
                    continue;
                const double w = std::pow(c, power) * texel.weight;
                sum = sum + texel.light * w;
                total += w;
            }
            map.texels[static_cast<std::size_t>(row * side + column)] = sum * (1 / total);
        }
    return map;
}

void write_map(std::FILE* f, const char* name, const Map& map) {
    std::fprintf(f, "const float %s[%d] = {\n", name, map.side * map.side * 3);
    int on_line = 0;
    for (const V3& t : map.texels) {
        const double values[3] = {t.x, t.y, t.z};
        for (double value : values) {
            if (on_line == 0)
                std::fprintf(f, "   ");
            std::fprintf(f, " %.5ff,", value);
            ++on_line;
            if (on_line == 10) {
                std::fprintf(f, "\n");
                on_line = 0;
            }
        }
    }
    if (on_line != 0)
        std::fprintf(f, "\n");
    std::fprintf(f, "};\n\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "ts_bake_room <sun|window> <samples per texel> <out.cpp>\n");
        return 2;
    }
    const std::string room = argv[1];
    const int samples = std::max(1, std::atoi(argv[2]));
    if (room == "window")
        side_y0 = 1.0;
    // The card tables' patch is 1.2 m across and 1 m deep; the tray lies at its middle,
    // its stones a centimetre and a half above the wood.
    const Scene scene(1.2, 1.0);
    const V3 at{.6, .5, .015};
    std::fprintf(stderr, "tracing the %s room, %d samples a texel\n", room.c_str(), samples);
    const Map mirror = trace_mirror(scene, at, 128, samples);
    const std::vector<Texel> source = texels_of(mirror);
    const Map gloss = blur(source, 32, 90);
    const Map satin = blur(source, 16, 14);
    const Map diffuse = blur(source, 16, 1);
    // The sun, in the tray's directions, and how much of its disc the tray sees.
    Random random{12345};
    const double seen = scene.sun_seen(at, random, 4096);
    const V3 sun = sun_direction();
    std::FILE* f = std::fopen(argv[3], "w");
    if (f == nullptr) {
        std::perror(argv[3]);
        return 1;
    }
    std::fprintf(f, "// GENERATED by games/tumbledstones/tools/bake_room.cpp (%s room, %d samples a\n",
                 room.c_str(), samples);
    std::fprintf(f, "// texel) - do not edit by hand. The light arriving at the tray: see room.hpp.\n");
    std::fprintf(f, "#include \"room.hpp\"\n\nnamespace ts {\n\n");
    write_map(f, "room_mirror_texels", mirror);
    write_map(f, "room_gloss_texels", gloss);
    write_map(f, "room_satin_texels", satin);
    write_map(f, "room_diffuse_texels", diffuse);
    std::fprintf(f, "const float room_sun_bake[7] = {%.6ff, %.6ff, %.6ff, %.6ff, %.6ff, %.6ff, %.6ff};\n\n",
                 sun.x, -sun.y, sun.z, sun_light.x, sun_light.y, sun_light.z, seen);
    std::fprintf(f, "}  // namespace ts\n");
    std::fclose(f);
    std::fprintf(stderr, "sun seen %.3f; wrote %s\n", seen, argv[3]);
    return 0;
}
