// Renders any state of the game to a PNG, with the scene the window uses, the kit's
// headless text and the card tables' felt:
//
//   sc_preview <out.png> <width> <height> <scale> <state> [n [seed]]
//
// states: start (the pile just filled), sorting (stones laid out on the felt), held (a
// stone in hand over a socket), wrong (over a pinned socket), falling (a stone on its way
// to the pile), full (the cabinet full, with clashes), choose, won, help. Prints how long
// the table, the stones and one whole frame took.
#include "carpet.hpp"
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

// Takes stones from the pile and lays them on the felt in rows by shape, as a player sorts
// them: `count` of them.
void sort_some(sc::Session& session, int count) {
    const int n = session.puzzle.n;
    std::vector<int> along(static_cast<std::size_t>(n), 0);
    for (int k = 0; k < count && !session.pile.empty(); ++k) {
        const int stone = session.pile.front();
        const int shape = sc::stone_shape(session.puzzle, stone);
        const double x = .08 + .26 * along[static_cast<std::size_t>(shape)]++;
        const double y = .3 + .7 * shape / std::max(1, n - 1);
        static_cast<void>(sc::set_on_felt(session, stone, std::min(x, 1.0), y));
    }
}

// Sets every stone where the answer has it, as the tumbler gives them up.
void set_answer(sc::Session& session) {
    const int cells = session.puzzle.n * session.puzzle.n;
    for (int step = 0; step < cells * 4; ++step) {
        int cell = 0;
        while (cell < cells && !(sc::stone_in(session, cell) != sc::solution_stone(session.puzzle, cell) &&
                                 sc::movable(session, sc::solution_stone(session.puzzle, cell))))
            ++cell;
        if (cell == cells)
            return;
        static_cast<void>(sc::set_in_cabinet(session, sc::solution_stone(session.puzzle, cell), cell));
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr, "sc_preview <out.png> <width> <height> <scale> <state> [n [seed]]\n");
        return 2;
    }
    const std::string out = argv[1];
    const double width = std::atof(argv[2]);
    const double height = std::atof(argv[3]);
    const double scale = std::atof(argv[4]);
    const std::string state = argv[5];
    const int n = argc > 6 ? std::atoi(argv[6]) : 5;
    const std::uint64_t seed = argc > 7 ? std::strtoull(argv[7], nullptr, 10) : 20261017ULL;

    sc::Session session;
    session.best[5] = sc::Best{31, 412};
    sc::new_game(session, seed, n);
    session.moves = 9;
    session.seconds = 187;
    sc::Frame frame;
    frame.session = &session;
    std::vector<int> waiting;
    frame.waiting = &waiting;

    const sc::Layout layout = sc::compute_layout(width, height, n);
    sc::Scene scene;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    const paint::Image cloth = paint::render_carpet_tile(paint::carpet_preset(3), 256);
    std::vector<std::byte> felt(cloth.pixels.size() * 4);
    for (std::size_t i = 0; i < cloth.pixels.size(); ++i) {
        felt[4 * i] = static_cast<std::byte>(cloth.pixels[i].b);
        felt[4 * i + 1] = static_cast<std::byte>(cloth.pixels[i].g);
        felt[4 * i + 2] = static_cast<std::byte>(cloth.pixels[i].r);
        felt[4 * i + 3] = std::byte{255};
    }
    scene.set_felt(felt, 256);
    scene.resize(layout, scale, render::Order::bgra);
    const double table_seconds = seconds_since(start);

    if (state == "sorting" || state == "held" || state == "wrong" || state == "falling") {
        sort_some(session, 2 * n);
        if (state == "held" || state == "wrong") {
            const int stone = session.pile.front();
            int cell = -1;
            for (int c = 0; c < n * n && cell < 0; ++c)
                if (session.puzzle.clue[static_cast<std::size_t>(c)] == (state == "wrong"))
                    cell = c;
            const sc::Box b = sc::cell_box(layout, cell);
            frame.held = stone;
            frame.held_x = b.x + b.w * .58;
            frame.held_y = b.y + b.h * .44;
            frame.target = cell;
            frame.target_ok = state == "held";
        } else if (state == "falling") {
            waiting.push_back(session.pile.back());
            frame.falling = session.pile.back();
            frame.fall = .55;
            frame.turn = 1.1;
        }
    } else if (state == "full") {
        // Every stone set, one pair of a row's stones changed round: the clashes marked.
        set_answer(session);
        session.won = false;
        // Two unpinned stones of one row changed round: their columns clash.
        int a = -1;
        int b = -1;
        for (int cell = 0; cell < n * n && b < 0; ++cell) {
            if (session.puzzle.clue[static_cast<std::size_t>(cell)])
                continue;
            if (a < 0)
                a = cell;
            else if (cell / n == a / n)
                b = cell;
        }
        if (b >= 0) {
            const int first = sc::stone_in(session, a);
            const int second = sc::stone_in(session, b);
            session.where[static_cast<std::size_t>(first)].cell = b;
            session.where[static_cast<std::size_t>(second)].cell = a;
        }
        frame.clashes = true;
    } else if (state == "choose") {
        frame.panel = sc::Panel::choose;
        frame.can_cancel = true;
    } else if (state == "won") {
        set_answer(session);
        frame.panel = sc::Panel::won;
        frame.new_best = session.won;
    } else if (state == "help") {
        frame.help = true;
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
    // A second frame, the kept table and stones in place: what a step of the hand costs.
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
    std::printf("%s %s: table %.0f ms, stones %.0f ms, first frame %.0f ms, next %.1f ms\n", ok ? "wrote" : "FAILED",
                out.c_str(), table_seconds * 1000, stone_seconds * 1000, frame_seconds * 1000, second_seconds * 1000);
    return ok ? 0 : 1;
}
