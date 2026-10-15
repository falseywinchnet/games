#include "shapes.hpp"

#include "noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace ts {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double outside_height = -.35;

double smoothstep(double edge0, double edge1, double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

// A smooth minimum: within about 1/k of each other the two blend.
double soft_min(double a, double b, double k) {
    const double h = std::clamp(.5 + .5 * (b - a) * k, 0.0, 1.0);
    return b + (a - b) * h - h * (1 - h) / k;
}

// The edge profile: 1 in the middle, 0 at the outline, meeting it upright, so the edge is
// rounded; a higher power flattens the face and tightens the edge.
double profile(double u, double power) {
    const double inside = 1 - std::pow(std::min(u, 1.0), power);
    return std::sqrt(std::max(0.0, inside));
}

// Angle difference wrapped to -pi..pi.
double wrap_angle(double a) {
    return a - 2 * pi * std::floor((a + pi) / (2 * pi));
}


}  // namespace

// How far the socket's floor reaches at `angle`: a rounded square of half-side 0.98 and
// corner radius 0.5, the room a stone may fill under any of its quarter turns.
double socket_reach(double angle) {
    const double c = std::abs(std::cos(angle));
    const double s = std::abs(std::sin(angle));
    double low = 0;
    double high = 2;
    for (int step = 0; step < 30; ++step) {
        const double mid = (low + high) * .5;
        const double qx = mid * c - (.98 - .5);
        const double qy = mid * s - (.98 - .5);
        const double d = std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0) - .5;
        if (d <= 0)
            low = mid;
        else
            high = mid;
    }
    return low;
}

const char* style_name(Style style) {
    switch (style) {
    case Style::pebble:
        return "worry stone";
    case Style::shell:
        return "shell";
    case Style::spiral:
        return "spiral";
    case Style::wood:
        return "petrified wood";
    case Style::candy:
        return "pulled candy";
    case Style::bean:
        return "bean";
    case Style::pillow:
        return "folded pillow";
    case Style::facet:
        return "faceted tumble";
    case Style::nodule:
        return "agate nodule";
    }
    return "stone";
}

Shape Shape::make(std::uint32_t seed, Style style, double likeness, const Shape* parent) {
    // A family whose orientations look too much alike is drawn again: some are nearly
    // symmetric, none so nearly that a careful look cannot tell.
    Shape best;
    double best_distance = -1;
    for (int attempt = 0; attempt < 8; ++attempt) {
        Shape shape = make_once(seed, style, likeness, parent, attempt);
        const double distance = self_distance(shape, 24);
        if (distance > best_distance) {
            best_distance = distance;
            best = std::move(shape);
        }
        if (distance >= least_self_distance)
            break;
    }
    best.bake_detail();
    return best;
}

Shape Shape::make_once(std::uint32_t seed, Style style, double likeness, const Shape* parent, int attempt) {
    Shape shape;
    shape.seed_ = seed;
    shape.style_ = parent != nullptr ? (*parent).style_ : style;
    std::uint64_t random = 0x51ed2701ULL ^ (static_cast<std::uint64_t>(seed) << 17) ^
                           (static_cast<std::uint64_t>(attempt) * 0x9e3779b97f4a7c15ULL);
    shape.build(random);
    if (parent != nullptr) {
        // A near relation: every measure drawn towards the parent's, never onto it.
        const double keep = std::clamp(likeness, 0.0, .92);
        const Measures& from = (*parent).measures_;
        Measures& to = shape.measures_;
        to.aspect = from.aspect + (to.aspect - from.aspect) * (1 - keep);
        to.turn = from.turn + wrap_angle(to.turn - from.turn) * (1 - keep);
        for (std::size_t k = 0; k < to.lobe.size(); ++k) {
            to.lobe[k] = from.lobe[k] + (to.lobe[k] - from.lobe[k]) * (1 - keep);
            to.lobe_phase[k] = from.lobe_phase[k] + wrap_angle(to.lobe_phase[k] - from.lobe_phase[k]) * (1 - keep);
        }
        to.mark_angle = from.mark_angle + wrap_angle(to.mark_angle - from.mark_angle) * (1 - keep);
        to.mark_width = from.mark_width + (to.mark_width - from.mark_width) * (1 - keep);
        to.mark_depth = from.mark_depth + (to.mark_depth - from.mark_depth) * (1 - keep);
        to.top = from.top + (to.top - from.top) * (1 - keep);
        to.bottom = from.bottom + (to.bottom - from.bottom) * (1 - keep);
        to.top_power = from.top_power + (to.top_power - from.top_power) * (1 - keep);
        to.bottom_power = from.bottom_power + (to.bottom_power - from.bottom_power) * (1 - keep);
        for (std::size_t k = 0; k < to.feature.size(); ++k)
            to.feature[k] = from.feature[k] + (to.feature[k] - from.feature[k]) * (1 - keep);
        // A relation of a piece of fossil log is the same kind of piece.
        if (shape.style_ == Style::wood)
            to.feature[4] = from.feature[4] + 1;
    }
    if (shape.style_ == Style::wood) {
        shape.grow_wood();
    } else {
        shape.sample();
        shape.mesh();
    }
    return shape;
}

// The measures of the family's style, drawn from the seed.
void Shape::build(std::uint64_t& random) {
    Measures& m = measures_;
    m.turn = next_between(random, -pi, pi);
    for (std::size_t k = 0; k < m.lobe.size(); ++k) {
        const double falloff = 1.0 / static_cast<double>(k + 1);
        m.lobe[k] = next_between(random, .01, .05) * falloff;
        m.lobe_phase[k] = next_between(random, -pi, pi);
    }
    m.mark_angle = next_between(random, -pi, pi);
    m.mark_width = next_between(random, .25, .45);
    m.mark_depth = next_between(random, -.06, .08);
    std::array<double, 16>& f = m.feature;
    switch (style_) {
    case Style::pebble:
        m.aspect = next_between(random, .62, .88);
        m.top = next_between(random, .40, .50);
        m.bottom = next_between(random, .26, .34);
        m.top_power = next_between(random, 2.0, 2.6);
        m.bottom_power = next_between(random, 2.2, 3.0);
        f[0] = next_between(random, -.3, .3);   // the thumb's dent: where
        f[1] = next_between(random, -.2, .2);
        f[2] = next_between(random, .26, .42);  // its radii
        f[3] = next_between(random, .18, .28);
        f[4] = next_between(random, -pi, pi);   // its turn
        f[5] = next_between(random, .09, .15);  // its depth
        f[6] = next_between(random, -pi, pi);   // the underside's ridge: its turn
        f[7] = next_between(random, -.3, .3);   // and offset
        f[8] = next_between(random, .04, .07);  // its height
        f[9] = next_between(random, .12, .2);   // and width
        f[10] = next_between(random, -pi, pi);  // the crown's lean
        break;
    case Style::shell:
        m.aspect = next_between(random, .82, 1.0);
        m.top = next_between(random, .34, .44);
        m.bottom = next_between(random, .20, .26);
        m.top_power = next_between(random, 2.0, 2.4);
        m.bottom_power = next_between(random, 2.0, 2.4);
        m.mark_depth = 0;
        f[0] = std::floor(next_between(random, 13, 21));  // ribs
        f[1] = next_between(random, .025, .045);           // rib height
        f[2] = next_between(random, .10, .20);             // the larger ear
        f[3] = next_between(random, 0.0, .07);             // the smaller ear
        f[4] = next_between(random, -.25, .25);            // the dish's lean
        f[5] = next_between(random, .34, .46);             // the dish's depth
        f[6] = next_between(random, -.35, .35);            // the muscle scar: where
        f[7] = next_between(random, .05, .25);
        f[8] = next_between(random, .004, .009);           // growth lines
        f[9] = next_between(random, .6, .75);              // the hinge's narrowness
        break;
    case Style::spiral:
        m.aspect = next_between(random, .86, 1.0);
        m.top = next_between(random, .34, .42);
        m.bottom = next_between(random, .28, .36);
        m.top_power = next_between(random, 2.0, 2.6);
        m.bottom_power = next_between(random, 2.2, 2.8);
        m.mark_depth = 0;
        f[0] = next_between(random, .045, .075);   // growth per radian (log spiral)
        f[1] = next_between(random, -pi, pi);      // the aperture's angle
        f[2] = std::floor(next_between(random, 18, 30));  // ribs
        f[3] = next_between(random, .025, .04);    // rib height
        f[4] = next_between(random, .05, .08);     // the seam's depth
        f[5] = next_between(random, .08, .14);     // the navel's depth, above
        f[6] = next_between(random, .12, .2);      // and below
        f[7] = next_between(random, .3, .6);       // the underside's ribs, as a share
        f[8] = next_between(random, .05, .12);     // the aperture's lip
        break;
    case Style::wood:
        // A chunk broken out of a fossil log (wood_volume.cpp): the log's measures, and how
        // many breaks freed the chunk.
        f[0] = std::floor(next_between(random, 4, 8));   // breaks
        f[1] = next_between(random, .75, 1.05);          // the log's radius
        f[2] = next_between(random, 12, 24);             // growth rings
        f[3] = std::floor(next_between(random, 18, 36)); // rays
        f[4] = std::floor(next_between(random, 1, 60000)); // the seed of its breaks, bundles, bark and chips
        f[5] = next_between(random, .05, .13);           // how far bundles' breaks stagger
        f[6] = next_between(random, .06, .18);           // the share that tear out as splinters
        f[7] = std::floor(next_between(random, 2, 6));   // glassy chips
        f[8] = next_between(random, .04, .12);           // the pith's wander
        f[9] = next_between(random, .02, .05);           // the bark's fissures
        f[10] = next_between(random, .35, .7);           // where in the log the chunk was: from pith to bark
        f[11] = next_between(random, -pi, pi);           // and round it
        f[12] = next_between(random, .42, .62);          // the chunk's reach from its middle to a break
        f[14] = next_between(random, .0012, .0026);      // fibres
        break;
    case Style::nodule:
        // A water-worn agate nodule: a rounded lump whose broken faces show the agate's
        // fortification bands as fine stepped ridges, the rest a pitted husk.
        m.aspect = next_between(random, .78, .96);
        m.top = next_between(random, .44, .54);
        m.bottom = next_between(random, .34, .44);
        m.top_power = next_between(random, 2.0, 2.6);
        m.bottom_power = next_between(random, 2.0, 2.6);
        m.mark_depth = 0;
        f[0] = std::floor(next_between(random, 3, 6));   // the lump's sides
        f[1] = next_between(random, 5, 9);               // how sharp its corners are
        f[2] = next_between(random, 13, 20);             // bands a unit
        f[3] = next_between(random, .012, .02);          // a band's step
        f[4] = std::floor(next_between(random, 1, 60000)); // the seed of its sides, breaks and pits
        f[5] = std::floor(next_between(random, 2, 4));   // broken faces above
        f[6] = next_between(random, .45, .7);            // their size
        f[7] = std::floor(next_between(random, 5, 9));   // the bands' angles
        f[8] = next_between(random, .6, 1.0);            // the husk's pits
        f[9] = next_between(random, -.55, .55);          // the bands' centre
        f[10] = next_between(random, -.45, .45);
        break;
    case Style::candy:
        m.aspect = next_between(random, .46, .58);
        m.top = next_between(random, .30, .38);
        m.bottom = next_between(random, .26, .34);
        m.top_power = next_between(random, 2.0, 2.6);
        m.bottom_power = next_between(random, 2.0, 2.6);
        m.mark_depth = 0;
        f[0] = next_between(random, 1.5, 1.9);     // the lozenge's squareness
        f[1] = next_between(random, .15, .3);      // the pulled tail
        f[2] = next_between(random, -.35, .35);    // the bend
        f[3] = next_between(random, .55, .95);     // the twist's slant
        f[4] = next_between(random, 5.0, 7.5);     // twists along the length
        f[5] = next_between(random, .035, .055);   // their height
        f[6] = next_between(random, .5, 1.0);      // tightening towards the tail
        f[7] = next_between(random, -.4, .4);      // the pinch, above: where
        f[8] = next_between(random, .05, .09);     // and how deep
        f[9] = next_between(random, -.5, .5);      // the flat, below: where
        break;
    case Style::bean:
        m.aspect = next_between(random, .52, .66);
        m.top = next_between(random, .38, .46);
        m.bottom = next_between(random, .26, .34);
        m.top_power = next_between(random, 2.0, 2.4);
        m.bottom_power = next_between(random, 2.0, 2.6);
        m.mark_depth = 0;
        f[0] = next_between(random, .25, .45);     // the bend
        f[1] = next_between(random, .06, .1);      // the crease's depth
        f[2] = next_between(random, .3, .55);      // and length
        f[3] = next_between(random, -.25, .25);    // the keel below: offset
        f[4] = next_between(random, .04, .07);     // and height
        f[5] = next_between(random, -.2, .2);      // one end fuller than the other
        break;
    case Style::pillow:
        m.aspect = next_between(random, .74, .94);
        m.top = next_between(random, .32, .40);
        m.bottom = next_between(random, .26, .34);
        m.top_power = next_between(random, 2.6, 3.4);
        m.bottom_power = next_between(random, 2.2, 2.8);
        f[0] = next_between(random, 2.8, 3.6);     // squareness
        f[1] = next_between(random, -pi, pi);      // the fold's turn
        f[2] = next_between(random, -.3, .3);      // and offset
        f[3] = next_between(random, .07, .11);     // its height
        f[4] = next_between(random, .16, .26);     // and width
        f[5] = next_between(random, -.35, .35);    // the dimple below: where
        f[6] = next_between(random, -.35, .35);
        f[7] = next_between(random, .06, .1);      // and depth
        f[8] = next_between(random, -.2, .2);      // the outline's shear
        break;
    case Style::facet:
        m.aspect = next_between(random, .72, .95);
        m.top = next_between(random, .40, .50);
        m.bottom = next_between(random, .26, .34);
        m.top_power = next_between(random, 2.2, 2.8);
        m.bottom_power = next_between(random, 2.0, 2.6);
        f[0] = std::floor(next_between(random, 5, 8));  // facets
        f[1] = next_between(random, -pi, pi);           // their first azimuth
        f[2] = next_between(random, .55, .9);           // the table's height (share of top)
        f[3] = next_between(random, 0, 1);              // the facets' irregularity
        f[4] = next_between(random, 0, 1000);           // a seed for it
        f[5] = next_between(random, .8, 1.15);          // facet steepness
        break;
    }
}

double Shape::hashed(int a, int b) const {
    const std::uint32_t key = static_cast<std::uint32_t>(measures_.feature[4]);
    return (hash_signed(a, b, 7, key) + 1) * .5;
}

// An agate nodule: a rounded polygon of a few uneven sides.
bool Shape::nodule_inside(double x, double y) const {
    const Measures& m = measures_;
    const std::array<double, 16>& f = m.feature;
    const int sides = static_cast<int>(f[0]);
    const double k = f[1];
    double sum = 0;
    for (int i = 0; i < sides; ++i) {
        const double angle = 2 * pi * (i + .35 * (hashed(i, 81) - .5)) / sides;
        const double reach = .82 + .18 * hashed(i, 82);
        const double along = x * std::cos(angle) + y / m.aspect * std::sin(angle);
        sum += std::exp(k * (along - reach));
    }
    // A soft maximum of the sides' distances, rounded by a circle.
    const double polygon = std::log(sum) / k;
    const double round = std::hypot(x, y / m.aspect) - 1;
    return std::max(polygon, round) <= 0;
}

// The outermost inside point along the ray (c, s), marched in from beyond.
double Shape::reach_inward(double c, double s) const {
    for (int step = 500; step > 0; --step) {
        const double r = step * .005;
        const double x = r * c;
        const double y = r * s;
        if (nodule_inside(x, y))
            return r;
    }
    return .05;
}

// The outline's radius at `angle`, before the family is scaled to the unit circle.
double Shape::outline_at(double angle) const {
    const Measures& m = measures_;
    const std::array<double, 16>& f = m.feature;
    const double a = angle - m.turn;
    const double c = std::cos(a);
    const double s = std::sin(a);
    double r = 1;
    switch (style_) {
    case Style::pebble:
    case Style::facet: {
        const double y = s / m.aspect;
        r = 1 / std::sqrt(c * c + y * y);
        if (style_ == Style::facet) {
            // A rounded polygon: the nearest of its sides, softly.
            const int sides = static_cast<int>(f[0]);
            double nearest = 1e9;
            double soft = 0;
            for (int i = 0; i < sides; ++i) {
                const double jitter = (hash_signed(i, 7, 3, static_cast<std::uint32_t>(f[4])) * .35) * f[3];
                const double side_angle = f[1] + 2 * pi * (i + jitter) / sides;
                const double reach = .86 + .12 * hash_signed(i, 9, 1, static_cast<std::uint32_t>(f[4])) * f[3];
                const double along = std::cos(a - side_angle);
                if (along <= .05)
                    continue;
                const double hit = reach / along;
                nearest = std::min(nearest, hit);
                soft += std::exp(-hit * 14);
            }
            const double polygon = soft > 0 ? -std::log(soft) / 14 : nearest;
            r = std::min(r * 1.12, std::max(polygon, nearest * .9));
        }
        break;
    }
    case Style::shell: {
        // A fan narrowing to a hinge at the bottom, with two unequal ears beside it.
        const double up = (s + 1) * .5;
        r = f[9] + (1 - f[9]) * std::pow(up, .6);
        r *= 1 / std::sqrt(c * c * (m.aspect * m.aspect) + s * s) * m.aspect;
        const double ear_left = std::exp(-std::pow(wrap_angle(a - (-pi / 2 - .62)) / .2, 2));
        const double ear_right = std::exp(-std::pow(wrap_angle(a - (-pi / 2 + .62)) / .2, 2));
        r += f[2] * ear_left + f[3] * ear_right;
        // The ribs scallop the edge.
        const double hinge_angle = std::atan2(r * s + .55, r * c);
        r *= 1 + .022 * std::cos(f[0] * hinge_angle) * up;
        break;
    }
    case Style::spiral: {
        // A logarithmic spiral's last whorl: growing round from the aperture to it again.
        const double k = f[0];
        double turned = a - f[1];
        turned -= 2 * pi * std::floor(turned / (2 * pi));
        r = std::exp(k * turned);
        // The aperture's step, eased over a few degrees, and its lip.
        const double step = smoothstep(2 * pi - .2, 2 * pi, turned);
        r = r * (1 - step) + 1.0 * step;
        r += f[8] * std::exp(-std::pow((2 * pi - turned) / .25, 2));
        r *= 1 / std::sqrt(c * c + s * s / (m.aspect * m.aspect)) * m.aspect;
        r = std::max(r, .7);
        break;
    }
    case Style::wood:
        // Grown and broken as a solid (wood_volume.cpp); never drawn by an outline.
        r = 1;
        break;
    case Style::nodule:
        r = reach_inward(c, s);
        break;
    case Style::candy:
    case Style::bean: {
        // A bent lozenge, found along the ray by halving.
        double low = 0;
        double high = 2.5;
        for (int step = 0; step < 32; ++step) {
            const double mid = (low + high) * .5;
            const double x = mid * c;
            const double y = mid * s;
            double inside = 0;
            if (style_ == Style::candy) {
                const double tail = 1 + f[1] * smoothstep(.2, 1, x);
                const double bent = y - f[2] * (x * x - .3);
                const double n = f[0];
                inside = std::pow(std::abs(x / tail), n) + std::pow(std::abs(bent / m.aspect), n);
            } else {
                const double bent = y - f[0] * (x * x - .35);
                const double fuller = 1 + f[5] * x;
                inside = x * x + std::pow(bent / (m.aspect * fuller), 2);
            }
            if (inside <= 1)
                low = mid;
            else
                high = mid;
        }
        r = low;
        break;
    }
    case Style::pillow: {
        const double n = f[0];
        const double x = c + f[8] * s;
        const double y = s / m.aspect;
        r = std::pow(std::pow(std::abs(x), n) + std::pow(std::abs(y), n), -1 / n);
        break;
    }
    }
    // The wobble every stone has, and a notch or a bulge where it was chipped or grew.
    double wobble = 1;
    for (std::size_t k = 0; k < m.lobe.size(); ++k)
        wobble += m.lobe[k] * std::cos(static_cast<double>(k + 2) * angle + m.lobe_phase[k]);
    const double mark = m.mark_depth * std::exp(-std::pow(wrap_angle(angle - m.mark_angle) / m.mark_width, 2));
    return r * wobble * (1 + mark);
}

// The style's relief on face `face` at local (x, y) (unturned), `u` across the outline.
double Shape::feature_height(int face, double x, double y, double u) const {
    const Measures& m = measures_;
    const std::array<double, 16>& f = m.feature;
    const double base = (face == 0 ? m.top : m.bottom) * profile(u, face == 0 ? m.top_power : m.bottom_power);
    const double keep = 1 - std::pow(u, 6);
    switch (style_) {
    case Style::pebble: {
        if (face == 0) {
            const double ca = std::cos(f[4]);
            const double sa = std::sin(f[4]);
            const double dx = x - f[0];
            const double dy = y - f[1];
            const double along = dx * ca + dy * sa;
            const double across = -dx * sa + dy * ca;
            const double dent = f[5] * std::exp(-1.6 * (along * along / (f[2] * f[2]) + across * across / (f[3] * f[3])));
            const double lean = .07 * (x * std::cos(f[10]) + y * std::sin(f[10]));
            return base * (1 + lean) - dent * keep;
        }
        const double d = -x * std::sin(f[6]) + y * std::cos(f[6]) - f[7];
        return base + f[8] * std::exp(-d * d / (f[9] * f[9])) * keep;
    }
    case Style::shell: {
        const double hx = 0;
        const double hy = -.55;
        const double dist = std::hypot(x - hx, y - hy);
        const double fan = std::atan2(y - hy, x - hx);
        const double rib = std::pow(std::abs(std::cos(f[0] * fan * .5)), .7);
        if (face == 0) {
            const double crown = .07 * std::exp(-(x * x + (y + .25) * (y + .25)) / .18);
            const double growth = f[8] * std::sin(dist * 34) * smoothstep(.15, .5, dist);
            return base + (crown + f[1] * (rib - .5) * smoothstep(.1, .45, dist) + growth) * keep;
        }
        // The inside: a dish, faintly ribbed towards the edge, with the muscle's scar.
        const double dish = f[5] * profile(std::min(1.0, std::hypot(x - f[4] * .3, y + .1) / .95), 2.0);
        const double groove = .4 * f[1] * (rib - .5) * smoothstep(.6, .95, u);
        const double sx = x - f[6];
        const double sy = y - f[7];
        const double scar = .03 * std::exp(-(sx * sx / .02 + sy * sy / .035));
        return base - dish * keep - groove * keep + scar * keep;
    }
    case Style::spiral: {
        const double r = std::hypot(x, y);
        const double angle = std::atan2(y, x);
        const double k = f[0];
        // Which whorl a point is on: the seams lie where log(r) - k * angle is a whole
        // number of turns' growth apart.
        const double growth = 2 * pi * k;
        double turned = angle - f[1];
        turned -= 2 * pi * std::floor(turned / (2 * pi));
        const double phase = (std::log(std::max(r, 1e-3)) - k * turned) / growth;
        const double seam_distance = (phase - std::round(phase)) * growth;
        const double seam = std::exp(-std::pow(seam_distance / .06, 2));
        const double ribs = std::pow(std::abs(std::cos(f[2] * .5 * angle)), .8) - .5;
        const double navel_depth = face == 0 ? f[5] : f[6];
        const double navel = navel_depth * std::exp(-r * r / .03);
        const double rib_share = face == 0 ? 1.0 : f[7];
        return base + (-f[4] * seam + f[3] * ribs * rib_share * smoothstep(.12, .35, r) - navel) * keep;
    }
    case Style::wood:
        // Grown and broken as a solid (wood_volume.cpp).
        return base;
    case Style::nodule: {
        // Broken faces: broad shallow breaks across the lump, where the bands show most.
        double h = base;
        if (face == 0) {
            const double power = m.top_power;
            for (int k = 0; k < static_cast<int>(f[5]); ++k) {
                const double angle = 2 * pi * (k + .6 * hashed(k, 41)) / f[5];
                const double rim = .7 + .4 * hashed(k, 42);
                const double sx = rim * std::cos(angle);
                const double sy = rim * std::sin(angle) * m.aspect;
                const double size = f[6] * (.8 + .5 * hashed(k, 43));
                const double depth = .12 + .1 * hashed(k, 44);
                const double ground = m.top * profile(.75, power) - depth;
                const double d = std::hypot(x - sx, y - sy) / size;
                h = soft_min(h, ground + depth * .9 * d * d, 90);
            }
        }
        // A lumpy husk.
        h += .025 * fbm2(x * 2.6, y * 2.6, static_cast<std::uint32_t>(f[4]) + 11 + static_cast<std::uint32_t>(face), 3) * keep;
        return h;
    }
    case Style::candy: {
        const double slant = f[3];
        const double tighten = 1 + f[6] * smoothstep(-.2, 1, x);
        // Below, the stripes slant the other way in the stone's own frame, so the stone
        // turned over shows the same slant: the twist looks nearly the same either way up.
        const double sx = face == 0 ? x : -x;
        const double along = (sx * std::cos(slant) + (y - f[2] * (x * x - .3)) * std::sin(slant) * 2.2) * f[4] * tighten;
        const double stripe = std::pow(std::abs(std::cos(pi * along * .5)), .6) - .55;
        double h = base + f[5] * stripe * keep;
        if (face == 0) {
            const double px = x - f[7];
            h -= f[8] * std::exp(-(px * px / .02 + y * y / .05)) * keep;
        } else {
            const double flat_from = f[9];
            const double side = x - flat_from;
            if (side > 0)
                h = std::min(h, base * (1 - .5 * smoothstep(0, .3, side)) + .02);
        }
        return h;
    }
    case Style::bean: {
        if (face == 0) {
            // A crease running in from the inner curve.
            const double cy = y - f[0] * (x * x - .35);
            const double along = std::clamp((-cy) / f[2], 0.0, 1.0);
            const double crease = f[1] * std::exp(-x * x / .015) * (1 - along) * smoothstep(-.1, .2, -cy + .3);
            return base - crease * keep;
        }
        const double keel = x - f[3];
        return base + f[4] * std::exp(-keel * keel / .02) * keep;
    }
    case Style::pillow: {
        if (face == 0) {
            const double d = -x * std::sin(f[1]) + y * std::cos(f[1]) - f[2];
            const double fold = f[3] * std::pow(std::max(0.0, 1 - std::abs(d) / f[4]), 2);
            const double puff = .04 * std::cos(x * 4.2) * std::cos(y * 4.2);
            return base + (fold + puff) * keep;
        }
        const double dx = x - f[5];
        const double dy = y - f[6];
        return base - f[7] * std::exp(-(dx * dx + dy * dy) / .025) * keep;
    }
    case Style::facet: {
        if (face == 0) {
            // A dome cut into planes, its crown a table.
            const int sides = static_cast<int>(f[0]);
            double h = m.top * f[2];
            for (int i = 0; i < sides; ++i) {
                const double jitter = hash_signed(i, 3, 5, static_cast<std::uint32_t>(f[4])) * .3 * f[3];
                const double azimuth = f[1] + 2 * pi * (i + .5 + jitter) / sides;
                const double steep = f[5] * (1 + .25 * hash_signed(i, 1, 9, static_cast<std::uint32_t>(f[4])) * f[3]);
                const double plane = m.top * 1.25 - steep * m.top * (x * std::cos(azimuth) + y * std::sin(azimuth)) * 1.6;
                h = soft_min(h, plane, 60);
            }
            return soft_min(base * 1.1, h, 40);
        }
        return base;
    }
    }
    return base;
}

void Shape::sample() {
    // The outline, unscaled, then everything scaled as large as the socket allows.
    outline_.assign(sectors, 0.0F);
    double fit = 1e9;
    std::vector<double> raw(sectors);
    for (int j = 0; j < sectors; ++j) {
        const double angle = 2 * pi * j / sectors;
        raw[static_cast<std::size_t>(j)] = outline_at(angle);
        fit = std::min(fit, socket_reach(angle) / raw[static_cast<std::size_t>(j)]);
    }
    const double scale = fit * .97;
    scale_ = scale;
    for (int j = 0; j < sectors; ++j)
        outline_[static_cast<std::size_t>(j)] = static_cast<float>(raw[static_cast<std::size_t>(j)] * scale);
    ring_u_.assign(rings + 1, 0.0F);
    for (int i = 0; i <= rings; ++i)
        ring_u_[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(pi * .5 * i / rings));
    const std::size_t count = static_cast<std::size_t>((rings + 1) * sectors);
    top_.assign(count, 0.0F);
    bottom_.assign(count, 0.0F);
    top_peak_ = 0;
    bottom_peak_ = 0;
    const double turn_c = std::cos(-measures_.turn);
    const double turn_s = std::sin(-measures_.turn);
    for (int i = 0; i <= rings; ++i) {
        const double u = ring_u_[static_cast<std::size_t>(i)];
        for (int j = 0; j < sectors; ++j) {
            const double angle = 2 * pi * j / sectors;
            // Features are drawn in the unscaled outline's own (unturned) frame.
            const double r = u * raw[static_cast<std::size_t>(j)];
            const double wx = r * std::cos(angle);
            const double wy = r * std::sin(angle);
            const double lx = wx * turn_c - wy * turn_s;
            const double ly = wx * turn_s + wy * turn_c;
            double top = feature_height(0, lx, ly, u);
            double bottom = feature_height(1, lx, ly, u);
            if (i == rings) {
                top = 0;
                bottom = 0;
            }
            // Never thinner than a sliver.
            const double least = .08 * profile(u, 2.0);
            if (top + bottom < least)
                bottom = least - top;
            const std::size_t at = static_cast<std::size_t>(i * sectors + j);
            top_[at] = static_cast<float>(top * scale);
            bottom_[at] = static_cast<float>(bottom * scale);
            top_peak_ = std::max(top_peak_, top * scale);
            bottom_peak_ = std::max(bottom_peak_, bottom * scale);
        }
    }
}

void Shape::mesh() {
    vertices_.clear();
    triangles_.clear();
    // Top: a centre and rings 1..rings; bottom: a centre and rings 1..rings - 1, sharing
    // the top's outermost ring, which is the edge.
    std::vector<std::uint32_t> top_index(static_cast<std::size_t>((rings + 1) * sectors));
    std::vector<std::uint32_t> bottom_index(static_cast<std::size_t>((rings + 1) * sectors));
    for (int face = 0; face < 2; ++face) {
        const std::vector<float>& heights = face == 0 ? top_ : bottom_;
        std::vector<std::uint32_t>& index = face == 0 ? top_index : bottom_index;
        const float sign = face == 0 ? 1.0F : -1.0F;
        double centre = 0;
        for (int j = 0; j < sectors; ++j)
            centre += heights[static_cast<std::size_t>(j)];
        centre /= sectors;
        const std::uint32_t middle = static_cast<std::uint32_t>(vertices_.size());
        vertices_.push_back({0, 0, sign * static_cast<float>(centre), 0, 0, 0});
        for (int j = 0; j < sectors; ++j)
            index[static_cast<std::size_t>(j)] = middle;
        for (int i = 1; i <= rings; ++i) {
            const double u = ring_u_[static_cast<std::size_t>(i)];
            for (int j = 0; j < sectors; ++j) {
                const std::size_t at = static_cast<std::size_t>(i * sectors + j);
                if (face == 1 && i == rings) {
                    index[at] = top_index[at];
                    continue;
                }
                const double angle = 2 * pi * j / sectors;
                const double r = u * outline_[static_cast<std::size_t>(j)];
                index[at] = static_cast<std::uint32_t>(vertices_.size());
                vertices_.push_back({static_cast<float>(r * std::cos(angle)), static_cast<float>(r * std::sin(angle)),
                                     sign * heights[at], 0, 0, 0});
            }
        }
        for (int i = 0; i < rings; ++i) {
            for (int j = 0; j < sectors; ++j) {
                const int next = (j + 1) % sectors;
                const std::uint32_t a = index[static_cast<std::size_t>(i * sectors + j)];
                const std::uint32_t b = index[static_cast<std::size_t>((i + 1) * sectors + j)];
                const std::uint32_t c = index[static_cast<std::size_t>((i + 1) * sectors + next)];
                const std::uint32_t d = index[static_cast<std::size_t>(i * sectors + next)];
                if (face == 0) {
                    triangles_.insert(triangles_.end(), {a, b, c});
                    if (i > 0)
                        triangles_.insert(triangles_.end(), {a, c, d});
                } else {
                    triangles_.insert(triangles_.end(), {a, c, b});
                    if (i > 0)
                        triangles_.insert(triangles_.end(), {a, d, c});
                }
            }
        }
    }
    // Normals: each vertex the area-weighted sum of its triangles'.
    std::vector<double> sum(vertices_.size() * 3, 0.0);
    for (std::size_t t = 0; t + 2 < triangles_.size(); t += 3) {
        const ShapeVertex& p = vertices_[triangles_[t]];
        const ShapeVertex& q = vertices_[triangles_[t + 1]];
        const ShapeVertex& r = vertices_[triangles_[t + 2]];
        const double ux = q.x - p.x, uy = q.y - p.y, uz = q.z - p.z;
        const double vx = r.x - p.x, vy = r.y - p.y, vz = r.z - p.z;
        const double nx = uy * vz - uz * vy;
        const double ny = uz * vx - ux * vz;
        const double nz = ux * vy - uy * vx;
        for (int k = 0; k < 3; ++k) {
            const std::size_t v = triangles_[t + static_cast<std::size_t>(k)];
            sum[v * 3] += nx;
            sum[v * 3 + 1] += ny;
            sum[v * 3 + 2] += nz;
        }
    }
    for (std::size_t v = 0; v < vertices_.size(); ++v) {
        const double length = std::sqrt(sum[v * 3] * sum[v * 3] + sum[v * 3 + 1] * sum[v * 3 + 1] + sum[v * 3 + 2] * sum[v * 3 + 2]);
        const double inverse = length > 0 ? 1 / length : 0;
        vertices_[v].nx = static_cast<float>(sum[v * 3] * inverse);
        vertices_[v].ny = static_cast<float>(sum[v * 3 + 1] * inverse);
        vertices_[v].nz = static_cast<float>(sum[v * 3 + 2] * inverse);
    }
}

bool Shape::height(int face, double x, double y, double& out) const {
    const double r = std::hypot(x, y);
    double angle = std::atan2(y, x);
    if (angle < 0)
        angle += 2 * pi;
    const double sector = angle / (2 * pi) * sectors;
    const int j0 = static_cast<int>(std::floor(sector)) % sectors;
    const int j1 = (j0 + 1) % sectors;
    const double tj = sector - std::floor(sector);
    const double edge = outline_[static_cast<std::size_t>(j0)] * (1 - tj) + outline_[static_cast<std::size_t>(j1)] * tj;
    if (r >= edge)
        return false;
    const double u = r / edge;
    const double ring = std::asin(std::min(u, 1.0)) / (pi * .5) * rings;
    const int i0 = std::min(static_cast<int>(std::floor(ring)), rings - 1);
    const double ti = ring - i0;
    const std::vector<float>& h = face == 0 ? top_ : bottom_;
    const double a = h[static_cast<std::size_t>(i0 * sectors + j0)] * (1 - tj) + h[static_cast<std::size_t>(i0 * sectors + j1)] * tj;
    const double b = h[static_cast<std::size_t>((i0 + 1) * sectors + j0)] * (1 - tj) +
                     h[static_cast<std::size_t>((i0 + 1) * sectors + j1)] * tj;
    out = a + (b - a) * ti;
    return true;
}

// The agate's bands: nested, angular shells about a centre, as fortification agate grows.
double Shape::band_field(double x, double y, double z) const {
    const std::array<double, 16>& f = measures_.feature;
    const double px = x - f[9];
    const double py = y - f[10];
    const double pz = z * 1.6;
    double angular = -1e9;
    for (int i = 0; i < static_cast<int>(f[7]); ++i) {
        const double a = 2 * pi * (i + .4 * hashed(i, 91)) / f[7];
        const double tilt = .35 * (hashed(i, 92) - .5);
        const double nx = std::cos(a) * std::cos(tilt);
        const double ny = std::sin(a) * std::cos(tilt);
        const double nz = std::sin(tilt);
        angular = std::max(angular, (px * nx + py * ny + pz * nz) * (.9 + .25 * hashed(i, 93)));
    }
    const double round = std::sqrt(px * px + py * py + pz * pz);
    // Bands of uneven widths: the field runs faster and slower as the agate grew.
    const double even = .6 * angular + .4 * round;
    return even + .05 * std::sin(even * 9 + 2 * hashed(0, 94)) + .015 * noise2(x * 6, y * 6, static_cast<std::uint32_t>(f[4]) + 13);
}

void Shape::grain_axis(double out[3]) const {
    if (grown_) {
        out[0] = grain_[0];
        out[1] = grain_[1];
        out[2] = grain_[2];
        return;
    }
    out[0] = std::cos(measures_.turn);
    out[1] = std::sin(measures_.turn);
    out[2] = 0;
}

void Shape::grain_centre(double out[3]) const {
    out[0] = pith_[0];
    out[1] = pith_[1];
    out[2] = pith_[2];
}

double Shape::bands(double x, double y, double z) const {
    return style_ == Style::nodule ? band_field(x, y, z) * measures_.feature[2] : -1;
}

double Shape::fine_height(int face, double x, double y) const {
    const Measures& m = measures_;
    const std::array<double, 16>& f = m.feature;
    if (style_ == Style::wood) {
        // Fine grain: fibres along the log, or the end grain's pores on a round's break.
        const std::uint32_t grain = static_cast<std::uint32_t>(f[4]);
        // Across the grain as it lies in the tray.
        const double gx = grain_[0];
        const double gy = grain_[1];
        const double flat = std::sqrt(gx * gx + gy * gy);
        if (flat < .35)
            return -.004 * std::max(0.0, noise2(x * 70, y * 70, grain + 5) - .35) + wear(face, x, y) * .4;
        const double along = (x * gx + y * gy) / flat / scale_;
        const double across = (-x * gy + y * gx) / flat / scale_;
        const double fibre = f[14] * std::sin(across * 170 + 8 * fbm2(along * 2.5, across * 9, grain + 21, 3));
        return fibre + wear(face, x, y) * .4;
    }
    if (style_ == Style::nodule) {
        double z = 0;
        if (!height(face, x, y, z))
            z = 0;
        if (face == 1)
            z = -z;
        // The bands weather into terraces, each a small step, plainer on the husk.
        const double t = band_field(x, y, z) * f[2];
        const double within = t - std::floor(t);
        const double ridge = f[3] * (smoothstep(0, .12, within) - within) * (face == 0 ? 1.0 : .4);
        // Pits in the husk: small round hollows.
        double pits = 0;
        const double cells = 16;
        const int cx = static_cast<int>(std::floor(x * cells));
        const int cy = static_cast<int>(std::floor(y * cells));
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const std::uint32_t h = hash_cell(cx + dx, cy + dy, face, static_cast<std::uint32_t>(f[4]) + 31);
                if (static_cast<double>(h & 0xffU) / 255.0 > f[8] * (face == 0 ? .08 : .45))
                    continue;
                const double px = (cx + dx + static_cast<double>((h >> 8) & 0xffU) / 255.0) / cells;
                const double py = (cy + dy + static_cast<double>((h >> 16) & 0xffU) / 255.0) / cells;
                const double radius = (.25 + .3 * static_cast<double>((h >> 24) & 0xffU) / 255.0) / cells;
                const double d = std::hypot(x - px, y - py) / radius;
                if (d < 1)
                    pits = std::min(pits, -.018 * (1 - d * d));
            }
        return ridge + pits;
    }
    // The other styles' own fine relief, in their unturned, unscaled frame.
    const double c = std::cos(-m.turn);
    const double sn = std::sin(-m.turn);
    const double lx = (x * c - y * sn) / scale_;
    const double ly = (x * sn + y * c) / scale_;
    double h = wear(face, x, y);
    if (style_ == Style::shell) {
        const double hx = 0;
        const double hy = -.55;
        const double dist = std::hypot(lx - hx, ly - hy);
        const double fan = std::atan2(ly - hy, lx - hx);
        if (face == 0) {
            // Growth lines round the hinge, fine and uneven, with a stronger check now and then;
            // and the ribs' crests kept crisp.
            const double growth = dist * 95 + 3 * noise2(fan * 3, dist * 4, seed_ + 41);
            h += .0035 * std::sin(growth) + .006 * smoothstep(.85, 1, std::sin(growth * .21));
            const double crest = std::pow(std::abs(std::cos(f[0] * fan * .5)), 6);
            h += .01 * crest * smoothstep(.15, .5, dist);
        } else {
            // Inside, the rim is toothed where the ribs end.
            double u = 0;
            if (height(1, x, y, u)) {
                const double r = std::hypot(lx, ly);
                const double teeth = std::pow(std::abs(std::cos(f[0] * fan * .5)), 2) - .5;
                h += .012 * teeth * smoothstep(.62, .9, r);
            }
        }
    } else if (style_ == Style::spiral) {
        // Polished through, an ammonite shows its sutures: frilled lines across each whorl
        // where a chamber wall met the shell.
        const double r = std::hypot(lx, ly);
        const double angle = std::atan2(ly, lx);
        const int chambers = 14;
        const double frill = .07 * std::sin(r * 70 + 1.3) + .035 * std::sin(r * 160) + .02 * std::sin(r * 330);
        const double at = (angle + frill) / (2 * pi) * chambers;
        const double d = std::abs(at - std::round(at)) * (2 * pi / chambers) * r;
        if (d < .008 && r > .12)
            h -= .014 * (1 - d / .008) * (face == 0 ? 1.0 : .8);
    }
    return h;
}

// What tumbling leaves on any stone: a few small pits and a hairline fracture or two.
double Shape::wear(int face, double x, double y) const {
    const std::uint32_t key = seed_ * 7U + static_cast<std::uint32_t>(face) * 131U;
    double h = 0;
    const double cells = 9;
    const int cx = static_cast<int>(std::floor(x * cells));
    const int cy = static_cast<int>(std::floor(y * cells));
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const std::uint32_t bits = hash_cell(cx + dx, cy + dy, 5, key);
            if ((bits & 0xffU) > 16U)
                continue;
            const double px = (cx + dx + static_cast<double>((bits >> 8) & 0xffU) / 255.0) / cells;
            const double py = (cy + dy + static_cast<double>((bits >> 16) & 0xffU) / 255.0) / cells;
            const double radius = (.08 + .14 * static_cast<double>((bits >> 24) & 0xffU) / 255.0) / cells;
            const double d = std::hypot(x - px, y - py) / radius;
            if (d < 1)
                h = std::min(h, -.012 * (1 - d * d));
        }
    for (int k = 0; k < 2; ++k) {
        const std::uint32_t bits = hash_cell(k, 9, 11, key);
        if ((bits & 0xffU) > 150U)
            continue;
        const double angle = static_cast<double>((bits >> 8) & 0xffU) / 255.0 * pi;
        const double offset = (static_cast<double>((bits >> 16) & 0xffU) / 255.0 - .5) * 1.2;
        const double along = x * std::cos(angle) + y * std::sin(angle);
        const double start = (static_cast<double>((bits >> 24) & 0xffU) / 255.0 - .5) * 1.2;
        if (along < start || along > start + .5)
            continue;
        const double across = -x * std::sin(angle) + y * std::cos(angle) - offset - .025 * noise2(along * 7, k * 3.0, key);
        const double d = std::abs(across);
        if (d < .006)
            h -= .012 * (1 - d / .006) * smoothstep(start, start + .06, along) * (1 - smoothstep(start + .44, start + .5, along));
    }
    return h;
}

void Shape::bake_detail() {
    constexpr int side = 320;
    const double texel = 2 * reach / side;
    for (int face = 0; face < 2; ++face) {
        Detail& d = detail_[face];
        d.side = side;
        std::vector<double> h(static_cast<std::size_t>((side + 4) * (side + 4)));
        for (int row = 0; row < side + 4; ++row)
            for (int column = 0; column < side + 4; ++column) {
                const double x = (column - 2 + .5) * texel - reach;
                const double y = reach - (row - 2 + .5) * texel;
                h[static_cast<std::size_t>(row * (side + 4) + column)] = fine_height(face, x, y);
            }
        d.slope_x.assign(static_cast<std::size_t>(side * side), 0.0F);
        d.slope_y.assign(static_cast<std::size_t>(side * side), 0.0F);
        d.hollow.assign(static_cast<std::size_t>(side * side), 0.0F);
        const int w = side + 4;
        for (int row = 0; row < side; ++row)
            for (int column = 0; column < side; ++column) {
                const std::size_t at = static_cast<std::size_t>((row + 2) * w + column + 2);
                const double right = h[at + 1];
                const double left = h[at - 1];
                const double up = h[at - static_cast<std::size_t>(w)];
                const double down = h[at + static_cast<std::size_t>(w)];
                double around = 0;
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx)
                        around += h[at + static_cast<std::size_t>(dy * w + dx)];
                around /= 25;
                const std::size_t out = static_cast<std::size_t>(row * side + column);
                d.slope_x[out] = static_cast<float>((right - left) / (2 * texel));
                d.slope_y[out] = static_cast<float>((up - down) / (2 * texel));
                d.hollow[out] = static_cast<float>(std::clamp((around - h[at]) / .012, 0.0, 1.0));
            }
    }
}

bool Shape::fine(int face, double x, double y, float& slope_x, float& slope_y, float& hollow) const {
    const Detail& d = detail_[face == 0 ? 0 : 1];
    if (d.side == 0)
        return false;
    const double fx = std::clamp((x / reach + 1) * .5 * d.side - .5, 0.0, d.side - 1.001);
    const double fy = std::clamp((1 - y / reach) * .5 * d.side - .5, 0.0, d.side - 1.001);
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    const float tx = static_cast<float>(fx - x0);
    const float ty = static_cast<float>(fy - y0);
    const std::size_t a = static_cast<std::size_t>(y0 * d.side + x0);
    const std::size_t b = a + static_cast<std::size_t>(d.side);
    const float w00 = (1 - tx) * (1 - ty);
    const float w01 = tx * (1 - ty);
    const float w10 = (1 - tx) * ty;
    const float w11 = tx * ty;
    slope_x = d.slope_x[a] * w00 + d.slope_x[a + 1] * w01 + d.slope_x[b] * w10 + d.slope_x[b + 1] * w11;
    slope_y = d.slope_y[a] * w00 + d.slope_y[a + 1] * w01 + d.slope_y[b] * w10 + d.slope_y[b + 1] * w11;
    hollow = d.hollow[a] * w00 + d.hollow[a + 1] * w01 + d.hollow[b] * w10 + d.hollow[b + 1] * w11;
    return true;
}

std::vector<float> Shape::seen(int orientation, int side) const {
    const int face = orientation / 4;
    const int turns = orientation % 4;
    std::vector<float> grid(static_cast<std::size_t>(side * side), static_cast<float>(outside_height));
    for (int row = 0; row < side; ++row) {
        for (int column = 0; column < side; ++column) {
            // The screen point, then back through the quarter turns (clockwise on screen,
            // so undone anticlockwise) and the flip about the screen's upright.
            double x = ((column + .5) / side * 2 - 1) * reach;
            double y = (1 - (row + .5) / side * 2) * reach;
            for (int k = 0; k < turns; ++k) {
                const double ox = -y;
                const double oy = x;
                x = ox;
                y = oy;
            }
            if (face == 1)
                x = -x;
            double h = 0;
            if (height(face, x, y, h))
                grid[static_cast<std::size_t>(row * side + column)] = static_cast<float>(h);
        }
    }
    return grid;
}

double seen_distance(const std::vector<float>& a, const std::vector<float>& b, int side) {
    double heights = 0;
    double slopes = 0;
    for (int row = 0; row < side; ++row) {
        for (int column = 0; column < side; ++column) {
            const std::size_t at = static_cast<std::size_t>(row * side + column);
            heights += std::abs(a[at] - b[at]);
            if (column + 1 < side && row + 1 < side) {
                const double ax = a[at + 1] - a[at];
                const double ay = a[at + static_cast<std::size_t>(side)] - a[at];
                const double bx = b[at + 1] - b[at];
                const double by = b[at + static_cast<std::size_t>(side)] - b[at];
                slopes += std::abs(ax - bx) + std::abs(ay - by);
            }
        }
    }
    const double cells = static_cast<double>(side * side);
    return heights / cells + slopes / cells * side * .1;
}

double self_distance(const Shape& shape, int side) {
    std::vector<std::vector<float>> seen;
    for (int o = 0; o < 8; ++o)
        seen.push_back(shape.seen(o, side));
    double least = 1e9;
    for (int a = 0; a < 8; ++a)
        for (int b = a + 1; b < 8; ++b)
            least = std::min(least, seen_distance(seen[static_cast<std::size_t>(a)], seen[static_cast<std::size_t>(b)], side));
    return least;
}

double cross_distance(const Shape& a, const Shape& b, int side) {
    const std::vector<float> first = a.seen(0, side);
    double least = 1e9;
    for (int o = 0; o < 8; ++o)
        least = std::min(least, seen_distance(first, b.seen(o, side), side));
    return least;
}

}  // namespace ts
