// The card table in a large sunlit room, path traced, to bake the light the table's cards and
// cloth receive. The room never changes, so what reaches the table is worked out once here.
//
// The room, in metres: x across the table (the window's x), y towards the player (the
// window's y), z up, the cloth at z = 0 and the window's top left corner at the origin. The
// player faces a wall of glass 2.4 m beyond the table, which the cards' varnish mirrors, and
// the sun comes in through more glass 1.6 m to the left, 32° up and 80° left of straight ahead,
// throwing shadows right and a little towards the player. Its own mirror image lies well
// off the table, so its glare reaches the cards only as the varnish's faint far reach.
// A grid of ceiling lights 4 m up fills the room; walls, floor and ceiling pass light on.
//
// Two bakes, each a grid of points over the visible cloth (`width` x `depth` metres):
//   cells  everything but the sun's disc falling on the cloth, binned by direction
//          (64 azimuths x 16 cosine-weighted bands), so cards' shadows can be cut from it;
//          also how much of the sun's disc each point sees through the glass.
//   coat   the light the cards' varnish reflects towards the player's eye.
// Output is raw little-endian float32, described on stdout. bake.py does the rest.
//
//   trace cells <width> <depth> <nx> <ny> <samples per bin> <first row> <rows> <out>
//   trace coat  <width> <depth> <nx> <ny> <samples> <first row> <rows> <out>
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
    double x = 0, y = 0, z = 0;
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
    return v * (1 / std::sqrt(dot(v, v)));
}
double axis(V3 v, int k) {
    return k == 0 ? v.x : k == 1 ? v.y : v.z;
}

// A small, fast generator; each point seeds its own so parts can run anywhere.
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
    int k;            // the axis it faces along
    double at;        // its place on that axis
    double lo[2], hi[2];  // its extent on the other two axes, in order
    V3 albedo, emit;
};

struct Hit {
    double t;
    const Rect* rect;
};

// The room.
constexpr double wall_x = -1.6, far_x = 6, back_y = -2.4, front_y = 7, floor_z = -.75,
                 ceiling_z = 3.25;
// The glass: floor to ceiling across the wall ahead, the far wall.
constexpr double glass_x0 = -1.4, glass_x1 = 5.4, glass_z0 = -.55, glass_z1 = 3.05;
// The sun's elevation, and its bearing left of straight ahead.
constexpr double sun_up = 32 * pi / 180, sun_round = 80 * pi / 180;
// The left wall's glass. With TABLE_ROOM=window it starts further back, so the sun falls on
// the floor behind the player and the table is lit by the window ahead and the room.
constexpr double side_y1 = 4.5;
double side_y0 = -1.8;
constexpr double sun_radius = .267 * pi / 180;
const V3 sun_light{1.0, .96, .90};       // irradiance face on: the scale of everything
const V3 sky_tint{.55, .70, 1.0};
const V3 lamp_glow = V3{1.0, .80, .58} * .025;  // a ceiling light's radiance
constexpr double lamp_side = .6, lamp_spacing = 2.4;

V3 sun_direction() {
    return {-std::cos(sun_up) * std::sin(sun_round), -std::cos(sun_up) * std::cos(sun_round),
            std::sin(sun_up)};
}

struct Scene {
    std::vector<Rect> rects;
    double width, depth;

    Scene(double w, double d) : width(w), depth(d) {
        if (const char* room = std::getenv("TABLE_ROOM"); room && std::string(room) == "window")
            side_y0 = 1.0;
        const V3 plaster{.62, .58, .52}, ceiling{.75, .75, .73}, boards{.22, .14, .09},
            felt{.08, .22, .15}, wood{.18, .10, .06};
        const V3 none{};
        rects.push_back({2, floor_z, {wall_x, back_y}, {far_x, front_y}, boards, none});
        rects.push_back({2, ceiling_z, {wall_x, back_y}, {far_x, front_y}, ceiling, none});
        rects.push_back({0, far_x, {back_y, floor_z}, {front_y, ceiling_z}, plaster, none});
        rects.push_back({0, wall_x, {back_y, floor_z}, {front_y, glass_z0}, plaster, none});
        rects.push_back({0, wall_x, {back_y, glass_z1}, {front_y, ceiling_z}, plaster, none});
        rects.push_back({0, wall_x, {back_y, glass_z0}, {side_y0, glass_z1}, plaster, none});
        rects.push_back({0, wall_x, {side_y1, glass_z0}, {front_y, glass_z1}, plaster, none});
        rects.push_back({1, front_y, {wall_x, floor_z}, {far_x, ceiling_z}, plaster, none});
        // The glass wall: solid around its opening.
        rects.push_back({1, back_y, {wall_x, floor_z}, {far_x, glass_z0}, plaster, none});
        rects.push_back({1, back_y, {wall_x, glass_z1}, {far_x, ceiling_z}, plaster, none});
        rects.push_back({1, back_y, {wall_x, glass_z0}, {glass_x0, glass_z1}, plaster, none});
        rects.push_back({1, back_y, {glass_x1, glass_z0}, {far_x, glass_z1}, plaster, none});
        // Its frame: mullions every 1.5 m and a transom, painted off-white.
        const V3 frame{.70, .69, .66};
        for (double x = glass_x0 + 1.5; x < glass_x1 - .1; x += 1.5)
            rects.push_back({1, back_y + .01, {x - .06, glass_z0}, {x + .06, glass_z1}, frame,
                             none});
        rects.push_back({1, back_y + .01, {glass_x0, 2.0}, {glass_x1, 2.1}, frame, none});
        // The table, its top a little larger than what the player sees, its edge 4 cm deep.
        const double x0 = -.45, x1 = w + .45, y0 = -.5, y1 = d + .25, under = -.04;
        rects.push_back({2, 0, {x0, y0}, {x1, y1}, felt, none});
        rects.push_back({2, under, {x0, y0}, {x1, y1}, wood, none});
        rects.push_back({0, x0, {y0, under}, {y1, 0}, wood, none});
        rects.push_back({0, x1, {y0, under}, {y1, 0}, wood, none});
        rects.push_back({1, y0, {x0, under}, {x1, 0}, wood, none});
        rects.push_back({1, y1, {x0, under}, {x1, 0}, wood, none});
        // The ceiling's lights, a grid over the whole room.
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
            const int a = r.k == 0 ? 1 : 0, b = r.k == 2 ? 1 : 2;
            const double u = axis(p, a), v = axis(p, b);
            if (u < r.lo[0] || u > r.hi[0] || v < r.lo[1] || v > r.hi[1])
                continue;
            out.t = t;
            out.rect = &r;
        }
        return out.rect != nullptr;
    }

    // Daylight from outside, seen through the glass: a clear sky brightening towards the
    // horizon and round the sun, and sunlit ground below. Not the sun's own disc.
    V3 sky(V3 d) const {
        const V3 s = sun_direction();
        // Below the horizon, and a little above it, sunlit paving and pale buildings.
        if (d.z < .12) {
            const double lit = (std::sin(sun_up) * .8 + .2) / pi * (d.z < 0 ? .30 : .45);
            return V3{.97, .90, .80} * lit;
        }
        // A hazy afternoon sky, whitening towards the horizon and round the sun.
        const double horizon = 1 + 2 * std::pow(1 - d.z, 3);
        const double gamma = std::acos(std::clamp(dot(d, s), -1.0, 1.0));
        const double aureole = 1 + 3 * std::exp(-4 * gamma);
        const double white = std::pow(1 - d.z, 2) * .6;
        return (sky_tint * (1 - white) + V3{1, 1, 1} * white) * (.06 * horizon * aureole);
    }

    // How much of the sun's disc is seen from p, by `n` rays across it.
    double sun_seen(V3 p, Random& random, int n) const {
        const V3 s = sun_direction();
        const V3 a = unit(cross(s, V3{0, 0, 1})), b = cross(a, s);
        int seen = 0;
        Hit h;
        for (int i = 0; i < n; ++i) {
            const double r = sun_radius * std::sqrt(random.next()), phi = 2 * pi * random.next();
            const V3 d = unit(s + a * (r * std::cos(phi)) + b * (r * std::sin(phi)));
            if (!hit(p, d, 1e9, h))
                ++seen;
        }
        return static_cast<double>(seen) / n;
    }

    // The light arriving at o from direction d: everything except the sun's disc, which
    // each diffuse surface gathers directly instead.
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
        (r.k == 0 ? n.x : r.k == 1 ? n.y : n.z) = axis(d, r.k) < 0 ? 1 : -1;
        const V3 s = sun_direction();
        V3 gathered{};
        if (const double c = dot(n, s); c > 0)
            gathered = sun_light * (c * sun_seen(p + n * 1e-6, random, 1));
        double carry = 1;
        if (depth >= 3) {
            carry = std::max({r.albedo.x, r.albedo.y, r.albedo.z});
            if (random.next() > carry)
                return out + r.albedo * gathered * (1 / pi);
        }
        // Cosine-weighted, the albedo is the whole weight.
        const double u1 = random.next(), u2 = random.next();
        const double rr = std::sqrt(u1), phi = 2 * pi * u2;
        V3 t = std::abs(n.x) > .5 ? V3{0, 1, 0} : V3{1, 0, 0};
        const V3 tu = unit(cross(n, t)), tv = cross(n, tu);
        const V3 next = unit(tu * (rr * std::cos(phi)) + tv * (rr * std::sin(phi)) +
                             n * std::sqrt(std::max(0.0, 1 - u1)));
        const V3 bounce = arriving(p + n * 1e-6, next, random, depth + 1);
        return out + r.albedo * (gathered * (1 / pi) + bounce * (1 / carry));
    }
};

// The varnish: GGX microfacets on a dielectric of index 1.5.
constexpr double coat_alpha = .10, coat_f0 = .04;
double fresnel(double c) {
    return coat_f0 + (1 - coat_f0) * std::pow(1 - std::clamp(c, 0.0, 1.0), 5);
}
double ggx_d(double nh) {
    const double a2 = coat_alpha * coat_alpha, k = nh * nh * (a2 - 1) + 1;
    return a2 / (pi * k * k);
}
double smith_g1(double nv) {
    const double a2 = coat_alpha * coat_alpha;
    return 2 * nv / (nv + std::sqrt(a2 + (1 - a2) * nv * nv));
}

// The player's eyes: centred, 30 cm back from the bottom of the view and 42 cm above it.
V3 eye(double width, double depth) {
    return {width * .5, depth + .30, .42};
}

void write(const std::string& path, const std::vector<float>& data) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f || std::fwrite(data.data(), sizeof(float), data.size(), f) != data.size()) {
        std::perror(path.c_str());
        std::exit(1);
    }
    std::fclose(f);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 10) {
        std::fprintf(stderr, "trace cells|coat width depth nx ny samples first rows out\n");
        return 2;
    }
    const std::string mode = argv[1];
    const double width = std::atof(argv[2]), depth = std::atof(argv[3]);
    const int nx = std::atoi(argv[4]), ny = std::atoi(argv[5]), samples = std::atoi(argv[6]);
    const int first = std::atoi(argv[7]), rows = std::atoi(argv[8]);
    const Scene scene(width, depth);
    std::vector<float> out;
    for (int row = first; row < first + rows && row < ny; ++row)
        for (int column = 0; column < nx; ++column) {
            const V3 p{width * column / (nx - 1), depth * row / (ny - 1), 1e-6};
            Random random{0x9e3779b97f4a7c15ULL ^ (static_cast<std::uint64_t>(row) << 32) ^
                          static_cast<std::uint64_t>(column + 1) * 0xbf58476d1ce4e5b9ULL};
            if (mode == "cells") {
                // 64 azimuths x 16 bands of equal cosine-weighted measure: band j holds
                // directions whose sin² from the zenith lies in [j/16, (j+1)/16).
                for (int a = 0; a < 64; ++a)
                    for (int j = 0; j < 16; ++j) {
                        V3 sum{};
                        for (int s = 0; s < samples; ++s) {
                            const double u1 = (j + random.next()) / 16;
                            const double phi = 2 * pi * (a + random.next()) / 64;
                            const double r = std::sqrt(u1);
                            const V3 d{r * std::cos(phi), r * std::sin(phi),
                                       std::sqrt(1 - u1)};
                            sum = sum + scene.arriving(p, d, random, 0);
                        }
                        out.push_back(static_cast<float>(sum.x / samples));
                        out.push_back(static_cast<float>(sum.y / samples));
                        out.push_back(static_cast<float>(sum.z / samples));
                    }
                out.push_back(static_cast<float>(scene.sun_seen(p, random, 256)));
            } else {
                const V3 v = unit(eye(width, depth) - p);
                const double nv = v.z;
                V3 sum{};
                for (int s = 0; s < samples; ++s) {
                    // Sample the half vector by D(h) cos, reflect the view about it.
                    const double u1 = random.next(), phi = 2 * pi * random.next();
                    const double a2 = coat_alpha * coat_alpha;
                    const double ct = std::sqrt((1 - u1) / (1 + (a2 - 1) * u1));
                    const double st = std::sqrt(std::max(0.0, 1 - ct * ct));
                    const V3 h{st * std::cos(phi), st * std::sin(phi), ct};
                    const double vh = dot(v, h);
                    if (vh <= 0)
                        continue;
                    const V3 l = h * (2 * vh) - v;
                    if (l.z <= 0)
                        continue;
                    const double weight =
                        fresnel(vh) * smith_g1(nv) * smith_g1(l.z) * vh / (nv * ct);
                    sum = sum + scene.arriving(p, l, random, 0) * weight;
                }
                sum = sum * (1.0 / samples);
                // The sun's own reflection, gathered directly.
                const V3 l = sun_direction();
                const V3 h = unit(l + v);
                const double f = ggx_d(h.z) * fresnel(dot(v, h)) * smith_g1(nv) * smith_g1(l.z) /
                                 (4 * nv * l.z);
                sum = sum + sun_light * (f * l.z * scene.sun_seen(p, random, 64));
                out.push_back(static_cast<float>(sum.x));
                out.push_back(static_cast<float>(sum.y));
                out.push_back(static_cast<float>(sum.z));
            }
        }
    write(argv[9], out);
    std::printf("%s rows %d..%d of %d x %d, %zu floats\n", mode.c_str(), first,
                std::min(ny, first + rows) - 1, nx, ny, out.size());
    return 0;
}
