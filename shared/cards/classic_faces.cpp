#include "classic_faces.hpp"

#include "card_backs.hpp"
#include "presentation.hpp"
#include "gui_forms/paint_framebuffer.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace games {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double W = card_art_width, H = card_art_height;

constexpr gf::Color rgb(int r, int g, int b, int a = 255) {
    return gf::Color::rgba(static_cast<unsigned char>(r), static_cast<unsigned char>(g), static_cast<unsigned char>(b),
                           static_cast<unsigned char>(a));
}
const gf::Color red = rgb(204, 30, 40), black = rgb(26, 26, 32), outline = rgb(30, 26, 30);
const gf::Color gold = rgb(232, 182, 50), blue = rgb(38, 74, 162), skin = rgb(248, 216, 182);
const gf::Color white = rgb(255, 255, 255), cream = rgb(246, 242, 232);

bool is_red(int suit) {
    return suit == 1 || suit == 2;
}

void disc(gf::Painter& p, double x, double y, double r, gf::Color c) {
    p.fill_rounded_rect({x - r, y - r, 2 * r, 2 * r}, r, c);
}

void ring(gf::Painter& p, double x, double y, double r, gf::Color c, double width) {
    p.stroke_rounded_rect({x - r, y - r, 2 * r, 2 * r}, r, c, width);
}

// The stem under a spade or a club: narrow at the top, flaring to a foot.
void stem(gf::Painter& p, double cx, double top, double size, gf::Color c) {
    std::vector<gf::Point> pts;
    const double foot = top + size * .38;
    for (int k = 0; k <= 8; ++k) {
        const double t = k / 8.0;
        pts.push_back({cx - size * (.035 + .2 * t * t), top + (foot - top) * t});
    }
    for (int k = 8; k >= 0; --k) {
        const double t = k / 8.0;
        pts.push_back({cx + size * (.035 + .2 * t * t), top + (foot - top) * t});
    }
    paint_polygon(p, pts, c);
}

// A suit sign `size` across, centred at (cx, cy), upright.
void pip(gf::Painter& p, int suit, double cx, double cy, double size) {
    const gf::Color c = is_red(suit) ? red : black;
    if (suit == 1) {
        // Diamond: four points, the sides drawn in a little.
        std::vector<gf::Point> pts;
        const gf::Point corner[4] = {{cx, cy - size * .52}, {cx + size * .4, cy}, {cx, cy + size * .52}, {cx - size * .4, cy}};
        for (int e = 0; e < 4; ++e) {
            const gf::Point a = corner[e], b = corner[(e + 1) % 4];
            for (int k = 0; k < 10; ++k) {
                const double t = k / 10.0;
                const double bow = std::sin(t * pi) * size * .035;
                const double mx = a.x + (b.x - a.x) * t, my = a.y + (b.y - a.y) * t;
                // toward the centre
                const double dx = cx - mx, dy = cy - my, d = std::max(1e-6, std::hypot(dx, dy));
                pts.push_back({mx + dx / d * bow, my + dy / d * bow});
            }
        }
        paint_polygon(p, pts, c);
        return;
    }
    if (suit == 2 || suit == 3) {
        // Heart (and, turned point up, the spade's blade): the classic heart curve.
        std::vector<gf::Point> pts;
        const double k = size / 33.0;
        const double flip = suit == 3 ? -1 : 1;
        const double lift = suit == 3 ? -size * .1 : size * .02;
        for (int i = 0; i < 72; ++i) {
            const double t = 2 * pi * i / 72;
            const double x = 16 * std::pow(std::sin(t), 3);
            const double y = 13 * std::cos(t) - 5 * std::cos(2 * t) - 2 * std::cos(3 * t) - std::cos(4 * t);
            pts.push_back({cx + x * k, cy + lift - flip * (y - 2.5) * k});
        }
        paint_polygon(p, pts, c);
        if (suit == 3)
            stem(p, cx, cy + size * .1, size, c);
        return;
    }
    // Club: three round leaves and a stem.
    const double r = size * .235;
    disc(p, cx, cy - size * .22, r, c);
    disc(p, cx - size * .245, cy + size * .06, r, c);
    disc(p, cx + size * .245, cy + size * .06, r, c);
    p.fill_rect({cx - size * .12, cy - size * .1, size * .24, size * .2}, c);
    stem(p, cx, cy + size * .04, size, c);
}

const char* rank_text(int rank) {
    static const char* const names[13] = {"A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};
    return names[std::clamp(rank, 1, 13) - 1];
}

// The top-left index: the rank and a small suit sign under it.
void corner_index(gf::Painter& p, int suit, int rank) {
    const gf::Color c = is_red(suit) ? red : black;
    const std::string text = rank_text(rank);
    const bool ten = rank == 10;
    const gf::FontSpec f{gf::FontRole::content, ten ? 40.0 : 46.0, 700, false, ten ? -2.5 : 0.0};
    const gf::Size m = p.measure_text_utf8(text, f);
    p.draw_text_utf8({36 - m.width * .5, 62}, text, f, c);
    pip(p, suit, 36, 90, 30);
}

// Where the pips lie: columns (left, middle, right) and rows from top (0) to bottom (1).
struct Spot {
    int column;
    double row;
};
std::vector<Spot> layout(int rank) {
    switch (rank) {
    case 2: return {{1, 0}, {1, 1}};
    case 3: return {{1, 0}, {1, .5}, {1, 1}};
    case 4: return {{0, 0}, {2, 0}, {0, 1}, {2, 1}};
    case 5: return {{0, 0}, {2, 0}, {1, .5}, {0, 1}, {2, 1}};
    case 6: return {{0, 0}, {2, 0}, {0, .5}, {2, .5}, {0, 1}, {2, 1}};
    case 7: return {{0, 0}, {2, 0}, {1, .25}, {0, .5}, {2, .5}, {0, 1}, {2, 1}};
    case 8: return {{0, 0}, {2, 0}, {1, .25}, {0, .5}, {2, .5}, {1, .75}, {0, 1}, {2, 1}};
    case 9: return {{0, 0}, {2, 0}, {0, 1.0 / 3}, {2, 1.0 / 3}, {1, .5}, {0, 2.0 / 3}, {2, 2.0 / 3}, {0, 1}, {2, 1}};
    default:
        return {{0, 0}, {2, 0}, {1, 1.0 / 6}, {0, 1.0 / 3}, {2, 1.0 / 3}, {0, 2.0 / 3}, {2, 2.0 / 3}, {1, 5.0 / 6}, {0, 1}, {2, 1}};
    }
}

struct Colours {
    gf::Color robe, accent;
};
Colours colours(int rank, int suit) {
    // Kings in the suit's own colour, queens and jacks in the other, as old decks vary them.
    const bool redsuit = is_red(suit);
    if (rank == 13)
        return redsuit ? Colours{red, blue} : Colours{blue, red};
    if (rank == 12)
        return redsuit ? Colours{blue, red} : Colours{red, gold};
    return redsuit ? Colours{gold, blue} : Colours{gold, red};
}

void poly(gf::Painter& p, std::vector<gf::Point> pts, gf::Color fill) {
    paint_polygon(p, pts, fill);
}

void face(gf::Painter& p, double x, double y, double r) {
    disc(p, x, y, r, skin);
    ring(p, x, y, r, outline, 1.4);
    disc(p, x - r * .36, y - r * .1, 2.2, outline);
    disc(p, x + r * .36, y - r * .1, 2.2, outline);
    p.draw_line({x, y - r * .02}, {x - r * .1, y + r * .3}, outline, 1.2);
    p.draw_line({x - r * .25, y + r * .52}, {x + r * .25, y + r * .52}, rgb(170, 50, 50), 1.6);
}

// The upper half of a court figure, in the frame's top half (y 60 to 252); the lower
// half is the same drawn upside down. Proportioned like an old court card: a large
// head under its crown, the shoulders high, the robe filling the frame to the middle.
void figure(gf::Painter& p, int rank, int suit) {
    const Colours c = colours(rank, suit);
    const double cx = 180, head_y = 132, head_r = 31;
    // the robe from the shoulders down to the middle, edged in black
    poly(p, {{86, 254}, {104, 176}, {256, 176}, {274, 254}}, c.robe);
    p.draw_line({104, 176}, {86, 254}, outline, 2);
    p.draw_line({256, 176}, {274, 254}, outline, 2);
    p.draw_line({104, 176}, {256, 176}, outline, 2);
    // a broad sash across the robe, and a pattern of small lozenges
    poly(p, {{100, 210}, {112, 190}, {270, 238}, {266, 254}, {250, 254}}, c.accent);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 7; ++col) {
            const double x = 114 + col * 22 + (row & 1) * 11, y = 200 + row * 18;
            poly(p, {{x, y - 4.5}, {x + 4.5, y}, {x, y + 4.5}, {x - 4.5, y}}, rgb(255, 255, 255, 140));
        }
    if (rank == 13) {
        // the king: a sword over his shoulder, ermine at the neck, a beard and a crown
        poly(p, {{102, 66}, {111, 66}, {124, 254}, {113, 254}}, rgb(206, 212, 222));
        p.draw_line({107, 66}, {117, 254}, rgb(120, 126, 140), 1.2);
        p.draw_line({90, 104}, {130, 100}, gold, 7);
        disc(p, 110, 102, 5, red);
        p.fill_rect({118, 168, 124, 16}, white);
        for (int k = 0; k < 10; ++k)
            disc(p, 124 + k * 12.6, 176, 2.2, black);
        p.fill_rounded_rect({cx - 40, head_y - 30, 80, 56}, 20, rgb(110, 70, 40));
        face(p, cx, head_y, head_r);
        poly(p, {{cx - 28, head_y + 8}, {cx + 28, head_y + 8}, {cx + 24, head_y + 40}, {cx, head_y + 54}, {cx - 24, head_y + 40}},
             rgb(238, 234, 226));
        p.draw_line({cx - 10, head_y + 14}, {cx + 10, head_y + 14}, rgb(170, 50, 50), 2);
        poly(p, {{cx - 38, head_y - 24}, {cx - 38, head_y - 58}, {cx - 22, head_y - 42}, {cx - 10, head_y - 70}, {cx, head_y - 48},
                 {cx + 10, head_y - 70}, {cx + 22, head_y - 42}, {cx + 38, head_y - 58}, {cx + 38, head_y - 24}},
             gold);
        p.fill_rect({cx - 38, head_y - 32, 76, 8}, red);
        disc(p, cx - 10, head_y - 58, 4.5, red);
        disc(p, cx + 10, head_y - 58, 4.5, red);
        disc(p, cx, head_y - 40, 4, blue);
    } else if (rank == 12) {
        // the queen: long golden hair, a lace collar, a small crown and a flower
        p.fill_rounded_rect({cx - 42, head_y - 34, 84, 92}, 30, gold);
        for (int k = 0; k < 11; ++k)
            disc(p, 112 + k * 13.6, 177, 7.5, white);
        face(p, cx, head_y, head_r - 1);
        poly(p, {{cx - 28, head_y - 26}, {cx - 28, head_y - 46}, {cx - 14, head_y - 36}, {cx, head_y - 58}, {cx + 14, head_y - 36},
                 {cx + 28, head_y - 46}, {cx + 28, head_y - 26}},
             gold);
        disc(p, cx, head_y - 42, 5, blue);
        p.draw_line({240, 250}, {246, 196}, rgb(40, 120, 60), 4);
        for (int k = 0; k < 5; ++k) {
            const double a = k * 2 * pi / 5;
            disc(p, 246 + std::cos(a) * 10, 186 + std::sin(a) * 10, 8, red);
        }
        disc(p, 246, 186, 6, gold);
    } else {
        // the jack: curls, a feathered cap and a halberd
        p.draw_line({252, 70}, {260, 254}, rgb(120, 80, 40), 5);
        poly(p, {{244, 66}, {274, 54}, {278, 92}, {250, 98}}, rgb(198, 204, 214));
        disc(p, cx - 30, head_y - 2, 13, rgb(110, 70, 40));
        disc(p, cx + 30, head_y - 2, 13, rgb(110, 70, 40));
        face(p, cx, head_y, head_r - 1);
        poly(p, {{cx - 40, head_y - 14}, {cx - 34, head_y - 50}, {cx, head_y - 62}, {cx + 38, head_y - 50}, {cx + 42, head_y - 14}}, red);
        p.draw_line({cx - 40, head_y - 16}, {cx + 42, head_y - 16}, gold, 6);
        poly(p, {{cx + 22, head_y - 52}, {cx + 56, head_y - 80}, {cx + 72, head_y - 74}, {cx + 58, head_y - 60}, {cx + 28, head_y - 42}}, gold);
    }
    // the suit sign in the frame's corner
    pip(p, suit, 86, 88, 28);
}

// One layer of a face: everything upright, or (`turned`) everything that is upside down
// on the card, drawn upright at its mirrored place so the layer can be turned round.
void paint_layer(gf::Painter& p, int suit, int rank, bool turned) {
    if (!turned) {
        p.fill_rounded_rect({2.4, 2.4, W - 4.8, H - 4.8}, 18, white);
        p.stroke_rounded_rect({3, 3, W - 6, H - 6}, 17.4, rgb(196, 198, 206), 1.2);
    }
    corner_index(p, suit, rank);
    if (rank >= 11) {
        const gf::Rect frame{62, 60, 236, 384};
        if (!turned)
            p.fill_rect(frame, cream);
        p.save();
        p.clip_rect({frame.x, frame.y, frame.width, H * .5 - frame.y});
        figure(p, rank, suit);
        p.restore();
        if (!turned) {
            p.stroke_rect(frame, is_red(suit) ? red : blue, 3.5);
            p.draw_line({frame.x, H * .5}, {frame.x + frame.width, H * .5}, outline, 2.5);
        }
        return;
    }
    if (rank == 1) {
        if (!turned) {
            pip(p, suit, W * .5, H * .5, suit == 3 ? 170 : 140);
            if (suit == 3)
                ring(p, W * .5, H * .5, 112, black, 1.6);
        }
        return;
    }
    const double columns[3] = {112, 180, 248};
    const double top = 100, bottom = 404;
    const double size = rank >= 9 ? 54 : 60;
    for (const Spot& s : layout(rank)) {
        const double x = columns[s.column], y = top + (bottom - top) * s.row;
        const bool lower = s.row > .5 + 1e-9;
        if (lower != turned)
            continue;
        if (turned)
            pip(p, suit, W - x, H - y, size);
        else
            pip(p, suit, x, y, size);
    }
}

bool render(gf::PaintFramebuffer& frame, const gf::ImageRegistry& images, int suit, int rank, bool turned) {
    if (!frame.begin(images, {0, 0, W, H}))
        return false;
    std::span<std::byte> pixels = frame.pixels();
    std::memset(pixels.data(), 0, pixels.size());
    paint_layer(frame.painter(), suit, rank, turned);
    frame.end();
    return true;
}

}  // namespace

bool make_classic_faces(gf::Window& window, std::array<gf::ImageId, 52>& faces) {
    std::unique_ptr<gf::PaintFramebuffer> upright = window.create_framebuffer({W, H}, 1.0);
    std::unique_ptr<gf::PaintFramebuffer> turned = window.create_framebuffer({W, H}, 1.0);
    if (!upright || !turned)
        return false;
    const int w = static_cast<int>((*upright).width()), h = static_cast<int>((*upright).height());
    const bool rgba = (*upright).channel_order() == gf::FramebufferChannelOrder::rgba;
    std::vector<std::byte> out(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    for (int suit = 0; suit < 4; ++suit)
        for (int rank = 1; rank <= 13; ++rank) {
            if (!render(*upright, window.image_resources(), suit, rank, false) ||
                !render(*turned, window.image_resources(), suit, rank, true))
                return false;
            const std::span<std::byte> a = (*upright).pixels();
            const std::span<std::byte> b = (*turned).pixels();
            const std::size_t ra = (*upright).row_bytes(), rb = (*turned).row_bytes();
            // The turned layer, turned half round, over the upright one (premultiplied).
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const std::byte* pa = a.data() + static_cast<std::size_t>(y) * ra + static_cast<std::size_t>(x) * 4;
                    const std::byte* pb = b.data() + static_cast<std::size_t>(h - 1 - y) * rb + static_cast<std::size_t>(w - 1 - x) * 4;
                    const unsigned keep = 255U - static_cast<unsigned>(pb[3]);
                    std::byte* o = out.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4;
                    unsigned c[4];
                    for (int k = 0; k < 4; ++k)
                        c[k] = std::min(255U, static_cast<unsigned>(pb[k]) + (static_cast<unsigned>(pa[k]) * keep + 127U) / 255U);
                    // the window's images are BGRA
                    o[0] = static_cast<std::byte>(rgba ? c[2] : c[0]);
                    o[1] = static_cast<std::byte>(c[1]);
                    o[2] = static_cast<std::byte>(rgba ? c[0] : c[2]);
                    o[3] = static_cast<std::byte>(c[3]);
                }
            gf::ImageId& image = faces[static_cast<std::size_t>(suit * 13 + rank - 1)];
            if (image.value)
                static_cast<void>(window.remove_image(image));
            image = window.load_bgra32_premultiplied(static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h),
                                                     static_cast<std::uint64_t>(w) * 4, out)
                        .image;
        }
    return true;
}

}  // namespace games
