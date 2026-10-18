// Headless checks of the rules, the save and the layout. No window, no toolkit.
#include "rules.hpp"
#include "save.hpp"
#include "stage.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAILED: " << what << "\n";
        ++failures;
    }
}

// The solution is a Graeco-Latin square: every row and column holds each shape and each
// material once, and every stone is used once.
bool graeco_latin(const sc::Puzzle& p) {
    const int n = p.n;
    std::vector<int> pairs(static_cast<std::size_t>(n * n), 0);
    for (int i = 0; i < n; ++i) {
        int row_shape = 0, row_material = 0, column_shape = 0, column_material = 0;
        for (int j = 0; j < n; ++j) {
            row_shape |= 1 << p.shape[static_cast<std::size_t>(i * n + j)];
            row_material |= 1 << p.material[static_cast<std::size_t>(i * n + j)];
            column_shape |= 1 << p.shape[static_cast<std::size_t>(j * n + i)];
            column_material |= 1 << p.material[static_cast<std::size_t>(j * n + i)];
            ++pairs[static_cast<std::size_t>(sc::solution_stone(p, i * n + j))];
        }
        const int all = (1 << n) - 1;
        if (row_shape != all || row_material != all || column_shape != all || column_material != all)
            return false;
    }
    for (int count : pairs)
        if (count != 1)
            return false;
    return true;
}

void test_generation() {
    for (int n : sc::sizes) {
        double slowest = 0;
        int fewest = 99;
        int most = 0;
        for (std::uint64_t seed = 1; seed <= 24; ++seed) {
            const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            const sc::Puzzle p = sc::make_puzzle(seed * 7919, n);
            slowest = std::max(slowest, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
            check(graeco_latin(p), "the solution is a Graeco-Latin square");
            std::array<int, sc::most_stones> given{};
            given.fill(-1);
            int clues = 0;
            for (int cell = 0; cell < n * n; ++cell)
                if (p.clue[static_cast<std::size_t>(cell)]) {
                    given[static_cast<std::size_t>(cell)] = sc::solution_stone(p, cell);
                    ++clues;
                }
            check(sc::count_solutions(p, given, 2) == 1, "the clues allow exactly one answer");
            fewest = std::min(fewest, clues);
            most = std::max(most, clues);
            const sc::Puzzle again = sc::make_puzzle(seed * 7919, n);
            check(again.shape == p.shape && again.material == p.material && again.clue == p.clue &&
                      again.family_seed == p.family_seed && again.material_kind == p.material_kind,
                  "the same seed makes the same cabinet");
            // n different materials and every style before any repeats.
            for (int a = 0; a < n; ++a)
                for (int b = a + 1; b < n; ++b) {
                    check(p.material_kind[static_cast<std::size_t>(a)] != p.material_kind[static_cast<std::size_t>(b)],
                          "the materials differ");
                    if (b < sc::style_choices)
                        check(p.family_style[static_cast<std::size_t>(a)] != p.family_style[static_cast<std::size_t>(b)],
                              "each style once before any repeats");
                }
        }
        std::printf("%d x %d: %d to %d clues, slowest %.3f s\n", n, n, fewest, most, slowest);
    }
}

void test_play() {
    sc::Session s;
    sc::new_game(s, 4242, 5);
    const sc::Puzzle& p = s.puzzle;
    int clues = 0;
    for (int cell = 0; cell < 25; ++cell)
        if (p.clue[static_cast<std::size_t>(cell)]) {
            ++clues;
            check(sc::stone_in(s, cell) == sc::solution_stone(p, cell), "a clue starts in its socket");
            check(!sc::movable(s, sc::stone_in(s, cell)), "a clue is pinned");
        }
    check(static_cast<int>(s.pile.size()) == sc::pile_size, "the pile starts full");
    check(static_cast<int>(s.drum.size()) == 25 - clues - sc::pile_size, "the rest wait in the tumbler");
    // A stone from the pile to a socket: the tumbler drops the next.
    const int first = s.pile.front();
    int empty = -1;
    for (int cell = 0; cell < 25 && empty < 0; ++cell)
        if (sc::stone_in(s, cell) < 0)
            empty = cell;
    const int waiting = s.drum.front();
    sc::Outcome o = sc::set_in_cabinet(s, first, empty);
    check(o.accepted && sc::stone_in(s, empty) == first, "a stone is set in a socket");
    check(o.dropped.size() == 1 && o.dropped[0] == waiting && s.pile.back() == waiting, "the tumbler drops the next stone");
    check(s.moves == 1, "setting a stone in a socket is a move");
    // A stone from the pile onto that socket: the two trade places.
    const int second = s.pile.front();
    o = sc::set_in_cabinet(s, second, empty);
    check(o.accepted && o.displaced == first && sc::stone_in(s, empty) == second &&
              s.where[static_cast<std::size_t>(first)].place == sc::Place::pile,
          "a stone set on an occupied socket trades places with the one there");
    // Onto the felt: free, and no move.
    o = sc::set_on_felt(s, first, .3, .6);
    check(o.accepted && s.where[static_cast<std::size_t>(first)].place == sc::Place::felt && s.moves == 2,
          "a stone laid on the felt costs nothing");
    // A clue's socket refuses.
    int clue_cell = -1;
    for (int cell = 0; cell < 25 && clue_cell < 0; ++cell)
        if (p.clue[static_cast<std::size_t>(cell)])
            clue_cell = cell;
    check(!sc::set_in_cabinet(s, first, clue_cell).accepted, "a clue's socket refuses another stone");
    // A stone in the tumbler cannot be taken.
    if (!s.drum.empty())
        check(!sc::movable(s, s.drum.front()), "a stone in the tumbler cannot be taken");

    // Every stone where the solution has it: solved, the record kept.
    sc::Session solved;
    sc::new_game(solved, 99, 4);
    bool won = false;
    bool best = false;
    int guard = 0;
    while (!solved.won && guard++ < 200) {
        for (int cell = 0; cell < 16; ++cell) {
            const int stone = sc::solution_stone(solved.puzzle, cell);
            if (sc::movable(solved, stone) && sc::stone_in(solved, cell) != stone) {
                const sc::Outcome r = sc::set_in_cabinet(solved, stone, cell);
                won = won || r.won;
                best = best || r.new_best;
            }
        }
    }
    check(solved.won && won && best, "the cabinet filled correctly is solved, and its record kept");
    check(sc::clashes(solved).empty(), "a solved cabinet has no clashes");
    check(solved.best[4].moves == solved.moves, "the record holds the moves");

    // A clash: two stones of one shape in a row.
    sc::Session c;
    sc::new_game(c, 7, 4);
    int a = -1;
    int b = -1;
    for (int stone : c.pile)
        for (int other : c.pile)
            if (stone != other && sc::stone_shape(c.puzzle, stone) == sc::stone_shape(c.puzzle, other) && a < 0) {
                a = stone;
                b = other;
            }
    if (a >= 0) {
        int row_empty[2] = {-1, -1};
        for (int r = 0; r < 4 && row_empty[1] < 0; ++r) {
            int found = 0;
            int cells[2] = {-1, -1};
            for (int col = 0; col < 4 && found < 2; ++col)
                if (sc::stone_in(c, r * 4 + col) < 0)
                    cells[found++] = r * 4 + col;
            if (found == 2) {
                row_empty[0] = cells[0];
                row_empty[1] = cells[1];
            }
        }
        if (row_empty[1] >= 0) {
            static_cast<void>(sc::set_in_cabinet(c, a, row_empty[0]));
            static_cast<void>(sc::set_in_cabinet(c, b, row_empty[1]));
            const std::vector<int> bad = sc::clashes(c);
            check(std::find(bad.begin(), bad.end(), row_empty[0]) != bad.end(), "two of one shape in a row clash");
        }
    }
}

void test_save() {
    sc::Session s;
    sc::new_game(s, 31337, 7);
    static_cast<void>(sc::set_on_felt(s, s.pile.front(), .25, .75));
    s.best[5] = sc::Best{30, 600};
    const std::string body = sc::encode_session(s);
    sc::Session back;
    check(sc::decode_session(body, back), "a session reloads");
    check(sc::encode_session(back) == body, "and is the same session");
    std::string body_sealed = sc::seal(body);
    std::string unsealed;
    check(sc::unseal(body_sealed, unsealed) && unsealed == body, "the envelope keeps the body");
    body_sealed[body_sealed.size() / 2] ^= 1;
    check(!sc::unseal(body_sealed, unsealed), "a damaged save is refused");
    // Damaged bodies: a stone twice, a clue moved, a stone missing.
    std::string twice = body;
    const std::size_t at = twice.find("stone=1,");
    twice.replace(at, 8, "stone=0,");
    sc::Session untouched;
    check(!sc::decode_session(twice, untouched), "a stone listed twice is refused");
    check(!sc::decode_session("version=1\nn=6\nseed=1\n", untouched), "a 6 x 6 is refused");
    // A square that is not Graeco-Latin: two of its sockets swapped.
    std::string swapped = body;
    const std::size_t square = swapped.find("square=") + 7;
    const std::size_t comma = swapped.find(',', square);
    const std::string first = swapped.substr(square, comma - square);
    const std::size_t second_end = swapped.find(',', comma + 1);
    const std::string second = swapped.substr(comma + 1, second_end - comma - 1);
    swapped.replace(square, second_end - square, second + "," + first);
    check(first == second || !sc::decode_session(swapped, untouched), "a square that breaks the rule is refused");
    check(!sc::decode_session("", untouched), "an empty body is refused");
}

void test_layout() {
    const double sizes[][2] = {{1100, 760}, {600, 370}, {600, 320}, {1600, 1000}, {900, 600}};
    for (int n : sc::sizes)
        for (const double* size : sizes) {
            const sc::Layout l = sc::compute_layout(size[0], size[1], n);
            const sc::Box screen{0, 0, size[0], size[1]};
            check(l.cabinet.x >= 0 && l.cabinet.y >= sc::capsule_clearance - 1 && l.cabinet.x + l.cabinet.w <= size[0] &&
                      l.cabinet.y + l.cabinet.h <= size[1],
                  "the cabinet is on the surface, below the capsule");
            check(l.felt.x + l.felt.w <= size[0] + .5 && l.felt.y + l.felt.h <= size[1] + .5, "the felt is on the surface");
            check(!sc::overlap(l.cabinet, l.felt), "the cabinet and the felt are apart");
            check(l.mouth_x > l.felt.x && l.mouth_x < l.felt.x + l.felt.w, "the tumbler's mouth is over the felt");
            for (int k = 0; k < sc::pile_size; ++k)
                check(sc::inside(l.felt, l.pile[static_cast<std::size_t>(k * 2)], l.pile[static_cast<std::size_t>(k * 2 + 1)]),
                      "the pile lies on the felt");
            check(sc::inside(screen, l.panel.x, l.panel.y), "the panel is on the surface");
            for (int cell = 0; cell < n * n; ++cell) {
                const sc::Box b = sc::cell_box(l, cell);
                check(sc::pick_cell(l, b.x + b.w * .5, b.y + b.h * .5) == cell, "a socket's middle picks it");
            }
            double x = 0, y = 0, fx = 0, fy = 0;
            sc::felt_point(l, .3, .7, x, y);
            sc::felt_fraction(l, x, y, fx, fy);
            check(std::abs(fx - .3) < 1e-9 && std::abs(fy - .7) < 1e-9, "a felt point goes there and back");
        }
}

}  // namespace

int main() {
    test_generation();
    test_play();
    test_save();
    test_layout();
    std::printf("%d checks failed\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
