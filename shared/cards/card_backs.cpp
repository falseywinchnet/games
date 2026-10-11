#include "card_backs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace games {
namespace {

constexpr double pi = 3.14159265358979323846;

struct Rgb {
    double r = 0, g = 0, b = 0;
};
Rgb mix(Rgb a, Rgb b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
double smooth(double a, double b, double x) {
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}
// Coverage of a line of half-width `half` at distance `d`, a pixel soft.
double line(double d, double half) {
    return 1 - smooth(half - .5, half + .5, std::abs(d));
}
// Signed distance to a rounded rectangle centred at the origin.
double rounded(double x, double y, double half_w, double half_h, double radius) {
    const double qx = std::abs(x) - (half_w - radius), qy = std::abs(y) - (half_h - radius);
    return std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0) - radius;
}
double hash01(int x, int y) {
    std::uint32_t k = static_cast<std::uint32_t>(x) * 73856093U ^ static_cast<std::uint32_t>(y) * 19349663U;
    k = (k ^ (k >> 13)) * 0x5bd1e995U;
    return ((k ^ (k >> 15)) & 0xffffU) / 65535.0;
}
// Distance from a repeating coordinate to the nearest multiple of `period`.
double to_grid(double v, double period) {
    return std::abs(v - std::round(v / period) * period);
}

Rgb lattice(double x, double y) {
    const Rgb navy{22, 44, 104}, white{236, 240, 250};
    // Lines on the two diagonals, and a small diamond (a square on the diagonals) at
    // each crossing.
    const double period = 17;
    const double u = (x + y) / std::sqrt(2.0), v = (x - y) / std::sqrt(2.0);
    const double du = to_grid(u, period), dv = to_grid(v, period);
    double ink = std::max(line(du, .7), line(dv, .7)) * .8;
    ink = std::max(ink, 1 - smooth(2.2, 3.2, std::max(du, dv)));
    return mix(navy, white, ink);
}

Rgb rings(double x, double y) {
    const Rgb red{124, 16, 30}, gold{232, 190, 104};
    // Coarse enough that the rings stay rings when the card is drawn small.
    const double spacing = 40, radius = 28.3;
    double ink = 0;
    for (int k = 0; k < 2; ++k) {
        const double ox = k == 0 ? 0 : spacing * .5, oy = k == 0 ? 0 : spacing * .5;
        const double gx = std::round((x - ox) / spacing) * spacing + ox;
        const double gy = std::round((y - oy) / spacing) * spacing + oy;
        for (int i = -1; i <= 1; ++i)
            for (int j = -1; j <= 1; ++j) {
                const double d = std::hypot(x - (gx + i * spacing), y - (gy + j * spacing)) - radius;
                ink = std::max(ink, line(d, 1.3));
            }
    }
    return mix(red, gold, ink * .9);
}

// A sett: the band of colour across the cloth at a position along it.
Rgb sett(double v) {
    const double p = std::fmod(std::fmod(v, 64) + 64, 64);
    if (p < 26)
        return {22, 74, 44};    // the dark green ground
    if (p < 30)
        return {214, 182, 80};  // a thin gold line
    if (p < 42)
        return {18, 34, 62};    // navy
    if (p < 46)
        return {22, 74, 44};
    if (p < 58)
        return {48, 118, 66};   // a lighter green
    return {12, 40, 26};
}
Rgb tartan(double x, double y) {
    const Rgb warp = sett(x), weft = sett(y);
    // Twill: each thread passes over two and under two, on the diagonal.
    const int twill = static_cast<int>(std::floor((x + y) / 2.0)) & 3;
    const Rgb woven = twill < 2 ? mix(warp, weft, .3) : mix(warp, weft, .7);
    // The weave's small shadow along each thread.
    const double grain = .92 + .08 * std::sin((x - y) * pi / 2);
    return {woven.r * grain, woven.g * grain, woven.b * grain};
}

Rgb star(double x, double y, double half_w, double half_h) {
    const Rgb deep{30, 18, 58}, glow{70, 44, 120}, gold{236, 204, 122};
    const double r = std::hypot(x, y), a = std::atan2(y, x);
    Rgb c = mix(glow, deep, smooth(0, std::hypot(half_w, half_h) * .8, r));
    // The compass star: eight points, the four long ones on the axes.
    const double longer = std::pow(std::abs(std::cos(2 * a)), 18);
    const double shorter = std::pow(std::abs(std::sin(2 * a)), 18);
    const double edge = 22 + 70 * longer + 38 * shorter;
    c = mix(c, gold, 1 - smooth(edge - .8, edge + .8, r));
    // A ring round it, and small stars on a wider ring.
    c = mix(c, gold, line(r - 104, .9) * .85);
    for (int k = 0; k < 16; ++k) {
        const double t = k * 2 * pi / 16;
        const double sx = std::cos(t) * 122, sy = std::sin(t) * 122;
        const double d = std::hypot(x - sx, y - sy);
        c = mix(c, gold, (1 - smooth(1.6, 2.8, d)) * .9);
    }
    // Scattered faint stars.
    const int gx = static_cast<int>(std::floor(x / 11)), gy = static_cast<int>(std::floor(y / 11));
    const double h = hash01(gx, gy);
    if (h > .82 && r > 135) {
        const double sx = (gx + .5) * 11, sy = (gy + .5) * 11;
        c = mix(c, Rgb{220, 210, 250}, (1 - smooth(.5, 1.5, std::hypot(x - sx, y - sy))) * (h - .8) * 4);
    }
    return c;
}

}  // namespace

std::vector<std::byte> make_card_back(int design, int w, int h) {
    std::vector<std::byte> out(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    const double s = w / static_cast<double>(card_art_width);
    const double half_w = w * .5, half_h = h * .5;
    const double inset = 2.4 * s, radius = 18 * s;
    const double border = 15 * s, rule_at = 19 * s, panel_at = 22 * s;
    const Rgb paper{250, 247, 238};
    const Rgb rules[4] = {{200, 210, 236}, {232, 190, 104}, {214, 182, 80}, {236, 204, 122}};
    const int which = std::clamp(design - painted_back_count, 0, 3);
    constexpr int samples = 2;
    for (int py = 0; py < h; ++py)
        for (int px = 0; px < w; ++px) {
            double r = 0, g = 0, b = 0, cover = 0;
            for (int sy = 0; sy < samples; ++sy)
                for (int sx = 0; sx < samples; ++sx) {
                    const double x = px + (sx + .5) / samples - half_w, y = py + (sy + .5) / samples - half_h;
                    const double outer = rounded(x, y, half_w - inset, half_h - inset, radius);
                    if (outer > 0)
                        continue;
                    Rgb c = paper;
                    const double panel = rounded(x, y, half_w - panel_at, half_h - panel_at, 10 * s);
                    if (panel < 0) {
                        // The pattern is laid in card pixels at the painted backs' scale.
                        const double ux = x / s, uy = y / s;
                        switch (which) {
                        case 0: c = lattice(ux, uy); break;
                        case 1: c = rings(ux, uy); break;
                        case 2: c = tartan(ux + 1000, uy + 1000); break;
                        default: c = star(ux, uy, (half_w - panel_at) / s, (half_h - panel_at) / s); break;
                        }
                    }
                    // The fine rule between border and panel.
                    const double rule = rounded(x, y, half_w - rule_at, half_h - rule_at, 12 * s);
                    c = mix(c, rules[which], line(rule / std::max(s, .5), .7) * .9);
                    // The white border's inner edge, softly shaded.
                    const double edge = rounded(x, y, half_w - border, half_h - border, 13 * s);
                    if (edge < 0 && panel > 0)
                        c = mix(c, Rgb{232, 228, 216}, .5);
                    r += c.r;
                    g += c.g;
                    b += c.b;
                    cover += 1;
                }
            const std::size_t n = (static_cast<std::size_t>(py) * static_cast<std::size_t>(w) + static_cast<std::size_t>(px)) * 4;
            const double k = 1.0 / (samples * samples);
            // Premultiplied: the colour sums already carry the coverage.
            out[n] = static_cast<std::byte>(std::lround(std::clamp(b * k, 0.0, 255.0)));
            out[n + 1] = static_cast<std::byte>(std::lround(std::clamp(g * k, 0.0, 255.0)));
            out[n + 2] = static_cast<std::byte>(std::lround(std::clamp(r * k, 0.0, 255.0)));
            out[n + 3] = static_cast<std::byte>(std::lround(std::clamp(cover * k * 255, 0.0, 255.0)));
        }
    return out;
}

}  // namespace games
