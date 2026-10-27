// Shell-ness, grown and broken: four kinds of piece, each a solid made first and then
// broken, so only a section of it is kept (rounded, squarish or oddly shaped, often small
// and seen close):
//   0 a fragment of a fossil shell: a ribbed valve grown from its hinge, its growth lines,
//     its thick fossil wall, broken out round a spot on the valve;
//   1 a section of rock bearing a shell's mould: a lumpy rock, flat broken faces, and the
//     ribbed outside of a valve pressed into its top as a concave scallop;
//   2 a scalloped rock: a lumpy rock with broad concave hollows taken out of it, as flakes
//     leave on a knapped stone;
//   3 a pottery shard: a thrown vessel's wall with its throwing ridges, now and then its rim
//     and an incised band, broken out round a spot on the wall.
// Each is meshed as a solid by grow_from (wood_volume.cpp).
#include "shapes.hpp"

#include "noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace stones {
namespace {

constexpr double pi = 3.14159265358979323846;

double unit_hash(int a, int b, std::uint32_t key) {
    return (hash_signed(a, b, 23, key) + 1) * .5;
}

// A rough fracture: a plane whose broken side is outside, roughened.
struct Fracture {
    double n[3] = {1, 0, 0};
    double offset = 0;
};

struct Piece final : Solid {
    int kind = 0;
    std::uint32_t key = 1;
    double middle[3] = {0, 0, 0};
    std::vector<Fracture> fractures;
    // The shell's valve: its hinge at the origin, opening along +y, domed up z.
    double span = 2.0;         // the fan's angle
    double reach = 1.0;        // the valve's length from the hinge
    double height = .45;       // its dome
    double ribs = 18;
    double rib = .035;
    double growth = .006;
    double wall = .1;
    // The rock.
    double axes[3] = {1, .8, .55};
    double lumps = .08;
    std::vector<std::array<double, 4>> scoops;  // concave hollows: centre, radius
    double mould_top = 0;       // where a mould is pressed in
    double mould_depth = .25;
    // The pot.
    double pot_radius = 1;
    double bulge = .15;
    double pot_top = .9;
    double band_at = 0;
    bool banded = false;

    // The valve's own surface height above its hinge plane at (x, y), and whether (x, y)
    // lies within its outline; the ribs and growth lines on its outside when `outside`.
    double valve(double x, double y, bool outside, double& edge) const {
        const double rho = std::sqrt(x * x + y * y);
        const double phi = std::atan2(x, y);
        const double half = span * .5;
        const double limit = reach * (1 - .12 * (phi / half) * (phi / half));
        edge = std::max(rho - limit, (std::abs(phi) - half) * std::max(rho, .1));
        const double u = std::min(1.0, rho / limit);
        double z = height * std::sqrt(std::max(0.0, 1 - u * u)) * (1 - .25 * (phi / half) * (phi / half));
        if (outside) {
            const double crest = std::pow(std::abs(std::cos(ribs * phi * .5)), .6) * 2 - 1;
            z += rib * crest * smoothstep(.08, .35, u);
            const double line = rho * 70 + 2 * noise2(phi * 3, rho * 4, key + 3);
            z += growth * (line - std::floor(line) - .5) + .004 * std::sin(rho * 23);
        }
        return z;
    }

    static double smoothstep(double edge0, double edge1, double x) {
        const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
        return t * t * (3 - 2 * t);
    }

    double rock(double x, double y, double z) const {
        const double ex = x / axes[0];
        const double ey = y / axes[1];
        const double ez = z / axes[2];
        const double r = std::sqrt(ex * ex + ey * ey + ez * ez);
        return (r - 1) * std::min({axes[0], axes[1], axes[2]}) + lumps * fbm3(x * 1.8, y * 1.8, z * 1.8, key + 5, 4);
    }

    double density(double x, double y, double z) const override {
        double d = 0;
        if (kind == 0) {
            double edge = 0;
            const double outer = valve(x, y, true, edge);
            double inner_edge = 0;
            const double inner = valve(x, y, false, inner_edge) - wall;
            d = std::max({z - outer, inner - z, edge});
        } else if (kind == 1 || kind == 2) {
            d = rock(x, y, z);
            if (kind == 1) {
                // The valve's outside pressed into the rock's top, hinge at the middle.
                double edge = 0;
                const double vx = x;
                const double vy = y + reach * .45;
                double plain_edge = 0;
                const double smooth = valve(vx, vy, false, plain_edge);
                const double ribbed = valve(vx, vy, true, edge);
                // The ribs printed deeper than the dome, so the mould reads as a shell's.
                const double imprint = smooth / height * mould_depth + (ribbed - smooth) * 2.2;
                if (edge < 0) {
                    const double cut = mould_top - imprint;
                    d = std::max(d, z - cut);
                }
            }
            for (const std::array<double, 4>& scoop : scoops) {
                const double cx = x - scoop[0];
                const double cy = y - scoop[1];
                const double cz = z - scoop[2];
                d = std::max(d, scoop[3] - std::sqrt(cx * cx + cy * cy + cz * cz));
            }
        } else {
            // A thrown pot: its wall about the upright, ridged by the fingers as it turned.
            const double rr = std::sqrt(x * x + y * y);
            const double profile = pot_radius * (1 + bulge * std::sin((z + 1) * 1.4));
            const double ridges = .014 * std::sin(z * 38 + 1.3 * noise2(z * 2, std::atan2(y, x), key + 7));
            double outer = profile + ridges;
            if (banded && std::abs(z - band_at) < .025)
                outer -= .02 * (1 - std::abs(z - band_at) / .025);
            const double inner = profile - wall;
            d = std::max(rr - outer, inner - rr);
            // The rim: a rounded lip at the top.
            const double lip = std::sqrt((rr - profile + wall * .3) * (rr - profile + wall * .3) +
                                         (z - pot_top) * (z - pot_top)) - wall * .75;
            d = z > pot_top ? lip : std::min(d, lip);
            d = std::max(d, -1.2 - z);
        }
        // The breaks that freed the section, rough.
        const double mx = x - middle[0];
        const double my = y - middle[1];
        const double mz = z - middle[2];
        for (const Fracture& f : fractures) {
            const double plane = mx * f.n[0] + my * f.n[1] + mz * f.n[2] - f.offset;
            d = std::max(d, plane + .025 * fbm3(x * 5, y * 5, z * 5, key + 11, 3));
        }
        // A fossil's or a shard's weathering: shallow pitting everywhere.
        return d + .006 * fbm3(x * 14, y * 14, z * 14, key + 13, 2);
    }
};

// Fractures round a middle: `count` of them, their normals spread round `up`'s horizon
// (tilted a little), each `size` and a bit from the middle.
void break_round(Piece& piece, const double up[3], int count, double size, std::uint32_t key) {
    // Two directions across `up`.
    double a[3] = {1, 0, 0};
    if (std::abs(up[0]) > .8) {
        a[0] = 0;
        a[1] = 1;
    }
    const double dot = a[0] * up[0] + a[1] * up[1] + a[2] * up[2];
    for (int k = 0; k < 3; ++k)
        a[k] -= dot * up[k];
    const double la = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    for (double& v : a)
        v /= la;
    const double b[3] = {up[1] * a[2] - up[2] * a[1], up[2] * a[0] - up[0] * a[2], up[0] * a[1] - up[1] * a[0]};
    const double start = 2 * pi * unit_hash(0, 31, key);
    for (int k = 0; k < count; ++k) {
        const double angle = start + 2 * pi * (k + .55 * (unit_hash(k, 32, key) - .5)) / count;
        const double tilt = .35 * (unit_hash(k, 33, key) - .5);
        Fracture f;
        for (int i = 0; i < 3; ++i)
            f.n[i] = std::cos(angle) * a[i] + std::sin(angle) * b[i] + tilt * up[i];
        const double l = std::sqrt(f.n[0] * f.n[0] + f.n[1] * f.n[1] + f.n[2] * f.n[2]);
        for (double& v : f.n)
            v /= l;
        f.offset = size * (.7 + .55 * unit_hash(k, 34, key));
        piece.fractures.push_back(f);
    }
}

}  // namespace

void Shape::grow_shell() {
    const std::array<double, 16>& f = measures_.feature;
    Piece piece;
    piece.kind = static_cast<int>(f[0]);
    piece.key = static_cast<std::uint32_t>(f[4]);
    piece.ribs = f[2];
    piece.rib = f[3];
    piece.wall = f[5];
    piece.span = f[6];
    piece.height = f[7];
    piece.growth = f[13];
    const std::uint32_t key = piece.key;
    const int count = static_cast<int>(f[8]);
    const double size = f[1];
    double box = 1.6;
    const double up[3] = {0, 0, 1};
    if (piece.kind == 0) {
        // A spot on the valve, and the section broken round it.
        const double rho = f[9] * piece.reach;
        const double phi = f[10] * piece.span;
        piece.middle[0] = rho * std::sin(phi);
        piece.middle[1] = rho * std::cos(phi);
        double edge = 0;
        piece.middle[2] = piece.valve(piece.middle[0], piece.middle[1], false, edge) - piece.wall * .5;
        break_round(piece, up, count, size, key);
        box = size * 1.5 + .2;
    } else if (piece.kind == 1 || piece.kind == 2) {
        piece.axes[0] = .9 + .3 * unit_hash(1, 41, key);
        piece.axes[1] = .7 + .3 * unit_hash(2, 41, key);
        piece.axes[2] = .45 + .2 * unit_hash(3, 41, key);
        piece.lumps = f[11];
        // A few flat broken faces.
        break_round(piece, up, std::max(2, count - 2), .65, key + 3);
        if (piece.kind == 1) {
            piece.mould_top = piece.axes[2] * .85;
            piece.mould_depth = .15 + .15 * unit_hash(4, 41, key);
            piece.reach = .75 + .25 * unit_hash(5, 41, key);
        } else {
            // Broad hollows: big spheres just grazing the rock, all round it.
            const int scoops = static_cast<int>(f[14]);
            for (int k = 0; k < scoops; ++k) {
                const double zz = unit_hash(k, 42, key) * 1.6 - .8;
                const double a = 2 * pi * unit_hash(k, 43, key);
                const double rr = std::sqrt(std::max(0.0, 1 - zz * zz));
                const double dir[3] = {rr * std::cos(a), rr * std::sin(a), zz};
                const double radius = .45 + .5 * unit_hash(k, 44, key);
                const double bite = .06 + .1 * unit_hash(k, 45, key);
                const double out = 1 + radius - bite;
                piece.scoops.push_back({dir[0] * piece.axes[0] * out, dir[1] * piece.axes[1] * out,
                                        dir[2] * piece.axes[2] * out, radius});
            }
        }
        box = 1.5;
    } else {
        // A spot on the pot's wall, near the rim now and then, and the shard broken round it.
        piece.pot_radius = .55 + .25 * unit_hash(10, 41, key);
        piece.bulge = f[11];
        piece.pot_top = .9;
        piece.wall = f[5] * .7;
        piece.banded = unit_hash(6, 41, key) > .5;
        piece.band_at = -.2 + .7 * unit_hash(7, 41, key);
        const double angle = 2 * pi * unit_hash(8, 41, key);
        const double z = f[12] > .6 ? piece.pot_top - size * .5 : -.6 + 1.2 * unit_hash(9, 41, key);
        const double radius = piece.pot_radius * (1 + piece.bulge * std::sin((z + 1) * 1.4)) - piece.wall * .5;
        piece.middle[0] = radius * std::cos(angle);
        piece.middle[1] = radius * std::sin(angle);
        piece.middle[2] = z;
        const double outward[3] = {std::cos(angle), std::sin(angle), 0};
        break_round(piece, outward, count, size, key);
        // Only this wall: not the pot's far side.
        Fracture in;
        in.n[0] = -outward[0];
        in.n[1] = -outward[1];
        in.offset = .3;
        piece.fractures.push_back(in);
        box = size * 1.5 + .2;
    }
    const double grain[3] = {1, 0, 0};
    grow_from(piece, piece.middle, box, grain, piece.middle);
}

}  // namespace stones
