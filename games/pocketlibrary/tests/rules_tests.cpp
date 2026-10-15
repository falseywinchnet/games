// Headless checks of the rules, the save and the layout. No window, no toolkit.
// These run in the repository's portable-core profile on every platform, so they
// must be deterministic and must not depend on timing.
#include "rules.hpp"
#include "save.hpp"
#include "stage.hpp"

#include <chrono>
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
    for (int side = pl::minimum_side; side <= pl::maximum_side; ++side) {
        for (std::uint64_t seed = 1; seed <= 200; ++seed) {
            const pl::Board first = pl::new_game(side, seed);
            const pl::Board second = pl::new_game(side, seed);
            check(first.lit == second.lit, "the same seed gives the same board");
            check(!pl::solved(first), "a new board is never already solved");
            check(static_cast<int>(first.lit.size()) == side * side, "a board has side * side lamps");
        }
    }
    // The seeded boards are pinned: a change here means saved games would no longer
    // reload to the position the player left. Update deliberately, with a new save version.
    const pl::Board pinned = pl::new_game(4, 20261003);
    std::string picture;
    for (std::size_t index = 0; index < pinned.lit.size(); ++index) {
        picture += pinned.lit[index] != 0 ? '1' : '0';
    }
    std::cout << "seed 20261003, side 4: " << picture << "\n";
    check(picture == "0011001011001111", "the generator's output for a known seed has not drifted");
}

void test_moves() {
    pl::Board board = pl::new_game(4, 7);
    const std::vector<std::uint8_t> start = board.lit;
    check(pl::press(board, 5), "a press inside the board is accepted");
    check(board.lit != start, "a press changes the board");
    check(!pl::press(board, -1) && !pl::press(board, 16), "a press outside the board is refused");
    check(board.moves.size() == 1, "refused presses are not recorded");
    check(pl::undo(board), "a press can be taken back");
    check(board.lit == start && board.moves.empty(), "undo restores the exact position");
    check(!pl::undo(board), "undo with no presses reports false");
    // Pressing the same lamp twice returns to the start: the rule every solution rests on.
    static_cast<void>(pl::press(board, 9));
    static_cast<void>(pl::press(board, 9));
    check(board.lit == start, "two presses of one lamp cancel");
}

// Proves each generated board can be solved, independently of how it was generated:
// solves "which lamps must be pressed" as linear equations over the two values
// dark and lit (Gaussian elimination), presses the answer and checks the board.
bool solve_board(const pl::Board& board, std::vector<int>& presses) {
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
    for (int side = pl::minimum_side; side <= pl::maximum_side; ++side) {
        for (std::uint64_t seed = 1; seed <= 300; ++seed) {
            pl::Board board = pl::new_game(side, seed);
            std::vector<int> presses;
            const bool has_solution = solve_board(board, presses);
            check(has_solution, "every generated board has a solution");
            for (std::size_t index = 0; index < presses.size(); ++index) {
                static_cast<void>(pl::press(board, presses[index]));
            }
            check(pl::solved(board), "pressing the solver's lamps lights the board");
            check(board.best == static_cast<int>(presses.size()), "solving records the best count");
        }
    }
}

void test_save() {
    pl::Session session;
    session.board = pl::new_game(5, 99);
    session.next_side = 6;
    static_cast<void>(pl::press(session.board, 3));
    static_cast<void>(pl::press(session.board, 11));
    const std::string body = pl::encode_session(session);
    pl::Session loaded;
    check(pl::decode_session(body, loaded), "a saved session decodes");
    check(loaded.board.lit == session.board.lit && loaded.board.moves == session.board.moves,
          "the decoded board is the saved board");
    check(loaded.next_side == 6 && loaded.board.seed == 99, "the decoded settings are the saved settings");

    std::string body_out;
    const std::string sealed = pl::seal(body);
    check(pl::unseal(sealed, body_out) && body_out == body, "the envelope round-trips");
    std::string damaged = sealed;
    damaged[damaged.size() / 2] ^= 1;
    check(!pl::unseal(damaged, body_out), "one flipped bit is detected");
    check(!pl::unseal("SOMETHINGELSE1\n" + body, body_out), "another game's file is refused");
    check(!pl::unseal(std::string(pl::save_limit_bytes + 10, 'x'), body_out), "an oversized file is refused");

    pl::Session untouched;
    untouched.next_side = 3;
    check(!pl::decode_session("side=99\nseed=1\nmoves=\n", untouched), "an impossible size is refused");
    check(!pl::decode_session("side=4\nseed=1\nmoves=0,77\n", untouched), "an impossible move is refused");
    check(!pl::decode_session("side=4\nseed=abc\n", untouched), "a malformed number is refused");
    check(untouched.next_side == 3, "a refused load leaves the destination untouched");

    // A save folder of this run's own, so two runs at once cannot collide.
    const long long stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("pl-save-test-" + std::to_string(stamp));
    std::filesystem::remove_all(directory);
#ifdef _WIN32
    static_cast<void>(_putenv_s("GAMES_STATE_DIR", directory.string().c_str()));
#else
    static_cast<void>(setenv("GAMES_STATE_DIR", directory.string().c_str(), 1));
#endif
    check(pl::write_save("pocket_library-dev-v1.txt", body), "a save is written, creating its folder");
    check(pl::read_save("pocket_library-dev-v1.txt", body_out) && body_out == body, "the written save reads back");
    check(!pl::read_save("absent.txt", body_out), "a missing save reports false");
    check(!pl::write_save("../escape.txt", body), "a save name cannot leave the game's folder");
    std::filesystem::remove_all(directory);
}

// Sweeps window sizes from below the minimum up to a large display and proves the
// layout never leaves the surface and never overlaps itself.
void test_layout() {
    for (int width = 320; width <= 2600; width += 20) {
        for (int height = 240; height <= 1500; height += 10) {
            const pl::Layout layout = pl::compute_layout(width, height);
            const pl::Box boxes[3] = {layout.status, layout.board, layout.message};
            for (int index = 0; index < 3; ++index) {
                const pl::Box& box = boxes[index];
                const bool within = box.x >= 0 && box.y >= 0 && box.x + box.w <= width + .001 &&
                                    box.y + box.h <= height + .001 && box.w > 0 && box.h > 0;
                if (!within) {
                    std::cerr << "layout " << width << "x" << height << " box " << index << "\n";
                }
                check(within, "every region stays on the surface");
            }
            check(!pl::overlap(layout.status, layout.board), "the status strip clears the board");
            check(!pl::overlap(layout.message, layout.board), "the message line clears the board");
            check(layout.panel.x >= 0 && layout.panel.y >= 0 &&
                      layout.panel.x + layout.panel.w <= width && layout.panel.y + layout.panel.h <= height,
                  "the help card stays on the surface");
        }
    }
    // At the smallest hosted surface the lamps must still be comfortable targets.
    const pl::Layout small = pl::compute_layout(pl::minimum_width, pl::minimum_height);
    const pl::Box cell = pl::cell_box(small, pl::maximum_side, 0);
    check(cell.w >= 36, "at 600 x 370 the smallest lamp is still at least 36 points across");
    check(small.compact, "600 x 370 uses the compact layout");
    // Picking: the centre of each cell picks it; the gap between lamps picks nothing.
    const pl::Layout layout = pl::compute_layout(900, 700);
    for (int cell_index = 0; cell_index < 16; ++cell_index) {
        const pl::Box box = pl::cell_box(layout, 4, cell_index);
        check(pl::pick_cell(layout, 4, box.x + box.w * .5, box.y + box.h * .5) == cell_index,
              "the centre of a lamp picks that lamp");
    }
    const pl::Box first = pl::cell_box(layout, 4, 0);
    check(pl::pick_cell(layout, 4, first.x + 1, first.y + 1) == -1, "a cell's corner is not a target");
    check(pl::pick_cell(layout, 4, -5, -5) == -1, "outside the board picks nothing");
}

// The animation must come to rest, or the game would never go idle.
void test_settles() {
    pl::Board board = pl::new_game(6, 5);
    pl::Visual visual;
    pl::snap(visual, board);
    static_cast<void>(pl::press(board, 14));
    int frames = 0;
    while (pl::advance(visual, board, 1.0 / 60, false)) {
        ++frames;
        if (frames > 600) {
            break;
        }
    }
    check(frames > 2 && frames < 120, "a press animates briefly and then settles");
    check(!pl::advance(visual, board, 1.0 / 60, false), "a settled picture stays settled");
    for (std::size_t index = 0; index < board.lit.size(); ++index) {
        check(visual.glow[index] == (board.lit[index] != 0 ? 1.0 : 0.0), "a settled lamp sits exactly on its value");
    }
    static_cast<void>(pl::press(board, 2));
    check(pl::advance(visual, board, 1.0 / 60, true), "reduced motion applies the change at once");
    check(!pl::advance(visual, board, 1.0 / 60, true), "and is then settled, with no easing");
}

// A reaction to a pause is one scheduled event, never a reason to keep ticking.
void test_remark() {
    pl::Board board = pl::new_game(4, 3);
    pl::Visual visual;
    pl::snap(visual, board);
    check(pl::seconds_until_remark(visual, board, 0) == pl::remark_after_seconds, "the remark is due after the wait");
    check(!pl::update_remark(visual, board, pl::remark_after_seconds - 1), "nothing is said early");
    check(pl::update_remark(visual, board, pl::remark_after_seconds + .5), "the remark is made when due");
    check(pl::seconds_until_remark(visual, board, 999) < 0, "and then nothing more is pending");
    check(!pl::update_remark(visual, board, 999), "it is made once");
    check(pl::note_input(visual) && !visual.remarked, "the player's next action withdraws it");
    visual.help = true;
    check(pl::seconds_until_remark(visual, board, 999) < 0, "no remark while help is open");
}

}  // namespace

int main() {
    test_generation();
    test_moves();
    test_solvable();
    test_save();
    test_layout();
    test_settles();
    test_remark();
    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "Pocket Library: rules, save, layout, settling and timed remark passed\n";
    return 0;
}
