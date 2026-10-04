#include "scene.hpp"

#include "platform/text.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace tg {
namespace {

const Col ink = hex(0x2B2118);
const Col cream = hex(0xFFF6E0);
const Col brass = hex(0xE6B866);
const Col wall_top = hex(0x3A2A1E);
const Col wall_bottom = hex(0x1C1410);
const Col plate = hex(0x241A13);
const Col glass_dark = hex(0x4A3D33);
const Col glass_lit = hex(0xFFD77A);

enum class Align { left, centre, right };

// Draws one line (or a wrapped paragraph) with its top-left at a point given in points.
// Returns the drawn height in points.
double words(Canvas& canvas, double scale, const std::string& text, Font font, double points,
             double x, double y, Col color, Align align, double wrap_points) {
    const double size = points * scale;
    const double wrap = wrap_points * scale;
    const Mask& mask = text_mask(text, font, size, wrap);
    double left = x * scale;
    if (align == Align::centre) {
        left -= mask.w * .5;
    } else if (align == Align::right) {
        left -= mask.w;
    }
    const int device_x = static_cast<int>(std::lround(left));
    const int device_y = static_cast<int>(std::lround(y * scale));
    canvas.draw_mask(mask, device_x, device_y, color, 1);
    const double height = mask.h / scale;
    return height;
}

void draw_lamp(Canvas& canvas, const Box& box, double glow, bool hover, bool cursor) {
    const double centre_x = box.x + box.w * .5;
    const double centre_y = box.y + box.h * .5;
    const double radius = box.w * .36;
    if (glow > 0) {
        // The halo: a soft light on the plate that grows with the lamp.
        const double halo = radius * (1.25 + .25 * glow);
        Paint light = Paint::rad(centre_x, centre_y, halo,
                                 {{0, alpha(glass_lit, static_cast<float>(.55 * glow))},
                                  {1, alpha(glass_lit, 0)}});
        canvas.begin();
        canvas.circle(centre_x, centre_y, halo);
        canvas.fill(light);
    }
    const Col body = mix(glass_dark, glass_lit, static_cast<float>(glow));
    Paint glass = Paint::rad(centre_x - radius * .3, centre_y - radius * .35, radius * 1.5,
                             {{0, shade(body, 1.35f)}, {1, shade(body, .72f)}});
    canvas.begin();
    canvas.circle(centre_x, centre_y, radius);
    canvas.fill(glass);
    canvas.begin();
    canvas.circle(centre_x, centre_y, radius);
    canvas.stroke(alpha(ink, .55f), std::max(1.0, box.w * .02));
    if (hover || cursor) {
        canvas.begin();
        canvas.circle(centre_x, centre_y, radius + box.w * .06);
        canvas.stroke(alpha(cursor ? cream : brass, .9f), std::max(1.5, box.w * .03));
    }
}

std::string status_right(const Board& board) {
    std::string text = "Presses " + std::to_string(board.moves.size());
    if (board.best > 0) {
        text += "  \xC2\xB7  Best " + std::to_string(board.best);
    }
    return text;
}

std::string message_line(const Board& board) {
    if (solved(board)) {
        const std::string count = std::to_string(board.moves.size());
        return "All lit in " + count + ". Click or press Enter for another board.";
    }
    const int dark = board.side * board.side - lit_count(board);
    if (dark == 1) {
        return "One lamp still dark.";
    }
    return std::to_string(dark) + " lamps still dark. Press one to turn it and its neighbours.";
}

void draw_help(Canvas& canvas, double scale, const Layout& layout) {
    canvas.fill_rect(0, 0, layout.width, layout.height, alpha(hex(0x000000), .55f));
    const Box& panel = layout.panel;
    canvas.begin();
    canvas.rrect(panel.x, panel.y, panel.w, panel.h, 10);
    canvas.fill(cream);
    canvas.begin();
    canvas.rrect(panel.x, panel.y, panel.w, panel.h, 10);
    canvas.stroke(brass, 2);
    const std::vector<std::string> paragraphs = help_text();
    const double pad = layout.compact ? 12 : 20;
    const double wrap = panel.w - 2 * pad;
    // Choose the largest body size whose paragraphs fit the card, so the help reads
    // at every window size instead of running off the bottom.
    double body = layout.type;
    for (int attempt = 0; attempt < 8; ++attempt) {
        double needed = layout.title * 1.5;
        for (std::size_t index = 1; index < paragraphs.size(); ++index) {
            const Mask& mask = text_mask(paragraphs[index], Font::speech, body * scale, wrap * scale);
            needed += mask.h / scale + body * .45;
        }
        if (needed <= panel.h - 2 * pad || body <= 8.5) {
            break;
        }
        body -= .75;
    }
    double y = panel.y + pad;
    static_cast<void>(words(canvas, scale, paragraphs[0], Font::title, layout.title, panel.x + pad,
                            y, ink, Align::left, 0));
    y += layout.title * 1.5;
    for (std::size_t index = 1; index < paragraphs.size(); ++index) {
        const double height = words(canvas, scale, paragraphs[index], Font::speech, body,
                                    panel.x + pad, y, ink, Align::left, wrap);
        y += height + body * .45;
    }
}

}  // namespace

std::vector<std::string> help_text() {
    std::vector<std::string> paragraphs;
    paragraphs.push_back("Template Game");
    paragraphs.push_back("Light every lamp. Pressing a lamp turns it over, and turns over the "
                         "lamps directly above, below, left and right of it.");
    paragraphs.push_back("Click a lamp, or move with the arrow keys and press Enter or Space. "
                         "Z takes back a press. N deals a new board.");
    paragraphs.push_back("Every board can be solved. Your fewest presses for this size is kept "
                         "as your best.");
    paragraphs.push_back("Esc closes this card.");
    return paragraphs;
}

void draw_scene(Canvas& canvas, double scale, const Layout& layout, const Board& board,
                const Visual& visual) {
    canvas.set_transform(Mat{});
    canvas.global_alpha = 1;
    canvas.save();
    canvas.scale(scale, scale);
    Paint wall = Paint::lin(0, 0, 0, layout.height, {{0, wall_top}, {1, wall_bottom}});
    canvas.begin();
    canvas.rect(0, 0, layout.width, layout.height);
    canvas.fill(wall);

    const Box& area = layout.board;
    canvas.begin();
    canvas.rrect(area.x, area.y, area.w, area.h, area.w * .04);
    canvas.fill(plate);
    canvas.begin();
    canvas.rrect(area.x, area.y, area.w, area.h, area.w * .04);
    canvas.stroke(alpha(brass, .5f), 1.5);

    const int cells = board.side * board.side;
    for (int cell = 0; cell < cells; ++cell) {
        const std::size_t index = static_cast<std::size_t>(cell);
        const double glow = index < visual.glow.size() ? visual.glow[index] : 0;
        const Box box = cell_box(layout, board.side, cell);
        draw_lamp(canvas, box, glow, visual.hover == cell, visual.cursor == cell);
    }

    const double status_y = layout.status.y + (layout.status.h - layout.title) * .5;
    const std::string size_name = std::to_string(board.side) + " \xC3\x97 " + std::to_string(board.side);
    static_cast<void>(words(canvas, scale, "Lamps " + size_name, Font::title, layout.title,
                            layout.status.x, status_y, cream, Align::left, 0));
    const double fact_y = layout.status.y + (layout.status.h - layout.type) * .5;
    static_cast<void>(words(canvas, scale, status_right(board), Font::speech, layout.type,
                            layout.status.x + layout.status.w, fact_y, brass, Align::right, 0));

    const Col message_color = mix(cream, glass_lit, static_cast<float>(visual.won));
    const double message_y = layout.message.y + (layout.message.h - layout.type) * .5;
    static_cast<void>(words(canvas, scale, message_line(board), Font::speech, layout.type,
                            layout.width * .5, message_y, message_color, Align::centre, 0));

    if (visual.help) {
        draw_help(canvas, scale, layout);
    }
    canvas.restore();
    text_cache_trim();
}

}  // namespace tg
