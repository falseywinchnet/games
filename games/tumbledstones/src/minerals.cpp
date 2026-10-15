#include "minerals.hpp"

#include "noise.hpp"

#include <algorithm>
#include <cmath>

namespace ts {
namespace {

constexpr double pi = 3.14159265358979323846;

Linear hex(std::uint32_t rgb) {
    const float r = static_cast<float>((rgb >> 16) & 0xffU) / 255.0F;
    const float g = static_cast<float>((rgb >> 8) & 0xffU) / 255.0F;
    const float b = static_cast<float>(rgb & 0xffU) / 255.0F;
    return {linear_from_srgb(r), linear_from_srgb(g), linear_from_srgb(b)};
}

Linear mix(Linear a, Linear b, double t) {
    const float k = static_cast<float>(std::clamp(t, 0.0, 1.0));
    return {a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k};
}

Linear scaled(Linear a, double k) {
    const float f = static_cast<float>(k);
    return {a.r * f, a.g * f, a.b * f};
}

double smoothstep(double edge0, double edge1, double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

double fraction(double x) {
    return x - std::floor(x);
}

// A colour a little off the mineral's own: brighter or darker, warmer or cooler.
Linear varied(Linear c, std::uint64_t& random, double amount) {
    const double light = 1 + next_between(random, -amount, amount);
    const double warm = next_between(random, -amount, amount) * .5;
    return {static_cast<float>(c.r * light * (1 + warm)), static_cast<float>(c.g * light),
            static_cast<float>(c.b * light * (1 - warm))};
}

void unit_vector(std::uint64_t& random, double out[3]) {
    const double z = next_between(random, -1, 1);
    const double a = next_between(random, -pi, pi);
    const double r = std::sqrt(std::max(0.0, 1 - z * z));
    out[0] = r * std::cos(a);
    out[1] = r * std::sin(a);
    out[2] = z;
}

// Through the palette's first `count` colours by t's fraction, with soft steps between.
Linear banded(const Finish& f, double t, int count, double softness) {
    const double at = fraction(t) * count;
    const int index = static_cast<int>(std::floor(at)) % count;
    const int next = (index + 1) % count;
    const double within = at - std::floor(at);
    const double blend = smoothstep(1 - softness, 1.0, within);
    return mix(f.palette[static_cast<std::size_t>(index)], f.palette[static_cast<std::size_t>(next)], blend);
}

double along(const Finish& f, double x, double y, double z) {
    return (x - f.centre[0]) * f.axis[0] + (y - f.centre[1]) * f.axis[1] + (z - f.centre[2]) * f.axis[2];
}

double from_centre(const Finish& f, double x, double y, double z) {
    const double dx = x - f.centre[0];
    const double dy = y - f.centre[1];
    const double dz = z - f.centre[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Distance from the line through the centre along the axis: a tree's rings.
double from_axis(const Finish& f, double x, double y, double z) {
    const double dx = x - f.centre[0];
    const double dy = y - f.centre[1];
    const double dz = z - f.centre[2];
    const double a = dx * f.axis[0] + dy * f.axis[1] + dz * f.axis[2];
    const double px = dx - f.axis[0] * a;
    const double py = dy - f.axis[1] * a;
    const double pz = dz - f.axis[2] * a;
    return std::sqrt(px * px + py * py + pz * pz);
}

// A speck where a cell's random point lies near: flecks of pyrite, pores, inclusions.
double specks(double x, double y, double z, double frequency, double size, double share, std::uint32_t seed) {
    const double sx = x * frequency;
    const double sy = y * frequency;
    const double sz = z * frequency;
    const int cx = static_cast<int>(std::floor(sx));
    const int cy = static_cast<int>(std::floor(sy));
    const int cz = static_cast<int>(std::floor(sz));
    double best = 0;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const std::uint32_t h = hash_cell(cx + dx, cy + dy, cz + dz, seed);
                if (static_cast<double>(h & 0xffffU) / 65536.0 > share)
                    continue;
                const double px = cx + dx + static_cast<double>((h >> 16) & 0xffU) / 256.0;
                const double py = cy + dy + static_cast<double>((h >> 24) & 0xffU) / 256.0;
                const double pz = cz + dz + static_cast<double>(hash_bits(h) & 0xffU) / 256.0;
                const double d = std::sqrt((sx - px) * (sx - px) + (sy - py) * (sy - py) + (sz - pz) * (sz - pz));
                best = std::max(best, 1 - smoothstep(size * .6, size, d));
            }
    return best;
}

struct Weighted {
    Mineral mineral;
    double weight;
};
// Each kind of material about as likely as any other, so the tray holds them all: dark
// polished stone, woods, bone, candy, shell, glass, and the pretty gems.
constexpr Weighted mineral_weights[] = {
    // dark polished stone
    {Mineral::obsidian, 1.1}, {Mineral::labradorite, .7}, {Mineral::lava, 1.0},
    // woods
    {Mineral::walnut, 1.0}, {Mineral::petrified, 1.4},
    // bone
    {Mineral::bone, 1.6},
    // pulled candy
    {Mineral::candy, 1.8},
    // shell
    {Mineral::nacre, 1.0}, {Mineral::cone, .9},
    // glass and the clear stones
    {Mineral::seaglass, 1.0}, {Mineral::quartz, .4}, {Mineral::smoky, .3}, {Mineral::amber, 1.0},
    // cave stone
    {Mineral::dripstone, 1.0},
    // the pretty gems, blues and purples first
    {Mineral::amethyst, .5}, {Mineral::lapis, .5}, {Mineral::sodalite, .3}, {Mineral::blue_lace, .3},
    {Mineral::chrysocolla, .2}, {Mineral::rose, .2}, {Mineral::jade, .2}, {Mineral::malachite, .2},
    {Mineral::agate, .5}, {Mineral::jasper, .3},
};

}  // namespace

float linear_from_srgb(float coded) {
    const double c = std::clamp(static_cast<double>(coded), 0.0, 1.0);
    const double linear = c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4);
    return static_cast<float>(linear);
}

float srgb_from_linear(float linear) {
    const double l = std::clamp(static_cast<double>(linear), 0.0, 1.0);
    const double coded = l <= .0031308 ? l * 12.92 : 1.055 * std::pow(l, 1 / 2.4) - .055;
    return static_cast<float>(coded);
}

const char* mineral_name(Mineral mineral) {
    switch (mineral) {
    case Mineral::obsidian:
        return "obsidian";
    case Mineral::walnut:
        return "walnut";
    case Mineral::petrified:
        return "petrified wood";
    case Mineral::bone:
        return "bone";
    case Mineral::agate:
        return "agate";
    case Mineral::amethyst:
        return "amethyst";
    case Mineral::lapis:
        return "lapis lazuli";
    case Mineral::sodalite:
        return "sodalite";
    case Mineral::malachite:
        return "malachite";
    case Mineral::quartz:
        return "rock crystal";
    case Mineral::smoky:
        return "smoky quartz";
    case Mineral::rose:
        return "rose quartz";
    case Mineral::jade:
        return "jade";
    case Mineral::labradorite:
        return "labradorite";
    case Mineral::jasper:
        return "jasper";
    case Mineral::amber:
        return "amber";
    case Mineral::blue_lace:
        return "blue lace agate";
    case Mineral::chrysocolla:
        return "chrysocolla";
    case Mineral::candy:
        return "pulled candy";
    case Mineral::nacre:
        return "mother of pearl";
    case Mineral::cone:
        return "cone shell";
    case Mineral::seaglass:
        return "sea glass";
    case Mineral::lava:
        return "lava";
    case Mineral::dripstone:
        return "dripstone";
    }
    return "stone";
}

int mineral_weight(int mineral) {
    double total = 0;
    double mine = 0;
    for (const Weighted& w : mineral_weights) {
        total += w.weight;
        if (static_cast<int>(w.mineral) == mineral)
            mine += w.weight;
    }
    return static_cast<int>(std::lround(mine / total * 1000));
}

Finish draw_finish(std::uint32_t seed) {
    std::uint64_t random = 0xa5a5f00dULL ^ (static_cast<std::uint64_t>(seed) * 0x9e3779b97f4a7c15ULL);
    double total = 0;
    for (const Weighted& w : mineral_weights)
        total += w.weight;
    double pick = next_unit(random) * total;
    Mineral mineral = Mineral::obsidian;
    for (const Weighted& w : mineral_weights) {
        mineral = w.mineral;
        pick -= w.weight;
        if (pick < 0)
            break;
    }
    return make_finish(mineral, seed);
}

Finish make_finish(Mineral mineral, std::uint32_t seed) {
    Finish f;
    f.mineral = mineral;
    f.seed = seed;
    std::uint64_t random = 0x3c6ef372ULL ^ (static_cast<std::uint64_t>(seed) << 21) ^ static_cast<std::uint64_t>(mineral);
    unit_vector(random, f.axis);
    f.centre[0] = next_between(random, -.6, .6);
    f.centre[1] = next_between(random, -.6, .6);
    f.centre[2] = next_between(random, -.3, .3);
    f.variant = next_below(random, 4);
    std::array<Linear, 5>& p = f.palette;
    switch (mineral) {
    case Mineral::obsidian:
        p = {hex(0x0b0a0d), hex(0x1e1d24), hex(0xd8d6cc), hex(0x3a3640), hex(0x050506)};
        f.variant = next_below(random, 3);  // 0 plain, 1 snowflake, 2 a rainbow sheen
        f.frequency = next_between(random, 2, 5);
        f.warp = next_between(random, .2, .6);
        f.roughness = .03;
        f.clarity = .04;
        f.absorb = {6, 6, 5};
        if (f.variant == 2) {
            f.sheen = .5;
            f.sheen_colour = hex(0x6a5aa8);
        }
        break;
    case Mineral::walnut:
        // Black walnut, oiled: chocolate heartwood with a purple-grey cast, darker streaks
        // and lighter figure, its pores fine dark dashes along the grain. Plain-sawn, the
        // rings rise in arches; quartersawn, they run in stripes; a burl swirls round its
        // eyes; now and then a streak of pale sapwood. Its curl shimmers as it turns.
        f.variant = next_below(random, 4);
        p = {varied(hex(0x4a3024), random, .08), varied(hex(0x2c1c16), random, .08), varied(hex(0x6a4a34), random, .08),
             hex(0x150c08), hex(0xd2b488)};
        f.axis[2] *= .3;
        f.centre[2] = f.variant == 0 ? (next_unit(random) < .5 ? -1.0 : 1.0) * next_between(random, .9, 1.5) : 0.0;
        f.frequency = next_between(random, 10, 18);
        f.warp = next_between(random, .25, .6);
        // Only some walnut is curly.
        f.sheen = next_unit(random) < .4 ? .22 : 0.0;
        f.sheen_colour = hex(0xc89a62);
        break;
    case Mineral::petrified: {
        // Fossil wood, after tumble-polished pieces: a ground colour, the grain's fibres in
        // a second, patches of a third, and the darkest for the hairline cracks.
        //   0 cream and tan (Wyoming), 1 black with cream (Japan), 2 red jasperized with
        //   yellow (Arizona), 3 grey-brown with white.
        f.variant = next_below(random, 4);
        if (f.variant == 0)
            p = {varied(hex(0xd8c4a2), random, .06), hex(0x5a3a22), hex(0xb07a3a), hex(0xeee2cc), hex(0x241610)};
        else if (f.variant == 1)
            p = {varied(hex(0x1c1814), random, .1), hex(0xd8c8a8), hex(0x8a7a62), hex(0x3a3028), hex(0x0a0807)};
        else if (f.variant == 2)
            p = {varied(hex(0x9a3a20), random, .08), hex(0xe2a64a), hex(0x5a1c10), hex(0xc86a30), hex(0x2a0e08)};
        else
            p = {varied(hex(0x8a7c6a), random, .06), hex(0x3a2c22), hex(0xe6ddcc), hex(0xb0a28a), hex(0x1c1612)};
        f.axis[2] *= .2;
        f.frequency = next_between(random, 5, 9);
        f.warp = next_between(random, .2, .45);
        f.roughness = .06;
        f.clarity = .04;
        f.absorb = {3, 4, 5};
        break;
    }
    case Mineral::bone:
        p = {varied(hex(0xe8dec8), random, .06), varied(hex(0xcbbb9c), random, .06), hex(0x9c8a6c), hex(0xf2ead8),
             hex(0xb8a888)};
        f.frequency = next_between(random, 6, 12);
        f.warp = next_between(random, .2, .5);
        f.roughness = .28;
        f.clarity = .12;
        f.absorb = {1.0, 1.4, 2.4};
        break;
    case Mineral::agate: {
        f.variant = next_below(random, 4);
        if (f.variant == 0)
            p = {hex(0x4f6278), hex(0xa9b8c9), hex(0x2a3446), hex(0xe4e8ec), hex(0x7a8ca2)};
        else if (f.variant == 1)
            p = {hex(0x8a2a12), hex(0xd06a38), hex(0x5a170a), hex(0xf0c8a4), hex(0xa83c1c)};
        else if (f.variant == 2)
            p = {hex(0x3e1e5e), hex(0x8a5ab4), hex(0x241238), hex(0xdccbec), hex(0x5c3486)};
        else
            p = {hex(0x24493a), hex(0x6aa088), hex(0x123024), hex(0xd8e8de), hex(0x3a6a52)};
        for (Linear& c : p)
            c = varied(c, random, .08);
        f.frequency = next_between(random, 7, 14);
        f.warp = next_between(random, .25, .6);
        f.roughness = .03;
        f.clarity = .45;
        f.absorb = {1.2, 1.2, 1.2};
        break;
    }
    case Mineral::amethyst:
        p = {varied(hex(0x2c0f48), random, .1), varied(hex(0x6c38a0), random, .1), varied(hex(0xc8b0e4), random, .06),
             hex(0xece4f4), hex(0x180828)};
        f.frequency = next_between(random, 5, 9);
        f.warp = next_between(random, .1, .3);
        f.roughness = .02;
        f.clarity = .6;
        f.absorb = {1.0, 2.6, .7};
        break;
    case Mineral::lapis:
        p = {varied(hex(0x14267a), random, .1), varied(hex(0x2a44b0), random, .1), hex(0xd4d8dc), hex(0xd8b24c),
             hex(0x0a1450)};
        f.frequency = next_between(random, 2, 4);
        f.warp = .5;
        f.roughness = .09;
        f.flecks = next_between(random, .5, 1);
        break;
    case Mineral::sodalite:
        p = {varied(hex(0x1c2c6c), random, .1), varied(hex(0x2e4290), random, .1), hex(0xdcdfe6), hex(0x101a44),
             hex(0x8c96b8)};
        f.frequency = next_between(random, 2, 4);
        f.warp = .6;
        f.roughness = .09;
        break;
    case Mineral::malachite:
        p = {varied(hex(0x0b4a2a), random, .1), varied(hex(0x1e8a50), random, .1), varied(hex(0x52bc80), random, .08),
             hex(0x062a16), hex(0x2aa060)};
        f.frequency = next_between(random, 10, 18);
        f.warp = next_between(random, .3, .6);
        f.roughness = .05;
        break;
    case Mineral::quartz:
        p = {hex(0xf0f2f4), hex(0xe2e6ea), hex(0xffffff), hex(0xd8dce0), hex(0xf8f8f8)};
        f.frequency = 3;
        f.warp = .5;
        f.roughness = .02;
        f.clarity = .9;
        f.absorb = {.08, .06, .04};
        break;
    case Mineral::smoky:
        p = {varied(hex(0x5a4a3c), random, .1), hex(0x8a7a6a), hex(0x2a221c), hex(0xb0a090), hex(0x40342a)};
        f.frequency = 3;
        f.warp = .5;
        f.roughness = .02;
        f.clarity = .75;
        f.absorb = {1.6, 2.0, 2.6};
        break;
    case Mineral::rose:
        p = {varied(hex(0xeab0bc), random, .06), hex(0xf6d4dc), hex(0xd890a0), hex(0xfce8ee), hex(0xe0a0b0)};
        f.frequency = 3;
        f.warp = .5;
        f.roughness = .05;
        f.clarity = .45;
        f.absorb = {.3, 1.0, .8};
        break;
    case Mineral::jade:
        p = {varied(hex(0x3a7c4c), random, .1), varied(hex(0x74aa7c), random, .1), hex(0x1c4a2a), hex(0xa8d0a8),
             hex(0x2a6038)};
        f.frequency = 3;
        f.warp = .6;
        f.roughness = .14;
        f.clarity = .32;
        f.absorb = {1.8, .6, 1.4};
        break;
    case Mineral::labradorite:
        p = {varied(hex(0x34373c), random, .1), hex(0x4c5058), hex(0x1c1e22), hex(0x60646a), hex(0x26282c)};
        f.frequency = 3;
        f.warp = .6;
        f.roughness = .05;
        f.sheen = 1.0;
        f.variant = next_below(random, 3);
        f.sheen_colour = f.variant == 0 ? hex(0x2a78d8) : (f.variant == 1 ? hex(0x30b0a0) : hex(0xd8a030));
        break;
    case Mineral::jasper:
        p = {varied(hex(0x7a2a1a), random, .12), varied(hex(0xa8582c), random, .12), hex(0x4a1a10), hex(0xc8904c),
             hex(0x5c3424)};
        f.frequency = next_between(random, 3, 6);
        f.warp = next_between(random, .6, 1.0);
        f.roughness = .07;
        break;
    case Mineral::amber: {
        // Fossil resin: honey, cognac, cherry or cloudy butterscotch, laid down in flows that
        // show as faint curved sheets, with bubbles, bits of bark and now and then a
        // glittering sun spangle where it crazed.
        f.variant = next_below(random, 4);
        const std::uint32_t bodies[4] = {0xe0901c, 0xb85a12, 0xc0401a, 0xeab04a};
        const std::uint32_t flows[4] = {0xf4b440, 0xd88024, 0xd8602a, 0xf4cc78};
        p = {varied(hex(bodies[f.variant]), random, .08), varied(hex(flows[f.variant]), random, .08), hex(0x4a2406),
             hex(0xfff0c0), hex(0x2a1404)};
        f.frequency = next_between(random, 2.5, 5);
        f.warp = next_between(random, .4, .9);
        f.roughness = .03;
        f.clarity = f.variant == 3 ? .5 : .78;
        f.flecks = f.variant == 3 ? 0 : next_between(random, 0, 1);
        const Linear body = p[0];
        const double depth = f.variant == 1 || f.variant == 2 ? 1.5 : 1.1;
        f.absorb = {static_cast<float>(-std::log(std::max(.03, static_cast<double>(body.r))) * depth),
                    static_cast<float>(-std::log(std::max(.03, static_cast<double>(body.g))) * depth),
                    static_cast<float>(-std::log(std::max(.03, static_cast<double>(body.b))) * depth)};
        break;
    }
    case Mineral::lava: {
        // Polished lava: black basalt riddled with gas pores, red-brown scoria, grey lava
        // with pale crystals in it, or glassy black with ropy flow lines.
        f.variant = next_below(random, 4);
        const std::uint32_t grounds[4] = {0x1a1a1c, 0x4a1c12, 0x3c3a3a, 0x0e0d0e};
        const std::uint32_t seconds[4] = {0x2c2a2a, 0x6a2a16, 0x55524f, 0x26222a};
        p = {varied(hex(grounds[f.variant]), random, .1), varied(hex(seconds[f.variant]), random, .1), hex(0x060505),
             hex(0xd8d2c4), hex(0x7a3418)};
        f.frequency = next_between(random, 6, 11);
        f.warp = next_between(random, .5, 1.0);
        f.roughness = f.variant == 3 ? .05 : .22;
        f.shine = f.variant == 3 ? .045 : .035;
        break;
    }
    case Mineral::dripstone: {
        // Cave onyx: calcite laid down drip by drip, translucent, in fine wavy layers of
        // honey, cream and caramel; across a stalactite the layers are rings. Some are green
        // and white, some banded with rust.
        f.variant = next_below(random, 4);
        if (f.variant == 2) {
            p = {varied(hex(0xc8dcb0), random, .06), hex(0x8aae78), hex(0xf2f2e4), hex(0x5a8a5a), hex(0xe2ecd2)};
        } else if (f.variant == 3) {
            p = {varied(hex(0xe8c89a), random, .06), hex(0xa8441a), hex(0xf6ead6), hex(0x6a2a10), hex(0xd89a5a)};
        } else {
            p = {varied(hex(0xe6c68e), random, .06), hex(0xb8823e), hex(0xf8eed8), hex(0x8a5a26), hex(0xd8a868)};
        }
        f.frequency = next_between(random, 9, 18);
        f.warp = next_between(random, .25, .6);
        f.roughness = .05;
        f.clarity = .42;
        f.scatter = .45;
        f.absorb = {.6F, 1.0F, 1.8F};
        break;
    }
    case Mineral::blue_lace:
        p = {varied(hex(0x9cb8d4), random, .06), hex(0xdce8f2), hex(0x6a8cb0), hex(0xeef4f8), hex(0x84a4c4)};
        f.frequency = next_between(random, 14, 24);
        f.warp = next_between(random, .3, .6);
        f.roughness = .04;
        f.clarity = .3;
        f.absorb = {1.2, .8, .5};
        break;
    case Mineral::chrysocolla:
        p = {varied(hex(0x1a6a88), random, .1), varied(hex(0x3aa8b4), random, .1), hex(0x0a2c40), hex(0x7cc8c0),
             hex(0x2a3a2a)};
        f.frequency = 3;
        f.warp = .7;
        f.roughness = .1;
        break;
    case Mineral::candy: {
        // Boiled sugar: clear and coloured, with opaque stripes pulled through it.
        const std::uint32_t colours[6] = {0xc81830, 0x3aa020, 0x6a1a9a, 0x1a5ad0, 0xe86a10, 0xe0508a};
        const std::uint32_t stripes[6] = {0xf4f0ea, 0xf4f0ea, 0xf2d040, 0xf4f0ea, 0xf4f0ea, 0x40b0e0};
        f.variant = next_below(random, 6);
        const Linear sugar = hex(colours[f.variant]);
        p = {sugar, hex(stripes[f.variant]), hex(colours[(f.variant + 3) % 6]), hex(0xfaf6f0), sugar};
        f.frequency = next_between(random, 3.5, 6);
        f.warp = next_between(random, .3, .8);
        f.roughness = .015;
        f.clarity = .74;
        f.scatter = .55;
        f.absorb = {static_cast<float>(-std::log(std::max(.04, static_cast<double>(sugar.r))) * 1.6),
                    static_cast<float>(-std::log(std::max(.04, static_cast<double>(sugar.g))) * 1.6),
                    static_cast<float>(-std::log(std::max(.04, static_cast<double>(sugar.b))) * 1.6)};
        break;
    }
    case Mineral::nacre:
        // Mother of pearl: thin layers that send back a rainbow running with the angle and
        // with the growth lines. White, golden pink, black (Tahitian) or abalone, whose
        // swirls flash blue, green and violet.
        f.variant = next_below(random, 4);
        if (f.variant == 0)
            p = {varied(hex(0xece6e0), random, .04), hex(0xdcd4d8), hex(0xf8f4f0), hex(0xc8c0cc), hex(0xb8b0bc)};
        else if (f.variant == 1)
            p = {varied(hex(0xf0dccc), random, .04), hex(0xe4c4b4), hex(0xfaf0e6), hex(0xd8b4a4), hex(0xc8a090)};
        else if (f.variant == 2)
            p = {varied(hex(0x3c3c46), random, .06), hex(0x26262e), hex(0x5c5a68), hex(0x18181e), hex(0x6c6c7c)};
        else
            p = {varied(hex(0x24404e), random, .06), hex(0x2a5a5a), hex(0x3c3c6c), hex(0x123040), hex(0x4a6a5a)};
        f.frequency = next_between(random, 12, 22);
        f.warp = f.variant == 3 ? next_between(random, 1.0, 1.6) : next_between(random, .3, .6);
        f.roughness = .035;
        f.clarity = .1;
        f.absorb = {1.5, 1.5, 1.5};
        f.sheen = f.variant >= 2 ? 1.5 : 1.2;
        f.rainbow = true;
        break;
    case Mineral::cone: {
        // A cone shell's porcelain: cream, with brown tents and zigzags, a satin glaze.
        const std::uint32_t marks[3] = {0x6a3416, 0x3a2418, 0x9a4a1c};
        p = {varied(hex(0xf0e2c8), random, .06), hex(marks[next_below(random, 3)]), hex(0xe8c89a), hex(0xfaf2e2),
             hex(0xc89a6a)};
        f.frequency = next_between(random, 5, 9);
        f.warp = next_between(random, .3, .6);
        f.roughness = .2;
        break;
    }
    case Mineral::seaglass: {
        // Tumbled glass, frosted all over: the light goes in and glows.
        const std::uint32_t tints[5] = {0x8ed2c4, 0x6aa878, 0x3a5aaa, 0xb88a3a, 0xe4eae6};
        const Linear tint = varied(hex(tints[next_below(random, 5)]), random, .05);
        p = {tint, tint, hex(0xffffff), tint, tint};
        f.frequency = 3;
        f.warp = .4;
        f.roughness = .5;
        f.clarity = .68;
        f.scatter = .32;
        f.absorb = {static_cast<float>(-std::log(std::max(.05, static_cast<double>(tint.r))) * 1.2),
                    static_cast<float>(-std::log(std::max(.05, static_cast<double>(tint.g))) * 1.2),
                    static_cast<float>(-std::log(std::max(.05, static_cast<double>(tint.b))) * 1.2)};
        break;
    }
    }
    // Glints: pyrite in lapis gold, mica silver-green, crystal faces white.
    if (mineral == Mineral::amethyst) {
        f.flecks = next_between(random, .5, 1);
        f.glitter = hex(0xf0e8ff);
    }
    if (mineral == Mineral::quartz) {
        f.flecks = next_between(random, .4, 1);
        f.glitter = hex(0xffffff);
    }
    if (mineral == Mineral::jade && next_unit(random) < .35) {
        f.flecks = next_between(random, .6, 1);
        f.glitter = hex(0xd8f8d0);
    }
    if (mineral == Mineral::candy && next_unit(random) < .3) {
        f.flecks = 1;
        f.glitter = hex(0xffffff);
        f.roughness = .45;
    }
    if (mineral == Mineral::amber)
        f.glitter = hex(0xffd890);
    if (mineral == Mineral::malachite) {
        f.sheen = .45;
        f.sheen_colour = hex(0x9ae8b8);
    }
    // Finishes the minerals' own surfaces have: oiled wood is satin, bone chalky, porous
    // lava and chalky chrysocolla matte, rose quartz and jade waxy.
    if (mineral == Mineral::lava && f.variant != 3)
        f.roughness = .4;
    if (mineral == Mineral::chrysocolla)
        f.roughness = .32;
    if (mineral == Mineral::rose)
        f.roughness = .22;
    if (mineral == Mineral::sodalite)
        f.roughness = .18;
    if (mineral == Mineral::jasper)
        f.roughness = .12;
    if (mineral == Mineral::jade)
        f.roughness = f.flecks > 0 ? .12 : .2;
    if (mineral == Mineral::dripstone)
        f.roughness = .08;
    if (mineral == Mineral::walnut)
        f.roughness = .3;
    if (mineral == Mineral::bone) {
        f.roughness = .62;
        f.shine = .022;
        f.scatter = .3;
    }
    if (mineral == Mineral::jade)
        f.scatter = .3;
    if (mineral == Mineral::amber || mineral == Mineral::rose)
        f.scatter = .35;
    return f;
}

MineralSample sample_mineral(const Finish& f, double x, double y, double z) {
    MineralSample s;
    const std::uint32_t seed = f.seed;
    s.clarity = static_cast<float>(f.clarity);
    switch (f.mineral) {
    case Mineral::obsidian: {
        const double t = along(f, x, y, z) * f.frequency + f.warp * fbm3(x * 1.5, y * 1.5, z * 1.5, seed, 4);
        const double band = .5 + .5 * std::sin(t * 2 * pi);
        s.albedo = mix(f.palette[0], f.palette[1], band * band * .5);
        if (f.variant == 1) {
            const double flake = specks(x, y, z, 5, .32, .5, seed + 7);
            const double soft = smoothstep(.0, 1.0, flake) * (.6 + .4 * fbm3(x * 9, y * 9, z * 9, seed + 3, 2));
            s.albedo = mix(s.albedo, f.palette[2], soft * .85);
            s.clarity = static_cast<float>(f.clarity * (1 - soft));
        }
        if (f.variant == 2)
            s.sheen = static_cast<float>(.5 + .5 * std::sin(t * pi * 1.3));
        break;
    }
    case Mineral::walnut: {
        // The grain's own frame: along it, and two ways across.
        double u[3] = {-f.axis[1], f.axis[0], 0};
        const double ul = std::sqrt(u[0] * u[0] + u[1] * u[1]) + 1e-9;
        u[0] /= ul;
        u[1] /= ul;
        const double v[3] = {f.axis[1] * u[2] - f.axis[2] * u[1], f.axis[2] * u[0] - f.axis[0] * u[2],
                             f.axis[0] * u[1] - f.axis[1] * u[0]};
        const double a = along(f, x, y, z);
        const double cu = (x - f.centre[0]) * u[0] + (y - f.centre[1]) * u[1] + (z - f.centre[2]) * u[2];
        const double cv = (x - f.centre[0]) * v[0] + (y - f.centre[1]) * v[1] + (z - f.centre[2]) * v[2];
        const double r = std::sqrt(cu * cu + cv * cv);
        // The rings, wandering along the length; a burl's swirl round its eyes.
        double ring = r * f.frequency + f.warp * fbm3(x * 1.1, y * 1.1, z * 1.1, seed, 3) + .25 * noise3(a * 1.3, 0, 0, seed + 5);
        double eye = 0;
        if (f.variant == 2) {
            ring = 2.2 * fbm3(x * 2.2, y * 2.2, z * 2.2, seed + 17, 4) + r * f.frequency * .35;
            eye = specks(x, y, z, 6, .3, .45, seed + 19);
            ring += eye * 1.5;
        }
        const double within = fraction(ring);
        // Each year: open, porous early wood, darker, then the close late wood.
        const double early = 1 - smoothstep(0, .22, within);
        s.albedo = mix(f.palette[0], f.palette[2], smoothstep(.4, .95, within) * .35);
        s.albedo = mix(s.albedo, f.palette[1], early * .5);
        // Streaks of colour running along the grain.
        const double streak = fbm3(a * .7, cu * 5, cv * 5, seed + 3, 3);
        s.albedo = mix(s.albedo, f.palette[1], smoothstep(0, .45, streak) * .45);
        s.albedo = mix(s.albedo, f.palette[2], smoothstep(0, .45, -streak) * .3);
        // Pores: fine dark dashes along the grain, thickest in the early wood.
        const double pores = specks(a * .12, cu, cv, 80, .3, .12 + .25 * early, seed + 7);
        s.albedo = mix(s.albedo, f.palette[3], pores * .5);
        s.albedo = mix(s.albedo, f.palette[3], eye * .8);
        // A streak of pale sapwood.
        if (f.variant == 3) {
            const double sap = smoothstep(-.05, .08, cu * .8 + cv * .3 + .1 * fbm3(a * .8, cu * 3, 0, seed + 23, 3) - .25);
            s.albedo = mix(s.albedo, scaled(f.palette[4], .85 + .3 * within), sap);
        }
        // The curl: bands across the grain that shimmer as the stone turns.
        s.sheen = static_cast<float>(std::pow(.5 + .5 * std::sin(a * 30 + 4 * fbm3(x * 1.5, y * 1.5, z * 1.5, seed + 29, 3)), 2) *
                                     (.4 + .6 * smoothstep(-.2, .4, fbm3(x * .8, y * .8, z * .8, seed + 31, 2))));
        break;
    }
    case Mineral::petrified: {
        const double a = along(f, x, y, z);
        const double across = from_axis(f, x, y, z);
        // The grain: long fine fibres, wavering a little, some gathered into darker streaks.
        const double waver = .05 * fbm3(a * 1.2, across * 3, 0, seed + 3, 2);
        const double fibre = noise3(a * 1.1, (across + waver) * 85, 0, seed + 9);
        const double streak = noise3(a * .55, (across + waver) * 22, 0, seed + 19);
        const double grain = smoothstep(.25, .6, fibre) * .5 + smoothstep(.35, .7, streak);
        s.albedo = mix(f.palette[0], f.palette[1], std::min(1.0, grain) * .85);
        // Patches where other minerals came in, and the growth rings, faint.
        const double patch = fbm3(a * .9, across * 3.5, z * 2, seed + 13, 4);
        s.albedo = mix(s.albedo, f.palette[2], smoothstep(.15, .45, patch) * .75);
        s.albedo = mix(s.albedo, f.palette[3], smoothstep(.2, .5, -patch) * .5);
        const double ring = fraction(across * f.frequency + f.warp * fbm3(x * 2, y * 2, z * 2, seed, 3));
        s.albedo = mix(s.albedo, f.palette[1], smoothstep(.9, .97, ring) * .3);
        // Hairline cracks: along the grain, and now and then across it.
        const double along_crack = std::abs(noise3(a * .4, (across + waver) * 9, 0, seed + 31));
        const double across_crack = std::abs(noise3(a * 7 + 2.0 * fbm3(x * 2, y * 2, z * 2, seed + 33, 2), across * 2, 0, seed + 35));
        s.albedo = mix(s.albedo, f.palette[4], (1 - smoothstep(.0, .02, along_crack)) * .9);
        s.albedo = mix(s.albedo, f.palette[4], (1 - smoothstep(.0, .012, across_crack)) * smoothstep(.2, .5, patch + .3) * .8);
        break;
    }
    case Mineral::bone: {
        const double lines = .5 + .5 * std::sin((along(f, x, y, z) * f.frequency + f.warp * fbm3(x, y, z, seed, 3)) * 2 * pi);
        s.albedo = mix(f.palette[0], f.palette[1], lines * .35 + .25 * std::max(0.0, fbm3(x * 4, y * 4, z * 4, seed + 1, 3)));
        const double pores = specks(x, y, z, 26, .3, .45, seed + 17);
        s.albedo = mix(s.albedo, f.palette[2], pores * .55);
        s.albedo = mix(s.albedo, f.palette[3], std::max(0.0, fbm3(x * 2, y * 2, z * 2, seed + 4, 3)) * .4);
        break;
    }
    case Mineral::agate:
    case Mineral::malachite: {
        const double wobble = f.warp * fbm3(x * 1.3, y * 1.3, z * 1.3, seed, 4);
        const double t = (from_centre(f, x, y, z) + wobble) * f.frequency;
        const double fine = .5 + .5 * std::sin(t * 2 * pi * 3.1);
        s.albedo = banded(f, t * .25, 4, f.mineral == Mineral::agate ? .35 : .2);
        s.albedo = scaled(s.albedo, .9 + .2 * fine);
        if (f.mineral == Mineral::agate) {
            const double light = (s.albedo.r + s.albedo.g + s.albedo.b) / 3;
            s.clarity = static_cast<float>(std::clamp(f.clarity * (.4 + 1.4 * light), 0.0, .8));
        } else {
            // Malachite's silky lustre: its fibrous bands catch the light in turn.
            s.sheen = static_cast<float>(fine * fine);
        }
        break;
    }
    case Mineral::amethyst: {
        const double a = along(f, x, y, z);
        const double zone = smoothstep(-.8, .9, a + .25 * fbm3(x, y, z, seed, 3));
        const double perp = std::abs((x - f.centre[0]) * f.axis[1] - (y - f.centre[1]) * f.axis[0]);
        const double chevron = .5 + .5 * std::sin((perp * .9 + a) * f.frequency * 2 * pi);
        s.albedo = mix(f.palette[0], f.palette[1], zone);
        s.albedo = mix(s.albedo, f.palette[2], smoothstep(.6, 1.0, zone) * .7);
        s.albedo = scaled(s.albedo, .88 + .24 * chevron);
        s.clarity = static_cast<float>(f.clarity * (.6 + .5 * zone));
        // Now and then a tiny crystal face inside catches the light.
        s.fleck = static_cast<float>(specks(x, y, z, 34, .3, .1 * f.flecks, seed + 13));
        break;
    }
    case Mineral::lapis: {
        const double m = fbm3(x * 2.4, y * 2.4, z * 2.4, seed, 5);
        s.albedo = mix(f.palette[0], f.palette[1], smoothstep(-.4, .5, m));
        const double calcite = fbm3(x * 3.1, y * 3.1, z * 3.1, seed + 3, 5);
        s.albedo = mix(s.albedo, f.palette[2], smoothstep(.25, .5, calcite) * .9);
        const double fleck = specks(x, y, z, 30, .35, .35 * f.flecks, seed + 11);
        s.albedo = mix(s.albedo, f.palette[3], fleck);
        s.fleck = static_cast<float>(fleck);
        break;
    }
    case Mineral::sodalite:
    case Mineral::chrysocolla: {
        const double m = fbm3(x * 2, y * 2, z * 2, seed, 4);
        s.albedo = mix(f.palette[0], f.palette[1], smoothstep(-.4, .5, m));
        const double vein = 1 - std::abs(fbm3(x * 1.7, y * 1.7, z * 1.7, seed + 5, 4));
        const double v = smoothstep(.86, .97, vein);
        s.albedo = mix(s.albedo, f.palette[2], v * (f.mineral == Mineral::sodalite ? .85 : .6));
        if (f.mineral == Mineral::chrysocolla)
            s.albedo = mix(s.albedo, f.palette[4], smoothstep(.3, .6, fbm3(x * 3, y * 3, z * 3, seed + 8, 3)) * .7);
        break;
    }
    case Mineral::quartz:
    case Mineral::smoky:
    case Mineral::rose: {
        const double veil = fbm3(x * 2.2, y * 2.2, z * 2.2, seed, 4);
        const double milky = smoothstep(.15, .6, veil);
        s.albedo = mix(f.palette[0], f.palette[1], std::max(0.0, veil));
        s.clarity = static_cast<float>(f.clarity * (1 - .3 * milky));
        // Rock crystal's healed cracks flash as it turns.
        if (f.flecks > 0)
            s.fleck = static_cast<float>(specks(x, y, z, 26, .3, .08 * f.flecks, seed + 15));
        break;
    }
    case Mineral::jade: {
        const double m = fbm3(x * 1.8, y * 1.8, z * 1.8, seed, 5);
        s.albedo = mix(f.palette[0], f.palette[1], smoothstep(-.3, .6, m));
        s.albedo = mix(s.albedo, f.palette[2], smoothstep(.2, .7, -m) * .6);
        s.clarity = static_cast<float>(f.clarity * (.7 + .5 * smoothstep(-.3, .6, m)));
        // Aventurine: a green stone full of mica that glitters.
        if (f.flecks > 0)
            s.fleck = static_cast<float>(specks(x, y, z, 55, .3, .35 * f.flecks, seed + 17));
        break;
    }
    case Mineral::labradorite: {
        const double m = fbm3(x * 2.5, y * 2.5, z * 2.5, seed, 4);
        s.albedo = mix(f.palette[0], f.palette[1], smoothstep(-.4, .5, m));
        s.albedo = mix(s.albedo, f.palette[2], smoothstep(.2, .6, -m) * .5);
        const double patch = fbm3(x * 1.4, y * 1.4, z * 1.4, seed + 9, 4);
        const double streak = .5 + .5 * std::sin((along(f, x, y, z) * 9 + 2 * patch) * pi);
        s.sheen = static_cast<float>(smoothstep(-.05, .35, patch) * (.55 + .45 * streak));
        break;
    }
    case Mineral::jasper: {
        const double wobble = f.warp * fbm3(x * 1.5, y * 1.5, z * 1.5, seed, 5);
        const double t = (along(f, x, y, z) + wobble) * f.frequency;
        s.albedo = banded(f, t * .3, 4, .6);
        s.albedo = mix(s.albedo, f.palette[2], smoothstep(.3, .7, fbm3(x * 4, y * 4, z * 4, seed + 3, 3)) * .5);
        break;
    }
    case Mineral::amber: {
        // The flows: curved sheets, each its own shade, a thin darker skin between.
        const double m = fbm3(x * 1.2, y * 1.2, z * 1.2, seed, 4);
        const double t = along(f, x, y, z) * f.frequency + f.warp * m * 2;
        const double flow = fraction(t);
        const double shade = hash_signed(static_cast<int>(std::floor(t)), 3, 5, seed) * .5 + .5;
        s.albedo = mix(f.palette[0], f.palette[1], shade * .7);
        const double skin = 1 - smoothstep(0, .06, flow);
        s.albedo = mix(s.albedo, f.palette[2], skin * .45);
        double clear = f.clarity * (1 - .4 * skin);
        if (f.variant == 3) {
            // Cloudy amber: clouds of tiny bubbles, the clear resin swirled through.
            const double cloud = smoothstep(-.2, .4, fbm3(x * 2.2, y * 2.2, z * 2.2, seed + 9, 4));
            s.albedo = mix(s.albedo, f.palette[3], cloud * .3);
            clear = f.clarity * (1 - .6 * cloud) + .3 * (1 - cloud);
        }
        // Bubbles bright, bits of bark dark.
        const double bubbles = specks(x, y, z, 16, .2, .12, seed + 31);
        const double bark = specks(x, y, z, 9, .22, .08, seed + 37);
        s.albedo = mix(s.albedo, f.palette[3], bubbles * .6);
        s.albedo = mix(s.albedo, f.palette[4], bark * .9);
        s.clarity = static_cast<float>(clear * (1 - .85 * bark));
        // Sun spangles: little crazed discs that glitter.
        if (f.flecks > .4)
            s.fleck = static_cast<float>(specks(x, y, z, 5, .28, .25 * f.flecks, seed + 41));
        break;
    }
    case Mineral::lava: {
        const double m = fbm3(x * 2.2, y * 2.2, z * 2.2, seed, 4);
        s.albedo = mix(f.palette[0], f.palette[1], smoothstep(-.3, .5, m));
        if (f.variant == 1) {
            // Scoria rusts in patches.
            s.albedo = mix(s.albedo, f.palette[4], smoothstep(.1, .5, fbm3(x * 3, y * 3, z * 3, seed + 3, 3)) * .6);
        }
        if (f.variant == 2) {
            // Pale crystals: small, angular, scattered.
            const double crystals = specks(x, y, z, 18, .3, .22, seed + 5);
            s.albedo = mix(s.albedo, f.palette[3], smoothstep(.4, .7, crystals) * .8);
        }
        if (f.variant == 3) {
            // Ropy flow lines folded through the glass.
            const double t = along(f, x, y, z) * f.frequency * .6 + f.warp * 1.5 * m;
            const double line = std::pow(.5 + .5 * std::sin(t * 2 * pi), 8);
            s.albedo = mix(s.albedo, f.palette[1], line * .7);
        } else {
            // Gas pores, large and small: black holes with a paler rim.
            const double big = specks(x, y, z, 6, .42, .45, seed + 11);
            const double small = specks(x, y, z, 15, .38, .5, seed + 13);
            const double pore = std::max(big, small);
            s.albedo = mix(s.albedo, scaled(f.palette[1], 1.4), smoothstep(.05, .35, pore) * .5);
            s.albedo = mix(s.albedo, f.palette[2], smoothstep(.35, .6, pore));
        }
        break;
    }
    case Mineral::dripstone: {
        // Layers: fine laminae within broader bands; a stalactite's run round its axis.
        const bool rings = f.variant == 1;
        const double base = rings ? from_axis(f, x, y, z) : along(f, x, y, z);
        const double wave = f.warp * (fbm3(x * 1.4, y * 1.4, z * 1.4, seed, 4) + .3 * std::sin(x * 7 + y * 5 + z * 3));
        const double t = (base + wave * .3) * f.frequency;
        const double band = hash_signed(static_cast<int>(std::floor(t * .25)), 7, 9, seed) * .5 + .5;
        s.albedo = mix(f.palette[0], f.palette[4], band);
        const double dark = hash_signed(static_cast<int>(std::floor(t * .25)), 8, 9, seed) > .55 ? 1.0 : 0.0;
        s.albedo = mix(s.albedo, f.palette[1], dark * .7);
        const double lamina = .5 + .5 * std::sin(t * 2 * pi);
        s.albedo = mix(s.albedo, f.palette[2], std::pow(lamina, 6) * .55);
        s.albedo = mix(s.albedo, f.palette[3], std::pow(1 - lamina, 10) * .35);
        s.clarity = static_cast<float>(f.clarity * (.6 + .6 * (1 - band)) * (1 - .5 * dark));
        break;
    }
    case Mineral::blue_lace: {
        const double wobble = f.warp * fbm3(x * 2.2, y * 2.2, z * 2.2, seed, 4);
        const double t = (along(f, x, y, z) + wobble + .08 * std::sin(x * 14 + y * 9)) * f.frequency;
        s.albedo = banded(f, t * .5, 4, .5);
        const double light = (s.albedo.r + s.albedo.g + s.albedo.b) / 3;
        s.clarity = static_cast<float>(f.clarity * (1.3 - light));
        break;
    }
    case Mineral::candy: {
        // Stripes pulled along the sugar and twisted: opaque white, a second colour, and
        // clear sugar between.
        const double a = along(f, x, y, z);
        const double twist = from_axis(f, x, y, z) * 2.2 + f.warp * fbm3(x * 1.3, y * 1.3, z * 1.3, seed, 3);
        const double t = fraction((a * f.frequency + twist) * .5);
        s.albedo = f.palette[0];
        s.clarity = static_cast<float>(f.clarity);
        if (t < .16) {
            s.albedo = f.palette[1];
            s.clarity = .05F;
        } else if (t > .5 && t < .58) {
            s.albedo = f.palette[2];
            s.clarity = .25F;
        }
        // Rolled in sanding sugar: frosted, glittering.
        if (f.flecks > 0) {
            s.clarity *= .55F;
            s.fleck = static_cast<float>(specks(x, y, z, 60, .32, .45, seed + 19));
        }
        break;
    }
    case Mineral::nacre: {
        const double swirl = fbm3(x * 1.6, y * 1.6, z * 1.6, seed, 4);
        const double t = along(f, x, y, z) * f.frequency + f.warp * swirl * 3;
        const double line = .5 + .5 * std::sin(t * 2 * pi);
        s.albedo = mix(f.palette[0], f.palette[1], line * .45);
        s.albedo = mix(s.albedo, f.palette[2], std::max(0.0, fbm3(x * 3, y * 3, z * 3, seed + 2, 3)) * .5);
        if (f.variant == 3)
            s.albedo = mix(s.albedo, f.palette[2], smoothstep(-.1, .4, swirl) * .6);
        // The sheen's colour follows the growth lines, so the rainbow runs in bands.
        s.sheen = static_cast<float>(std::clamp(.5 + .3 * std::sin(t * .9) + .25 * fbm3(x * 3, y * 3, z * 3, seed + 5, 2), 0.0, 1.0));
        break;
    }
    case Mineral::cone: {
        // Tents: little triangles of cream in brown nets, and zigzag lines.
        const double a = along(f, x, y, z) * f.frequency;
        const double across = from_axis(f, x, y, z) * f.frequency * 1.4;
        const double zig = std::abs(fraction(across + .35 * std::abs(fraction(a * 2) - .5)) - .5);
        const double blotch = fbm3(x * 3, y * 3, z * 3, seed, 4);
        s.albedo = f.palette[0];
        s.albedo = mix(s.albedo, f.palette[1], smoothstep(.36, .44, zig) * smoothstep(-.1, .2, blotch));
        s.albedo = mix(s.albedo, f.palette[2], std::max(0.0, -blotch) * .6);
        break;
    }
    case Mineral::seaglass: {
        const double frost = fbm3(x * 5, y * 5, z * 5, seed, 3);
        s.albedo = mix(f.palette[0], f.palette[2], .25 + .15 * frost);
        s.clarity = static_cast<float>(f.clarity * (.85 + .15 * frost));
        break;
    }
    }
    return s;
}

void print_mineral(const Finish& given, const Shape& shape, int side, MineralPicture& out) {
    // Fibrous minerals are cut along their grain: it runs the stone's length.
    Finish finish = given;
    if (finish.mineral == Mineral::walnut || finish.mineral == Mineral::petrified) {
        double grain[3] = {1, 0, 0};
        shape.grain_axis(grain);
        const double lean = .08 * finish.axis[2];
        finish.axis[0] = grain[0] + lean * grain[2];
        finish.axis[1] = grain[1] + lean;
        finish.axis[2] = grain[2] + lean;
        const double length = std::sqrt(finish.axis[0] * finish.axis[0] + finish.axis[1] * finish.axis[1] + finish.axis[2] * finish.axis[2]);
        for (double& v : finish.axis)
            v /= length;
        // Wood's rings centre on its pith; plain-sawn walnut's lie far below the face.
        if (finish.mineral != Mineral::walnut)
            finish.centre[2] = 0;
        if (shape.style() == Style::wood)
            shape.grain_centre(finish.centre);
    }
    out.side = side;
    const std::size_t per_face = static_cast<std::size_t>(side * side);
    out.colour.assign(per_face * 2, 0U);
    out.extra.assign(per_face * 2, 0U);
    for (int face = 0; face < 2; ++face) {
        for (int row = 0; row < side; ++row) {
            const double y = (1 - (row + .5) / side * 2) * reach;
            for (int column = 0; column < side; ++column) {
                const double x = ((column + .5) / side * 2 - 1) * reach;
                double h = 0;
                const bool inside = shape.height(face, x, y, h);
                const double z = inside ? (face == 0 ? h : -h) : 0.0;
                MineralSample s = sample_mineral(finish, x, y, z);
                const double grown = finish.mineral == Mineral::agate ? shape.bands(x, y, z) : -1.0;
                if (grown >= 0) {
                    // An agate nodule: the colour runs with the bands the stone grew in: broad
                    // bands of the agate's colours parted by thin white lines, and clear
                    // quartz filling the middle.
                    const double band = std::floor(grown);
                    const double within = grown - band;
                    const int which = static_cast<int>(hash_cell(static_cast<int>(band), 1, 2, finish.seed) % 3U);
                    const Linear colours[3] = {finish.palette[0], finish.palette[2], finish.palette[4]};
                    s.albedo = colours[which];
                    s.clarity = static_cast<float>(finish.clarity * (which == 1 ? .5 : 1.0));
                    if (within < .14) {
                        s.albedo = mix(s.albedo, finish.palette[3], 1 - within / .14);
                        s.clarity = static_cast<float>(finish.clarity * .3);
                    }
                    if (grown < 1.6) {
                        const double core = smoothstep(1.6, 1.0, grown);
                        s.albedo = mix(s.albedo, Linear{.85F, .86F, .88F}, core);
                        s.clarity = static_cast<float>(std::max<double>(s.clarity, .85 * core));
                    }
                }
                const std::uint32_t r = static_cast<std::uint32_t>(std::lround(srgb_from_linear(s.albedo.r) * 255));
                const std::uint32_t g = static_cast<std::uint32_t>(std::lround(srgb_from_linear(s.albedo.g) * 255));
                const std::uint32_t b = static_cast<std::uint32_t>(std::lround(srgb_from_linear(s.albedo.b) * 255));
                const std::uint32_t c = static_cast<std::uint32_t>(std::lround(std::clamp(s.clarity, 0.0F, 1.0F) * 255));
                const std::size_t at = static_cast<std::size_t>(face) * per_face + static_cast<std::size_t>(row * side + column);
                out.colour[at] = r | (g << 8) | (b << 16) | (c << 24);
                const std::uint32_t fleck = static_cast<std::uint32_t>(std::lround(std::clamp(s.fleck, 0.0F, 1.0F) * 255));
                const std::uint32_t sheen = static_cast<std::uint32_t>(std::lround(std::clamp(s.sheen, 0.0F, 1.0F) * 255));
                out.extra[at] = static_cast<std::uint16_t>(fleck | (sheen << 8));
            }
        }
    }
}

}  // namespace ts
