// A knapped flake, of flint or obsidian: struck from a core at its platform, one face is
// the single break that freed it (smooth, curling a little, swelling into the bulb below
// the platform, rippled in rings round where the blow fell and fine hackles towards the
// edge), the other the scars of the flakes struck before it, shallow dishes meeting in
// sharp ridges. It thins to keen edges everywhere but the platform.
#include "shapes.hpp"
#include "volumes.hpp"

#include <vector>

namespace ts {
namespace {

struct Scar {
    double at[2] = {0, 0};
    double depth = .04;       // how near the flake's underside it was struck
    double dish = .5;         // how quickly it rises from there
    double toward[2] = {1, 0}; // the way it ran in from the edge
    double run = 1.8;          // how much longer than wide
};

struct Flake final : Solid {
    std::uint32_t key = 1;
    double length = 2;
    double width = .7;        // half its widest
    double thickness = .28;
    double curl = .2;
    double bulb = .07;
    double skew = 0;
    double tip = .6;          // where it begins to close towards its far end
    double platform = .4;     // the platform's slope
    std::vector<Scar> scars;
    std::vector<BrokenFace> sides;  // straight runs of its edge, where it snapped short

    double start() const {
        return -length * .5;
    }

    // The underside: curling, the bulb under the platform, rings round the blow.
    double under(double x, double y, bool rippled) const {
        const double t = (x - start()) / length;
        const double bx = x - start() - .2;
        double z = curl * t * t * .6 - bulb * std::exp(-(bx * bx + y * y * 1.5) / .06);
        if (rippled) {
            const double r = std::hypot(x - start(), y);
            z += .006 * std::sin(r * 30) * std::exp(-r * .8);
        }
        return z;
    }

    double half_width(double t) const {
        const double closing = t > tip ? (t - tip) / (1 - tip) : 0.0;
        return width * (.3 + .7 * std::sqrt(std::clamp(t, 0.0, 1.0))) * std::sqrt(std::max(0.0, 1 - closing * closing));
    }

    // The upper face: the thickness it was struck with, cut down by older scars. Which scar
    // lies lowest is returned in `which` (-1 none).
    double over(double x, double y, int& which) const {
        const double t = (x - start()) / length;
        const double base = under(x, y, false);
        const double w = std::max(.05, half_width(t));
        const double across = (y - skew * (x - start())) / w;
        double z = base + thickness * (1 - .55 * t) * (1 - .6 * across * across);
        which = -1;
        for (std::size_t k = 0; k < scars.size(); ++k) {
            const Scar& s = scars[k];
            const double dx = x - s.at[0];
            const double dy = y - s.at[1];
            const double along = (dx * s.toward[0] + dy * s.toward[1]) / s.run;
            const double across = -dx * s.toward[1] + dy * s.toward[0];
            const double dish = base + s.depth + s.dish * (along * along + across * across);
            if (dish < z) {
                z = dish;
                which = static_cast<int>(k);
            }
        }
        return std::max(z, base + .045);
    }

    double edge(double x, double y) const {
        const double t = (x - start()) / length;
        const double outline = std::abs(y - skew * (x - start())) - half_width(t);
        double d = std::max(outline, (x - start()) - length);
        for (const BrokenFace& side : sides) {
            double lateral = 0;
            d = std::max(d, side.value(x, y, 0, lateral));
        }
        return d + .045 * fbm2(x * 4, y * 4, key + 3, 3);
    }

    double platform_face(double x, double z) const {
        // Facing back and a little up, at the struck end.
        return -(x - start()) * std::cos(platform) - (z - thickness * .5) * std::sin(platform) * .3;
    }

    double density(double x, double y, double z) const override {
        int which = -1;
        return std::max({under(x, y, true) - z, z - over(x, y, which), edge(x, y), platform_face(x, z)});
    }

    double relief(double x, double y, double z) const override {
        int which = -1;
        const double below = under(x, y, true) - z;
        const double above = z - over(x, y, which);
        const double side = edge(x, y);
        const double back = platform_face(x, z);
        if (below >= above && below >= side && below >= back) {
            const double r = std::hypot(x - start(), y);
            const double rings = .0014 * std::sin(r * 140 + 1.5 * noise2(x * 3, y * 3, key + 5)) * std::exp(-r * .7);
            const double hackles = .0005 * std::sin(std::atan2(y, x - start()) * 260) * grown_ease(.8, 1.6, r);
            return rings + hackles;
        }
        if (above >= side && above >= back && which >= 0) {
            const Scar& s = scars[static_cast<std::size_t>(which)];
            const double r = std::hypot(x - s.at[0], y - s.at[1]);
            return .001 * std::sin(r * 150 + noise2(x * 3, y * 3, key + 7)) * std::exp(-r * .9);
        }
        return .001 * noise3(x * 60, y * 60, z * 60, key + 9);
    }
};

}  // namespace

void Shape::grow_flake() {
    const std::array<double, 16>& f = measures_.feature;
    Flake flake;
    flake.key = static_cast<std::uint32_t>(f[4]);
    const std::uint32_t key = flake.key;
    flake.length = f[1];
    flake.width = f[2];
    flake.thickness = f[3];
    flake.curl = f[5];
    flake.bulb = f[6];
    flake.skew = f[8];
    flake.platform = f[9];
    flake.tip = f[10];
    const int count = static_cast<int>(f[7]);
    for (int k = 0; k < count; ++k) {
        // Struck from round the edge, away from the platform.
        const double angle = volume_pi * (-.85 + 1.7 * (k + .6 * grown_unit(k, 81, key)) / count);
        const double reach = .55 + .4 * grown_unit(k, 82, key);
        Scar s;
        s.at[0] = std::cos(angle) * flake.length * .5 * reach;
        s.at[1] = std::sin(angle) * (flake.width + .1) * reach;
        s.depth = .05 * grown_unit(k, 83, key);
        s.dish = .9 + .8 * grown_unit(k, 84, key);
        s.toward[0] = -std::cos(angle);
        s.toward[1] = -std::sin(angle);
        s.run = 1.5 + .8 * grown_unit(k, 85, key);
        flake.scars.push_back(s);
    }
    const double middle_line[3] = {0, 0, 0};
    const double up[3] = {0, 0, 1};
    grown_break_round(middle_line, up, static_cast<int>(f[11]), flake.width * .9, 0, key + 9, flake.sides);
    for (BrokenFace& side : flake.sides) {
        // Stretched along the flake: its runs of edge lie along its length more often.
        side.at[0] *= flake.length * .5 / flake.width;
        side.at[2] = 0;
    }
    const double middle[3] = {0, 0, .1};
    const double grain[3] = {1, 0, 0};
    grow_from(flake, middle, flake.length * .5 + .35, grain, middle);
}

}  // namespace ts
