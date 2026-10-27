#include "paints.hpp"

#include "platform/canvas.hpp"
#include "textures.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace mz {
namespace {

constexpr double kTau = 6.28318530717958647692;

std::uint32_t hsh(std::uint32_t x) {
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return x;
}
double h01(std::uint32_t x) { return (hsh(x) & 0xFFFF) / 65535.0; }

Tex32 to_tex(const Canvas& c) {
    Tex32 t;
    t.make(c.w, c.h);
    for (size_t i = 0; i < t.px.size(); ++i) {
        const std::uint8_t* p = &c.px[i * 4];
        const unsigned a = p[3];
        const unsigned r = a ? std::min(255u, p[2] * 255u / a) : 0u, g = a ? std::min(255u, p[1] * 255u / a) : 0u, b = a ? std::min(255u, p[0] * 255u / a) : 0u;
        t.px[i] = a << 24 | r << 16 | g << 8 | b;
    }
    return t;
}
void put(Tex32& t, int x, int y, double r, double g, double b) {
    const auto q = [](double v) { return static_cast<std::uint32_t>(std::clamp(v, 0.0, 1.0) * 255 + .5); };
    t.px[static_cast<size_t>(y) * t.w + x] = 0xFF000000u | q(r) << 16 | q(g) << 8 | q(b);
}

// one eye, painted texel by texel (2 x 2 samples each): an almond `open` of the way open
// (0 shut, a lid line), the iris looking toward (lx, ly), lashes along the upper lid if asked
void eye(Canvas& c, double cx, double cy, double rx, double ry, Col iris, double lx, double ly, double open, bool lashes, Col skin) {
    const double ir = ry * .78, ix = cx + lx * rx * .35, iy = cy + ly * ry * .25;
    for (int y = static_cast<int>(cy - ry - 3); y <= static_cast<int>(cy + ry + 3); ++y)
        for (int x = static_cast<int>(cx - rx - 2); x <= static_cast<int>(cx + rx + 2); ++x) {
            float r = 0, g = 0, b = 0;
            int in = 0;
            for (int s = 0; s < 4; ++s) {
                const double px = x + .25 + .5 * (s % 2), py = y + .25 + .5 * (s / 2);
                const double u = (px - cx) / rx;
                if (std::fabs(u) >= 1) continue;
                const double edge = std::pow(1 - u * u, .75);
                const double top = cy - ry * edge * (2 * open - 1), bottom = cy + ry * edge;
                if (py < top - 1.2 || py > bottom + 1.2) continue;
                Col k = skin;
                if (py >= top && py <= bottom) {
                    const double d = std::hypot(px - ix, py - iy);
                    k = d < ir * .42 ? hex(0x080808) : d < ir ? mix(iris, hex(0x000000), static_cast<float>(d / ir * .45)) : hex(0xF8F6F0);
                    if (std::hypot(px - ix + ir * .3, py - iy + ir * .3) < ir * .18) k = hex(0xFFFFFF);  // the glint
                    if (py - top < 1.4) k = mix(k, hex(0x3A2028), .6f);                               // the lid's shadow
                } else {
                    k = hex(0x3A2028);  // the outline
                }
                r += k.r; g += k.g; b += k.b; ++in;
            }
            if (!in) continue;
            c.fill_rect(x, y, 1, 1, {r / in, g / in, b / in, in / 4.f});
        }
    if (lashes)
        for (int k = 0; k < 7; ++k) {
            const double u = -.8 + k * .267, edge = std::pow(1 - u * u, .75);
            const double x0 = cx + u * rx, y0 = cy - ry * edge * (2 * open - 1);
            c.stroke_line(x0, y0, x0 + u * rx * .25, y0 - ry * .45, hex(0x2A1A20), 1.1);
        }
}

// Where an eye looks and how open it is at frame f of n: it holds a gaze, glances to
// another, and blinks once in the loop, each eye on its own timetable.
struct Look {
    double lx, ly, open;
};
Look look_at(std::uint32_t seed, int f, int n, double rest) {
    const int changes[3] = {static_cast<int>(h01(seed + 1) * n / 3), n / 3 + static_cast<int>(h01(seed + 2) * n / 3),
                            2 * n / 3 + static_cast<int>(h01(seed + 3) * n / 3)};
    int held = 0;
    for (int k = 0; k < 3; ++k)
        if (f >= changes[k]) held = k + 1;
    const std::uint32_t g = seed * 7 + static_cast<std::uint32_t>(held % 3);
    Look l{h01(g + 11) * 2 - 1, h01(g + 12) * 1.6 - .8, rest};
    const int blink = static_cast<int>(h01(seed + 4) * (n - 3));
    const int into = f - blink;
    if (into == 0 || into == 2) l.open = std::min(rest, .45);
    else if (into == 1) l.open = 0;
    return l;
}

// ---------------------------------------------------------------- the paints, frame by frame
std::vector<Tex32> zebra() {  // the stripes sway, a wave running down them
    std::vector<Tex32> frames;
    for (int f = 0; f < 16; ++f) {
        Tex32 t;
        t.make(64, 64);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                double ink = 0;
                for (int s = 0; s < 4; ++s) {
                    const double px = x + .25 + .5 * (s % 2), py = y + .25 + .5 * (s / 2);
                    const double u = px + 5 * std::sin(kTau * (py / 64 - f / 16.0)) + 3 * std::sin(kTau * py / 32);
                    ink += u / 8 - std::floor(u / 8) < .5 ? .25 : 0;
                }
                const double k = .96 - .9 * ink;
                put(t, x, y, k, k, k * .98);
            }
        frames.push_back(std::move(t));
    }
    return frames;
}

std::vector<Tex32> plasma() {  // the colours flow round
    std::vector<Tex32> frames;
    for (int f = 0; f < 32; ++f) {
        Tex32 t;
        t.make(64, 64);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const double v = std::sin(x * kTau / 32) + std::sin(y * kTau / 64 * 2 + f * kTau / 32) + std::sin((x + y) * kTau / 64);
                const double hue = (v + 3) / 6 + f / 32.0;
                put(t, x, y, .5 + .5 * std::sin(kTau * hue), .5 + .5 * std::sin(kTau * (hue + .33)), .5 + .5 * std::sin(kTau * (hue + .66)));
            }
        frames.push_back(std::move(t));
    }
    return frames;
}

std::vector<Tex32> eyes_pink() {  // eight eyes, every one different, glancing about and blinking
    const Col irises[] = {hex(0x2A8A4A), hex(0x2A6AD0), hex(0x6A3A1A), hex(0x8A3AC0), hex(0xC08A20), hex(0x3AA8B0), hex(0xA02030)};
    const double spots[][4] = {{22, 22, 17, 10}, {80, 16, 11, 7}, {112, 46, 13, 9}, {46, 62, 22, 13}, {98, 92, 18, 11},
                               {18, 100, 12, 8}, {62, 112, 10, 6}, {116, 122, 8, 5}};
    std::vector<Tex32> frames;
    Canvas c;
    c.resize(128, 128);
    for (int f = 0; f < 48; ++f) {
        c.clear(hex(0xF0A0C0));
        for (int k = 0; k < 8; ++k) {
            const double rest = k == 5 ? .18 : k % 3 == 1 ? .62 : 1;  // one nearly shut, some heavy-lidded
            const Look l = look_at(static_cast<std::uint32_t>(k * 97 + 11), f, 48, rest);
            eye(c, spots[k][0], spots[k][1], spots[k][2], spots[k][3], irises[k % 7], l.lx, l.ly, l.open, k % 2 == 0, hex(0xF0A0C0));
        }
        frames.push_back(to_tex(c));
    }
    return frames;
}

std::vector<Tex32> dot_spam() {  // every colour, everywhere, each dot breathing in its own time
    const Col bright[] = {hex(0xE83A6A), hex(0x2E7CF0), hex(0xF8D030), hex(0x2EC860), hex(0xA850F0), hex(0xF07A20), hex(0x20C8D8), hex(0xF050C0),
                          hex(0x101010), hex(0x8AE040)};
    std::vector<Tex32> frames;
    Canvas c;
    c.resize(128, 128);
    for (int f = 0; f < 16; ++f) {
        c.clear(hex(0xFFF6E8));
        for (int i = 0; i < 300; ++i) {
            const double x = h01(i * 3 + 401) * 128, y = h01(i * 3 + 402) * 128;
            const double r = (2.5 + 7 * std::pow(h01(i * 3 + 403), 2)) * (1 + .3 * std::sin(kTau * (f / 16.0 + h01(i + 4000))));
            for (int oy = -1; oy <= 1; ++oy)
                for (int ox = -1; ox <= 1; ++ox) c.fill_circle(x + ox * 128, y + oy * 128, r, bright[hsh(i + 77) % 10]);
        }
        frames.push_back(to_tex(c));
    }
    return frames;
}

std::vector<Tex32> static_snow() {  // a set that is never tuned
    std::vector<Tex32> frames;
    for (int f = 0; f < 8; ++f) {
        Tex32 t;
        t.make(64, 64);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const double g = h01(static_cast<std::uint32_t>(x * 64 + y + 5000 + f * 4099));
                put(t, x, y, g, g, g);
            }
        frames.push_back(std::move(t));
    }
    return frames;
}

std::vector<Tex32> checkerboard() {  // a checkerboard that will not hold still: it slides corner to corner
    std::vector<Tex32> frames;
    for (int f = 0; f < 8; ++f) {
        Tex32 t;
        t.make(64, 64);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const double k = ((x + f) / 8 + (y + f) / 8) % 2 ? .94 : .06;
                put(t, x, y, k, k, k);
            }
        frames.push_back(std::move(t));
    }
    return frames;
}

std::vector<Tex32> fish_scales() {  // a shimmer runs across them
    Canvas c;
    c.resize(64, 64);
    c.clear(hex(0x2A8AA8));
    for (int y = 0; y < 72; y += 8)
        for (int x = -8; x < 72; x += 16) { c.begin(); c.circle(x + (y / 8 % 2) * 8, y, 8); c.stroke(hex(0x8AE8F0), 1.5); }
    const Tex32 base = to_tex(c);
    std::vector<Tex32> frames;
    for (int f = 0; f < 16; ++f) {
        Tex32 t = base;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const int d = ((x + y - f * 4) % 64 + 64) % 64;
                const double lift = d < 12 ? std::sin(d / 12.0 * kTau / 2) * .55 : 0;
                const std::uint32_t p = base.px[static_cast<size_t>(y) * 64 + x];
                const double r = ((p >> 16) & 255) / 255.0, g = ((p >> 8) & 255) / 255.0, b = (p & 255) / 255.0;
                put(t, x, y, r + (1 - r) * lift, g + (1 - g) * lift, b + (1 - b) * lift);
            }
        frames.push_back(std::move(t));
    }
    return frames;
}

std::vector<Tex32> tie_dye() {  // the spiral turns
    std::vector<Tex32> frames;
    for (int f = 0; f < 32; ++f) {
        Tex32 t;
        t.make(64, 64);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const double r = std::hypot(x - 32, y - 32), a = std::atan2(y - 32, x - 32);
                const double hue = (r * .05 + a * .3) * 6 - f * kTau / 32;
                put(t, x, y, .6 + .4 * std::sin(hue), .6 + .4 * std::sin(hue + 2), .6 + .4 * std::sin(hue + 4));
            }
        frames.push_back(std::move(t));
    }
    return frames;
}

// the debug rain: columns of log text, each with a bright head and a fading trail; the
// renderer lets every column fall at its own pace (kRain)
std::vector<Tex32> debug_rain() {
    static const char* const logs[] = {
        "STOP 0X0000000A IRQL NOT LESS OR EQUAL", "FATAL EXCEPTION 0E AT 0028:C0011E36", "VXD VMM(01) 00010E36", "MAZE.EXE PAGE FAULT",
        "DUMPING PHYSICAL MEMORY", "KERNEL32.DLL AT 0137:BFF9A5D0", "0XDEADBEEF 0XC0000005", "STACK DUMP BFF8B4D8 0000FFFF",
        "ASSERT WALL!=NULL", "EAX=C0011E36 EBX=00000000", "PRESS ANY KEY", "CRC ERROR SECTOR 1F3", "INT 13 RETRY 3/3", "HEAP CORRUPT 004A2F10",
    };
    Canvas c;
    c.resize(128, 128);
    c.clear(hex(0x0000AA));
    for (int col = 0; col < 16; ++col) {
        const std::string text = std::string(logs[hsh(col * 31 + 7) % 14]) + " " + logs[hsh(col * 17 + 3) % 14] + " ";
        const int head = static_cast<int>(hsh(col * 13 + 1) % 16);
        for (int row = 0; row < 16; ++row) {
            const char ch = text[static_cast<size_t>(row) % text.size()];
            if (ch == ' ') continue;
            const int behind = ((head - row) % 16 + 16) % 16;  // rows above the falling head
            const double k = behind < 11 ? 1 - behind / 12.0 : .14;
            const Col ink = k > .97 ? hex(0xFFFFFF) : mix(hex(0x2A3AD0), hex(0xD8E0FF), static_cast<float>(k));
            const char* gl = pixel_glyph(ch);
            for (int q = 0; q < 35; ++q)
                if (gl[q] == '1') c.fill_rect(col * 8 + 1 + q % 5, row * 8 + q / 5, 1, 1, ink);
        }
    }
    return {to_tex(c)};
}

// patterns: one frame each, crept up the wall by the renderer (kSlide)
std::vector<Tex32> pattern(int which) {
    Canvas c;
    c.resize(64, 64);
    switch (which) {
        case 0:  // houndstooth
            c.clear(hex(0xF0E8D8));
            for (int y = 0; y < 64; y += 8)
                for (int x = 0; x < 64; x += 8) {
                    c.begin(); c.move(x, y); c.line(x + 4, y); c.line(x + 8, y + 4); c.line(x + 4, y + 4); c.line(x + 4, y + 8); c.line(x, y + 4); c.close();
                    c.fill(hex(0x2A2A2A));
                }
            break;
        case 1:  // argyle
            c.clear(hex(0x2A3A6A));
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 3; ++x) {
                    const double cx = x * 32 + (y % 2) * 16, cy = y * 32 + 16;
                    c.begin(); c.move(cx, cy - 16); c.line(cx + 16, cy); c.line(cx, cy + 16); c.line(cx - 16, cy); c.close();
                    c.fill((x + y) % 2 ? hex(0xC03A4A) : hex(0xE8C870));
                }
            for (int k = -4; k < 8; ++k) {
                c.stroke_line(k * 16, 0, k * 16 + 64, 64, hex(0xF0F0F0), .8);
                c.stroke_line(k * 16 + 64, 0, k * 16, 64, hex(0xF0F0F0), .8);
            }
            break;
        case 2:  // gingham
            c.clear(hex(0xFFFFFF));
            for (int k = 0; k < 8; ++k) {
                c.fill_rect(k * 8, 0, 4, 64, hex(0xE0405A, .55f));
                c.fill_rect(0, k * 8, 64, 4, hex(0xE0405A, .55f));
            }
            break;
        case 3:  // tartan
            c.clear(hex(0x1A5A3A));
            for (int k = 0; k < 4; ++k) {
                c.fill_rect(k * 16 + 2, 0, 6, 64, hex(0x10203A, .7f)); c.fill_rect(0, k * 16 + 2, 64, 6, hex(0x10203A, .7f));
                c.fill_rect(k * 16 + 12, 0, 1, 64, hex(0xF0D040)); c.fill_rect(0, k * 16 + 12, 64, 1, hex(0xF0D040));
                c.fill_rect(k * 16 + 9, 0, 1, 64, hex(0xC02030, .8f)); c.fill_rect(0, k * 16 + 9, 64, 1, hex(0xC02030, .8f));
            }
            break;
        case 4: {  // chevron
            c.clear(hex(0x101010));
            const Col band[4] = {hex(0xF04080), hex(0xF8D030), hex(0x30C8E0), hex(0xF0F0F0)};
            for (int k = 0; k < 8; ++k) {
                c.begin(); c.move(0, k * 8 - 8);
                for (int x = 0; x <= 64; x += 8) c.line(x, k * 8 - 8 + (x / 8 % 2 ? 6 : 0) + 8);
                c.stroke(band[k % 4], 4);
            }
            break;
        }
        case 5:  // Greek key
            c.clear(hex(0xE8DCC0));
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 2; ++x) {
                    const double ox = x * 32 + 4, oy = y * 32 + 4;
                    const Col ink = hex(0x2A2418);
                    c.fill_rect(ox, oy, 24, 3, ink); c.fill_rect(ox + 21, oy, 3, 24, ink); c.fill_rect(ox + 6, oy + 21, 18, 3, ink);
                    c.fill_rect(ox + 6, oy + 9, 3, 15, ink); c.fill_rect(ox + 6, oy + 9, 10, 3, ink); c.fill_rect(ox + 13, oy + 9, 3, 7, ink);
                }
            break;
        default:  // herringbone
            c.clear(hex(0x6A5A4A));
            for (int y = 0; y < 64; y += 4)
                for (int x = 0; x < 64; x += 16) {
                    c.stroke_line(x, y, x + 8, y + 4, hex(0xC8B898), 2);
                    c.stroke_line(x + 16, y, x + 8, y + 4, hex(0x3A3028), 2);
                }
            break;
    }
    return {to_tex(c)};
}

std::vector<Tex32> eyes_dark() {  // a crowd of eyes in the dark, all looking somewhere else
    const Col irises[] = {hex(0x40E060), hex(0xF0C020), hex(0x40A0F0), hex(0xF04060), hex(0xC060F0)};
    std::vector<Tex32> frames;
    Canvas c;
    c.resize(128, 128);
    for (int f = 0; f < 48; ++f) {
        c.clear(hex(0x140A18));
        for (int i = 0; i < 26; ++i) {
            const double rx = 5 + 7 * h01(i * 5 + 901), x = 8 + h01(i * 5 + 902) * 112, y = 8 + h01(i * 5 + 903) * 112;
            const Look l = look_at(static_cast<std::uint32_t>(i * 131 + 7), f, 48, i % 4 == 3 ? .5 : 1);
            eye(c, x, y, rx, rx * .62, irises[i % 5], l.lx, l.ly, l.open, false, hex(0x140A18));
        }
        frames.push_back(to_tex(c));
    }
    return frames;
}

std::vector<Tex32> great_eye() {  // seven gazes across, each open, half shut and shut: it follows you
    std::vector<Tex32> frames;
    Canvas c;
    c.resize(128, 128);
    for (int g = 0; g < 7; ++g)
        for (int lid = 0; lid < 3; ++lid) {
            c.clear(hex(0xF4C8B0));
            eye(c, 64, 66, 56, 34, hex(0x3A8AC8), (g - 3) / 3.0, .1, lid == 0 ? .9 : lid == 1 ? .45 : 0, true, hex(0xF4C8B0));
            frames.push_back(to_tex(c));
        }
    return frames;
}

struct Paint {
    std::vector<Tex32> (*paint)();
    double fps;
    std::uint32_t flags;
    bool watches;
};

std::vector<Tex32> houndstooth() { return pattern(0); }
std::vector<Tex32> argyle() { return pattern(1); }
std::vector<Tex32> gingham() { return pattern(2); }
std::vector<Tex32> tartan() { return pattern(3); }
std::vector<Tex32> chevron() { return pattern(4); }
std::vector<Tex32> greek_key() { return pattern(5); }
std::vector<Tex32> herringbone() { return pattern(6); }

const Paint kPaints[] = {
    {zebra, 8, 0, false},          {plasma, 12, kGlitch, false},     {eyes_pink, 12, 0, false},  {dot_spam, 10, 0, false},
    {static_snow, 15, kGlitch, false}, {checkerboard, 12, kGlitch, false}, {fish_scales, 10, 0, false}, {tie_dye, 12, kGlitch, false},
    {debug_rain, 0, kRain, false}, {houndstooth, 0, kSlide, false},  {argyle, 0, kSlide, false},     {gingham, 0, kSlide, false},
    {tartan, 0, kSlide, false},    {chevron, 0, kSlide, false},      {greek_key, 0, kSlide, false},  {herringbone, 0, kSlide, false},
    {eyes_dark, 12, 0, false},     {great_eye, 0, 0, true},
};
constexpr int kCount = static_cast<int>(sizeof kPaints / sizeof kPaints[0]);

int index(int i) { return ((i % kCount) + kCount) % kCount; }

// each paint's frames, painted the first time they are wanted
const std::vector<Tex32>& frames(int i) {
    static std::vector<std::vector<Tex32>> made(kCount);
    std::vector<Tex32>& f = made[static_cast<size_t>(i)];
    if (f.empty()) f = kPaints[i].paint();
    return f;
}

}  // namespace

int paint_count() { return kCount; }
const Tex32& tex_paint(int i) { return frames(index(i)).front(); }
const Tex32& tex_paint_at(int i, double time, std::uint32_t phase, double gaze) {
    i = index(i);
    const std::vector<Tex32>& f = frames(i);
    if (kPaints[i].watches) {
        const int g = std::clamp(static_cast<int>(std::lround((gaze + 1) * 3)), 0, 6);
        const double b = std::fmod(time + (phase % 997) * .0053, 5.3);  // a blink every few seconds
        const int lid = b < .07 ? 1 : b < .16 ? 2 : b < .23 ? 1 : 0;
        return f[static_cast<size_t>(g * 3 + lid)];
    }
    const long long n = static_cast<long long>(f.size());
    return f[static_cast<size_t>((static_cast<long long>(time * kPaints[i].fps) + phase) % n)];
}
std::uint32_t paint_flags(int i) { return kPaints[index(i)].flags; }

}  // namespace mz
