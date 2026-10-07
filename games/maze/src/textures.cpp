#include "textures.hpp"

#include "platform/raster.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

namespace mz {

namespace {
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
        auto un = [&](unsigned v) { return a ? std::min(255u, v * 255 / a) : 0u; };
        t.px[i] = a << 24 | un(p[2]) << 16 | un(p[1]) << 8 | un(p[0]);
    }
    return t;
}

// A 5x7 pixel font for signs: A-Z, 0-9 and a little punctuation.
const char* glyph(char ch) {
    static const std::map<char, const char*> g = {
        {'A', "01110100011000111111100011000110001"}, {'B', "11110100011000111110100011000111110"}, {'C', "01110100011000010000100001000101110"},
        {'D', "11110100011000110001100011000111110"}, {'E', "11111100001000011110100001000011111"}, {'F', "11111100001000011110100001000010000"},
        {'G', "01110100011000010111100011000101111"}, {'H', "10001100011000111111100011000110001"}, {'I', "01110001000010000100001000010001110"},
        {'J', "00111000100001000010000101001001100"}, {'K', "10001100101010011000101001001010001"}, {'L', "10000100001000010000100001000011111"},
        {'M', "10001110111010110101100011000110001"}, {'N', "10001100011100110101100111000110001"}, {'O', "01110100011000110001100011000101110"},
        {'P', "11110100011000111110100001000010000"}, {'Q', "01110100011000110001101011001001101"}, {'R', "11110100011000111110101001001010001"},
        {'S', "01111100001000001110000010000111110"}, {'T', "11111001000010000100001000010000100"}, {'U', "10001100011000110001100011000101110"},
        {'V', "10001100011000110001100010101000100"}, {'W', "10001100011000110101101011010101010"}, {'X', "10001100010101000100010101000110001"},
        {'Y', "10001100010101000100001000010000100"}, {'Z', "11111000010001000100010001000011111"}, {'0', "01110100011001110101110011000101110"},
        {'1', "00100011000010000100001000010001110"}, {'2', "01110100010000100010001000100011111"}, {'3', "11111000100010000010000011000101110"},
        {'4', "00010001100101010010111110001000010"}, {'5', "11111100001111000001000011000101110"}, {'6', "00110010001000011110100011000101110"},
        {'7', "11111000010001000100010000100001000"}, {'8', "01110100011000101110100011000101110"}, {'9', "01110100011000101111000010001001100"},
        {'.', "00000000000000000000000000110001100"}, {'!', "00100001000010000100001000000000100"}, {'?', "01110100010000100010001000000000100"},
        {':', "00000011000110000000011000110000000"}, {'-', "00000000000000011111000000000000000"}, {'(', "00010001000100001000010000010000010"},
        {')', "01000001000001000010000100010001000"}, {'/', "00001000010001000100010001000010000"}, {' ', "00000000000000000000000000000000000"},
    };
    auto it = g.find(ch);
    return it == g.end() ? g.at(' ') : it->second;
}
void pixel_text(Canvas& c, const std::string& s, double x, double y, double px, Col col) {
    for (size_t i = 0; i < s.size(); ++i) {
        const char* gl = glyph(static_cast<char>(std::toupper(static_cast<unsigned char>(s[i]))));
        for (int k = 0; k < 35; ++k)
            if (gl[k] == '1') c.fill_rect(x + (i * 6 + k % 5) * px, y + (k / 5) * px, px, px, col);
    }
}
double text_width(const std::string& s, double px) { return s.size() * 6 * px - px; }

const Col kKey[kColors] = {hex(0xE8303A), hex(0x2E7CF0), hex(0x2EC860), hex(0xF8D030), hex(0xA850F0)};
}  // namespace

std::uint32_t key_rgb(int color) {
    const Col c = kKey[((color % kColors) + kColors) % kColors];
    return static_cast<std::uint32_t>(c.r * 255) << 16 | static_cast<std::uint32_t>(c.g * 255) << 8 | static_cast<std::uint32_t>(c.b * 255);
}

// ------------------------------------------------------------------ walls
namespace {
Tex32 paint_brick(int variant) {
    Canvas c;
    c.resize(64, 64);
    c.clear(hex(0xB8AEA0));  // mortar
    for (int row = 0; row < 4; ++row) {
        const double off = row % 2 ? 16 : 0;
        for (int k = -1; k < 3; ++k) {
            const double x = off + k * 32, y = row * 16;
            const std::uint32_t h = static_cast<std::uint32_t>(variant * 977 + row * 31 + k * 7 + 3);
            const double tone = h01(h);
            Col b = mix(hex(0x9A3A24), hex(0xC0583A), static_cast<float>(tone));
            if (h01(h + 9) < .12) b = mix(b, hex(0x5A2A1C), .4f);
            c.fill_rect(x + 1, y + 1, 30, 14, b);
            c.fill_rect(x + 1, y + 1, 30, 2, mix(b, hex(0xFFFFFF), .18f));   // a lit top edge
            c.fill_rect(x + 1, y + 13, 30, 2, mix(b, hex(0x000000), .25f));  // a shadowed lower edge
            for (int s = 0; s < 14; ++s)
                c.fill_rect(x + 2 + h01(h * 13 + s) * 27, y + 3 + h01(h * 17 + s) * 10, 1, 1, mix(b, hex(0x000000), .3f));
        }
    }
    return to_tex(c);
}
}  // namespace

const Tex32& tex_brick() {
    static Tex32 t = paint_brick(0);
    return t;
}
const Tex32& tex_brick_alt(int variant) {
    static std::map<int, Tex32> cache;
    auto it = cache.find(variant % 4);
    if (it != cache.end()) return it->second;
    return cache.emplace(variant % 4, paint_brick(1 + variant % 4)).first->second;
}

const Tex32& tex_gold_wall() {
    static Tex32 t = [] {
        Canvas c;
        c.resize(64, 64);
        c.clear(hex(0xC89A2E));
        for (int y = 0; y < 64; y += 16)
            for (int x = 0; x < 64; x += 16) {
                const double ox = (y / 16) % 2 ? 8 : 0;
                c.begin(); c.move(x + ox + 8, y + 2); c.line(x + ox + 14, y + 8); c.line(x + ox + 8, y + 14); c.line(x + ox + 2, y + 8); c.close();
                c.fill(hex(0xE8C058));
                c.fill_circle(x + ox + 8, y + 8, 2, hex(0xFFF0B0));
            }
        c.fill_rect(0, 0, 64, 3, hex(0x8A6418));
        c.fill_rect(0, 61, 64, 3, hex(0x8A6418));
        return to_tex(c);
    }();
    return t;
}

// ------------------------------------------------------------------ carpets and ceilings
namespace {
Tex32 paint_carpet(int theme, bool wrong) {
    Canvas c;
    c.resize(64, 64);
    switch (theme % 4) {
        case 0: {  // hotel teal with a violet and gold lattice
            c.clear(hex(0x1E7A7A));
            for (int y = 0; y < 64; y += 32)
                for (int x = 0; x < 64; x += 32) {
                    c.begin(); c.move(x + 16, y + 2); c.line(x + 30, y + 16); c.line(x + 16, y + 30); c.line(x + 2, y + 16); c.close(); c.stroke(hex(0x7A3A9A), 3);
                    c.fill_circle(x + 16, y + 16, 3, wrong ? hex(0xF0C860) : hex(0xE8B848));
                }
            break;
        }
        case 1: {  // the arcade carpet: neon squiggles on midnight
            c.clear(hex(0x161636));
            for (int i = 0; i < 9; ++i) {
                const double x = h01(i * 3 + 1) * 64, y = h01(i * 3 + 2) * 64;
                const Col col = i % 3 == 0 ? hex(0xF040C0) : i % 3 == 1 ? hex(0x40E0F0) : hex(0xF0E040);
                c.begin(); c.move(x, y); c.line(x + 6, y - 4); c.line(x + 12, y); c.line(x + 18, y - 4); c.stroke(col, 2);
                c.begin(); c.move(x + 4, y + 14); c.line(x + 10, y + 22); c.line(x - 2, y + 22); c.close(); c.stroke(col, 1.5);
            }
            break;
        }
        case 2: {  // burgundy with gold medallions
            c.clear(hex(0x6A1A24));
            for (int y = 0; y < 64; y += 32)
                for (int x = 0; x < 64; x += 32) {
                    c.begin(); c.circle(x + 16, y + 16, 10); c.stroke(hex(0xC89A3A), 2);
                    for (int k = 0; k < 4; ++k) c.fill_circle(x + 16 + 6 * std::cos(k * 1.5708), y + 16 + 6 * std::sin(k * 1.5708), 2, hex(0xD8AA4A));
                }
            break;
        }
        default: {  // office grey-blue loop pile
            c.clear(hex(0x50607A));
            for (int i = 0; i < 400; ++i) c.fill_rect(h01(i + 50) * 64, h01(i + 90) * 64, 1, 1, h01(i + 130) > .5 ? hex(0x6A7A94) : hex(0x3A4860));
            break;
        }
    }
    if (wrong) {
        // the portal's carpet: the same pattern, a shade warmer and with the weave turned
        for (int y = 0; y < 64; y += 4) c.fill_rect(0, y, 64, 1, hex(0xFFFFFF, .05f));
        c.fill_rect(0, 0, 64, 64, hex(0xFF80A0, .06f));
    }
    return to_tex(c);
}
}  // namespace

int carpet_themes() { return 4; }
const char* carpet_name(int theme) {
    static const char* n[] = {"hotel", "arcade", "burgundy", "office"};
    return n[theme % 4];
}
const Tex32& tex_carpet(int theme) {
    static std::map<int, Tex32> cache;
    auto it = cache.find(theme % 4);
    if (it != cache.end()) return it->second;
    return cache.emplace(theme % 4, paint_carpet(theme, false)).first->second;
}
const Tex32& tex_portal_carpet(int theme) {
    static std::map<int, Tex32> cache;
    auto it = cache.find(theme % 4);
    if (it != cache.end()) return it->second;
    return cache.emplace(theme % 4, paint_carpet(theme, true)).first->second;
}

const Tex32& tex_ceiling(int theme) {
    static std::map<int, Tex32> cache;
    auto it = cache.find(theme % 3);
    if (it != cache.end()) return it->second;
    Canvas c;
    c.resize(64, 64);
    switch (theme % 3) {
        case 0:  // acoustic tiles in a grid
            c.clear(hex(0xE4E0D4));
            c.fill_rect(0, 0, 64, 2, hex(0xA8A496));
            c.fill_rect(0, 0, 2, 64, hex(0xA8A496));
            c.fill_rect(0, 32, 64, 1, hex(0xC8C4B6));
            c.fill_rect(32, 0, 1, 64, hex(0xC8C4B6));
            for (int i = 0; i < 160; ++i) c.fill_rect(3 + h01(i + 200) * 60, 3 + h01(i + 300) * 60, 1, 1, hex(0x9A968A));
            break;
        case 1:  // wood panelling
            c.clear(hex(0x8A5A34));
            for (int x = 0; x < 64; x += 16) {
                c.fill_rect(x, 0, 1, 64, hex(0x4A2A14));
                for (int k = 0; k < 6; ++k) c.fill_rect(x + 3 + h01(x + k) * 10, 0, 1, 64, hex(0x9A6A40, .5f));
            }
            break;
        default:  // a night sky of the vaporwave kind
            c.clear(hex(0x1A0A30));
            for (int i = 0; i < 26; ++i) c.fill_rect(h01(i + 400) * 64, h01(i + 500) * 64, 1, 1, i % 4 ? hex(0xFFFFFF) : hex(0xF080E0));
            c.fill_rect(0, 0, 64, 1, hex(0x3A1A5A));
            c.fill_rect(0, 0, 1, 64, hex(0x3A1A5A));
            break;
    }
    return cache.emplace(theme % 3, to_tex(c)).first->second;
}

// ------------------------------------------------------------------ doors, pads, plates
const Tex32& tex_door(int color) {
    static std::map<int, Tex32> cache;
    auto it = cache.find(color);
    if (it != cache.end()) return it->second;
    Canvas c;
    c.resize(64, 64);
    c.clear(hex(0x8A9098));
    for (int y = 4; y < 64; y += 8) c.fill_rect(4, y, 56, 1, hex(0x6A7078));
    c.fill_rect(0, 0, 64, 4, hex(0x4A5058));
    c.fill_rect(0, 0, 4, 64, hex(0x4A5058));
    c.fill_rect(60, 0, 4, 64, hex(0x4A5058));
    const Col k = kKey[color % kColors];
    c.fill_rect(4, 26, 56, 12, k);
    c.fill_rect(4, 26, 56, 2, mix(k, hex(0xFFFFFF), .4f));
    c.fill_circle(32, 32, 7, hex(0x2A2E34));
    c.fill_circle(32, 32, 5, k);
    c.fill_circle(32, 30, 1.6, hex(0x101010));
    c.fill_rect(31, 31, 2, 4, hex(0x101010));
    return cache.emplace(color, to_tex(c)).first->second;
}

const Tex32& tex_pad(int color, bool lit) {
    static std::map<int, Tex32> cache;
    const int key = color * 2 + lit;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    Canvas c;
    c.resize(64, 64);
    c.clear({0, 0, 0, 0});
    const Col k = kKey[color % kColors];
    c.fill_circle(32, 32, 28, hex(0x2A2E34));
    c.fill_circle(32, 32, 24, lit ? mix(k, hex(0xFFFFFF), .35f) : mix(k, hex(0x000000), .35f));
    c.begin(); c.circle(32, 32, 18); c.stroke(lit ? hex(0xFFFFFF, .8f) : mix(k, hex(0x000000), .55f), 3);
    c.fill_circle(26, 25, 5, hex(0xFFFFFF, lit ? .6f : .2f));
    return cache.emplace(key, to_tex(c)).first->second;
}

const Tex32& tex_elevator() {
    static Tex32 t = [] {
        Canvas c;
        c.resize(64, 64);
        c.clear(hex(0x9AA0A8));
        for (int y = 2; y < 64; y += 8)
            for (int x = 2; x < 64; x += 8) {
                c.begin(); c.move(x, y + 3); c.line(x + 4, y); c.stroke(hex(0xC8CED6), 1.5);
                c.begin(); c.move(x + 3, y + 7); c.line(x + 7, y + 4); c.stroke(hex(0x6A7078), 1);
            }
        // hazard stripes all round
        for (int i = -64; i < 128; i += 8) {
            for (int e = 0; e < 4; ++e) {
                c.save();
                c.translate(32, 32);
                c.rotate(e * 1.5708);
                c.translate(-32, -32);
                c.begin(); c.move(i, 0); c.line(i + 4, 0); c.line(i + 1, 5); c.line(i - 3, 5); c.close(); c.fill(hex(0xF0C020));
                c.restore();
            }
        }
        return to_tex(c);
    }();
    return t;
}

const Tex32& tex_marble() {
    static Tex32 t = [] {
        Canvas c;
        c.resize(64, 64);
        c.clear(hex(0x8E8C88));
        for (int i = 0; i < 300; ++i) c.fill_rect(h01(i + 700) * 64, h01(i + 800) * 64, 1, 1, h01(i + 900) > .5 ? hex(0xA6A49E) : hex(0x6E6C68));
        for (int k = 0; k < 5; ++k) {
            c.begin();
            double x = h01(k + 40) * 64, y = 0;
            c.move(x, y);
            for (int s = 0; s < 8; ++s) { x += (h01(k * 9 + s) - .5) * 16; y += 8; c.line(x, y); }
            c.stroke(hex(0x5A5854), 1.2);
        }
        return to_tex(c);
    }();
    return t;
}

const Tex32& tex_glow() {
    static Tex32 t = [] {
        Canvas c;
        c.resize(32, 32);
        c.clear({0, 0, 0, 0});
        c.begin(); c.circle(16, 16, 16); c.fill(Paint::rad(16, 16, 16, {{0, {1, 1, 1, 1}}, {.3f, {1, 1, 1, .6f}}, {1, {1, 1, 1, 0}}}));
        return to_tex(c);
    }();
    return t;
}

// ------------------------------------------------------------------ posters: vaporwave and old computers
namespace {
void bust(Canvas& c, double cx, double cy, double s, Col stone, Col shade) {
    // a classical marble bust in profile-ish three quarters: head, curls, neck, shoulders on a plinth
    c.begin(); c.move(cx - 22 * s, cy + 30 * s); c.quad(cx - 20 * s, cy + 10 * s, cx - 8 * s, cy + 8 * s); c.line(cx - 6 * s, cy - 2 * s);
    c.quad(cx - 14 * s, cy - 8 * s, cx - 12 * s, cy - 18 * s); c.quad(cx - 8 * s, cy - 30 * s, cx + 4 * s, cy - 28 * s); c.quad(cx + 14 * s, cy - 26 * s, cx + 12 * s, cy - 14 * s);
    c.line(cx + 15 * s, cy - 8 * s); c.line(cx + 11 * s, cy - 7 * s); c.quad(cx + 10 * s, cy - 2 * s, cx + 6 * s, cy); c.line(cx + 6 * s, cy + 8 * s);
    c.quad(cx + 20 * s, cy + 10 * s, cx + 22 * s, cy + 30 * s); c.close();
    c.fill(stone);
    for (int k = 0; k < 6; ++k) c.fill_circle(cx - 8 * s + k * 3.5 * s, cy - 26 * s + (k % 2) * 3 * s, 3.2 * s, mix(stone, shade, .3f));
    c.begin(); c.move(cx - 6 * s, cy - 2 * s); c.quad(cx - 2 * s, cy + 4 * s, cx + 6 * s, cy); c.stroke(shade, 1.2 * s);
    c.fill_rect(cx - 24 * s, cy + 30 * s, 48 * s, 5 * s, mix(stone, shade, .5f));
}
void palm(Canvas& c, double x, double y, double s, Col col) {
    c.begin(); c.move(x, y); c.quad(x + 4 * s, y - 20 * s, x + 2 * s, y - 40 * s); c.stroke(col, 3 * s);
    for (int k = 0; k < 6; ++k) {
        const double a = -2.6 + k * .55;
        c.begin(); c.move(x + 2 * s, y - 40 * s); c.quad(x + 2 * s + std::cos(a) * 12 * s, y - 46 * s + std::sin(a) * 4 * s, x + 2 * s + std::cos(a) * 20 * s, y - 36 * s + std::sin(a) * 10 * s);
        c.stroke(col, 2.5 * s);
    }
}

std::vector<std::unique_ptr<Tex32>>& posters() {
    static std::vector<std::unique_ptr<Tex32>> v;
    if (!v.empty()) return v;
    auto add = [&](const Canvas& c) { v.push_back(std::make_unique<Tex32>(to_tex(c))); };
    Canvas c;
    // 0: the sunset over the neon grid
    c.resize(64, 64);
    c.begin(); c.rect(0, 0, 64, 64); c.fill(Paint::lin(0, 0, 0, 40, {{0, hex(0x2A0A4A)}, {.6f, hex(0xE0407A)}, {1, hex(0xF8A050)}}));
    c.fill_circle(32, 34, 14, hex(0xFFD860));
    for (int k = 0; k < 4; ++k) c.fill_rect(16, 34 + k * 3.5, 32, 1.2 + k * .4, hex(0xE0407A));
    c.fill_rect(0, 40, 64, 24, hex(0x14082A));
    for (int k = 0; k < 7; ++k) c.fill_rect(0, 41 + k * k * .5 + k, 64, 1, hex(0xF040D0));
    for (int k = -6; k <= 6; ++k) c.stroke_line(32 + k * 3, 40, 32 + k * 12, 64, hex(0xF040D0), 1);
    add(c);
    // 1: the marble bust on pink and teal
    c.clear(hex(0xF4A0C8));
    c.fill_rect(0, 40, 64, 24, hex(0x40D0C8));
    for (int y = 40; y < 64; y += 6) c.fill_rect(0, y, 64, 1, hex(0x20A0A0));
    bust(c, 32, 30, .85, hex(0xF0EEE8), hex(0x9A9A98));
    add(c);
    // 2: a palm, a dolphin, a teal sea
    c.begin(); c.rect(0, 0, 64, 64); c.fill(Paint::lin(0, 0, 0, 64, {{0, hex(0x60E0F0)}, {1, hex(0x2060C0)}}));
    palm(c, 14, 62, 1.0, hex(0x1A2A3A));
    c.begin(); c.move(30, 40); c.quad(42, 26, 56, 36); c.quad(50, 34, 46, 40); c.quad(40, 38, 30, 40); c.fill(hex(0x8A9AC8));
    c.begin(); c.move(44, 31); c.line(47, 25); c.line(49, 32); c.fill(hex(0x8A9AC8));
    add(c);
    // 3: AESTHETIC, in the wide spacing it deserves
    c.begin(); c.rect(0, 0, 64, 64); c.fill(Paint::lin(0, 0, 64, 64, {{0, hex(0x80F0E0)}, {.5f, hex(0xC090F0)}, {1, hex(0xF090C0)}}));
    pixel_text(c, "A E S", 32 - text_width("A E S", 1.4) / 2, 14, 1.4, hex(0xFFFFFF));
    pixel_text(c, "T H E", 32 - text_width("T H E", 1.4) / 2, 28, 1.4, hex(0xFFFFFF));
    pixel_text(c, "T I C", 32 - text_width("T I C", 1.4) / 2, 42, 1.4, hex(0xFFFFFF));
    add(c);
    // 4: Rainstar 95: a waving four-colour star on black
    c.clear(hex(0x080810));
    {
        const Col q[4] = {hex(0xF04848), hex(0x48C060), hex(0x4890F0), hex(0xF0D040)};
        for (int k = 0; k < 4; ++k) {
            const double a = k * 1.5708 + .3;
            c.begin(); c.move(30, 28); c.line(30 + std::cos(a) * 18, 28 + std::sin(a) * 18); c.line(30 + std::cos(a + .8) * 8, 28 + std::sin(a + .8) * 8); c.close(); c.fill(q[k]);
        }
        for (int k = 0; k < 6; ++k) c.fill_rect(4 + k * 2, 22 + k * 2.5, 6 - k, 1.2, hex(0xFFFFFF, .6f));
        pixel_text(c, "RAINSTAR 95", 32 - text_width("RAINSTAR 95", 1) / 2, 52, 1, hex(0xE8E8F0));
    }
    add(c);
    // 5: a penguin (Tux, by Larry Ewing, drawn after him with thanks)
    c.clear(hex(0xF0F0E8));
    c.fill_ellipse(32, 36, 15, 20, hex(0x14141A));
    c.fill_ellipse(32, 40, 10, 15, hex(0xF8F8F0));
    c.fill_ellipse(32, 18, 11, 10, hex(0x14141A));
    c.fill_ellipse(28, 17, 3, 4, hex(0xFFFFFF)); c.fill_ellipse(36, 17, 3, 4, hex(0xFFFFFF));
    c.fill_circle(28.5, 18, 1.4, hex(0x000000)); c.fill_circle(35.5, 18, 1.4, hex(0x000000));
    c.begin(); c.move(26, 23); c.quad(32, 20, 38, 23); c.quad(32, 27, 26, 23); c.fill(hex(0xF0B020));
    c.fill_ellipse(24, 56, 7, 3, hex(0xF0B020)); c.fill_ellipse(40, 56, 7, 3, hex(0xF0B020));
    add(c);
    // 6: a gnu, horned and bearded
    c.clear(hex(0xE8E0C8));
    c.fill_ellipse(32, 34, 13, 17, hex(0x6A4A2A));
    c.fill_ellipse(32, 44, 7, 8, hex(0x4A3018));
    c.begin(); c.move(20, 22); c.quad(10, 10, 18, 6); c.stroke(hex(0x3A2A1A), 2.5);
    c.begin(); c.move(44, 22); c.quad(54, 10, 46, 6); c.stroke(hex(0x3A2A1A), 2.5);
    c.fill_circle(27, 30, 2, hex(0xFFFFFF)); c.fill_circle(37, 30, 2, hex(0xFFFFFF));
    c.fill_circle(27, 30, 1, hex(0x000000)); c.fill_circle(37, 30, 1, hex(0x000000));
    for (int k = 0; k < 5; ++k) c.stroke_line(28 + k * 2, 50, 27 + k * 2.5, 58, hex(0x3A2A1A), 1);
    add(c);
    // 7: a floppy disk
    c.clear(hex(0x2A4A8A));
    c.fill_rect(12, 10, 40, 44, hex(0x1A1A22));
    c.fill_rect(20, 10, 22, 14, hex(0xB8BCC4));
    c.fill_rect(34, 12, 5, 10, hex(0x1A1A22));
    c.fill_rect(18, 32, 28, 20, hex(0xF0EEE4));
    for (int k = 0; k < 3; ++k) c.fill_rect(21, 37 + k * 5, 22, 1, hex(0x8A8AA0));
    add(c);
    // 8: please wait (a dialog with an hourglass and a progress bar)
    c.clear(hex(0x008080));
    c.fill_rect(6, 14, 52, 36, hex(0xC0C0C0));
    c.fill_rect(6, 14, 52, 7, hex(0x000080));
    pixel_text(c, "WAIT", 8, 15.5, .7, hex(0xFFFFFF));
    c.fill_rect(6, 14, 52, 1, hex(0xFFFFFF)); c.fill_rect(6, 14, 1, 36, hex(0xFFFFFF));
    c.fill_rect(57, 14, 1, 36, hex(0x404040)); c.fill_rect(6, 49, 52, 1, hex(0x404040));
    c.begin(); c.move(14, 26); c.line(22, 26); c.line(18, 31); c.line(22, 36); c.line(14, 36); c.line(18, 31); c.close(); c.fill(hex(0x202020));
    c.fill_rect(26, 38, 28, 6, hex(0xFFFFFF));
    c.fill_rect(27, 39, 17, 4, hex(0x000080));
    pixel_text(c, "PLEASE", 27, 27, .8, hex(0x000000));
    add(c);
    // 9: memphis: squiggles, triangles, dots and a checkerboard
    c.clear(hex(0xF4F0E8));
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 4; ++x) if ((x + y) % 2) c.fill_rect(x * 8, 48 + y * 8, 8, 8, hex(0x101010));
    c.begin(); c.move(36, 52); c.line(56, 52); c.line(46, 36); c.close(); c.fill(hex(0x40C8E8));
    c.begin(); c.move(6, 14); for (int k = 0; k < 6; ++k) c.quad(10 + k * 8, k % 2 ? 6 : 22, 14 + k * 8, 14); c.stroke(hex(0xF040A0), 2.5);
    for (int k = 0; k < 9; ++k) c.fill_circle(10 + (k % 3) * 6, 28 + (k / 3) * 6, 1.4, hex(0xF0C020));
    c.fill_circle(48, 24, 8, hex(0xF0C020));
    add(c);
    // 10: an old computer, beige and humming
    c.clear(hex(0x3A6A9A));
    c.fill_rect(14, 10, 36, 30, hex(0xD8D0B8));
    c.fill_rect(18, 14, 28, 21, hex(0x1A3A2A));
    for (int k = 0; k < 4; ++k) c.fill_rect(20, 17 + k * 4, 10 + (k * 7) % 14, 1.5, hex(0x40F080));
    c.fill_rect(10, 44, 44, 10, hex(0xD8D0B8));
    c.fill_rect(14, 46, 20, 2, hex(0x6A6A60));
    c.fill_rect(42, 46, 8, 5, hex(0x6A6A60));
    add(c);
    // 11: a smiling face, as at the start of a certain old maze
    c.clear(hex(0x101828));
    c.fill_circle(32, 32, 22, hex(0xF8E030));
    c.fill_ellipse(24, 26, 3, 5, hex(0x101010)); c.fill_ellipse(40, 26, 3, 5, hex(0x101010));
    c.begin(); c.move(20, 38); c.quad(32, 52, 44, 38); c.stroke(hex(0x101010), 3);
    add(c);
    // 12: the blue screen of contentment
    c.clear(hex(0x0020A8));
    pixel_text(c, ":)", 8, 10, 2.2, hex(0xFFFFFF));
    pixel_text(c, "ALL IS", 8, 34, 1, hex(0xFFFFFF));
    pixel_text(c, "WELL.", 8, 44, 1, hex(0xFFFFFF));
    add(c);
    // 13: a starfield with chrome lettering
    c.clear(hex(0x000010));
    for (int i = 0; i < 40; ++i) c.fill_rect(h01(i + 1000) * 64, h01(i + 1100) * 64, 1, 1, hex(0xFFFFFF, static_cast<float>(.4 + .6 * h01(i + 1200))));
    pixel_text(c, "MAZE", 32 - text_width("MAZE", 2) / 2, 18, 2, hex(0xC0C8D8));
    pixel_text(c, "MAZE", 32 - text_width("MAZE", 2) / 2, 17, 2, hex(0xF0F4FF));
    pixel_text(c, "95", 32 - text_width("95", 2) / 2, 38, 2, hex(0xF040D0));
    add(c);
    // 14: the lady with the smile (after Leonardo, who will not mind)
    c.clear(hex(0x5A6A3A));
    c.begin(); c.rect(0, 0, 64, 64); c.fill(Paint::lin(0, 0, 0, 64, {{0, hex(0x7A8A5A)}, {1, hex(0x3A4A2A)}}));
    c.fill_ellipse(32, 62, 22, 20, hex(0x2A2418));
    c.fill_ellipse(32, 24, 10, 13, hex(0xD8B080));
    c.begin(); c.move(20, 26); c.quad(20, 6, 32, 8); c.quad(44, 6, 44, 26); c.line(46, 44); c.line(40, 40); c.quad(42, 18, 32, 14); c.quad(22, 18, 24, 40); c.line(18, 44); c.close(); c.fill(hex(0x2A1A10));
    c.fill_ellipse(28, 23, 1.6, 1, hex(0x2A1A10)); c.fill_ellipse(36, 23, 1.6, 1, hex(0x2A1A10));
    c.begin(); c.move(28, 31); c.quad(32, 33, 36, 31); c.stroke(hex(0x8A5A3A), 1);
    c.fill_ellipse(32, 46, 8, 3, hex(0xD8B080));
    add(c);
    // 15: KILROY WAS HERE
    c.clear(hex(0xC8C0B0));
    c.fill_rect(0, 30, 64, 3, hex(0x6A6050));
    c.fill_ellipse(32, 30, 12, 9, hex(0xC8C0B0));
    c.begin(); c.ellipse(32, 30, 12, 9); c.stroke(hex(0x2A2418), 1.5);
    c.fill_rect(18, 30, 28, 4, hex(0xC8C0B0));
    c.fill_circle(27, 28, 2, hex(0xFFFFFF)); c.fill_circle(37, 28, 2, hex(0xFFFFFF));
    c.fill_circle(27, 29, 1, hex(0x000000)); c.fill_circle(37, 29, 1, hex(0x000000));
    c.begin(); c.move(32, 26); c.quad(34, 36, 32, 40); c.stroke(hex(0x2A2418), 1.5);
    for (int s = -1; s <= 1; s += 2) { c.begin(); c.move(32 + s * 18, 34); c.line(32 + s * 18, 30); c.line(32 + s * 21, 30); c.stroke(hex(0x2A2418), 1.5); }
    pixel_text(c, "KILROY", 32 - text_width("KILROY", 1) / 2, 44, 1, hex(0x2A2418));
    pixel_text(c, "WAS HERE", 32 - text_width("WAS HERE", 1) / 2, 53, 1, hex(0x2A2418));
    add(c);
    return v;
}

std::vector<std::pair<std::unique_ptr<Tex32>, bool>>& paints() {
    static std::vector<std::pair<std::unique_ptr<Tex32>, bool>> v;
    if (!v.empty()) return v;
    auto add = [&](const Canvas& c, bool glitch) { v.push_back({std::make_unique<Tex32>(to_tex(c)), glitch}); };
    Canvas c;
    c.resize(64, 64);
    // zebra
    c.clear(hex(0xF4F4F0));
    for (int k = 0; k < 8; ++k) { c.begin(); c.move(k * 10 - 6, 0); c.quad(k * 10 + 6, 32, k * 10 - 4, 64); c.line(k * 10 + 1, 64); c.quad(k * 10 + 11, 32, k * 10, 0); c.close(); c.fill(hex(0x101010)); }
    add(c, false);
    // rainbow plasma
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const double v2 = std::sin(x * .2) + std::sin(y * .15) + std::sin((x + y) * .1);
            const double hue = (v2 + 3) / 6;
            c.fill_rect(x, y, 1, 1, {static_cast<float>(.5 + .5 * std::sin(6.283 * hue)), static_cast<float>(.5 + .5 * std::sin(6.283 * (hue + .33))), static_cast<float>(.5 + .5 * std::sin(6.283 * (hue + .66))), 1});
        }
    add(c, true);
    // eyes, watching
    c.clear(hex(0xF0A0C0));
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
        const double cx = 16 + x * 32 + (y % 2) * 8, cy = 16 + y * 32;
        c.fill_ellipse(cx, cy, 11, 7, hex(0xFFFFFF)); c.fill_circle(cx + 2, cy, 4.5, hex(0x2A8A4A)); c.fill_circle(cx + 2, cy, 2, hex(0x000000));
        c.begin(); c.ellipse(cx, cy, 11, 7); c.stroke(hex(0x5A1A3A), 1.2);
    }
    add(c, false);
    // polka dots
    c.clear(hex(0xF8D040));
    for (int y = 0; y < 64; y += 16) for (int x = 0; x < 64; x += 16) c.fill_circle(x + 8 + (y / 16 % 2) * 8, y + 8, 5, hex(0xE83A6A));
    add(c, false);
    // television static
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) { const float g = static_cast<float>(h01(x * 64 + y + 5000)); c.fill_rect(x, y, 1, 1, {g, g, g, 1}); }
    add(c, true);
    // a checkerboard that will not hold still
    c.clear(hex(0x101010));
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) if ((x + y) % 2) c.fill_rect(x * 8, y * 8, 8, 8, hex(0xF0F0F0));
    add(c, true);
    // fish scales
    c.clear(hex(0x2A8AA8));
    for (int y = 0; y < 72; y += 8) for (int x = -8; x < 72; x += 16) { c.begin(); c.circle(x + (y / 8 % 2) * 8, y, 8); c.stroke(hex(0x8AE8F0), 1.5); }
    add(c, false);
    // tie-dye
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const double r = std::hypot(x - 32, y - 32), a = std::atan2(y - 32, x - 32);
            const double hue = r * .05 + a * .3;
            c.fill_rect(x, y, 1, 1, {static_cast<float>(.6 + .4 * std::sin(hue * 6)), static_cast<float>(.6 + .4 * std::sin(hue * 6 + 2)), static_cast<float>(.6 + .4 * std::sin(hue * 6 + 4)), 1});
        }
    add(c, true);
    // ERROR, over and over
    c.clear(hex(0x0000A0));
    for (int k = 0; k < 6; ++k) pixel_text(c, "ERROR", 2 + (k % 2) * 6, 2 + k * 10, 1, k % 2 ? hex(0xF0F0F0) : hex(0xF0F040));
    add(c, true);
    // little houndstooth
    c.clear(hex(0xF0E8D8));
    for (int y = 0; y < 64; y += 8) for (int x = 0; x < 64; x += 8) { c.begin(); c.move(x, y); c.line(x + 4, y); c.line(x + 8, y + 4); c.line(x + 4, y + 4); c.line(x + 4, y + 8); c.line(x, y + 4); c.close(); c.fill(hex(0x2A2A2A)); }
    add(c, false);
    return v;
}
}  // namespace

int poster_count() { return static_cast<int>(posters().size()); }
const Tex32& tex_poster(int i) { return *posters()[static_cast<size_t>(((i % poster_count()) + poster_count()) % poster_count())]; }
int paint_count() { return static_cast<int>(paints().size()); }
const Tex32& tex_paint(int i) { return *paints()[static_cast<size_t>(((i % paint_count()) + paint_count()) % paint_count())].first; }
bool paint_glitches(int i) { return paints()[static_cast<size_t>(((i % paint_count()) + paint_count()) % paint_count())].second; }

}  // namespace mz
