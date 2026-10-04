// Headless frames: renders the game at any size and state to a PNG, with real
// fonts, on any machine. Look at the picture; do not assume it.
//
//   preview out.png [width height [scale [state]]]
//     width, height  the surface in points (default 1100 x 760; the minimum is 600 x 370)
//     scale          device pixels per point (default 1; try 2 and 1.5)
//     state          start | mid | solved | help   (default mid)
#include "png_writer.hpp"
#include "rules.hpp"
#include "scene.hpp"
#include "stage.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: preview out.png [width height [scale [start|mid|solved|help]]]\n";
        return 2;
    }
    const double width = argc > 3 ? std::atof(argv[2]) : 1100;
    const double height = argc > 3 ? std::atof(argv[3]) : 760;
    const double scale = argc > 4 ? std::atof(argv[4]) : 1;
    const std::string state = argc > 5 ? argv[5] : "mid";
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
