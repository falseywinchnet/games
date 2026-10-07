#include "soil.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace soil {

namespace {

constexpr double kPi = 3.14159265358979323846;

enum Kind : std::uint8_t { kSoil = 0, kPebble, kStraw, kTwig, kLeaf, kCrack, kGrit };

// ------------------------------------------------------------------ numbers

std::uint32_t mix32(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value;
}

// A splitmix64 sequence: the same seed draws the same numbers on every platform.
struct Random {
    std::uint64_t state = 0;
    explicit Random(std::uint64_t seed) : state(seed) {}
    std::uint64_t next() {
        state += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double unit() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
    double range(double low, double high) { return low + (high - low) * unit(); }
};

double clamp01(double value) { return std::clamp(value, 0.0, 1.0); }
double smoothstep(double edge0, double edge1, double value) {
    const double t = clamp01((value - edge0) / (edge1 - edge0));
    return t * t * (3.0 - 2.0 * t);
}
double lerp(double a, double b, double t) { return a + (b - a) * t; }

double to_linear(double srgb) {
    const double c = clamp01(srgb);
    const double linear = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    return linear;
}
std::uint8_t to_srgb8(double linear) {
    const double c = clamp01(linear);
    const double srgb = c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
    return static_cast<std::uint8_t>(std::lround(clamp01(srgb) * 255.0));
}

struct Linear {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
};
Linear linear_of(Colour colour) {
    const Linear result{to_linear(colour.red), to_linear(colour.green), to_linear(colour.blue)};
    return result;
}
Linear scaled(Linear colour, double k) {
    const Linear result{colour.r * k, colour.g * k, colour.b * k};
    return result;
}
Linear mixed(Linear a, Linear b, double t) {
    const Linear result{lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t)};
    return result;
}
// Moves a colour's saturation by `k` (1 unchanged) about its luminance.
Linear saturated(Linear colour, double k) {
    const double luminance = 0.2126 * colour.r + 0.7152 * colour.g + 0.0722 * colour.b;
    const Linear result{std::max(0.0, luminance + (colour.r - luminance) * k),
                        std::max(0.0, luminance + (colour.g - luminance) * k),
                        std::max(0.0, luminance + (colour.b - luminance) * k)};
    return result;
}

// ------------------------------------------------------------------ periodic noise

// Value noise on a lattice of cells_x by cells_y cells spread over the whole
// picture, so it repeats exactly at the picture's edges. Coordinates are texels.
struct Noise {
    int cells_x = 1;
    int cells_y = 1;
    double texel_to_cell_x = 1.0;
    double texel_to_cell_y = 1.0;
    std::uint32_t seed = 0;
};

Noise make_noise(int width, int height, double feature_texels, std::uint32_t seed) {
    Noise noise;
    noise.cells_x = std::max(1, static_cast<int>(std::lround(width / std::max(1.0, feature_texels))));
    noise.cells_y = std::max(1, static_cast<int>(std::lround(height / std::max(1.0, feature_texels))));
    noise.texel_to_cell_x = static_cast<double>(noise.cells_x) / width;
    noise.texel_to_cell_y = static_cast<double>(noise.cells_y) / height;
    noise.seed = seed;
    return noise;
}

double lattice(const Noise& noise, int cell_x, int cell_y) {
    const int wrapped_x = ((cell_x % noise.cells_x) + noise.cells_x) % noise.cells_x;
    const int wrapped_y = ((cell_y % noise.cells_y) + noise.cells_y) % noise.cells_y;
    const std::uint32_t key = mix32(static_cast<std::uint32_t>(wrapped_x) * 0x9E3779B1u ^
                                    mix32(static_cast<std::uint32_t>(wrapped_y) + noise.seed));
    return static_cast<double>(key & 0xFFFFFFu) / 16777215.0;
}

// Smooth value noise in 0..1 at texel position (x, y).
double sample(const Noise& noise, double x, double y) {
    const double u = x * noise.texel_to_cell_x;
    const double v = y * noise.texel_to_cell_y;
    const double fu = std::floor(u);
    const double fv = std::floor(v);
    const int cell_x = static_cast<int>(fu);
    const int cell_y = static_cast<int>(fv);
    double tx = u - fu;
    double ty = v - fv;
    tx = tx * tx * tx * (tx * (tx * 6.0 - 15.0) + 10.0);
    ty = ty * ty * ty * (ty * (ty * 6.0 - 15.0) + 10.0);
    const double a = lattice(noise, cell_x, cell_y);
    const double b = lattice(noise, cell_x + 1, cell_y);
    const double c = lattice(noise, cell_x, cell_y + 1);
    const double d = lattice(noise, cell_x + 1, cell_y + 1);
    const double result = lerp(lerp(a, b, tx), lerp(c, d, tx), ty);
    return result;
}

// Three octaves, each half the size; centred on 0, roughly -0.5..0.5.
struct Fractal {
    Noise octave[3];
};
Fractal make_fractal(int width, int height, double feature_texels, std::uint32_t seed) {
    Fractal fractal;
    for (int index = 0; index < 3; ++index) {
        const double size = feature_texels / static_cast<double>(1 << index);
        fractal.octave[index] = make_noise(width, height, size, seed + static_cast<std::uint32_t>(index) * 7919u);
    }
    return fractal;
}
double sample(const Fractal& fractal, double x, double y) {
    const double result = (sample(fractal.octave[0], x, y) - 0.5) * 0.57 +
                          (sample(fractal.octave[1], x, y) - 0.5) * 0.29 +
                          (sample(fractal.octave[2], x, y) - 0.5) * 0.14;
    return result;
}

// ------------------------------------------------------------------ the field

// Everything about the ground, texel by texel (width * height, row-major).
struct Field {
    int width = 0;
    int height = 0;
    bool wraps = true;                  // a repeating surface, or a heap with edges
    double metres_per_texel = 0.01;
    std::vector<float> relief;          // metres
    std::vector<float> red, green, blue;  // linear albedo
    std::vector<float> damp;            // added to the moisture, -1..1
    std::vector<float> gloss;           // 0 matt .. 1 polished
    std::vector<float> presence;        // spoil only: 1 where soil lies
    std::vector<std::uint8_t> kind;

    void reset(int w, int h, bool repeating, double texels_per_metre) {
        width = w;
        height = h;
        wraps = repeating;
        metres_per_texel = 1.0 / texels_per_metre;
        const std::size_t count = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
        relief.assign(count, 0.0f);
        red.assign(count, 0.0f);
        green.assign(count, 0.0f);
        blue.assign(count, 0.0f);
        damp.assign(count, 0.0f);
        gloss.assign(count, 0.0f);
        presence.assign(count, repeating ? 1.0f : 0.0f);
        kind.assign(count, kSoil);
    }
    // The index of texel (x, y), wrapped or clamped; -1 when outside a heap.
    std::ptrdiff_t index(int x, int y) const {
        if (wraps) {
            const int wx = ((x % width) + width) % width;
            const int wy = ((y % height) + height) % height;
            return static_cast<std::ptrdiff_t>(wy) * width + wx;
        }
        if (x < 0 || y < 0 || x >= width || y >= height) return -1;
        return static_cast<std::ptrdiff_t>(y) * width + x;
    }
    // The index of texel (x, y), wrapped, or clamped to the nearest edge texel.
    std::size_t nearest(int x, int y) const {
        if (wraps) return static_cast<std::size_t>(index(x, y));
        const int cx = std::clamp(x, 0, width - 1);
        const int cy = std::clamp(y, 0, height - 1);
        return static_cast<std::size_t>(cy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(cx);
    }
    double relief_at(int x, int y) const { return static_cast<double>(relief[nearest(x, y)]); }
    double relief_bilinear(double x, double y) const {
        const double fx = std::floor(x);
        const double fy = std::floor(y);
        const int ix = static_cast<int>(fx);
        const int iy = static_cast<int>(fy);
        const double tx = x - fx;
        const double ty = y - fy;
        const double top = lerp(relief_at(ix, iy), relief_at(ix + 1, iy), tx);
        const double bottom = lerp(relief_at(ix, iy + 1), relief_at(ix + 1, iy + 1), tx);
        return lerp(top, bottom, ty);
    }
    void paint(std::size_t at, Linear colour, std::uint8_t what, double wet, double shine) {
        red[at] = static_cast<float>(colour.r);
        green[at] = static_cast<float>(colour.g);
        blue[at] = static_cast<float>(colour.b);
        kind[at] = what;
        damp[at] = static_cast<float>(wet);
        gloss[at] = static_cast<float>(shine);
    }
};

// A lump raised on the field: a clod, a crumb or a pebble.
struct Lump {
    double x = 0.0;            // centre, texels
    double y = 0.0;
    double radius = 1.0;       // texels
    double aspect = 1.0;       // radius across / radius along
    double angle = 0.0;        // of its long axis
    double base = 0.0;         // metres: where its foot sits
    double rise = 0.0;         // metres above its foot at the centre
    double roundness = 0.6;    // profile exponent: small is flat-topped, 1 a dome
    double wobble[3] = {0.0, 0.0, 0.0};  // outline harmonics 2, 3 and 5
    double phase[3] = {0.0, 0.0, 0.0};
    double tilt_x = 0.0;       // metres per texel: facets, so clods are not all domes
    double tilt_y = 0.0;
    Linear colour;
    std::uint8_t what = kSoil;
    double wet = 0.0;
    double shine = 0.0;
    bool marks_presence = false;
};

void stamp(Field& field, const Lump& lump) {
    const double swing = std::fabs(lump.wobble[0]) + std::fabs(lump.wobble[1]) + std::fabs(lump.wobble[2]);
    const double reach = lump.radius * (1.0 + swing) + 1.0;
    const double reach2 = lump.radius * (1.0 + swing) * lump.radius * (1.0 + swing);
    const int x0 = static_cast<int>(std::floor(lump.x - reach));
    const int x1 = static_cast<int>(std::ceil(lump.x + reach));
    const int y0 = static_cast<int>(std::floor(lump.y - reach));
    const int y1 = static_cast<int>(std::ceil(lump.y + reach));
    const double ca = std::cos(lump.angle);
    const double sa = std::sin(lump.angle);
    const double across = 1.0 / std::max(0.2, lump.aspect);
    // The outline's harmonics, cos(n theta + phase), from the direction's cosine and
    // sine by complex multiplication: no trigonometry per texel.
    const double phase_cos[3] = {std::cos(lump.phase[0]), std::cos(lump.phase[1]), std::cos(lump.phase[2])};
    const double phase_sin[3] = {std::sin(lump.phase[0]), std::sin(lump.phase[1]), std::sin(lump.phase[2])};
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double dx = x + 0.5 - lump.x;
            const double dy = y + 0.5 - lump.y;
            const double along = dx * ca + dy * sa;
            const double side = (-dx * sa + dy * ca) * across;
            const double distance2 = along * along + side * side;
            if (distance2 >= reach2) continue;
            const std::ptrdiff_t at = field.index(x, y);
            if (at < 0) continue;
            const double distance = std::sqrt(distance2);
            const double c1 = distance > 1e-9 ? along / distance : 1.0;
            const double s1 = distance > 1e-9 ? side / distance : 0.0;
            const double c2 = c1 * c1 - s1 * s1;
            const double s2 = 2.0 * c1 * s1;
            const double c3 = c2 * c1 - s2 * s1;
            const double s3 = s2 * c1 + c2 * s1;
            const double c5 = c3 * c2 - s3 * s2;
            const double s5 = s3 * c2 + c3 * s2;
            const double outline = lump.radius * (1.0 + lump.wobble[0] * (c2 * phase_cos[0] - s2 * phase_sin[0]) +
                                                  lump.wobble[1] * (c3 * phase_cos[1] - s3 * phase_sin[1]) +
                                                  lump.wobble[2] * (c5 * phase_cos[2] - s5 * phase_sin[2]));
            if (outline <= 0.0) continue;
            const double q = distance / outline;
            if (q >= 1.0) continue;
            const double inside = 1.0 - q * q;
            const double top = lump.base + lump.rise * std::pow(inside, lump.roundness) +
                               (lump.tilt_x * dx + lump.tilt_y * dy) * std::sqrt(inside);
            const std::size_t index = static_cast<std::size_t>(at);
            if (top <= static_cast<double>(field.relief[index])) continue;
            field.relief[index] = static_cast<float>(top);
            field.paint(index, lump.colour, lump.what, lump.wet, lump.shine);
            if (lump.marks_presence) field.presence[index] = 1.0f;
        }
    }
}

// A thin thing lying on the ground: a straw, a twig, a root. Points are texels.
struct Stick {
    double x[5] = {0, 0, 0, 0, 0};
    double y[5] = {0, 0, 0, 0, 0};
    int points = 2;
    double radius = 0.5;   // texels
    double rise = 0.002;   // metres at its axis
    double base = 0.0;
    Linear colour;
    Linear edge;           // its colour where it turns away (bark, the straw's sheen line)
    std::uint8_t what = kStraw;
    double shine = 0.0;
};

void stamp(Field& field, const Stick& stick) {
    for (int segment = 0; segment + 1 < stick.points; ++segment) {
        const double ax = stick.x[segment];
        const double ay = stick.y[segment];
        const double bx = stick.x[segment + 1];
        const double by = stick.y[segment + 1];
        const double reach = stick.radius + 1.0;
        const int x0 = static_cast<int>(std::floor(std::min(ax, bx) - reach));
        const int x1 = static_cast<int>(std::ceil(std::max(ax, bx) + reach));
        const int y0 = static_cast<int>(std::floor(std::min(ay, by) - reach));
        const int y1 = static_cast<int>(std::ceil(std::max(ay, by) + reach));
        const double ex = bx - ax;
        const double ey = by - ay;
        const double length2 = std::max(1e-9, ex * ex + ey * ey);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const std::ptrdiff_t at = field.index(x, y);
                if (at < 0) continue;
                const double px = x + 0.5 - ax;
                const double py = y + 0.5 - ay;
                const double t = std::clamp((px * ex + py * ey) / length2, 0.0, 1.0);
                const double dx = px - ex * t;
                const double dy = py - ey * t;
                const double q = std::sqrt(dx * dx + dy * dy) / stick.radius;
                if (q >= 1.0) continue;
                const double top = stick.base + stick.rise * std::sqrt(1.0 - q * q);
                const std::size_t index = static_cast<std::size_t>(at);
                if (top <= static_cast<double>(field.relief[index])) continue;
                field.relief[index] = static_cast<float>(top);
                field.paint(index, mixed(stick.colour, stick.edge, smoothstep(0.45, 1.0, q)), stick.what, 0.0,
                            stick.shine);
            }
        }
    }
}

// A fallen leaf: a pointed oval with a midrib, curling up at the edges.
struct Leaf {
    double x = 0.0;
    double y = 0.0;
    double length = 4.0;   // texels, tip to stalk
    double width = 2.0;
    double angle = 0.0;
    double base = 0.0;
    double curl = 0.004;   // metres the edges lift
    Linear colour;
    Linear rib;
};

void stamp(Field& field, const Leaf& leaf) {
    const double reach = leaf.length * 0.5 + 1.5;
    const int x0 = static_cast<int>(std::floor(leaf.x - reach));
    const int x1 = static_cast<int>(std::ceil(leaf.x + reach));
    const int y0 = static_cast<int>(std::floor(leaf.y - reach));
    const int y1 = static_cast<int>(std::ceil(leaf.y + reach));
    const double ca = std::cos(leaf.angle);
    const double sa = std::sin(leaf.angle);
    const double half_length = leaf.length * 0.5;
    const double half_width = std::max(0.5, leaf.width * 0.5);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const std::ptrdiff_t at = field.index(x, y);
            if (at < 0) continue;
            const double dx = x + 0.5 - leaf.x;
            const double dy = y + 0.5 - leaf.y;
            const double u = (dx * ca + dy * sa) / half_length;
            const double v = (-dx * sa + dy * ca) / half_width;
            if (u <= -1.0 || u >= 1.0) continue;
            const double outline = std::pow(1.0 - u * u, 0.75);
            if (std::fabs(v) >= outline) continue;
            const double across = std::fabs(v) / std::max(1e-6, outline);
            const double top = leaf.base + 0.0015 + leaf.curl * across * across;
            const std::size_t index = static_cast<std::size_t>(at);
            if (top <= static_cast<double>(field.relief[index])) continue;
            field.relief[index] = static_cast<float>(top);
            const double vein = std::fabs(v) * half_width < 0.6 ? 1.0 : 0.0;
            field.paint(index, mixed(leaf.colour, leaf.rib, vein * 0.6), kLeaf, 0.0, 0.15);
        }
    }
}

// ------------------------------------------------------------------ building the ground

// The period of a wave across the picture: a whole number of cycles in x and y
// nearest to `spacing_texels` at `angle` (the direction the rows run).
struct Wave {
    double kx = 0.0;   // cycles per texel
    double ky = 0.0;
    bool present = false;
};
Wave fit_wave(int width, int height, double angle, double spacing_texels) {
    Wave wave;
    if (spacing_texels <= 1.5) return wave;
    // Rows run along `angle`; the wave climbs across them.
    const double nx = -std::sin(angle);
    const double ny = std::cos(angle);
    const double cycles_x = std::round(nx * width / spacing_texels);
    const double cycles_y = std::round(ny * height / spacing_texels);
    if (cycles_x == 0.0 && cycles_y == 0.0) return wave;
    wave.kx = cycles_x / width;
    wave.ky = cycles_y / height;
    wave.present = true;
    return wave;
}

struct Palette {
    Linear soil;          // dry albedo
    Linear subsoil;       // paler, yellower clay from below
    Linear stones[5];
    Linear straw;
    Linear twig;
    Linear twig_grey;
    Linear leaf;
};

Palette make_palette(const Parameters& p) {
    Palette palette;
    palette.soil = linear_of(p.colour);
    palette.subsoil = saturated(mixed(palette.soil, linear_of(Colour{0.74, 0.58, 0.36}), 0.55), 1.1);
    palette.stones[0] = linear_of(Colour{0.62, 0.60, 0.56});  // grey flint
    palette.stones[1] = linear_of(Colour{0.72, 0.64, 0.52});  // buff sandstone
    palette.stones[2] = linear_of(Colour{0.66, 0.50, 0.36});  // ochre
    palette.stones[3] = linear_of(Colour{0.50, 0.52, 0.55});  // blue-grey slate
    palette.stones[4] = linear_of(Colour{0.85, 0.83, 0.78});  // white quartz
    palette.straw = linear_of(Colour{0.86, 0.73, 0.43});
    palette.twig = linear_of(Colour{0.36, 0.25, 0.17});
    palette.twig_grey = linear_of(Colour{0.50, 0.45, 0.39});
    palette.leaf = linear_of(p.leaf_colour);
    return palette;
}

// The gentle ground under everything: undulation, furrows.
void lay_base(Field& field, const Parameters& p, std::uint32_t seed) {
    const double tpm = 1.0 / field.metres_per_texel;
    const double relief = std::max(0.0, p.relief);
    const Fractal swell = make_fractal(field.width, field.height, 0.35 * tpm, seed + 11u);
    const Noise wander = make_noise(field.width, field.height, std::max(4.0, 0.6 * tpm), seed + 23u);
    const Wave furrow = fit_wave(field.width, field.height, p.furrow_angle, p.furrow_spacing * tpm);
    const double swell_height = (0.012 + 0.018 * p.looseness) * relief;
    const double furrow_depth = p.furrow_depth * relief;
    const double wander_cycles = 0.06;
    for (int y = 0; y < field.height; ++y) {
        for (int x = 0; x < field.width; ++x) {
            double h = sample(swell, x, y) * swell_height;
            if (furrow.present) {
                const double phase = (x * furrow.kx + y * furrow.ky) + (sample(wander, x, y) - 0.5) * wander_cycles * 2.0;
                const double c = std::cos(2.0 * kPi * phase);
                // A rounded ridge with a narrower trough: soil slumps into the bottom.
                const double ridge = 0.5 + 0.5 * c;
                h += (std::pow(ridge, 0.7) - 0.5) * furrow_depth;
            }
            field.relief[static_cast<std::size_t>(y) * field.width + x] = static_cast<float>(h);
        }
    }
}

// A periodic lattice of jittered points: Voronoi cells that tile the picture.
struct Cells {
    int count_x = 1;
    int count_y = 1;
    double size_x = 1.0;   // texels
    double size_y = 1.0;
    double offset_x = 0.0;   // the lattice is shifted by a seeded amount, so no line of
    double offset_y = 0.0;   // the picture (the seam least of all) sits on a cell border
    std::vector<double> point_x;  // count_x * count_y jitters within each cell, 0..1
    std::vector<double> point_y;
};
Cells make_cells(int width, int height, double size_texels, std::uint32_t seed) {
    Cells cells;
    cells.count_x = std::max(1, static_cast<int>(std::lround(width / size_texels)));
    cells.count_y = std::max(1, static_cast<int>(std::lround(height / size_texels)));
    cells.size_x = static_cast<double>(width) / cells.count_x;
    cells.size_y = static_cast<double>(height) / cells.count_y;
    cells.offset_x = static_cast<double>(mix32(seed * 3u + 1u) & 0xFFFFu) / 65536.0 * cells.size_x;
    cells.offset_y = static_cast<double>(mix32(seed * 3u + 2u) & 0xFFFFu) / 65536.0 * cells.size_y;
    const std::size_t count = static_cast<std::size_t>(cells.count_x) * static_cast<std::size_t>(cells.count_y);
    cells.point_x.resize(count);
    cells.point_y.resize(count);
    for (std::size_t index = 0; index < count; ++index) {
        const std::uint32_t key = mix32(static_cast<std::uint32_t>(index) ^ (seed * 0x51ED27u));
        cells.point_x[index] = static_cast<double>(key & 0xFFFFu) / 65535.0;
        cells.point_y[index] = static_cast<double>(key >> 16) / 65535.0;
    }
    return cells;
}
// Half the gap between the nearest and second-nearest points: the distance, in
// texels, from (x, y) to the edge between two cells. Points may lie anywhere in
// their cell, so the lattice leaves no trace; the 4 x 4 cells nearest the texel
// hold both points.
double cell_edge(const Cells& cells, double x, double y) {
    const double px = x + cells.offset_x;
    const double py = y + cells.offset_y;
    const double ux = px / cells.size_x;
    const double uy = py / cells.size_y;
    // The block starts one or two cells back, whichever side of its cell the texel is on.
    const int bx = static_cast<int>(std::floor(ux)) - (ux - std::floor(ux) < 0.5 ? 2 : 1);
    const int by = static_cast<int>(std::floor(uy)) - (uy - std::floor(uy) < 0.5 ? 2 : 1);
    double first = 1e30;
    double second = 1e30;
    for (int oy = 0; oy < 4; ++oy) {
        const int gy = by + oy;
        const int wy = ((gy % cells.count_y) + cells.count_y) % cells.count_y;
        for (int ox = 0; ox < 4; ++ox) {
            const int gx = bx + ox;
            const int wx = ((gx % cells.count_x) + cells.count_x) % cells.count_x;
            const std::size_t index = static_cast<std::size_t>(wy) * static_cast<std::size_t>(cells.count_x) + static_cast<std::size_t>(wx);
            const double dx = px - (gx + cells.point_x[index]) * cells.size_x;
            const double dy = py - (gy + cells.point_y[index]) * cells.size_y;
            const double distance2 = dx * dx + dy * dy;
            if (distance2 < first) {
                second = first;
                first = distance2;
            } else if (distance2 < second) {
                second = distance2;
            }
        }
    }
    return (std::sqrt(second) - std::sqrt(first)) * 0.5;
}

// A dry crust split into plates. The first cracks open wide round large plates;
// finer ones split the plates again and fade out partway. Plate rims curl up.
void crack_crust(Field& field, const Parameters& p, std::uint32_t seed) {
    if (p.cracks <= 0.0) return;
    const double tpm = 1.0 / field.metres_per_texel;
    const double plate = std::max(4.0, (0.09 + 0.08 * (1.0 - p.cracks)) * tpm);
    const Cells primary = make_cells(field.width, field.height, plate, seed + 29u);
    const Cells secondary = make_cells(field.width, field.height, plate * 0.42, seed + 37u);
    const double wide = std::max(0.5, (0.0015 + 0.0045 * p.cracks) * tpm);
    const double crack_depth = 0.008 + 0.012 * p.cracks;
    const double curl = 0.0035 * p.cracks;
    // Along a crack its width swells and narrows; the fine cracks run out.
    const Noise swell = make_noise(field.width, field.height, std::max(3.0, plate * 0.35), seed + 31u);
    const Noise reach = make_noise(field.width, field.height, std::max(3.0, plate * 0.6), seed + 33u);
    for (int y = 0; y < field.height; ++y) {
        for (int x = 0; x < field.width; ++x) {
            const double px = x + 0.5;
            const double py = y + 0.5;
            const double width_here = wide * (0.35 + 1.1 * sample(swell, px, py));
            const double fine_open = smoothstep(0.45, 0.7, sample(reach, px, py) + 0.2 * p.cracks);
            const double edge_one = cell_edge(primary, px, py);
            const double edge_two = cell_edge(secondary, px, py);
            // Coverage of the crack within this texel, so hairline cracks fade rather than alias.
            const double one = smoothstep(width_here + 0.5, std::max(0.0, width_here - 0.5), edge_one) * std::min(1.0, width_here * 1.6);
            const double fine_width = width_here * 0.5 * fine_open;
            const double two = fine_open > 0.0 ? smoothstep(fine_width + 0.5, std::max(0.0, fine_width - 0.5), edge_two) * std::min(1.0, fine_width * 1.6) * 0.7 : 0.0;
            const double crack = std::max(one, two);
            const std::size_t at = static_cast<std::size_t>(y) * field.width + x;
            if (crack > 0.02) {
                field.relief[at] -= static_cast<float>(crack_depth * crack);
                if (crack > 0.5) field.kind[at] = kCrack;
                field.damp[at] = static_cast<float>(0.5 * crack);
            }
            // Each plate lifts a little at its rim.
            const double rim = std::exp(-std::max(0.0, edge_one - width_here) * 0.8 / std::max(1.0, width_here));
            field.relief[at] += static_cast<float>(curl * rim * (1.0 - crack));
        }
    }
}

// Clods and crumbs, largest first so the small ones rest on the large.
void scatter_clods(Field& field, const Parameters& p, const Palette& palette, Random& random,
                   double area_scale, const Field* envelope) {
    const double tpm = 1.0 / field.metres_per_texel;
    const double relief = std::max(0.0, p.relief);
    const double area_m2 = field.width * field.metres_per_texel * field.height * field.metres_per_texel * area_scale;
    const double loose = clamp01(p.looseness);
    const double tilth = clamp01(p.tilth);
    struct Band {
        double small;      // radius, metres
        double large;
        double coverage;   // share of the ground it covers
    };
    // Coarse clods, small clods, crumbs and fine crumbs. A worked soil is all
    // aggregate: together they cover the ground more than once over.
    const Band bands[4] = {
        {0.02 + 0.02 * tilth, 0.03 + 0.06 * tilth, (0.06 + 0.5 * tilth) * loose * loose},
        {0.01, 0.022 + 0.01 * tilth, (0.25 + 0.4 * tilth) * (0.2 + 0.8 * loose)},
        {0.0045, 0.01, 0.75 * (0.25 + 0.75 * loose)},
        {0.002, 0.0045, 0.9 * (0.4 + 0.6 * loose)},
    };
    // A lump under a texel and a half is a single bump: the mottling stands for it.
    const double min_radius_texels = 1.0;
    for (int band_index = 0; band_index < 4; ++band_index) {
        const Band band = bands[band_index];
        if (band.coverage <= 0.0) continue;
        double small = band.small;
        const double large = band.large;
        if (large * tpm < min_radius_texels) continue;  // finer than a texel: left to the mottling
        small = std::max(small, min_radius_texels / tpm);
        const double mean_area = kPi * (small * small + small * large + large * large) / 3.0;
        const int count = static_cast<int>(std::min(400000.0, band.coverage * area_m2 / mean_area));
        for (int index = 0; index < count; ++index) {
            Lump lump;
            lump.x = random.range(0.0, field.width);
            lump.y = random.range(0.0, field.height);
            double weight = 1.0;
            if (envelope != nullptr) {
                const std::ptrdiff_t at = (*envelope).index(static_cast<int>(lump.x), static_cast<int>(lump.y));
                weight = at < 0 ? 0.0 : static_cast<double>((*envelope).damp[static_cast<std::size_t>(at)]);
                if (random.unit() >= weight) continue;
            }
            const double radius_m = lerp(small, large, std::pow(random.unit(), 1.6));
            lump.radius = radius_m * tpm;
            lump.aspect = random.range(0.6, 1.0);
            lump.angle = random.range(0.0, kPi);
            const double flat = lerp(0.3, 0.85, loose);
            lump.rise = radius_m * flat * random.range(0.7, 1.2) * relief;
            const std::ptrdiff_t centre = field.index(static_cast<int>(lump.x), static_cast<int>(lump.y));
            const double ground = centre < 0 ? 0.0 : static_cast<double>(field.relief[static_cast<std::size_t>(centre)]);
            lump.base = ground - lump.rise * lerp(0.8, 0.35, loose);
            lump.roundness = random.range(0.3, 0.6);
            lump.wobble[0] = random.range(0.0, 0.25);
            lump.wobble[1] = random.range(0.0, 0.16);
            lump.wobble[2] = random.range(0.0, 0.09);
            for (int k = 0; k < 3; ++k) lump.phase[k] = random.range(0.0, 2.0 * kPi);
            const double facet = lump.rise / std::max(1.0, lump.radius) * 0.45;
            lump.tilt_x = random.range(-facet, facet);
            lump.tilt_y = random.range(-facet, facet);
            // Each clod is its own shade: some turned up damper and redder from below,
            // some dried paler and greyer on the surface.
            const double shade = random.range(0.82, 1.12);
            Linear colour = scaled(palette.soil, shade);
            colour.r *= random.range(0.95, 1.06);
            colour.b *= random.range(0.9, 1.08);
            lump.colour = saturated(colour, random.range(0.88, 1.1));
            if (band_index < 2 && random.unit() < p.subsoil * 0.45) lump.colour = scaled(palette.subsoil, random.range(0.85, 1.1));
            lump.what = kSoil;
            lump.wet = random.range(-0.08, 0.12) + (band_index == 0 ? 0.08 : 0.0);
            lump.marks_presence = envelope != nullptr;
            stamp(field, lump);
        }
    }
}

void scatter_pebbles(Field& field, const Parameters& p, const Palette& palette, Random& random, double area_scale,
                     const Field* envelope) {
    if (p.pebbles <= 0.0) return;
    const double tpm = 1.0 / field.metres_per_texel;
    const double area_m2 = field.width * field.metres_per_texel * field.height * field.metres_per_texel * area_scale;
    const int count = static_cast<int>(std::min(50000.0, p.pebbles * area_m2 * 90.0));
    const double sink = lerp(0.7, 0.35, clamp01(p.looseness));
    for (int index = 0; index < count; ++index) {
        Lump lump;
        lump.x = random.range(0.0, field.width);
        lump.y = random.range(0.0, field.height);
        if (envelope != nullptr) {
            const std::ptrdiff_t at = (*envelope).index(static_cast<int>(lump.x), static_cast<int>(lump.y));
            const double weight = at < 0 ? 0.0 : static_cast<double>((*envelope).damp[static_cast<std::size_t>(at)]);
            if (random.unit() >= weight) continue;
        }
        const double radius_m = 0.004 + 0.018 * std::pow(random.unit(), 2.2);
        // A stone smaller than a texel is drawn a texel wide, so draw fewer of them.
        const double true_radius = radius_m * tpm;
        const double keep = std::min(1.0, (true_radius / 0.6) * (true_radius / 0.6));
        if (random.unit() >= keep) continue;
        lump.radius = std::max(0.6, true_radius);
        lump.aspect = random.range(0.55, 0.95);
        lump.angle = random.range(0.0, kPi);
        lump.rise = radius_m * random.range(0.5, 0.8);
        const std::ptrdiff_t centre = field.index(static_cast<int>(lump.x), static_cast<int>(lump.y));
        const double ground = centre < 0 ? 0.0 : static_cast<double>(field.relief[static_cast<std::size_t>(centre)]);
        lump.base = ground - lump.rise * sink;
        lump.roundness = random.range(0.45, 0.7);
        lump.wobble[0] = random.range(0.0, 0.08);
        lump.wobble[1] = random.range(0.0, 0.05);
        const int stone = static_cast<int>(random.unit() * 4.999);
        lump.colour = scaled(palette.stones[stone], random.range(0.82, 1.12));
        lump.what = kPebble;
        lump.wet = 0.0;
        lump.shine = random.range(0.25, 0.55);
        lump.marks_presence = envelope != nullptr;
        stamp(field, lump);
    }
}

// Grains of sand and grit a texel across: mostly the soil's own mineral colour,
// some pale quartz, some dark. Never more than one texel in sixty.
void scatter_grit(Field& field, const Parameters& p, const Palette& palette, Random& random) {
    if (p.grit <= 0.0) return;
    const double texels = static_cast<double>(field.width) * field.height;
    const double area_m2 = texels * field.metres_per_texel * field.metres_per_texel;
    // A grain is a few millimetres: where a texel is larger, fewer texels show one.
    const double grain = std::min(1.0, 0.004 / field.metres_per_texel);
    const int count = static_cast<int>(std::min(p.grit * area_m2 * 500.0 * grain * grain, texels / 60.0));
    const Linear sand = mixed(palette.soil, linear_of(Colour{0.78, 0.70, 0.56}), 0.6);
    const Linear quartz = linear_of(Colour{0.80, 0.78, 0.74});
    const Linear dark = linear_of(Colour{0.16, 0.14, 0.13});
    for (int index = 0; index < count; ++index) {
        const int x = static_cast<int>(random.unit() * field.width);
        const int y = static_cast<int>(random.unit() * field.height);
        const double pick = random.unit();
        const double shade = random.range(0.8, 1.15);
        const std::ptrdiff_t at = field.index(x, y);
        if (at < 0) continue;
        const std::size_t i = static_cast<std::size_t>(at);
        if (field.kind[i] != kSoil || field.presence[i] < 0.5f) continue;
        const Linear colour = scaled(pick < 0.55 ? sand : pick < 0.75 ? quartz : dark, shade);
        field.paint(i, colour, kGrit, -0.2, pick >= 0.55 && pick < 0.75 ? 0.4 : 0.1);
        field.relief[i] += static_cast<float>(field.metres_per_texel * 0.2);
    }
}

double ground_under(const Field& field, double x, double y) {
    const std::ptrdiff_t at = field.index(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)));
    const double ground = at < 0 ? 0.0 : static_cast<double>(field.relief[static_cast<std::size_t>(at)]);
    return ground;
}

// Straw, twigs and leaves. Things thinner than a texel are drawn a texel wide and
// fewer, so the share of ground they cover stays true at any resolution.
void scatter_litter(Field& field, const Parameters& p, const Palette& palette, Random& random, double area_scale) {
    const double tpm = 1.0 / field.metres_per_texel;
    const double area_m2 = field.width * field.metres_per_texel * field.height * field.metres_per_texel * area_scale;
    // Leaves first: twigs and straw lie over them as often as under.
    if (p.leaves > 0.0) {
        const int count = static_cast<int>(std::min(20000.0, p.leaves * area_m2 * 70.0));
        for (int index = 0; index < count; ++index) {
            Leaf leaf;
            leaf.x = random.range(0.0, field.width);
            leaf.y = random.range(0.0, field.height);
            const double length_m = random.range(0.035, 0.09);
            leaf.length = std::max(2.5, length_m * tpm);
            leaf.width = leaf.length * random.range(0.42, 0.66);
            leaf.angle = random.range(0.0, 2.0 * kPi);
            const double half = leaf.length * 0.5;
            const double tip_x = leaf.x + std::cos(leaf.angle) * half;
            const double tip_y = leaf.y + std::sin(leaf.angle) * half;
            const double tail_x = leaf.x - std::cos(leaf.angle) * half;
            const double tail_y = leaf.y - std::sin(leaf.angle) * half;
            leaf.base = std::max({ground_under(field, leaf.x, leaf.y), ground_under(field, tip_x, tip_y),
                                  ground_under(field, tail_x, tail_y)}) - 0.002;
            leaf.curl = random.range(0.001, 0.008);
            // Fallen leaves: the season's colour, some yellower, some redder, many
            // already browned and faded.
            const double pick = random.unit();
            const Linear yellow = linear_of(Colour{0.80, 0.62, 0.20});
            const Linear red = linear_of(Colour{0.62, 0.20, 0.10});
            const Linear brown = linear_of(Colour{0.45, 0.30, 0.17});
            Linear colour = pick < 0.35 ? palette.leaf : pick < 0.55 ? mixed(palette.leaf, yellow, 0.7) : pick < 0.68 ? mixed(palette.leaf, red, 0.6) : mixed(palette.leaf, brown, 0.75);
            colour = scaled(colour, random.range(0.8, 1.12));
            colour.g *= random.range(0.9, 1.1);
            leaf.colour = colour;
            leaf.rib = scaled(colour, 0.7);
            stamp(field, leaf);
        }
    }
    if (p.twigs > 0.0) {
        const double true_radius = 0.0022;
        const double drawn = std::max(0.55, true_radius * tpm);
        const double thinning = std::min(1.0, true_radius * tpm / drawn);
        const int count = static_cast<int>(std::min(20000.0, p.twigs * area_m2 * 14.0 * std::sqrt(thinning)));
        for (int index = 0; index < count; ++index) {
            Stick stick;
            stick.points = 2 + static_cast<int>(random.unit() * 3.0);
            const double length = random.range(0.04, 0.14) * tpm;
            double heading = random.range(0.0, 2.0 * kPi);
            stick.x[0] = random.range(0.0, field.width);
            stick.y[0] = random.range(0.0, field.height);
            for (int k = 1; k < stick.points; ++k) {
                heading += random.range(-0.5, 0.5);
                stick.x[k] = stick.x[k - 1] + std::cos(heading) * length / (stick.points - 1);
                stick.y[k] = stick.y[k - 1] + std::sin(heading) * length / (stick.points - 1);
            }
            stick.radius = std::max(drawn, random.range(0.0015, 0.004) * tpm);
            stick.rise = stick.radius * field.metres_per_texel * 1.2;
            stick.base = ground_under(field, stick.x[0], stick.y[0]) + stick.rise * 0.2;
            const Linear bark = mixed(palette.twig, palette.twig_grey, random.unit());
            stick.colour = scaled(bark, random.range(0.85, 1.15));
            stick.edge = scaled(stick.colour, 0.6);
            stick.what = kTwig;
            stick.shine = 0.05;
            stamp(field, stick);
            // Now and then a side shoot.
            if (stick.points > 2 && random.unit() < 0.5) {
                Stick shoot = stick;
                shoot.points = 2;
                const double fork = std::atan2(stick.y[2] - stick.y[1], stick.x[2] - stick.x[1]) + (random.unit() < 0.5 ? -0.7 : 0.7);
                shoot.x[0] = stick.x[1];
                shoot.y[0] = stick.y[1];
                shoot.x[1] = stick.x[1] + std::cos(fork) * length * 0.3;
                shoot.y[1] = stick.y[1] + std::sin(fork) * length * 0.3;
                shoot.radius = std::max(drawn, stick.radius * 0.7);
                stamp(field, shoot);
            }
        }
    }
    if (p.straw > 0.0) {
        const double true_radius = 0.0016;
        const double drawn = std::max(0.5, true_radius * tpm);
        const double thinning = std::min(1.0, true_radius * tpm / drawn);
        const int count = static_cast<int>(std::min(40000.0, p.straw * area_m2 * 60.0 * std::sqrt(thinning)));
        for (int index = 0; index < count; ++index) {
            Stick stick;
            stick.points = 3;
            const double length = random.range(0.025, 0.09) * tpm;
            const double heading = random.range(0.0, 2.0 * kPi);
            const double bend = random.range(-0.25, 0.25);
            stick.x[0] = random.range(0.0, field.width);
            stick.y[0] = random.range(0.0, field.height);
            stick.x[1] = stick.x[0] + std::cos(heading) * length * 0.5;
            stick.y[1] = stick.y[0] + std::sin(heading) * length * 0.5;
            stick.x[2] = stick.x[1] + std::cos(heading + bend) * length * 0.5;
            stick.y[2] = stick.y[1] + std::sin(heading + bend) * length * 0.5;
            stick.radius = drawn;
            stick.rise = std::max(0.0015, stick.radius * field.metres_per_texel);
            stick.base = std::max(ground_under(field, stick.x[0], stick.y[0]), ground_under(field, stick.x[2], stick.y[2])) - 0.001;
            Linear colour = scaled(palette.straw, random.range(0.78, 1.1));
            colour = mixed(colour, linear_of(Colour{0.62, 0.55, 0.40}), random.unit() * 0.5);  // weathered
            stick.colour = colour;
            stick.edge = scaled(colour, 0.72);
            stick.what = kStraw;
            stick.shine = 0.3;
            stamp(field, stick);
        }
    }
}

// Rake tines drag through whatever lies on top.
void rake(Field& field, const Parameters& p, std::uint32_t seed) {
    const double tpm = 1.0 / field.metres_per_texel;
    const Wave wave = fit_wave(field.width, field.height, p.rake_angle, p.rake_spacing * tpm);
    if (!wave.present) return;
    const Noise wander = make_noise(field.width, field.height, std::max(4.0, 0.3 * tpm), seed + 41u);
    const Noise lift = make_noise(field.width, field.height, std::max(4.0, 0.25 * tpm), seed + 43u);
    const double depth = p.rake_depth * std::max(0.0, p.relief);
    for (int y = 0; y < field.height; ++y) {
        for (int x = 0; x < field.width; ++x) {
            const double phase = x * wave.kx + y * wave.ky + (sample(wander, x, y) - 0.5) * 0.35;
            const double groove = std::pow(0.5 + 0.5 * std::cos(2.0 * kPi * phase), 3.0);
            // The rake rides up over stones and lifts off in places.
            const double pressure = smoothstep(0.2, 0.55, sample(lift, x, y));
            const std::size_t at = static_cast<std::size_t>(y) * field.width + x;
            if (field.kind[at] == kPebble) continue;
            field.relief[at] -= static_cast<float>(depth * groove * pressure);
        }
    }
}

// Sub-texel crumb: what is too small to stamp becomes mottling in height and shade.
void mottle(Field& field, const Parameters& p, const Palette& palette, std::uint32_t seed) {
    const double tpm = 1.0 / field.metres_per_texel;
    const Noise fine = make_noise(field.width, field.height, std::max(1.5, 0.006 * tpm), seed + 51u);
    const Noise mid = make_noise(field.width, field.height, std::max(2.5, 0.03 * tpm), seed + 53u);
    const Fractal broad = make_fractal(field.width, field.height, std::max(6.0, 0.5 * tpm), seed + 57u);
    const double amplitude = (0.0015 + 0.002 * clamp01(p.looseness)) * std::max(0.0, p.relief);
    for (int y = 0; y < field.height; ++y) {
        for (int x = 0; x < field.width; ++x) {
            const std::size_t at = static_cast<std::size_t>(y) * field.width + x;
            const double f = sample(fine, x, y) - 0.5;
            const double m = sample(mid, x, y) - 0.5;
            // Broad patches where the soil is a little paler or darker (worn, turned, washed).
            const double tone = 1.0 + sample(broad, x, y) * 0.22;
            if (field.kind[at] == kSoil || field.kind[at] == kCrack) {
                field.relief[at] += static_cast<float>(f * amplitude);
                if (field.red[at] <= 0.0f && field.green[at] <= 0.0f) {
                    // Ground no clod covers: the finer, damper soil between them.
                    const Linear colour = scaled(palette.soil, (0.84 + m * 0.25) * tone);
                    field.red[at] = static_cast<float>(colour.r);
                    field.green[at] = static_cast<float>(colour.g);
                    field.blue[at] = static_cast<float>(colour.b);
                    if (field.kind[at] == kSoil) field.damp[at] = static_cast<float>(0.1 + m * 0.2);
                } else {
                    const double k = (1.0 + f * 0.12) * tone;
                    field.red[at] = static_cast<float>(field.red[at] * k);
                    field.green[at] = static_cast<float>(field.green[at] * k);
                    field.blue[at] = static_cast<float>(field.blue[at] * k);
                }
            }
        }
    }
}

// A separable box blur of the relief, periodic or clamped like the field.
void blur_relief(const Field& field, int radius, std::vector<float>& scratch, std::vector<float>& out) {
    const int w = field.width;
    const int h = field.height;
    const std::size_t count = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    scratch.assign(count, 0.0f);
    out.assign(count, 0.0f);
    const double norm = 1.0 / (2.0 * radius + 1.0);
    for (int y = 0; y < h; ++y) {
        double sum = 0.0;
        for (int k = -radius; k <= radius; ++k) sum += field.relief_at(k, y);
        for (int x = 0; x < w; ++x) {
            scratch[static_cast<std::size_t>(y) * w + x] = static_cast<float>(sum * norm);
            sum += field.relief_at(x + radius + 1, y) - field.relief_at(x - radius, y);
        }
    }
    for (int x = 0; x < w; ++x) {
        double sum = 0.0;
        for (int k = -radius; k <= radius; ++k) sum += scratch[field.nearest(x, k)];
        for (int y = 0; y < h; ++y) {
            out[static_cast<std::size_t>(y) * w + x] = static_cast<float>(sum * norm);
            sum += static_cast<double>(scratch[field.nearest(x, y + radius + 1)]) -
                   static_cast<double>(scratch[field.nearest(x, y - radius)]);
        }
    }
}

struct Direction {
    double x = 0.0;
    double y = 0.0;
    double z = 1.0;
};
Direction direction(double azimuth, double elevation) {
    const Direction d{std::cos(azimuth) * std::cos(elevation), std::sin(azimuth) * std::cos(elevation),
                      std::sin(elevation)};
    return d;
}

// Lights the field and writes the picture. Spoil pictures also receive the
// shadow the heap throws on the ground around it.
void shade(const Field& field, const Parameters& p, std::uint32_t seed, Image& out) {
    const int w = field.width;
    const int h = field.height;
    const double mpt = field.metres_per_texel;
    const double tpm = 1.0 / mpt;
    const std::size_t count = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    std::vector<float> scratch;
    std::vector<float> near_blur;
    std::vector<float> far_blur;
    std::vector<float> snow_blur;
    blur_relief(field, std::max(1, static_cast<int>(std::lround(0.012 * tpm))), scratch, near_blur);
    blur_relief(field, std::max(2, static_cast<int>(std::lround(0.05 * tpm))), scratch, far_blur);
    blur_relief(field, std::max(1, static_cast<int>(std::lround(0.02 * tpm))), scratch, snow_blur);

    const Direction light = direction(p.light_azimuth, p.light_elevation);
    const Direction view = direction(p.view_azimuth, p.view_elevation);
    Direction half{light.x + view.x, light.y + view.y, light.z + view.z};
    const double half_length = std::sqrt(half.x * half.x + half.y * half.y + half.z * half.z);
    half = Direction{half.x / half_length, half.y / half_length, half.z / half_length};
    const double planar = std::max(1e-6, std::sqrt(light.x * light.x + light.y * light.y));
    const double step_x = light.x / planar;
    const double step_y = light.y / planar;
    const double climb = light.z / planar;  // metres of rise per metre toward the light
    const double sky = 0.42;
    const double sun = 0.85;
    const double flat = sky + sun * light.z;

    const Fractal wetness = make_fractal(w, h, 0.4 * tpm, seed + 61u);
    const Noise flake = make_noise(w, h, std::max(2.0, 0.015 * tpm), seed + 67u);
    const Fractal drift = make_fractal(w, h, 0.25 * tpm, seed + 71u);
    const Linear frost_colour = linear_of(Colour{0.86, 0.90, 0.96});
    const Linear snow_colour = linear_of(Colour{0.94, 0.96, 1.0});
    const Linear snow_shadow = linear_of(Colour{0.68, 0.76, 0.92});
    const double moisture = clamp01(p.moisture);
    const double patches = clamp01(p.moisture_patches);

    // Shadow rays: short steps near the texel, longer ones farther away.
    double reach[10];
    for (int k = 0; k < 10; ++k) reach[k] = 0.8 * std::pow(1.4, k);
    const double longest = std::max(3.0, 0.12 * tpm);

    out.width = w;
    out.height = h;
    out.rgba.assign(count * 4, 0);
    out.relief.assign(field.relief.begin(), field.relief.end());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t at = static_cast<std::size_t>(y) * w + x;
            const double height0 = field.relief[at];
            const double gx = (field.relief_at(x + 1, y) - field.relief_at(x - 1, y)) * 0.5 * tpm;
            const double gy = (field.relief_at(x, y + 1) - field.relief_at(x, y - 1)) * 0.5 * tpm;
            const double nl = 1.0 / std::sqrt(gx * gx + gy * gy + 1.0);
            const Direction normal{-gx * nl, -gy * nl, nl};

            // Soft self-shadow: how far the ground toward the light rises above the ray.
            double occlusion = 0.0;
            for (int k = 0; k < 10; ++k) {
                const double d = reach[k];
                if (d > longest) break;
                const double sample_height = field.relief_bilinear(x + step_x * d, y + step_y * d);
                const double ray = height0 + d * mpt * climb;
                const double over = (sample_height - ray) / (0.12 * d * mpt + 0.0008);
                occlusion = std::max(occlusion, over);
            }
            const double lit = 1.0 - smoothstep(0.0, 1.0, occlusion);
            const double diffuse = std::max(0.0, normal.x * light.x + normal.y * light.y + normal.z * light.z);

            // Crevices: below the local average is darker, above it a touch brighter.
            const double near_cavity = (static_cast<double>(near_blur[at]) - height0) / 0.006;
            const double far_cavity = (static_cast<double>(far_blur[at]) - height0) / 0.02;
            double ambient = 1.0 - 0.45 * clamp01(near_cavity) - 0.3 * clamp01(far_cavity) + 0.1 * clamp01(-near_cavity);
            ambient *= 0.55 + 0.45 * normal.z;
            ambient = std::max(0.12, ambient);

            // Moisture: patches, damper in hollows and cracks, drier on the tops.
            double wet = moisture + patches * sample(wetness, x, y) * 1.2 + static_cast<double>(field.damp[at]) +
                         0.18 * clamp01(far_cavity) - 0.12 * clamp01(-far_cavity) * (1.0 - moisture);
            wet = clamp01(wet);
            const std::uint8_t what = field.kind[at];
            Linear albedo{field.red[at], field.green[at], field.blue[at]};
            double gloss = field.gloss[at];
            if (what == kSoil || what == kCrack || what == kGrit) {
                // Damp soil is about half as bright and richer in colour.
                albedo = saturated(scaled(albedo, 1.0 - 0.58 * std::pow(wet, 0.9)), 1.0 + 0.18 * wet);
                if (what == kCrack) albedo = scaled(albedo, 0.55);
                gloss += 0.35 * smoothstep(0.55, 1.0, wet);
            } else {
                albedo = scaled(albedo, 1.0 - 0.3 * wet);
                gloss += 0.25 * wet;
            }

            double light_amount = (sky * ambient + sun * diffuse * lit) / flat;
            Linear colour = scaled(albedo, light_amount);
            // Glints: wet crumbs and polished stones catch the light, a few at a time.
            const double nh = std::max(0.0, normal.x * half.x + normal.y * half.y + normal.z * half.z);
            const double chance = static_cast<double>(mix32(static_cast<std::uint32_t>(at) * 3u ^ seed) & 0xFFFFu) / 65535.0;
            const bool stone = what == kPebble || what == kGrit;
            const double sparkle = stone ? 1.0 : smoothstep(0.86, 0.97, chance);
            const double glint = gloss * sparkle * std::pow(nh, stone ? 60.0 : 140.0) * lit * (stone ? 0.45 : 0.8);
            colour.r += glint;
            colour.g += glint;
            colour.b += glint;

            // Hoar frost: a fur of white crystals on the tops and the edges facing the
            // sky; the soil shows through it, and the hollows stay dark.
            if (p.frost > 0.0) {
                const double convex = clamp01(0.3 - near_cavity * 0.7);
                const double crystal = static_cast<double>(mix32(static_cast<std::uint32_t>(at) ^ seed) & 0xFFFFu) / 65535.0;
                double cover = p.frost * smoothstep(0.5, 0.95, normal.z) * (0.15 + 0.85 * convex);
                cover *= 0.6 + 0.4 * crystal;
                if (what == kCrack) cover *= 0.15;
                cover = std::min(0.8, cover);
                Linear frosted = scaled(frost_colour, (0.62 + 0.38 * lit) * std::min(1.0, ambient * 1.25));
                if (crystal > 0.985 - 0.02 * p.frost && lit > 0.6 && cover > 0.3) frosted = scaled(frost_colour, 1.25);
                colour = mixed(colour, frosted, cover);
            }
            // Snow settles on what faces up, then fills the hollows. Its edge is
            // granular, but it lies in patches, not as a sprinkling.
            if (p.snow > 0.0) {
                const double depth = p.snow * (0.55 + 1.1 * (sample(drift, x, y) + 0.5));
                const double sgx = (static_cast<double>(snow_blur[field.nearest(x + 1, y)]) -
                                    static_cast<double>(snow_blur[field.nearest(x - 1, y)])) * 0.5 * tpm;
                const double sgy = (static_cast<double>(snow_blur[field.nearest(x, y + 1)]) -
                                    static_cast<double>(snow_blur[field.nearest(x, y - 1)])) * 0.5 * tpm;
                const double snl = 1.0 / std::sqrt(sgx * sgx + sgy * sgy + 1.0);
                // Wind and gravity take it to the hollows first; then it caps what faces up.
                const double hollow = (static_cast<double>(far_blur[at]) - height0) / 0.01;
                const double steep = 1.0 - normal.z;
                double cover = depth * 1.2 + hollow * 0.6 - steep * 3.2 - 0.35;
                cover += (sample(flake, x, y) - 0.5) * 0.2;
                cover = smoothstep(0.0, 0.2, cover);
                const double snow_diffuse = std::max(0.0, (-sgx * light.x - sgy * light.y + light.z) * snl);
                const double snow_lit = 0.3 + 0.7 * lit;
                Linear snowy = mixed(snow_shadow, snow_colour, clamp01(snow_diffuse * snow_lit * 1.15));
                snowy = scaled(snowy, 0.8 + 0.2 * std::min(1.0, ambient * 1.2));
                colour = mixed(colour, snowy, cover);
            }

            std::uint8_t* pixel = &out.rgba[at * 4];
            pixel[0] = to_srgb8(colour.r);
            pixel[1] = to_srgb8(colour.g);
            pixel[2] = to_srgb8(colour.b);
            pixel[3] = 255;
            if (!field.wraps && field.presence[at] < 0.5f) {
                // Bare ground beside a heap: only the heap's shadow is drawn.
                const double shadow = (1.0 - lit) * 0.55;
                pixel[0] = 12;
                pixel[1] = 9;
                pixel[2] = 6;
                pixel[3] = static_cast<std::uint8_t>(std::lround(clamp01(shadow) * 255.0));
            }
        }
    }
}

bool valid_side(int side) { return side >= 4 && side <= max_side; }

double finite_or(double value, double fallback) { return std::isfinite(value) ? value : fallback; }
double unit_or(double value, double fallback) { return clamp01(finite_or(value, fallback)); }

// Parameters come from callers: clamp each to its range, and replace what is not a number.
Parameters sanitized(const Parameters& p) {
    const Parameters d;
    Parameters q = p;
    q.texels_per_metre = std::clamp(finite_or(p.texels_per_metre, d.texels_per_metre), 4.0, 4000.0);
    q.colour = Colour{unit_or(p.colour.red, d.colour.red), unit_or(p.colour.green, d.colour.green),
                      unit_or(p.colour.blue, d.colour.blue)};
    q.leaf_colour = Colour{unit_or(p.leaf_colour.red, d.leaf_colour.red), unit_or(p.leaf_colour.green, d.leaf_colour.green),
                           unit_or(p.leaf_colour.blue, d.leaf_colour.blue)};
    q.moisture = unit_or(p.moisture, d.moisture);
    q.moisture_patches = unit_or(p.moisture_patches, d.moisture_patches);
    q.tilth = unit_or(p.tilth, d.tilth);
    q.looseness = unit_or(p.looseness, d.looseness);
    q.relief = std::clamp(finite_or(p.relief, d.relief), 0.0, 4.0);
    q.furrow_angle = finite_or(p.furrow_angle, 0.0);
    q.furrow_spacing = std::clamp(finite_or(p.furrow_spacing, 0.0), 0.0, 10.0);
    q.furrow_depth = std::clamp(finite_or(p.furrow_depth, d.furrow_depth), 0.0, 0.5);
    q.rake_angle = finite_or(p.rake_angle, 0.0);
    q.rake_spacing = std::clamp(finite_or(p.rake_spacing, 0.0), 0.0, 1.0);
    q.rake_depth = std::clamp(finite_or(p.rake_depth, d.rake_depth), 0.0, 0.05);
    q.pebbles = unit_or(p.pebbles, d.pebbles);
    q.grit = unit_or(p.grit, d.grit);
    q.straw = unit_or(p.straw, 0.0);
    q.twigs = unit_or(p.twigs, 0.0);
    q.leaves = unit_or(p.leaves, 0.0);
    q.cracks = unit_or(p.cracks, 0.0);
    q.frost = unit_or(p.frost, 0.0);
    q.snow = unit_or(p.snow, 0.0);
    q.light_azimuth = finite_or(p.light_azimuth, d.light_azimuth);
    q.light_elevation = std::clamp(finite_or(p.light_elevation, d.light_elevation), 0.1, 1.5707);
    q.view_azimuth = finite_or(p.view_azimuth, d.view_azimuth);
    q.view_elevation = std::clamp(finite_or(p.view_elevation, d.view_elevation), 0.1, 1.5707);
    return q;
}

Spoil sanitized(const Spoil& s) {
    const Spoil d;
    Spoil q = s;
    q.hole_radius = std::clamp(finite_or(s.hole_radius, d.hole_radius), 0.0, 10.0);
    q.rim_width = std::clamp(finite_or(s.rim_width, d.rim_width), 0.001, 10.0);
    q.rim_height = std::clamp(finite_or(s.rim_height, d.rim_height), 0.0, 1.0);
    q.fan_direction = finite_or(s.fan_direction, d.fan_direction);
    q.fan_spread = std::clamp(finite_or(s.fan_spread, d.fan_spread), 0.0, 3.2);
    q.fan_reach = std::clamp(finite_or(s.fan_reach, d.fan_reach), 0.001, 10.0);
    q.fan_height = std::clamp(finite_or(s.fan_height, d.fan_height), 0.0, 1.0);
    q.threshold = unit_or(s.threshold, d.threshold);
    return q;
}

}  // namespace

// ------------------------------------------------------------------ presets

const char* preset_name(Preset preset) {
    switch (preset) {
        case Preset::tilled_loam: return "Tilled loam";
        case Preset::raked_bed: return "Raked bed";
        case Preset::furrowed_rows: return "Furrowed rows";
        case Preset::compacted_path: return "Compacted path";
        case Preset::dry_crust: return "Dry crust";
        case Preset::damp_dark: return "Damp and dark";
        case Preset::leaf_litter: return "Leaf litter";
        case Preset::frosted: return "Frosted";
        case Preset::snow_dusted: return "Snow dusted";
        case Preset::fresh_spoil: return "Fresh spoil";
    }
    return "Soil";
}

Parameters preset(Preset preset) {
    Parameters p;
    switch (preset) {
        case Preset::tilled_loam:
            p.tilth = 0.65;
            p.subsoil = 0.1;
            p.moisture = 0.42;
            p.straw = 0.15;
            p.pebbles = 0.3;
            break;
        case Preset::raked_bed:
            p.tilth = 0.15;
            p.looseness = 0.85;
            p.moisture = 0.38;
            p.rake_spacing = 0.035;
            p.rake_depth = 0.007;
            p.pebbles = 0.25;
            break;
        case Preset::furrowed_rows:
            p.tilth = 0.4;
            p.moisture = 0.45;
            p.furrow_spacing = 0.3;
            p.furrow_depth = 0.07;
            p.straw = 0.1;
            break;
        case Preset::compacted_path:
            p.colour = Colour{0.58, 0.49, 0.39};
            p.tilth = 0.1;
            p.looseness = 0.08;
            p.moisture = 0.25;
            p.moisture_patches = 0.6;
            p.pebbles = 1.0;
            p.cracks = 0.15;
            p.grit = 0.8;
            p.relief = 0.6;
            break;
        case Preset::dry_crust:
            p.colour = Colour{0.60, 0.50, 0.40};
            p.tilth = 0.15;
            p.looseness = 0.25;
            p.moisture = 0.08;
            p.moisture_patches = 0.2;
            p.cracks = 0.8;
            p.pebbles = 0.2;
            break;
        case Preset::damp_dark:
            p.tilth = 0.55;
            p.moisture = 0.9;
            p.moisture_patches = 0.15;
            p.pebbles = 0.2;
            p.straw = 0.05;
            break;
        case Preset::leaf_litter:
            p.tilth = 0.45;
            p.moisture = 0.6;
            p.leaves = 0.7;
            p.twigs = 0.4;
            p.straw = 0.05;
            break;
        case Preset::frosted:
            p.tilth = 0.6;
            p.moisture = 0.55;
            p.frost = 0.85;
            break;
        case Preset::snow_dusted:
            p.tilth = 0.6;
            p.moisture = 0.6;
            p.frost = 0.3;
            p.snow = 0.45;
            break;
        case Preset::fresh_spoil:
            p.colour = Colour{0.44, 0.33, 0.24};
            p.subsoil = 0.35;
            p.tilth = 0.75;
            p.moisture = 0.78;
            p.moisture_patches = 0.1;
            p.pebbles = 0.45;
            p.grit = 0.3;
            p.twigs = 0.15;
            break;
    }
    return p;
}

void apply_season(Parameters& p, Season season) {
    switch (season) {
        case Season::spring:
            p.moisture = std::max(p.moisture, 0.55);
            p.cracks = 0.0;
            p.leaves = 0.0;
            p.frost = 0.0;
            p.snow = 0.0;
            break;
        case Season::summer:
            p.moisture = std::min(p.moisture, 0.22);
            p.moisture_patches = std::max(p.moisture_patches, 0.4);
            p.cracks = std::max(p.cracks, 0.3 * (1.0 - p.looseness * 0.6));
            p.straw = std::max(p.straw, 0.15);
            p.leaves = 0.0;
            p.frost = 0.0;
            p.snow = 0.0;
            break;
        case Season::autumn:
            p.moisture = std::max(p.moisture, 0.55);
            p.leaves = std::max(p.leaves, 0.35);
            p.twigs = std::max(p.twigs, 0.2);
            p.frost = 0.0;
            p.snow = 0.0;
            break;
        case Season::winter:
            p.moisture = std::max(p.moisture, 0.5);
            p.leaves = std::min(p.leaves, 0.08);
            p.frost = std::max(p.frost, 0.6);
            p.snow = std::max(p.snow, 0.35);
            break;
    }
}

// ------------------------------------------------------------------ generation

bool generate_surface(const Parameters& parameters, int width, int height, Image& out) {
    if (!valid_side(width) || !valid_side(height)) return false;
    const Parameters p = sanitized(parameters);
    const std::uint32_t seed = mix32(p.seed ^ 0xA5u);
    Random random(static_cast<std::uint64_t>(p.seed) * 0x2545F4914F6CDD1Dull + 0x51u);
    const Palette palette = make_palette(p);
    Field field;
    field.reset(width, height, true, p.texels_per_metre);
    lay_base(field, p, seed);
    crack_crust(field, p, seed);
    scatter_clods(field, p, palette, random, 1.0, nullptr);
    mottle(field, p, palette, seed);
    scatter_pebbles(field, p, palette, random, 1.0, nullptr);
    scatter_litter(field, p, palette, random, 1.0);
    rake(field, p, seed);
    scatter_grit(field, p, palette, random);
    Image image;
    shade(field, p, seed, image);
    out = std::move(image);
    return true;
}

bool generate_spoil(const Parameters& parameters, const Spoil& shape, int width, int height, Image& out) {
    if (!valid_side(width) || !valid_side(height)) return false;
    const Parameters p = sanitized(parameters);
    const Spoil spoil = sanitized(shape);
    const std::uint32_t seed = mix32(p.seed ^ 0x5Cu);
    Random random(static_cast<std::uint64_t>(p.seed) * 0x9E3779B97F4A7C15ull + 0x77u);
    const Palette palette = make_palette(p);
    const double tpm = p.texels_per_metre;
    const double cx = width * 0.5;
    const double cy = height * 0.5;
    const double hole = spoil.hole_radius * tpm;
    const double rim = std::max(1.0, spoil.rim_width * tpm);
    const double reach = std::max(1.0, spoil.fan_reach * tpm);

    // The heap's envelope: its height (relief) and how likely a clod lands there (damp).
    Field envelope;
    envelope.reset(width, height, false, tpm);
    std::vector<float> tread(envelope.relief.size(), 0.0f);  // 0..1 trodden flat
    const Fractal lumps = make_fractal(width, height, std::max(3.0, rim * 1.2), seed + 3u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double dx = x + 0.5 - cx;
            const double dy = y + 0.5 - cy;
            const double r = std::sqrt(dx * dx + dy * dy);
            const double beyond = r - hole;
            const std::size_t at = static_cast<std::size_t>(y) * width + x;
            if (beyond < 0.0) continue;
            double angle = std::atan2(dy, dx) - spoil.fan_direction;
            angle = std::remainder(angle, 2.0 * kPi);
            const double wobble = sample(lumps, x, y);
            const double ring_t = (beyond - rim * 0.45) / (rim * 0.6);
            const double ring = std::exp(-ring_t * ring_t) * spoil.rim_height;
            const double in_fan = 1.0 - smoothstep(spoil.fan_spread * 0.55, spoil.fan_spread * 1.1, std::fabs(angle) + wobble * 0.5);
            // The apron: nearly level, then a lobed toe where the thrown soil stopped.
            const double along = clamp01(beyond / (reach * (1.0 + wobble * 0.3)));
            const double fan = in_fan * spoil.fan_height * (1.0 - std::pow(along, 1.4)) * smoothstep(0.0, rim * 0.6, beyond);
            // Where the raccoons come and go the lip is packed down.
            const double core = 1.0 - smoothstep(spoil.fan_spread * 0.25, spoil.fan_spread * 0.7, std::fabs(angle));
            const double trodden = spoil.threshold * core * (1.0 - smoothstep(rim * 0.3, rim * 1.4, beyond));
            tread[at] = static_cast<float>(trodden);
            const double heap = std::max(ring, fan) * (1.0 + wobble * 0.8) * (1.0 - 0.45 * trodden);
            envelope.relief[at] = static_cast<float>(std::max(0.0, heap));
            // Crumbs land densely on the rim and the fan, and a few stray all round.
            const double ring_t2 = (beyond - rim * 0.45) / (rim * 0.6);
            const double ring_presence = std::exp(-ring_t2 * ring_t2);
            const double fan_presence = in_fan * std::pow(1.0 - along, 0.7) * (beyond < reach ? 1.0 : 0.0);
            const double stray = beyond < reach * 1.3 ? 0.035 * (1.0 - beyond / (reach * 1.3)) : 0.0;
            const double presence = std::max(ring_presence, fan_presence) * (1.15 + wobble * 0.6) + stray;
            envelope.damp[at] = static_cast<float>(clamp01(presence) * (1.0 - 0.85 * trodden));
        }
    }

    Field field;
    field.reset(width, height, false, tpm);
    const double solid = std::max(spoil.rim_height, spoil.fan_height) * 0.18;
    for (std::size_t at = 0; at < field.relief.size(); ++at) {
        const double heap = envelope.relief[at];
        if (heap > solid) {
            field.relief[at] = static_cast<float>(heap);
            field.presence[at] = 1.0f;
        }
    }
    // The heap is thrown loose: clods only where the envelope says, on top of it.
    scatter_clods(field, p, palette, random, 2.2, &envelope);
    mottle(field, p, palette, seed);
    // The trodden lip: smooth, firm and a shade paler than the loose spoil.
    const Linear packed = scaled(palette.soil, 1.04);
    for (std::size_t at = 0; at < field.relief.size(); ++at) {
        const double trodden = tread[at];
        if (trodden <= 0.02 || field.presence[at] < 0.5f) continue;
        field.relief[at] = static_cast<float>(lerp(field.relief[at], envelope.relief[at], trodden));
        const Linear here{field.red[at], field.green[at], field.blue[at]};
        field.paint(at, mixed(here, packed, trodden), kSoil, lerp(field.damp[at], -0.15, trodden), 0.0);
    }
    scatter_pebbles(field, p, palette, random, 1.0, &envelope);
    // Nothing hangs over the hole itself.
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double dx = x + 0.5 - cx;
            const double dy = y + 0.5 - cy;
            if (dx * dx + dy * dy < hole * hole * 0.94) {
                const std::size_t at = static_cast<std::size_t>(y) * width + x;
                field.presence[at] = 0.0f;
                field.relief[at] = 0.0f;
            }
        }
    }
    scatter_grit(field, p, palette, random);
    Image image;
    shade(field, p, seed, image);
    // The hole is the caller's to draw: leave it clear, shadow and all.
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double dx = x + 0.5 - cx;
            const double dy = y + 0.5 - cy;
            const std::size_t at = static_cast<std::size_t>(y) * width + x;
            if (dx * dx + dy * dy < hole * hole && field.presence[at] < 0.5f) image.rgba[at * 4 + 3] = 0;
        }
    }
    out = std::move(image);
    return true;
}

}  // namespace soil
