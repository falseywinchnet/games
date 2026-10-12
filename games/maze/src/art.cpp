#include "art.hpp"

#include "platform/canvas.hpp"
#include "textures.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace mz {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int N = 512;  // floors and ceilings are painted at this size, then halved

using Pts = std::vector<std::pair<double, double>>;

std::uint32_t hsh(std::uint32_t x) {
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return x;
}
double h01(std::uint32_t x) { return (hsh(x) & 0xFFFFFF) / 16777215.0; }

struct Rand {
    std::uint32_t s;
    double next() { s = hsh(s + 0x9E3779B9u); return (s & 0xFFFFFF) / 16777215.0; }
    double in(double a, double b) { return a + (b - a) * next(); }
};

// Where a motif centred at (x, y), reaching r, must also be drawn so the tile wraps: the
// offsets of its copies (the motif itself first). Returns how many.
int copies(double x, double y, double r, double* ox, double* oy) {
    int n = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const double px = x + dx * N, py = y + dy * N;
            if (px + r < 0 || px - r > N || py + r < 0 || py - r > N) continue;
            ox[n] = dx * N;
            oy[n] = dy * N;
            ++n;
        }
    return n;
}

// Value noise that wraps every N.
double vnoise(double x, double y, int cell, std::uint32_t seed) {
    const int n = N / cell;
    const double fx = x / cell, fy = y / cell;
    const int ix = static_cast<int>(std::floor(fx)), iy = static_cast<int>(std::floor(fy));
    const double tx = fx - ix, ty = fy - iy;
    const double sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
    double v[4];
    for (int k = 0; k < 4; ++k) {
        const int i = ((ix + k % 2) % n + n) % n, j = ((iy + k / 2) % n + n) % n;
        v[k] = h01(seed ^ static_cast<std::uint32_t>(i * 73856093 ^ j * 19349663));
    }
    return (v[0] + (v[1] - v[0]) * sx) * (1 - sy) + (v[2] + (v[3] - v[2]) * sx) * sy;
}

std::uint8_t* at(Canvas& c, int x, int y) { return &c.px[(static_cast<size_t>(y) * c.w + x) * 4]; }
void put(Canvas& c, int x, int y, double r, double g, double b) {
    std::uint8_t* q = at(c, x, y);
    q[2] = static_cast<std::uint8_t>(std::clamp(r, 0.0, 255.0));
    q[1] = static_cast<std::uint8_t>(std::clamp(g, 0.0, 255.0));
    q[0] = static_cast<std::uint8_t>(std::clamp(b, 0.0, 255.0));
    q[3] = 255;
}

// The pile: each tuft a touch lighter or darker, a little longer across than down.
void pile(Canvas& c, double amount) {
    for (int y = 0; y < c.h; ++y)
        for (int x = 0; x < c.w; ++x) {
            const double k = 1 + amount * ((h01(static_cast<std::uint32_t>(x / 2 * 7919 + y * 104729)) - .5) +
                                           .6 * (vnoise(x, y, 8, 77) - .5));
            std::uint8_t* q = at(c, x, y);
            for (int i = 0; i < 3; ++i) q[i] = static_cast<std::uint8_t>(std::clamp(q[i] * k, 0.0, 255.0));
        }
}

// A polyline stroked at every wrapped offset its bounds need.
void ribbon(Canvas& c, const Pts& pts, Col col, double width) {
    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    for (const std::pair<double, double>& p : pts) {
        x0 = std::min(x0, p.first); x1 = std::max(x1, p.first);
        y0 = std::min(y0, p.second); y1 = std::max(y1, p.second);
    }
    for (int oy = static_cast<int>(std::floor((y0 - width) / N)); oy <= static_cast<int>(std::floor((y1 + width) / N)); ++oy)
        for (int ox = static_cast<int>(std::floor((x0 - width) / N)); ox <= static_cast<int>(std::floor((x1 + width) / N)); ++ox) {
            c.begin();
            for (size_t i = 0; i < pts.size(); ++i) {
                const double px = pts[i].first - ox * N, py = pts[i].second - oy * N;
                if (i) c.line(px, py); else c.move(px, py);
            }
            c.stroke(col, width);
        }
}

Col rainbow(double h, double light) {
    h -= std::floor(h);
    const double r = .5 + .5 * std::cos(2 * kPi * h), g = .5 + .5 * std::cos(2 * kPi * (h - 1.0 / 3)),
                 b = .5 + .5 * std::cos(2 * kPi * (h - 2.0 / 3));
    return rgb(static_cast<int>(r * light * 255), static_cast<int>(g * light * 255), static_cast<int>(b * light * 255));
}

// The painted tile, halved by a box filter into a texture.
Tex32 shrink(const Canvas& c) {
    Tex32 t;
    t.make(c.w / 2, c.h / 2);
    for (int y = 0; y < t.h; ++y)
        for (int x = 0; x < t.w; ++x) {
            unsigned s[3] = {0, 0, 0};
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    const std::uint8_t* p = &c.px[((static_cast<size_t>(y) * 2 + dy) * c.w + x * 2 + dx) * 4];
                    for (int i = 0; i < 3; ++i) s[i] += p[i];
                }
            t.px[static_cast<size_t>(y) * t.w + x] = 0xFF000000u | (s[2] / 4) << 16 | (s[1] / 4) << 8 | s[0] / 4;
        }
    return t;
}
// A 64-texel tile laid 4 x 4 into a floor- or ceiling-sized one.
Tex32 four_by_four(const Tex32& small) {
    Tex32 t;
    t.make(small.w * 4, small.h * 4);
    for (int y = 0; y < t.h; ++y)
        for (int x = 0; x < t.w; ++x) t.px[static_cast<size_t>(y) * t.w + x] = small.px[static_cast<size_t>(y % small.h) * small.w + x % small.w];
    return t;
}

// ---------------------------------------------------------------- floors

void blob(Canvas& c, double x, double y, double r, double wobble, std::uint32_t seed) {
    c.begin();
    for (int i = 0; i <= 40; ++i) {
        const double a = i * 2 * kPi / 40;
        const double rr = r * (1 + wobble * (std::sin(a * 3 + h01(seed) * 6) * .5 + std::sin(a * 5 + h01(seed + 1) * 6) * .3));
        if (i) c.line(x + rr * std::cos(a), y + rr * std::sin(a)); else c.move(x + rr * std::cos(a), y + rr * std::sin(a));
    }
    c.close();
}
void curl(Canvas& c, double x, double y, double r, double turn, Col col, double width) {
    c.begin();
    for (int i = 0; i <= 60; ++i) {
        const double t = i / 60.0, a = turn + t * 3.4 * kPi, rr = r * (1 - t * .85);
        if (i) c.line(x + rr * std::cos(a), y + rr * std::sin(a)); else c.move(x + rr * std::cos(a), y + rr * std::sin(a));
    }
    c.stroke(col, width);
    c.begin();
    c.move(x + r * std::cos(turn), y + r * std::sin(turn));
    c.cubic(x + r * 1.8 * std::cos(turn - .8), y + r * 1.8 * std::sin(turn - .8), x + r * 2.6 * std::cos(turn - .2),
            y + r * 2.6 * std::sin(turn - .2), x + r * 3.2 * std::cos(turn + .5), y + r * 3.2 * std::sin(turn + .5));
    c.stroke(col, width * .8);
}

// Redraws every pixel near `key` (filled shapes) with a weave: stripes or striated rows.
enum class Weave { red_stripes, gold_stripes, suit_rows, ground_rows };
Col weave(Weave w, int x, int y) {
    switch (w) {
        case Weave::red_stripes: return ((x + y) / 7) % 2 ? hex(0xD02840) : hex(0x6A0C1C);
        case Weave::gold_stripes: return ((x - y + 2048) / 7) % 2 ? hex(0xF4C030) : hex(0xB86A10);
        case Weave::suit_rows: {
            const double row = h01(static_cast<std::uint32_t>(y) * 2654435761u) - .5;
            return mix(hex(0x7A2232), hex(0x9A3A40), static_cast<float>(std::clamp(.5 + row * 1.6 + .2 * std::sin(x * .05), 0.0, 1.0)));
        }
        default: {
            const double row = h01(static_cast<std::uint32_t>(y) * 40503u + 7) - .5;
            return mix(hex(0x200814), hex(0x3A1426), static_cast<float>(.5 + row));
        }
    }
}
void restyle(Canvas& c, Col key, double tolerance, Weave w) {
    const int kr = static_cast<int>(key.r * 255), kg = static_cast<int>(key.g * 255), kb = static_cast<int>(key.b * 255);
    for (int y = 0; y < c.h; ++y)
        for (int x = 0; x < c.w; ++x) {
            std::uint8_t* q = at(c, x, y);
            const double d = std::abs(q[2] - kr) + std::abs(q[1] - kg) + std::abs(q[0] - kb);
            if (d > tolerance) continue;
            const Col out = weave(w, x, y);
            const double t = std::clamp(1 - d / tolerance, 0.0, 1.0);
            q[2] = static_cast<std::uint8_t>(q[2] * (1 - t) + out.r * 255 * t);
            q[1] = static_cast<std::uint8_t>(q[1] * (1 - t) + out.g * 255 * t);
            q[0] = static_cast<std::uint8_t>(q[0] * (1 - t) + out.b * 255 * t);
        }
}

Canvas swirl() {
    Canvas c; c.resize(N, N); c.clear(hex(0x1C3CA4));
    Rand r{11};
    double ox[9], oy[9];
    for (int i = 0; i < 900; ++i) {
        const double x = r.in(0, N), y = r.in(0, N), a = r.in(0, kPi);
        c.stroke_line(x, y, x + 9 * std::cos(a), y + 9 * std::sin(a), hex(0x0C1A5A, .7f), 1.1);
    }
    const Col red_key = hex(0xB41E2E), gold_key = hex(0xE6AE22);
    for (int i = 0; i < 16; ++i) {
        const double x = r.in(0, N), y = r.in(0, N), rad = r.in(36, 70);
        const int n = copies(x, y, rad * 1.6, ox, oy);
        for (int k = 0; k < n; ++k) { blob(c, x + ox[k], y + oy[k], rad, .35, 100 + i); c.fill(i % 2 ? red_key : gold_key); }
    }
    restyle(c, red_key, 40, Weave::red_stripes);
    restyle(c, gold_key, 40, Weave::gold_stripes);
    const Col rings[] = {hex(0xF0C030), hex(0x101010), hex(0x2A8AD0), hex(0xD02030), hex(0x30B0A0)};
    for (int i = 0; i < 30; ++i) {
        const double x = r.in(0, N), y = r.in(0, N), rad = r.in(16, 34);
        const int s = static_cast<int>(r.in(0, 5)), n = copies(x, y, rad, ox, oy);
        for (int k = 0; k < n; ++k)
            for (int ring = 0; ring < 4; ++ring) c.fill_circle(x + ox[k], y + oy[k], rad * (1 - ring * .23), rings[(s + ring) % 5]);
    }
    for (int i = 0; i < 14; ++i) {
        const double x = r.in(0, N), y = r.in(0, N), rad = r.in(26, 44), turn = r.in(0, 2 * kPi);
        const int n = copies(x, y, rad * 3.4, ox, oy);
        for (int k = 0; k < n; ++k) curl(c, x + ox[k], y + oy[k], rad, turn, i % 4 == 3 ? hex(0x38B8A8) : hex(0x0A0A0A), 11);
    }
    pile(c, .14);
    return c;
}

void suit_shape(Canvas& c, int kind, double x, double y, double s, double a, Col col) {
    c.save(); c.translate(x, y); c.rotate(a);
    c.begin();
    if (kind == 0) {
        c.move(0, s * .9); c.cubic(-s * 1.25, s * .05, -s * .62, -s * .95, 0, -s * .3); c.cubic(s * .62, -s * .95, s * 1.25, s * .05, 0, s * .9);
    } else if (kind == 1) {
        c.move(0, -s * .95); c.cubic(s * 1.25, -s * .05, s * .62, s * .85, 0, s * .25); c.cubic(-s * .62, s * .85, -s * 1.25, -s * .05, 0, -s * .95);
        c.move(0, s * .05); c.line(s * .32, s * .98); c.line(-s * .32, s * .98);
    } else if (kind == 2) {
        c.move(0, -s); c.line(s * .72, 0); c.line(0, s); c.line(-s * .72, 0);
    } else {
        c.ellipse(0, -s * .45, s * .4, s * .4); c.ellipse(-s * .47, s * .12, s * .4, s * .4); c.ellipse(s * .47, s * .12, s * .4, s * .4);
        c.move(0, 0); c.line(s * .3, s * .98); c.line(-s * .3, s * .98);
    }
    c.close(); c.stroke(hex(0x2A0C1C), 5); c.fill(col); c.restore();
}
Canvas suits() {
    Canvas c; c.resize(N, N); c.clear(hex(0x2A0C1C));
    Rand r{23};
    double ox[9], oy[9];
    const Col key = hex(0x721C2C);
    for (int i = 0; i < 64; ++i) {
        const double x = r.in(0, N), y = r.in(0, N), s = r.in(34, 60), a = r.in(-.5, .5);
        const int kind = static_cast<int>(r.in(0, 4)), n = copies(x, y, s * 1.4, ox, oy);
        for (int k = 0; k < n; ++k) suit_shape(c, kind, x + ox[k], y + oy[k], s, a, key);
    }
    restyle(c, key, 60, Weave::suit_rows);
    restyle(c, hex(0x2A0C1C), 30, Weave::ground_rows);
    pile(c, .1);
    return c;
}

void flower(Canvas& c, double x, double y, double r, Col petal, Col edge, Col centre, double turn) {
    for (int k = 0; k < 8; ++k) {
        const double a = turn + k * kPi / 4;
        c.fill_ellipse(x + r * .55 * std::cos(a), y + r * .55 * std::sin(a), r * .38, r * .38, petal);
    }
    c.begin(); c.circle(x, y, r * .8); c.stroke(edge, 1.2);
    c.fill_circle(x, y, r * .32, centre);
}
Canvas blossom() {
    Canvas c; c.resize(N, N); c.clear(hex(0x1A22C0));
    Rand r{37};
    double ox[9], oy[9];
    const int step = 32;
    for (int gy = 0; gy < N / step; ++gy)
        for (int gx = 0; gx < N / step; ++gx) {
            const double x = (gx + .5) * step + r.in(-6, 6), y = (gy + .5) * step + r.in(-6, 6), rad = r.in(8, 11.5);
            const int n = copies(x, y, 12, ox, oy);
            for (int k = 0; k < n; ++k) c.fill_circle(x + ox[k], y + oy[k], rad, hex(0x06061A));
        }
    const Col petals[] = {hex(0xE83020), hex(0xF06A20), hex(0xF8D020), hex(0xE84060)};
    const Col centres[] = {hex(0xF8E040), hex(0xC81818), hex(0xE83020), hex(0xF8E040)};
    for (int gy = 0; gy < N / step; ++gy)
        for (int gx = 0; gx < N / step; ++gx) {
            const double x = gx * step + r.in(-9, 9), y = gy * step + r.in(-9, 9);
            const int s = static_cast<int>(r.in(0, 4)), rad = static_cast<int>(r.in(9, 15));
            const double turn = r.in(0, 1);
            const int n = copies(x, y, 16, ox, oy);
            for (int k = 0; k < n; ++k) flower(c, x + ox[k], y + oy[k], rad, petals[s], hex(0xF8C850), centres[s], turn);
        }
    pile(c, .12);
    return c;
}

void paisley(Canvas& c, double x, double y, double s, double a, const Col* bands) {
    c.save(); c.translate(x, y); c.rotate(a);
    for (int b = 0; b < 4; ++b) {
        const double k = 1 - b * .2;
        c.begin();
        c.move(0, -s * k);
        c.cubic(s * .75 * k, -s * .6 * k, s * .8 * k, s * .55 * k, 0, s * .6 * k);
        c.cubic(-s * .65 * k, s * .6 * k, -s * .7 * k, -s * .05 * k, -s * .05 * k, -s * .2 * k);
        c.cubic(s * .2 * k, -s * .35 * k, -s * .1 * k, -s * .7 * k, 0, -s * k);
        c.close(); c.fill(bands[b]);
    }
    for (int d = 0; d < 6; ++d) c.fill_circle(s * .05, s * (-.25 + d * .1), s * .045, bands[0]);
    c.restore();
}
Canvas paisleys() {
    Canvas c; c.resize(N, N); c.clear(hex(0x0A0A0E));
    Rand r{41};
    double ox[9], oy[9];
    const Col pal[] = {hex(0xE0202C), hex(0xF4D028), hex(0xF070A8), hex(0x2858D8), hex(0x24A848), hex(0xF8F0E0), hex(0xF08020), hex(0x8A38C8)};
    for (int i = 0; i < 160; ++i) {
        const double x = r.in(0, N), y = r.in(0, N);
        const Col col = pal[static_cast<int>(r.in(0, 8))];
        const double turn = r.in(0, 1);
        const int n = copies(x, y, 10, ox, oy);
        for (int k = 0; k < n; ++k) flower(c, x + ox[k], y + oy[k], 7, col, hex(0xF8F0E0), hex(0x0A0A0E), turn);
    }
    for (int i = 0; i < 46; ++i) {
        const double x = r.in(0, N), y = r.in(0, N), s = r.in(44, 78), a = r.in(0, 2 * kPi);
        Col bands[4];
        for (int b = 0; b < 4; ++b) bands[b] = pal[static_cast<int>(r.in(0, 8))];
        bands[1] = hex(0x0A0A0E);
        const int n = copies(x, y, s * 1.1, ox, oy);
        for (int k = 0; k < n; ++k) paisley(c, x + ox[k], y + oy[k], s, a, bands);
    }
    pile(c, .12);
    return c;
}

double flame_angle(double x, double y) {
    return -kPi / 2 + 1.6 * (vnoise(x, y, 256, 601) - .5) * 2 + 1.1 * (vnoise(x, y, 128, 602) - .5) * 2 + .5 * (vnoise(x, y, 64, 603) - .5) * 2;
}
Canvas flames() {
    Canvas c; c.resize(N, N); c.clear(hex(0x5A080A));
    Rand r{181};
    double ox[9], oy[9];
    const Col palette[] = {hex(0x8A0E0E), hex(0xC01A12), hex(0xE04A14), hex(0xF07A1C), hex(0xF8A830), hex(0xFCD27A)};
    for (int i = 0; i < 420; ++i) {
        Pts pts;
        double x = r.in(0, N), y = r.in(0, N);
        const int len = 40 + static_cast<int>(r.in(0, 80));
        for (int s = 0; s < len; ++s) {
            pts.emplace_back(x, y);
            const double a = flame_angle(std::fmod(x + 8 * N, N), std::fmod(y + 8 * N, N));
            x += 3 * std::cos(a); y += 3 * std::sin(a);
        }
        const double band = vnoise(pts[0].first, pts[0].second, 128, 650) * 5.99;
        ribbon(c, pts, hex(0x3A0406), 6.5);
        ribbon(c, pts, palette[static_cast<int>(band)], 4.2);
        if (i % 9 == 0) ribbon(c, pts, hex(0x38B0A8), 1.2);
    }
    for (int i = 0; i < 7; ++i) {
        const double x = r.in(0, N), y = r.in(0, N);
        const int n = copies(x, y, 16, ox, oy);
        for (int k = 0; k < n; ++k) {
            c.fill_circle(x + ox[k], y + oy[k], 12, hex(0x3A0406));
            c.begin(); c.circle(x + ox[k], y + oy[k], 8.5); c.stroke(hex(0x38B0A8), 3);
            c.fill_circle(x + ox[k], y + oy[k], 3.6, hex(0x8040A0));
        }
    }
    pile(c, .1);
    return c;
}

void boomerang(Canvas& c, double x, double y, double s, double a, Col col) {
    c.save(); c.translate(x, y); c.rotate(a);
    const double p[3][2] = {{0, -s}, {s * .95, s * .55}, {-s * .7, s * .75}};
    c.begin(); c.move(p[0][0], p[0][1]);
    for (int k = 0; k < 3; ++k) {
        const double* b0 = p[(k + 1) % 3];
        c.quad((p[k][0] + b0[0]) / 2 * .45, (p[k][1] + b0[1]) / 2 * .45, b0[0], b0[1]);
    }
    c.close(); c.stroke(col, s * .28);
    c.restore();
}
Canvas atomic() {
    Canvas c; c.resize(N, N); c.clear(hex(0x1A4848));
    Rand r{67};
    double ox[9], oy[9];
    const Col cols[] = {hex(0x9A4AC8), hex(0xD8408C), hex(0xF0E8D0), hex(0x6A3A28), hex(0xC83030), hex(0x2E8A9A)};
    for (int i = 0; i < 46; ++i) {
        const double x = r.in(0, N), y = r.in(0, N), s = r.in(32, 58), a = r.in(0, 2 * kPi);
        const int n = copies(x, y, s * 1.2, ox, oy);
        for (int k = 0; k < n; ++k) boomerang(c, x + ox[k], y + oy[k], s, a, cols[i % 6]);
    }
    for (int i = 0; i < 22; ++i) {
        const double x = r.in(0, N), y = r.in(0, N), rad = r.in(10, 20);
        const int n = copies(x, y, rad + 3, ox, oy);
        for (int k = 0; k < n; ++k)
            for (int s = 0; s < 8; ++s) {
                const double a = s * kPi / 4 + .2, ex = x + ox[k] + rad * std::cos(a), ey = y + oy[k] + rad * std::sin(a);
                c.stroke_line(x + ox[k], y + oy[k], ex, ey, hex(0xE89040), 1.6);
                c.fill_circle(ex, ey, 2, hex(0xE89040));
            }
    }
    pile(c, .16);
    return c;
}

Canvas acanthus() {
    Canvas c; c.resize(N, N); c.clear(hex(0x12883A));
    Rand r{79};
    double ox[9], oy[9];
    for (int i = 0; i < 110; ++i) {
        const double x = r.in(0, N), y = r.in(0, N), a = r.in(0, 2 * kPi);
        const int n = copies(x, y, 12, ox, oy);
        for (int k = 0; k < n; ++k) {
            c.save(); c.translate(x + ox[k], y + oy[k]); c.rotate(a);
            c.fill_ellipse(-5, 0, 6, 2.6, hex(0x8AD8C0)); c.fill_ellipse(5, -2, 6, 2.6, hex(0x8AD8C0));
            c.stroke_line(-11, 1, 11, -3, hex(0x4A9A80), 1);
            c.restore();
        }
    }
    const Col cols[] = {hex(0xD82818), hex(0xF0D020)};
    for (int i = 0; i < 52; ++i) {
        const double x = r.in(0, N), y = r.in(0, N), s = r.in(34, 56), a = r.in(0, 2 * kPi);
        const int n = copies(x, y, s * 1.6, ox, oy);
        for (int k = 0; k < n; ++k) {
            c.save(); c.translate(x + ox[k], y + oy[k]); c.rotate(a);
            for (int pass = 0; pass < 2; ++pass) {
                const Col cc = pass ? cols[i % 2] : hex(0x5A1008);
                const double w = pass ? 12 : 16;
                c.begin(); c.move(-s, s * .4); c.cubic(-s * .5, -s * .6, s * .5, -s * .7, s * .8, 0);
                c.cubic(s * .95, s * .3, s * .6, s * .5, s * .45, s * .25); c.stroke(cc, w);
                c.begin(); c.move(-s, s * .4); c.quad(-s * 1.2, s * .8, -s * .8, s * .9); c.stroke(cc, w * .8);
            }
            c.restore();
        }
    }
    pile(c, .14);
    return c;
}

Canvas cosmos() {
    Canvas c; c.resize(N, N); c.clear(hex(0x070718));
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const double a = std::max(0.0, vnoise(x, y, 64, 5) * .7 + vnoise(x, y, 16, 6) * .3 - .5);
            const double b = std::max(0.0, vnoise(x, y, 128, 9) * .7 + vnoise(x, y, 32, 10) * .3 - .52);
            put(c, x, y, 7 + a * 140 + b * 30, 7 + a * 20 + b * 110, 24 + a * 170 + b * 120);
        }
    Rand r{97};
    double ox[9], oy[9];
    for (int i = 0; i < 260; ++i) c.fill_rect(r.in(0, N), r.in(0, N), 1.4, 1.4, i % 5 ? hex(0xE8E8FF, .8f) : hex(0xF8E090, .9f));
    for (int i = 0; i < 18; ++i) {
        const double x = r.in(0, N), y = r.in(0, N), s = r.in(4, 7);
        const int n = copies(x, y, s, ox, oy);
        for (int k = 0; k < n; ++k) {
            const double px = x + ox[k], py = y + oy[k];
            c.stroke_line(px - s, py, px + s, py, hex(0xFFFFFF), 1.2); c.stroke_line(px, py - s, px, py + s, hex(0xFFFFFF), 1.2);
            c.fill_circle(px, py, 1.6, hex(0xFFFFFF));
        }
    }
    const Col planets[] = {hex(0xC86A30), hex(0x4A7AC8), hex(0x9A5AB0)};
    for (int i = 0; i < 5; ++i) {
        const double x = r.in(0, N), y = r.in(0, N), rad = r.in(7, 11);
        const int n = copies(x, y, rad * 2, ox, oy);
        for (int k = 0; k < n; ++k) {
            c.fill_circle(x + ox[k], y + oy[k], rad, planets[i % 3]);
            c.begin(); c.ellipse(x + ox[k], y + oy[k], rad * 1.9, rad * .5); c.stroke(hex(0xD8C8E8, .8f), 1.4);
        }
    }
    pile(c, .1);
    return c;
}

Canvas circuit() {
    Canvas c; c.resize(N, N); c.clear(hex(0x0C4422));
    for (int y = 0; y < N; ++y)  // the glass weave under the solder mask
        for (int x = 0; x < N; ++x) {
            const double w = ((x / 3 + y / 3) % 2 ? .04 : -.04) + .06 * (vnoise(x, y, 32, 31) - .5);
            std::uint8_t* q = at(c, x, y);
            for (int i = 0; i < 3; ++i) q[i] = static_cast<std::uint8_t>(std::clamp(q[i] * (1 + w), 0.0, 255.0));
        }
    Rand r{149};
    double ox[9], oy[9];
    const double g = 8;
    const Col copper = hex(0x3CB060), gold = hex(0xD8B048);
    const double ax[] = {1, 1, 0, -1, -1, -1, 0, 1}, ay[] = {0, 1, 1, 1, 0, -1, -1, -1};
    for (int i = 0; i < 70; ++i) {
        Pts pts;
        double x = std::round(r.in(0, N) / g) * g, y = std::round(r.in(0, N) / g) * g;
        int dir = static_cast<int>(r.in(0, 8));
        pts.emplace_back(x, y);
        const int legs = 2 + static_cast<int>(r.in(0, 3));
        for (int l = 0; l < legs; ++l) {
            const int steps = 2 + static_cast<int>(r.in(0, 8));
            x += ax[dir] * g * steps; y += ay[dir] * g * steps;
            pts.emplace_back(x, y);
            dir = (dir + (r.next() < .5 ? 1 : 7)) % 8;
            if (dir % 2 && r.next() < .5) dir = (dir + 1) % 8;
        }
        const bool power = i % 9 == 0;
        ribbon(c, pts, power ? gold : copper, power ? 6 : 2.6);
        for (int e = 0; e < 2; ++e) {
            const std::pair<double, double>& end = e ? pts.back() : pts.front();
            const double px = std::fmod(end.first + 8 * N, N), py = std::fmod(end.second + 8 * N, N);
            const int n = copies(px, py, 6, ox, oy);
            for (int k = 0; k < n; ++k) { c.fill_circle(px + ox[k], py + oy[k], 4.6, gold); c.fill_circle(px + ox[k], py + oy[k], 1.8, hex(0x0A0A0A)); }
        }
    }
    for (int i = 0; i < 9; ++i) {  // chips with legs, outlined in silkscreen
        const double x = std::round(r.in(0, N) / g) * g, y = std::round(r.in(0, N) / g) * g;
        const int legs = 4 + 2 * static_cast<int>(r.in(0, 4));
        const double w = legs * 8 + 6, h = 30;
        const int n = copies(x + w / 2, y + h / 2, w, ox, oy);
        for (int k = 0; k < n; ++k) {
            const double px = x + ox[k], py = y + oy[k];
            c.begin(); c.rect(px - 3, py - 3, w + 6, h + 6); c.stroke(hex(0xF0F0E8), 1.2);
            for (int s = 0; s < legs; ++s) {
                c.fill_rect(px + 5 + s * 8, py - 6, 3, 7, hex(0xC8C8C8));
                c.fill_rect(px + 5 + s * 8, py + h - 1, 3, 7, hex(0xC8C8C8));
            }
            c.fill_rect(px, py, w, h, hex(0x16161A));
            c.fill_circle(px + 6, py + 6, 2.2, hex(0x30303A));
            for (int s = 0; s < 4; ++s) c.fill_rect(px + 10 + s * 7, py + 13, 5, 2, hex(0xB8B8C0));
        }
    }
    const Col bands[] = {hex(0xB02020), hex(0x202020), hex(0xE07020), hex(0xD8B048)};
    for (int i = 0; i < 16; ++i) {  // resistors
        const double x = r.in(0, N), y = r.in(0, N);
        const int n = copies(x, y, 22, ox, oy);
        for (int k = 0; k < n; ++k) {
            c.save(); c.translate(x + ox[k], y + oy[k]); if (i % 2) c.rotate(kPi / 2);
            c.stroke_line(-18, 0, 18, 0, hex(0xC8C8C8), 1.6);
            c.begin(); c.rrect(-10, -4, 20, 8, 4); c.fill(hex(0xD8C8A0));
            for (int s = 0; s < 4; ++s) c.fill_rect(-7 + s * 4, -4, 1.8, 8, bands[(i + s) % 4]);
            c.restore();
        }
    }
    for (int i = 0; i < 10; ++i) {  // capacitors
        const double x = r.in(0, N), y = r.in(0, N);
        const int n = copies(x, y, 12, ox, oy);
        for (int k = 0; k < n; ++k) {
            c.begin(); c.circle(x + ox[k], y + oy[k], 11); c.stroke(hex(0xF0F0E8), 1.2);
            c.fill_circle(x + ox[k], y + oy[k], 9, hex(0x284C9A));
            c.fill_rect(x + ox[k] - 9, y + oy[k] - 2, 4, 4, hex(0xC8D0E0));
        }
    }
    return c;
}

Canvas bezier() {
    Canvas c; c.resize(N, N); c.clear(hex(0x050510));
    Rand r{191};
    for (int b = 0; b < 9; ++b) {
        // a curve across the whole tile that wraps: sums of waves of the tile's own period
        const bool across = b % 3 != 2;
        const double base = r.in(0, N), a1 = r.in(40, 110), a2 = r.in(10, 40), p1 = r.in(0, 2 * kPi), p2 = r.in(0, 2 * kPi);
        const int k1 = 1 + static_cast<int>(r.in(0, 2)), k2 = 2 + static_cast<int>(r.in(0, 2));
        const double drift = r.in(-2.5, 2.5), hue = r.in(0, 1);
        for (int e = 0; e < 16; ++e) {  // the echoes
            Pts pts;
            for (int s = 0; s <= 128; ++s) {
                const double t = s * N / 128.0;
                const double v = base + e * 5 + a1 * std::sin(2 * kPi * k1 * t / N + p1 + e * .03) +
                                 a2 * std::sin(2 * kPi * k2 * t / N + p2 + e * drift * .02);
                if (across) pts.emplace_back(t, v); else pts.emplace_back(v, t);
            }
            ribbon(c, pts, rainbow(hue + e * .03, 1), 2.4);
        }
    }
    pile(c, .06);
    return c;
}

Canvas fractal() {
    // Chirikov's standard map lives on a torus, so its orbits wrap with the tile: chaotic
    // seas threaded with chains of islands, a fractal all the way down.
    Canvas c; c.resize(N, N); c.clear(hex(0x06040E));
    const double K = .97;
    Rand r{211};
    for (int o = 0; o < 520; ++o) {
        double th = r.in(0, 2 * kPi), pp = r.in(0, 2 * kPi);
        const Col col = rainbow(pp / (2 * kPi) * 1.3 + th / (2 * kPi) * .2, .95);
        for (int s = 0; s < 1400; ++s) {
            pp = std::fmod(pp + K * std::sin(th) + 4 * kPi, 2 * kPi);
            th = std::fmod(th + pp + 4 * kPi, 2 * kPi);
            const int x = static_cast<int>(th / (2 * kPi) * N) % N, y = static_cast<int>(pp / (2 * kPi) * N) % N;
            for (int d = 0; d < 4; ++d) {
                std::uint8_t* q = at(c, (x + d % 2) % N, (y + d / 2) % N);
                q[2] = static_cast<std::uint8_t>(std::min(255.0, q[2] * .55 + col.r * 255 * .6));
                q[1] = static_cast<std::uint8_t>(std::min(255.0, q[1] * .55 + col.g * 255 * .6));
                q[0] = static_cast<std::uint8_t>(std::min(255.0, q[0] * .55 + col.b * 255 * .6));
            }
        }
    }
    pile(c, .06);
    return c;
}

// ---------------------------------------------------------------- ceilings

Canvas clouds() {
    Canvas c; c.resize(N, N);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const double n = vnoise(x, y, 128, 801) * .55 + vnoise(x, y, 64, 802) * .28 + vnoise(x, y, 32, 803) * .12 + vnoise(x, y, 16, 804) * .05;
            const double cloud = std::clamp((n - .5) * 4.2, 0.0, 1.0), shade = std::clamp((vnoise(x + 10, y + 14, 64, 802) - .45) * 2, 0.0, 1.0);
            put(c, x, y, 74 + (250 - 34 * shade - 74) * cloud, 140 + (250 - 28 * shade - 140) * cloud, 214 + (255 - 18 * shade - 214) * cloud);
        }
    return c;
}
Canvas drop_ceiling() {
    Canvas c; c.resize(N, N); c.clear(hex(0xD8D6CC));
    const double cell = N / 4.0;
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i) {
            const double x = i * cell, y = j * cell;
            for (int k = 0; k < 260; ++k)
                c.fill_rect(x + h01(static_cast<std::uint32_t>(k * 3 + i * 977 + j * 131)) * cell,
                            y + h01(static_cast<std::uint32_t>(k * 3 + 1 + i * 977 + j * 131)) * cell, 1.6, 1.6, hex(0xA8A496));
            if ((i + j) % 2 == 0) {  // a light panel in every other tile
                c.fill_rect(x + 10, y + 10, cell - 20, cell - 20, hex(0xF8FAF4));
                for (int s = 0; s < 5; ++s) c.fill_rect(x + 14 + s * (cell - 28) / 5, y + 12, 2, cell - 24, hex(0xE0E4DC));
            }
            c.fill_rect(x, y, cell, 4, hex(0x9A988E)); c.fill_rect(x, y, 4, cell, hex(0x9A988E));
        }
    return c;
}
Canvas mirror_tiles() {
    Canvas c; c.resize(N, N);
    Rand r{901};
    const int n = 32;
    const double s = static_cast<double>(N) / n;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const double x = i * s, y = j * s, g = .55 + .35 * r.next();
            Col col = rgb(static_cast<int>(200 * g), static_cast<int>(204 * g), static_cast<int>(214 * g));
            const double spot = vnoise(x, y, 128, 902), hue = vnoise(x, y, 256, 903);
            if (spot > .62) col = mix(col, hue < .33 ? hex(0xF040A0) : hue < .66 ? hex(0x40C0F0) : hex(0xF0D040), static_cast<float>((spot - .62) * 2.4));
            c.fill_rect(x + 1, y + 1, s - 2, s - 2, col);
            c.fill_rect(x + 2, y + 2, s * .4, 2, hex(0xFFFFFF, .7f));
        }
    return c;
}
Canvas neon_grid() {
    Canvas c; c.resize(N, N);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const double t = .5 + .5 * std::sin(2 * kPi * (x + y) / N);
            put(c, x, y, 40 + 40 * t, 10 + 10 * t, 70 + 50 * t);
        }
    for (int k = 0; k < 8; ++k) {
        const double p = k * N / 8.0;
        for (int w = 7; w >= 1; w -= 3) {
            c.fill_rect(p - w / 2.0, 0, w, N, hex(0xF040C0, w == 1 ? 1.f : .18f));
            c.fill_rect(0, p - w / 2.0, N, w, hex(0xF040C0, w == 1 ? 1.f : .18f));
        }
    }
    return c;
}
Canvas stained_glass() {
    Canvas c; c.resize(N, N);
    Rand r{1013};
    const Col jewels[] = {hex(0xC81830), hex(0x1E50C0), hex(0x20A050), hex(0xE8B020), hex(0x8A30B0), hex(0x20A0B0), hex(0xE06820)};
    std::vector<std::pair<double, double>> seeds;
    for (int i = 0; i < 46; ++i) seeds.emplace_back(r.in(0, N), r.in(0, N));
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            double d1 = 1e9, d2 = 1e9;
            int best = 0;
            for (size_t i = 0; i < seeds.size(); ++i)
                for (int o = 0; o < 9; ++o) {
                    const double dx = x - seeds[i].first - (o % 3 - 1) * N, dy = y - seeds[i].second - (o / 3 - 1) * N;
                    const double d = std::sqrt(dx * dx + dy * dy);
                    if (d < d1) { d2 = d1; d1 = d; best = static_cast<int>(i); } else if (d < d2) d2 = d;
                }
            const double lead = std::clamp((d2 - d1 - 2.5) / 2.0, 0.0, 1.0);
            const double glow = std::clamp(1.25 - d1 / 46, .45, 1.25) * (.92 + .16 * vnoise(x, y, 8, 1020));
            const Col g = jewels[best % 7];
            put(c, x, y, 18 + (g.r * 255 * glow - 18) * lead, 16 + (g.g * 255 * glow - 16) * lead, 20 + (g.b * 255 * glow - 20) * lead);
        }
    return c;
}
// A pipe along x or y, shaded as a cylinder lit from one side, and a ball joint.
void pipe(Canvas& c, double x0, double y0, double x1, double y1, double w, Col col) {
    const bool across = std::abs(x1 - x0) > std::abs(y1 - y0);
    for (int k = 0; k < 9; ++k) {
        const double t = (k + .5) / 9 - .5, lit = .35 + .75 * std::max(0.0, std::cos((t + .18) * kPi));
        const Col cc = mix(hex(0x000000), mix(col, hex(0xFFFFFF), static_cast<float>(std::max(0.0, lit - .9) * 3)), static_cast<float>(std::min(1.0, lit)));
        const double o = t * w;
        Pts pts;
        if (across) { pts.emplace_back(x0, y0 + o); pts.emplace_back(x1, y1 + o); }
        else { pts.emplace_back(x0 + o, y0); pts.emplace_back(x1 + o, y1); }
        ribbon(c, pts, cc, w / 9 + .9);
    }
}
void joint(Canvas& c, double x, double y, double r, Col col) {
    double ox[9], oy[9];
    const int n = copies(x, y, r, ox, oy);
    for (int k = 0; k < n; ++k)
        for (int s = 10; s >= 1; --s) {
            const double t = s / 10.0;
            c.fill_circle(x + ox[k] - r * .25 * (1 - t), y + oy[k] - r * .25 * (1 - t), r * t,
                          mix(mix(col, hex(0xFFFFFF), static_cast<float>(std::max(0.0, .7 - t))), hex(0x000000), static_cast<float>(t * .45)));
        }
}
Canvas pipes() {
    Canvas c; c.resize(N, N); c.clear(hex(0x18181E));
    Rand r{1001};
    const Col cols[] = {hex(0xD02020), hex(0x2050D0), hex(0x20A040), hex(0xE0B020), hex(0xC0C0C8), hex(0x9030C0)};
    const double g = N / 8.0, dx[] = {1, 0, -1, 0}, dy[] = {0, 1, 0, -1};
    for (int p = 0; p < 6; ++p) {
        double x = (std::floor(r.in(0, 8)) + .5) * g, y = (std::floor(r.in(0, 8)) + .5) * g;
        int dir = static_cast<int>(r.in(0, 4));
        for (int seg = 0; seg < 7; ++seg) {
            const int steps = 1 + static_cast<int>(r.in(0, 4));
            const double nx = x + dx[dir] * steps * g, ny = y + dy[dir] * steps * g;
            pipe(c, x, y, nx, ny, 22, cols[p]);
            joint(c, std::fmod(x + 8 * N, N), std::fmod(y + 8 * N, N), 15, cols[p]);
            x = nx; y = ny;
            dir = (dir + (r.next() < .5 ? 1 : 3)) % 4;
        }
        joint(c, std::fmod(x + 8 * N, N), std::fmod(y + 8 * N, N), 15, cols[p]);
    }
    return c;
}

// ---------------------------------------------------------------- walls

Tex32 sandstone(int variant) {
    Canvas c; c.resize(64, 64); c.clear(hex(0xC8AE84));
    const double ch = 64.0 / 3, bw = 42.667;
    for (int row = 0; row < 3; ++row) {
        const double off = row % 2 ? 21 : 0;
        for (int k = -1; k < 3; ++k) {
            const double x = off + k * bw, y = row * ch;
            const std::uint32_t h = static_cast<std::uint32_t>(variant * 911 + row * 37 + k * 11 + 5);
            const Col b = mix(hex(0xD8B47E), hex(0xC0905A), static_cast<float>(h01(h)));
            c.fill_rect(x + 1, y + 1, bw - 2, ch - 2, b);
            for (int s = 0; s < 3; ++s) {  // faint, rippling layers
                const double sy = y + 3 + (s + h01(h + s)) * (ch - 6) / 3;
                for (double sx = x + 1; sx < x + bw - 2; sx += 4)
                    c.fill_rect(sx, sy + std::sin(sx * .35 + h) * .8, 4, 1, mix(b, s % 2 ? hex(0xE8CC98) : hex(0xA87848), .16f));
            }
            c.fill_rect(x + 1, y + 1, bw - 2, 1.5, mix(b, hex(0xFFF0D0), .3f));
            c.fill_rect(x + 1, y + ch - 2.5, bw - 2, 1.5, mix(b, hex(0x5A3818), .25f));
            for (int g = 0; g < 26; ++g)
                c.fill_rect(x + 2 + h01(h * 13 + g) * (bw - 4), y + 2 + h01(h * 17 + g) * (ch - 4), 1, 1,
                            mix(b, g % 3 ? hex(0x6A4420) : hex(0xFFF4DC), .3f));
        }
    }
    Tex32 t;
    t.make(64, 64);
    for (size_t i = 0; i < t.px.size(); ++i)
        t.px[i] = 0xFF000000u | static_cast<std::uint32_t>(c.px[i * 4 + 2]) << 16 | static_cast<std::uint32_t>(c.px[i * 4 + 1]) << 8 | c.px[i * 4];
    return t;
}

// Op art, four faces wide (512 x 128), each texel the mean of 4 x 4 exact samples.
// u runs along the four faces, v up one face; 1 is the dark ink.
double cataract(double u, double v) {  // wavy stripes whose black swells and thins
    const double t = v * 14 + .9 * std::sin(2 * kPi * (u * 8 + v * .7));
    const double width = .5 + .32 * std::sin(2 * kPi * (u * 2 + std::floor(t) * .11));
    return t - std::floor(t) < width ? 1.0 : 0.0;
}
double fall(double u, double v) {  // lines bending in waves that tighten toward the floor
    const double t = u * 48 + (1.4 - v) * 1.8 * std::sin(2 * kPi * (v * (2 + 3 * v)) + 2 * kPi * u * 4) * .6;
    return t - std::floor(t) < .5 ? 1.0 : 0.0;
}
Tex32 op_art(double (*ink)(double, double), std::uint32_t light, std::uint32_t dark) {
    Tex32 t;
    t.make(512, 128);
    for (int y = 0; y < t.h; ++y)
        for (int x = 0; x < t.w; ++x) {
            double sum = 0;
            for (int s = 0; s < 16; ++s) sum += ink((x + (s % 4 + .5) / 4) / t.w, 1 - (y + (s / 4 + .5) / 4) / t.h);
            const double k = sum / 16;
            std::uint32_t out = 0xFF000000u;
            for (int ch = 0; ch < 3; ++ch) {
                const double a = (light >> (ch * 8)) & 255, b = (dark >> (ch * 8)) & 255;
                out |= static_cast<std::uint32_t>(a + (b - a) * k + .5) << (ch * 8);
            }
            t.px[static_cast<size_t>(y) * t.w + x] = out;
        }
    return t;
}

// The portal's carpet: the same pattern, a shade warmer and with the weave turned.
Tex32 slightly_wrong(Tex32 t) {
    for (int y = 0; y < t.h; ++y)
        for (int x = 0; x < t.w; ++x) {
            std::uint32_t& p = t.px[static_cast<size_t>(y) * t.w + x];
            double r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
            if (y % 16 == 0) { r += (255 - r) * .05; g += (255 - g) * .05; b += (255 - b) * .05; }
            r += (255 - r) * .06; g += (128 - g) * .06; b += (160 - b) * .06;
            p = 0xFF000000u | static_cast<std::uint32_t>(r) << 16 | static_cast<std::uint32_t>(g) << 8 | static_cast<std::uint32_t>(b);
        }
    return t;
}

const char* const kFloors[] = {"Office", "Swirl", "Suits", "Blossom", "Paisley", "Flame",
                               "Atomic", "Acanthus", "Cosmos", "Circuit board", "Bezier", "Fractal"};
const char* const kWalls[] = {"Red brick", "Sandstone", "Cataract", "Fall"};
const char* const kCeilings[] = {"Acoustic tiles", "Wood panelling", "Night sky", "Clouds", "Drop ceiling",
                                 "Mirror tiles", "Neon grid", "Stained glass", "Pipes"};
constexpr int kFloorCount = 12, kWallCount = 4, kCeilingCount = 9;

Tex32 paint_floor(int i) {
    switch (i) {
        case 1: return shrink(swirl());
        case 2: return shrink(suits());
        case 3: return shrink(blossom());
        case 4: return shrink(paisleys());
        case 5: return shrink(flames());
        case 6: return shrink(atomic());
        case 7: return shrink(acanthus());
        case 8: return shrink(cosmos());
        case 9: return shrink(circuit());
        case 10: return shrink(bezier());
        case 11: return shrink(fractal());
        default: return four_by_four(tex_carpet(3));  // office loop pile
    }
}
Tex32 paint_ceiling(int i) {
    switch (i) {
        case 3: return shrink(clouds());
        case 4: return shrink(drop_ceiling());
        case 5: return shrink(mirror_tiles());
        case 6: return shrink(neon_grid());
        case 7: return shrink(stained_glass());
        case 8: return shrink(pipes());
        default: return four_by_four(tex_ceiling(i));  // acoustic tiles, wood, night sky
    }
}
int clamp_index(int i, int count) { return std::clamp(i, 0, count - 1); }

}  // namespace

int floor_count() { return kFloorCount; }
const char* floor_name(int i) { return kFloors[clamp_index(i, kFloorCount)]; }
bool floor_calm(int i) { i = clamp_index(i, kFloorCount); return i == 0 || i == 8; }
const Tex32& tex_floor(int i) {
    static std::map<int, Tex32> cache;
    i = clamp_index(i, kFloorCount);
    std::map<int, Tex32>::iterator it = cache.find(i);
    return it != cache.end() ? it->second : cache.emplace(i, paint_floor(i)).first->second;
}
const Tex32& tex_floor_portal(int i) {
    static std::map<int, Tex32> cache;
    i = clamp_index(i, kFloorCount);
    std::map<int, Tex32>::iterator it = cache.find(i);
    return it != cache.end() ? it->second : cache.emplace(i, slightly_wrong(tex_floor(i))).first->second;
}

int wall_count() { return kWallCount; }
const char* wall_name(int i) { return kWalls[clamp_index(i, kWallCount)]; }
bool wall_calm(int i) { return clamp_index(i, kWallCount) < 2; }
int wall_span(int i) { return clamp_index(i, kWallCount) >= 2 ? 4 : 1; }
const Tex32& tex_wall(int i, int variant) {
    i = clamp_index(i, kWallCount);
    if (i == 0) return tex_brick_alt(variant);
    static std::map<int, Tex32> cache;
    const int key = i * 8 + (i == 1 ? variant % 4 : 0);
    std::map<int, Tex32>::iterator it = cache.find(key);
    if (it != cache.end()) return it->second;
    if (i == 1) return cache.emplace(key, sandstone(1 + variant % 4)).first->second;
    if (i == 2) return cache.emplace(key, op_art(cataract, 0xF4F4F0u, 0x0E0E0Eu)).first->second;
    return cache.emplace(key, op_art(fall, 0xF68C28u, 0x541C80u)).first->second;
}

int ceiling_count() { return kCeilingCount; }
const char* ceiling_name(int i) { return kCeilings[clamp_index(i, kCeilingCount)]; }
bool ceiling_calm(int i) { i = clamp_index(i, kCeilingCount); return i <= 1 || i == 3 || i == 4; }
const Tex32& tex_ceiling_art(int i) {
    static std::map<int, Tex32> cache;
    i = clamp_index(i, kCeilingCount);
    std::map<int, Tex32>::iterator it = cache.find(i);
    return it != cache.end() ? it->second : cache.emplace(i, paint_ceiling(i)).first->second;
}

}  // namespace mz
