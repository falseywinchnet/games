// The lawn's pictures: long grass with its flowers, short grass laid each way, and bark mulch, each made for the
// whole lawn at once. See lawn_art.hpp.
//
//   1. A kit of ready-drawn pieces is made once per look: a few hundred plants, flowers or bark pieces, each drawn
//      as chains of round segments into a height buffer, lit as thin pigmented leaves (or matte petals, or bark),
//      and kept as rows of samples holding height, sky-lit colour and sun-lit colour.
//   2. A field is then only stamping: each plant is a row-by-row "keep the higher sample" copy of a kit piece.
//   3. Shade one plant casts on the next is not drawn blade by blade. A coarse map of how much leaf stands above
//      each height answers "how much sun reaches here", and a few looks towards the sun across the heights give
//      the close shadows.
//
// The field is cut into tiles so every core works at once; tiles share nothing but the read-only maps.
#include "lawn_art.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <thread>

namespace mm {
namespace {

constexpr double pi = std::numbers::pi;
struct Vec {
    double x = 0, y = 0, z = 0;
    Vec operator+(Vec b) const {
        return {x + b.x, y + b.y, z + b.z};
    }
    Vec operator-(Vec b) const {
        return {x - b.x, y - b.y, z - b.z};
    }
    Vec operator*(double b) const {
        return {x * b, y * b, z * b};
    }
};
double dot(Vec a, Vec b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec cross(Vec a, Vec b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec unit(Vec a) {
    return a * (1 / std::max(1e-15, std::sqrt(dot(a, a))));
}
struct Basis {
    Vec r, u, v;
    explicit Basis(Vec direction) {
        v = unit(direction);
        r = unit(cross({0, 1, 0}, v));
        u = cross(v, r);
    }
    Vec project(Vec a) const {
        return {dot(a, r), dot(a, u), dot(a, v)};
    }
};
struct Random {
    std::uint32_t state;
    double uniform() {
        state += 0x6d2b79f5U;
        std::uint32_t t = (state ^ (state >> 15)) * (1U | state);
        t ^= t + (t ^ (t >> 7)) * (61U | t);
        return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
    }
    double normal() {
        const double u = uniform(), v = uniform();
        return std::sqrt(-2 * std::log(std::max(1e-9, u))) * std::cos(6.2831853 * v);
    }
};
struct Surface {
    Vec tangent, normal;
    double radius = 0, dye = 1, dry = 0;
};
template <bool full> struct Raster {
    int w, h;
    double hw, hh;
    Basis basis;
    std::vector<double> z;
    std::vector<Surface> surface;
    Raster(int width, int height, double half_width, double half_height, Basis axes)
        : w(width), h(height), hw(half_width), hh(half_height), basis(axes),
          z(static_cast<std::size_t>(w) * h, -1e6) {
        if constexpr (full) {
            surface.resize(z.size());
        }
    }
    void segment(Vec a, Vec b, double radius, double dye, double dry = 0) {
        a = basis.project(a);
        b = basis.project(b);
        const double sx = w / (2 * hw), sy = h / (2 * hh);
        const int xmin =
            std::max(0, static_cast<int>(std::floor((std::min(a.x, b.x) - radius + hw) * sx - .5)));
        const int xmax =
            std::min(w - 1, static_cast<int>(std::ceil((std::max(a.x, b.x) + radius + hw) * sx - .5)));
        const int ymin =
            std::max(0, static_cast<int>(std::floor((std::min(a.y, b.y) - radius + hh) * sy - .5)));
        const int ymax =
            std::min(h - 1, static_cast<int>(std::ceil((std::max(a.y, b.y) + radius + hh) * sy - .5)));
        if (xmin > xmax || ymin > ymax) {
            return;
        }
        const Vec delta = b - a;
        const double length = std::sqrt(dot(delta, delta));
        if (length < 1e-9) {
            return;
        }
        const Vec t = delta * (1 / length);
        const double A = 1 - t.z * t.z, r2 = radius * radius;
        // A capsule is contained in the convex hull of its endpoint spheres.
        // Reject already occluded samples before solving either intersection.
        const double top = std::max(a.z, b.z) + radius;
        for (int y = ymin; y <= ymax; ++y) {
            const double Y = (y + .5) / sy - hh, ry = Y - a.y;
            for (int x = xmin; x <= xmax; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * w + x;
                if (top < z[index]) {
                    continue;
                }
                const double X = (x + .5) / sx - hw, rx = X - a.x, u = rx * t.x + ry * t.y;
                const double C = rx * rx + ry * ry - u * u - r2, disc = u * u * t.z * t.z - A * C;
                double zz = -1e6;
                Vec normal{0, 0, 1};
                if (disc >= 0 && A > 1e-9) {
                    const double q = (u * t.z + std::sqrt(disc)) / A, s = u + q * t.z;
                    if (s >= 0 && s <= length) {
                        zz = a.z + q;
                        if constexpr (full) {
                            normal = {(rx - t.x * s) / radius, (ry - t.y * s) / radius,
                                      (q - t.z * s) / radius};
                        }
                    }
                }
                const double d0 = rx * rx + ry * ry;
                if (d0 <= r2) {
                    const double q = std::sqrt(r2 - d0);
                    if (a.z + q > zz) {
                        zz = a.z + q;
                        if constexpr (full) {
                            normal = {rx / radius, ry / radius, q / radius};
                        }
                    }
                }
                const double ex = X - b.x, ey = Y - b.y, d1 = ex * ex + ey * ey;
                if (d1 <= r2) {
                    const double q = std::sqrt(r2 - d1);
                    if (b.z + q > zz) {
                        zz = b.z + q;
                        if constexpr (full) {
                            normal = {ex / radius, ey / radius, q / radius};
                        }
                    }
                }
                if (zz > z[index]) {
                    z[index] = zz;
                    if constexpr (full) {
                        surface[index] = {t, normal, radius, dye, dry};
                    }
                }
            }
        }
    }
};

struct GrassParameters {
    bool mown = true;
    double scale = 4.5;        // how much larger than life the grass is drawn
    double pixels_per_metre = 96.0;
    std::uint32_t seed = 41;
    double wetness = 0;        // 0 dry .. 1 dewy: a sheen that shows which way the grass lies
    double lay = 0;            // 0 standing .. 1 pressed over by the mower
    double lay_turn = 0;       // which way it is pressed: 0 towards the sun (the darker, dewy way), pi away from it
    const LawnScene* scene = nullptr;
    bool mulch = false;
    double mulch_tint[3] = {.070, .023, .014};  // the one dye the whole bed shares, linear: mahogany
};

const double sun_azimuth = 140 * pi / 180, sun_elevation = 50 * pi / 180;
const double sun_azimuth_from_y = pi / 2 - sun_azimuth;  // the same bearing measured from +y towards +x

double clamp01(double x) {
    return std::clamp(x, 0.0, 1.0);
}
double smooth_between(double a, double b, double x) {
    const double t = clamp01((x - a) / (b - a));
    return t * t * (3 - 2 * t);
}
double lattice_value(int x, int z, int seed) {
    std::uint32_t a = static_cast<std::uint32_t>(x) * 374761393U + static_cast<std::uint32_t>(z) * 668265263U + static_cast<std::uint32_t>(seed);
    a = (a ^ (a >> 13U)) * 1274126177U;
    return static_cast<double>(a ^ (a >> 16U)) / 4294967295.0;
}
double value_noise(double x, double z, int seed) {
    const double fx = std::floor(x), fz = std::floor(z);
    const int ix = static_cast<int>(fx), iz = static_cast<int>(fz);
    double u = x - fx, v = z - fz;
    u = u * u * (3 - 2 * u);
    v = v * v * (3 - 2 * v);
    const double top = lattice_value(ix, iz, seed) + (lattice_value(ix + 1, iz, seed) - lattice_value(ix, iz, seed)) * u;
    const double bottom = lattice_value(ix, iz + 1, seed) + (lattice_value(ix + 1, iz + 1, seed) - lattice_value(ix, iz + 1, seed)) * u;
    return top + (bottom - top) * v;
}

// Lawn metres to the nearest thing the grass must stop at, for a point in picture metres from the picture's centre
// (y up, as the kit pieces are drawn).
double scene_distance(const GrassParameters& p, double picture_x, double picture_y, double metres_w, double metres_h) {
    if (p.scene == nullptr || (*p.scene).width < 2 || (*p.scene).height < 2)
        return 100;
    const LawnScene& g = *p.scene;
    const double u = std::clamp((picture_x / metres_w + .5) * (g.width - 1), 0.0, g.width - 1.001), v = std::clamp((.5 - picture_y / metres_h) * (g.height - 1), 0.0, g.height - 1.001);
    const int iu = static_cast<int>(u), iv = static_cast<int>(v);
    const double fu = u - iu, fv = v - iv;
    const std::size_t o = static_cast<std::size_t>(iv) * g.width + iu;
    const double a = g.distance[o] + (g.distance[o + 1] - g.distance[o]) * fu, b = g.distance[o + g.width] + (g.distance[o + g.width + 1] - g.distance[o + g.width]) * fu;
    return a + (b - a) * fv;
}

// Flower and mushroom parts are the same round segments as the grass, marked with a material:
// 0 clover floret, 1 buttercup, 2 dandelion, 3 mushroom cap, 4 daisy, 5 blue, 6 gold centre, 7 pale eye.
template <bool full_a, bool full_b>
void emit_part(Vec a, Vec b, double radius, double shade, int material, Raster<full_a>& camera, Raster<full_b>& shadow) {
    camera.segment(a, b, radius, shade, 4 + material);
    shadow.segment(a, b, radius, shade, 4 + material);
}
template <bool full_a, bool full_b>
void emit_blob(Vec centre, double radius, double shade, int material, Raster<full_a>& camera, Raster<full_b>& shadow) {
    emit_part(centre, {centre.x, centre.y, centre.z + radius * .05}, radius, shade, material, camera, shadow);
}
// One petal: narrow and darker at the base, broadest past the middle, lighter and rounded at the tip.
template <bool full_a, bool full_b>
void emit_petal(double x, double y, double z, double angle, double inner, double outer, double width, double cup, double shade, int material, double k,
                Raster<full_a>& camera, Raster<full_b>& shadow) {
    const double widths[3] = {.55, 1.0, .82}, shades[3] = {.62, .90, 1.06};
    const double co = std::cos(angle), si = std::sin(angle);
    Vec previous{(x + co * inner) * k, (y + si * inner) * k, z * k};
    for (int j = 1; j <= 3; ++j) {
        const double t = j / 3.0, r = inner + (outer - inner) * t;
        const Vec next{(x + co * r) * k, (y + si * r) * k, (z + cup * t * t) * k};
        emit_part(previous, next, width * widths[j - 1] * k, shade * shades[j - 1], material, camera, shadow);
        previous = next;
    }
}

struct Leaf {
    double root_x, root_y, root_z;  // metres, life size; z is up
    double azimuth, length, width, bend, tilt, dry, green;
};

// One leaf as a chain of round segments. All lengths are life-size metres; `k` turns them into the renderer's units.
template <bool full_a, bool full_b>
void emit_leaf(const Leaf& leaf, double cut, double k, Random& random, Raster<full_a>& camera, Raster<full_b>& shadow) {
    const double L = leaf.length;
    // GrassLab's cubic midrib: x outward, y up, z sideways; then scaled so its arc is the leaf's length.
    double A[3] = {L * .29 * std::sin(leaf.tilt), L * .29 * std::cos(leaf.tilt), 0};
    double B[3] = {L * (.14 + .44 * leaf.bend), L * (.60 + .11 * (1 - leaf.bend)), L * (random.uniform() * .10 - .05)};
    double C[3] = {L * (.23 + .49 * leaf.bend), L * (.87 - .71 * leaf.bend), L * (random.uniform() * .18 - .09)};
    double arc = 0, last[3] = {0, 0, 0};
    for (int j = 1; j <= 12; ++j) {
        const double t = j / 12.0, s = 1 - t;
        double q[3];
        for (int c = 0; c < 3; ++c)
            q[c] = 3 * s * s * t * A[c] + 3 * s * t * t * B[c] + t * t * t * C[c];
        arc += std::sqrt((q[0] - last[0]) * (q[0] - last[0]) + (q[1] - last[1]) * (q[1] - last[1]) + (q[2] - last[2]) * (q[2] - last[2]));
        for (int c = 0; c < 3; ++c)
            last[c] = q[c];
    }
    const double fit = L / std::max(arc, 1e-8);
    const double si = std::sin(leaf.azimuth), co = std::cos(leaf.azimuth);
    // where the mowing height cuts it
    double cut_t = 1;
    if (cut > 0) {
        if (leaf.root_z > cut)
            return;
        for (int j = 1; j <= 40; ++j) {
            const double t = j / 40.0, s = 1 - t;
            const double up = (3 * s * s * t * A[1] + 3 * s * t * t * B[1] + t * t * t * C[1]) * fit;
            if (leaf.root_z + up >= cut) {
                cut_t = (j - .5) / 40.0;
                break;
            }
        }
    }
    const int pieces = cut_t < .45 ? 4 : 8;
    Vec previous{leaf.root_x * k, leaf.root_y * k, leaf.root_z * k};
    for (int j = 1; j <= pieces; ++j) {
        const double t = cut_t * j / pieces, s = 1 - t;
        double q[3];
        for (int c = 0; c < 3; ++c)
            q[c] = (3 * s * s * t * A[c] + 3 * s * t * t * B[c] + t * t * t * C[c]) * fit;
        const Vec next{(leaf.root_x + q[0] * si + q[2] * co) * k, (leaf.root_y + q[0] * co - q[2] * si) * k, (leaf.root_z + q[1]) * k};
        // GrassLab's width profile: a narrow stalk, full width a fifth of the way up, drawn out to a point
        const double mid = cut_t * (j - .5) / pieces;
        const double profile = (.56 + .44 * clamp01(mid * 5)) * std::pow(std::max(.001, 1 - mid), .67);
        const double radius = std::max(leaf.width * .5 * profile, leaf.width * .06) * k;
        const double mark = leaf.dry + (cut_t < 1 && j == pieces ? 2 : 0);  // 2 and over: the cut end of a mown leaf
        // how far along the visible leaf this piece is rides in the dye's whole fours
        const double dye = leaf.green + 4 * std::floor((j - .5) / pieces * 15 + .5);
        camera.segment(previous, next, radius, dye, mark);
        shadow.segment(previous, next, radius, dye, mark);
        previous = next;
    }
}

constexpr int lawn_ss = 2;      // samples per pixel each way
constexpr int lawn_block = 16;  // samples per coarse cell each way
constexpr int lawn_margin = 12; // samples drawn beyond a tile's edge, so close shadows cross tile edges
constexpr int lawn_levels = 6;  // heights at which the coarse map records cover

struct Sprite {
    int x0 = 0, y0 = 0, rows = 0;         // where its first sample sits relative to the root, in samples
    std::vector<int> row_first;           // rows + 1 entries into spans
    std::vector<int> span_x, span_len, span_at;
    std::vector<float> z;
    std::vector<std::uint32_t> sky, sun;  // 8 bits a channel, square-root coded
    int fx0 = 0, fy0 = 0, fw = 0, fh = 0; // coarse cover, in blocks relative to the root's block
    std::vector<float> tau;               // fw * fh * lawn_levels: optical depth of leaf above each level
};

struct LawnLibrary {
    GrassParameters made_for;
    double tall = 0;                      // renderer units
    std::vector<Sprite> plants;           // [edge][vigour][variant], see plant_index
    int plant_variants[3] = {0, 0, 0};
    std::vector<Sprite> flowers[7];
    std::vector<Sprite> pieces[3];
    std::vector<Sprite> bed_plants[9][3];  // [flower][tone], a few of each
};

double lawn_unit(const GrassParameters& p) {
    return .0011 * p.scale / .010;  // picture metres per renderer unit, as in render_grass
}
std::uint32_t code(double v) {
    return static_cast<std::uint32_t>(std::lround(255 * std::sqrt(std::clamp(v, 0.0, 4.0) / 4)));
}

// Paint for bed flowers and trees, linear: material 11 + index.
constexpr int palette_count = 23;
const double palette[palette_count][3] = {
    {.30, .12, .55}, {.82, .81, .77}, {.90, .62, .03}, {.62, .02, .03}, {.74, .17, .28}, {.90, .84, .66}, {.92, .36, .03}, {.62, .08, .42},
    {.33, .03, .30}, {.48, .02, .09}, {.20, .30, .78}, {.44, .28, .74}, {.10, .05, .02}, {.28, .15, .03}, {.38, .11, .02}, {.045, .135, .02},
    {.09, .22, .035}, {.84, .40, .52}, {.80, .78, .74}, {.26, .055, .03}, {.44, .12, .045}, {.12, .07, .04}, {.55, .70, .35}};
enum Dye : int { purple = 11, white, yellow, red, pink, cream, orange, magenta, deep_purple, crimson, blue, lilac, disc_brown, disc_ring, rust,
                   tree_green, tree_light, blossom_pink, blossom_white, copper, copper_light, branch, pale_green };

// The slow renderer's surface lighting, split into the part the sky gives and the part the sun gives.
void lit(const GrassParameters& p, const Surface& s, double z, double tall, Vec L, double mx, double my, double sky_out[3], double sun_out[3]) {
    const double sun_colour[3] = {1.03, .90, .76};
    const double canopy = clamp01(1 - z / std::max(tall * .82, 1e-6));
    Vec N = s.normal;
    if (N.z < 0)
        N = N * -1;
    if (s.dry >= 4) {
        const int material = static_cast<int>(s.dry) - 4;
        const double albedo[8][3] = {{.78, .78, .69}, {.88, .66, .03}, {.92, .58, .02}, {.84, .80, .70}, {.87, .87, .85}, {.14, .24, .80}, {.78, .47, .03}, {.86, .83, .50}};
        const bool bark = material >= 8 && material <= 10;
        const bool painted = material >= 11;
        double rough = 1;
        if (bark) {
            const double flat = material == 9 ? .72 : .30;
            N = N * (1 - flat) + Vec{0, 0, flat};
            N = N * (1 / std::sqrt(dot(N, N)));
            const Vec T = s.tangent;
            const double tl = std::max(1e-6, std::hypot(T.x, T.y));
            const double along = (mx * T.x + my * T.y) / tl, across = (-mx * T.y + my * T.x) / tl;
            rough = (.74 + .52 * value_noise(along * 28, across * 330, 71)) * (.86 + .28 * value_noise(mx * 210, my * 210, 73));
        }
        // bark's own depth in the pile is applied when the field is put together, where the pile is known
        const double sky_vis = bark ? 1.0 : .15 + .85 * std::exp(-2.2 * canopy);
        const double nl = std::max(dot(N, L), 0.0), glow = material == 3 || bark ? 0 : .16;
        const double crown = material == 3 ? std::pow(std::max(N.z, 0.0), 10) : 0, rim = material == 3 ? .70 + .30 * smooth_between(.15, .55, N.z) : 1;
        const double tan_c[3] = {.62, .44, .26};
        for (int c = 0; c < 3; ++c) {
            const double wood = material == 10 ? p.mulch_tint[c] * 1.5 + .012 : p.mulch_tint[c];
            const double base = bark ? wood * s.dye * rough : painted ? palette[std::min(material - 11, palette_count - 1)][c] * s.dye : (albedo[std::min(material, 7)][c] + (tan_c[c] - albedo[std::min(material, 7)][c]) * crown * .55) * rim * s.dye;
            const double lift = material == 9 ? 1.25 : 1;
            sky_out[c] = base * .74 * (c == 0 ? .66 : c == 1 ? .77 : .95) * sky_vis * (.6 + .4 * N.z);
            sun_out[c] = base * sun_colour[c] * (nl * .8 * (bark ? lift : 1) + glow) * (painted ? 1.6 : 2.0);
        }
        return;
    }
    const bool cut_end = s.dry >= 2;
    const double along = std::floor(s.dye / 4), dye = s.dye - 4 * along, tipness = along / 15;
    const double dry = clamp01(cut_end ? s.dry - 2 : s.dry), green = 1 / dye;
    const Vec raw = N;
    // the dew shows on grass laid towards the sun, and fades as the lay turns from it
    const double wet = p.lay > 0 ? p.wetness * std::max(0.0, std::cos(p.lay_turn)) : 0;
    const double flat = .55 - .25 * p.lay - .20 * wet;
    N = N * (1 - flat) + Vec{0, 0, flat};
    N = N * (1 / std::sqrt(dot(N, N)));
    const double tip_w = smooth_between(.60, 1.0, tipness), shaft = .30 + .70 * tipness * std::sqrt(tipness);
    const double absorb_tip[3] = {1.78, 1.14, 3.30};
    const double nl = dot(N, L), forward = .32 + .08 * dry;
    const double absorb_green[3] = {2.75, 1.72, 3.75}, absorb_dry[3] = {.55, .69, 1.31};
    const double sky[3] = {.66 * .74, .77 * .74, .95 * .74}, bounce[3] = {.18 * .27, .24 * .27, .065 * .27}, tip[3] = {.17, .27, .06};
    const double lai = p.mown ? 3.2 : 4.4;
    const double sky_vis = .07 + .93 * std::exp(-lai * .62 * canopy), facing = .60 + .40 * std::abs(N.z);
    const double hl = std::sqrt(L.x * L.x + L.y * L.y + (L.z + 1) * (L.z + 1));
    const Vec H{L.x / hl, L.y / hl, (L.z + 1) / hl};
    const double sheen = .05 * std::pow(std::max(dot(N, H), 0.0), 24) + wet * .16 * std::pow(std::max(dot(raw, L), 0.0), 3);
    for (int c = 0; c < 3; ++c) {
        const double pigment = absorb_green[c] + (absorb_tip[c] - absorb_green[c]) * tip_w * .55;
        const double survival = std::exp(-(pigment + (absorb_dry[c] - pigment) * dry));
        double refl = .955 * survival * (1 - forward) * green * shaft * (1 - .10 * wet);
        const double trans = .955 * survival * forward;
        if (cut_end)
            refl += (tip[c] - refl) * .22;
        sky_out[c] = (refl + trans * .75) * sky[c] * facing * sky_vis + refl * bounce[c] * (.25 + .75 * canopy);
        sun_out[c] = sun_colour[c] * (refl * std::max(nl, 0.0) + trans * std::max(-nl, 0.0) + sheen * std::max(nl, 0.0)) * 2.5;
    }
}

// One grass plant at the origin: grass_geometry's plant, with its vigour and how near an object it stands given.
void emit_plant(const GrassParameters& p, double vigour, double length_factor, double edge_dry, Random& random, double k, Raster<true>& camera, Raster<false>& shadow) {
    const double E = p.mown ? .64 : .56, stage = p.mown ? .39 : .29, density = p.mown ? .86 : .79 * .60, dryness = p.mown ? .08 : .04;
    const double cut = p.mown ? .050 : 0, length_base = p.mown ? E * (.16 + .62 * stage * stage) : .175;
    const double cell = 1 / std::sqrt((1700 + 4300 * (1 - stage)) * density / (E * E));
    const double base_fan = random.uniform() * 2 * pi;
    const int tillers = 2 + (random.uniform() < .40 ? 1 : 0);
    for (int t = 0; t < tillers; ++t) {
        const double ta = base_fan + t * 2.399 + (random.uniform() * .6 - .3), spread = cell * (.15 + .55 * stage);
        const double rx = std::sin(ta) * spread, ry = std::cos(ta) * spread;
        const int leaf_count = p.mown ? 3 : 4 + (random.uniform() < .5 ? 1 : 0);
        for (int j = 0; j < leaf_count; ++j) {
            const double age = (j + random.uniform() * .7) / leaf_count, young = 1 - age;
            Leaf leaf{};
            leaf.root_x = rx;
            leaf.root_y = ry;
            leaf.root_z = t * E * .0006;
            leaf.azimuth = ta + (j % 2 ? pi : 0) + random.normal() * (.3 + .23 * stage);
            const double spread_l = std::clamp(std::exp(random.normal() * (.17 + .13 * stage)), .50, 1.5);
            double dry = clamp01(dryness * (.6 + .55 * random.uniform()) + smooth_between(.65, 1, age) * (.16 + .22 * stage) + random.normal() * .045 - .045);
            if (random.uniform() < dryness * .40)
                dry = clamp01(dry + .3 + .25 * random.uniform());
            if (p.mown) {
                leaf.length = length_base * spread_l * vigour * (.50 + .64 * age);
                leaf.width = E * (.0020 + .0038 * stage) * (.73 + .47 * random.uniform()) * (.67 + .33 * age);
                leaf.bend = clamp01(.04 + age * (.13 + .57 * stage) + dry * .15 + random.normal() * .035);
                leaf.tilt = .06 + .34 * age + random.uniform() * .14;
            } else {
                leaf.length = length_base * spread_l * vigour * (.62 + .50 * young);
                leaf.width = E * .0034 * (.73 + .47 * random.uniform()) * (.50 + 1.25 * age * age);
                leaf.bend = clamp01(.03 + .80 * age * age + dry * .1 + random.normal() * .04);
                leaf.tilt = .05 + .95 * age + random.uniform() * .12;
            }
            if (p.mown && p.lay > 0) {
                const double way = sun_azimuth_from_y + p.lay_turn;
                const double dx = std::sin(leaf.azimuth) + std::sin(way) * p.lay * 2.6, dy = std::cos(leaf.azimuth) + std::cos(way) * p.lay * 2.6;
                leaf.azimuth = std::atan2(dx, dy);
                leaf.tilt += .58 * p.lay;
            }
            leaf.length *= length_factor;
            leaf.dry = clamp01(dry + .35 * edge_dry * random.uniform());
            leaf.green = 1 / (.78 + .40 * random.uniform());
            emit_leaf(leaf, cut, k, random, camera, shadow);
        }
    }
    if (random.uniform() < .24 + dryness * .28) {
        Leaf leaf{0, 0, E * .0015, random.uniform() * 2 * pi, E * (.019 + .031 * random.uniform()) * (1 + stage), E * .0012, .95, 1.4, .9, 1.0};
        emit_leaf(leaf, 0, k, random, camera, shadow);
    }
}

// One flower head (0 clover, 1 buttercup, 2 dandelion, 3 daisy, 4 blue, 6 dandelion clock) or mushroom cap (5) at the origin.
void emit_flower(int kind, Random& random, double k, Raster<true>& camera, Raster<false>& shadow) {
    const double x = 0, y = 0;
    if (kind == 0) {
        const double z = .11 + .06 * random.uniform(), r = .010 + .003 * random.uniform();
        for (int j = 0; j < 22; ++j) {
            const double a = random.uniform() * 2 * pi, e = .15 + random.uniform() * 1.35, ce = std::cos(e), se = std::sin(e);
            const Vec inner{(x + std::cos(a) * ce * r * .35) * k, (y + std::sin(a) * ce * r * .35) * k, (z + se * r * .35) * k};
            const Vec outer{(x + std::cos(a) * ce * r) * k, (y + std::sin(a) * ce * r) * k, (z + se * r) * k};
            emit_part(inner, outer, r * .17 * k, (.55 + .50 * se) * (.9 + .2 * random.uniform()), 0, camera, shadow);
        }
    } else if (kind == 1) {
        const double z = .14 + .06 * random.uniform(), r = .0095 + .003 * random.uniform(), turn = random.uniform() * 2 * pi;
        for (int j = 0; j < 5; ++j)
            emit_petal(x, y, z, turn + j * 2 * pi / 5 + random.normal() * .06, r * .12, r, r * .40, r * .30, .9 + .2 * random.uniform(), 1, k, camera, shadow);
        emit_blob({x * k, y * k, (z + r * .05) * k}, r * .26 * k, .9, 6, camera, shadow);
    } else if (kind == 2) {
        const double z = .12 + .07 * random.uniform(), r = .016 + .005 * random.uniform();
        for (int j = 0; j < 46; ++j) {
            const double a = random.uniform() * 2 * pi, reach = j < 28 ? r * (.75 + .25 * random.uniform()) : r * (.30 + .30 * random.uniform());
            const double lift = j < 28 ? 0 : r * .16;
            const Vec inner{x * k, y * k, (z + r * .12 + lift) * k}, mid{(x + std::cos(a) * reach * .5) * k, (y + std::sin(a) * reach * .5) * k, (z + r * .10 + lift) * k};
            const Vec outer{(x + std::cos(a) * reach) * k, (y + std::sin(a) * reach) * k, (z + lift * .5) * k};
            const double tone = .85 + .3 * random.uniform();
            emit_part(inner, mid, r * .075 * k, tone * (j < 28 ? .70 : .80), 2, camera, shadow);
            emit_part(mid, outer, r * .085 * k, tone * (j < 28 ? 1.04 : .92), 2, camera, shadow);
        }
    } else if (kind == 3) {
        const double z = .10 + .06 * random.uniform(), r = .011 + .003 * random.uniform(), turn = random.uniform() * 2 * pi;
        const int petals = 13 + static_cast<int>(random.uniform() * 5);
        for (int j = 0; j < petals; ++j)
            emit_petal(x, y, z, turn + j * 2 * pi / petals + random.normal() * .05, r * .28, r * (.9 + .15 * random.uniform()), r * .115, -r * .08,
                       .92 + .14 * random.uniform(), 4, k, camera, shadow);
        emit_blob({x * k, y * k, (z + r * .06) * k}, r * .30 * k, 1.0, 6, camera, shadow);
    } else if (kind == 4) {
        const double z = .12 + .05 * random.uniform(), r = .0065 + .0015 * random.uniform(), turn = random.uniform() * 2 * pi;
        for (int j = 0; j < 5; ++j)
            emit_petal(x, y, z, turn + j * 2 * pi / 5, r * .15, r, r * .36, r * .1, .9 + .2 * random.uniform(), 5, k, camera, shadow);
        emit_blob({x * k, y * k, (z + r * .04) * k}, r * .20 * k, 1.0, 7, camera, shadow);
    } else if (kind == 6) {
        // a dandelion clock: a ball of fine white down
        const double z = .13 + .06 * random.uniform(), r = .015 + .004 * random.uniform();
        for (int j = 0; j < 46; ++j) {
            const double a = random.uniform() * 2 * pi, e = random.uniform() * 1.5, ce = std::cos(e), se = std::sin(e);
            const Vec inner{(x + std::cos(a) * ce * r * .3) * k, (y + std::sin(a) * ce * r * .3) * k, (z + se * r * .3) * k};
            const Vec outer{(x + std::cos(a) * ce * r) * k, (y + std::sin(a) * ce * r) * k, (z + se * r) * k};
            emit_part(inner, outer, r * .07 * k, (.70 + .35 * se) * (.9 + .2 * random.uniform()), 4, camera, shadow);
        }
    } else {
        const double r = .011 + .017 * random.uniform() * random.uniform();
        emit_blob({x * k, y * k, (.055 + .05 * random.uniform()) * k}, r * k, .9 + .18 * random.uniform(), 3, camera, shadow);
    }
}

// One piece of bark at the origin lying on the ground: 0 needle, 1 sliver, 2 chunk.
void emit_piece(int sort, Random& random, double k, Raster<true>& camera, Raster<false>& shadow) {
    double length, width, thick;
    if (sort == 0) {
        length = .015 + .075 * random.uniform() * random.uniform();
        thick = .0010 + .0014 * random.uniform();
        width = thick * (1 + random.uniform());
    } else if (sort == 1) {
        length = .025 + .075 * random.uniform() * random.uniform();
        thick = .0016 + .0018 * random.uniform();
        width = .004 + .007 * random.uniform();
    } else {
        length = .028 + .050 * random.uniform();
        thick = .0026 + .0030 * random.uniform();
        width = std::min(length * .6, .009 + .015 * random.uniform());
    }
    const double z = thick + .012;  // room to tilt; the field adds each piece's place in the pile
    const double heading = random.uniform() * 2 * pi, pitch = random.normal() * (sort == 2 ? .10 : .22), roll = random.normal() * (sort == 2 ? .14 : .4);
    const double ax = std::cos(heading), ay = std::sin(heading);
    const int strands = std::max(1, static_cast<int>(width / (thick * .6)));
    const bool pale = random.uniform() < .035;
    const double shade = (.78 + .34 * random.uniform()) * (sort == 2 ? 1.12 : sort == 1 ? 1.0 : .92);
    const int material = pale ? 10 : sort == 2 ? 9 : 8;
    const double bow = random.normal() * length * (sort == 0 ? .10 : .04);
    double back_edge = .6 + .4 * random.uniform(), front_edge = .6 + .4 * random.uniform();
    for (int j = 0; j < strands; ++j) {
        const double across = strands > 1 ? (j + .5) / strands - .5 : 0, off = across * width;
        back_edge = std::clamp(back_edge + random.normal() * .13, .25, 1.0);
        front_edge = std::clamp(front_edge + random.normal() * .13, .25, 1.0);
        const double taper = 1 - .9 * across * across;
        const double back = length * .5 * back_edge * taper * (random.uniform() < .06 ? 1.3 : 1), front = length * .5 * front_edge * taper * (random.uniform() < .06 ? 1.3 : 1);
        const double zz = z + off * std::sin(roll), radius = thick * (.85 + .25 * random.uniform()) * k, tone = shade * (.94 + .12 * random.uniform());
        const Vec a{(-ax * back - ay * off) * k, (-ay * back + ax * off) * k, std::max(thick * .5, zz - back * std::sin(pitch)) * k};
        const Vec m{(-ay * (off + bow)) * k, (ax * (off + bow)) * k, std::max(thick * .5, zz) * k};
        const Vec b{(ax * front - ay * off) * k, (ay * front + ax * off) * k, std::max(thick * .5, zz + front * std::sin(pitch)) * k};
        emit_part(a, m, radius, tone, material, camera, shadow);
        emit_part(m, b, radius, tone, material, camera, shadow);
    }
}

// Bed flowers and trees are written in metres as drawn; the segment emitters take life-size metres.
struct Maker {
    Random& random;
    double k;
    double u;  // 1 / scale
    Raster<true>& camera;
    Raster<false>& shadow;
    void petal(double x, double y, double z, double angle, double inner, double outer, double width, double cup, double shade, int material) {
        emit_petal(x * u, y * u, z * u, angle, inner * u, outer * u, width * u, cup * u, shade, material, k, camera, shadow);
    }
    void blob(double x, double y, double z, double r, double shade, int material) {
        emit_blob({x * u * k, y * u * k, z * u * k}, r * u * k, shade, material, camera, shadow);
    }
    void part(double ax, double ay, double az, double bx, double by, double bz, double r, double shade, int material) {
        emit_part({ax * u * k, ay * u * k, az * u * k}, {bx * u * k, by * u * k, bz * u * k}, r * u * k, shade, material, camera, shadow);
    }
    // A leaf of foliage from a point, lit as grass is.
    void leaf(double x, double y, double z, double azimuth, double length, double width, double bend, double tilt, double green) {
        Leaf leaf{x * u, y * u, z * u, azimuth, length * u, width * u, bend, tilt, clamp01(.03 + random.normal() * .03), green};
        emit_leaf(leaf, 0, k, random, camera, shadow);
    }
    void foliage(int count, double spread, double length, double width, double tilt, double green) {
        const double turn = random.uniform() * 2 * pi;
        count = count * 3 / 2;
        green *= 1.45;
        for (int j = 0; j < count; ++j) {
            const double a = turn + j * 2.399 + random.normal() * .2, d = spread * std::sqrt(random.uniform());
            leaf(std::sin(a) * d * .5, std::cos(a) * d * .5, 0, a, length * (.7 + .5 * random.uniform()), width * (.8 + .4 * random.uniform()),
                 .25 + .35 * random.uniform(), tilt * (.75 + .4 * random.uniform()), green * (.9 + .25 * random.uniform()));
        }
    }
};

// One bed plant at the origin: 0 crocus, 1 rose, 2 sunflower, 3 tulip, 4 lily, 5 orchid, 6 peony, 7 hydrangea, 8 daisy.
// `tone` picks which of the kind's three colours it flowers in.
void emit_bed_plant(int kind, int tone, Random& random, double k, double scale, Raster<true>& camera, Raster<false>& shadow) {
    Maker m{random, k, 1.6 / scale, camera, shadow};  // bed flowers are drawn large, as the grass is
    const int tones[9][3] = {{purple, white, yellow}, {red, pink, cream}, {yellow, yellow, orange}, {red, yellow, pink}, {white, orange, pink},
                             {magenta, white, lilac}, {pink, white, crimson}, {blue, pink, lilac}, {white, white, cream}};
    const int colour = tones[std::clamp(kind, 0, 8)][std::clamp(tone, 0, 2)];
    if (kind == 0) {
        // crocus: a few cups of six pointed petals low among grassy leaves
        m.foliage(7, .05, .11, .008, .55, 1.05);
        const int flowers = 1 + static_cast<int>(random.uniform() * 3);
        for (int f = 0; f < flowers; ++f) {
            const double a = random.uniform() * 2 * pi, d = flowers > 1 ? .045 : 0, x = std::cos(a + f * 2.1) * d, y = std::sin(a + f * 2.1) * d, turn = random.uniform() * pi;
            for (int j = 0; j < 3; ++j)
                m.petal(x, y, .050, turn + j * 2 * pi / 3, .004, .040, .016, .022, .92 + .14 * random.uniform(), colour);
            for (int j = 0; j < 3; ++j)
                m.petal(x, y, .058, turn + pi / 3 + j * 2 * pi / 3, .003, .031, .014, .026, .80 + .12 * random.uniform(), colour);
            for (int j = 0; j < 3; ++j)
                m.blob(x + std::cos(turn + j * 2.1) * .006, y + std::sin(turn + j * 2.1) * .006, .066, .0045, 1.0, orange);
        }
    } else if (kind == 1) {
        // rose: a bush of dark leaflets carrying blooms wound from the outside in
        m.foliage(22, .26, .10, .030, .85, .78);
        const int blooms = 3 + static_cast<int>(random.uniform() * 4);
        for (int f = 0; f < blooms; ++f) {
            const double a = random.uniform() * 2 * pi, d = .19 * std::sqrt(random.uniform()), x = std::cos(a) * d, y = std::sin(a) * d;
            const double r = .046 + .014 * random.uniform(), z = .20 + .06 * random.uniform(), turn = random.uniform() * 2 * pi;
            for (int j = 0; j < 5; ++j)
                m.petal(x, y, z, turn + j * 2 * pi / 5, r * .25, r, r * .46, -r * .12, .95 + .12 * random.uniform(), colour);
            for (int j = 0; j < 17; ++j) {
                // each inner petal shows as its curled rim: a short arc across the radius
                const double t = j / 17.0, ring = r * (.78 - .70 * t), at = turn + j * 2.399, half = ring * .62 + .004;
                const double cx = x + std::cos(at) * ring, cy = y + std::sin(at) * ring, zz = z + r * (.10 + .55 * t);
                m.part(cx - std::sin(at) * half, cy + std::cos(at) * half, zz, cx + std::sin(at) * half, cy - std::cos(at) * half, zz, r * (.13 - .05 * t),
                       (j % 2 ? .80 : 1.0) * (.92 + .16 * random.uniform()), colour);
            }
        }
    } else if (kind == 2) {
        // sunflower: one great head over broad leaves
        m.foliage(7, .10, .25, .070, 1.05, .85);
        const double x = random.normal() * .02, y = random.normal() * .02, z = .46, disc = .070 + .012 * random.uniform(), turn = random.uniform() * 2 * pi;
        for (int j = 0; j < 21; ++j)
            m.petal(x, y, z, turn + j * 2 * pi / 21 + random.normal() * .03, disc * .85, disc * (2.15 + .25 * random.uniform()), .017, -.012, .92 + .14 * random.uniform(), colour);
        for (int j = 0; j < 17; ++j)
            m.petal(x, y, z + .008, turn + .15 + j * 2 * pi / 17 + random.normal() * .04, disc * .8, disc * (1.75 + .2 * random.uniform()), .016, .004, .84 + .12 * random.uniform(), colour);
        m.blob(x, y, z - disc * .55, disc, .9, disc_brown);
        for (int j = 0; j < 70; ++j) {
            // seeds in rings: paler towards the rim
            const double t = std::sqrt((j + .5) / 70.0), a = j * 2.399;
            m.blob(x + std::cos(a) * disc * t * .92, y + std::sin(a) * disc * t * .92, z + disc * (.42 - .22 * t * t), disc * .085, .8 + .5 * random.uniform(), t > .62 ? disc_ring : disc_brown);
        }
    } else if (kind == 3) {
        // tulip: a cup of six broad petals over two or three strap leaves
        const double turn0 = random.uniform() * 2 * pi;
        for (int j = 0; j < 3; ++j)
            m.leaf(0, 0, 0, turn0 + j * 2.2 + random.normal() * .2, .17 + .05 * random.uniform(), .034, .45, 1.0 + .2 * random.uniform(), .92);
        const double z = .15, r = .046 + .008 * random.uniform(), turn = random.uniform() * pi;
        for (int j = 0; j < 3; ++j)
            m.petal(0, 0, z, turn + j * 2 * pi / 3, r * .1, r, r * .50, r * .30, .98 + .10 * random.uniform(), colour);
        for (int j = 0; j < 3; ++j)
            m.petal(0, 0, z + .010, turn + pi / 3 + j * 2 * pi / 3, r * .1, r * .80, r * .46, r * .42, .78 + .10 * random.uniform(), colour);
        m.blob(0, 0, z + .004, r * .16, .5, colour == yellow ? orange : deep_purple);
    } else if (kind == 4) {
        // lily: stars of six long tepals curling back, freckled, with rust anthers standing out
        m.foliage(12, .06, .15, .014, .95, .88);
        const int flowers = 1 + static_cast<int>(random.uniform() * 3);
        for (int f = 0; f < flowers; ++f) {
            const double a = random.uniform() * 2 * pi, d = flowers > 1 ? .085 : 0, x = std::cos(a + f * 2.2) * d, y = std::sin(a + f * 2.2) * d;
            const double z = .26 + .05 * random.uniform(), r = .088 + .012 * random.uniform(), turn = random.uniform() * pi;
            for (int j = 0; j < 6; ++j) {
                const double at = turn + j * pi / 3 + random.normal() * .03;
                m.petal(x, y, z + (j % 2) * .006, at, r * .08, r, r * (j % 2 ? .25 : .21), -r * .22, .94 + .12 * random.uniform(), colour);
                for (int q = 0; q < 4; ++q) {
                    const double along = r * (.16 + .30 * random.uniform()), across = random.normal() * r * .05;
                    m.blob(x + std::cos(at) * along - std::sin(at) * across, y + std::sin(at) * along + std::cos(at) * across, z + .012, .0035, .8, colour == white ? crimson : rust);
                }
            }
            for (int j = 0; j < 6; ++j) {
                const double at = turn + pi / 6 + j * pi / 3, reach = r * .46;
                m.part(x, y, z + .01, x + std::cos(at) * reach, y + std::sin(at) * reach, z + .045, .0022, 1.0, pale_green);
                m.blob(x + std::cos(at) * reach, y + std::sin(at) * reach, z + .047, .0065, 1.0, rust);
            }
        }
    } else if (kind == 5) {
        // orchid: three narrow sepals, two broad petals, and a lip of another colour
        const double turn0 = random.uniform() * 2 * pi;
        for (int j = 0; j < 4; ++j)
            m.leaf(0, 0, 0, turn0 + j * 1.6 + random.normal() * .2, .15 + .04 * random.uniform(), .030, .5, 1.1, .85);
        const int flowers = 2 + static_cast<int>(random.uniform() * 2);
        for (int f = 0; f < flowers; ++f) {
            const double a = turn0 + f * 2.3, d = .03 + .045 * f, x = std::cos(a) * d, y = std::sin(a) * d, z = .16 + .03 * f, face = random.uniform() * 2 * pi;
            const double r = .048 + .008 * random.uniform();
            for (int j = 0; j < 3; ++j)
                m.petal(x, y, z, face + pi + j * 2 * pi / 3, r * .1, r, r * .20, -r * .08, .88 + .1 * random.uniform(), colour);
            m.petal(x, y, z + .006, face + pi / 2 + .25, r * .1, r * .92, r * .42, r * .05, 1.0, colour);
            m.petal(x, y, z + .006, face - pi / 2 - .25, r * .1, r * .92, r * .42, r * .05, 1.0, colour);
            m.petal(x, y, z + .012, face, r * .05, r * .62, r * .34, r * .16, 1.0, colour == magenta ? white : deep_purple);
            m.blob(x + std::cos(face) * r * .12, y + std::sin(face) * r * .12, z + .020, r * .11, 1.0, yellow);
        }
    } else if (kind == 6) {
        // peony: full, ruffled balls of petals on a leafy bush
        m.foliage(16, .22, .17, .034, .95, .82);
        const int blooms = 2 + static_cast<int>(random.uniform() * 3);
        for (int f = 0; f < blooms; ++f) {
            const double a = random.uniform() * 2 * pi, d = .15 * std::sqrt(random.uniform()), x = std::cos(a) * d, y = std::sin(a) * d;
            const double r = .070 + .018 * random.uniform(), z = .24 + .06 * random.uniform();
            const int rings[4] = {9, 8, 7, 5};
            for (int ring = 0; ring < 4; ++ring) {
                const double reach = r * (1 - ring * .23), turn = random.uniform() * 2 * pi;
                for (int j = 0; j < rings[ring]; ++j)
                    m.petal(x, y, z + ring * r * .16, turn + j * 2 * pi / rings[ring] + random.normal() * .12, reach * .25, reach * (.9 + .2 * random.uniform()),
                            reach * (.40 + .08 * random.uniform()), reach * (.05 + ring * .10), (.80 + ring * .07) * (.92 + .16 * random.uniform()), colour);
            }
            for (int j = 0; j < 6; ++j)
                m.blob(x + random.normal() * r * .08, y + random.normal() * r * .08, z + r * .66, r * .10, .95 + .1 * random.uniform(), colour == white ? cream : colour);
        }
    } else if (kind == 7) {
        // hydrangea: mopheads, each a dome of small four-petalled florets, over big leaves
        m.foliage(12, .26, .20, .064, 1.0, .80);
        const int heads = 2 + static_cast<int>(random.uniform() * 2);
        for (int f = 0; f < heads; ++f) {
            const double a = random.uniform() * 2 * pi + f * 2.1, d = heads > 1 ? .13 : 0, x = std::cos(a) * d, y = std::sin(a) * d;
            const double r = .095 + .02 * random.uniform(), z = .26 + .05 * random.uniform();
            m.blob(x, y, z - r * .5, r * .86, .55, colour);
            for (int j = 0; j < 46; ++j) {
                const double t = std::sqrt((j + .5) / 46.0), at = j * 2.399 + random.normal() * .1, fx = x + std::cos(at) * r * t, fy = y + std::sin(at) * r * t;
                const double fz = z + r * (.42 - .40 * t * t), turn = random.uniform() * pi, shade = (.78 + .30 * random.uniform()) * (1.05 - .2 * t);
                for (int q = 0; q < 4; ++q)
                    m.petal(fx, fy, fz, turn + q * pi / 2, .001, .019, .0105, .002, shade, colour);
                m.blob(fx, fy, fz + .004, .0032, .7, colour == white ? pale_green : cream);
            }
        }
    } else {
        // shasta daisies: a clump with a handful of white-rayed flowers
        m.foliage(10, .08, .10, .016, .9, .85);
        const int flowers = 3 + static_cast<int>(random.uniform() * 3);
        for (int f = 0; f < flowers; ++f) {
            const double a = random.uniform() * 2 * pi + f * 2.4, d = .085 * std::sqrt(random.uniform() + .15), x = std::cos(a) * d, y = std::sin(a) * d;
            const double z = .14 + .06 * random.uniform(), r = .040 + .008 * random.uniform(), turn = random.uniform() * pi;
            const int petals = 17 + static_cast<int>(random.uniform() * 5);
            for (int j = 0; j < petals; ++j)
                m.petal(x, y, z, turn + j * 2 * pi / petals + random.normal() * .04, r * .28, r * (.9 + .15 * random.uniform()), r * .12, -r * .08, .92 + .14 * random.uniform(), colour);
            m.blob(x, y, z + r * .05, r * .30, 1.0, 6);
        }
    }
}

// A small tree's canopy at the origin, seen from above: boughs of leaves heaped into a dome, each bough a
// mound of its own, so light and shade model it. 0 green, 1 pink blossom, 2 white blossom, 3 copper.
void emit_tree(int kind, double crown, Random& random, double k, double scale, Raster<true>& camera, Raster<false>& shadow) {
    Maker m{random, k, 1 / scale, camera, shadow};
    const int dark = kind == 3 ? copper : tree_green, light = kind == 3 ? copper_light : tree_light;
    const int flower = kind == 1 ? blossom_pink : blossom_white;
    const int boughs = 14 + static_cast<int>(crown * crown * 9);
    // limbs glimpsed through the gaps
    for (int j = 0; j < 7; ++j) {
        const double a = j * 2 * pi / 7 + random.normal() * .2, reach = crown * (.55 + .3 * random.uniform());
        m.part(0, 0, crown * .25, std::cos(a) * reach, std::sin(a) * reach, crown * .55, crown * .028, 1.0, branch);
    }
    for (int b = 0; b < boughs; ++b) {
        const double a = b * 2.399 + random.normal() * .3, t = std::sqrt((b + .5) / boughs), d = crown * t * .80;
        const double bx = std::cos(a) * d, by = std::sin(a) * d, br = crown * (.20 + .12 * random.uniform()), bz = crown * (1.05 - .55 * t * t);
        const int leaves = static_cast<int>(br * br * 2600);
        for (int j = 0; j < leaves; ++j) {
            const double la = random.uniform() * 2 * pi, lt = std::sqrt(random.uniform()), lx = bx + std::cos(la) * br * lt, ly = by + std::sin(la) * br * lt;
            if (std::hypot(lx, ly) > crown)
                continue;
            const double lz = bz + br * (.55 - .65 * lt * lt) + random.normal() * .02, heading = random.uniform() * 2 * pi, length = .050 + .030 * random.uniform();
            const double dip = random.normal() * .25;
            const bool bloom = kind == 1 || kind == 2 ? random.uniform() < .80 : false;
            if (bloom) {
                // a cluster of blossom: five round petals and a dark eye
                for (int q = 0; q < 5; ++q)
                    m.blob(lx + std::cos(heading + q * 1.2566) * .026, ly + std::sin(heading + q * 1.2566) * .026, lz, .021, .86 + .22 * random.uniform(), flower);
                m.blob(lx, ly, lz + .006, .007, .9, kind == 1 ? crimson : pale_green);
                continue;
            }
            m.part(lx - std::cos(heading) * length * .45, ly - std::sin(heading) * length * .45, lz - length * dip * .45, lx + std::cos(heading) * length * .45,
                   ly + std::sin(heading) * length * .45, lz + length * dip * .45, .024 + .010 * random.uniform(), .72 + .5 * random.uniform(),
                   random.uniform() < .22 + .30 * (1 - lt) ? light : dark);
        }
    }
}

struct BakeJob {
    int what = 0;  // 0 plant, 1 flower, 2 bark piece, 3 bed plant, 4 tree
    int kind = 0;
    int tone = 0;
    double crown = 1;
    double vigour = 1, length_factor = 1, edge_dry = 0, reach = .1;  // reach: life-size metres from the root
    std::uint32_t seed = 1;
    Sprite* out = nullptr;
};

void bake(const GrassParameters& p, double tall, const BakeJob& job) {
    const double unit = lawn_unit(p), k = p.scale / unit, sample = 1 / (p.pixels_per_metre * lawn_ss) / unit;
    const int half = (static_cast<int>(std::ceil(job.reach * k / sample)) + lawn_block + 1) / lawn_block * lawn_block, n = 2 * half;
    const double hw = half * sample;
    Raster<true> camera(n, n, hw, hw, Basis({0, 0, 1}));
    const int shadow_side = std::clamp(static_cast<int>(n * 1.3), 48, 1400);
    const double shadow_half = hw * 1.45 + tall;
    Raster<false> shadow(shadow_side, shadow_side, shadow_half, shadow_half, Basis({std::cos(sun_azimuth) * std::cos(sun_elevation), std::sin(sun_azimuth) * std::cos(sun_elevation), std::sin(sun_elevation)}));
    Random random{job.seed};
    if (job.what == 0)
        emit_plant(p, job.vigour, job.length_factor, job.edge_dry, random, k, camera, shadow);
    else if (job.what == 1)
        emit_flower(job.kind, random, k, camera, shadow);
    else if (job.what == 3)
        emit_bed_plant(job.kind, job.tone, random, k, p.scale, camera, shadow);
    else if (job.what == 4)
        emit_tree(job.kind, job.crown, random, k, p.scale, camera, shadow);
    else
        emit_piece(job.kind, random, k, camera, shadow);
    const Vec L = shadow.basis.v, sr = shadow.basis.r, su = shadow.basis.u;
    const double radius = .010;
    Sprite& sprite = *job.out;
    int top = -1, bottom = n;
    for (int y = 0; y < n && top < 0; ++y)
        for (int x = 0; x < n; ++x)
            if (camera.surface[static_cast<std::size_t>(y) * n + x].radius > 0) {
                top = y;
                break;
            }
    if (top < 0)
        return;
    for (int y = n - 1; y >= top && bottom == n; --y)
        for (int x = 0; x < n; ++x)
            if (camera.surface[static_cast<std::size_t>(y) * n + x].radius > 0) {
                bottom = y;
                break;
            }
    sprite.x0 = -half;
    sprite.y0 = top - half;
    sprite.rows = bottom - top + 1;
    sprite.fw = sprite.fh = n / lawn_block;
    sprite.fx0 = sprite.fy0 = -half / lawn_block;
    std::vector<int> count(static_cast<std::size_t>(sprite.fw) * sprite.fh * lawn_levels, 0);
    for (int y = top; y <= bottom; ++y) {
        sprite.row_first.push_back(static_cast<int>(sprite.span_x.size()));
        int open = -1;
        for (int x = 0; x <= n; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * n + x;
            const bool hit = x < n && camera.surface[i].radius > 0;
            if (hit && open < 0) {
                open = x;
                sprite.span_x.push_back(x);
                sprite.span_at.push_back(static_cast<int>(sprite.z.size()));
            }
            if (!hit && open >= 0) {
                sprite.span_len.push_back(x - open);
                open = -1;
            }
            if (!hit)
                continue;
            const Surface& s = camera.surface[i];
            const Vec pos{(x + .5) * sample - hw, (y + .5) * sample - hw, camera.z[i]};
            const int qx = static_cast<int>(std::floor((dot(pos, sr) / shadow.hw * .5 + .5) * shadow.w)), qy = static_cast<int>(std::floor((dot(pos, su) / shadow.hh * .5 + .5) * shadow.h));
            const double qz = dot(pos, L);
            double visibility = 0;
            for (int by = -1; by <= 1; ++by)
                for (int bx = -1; bx <= 1; ++bx) {
                    const int a = qx + bx, b = qy + by;
                    if (a < 0 || a >= shadow.w || b < 0 || b >= shadow.h)
                        visibility += 1;
                    else
                        visibility += std::exp(-std::max(0.0, shadow.z[static_cast<std::size_t>(b) * shadow.w + a] - qz - s.radius * .8) / (radius * 2));
                }
            visibility /= 9;
            double sky[3], sun[3];
            lit(p, s, pos.z, tall, L, pos.x * unit, pos.y * unit, sky, sun);
            sprite.z.push_back(static_cast<float>(pos.z));
            sprite.sky.push_back(code(sky[0]) | code(sky[1]) << 8 | code(sky[2]) << 16);
            sprite.sun.push_back(code(sun[0] * visibility) | code(sun[1] * visibility) << 8 | code(sun[2] * visibility) << 16);
            const std::size_t cell = (static_cast<std::size_t>(y / lawn_block) * sprite.fw + x / lawn_block) * lawn_levels;
            for (int level = 0; level < lawn_levels; ++level)
                if (pos.z > tall * level / lawn_levels)
                    ++count[cell + level];
        }
    }
    sprite.row_first.push_back(static_cast<int>(sprite.span_x.size()));
    sprite.tau.resize(count.size());
    for (std::size_t i = 0; i < count.size(); ++i)
        sprite.tau[i] = static_cast<float>(-std::log(1 - std::min(.97, count[i] / static_cast<double>(lawn_block * lawn_block))));
}

struct Crew {
    void (*work)(void*, int) = nullptr;
    void* data = nullptr;
    int count = 0;
    std::atomic<int> next{0};
};
void crew_run(Crew* crew) {
    for (;;) {
        const int job = (*crew).next.fetch_add(1);
        if (job >= (*crew).count)
            return;
        (*crew).work((*crew).data, job);
    }
}
void in_parallel(void (*work)(void*, int), void* data, int count, int threads) {
    Crew crew;
    crew.work = work;
    crew.data = data;
    crew.count = count;
    std::vector<std::thread> pool;
    for (int t = 1; t < threads; ++t)
        pool.emplace_back(crew_run, &crew);
    crew_run(&crew);
    for (std::thread& thread : pool)
        thread.join();
}
int lawn_threads() {
    return std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
}

struct BakeAll {
    const GrassParameters* p;
    double tall;
    std::vector<BakeJob> jobs;
};
void bake_one(void* data, int job) {
    BakeAll& all = *static_cast<BakeAll*>(data);
    bake(*all.p, all.tall, all.jobs[static_cast<std::size_t>(job)]);
}

const double lawn_vigour[4] = {.55, .75, .95, 1.15};
const double lawn_edge_length[3] = {1.0, .64, .34};
int plant_index(const LawnLibrary& library, int edge, int vigour, int variant) {
    int at = 0;
    for (int e = 0; e < edge; ++e)
        at += library.plant_variants[e] * 4;
    return at + vigour * library.plant_variants[edge] + variant;
}

LawnLibrary make_lawn_library(const GrassParameters& p, bool with_edges) {
    LawnLibrary library;
    library.made_for = p;
    const double unit = lawn_unit(p), k = p.scale / unit;
    library.tall = (p.mulch ? .075 : p.mown ? .050 : .24) * k;
    BakeAll all;
    all.p = &p;
    all.tall = library.tall;
    if (p.mulch) {
        const int counts[3] = {120, 100, 48};
        for (int sort = 0; sort < 3; ++sort) {
            library.pieces[sort].resize(static_cast<std::size_t>(counts[sort]));
            for (int v = 0; v < counts[sort]; ++v) {
                BakeJob job;
                job.what = 2;
                job.kind = sort;
                job.reach = .075;
                job.seed = 9001U + static_cast<std::uint32_t>(sort * 1000 + v);
                job.out = &library.pieces[sort][static_cast<std::size_t>(v)];
                all.jobs.push_back(job);
            }
        }
        const double reaches[9] = {.13, .34, .36, .22, .26, .22, .32, .36, .18};
        for (int kind = 0; kind < 9; ++kind)
            for (int tone = 0; tone < 3; ++tone) {
                library.bed_plants[kind][tone].resize(4);
                for (int v = 0; v < 4; ++v) {
                    BakeJob job;
                    job.what = 3;
                    job.kind = kind;
                    job.tone = tone;
                    job.reach = reaches[kind] * 1.6 / p.scale;
                    job.seed = 12001U + static_cast<std::uint32_t>(kind * 100 + tone * 10 + v);
                    job.out = &library.bed_plants[kind][tone][static_cast<std::size_t>(v)];
                    all.jobs.push_back(job);
                }
            }
    } else {
        library.plant_variants[0] = 28;
        library.plant_variants[1] = with_edges ? 8 : 0;
        library.plant_variants[2] = with_edges ? 8 : 0;
        library.plants.resize(static_cast<std::size_t>((library.plant_variants[0] + library.plant_variants[1] + library.plant_variants[2]) * 4));
        for (int edge = 0; edge < 3; ++edge)
            for (int vigour = 0; vigour < 4; ++vigour)
                for (int v = 0; v < library.plant_variants[edge]; ++v) {
                    BakeJob job;
                    job.vigour = lawn_vigour[vigour];
                    job.length_factor = lawn_edge_length[edge];
                    job.edge_dry = edge * .5;
                    job.reach = p.mown ? .085 : .175 * 1.5 * 1.12 * job.vigour * job.length_factor + .01;
                    job.seed = 77U + static_cast<std::uint32_t>(edge * 4000 + vigour * 1000 + v);
                    job.out = &library.plants[static_cast<std::size_t>(plant_index(library, edge, vigour, v))];
                    all.jobs.push_back(job);
                }
        if (!p.mown) {
            const int counts[7] = {6, 6, 6, 6, 4, 10, 4};
            for (int kind = 0; kind < 7; ++kind) {
                library.flowers[kind].resize(static_cast<std::size_t>(counts[kind]));
                for (int v = 0; v < counts[kind]; ++v) {
                    BakeJob job;
                    job.what = 1;
                    job.kind = kind;
                    job.reach = .04;
                    job.seed = 5001U + static_cast<std::uint32_t>(kind * 100 + v);
                    job.out = &library.flowers[kind][static_cast<std::size_t>(v)];
                    all.jobs.push_back(job);
                }
            }
        }
    }
    in_parallel(bake_one, &all, static_cast<int>(all.jobs.size()), lawn_threads());
    return library;
}


struct Placed {
    int x = 0, y = 0;         // root, in field samples
    const Sprite* sprite = nullptr;
    float lift = 0;           // added to the sprite's heights
};

struct Field {
    const GrassParameters* p;
    const LawnLibrary* library;
    int width, height, w, h;  // pixels; samples
    double metres_w, metres_h, unit;
    // plants on their growing grid
    int nx = 0, ny = 0;
    double cell = 0, half_w = 0, half_h = 0;  // life-size metres
    std::vector<Placed> grid;
    int reach = 0;            // furthest any sprite spreads from its root, samples
    std::vector<Placed> extras;  // flowers and mushrooms
    // coarse maps
    int lw = 0, lh = 0;
    std::vector<float> tau, light;  // lawn_levels per cell; 3 per cell: sun reaching the ground, 1/3 and 2/3 of the way up
    Layer* image;
    float decode[256];
    std::uint8_t tone[4096];
    int tile = 128, tiles_x = 0, tiles_y = 0;
};

std::uint32_t mix(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    std::uint32_t h = a * 374761393U + b * 668265263U + c * 2246822519U;
    h = (h ^ (h >> 13U)) * 1274126177U;
    return h ^ (h >> 16U);
}

// Decide every cell of the growing grid: is there a plant, which library plant, exactly where.
void place_row(void* data, int iy) {
    Field& f = *static_cast<Field*>(data);
    const GrassParameters& p = *f.p;
    const LawnLibrary& library = *f.library;
    const int seed = static_cast<int>(p.seed);
    const double to_sample = p.pixels_per_metre * lawn_ss;
    for (int ix = 0; ix < f.nx; ++ix) {
        Placed& placed = f.grid[static_cast<std::size_t>(iy) * f.nx + ix];
        placed.sprite = nullptr;
        Random random{mix(static_cast<std::uint32_t>(ix), static_cast<std::uint32_t>(iy), p.seed)};
        if (p.mulch) {
            const double x = -f.half_w + (ix + random.uniform() * 2 - .5) * f.cell, y = -f.half_h + (iy + random.uniform() * 2 - .5) * f.cell;
            const double pick = random.uniform();
            const int sort = pick < .66 ? 0 : pick < .972 ? 1 : 2;
            const std::vector<Sprite>& set = library.pieces[sort];
            placed.sprite = &set[static_cast<std::size_t>(random.uniform() * set.size()) % set.size()];
            placed.lift = static_cast<float>((.045 * random.uniform() - .012) * p.scale / f.unit);
            placed.x = static_cast<int>(std::floor((x * p.scale + f.metres_w * .5) * to_sample + .5));
            placed.y = static_cast<int>(std::floor((y * p.scale + f.metres_h * .5) * to_sample + .5));
            continue;
        }
        const double E = p.mown ? .64 : .56, patch = p.mown ? .17 : .22;
        const double x = -f.half_w + (ix + .15 + .7 * random.uniform()) * f.cell, y = -f.half_h + (iy + .15 + .7 * random.uniform()) * f.cell;
        const double n = value_noise(x / E * 5 + 7, y / E * 5 + 11, seed + 200);
        const double hole = smooth_between(.17, .61, n), presence = .99 + (.22 + .75 * hole - .99) * patch;
        int edge_bucket = 0;
        double edge = 1;
        if (p.scene != nullptr) {
            const double near = scene_distance(p, x * p.scale, y * p.scale, f.metres_w, f.metres_h) + (value_noise(x * p.scale * 9, y * p.scale * 9, seed + 211) - .5) * .035;
            edge = p.mown ? smooth_between(.07, .15, near) : smooth_between(.10, .20, near);
            const double factor = .28 + .72 * smooth_between(.08, .34, near);
            edge_bucket = factor > .82 ? 0 : factor > .50 ? 1 : 2;
            if (library.plant_variants[edge_bucket] == 0)
                edge_bucket = 0;
        }
        if (random.uniform() > presence * edge)
            continue;
        const double vigour = std::clamp(.68 + .5 * n + random.normal() * .09, .45, 1.25);
        const int vigour_bucket = std::clamp(static_cast<int>((vigour - .45) / .2), 0, 3);
        const int variant = static_cast<int>(random.uniform() * library.plant_variants[edge_bucket]) % library.plant_variants[edge_bucket];
        placed.sprite = &library.plants[static_cast<std::size_t>(plant_index(library, edge_bucket, vigour_bucket, variant))];
        placed.x = static_cast<int>(std::floor((x * p.scale + f.metres_w * .5) * to_sample + .5));
        placed.y = static_cast<int>(std::floor((y * p.scale + f.metres_h * .5) * to_sample + .5));
    }
}

// The flowers and mushrooms the garden says grow here, as library pieces to stamp.
void place_flowers(Field& f) {
    const GrassParameters& p = *f.p;
    const LawnLibrary& library = *f.library;
    const double to_sample = p.pixels_per_metre * lawn_ss;
    std::uint32_t pick = p.seed * 2654435761U + 99U;
    for (const LawnFlower& flower : (*p.scene).flowers) {
        const std::vector<Sprite>& set = library.flowers[std::clamp(flower.kind, 0, 6)];
        pick = pick * 1664525U + 1013904223U;
        Placed placed;
        placed.sprite = &set[(pick >> 8U) % set.size()];
        placed.x = static_cast<int>(std::floor(flower.x * to_sample + .5));
        placed.y = static_cast<int>(std::floor((f.metres_h - flower.y) * to_sample + .5));
        f.extras.push_back(placed);
    }
}

// The beds' plants, as the garden placed them.
void place_plants(Field& f) {
    const GrassParameters& p = *f.p;
    const LawnLibrary& library = *f.library;
    const double to_sample = p.pixels_per_metre * lawn_ss;
    for (const LawnPlant& plant : (*p.scene).plants) {
        const std::vector<Sprite>& set = library.bed_plants[std::clamp(plant.kind, 0, 8)][std::clamp(plant.tone, 0, 2)];
        Placed placed;
        placed.sprite = &set[plant.seed % set.size()];
        placed.lift = static_cast<float>(.046 * p.scale / f.unit);  // rooted at the top of the mulch, not under it
        placed.x = static_cast<int>(std::floor(plant.x * to_sample + .5));
        placed.y = static_cast<int>(std::floor((f.metres_h - plant.y) * to_sample + .5));
        f.extras.push_back(placed);
    }
}

// Which rows of the growing grid can reach sample rows y0..y1.
void grid_rows(const Field& f, int y0, int y1, int& first, int& last) {
    const GrassParameters& p = *f.p;
    const double to_life = 1 / (p.pixels_per_metre * lawn_ss) / p.scale;
    first = std::max(0, static_cast<int>(std::floor(((y0 - f.reach) * to_life - f.metres_h / p.scale * .5 + f.half_h) / f.cell)) - 2);
    last = std::min(f.ny - 1, static_cast<int>(std::ceil(((y1 + f.reach) * to_life - f.metres_h / p.scale * .5 + f.half_h) / f.cell)) + 2);
}
void grid_columns(const Field& f, int x0, int x1, int& first, int& last) {
    const GrassParameters& p = *f.p;
    const double to_life = 1 / (p.pixels_per_metre * lawn_ss) / p.scale;
    first = std::max(0, static_cast<int>(std::floor(((x0 - f.reach) * to_life - f.metres_w / p.scale * .5 + f.half_w) / f.cell)) - 2);
    last = std::min(f.nx - 1, static_cast<int>(std::ceil(((x1 + f.reach) * to_life - f.metres_w / p.scale * .5 + f.half_w) / f.cell)) + 2);
}

// Coarse cover: add every plant's share of leaf above each level, a band of coarse rows at a time.
constexpr int cover_band = 8;
void cover_band_work(void* data, int band) {
    Field& f = *static_cast<Field*>(data);
    const int row0 = band * cover_band, row1 = std::min(f.lh, row0 + cover_band) - 1;
    int first, last;
    grid_rows(f, row0 * lawn_block, row1 * lawn_block + lawn_block - 1, first, last);
    for (int iy = first; iy <= last; ++iy)
        for (int ix = 0; ix < f.nx; ++ix) {
            const Placed& placed = f.grid[static_cast<std::size_t>(iy) * f.nx + ix];
            if (placed.sprite == nullptr)
                continue;
            const Sprite& s = *placed.sprite;
            const int bx = (placed.x + lawn_block / 2) / lawn_block + s.fx0, by = (placed.y + lawn_block / 2) / lawn_block + s.fy0;
            const int j0 = std::max(0, row0 - by), j1 = std::min(s.fh - 1, row1 - by);
            const int i0 = std::max(0, -bx), i1 = std::min(s.fw - 1, f.lw - 1 - bx);
            for (int j = j0; j <= j1; ++j) {
                const float* from = &s.tau[(static_cast<std::size_t>(j) * s.fw + i0) * lawn_levels];
                float* to = &f.tau[(static_cast<std::size_t>(by + j) * f.lw + bx + i0) * lawn_levels];
                const int count = (i1 - i0 + 1) * lawn_levels;
                for (int i = 0; i < count; ++i)
                    to[i] += from[i];
            }
        }
}

float cover_above(const Field& f, int cx, int cy, float level) {
    if (cx < 0 || cy < 0 || cx >= f.lw || cy >= f.lh || level >= lawn_levels)
        return 0;
    const float* t = &f.tau[(static_cast<std::size_t>(cy) * f.lw + cx) * lawn_levels];
    const int k = static_cast<int>(level);
    const float next = k + 1 < lawn_levels ? t[k + 1] : 0.0F;
    return t[k] + (next - t[k]) * (level - k);
}

// How much sun reaches three heights over each coarse cell: walk towards the sun, rising, through the cover.
void light_row(void* data, int cy) {
    Field& f = *static_cast<Field*>(data);
    const double tall = (*f.library).tall, cell_units = lawn_block / ((*f.p).pixels_per_metre * lawn_ss) / f.unit;
    const double horizontal = std::cos(sun_elevation), dx = std::cos(sun_azimuth), dy = std::sin(sun_azimuth);
    const float rise = static_cast<float>(cell_units * std::sin(sun_elevation) / horizontal / tall * lawn_levels);  // levels climbed per cell walked
    const float strength = (*f.p).mulch ? 3.2F : 1.6F;
    for (int cx = 0; cx < f.lw; ++cx)
        for (int from = 0; from < 3; ++from) {
            float level = from * lawn_levels / 3.0F, depth = 0;
            for (int step = 0; level < lawn_levels && step < 64; ++step) {
                const int ax = cx + static_cast<int>(std::lround(dx * (step + .5))), ay = cy + static_cast<int>(std::lround(dy * (step + .5)));
                const float upper = std::min(static_cast<float>(lawn_levels), level + rise);
                depth += std::max(0.0F, cover_above(f, ax, ay, level) - cover_above(f, ax, ay, upper));
                level = upper;
            }
            f.light[(static_cast<std::size_t>(cy) * f.lw + cx) * 3 + from] = std::exp(-strength * depth);
        }
}

void stamp(const Placed& placed, int tx0, int ty0, int tw, int th, float* fz, std::uint32_t* fsky, std::uint32_t* fsun) {
    const Sprite& s = *placed.sprite;
    const int top = placed.y + s.y0, j0 = std::max(0, ty0 - top), j1 = std::min(s.rows - 1, ty0 + th - 1 - top);
    const float lift = placed.lift;
    for (int j = j0; j <= j1; ++j) {
        const std::size_t row = static_cast<std::size_t>(top + j - ty0) * tw;
        for (int span = s.row_first[static_cast<std::size_t>(j)]; span < s.row_first[static_cast<std::size_t>(j) + 1]; ++span) {
            const int left = placed.x + s.x0 + s.span_x[static_cast<std::size_t>(span)] - tx0;
            const int a = std::max(0, -left), b = std::min(s.span_len[static_cast<std::size_t>(span)], tw - left);
            if (a >= b)
                continue;
            const std::size_t at = static_cast<std::size_t>(s.span_at[static_cast<std::size_t>(span)]);
            const float* sz = &s.z[at];
            const std::uint32_t* ssky = &s.sky[at];
            const std::uint32_t* ssun = &s.sun[at];
            float* dz = fz + row + left;
            std::uint32_t* dsky = fsky + row + left;
            std::uint32_t* dsun = fsun + row + left;
            for (int i = a; i < b; ++i) {
                const float z = sz[i] + lift;
                const bool over = z > dz[i];
                dz[i] = over ? z : dz[i];
                dsky[i] = over ? ssky[i] : dsky[i];
                dsun[i] = over ? ssun[i] : dsun[i];
            }
        }
    }
}

void tile_work(void* data, int index) {
    Field& f = *static_cast<Field*>(data);
    const GrassParameters& p = *f.p;
    const int tile_x = index % f.tiles_x, tile_y = index / f.tiles_x;
    const int px0 = tile_x * f.tile, py0 = tile_y * f.tile, pw = std::min(f.tile, f.width - px0), ph = std::min(f.tile, f.height - py0);
    const int tx0 = px0 * lawn_ss - lawn_margin, ty0 = py0 * lawn_ss - lawn_margin, tw = pw * lawn_ss + 2 * lawn_margin, th = ph * lawn_ss + 2 * lawn_margin;
    std::vector<float> z(static_cast<std::size_t>(tw) * th, -1.0F);
    std::vector<std::uint32_t> sky(z.size(), 0), sun(z.size(), 0);
    int row_first, row_last, column_first, column_last;
    grid_rows(f, ty0, ty0 + th - 1, row_first, row_last);
    grid_columns(f, tx0, tx0 + tw - 1, column_first, column_last);
    for (int iy = row_first; iy <= row_last; ++iy)
        for (int ix = column_first; ix <= column_last; ++ix) {
            const Placed& placed = f.grid[static_cast<std::size_t>(iy) * f.nx + ix];
            if (placed.sprite != nullptr)
                stamp(placed, tx0, ty0, tw, th, z.data(), sky.data(), sun.data());
        }
    for (const Placed& placed : f.extras)
        if (placed.x + 200 > tx0 && placed.x - 200 < tx0 + tw && placed.y + 200 > ty0 && placed.y - 200 < ty0 + th)
            stamp(placed, tx0, ty0, tw, th, z.data(), sky.data(), sun.data());
    const double sample_m = 1 / (p.pixels_per_metre * lawn_ss);
    if (p.scene != nullptr && !p.mulch) {
        // the bare strip at an object's foot: nothing overhangs it
        for (int by = 0; by < th; by += lawn_block)
            for (int bx = 0; bx < tw; bx += lawn_block) {
                const double mx = (tx0 + bx + lawn_block * .5) * sample_m - f.metres_w * .5, my = (ty0 + by + lawn_block * .5) * sample_m - f.metres_h * .5;
                if (scene_distance(p, mx, my, f.metres_w, f.metres_h) > .055 + lawn_block * sample_m)
                    continue;
                for (int y = by; y < std::min(th, by + lawn_block); ++y)
                    for (int x = bx; x < std::min(tw, bx + lawn_block); ++x)
                        if (scene_distance(p, (tx0 + x + .5) * sample_m - f.metres_w * .5, (ty0 + y + .5) * sample_m - f.metres_h * .5, f.metres_w, f.metres_h) < .055)
                            z[static_cast<std::size_t>(y) * tw + x] = -1.0F;
            }
    }
    // close shadows: a few looks towards the sun across the tile's own heights
    const float sample_units = static_cast<float>(sample_m / f.unit), climb = static_cast<float>(std::tan(sun_elevation)) * sample_units;
    const int taps = 4, tap_distance[4] = {2, 4, 7, 11};
    int tap_offset[4];
    float tap_rise[4];
    for (int k = 0; k < taps; ++k) {
        const int ox = static_cast<int>(std::lround(std::cos(sun_azimuth) * tap_distance[k])), oy = static_cast<int>(std::lround(std::sin(sun_azimuth) * tap_distance[k]));
        tap_offset[k] = oy * tw + ox;
        tap_rise[k] = static_cast<float>(std::hypot(ox, oy)) * climb;
    }
    const float soften = 1 / (2.5F * climb), close_strength = p.mulch ? 1.0F : .45F;
    const float inv_tall = static_cast<float>(1 / (*f.library).tall);
    const float sun_z = static_cast<float>(std::sin(sun_elevation));
    const float exposure = static_cast<float>(p.mulch ? 1.0 : p.mown ? .76 : .80) / (lawn_ss * lawn_ss);
    const float sun_colour[3] = {1.03F, .90F, .76F};
    for (int y = 0; y < ph; ++y) {
        std::uint32_t* out = &(*f.image).px[static_cast<std::size_t>(f.height - 1 - (py0 + y)) * f.width + px0];
        for (int x = 0; x < pw; ++x) {
            // sun reaching this pixel at three heights, from the four coarse cells around it
            const float gx = std::max(0.0F, ((tx0 + lawn_margin + x * lawn_ss + 1.0F) / lawn_block) - .5F), gy = std::max(0.0F, ((ty0 + lawn_margin + y * lawn_ss + 1.0F) / lawn_block) - .5F);
            const int cx = std::min(static_cast<int>(gx), f.lw - 2), cy = std::min(static_cast<int>(gy), f.lh - 2);
            const float ux = std::min(1.0F, gx - cx), uy = std::min(1.0F, gy - cy);
            const float* l00 = &f.light[(static_cast<std::size_t>(cy) * f.lw + cx) * 3];
            const float* l01 = l00 + 3;
            const float* l10 = l00 + static_cast<std::size_t>(f.lw) * 3;
            const float* l11 = l10 + 3;
            float reach[4];
            for (int k = 0; k < 3; ++k)
                reach[k] = (l00[k] * (1 - ux) + l01[k] * ux) * (1 - uy) + (l10[k] * (1 - ux) + l11[k] * ux) * uy;
            reach[3] = 1;
            const float* cover = &f.tau[(static_cast<std::size_t>(std::min(static_cast<int>(gy + .5F), f.lh - 1)) * f.lw + std::min(static_cast<int>(gx + .5F), f.lw - 1)) * lawn_levels];
            float sum[3] = {0, 0, 0};
            for (int sy = 0; sy < lawn_ss; ++sy)
                for (int sx = 0; sx < lawn_ss; ++sx) {
                    const std::size_t i = static_cast<std::size_t>(y * lawn_ss + sy + lawn_margin) * tw + x * lawn_ss + sx + lawn_margin;
                    const float height = z[i];
                    float close = 1;
                    for (int k = 0; k < taps; ++k) {
                        const float over = z[i + tap_offset[k]] - std::max(height, 0.0F) - tap_rise[k];
                        close *= 1 - close_strength * std::clamp(over * soften, 0.0F, 1.0F);
                    }
                    if (height < 0) {
                        // bare ground, as the slow renderer colours it
                        const double ux_units = ((tx0 + lawn_margin + x * lawn_ss + sx + .5) * sample_m - f.metres_w * .5) / f.unit, uy_units = ((ty0 + lawn_margin + y * lawn_ss + sy + .5) * sample_m - f.metres_h * .5) / f.unit;
                        const double grit = value_noise(ux_units * 60, uy_units * 60, 5), clump = value_noise(ux_units * 17, uy_units * 17, 9), broad = value_noise(ux_units * 2.2, uy_units * 2.2, 13);
                        const double bare = p.scene == nullptr ? 0 : 1 - smooth_between(.05, .16, scene_distance(p, ux_units * f.unit, uy_units * f.unit, f.metres_w, f.metres_h));
                        const double lo[3] = {.029, .020, .009}, hi[3] = {.068, .047, .022}, moss[3] = {.019, .025, .007}, dirt[3] = {.115, .078, .046};
                        const double shade = .45 + .55 * reach[0];
                        for (int c = 0; c < 3; ++c) {
                            double soil = (lo[c] + (hi[c] - lo[c]) * clump) * (.65 + grit * .72) * (.80 + .35 * broad);
                            soil += (moss[c] - soil) * .19;
                            soil += (dirt[c] * (.55 + .9 * grit * clump) - soil) * bare;
                            sum[c] += static_cast<float>(soil * (.30 + sun_z * 1.2 * reach[0] * close) * shade) * sun_colour[c];
                        }
                        continue;
                    }
                    const float t = std::min(2.999F, std::max(0.0F, height * inv_tall * 3));
                    const int k = static_cast<int>(t);
                    const float lightness = (reach[k] + (reach[k + 1] - reach[k]) * (t - k)) * close;
                    float sky_scale = 1;
                    if (p.mulch) {
                        // a piece deep in the pile sees little sky
                        const float level = std::min(lawn_levels - 1.001F, std::max(0.0F, height * inv_tall * lawn_levels));
                        const int q = static_cast<int>(level);
                        const float above = cover[q] + (cover[q + 1] - cover[q]) * (level - q);
                        sky_scale = (.10F + .90F * std::exp(-3.0F * std::max(0.0F, 1 - height * inv_tall / .82F))) * std::exp(-1.1F * above);
                    }
                    const std::uint32_t a = sky[i], b = sun[i];
                    sum[0] += f.decode[a & 255] * sky_scale + f.decode[b & 255] * lightness;
                    sum[1] += f.decode[(a >> 8) & 255] * sky_scale + f.decode[(b >> 8) & 255] * lightness;
                    sum[2] += f.decode[(a >> 16) & 255] * sky_scale + f.decode[(b >> 16) & 255] * lightness;
                }
            const std::uint32_t red = f.tone[std::min(4095, static_cast<int>(std::sqrt(sum[0] * exposure * .125F) * 4095))];
            const std::uint32_t green = f.tone[std::min(4095, static_cast<int>(std::sqrt(sum[1] * exposure * .125F) * 4095))];
            const std::uint32_t blue = f.tone[std::min(4095, static_cast<int>(std::sqrt(sum[2] * exposure * .125F) * 4095))];
            out[x] = 0xFF000000U | (red << 16U) | (green << 8U) | blue;
        }
    }
}

Layer render_field(const LawnLibrary& library, const GrassParameters& p, int width, int height) {
    const int threads = lawn_threads();
    Field f;
    f.p = &p;
    f.library = &library;
    f.width = width;
    f.height = height;
    f.w = width * lawn_ss;
    f.h = height * lawn_ss;
    f.metres_w = width / p.pixels_per_metre;
    f.metres_h = height / p.pixels_per_metre;
    f.unit = lawn_unit(p);
    if (p.mulch) {
        f.cell = 1 / std::sqrt(16000.0);
        f.half_w = f.metres_w / p.scale * .5 + .06;
        f.half_h = f.metres_h / p.scale * .5 + .06;
    } else {
        const double E = p.mown ? .64 : .56, stage = p.mown ? .39 : .29, density = p.mown ? .86 : .79 * .60;
        f.cell = 1 / std::sqrt((1700 + 4300 * (1 - stage)) * density / (E * E));
        f.half_w = f.metres_w / p.scale * .5 + .12;
        f.half_h = f.metres_h / p.scale * .5 + .12;
    }
    f.nx = static_cast<int>(std::ceil(2 * f.half_w / f.cell));
    f.ny = static_cast<int>(std::ceil(2 * f.half_h / f.cell));
    f.grid.resize(static_cast<std::size_t>(f.nx) * f.ny);
    for (const Sprite& s : library.plants)
        f.reach = std::max(f.reach, std::max(-s.x0, std::max(-s.y0, s.rows + s.y0)));
    for (int sort = 0; sort < 3; ++sort)
        for (const Sprite& s : library.pieces[sort])
            f.reach = std::max(f.reach, std::max(-s.x0, std::max(-s.y0, s.rows + s.y0)));
    in_parallel(place_row, &f, f.ny, threads);
    if (!p.mulch && !p.mown && p.scene != nullptr)
        place_flowers(f);
    if (p.mulch && p.scene != nullptr)
        place_plants(f);
    f.lw = f.w / lawn_block + 2;
    f.lh = f.h / lawn_block + 2;
    f.tau.assign(static_cast<std::size_t>(f.lw) * f.lh * lawn_levels, 0.0F);
    f.light.assign(static_cast<std::size_t>(f.lw) * f.lh * 3, 1.0F);
    in_parallel(cover_band_work, &f, (f.lh + cover_band - 1) / cover_band, threads);
    in_parallel(light_row, &f, f.lh, threads);
    for (int i = 0; i < 256; ++i)
        f.decode[i] = static_cast<float>(i / 255.0 * (i / 255.0) * 4);
    for (int i = 0; i < 4096; ++i) {
        const double root = (i + .5) / 4095, v = root * root * 8;
        const double aces = std::clamp((v * (2.51 * v + .03)) / (v * (2.43 * v + .59) + .14), 0.0, 1.0);
        f.tone[i] = static_cast<std::uint8_t>(std::lround(255 * std::pow(aces, 1 / 2.2)));
    }
    Layer image;
    image.width = width;
    image.height = height;
    image.px.assign(static_cast<std::size_t>(width) * height, 0xFF000000U);
    f.image = &image;
    f.tiles_x = (width + f.tile - 1) / f.tile;
    f.tiles_y = (height + f.tile - 1) / f.tile;
    in_parallel(tile_work, &f, f.tiles_x * f.tiles_y, threads);
    return image;
}


struct Kits {
    std::mutex guard;
    std::map<std::pair<int, int>, std::shared_ptr<const LawnLibrary>> made;
};
Kits& kits() {
    static Kits all;
    return all;
}

} // namespace

Layer render_lawn(LawnLook look, const LawnScene& scene, std::uint64_t seed, double metres_w, double metres_h, double pixels_per_metre) {
    GrassParameters p;
    p.pixels_per_metre = pixels_per_metre;
    p.mown = look != LawnLook::tall;
    p.mulch = look == LawnLook::mulch || look == LawnLook::beds;
    if (look == LawnLook::mown_dark || look == LawnLook::mown_light || look == LawnLook::mown_quarter || look == LawnLook::mown_three_quarter) {
        p.lay = .8;
        p.wetness = .7;
        p.lay_turn = look == LawnLook::mown_light ? pi : look == LawnLook::mown_quarter ? pi / 2 : look == LawnLook::mown_three_quarter ? 3 * pi / 2 : 0;
    }
    std::shared_ptr<const LawnLibrary> kit;
    {
        Kits& all = kits();
        const std::lock_guard<std::mutex> lock(all.guard);
        const std::pair<int, int> key{static_cast<int>(p.mulch ? LawnLook::mulch : look), static_cast<int>(std::lround(pixels_per_metre * 16))};
        std::shared_ptr<const LawnLibrary>& slot = all.made[key];
        if (!slot)
            slot = std::make_shared<const LawnLibrary>(make_lawn_library(p, true));
        kit = slot;
    }
    // the short grass grows from the same seed both ways, so the two lays are one lawn
    p.seed = static_cast<std::uint32_t>(seed * 2654435761ULL + (p.mulch ? 17U : p.mown ? 5U : 1U));
    p.scene = look == LawnLook::mulch ? nullptr : &scene;
    const int width = std::max(1, static_cast<int>(std::lround(metres_w * pixels_per_metre))), height = std::max(1, static_cast<int>(std::lround(metres_h * pixels_per_metre)));
    return render_field(*kit, p, width, height);
}


// A tree's canopy as the garden's camera sees it: from the front and above, at the angle the
// yard is drawn (the ground foreshortened to 0.78, heights to 0.62), so the canopy has its
// sides as well as its top, and its far side is hidden by its near side.
Cutout render_tree(int kind, double reach, std::uint64_t seed, double pixels_per_metre) {
    // A full, rounded head of leaves, a little wider than the reach the garden plans for.
    const double crown = reach * 1.22;
    GrassParameters p;
    p.pixels_per_metre = pixels_per_metre;
    p.mown = false;
    const double unit = lawn_unit(p), k = p.scale / unit;
    const int ss = 2;
    const double sample = 1 / (pixels_per_metre * ss) / unit;  // renderer units per sample
    // Towards the viewer: south (−y here, where y runs up the lawn) and up.
    const double tilt = 0.78, rise = 0.62;
    const Basis view(Vec{0, -rise, tilt});
    const double hw = (crown * 1.25 + 0.25) / unit;
    // The raster is centred on the canopy's root (the top of the trunk): room above for the
    // crown standing up the picture, and below for its front hanging towards the viewer.
    const double hh = (crown * 1.9 + 0.3) / unit;
    const int w = static_cast<int>(std::ceil(2 * hw / sample));
    const int h = static_cast<int>(std::ceil(2 * hh / sample));
    Raster<true> camera(w, h, hw, hh, view);
    const double tall = crown * 1.7 / unit;
    const double shadow_half = hw * 1.6 + tall;
    const int shadow_side = std::clamp(static_cast<int>(w * 1.4), 64, 1600);
    Raster<false> shadow(shadow_side, shadow_side, shadow_half, shadow_half,
                         Basis({std::cos(sun_azimuth) * std::cos(sun_elevation), std::sin(sun_azimuth) * std::cos(sun_elevation), std::sin(sun_elevation)}));
    Random random{static_cast<std::uint32_t>(seed * 2654435761ULL + 3U)};
    emit_tree(std::clamp(kind, 0, 3), crown, random, k, p.scale, camera, shadow);
    const Vec L = shadow.basis.v, sr = shadow.basis.r, su = shadow.basis.u;
    const double radius = .010;
    Cutout cut{};
    cut.ppm = pixels_per_metre;
    // The picture keeps only the rows the canopy reaches.
    int top = h, bottom = -1, left = w, right = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (camera.surface[static_cast<std::size_t>(y) * w + x].radius > 0) {
                top = std::min(top, y);
                bottom = std::max(bottom, y);
                left = std::min(left, x);
                right = std::max(right, x);
            }
    if (bottom < 0)
        return cut;
    left = left / ss * ss;
    top = top / ss * ss;
    const int pw = (right - left) / ss + 1, ph = (bottom - top) / ss + 1;
    std::vector<float> sum(static_cast<std::size_t>(pw) * ph * 4, 0.0F);
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * w + x;
            const Surface& s = camera.surface[i];
            if (s.radius <= 0)
                continue;
            // Back from the camera's axes to the garden's.
            const double X = (x + .5) * sample - hw, Y = (y + .5) * sample - hh, Z = camera.z[i];
            const Vec pos = view.r * X + view.u * Y + view.v * Z;
            Surface world = s;
            world.normal = view.r * s.normal.x + view.u * s.normal.y + view.v * s.normal.z;
            world.tangent = view.r * s.tangent.x + view.u * s.tangent.y + view.v * s.tangent.z;
            const int qx = static_cast<int>(std::floor((dot(pos, sr) / shadow.hw * .5 + .5) * shadow.w));
            const int qy = static_cast<int>(std::floor((dot(pos, su) / shadow.hh * .5 + .5) * shadow.h));
            const double qz = dot(pos, L);
            double visibility = 0;
            for (int by = -1; by <= 1; ++by)
                for (int bx = -1; bx <= 1; ++bx) {
                    const int a = qx + bx, b = qy + by;
                    if (a < 0 || a >= shadow.w || b < 0 || b >= shadow.h)
                        visibility += 1;
                    else
                        visibility += std::exp(-std::max(0.0, shadow.z[static_cast<std::size_t>(b) * shadow.w + a] - qz - s.radius * .8) / (radius * 2));
                }
            visibility /= 9;
            double sky[3], sun[3];
            lit(p, world, pos.z, tall, L, pos.x * unit, pos.y * unit, sky, sun);
            const int px = (x - left) / ss, py = (bottom - y) / ss;
            float* cell = &sum[(static_cast<std::size_t>(py) * pw + px) * 4];
            for (int c = 0; c < 3; ++c)
                cell[c] += static_cast<float>(sky[c] + sun[c] * visibility);
            cell[3] += 1;
        }
    cut.image.width = pw;
    cut.image.height = ph;
    cut.image.px.assign(static_cast<std::size_t>(pw) * ph, 0U);
    for (int y = 0; y < ph; ++y)
        for (int x = 0; x < pw; ++x) {
            const float* cell = &sum[(static_cast<std::size_t>(y) * pw + x) * 4];
            if (cell[3] <= 0)
                continue;
            const double cover = cell[3] / (ss * ss);
            std::uint32_t channel[3];
            for (int c = 0; c < 3; ++c) {
                const double v = cell[c] / cell[3] * .52;
                const double aces = std::clamp((v * (2.51 * v + .03)) / (v * (2.43 * v + .59) + .14), 0.0, 1.0);
                channel[c] = static_cast<std::uint32_t>(std::lround(255 * std::pow(aces, 1 / 2.2) * cover));
            }
            const std::uint32_t alpha = static_cast<std::uint32_t>(std::lround(255 * cover));
            cut.image.px[static_cast<std::size_t>(y) * pw + x] = (alpha << 24U) | (channel[0] << 16U) | (channel[1] << 8U) | channel[2];
        }
    // Where the canopy's root (the top of the trunk) falls in the picture.
    const double root_sample_x = hw / sample, root_sample_y = hh / sample;
    cut.root_x = (root_sample_x - left) / ss;
    cut.root_y = (bottom + 1 - root_sample_y) / ss;
    return cut;
}

} // namespace mm
