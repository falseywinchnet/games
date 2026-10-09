#include "rewards.hpp"

#include "platform/canvas.hpp"

#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace mz {

namespace {
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
const Col K = hex(0x1A1418);  // ink
void ink(Canvas& c, double w = 1.6) { c.stroke(K, w); }

struct Reward {
    std::string article, name;
    std::function<void(Canvas&)> draw;
};

const std::vector<Reward>& table() {
    static std::vector<Reward> v = {
        {"the", "cheese", [](Canvas& c) {
             c.begin(); c.move(6, 46); c.line(58, 30); c.line(58, 50); c.line(6, 58); c.close(); c.fill(hex(0xF0C838)); c.begin(); c.move(6, 46); c.line(58, 30); c.line(58, 50); c.line(6, 58); c.close(); ink(c);
             c.begin(); c.move(6, 46); c.line(58, 30); c.line(48, 22); c.close(); c.fill(hex(0xF8DC60)); c.begin(); c.move(6, 46); c.line(58, 30); c.line(48, 22); c.close(); ink(c);
             for (auto [x, y, r] : std::vector<std::array<double, 3>>{{22, 49, 3.5}, {38, 46, 2.5}, {50, 42, 3}, {30, 54, 2}}) c.fill_circle(x, y, r, hex(0xC89818));
         }},
        {"a", "bucket of fried chicken", [](Canvas& c) {
             // drumsticks poking out, bone ends up
             for (int k = 0; k < 4; ++k) {
                 const double x = 17 + k * 10, y = 18 - (k % 2) * 3, a = -.5 + k * .33;
                 c.save(); c.translate(x, y); c.rotate(a);
                 c.fill_ellipse(0, 4, 7, 9, hex(0xC8782A)); c.fill_ellipse(-2, 2, 3, 4, hex(0xE09A44));
                 c.fill_rect(-1.5, -10, 3, 8, hex(0xF4ECDC)); c.fill_circle(-2, -11, 2.4, hex(0xF4ECDC)); c.fill_circle(2, -11, 2.4, hex(0xF4ECDC));
                 c.begin(); c.ellipse(0, 4, 7, 9); c.stroke(K, 1);
                 c.restore();
             }
             c.begin(); c.move(10, 20); c.line(54, 20); c.line(48, 60); c.line(16, 60); c.close(); c.fill(hex(0xF4F0E8));
             for (int k = 0; k < 4; ++k) { c.begin(); c.move(13 + k * 11, 20); c.line(19 + k * 11, 20); c.line(19.5 + k * 9.5 - k * .4, 60); c.line(16 + k * 9, 60); c.close(); c.fill(hex(0xD8202A)); }
             c.begin(); c.move(10, 20); c.line(54, 20); c.line(48, 60); c.line(16, 60); c.close(); ink(c);
         }},
        {"a", "slice of pizza", [](Canvas& c) {
             c.begin(); c.move(32, 60); c.line(8, 12); c.quad(32, 2, 56, 12); c.close(); c.fill(hex(0xF0C060));
             c.begin(); c.move(32, 56); c.line(12, 16); c.quad(32, 8, 52, 16); c.close(); c.fill(hex(0xE04030));
             for (auto [x, y] : std::vector<std::array<double, 2>>{{26, 22}, {38, 24}, {32, 36}, {24, 30}}) c.fill_circle(x, y, 3.5, hex(0x9A2020));
             c.begin(); c.move(32, 60); c.line(8, 12); c.quad(32, 2, 56, 12); c.close(); ink(c);
         }},
        {"a", "rubber duck", [](Canvas& c) {
             c.fill_ellipse(30, 44, 22, 14, hex(0xF8D828)); c.fill_circle(40, 24, 12, hex(0xF8D828));
             c.begin(); c.move(50, 24); c.line(60, 27); c.line(50, 30); c.close(); c.fill(hex(0xF08020));
             c.fill_circle(43, 21, 2, K);
             c.begin(); c.ellipse(30, 44, 22, 14); ink(c); c.begin(); c.circle(40, 24, 12); ink(c);
             c.begin(); c.move(16, 42); c.quad(26, 36, 34, 44); c.stroke(hex(0xD8B010), 2);
         }},
        {"a", "golden trophy", [](Canvas& c) {
             c.begin(); c.move(16, 8); c.line(48, 8); c.quad(48, 34, 32, 38); c.quad(16, 34, 16, 8); c.fill(hex(0xF0C030));
             c.begin(); c.move(16, 12); c.quad(4, 14, 8, 24); c.quad(12, 30, 18, 28); c.stroke(hex(0xD8A020), 3);
             c.begin(); c.move(48, 12); c.quad(60, 14, 56, 24); c.quad(52, 30, 46, 28); c.stroke(hex(0xD8A020), 3);
             c.fill_rect(29, 38, 6, 10, hex(0xD8A020)); c.fill_rect(18, 48, 28, 8, hex(0x6A4A2A));
             c.fill_rect(20, 12, 4, 16, hex(0xFFF0A0));
         }},
        {"a", "floppy disk", [](Canvas& c) {
             c.fill_rect(8, 8, 48, 48, hex(0x2A2A3A)); c.fill_rect(18, 8, 26, 16, hex(0xB8BCC4)); c.fill_rect(36, 10, 5, 12, hex(0x2A2A3A));
             c.fill_rect(14, 32, 36, 24, hex(0xF0EEE4)); for (int k = 0; k < 3; ++k) c.fill_rect(18, 37 + k * 6, 28, 1, hex(0x7A7AA0));
             c.begin(); c.rect(8, 8, 48, 48); ink(c);
         }},
        {"a", "lava lamp", [](Canvas& c) {
             c.begin(); c.move(22, 54); c.line(26, 12); c.line(38, 12); c.line(42, 54); c.close(); c.fill(hex(0x6A2AA0));
             c.fill_ellipse(30, 22, 4, 6, hex(0xF8603A)); c.fill_ellipse(36, 38, 5, 7, hex(0xF8603A)); c.fill_ellipse(29, 48, 4, 3, hex(0xF8603A));
             c.begin(); c.move(26, 12); c.line(28, 4); c.line(36, 4); c.line(38, 12); c.close(); c.fill(hex(0xC0C0C8));
             c.begin(); c.move(22, 54); c.line(18, 62); c.line(46, 62); c.line(42, 54); c.close(); c.fill(hex(0xC0C0C8));
             c.begin(); c.move(22, 54); c.line(26, 12); c.line(38, 12); c.line(42, 54); c.close(); ink(c);
         }},
        {"a", "boombox", [](Canvas& c) {
             c.fill_rect(4, 20, 56, 32, hex(0x3A3A44)); c.fill_rect(14, 10, 36, 3, hex(0x8A8A94));
             c.fill_circle(16, 38, 10, hex(0x1A1A20)); c.fill_circle(48, 38, 10, hex(0x1A1A20)); c.fill_circle(16, 38, 4, hex(0x6A6A74)); c.fill_circle(48, 38, 4, hex(0x6A6A74));
             c.fill_rect(26, 26, 12, 8, hex(0x8AD0E0)); for (int k = 0; k < 4; ++k) c.fill_rect(26 + k * 3, 40, 2, 4, hex(0xC0C0C8));
             c.begin(); c.rect(4, 20, 56, 32); ink(c);
         }},
        {"a", "cassette tape", [](Canvas& c) {
             c.fill_rect(4, 16, 56, 34, hex(0x2A2A30)); c.fill_rect(10, 20, 44, 16, hex(0xF0E070));
             c.fill_rect(18, 26, 28, 7, hex(0x3A3A40)); c.fill_circle(23, 29.5, 3, hex(0xE8E8E8)); c.fill_circle(41, 29.5, 3, hex(0xE8E8E8));
             c.begin(); c.move(16, 50); c.line(20, 42); c.line(44, 42); c.line(48, 50); c.stroke(hex(0x5A5A60), 1.5);
             c.begin(); c.rect(4, 16, 56, 34); ink(c);
         }},
        {"a", "VHS tape", [](Canvas& c) {
             c.fill_rect(4, 18, 56, 30, hex(0x1A1A1E)); c.fill_rect(10, 22, 44, 9, hex(0xF0F0F0)); c.fill_rect(12, 24, 20, 2, hex(0xD02020));
             c.fill_rect(14, 34, 36, 10, hex(0x3A3A40)); c.fill_circle(22, 39, 4, hex(0x6A6A70)); c.fill_circle(42, 39, 4, hex(0x6A6A70));
             c.begin(); c.rect(4, 18, 56, 30); ink(c);
         }},
        {"a", "rotary telephone", [](Canvas& c) {
             c.begin(); c.move(8, 56); c.line(14, 30); c.line(50, 30); c.line(56, 56); c.close(); c.fill(hex(0xD82838));
             c.fill_circle(32, 44, 10, hex(0xF0F0F0)); for (int k = 0; k < 10; ++k) c.fill_circle(32 + std::cos(k * .628) * 7, 44 + std::sin(k * .628) * 7, 1.3, hex(0x2A2A2A));
             c.begin(); c.move(8, 24); c.quad(32, 14, 56, 24); c.line(56, 30); c.line(48, 30); c.quad(32, 22, 16, 30); c.line(8, 30); c.close(); c.fill(hex(0xB81828));
             c.begin(); c.move(8, 56); c.line(14, 30); c.line(50, 30); c.line(56, 56); c.close(); ink(c);
         }},
        {"a", "glazed donut", [](Canvas& c) {
             c.fill_ellipse(32, 34, 26, 22, hex(0xD8A060)); c.fill_ellipse(32, 31, 23, 18, hex(0xF070B0));
             for (int k = 0; k < 12; ++k) { const double a = k * .52; c.fill_rect(32 + std::cos(a) * 15, 31 + std::sin(a) * 12, 3, 1.4, k % 3 == 0 ? hex(0x40C0F0) : k % 3 == 1 ? hex(0xF8F040) : hex(0xFFFFFF)); }
             c.fill_ellipse(32, 32, 7, 5, hex(0xC08040));
             c.begin(); c.ellipse(32, 34, 26, 22); ink(c);
         }},
        {"a", "birthday cake", [](Canvas& c) {
             c.fill_rect(10, 30, 44, 26, hex(0xF8E0E8)); c.fill_rect(10, 30, 44, 6, hex(0xF070A0)); for (int k = 0; k < 6; ++k) c.fill_circle(14 + k * 7.2, 36, 3, hex(0xF070A0));
             for (int k = 0; k < 3; ++k) { c.fill_rect(20 + k * 11, 16, 3, 14, hex(0x60C0F0)); c.fill_ellipse(21.5 + k * 11, 12, 2.5, 4, hex(0xF8C030)); }
             c.begin(); c.rect(10, 30, 44, 26); ink(c);
         }},
        {"a", "hamburger", [](Canvas& c) {
             c.begin(); c.move(8, 30); c.quad(32, 4, 56, 30); c.close(); c.fill(hex(0xE0A050));
             for (int k = 0; k < 6; ++k) c.fill_ellipse(18 + k * 6, 18 + (k % 2) * 4, 1.5, 1, hex(0xFFF0C0));
             c.fill_rect(6, 30, 52, 5, hex(0x4AB840)); c.fill_rect(8, 35, 48, 6, hex(0xF8D040)); c.fill_rect(8, 41, 48, 8, hex(0x6A3A1A));
             c.begin(); c.move(8, 49); c.line(56, 49); c.quad(56, 58, 32, 58); c.quad(8, 58, 8, 49); c.fill(hex(0xE0A050));
             c.begin(); c.move(8, 30); c.quad(32, 4, 56, 30); c.close(); ink(c);
         }},
        {"a", "hot dog", [](Canvas& c) {
             c.fill_ellipse(32, 40, 28, 12, hex(0xE8B060)); c.fill_ellipse(32, 34, 27, 7, hex(0xC8503A));
             c.begin(); c.move(10, 33); for (int k = 0; k < 8; ++k) c.quad(13 + k * 6, k % 2 ? 29 : 37, 16 + k * 6, 33); c.stroke(hex(0xF8D820), 2);
             c.begin(); c.ellipse(32, 40, 28, 12); ink(c);
         }},
        {"a", "goldfish in a bowl", [](Canvas& c) {
             c.fill_circle(32, 36, 24, hex(0xA0E0F8, .8f)); c.fill_rect(10, 8, 44, 10, {0, 0, 0, 0});
             c.fill_ellipse(30, 38, 9, 6, hex(0xF88020)); c.begin(); c.move(38, 38); c.line(46, 32); c.line(46, 44); c.close(); c.fill(hex(0xF88020));
             c.fill_circle(26, 37, 1.5, K); c.fill_circle(20, 26, 2, hex(0xFFFFFF)); c.fill_circle(24, 18, 1.5, hex(0xFFFFFF));
             c.begin(); c.circle(32, 36, 24); ink(c);
         }},
        {"a", "snow globe", [](Canvas& c) {
             c.fill_circle(32, 28, 22, hex(0xC8E8F8)); c.begin(); c.move(20, 40); c.line(32, 18); c.line(44, 40); c.close(); c.fill(hex(0x2A8A4A));
             for (int k = 0; k < 14; ++k) c.fill_circle(14 + std::fmod(k * 17.0, 36), 12 + std::fmod(k * 11.0, 30), 1, hex(0xFFFFFF));
             c.fill_rect(14, 48, 36, 10, hex(0x6A3A2A)); c.begin(); c.circle(32, 28, 22); ink(c);
         }},
        {"a", "potted cactus", [](Canvas& c) {
             c.begin(); c.move(16, 44); c.line(48, 44); c.line(44, 60); c.line(20, 60); c.close(); c.fill(hex(0xC86A3A));
             c.fill_rect(27, 10, 10, 34, hex(0x3A9A4A)); c.fill_ellipse(32, 10, 5, 4, hex(0x3A9A4A));
             c.fill_rect(16, 22, 6, 12, hex(0x3A9A4A)); c.fill_rect(16, 30, 12, 5, hex(0x3A9A4A)); c.fill_rect(42, 16, 6, 12, hex(0x3A9A4A)); c.fill_rect(36, 24, 12, 5, hex(0x3A9A4A));
             c.fill_circle(32, 6, 3, hex(0xF060A0));
         }},
        {"a", "disco ball", [](Canvas& c) {
             c.stroke_line(32, 0, 32, 10, hex(0x6A6A6A), 1.5); c.fill_circle(32, 34, 24, hex(0xB0B8C8));
             for (int y = 12; y < 58; y += 6) for (int x = 10; x < 56; x += 6) if (std::hypot(x - 32 + 3, y - 34 + 3) < 22) c.fill_rect(x, y, 5, 5, ((x + y) / 6) % 3 == 0 ? hex(0xFFFFFF) : ((x * y) % 5 == 0 ? hex(0xF0A0F0) : hex(0x8890A8)));
             c.begin(); c.circle(32, 34, 24); ink(c);
         }},
        {"a", "pair of roller skates", [](Canvas& c) {
             for (int s = 0; s < 2; ++s) {
                 const double ox = s * 26;
                 c.begin(); c.move(6 + ox, 14); c.line(18 + ox, 14); c.line(20 + ox, 34); c.line(32 + ox, 38); c.line(32 + ox, 46); c.line(6 + ox, 46); c.close(); c.fill(s ? hex(0xF870B0) : hex(0xF8F8F8));
                 c.fill_circle(10 + ox, 52, 5, hex(0xF8D030)); c.fill_circle(26 + ox, 52, 5, hex(0xF8D030));
                 c.begin(); c.move(6 + ox, 14); c.line(18 + ox, 14); c.line(20 + ox, 34); c.line(32 + ox, 38); c.line(32 + ox, 46); c.line(6 + ox, 46); c.close(); ink(c);
             }
         }},
        {"a", "wind-up robot", [](Canvas& c) {
             c.fill_rect(18, 24, 28, 26, hex(0xB8C0C8)); c.fill_rect(20, 8, 24, 16, hex(0xC8D0D8)); c.fill_circle(26, 15, 3, hex(0xF83030)); c.fill_circle(38, 15, 3, hex(0xF83030));
             c.fill_rect(24, 30, 16, 8, hex(0x3A80C0)); c.fill_rect(20, 50, 8, 10, hex(0x8A9098)); c.fill_rect(36, 50, 8, 10, hex(0x8A9098));
             c.stroke_line(46, 36, 56, 36, hex(0xC8A040), 3); c.fill_rect(54, 30, 3, 12, hex(0xC8A040));
             c.stroke_line(32, 8, 32, 2, K, 1.5); c.fill_circle(32, 2, 2, hex(0xF83030));
             c.begin(); c.rect(18, 24, 28, 26); ink(c); c.begin(); c.rect(20, 8, 24, 16); ink(c);
         }},
        {"a", "teddy bear", [](Canvas& c) {
             c.fill_circle(18, 12, 6, hex(0xA06A3A)); c.fill_circle(46, 12, 6, hex(0xA06A3A)); c.fill_circle(32, 22, 14, hex(0xA06A3A));
             c.fill_ellipse(32, 46, 16, 16, hex(0xA06A3A)); c.fill_ellipse(32, 26, 6, 4, hex(0xE0B880)); c.fill_circle(32, 24, 2, K);
             c.fill_circle(26, 19, 2, K); c.fill_circle(38, 19, 2, K); c.fill_ellipse(32, 48, 8, 9, hex(0xE0B880));
             c.begin(); c.move(26, 34); c.line(38, 34); c.line(32, 38); c.close(); c.fill(hex(0xD03040));
         }},
        {"a", "jewelled crown", [](Canvas& c) {
             c.begin(); c.move(8, 50); c.line(8, 20); c.line(20, 34); c.line(32, 14); c.line(44, 34); c.line(56, 20); c.line(56, 50); c.close(); c.fill(hex(0xF0C030));
             c.fill_circle(32, 40, 4, hex(0xD02040)); c.fill_circle(18, 42, 3, hex(0x2060D0)); c.fill_circle(46, 42, 3, hex(0x20A050));
             for (int x : {8, 32, 56}) c.fill_circle(x, x == 32 ? 14 : 20, 3, hex(0xFFF0A0));
             c.begin(); c.move(8, 50); c.line(8, 20); c.line(20, 34); c.line(32, 14); c.line(44, 34); c.line(56, 20); c.line(56, 50); c.close(); ink(c);
         }},
        {"a", "pet rock", [](Canvas& c) {
             c.fill_ellipse(32, 40, 24, 16, hex(0x8A847A)); c.fill_ellipse(26, 34, 8, 4, hex(0xA8A296));
             c.fill_circle(24, 36, 6, hex(0xFFFFFF)); c.fill_circle(40, 36, 6, hex(0xFFFFFF)); c.fill_circle(26, 38, 3, K); c.fill_circle(38, 34, 3, K);
             c.begin(); c.ellipse(32, 40, 24, 16); ink(c);
         }},
        {"a", "jar of pickles", [](Canvas& c) {
             c.fill_rect(16, 16, 32, 44, hex(0xC8E0B0, .9f)); c.fill_rect(14, 8, 36, 9, hex(0xD8D8E0));
             for (int k = 0; k < 4; ++k) c.fill_ellipse(24 + (k % 2) * 14, 28 + k * 8, 6, 11, hex(0x4A8A2A));
             c.begin(); c.rect(16, 16, 32, 44); ink(c);
         }},
        {"a", "gold record", [](Canvas& c) {
             c.fill_circle(32, 32, 28, hex(0xE8B828)); for (int r = 10; r < 27; r += 4) { c.begin(); c.circle(32, 32, r); c.stroke(hex(0xC89818), 1); }
             c.fill_circle(32, 32, 9, hex(0xD02040)); c.fill_circle(32, 32, 2, K); c.begin(); c.circle(32, 32, 28); ink(c);
         }},
        {"a", "television set", [](Canvas& c) {
             c.fill_rect(6, 16, 52, 40, hex(0x6A4A2A)); c.fill_rect(10, 20, 36, 30, hex(0x2A3A3A)); c.fill_rect(14, 24, 28, 22, hex(0x60A0A0));
             c.fill_circle(52, 26, 3, hex(0xC0C0C0)); c.fill_circle(52, 36, 3, hex(0xC0C0C0)); c.stroke_line(24, 16, 16, 4, K, 1.5); c.stroke_line(36, 16, 46, 4, K, 1.5);
             c.begin(); c.rect(6, 16, 52, 40); ink(c);
         }},
        {"a", "puzzle cube", [](Canvas& c) {
             const Col f[3] = {hex(0xF8F8F8), hex(0xE02020), hex(0x2050E0)};
             for (int k = 0; k < 9; ++k) c.fill_rect(10 + (k % 3) * 13, 22 + (k / 3) * 13, 12, 12, k % 2 ? hex(0x30B050) : hex(0xF8D020));
             for (int k = 0; k < 3; ++k) { c.begin(); c.move(10 + k * 13, 22); c.line(16 + k * 13, 12); c.line(28 + k * 13, 12); c.line(22 + k * 13, 22); c.close(); c.fill(f[k % 3]); c.begin(); c.move(10 + k * 13, 22); c.line(16 + k * 13, 12); c.line(28 + k * 13, 12); c.line(22 + k * 13, 22); c.close(); ink(c, 1); }
             c.begin(); c.rect(10, 22, 39, 39); ink(c);
         }},
        {"a", "yo-yo", [](Canvas& c) {
             c.stroke_line(32, 0, 32, 20, K, 1.2); c.fill_circle(32, 38, 20, hex(0x3070E0)); c.fill_circle(32, 38, 12, hex(0x60A0F8)); c.fill_circle(32, 38, 4, hex(0xF8F8F8));
             c.begin(); c.circle(32, 38, 20); ink(c);
         }},
        {"a", "pair of 3D glasses", [](Canvas& c) {
             c.fill_rect(4, 24, 56, 18, hex(0xF8F8F8)); c.fill_rect(8, 27, 20, 12, hex(0xE02838)); c.fill_rect(36, 27, 20, 12, hex(0x28A8E0));
             c.begin(); c.rect(4, 24, 56, 18); ink(c);
         }},
        {"a", "coiled spring toy", [](Canvas& c) {
             for (int k = 0; k < 12; ++k) { c.begin(); c.ellipse(32, 14 + k * 3.6, 18, 6); c.stroke(k % 2 ? hex(0xC8CCD4) : hex(0x9AA0AA), 2.5); }
         }},
        {"a", "tin lunchbox", [](Canvas& c) {
             c.fill_rect(8, 20, 48, 36, hex(0x3A80D0)); c.begin(); c.move(22, 20); c.quad(32, 6, 42, 20); c.stroke(hex(0x8A9098), 3);
             c.fill_rect(12, 28, 40, 20, hex(0xF8D040)); c.fill_circle(24, 38, 5, hex(0xF06040)); c.fill_circle(40, 38, 5, hex(0x40B060));
             c.begin(); c.rect(8, 20, 48, 36); ink(c);
         }},
        {"a", "bowl of ramen", [](Canvas& c) {
             c.begin(); c.move(6, 30); c.line(58, 30); c.quad(54, 58, 32, 58); c.quad(10, 58, 6, 30); c.fill(hex(0xE8E0D0));
             c.fill_ellipse(32, 30, 26, 6, hex(0xD8A050)); for (int k = 0; k < 5; ++k) { c.begin(); c.move(14 + k * 8, 28); c.quad(18 + k * 8, 24, 22 + k * 8, 30); c.stroke(hex(0xF8E070), 1.5); }
             c.fill_ellipse(42, 28, 6, 4, hex(0xF8F8F0)); c.fill_ellipse(42, 28, 3, 2, hex(0xF8B020));
             c.stroke_line(40, 4, 26, 30, hex(0x8A5A2A), 2); c.stroke_line(48, 6, 30, 30, hex(0x8A5A2A), 2);
         }},
        {"a", "milkshake", [](Canvas& c) {
             c.begin(); c.move(18, 20); c.line(46, 20); c.line(40, 60); c.line(24, 60); c.close(); c.fill(hex(0xF8B0C8));
             c.fill_ellipse(32, 18, 16, 8, hex(0xFFFFFF)); c.fill_circle(32, 10, 4, hex(0xE02030)); c.stroke_line(38, 2, 36, 16, hex(0x40A0F0), 2.5);
             c.begin(); c.move(18, 20); c.line(46, 20); c.line(40, 60); c.line(24, 60); c.close(); ink(c);
         }},
        {"an", "ice cream cone", [](Canvas& c) {
             c.begin(); c.move(18, 30); c.line(46, 30); c.line(32, 62); c.close(); c.fill(hex(0xE0A858));
             c.fill_circle(32, 22, 13, hex(0xF8D8E8)); c.fill_circle(32, 10, 9, hex(0x8A5030)); c.fill_circle(36, 2, 3, hex(0xE02030));
             c.begin(); c.move(18, 30); c.line(46, 30); c.line(32, 62); c.close(); ink(c);
         }},
        {"a", "fortune ball", [](Canvas& c) {
             c.fill_circle(32, 32, 26, hex(0x101014)); c.fill_circle(32, 30, 11, hex(0xF8F8F8)); c.begin(); c.move(26, 28); c.line(38, 28); c.line(32, 36); c.close(); c.fill(hex(0x2040C0));
             c.fill_circle(22, 20, 4, hex(0xFFFFFF, .5f));
         }},
        {"a", "treasure chest", [](Canvas& c) {
             c.fill_rect(8, 30, 48, 26, hex(0x8A5A2A)); c.begin(); c.move(8, 30); c.quad(8, 12, 32, 12); c.quad(56, 12, 56, 30); c.close(); c.fill(hex(0x9A6A3A));
             c.fill_rect(8, 30, 48, 4, hex(0xD8B040)); c.fill_rect(28, 28, 8, 10, hex(0xD8B040)); c.fill_circle(32, 34, 1.5, K);
             for (int k = 0; k < 4; ++k) c.fill_circle(16 + k * 10, 28, 3, hex(0xF8E060));
         }},
        {"a", "bunch of bananas", [](Canvas& c) {
             for (int k = 0; k < 3; ++k) { c.begin(); c.move(14 + k * 6, 14); c.quad(10 + k * 6, 46, 44 + k * 4, 54); c.quad(20 + k * 6, 40, 20 + k * 6, 14); c.close(); c.fill(hex(0xF8D838)); c.begin(); c.move(14 + k * 6, 14); c.quad(10 + k * 6, 46, 44 + k * 4, 54); ink(c, 1.2); }
             c.fill_rect(14, 8, 14, 6, hex(0x6A5A2A));
         }},
        {"a", "can of fizzy pop", [](Canvas& c) {
             c.fill_rect(18, 10, 28, 46, hex(0x40B0E0)); c.fill_ellipse(32, 10, 14, 4, hex(0xC8CCD4)); c.fill_ellipse(32, 56, 14, 4, hex(0x8A9098));
             c.begin(); c.move(18, 34); c.quad(32, 22, 46, 34); c.line(46, 42); c.quad(32, 30, 18, 42); c.close(); c.fill(hex(0xF8F8F8));
             c.begin(); c.rect(18, 10, 28, 46); ink(c);
         }},
        {"a", "pocket game machine", [](Canvas& c) {
             c.fill_rect(16, 4, 32, 56, hex(0xC8C4B8)); c.fill_rect(20, 8, 24, 20, hex(0x6A6A70)); c.fill_rect(23, 11, 18, 14, hex(0x9AB040));
             c.fill_rect(20, 38, 9, 3, hex(0x2A2A2E)); c.fill_rect(23, 35, 3, 9, hex(0x2A2A2E)); c.fill_circle(36, 42, 2.5, hex(0xA02050)); c.fill_circle(42, 38, 2.5, hex(0xA02050));
             c.begin(); c.rect(16, 4, 32, 56); ink(c);
         }},
        {"a", "pager", [](Canvas& c) {
             c.fill_rect(10, 20, 44, 28, hex(0x2A2A30)); c.fill_rect(14, 24, 28, 12, hex(0x90B880)); c.fill_rect(46, 26, 4, 8, hex(0x6A6A70));
             for (int k = 0; k < 6; ++k) c.fill_rect(16 + k * 4, 29, 2, 3, hex(0x2A3A2A));
             c.begin(); c.rect(10, 20, 44, 28); ink(c);
         }},
        {"a", "camcorder", [](Canvas& c) {
             c.fill_rect(8, 22, 40, 24, hex(0x2A2A30)); c.fill_circle(52, 34, 9, hex(0x3A3A40)); c.fill_circle(52, 34, 5, hex(0x6080C0));
             c.fill_rect(14, 14, 16, 8, hex(0x3A3A40)); c.fill_circle(14, 28, 2, hex(0xE02020));
             c.begin(); c.rect(8, 22, 40, 24); ink(c);
         }},
        {"a", "beanbag plush", [](Canvas& c) {
             c.fill_ellipse(32, 42, 20, 14, hex(0x9A60D0)); c.fill_circle(32, 24, 12, hex(0x9A60D0)); c.fill_circle(22, 14, 5, hex(0x9A60D0)); c.fill_circle(42, 14, 5, hex(0x9A60D0));
             c.fill_circle(28, 22, 2, K); c.fill_circle(36, 22, 2, K); c.fill_ellipse(32, 28, 3, 2, hex(0xF0A0C0));
             c.begin(); c.move(26, 46); c.line(38, 46); c.line(32, 52); c.close(); c.fill(hex(0xF04060));
         }},
        {"a", "pot of honey", [](Canvas& c) {
             c.begin(); c.move(14, 22); c.quad(6, 44, 18, 58); c.line(46, 58); c.quad(58, 44, 50, 22); c.close(); c.fill(hex(0xC8803A));
             c.fill_rect(12, 14, 40, 9, hex(0xF0B030)); c.begin(); c.move(20, 23); c.quad(22, 34, 26, 30); c.stroke(hex(0xF0B030), 3);
             c.fill_rect(20, 34, 24, 12, hex(0xF0E8D0)); c.stroke_line(23, 38, 41, 38, hex(0x8A5A2A), 1.2); c.stroke_line(23, 42, 37, 42, hex(0x8A5A2A), 1.2);
         }},
        {"a", "tin of spinach", [](Canvas& c) {
             c.fill_rect(16, 14, 32, 44, hex(0xC0C4CC)); c.fill_rect(16, 22, 32, 26, hex(0x2A8A3A)); c.fill_ellipse(32, 14, 16, 4, hex(0xD8DCE4));
             for (int k = 0; k < 4; ++k) c.fill_ellipse(22 + k * 7, 34, 3, 6, hex(0x6AC85A));
             c.begin(); c.rect(16, 14, 32, 44); ink(c);
         }},
        {"a", "mug of hot cocoa", [](Canvas& c) {
             c.fill_rect(14, 22, 30, 34, hex(0xF0E8D8)); c.begin(); c.move(44, 28); c.quad(56, 30, 54, 42); c.quad(52, 50, 44, 48); c.stroke(hex(0xF0E8D8), 5);
             c.fill_ellipse(29, 22, 15, 4, hex(0x6A3A1A)); for (int k = 0; k < 3; ++k) c.fill_rect(22 + k * 6, 18, 5, 5, hex(0xFFFFFF));
             c.begin(); c.move(24, 14); c.quad(28, 8, 24, 2); c.stroke(hex(0xC8C8C8, .7f), 1.5); c.begin(); c.move(32, 14); c.quad(36, 8, 32, 2); c.stroke(hex(0xC8C8C8, .7f), 1.5);
             c.begin(); c.rect(14, 22, 30, 34); ink(c);
         }},
    };
    return v;
}
}  // namespace

int reward_count() { return static_cast<int>(table().size()); }
const std::string& reward_name(int i) { return table()[static_cast<size_t>(((i % reward_count()) + reward_count()) % reward_count())].name; }
const std::string& reward_article(int i) { return table()[static_cast<size_t>(((i % reward_count()) + reward_count()) % reward_count())].article; }
std::string reward_a(int i) { return reward_article(i) + " " + reward_name(i); }
std::string reward_the(int i) { return "the " + reward_name(i); }

const Tex32& reward_tex(int i) {
    static std::vector<std::unique_ptr<Tex32>> cache(static_cast<size_t>(reward_count()));
    const size_t k = static_cast<size_t>(((i % reward_count()) + reward_count()) % reward_count());
    if (!cache[k]) {
        Canvas c;
        c.resize(64, 64);
        c.clear({0, 0, 0, 0});
        table()[k].draw(c);
        cache[k] = std::make_unique<Tex32>(to_tex(c));
    }
    return *cache[k];
}

}  // namespace mz
