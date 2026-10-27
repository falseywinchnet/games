// Headless frames: renders the game at any size and state to a PNG, with real
// fonts, on any machine. Look at the picture; do not assume it.
//
//   preview out.png [width height [scale [state]]]
//     width, height  the surface in points (default 1100 x 760; the minimum is 600 x 370)
//     scale          device pixels per point (default 1; try 2 and 1.5)
//     state          start | mid | remark | solved | help | box   (default mid)
//
// "box" draws a mock of the game's box on the shelf at three sizes, so the emblem
// and cover colours can be judged alongside the native emblem in cover.cpp.
#include "platform/text.hpp"
#include "png_writer.hpp"
#include "rules.hpp"
#include "scene.hpp"
#include "stage.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

// Keep these equal to "colors" in GAME.json (top, bottom, accent).
const tg::Col cover_top = tg::rgb(92, 66, 44);
const tg::Col cover_bottom = tg::rgb(36, 26, 19);
const tg::Col cover_accent = tg::rgb(255, 215, 122);

// The box emblem, prototyped with the canvas. `s` is the emblem's size and (cx, cy) its
// centre, as in paint_entry_emblem. Use only discs, rounded rectangles, lines and
// polygons, sized as fractions of `s`, so it transcribes line for line to gf::Painter.
void draw_emblem(tg::Canvas& canvas, double cx, double cy, double s) {
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 2; ++column) {
            const bool lit = row != column;
            const tg::Col colour = lit ? cover_accent : tg::rgb(110, 96, 84);
            canvas.fill_circle(cx + (column - .5) * s * .46, cy + (row - .5) * s * .46, s * .19, colour);
        }
    }
}

// An approximation of ShelfBox::on_paint: cover gradient, accent glow, emblem, and the
// title on a dark scrim. The real shelf letters the title in Barlow Condensed.
void draw_box(tg::Canvas& canvas, double x, double y, double width) {
    const double height = width * 1.34;
    canvas.begin();
    canvas.rrect(x, y, width, height, width * .04);
    canvas.fill(tg::Paint::lin(0, y, 0, y + height, {{0, cover_top}, {1, cover_bottom}}));
    const double emblem = width * .56;
    const double cx = x + width * .5;
    const double cy = y + height * .38;
    canvas.begin();
    canvas.circle(cx, cy, emblem * .95);
    canvas.fill(tg::Paint::rad(cx, cy, emblem * .95, {{0, tg::alpha(cover_accent, .35f)}, {1, tg::alpha(cover_accent, 0)}}));
    draw_emblem(canvas, cx, cy, emblem);
    canvas.fill_rect(x, y + height * .7, width, height * .2, tg::alpha(tg::hex(0x000000), .45f));
    const double size = std::clamp(width * .11, 8.0, 18.0);
    const tg::Mask& title = tg::text_mask("TEMPLATE GAME", tg::Font::title, size, 0);
    canvas.draw_mask(title, static_cast<int>(cx - title.w * .5), static_cast<int>(y + height * .8 - title.h * .5),
                     tg::hex(0xFFF6E0));
}

int preview_box(const char* path) {
    tg::Canvas canvas;
    canvas.resize(560, 300);
    canvas.clear(tg::hex(0x4A3020));
    draw_box(canvas, 24, 150, 64);    // a crowded shelf in a small window
    draw_box(canvas, 120, 100, 110);  // the usual size
    draw_box(canvas, 270, 30, 190);   // lifted toward the viewer
    if (!kit::write_png(path, canvas.w, canvas.h, canvas.px)) {
        std::cerr << "preview: cannot write " << path << "\n";
        return 1;
    }
    std::cout << path << "  mock shelf boxes at 64, 110 and 190 px\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: preview out.png [width height [scale [start|mid|remark|solved|help|box]]]\n";
        return 2;
    }
    const double width = argc > 3 ? std::atof(argv[2]) : 1100;
    const double height = argc > 3 ? std::atof(argv[3]) : 760;
    const double scale = argc > 4 ? std::atof(argv[4]) : 1;
    const std::string state = argc > 5 ? argv[5] : "mid";
    if (state == "box") {
        const int result = preview_box(argv[1]);
        return result;
    }
    if (width < 100 || height < 100 || scale < .5 || scale > 4) {
        std::cerr << "preview: size or scale out of range\n";
        return 2;
    }
    tg::Board board = tg::new_game(5, 20261003);
    tg::Visual visual;
    if (state == "solved") {
        board.lit.assign(board.lit.size(), 1);
        board.moves.assign(11, 0);
        board.best = 11;
    } else if (state != "start") {
        static_cast<void>(tg::press(board, 7));
        static_cast<void>(tg::press(board, 12));
        static_cast<void>(tg::press(board, 18));
        board.best = 14;
    }
    tg::snap(visual, board);
    if (state == "mid") {
        visual.hover = 8;
        visual.glow[12] = .5;  // a lamp caught halfway through its turn
    }
    visual.help = state == "help";
    visual.remarked = state == "remark";
    const tg::Layout layout = tg::compute_layout(width, height);
    tg::Canvas canvas;
    canvas.resize(static_cast<int>(std::lround(width * scale)), static_cast<int>(std::lround(height * scale)));
    tg::draw_scene(canvas, scale, layout, board, visual);
    if (!kit::write_png(argv[1], canvas.w, canvas.h, canvas.px)) {
        std::cerr << "preview: cannot write " << argv[1] << "\n";
        return 1;
    }
    std::cout << argv[1] << "  " << canvas.w << " x " << canvas.h << " px  (" << state << ")\n";
    return 0;
}
