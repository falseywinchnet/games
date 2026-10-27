#include "site.hpp"

#include "platform/mesh.hpp"
#include "shapes.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>

namespace zc {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFov = 0.70;   // radians, vertical
constexpr double kRiffleX = 1.05;  // where the brook runs shallow and broken over stones

// ---------------------------------------------------------------- small helpers

double smoothstep(double a, double b, double x) {
    const double t = std::min(1.0, std::max(0.0, (x - a) / (b - a)));
    return t * t * (3 - 2 * t);
}

std::uint32_t hash2(int x, int y, std::uint32_t seed) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

double hash_unit(int x, int y, std::uint32_t seed) {
    return static_cast<double>(hash2(x, y, seed) & 0xffffu) / 65535.0;
}

// smooth value noise in [0, 1]
double value_noise(double x, double y, std::uint32_t seed) {
    const double fx = std::floor(x);
    const double fy = std::floor(y);
    const int ix = static_cast<int>(fx);
    const int iy = static_cast<int>(fy);
    const double tx = x - fx;
    const double ty = y - fy;
    const double u = tx * tx * (3 - 2 * tx);
    const double v = ty * ty * (3 - 2 * ty);
    const double a = hash_unit(ix, iy, seed);
    const double b = hash_unit(ix + 1, iy, seed);
    const double c = hash_unit(ix, iy + 1, seed);
    const double d = hash_unit(ix + 1, iy + 1, seed);
    const double top = a + (b - a) * u;
    const double bottom = c + (d - c) * u;
    return top + (bottom - top) * v;
}

std::uint32_t pack(double r, double g, double b, double a) {
    const int ri = static_cast<int>(std::lround(std::min(1.0, std::max(0.0, r)) * 255));
    const int gi = static_cast<int>(std::lround(std::min(1.0, std::max(0.0, g)) * 255));
    const int bi = static_cast<int>(std::lround(std::min(1.0, std::max(0.0, b)) * 255));
    const int ai = static_cast<int>(std::lround(std::min(1.0, std::max(0.0, a)) * 255));
    return (static_cast<std::uint32_t>(ai) << 24) | (static_cast<std::uint32_t>(ri) << 16) | (static_cast<std::uint32_t>(gi) << 8) |
           static_cast<std::uint32_t>(bi);
}

V3 to_v3(phys::Vec3 p) {
    return V3{p.x, p.y, p.z};
}

// A flat-shaded triangle in world space.
void add_triangle(std::vector<Vtx>& out, V3 a, V3 b, V3 c, Col colour, double tex_scale) {
    const V3 n = norm(cross(b - a, c - a));
    const V3 corners[3] = {a, b, c};
    for (int k = 0; k < 3; k += 1) {
        Vtx v;
        v.p = corners[k];
        v.n = n;
        v.c = colour;
        v.s = corners[k].x * tex_scale;
        v.t = corners[k].y * tex_scale;
        out.push_back(v);
    }
}

// the bedrock ledge's outline: radius at angle a round the stack's spot
double ledge_radius(double a) {
    const double wobble = 0.16 * (value_noise(std::cos(a) * 1.8 + 5, std::sin(a) * 1.8 + 5, 401u) - 0.5) +
                          0.06 * (value_noise(std::cos(a) * 5 + 9, std::sin(a) * 5 + 9, 403u) - 0.5);
    return 0.44 * (1 + wobble);
}

bool on_ledge(double x, double y, double margin) {
    const SiteLayout& layout = site_layout();
    const double dx = x - layout.stack_centre.x;
    const double dy = (y - layout.stack_centre.y) / 0.74;
    const double a = std::atan2(dy, dx);
    return std::sqrt(dx * dx + dy * dy) < ledge_radius(a) + margin;
}

// What a terrain patch is.
enum class Terrain { bank, brook_bed, far_bank };

// The bank: river gravel round the bedrock ledge, darker and dipping toward
// the brook, rising into stony slopes away from the work.
// The brook's banks wander. The near edge keeps its distance by the work
// (it may only swing out a little there); both wander freely beyond.
double brook_near_at(double x) {
    const SiteLayout& layout = site_layout();
    const double free = smoothstep(0.9, 2.0, std::abs(x));
    return layout.brook_near + 0.025 * std::sin(x * 2.1 + 0.4) + 0.07 * free * std::sin(x * 0.85 + 1.3) + 0.03 * free * std::sin(x * 2.9 + 2.2);
}

double brook_far_at(double x) {
    const SiteLayout& layout = site_layout();
    return layout.brook_far + 0.08 * std::sin(x * 0.9 + 1.7) + 0.04 * std::sin(x * 2.3 + 0.5) + 0.015 * std::sin(x * 6.1);
}

double terrain_height(Terrain kind, double x, double y) {
    const SiteLayout& layout = site_layout();
    if (kind == Terrain::bank) {
        const double fine = 0.010 * (value_noise(x * 7, y * 7, 3u) - 0.5) + 0.016 * (value_noise(x * 1.6, y * 1.6, 7u) - 0.5);
        const double edge = brook_near_at(x);
        // down to the waterline, then on under the water, below the bed
        const double to_water = -0.075 * smoothstep(edge - 0.22, edge + 0.02, y) - 0.07 * smoothstep(edge, edge + 0.14, y);
        const double dx = std::max(0.0, std::abs(x - 0.0) - 1.3);
        const double dy = std::max(0.0, -0.75 - y);
        const double away = std::sqrt(dx * dx + dy * dy);
        // the slopes round the work flatten out toward the water
        const double slopes = 0.30 * smoothstep(0.0, 1.6, away) * (0.7 + 0.6 * value_noise(x * 0.8, y * 0.8, 9u)) *
                              (1 - smoothstep(brook_near_at(x) - 0.45, brook_near_at(x) - 0.05, y));
        // under the bedrock ledge the gravel sits well below its top, so it never shows through
        const double under = on_ledge(x, y, 0.08) ? -0.03 : 0.0;
        return fine - 0.004 + to_water + slopes + under;
    }
    if (kind == Terrain::brook_bed) {
        return layout.water_level - 0.07 + 0.025 * (value_noise(x * 4, y * 4, 5u) - 0.5);
    }
    // far bank: up from the water into a grassy slope
    const double edge = brook_far_at(x);
    // out of the water in a short lip, then up the grassy slope
    const double lip = 0.05 * smoothstep(edge - 0.03, edge + 0.10, y);
    const double rise = 0.24 * smoothstep(edge, edge + 1.4, y);
    const double under = -0.09 * (1 - smoothstep(edge - 0.16, edge, y));
    return layout.water_level - 0.025 + lip + rise + under + 0.06 * (value_noise(x * 0.9, y * 0.9, 13u) - 0.5) * smoothstep(edge + 0.1, edge + 0.6, y);
}

Col terrain_colour(Terrain kind, double x, double y) {
    if (kind == Terrain::bank) {
        const float k = static_cast<float>(0.92 + 0.14 * value_noise(x * 2.2, y * 2.2, 23u));
        // wet and darker within a hand's width of the water
        const float wet = static_cast<float>(smoothstep(brook_near_at(x) - 0.16, brook_near_at(x) - 0.01, y));
        const Col dry{0.86f * k, 0.82f * k, 0.76f * k, 1};
        const Col damp{0.52f * k, 0.50f * k, 0.46f * k, 1};
        return mix(dry, damp, wet);
    }
    if (kind == Terrain::brook_bed) {
        return Col{0.46f, 0.44f, 0.36f, 1};
    }
    const float k = static_cast<float>(0.8 + 0.3 * value_noise(x * 1.1, y * 1.1, 29u));
    return Col{0.42f * k, 0.50f * k, 0.30f * k, 1};
}

// A grid of terrain over [x0, x1] x [y0, y1], skipping cells whose centre
// lies inside the hole. Each vertex carries a splat weight (`w`): where the
// cobbles show through the fine gravel.
void build_terrain(std::vector<Vtx>& out, Terrain kind, double x0, double x1, double y0, double y1, double step, double tex_scale,
                   double hole_x0, double hole_x1, double hole_y0, double hole_y1) {
    const int nx = std::max(1, static_cast<int>(std::ceil((x1 - x0) / step)));
    const int ny = std::max(1, static_cast<int>(std::ceil((y1 - y0) / step)));
    const double sx = (x1 - x0) / nx;
    const double sy = (y1 - y0) / ny;
    for (int j = 0; j < ny; j += 1) {
        for (int i = 0; i < nx; i += 1) {
            const double xa = x0 + i * sx;
            const double ya = y0 + j * sy;
            const double xb = xa + sx;
            const double yb = ya + sy;
            const double cx = 0.5 * (xa + xb);
            const double cy = 0.5 * (ya + yb);
            if (cx > hole_x0 && cx < hole_x1 && cy > hole_y0 && cy < hole_y1) {
                continue;
            }
            const double xs[4] = {xa, xb, xb, xa};
            const double ys[4] = {ya, ya, yb, yb};
            V3 p[4];
            for (int k = 0; k < 4; k += 1) {
                p[k] = V3{xs[k], ys[k], terrain_height(kind, xs[k], ys[k])};
            }
            const int order[6] = {0, 1, 2, 0, 2, 3};
            const V3 n0 = norm(cross(p[1] - p[0], p[2] - p[0]));
            const V3 n1 = norm(cross(p[2] - p[0], p[3] - p[0]));
            for (int k = 0; k < 6; k += 1) {
                const int c = order[k];
                Vtx v;
                v.p = p[c];
                v.n = k < 3 ? n0 : n1;
                v.c = terrain_colour(kind, xs[c], ys[c]);
                v.s = xs[c] * tex_scale;
                v.t = ys[c] * tex_scale;
                // cobble patches: soft noise, more of them down by the water
                const double patch = value_noise(xs[c] * 2.6, ys[c] * 2.6, 211u);
                const double near_water = smoothstep(0.2, 0.8, ys[c]);
                v.w = static_cast<float>(std::min(0.85, std::max(0.0, (patch - 0.55) * 2.0 + 0.3 * near_water)));
                out.push_back(v);
            }
        }
    }
}

// ---------------------------------------------------------------- textures

// The bowl was turned from a log lying along x below it; its growth rings are
// circles round that axis, so they cross the floor as bands and arch up the
// walls. The texture runs along the log in s and out from its pith in t.
const double kBowlPithY = 0.10;     // from the bowl's centre
const double kBowlPithZ = -0.62;
const double kBowlRingNear = 0.50;  // the ring distances t spans
const double kBowlRingFar = 0.98;

void make_bowl_wood(Tex& t) {
    const int size = 512;
    t.make(size, size);
    const double rings = 13;   // across t
    for (int y = 0; y < size; y += 1) {
        for (int x = 0; x < size; x += 1) {
            const double u = static_cast<double>(x) / size;
            const double v = static_cast<double>(y) / size;
            // the rings wander a little along the log
            const double wobble = 0.55 * value_noise(u * 3.0, v * 2.0, 311u) + 0.25 * value_noise(u * 9.0, v * 6.0, 313u);
            const double phase = v * rings + wobble;
            const double f = phase - std::floor(phase);
            // earlywood pale and open, latewood a dark band with a sharp outer edge
            const double late = smoothstep(0.55, 0.88, f) * (1 - smoothstep(0.93, 1.0, f));
            // fibres: streaks along the log
            const double fibre = value_noise(u * 6.0, v * 420.0, 317u) - 0.5;
            const double ray = hash_unit(x / 3, y, 331u) > 0.985 ? 0.05 : 0.0;   // medullary flecks
            const double pore = late > 0.3 && hash_unit(x, y, 337u) > 0.93 ? 0.10 : 0.0;
            const double tone = 1.0 - 0.17 * late + 0.06 * fibre + ray - 0.6 * pore;
            const double warm = 0.5 + 0.5 * value_noise(u * 2.0, v * 1.5, 347u);
            const double r = (0.70 + 0.05 * warm) * tone;
            const double g = (0.52 + 0.03 * warm) * tone * (1 - 0.05 * late);
            const double b = (0.36 - 0.02 * warm) * tone * (1 - 0.10 * late);
            t.at(x, y) = pack(r, g, b, 1);
        }
    }
    t.build_mips();
}

// River gravel: rounded pebbles round random sites, each its own colour, lit
// from the upper left as little domes, with dark grit in the gaps.
void make_pebbles(Tex& t, int size, int cells, std::uint32_t seed, double grit) {
    t.make(size, size);
    const double cell = static_cast<double>(size) / cells;
    const double palette[6][3] = {{0.70, 0.70, 0.68}, {0.80, 0.72, 0.60}, {0.70, 0.55, 0.45}, {0.56, 0.59, 0.62}, {0.88, 0.84, 0.76}, {0.60, 0.56, 0.50}};
    for (int y = 0; y < size; y += 1) {
        for (int x = 0; x < size; x += 1) {
            double nearest = 1e9;
            double second = 1e9;
            int owner_x = 0;
            int owner_y = 0;
            double ox = 0;
            double oy = 0;
            const int cx = static_cast<int>(x / cell);
            const int cy = static_cast<int>(y / cell);
            for (int j = -1; j <= 1; j += 1) {
                for (int i = -1; i <= 1; i += 1) {
                    const int gx = (cx + i + cells) % cells;
                    const int gy = (cy + j + cells) % cells;
                    const double px = (cx + i + 0.15 + 0.7 * hash_unit(gx, gy, seed)) * cell;
                    const double py = (cy + j + 0.15 + 0.7 * hash_unit(gx, gy, seed + 17u)) * cell;
                    const double dx = x + 0.5 - px;
                    const double dy = y + 0.5 - py;
                    const double d = std::sqrt(dx * dx + dy * dy);
                    if (d < nearest) {
                        second = nearest;
                        nearest = d;
                        owner_x = gx;
                        owner_y = gy;
                        ox = dx;
                        oy = dy;
                    } else if (d < second) {
                        second = d;
                    }
                }
            }
            const double* base = palette[hash2(owner_x, owner_y, seed + 31u) % 6u];
            const double tone = 0.86 + 0.24 * hash_unit(owner_x, owner_y, seed + 41u);
            const double edge = std::min(1.0, (second - nearest) / (cell * 0.12));
            // a dome: brighter toward the upper left, darker toward the lower right
            const double radius = std::max(1.0, 0.5 * (nearest + second));
            const double shade = 0.92 - 0.14 * (ox + oy) / (radius * 1.4) - 0.06 * (nearest / radius);
            const double speck = hash_unit(x, y, seed + 53u) < 0.05 ? 0.85 : 1.0;
            double r = base[0] * tone * shade * speck;
            double g = base[1] * tone * shade * speck;
            double b = base[2] * tone * shade * speck;
            if (edge < 1.0) {
                // the grit between pebbles
                const double k = 1 - edge;
                r = r * (1 - k) + grit * 0.55 * k;
                g = g * (1 - k) + grit * 0.5 * k;
                b = b * (1 - k) + grit * 0.44 * k;
            }
            t.at(x, y) = pack(r * 1.08, g * 1.08, b * 1.08, 1);
        }
    }
    t.build_mips();
}

// Bedrock: grey layered stone with a few dark cracks and pale lichen.
void make_bedrock(Tex& t) {
    t.make(128, 128);
    std::vector<float> crack(128 * 128, 0.f);
    for (int line = 0; line < 3; line += 1) {
        double x = 128 * hash_unit(line, 1, 301u);
        double y = 128 * hash_unit(line, 2, 301u);
        double heading = 2 * kPi * hash_unit(line, 3, 301u);
        for (int step = 0; step < 140; step += 1) {
            heading += 0.35 * (hash_unit(line, step, 303u) - 0.5);
            x += std::cos(heading);
            y += std::sin(heading);
            const int ix = (static_cast<int>(std::floor(x)) % 128 + 128) % 128;
            const int iy = (static_cast<int>(std::floor(y)) % 128 + 128) % 128;
            crack[static_cast<size_t>(iy * 128 + ix)] = 1.f;
        }
    }
    for (int y = 0; y < 128; y += 1) {
        for (int x = 0; x < 128; x += 1) {
            const double layers = 0.5 + 0.5 * std::sin((y + 6 * value_noise(x / 20.0, y / 20.0, 307u)) * 0.28);
            const double soft = value_noise(x / 9.0, y / 9.0, 309u);
            const double grain = hash_unit(x, y, 311u);
            double v = 0.78 + 0.08 * layers + 0.12 * soft + 0.05 * (grain - 0.5);
            double r = v * 0.98;
            double g = v * 0.97;
            double b = v * 0.94;
            if (crack[static_cast<size_t>(y * 128 + x)] > 0.f) {
                r *= 0.72;
                g *= 0.72;
                b *= 0.72;
            }
            const double lichen = value_noise(x / 6.0, y / 6.0, 313u);
            if (lichen > 0.82 && grain > 0.3) {
                r = r * 0.6 + 0.78 * 0.4;
                g = g * 0.6 + 0.80 * 0.4;
                b = b * 0.6 + 0.66 * 0.4;
            }
            t.at(x, y) = pack(r, g, b, 1);
        }
    }
    t.build_mips();
}

void make_grass(Tex& t) {
    t.make(64, 64);
    for (int y = 0; y < 64; y += 1) {
        for (int x = 0; x < 64; x += 1) {
            const double blade = hash_unit(x, y / 3, 107u);
            const double soft = value_noise(x / 5.0, y / 5.0, 109u);
            const double v = 0.75 + 0.3 * blade * soft + 0.1 * soft;
            t.at(x, y) = pack(v * 0.95, v, v * 0.85, 1);
        }
    }
    t.build_mips();
}

void make_wood(Tex& t) {
    t.make(64, 64);
    for (int y = 0; y < 64; y += 1) {
        for (int x = 0; x < 64; x += 1) {
            const double wobble = 3 * value_noise(x / 16.0, y / 4.0, 139u);
            const double ring = 0.5 + 0.5 * std::sin((y + wobble) * 0.9);
            const double v = 0.78 + 0.16 * ring + 0.06 * hash_unit(x, y, 149u);
            t.at(x, y) = pack(v, v * 0.92, v * 0.84, 1);
        }
    }
    t.build_mips();
}

// Water: soft long ripples along the flow; translucent.
void make_water(Tex& t) {
    t.make(64, 64);
    for (int y = 0; y < 64; y += 1) {
        for (int x = 0; x < 64; x += 1) {
            const double a = std::sin((x + 3 * std::sin(y * 0.2)) * 0.2) * std::sin((y + 2 * std::sin(x * 0.15)) * 0.39);
            const double n = value_noise(x / 9.0, y / 4.0, 151u);
            const double v = 0.88 + 0.10 * a + 0.1 * n;
            t.at(x, y) = pack(v, v, v, 0.58 + 0.2 * n);
        }
    }
    t.build_mips();
}

// Glints: sparse bright sparkles, drawn additively and scrolled faster than the water.
void make_glints(Tex& t) {
    t.make(64, 64);
    for (int y = 0; y < 64; y += 1) {
        for (int x = 0; x < 64; x += 1) {
            const double streak = value_noise(x / 3.0, y / 1.2, 157u);
            const double v = std::max(0.0, streak - 0.72) * 3.2;
            t.at(x, y) = pack(v, v, v * 0.95, 1);
        }
    }
    t.build_mips();
}

// Streaks of white water, long along the flow (x), seamless both ways so they
// can slide: for the riffle, the wakes behind stones, the lapping edges.
double streak_field(double x, double y) {
    const double n = value_noise(x / 10.0, y / 1.7, 181u);
    const double m = value_noise(x / 4.0, y / 0.9, 191u);
    return 0.65 * n + 0.45 * m;
}

void make_streaks(Tex& t) {
    t.make(64, 64);
    for (int y = 0; y < 64; y += 1) {
        for (int x = 0; x < 64; x += 1) {
            const double ax = x / 64.0;
            const double ay = y / 64.0;
            const double f = streak_field(x, y) * (1 - ax) * (1 - ay) + streak_field(x - 64, y) * ax * (1 - ay) +
                             streak_field(x, y - 64) * (1 - ax) * ay + streak_field(x - 64, y - 64) * ax * ay;
            const double a = std::min(1.0, std::max(0.0, (f - 0.52) * 3.2));
            t.at(x, y) = pack(1, 1, 1, a);
        }
    }
    t.build_mips();
}

// A soft round glow for lamps, white, falling off to nothing.
void make_glow(Tex& t) {
    t.make(32, 32);
    for (int y = 0; y < 32; y += 1) {
        for (int x = 0; x < 32; x += 1) {
            const double dx = (x + 0.5 - 16) / 16;
            const double dy = (y + 0.5 - 16) / 16;
            const double r = std::sqrt(dx * dx + dy * dy);
            const double v = std::max(0.0, 1 - r);
            const double glow = v * v * (0.6 + 0.4 * v);
            t.at(x, y) = pack(glow, glow, glow, 1);
        }
    }
    t.build_mips();
}

// Foam: a ragged white ring.
void make_foam(Tex& t) {
    t.make(64, 64);
    for (int y = 0; y < 64; y += 1) {
        for (int x = 0; x < 64; x += 1) {
            const double dx = (x + 0.5) / 32.0 - 1;
            const double dy = (y + 0.5) / 32.0 - 1;
            const double r = std::sqrt(dx * dx + dy * dy);
            const double ring = std::max(0.0, 1 - std::abs(r - 0.68) / 0.22);
            const double rag = value_noise(x / 4.0, y / 4.0, 163u);
            const double a = std::min(1.0, ring * (0.4 + 0.9 * rag));
            t.at(x, y) = pack(1, 1, 1, a * 0.85);
        }
    }
    t.build_mips();
}

void make_splat(Tex& t) {
    t.make(64, 64);
    for (int y = 0; y < 64; y += 1) {
        for (int x = 0; x < 64; x += 1) {
            const double v = 0.15 + 0.7 * value_noise(x / 3.0, y / 3.0, 167u);
            t.at(x, y) = pack(v, v, v, 1);
        }
    }
}

// ---------------------------------------------------------------- floaters on the brook

struct Floater {
    double x, y, z, yaw;
    bool twig;
    double length;
    Col colour;
};

// Where the leaves and the odd twig are at time t: carried along +x by the
// current, bobbing and turning slowly. Deterministic in t, so no state.
void floaters_at(double time, std::vector<Floater>& out) {
    const SiteLayout& layout = site_layout();
    out.clear();
    const double span = 8.0;
    const int leaves = 12;
    for (int i = 0; i < leaves + 3; i += 1) {
        const bool twig = i >= leaves;
        const double speed = (twig ? 0.07 : 0.09) + 0.05 * hash_unit(i, 1, 157u);
        const double phase = span * hash_unit(i, 2, 157u);
        // twigs drift past only now and then: most of their cycle is spent out of sight
        const double travel = twig ? span * 3 : span;
        const double x = std::fmod(phase + time * speed, travel) - span * 0.5;
        if (x > span * 0.5) {
            continue;
        }
        const double width = brook_far_at(x) - brook_near_at(x);
        const double lane = brook_near_at(x) + width * (0.12 + 0.76 * hash_unit(i, 3, 157u));
        Floater f;
        f.x = x;
        f.y = lane + 0.03 * std::sin(time * 0.7 + i);
        f.z = layout.water_level + 0.003 + 0.002 * std::sin(time * 2.1 + i * 1.7);
        f.yaw = hash_unit(i, 4, 157u) * 2 * kPi + time * (0.25 * (hash_unit(i, 5, 157u) - 0.5));
        f.twig = twig;
        f.length = twig ? 0.12 + 0.12 * hash_unit(i, 6, 157u) : 0.035 + 0.02 * hash_unit(i, 6, 157u);
        const double hue = hash_unit(i, 7, 157u);
        if (hue < 0.4) {
            f.colour = Col{0.86f, 0.55f, 0.18f, 1};   // amber
        } else if (hue < 0.65) {
            f.colour = Col{0.72f, 0.28f, 0.16f, 1};   // rust red
        } else if (hue < 0.85) {
            f.colour = Col{0.80f, 0.70f, 0.26f, 1};   // yellow
        } else {
            f.colour = Col{0.42f, 0.55f, 0.24f, 1};   // still green
        }
        if (twig) {
            f.colour = Col{0.42f, 0.32f, 0.22f, 1};
        }
        out.push_back(f);
    }
}

// the crane's paint

}  // namespace

M34 pose_matrix(const phys::Pose& pose) {
    const phys::Quat q = pose.q;
    M34 m;
    m.m[0] = 1 - 2 * (q.y * q.y + q.z * q.z);
    m.m[1] = 2 * (q.x * q.y - q.w * q.z);
    m.m[2] = 2 * (q.x * q.z + q.w * q.y);
    m.m[3] = pose.p.x;
    m.m[4] = 2 * (q.x * q.y + q.w * q.z);
    m.m[5] = 1 - 2 * (q.x * q.x + q.z * q.z);
    m.m[6] = 2 * (q.y * q.z - q.w * q.x);
    m.m[7] = pose.p.y;
    m.m[8] = 2 * (q.x * q.z - q.w * q.y);
    m.m[9] = 2 * (q.y * q.z + q.w * q.x);
    m.m[10] = 1 - 2 * (q.x * q.x + q.y * q.y);
    m.m[11] = pose.p.z;
    return m;
}

Site::Site() {
    build_textures();
    build_scenery();
    place_crane();
    r.light.sun = norm(V3{-0.38, -0.52, 0.76});
    r.light.sun_col = Col{0.88f, 0.82f, 0.70f, 1};
    r.light.amb_col = Col{0.50f, 0.56f, 0.66f, 1};
    r.light.amb_ground = Col{0.50f, 0.47f, 0.42f, 1};
    r.light.hemisphere = true;
    r.light.fog_col = Col{0.80f, 0.86f, 0.90f, 1};
    r.light.fog_near = 3.5;
    r.light.fog_far = 9.0;
    r.shadow_darkness = 0.52f;
}

void Site::build_textures() {
    make_pebbles(gravel_tex_, 128, 30, 701u, 0.62);
    make_pebbles(cobble_tex_, 128, 14, 709u, 0.58);
    make_splat(splat_tex_);
    make_bedrock(bedrock_tex_);
    make_grass(grass_tex_);
    make_wood(wood_tex_);
    make_bowl_wood(bowl_tex_);
    make_water(water_tex_);
    make_glints(glint_tex_);
    make_foam(foam_tex_);
    make_glow(glow_tex_);
    make_streaks(streak_tex_);
    sign_tex_.make(4, 4);
}

void Site::build_scenery() {
    const SiteLayout& layout = site_layout();
    const M34 world;
    // the gravel bank: fine near the work, coarse farther out
    const double fx0 = -1.5;
    const double fx1 = 1.5;
    const double fy0 = -1.1;
    const double fy1 = layout.brook_near + 0.26;
    build_terrain(bank_, Terrain::bank, fx0, fx1, fy0, fy1, 0.05, 5.0, 1e9, -1e9, 1e9, -1e9);
    build_terrain(bank_, Terrain::bank, -4.5, 4.5, -3.4, fy1, 0.2, 5.0, fx0, fx1, fy0, fy1);
    build_terrain(brook_bed_, Terrain::brook_bed, -4.5, 4.5, layout.brook_near - 0.12, layout.brook_far + 0.18, 0.1, 2.6, 1e9, -1e9, 1e9, -1e9);
    build_terrain(far_bank_, Terrain::far_bank, -4.5, 4.5, layout.brook_far - 0.3, 4.8, 0.12, 2.0, 1e9, -1e9, 1e9, -1e9);
    for (size_t k = 0; k < brook_bed_.size(); k += 1) {
        brook_bed_[k].w = 0.8f;   // the brook's bed is cobbles
    }
    // the far bank's foot is wet gravel and mud, not grass
    for (size_t k = 0; k < far_bank_.size(); k += 1) {
        const double edge = brook_far_at(far_bank_[k].p.x);
        const double shore = 1 - smoothstep(edge + 0.04, edge + 0.20, far_bank_[k].p.y);
        far_bank_[k].w = static_cast<float>(shore);
        far_bank_[k].c = mix(far_bank_[k].c, Col{0.50f, 0.46f, 0.38f, 1}, static_cast<float>(shore));
    }
    // the bedrock ledge under the stack: a flat top at the ground's level, and a broken edge
    {
        const int around = 48;
        const phys::Vec3 c = layout.stack_centre;
        std::vector<V3> rim;
        std::vector<V3> foot;
        for (int k = 0; k < around; k += 1) {
            const double a = 2 * kPi * k / around;
            const double radius = ledge_radius(a);
            rim.push_back(V3{c.x + radius * std::cos(a), c.y + radius * std::sin(a) * 0.74, 0.0});
            const double out = radius + 0.035 + 0.02 * hash_unit(k, 1, 409u);
            foot.push_back(V3{c.x + out * std::cos(a), c.y + out * std::sin(a) * 0.74, -0.045});
        }
        const V3 middle{c.x, c.y, 0.0};
        for (int k = 0; k < around; k += 1) {
            const int next = (k + 1) % around;
            const float tone = static_cast<float>(0.92 + 0.12 * hash_unit(k, 2, 409u));
            const size_t first_top = ledge_.size();
            add_triangle(ledge_, middle, rim[static_cast<size_t>(k)], rim[static_cast<size_t>(next)], Col{0.70f, 0.67f, 0.62f, 1}, 1.4);
            // the top's tone varies smoothly over the stone, not by slice
            for (size_t v = first_top; v < ledge_.size(); v += 1) {
                const float soft = static_cast<float>(0.9 + 0.16 * value_noise(ledge_[v].p.x * 4, ledge_[v].p.y * 4, 411u));
                ledge_[v].c = Col{0.70f * soft, 0.67f * soft, 0.62f * soft, 1};
            }
            const Col side{0.50f * tone, 0.48f * tone, 0.45f * tone, 1};
            add_triangle(ledge_, rim[static_cast<size_t>(k)], foot[static_cast<size_t>(k)], foot[static_cast<size_t>(next)], side, 1.4);
            add_triangle(ledge_, rim[static_cast<size_t>(k)], foot[static_cast<size_t>(next)], rim[static_cast<size_t>(next)], side, 1.4);
        }
        // the top is flat: give it straight-up normals so it lights evenly
        for (size_t v = 0; v < ledge_.size(); v += 1) {
            if (ledge_[v].p.z > -1e-9 && ledge_[v].n.z > 0.99) {
                ledge_[v].n = V3{0, 0, 1};
            }
        }
    }
    // stones lying about on the bank
    for (int i = 0; i < 150; i += 1) {
        const double x = -1.45 + 2.9 * hash_unit(i, 1, 421u);
        const double y = -1.05 + (brook_near_at(x) + 0.05 + 1.05) * hash_unit(i, 2, 421u);
        if (on_ledge(x, y, 0.05)) {
            continue;
        }
        const double to_bowl = std::hypot(x - layout.bowl_centre.x, y - layout.bowl_centre.y);
        if (to_bowl < layout.bowl_rim_radius + layout.bowl_wall + 0.05) {
            continue;
        }
        const double to_crane = std::hypot((x - layout.crane_base.x) / 0.36, (y - layout.crane_base.y) / 0.26);
        if (to_crane < 1.0) {
            continue;
        }
        const double size = 0.01 + 0.035 * std::pow(hash_unit(i, 3, 421u), 2.4);
        const Mesh lump = rock_mesh(1200u + static_cast<std::uint64_t>(i), 8, 6, 0.35);
        const double z = terrain_height(Terrain::bank, x, y);
        const M34 frame = M34::translate(x, y, z + size * 0.15) * M34::rot_z(hash_unit(i, 4, 421u) * 6) *
                          M34::scale(size, size * (0.6 + 0.4 * hash_unit(i, 5, 421u)), size * 0.5);
        const double* tint = nullptr;
        const double tints[4][3] = {{0.62, 0.60, 0.56}, {0.68, 0.58, 0.48}, {0.52, 0.53, 0.55}, {0.60, 0.48, 0.42}};
        tint = tints[hash2(i, 6, 421u) % 4u];
        const bool wet = y > brook_near_at(x) - 0.15;
        const float damp = wet ? 0.68f : 1.0f;
        add_mesh(cobbles_, lump, frame, Col{static_cast<float>(tint[0]) * damp, static_cast<float>(tint[1]) * damp, static_cast<float>(tint[2]) * damp, 1});
    }
    // the boulder the crane is perched on
    {
        const Mesh lump = rock_mesh(1999u, 18, 12, 0.12);
        const M34 frame = M34::translate(layout.crane_base.x, layout.crane_base.y, -0.03) * M34::rot_z(0.4) *
                          M34::scale(0.32, 0.22, layout.crane_boulder_top + 0.04);
        add_mesh(boulder_, lump, frame, Col{0.44f, 0.42f, 0.39f, 1});
    }
    // stones out in the brook, the water breaking round them: scattered
    // boulders, some flat and barely awash, and a line of them across the
    // stream where it runs shallow over a riffle
    for (int i = 0; i < 24; i += 1) {
        const bool riffle = i >= 16;
        double x = -3.6 + 7.2 * hash_unit(i, 1, 431u);
        if (riffle) {
            x = kRiffleX - 0.12 + 0.24 * hash_unit(i, 1, 433u);
        }
        const double near = brook_near_at(x);
        const double width = brook_far_at(x) - near;
        const double y = near + width * (riffle ? (static_cast<double>(i - 16) + 0.5 + 0.4 * (hash_unit(i, 2, 431u) - 0.5)) / 8.0
                                                 : 0.15 + 0.7 * hash_unit(i, 2, 431u));
        const double size = riffle ? 0.03 + 0.04 * hash_unit(i, 3, 431u) : 0.05 + 0.10 * std::pow(hash_unit(i, 3, 431u), 1.5);
        const bool flat = hash_unit(i, 7, 431u) < 0.4;
        const double height = flat ? size * 0.32 : size * 0.6;
        const double sink = riffle ? 0.55 : (flat ? 0.75 : 0.35 + 0.3 * hash_unit(i, 8, 431u));
        const Mesh lump = rock_mesh(1500u + static_cast<std::uint64_t>(i), 10, 7, 0.28);
        const M34 frame = M34::translate(x, y, layout.water_level + height * (1 - sink) - height * 0.5) * M34::rot_z(hash_unit(i, 4, 431u) * 6) *
                          M34::scale(size, size * (0.65 + 0.3 * hash_unit(i, 5, 431u)), height);
        const float k = static_cast<float>(0.85 + 0.25 * hash_unit(i, 6, 431u));
        add_mesh(stream_stones_, lump, frame, Col{0.50f * k, 0.50f * k, 0.46f * k, 1});
        stream_stone_spots_.push_back(V3{x, y, size});
    }
    // the water's surface: clear over the shallows by each bank, deeper and
    // greener out in the stream (the banks hide its edges)
    {
        const int rows = 10;
        for (int i = 0; i < 90; i += 1) {
            const double xa = -4.5 + i * 0.1;
            const double xb = xa + 0.1;
            for (int j = 0; j < rows; j += 1) {
                // each column runs from just under the near bank to just under the far one
                const double fa = static_cast<double>(j) / rows;
                const double fb = static_cast<double>(j + 1) / rows;
                const double na = brook_near_at(xa) - 0.10;
                const double nb = brook_near_at(xb) - 0.10;
                const double wa = brook_far_at(xa) + 0.04 - na;
                const double wb = brook_far_at(xb) + 0.04 - nb;
                const V3 p[4] = {V3{xa, na + wa * fa, layout.water_level}, V3{xb, nb + wb * fa, layout.water_level},
                                 V3{xb, nb + wb * fb, layout.water_level}, V3{xa, na + wa * fb, layout.water_level}};
                const int order[6] = {0, 1, 2, 0, 2, 3};
                for (int k = 0; k < 6; k += 1) {
                    Vtx v;
                    v.p = p[order[k]];
                    v.n = V3{0, 0, 1};
                    const double near = brook_near_at(v.p.x);
                    const double far = brook_far_at(v.p.x);
                    const double across = std::min(v.p.y - near, far - v.p.y);
                    const float depth = static_cast<float>(smoothstep(-0.02, 0.22, across));
                    const Col shallow{0.66f, 0.78f, 0.74f, 0.38f};
                    const Col deep{0.26f, 0.41f, 0.42f, 0.92f};
                    v.c = mix(shallow, deep, depth);
                    water_.push_back(v);
                }
            }
        }
    }
    // the lapping line along each bank: a ribbon of broken light at the water's edge
    for (int side = 0; side < 2; side += 1) {
        for (int i = 0; i < 180; i += 1) {
            const double xa = -4.5 + i * 0.05;
            const double xb = xa + 0.05;
            const double ea = side == 0 ? brook_near_at(xa) : brook_far_at(xa);
            const double eb = side == 0 ? brook_near_at(xb) : brook_far_at(xb);
            const double out = side == 0 ? 0.035 : -0.035;
            const V3 p[4] = {V3{xa, ea - out * 0.3, layout.water_level + 0.001}, V3{xb, eb - out * 0.3, layout.water_level + 0.001},
                             V3{xb, eb + out, layout.water_level + 0.001}, V3{xa, ea + out, layout.water_level + 0.001}};
            const double t[4] = {0, 0, 1, 1};
            const int order[6] = {0, 1, 2, 0, 2, 3};
            for (int k = 0; k < 6; k += 1) {
                Vtx v;
                v.p = p[order[k]];
                v.n = V3{0, 0, 1};
                v.c = Col{1, 1, 1, t[order[k]] < 0.5 ? 0.55f : 0.0f};
                v.t = t[order[k]];
                v.s = v.p.x * 3.0;
                v.w = static_cast<float>(side);
                lap_.push_back(v);
            }
        }
    }
    // reeds along the far edge and in the shallows; shrubs and trees beyond
    for (int i = 0; i < 90; i += 1) {
        const double x = -4.2 + 8.4 * hash_unit(i, 1, 163u);
        const bool far_side = hash_unit(i, 2, 163u) < 0.7;
        const double y = far_side ? brook_far_at(x) - 0.02 + 0.14 * hash_unit(i, 3, 163u) : brook_near_at(x) - 0.02 + 0.05 * hash_unit(i, 3, 163u);
        if (!far_side && std::abs(x - 0.0) < 1.2) {
            continue;   // keep the near bank by the work clear
        }
        const double height = 0.16 + 0.26 * hash_unit(i, 4, 163u);
        const double lean = 0.05 * (hash_unit(i, 5, 163u) - 0.5);
        const double base = far_side ? terrain_height(Terrain::far_bank, x, y) : terrain_height(Terrain::bank, x, y);
        const Col reed{0.48f + 0.16f * static_cast<float>(hash_unit(i, 6, 163u)), 0.58f, 0.30f, 1};
        add_triangle(reeds_, V3{x - 0.006, y, base}, V3{x + lean, y, base + height}, V3{x + 0.006, y, base}, reed, 1);
        add_triangle(reeds_, V3{x, y - 0.006, base}, V3{x + lean, y + 0.01, base + height}, V3{x, y + 0.006, base}, reed, 1);
    }
    for (int i = 0; i < 34; i += 1) {
        const double x = -4.0 + 8.0 * hash_unit(i, 1, 177u);
        const double y = brook_far_at(x) + 0.5 + 2.4 * hash_unit(i, 2, 177u);
        const double base = terrain_height(Terrain::far_bank, x, y);
        const bool tree = hash_unit(i, 3, 177u) < 0.5;
        if (tree) {
            const double height = 0.9 + 0.8 * hash_unit(i, 4, 177u);
            add_mesh(trees_, cylinder_mesh(7), M34::translate(x, y, base) * M34::scale(0.035, 0.035, height * 0.6), Col{0.42f, 0.32f, 0.24f, 1});
            for (int k = 0; k < 4; k += 1) {
                const double ox = 0.18 * (hash_unit(i, 10 + k, 177u) - 0.5);
                const double oy = 0.18 * (hash_unit(i, 20 + k, 177u) - 0.5);
                const double rr = 0.22 + 0.12 * hash_unit(i, 30 + k, 177u);
                const float g = static_cast<float>(0.8 + 0.3 * hash_unit(i, 40 + k, 177u));
                add_mesh(trees_, sphere_mesh(9, 6), M34::translate(x + ox, y + oy, base + height * (0.55 + 0.12 * k)) * M34::scale(rr, rr, rr * 0.85),
                         Col{0.30f * g, 0.46f * g, 0.22f * g, 1});
            }
        } else {
            const double rr = 0.12 + 0.1 * hash_unit(i, 5, 177u);
            add_mesh(trees_, sphere_mesh(8, 5), M34::translate(x, y, base + rr * 0.4) * M34::scale(rr * 1.3, rr, rr * 0.8), Col{0.36f, 0.50f, 0.24f, 1});
        }
    }
    // the bowl: a turned wooden bowl, lathed from a profile (radius, height)
    {
        const double rf = layout.bowl_floor_radius;
        const double rr = layout.bowl_rim_radius;
        const double w = layout.bowl_wall;
        const double h = layout.bowl_height;
        const double f = layout.bowl_floor;
        // corners of the profile, and how many pieces each run between them is cut into
        const double corners[8][2] = {{0.0, f}, {rf * 0.85, f}, {rf + 0.03, f + 0.012}, {rr, h}, {rr + w * 0.5, h + 0.006}, {rr + w, h}, {rf + w, 0.012}, {rf + w - 0.02, 0.0}};
        const int pieces[7] = {5, 2, 7, 2, 2, 7, 2};
        std::vector<double> pr, pz;
        std::vector<int> inside;   // the profile point is on the inner face
        for (int c = 0; c < 7; c += 1) {
            for (int p = 0; p < pieces[c]; p += 1) {
                const double t = static_cast<double>(p) / pieces[c];
                pr.push_back(corners[c][0] + t * (corners[c + 1][0] - corners[c][0]));
                pz.push_back(corners[c][1] + t * (corners[c + 1][1] - corners[c][1]));
                inside.push_back(c < 3 ? 1 : 0);
            }
        }
        pr.push_back(corners[7][0]);
        pz.push_back(corners[7][1]);
        inside.push_back(0);
        const int slices = 64;
        const phys::Vec3 c = layout.bowl_centre;
        for (int k = 0; k < slices; k += 1) {
            const double a0 = 2 * kPi * k / slices;
            const double a1 = 2 * kPi * (k + 1) / slices;
            for (size_t p = 0; p + 1 < pr.size(); p += 1) {
                const V3 q00{c.x + pr[p] * std::cos(a0), c.y + pr[p] * std::sin(a0), pz[p]};
                const V3 q01{c.x + pr[p] * std::cos(a1), c.y + pr[p] * std::sin(a1), pz[p]};
                const V3 q10{c.x + pr[p + 1] * std::cos(a0), c.y + pr[p + 1] * std::sin(a0), pz[p + 1]};
                const V3 q11{c.x + pr[p + 1] * std::cos(a1), c.y + pr[p + 1] * std::sin(a1), pz[p + 1]};
                // the inside a touch darker, the oiled rim a touch lighter
                const float shade = inside[p] ? 0.88f : 1.0f;
                const Col wood{shade, shade, shade, 1};
                if (pr[p] > 1e-6) {
                    add_triangle(bowl_, q00, q10, q11, wood, 1);
                }
                add_triangle(bowl_, q00, q11, q01, wood, 1);
            }
        }
        // the grain: along the log in s, out from the pith in t
        for (size_t v = 0; v < bowl_.size(); v += 1) {
            const double dy = bowl_[v].p.y - c.y - kBowlPithY;
            const double dz = bowl_[v].p.z - kBowlPithZ;
            bowl_[v].s = (bowl_[v].p.x - c.x) * 0.95 + 0.5;
            bowl_[v].t = (std::sqrt(dy * dy + dz * dz) - kBowlRingNear) / (kBowlRingFar - kBowlRingNear);
        }
        // smooth normals round the lathe: radial plus the profile's slope
        for (size_t v = 0; v < bowl_.size(); v += 1) {
            const double dx = bowl_[v].p.x - c.x;
            const double dy = bowl_[v].p.y - c.y;
            const double radial = std::hypot(dx, dy);
            if (radial < 1e-6) {
                continue;
            }
            const V3 outward{dx / radial, dy / radial, 0};
            const V3 face = bowl_[v].n;
            const double along = dot(face, outward);
            bowl_[v].n = norm(outward * along + V3{0, 0, face.z});
        }
    }
    // traffic cones set out on the bank, one knocked over
    {
        const double spots[3][4] = {{0.135, -0.555, 0.0, 0.0}, {0.215, -0.500, 0.0, 0.0}, {-0.330, -0.560, 1.0, 2.2}};
        for (int k = 0; k < 3; k += 1) {
            const double x = spots[k][0];
            const double y = spots[k][1];
            const double ground = terrain_height(Terrain::bank, x, y);
            M34 place = M34::translate(x, y, ground) * M34::rot_z(spots[k][3]);
            if (spots[k][2] > 0) {
                // lying on its side, base toward the crane
                place = M34::translate(x, y, ground + 0.016) * M34::rot_z(spots[k][3]) * M34::rot_y(kPi / 2 - 0.25) * M34::translate(0, 0, -0.030);
            }
            add_box(props_, place, V3{-0.022, -0.022, 0.0}, V3{0.022, 0.022, 0.005}, Col{0.12f, 0.12f, 0.13f, 1});
            const double r0[4] = {0.016, 0.0125, 0.0095, 0.0062};
            const double z0[4] = {0.005, 0.022, 0.038, 0.054};
            for (int band = 0; band < 3; band += 1) {
                const double r[2] = {r0[band], r0[band + 1]};
                const double z[2] = {z0[band], z0[band + 1]};
                const Col tone = band == 1 ? Col{0.97f, 0.97f, 0.95f, 1} : Col{1.0f, 0.40f, 0.06f, 1};
                add_lathe(props_, place, r, z, 2, 14, tone);
            }
            const double tr[3] = {0.0062, 0.0052, 0.0};
            const double tz[3] = {0.054, 0.058, 0.058};
            add_lathe(props_, place, tr, tz, 3, 14, Col{1.0f, 0.40f, 0.06f, 1});
        }
    }
    // the worksite sign's post, driven into the gravel by the ledge
    {
        const Col post{0.58f, 0.46f, 0.32f, 1};
        add_box(sign_post_, world, V3{0.745, -0.215, -0.03}, V3{0.76, -0.2, 0.17}, post);
    }
}

void Site::resize(int width, int height) {
    r.resize(width, height);
    cache_valid_ = false;
    set_camera(camera_);
}

void Site::set_camera(const OrbitCamera& camera) {
    camera_ = camera;
    r.yaw = camera.yaw;
    r.pitch = camera.pitch;
    r.persp = camera.distance;
    r.scale = (r.H * 0.5) / (camera.distance * std::tan(kFov * 0.5));
    r.ax = 0.5;
    r.ay = 0.52;
    r.target = to_v3(camera.target);
    r.light.focus = r.target;
    r.set_camera();
}

void Site::set_sign(const Tex& board) {
    sign_tex_ = board;
    sign_tex_.build_mips();
    cache_valid_ = false;
}

void Site::ray(double sx, double sy, phys::Vec3& origin, phys::Vec3& direction) const {
    const V3 right = r.right();
    const V3 up = r.up();
    const V3 forward = r.fwd();
    const V3 eye = r.target - forward * r.persp;
    const V3 on_plane = r.target + right * ((sx - r.W * r.ax) / r.scale) + up * ((r.H * r.ay - sy) / r.scale);
    const V3 d = norm(on_plane - eye);
    origin = phys::Vec3{eye.x, eye.y, eye.z};
    direction = phys::Vec3{d.x, d.y, d.z};
}

bool Site::project(phys::Vec3 p, double& sx, double& sy) const {
    double sz = 0;
    r.project(to_v3(p), sx, sy, sz);
    return sz > -r.persp + 0.05;
}

// ---------------------------------------------------------------- drawing

void Site::draw_sky() {
    // a soft gradient: pale at the horizon, bluer above
    for (int y = 0; y < r.H; y += 1) {
        const double t = static_cast<double>(y) / std::max(1, r.H - 1);
        const float k = static_cast<float>(t);
        const Col top{0.56f, 0.72f, 0.88f, 1};
        const Col low{0.86f, 0.89f, 0.88f, 1};
        const Col c = mix(top, low, std::min(1.f, k * 1.6f));
        float* row = r.rgb.data() + static_cast<size_t>(y) * static_cast<size_t>(r.W) * 3;
        for (int x = 0; x < r.W; x += 1) {
            row[x * 3] = c.r;
            row[x * 3 + 1] = c.g;
            row[x * 3 + 2] = c.b;
        }
    }
}

void Site::draw_scenery() {
    r.draw(far_bank_.data(), far_bank_.size(), &grass_tex_, opaque, nullptr, &gravel_tex_, &splat_tex_);
    r.draw(trees_.data(), trees_.size(), &stone_grain(), opaque);
    r.draw(brook_bed_.data(), brook_bed_.size(), &gravel_tex_, opaque, nullptr, &cobble_tex_, &splat_tex_);
    r.draw(bank_.data(), bank_.size(), &gravel_tex_, opaque, nullptr, &cobble_tex_, &splat_tex_);
    r.draw(ledge_.data(), ledge_.size(), &bedrock_tex_, opaque);
    r.draw(cobbles_.data(), cobbles_.size(), &stone_grain(), opaque);
    r.draw(stream_stones_.data(), stream_stones_.size(), &stone_grain(), opaque);
    r.draw(reeds_.data(), reeds_.size(), nullptr, static_cast<std::uint16_t>(double_sided));
    r.light.gloss_power = 24;
    r.light.gloss_strength = 0.35;
    r.draw(props_.data(), props_.size(), nullptr, gloss);
}

void Site::draw_brook(double time) {
    // the water: its ripples flow along +x
    scratch_ = water_;
    const double flow = time * 0.16;
    for (size_t k = 0; k < scratch_.size(); k += 1) {
        scratch_[k].s = scratch_[k].p.x * 1.4 - flow * 1.4;
        scratch_[k].t = scratch_[k].p.y * 1.8 + 0.06 * std::sin(time * 0.6 + scratch_[k].p.x);
    }
    // the surface carries the sky's light rather than the sun's shading
    r.draw(scratch_.data(), scratch_.size(), &water_tex_, static_cast<std::uint16_t>(translucent | double_sided | unlit));
    // glints, sliding faster than the ripples
    for (size_t k = 0; k < scratch_.size(); k += 1) {
        scratch_[k].p.z = scratch_[k].p.z + 0.0008;
        scratch_[k].s = scratch_[k].p.x * 2.2 - time * 0.55;
        scratch_[k].t = scratch_[k].p.y * 3.0 + 0.3 * std::sin(time * 0.9 + scratch_[k].p.x * 2);
        scratch_[k].c = Col{0.55f, 0.55f, 0.52f, 1};
    }
    r.draw(scratch_.data(), scratch_.size(), &glint_tex_, static_cast<std::uint16_t>(additive | double_sided | unlit | no_depth_write));
    // foam round the stones in the stream, and a short trail downstream
    scratch_.clear();
    const SiteLayout& layout = site_layout();
    for (size_t i = 0; i < stream_stone_spots_.size(); i += 1) {
        const V3 spot = stream_stone_spots_[i];
        const double pulse = 1 + 0.08 * std::sin(time * 3.1 + i * 1.3);
        const double rx = spot.z * 1.25 * pulse;
        const double ry = spot.z * 0.95 * pulse;
        const double z = layout.water_level + 0.002;
        const double spin = time * 0.4 + i;
        const double c = std::cos(spin);
        const double sn = std::sin(spin);
        const double corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int k = 0; k < 6; k += 1) {
            const double u = corners[order[k]][0];
            const double v = corners[order[k]][1];
            Vtx q;
            q.p = V3{spot.x + (u * c - v * sn) * rx, spot.y + (u * sn + v * c) * ry, z};
            q.n = V3{0, 0, 1};
            q.c = Col{1, 1, 1, 1};
            q.s = (u + 1) * 0.5;
            q.t = (v + 1) * 0.5;
            scratch_.push_back(q);
        }
    }
    r.draw(scratch_.data(), scratch_.size(), &foam_tex_, static_cast<std::uint16_t>(translucent | double_sided | unlit | no_depth_write));
    // the wakes: a V of broken water trailing downstream of each stone
    scratch_.clear();
    for (size_t i = 0; i < stream_stone_spots_.size(); i += 1) {
        const V3 spot = stream_stone_spots_[i];
        const double size = spot.z;
        const double z = layout.water_level + 0.0025;
        for (int arm = -1; arm <= 1; arm += 2) {
            const double length = 3.5 * size;
            const V3 a0{spot.x + 0.4 * size, spot.y + arm * 0.45 * size, z};
            const V3 a1{spot.x + 0.4 * size, spot.y + arm * 0.75 * size, z};
            const V3 b0{spot.x + 0.4 * size + length, spot.y + arm * 1.3 * size, z};
            const V3 b1{spot.x + 0.4 * size + length, spot.y + arm * 1.9 * size, z};
            const V3 corners[4] = {a0, b0, b1, a1};
            const float alpha[4] = {0.8f, 0.0f, 0.0f, 0.8f};
            const int order[6] = {0, 1, 2, 0, 2, 3};
            for (int k = 0; k < 6; k += 1) {
                Vtx q;
                q.p = corners[order[k]];
                q.n = V3{0, 0, 1};
                q.c = Col{1, 1, 1, alpha[order[k]]};
                q.s = (q.p.x - spot.x) / size * 0.6 - time * 1.6;
                q.t = (q.p.y - spot.y) / size * 0.4 + i * 0.37;
                scratch_.push_back(q);
            }
        }
    }
    r.draw(scratch_.data(), scratch_.size(), &streak_tex_, static_cast<std::uint16_t>(translucent | double_sided | unlit | no_depth_write));
    // the riffle: the stream breaking white over the shallow stones, two layers
    // of streaks sliding at different speeds
    scratch_.clear();
    for (int layer = 0; layer < 2; layer += 1) {
        const double speed = layer == 0 ? 0.42 : 0.68;
        for (int i = 0; i < 6; i += 1) {
            const double xa = kRiffleX - 0.16 + i * 0.09;
            const double xb = xa + 0.09;
            const float fa = static_cast<float>(std::sin(kPi * i / 6.0));
            const float fb = static_cast<float>(std::sin(kPi * (i + 1) / 6.0));
            const double na = brook_near_at(xa) + 0.03;
            const double nb = brook_near_at(xb) + 0.03;
            const double ra = brook_far_at(xa) - 0.03;
            const double rb = brook_far_at(xb) - 0.03;
            const double z = layout.water_level + 0.003 + 0.001 * layer;
            const V3 corners[4] = {V3{xa, na, z}, V3{xb, nb, z}, V3{xb, rb, z}, V3{xa, ra, z}};
            const float alpha[4] = {fa, fb, fb, fa};
            const int order[6] = {0, 1, 2, 0, 2, 3};
            for (int k = 0; k < 6; k += 1) {
                Vtx q;
                q.p = corners[order[k]];
                q.n = V3{0, 0, 1};
                q.c = Col{1, 1, 1, alpha[order[k]] * (layer == 0 ? 0.75f : 0.5f)};
                q.s = q.p.x * (layer == 0 ? 4.0 : 6.0) - time * speed * (layer == 0 ? 4.0 : 6.0);
                q.t = q.p.y * (layer == 0 ? 7.0 : 11.0) + layer * 0.5;
                scratch_.push_back(q);
            }
        }
    }
    r.draw(scratch_.data(), scratch_.size(), &streak_tex_, static_cast<std::uint16_t>(translucent | double_sided | unlit | no_depth_write));
    // the banks' lapping edge, coming and going
    scratch_ = lap_;
    for (size_t k = 0; k < scratch_.size(); k += 1) {
        scratch_[k].s = scratch_[k].p.x * 3.0 + 0.12 * std::sin(time * 0.9 + scratch_[k].p.x * 1.7) - time * 0.05;
        scratch_[k].t = scratch_[k].t * 0.6 + scratch_[k].w * 0.5;
    }
    r.draw(scratch_.data(), scratch_.size(), &streak_tex_, static_cast<std::uint16_t>(translucent | double_sided | unlit | no_depth_write));
    // leaves and twigs riding on it
    std::vector<Floater> floaters;
    floaters_at(time, floaters);
    scratch_.clear();
    for (size_t i = 0; i < floaters.size(); i += 1) {
        const Floater& f = floaters[i];
        const double c = std::cos(f.yaw);
        const double sn = std::sin(f.yaw);
        const double half = f.length * 0.5;
        const double width = f.twig ? 0.006 : f.length * 0.45;
        const V3 along{c * half, sn * half, 0};
        const V3 across{-sn * width, c * width, 0};
        const V3 centre{f.x, f.y, f.z};
        if (f.twig) {
            add_rod(scratch_, centre - along, centre + along, 0.004, f.colour, 4);
        } else {
            // a leaf: a pointed diamond
            add_triangle(scratch_, centre - along, centre + across, centre + along, f.colour, 1);
            add_triangle(scratch_, centre - along, centre + along, centre - across, f.colour, 1);
        }
    }
    r.draw(scratch_.data(), scratch_.size(), nullptr, static_cast<std::uint16_t>(double_sided));
}

void Site::draw_bowl() {
    // oiled wood: a soft, broad sheen
    r.light.gloss_power = 10;
    r.light.gloss_strength = 0.16;
    r.draw(bowl_.data(), bowl_.size(), &bowl_tex_, static_cast<std::uint16_t>(double_sided | gloss));
    r.draw(boulder_.data(), boulder_.size(), &stone_grain(), opaque);
}

void Site::draw_sign() {
    // with the arrow test on, a big arrow fixed to the ground, pointing
    // downstream (+x) like the arrows painted on the rocks, to compare with
    static const bool arrows = std::getenv("ZC_ARROW_ROCKS") != nullptr;
    if (arrows) {
        scratch_.clear();
        const V3 c{0.20, -0.05, 0.003};
        const Col red{0.9f, 0.05f, 0.05f, 1};
        add_triangle(scratch_, c + V3{-0.15, -0.025, 0}, c + V3{0.05, -0.025, 0}, c + V3{0.05, 0.025, 0}, red, 1);
        add_triangle(scratch_, c + V3{-0.15, -0.025, 0}, c + V3{0.05, 0.025, 0}, c + V3{-0.15, 0.025, 0}, red, 1);
        add_triangle(scratch_, c + V3{0.05, -0.07, 0}, c + V3{0.15, 0, 0}, c + V3{0.05, 0.07, 0}, red, 1);
        r.draw(scratch_.data(), scratch_.size(), nullptr, static_cast<std::uint16_t>(unlit | double_sided));
    }
    r.draw(sign_post_.data(), sign_post_.size(), &wood_tex_, opaque);
    // the board, facing south-west toward the default view
    scratch_.clear();
    const V3 c{0.752, -0.222, 0.135};
    const double angle = 0.15;
    const V3 along{std::cos(angle) * 0.075, std::sin(angle) * 0.075, 0};
    const V3 up{0, 0, 0.038};
    const V3 corners[4] = {c - along - up, c + along - up, c + along + up, c - along + up};
    const double st[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    const int order[6] = {0, 1, 2, 0, 2, 3};
    const V3 n = norm(cross(along, up));
    for (int k = 0; k < 6; k += 1) {
        Vtx v;
        v.p = corners[order[k]];
        v.n = n;
        v.c = Col{1, 1, 1, 1};
        v.s = st[order[k]][0];
        v.t = st[order[k]][1];
        scratch_.push_back(v);
    }
    r.draw(scratch_.data(), scratch_.size(), &sign_tex_, static_cast<std::uint16_t>(double_sided));
}

// The sun's shadow map. What doesn't move (the bowl, the boulder, the sign
// and props, the truck, every rock asleep) is cast once and kept; each frame
// adds what does (rocks awake, held or flying, and the crane's upper works).
void Site::cast_shadows(const SceneState& state) {
    const Run& run = *state.run;
    // which rocks are still, and where: the kept map is good while this matches
    std::uint64_t key = 1469598103934665603ull;
    std::vector<char>& still = shadow_still_;
    still.assign(run.rocks.size(), 0);
    for (size_t i = 0; i < run.rocks.size(); i += 1) {
        const RockState& rock = run.rocks[i];
        if (rock.body < 0 || static_cast<int>(i) == run.crane.rock || !run.world().state(rock.body).asleep) {
            continue;
        }
        still[i] = 1;
        const phys::Pose pose = run.rock_pose(static_cast<int>(i));
        const double values[7] = {pose.p.x, pose.p.y, pose.p.z, pose.q.w, pose.q.x, pose.q.y, pose.q.z};
        key = (key ^ static_cast<std::uint64_t>(i)) * 1099511628211ull;
        for (int k = 0; k < 7; k += 1) {
            std::uint64_t bits = 0;
            std::memcpy(&bits, &values[k], sizeof bits);
            key = (key ^ bits) * 1099511628211ull;
        }
        // a rock's mesh can change (its near mesh is built when it first leaves the bowl)
        key = (key ^ static_cast<std::uint64_t>(rock.rock.near_mesh.triangles.size())) * 1099511628211ull;
    }
    if (!shadow_kept_ || key != shadow_key_ || !r.shadow_saved()) {
        r.shadow_begin(V3{-0.05, 0.05, 0.0}, 1.2, 1024);
        for (size_t i = 0; i < run.rocks.size(); i += 1) {
            if (still[i] == 0) {
                continue;
            }
            const Rock& rock = run.rocks[i].rock;
            const RockMesh& mesh = rock.near_mesh.triangles.empty() ? rock.far_mesh : rock.near_mesh;
            const M34 model = pose_matrix(run.rock_pose(static_cast<int>(i)));
            r.shadow_cast(mesh.triangles.data(), mesh.triangles.size(), &model);
        }
        r.shadow_cast(bowl_.data(), bowl_.size(), nullptr);
        r.shadow_cast(sign_post_.data(), sign_post_.size(), nullptr);
        r.shadow_cast(boulder_.data(), boulder_.size(), nullptr);
        r.shadow_cast(props_.data(), props_.size(), nullptr);
        r.shadow_cast(crane_.paint.data(), crane_.truck_paint_count(), nullptr);
        r.shadow_cast(crane_.matte.data(), crane_.truck_matte_count(), nullptr);
        r.shadow_cast(crane_.striped.data(), crane_.truck_striped_count(), nullptr);
        r.shadow_save();
        shadow_key_ = key;
        shadow_kept_ = true;
    } else {
        r.shadow_restore();
    }
    for (size_t i = 0; i < run.rocks.size(); i += 1) {
        if (still[i] != 0) {
            continue;
        }
        const Rock& rock = run.rocks[i].rock;
        const RockMesh& mesh = rock.near_mesh.triangles.empty() ? rock.far_mesh : rock.near_mesh;
        const M34 model = pose_matrix(run.rock_pose(static_cast<int>(i)));
        r.shadow_cast(mesh.triangles.data(), mesh.triangles.size(), &model);
    }
    r.shadow_cast(crane_.paint.data() + crane_.truck_paint_count(), crane_.paint.size() - crane_.truck_paint_count(), nullptr);
    r.shadow_cast(crane_.matte.data() + crane_.truck_matte_count(), crane_.matte.size() - crane_.truck_matte_count(), nullptr);
    r.shadow_cast(crane_.striped.data() + crane_.truck_striped_count(), crane_.striped.size() - crane_.truck_striped_count(), nullptr);
    r.shadow_cast(crane_.plates.data(), crane_.plates.size(), nullptr);
}

void Site::draw_rocks(const SceneState& state) {
    const Run& run = *state.run;
    for (size_t i = 0; i < run.rocks.size(); i += 1) {
        const Rock& rock = run.rocks[i].rock;
        const bool near = !rock.near_mesh.triangles.empty();
        const RockMesh& mesh = near ? rock.near_mesh : rock.far_mesh;
        const M34 model = pose_matrix(run.rock_pose(static_cast<int>(i)));
        r.draw(mesh.triangles.data(), mesh.triangles.size(), &rock_texture(rock), opaque, &model);
        if (static_cast<int>(i) == state.hover_rock) {
            const Col edge = state.hover_ok ? Col{0.55f, 0.95f, 0.55f, 1} : Col{0.95f, 0.5f, 0.45f, 1};
            draw_outline(r, mesh.triangles, model, 0.0035, edge);
        }
    }
}

// The ground under a point: the bank's gravel, or the boulder the crane is
// perched on where that's higher.
double Site::ground_at(double x, double y) const {
    double best = std::max(terrain_height(Terrain::bank, x, y), 0.0);
    for (size_t k = 0; k + 2 < boulder_.size(); k += 3) {
        const V3 a = boulder_[k].p;
        const V3 b = boulder_[k + 1].p;
        const V3 c = boulder_[k + 2].p;
        const double det = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
        if (std::abs(det) < 1e-14) {
            continue;
        }
        const double l0 = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / det;
        const double l1 = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / det;
        const double l2 = 1 - l0 - l1;
        if (l0 < 0 || l1 < 0 || l2 < 0) {
            continue;
        }
        best = std::max(best, l0 * a.z + l1 * b.z + l2 * c.z);
    }
    return best;
}

// Sets the truck down on the boulder: of the planes through three of its
// wheels' footings, the highest that the fourth doesn't poke through, so it
// stands on three wheels with the fourth over thin air, as a toy left on a
// rock would.
void Site::place_crane() {
    const SiteLayout& layout = site_layout();
    const double heading = 0.78;
    const double wheel_x[4] = {0.137, 0.137, -0.125, -0.125};
    const double wheel_y[4] = {0.085, -0.085, 0.085, -0.085};
    const double ch = std::cos(heading);
    const double sh = std::sin(heading);
    double ground[4];
    for (int w = 0; w < 4; w += 1) {
        // the lowest point of the tyre's tread, sampled across its width
        double g = -1;
        for (int k = -1; k <= 1; k += 1) {
            for (int j = -2; j <= 2; j += 1) {
                const double lx = wheel_x[w] + 0.010 * j;
                const double ly = wheel_y[w] + 0.014 * k;
                const double x = layout.crane_base.x + ch * lx - sh * ly;
                const double y = layout.crane_base.y + sh * lx + ch * ly;
                // a wheel of radius 0.049 rests where the ground rises to meet its rim
                const double rise = 0.049 - std::sqrt(std::max(0.0, 0.049 * 0.049 - (0.010 * j) * (0.010 * j)));
                g = std::max(g, ground_at(x, y) - rise);
            }
        }
        ground[w] = g;
    }
    // the plane z = a + b x + c y through each three, in the truck's own x, y
    double best_a = -1;
    double best_b = 0;
    double best_c = 0;
    for (int skip = 0; skip < 4; skip += 1) {
        int ids[3];
        int n = 0;
        for (int w = 0; w < 4; w += 1) {
            if (w != skip) {
                ids[n] = w;
                n += 1;
            }
        }
        const V3 p0{wheel_x[ids[0]], wheel_y[ids[0]], ground[ids[0]]};
        const V3 p1{wheel_x[ids[1]], wheel_y[ids[1]], ground[ids[1]]};
        const V3 p2{wheel_x[ids[2]], wheel_y[ids[2]], ground[ids[2]]};
        const V3 normal = cross(p1 - p0, p2 - p0);
        if (std::abs(normal.z) < 1e-12) {
            continue;
        }
        const double b = -normal.x / normal.z;
        const double c = -normal.y / normal.z;
        const double a = p0.z - b * p0.x - c * p0.y;
        const double fourth = a + b * wheel_x[skip] + c * wheel_y[skip];
        if (fourth + 1e-6 < ground[skip]) {
            continue;   // the fourth wheel would be in the rock
        }
        if (a > best_a) {
            best_a = a;
            best_b = b;
            best_c = c;
        }
    }
    CraneSetup setup;
    setup.heading = heading;
    setup.chassis = M34::translate(layout.crane_base.x, layout.crane_base.y, best_a) * M34::rot_z(heading) * M34::rot_y(-std::atan(best_b)) *
                    M34::rot_x(std::atan(best_c));
    const double pad_x[4] = {0.072, 0.072, -0.196, -0.196};
    const double pad_y[4] = {0.137, -0.137, 0.137, -0.137};
    for (int k = 0; k < 4; k += 1) {
        const V3 spot = setup.chassis.apply(V3{pad_x[k], pad_y[k], 0});
        setup.pad_ground[k] = ground_at(spot.x, spot.y);
    }
    crane_.set_setup(setup);
}

void Site::build_crane(const SceneState& state) {
    const Crane& crane = state.run->crane;
    CranePose pose;
    pose.hook = to_v3(crane.hook);
    pose.wires = crane.wires;
    pose.moving = crane.mode != CraneMode::parked;
    pose.time = state.time;
    pose.mood = state.mood;
    if (crane.attached && crane.rock >= 0) {
        const phys::Vec3 heading = phys::rotate(state.run->rock_pose(crane.rock).q, phys::Vec3{1, 0, 0});
        pose.hook_follows = true;
        pose.hook_turn = std::atan2(heading.y, heading.x);
    }
    crane_.build(pose);
}

void Site::draw_crane() {
    r.light.gloss_power = 30;
    r.light.gloss_strength = 0.55;
    r.draw(crane_.paint.data(), crane_.paint.size(), nullptr, gloss);
    r.draw(crane_.striped.data(), crane_.striped.size(), &crane_.stripe_tex, gloss);
    r.draw(crane_.plates.data(), crane_.plates.size(), &crane_.plate_tex, static_cast<std::uint16_t>(cutout | double_sided | gloss));
    r.light.gloss_power = 12;
    r.light.gloss_strength = 0.10;
    r.draw(crane_.matte.data(), crane_.matte.size(), nullptr, gloss);
    r.draw(crane_.lamps.data(), crane_.lamps.size(), nullptr, unlit);
    if (crane_.beacon_glow > 0.02) {
        const float g = static_cast<float>(crane_.beacon_glow);
        const double size = 0.05 + 0.03 * crane_.beacon_glow;
        r.billboard(crane_.beacon_at - V3{0, 0, size * 0.5}, size, size, &glow_tex_, Col{1.0f * g, 0.55f * g, 0.12f * g, 1},
                    static_cast<std::uint16_t>(additive | unlit | no_fog));
    }
}

namespace {

// A flat strap along a path: `across` is the strap's breadth direction at
// each point, `facing` its face's normal.
void add_strap(std::vector<Vtx>& out, const std::vector<V3>& points, const std::vector<V3>& across, const std::vector<V3>& facing, double width,
               Col colour) {
    for (size_t k = 0; k + 1 < points.size(); k += 1) {
        const V3 a0 = points[k] - across[k] * (0.5 * width);
        const V3 a1 = points[k] + across[k] * (0.5 * width);
        const V3 b0 = points[k + 1] - across[k + 1] * (0.5 * width);
        const V3 b1 = points[k + 1] + across[k + 1] * (0.5 * width);
        const double st[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        const size_t first = out.size();
        add_quad(out, a0, a1, b1, b0, st, facing[k] + facing[k + 1], colour);
        // shade each end by its own normal
        for (size_t v = first; v < out.size(); v += 1) {
            const double da = len(out[v].p - points[k]);
            const double db = len(out[v].p - points[k + 1]);
            out[v].n = norm(da <= db ? facing[k] : facing[k + 1]);
        }
    }
}

// where a ray from the origin along `d` leaves a triangle mesh (body frame);
// the farthest crossing, or -1
double ray_exit(const std::vector<Vtx>& mesh, V3 d) {
    double best = -1;
    for (size_t k = 0; k + 2 < mesh.size(); k += 3) {
        const V3 a = mesh[k].p;
        const V3 e1 = mesh[k + 1].p - a;
        const V3 e2 = mesh[k + 2].p - a;
        const V3 p = cross(d, e2);
        const double det = dot(e1, p);
        if (std::abs(det) < 1e-14) {
            continue;
        }
        const double inv = 1.0 / det;
        const V3 s = a * -1.0;
        const double u = dot(s, p) * inv;
        if (u < 0 || u > 1) {
            continue;
        }
        const V3 q = cross(s, e1);
        const double v = dot(d, q) * inv;
        if (v < 0 || u + v > 1) {
            continue;
        }
        const double t = dot(e2, q) * inv;
        best = std::max(best, t);
    }
    return best;
}

double cross2(double ox, double oy, double ax, double ay, double bx, double by) {
    return (ax - ox) * (by - oy) - (ay - oy) * (bx - ox);
}

}  // namespace

// The slings: two orange straps crossed under the rock and drawn tight round
// it, their four legs up to the hook's bowl. Being let down they wrap the
// rock from both ends toward the bottom; reeled in they hang in a bunch.
void Site::draw_slings(const SceneState& state) {
    const Run& run = *state.run;
    const Crane& crane = run.crane;
    const V3 hook = to_v3(crane.hook);
    const Col strap{0.96f, 0.44f, 0.10f, 1};
    const double width = 0.0060;
    scratch_.clear();
    // the ring the legs hang from
    add_arc_tube(scratch_, M34::translate(hook.x, hook.y, hook.z + 0.0005) * M34::rot_x(kPi / 2), 0.0040, 0.0011, 0, 2 * kPi, 10, 4, Col{0.55f, 0.56f, 0.6f, 1});
    std::vector<V3> points, across, facing;
    if (crane.rock < 0 || crane.wires <= 0) {
        for (int k = 0; k < 4; k += 1) {
            const double a = kPi * 0.25 + kPi * 0.5 * k;
            const double swing = 0.12 * std::sin(state.time * 1.4 + k * 1.7);
            points.clear();
            across.clear();
            facing.clear();
            const V3 out{std::cos(a), std::sin(a), 0};
            for (int p = 0; p <= 4; p += 1) {
                const double t = p / 4.0;
                points.push_back(hook + out * (0.004 + 0.004 * t + swing * 0.01 * t) + V3{0, 0, -0.034 * t});
                across.push_back(V3{-out.y, out.x, 0});
                facing.push_back(out);
            }
            add_strap(scratch_, points, across, facing, width, strap);
            // the eye at the end, folded back
            add_arc_tube(scratch_, frame_along(points.back(), out) * M34::rot_y(kPi / 2), 0.0028, 0.0011, 0, 2 * kPi, 8, 3, strap);
        }
        r.light.gloss_power = 12;
        r.light.gloss_strength = 0.12;
        r.draw(scratch_.data(), scratch_.size(), nullptr, static_cast<std::uint16_t>(double_sided | gloss));
        return;
    }
    const RockState& held = run.rocks[static_cast<size_t>(crane.rock)];
    const phys::Pose pose = run.rock_pose(crane.rock);
    // the mesh that's drawn, so the straps lie on its bumps rather than in them
    const std::vector<Vtx>& mesh = held.rock.near_mesh.triangles.empty() ? held.rock.far_mesh.triangles : held.rock.near_mesh.triangles;
    const phys::Quat inverse = phys::conjugate(pose.q);
    const phys::Vec3 body_x = phys::rotate(pose.q, phys::Vec3{1, 0, 0});
    const double yaw = std::atan2(body_x.y, body_x.x);
    const V3 centre{pose.p.x, pose.p.y, pose.p.z};
    const double tension = crane.attached ? std::min(1.0, std::max(0.0, run.world().hold_state().tension)) : 1.0;
    const double shown = crane.attached ? 1.0 : crane.wires;
    for (int s = 0; s < 2; s += 1) {
        const double azimuth = yaw + kPi * 0.25 + kPi * 0.5 * s;
        const V3 u{std::cos(azimuth), std::sin(azimuth), 0};
        const V3 w{-u.y, u.x, 0};
        // the rock's outline in this upright plane through its centre, and the
        // tight loop of the strap round its lower part: the convex hull
        const int samples = 40;
        std::vector<double> hx, hy;
        double last = 0.5 * held.rock.diameter;
        for (int i = 0; i < samples; i += 1) {
            const double phi = 2 * kPi * i / samples;
            const V3 d = u * std::cos(phi) + V3{0, 0, std::sin(phi)};
            const phys::Vec3 local = phys::rotate(inverse, phys::Vec3{d.x, d.y, d.z});
            const double t = ray_exit(mesh, V3{local.x, local.y, local.z});
            if (t > 0) {
                last = t;
            }
            const double reach = last + 0.0026 + 0.0010 * s;
            hx.push_back(reach * std::cos(phi));
            hy.push_back(reach * std::sin(phi));
        }
        // Andrew's monotone chain, counter-clockwise
        std::vector<int> order;
        for (int i = 0; i < samples; i += 1) {
            order.push_back(i);
        }
        for (size_t i = 1; i < order.size(); i += 1) {
            for (size_t j = i; j > 0; j -= 1) {
                const int a = order[j - 1];
                const int b = order[j];
                if (hx[static_cast<size_t>(b)] < hx[static_cast<size_t>(a)] ||
                    (hx[static_cast<size_t>(b)] == hx[static_cast<size_t>(a)] && hy[static_cast<size_t>(b)] < hy[static_cast<size_t>(a)])) {
                    order[j - 1] = b;
                    order[j] = a;
                } else {
                    break;
                }
            }
        }
        std::vector<int> hull;
        for (int pass = 0; pass < 2; pass += 1) {
            const size_t floor = hull.size();
            for (size_t i = 0; i < order.size(); i += 1) {
                const int p = pass == 0 ? order[i] : order[order.size() - 1 - i];
                while (hull.size() >= floor + 2) {
                    const int a = hull[hull.size() - 2];
                    const int b = hull[hull.size() - 1];
                    if (cross2(hx[static_cast<size_t>(a)], hy[static_cast<size_t>(a)], hx[static_cast<size_t>(b)], hy[static_cast<size_t>(b)],
                               hx[static_cast<size_t>(p)], hy[static_cast<size_t>(p)]) <= 0) {
                        hull.pop_back();
                    } else {
                        break;
                    }
                }
                hull.push_back(p);
            }
            hull.pop_back();
        }
        const int count = static_cast<int>(hull.size());
        if (count < 3) {
            continue;
        }
        // where the legs leave the rock: the hull's tangents from the hook
        const double kx = dot(hook - centre, u);
        const double ky = hook.z - centre.z;
        int right = 0;
        int left = 0;
        double most = -1e9;
        double least = 1e9;
        int bottom = 0;
        for (int i = 0; i < count; i += 1) {
            const double px = hx[static_cast<size_t>(hull[static_cast<size_t>(i)])];
            const double py = hy[static_cast<size_t>(hull[static_cast<size_t>(i)])];
            const double angle = std::atan2(px - kx, -(py - ky));
            if (angle > most) {
                most = angle;
                right = i;
            }
            if (angle < least) {
                least = angle;
                left = i;
            }
            if (py < hy[static_cast<size_t>(hull[static_cast<size_t>(bottom)])]) {
                bottom = i;
            }
        }
        // round the bottom from right to left: whichever way passes it
        int step = -1;
        bool passes = false;
        for (int i = right;; i = (i + step + count) % count) {
            if (i == bottom) {
                passes = true;
            }
            if (i == left) {
                break;
            }
        }
        if (!passes) {
            step = 1;
        }
        std::vector<V3> loop, loop_normal;
        loop.push_back(hook);
        loop_normal.push_back(V3{0, 0, 1});
        for (int i = right;; i = (i + step + count) % count) {
            const size_t h = static_cast<size_t>(hull[static_cast<size_t>(i)]);
            const double nx = hx[h];
            const double ny = hy[h];
            const double l = std::sqrt(nx * nx + ny * ny);
            loop.push_back(centre + u * nx + V3{0, 0, ny});
            loop_normal.push_back(norm(u * (nx / l) + V3{0, 0, ny / l}));
            if (i == left) {
                break;
            }
        }
        loop.push_back(hook);
        loop_normal.push_back(V3{0, 0, 1});
        // the legs sag when the rock is set down and the wires go slack
        std::vector<V3> path, path_normal;
        for (size_t i = 0; i + 1 < loop.size(); i += 1) {
            const bool leg = i == 0 || i + 2 == loop.size();
            const int pieces = leg ? 6 : 1;
            const V3 a = loop[i];
            const V3 b = loop[i + 1];
            const double sag = leg ? (1 - tension) * 0.20 * len(b - a) : 0.0;
            for (int p = 0; p < pieces; p += 1) {
                const double t = static_cast<double>(p) / pieces;
                V3 point = a + (b - a) * t;
                point.z = point.z - sag * 4 * t * (1 - t);
                path.push_back(point);
                path_normal.push_back(leg ? norm(cross(b - a, w)) : loop_normal[i]);
            }
        }
        path.push_back(loop.back());
        path_normal.push_back(loop_normal.back());
        for (size_t i = 0; i < path_normal.size(); i += 1) {
            // legs face out from the rock
            if (dot(path_normal[i], path[i] - centre) < 0) {
                path_normal[i] = path_normal[i] * -1.0;
            }
        }
        // how much of it is let down: from both ends toward the middle
        std::vector<double> distance(path.size(), 0.0);
        for (size_t i = 1; i < path.size(); i += 1) {
            distance[i] = distance[i - 1] + len(path[i] - path[i - 1]);
        }
        const double total = distance.back();
        const double reach = 0.5 * total * shown;
        for (int end = 0; end < 2; end += 1) {
            points.clear();
            across.clear();
            facing.clear();
            for (size_t k = 0; k < path.size(); k += 1) {
                const size_t i = end == 0 ? k : path.size() - 1 - k;
                const double from_end = end == 0 ? distance[i] : total - distance[i];
                if (from_end > reach) {
                    // the last piece, cut where the strap ends
                    const size_t prev = end == 0 ? i - 1 : i + 1;
                    const double prev_from = end == 0 ? distance[prev] : total - distance[prev];
                    const double t = (reach - prev_from) / std::max(1e-9, from_end - prev_from);
                    points.push_back(path[prev] + (path[i] - path[prev]) * t);
                    across.push_back(w);
                    facing.push_back(path_normal[i]);
                    break;
                }
                points.push_back(path[i]);
                across.push_back(w);
                facing.push_back(path_normal[i]);
            }
            add_strap(scratch_, points, across, facing, width, strap);
        }
    }
    r.light.gloss_power = 12;
    r.light.gloss_strength = 0.12;
    r.draw(scratch_.data(), scratch_.size(), nullptr, static_cast<std::uint16_t>(double_sided | gloss));
}

void Site::render(const SceneState& state, bool still) {
    r.time = state.time;
    r.tris_drawn = 0;
    // the scenery that never changes, kept for as long as the camera stays put
    const bool camera_same = cache_valid_ && cached_w_ == r.W && cached_h_ == r.H && cached_camera_.yaw == camera_.yaw &&
                             cached_camera_.pitch == camera_.pitch && cached_camera_.distance == camera_.distance &&
                             cached_camera_.target.x == camera_.target.x && cached_camera_.target.y == camera_.target.y &&
                             cached_camera_.target.z == camera_.target.z;
    if (!camera_same) {
        r.shadow_use(false);
        r.clear_depth();
        draw_sky();
        draw_scenery();
        cache_rgb_ = r.rgb;
        cache_depth_ = r.depth;
        cached_camera_ = camera_;
        cached_w_ = r.W;
        cached_h_ = r.H;
        cache_valid_ = true;
    } else {
        std::copy(cache_rgb_.begin(), cache_rgb_.end(), r.rgb.begin());
        std::copy(cache_depth_.begin(), cache_depth_.end(), r.depth.begin());
    }
    (void)still;
    // this frame's shadows: rocks, the bowl, the sign and the crane
    build_crane(state);
    cast_shadows(state);
    // the deferred pass darkens the cached scenery where shadows fall
    r.shadow_use(true);
    r.shadow_screen_pass(0.55f);
    draw_bowl();
    draw_sign();
    draw_rocks(state);
    draw_crane();
    draw_slings(state);
    draw_brook(state.time);
    // the windows last, over whatever is behind them
    r.light.gloss_power = 40;
    r.light.gloss_strength = 0.9;
    r.draw(crane_.glass.data(), crane_.glass.size(), nullptr, static_cast<std::uint16_t>(translucent | gloss));
    r.shadow_use(false);
}

}  // namespace zc
