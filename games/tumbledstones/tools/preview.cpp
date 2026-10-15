// Renders any state of the game to a PNG, with the scene the window uses and the kit's
// headless text:
//
//   ts_preview <out.png> <width> <height> <scale> <state> [seed]
//
// states: play, hover, held, turning, drag, pull, fall, juggle, choose, won. Prints how
// long the tray, the stones and one whole frame took.
#include "noise.hpp"
#include "rules.hpp"
#include "scene.hpp"
#include "stage.hpp"

#include "png_writer.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// The first neighbouring pair of one family: a match to make.
bool find_pair(const ts::Board& board, int& a, int& b) {
    for (int cell = 0; cell < ts::cell_count; ++cell) {
        const int right = cell % ts::side + 1 < ts::side ? cell + 1 : -1;
        if (right >= 0 && board.cells[static_cast<std::size_t>(right)].family == board.cells[static_cast<std::size_t>(cell)].family) {
            a = cell;
            b = right;
            return true;
        }
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr, "ts_preview <out.png> <width> <height> <scale> <state> [seed]\n");
        return 2;
    }
    const std::string out = argv[1];
    const double width = std::atof(argv[2]);
    const double height = std::atof(argv[3]);
    const double scale = std::atof(argv[4]);
    const std::string state = argv[5];
    const std::uint64_t seed = argc > 6 ? std::strtoull(argv[6], nullptr, 10) : 20261015ULL;

    ts::Session session;
    session.board = ts::new_game(seed, 9);
    session.playing = true;
    session.next_target = 9;
    session.best[9] = ts::Best{312, 141, 171};
    session.board.moves = 23;
    session.board.points = 344;
    session.board.largest = 4;
    // A few swap marks, as play leaves them.
    session.board.cells[23].swapped = true;
    session.board.cells[24].swapped = true;
    ts::Motion motion;
    motion.settle(session.board);
    ts::Frame frame;
    frame.session = &session;
    frame.motion = &motion;

    const ts::Layout layout = ts::compute_layout(width, height);
    ts::Scene scene;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    scene.resize(layout, scale, render::Order::bgra);
    const double tray_seconds = seconds_since(start);

    int a = 0;
    int b = 1;
    find_pair(session.board, a, b);
    if (state == "hover") {
        frame.hover = 45;
        double x = 0;
        double y = 0;
        ts::cell_centre(layout, 45, x, y);
        frame.hover_x = x - layout.cell * .25;
        frame.hover_y = y - layout.cell * .3;
        frame.hover_lift = 1;
    } else if (state == "held" || state == "turning") {
        frame.held = session.board.cells[44].serial;
        frame.held_cell = 44;
        frame.held_orientation = ts::flipped(session.board.cells[44].orientation);
        frame.hover = 44;
        if (state == "turning") {
            frame.held_turning = true;
            frame.held_pose = ts::turned(ts::orientation_pose(session.board.cells[44].orientation), 1, 3.14159265 * .4);
        }
    } else if (state == "drag") {
        frame.drag_cell = 55;
        frame.drag_to = 56;
        frame.drag_allowed = true;
        double x = 0;
        double y = 0;
        ts::cell_centre(layout, 55, x, y);
        frame.drag_x = x + layout.cell * .45;
        frame.drag_y = y + layout.cell * .1;
    } else if (state == "pull" || state == "fall") {
        const ts::Board before = session.board;
        ts::Board turned_board = before;
        turned_board.cells[static_cast<std::size_t>(a)].orientation = turned_board.cells[static_cast<std::size_t>(b)].orientation;
        const ts::Outcome outcome = ts::set_down(session.board, a, turned_board.cells[static_cast<std::size_t>(b)].orientation);
        motion.plan(turned_board, outcome, session.board, false);
        motion.advance(state == "pull" ? .16 : .42);
    } else if (state == "juggle") {
        const ts::Board before = session.board;
        const ts::Outcome outcome = ts::juggle_tray(session.board);
        motion.plan(before, outcome, session.board, false);
        motion.advance(.1 + ts::juggle_rise + ts::juggle_fly * .4);
    } else if (state == "choose") {
        frame.panel = ts::Panel::choose;
        frame.choosing = 12;
        frame.can_cancel = true;
    } else if (state == "won") {
        session.board.won = true;
        session.board.largest = 11;
        session.board.target = 11;
        frame.panel = ts::Panel::won;
        frame.new_best = true;
    }

    const int w = static_cast<int>(width * scale);
    const int h = static_cast<int>(height * scale);
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(w * h), 0U);
    const render::Target target{pixels.data(), w, w, h, render::Order::bgra};
    const std::chrono::steady_clock::time_point stones = std::chrono::steady_clock::now();
    for (int round = 0; round < 3; ++round) {
        scene.want(frame);
        if (!scene.begin_batch())
            break;
        scene.run_batch();
        scene.end_batch();
    }
    const double stone_seconds = seconds_since(stones);
    const std::chrono::steady_clock::time_point drawing = std::chrono::steady_clock::now();
    scene.draw(target, frame);
    const double frame_seconds = seconds_since(drawing);
    // A second frame, the kept tray in place: what an animation step costs.
    const std::chrono::steady_clock::time_point again = std::chrono::steady_clock::now();
    scene.draw(target, frame);
    const double second_seconds = seconds_since(again);
    std::vector<std::uint8_t> bytes(pixels.size() * 4);
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        bytes[i * 4] = static_cast<std::uint8_t>(pixels[i] & 0xffU);
        bytes[i * 4 + 1] = static_cast<std::uint8_t>((pixels[i] >> 8) & 0xffU);
        bytes[i * 4 + 2] = static_cast<std::uint8_t>((pixels[i] >> 16) & 0xffU);
        bytes[i * 4 + 3] = 255;
    }
    const bool ok = kit::write_png(out, w, h, bytes);
    std::printf("%s %s: tray %.0f ms, stones %.0f ms, first frame %.0f ms, next %.1f ms\n", ok ? "wrote" : "FAILED",
                out.c_str(), tray_seconds * 1000, stone_seconds * 1000, frame_seconds * 1000, second_seconds * 1000);
    return ok ? 0 : 1;
}
