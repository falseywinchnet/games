// Chunks of pulled candy, grown and then cut, snapped or torn. Every chunk is about the same
// size and much the same lump: what tells one family from another is the candy's own
// structure, the strands pulling left along it, a fold's seams, a cut or a glassy break.
// Four kinds of chunk:
//   0 cut taffy: a length of a soft pulled rope, slumped where it lay, cut across at both
//     ends, a little pinched by the blades;
//   1 broken hard candy: a fat pulled rope set hard, snapped at both ends and broken along
//     its sides across glassy faces that bow and ripple out from where each break began;
//   2 folded taffy: two or three lengths pulled and laid against each other, run together
//     with seams between them, cut across;
//   3 torn taffy: cut at one end and torn at the other, where it drew out thinner and its
//     strands parted.
// Pulling leaves fine lines along every strand, and a glassy break ripples: both are the
// solid's relief, too fine for its mesh. Each chunk is meshed by grow_from (wood_volume.cpp).
#include "shapes.hpp"

#include "noise.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace stones {
namespace {

constexpr double pi = 3.14159265358979323846;

double unit_hash(int a, int b, std::uint32_t key) {
    return (hash_signed(a, b, 29, key) + 1) * .5;
}

double smoothstep(double edge0, double edge1, double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

// A cut or a break: a plane through `at` facing `n` (outside the chunk). A glassy break is
// bowed out or in about where it began and rippled outwards from there; a cut is flat.
struct Break {
    double n[3] = {1, 0, 0};
    double at[3] = {0, 0, 0};
    double bow = 0;
    double ripple = 0;
    bool glassy = false;

    // How far outside a point lies, and how far across the face from where it began.
    double value(double x, double y, double z, double& lateral) const {
        const double dx = x - at[0];
        const double dy = y - at[1];
        const double dz = z - at[2];
        const double q = dx * n[0] + dy * n[1] + dz * n[2];
        lateral = std::sqrt(std::max(0.0, dx * dx + dy * dy + dz * dz - q * q));
        return q + bow * lateral * lateral + ripple * std::sin(lateral * 40) * std::exp(-lateral * 1.4);
    }
};

// What a point's surface is, for its relief.
struct Hit {
    int surface = 0;      // 0 the candy's own skin, 1 a cut, 2 a glassy break
    double s = 0;         // along a strand
    double across = 0;    // round it: the strands' phase, as a length
    double lateral = 0;   // across a break from where it began
};

// One pulled rope: a path in the plane, its strands twisted along it. Each end is open
// (running straight on, to be cut or broken) or torn (drawn thinner, its strands parted).
struct Rope {
    std::vector<double> px, py, ps;
    double height = 0;      // its height
    double radius = .45;
    int strands = 4;
    double part = .05;      // how deep the strands part, a share of the radius
    double twist = 1;       // radians a unit along
    double phase = 0;
    double sag = .8;        // its height a share of its width, slumped where it lay
    double pinch = 0;       // narrowed near each end, where the blades closed
    double half = 1;        // where the blades closed, from the middle of the path
    bool torn[2] = {false, false};

    double length() const {
        return ps.back();
    }

    double value(double x, double y, double z_at, Hit& hit) const {
        const double z = z_at - height;
        double best = 1e18;
        double s = 0;
        double u = 0;
        double along = 0;
        const std::size_t last = px.size() - 2;
        for (std::size_t i = 0; i + 1 < px.size(); ++i) {
            const double dx = px[i + 1] - px[i];
            const double dy = py[i + 1] - py[i];
            const double l = std::sqrt(dx * dx + dy * dy);
            const double t = ((x - px[i]) * dx + (y - py[i]) * dy) / (l * l);
            const double low = (i == 0 && !torn[0]) ? -100.0 : 0.0;
            const double high = (i == last && !torn[1]) ? 100.0 : 1.0;
            const double clamped = std::clamp(t, low, high);
            const double qx = px[i] + clamped * dx;
            const double qy = py[i] + clamped * dy;
            const double e = (x - qx) * (x - qx) + (y - qy) * (y - qy) + z * z;
            if (e < best) {
                best = e;
                s = ps[i] + clamped * l;
                u = ((x - qx) * -dy + (y - qy) * dx) / l;
                along = ((x - qx) * dx + (y - qy) * dy) / l;
            }
        }
        const double total = length();
        const double middle = total * .5;
        double r = radius;
        double parting = part;
        for (int end = 0; end < 2; ++end) {
            const double from = end == 0 ? s : total - s;
            if (torn[end]) {
                // Drawn out and torn: thinner, the strands pulled apart.
                const double near = 1 - smoothstep(0, .55, from);
                r *= 1 - .3 * near;
                parting += .16 * near;
            }
        }
        // The blades' pinch, at the cuts.
        const double to_cut = half - std::abs(s - middle);
        r *= 1 - pinch * (1 - smoothstep(0, .4, to_cut));
        const double v = z / sag;
        const double rho = std::sqrt(u * u + v * v + along * along);
        const double theta = std::atan2(v, u);
        const double turned = theta - twist * s + phase;
        const double crest = std::pow(std::abs(std::cos(strands * turned * .5)), .55);
        hit.s = s;
        hit.across = turned * radius;
        return (rho - r * (1 - parting + parting * crest)) * sag;
    }
};

double smooth_min(double a, double b, double k) {
    const double h = std::clamp(.5 + .5 * (b - a) / k, 0.0, 1.0);
    return b + (a - b) * h - k * h * (1 - h);
}

struct Candy final : Solid {
    std::uint32_t key = 1;
    double fine = .0012;    // the fine lines' height
    double lumps = .04;
    double blend = .07;     // how folded lengths run together
    std::vector<Rope> ropes;
    std::vector<Break> breaks;

    double evaluate(double x, double y, double z, Hit& hit) const {
        double d = 1e9;
        double nearest = 1e9;
        for (const Rope& rope : ropes) {
            Hit here;
            const double v = rope.value(x, y, z, here);
            if (v < nearest) {
                nearest = v;
                hit = here;
            }
            d = d > 1e8 ? v : smooth_min(d, v, blend);
        }
        d += lumps * fbm3(x * 1.7, y * 1.7, z * 1.7, key + 5, 3);
        for (const Break& b : breaks) {
            double lateral = 0;
            const double v = b.value(x, y, z, lateral);
            if (v > d) {
                d = v;
                hit.surface = b.glassy ? 2 : 1;
                hit.lateral = lateral;
            }
        }
        return d;
    }

    double density(double x, double y, double z) const override {
        Hit hit;
        return evaluate(x, y, z, hit);
    }

    double relief(double x, double y, double z) const override {
        Hit hit;
        evaluate(x, y, z, hit);
        if (hit.surface == 2) {
            // A glassy break's fine ripples, fading from where it began.
            const double wave = hit.lateral * 150 + 1.5 * noise2(x * 4, y * 4, key + 11);
            return .0016 * std::sin(wave) * std::exp(-hit.lateral * 1.1);
        }
        if (hit.surface == 1) {
            // A blade's cut: faint shear lines.
            return fine * .5 * std::sin((x + y) * 90 + 2 * noise2(x * 3, z * 3, key + 13));
        }
        // Fine lines along the strands, wavering, now stronger and now fainter.
        const double strength = .6 + .4 * noise2(hit.s * 2.5, hit.across * 3, key + 19);
        const double wave = hit.across * 170 + 2.5 * noise2(hit.s * 3, hit.across * 6, key + 21);
        return fine * strength * std::sin(wave);
    }
};

// A path of `length` along x through (0, y), bent a little.
void lay_path(Rope& rope, double length, double y, double bend) {
    constexpr int steps = 20;
    rope.px.clear();
    rope.py.clear();
    for (int i = 0; i <= steps; ++i) {
        const double s = length * (static_cast<double>(i) / steps - .5);
        rope.px.push_back(s);
        rope.py.push_back(y + bend * (s * s - length * length / 12));
    }
    rope.ps.assign(rope.px.size(), 0.0);
    for (std::size_t i = 1; i < rope.px.size(); ++i)
        rope.ps[i] = rope.ps[i - 1] + std::hypot(rope.px[i] - rope.px[i - 1], rope.py[i] - rope.py[i - 1]);
}

// An end of the chunk at x = `sign` * `half`: a blade's cut, or a glassy snap.
Break end_face(double sign, double half, bool glassy, int index, std::uint32_t key, double radius) {
    const double slant = (glassy ? .6 : .3) * (unit_hash(index, 81, key) - .5);
    const double lift = (glassy ? .5 : .2) * (unit_hash(index, 82, key) - .5);
    Break b;
    b.n[0] = sign * std::cos(slant);
    b.n[1] = std::sin(slant);
    b.n[2] = lift;
    const double l = std::sqrt(b.n[0] * b.n[0] + b.n[1] * b.n[1] + b.n[2] * b.n[2]);
    for (double& v : b.n)
        v /= l;
    b.at[0] = sign * half;
    b.glassy = glassy;
    if (glassy) {
        // Begun at the skin, somewhere round it.
        const double round = 2 * pi * unit_hash(index, 83, key);
        b.at[1] = std::cos(round) * radius;
        b.at[2] = std::sin(round) * radius * .8;
        b.bow = (unit_hash(index, 84, key) - .5) * .8;
        b.ripple = .008;
    }
    return b;
}

}  // namespace

void Shape::grow_candy() {
    const std::array<double, 16>& f = measures_.feature;
    Candy candy;
    const int kind = static_cast<int>(f[0]);
    candy.key = static_cast<std::uint32_t>(f[4]);
    candy.fine = f[14];
    candy.lumps = f[9];
    const std::uint32_t key = candy.key;
    const double half = f[6] * .5;
    Rope rope;
    rope.radius = f[1];
    rope.strands = std::max(3, static_cast<int>(f[2]));
    rope.part = f[3];
    rope.twist = f[5];
    rope.phase = 2 * pi * unit_hash(1, 91, key);
    rope.sag = f[8];
    rope.pinch = f[11];
    rope.half = half;
    double reach_y = rope.radius;
    if (kind == 2) {
        // Two or three lengths side by side, now and then one laid a little over another.
        const int count = std::clamp(static_cast<int>(f[10]), 2, 3);
        const double r = rope.radius * (count == 2 ? .72 : .6);
        for (int k = 0; k < count; ++k) {
            Rope length = rope;
            length.radius = r * (.9 + .2 * unit_hash(k, 92, key));
            length.phase = 2 * pi * unit_hash(k, 93, key);
            length.twist = rope.twist * (unit_hash(k, 94, key) > .5 ? 1 : -1);
            const double y = (k - (count - 1) * .5) * r * 1.65;
            length.height = (unit_hash(k, 95, key) - .5) * r * .5;
            lay_path(length, half * 2 + 1.0, y, f[7] * (unit_hash(k, 96, key) - .3));
            candy.ropes.push_back(length);
            reach_y = std::max(reach_y, std::abs(y) + r);
        }
        candy.blend = .05 + .05 * unit_hash(4, 91, key);
    } else {
        if (kind == 1) {
            rope.radius *= 1.12;
            rope.pinch = 0;
        }
        // Torn taffy ends at its torn end; every other end runs on to its cut or break.
        if (kind == 3)
            rope.torn[unit_hash(2, 91, key) > .5 ? 1 : 0] = true;
        lay_path(rope, half * 2 + (kind == 3 ? 0.0 : 1.0), 0, f[7]);
        candy.ropes.push_back(rope);
    }
    // The ends.
    const bool glassy = kind == 1;
    if (kind == 3) {
        candy.breaks.push_back(end_face(rope.torn[0] ? 1.0 : -1.0, half, false, 0, key, rope.radius));
    } else {
        candy.breaks.push_back(end_face(-1, half, glassy, 0, key, rope.radius));
        candy.breaks.push_back(end_face(1, half, glassy, 1, key, rope.radius));
    }
    if (kind == 1) {
        // Broken along its sides too: a few glassy faces, each begun at an edge.
        const int sides = static_cast<int>(f[13]);
        for (int k = 0; k < sides; ++k) {
            const double angle = (unit_hash(k, 97, key) > .5 ? 1 : -1) * pi * .5 + .7 * (unit_hash(k, 98, key) - .5);
            const double tilt = .7 * (unit_hash(k, 99, key) - .5);
            Break b;
            b.n[0] = std::cos(angle) * std::cos(tilt);
            b.n[1] = std::sin(angle) * std::cos(tilt);
            b.n[2] = std::sin(tilt);
            const double depth = rope.radius * (.55 + .3 * unit_hash(k, 100, key));
            const double x = (unit_hash(k, 101, key) - .5) * half * 1.4;
            b.at[0] = x + b.n[0] * depth;
            b.at[1] = b.n[1] * depth;
            b.at[2] = b.n[2] * depth + (unit_hash(k, 102, key) > .5 ? 1 : -1) * rope.radius * .4;
            b.bow = (unit_hash(k, 103, key) - .5) * .6;
            b.ripple = .007;
            b.glassy = true;
            candy.breaks.push_back(b);
        }
    }
    const double middle[3] = {0, 0, 0};
    const double box = std::max(half, reach_y) + .45;
    const double grain[3] = {1, 0, 0};
    grow_from(candy, middle, box, grain, middle);
}

}  // namespace stones
