#include "country.hpp"

#include "rocks.hpp"
#include "run.hpp"
#include "shapes.hpp"
#include "terrain.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace zc {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kReach = 18.0;     // trees and plants stand out to here along the valley
constexpr int kCloudSize = 256;     // the cloud cover: kCloudSize x kCloudSize, row-major
constexpr int kCellWidth = 64;      // the plant atlas: eight cells of 64 x 128 texels side by side
constexpr int kCellHeight = 128;
constexpr int kCells = 8;

enum class Plant { grass_tuft = 0, dry_tuft = 1, cattails = 2, rushes = 3, daisies = 4, tansy = 5, loosestrife = 6, fern = 7 };

enum class Leaf { oak = 0, alder = 1, birch = 2, willow = 3 };

std::uint32_t pack(double r, double g, double b, double a) {
    const int ri = static_cast<int>(std::lround(std::clamp(r, 0.0, 1.0) * 255));
    const int gi = static_cast<int>(std::lround(std::clamp(g, 0.0, 1.0) * 255));
    const int bi = static_cast<int>(std::lround(std::clamp(b, 0.0, 1.0) * 255));
    const int ai = static_cast<int>(std::lround(std::clamp(a, 0.0, 1.0) * 255));
    return (static_cast<std::uint32_t>(ai) << 24) | (static_cast<std::uint32_t>(ri) << 16) | (static_cast<std::uint32_t>(gi) << 8) |
           static_cast<std::uint32_t>(bi);
}

Col scaled(Col c, double k) {
    const float f = static_cast<float>(k);
    return Col{c.r * f, c.g * f, c.b * f, c.a};
}

// Smooth value noise in [0, 1] that repeats every `period_x` by `period_y` units.
double tile_noise(double x, double y, int period_x, int period_y, std::uint32_t seed) {
    const double fx = std::floor(x);
    const double fy = std::floor(y);
    const int ix = ((static_cast<int>(fx) % period_x) + period_x) % period_x;
    const int iy = ((static_cast<int>(fy) % period_y) + period_y) % period_y;
    const int jx = (ix + 1) % period_x;
    const int jy = (iy + 1) % period_y;
    const double tx = x - fx;
    const double ty = y - fy;
    const double u = tx * tx * (3 - 2 * tx);
    const double v = ty * ty * (3 - 2 * ty);
    const double a = hash_unit(ix, iy, seed);
    const double b = hash_unit(jx, iy, seed);
    const double c = hash_unit(ix, jy, seed);
    const double d = hash_unit(jx, jy, seed);
    const double top = a + (b - a) * u;
    const double bottom = c + (d - c) * u;
    return top + (bottom - top) * v;
}

// ---------------------------------------------------------------- textures

// Leaves: a mat of small overlapping leaves in greys, the crown's vertex colours giving
// them their green; the gaps between them dark.
void make_leaves(Tex& t) {
    t.make(128, 128);
    for (int y = 0; y < 128; y += 1) {
        for (int x = 0; x < 128; x += 1) {
            const double v = 0.26 + 0.12 * tile_noise(x / 16.0, y / 16.0, 8, 8, 501u);
            t.at(x, y) = pack(v, v, v, 1);
        }
    }
    for (int i = 0; i < 1500; i += 1) {
        const double cx = 128 * hash_unit(i, 1, 503u);
        const double cy = 128 * hash_unit(i, 2, 503u);
        const double angle = 2 * kPi * hash_unit(i, 3, 503u);
        const double length = 3.0 + 3.2 * hash_unit(i, 4, 503u);
        const double width = 1.5 + 1.0 * hash_unit(i, 5, 503u);
        // later leaves lie on top and catch more light
        const double tone = 0.64 + 0.3 * hash_unit(i, 6, 503u) + 0.14 * i / 1500.0;
        const double warm = 0.94 + 0.12 * hash_unit(i, 7, 503u);
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        for (int dy = -6; dy <= 6; dy += 1) {
            for (int dx = -6; dx <= 6; dx += 1) {
                const double along = dx * c + dy * s;
                const double across = -dx * s + dy * c;
                const double inside = (along / length) * (along / length) + (across / width) * (across / width);
                if (inside > 1) {
                    continue;
                }
                // the leaf's midrib a little lighter, its edge a little darker
                const double k = tone * (1.02 - 0.16 * inside);
                const int px = static_cast<int>(std::floor(cx)) + dx;
                const int py = static_cast<int>(std::floor(cy)) + dy;
                t.at(px, py) = pack(k * warm, k, k * (2 - warm) * 0.96, 1);
            }
        }
    }
    t.build_mips();
}

// Bark: ridges and furrows running up the trunk (t), greys warmed by the vertex colour.
void make_bark(Tex& t) {
    t.make(64, 64);
    for (int y = 0; y < 64; y += 1) {
        for (int x = 0; x < 64; x += 1) {
            const double ridge = tile_noise(x / 5.0 + 0.6 * tile_noise(x / 8.0, y / 8.0, 8, 8, 511u), y / 16.0, 13, 4, 513u);
            const double furrow = smoothstep(0.04, 0.22, std::abs(ridge - 0.5));
            const double grain = hash_unit(x, y, 517u);
            const double v = 0.48 + 0.42 * furrow + 0.08 * (grain - 0.5);
            t.at(x, y) = pack(v, v * 0.97, v * 0.93, 1);
        }
    }
    t.build_mips();
}

// ---------------------------------------------------------------- the plant atlas

// A round dab of paint in a cell of the atlas; (x, y) in the cell's texels.
void dab(Tex& t, int cell, double x, double y, double radius, Col colour) {
    const int x0 = static_cast<int>(std::floor(x - radius));
    const int x1 = static_cast<int>(std::ceil(x + radius));
    const int y0 = static_cast<int>(std::floor(y - radius));
    const int y1 = static_cast<int>(std::ceil(y + radius));
    for (int py = y0; py <= y1; py += 1) {
        for (int px = x0; px <= x1; px += 1) {
            if (px < 1 || px >= kCellWidth - 1 || py < 1 || py >= kCellHeight) {
                continue;
            }
            const double dx = px + 0.5 - x;
            const double dy = py + 0.5 - y;
            if (dx * dx + dy * dy > radius * radius) {
                continue;
            }
            t.at(cell * kCellWidth + px, py) = pack(colour.r, colour.g, colour.b, 1);
        }
    }
}

// A blade from (x0, y0) to its tip at (x1, y1), bowed sideways by `bow` texels, `width`
// texels across at the foot and coming to a point; coloured from `foot` to `tip`.
void blade(Tex& t, int cell, double x0, double y0, double x1, double y1, double bow, double width, Col foot, Col tip) {
    const double length = std::hypot(x1 - x0, y1 - y0);
    if (length < 1) {
        return;
    }
    const double nx = -(y1 - y0) / length;
    const double ny = (x1 - x0) / length;
    const double cx = 0.5 * (x0 + x1) + nx * bow;
    const double cy = 0.5 * (y0 + y1) + ny * bow;
    const int steps = std::max(4, static_cast<int>(length * 1.6));
    for (int k = 0; k <= steps; k += 1) {
        const double u = static_cast<double>(k) / steps;
        const double a = (1 - u) * (1 - u);
        const double b = 2 * u * (1 - u);
        const double c = u * u;
        const double px = a * x0 + b * cx + c * x1;
        const double py = a * y0 + b * cy + c * y1;
        const double radius = 0.5 * width * std::pow(1 - u, 0.7) + 0.45;
        dab(t, cell, px, py, radius, mix(foot, tip, static_cast<float>(u)));
    }
}

// The point a blade from (x0, y0) to (x1, y1) bowed by `bow` reaches at `u` along it.
void blade_point(double x0, double y0, double x1, double y1, double bow, double u, double& out_x, double& out_y) {
    const double length = std::max(1.0, std::hypot(x1 - x0, y1 - y0));
    const double cx = 0.5 * (x0 + x1) - (y1 - y0) / length * bow;
    const double cy = 0.5 * (y0 + y1) + (x1 - x0) / length * bow;
    const double a = (1 - u) * (1 - u);
    const double b = 2 * u * (1 - u);
    const double c = u * u;
    out_x = a * x0 + b * cx + c * x1;
    out_y = a * y0 + b * cy + c * y1;
}

// A tuft of grass blades fanning from the cell's foot; `dry_share` of them straw.
void paint_tuft(Tex& t, int cell, std::uint32_t seed, int blades, double tallest, Col foot, Col tip, Col straw, double dry_share) {
    for (int i = 0; i < blades; i += 1) {
        const double bx = 32 + 12 * (hash_unit(i, 1, seed) - 0.5);
        const double height = tallest * (0.4 + 0.6 * std::pow(hash_unit(i, 2, seed), 0.7));
        const double lean = (hash_unit(i, 3, seed) - 0.5) * 1.3;
        const double x1 = bx + lean * height * 0.5;
        const double y1 = 127 - height * (1 - 0.18 * std::abs(lean));
        const double bow = (hash_unit(i, 4, seed) - 0.5) * 12;
        const double width = 2.2 + 1.8 * hash_unit(i, 5, seed);
        const bool dry = hash_unit(i, 6, seed) < dry_share;
        const double tone = 0.85 + 0.3 * hash_unit(i, 7, seed);
        blade(t, cell, bx, 127.5, x1, y1, bow, width, scaled(foot, tone), scaled(dry ? straw : tip, tone));
    }
}

void paint_cattails(Tex& t, int cell) {
    const Col foot{0.24f, 0.34f, 0.14f, 1};
    const Col tip{0.56f, 0.62f, 0.30f, 1};
    paint_tuft(t, cell, 821u, 18, 118, foot, tip, Col{0.70f, 0.64f, 0.38f, 1}, 0.15);
    for (int i = 0; i < 3; i += 1) {
        const double bx = 26 + 6 * i + 3 * hash_unit(i, 1, 823u);
        const double top = 8 + 14 * hash_unit(i, 2, 823u);
        const double lean = 4 * (hash_unit(i, 3, 823u) - 0.5);
        blade(t, cell, bx, 127.5, bx + lean, top, 1.5, 1.7, Col{0.34f, 0.42f, 0.20f, 1}, Col{0.48f, 0.50f, 0.26f, 1});
        // the brown head below the spike
        for (int k = 0; k < 16; k += 1) {
            const double u = 0.07 + 0.016 * k;
            const double hx = bx + lean * (1 - u) * 0.9;
            const double hy = top + (127.5 - top) * u;
            const double shade = 0.85 + 0.25 * (k % 3 == 0 ? 1 : 0) * hash_unit(i, k, 827u);
            dab(t, cell, hx, hy, 1.8, scaled(Col{0.40f, 0.25f, 0.13f, 1}, shade));
        }
    }
}

void paint_rushes(Tex& t, int cell) {
    for (int i = 0; i < 34; i += 1) {
        const double bx = 32 + 18 * (hash_unit(i, 1, 831u) - 0.5);
        const double height = 70 + 50 * hash_unit(i, 2, 831u);
        const double lean = (hash_unit(i, 3, 831u) - 0.5) * 0.35;
        const double tone = 0.8 + 0.35 * hash_unit(i, 4, 831u);
        blade(t, cell, bx, 127.5, bx + lean * height, 127 - height, 2 * (hash_unit(i, 5, 831u) - 0.5), 1.5,
              scaled(Col{0.18f, 0.30f, 0.12f, 1}, tone), scaled(Col{0.36f, 0.50f, 0.20f, 1}, tone));
        // now and then a little brown flower cluster out from the side near the top
        if (hash_unit(i, 6, 831u) < 0.3) {
            const double fx = bx + lean * height * 0.8 + 1.5;
            const double fy = 127 - height * 0.8;
            dab(t, cell, fx, fy, 1.6, Col{0.46f, 0.34f, 0.18f, 1});
            dab(t, cell, fx + 1.2, fy - 1.2, 1.1, Col{0.52f, 0.40f, 0.22f, 1});
        }
    }
}

// Stems with leaves at their foot; `head` paints each flower at a stem's top.
void paint_stems(Tex& t, int cell, std::uint32_t seed, int stems, double low, double high, Col stem) {
    paint_tuft(t, cell, seed + 7u, 10, 34, Col{0.22f, 0.36f, 0.14f, 1}, Col{0.40f, 0.56f, 0.22f, 1}, Col{0.5f, 0.5f, 0.3f, 1}, 0);
    for (int i = 0; i < stems; i += 1) {
        const double bx = 32 + 16 * (hash_unit(i, 1, seed) - 0.5);
        const double height = low + (high - low) * hash_unit(i, 2, seed);
        const double x1 = bx + 20 * (hash_unit(i, 3, seed) - 0.5);
        blade(t, cell, bx, 127.5, x1, 127 - height, 3 * (hash_unit(i, 4, seed) - 0.5), 1.4, stem, stem);
    }
}

void paint_daisies(Tex& t, int cell) {
    const std::uint32_t seed = 841u;
    paint_stems(t, cell, seed, 9, 46, 92, Col{0.30f, 0.44f, 0.18f, 1});
    for (int i = 0; i < 9; i += 1) {
        const double bx = 32 + 16 * (hash_unit(i, 1, seed) - 0.5);
        const double height = 46 + 46 * hash_unit(i, 2, seed);
        const double x1 = bx + 20 * (hash_unit(i, 3, seed) - 0.5);
        const double y1 = 127 - height;
        dab(t, cell, x1, y1, 4.2, Col{0.97f, 0.96f, 0.92f, 1});
        dab(t, cell, x1 + 0.6, y1 + 0.8, 3.0, Col{0.90f, 0.89f, 0.86f, 1});
        dab(t, cell, x1, y1, 1.7, Col{0.96f, 0.76f, 0.16f, 1});
    }
}

void paint_tansy(Tex& t, int cell) {
    const std::uint32_t seed = 851u;
    paint_stems(t, cell, seed, 8, 56, 104, Col{0.32f, 0.44f, 0.18f, 1});
    for (int i = 0; i < 8; i += 1) {
        const double bx = 32 + 16 * (hash_unit(i, 1, seed) - 0.5);
        const double height = 56 + 48 * hash_unit(i, 2, seed);
        const double x1 = bx + 20 * (hash_unit(i, 3, seed) - 0.5);
        const double y1 = 127 - height;
        // a flat-topped spray of yellow buttons
        for (int k = 0; k < 9; k += 1) {
            const double ox = 9 * (hash_unit(i, 10 + k, seed) - 0.5);
            const double oy = 3 * hash_unit(i, 20 + k, seed) + 0.08 * ox * ox;
            const double tone = 0.85 + 0.2 * hash_unit(i, 30 + k, seed);
            dab(t, cell, x1 + ox, y1 + oy, 2.0, scaled(Col{0.98f, 0.80f, 0.14f, 1}, tone));
        }
    }
}

void paint_loosestrife(Tex& t, int cell) {
    const std::uint32_t seed = 861u;
    paint_stems(t, cell, seed, 6, 80, 120, Col{0.34f, 0.40f, 0.20f, 1});
    for (int i = 0; i < 6; i += 1) {
        const double bx = 32 + 16 * (hash_unit(i, 1, seed) - 0.5);
        const double height = 80 + 40 * hash_unit(i, 2, seed);
        const double x1 = bx + 20 * (hash_unit(i, 3, seed) - 0.5);
        // a spike of magenta flowers along the stem's top third
        for (int k = 0; k < 14; k += 1) {
            const double u = 1 - 0.36 * k / 13.0;
            double px = 0;
            double py = 0;
            blade_point(bx, 127.5, x1, 127 - height, 3 * (hash_unit(i, 4, seed) - 0.5), u, px, py);
            const double side = (k % 2 == 0 ? 1.0 : -1.0) * (1.4 + 0.6 * hash_unit(i, 40 + k, seed));
            const double tone = 0.8 + 0.3 * hash_unit(i, 50 + k, seed);
            dab(t, cell, px + side, py, 2.0 - 0.06 * (13 - k) * 0.5, scaled(Col{0.80f, 0.30f, 0.62f, 1}, tone));
        }
    }
}

void paint_fern(Tex& t, int cell) {
    const std::uint32_t seed = 871u;
    for (int i = 0; i < 7; i += 1) {
        const double spread = (i - 3) / 3.0;   // -1 .. 1, out from the middle
        const double length = 74 + 40 * hash_unit(i, 1, seed) - 18 * std::abs(spread);
        const double x0 = 32 + 2 * spread;
        const double x1 = 32 + spread * length * 0.62;
        const double y1 = 127 - length * (0.95 - 0.45 * std::abs(spread));
        const double bow = spread * 14 + 4 * (hash_unit(i, 2, seed) - 0.5);
        const double tone = 0.88 + 0.22 * hash_unit(i, 3, seed);
        const Col rib = scaled(Col{0.28f, 0.44f, 0.16f, 1}, tone);
        blade(t, cell, x0, 127.5, x1, y1, bow, 1.6, rib, rib);
        // the leaflets, longest a third of the way out and none at the tip
        const int pairs = 16;
        for (int k = 1; k < pairs; k += 1) {
            const double u = static_cast<double>(k) / pairs;
            double px = 0;
            double py = 0;
            double qx = 0;
            double qy = 0;
            blade_point(x0, 127.5, x1, y1, bow, u, px, py);
            blade_point(x0, 127.5, x1, y1, bow, std::min(1.0, u + 0.02), qx, qy);
            const double dl = std::max(1e-6, std::hypot(qx - px, qy - py));
            const double ax = (qx - px) / dl;
            const double ay = (qy - py) / dl;
            const double reach = 11 * std::sin(kPi * std::pow(u, 0.75)) * (1 - 0.3 * std::abs(spread));
            const Col leaflet = scaled(Col{0.34f, 0.56f, 0.20f, 1}, tone * (0.9 + 0.2 * u));
            for (int side = -1; side <= 1; side += 2) {
                // swept a little toward the tip
                const double ex = px + (-ay * side * 0.85 + ax * 0.5) * reach;
                const double ey = py + (ax * side * 0.85 + ay * 0.5) * reach;
                blade(t, cell, px, py, ex, ey, 0, 2.0, leaflet, leaflet);
            }
        }
    }
}

void make_plants(Tex& t) {
    t.make(kCells * kCellWidth, kCellHeight);
    // the bare texels carry a plant's own colour, so its fringe doesn't fade to white far off
    const Col bare[kCells] = {{0.30f, 0.42f, 0.18f, 1}, {0.52f, 0.48f, 0.30f, 1}, {0.32f, 0.40f, 0.18f, 1}, {0.24f, 0.36f, 0.14f, 1},
                              {0.40f, 0.48f, 0.28f, 1}, {0.44f, 0.46f, 0.20f, 1}, {0.40f, 0.36f, 0.30f, 1}, {0.28f, 0.44f, 0.16f, 1}};
    for (int y = 0; y < kCellHeight; y += 1) {
        for (int x = 0; x < kCells * kCellWidth; x += 1) {
            const Col c = bare[x / kCellWidth];
            t.at(x, y) = pack(c.r, c.g, c.b, 0);
        }
    }
    paint_tuft(t, static_cast<int>(Plant::grass_tuft), 801u, 26, 104, Col{0.20f, 0.34f, 0.12f, 1}, Col{0.50f, 0.66f, 0.26f, 1},
               Col{0.72f, 0.68f, 0.40f, 1}, 0.08);
    paint_tuft(t, static_cast<int>(Plant::dry_tuft), 811u, 22, 84, Col{0.36f, 0.38f, 0.20f, 1}, Col{0.62f, 0.62f, 0.32f, 1},
               Col{0.82f, 0.74f, 0.48f, 1}, 0.6);
    paint_cattails(t, static_cast<int>(Plant::cattails));
    paint_rushes(t, static_cast<int>(Plant::rushes));
    paint_daisies(t, static_cast<int>(Plant::daisies));
    paint_tansy(t, static_cast<int>(Plant::tansy));
    paint_loosestrife(t, static_cast<int>(Plant::loosestrife));
    paint_fern(t, static_cast<int>(Plant::fern));
    t.build_mips();
    // Averaging thins a cut-out's coverage at each smaller level until distant plants
    // vanish; hold it up so they keep their body as they recede.
    for (std::size_t level = 0; level < t.mips.size(); level += 1) {
        const double boost = 1.5 + 0.5 * static_cast<double>(level);
        std::vector<std::uint32_t>& px = t.mips[level].px;
        for (std::size_t i = 0; i < px.size(); i += 1) {
            const double a = static_cast<double>(px[i] >> 24) / 255.0;
            const std::uint32_t lifted = static_cast<std::uint32_t>(std::lround(std::min(1.0, a * boost) * 255));
            px[i] = (px[i] & 0x00ffffffu) | (lifted << 24);
        }
    }
}

// Fair-weather cloud: fractal noise that repeats across the tile.
void make_clouds(std::vector<float>& cover) {
    cover.assign(static_cast<std::size_t>(kCloudSize) * kCloudSize, 0.f);
    for (int y = 0; y < kCloudSize; y += 1) {
        for (int x = 0; x < kCloudSize; x += 1) {
            double sum = 0;
            double weight = 0;
            double amplitude = 1;
            int period = 4;
            for (int octave = 0; octave < 5; octave += 1) {
                const double fx = static_cast<double>(x) * period / kCloudSize;
                const double fy = static_cast<double>(y) * period / kCloudSize;
                sum += amplitude * tile_noise(fx, fy, period, period, 701u + static_cast<std::uint32_t>(octave));
                weight += amplitude;
                amplitude *= 0.5;
                period *= 2;
            }
            cover[static_cast<std::size_t>(y) * kCloudSize + static_cast<std::size_t>(x)] = static_cast<float>(sum / weight);
        }
    }
}

// The cloud cover at (u, v), in repeats of the tile, blended between texels.
double cloud_at(const std::vector<float>& cover, double u, double v) {
    const double fx = u * kCloudSize - 0.5;
    const double fy = v * kCloudSize - 0.5;
    const double x0 = std::floor(fx);
    const double y0 = std::floor(fy);
    const double tx = fx - x0;
    const double ty = fy - y0;
    const int ix = ((static_cast<int>(x0) % kCloudSize) + kCloudSize) % kCloudSize;
    const int iy = ((static_cast<int>(y0) % kCloudSize) + kCloudSize) % kCloudSize;
    const int jx = (ix + 1) % kCloudSize;
    const int jy = (iy + 1) % kCloudSize;
    const double a = cover[static_cast<std::size_t>(iy * kCloudSize + ix)];
    const double b = cover[static_cast<std::size_t>(iy * kCloudSize + jx)];
    const double c = cover[static_cast<std::size_t>(jy * kCloudSize + ix)];
    const double d = cover[static_cast<std::size_t>(jy * kCloudSize + jx)];
    return (a + (b - a) * tx) * (1 - ty) + (c + (d - c) * tx) * ty;
}

// ---------------------------------------------------------------- placing

// Where nothing tall may stand: the brook and its margins, and the gravel bar the work is on.
bool kept_clear(double x, double y, double margin) {
    if (y > brook_near_at(x) - 0.2 - margin && y < brook_far_at(x) + 0.3 + margin) {
        return true;
    }
    return gravel_bar(x, y) > 0.05;
}

// Where the work is: the bowl, the stack's ledge, the crane and its boulder, the sign.
bool at_the_work(double x, double y) {
    const SiteLayout& layout = site_layout();
    if (std::hypot(x - layout.bowl_centre.x, y - layout.bowl_centre.y) < layout.bowl_rim_radius + layout.bowl_wall + 0.08) {
        return true;
    }
    if (on_ledge(x, y, 0.08)) {
        return true;
    }
    if (std::hypot((x - layout.crane_base.x) / 0.42, (y - layout.crane_base.y) / 0.32) < 1.0) {
        return true;
    }
    return std::hypot(x - 0.752, y + 0.21) < 0.1;
}

// Texture coordinates round an upright or leaning stem: once round in s per `around`
// repeats, `per_metre` repeats of t up it. A triangle across the seam is mended.
void wrap_bark(std::vector<Vtx>& out, std::size_t first, V3 foot, V3 axis, double around, double per_metre) {
    const V3 up = norm(axis);
    const V3 side = std::abs(up.z) < 0.9 ? norm(cross(up, V3{0, 0, 1})) : V3{1, 0, 0};
    const V3 other = cross(up, side);
    for (std::size_t k = first; k < out.size(); k += 1) {
        const V3 d = out[k].p - foot;
        const double angle = std::atan2(dot(d, other), dot(d, side));
        out[k].s = (angle / (2 * kPi) + 0.5) * around;
        out[k].t = dot(d, up) * per_metre;
    }
    for (std::size_t k = first; k + 2 < out.size(); k += 3) {
        const double low = std::min({out[k].s, out[k + 1].s, out[k + 2].s});
        for (std::size_t j = k; j < k + 3; j += 1) {
            if (out[j].s - low > 0.5 * around) {
                out[j].s -= around;
            }
        }
    }
}

// A clump of `cards` upright cards crossing at its foot, turned by `turn`, showing the
// atlas cell for `plant`. Their normals point up, so they light like the ground they
// grow from rather than like walls.
void add_clump(std::vector<Vtx>& out, V3 foot, double width, double height, double turn, Plant plant, int cards, Col tint, double lean_x,
               double lean_y) {
    const int cell = static_cast<int>(plant);
    const double s0 = (cell + 0.5 / kCellWidth) / kCells;
    const double s1 = (cell + 1 - 0.5 / kCellWidth) / kCells;
    for (int c = 0; c < cards; c += 1) {
        const double angle = turn + kPi * c / cards;
        const V3 along{std::cos(angle) * width * 0.5, std::sin(angle) * width * 0.5, 0};
        const V3 base = foot - V3{0, 0, 0.01};
        const V3 top_shift{lean_x * height, lean_y * height, height};
        const V3 corners[4] = {base - along, base + along, base + along + top_shift, base - along + top_shift};
        const double st[4][2] = {{s0, 0.996}, {s1, 0.996}, {s1, 0.002}, {s0, 0.002}};
        const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int k = 0; k < 6; k += 1) {
            Vtx v;
            v.p = corners[order[k]];
            v.n = V3{0, 0, 1};
            v.c = tint;
            v.s = st[order[k]][0];
            v.t = st[order[k]][1];
            out.push_back(v);
        }
    }
}

double distance_from_work(double x, double y) {
    return std::hypot(x, y);
}

// The tree capsule's nearest approach to the segment from a to b, and how far along it.
double capsule_gap(V3 foot, V3 top, V3 a, V3 b, double& along) {
    double best = 1e9;
    along = -1;
    for (int k = 0; k <= 6; k += 1) {
        const V3 p = foot + (top - foot) * (k / 6.0);
        const V3 ab = b - a;
        const double t = std::clamp(dot(p - a, ab) / std::max(1e-9, dot(ab, ab)), 0.0, 1.0);
        const double gap = len(a + ab * t - p);
        if (gap < best) {
            best = gap;
            along = t;
        }
    }
    return best;
}

}  // namespace

// ---------------------------------------------------------------- building

void Country::build(Col haze, V3 sun) {
    haze_ = haze;
    sun_ = norm(sun);
    make_leaves(leaf_tex_);
    make_bark(bark_tex_);
    make_plants(plant_tex_);
    make_clouds(clouds_);
    build_trees();
    build_plants();
    build_rocks_and_timber();
    build_hills();
}

void Country::finish_tree(Tree tree, std::size_t wood_first, std::size_t leaf_first) {
    tree.wood_first = wood_first;
    tree.wood_count = wood_.size() - wood_first;
    tree.leaf_first = leaf_first;
    tree.leaf_count = leaves_.size() - leaf_first;
    tree.bearing = std::atan2(tree.foot.y, tree.foot.x);
    trees_.push_back(tree);
}

// A lump of foliage: a lumpy ball shaded as part of the whole crown, lit on its sunny side
// and dark underneath and within.
void Country::add_crown_blob(V3 centre, double radius, double squash, V3 crown_centre, double crown, Col colour, std::uint32_t seed, int detail) {
    const int slices[3] = {6, 7, 8};
    const int stacks[3] = {4, 5, 6};
    const Mesh lump = rock_mesh(seed, slices[detail], stacks[detail], 0.24);
    for (std::size_t k = 0; k < lump.size(); k += 1) {
        const V3 local = lump[k].p;
        Vtx v;
        v.p = centre + V3{local.x * radius, local.y * radius, local.z * radius * squash};
        const V3 own = norm(V3{local.x, local.y, local.z / squash});
        const V3 whole = norm(V3{v.p.x - crown_centre.x, v.p.y - crown_centre.y, (v.p.z - crown_centre.z) * 1.3});
        v.n = norm(own * 0.4 + whole * 0.6);
        const double height = (v.p.z - crown_centre.z) / crown;
        const double depth = len(V3{v.p.x - crown_centre.x, v.p.y - crown_centre.y, 0}) / crown;
        const double shade = 0.64 + 0.34 * smoothstep(-1.0, 0.8, height) + 0.16 * smoothstep(0.2, 0.9, depth);
        v.c = scaled(colour, shade);
        v.s = (v.p.x * 0.8 + v.p.y * 0.6) * 1.7 + 0.37 * static_cast<double>(seed % 7u);
        v.t = v.p.z * 1.7;
        leaves_.push_back(v);
    }
}

// The pool of shade a tree throws on the ground, away from the sun: dark at its heart,
// fading at its edge, following the ground and never on the water.
void Country::add_shade(V3 foot, double height, double radius) {
    const double reach = height / std::max(0.2, sun_.z);
    const V3 centre{foot.x - sun_.x * reach * 0.62, foot.y - sun_.y * reach * 0.62, 0};
    const V3 stretch = norm(V3{-sun_.x, -sun_.y, 0});
    const int around = 14;
    const double water = site_layout().water_level;
    for (int k = 0; k < around; k += 1) {
        const double a0 = 2 * kPi * k / around;
        const double a1 = 2 * kPi * (k + 1) / around;
        V3 rim[2];
        const double angles[2] = {a0, a1};
        for (int j = 0; j < 2; j += 1) {
            const double c = std::cos(angles[j]);
            const double s = std::sin(angles[j]);
            // longer away from the sun than across
            const V3 across{-stretch.y, stretch.x, 0};
            rim[j] = centre + stretch * (c * radius * 1.35) + across * (s * radius);
        }
        const V3 points[3] = {centre, rim[0], rim[1]};
        for (int j = 0; j < 3; j += 1) {
            Vtx v;
            const double x = points[j].x;
            const double y = points[j].y;
            const bool wet = y > brook_near_at(x) - 0.02 && y < brook_far_at(x) + 0.02;
            v.p = V3{x, y, std::max(ground_height(x, y), water) + 0.012};
            v.n = V3{0, 0, 1};
            const float alpha = wet ? 0.f : (j == 0 ? 0.36f : 0.f);
            v.c = Col{0.08f, 0.12f, 0.05f, alpha};
            shade_.push_back(v);
        }
    }
}

void Country::add_broadleaf(V3 foot, double height, double crown, std::uint32_t seed, int detail, int kind) {
    const Leaf leaf = static_cast<Leaf>(kind);
    const std::size_t wood_first = wood_.size();
    const std::size_t leaf_first = leaves_.size();
    const bool birch = leaf == Leaf::birch;
    const bool willow = leaf == Leaf::willow;
    const V3 lean{0.06 * (hash_unit(static_cast<int>(seed), 1, 901u) - 0.5) * height, 0.06 * (hash_unit(static_cast<int>(seed), 2, 901u) - 0.5) * height, 0};
    const double squash = willow ? 1.35 : 0.88;
    const V3 crown_centre = foot + lean + V3{0, 0, height - crown * squash};
    // the trunk, tapering, up into the crown
    {
        const double r0 = birch ? 0.045 + 0.012 * crown : 0.06 + 0.05 * crown;
        const double radius[4] = {r0 * 1.25, r0, r0 * 0.78, r0 * 0.5};
        const double rise = crown_centre.z - foot.z;
        const double heights[4] = {-0.05, rise * 0.15, rise * 0.6, rise};
        const Col bark = birch ? Col{0.90f, 0.88f, 0.82f, 1} : Col{0.46f, 0.38f, 0.30f, 1};
        const std::size_t first = wood_.size();
        const M34 frame = M34::translate(foot.x, foot.y, foot.z) * M34::rot_x(-lean.y / std::max(0.1, rise)) * M34::rot_y(lean.x / std::max(0.1, rise));
        add_lathe(wood_, frame, radius, heights, 4, detail == 2 ? 7 : 5, bark);
        wrap_bark(wood_, first, foot, crown_centre - foot, birch ? 1.0 : 2.0, birch ? 1.2 : 3.0);
        if (birch) {
            // the dark lenticels and scars on its pale bark
            for (int k = 0; k < 5; k += 1) {
                const double u = 0.12 + 0.16 * k + 0.05 * hash_unit(static_cast<int>(seed), 10 + k, 903u);
                const V3 at = foot + (crown_centre - foot) * u;
                const double rr = (r0 * (1.2 - 0.6 * u)) * 1.06;
                const double ring_r[2] = {rr, rr};
                const double ring_z[2] = {0, 0.025 + 0.03 * hash_unit(static_cast<int>(seed), 20 + k, 903u)};
                add_lathe(wood_, M34::translate(at.x, at.y, at.z), ring_r, ring_z, 2, 5, Col{0.20f, 0.18f, 0.16f, 1});
            }
        }
    }
    // the colour of its leaves
    Col colour{0.30f, 0.46f, 0.20f, 1};
    if (leaf == Leaf::alder) {
        colour = Col{0.26f, 0.42f, 0.19f, 1};
    } else if (birch) {
        colour = Col{0.48f, 0.60f, 0.26f, 1};
    } else if (willow) {
        colour = Col{0.50f, 0.60f, 0.28f, 1};
    }
    const double tone = 0.9 + 0.2 * hash_unit(static_cast<int>(seed), 3, 905u);
    colour = scaled(colour, tone);
    // in autumn some trees are turning, birches first
    double turning = 0;
    Col turned{0.84f, 0.62f, 0.18f, 1};
    if (autumn_) {
        const double chance = hash_unit(static_cast<int>(seed), 4, 905u);
        if (birch) {
            turning = 0.55 + 0.4 * chance;
            turned = Col{0.86f, 0.72f, 0.24f, 1};
        } else if (chance < 0.6 && !willow) {
            turning = 0.35 + 0.6 * hash_unit(static_cast<int>(seed), 5, 905u);
            turned = hash_unit(static_cast<int>(seed), 6, 905u) < 0.5 ? Col{0.84f, 0.56f, 0.16f, 1} : Col{0.76f, 0.34f, 0.14f, 1};
        }
    }
    // the crown: a heart and lumps round it, more of them up close
    const int lumps[3] = {4, 6, 9};
    const int count = lumps[detail];
    for (int k = 0; k < count; k += 1) {
        V3 centre = crown_centre;
        double radius = crown * 0.66;
        if (k > 0) {
            const double z = -0.45 + 1.3 * hash_unit(static_cast<int>(seed), 30 + k, 907u);
            const double phi = 2 * kPi * (static_cast<double>(k) / count + 0.3 * hash_unit(static_cast<int>(seed), 50 + k, 907u));
            const double flat = std::sqrt(std::max(0.0, 1 - z * z));
            centre = crown_centre + V3{std::cos(phi) * flat * crown * 0.6, std::sin(phi) * flat * crown * 0.6, z * crown * squash * 0.55};
            radius = crown * (0.38 + 0.16 * hash_unit(static_cast<int>(seed), 70 + k, 907u));
            if (willow) {
                // a willow's lumps hang down toward the water
                centre.z -= crown * 0.35 * flat;
            }
            // a branch out to it
            if (k <= 3) {
                const V3 from = foot + (crown_centre - foot) * 0.72;
                const std::size_t first = wood_.size();
                add_rod(wood_, from, from + (centre - from) * 0.75, 0.018 + 0.012 * crown, birch ? Col{0.80f, 0.78f, 0.72f, 1} : Col{0.42f, 0.34f, 0.27f, 1},
                        detail == 2 ? 5 : 4);
                wrap_bark(wood_, first, from, centre - from, 1.0, 3.0);
            }
        }
        double change = 0;
        if (turning > 0) {
            change = std::clamp(turning + 0.5 * (hash_unit(static_cast<int>(seed), 90 + k, 909u) - 0.5), 0.0, 1.0);
        }
        const Col lump_colour = mix(scaled(colour, 0.92 + 0.16 * hash_unit(static_cast<int>(seed), 80 + k, 909u)), turned, static_cast<float>(change));
        add_crown_blob(centre, radius, willow ? 1.5 : 0.9, crown_centre, crown, lump_colour, seed * 31u + static_cast<std::uint32_t>(k), detail);
    }
    add_shade(foot, crown_centre.z - foot.z, crown * 0.9);
    Tree tree;
    tree.foot = foot;
    tree.top = foot + lean + V3{0, 0, height};
    tree.radius = crown;
    finish_tree(tree, wood_first, leaf_first);
}

void Country::add_conifer(V3 foot, double height, double radius, std::uint32_t seed, int detail) {
    const std::size_t wood_first = wood_.size();
    const std::size_t leaf_first = leaves_.size();
    {
        const double r0 = 0.05 + 0.04 * radius;
        const double rr[3] = {r0 * 1.3, r0, r0 * 0.35};
        const double hz[3] = {-0.05, height * 0.25, height * 0.92};
        const std::size_t first = wood_.size();
        add_lathe(wood_, M34::translate(foot.x, foot.y, foot.z), rr, hz, 3, 5, Col{0.40f, 0.30f, 0.24f, 1});
        wrap_bark(wood_, first, foot, V3{0, 0, 1}, 2.0, 3.0);
    }
    const int tiers_by_detail[3] = {4, 5, 7};
    const int slices_by_detail[3] = {6, 8, 9};
    const int tiers = tiers_by_detail[detail];
    const int slices = slices_by_detail[detail];
    const double tone = 0.88 + 0.24 * hash_unit(static_cast<int>(seed), 1, 911u);
    const Col needles = scaled(Col{0.15f, 0.29f, 0.18f, 1}, tone);
    const double spin = 2 * kPi * hash_unit(static_cast<int>(seed), 2, 911u);
    for (int i = 0; i < tiers; i += 1) {
        const double f = static_cast<double>(i) / tiers;
        const double z0 = height * (0.16 + 0.72 * f);
        const double reach = height * 0.84 / tiers * 1.9;
        const double z1 = std::min(height, z0 + reach);
        const double r = radius * (1 - 0.8 * f) * (0.9 + 0.2 * hash_unit(static_cast<int>(seed), 10 + i, 911u));
        // each tier's skirt droops: steep at its top, flattening toward its hem
        const double upper_r[3] = {r, r * 0.52, 0.0};
        const double upper_z[3] = {z0, z0 + (z1 - z0) * 0.42, z1};
        const double under_r[2] = {0.0, r};
        const double under_z[2] = {z0 + (z1 - z0) * 0.14, z0};
        const M34 frame = M34::translate(foot.x, foot.y, foot.z) * M34::rot_z(spin + i * 0.7);
        const std::size_t first = leaves_.size();
        add_lathe(leaves_, frame, upper_r, upper_z, 3, slices, scaled(needles, 0.78 + 0.3 * f));
        add_lathe(leaves_, frame, under_r, under_z, 2, slices, scaled(needles, 0.5));
        for (std::size_t k = first; k < leaves_.size(); k += 1) {
            // the hem a little ragged
            const V3 d = leaves_[k].p - foot;
            const double out = std::hypot(d.x, d.y);
            if (out > r * 0.8) {
                const double rag = 1 + 0.12 * (hash_unit(static_cast<int>(std::lround(leaves_[k].p.x * 1000)), static_cast<int>(std::lround(leaves_[k].p.y * 1000)), 913u) - 0.5);
                leaves_[k].p = foot + V3{d.x * rag, d.y * rag, d.z - 0.04 * r * (rag - 1) * 8};
            }
            leaves_[k].s = (leaves_[k].p.x * 0.8 + leaves_[k].p.y * 0.6) * 2.2;
            leaves_[k].t = leaves_[k].p.z * 2.2;
        }
    }
    add_shade(foot, height * 0.45, radius * 0.75);
    Tree tree;
    tree.foot = foot;
    tree.top = foot + V3{0, 0, height};
    tree.radius = radius;
    finish_tree(tree, wood_first, leaf_first);
}

void Country::add_shrub(V3 foot, double size, std::uint32_t seed) {
    const std::size_t wood_first = wood_.size();
    const std::size_t leaf_first = leaves_.size();
    const V3 centre = foot + V3{0, 0, size * 0.45};
    Col colour = scaled(Col{0.30f, 0.44f, 0.20f, 1}, 0.85 + 0.25 * hash_unit(static_cast<int>(seed), 1, 921u));
    if (autumn_ && hash_unit(static_cast<int>(seed), 3, 921u) < 0.45) {
        colour = mix(colour, Col{0.62f, 0.36f, 0.16f, 1}, static_cast<float>(0.3 + 0.4 * hash_unit(static_cast<int>(seed), 4, 921u)));
    }
    const int count = 3 + static_cast<int>(hash2(static_cast<int>(seed), 2, 921u) % 3u);
    for (int k = 0; k < count; k += 1) {
        const double phi = 2 * kPi * (static_cast<double>(k) / count + 0.2 * hash_unit(static_cast<int>(seed), 10 + k, 921u));
        const double out = k == 0 ? 0.0 : size * 0.38;
        const V3 at = centre + V3{std::cos(phi) * out, std::sin(phi) * out, (k == 0 ? 0.12 : -0.05) * size};
        add_crown_blob(at, size * (0.45 + 0.12 * hash_unit(static_cast<int>(seed), 20 + k, 921u)), 0.8, centre, size, colour,
                       seed * 17u + static_cast<std::uint32_t>(k), 0);
    }
    Tree tree;
    tree.foot = foot;
    tree.top = foot + V3{0, 0, size};
    tree.radius = size * 0.8;
    finish_tree(tree, wood_first, leaf_first);
}

void Country::build_trees() {
    int seed = 0;
    // along the far bank, alders and the odd willow leaning out over the water
    for (double x = -kReach; x <= kReach; x += 1.5) {
        seed += 1;
        const double jx = x + 0.9 * (hash_unit(seed, 1, 931u) - 0.5);
        if (hash_unit(seed, 2, 931u) > 0.6) {
            continue;
        }
        if (jx > -1.8 && jx < 2.0) {
            continue;   // the far bank in the work's view stays open woodland floor
        }
        const double y = brook_far_at(jx) + 0.55 + 0.7 * hash_unit(seed, 3, 931u);
        const double d = distance_from_work(jx, y);
        const int detail = d < 7 ? 2 : (d < 13 ? 1 : 0);
        const bool willow = hash_unit(seed, 4, 931u) < 0.18;
        const double height = willow ? 2.6 + 0.8 * hash_unit(seed, 5, 931u) : 2.6 + 1.4 * hash_unit(seed, 5, 931u);
        add_broadleaf(V3{jx, y, ground_height(jx, y)}, height, willow ? 1.05 : 0.75 + 0.35 * hash_unit(seed, 6, 931u),
                      static_cast<std::uint32_t>(seed), detail, static_cast<int>(willow ? Leaf::willow : Leaf::alder));
    }
    // the woods beyond, thickening and darkening with conifers toward the back
    for (double y = 3.0; y <= 11.5; y += 1.45) {
        for (double x = -kReach; x <= kReach; x += 1.45) {
            seed += 1;
            const double jx = x + 1.0 * (hash_unit(seed, 1, 941u) - 0.5);
            const double jy = y + 1.0 * (hash_unit(seed, 2, 941u) - 0.5) + brook_bend(jx);
            const double back = jy - brook_far_at(jx);
            if (back < 1.2) {
                continue;
            }
            const double chance = 0.3 + 0.45 * smoothstep(2.0, 6.0, back);
            if (hash_unit(seed, 3, 941u) > chance) {
                continue;
            }
            const double d = distance_from_work(jx, jy);
            const int detail = d < 7 ? 2 : (d < 13 ? 1 : 0);
            const V3 foot{jx, jy, ground_height(jx, jy)};
            const double kind = hash_unit(seed, 4, 941u);
            const double conifers = 0.12 + 0.5 * smoothstep(3.0, 8.0, back);
            if (kind < conifers) {
                add_conifer(foot, 4.2 + 3.6 * hash_unit(seed, 5, 941u), 1.0 + 0.5 * hash_unit(seed, 6, 941u), static_cast<std::uint32_t>(seed), detail);
            } else if (kind < conifers + 0.16) {
                add_broadleaf(foot, 3.6 + 1.6 * hash_unit(seed, 5, 941u), 0.8 + 0.3 * hash_unit(seed, 6, 941u), static_cast<std::uint32_t>(seed), detail,
                              static_cast<int>(Leaf::birch));
            } else {
                add_broadleaf(foot, 3.8 + 2.4 * hash_unit(seed, 5, 941u), 1.2 + 0.6 * hash_unit(seed, 6, 941u), static_cast<std::uint32_t>(seed), detail,
                              static_cast<int>(Leaf::oak));
            }
        }
    }
    // a hedgerow across the meadow behind the bar, an oak standing in it now and then
    for (double x = -kReach; x <= kReach; x += 0.75) {
        seed += 1;
        const double y = -7.2 + 0.5 * std::sin(x * 0.31) + 0.3 * (hash_unit(seed, 1, 951u) - 0.5) + brook_bend(x);
        if (hash_unit(seed, 2, 951u) < 0.1) {
            continue;   // a gap
        }
        const V3 foot{x, y, ground_height(x, y)};
        if (hash_unit(seed, 3, 951u) < 0.09) {
            add_broadleaf(foot, 4.0 + 1.6 * hash_unit(seed, 4, 951u), 1.3 + 0.4 * hash_unit(seed, 5, 951u), static_cast<std::uint32_t>(seed),
                          distance_from_work(x, y) < 9 ? 1 : 0, static_cast<int>(Leaf::oak));
        } else {
            add_shrub(foot, 0.55 + 0.35 * hash_unit(seed, 4, 951u), static_cast<std::uint32_t>(seed));
        }
    }
    // the meadow's lone trees and bushes, and alders along the near bank away from the work
    for (int i = 0; i < 70; i += 1) {
        seed += 1;
        const double x = -kReach + 2 * kReach * hash_unit(seed, 1, 961u);
        const bool waterside = i < 18;
        const double y = waterside ? brook_near_at(x) - 0.45 - 0.5 * hash_unit(seed, 2, 961u) : -2.5 - 9.5 * hash_unit(seed, 2, 961u) + brook_bend(x);
        if (kept_clear(x, y, 0.1) || std::abs(y + 7.2 - brook_bend(x)) < 0.8) {
            continue;
        }
        if (waterside && std::abs(x) < 4.0) {
            continue;
        }
        const V3 foot{x, y, ground_height(x, y)};
        const double d = distance_from_work(x, y);
        if (waterside || hash_unit(seed, 3, 961u) < 0.3) {
            add_broadleaf(foot, 2.6 + 1.6 * hash_unit(seed, 4, 961u), 0.8 + 0.4 * hash_unit(seed, 5, 961u), static_cast<std::uint32_t>(seed),
                          d < 7 ? 2 : (d < 13 ? 1 : 0), static_cast<int>(waterside ? Leaf::alder : Leaf::oak));
        } else {
            add_shrub(foot, 0.35 + 0.4 * hash_unit(seed, 4, 961u), static_cast<std::uint32_t>(seed));
        }
    }
    // in order of bearing from the work, so those in view are drawn in a few long runs
    std::vector<Tree> order = trees_;
    std::vector<std::pair<double, std::size_t>> by_bearing;
    for (std::size_t i = 0; i < order.size(); i += 1) {
        by_bearing.push_back(std::pair<double, std::size_t>(order[i].bearing, i));
    }
    std::sort(by_bearing.begin(), by_bearing.end());
    std::vector<Vtx> wood;
    std::vector<Vtx> leaves;
    trees_.clear();
    for (std::size_t n = 0; n < by_bearing.size(); n += 1) {
        Tree tree = order[by_bearing[n].second];
        const std::size_t wood_first = wood.size();
        const std::size_t leaf_first = leaves.size();
        wood.insert(wood.end(), wood_.begin() + static_cast<std::ptrdiff_t>(tree.wood_first),
                    wood_.begin() + static_cast<std::ptrdiff_t>(tree.wood_first + tree.wood_count));
        leaves.insert(leaves.end(), leaves_.begin() + static_cast<std::ptrdiff_t>(tree.leaf_first),
                      leaves_.begin() + static_cast<std::ptrdiff_t>(tree.leaf_first + tree.leaf_count));
        tree.wood_first = wood_first;
        tree.leaf_first = leaf_first;
        trees_.push_back(tree);
    }
    wood_.swap(wood);
    leaves_.swap(leaves);
}

void Country::build_plants() {
    int seed = 0;
    const Col white{1, 1, 1, 1};
    // beds of reeds and rushes along both edges and out into the shallows, with gaps between
    for (int bed = 0; bed < 64; bed += 1) {
        seed += 1;
        const bool far_side = bed % 3 != 0;
        const double bx = -kReach + 2 * kReach * hash_unit(seed, 1, 971u);
        if (!far_side && std::abs(bx) < 1.5) {
            continue;   // the near bank by the work stays clear
        }
        const double reeds = hash_unit(seed, 2, 971u);   // how much of the bed is cattails, the rest rushes
        const int clumps = 5 + static_cast<int>(hash2(seed, 3, 971u) % 9u);
        const double spread = 0.25 + 0.4 * hash_unit(seed, 4, 971u);
        for (int i = 0; i < clumps; i += 1) {
            const double x = bx + spread * 2 * (hash_unit(seed, 10 + i, 973u) - 0.5);
            const double into = std::pow(hash_unit(seed, 30 + i, 973u), 1.4);
            const double y = far_side ? brook_far_at(x) - 0.08 + 0.3 * into : brook_near_at(x) + 0.06 - 0.26 * into;
            if (at_the_work(x, y) || (!far_side && std::abs(x) < 1.25)) {
                continue;
            }
            const double pick = hash_unit(seed, 50 + i, 973u);
            const Plant plant = pick < reeds ? Plant::cattails : (pick < reeds + 0.12 ? Plant::loosestrife : Plant::rushes);
            const double size = 0.16 + 0.18 * hash_unit(seed, 70 + i, 973u);
            const double lean = far_side ? 0.06 : -0.06;
            add_clump(plants_, V3{x, y, std::max(ground_height(x, y), site_layout().water_level)}, size * 0.7, size, 3.0 * hash_unit(seed, 90 + i, 973u), plant,
                      2, scaled(white, 0.9 + 0.15 * hash_unit(seed, 110 + i, 973u)), 0.06 * (hash_unit(seed, 130 + i, 973u) - 0.5),
                      lean * hash_unit(seed, 150 + i, 973u));
        }
    }
    // the far bank: flowers in the open grass by the water, ferns under the trees
    for (int i = 0; i < 1100; i += 1) {
        seed += 1;
        const double x = -kReach + 2 * kReach * hash_unit(seed, 1, 975u);
        const double back = 0.3 + 9.0 * std::pow(hash_unit(seed, 2, 975u), 1.4);
        const double y = brook_far_at(x) + back;
        const double d = distance_from_work(x, y);
        if (d > 12 && hash_unit(seed, 9, 975u) < 0.6) {
            continue;
        }
        const double pick = hash_unit(seed, 3, 975u);
        Plant plant = Plant::grass_tuft;
        double size = 0.14 + 0.14 * hash_unit(seed, 4, 975u);
        if (back < 1.4) {
            if (pick < 0.22) {
                plant = Plant::daisies;
            } else if (pick < 0.36) {
                plant = Plant::tansy;
            } else if (pick < 0.44 && back < 0.8) {
                plant = Plant::loosestrife;
                size *= 1.6;
            }
        } else if (pick < 0.6) {
            plant = Plant::fern;
            size = 0.22 + 0.26 * hash_unit(seed, 4, 975u);
        } else if (pick < 0.68) {
            plant = Plant::daisies;
        }
        add_clump(plants_, V3{x, y, ground_height(x, y)}, size * (plant == Plant::fern ? 1.3 : 0.8), size, 3.0 * hash_unit(seed, 5, 975u), plant,
                  plant == Plant::fern ? 3 : 2, scaled(white, 0.88 + 0.18 * hash_unit(seed, 6, 975u)), 0, 0);
    }
    // the meadow behind and beside the bar, and its fringe of dry grass
    for (int i = 0; i < 1300; i += 1) {
        seed += 1;
        const double x = -kReach + 2 * kReach * hash_unit(seed, 1, 977u);
        const double y = -12.5 + (brook_near_at(x) - 0.2 + 12.5) * hash_unit(seed, 2, 977u);
        const double d = distance_from_work(x, y);
        if (d > 12 && hash_unit(seed, 9, 977u) < 0.6) {
            continue;
        }
        const double bar = gravel_bar(x, y);
        if (bar > 0.75 || at_the_work(x, y)) {
            continue;
        }
        const double pick = hash_unit(seed, 3, 977u);
        Plant plant = Plant::grass_tuft;
        if (bar > 0.25) {
            plant = Plant::dry_tuft;
        } else if (pick < 0.14) {
            plant = Plant::daisies;
        } else if (pick < 0.24) {
            plant = Plant::tansy;
        } else if (pick < 0.4) {
            plant = Plant::dry_tuft;
        }
        const double size = (bar > 0.25 ? 0.07 : 0.1) + 0.12 * hash_unit(seed, 4, 977u);
        add_clump(plants_, V3{x, y, ground_height(x, y)}, size * 0.8, size, 3.0 * hash_unit(seed, 5, 977u), plant, 2,
                  scaled(white, 0.88 + 0.18 * hash_unit(seed, 6, 977u)), 0, 0);
    }
    // a few tufts in the gravel at the bar's water edge, either side of the work
    for (int i = 0; i < 40; i += 1) {
        seed += 1;
        const double x = -1.7 + 3.4 * hash_unit(seed, 1, 979u);
        const double y = brook_near_at(x) - 0.04 - 0.3 * hash_unit(seed, 2, 979u);
        if (std::abs(x) < 0.95 || at_the_work(x, y)) {
            continue;
        }
        const double size = 0.06 + 0.07 * hash_unit(seed, 4, 979u);
        add_clump(plants_, V3{x, y, ground_height(x, y)}, size * 0.8, size, 3.0 * hash_unit(seed, 5, 979u),
                  hash_unit(seed, 3, 979u) < 0.6 ? Plant::dry_tuft : Plant::grass_tuft, 2, scaled(white, 0.9 + 0.15 * hash_unit(seed, 6, 979u)), 0, 0);
    }
}

void Country::build_rocks_and_timber() {
    // mossy boulders: in the woods, out on the meadow, and two at the water's far edge
    const double spots[][4] = {
        // x, y, size, moss
        {-1.6, 2.55, 0.34, 0.8},  {1.9, 2.9, 0.42, 0.9},   {-4.2, 3.6, 0.6, 1.0},  {5.1, 4.2, 0.5, 1.0},  {-8.0, 4.8, 0.7, 1.0},
        {8.6, 6.0, 0.6, 1.0},     {-2.6, 1.72, 0.30, 0.4}, {3.3, 1.62, 0.36, 0.5}, {-3.4, -3.6, 0.5, 0.5}, {4.4, -4.6, 0.62, 0.6},
        {-6.8, -2.2, 0.45, 0.5},  {7.5, -1.6, 0.5, 0.5},   {-1.2, -5.2, 0.4, 0.4}, {11.0, -5.5, 0.8, 0.6}, {-11.5, -4.0, 0.7, 0.6},
        {2.3, 2.15, 0.22, 0.6},   {0.4, 3.05, 0.26, 0.9},
    };
    const int count = static_cast<int>(sizeof spots / sizeof spots[0]);
    for (int i = 0; i < count; i += 1) {
        const double x = spots[i][0];
        const double y = spots[i][1] + brook_bend(spots[i][0]);
        const double size = spots[i][2];
        const double moss = spots[i][3];
        const Mesh lump = rock_mesh(2400u + static_cast<std::uint64_t>(i), 14, 10, 0.2);
        const double z = ground_height(x, y);
        const M34 frame = M34::translate(x, y, z - size * 0.12) * M34::rot_z(hash_unit(i, 1, 981u) * 6) *
                          M34::scale(size, size * (0.7 + 0.25 * hash_unit(i, 2, 981u)), size * (0.45 + 0.15 * hash_unit(i, 3, 981u)));
        const std::size_t first = boulders_.size();
        const float k = static_cast<float>(0.85 + 0.25 * hash_unit(i, 4, 981u));
        add_mesh(boulders_, lump, frame, Col{0.58f * k, 0.57f * k, 0.53f * k, 1});
        for (std::size_t v = first; v < boulders_.size(); v += 1) {
            const double up = boulders_[v].n.z + 0.3 * (value_noise(boulders_[v].p.x * 9, boulders_[v].p.y * 9, 983u) - 0.5);
            const float green = static_cast<float>(moss * smoothstep(0.35, 0.8, up));
            boulders_[v].c = mix(boulders_[v].c, Col{0.30f, 0.44f, 0.16f, 1}, green);
        }
    }
    // a bleached driftwood log washed up on the near bank upstream
    {
        const double x = -2.4;
        const double y = brook_near_at(x) - 0.2;
        const double z = ground_height(x, y) + 0.03;
        const double radius[5] = {0.0, 0.052, 0.048, 0.036, 0.0};
        const double along[5] = {0.0, 0.01, 0.55, 1.05, 1.12};
        const M34 frame = M34::translate(x - 0.55, y, z) * M34::rot_z(0.22) * M34::rot_y(kPi / 2);
        const std::size_t first = timber_.size();
        add_lathe(timber_, frame, radius, along, 5, 7, Col{0.78f, 0.74f, 0.66f, 1});
        add_rod(timber_, frame.apply(V3{0, 0, 0.3}), frame.apply(V3{0.02, 0.13, 0.42}), 0.014, Col{0.74f, 0.70f, 0.62f, 1}, 4);
        add_rod(timber_, frame.apply(V3{0, 0, 0.7}), frame.apply(V3{-0.06, -0.11, 0.86}), 0.011, Col{0.74f, 0.70f, 0.62f, 1}, 4);
        wrap_bark(timber_, first, frame.apply(V3{0, 0, 0}), frame.dir(V3{0, 0, 1}), 1.0, 2.0);
    }
    // a fallen log in the edge of the woods, mossed along its top
    {
        const double x = 0.75;
        const double y = brook_far_at(x) + 1.05;
        const double z = ground_height(x, y) + 0.05;
        const double radius[5] = {0.0, 0.095, 0.088, 0.07, 0.0};
        const double along[5] = {0.0, 0.02, 0.8, 1.55, 1.6};
        const M34 frame = M34::translate(x - 0.75, y - 0.2, z) * M34::rot_z(0.32) * M34::rot_y(kPi / 2);
        const std::size_t first = timber_.size();
        add_lathe(timber_, frame, radius, along, 5, 9, Col{0.44f, 0.36f, 0.28f, 1});
        wrap_bark(timber_, first, frame.apply(V3{0, 0, 0}), frame.dir(V3{0, 0, 1}), 2.0, 3.0);
        for (std::size_t v = first; v < timber_.size(); v += 1) {
            const float green = static_cast<float>(smoothstep(0.3, 0.8, timber_[v].n.z + 0.3 * (value_noise(timber_[v].p.x * 8, timber_[v].p.y * 8, 985u) - 0.5)));
            timber_[v].c = mix(timber_[v].c, Col{0.30f, 0.46f, 0.16f, 1}, green);
        }
    }
    // two planks laid side by side across the brook downstream, each end on a flat stone
    {
        const double x = 2.7;
        const double y0 = brook_near_at(x) - 0.24;
        const double y1 = brook_far_at(x) + 0.22;
        const double z0 = ground_height(x, y0) + 0.045;
        const double z1 = ground_height(x, y1) + 0.045;
        for (int side = 0; side < 2; side += 1) {
            const double ox = (side == 0 ? -0.062 : 0.062) + 0.01 * side;
            const double skew = side == 0 ? 0.0 : 0.035;
            const V3 a{x + ox, y0 + skew, z0};
            const V3 b{x + ox + 0.02 * side, y1 + skew, z1};
            const V3 dir = b - a;
            const double length = len(dir);
            const M34 frame = frame_along(a, dir);
            const std::size_t first = timber_.size();
            add_box(timber_, frame, V3{-0.056, -0.014, 0}, V3{0.056, 0.014, length}, Col{0.66f, 0.56f, 0.42f, 1});
            for (std::size_t v = first; v < timber_.size(); v += 1) {
                // the grain along the plank
                timber_[v].s = (timber_[v].p.x - x) * 4.0 + 0.3 * side;
                timber_[v].t = (timber_[v].p.y - y0) * 1.2;
            }
        }
        for (int end = 0; end < 2; end += 1) {
            const double sy = end == 0 ? y0 : y1;
            const Mesh lump = rock_mesh(2600u + static_cast<std::uint64_t>(end), 10, 7, 0.12);
            add_mesh(boulders_, lump, M34::translate(x, sy, ground_height(x, sy) - 0.01) * M34::rot_z(0.6 * end) * M34::scale(0.2, 0.15, 0.05),
                     Col{0.56f, 0.55f, 0.52f, 1});
        }
    }
}

void Country::build_hills() {
    struct Ring {
        double radius, base, rise, crowns, notch, haze_top, haze_foot;
        Col colour;
    };
    const Ring rings[3] = {
        {21.0, 0.9, 1.7, 0.45, 0.6, 0.40, 0.58, Col{0.20f, 0.33f, 0.18f, 1}},
        {31.0, 1.6, 2.8, 0.30, 0.35, 0.58, 0.74, Col{0.24f, 0.36f, 0.28f, 1}},
        {46.0, 2.4, 4.2, 0.0, 0.0, 0.72, 0.84, Col{0.36f, 0.44f, 0.54f, 1}},
    };
    const int segments = 240;
    // hills facing the sun, across the valley from it, are the brightest
    const double lit_bearing = std::atan2(-sun_.y, -sun_.x);
    for (int ring = 0; ring < 3; ring += 1) {
        const Ring& g = rings[ring];
        std::vector<V3> top(static_cast<std::size_t>(segments) + 1);
        std::vector<Col> top_colour(top.size());
        std::vector<Col> foot_colour(top.size());
        for (int k = 0; k <= segments; k += 1) {
            const double angle = 2 * kPi * k / segments;
            const double c = std::cos(angle);
            const double s = std::sin(angle);
            const double o = 11.0 * ring;
            double ridge = g.base + g.rise * (0.6 * value_noise(c * 2.2 + o, s * 2.2 + o, 991u) + 0.4 * value_noise(c * 5.5 + o, s * 5.5 + o, 993u));
            // the woods' crowns break the nearer skylines
            ridge += g.crowns * value_noise(c * 46 + o, s * 46 + o, 995u);
            // lower where the valley runs out between them, both ways along the brook
            ridge *= 1 - g.notch * (1 - smoothstep(0.08, 0.5, std::abs(s)));
            top[static_cast<std::size_t>(k)] = V3{g.radius * c, g.radius * s, ridge};
            const double lit = 0.5 + 0.5 * std::cos(angle - lit_bearing);
            const Col own = scaled(g.colour, 0.82 + 0.32 * lit);
            top_colour[static_cast<std::size_t>(k)] = mix(own, haze_, static_cast<float>(g.haze_top));
            foot_colour[static_cast<std::size_t>(k)] = mix(own, haze_, static_cast<float>(g.haze_foot));
        }
        for (int k = 0; k < segments; k += 1) {
            const std::size_t a = static_cast<std::size_t>(k);
            const std::size_t b = a + 1;
            const V3 corners[4] = {V3{top[a].x, top[a].y, -1.0}, V3{top[b].x, top[b].y, -1.0}, top[b], top[a]};
            const Col colours[4] = {foot_colour[a], foot_colour[b], top_colour[b], top_colour[a]};
            const int order[6] = {0, 1, 2, 0, 2, 3};
            for (int j = 0; j < 6; j += 1) {
                Vtx v;
                v.p = corners[order[j]];
                v.n = V3{0, 0, 1};
                v.c = colours[order[j]];
                hills_.push_back(v);
            }
        }
    }
}

// ---------------------------------------------------------------- drawing

void Country::draw(R3D& r) const {
    const V3 eye = r.target - r.fwd() * r.persp;
    const V3 work = r.target;
    r.draw(shade_.data(), shade_.size(), nullptr, static_cast<std::uint16_t>(translucent | unlit | no_depth_write | double_sided));
    r.draw(boulders_.data(), boulders_.size(), &stone_grain(), opaque);
    r.draw(timber_.data(), timber_.size(), &bark_tex_, opaque);
    // the trees in view, in runs; any round the eye or standing between it and the work left out
    std::size_t run = trees_.size();
    for (std::size_t i = 0; i <= trees_.size(); i += 1) {
        bool shown = false;
        if (i < trees_.size()) {
            const Tree& tree = trees_[i];
            double along = 0;
            const double near_eye = capsule_gap(tree.foot, tree.top, eye, eye, along);
            const double in_the_way = capsule_gap(tree.foot, tree.top, eye, work, along);
            shown = near_eye > tree.radius + 0.9 && !(in_the_way < tree.radius + 0.1 && along > 0.0 && along < 1.0);
            if (shown) {
                // and on the screen
                const V3 middle = (tree.foot + tree.top) * 0.5;
                double sx = 0;
                double sy = 0;
                double sz = 0;
                r.project(middle, sx, sy, sz);
                const double depth = r.persp + sz;
                if (depth < -tree.radius - len(tree.top - tree.foot)) {
                    shown = false;
                } else if (depth > 0.2) {
                    const double extent = (tree.radius + 0.5 * len(tree.top - tree.foot)) * r.scale * r.persp / depth;
                    shown = sx + extent > 0 && sx - extent < r.W && sy + extent > 0 && sy - extent < r.H;
                }
            }
        }
        if (shown && run == trees_.size()) {
            run = i;
        }
        if (!shown && run < trees_.size()) {
            const Tree& first = trees_[run];
            const Tree& last = trees_[i - 1];
            r.draw(wood_.data() + first.wood_first, last.wood_first + last.wood_count - first.wood_first, &bark_tex_, opaque);
            r.draw(leaves_.data() + first.leaf_first, last.leaf_first + last.leaf_count - first.leaf_first, &leaf_tex_, opaque);
            run = trees_.size();
        }
    }
    r.draw(plants_.data(), plants_.size(), &plant_tex_, static_cast<std::uint16_t>(cutout | double_sided));
    r.draw(hills_.data(), hills_.size(), nullptr, static_cast<std::uint16_t>(unlit | no_fog | double_sided));
}

void Country::draw_sky(R3D& r) const {
    const V3 forward = r.fwd() * r.persp;
    const V3 right = r.right();
    const V3 up = r.up();
    const Col zenith{0.36f, 0.56f, 0.86f, 1};
    const Col cloud_lit{1.0f, 0.99f, 0.96f, 1};
    const Col cloud_shade{0.66f, 0.71f, 0.80f, 1};
    const V3 toward_sun = norm(V3{sun_.x, sun_.y, 0});
    for (int y = 0; y < r.H; y += 1) {
        const double b = (r.H * r.ay - (y + 0.5)) / r.scale;
        for (int x = 0; x < r.W; x += 1) {
            const std::size_t at = static_cast<std::size_t>(y) * static_cast<std::size_t>(r.W) + static_cast<std::size_t>(x);
            if (r.depth[at] < 1e29f) {
                continue;
            }
            const double a = (x + 0.5 - r.W * r.ax) / r.scale;
            const V3 d = norm(forward + right * a + up * b);
            const double rise = std::max(0.0, d.z);
            // pale and hazy low down, deepening overhead; warmer toward the sun
            Col colour = mix(haze_, zenith, static_cast<float>(1 - std::exp(-rise * 6.5)));
            const double glow = std::pow(std::max(0.0, dot(d, sun_)), 4);
            colour = Col{colour.r + static_cast<float>(0.14 * glow), colour.g + static_cast<float>(0.10 * glow), colour.b + static_cast<float>(0.03 * glow), 1};
            if (rise > 0.004) {
                // clouds on a high flat ceiling, crowding toward the horizon
                const double lift = 1 / (rise + 0.04);
                const double u = d.x * lift * 0.1 + 0.37;
                const double v = d.y * lift * 0.1 + 0.61;
                const double n = cloud_at(clouds_, u, v);
                const double cover = smoothstep(0.5, 0.7, n) * smoothstep(0.004, 0.06, rise);
                if (cover > 0) {
                    const double toward = cloud_at(clouds_, u + toward_sun.x * 0.01, v + toward_sun.y * 0.01);
                    const double light = std::clamp(0.62 + (n - toward) * 10, 0.2, 1.0);
                    Col cloud = mix(cloud_shade, cloud_lit, static_cast<float>(light));
                    cloud = mix(cloud, haze_, static_cast<float>(0.5 * (1 - smoothstep(0.0, 0.12, rise))));
                    colour = mix(colour, cloud, static_cast<float>(cover));
                }
            }
            float* px = r.rgb.data() + at * 3;
            px[0] = colour.r;
            px[1] = colour.g;
            px[2] = colour.b;
        }
    }
}

}  // namespace zc
