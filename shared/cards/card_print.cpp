#include "card_print.hpp"

#include "inflate.hpp"
#include "paper.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace games {
namespace {

constexpr double kPaperLevel = 238;    // the paper's average, sRGB 0..255
constexpr double kKnee = .92;          // linear light above which highlights roll off
constexpr double kGlareAt = .35;       // the glare's middle, along the card from the lamp's corner (0..1)
constexpr double kGlareWidth = .2;     // and its half-width there
const double kStock[3] = {251 / 255.0, 252 / 255.0, 252 / 255.0};  // what the artwork was printed on

double to_linear(double c) {
    const double v = c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4);
    return v;
}
double to_srgb(double x) {
    const double v = x <= .0031308 ? x * 12.92 : 1.055 * std::pow(std::max(x, 0.0), 1 / 2.4) - .055;
    return std::clamp(v, 0.0, 1.0);
}
// Linear below the knee; above it, a smooth approach to 1.
double roll_off(double x) {
    if (x < kKnee)
        return x;
    const double result = kKnee + (1 - kKnee) * std::tanh((x - kKnee) / (1 - kKnee));
    return result;
}

// sRGB bytes to linear light, and linear light to sRGB, by table: a card's picture is made in a
// fraction of a millisecond, so a deck is printed over a few frames.
constexpr int kEncodeSteps = 8192;
std::array<double, 256> make_linear_table() {
    std::array<double, 256> table{};
    for (int i = 0; i < 256; ++i)
        table[static_cast<std::size_t>(i)] = to_linear(i / 255.0);
    return table;
}
std::vector<double> make_encode_table() {
    std::vector<double> table(kEncodeSteps + 2);
    for (int i = 0; i <= kEncodeSteps + 1; ++i)
        table[static_cast<std::size_t>(i)] = to_srgb(static_cast<double>(i) / kEncodeSteps);
    return table;
}
double linear_of(std::uint8_t byte) {
    static const std::array<double, 256> table = make_linear_table();
    return table[byte];
}
// Linear light (0..1) to sRGB (0..1), interpolated in a fine table.
double encode(double x) {
    static const std::vector<double> table = make_encode_table();
    const double at = std::clamp(x, 0.0, 1.0) * kEncodeSteps;
    const int i = static_cast<int>(at);
    const double f = at - i;
    const double v = table[static_cast<std::size_t>(i)] * (1 - f) + table[static_cast<std::size_t>(i) + 1] * f;
    return v;
}

// The air-cushion finish: dimples pressed into the stock on a jittered hexagonal lattice
// turned 45 degrees, `pitch` device pixels apart, lit from the upper left, averaged over a
// few samples a pixel. Returns the light's deviation, in standard deviations, a pixel at a time.
// Finer than about two device pixels apart, dimples average away to tone; the pitch is set in
// the Settings screen (Emboss size).
std::uint32_t lattice_key(std::int64_t r, std::int64_t c) {
    std::uint32_t k = (static_cast<std::uint32_t>(r) * 73856093U) ^ (static_cast<std::uint32_t>(c) * 19349663U);
    k = (k ^ (k >> 13)) * 0x5bd1e995U;
    return k;
}
// The light on the finish at (x, y), device pixels: 1 on the flat stock.
double dimple_light(double x, double y, double pitch) {
    constexpr double kRoot = .70710678;
    constexpr double kLight[3] = {-.45, -.6, .66};
    const double X = (x + y) * kRoot / pitch, Y = (y - x) * kRoot / pitch;  // turned, in pitches
    const double row_h = .866;
    const std::int64_t row = static_cast<std::int64_t>(std::floor(Y / row_h));
    double gX = 0, gY = 0;
    for (std::int64_t r = row - 1; r <= row + 1; ++r) {
        const double shift = (r & 1) ? .5 : 0.0;
        const std::int64_t col = static_cast<std::int64_t>(std::floor(X - shift));
        for (std::int64_t c = col - 1; c <= col + 1; ++c) {
            const std::uint32_t k = lattice_key(r, c);
            const double jit = ((k >> 8) & 255U) / 255.0, jit2 = ((k >> 16) & 255U) / 255.0;
            const double cx = c + shift + .5 + (jit - .5) * .45;
            const double cy = r * row_h + row_h * .5 + (jit2 - .5) * row_h * .45;
            const double d2 = ((X - cx) * (X - cx) + (Y - cy) * (Y - cy)) / .2;
            if (d2 >= 1)
                continue;
            // depth -(1 - d2)^2 * amplitude; its slope, in pitches
            const double amplitude = .9 * (.75 + .5 * jit);
            const double slope = 2 * amplitude * (1 - d2) * 2 / .2;
            gX += slope * (X - cx);
            gY += slope * (Y - cy);
        }
    }
    // back to the screen's axes, with the steepness the offline bake used
    const double gx = (gX - gY) * kRoot, gy = (gX + gY) * kRoot;
    const double nx = -gx * 1.5625, ny = -gy * 1.5625;
    const double lit = (nx * kLight[0] + ny * kLight[1] + kLight[2]) / std::sqrt(nx * nx + ny * ny + 1) / kLight[2];
    return lit;
}
std::vector<float> make_emboss(int w, int h, double pitch) {
    const int n = pitch < 1.5 ? 4 : pitch < 3 ? 3 : 2;  // samples across a pixel
    std::vector<double> light(static_cast<std::size_t>(w) * h);
    double sum = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double total = 0;
            for (int sy = 0; sy < n; ++sy)
                for (int sx = 0; sx < n; ++sx)
                    total += dimple_light(x + (sx + .5) / n, y + (sy + .5) / n, pitch);
            const double mean = total / (n * n);
            light[static_cast<std::size_t>(y) * w + x] = mean;
            sum += mean;
        }
    const double mean = sum / static_cast<double>(light.size());
    double spread = 0;
    for (double v : light)
        spread += (v - mean) * (v - mean);
    const double deviation = std::sqrt(spread / static_cast<double>(light.size()));
    std::vector<float> out(light.size(), 0.0F);
    if (deviation > 1e-9)
        for (std::size_t i = 0; i < light.size(); ++i)
            out[i] = static_cast<float>((light[i] - mean) / deviation);
    return out;
}

// Signed distance to the card's rounded outline (negative inside), in device pixels; the
// artwork's card is a 360 x 504 image whose rounded rectangle starts 2.4 px in with an 18 px
// corner radius.
struct Outline {
    double half_w, half_h, radius, cx, cy;
    double operator()(double x, double y) const {
        const double qx = std::abs(x - cx) - (half_w - radius), qy = std::abs(y - cy) - (half_h - radius);
        const double ox = std::max(qx, 0.0), oy = std::max(qy, 0.0);
        return std::hypot(ox, oy) + std::min(std::max(qx, qy), 0.0) - radius;
    }
};
Outline card_outline(int w, int h) {
    const double s = w / 360.0;
    const double inset = 2.4 * s, radius = 18 * s;
    const Outline outline{w * .5 - inset, h * .5 - inset, radius, w * .5, h * .5};
    return outline;
}

std::uint32_t big_endian(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}
std::uint8_t paeth(int left, int up, int corner) {
    const int estimate = left + up - corner;
    const int a = std::abs(estimate - left), b = std::abs(estimate - up), c = std::abs(estimate - corner);
    return static_cast<std::uint8_t>(a <= b && a <= c ? left : (b <= c ? up : corner));
}
// Reverses PNG's per-row filters in place; `rows` holds a filter byte before each row.
bool unfilter(std::vector<std::uint8_t>& rows, std::size_t stride, std::size_t bpp, std::uint32_t height) {
    const std::vector<std::uint8_t> zero(stride, 0);
    for (std::uint32_t y = 0; y < height; ++y) {
        std::uint8_t* row = rows.data() + static_cast<std::size_t>(y) * (stride + 1) + 1;
        const std::uint8_t* up = y ? row - (stride + 1) : zero.data();
        const std::uint8_t filter = row[-1];
        if (filter == 1) {
            for (std::size_t i = bpp; i < stride; ++i) row[i] = static_cast<std::uint8_t>(row[i] + row[i - bpp]);
        } else if (filter == 2) {
            for (std::size_t i = 0; i < stride; ++i) row[i] = static_cast<std::uint8_t>(row[i] + up[i]);
        } else if (filter == 3) {
            for (std::size_t i = 0; i < bpp; ++i) row[i] = static_cast<std::uint8_t>(row[i] + (up[i] >> 1));
            for (std::size_t i = bpp; i < stride; ++i) row[i] = static_cast<std::uint8_t>(row[i] + ((row[i - bpp] + up[i]) >> 1));
        } else if (filter == 4) {
            for (std::size_t i = 0; i < bpp; ++i) row[i] = static_cast<std::uint8_t>(row[i] + up[i]);
            for (std::size_t i = bpp; i < stride; ++i) row[i] = static_cast<std::uint8_t>(row[i] + paeth(row[i - bpp], up[i], up[i - bpp]));
        } else if (filter != 0) {
            return false;
        }
    }
    return true;
}

// Area weights for one axis: for each target sample, the source samples it covers and how much.
struct Span {
    int first = 0;
    std::vector<double> weights;
};
std::vector<Span> area_spans(int source, int target) {
    std::vector<Span> spans(static_cast<std::size_t>(target));
    const double step = static_cast<double>(source) / target;
    for (int t = 0; t < target; ++t) {
        Span& span = spans[static_cast<std::size_t>(t)];
        if (step <= 1) {
            // enlarging: the two nearest source samples, linearly
            const double at = (t + .5) * step - .5;
            const int left = std::clamp(static_cast<int>(std::floor(at)), 0, source - 1);
            const int right = std::min(left + 1, source - 1);
            const double f = std::clamp(at - left, 0.0, 1.0);
            span.first = left;
            span.weights = right == left ? std::vector<double>{1.0} : std::vector<double>{1 - f, f};
            continue;
        }
        const double low = t * step, high = (t + 1) * step;
        const int first = static_cast<int>(std::floor(low));
        const int last = std::min(source - 1, static_cast<int>(std::ceil(high)) - 1);
        span.first = first;
        for (int s = first; s <= last; ++s) {
            const double covered = std::min(high, s + 1.0) - std::max(low, static_cast<double>(s));
            span.weights.push_back(covered / step);
        }
    }
    return spans;
}

// Facets of the glint: the card is divided into cells `grain` device pixels across, and each
// cell may hold one facet, a soft round dot at a scattered place, caught by the lamp when it
// faces it; facets in the glare's band catch most often. Returns the light added here, 0..1+.
std::uint32_t mix(std::uint32_t h) {
    h ^= h >> 16;
    h *= 0x7feb352dU;
    h ^= h >> 15;
    h *= 0x846ca68bU;
    h ^= h >> 16;
    return h;
}
double unit(std::uint32_t h) {
    const double u = (h & 0xffffffU) / 16777216.0;
    return u;
}
double facet_flash(double px, double py, int w, int h, double grain, double sparkle, std::uint32_t seed) {
    const int cx = static_cast<int>(std::floor(px / grain)), cy = static_cast<int>(std::floor(py / grain));
    double light = 0;
    for (int ny = cy - 1; ny <= cy + 1; ++ny)
        for (int nx = cx - 1; nx <= cx + 1; ++nx) {
            const std::uint32_t key = mix(static_cast<std::uint32_t>(nx) * 0x9e3779b1U ^
                                        mix(static_cast<std::uint32_t>(ny) * 0x85ebca77U ^ seed));
            const double fx = (nx + .2 + .6 * unit(mix(key ^ 1U))) * grain, fy = (ny + .2 + .6 * unit(mix(key ^ 2U))) * grain;
            // a facet faces the lamp most readily where the glare falls
            const double along = fx / w * .45 + fy / h * .55;
            const double band = std::exp(-std::pow((along - kGlareAt) / (kGlareWidth * 1.6), 2));
            if (unit(key) > .25 * sparkle * (.25 + .75 * band))
                continue;
            const double radius = grain * (.3 + .25 * unit(mix(key ^ 3U)));
            const double d = std::hypot(px - fx, py - fy) / radius;
            if (d >= 1)
                continue;
            const double brightness = .55 + .45 * unit(mix(key ^ 4U));
            light += brightness * std::pow(1 - d * d, 1.5);
        }
    return light;
}

}  // namespace

bool decode_card_png(std::span<const std::uint8_t> file, CardArt& out) {
    static const std::array<std::uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (file.size() < signature.size() || !std::equal(signature.begin(), signature.end(), file.begin()))
        return false;
    std::uint32_t width = 0, height = 0;
    std::size_t bpp = 0;
    std::vector<std::uint8_t> compressed;
    bool header = false, ended = false;
    std::size_t at = signature.size();
    while (!ended) {
        if (file.size() - at < 12)
            return false;
        const std::uint32_t length = big_endian(&file[at]);
        if (length > file.size() - at - 12)
            return false;
        const std::uint8_t* type = &file[at + 4];
        const std::uint8_t* data = &file[at + 8];
        if (!header) {
            if (std::memcmp(type, "IHDR", 4) != 0 || length != 13)
                return false;
            width = big_endian(data);
            height = big_endian(data + 4);
            if (width == 0 || height == 0 || width > 4096 || height > 4096)
                return false;
            // 8-bit RGB or RGBA, deflate, standard filters, no interlace
            if (data[8] != 8 || (data[9] != 6 && data[9] != 2) || data[10] != 0 || data[11] != 0 || data[12] != 0)
                return false;
            bpp = data[9] == 6 ? 4 : 3;
            header = true;
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            compressed.insert(compressed.end(), data, data + length);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            ended = true;
        } else if ((type[0] & 32) == 0) {
            return false;  // an unknown critical chunk changes the meaning of the pixels
        }
        at += 12 + static_cast<std::size_t>(length);
    }
    const std::size_t stride = static_cast<std::size_t>(width) * bpp;
    std::vector<std::uint8_t> rows;
    if (!ambient::inflate_zlib(compressed, (stride + 1) * height, rows) || !unfilter(rows, stride, bpp, height))
        return false;
    CardArt decoded;
    decoded.width = static_cast<int>(width);
    decoded.height = static_cast<int>(height);
    decoded.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* source = rows.data() + static_cast<std::size_t>(y) * (stride + 1) + 1;
        std::uint8_t* target = decoded.rgba.data() + static_cast<std::size_t>(y) * width * 4;
        for (std::uint32_t x = 0; x < width; ++x) {
            for (std::size_t c = 0; c < 3; ++c)
                target[x * 4 + c] = source[x * bpp + c];
            target[x * 4 + 3] = bpp == 4 ? source[x * bpp + 3] : 255;
        }
    }
    out = std::move(decoded);
    return true;
}

bool load_card_png(const std::string& path, CardArt& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const bool decoded = decode_card_png(bytes, out);
    return decoded;
}

CardArt card_art_from_bgra(int width, int height, std::span<const std::byte> bgra) {
    CardArt art;
    if (width <= 0 || height <= 0 || bgra.size() < static_cast<std::size_t>(width) * height * 4)
        return art;
    art.width = width;
    art.height = height;
    art.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; ++i) {
        const double a = std::to_integer<int>(bgra[i * 4 + 3]);
        for (int c = 0; c < 3; ++c) {
            const double premultiplied = std::to_integer<int>(bgra[i * 4 + 2 - static_cast<std::size_t>(c)]);
            art.rgba[i * 4 + static_cast<std::size_t>(c)] =
                static_cast<std::uint8_t>(a > 0 ? std::clamp(std::lround(premultiplied * 255 / a), 0L, 255L) : 0);
        }
        art.rgba[i * 4 + 3] = static_cast<std::uint8_t>(a);
    }
    return art;
}

void reduce_card_art(const CardArt& source, int width, int height, CardArt& out) {
    if (source.width <= 0 || source.height <= 0 || width <= 0 || height <= 0)
        return;
    // linear, premultiplied
    const std::size_t pixels = static_cast<std::size_t>(source.width) * source.height;
    std::vector<double> linear(pixels * 4);
    for (std::size_t i = 0; i < pixels; ++i) {
        const double a = source.rgba[i * 4 + 3] / 255.0;
        for (int c = 0; c < 3; ++c)
            linear[i * 4 + static_cast<std::size_t>(c)] = linear_of(source.rgba[i * 4 + static_cast<std::size_t>(c)]) * a;
        linear[i * 4 + 3] = a;
    }
    const std::vector<Span> across = area_spans(source.width, width);
    const std::vector<Span> down = area_spans(source.height, height);
    // across, then down
    std::vector<double> rows(static_cast<std::size_t>(source.height) * width * 4, 0.0);
    for (int y = 0; y < source.height; ++y)
        for (int x = 0; x < width; ++x) {
            const Span& span = across[static_cast<std::size_t>(x)];
            double* target = &rows[(static_cast<std::size_t>(y) * width + x) * 4];
            for (std::size_t k = 0; k < span.weights.size(); ++k) {
                const double* from = &linear[(static_cast<std::size_t>(y) * source.width + span.first + static_cast<int>(k)) * 4];
                for (int c = 0; c < 4; ++c)
                    target[c] += from[c] * span.weights[k];
            }
        }
    CardArt reduced;
    reduced.width = width;
    reduced.height = height;
    reduced.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const Span& span = down[static_cast<std::size_t>(y)];
            double sum[4] = {};
            for (std::size_t k = 0; k < span.weights.size(); ++k) {
                const double* from = &rows[(static_cast<std::size_t>(span.first + static_cast<int>(k)) * width + x) * 4];
                for (int c = 0; c < 4; ++c)
                    sum[c] += from[c] * span.weights[k];
            }
            std::uint8_t* target = &reduced.rgba[(static_cast<std::size_t>(y) * width + x) * 4];
            const double a = std::clamp(sum[3], 0.0, 1.0);
            for (int c = 0; c < 3; ++c)
                target[c] = static_cast<std::uint8_t>(a > 1e-9 ? std::lround(255 * encode(sum[c] / a)) : 0);
            target[3] = static_cast<std::uint8_t>(std::lround(255 * a));
        }
    out = std::move(reduced);
}

std::vector<std::byte> premultiplied_bgra(const CardArt& art) {
    std::vector<std::byte> out(static_cast<std::size_t>(art.width) * art.height * 4);
    for (std::size_t i = 0; i < static_cast<std::size_t>(art.width) * art.height; ++i) {
        const unsigned a = art.rgba[i * 4 + 3];
        out[i * 4] = static_cast<std::byte>((art.rgba[i * 4 + 2] * a + 127) / 255);
        out[i * 4 + 1] = static_cast<std::byte>((art.rgba[i * 4 + 1] * a + 127) / 255);
        out[i * 4 + 2] = static_cast<std::byte>((art.rgba[i * 4] * a + 127) / 255);
        out[i * 4 + 3] = static_cast<std::byte>(a);
    }
    return out;
}

CardPaper make_card_paper(int w, int h, double pixels_per_point, double emboss_depth, double emboss_pitch) {
    CardPaper out;
    if (w <= 0 || h <= 0)
        return out;
    out.width = w;
    out.height = h;
    const std::size_t pixels = static_cast<std::size_t>(w) * h;
    out.light.assign(pixels * 3, 1.0F);
    out.cover.assign(pixels, 0.0F);
    const Outline outline = card_outline(w, h);
    const double s = w / 360.0;
    // The sheet, in linear light, its brightest channel averaging 1.
    paper::Parameters sheet_parameters = paper::preset(paper::Preset::copy);
    sheet_parameters.colour = {0.991, 1.0, 0.997};
    sheet_parameters.machine_grain = 1.0;
    sheet_parameters.warmth = 0.0;
    sheet_parameters.pixels_per_point = pixels_per_point;
    paper::Image sheet;
    std::vector<double> sheet_linear(pixels * 3, 1.0);
    if (paper::generate_tile(sheet_parameters, w, h, sheet)) {
        double sums[3] = {};
        for (std::size_t i = 0; i < pixels; ++i)
            for (int c = 0; c < 3; ++c) {
                const double v = linear_of(sheet.rgba[i * 4 + static_cast<std::size_t>(c)]);
                sheet_linear[i * 3 + static_cast<std::size_t>(c)] = v;
                sums[c] += v;
            }
        const double brightest = std::max({sums[0], sums[1], sums[2]}) / static_cast<double>(pixels);
        for (double& v : sheet_linear)
            v /= brightest;
    }
    // The shading: emboss, lamp and rolled edge; then exposure.
    out.emboss = make_emboss(w, h, std::max(.5, emboss_pitch));
    const double target = to_linear(kPaperLevel / 255.0);
    const double level = to_linear((kPaperLevel + .5) / 255.0) - to_linear((kPaperLevel - .5) / 255.0);
    const double depth = emboss_depth * level / target;
    const double lx = -.5, ly = -.7;  // toward the lamp, in the card plane
    const double rim = std::max(1.6, 3.2 * s);
    std::vector<double> shade(pixels, 0.0);
    double sum = 0;
    long long counted = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * w + x;
            const double px = x + .5, py = y + .5;
            const double d = outline(px, py);
            const double cover = std::clamp(.5 - d, 0.0, 1.0);
            out.cover[i] = static_cast<float>(cover);
            if (cover <= 0)
                continue;
            const double relief = 1 + out.emboss[i] * depth;
            const double u = px / w, v = py / h, diagonal = u * .45 + v * .55;
            const double falloff = 1 + .10 * (1 - diagonal) * (1 - diagonal) - .10 * diagonal * diagonal * diagonal;
            double edge = 1;
            const double depth_in = -d;
            if (depth_in < rim) {
                const double nx = outline(px + .5, py) - outline(px - .5, py), ny = outline(px, py + .5) - outline(px, py - .5);
                const double band = 1 - depth_in / rim;
                edge = 1 + (nx * lx + ny * ly) * band * band * .6;
            }
            shade[i] = relief * falloff * edge;
            if (cover > .99) {
                sum += shade[i];
                ++counted;
            }
        }
    const double exposure = counted > 0 ? target / (sum / static_cast<double>(counted)) : 1.0;
    for (std::size_t i = 0; i < pixels; ++i)
        for (int c = 0; c < 3; ++c)
            out.light[i * 3 + static_cast<std::size_t>(c)] =
                static_cast<float>(sheet_linear[i * 3 + static_cast<std::size_t>(c)] * shade[i] * exposure);
    return out;
}

std::vector<std::byte> paper_overlay(const CardPaper& paper) {
    std::vector<std::byte> out(static_cast<std::size_t>(paper.width) * paper.height * 4);
    const double stock_mean = (kStock[0] + kStock[1] + kStock[2]) / 3;
    for (std::size_t i = 0; i < static_cast<std::size_t>(paper.width) * paper.height; ++i) {
        if (paper.cover[i] <= 0)
            continue;
        double mean = 0;
        for (int c = 0; c < 3; ++c)
            mean += encode(roll_off(paper.light[i * 3 + static_cast<std::size_t>(c)]));
        mean /= 3;
        const double a = std::clamp(1 - mean / stock_mean, 0.0, 1.0) * paper.cover[i];
        out[i * 4 + 3] = static_cast<std::byte>(std::lround(a * 255));
    }
    return out;
}

void print_card(const CardArt& art, const CardPaper& paper, const LightAt& light, const CardGlint& glint,
                std::vector<std::byte>& out) {
    const int w = paper.width, h = paper.height;
    out.assign(static_cast<std::size_t>(w) * h * 4, std::byte{0});
    if (art.width != w || art.height != h)
        return;
    const double stock_linear[3] = {to_linear(kStock[0]), to_linear(kStock[1]), to_linear(kStock[2])};
    const double sparkle = std::clamp(glint.sparkle, 0.0, 1.0);
    const double grain = std::clamp(glint.grain, 1.0, 16.0);
    const double glare = .6 * std::clamp(glint.glare, 0.0, 1.0);
    // The room's light grid, stretched over the table as the window draws it.
    const TableLight* grid = light.light;
    const bool lit = grid != nullptr && (*grid).width > 1 && (*grid).height > 1 && light.table_width > 0 && light.table_height > 0;
    const double cell_x = lit ? light.table_width / (*grid).width : 1, cell_y = lit ? light.table_height / (*grid).height : 1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * w + x;
            const double alpha = paper.cover[i] * art.rgba[i * 4 + 3] / 255.0;
            if (alpha <= 0)
                continue;
            // the ink: what the print takes from the stock it was printed on, into the paper
            double ink[3] = {};
            double v[3] = {};
            for (int c = 0; c < 3; ++c) {
                const std::size_t k = static_cast<std::size_t>(c);
                ink[c] = std::clamp(linear_of(art.rgba[i * 4 + k]) / stock_linear[c], 0.0, 1.0);
            }
            const double paperness = (ink[0] + ink[1] + ink[2]) / 3;
            const double u = (x + .5) / w, vv = (y + .5) / h, along = u * .45 + vv * .55;
            const double e = paper.emboss.empty() ? 0 : paper.emboss[i];
            for (int c = 0; c < 3; ++c) {
                const std::size_t k = static_cast<std::size_t>(c);
                v[c] = encode(roll_off(paper.light[i * 3 + k] * ink[c]));
            }
            if (lit) {
                // bilinear, cell centres at the middle of each cell, edges clamped
                const double gx = (light.x + x + .5) / cell_x - .5, gy = (light.y + y + .5) / cell_y - .5;
                const int x0 = std::clamp(static_cast<int>(std::floor(gx)), 0, (*grid).width - 1);
                const int y0 = std::clamp(static_cast<int>(std::floor(gy)), 0, (*grid).height - 1);
                const int x1 = std::min(x0 + 1, (*grid).width - 1), y1 = std::min(y0 + 1, (*grid).height - 1);
                const double fx = std::clamp(gx - x0, 0.0, 1.0), fy = std::clamp(gy - y0, 0.0, 1.0);
                const std::byte* f = (*grid).face.data();
                double sample[4] = {};
                for (int c = 0; c < 4; ++c) {
                    const double a00 = std::to_integer<int>(f[(static_cast<std::size_t>(y0) * (*grid).width + x0) * 4 + static_cast<std::size_t>(c)]);
                    const double a10 = std::to_integer<int>(f[(static_cast<std::size_t>(y0) * (*grid).width + x1) * 4 + static_cast<std::size_t>(c)]);
                    const double a01 = std::to_integer<int>(f[(static_cast<std::size_t>(y1) * (*grid).width + x0) * 4 + static_cast<std::size_t>(c)]);
                    const double a11 = std::to_integer<int>(f[(static_cast<std::size_t>(y1) * (*grid).width + x1) * 4 + static_cast<std::size_t>(c)]);
                    sample[c] = ((a00 * (1 - fx) + a10 * fx) * (1 - fy) + (a01 * (1 - fx) + a11 * fx) * fy) / 255.0;
                }
                // BGRA, premultiplied: the gloss adds, the level dims (seen = face x level + gloss)
                for (int c = 0; c < 3; ++c)
                    v[c] = std::clamp(sample[2 - c] + v[c] * (1 - sample[3]), 0.0, 1.0);
            }
            if (sparkle > 0 && paperness > .05) {
                // the facets near this pixel, one possible facet to a cell of the grain
                const double flash = facet_flash(x + .5, y + .5, w, h, grain, sparkle, glint.seed);
                const double a = std::min(1.0, flash) * paperness * paperness;
                for (int c = 0; c < 3; ++c)
                    v[c] = v[c] + (1 - v[c]) * a;
            }
            if (glare > 0) {
                // the lamp's glare in the varnish, over paper and ink alike, grained by the finish
                const double band = std::exp(-std::pow((along - kGlareAt) / kGlareWidth, 2));
                const double facet = std::clamp((e + 2.5) / 5, 0.0, 1.0);
                const double a = glare * band * (.35 + .65 * facet);
                for (int c = 0; c < 3; ++c)
                    v[c] = v[c] + (1 - v[c]) * a;
            }
            std::byte* target = &out[i * 4];
            target[0] = static_cast<std::byte>(std::lround(255 * v[2] * alpha));
            target[1] = static_cast<std::byte>(std::lround(255 * v[1] * alpha));
            target[2] = static_cast<std::byte>(std::lround(255 * v[0] * alpha));
            target[3] = static_cast<std::byte>(std::lround(255 * alpha));
        }
}

}  // namespace games
