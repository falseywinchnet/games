#include "suite.hpp"
#include "presentation.hpp"
#include <algorithm>
#include <cmath>
namespace games {
namespace {
constexpr gf::Color rgb(int r, int g, int b, int a = 255) {
    return gf::Color::rgba(static_cast<unsigned char>(r), static_cast<unsigned char>(g),
                           static_cast<unsigned char>(b), static_cast<unsigned char>(a));
}
const std::array<EntryInfo, entry_count> entries{{
    {"Solitaire", "Klondike patience", "Build four suits from ace to king. Draw one card or three.",
     rgb(36, 125, 84), rgb(14, 61, 40), rgb(242, 201, 104)},
    {"Spider", "Patience", "Ten columns, one to four suits. Complete eight same-suit runs.",
     rgb(52, 70, 150), rgb(20, 28, 72), rgb(176, 196, 255)},
    {"FreeCell", "Patience",
     "Four free cells and every card face up. Nearly every deal can be won.", rgb(26, 124, 132),
     rgb(10, 58, 64), rgb(163, 234, 226)},
    {"Hearts", "Card table", "Three computer opponents. Pass, dodge the points, or shoot the moon.",
     rgb(178, 38, 62), rgb(88, 14, 30), rgb(255, 196, 172)},
    {"Sudoku", "Number logic", "Fresh puzzles at three difficulties, with notes and a night mode.",
     rgb(40, 92, 176), rgb(18, 44, 96), rgb(214, 232, 255)},
    {"Gems", "Match three", "Swap jewels into rows. Set off bombs, stars and hypercubes.",
     rgb(118, 46, 170), rgb(52, 18, 86), rgb(255, 190, 250)},
    {"Nature Cube", "Path puzzle",
     "Join each colored pair across three faces of a tilting glass cube.", rgb(50, 134, 108),
     rgb(20, 64, 56), rgb(206, 244, 214)},
    {"Untangle", "Graph puzzle", "Drag the points until no two lines cross.", rgb(28, 132, 156),
     rgb(12, 62, 76), rgb(180, 240, 250)},
    {"Atom Probe", "Deduction", "Fire probes into the sealed box and find the hidden atoms.",
     rgb(40, 56, 92), rgb(12, 18, 38), rgb(110, 232, 238)},
    {"Four Pegs", "Codebreaking", "Crack the Curator's four-peg code in ten tries.",
     rgb(116, 32, 70), rgb(46, 10, 28), rgb(255, 210, 122)},
    {"Switchbox", "Switch puzzle", "Find the order that lights every switch.", rgb(170, 60, 122),
     rgb(80, 18, 56), rgb(255, 228, 120)},
    {"Puzzle Solve", "Reconstruction", "Turn and flip seven pieces to rebuild the target picture.",
     rgb(44, 110, 182), rgb(18, 50, 98), rgb(250, 205, 75)},
    {"Eggy", "A long climb", "Help a duckling climb a very, very tall mountain.", rgb(222, 140, 36),
     rgb(138, 70, 16), rgb(255, 238, 170)},
    {"Koi-Koi", "Hanafuda", "Match the months, collect sets, and choose when to risk another turn.",
     rgb(148, 46, 38), rgb(62, 24, 24), rgb(255, 218, 138)},
    {"The Parrot's Table", "Deduction",
     "Question the parrots, test their stories, and name the culprit.", rgb(34, 122, 85),
     rgb(12, 52, 40), rgb(244, 216, 104)},
    {"Liar's Dice", "Bluffing", "Read the crew, raise the bid, or call their bluff.",
     rgb(38, 80, 112), rgb(12, 30, 50), rgb(230, 186, 100)},
    {"Pen the Sheep", "Pasture puzzle", "Place fences before a clever sheep finds its way out.",
     rgb(95, 146, 64), rgb(34, 66, 28), rgb(255, 238, 176)},
}};
void disc(gf::Painter& p, double cx, double cy, double r, gf::Color c) {
    p.fill_rounded_rect({cx - r, cy - r, 2 * r, 2 * r}, r, c);
}
// Suits are drawn as shapes so they never depend on font coverage.
void paint_suit(gf::Painter& p, double cx, double cy, double s, int suit, gf::Color c) {
    if (suit == 0) { // spade
        disc(p, cx - s * .24, cy + s * .02, s * .27, c);
        disc(p, cx + s * .24, cy + s * .02, s * .27, c);
        paint_polygon(
            p, {{cx - s * .5, cy + .02 * s}, {cx, cy - s * .55}, {cx + s * .5, cy + .02 * s}}, c);
        paint_polygon(p, {{cx, cy}, {cx - s * .2, cy + s * .5}, {cx + s * .2, cy + s * .5}}, c);
    } else if (suit == 1) { // heart
        disc(p, cx - s * .24, cy - s * .16, s * .27, c);
        disc(p, cx + s * .24, cy - s * .16, s * .27, c);
        paint_polygon(
            p, {{cx - s * .5, cy - s * .08}, {cx + s * .5, cy - s * .08}, {cx, cy + s * .52}}, c);
    } else if (suit == 2) { // diamond
        paint_polygon(
            p, {{cx, cy - s * .55}, {cx + s * .4, cy}, {cx, cy + s * .55}, {cx - s * .4, cy}}, c);
    } else { // club
        disc(p, cx, cy - s * .26, s * .24, c);
        disc(p, cx - s * .26, cy + s * .08, s * .24, c);
        disc(p, cx + s * .26, cy + s * .08, s * .24, c);
        paint_polygon(p, {{cx, cy}, {cx - s * .18, cy + s * .52}, {cx + s * .18, cy + s * .52}}, c);
    }
}
void paint_card(gf::Painter& p, gf::Rect r, int suit, const char* rank) {
    p.draw_box_shadow(r, r.width * .09, {1, 2}, 3, 0, rgb(0, 0, 0, 90));
    p.fill_rounded_rect(r, r.width * .09, rgb(252, 248, 236));
    p.stroke_rounded_rect(r, r.width * .09, rgb(120, 110, 92), 1);
    gf::Color ink = suit == 1 || suit == 2 ? rgb(184, 24, 46) : rgb(24, 26, 32);
    paint_suit(p, r.x + r.width * .5, r.y + r.height * .58, r.width * .46, suit, ink);
    p.draw_text_utf8({r.x + r.width * .1, r.y + r.height * .28}, rank,
                     {gf::FontRole::content, r.width * .3, 700, false}, ink);
}
void paint_card_back(gf::Painter& p, gf::Rect r, gf::Color c) {
    p.draw_box_shadow(r, r.width * .09, {1, 2}, 3, 0, rgb(0, 0, 0, 90));
    p.fill_rounded_rect(r, r.width * .09, rgb(250, 246, 234));
    gf::Rect inner{r.x + r.width * .1, r.y + r.width * .1, r.width * .8, r.height - r.width * .2};
    p.fill_rounded_rect(inner, r.width * .06, c);
    for (double d = -inner.height; d < inner.width; d += r.width * .18) {
        double x0 = inner.x + std::max(0.0, d), y0 = inner.y + std::max(0.0, -d);
        double len = std::min(inner.width - std::max(0.0, d), inner.height - std::max(0.0, -d));
        p.draw_line({x0, y0}, {x0 + len, y0 + len}, rgb(255, 255, 255, 55), 1);
    }
}
} // namespace
const EntryInfo& entry_info(Entry entry) {
    return entries[static_cast<std::size_t>(
        std::clamp(static_cast<int>(entry), 0, entry_count - 1))];
}
const char* suite_name() {
    return "PlaySuite";
}
gf::Color mix_color(gf::Color a, gf::Color b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    struct MixChannel {
        double t;
        unsigned char operator()(unsigned char x, unsigned char y) const {
            return static_cast<unsigned char>(std::lround(x * (1 - t) + y * t));
        }
    };
    MixChannel m{t};
    return gf::Color::rgba(m(a.red, b.red), m(a.green, b.green), m(a.blue, b.blue),
                           m(a.alpha, b.alpha));
}
gf::Color with_alpha(gf::Color c, int alpha) {
    c.alpha = static_cast<unsigned char>(std::clamp(alpha, 0, 255));
    return c;
}
void fill_vertical(gf::Painter& p, gf::Rect r, gf::Color top, gf::Color bottom) {
    const gf::GradientStop stops[] = {{0, top}, {1, bottom}};
    p.fill_linear_gradient(r, {r.x, r.y}, {r.x, r.y + r.height}, stops);
}
gf::Color gloss_ink(GlossTone tone, bool enabled) {
    gf::Color ink = tone == GlossTone::gold    ? rgb(59, 42, 7)
                    : tone == GlossTone::navy  ? rgb(255, 255, 255)
                    : tone == GlossTone::smoke ? rgb(242, 244, 248)
                                               : rgb(23, 43, 83);
    return enabled ? ink : with_alpha(ink, 110);
}
void paint_gloss(gf::Painter& p, gf::Rect r, double radius, GlossTone tone, GlossState s) {
    std::array<gf::Color, 4> c{};
    gf::Color border{};
    switch (tone) {
    case GlossTone::chrome:
        c = {rgb(255, 255, 255), rgb(219, 227, 239), rgb(189, 201, 220), rgb(230, 235, 243)};
        border = rgb(100, 114, 139);
        if (s.hot)
            c = {rgb(255, 255, 255), rgb(238, 243, 255), rgb(215, 227, 255), rgb(255, 255, 255)};
        break;
    case GlossTone::gold:
        c = {rgb(255, 244, 214), rgb(255, 226, 160), rgb(242, 199, 102), rgb(255, 233, 180)};
        border = rgb(138, 106, 34);
        if (s.hot)
            c = {rgb(255, 250, 230), rgb(255, 234, 178), rgb(248, 210, 120), rgb(255, 240, 200)};
        break;
    case GlossTone::navy:
        c = {rgb(90, 127, 214), rgb(42, 78, 168), rgb(23, 52, 127), rgb(58, 95, 189)};
        border = rgb(10, 24, 64);
        if (s.hot)
            c = {rgb(112, 148, 232), rgb(58, 96, 190), rgb(34, 66, 150), rgb(76, 114, 208)};
        break;
    case GlossTone::smoke:
        c = {rgb(88, 100, 128, 235), rgb(58, 68, 92, 235), rgb(40, 48, 68, 235),
             rgb(52, 62, 84, 235)};
        border = rgb(255, 255, 255, 80);
        if (s.hot)
            c = {rgb(112, 126, 158, 245), rgb(76, 88, 116, 245), rgb(54, 64, 88, 245),
                 rgb(70, 82, 108, 245)};
        break;
    }
    if (s.down)
        c = {c[2], c[2], c[1], c[0]};
    if (!s.enabled)
        for (gf::Color& x : c)
            x = mix_color(x, rgb(150, 156, 168, x.alpha), .45);
    if (!s.down && s.enabled)
        p.draw_box_shadow(r, radius, {0, 2}, 2, 0, rgb(10, 18, 40, 70));
    p.save();
    p.clip_rounded_rect(r, radius);
    const gf::GradientStop stops[] = {{0, c[0]}, {.48, c[1]}, {.52, c[2]}, {1, c[3]}};
    p.fill_linear_gradient(r, {r.x, r.y}, {r.x, r.y + r.height}, stops);
    p.draw_line({r.x + radius * .6, r.y + 1.5}, {r.x + r.width - radius * .6, r.y + 1.5},
                rgb(255, 255, 255,
                    tone == GlossTone::smoke ? 50
                    : s.down                 ? 40
                                             : 190),
                1);
    p.restore();
    p.stroke_rounded_rect(r, radius, border, 1);
    if (s.hot || s.focus)
        p.stroke_rounded_rect({r.x - 2, r.y - 2, r.width + 4, r.height + 4}, radius + 2,
                              rgb(255, 210, 122), 2);
}
void paint_glyph(gf::Painter& p, gf::Rect r, Glyph glyph, gf::Color ink, bool crossed) {
    const double cx = r.x + r.width * .5, cy = r.y + r.height * .5, s = std::min(r.width, r.height);
    switch (glyph) {
    case Glyph::none:
        return;
    case Glyph::back:
        p.draw_line({cx + s * .12, cy - s * .28}, {cx - s * .16, cy}, ink, 2.4);
        p.draw_line({cx - s * .16, cy}, {cx + s * .12, cy + s * .28}, ink, 2.4);
        break;
    case Glyph::play:
        paint_polygon(
            p, {{cx - s * .18, cy - s * .26}, {cx + s * .26, cy}, {cx - s * .18, cy + s * .26}},
            ink);
        break;
    case Glyph::music:
        disc(p, cx - s * .14, cy + s * .2, s * .12, ink);
        disc(p, cx + s * .2, cy + s * .12, s * .12, ink);
        p.draw_line({cx - s * .03, cy + s * .2}, {cx - s * .03, cy - s * .3}, ink, 1.8);
        p.draw_line({cx + s * .31, cy + s * .12}, {cx + s * .31, cy - s * .36}, ink, 1.8);
        p.draw_line({cx - s * .03, cy - s * .3}, {cx + s * .31, cy - s * .36}, ink, 3);
        break;
    case Glyph::sound:
        p.fill_rect({cx - s * .32, cy - s * .1, s * .14, s * .2}, ink);
        paint_polygon(p,
                      {{cx - s * .2, cy - s * .1},
                       {cx + s * .02, cy - s * .3},
                       {cx + s * .02, cy + s * .3},
                       {cx - s * .2, cy + s * .1}},
                      ink);
        if (!crossed) {
            p.draw_line({cx + s * .14, cy - s * .12}, {cx + s * .2, cy}, ink, 1.6);
            p.draw_line({cx + s * .2, cy}, {cx + s * .14, cy + s * .12}, ink, 1.6);
            p.draw_line({cx + s * .24, cy - s * .24}, {cx + s * .34, cy}, ink, 1.6);
            p.draw_line({cx + s * .34, cy}, {cx + s * .24, cy + s * .24}, ink, 1.6);
        }
        break;
    case Glyph::motion:
        // A ball with speed lines trailing behind it.
        disc(p, cx + s * .18, cy, s * .17, ink);
        p.draw_line({cx - s * .36, cy - s * .16}, {cx - s * .04, cy - s * .16}, ink, 1.8);
        p.draw_line({cx - s * .26, cy}, {cx - s * .02, cy}, ink, 1.8);
        p.draw_line({cx - s * .36, cy + s * .16}, {cx - s * .04, cy + s * .16}, ink, 1.8);
        break;
    case Glyph::more:
        for (int i = -1; i <= 1; ++i)
            disc(p, cx + i * s * .22, cy, s * .07, ink);
        break;
    }
    if (crossed)
        p.draw_line({cx - s * .34, cy + s * .34}, {cx + s * .34, cy - s * .34}, rgb(200, 40, 50),
                    2.4);
}
void paint_entry_emblem(gf::Painter& p, gf::Rect r, Entry entry) {
    const double x = r.x, y = r.y, w = r.width, h = r.height;
    const double cx = x + w * .5, cy = y + h * .5, s = std::min(w, h);
    switch (entry) {
    case Entry::solitaire:
        paint_card_back(p, {x + w * .08, y + h * .14, w * .5, h * .7}, rgb(150, 34, 54));
        paint_card(p, {x + w * .4, y + h * .08, w * .5, h * .7}, 0, "A");
        break;
    case Entry::spider:
        for (int i = 0; i < 3; ++i)
            paint_card(p, {x + w * (.08 + i * .16), y + h * (.06 + i * .1), w * .5, h * .66}, 0,
                       i == 2   ? "K"
                       : i == 1 ? "Q"
                                : "J");
        {
            double cx = x + w * .8, cy = y + h * .82, s = w * .13;
            for (int i = 0; i < 4; ++i) {
                double a = -.9 + i * .6;
                p.draw_line({cx, cy}, {cx - s * 1.6, cy + std::sin(a) * s * 1.4}, rgb(20, 20, 26),
                            1.4);
                p.draw_line({cx, cy}, {cx + s * 1.6, cy + std::sin(a) * s * 1.4}, rgb(20, 20, 26),
                            1.4);
            }
            disc(p, cx, cy, s * .55, rgb(20, 20, 26));
            disc(p, cx, cy - s * .6, s * .35, rgb(20, 20, 26));
        }
        break;
    case Entry::freecell:
        for (int i = 0; i < 4; ++i)
            p.stroke_rounded_rect({x + w * (.06 + i * .23), y + h * .06, w * .19, h * .26}, 2,
                                  rgb(255, 255, 255, 170), 1.2);
        paint_card(p, {x + w * .52, y + h * .06, w * .19, h * .26}, 1, "");
        paint_card(p, {x + w * .26, y + h * .38, w * .46, h * .58}, 3, "7");
        break;
    case Entry::hearts: {
        double cx = x + w * .5, cy = y + h * .52;
        paint_suit(p, cx + w * .02, cy + h * .04, w * .82, 1, rgb(90, 8, 22, 120));
        paint_suit(p, cx, cy, w * .82, 1, rgb(214, 30, 58));
        disc(p, cx - w * .2, cy - h * .2, w * .08, rgb(255, 255, 255, 120));
        break;
    }
    case Entry::sudoku: {
        gf::Rect g{x + w * .1, y + h * .1, w * .8, h * .8};
        p.draw_box_shadow(g, 3, {1, 2}, 3, 0, rgb(0, 0, 0, 80));
        p.fill_rounded_rect(g, 3, rgb(245, 249, 255));
        for (int i = 1; i < 9; ++i) {
            double t = i / 9.0;
            double wide = i % 3 == 0 ? 1.6 : .7;
            gf::Color line = i % 3 == 0 ? rgb(40, 80, 150) : rgb(160, 186, 220);
            p.draw_line({g.x + g.width * t, g.y}, {g.x + g.width * t, g.y + g.height}, line, wide);
            p.draw_line({g.x, g.y + g.height * t}, {g.x + g.width, g.y + g.height * t}, line, wide);
        }
        const char* digits[] = {"5", "3", "7", "9", "1"};
        const int cells[] = {0, 10, 40, 60, 80};
        for (int i = 0; i < 5; ++i) {
            double cxl = g.x + g.width * ((cells[i] % 9) + .22) / 9,
                   cyl = g.y + g.height * ((cells[i] / 9) + .88) / 9;
            p.draw_text_utf8({cxl, cyl}, digits[i],
                             {gf::FontRole::content, g.height / 9 * .9, 700, false},
                             rgb(24, 60, 130));
        }
        p.stroke_rounded_rect(g, 3, rgb(30, 64, 124), 1.4);
        break;
    }
    case Entry::gems: {
        struct PaintBoxGem {
            gf::Painter& p;
            void operator()(double cx, double cy, double s, gf::Color dark, gf::Color mid,
                            gf::Color light) const {
                paint_polygon(p,
                              {{cx - s, cy - s * .3},
                               {cx - s * .55, cy - s * .85},
                               {cx + s * .55, cy - s * .85},
                               {cx + s, cy - s * .3},
                               {cx, cy + s}},
                              dark);
                paint_polygon(p, {{cx - s, cy - s * .3}, {cx + s, cy - s * .3}, {cx, cy + s}}, mid);
                paint_polygon(
                    p, {{cx - s * .45, cy - s * .3}, {cx + s * .45, cy - s * .3}, {cx, cy + s}},
                    light);
                paint_polygon(p,
                              {{cx - s * .55, cy - s * .85},
                               {cx + s * .55, cy - s * .85},
                               {cx + s * .3, cy - s * .3},
                               {cx - s * .3, cy - s * .3}},
                              mix_color(light, rgb(255, 255, 255), .4));
            }
        };
        PaintBoxGem gem{p};
        gem(x + w * .3, y + h * .66, w * .2, rgb(14, 90, 60), rgb(40, 180, 110),
            rgb(150, 240, 190));
        gem(x + w * .72, y + h * .68, w * .18, rgb(130, 70, 0), rgb(240, 170, 20),
            rgb(255, 230, 140));
        gem(x + w * .5, y + h * .38, w * .3, rgb(80, 20, 120), rgb(160, 70, 220),
            rgb(232, 196, 255));
        disc(p, x + w * .44, y + h * .2, w * .03, rgb(255, 255, 255));
        p.draw_line({x + w * .44, y + h * .13}, {x + w * .44, y + h * .27}, rgb(255, 255, 255, 200),
                    1);
        p.draw_line({x + w * .37, y + h * .2}, {x + w * .51, y + h * .2}, rgb(255, 255, 255, 200),
                    1);
        break;
    }
    case Entry::cube: {
        double cx = x + w * .5, cy = y + h * .5, s = w * .4;
        gf::Point top{cx, cy - s}, left{cx - s * .87, cy - s * .5},
            right{cx + s * .87, cy - s * .5}, mid{cx, cy}, bl{cx - s * .87, cy + s * .5},
            br{cx + s * .87, cy + s * .5}, bottom{cx, cy + s};
        paint_polygon(p, {top, right, mid, left}, rgb(214, 240, 228));
        paint_polygon(p, {left, mid, bottom, bl}, rgb(96, 168, 140));
        paint_polygon(p, {mid, right, br, bottom}, rgb(150, 206, 184));
        p.draw_line({left.x + s * .3, left.y + s * .3}, {mid.x - s * .1, mid.y + s * .44},
                    rgb(232, 70, 80), 3);
        p.draw_line({mid.x + s * .2, mid.y + s * .5}, {right.x - s * .2, right.y + s * .6},
                    rgb(250, 200, 60), 3);
        p.draw_line({top.x - s * .3, top.y + s * .4}, {top.x + s * .3, top.y + s * .5},
                    rgb(70, 140, 230), 3);
        p.draw_line(top, mid, rgb(255, 255, 255, 140), 1);
        break;
    }
    case Entry::untangle: {
        gf::Point pts[] = {{x + w * .18, y + h * .22},
                           {x + w * .82, y + h * .2},
                           {x + w * .5, y + h * .5},
                           {x + w * .2, y + h * .8},
                           {x + w * .8, y + h * .78}};
        const int links[][2] = {{0, 1}, {0, 2}, {1, 2}, {2, 3}, {2, 4}, {3, 4}, {0, 3}, {1, 4}};
        for (const int (&l)[2] : links)
            p.draw_line(pts[l[0]], pts[l[1]], rgb(120, 230, 240), 2);
        for (gf::Point q : pts) {
            disc(p, q.x, q.y, w * .085, rgb(10, 50, 70));
            disc(p, q.x, q.y, w * .065, rgb(255, 255, 255));
        }
        break;
    }
    case Entry::atom: {
        for (int direction = 0; direction < 3; ++direction) {
            gf::Point previous{};
            for (int k = 0; k <= 48; ++k) {
                double a = k * 6.2831853 / 48, u = std::cos(a) * w * .42, v = std::sin(a) * h * .15,
                       turn = direction * 1.0471976;
                gf::Point q{x + w * .5 + u * std::cos(turn) - v * std::sin(turn),
                            y + h * .5 + u * std::sin(turn) + v * std::cos(turn)};
                if (k)
                    p.draw_line(previous, q, rgb(110, 232, 238), 1.6);
                previous = q;
            }
        }
        disc(p, x + w * .5, y + h * .5, w * .1, rgb(255, 120, 120));
        disc(p, x + w * .47, y + h * .47, w * .035, rgb(255, 230, 230));
        break;
    }
    case Entry::pegs: {
        const gf::Color colors[] = {rgb(240, 50, 74), rgb(255, 201, 58), rgb(70, 212, 106),
                                    rgb(46, 168, 255)};
        for (int i = 0; i < 4; ++i) {
            double cx = x + w * (.29 + (i % 2) * .42), cy = y + h * (.29 + (i / 2) * .42);
            disc(p, cx + 1, cy + 2, w * .17, rgb(0, 0, 0, 90));
            disc(p, cx, cy, w * .17, colors[i]);
            disc(p, cx - w * .05, cy - w * .05, w * .05, rgb(255, 255, 255, 150));
        }
        break;
    }
    case Entry::switchbox: {
        gf::Rect panel{x + w * .06, y + h * .28, w * .88, h * .46};
        p.fill_rounded_rect(panel, 5, rgb(236, 153, 187));
        p.stroke_rounded_rect(panel, 5, rgb(255, 220, 236), 1.2);
        for (int i = 0; i < 5; ++i) {
            gf::Rect slot{x + w * (.13 + i * .155), y + h * .36, w * .1, h * .3};
            p.fill_rounded_rect(slot, 2, rgb(31, 54, 58));
            bool on = i == 0 || i == 2 || i == 3;
            p.fill_rounded_rect({slot.x + 1.5, slot.y + (on ? 1.5 : slot.height * .5),
                                 slot.width - 3, slot.height * .5 - 1.5},
                                1.5, on ? rgb(255, 228, 110) : rgb(120, 130, 136));
        }
        break;
    }
    case Entry::solve: {
        gf::Color blue = rgb(70, 150, 225), gold = rgb(250, 205, 75);
        paint_polygon(
            p, {{x + w * .08, y + h * .08}, {x + w * .92, y + h * .08}, {x + w * .5, y + h * .5}},
            gold);
        paint_polygon(
            p, {{x + w * .08, y + h * .08}, {x + w * .5, y + h * .5}, {x + w * .08, y + h * .92}},
            blue);
        paint_polygon(
            p, {{x + w * .54, y + h * .54}, {x + w * .92, y + h * .16}, {x + w * .92, y + h * .92}},
            gold);
        paint_polygon(
            p, {{x + w * .12, y + h * .92}, {x + w * .5, y + h * .54}, {x + w * .88, y + h * .92}},
            blue);
        break;
    }
    case Entry::koikoi: {
        const gf::Rect card{cx - s * .32, cy - s * .47, s * .64, s * .94};
        p.fill_rounded_rect(card, s * .035, rgb(255, 243, 212));
        disc(p, cx + s * .10, cy - s * .20, s * .17, rgb(197, 47, 36));
        for (int i = 0; i < 3; ++i) {
            const double x = cx - s * .19 + i * s * .16;
            p.draw_line({x, cy + s * .37}, {x + s * .09, cy - s * .03}, rgb(36, 69, 46), s * .035);
            disc(p, x + s * .08, cy - s * .02, s * .11, rgb(48, 100, 61));
        }
        break;
    }
    case Entry::parrots:
        disc(p, cx, cy, s * .30, rgb(65, 161, 100));
        disc(p, cx + s * .08, cy - s * .18, s * .24, rgb(224, 68, 46));
        paint_polygon(p,
                      {{cx + s * .22, cy - s * .15},
                       {cx + s * .44, cy - s * .03},
                       {cx + s * .17, cy + s * .05}},
                      rgb(246, 201, 77));
        disc(p, cx + s * .12, cy - s * .22, s * .04, rgb(22, 24, 26));
        p.draw_line({cx - s * .12, cy + s * .24}, {cx - s * .27, cy + s * .48}, rgb(55, 130, 215),
                    s * .12);
        break;
    case Entry::liarsdice:
        p.fill_rounded_rect({cx - s * .34, cy - s * .34, s * .68, s * .68}, s * .08,
                            rgb(248, 232, 195));
        for (int i = -1; i <= 1; ++i)
            disc(p, cx + i * s * .19, cy + i * s * .19, s * .055, rgb(36, 31, 31));
        disc(p, cx - s * .19, cy + s * .19, s * .055, rgb(36, 31, 31));
        disc(p, cx + s * .19, cy - s * .19, s * .055, rgb(36, 31, 31));
        break;
    case Entry::penthesheep:
        for (int i = 0; i < 6; ++i) {
            const double a = i * 6.283185307179586 / 6;
            disc(p, cx + std::cos(a) * s * .20, cy + std::sin(a) * s * .15, s * .18,
                 rgb(255, 247, 225));
        }
        disc(p, cx + s * .26, cy + s * .02, s * .15, rgb(66, 60, 54));
        disc(p, cx + s * .30, cy - s * .01, s * .025, rgb(255, 250, 235));
        p.draw_line({cx - s * .16, cy + s * .22}, {cx - s * .16, cy + s * .40}, rgb(66, 60, 54),
                    s * .07);
        p.draw_line({cx + s * .12, cy + s * .22}, {cx + s * .12, cy + s * .40}, rgb(66, 60, 54),
                    s * .07);
        break;
    case Entry::eggy:
        disc(p, x + w * .44, y + h * .62, w * .3, rgb(237, 199, 91));
        disc(p, x + w * .62, y + h * .38, w * .2, rgb(251, 221, 125));
        paint_polygon(
            p, {{x + w * .78, y + h * .36}, {x + w * .98, y + h * .42}, {x + w * .78, y + h * .48}},
            rgb(214, 128, 52));
        disc(p, x + w * .67, y + h * .34, w * .035, rgb(38, 53, 57));
        p.fill_rounded_rect({x + w * .46, y + h * .14, w * .34, h * .09}, h * .04,
                            rgb(132, 104, 64));
        p.draw_line({x + w * .36, y + h * .88}, {x + w * .32, y + h * .98}, rgb(189, 117, 53), 3);
        p.draw_line({x + w * .54, y + h * .88}, {x + w * .6, y + h * .98}, rgb(189, 117, 53), 3);
        break;
    }
}
SuiteButton::SuiteButton(gf::StableId id, std::string text, GlossTone tone)
    : Button(std::move(id), std::move(text)), tone_(tone) {
    set_use_mnemonic(false);
    set_font({gf::FontRole::content, 13, 700, false, .2});
}
void SuiteButton::set_tone(GlossTone tone) {
    if (tone_ == tone)
        return;
    tone_ = tone;
    invalidate(gf::Dirty::paint);
}
void SuiteButton::set_glyph(Glyph glyph, bool crossed) {
    if (glyph_ == glyph && crossed_ == crossed)
        return;
    glyph_ = glyph;
    crossed_ = crossed;
    invalidate(gf::Dirty::paint);
}
void SuiteButton::set_radius(double radius) {
    if (radius_ == radius)
        return;
    radius_ = radius;
    invalidate(gf::Dirty::paint);
}
void SuiteButton::set_checked(bool checked) {
    // Hosts refresh command states often; unchanged states must not cause repaints.
    if (checked_ == checked)
        return;
    checked_ = checked;
    invalidate(gf::Dirty::paint);
}
double SuiteButton::preferred_width() const {
    const double chars = static_cast<double>(text().size());
    const double label = chars > 0 ? chars * 7.3 + 24 : 0;
    const double glyph = glyph_ != Glyph::none ? (label > 0 ? 22 : 34) : 0;
    return std::max(34.0, label + glyph);
}
void SuiteButton::on_paint(gf::Painter& p, gf::Rect) {
    gf::Rect b = client_rectangle();
    gf::Rect r{2, 2, b.width - 4, b.height - 5};
    GlossTone tone = checked_ ? GlossTone::gold : tone_;
    paint_gloss(p, r, radius_, tone,
                {hovered_visual(), pressed_visual(), enabled(), focus_cue_visible() || selected()});
    gf::Color ink = gloss_ink(tone, enabled());
    double x = r.x + 10;
    const double shift = pressed_visual() ? 1 : 0;
    if (glyph_ != Glyph::none) {
        double s = std::min(20.0, r.height - 8);
        double gx = text().empty() ? r.x + (r.width - s) * .5 : x;
        paint_glyph(p, {gx + shift, r.y + (r.height - s) * .5 + shift, s, s}, glyph_, ink,
                    crossed_);
        x = gx + s + 4;
    }
    if (!text().empty()) {
        gf::FontSpec f = font();
        gf::Size m = p.measure_text_utf8(text(), f);
        double tx = glyph_ == Glyph::none ? r.x + (r.width - m.width) * .5 : x;
        p.draw_text_utf8({tx + shift, r.y + (r.height + f.size * .72) * .5 + shift}, text(), f,
                         ink);
    }
}
} // namespace games
