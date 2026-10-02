#include "puzzles.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
using namespace games;
static PegFeedback reference(std::array<int, 4> a, std::array<int, 4> b) {
    PegFeedback f;
    for (int i = 0; i < 4; ++i)
        if (a[i] == b[i]) {
            ++f.exact;
            a[i] = -1;
            b[i] = -2;
        }
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (a[i] == b[j]) {
                ++f.misplaced;
                a[i] = -1;
                b[j] = -2;
                break;
            }
    return f;
}
static std::array<int, 4> code(int n) {
    std::array<int, 4> c;
    for (int& v : c) {
        v = 1 + n % 6;
        n /= 6;
    }
    return c;
}
static PuzzleGame gem_fixture() {
    PuzzleGame g(PuzzleKind::gems);
    g.deal(7);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            g.state.grid[y * 8 + x] = 1 + (x + 2 * y) % 5;
    return g;
}
static void special_fixtures() {
    for (int length : {4, 5}) {
        PuzzleGame g = gem_fixture();
        for (int x = 1; x <= length; ++x)
            g.state.grid[24 + x] = 1;
        g.state.grid[27] = 2;
        g.state.grid[19] = 1;
        assert(g.gem_swap(19, 27));
        assert(g.cascade_frames.size() >= 3);
        assert(g.cascade_frames[1][27] == (length == 4 ? 17 : 48));
    }
    {
        PuzzleGame g = gem_fixture();
        g.state.grid[26] = g.state.grid[28] = g.state.grid[35] = g.state.grid[43] =
            g.state.grid[19] = 1;
        g.state.grid[27] = g.state.grid[51] = 2;
        assert(g.gem_swap(19, 27));
        assert(g.cascade_frames[1][27] == 33);
    }
    {
        PuzzleGame g = gem_fixture();
        g.state.grid[25] = 1;
        g.state.grid[26] = 17;
        g.state.grid[27] = 2;
        g.state.grid[19] = 1;
        assert(g.gem_swap(19, 27));
        for (int y = 2; y <= 4; ++y)
            for (int x = 1; x <= 3; ++x)
                assert(g.cascade_frames[1][y * 8 + x] == 0);
    }
    {
        PuzzleGame g = gem_fixture();
        g.state.grid[0] = g.state.grid[1] = 48;
        assert(g.gem_swap(0, 1));
        for (int i = 0; i < 64; ++i)
            assert(g.cascade_frames[1][i] == 0);
        assert(g.state.score >= 640);
    }
}
static bool atom_at(unsigned board, int x, int y) {
    return x >= 0 && x < 4 && y >= 0 && y < 4 && (board & (1u << (y * 4 + x)));
}
static int reference_ray(unsigned board, int entry) {
    const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
    int x = entry < 4    ? entry
            : entry < 8  ? 4
            : entry < 12 ? 11 - entry
                         : -1,
        y = entry < 4    ? -1
            : entry < 8  ? entry - 4
            : entry < 12 ? 4
                         : 15 - entry;
    int direction = entry < 4 ? 2 : entry < 8 ? 3 : entry < 12 ? 0 : 1;
    std::array<bool, 144> seen{};
    for (int step = 0; step < 256; ++step) {
        int id = ((y + 1) * 6 + x + 1) * 4 + direction;
        if (seen[id])
            return -2;
        seen[id] = true;
        int nx = x + dx[direction], ny = y + dy[direction];
        if (atom_at(board, nx, ny))
            return -1;
        bool left = atom_at(board, nx - dy[direction], ny + dx[direction]),
             right = atom_at(board, nx + dy[direction], ny - dx[direction]);
        if (left || right) {
            if (x < 0 || x >= 4 || y < 0 || y >= 4 || left && right)
                return -2;
            direction = (direction + (left ? 3 : 1)) % 4;
        } else {
            x = nx;
            y = ny;
            if (x < 0 || x >= 4 || y < 0 || y >= 4) {
                int exit = x < 0 ? 15 - y : x >= 4 ? 4 + y : y < 0 ? x : 11 - x;
                return exit == entry ? -2 : exit;
            }
        }
    }
    return -2;
}
static void ray_fixtures() {
    PuzzleGame g(PuzzleKind::atom);
    std::array<int, 96> empty{};
    for (int port = 0; port < 16; ++port) {
        int expected = port < 4    ? 11 - port
                       : port < 8  ? 19 - port
                       : port < 12 ? 11 - port
                                   : 19 - port;
        assert(g.atom_trace(port, empty) == expected);
    }
    for (int a = 0; a < 14; ++a)
        for (int b = a + 1; b < 15; ++b)
            for (int c = b + 1; c < 16; ++c) {
                std::array<int, 96> board{};
                board[a] = board[b] = board[c] = 1;
                unsigned mask = (1u << a) | (1u << b) | (1u << c);
                for (int port = 0; port < 16; ++port)
                    assert(g.atom_trace(port, board) == reference_ray(mask, port));
            }
}
int main() {
    special_fixtures();
    ray_fixtures();
    // Release builds must retain assertions in this executable.
    for (int a = 0; a < 1296; ++a)
        for (int b = 0; b < 1296; ++b) {
            PegFeedback x = PuzzleGame::evaluate_pegs(code(a), code(b)),
                        y = reference(code(a), code(b));
            assert(x.exact == y.exact && x.misplaced == y.misplaced);
        }
    for (int i = 0; i < 96; ++i) {
        int count = 0;
        for (int j = 0; j < 96; ++j)
            count += PuzzleGame::cube_adjacent(i, j);
        assert(count == 4);
    }
    for (int k = 0; k < 8; ++k)
        for (int seed = 1; seed <= 24; ++seed) {
            PuzzleGame game(static_cast<PuzzleKind>(k));
            game.deal(seed);
            assert(game.invariant());
            std::filesystem::path path = std::filesystem::temp_directory_path() /
                                         ("rainstar-puzzle-test-" + std::to_string(k) + ".txt");
            assert(game.save(path));
            PuzzleGame restored(game.kind);
            assert(restored.load(path));
            assert(restored.state.grid == game.state.grid &&
                   restored.state.secret == game.state.secret);
            if (game.kind == PuzzleKind::cube) {
                assert(game.cube_witness_valid());
                for (const std::vector<int>& p : game.state.solution_paths) {
                    assert(PuzzleGame::cube_playable(p.front()));
                    assert(game.cube_start(p.front()));
                    for (std::size_t n = 1; n < p.size(); ++n) {
                        assert(PuzzleGame::cube_playable(p[n]));
                        assert(game.cube_extend(p[n]));
                    }
                }
                assert(game.state.won);
            }
            if (game.kind == PuzzleKind::untangle) {
                assert(game.crossings() > 0);
                game.state.nodes = game.state.embedding;
                assert(game.crossings() == 0);
            }
            if (game.kind == PuzzleKind::pegs) {
                std::array<int, 4> c;
                for (int i = 0; i < 4; ++i)
                    c[i] = game.state.secret[i];
                assert(game.guess_pegs(c));
                assert(game.state.won);
            }
            if (game.kind == PuzzleKind::solve) {
                assert(game.piece_count() == 7);
                int atoms = 0;
                for (int p = 0; p < 7; ++p) {
                    auto original = game.piece_cells(p, 0, false);
                    atoms += original.size();
                    for (int r = 0; r < 4; ++r)
                        for (bool f : {false, true})
                            assert(game.piece_cells(p, r, f).size() == original.size());
                }
                assert(atoms == 64);
                for (const auto& w : game.state.solution_paths)
                    assert(game.place_piece(w[0], w[1], w[2], w[3], w[4]));
                assert(game.state.won);
            }
            if (game.kind == PuzzleKind::sticks) {
                for (int i = 0; i < 37; ++i)
                    assert(game.set_stick(i, game.state.secret[i]));
                assert(game.state.won);
            }
            if (game.kind == PuzzleKind::atom) {
                game.state.grid = game.state.secret;
                assert(game.submit_atoms() && game.state.won);
            }
            if (game.kind == PuzzleKind::gems) {
                assert(game.gem_has_move());
                for (int turn = 0; turn < 40 && !game.state.over; ++turn) {
                    bool found = false;
                    for (int a = 0; a < 64 && !found; ++a)
                        for (int d : {1, 8}) {
                            int b = a + d;
                            if (b >= 64 || a % 8 == 7 && d == 1)
                                continue;
                            if (game.gem_swap(a, b)) {
                                found = true;
                                break;
                            }
                        }
                    assert(found);
                    assert(game.invariant());
                }
            }
            if (game.state.won) {
                assert(game.qualifies());
                assert(game.record("Ada"));
                assert(!game.record("Ada"));
                assert(game.save(path));
                assert(restored.load(path));
                assert(restored.recorded && restored.scores.size() == 1);
            }
        }
    std::cout << "All 1,679,616 peg evaluations, 24 seeds per game, solution replays, and saves "
                 "passed.\n";
}
