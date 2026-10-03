#include "stones.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace zc {

namespace {

constexpr int kSize = 128;

std::uint32_t hash2(std::int32_t x, std::int32_t y, std::uint32_t seed) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

double unit_hash(std::int32_t x, std::int32_t y, std::uint32_t seed) {
    return static_cast<double>(hash2(x, y, seed) & 0xFFFFFFu) / 16777216.0;
}

std::int32_t wrap(std::int32_t v, std::int32_t period) {
    return ((v % period) + period) % period;
}

// value noise on a lattice that repeats every `period` cells: about [-1, 1]
double tile_value(double x, double y, std::int32_t period, std::uint32_t seed) {
    const double fx = std::floor(x);
    const double fy = std::floor(y);
    const std::int32_t ix = static_cast<std::int32_t>(fx);
    const std::int32_t iy = static_cast<std::int32_t>(fy);
    double u = x - fx;
    double v = y - fy;
    u = u * u * (3 - 2 * u);
    v = v * v * (3 - 2 * v);
    const double a = unit_hash(wrap(ix, period), wrap(iy, period), seed);
    const double b = unit_hash(wrap(ix + 1, period), wrap(iy, period), seed);
    const double c = unit_hash(wrap(ix, period), wrap(iy + 1, period), seed);
    const double d = unit_hash(wrap(ix + 1, period), wrap(iy + 1, period), seed);
    const double top = a + (b - a) * u;
    const double bottom = c + (d - c) * u;
    return 2 * (top + (bottom - top) * v) - 1;
}

// octaves of it over the tile (u, v in [0, 1)), seamless
double tile_fbm(double u, double v, std::int32_t base, int octaves, std::uint32_t seed) {
    double sum = 0;
    double amplitude = 1;
    double total = 0;
    std::int32_t period = base;
    for (int k = 0; k < octaves; k += 1) {
        sum += amplitude * tile_value(u * period, v * period, period, seed + static_cast<std::uint32_t>(k) * 101u);
        total += amplitude;
        amplitude *= 0.5;
        period *= 2;
    }
    return sum / total;
}

// Cellular noise over the tile: the nearest and second-nearest feature
// points of a jittered grid of `cells` x `cells` (seamless), and which cell
// is nearest. Distances in cell widths.
struct Cellular {
    double d1 = 9, d2 = 9;
    std::uint32_t id = 0;
};

Cellular cellular(double u, double v, std::int32_t cells, std::uint32_t seed) {
    const double x = u * cells;
    const double y = v * cells;
    const std::int32_t ix = static_cast<std::int32_t>(std::floor(x));
    const std::int32_t iy = static_cast<std::int32_t>(std::floor(y));
    Cellular out;
    for (std::int32_t dy = -1; dy <= 1; dy += 1) {
        for (std::int32_t dx = -1; dx <= 1; dx += 1) {
            const std::int32_t cx = ix + dx;
            const std::int32_t cy = iy + dy;
            const std::int32_t wx = wrap(cx, cells);
            const std::int32_t wy = wrap(cy, cells);
            const double px = cx + 0.1 + 0.8 * unit_hash(wx, wy, seed);
            const double py = cy + 0.1 + 0.8 * unit_hash(wx, wy, seed + 17u);
            const double d = std::sqrt((px - x) * (px - x) + (py - y) * (py - y));
            if (d < out.d1) {
                out.d2 = out.d1;
                out.d1 = d;
                out.id = hash2(wx, wy, seed + 31u);
            } else if (d < out.d2) {
                out.d2 = d;
            }
        }
    }
    return out;
}

struct Rgb {
    double r = 0, g = 0, b = 0;
};

Rgb rgb(double r, double g, double b) {
    Rgb c;
    c.r = r;
    c.g = g;
    c.b = b;
    return c;
}

Rgb scale(Rgb c, double k) {
    return rgb(c.r * k, c.g * k, c.b * k);
}

Rgb blend(Rgb a, Rgb b, double t) {
    return rgb(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t);
}

double id_unit(std::uint32_t id, std::uint32_t salt) {
    std::uint32_t h = id ^ (salt * 2654435761u);
    h = (h ^ (h >> 15)) * 2246822519u;
    h = h ^ (h >> 13);
    return static_cast<double>(h & 0xFFFFFFu) / 16777216.0;
}

// A crystalline rock: grains as cells, each a mineral by its share, a little
// variation within and between grains, dark seams between them.
Rgb crystals(double u, double v, int x, int y, std::int32_t cells, std::uint32_t seed, const Rgb* minerals, const double* shares, int count) {
    const Cellular c = cellular(u, v, cells, seed);
    const double pick = id_unit(c.id, 1);
    int mineral = count - 1;
    double running = 0;
    for (int k = 0; k < count; k += 1) {
        running += shares[k];
        if (pick < running) {
            mineral = k;
            break;
        }
    }
    Rgb colour = scale(minerals[mineral], 0.9 + 0.2 * id_unit(c.id, 2));
    colour = scale(colour, 1 + 0.05 * (2 * unit_hash(x, y, seed + 7u) - 1) + 0.04 * tile_value(u * 64, v * 64, 64, seed + 9u));
    const double seam = c.d2 - c.d1;
    if (seam < 0.08) {
        colour = scale(colour, 0.82 + 0.18 * seam / 0.08);
    }
    return colour;
}

Rgb texel(Stone stone, int x, int y) {
    const double u = (x + 0.5) / kSize;
    const double v = (y + 0.5) / kSize;
    const double speck = unit_hash(x, y, 4001u + static_cast<std::uint32_t>(stone));
    const double fine = 2 * unit_hash(x, y, 5003u + static_cast<std::uint32_t>(stone)) - 1;
    if (stone == Stone::pink_granite) {
        const Rgb minerals[4] = {rgb(0.82, 0.57, 0.49), rgb(0.72, 0.71, 0.70), rgb(0.91, 0.89, 0.85), rgb(0.16, 0.15, 0.14)};
        const double shares[4] = {0.48, 0.28, 0.16, 0.08};
        return crystals(u, v, x, y, 9, 11u, minerals, shares, 4);
    }
    if (stone == Stone::grey_granite) {
        const Rgb minerals[4] = {rgb(0.88, 0.87, 0.84), rgb(0.64, 0.64, 0.65), rgb(0.80, 0.76, 0.68), rgb(0.15, 0.15, 0.16)};
        const double shares[4] = {0.42, 0.30, 0.14, 0.14};
        return crystals(u, v, x, y, 12, 12u, minerals, shares, 4);
    }
    if (stone == Stone::sandstone) {
        Rgb c = rgb(0.77, 0.64, 0.48);
        const double iron = tile_fbm(u, v, 3, 4, 13u);
        c = blend(c, rgb(0.68, 0.48, 0.34), std::max(0.0, iron) * 0.7);
        const double lamina = std::sin(2 * 3.14159265 * (v * 6 + 0.25 * tile_fbm(u, v, 2, 3, 14u)));
        c = scale(c, 1 + 0.035 * lamina + 0.07 * fine);
        if (speck < 0.05) {
            c = scale(c, 0.62);
        } else if (speck > 0.94) {
            c = scale(c, 1.15);
        }
        return c;
    }
    if (stone == Stone::slate) {
        Rgb c = rgb(0.48, 0.51, 0.55);
        const double streak = tile_value(u * 4, v * 48, 4, 15u) + 0.5 * tile_value(u * 8, v * 96, 8, 16u);
        c = scale(c, 1 + 0.05 * streak + 0.03 * fine + 0.05 * tile_fbm(u, v, 2, 3, 17u));
        const double rust = tile_fbm(u, v, 4, 3, 18u);
        if (rust > 0.42) {
            c = blend(c, rgb(0.48, 0.33, 0.22), std::min(1.0, (rust - 0.42) * 4) * 0.7);
        }
        return c;
    }
    if (stone == Stone::basalt) {
        Rgb c = rgb(0.41, 0.41, 0.42);
        c = scale(c, 1 + 0.06 * fine + 0.05 * tile_fbm(u, v, 3, 3, 19u));
        // gas bubbles frozen in: small pits with a pale rim
        const Cellular cell = cellular(u, v, 19, 20u);
        const double size = 0.12 + 0.2 * id_unit(cell.id, 4);
        if (id_unit(cell.id, 5) < 0.35 && cell.d1 < size) {
            const double rim = cell.d1 / size;
            c = rim > 0.75 ? scale(c, 1.35) : scale(c, 0.45);
        }
        if (speck > 0.985) {
            c = rgb(0.36, 0.40, 0.22);   // a grain of olivine
        }
        return c;
    }
    if (stone == Stone::quartzite) {
        Rgb c = rgb(0.86, 0.82, 0.76);
        const double streak = tile_fbm(u * 1.0, v, 2, 4, 21u);
        c = blend(c, rgb(0.84, 0.69, 0.62), std::max(0.0, streak) * 0.55);
        c = scale(c, 1 + 0.06 * fine);
        if (speck > 0.97) {
            c = rgb(0.98, 0.97, 0.95);   // sugary glints
        } else if (speck < 0.01) {
            c = scale(c, 0.55);
        }
        return c;
    }
    if (stone == Stone::limestone) {
        Rgb c = rgb(0.79, 0.78, 0.73);
        c = scale(c, 1 + 0.07 * tile_fbm(u, v, 3, 4, 23u) + 0.03 * fine);
        const Cellular pit = cellular(u, v, 23, 24u);
        if (id_unit(pit.id, 6) < 0.25 && pit.d1 < 0.13) {
            c = scale(c, 0.62);
        }
        if (speck < 0.03) {
            c = scale(c, 0.55);
        } else if (speck > 0.985) {
            c = rgb(0.95, 0.94, 0.90);
        }
        return c;
    }
    // greywacke: dark muddy sandstone with angular stony fragments in it
    Rgb c = rgb(0.50, 0.52, 0.48);
    c = scale(c, 1 + 0.08 * fine + 0.05 * tile_fbm(u, v, 3, 3, 25u));
    const Cellular clast = cellular(u, v, 11, 26u);
    if (id_unit(clast.id, 7) < 0.35) {
        const double which = id_unit(clast.id, 8);
        const Rgb tones[3] = {rgb(0.60, 0.61, 0.58), rgb(0.42, 0.43, 0.41), rgb(0.57, 0.52, 0.45)};
        c = scale(tones[static_cast<int>(which * 3) % 3], 1 + 0.07 * fine);
    }
    if (clast.d2 - clast.d1 < 0.04) {
        c = scale(c, 0.9);
    }
    return c;
}

struct Library {
    Tex textures[kStoneCount];
    Col means[kStoneCount];
    std::once_flag initialized[kStoneCount];
};

Library& library() {
    static Library shelf;
    return shelf;
}

void build(int k) {
    Library& shelf = library();
    Tex& t = shelf.textures[k];
    t.make(kSize, kSize);
    double sum_r = 0;
    double sum_g = 0;
    double sum_b = 0;
    for (int y = 0; y < kSize; y += 1) {
        for (int x = 0; x < kSize; x += 1) {
            const Rgb c = texel(static_cast<Stone>(k), x, y);
            const double r = std::min(1.0, std::max(0.0, c.r));
            const double g = std::min(1.0, std::max(0.0, c.g));
            const double b = std::min(1.0, std::max(0.0, c.b));
            sum_r += r;
            sum_g += g;
            sum_b += b;
            t.at(x, y) = 0xFF000000u | (static_cast<std::uint32_t>(std::lround(r * 255)) << 16) |
                         (static_cast<std::uint32_t>(std::lround(g * 255)) << 8) | static_cast<std::uint32_t>(std::lround(b * 255));
        }
    }
    t.build_mips();
    const double n = static_cast<double>(kSize) * kSize;
    shelf.means[k] = Col{static_cast<float>(sum_r / n), static_cast<float>(sum_g / n), static_cast<float>(sum_b / n), 1};
}

}  // namespace

// A test pattern for checking orientation: on a pale ground, a big red arrow
// pointing along the texture's s axis, a blue bar across its tail and a black
// dot to its left, so no two directions look alike.
const Tex& arrow_texture() {
    static Tex t;
    if (t.w == 0) {
        t.make(kSize, kSize);
        for (int y = 0; y < kSize; y += 1) {
            for (int x = 0; x < kSize; x += 1) {
                const double u = (x + 0.5) / kSize;
                const double v = (y + 0.5) / kSize;
                Rgb c = rgb(0.95, 0.90, 0.70);
                const bool shaft = u > 0.15 && u < 0.65 && std::abs(v - 0.5) < 0.07;
                const bool head = u >= 0.65 && u < 0.9 && std::abs(v - 0.5) < 0.25 * (0.9 - u) / 0.25;
                const bool bar = u > 0.1 && u < 0.17 && std::abs(v - 0.5) < 0.22;
                const bool dot = (u - 0.4) * (u - 0.4) + (v - 0.8) * (v - 0.8) < 0.006;
                if (shaft || head) {
                    c = rgb(0.85, 0.08, 0.06);
                } else if (bar) {
                    c = rgb(0.10, 0.25, 0.85);
                } else if (dot) {
                    c = rgb(0.05, 0.05, 0.05);
                }
                if (x == 0 || y == 0) {
                    c = rgb(0.3, 0.3, 0.3);
                }
                t.at(x, y) = 0xFF000000u | (static_cast<std::uint32_t>(c.r * 255) << 16) | (static_cast<std::uint32_t>(c.g * 255) << 8) |
                             static_cast<std::uint32_t>(c.b * 255);
            }
        }
        t.build_mips();
    }
    return t;
}

const Tex& stone_texture(Stone stone) {
    const int k = static_cast<int>(stone);
    std::call_once(library().initialized[k], build, k);
    return library().textures[k];
}

Col stone_mean(Stone stone) {
    const int k = static_cast<int>(stone);
    std::call_once(library().initialized[k], build, k);
    return library().means[k];
}

const char* stone_name(Stone stone) {
    const char* names[kStoneCount] = {"pink granite", "grey granite", "sandstone", "slate", "basalt", "quartzite", "limestone", "greywacke"};
    return names[static_cast<int>(stone)];
}

}  // namespace zc
