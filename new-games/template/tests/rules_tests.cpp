// Headless checks of the rules, the save and the layout. No window, no toolkit.
// These run in the repository's portable-core profile on every platform, so they
// must be deterministic and must not depend on timing.
#include "rules.hpp"
#include "save.hpp"
#include "stage.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAILED: " << what << "\n";
        ++failures;
    }
}

void test_generation() {
    for (int side = tg::minimum_side; side <= tg::maximum_side; ++side) {
        for (std::uint64_t seed = 1; seed <= 200; ++seed) {
            const tg::Board first = tg::new_game(side, seed);
            const tg::Board second = tg::new_game(side, seed);
            check(first.lit == second.lit, "the same seed gives the same board");
            check(!tg::solved(first), "a new board is never already solved");
            check(static_cast<int>(first.lit.size()) == side * side, "a board has side * side lamps");
        }
    }
    // The seeded boards are pinned: a change here means saved games would no longer
    // reload to the position the player left. Update deliberately, with a new save version.
    const tg::Board pinned = tg::new_game(4, 20261003);
    std::string picture;
    for (std::size_t index = 0; index < pinned.lit.size(); ++index) {
        picture += pinned.lit[index] != 0 ? '1' : '0';
    }
    std::cout << "seed 20261003, side 4: " << picture << "\n";
    check(picture == "0011001011001111", "the generator's output for a known seed has not drifted");
}

void test_moves() {
    tg::Board board = tg::new_game(4, 7);
    const std::vector<std::uint8_t> start = board.lit;
    check(tg::press(board, 5), "a press inside the board is accepted");
    check(board.lit != start, "a press changes the board");
    check(!tg::press(board, -1) && !tg::press(board, 16), "a press outside the board is refused");
    check(board.moves.size() == 1, "refused presses are not recorded");
    check(tg::undo(board), "a press can be taken back");
    check(board.lit == start && board.moves.empty(), "undo restores the exact position");
    check(!tg::undo(board), "undo with no presses reports false");
    // Pressing the same lamp twice returns to the start: the rule every solution rests on.
    static_cast<void>(tg::press(board, 9));
    static_cast<void>(tg::press(board, 9));
    check(board.lit == start, "two presses of one lamp cancel");
}

// Proves each generated board can be solved, independently of how it was generated:
// solves "which lamps must be pressed" as linear equations over the two values
// dark and lit (Gaussian elimination), presses the answer and checks the board.
bool solve_board(const tg::Board& board, std::vector<int>& presses) {
    const int cells = board.side * board.side;
    // rows[i] holds, for lamp i, which presses turn it over, then whether it must turn.
    std::vector<std::vector<std::uint8_t>> rows(static_cast<std::size_t>(cells),
                                                std::vector<std::uint8_t>(static_cast<std::size_t>(cells) + 1, 0));
    for (int cell = 0; cell < cells; ++cell) {
        const int row = cell / board.side;
        const int column = cell % board.side;
        const int neighbours[5][2] = {{row, column}, {row - 1, column}, {row + 1, column},
                                      {row, column - 1}, {row, column + 1}};
        for (int index = 0; index < 5; ++index) {
            const int r = neighbours[index][0];
            const int c = neighbours[index][1];
            if (r >= 0 && c >= 0 && r < board.side && c < board.side) {
                rows[static_cast<std::size_t>(r * board.side + c)][static_cast<std::size_t>(cell)] = 1;
            }
        }
        rows[static_cast<std::size_t>(cell)][static_cast<std::size_t>(cells)] =
            board.lit[static_cast<std::size_t>(cell)] != 0 ? 0 : 1;
    }
    std::vector<int> pivot_of_column(static_cast<std::size_t>(cells), -1);
    int rank = 0;
    for (int column = 0; column < cells && rank < cells; ++column) {
        int found = -1;
        for (int row = rank; row < cells; ++row) {
            if (rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] != 0) {
                found = row;
                break;
            }
        }
        if (found < 0) {
            continue;
        }
        std::swap(rows[static_cast<std::size_t>(rank)], rows[static_cast<std::size_t>(found)]);
        for (int row = 0; row < cells; ++row) {
            if (row == rank || rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] == 0) {
                continue;
            }
            for (int k = column; k <= cells; ++k) {
                rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(k)] ^=
                    rows[static_cast<std::size_t>(rank)][static_cast<std::size_t>(k)];
            }
        }
        pivot_of_column[static_cast<std::size_t>(column)] = rank;
        ++rank;
    }
    for (int row = rank; row < cells; ++row) {
        if (rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(cells)] != 0) {
            return false;  // an equation "nothing = 1": this board has no solution
        }
    }
    presses.clear();
    for (int column = 0; column < cells; ++column) {
        const int pivot = pivot_of_column[static_cast<std::size_t>(column)];
        if (pivot >= 0 && rows[static_cast<std::size_t>(pivot)][static_cast<std::size_t>(cells)] != 0) {
            presses.push_back(column);
        }
    }
    return true;
}

void test_solvable() {
    for (int side = tg::minimum_side; side <= tg::maximum_side; ++side) {
        for (std::uint64_t seed = 1; seed <= 300; ++seed) {
            tg::Board board = tg::new_game(side, seed);
            std::vector<int> presses;
            const bool has_solution = solve_board(board, presses);
            check(has_solution, "every generated board has a solution");
            for (std::size_t index = 0; index < presses.size(); ++index) {
                static_cast<void>(tg::press(board, presses[index]));
            }
            check(tg::solved(board), "pressing the solver's lamps lights the board");
            check(board.best == static_cast<int>(presses.size()), "solving records the best count");
        }
    }
}

void test_save() {
    tg::Session session;
    session.board = tg::new_game(5, 99);
    session.next_side = 6;
    static_cast<void>(tg::press(session.board, 3));
    static_cast<void>(tg::press(session.board, 11));
    const std::string body = tg::encode_session(session);
    tg::Session loaded;
    check(tg::decode_session(body, loaded), "a saved session decodes");
    check(loaded.board.lit == session.board.lit && loaded.board.moves == session.board.moves,
          "the decoded board is the saved board");
    check(loaded.next_side == 6 && loaded.board.seed == 99, "the decoded settings are the saved settings");

    std::string body_out;
    const std::string sealed = tg::seal(body);
    check(tg::unseal(sealed, body_out) && body_out == body, "the envelope round-trips");
    std::string damaged = sealed;
    damaged[damaged.size() / 2] ^= 1;
    check(!tg::unseal(damaged, body_out), "one flipped bit is detected");
    check(!tg::unseal("SOMETHINGELSE1\n" + body, body_out), "another game's file is refused");
    check(!tg::unseal(std::string(tg::save_limit_bytes + 10, 'x'), body_out), "an oversized file is refused");

    tg::Session untouched;
    untouched.next_side = 3;
    check(!tg::decode_session("side=99\nseed=1\nmoves=\n", untouched), "an impossible size is refused");
    check(!tg::decode_session("side=4\nseed=1\nmoves=0,77\n", untouched), "an impossible move is refused");
    check(!tg::decode_session("side=4\nseed=abc\n", untouched), "a malformed number is refused");
    check(untouched.next_side == 3, "a refused load leaves the destination untouched");

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "tg-template-save-test";
    std::filesystem::remove_all(directory);
    const std::filesystem::path file = directory / "nested" / "template_game-dev-v1.txt";
    check(tg::write_save(file, body), "a save is written, creating its folder");
    check(tg::read_save(file, body_out) && body_out == body, "the written save reads back");
    check(!std::filesystem::exists(file.string() + ".tmp"), "no temporary file is left behind");
    check(!tg::read_save(directory / "absent.txt", body_out), "a missing save reports false");
    std::filesystem::remove_all(directory);
}

// Sweeps window sizes from below the minimum up to a large display and proves the
// layout never leaves the surface and never overlaps itself.
void test_layout() {
    for (int width = 320; width <= 2600; width += 20) {
        for (int height = 240; height <= 1500; height += 10) {
            const tg::Layout layout = tg::compute_layout(width, height);
            const tg::Box boxes[3] = {layout.status, layout.board, layout.message};
            for (int index = 0; index < 3; ++index) {
                const tg::Box& box = boxes[index];
                const bool within = box.x >= 0 && box.y >= 0 && box.x + box.w <= width + .001 &&
                                    box.y + box.h <= height + .001 && box.w > 0 && box.h > 0;
                if (!within) {
                    std::cerr << "layout " << width << "x" << height << " box " << index << "\n";
                }
                check(within, "every region stays on the surface");
            }
            check(!tg::overlap(layout.status, layout.board), "the status strip clears the board");
            check(!tg::overlap(layout.message, layout.board), "the message line clears the board");
            check(layout.panel.x >= 0 && layout.panel.y >= 0 &&
                      layout.panel.x + layout.panel.w <= width && layout.panel.y + layout.panel.h <= height,
                  "the help card stays on the surface");
        }
    }
    // At the smallest hosted surface the lamps must still be comfortable targets.
    const tg::Layout small = tg::compute_layout(tg::minimum_width, tg::minimum_height);
    const tg::Box cell = tg::cell_box(small, tg::maximum_side, 0);
    check(cell.w >= 36, "at 600 x 370 the smallest lamp is still at least 36 points across");
    check(small.compact, "600 x 370 uses the compact layout");
    // Picking: the centre of each cell picks it; the gap between lamps picks nothing.
    const tg::Layout layout = tg::compute_layout(900, 700);
    for (int cell_index = 0; cell_index < 16; ++cell_index) {
        const tg::Box box = tg::cell_box(layout, 4, cell_index);
        check(tg::pick_cell(layout, 4, box.x + box.w * .5, box.y + box.h * .5) == cell_index,
              "the centre of a lamp picks that lamp");
    }
    const tg::Box first = tg::cell_box(layout, 4, 0);
    check(tg::pick_cell(layout, 4, first.x + 1, first.y + 1) == -1, "a cell's corner is not a target");
    check(tg::pick_cell(layout, 4, -5, -5) == -1, "outside the board picks nothing");
}

// The animation must come to rest, or the game would never go idle.
void test_settles() {
    tg::Board board = tg::new_game(6, 5);
    tg::Visual visual;
    tg::snap(visual, board);
    static_cast<void>(tg::press(board, 14));
    int frames = 0;
    while (tg::advance(visual, board, 1.0 / 60, false)) {
        ++frames;
        if (frames > 600) {
            break;
        }
    }
    check(frames > 2 && frames < 120, "a press animates briefly and then settles");
    check(!tg::advance(visual, board, 1.0 / 60, false), "a settled picture stays settled");
    for (std::size_t index = 0; index < board.lit.size(); ++index) {
        check(visual.glow[index] == (board.lit[index] != 0 ? 1.0 : 0.0), "a settled lamp sits exactly on its value");
    }
    static_cast<void>(tg::press(board, 2));
    check(tg::advance(visual, board, 1.0 / 60, true), "reduced motion applies the change at once");
    check(!tg::advance(visual, board, 1.0 / 60, true), "and is then settled, with no easing");
}

}  // namespace

int main() {
    test_generation();
    test_moves();
    test_solvable();
    test_save();
    test_layout();
    test_settles();
    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "template game rules, save, layout and settling passed\n";
    return 0;
}
