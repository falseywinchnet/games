#include "puzzles.hpp"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <set>
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
// A Puzzle Solve game saved by the original seven-piece version, with two pieces placed.
static const char* legacy_solve_save = R"save(RAINSTAR_PUZZLE 1 709093480120075517
6 42 3245049828 2 0 0 0 0 0 0
"Player"
0 1 0 0
0 1 0 0
0 2 0 0
0 2 0 0
0 1 0 0
0 1 0 0
0 1 0 0
0 1 0 0
0 1 0 0
0 1 0 0
0 1 0 0
0 1 0 0
0 1 0 0
0 1 0 0
0 1 0 0
0 1 0 0
0 2 0 0
0 2 0 0
0 2 0 0
0 2 0 0
0 1 0 0
0 1 0 0
0 2 0 0
0 2 0 0
0 1 0 0
10 2 0 0
10 2 0 0
0 1 0 0
0 1 0 0
0 1 0 0
0 1 0 0
0 1 0 0
0 2 0 0
0 2 0 0
0 2 0 0
0 2 0 0
0 2 0 0
6 2 0 0
6 2 0 0
0 2 0 0
10 2 0 0
10 2 0 0
5 1 0 0
5 1 0 0
0 1 0 0
0 2 0 0
0 2 0 0
0 1 0 0
0 2 0 4
0 2 0 3
0 2 0 5
0 2 0 0
6 2 0 1
6 2 0 2
0 2 0 6
0 2 0 0
5 1 0 0
0 2 0 0
0 2 0 0
5 1 0 0
0 2 0 0
0 2 0 0
0 2 0 0
0 2 0 0
0 0 0 0
0 0 0 0
0 0 0 0
0 0 0 3
0 0 0 0
0 0 0 1
0 0 0 3
0 0 0 0
0 0 0 0
0 0 0 0
0 0 0 0
0 0 0 0
0 0 0 0
0 0 0 0
0 0 0 0
0 0 0 0
0 0 0 2
0 0 0 1
0 0 0 2
0 0 0 2
0 0 0 2
0 0 0 1
0 0 0 2
0 0 0 0
0 0 0 0
0 0 0 0
0 0 0 0
0 0 0 0
0 0 0 0
0 0 0 0
0 0 0 0
0 0 0 3
0
0
0
0
7
5 0 1 2 0 0
5 1 2 1 0 0
5 2 3 0 0 0
5 3 0 0 0 0
5 4 0 0 0 0
5 5 2 2 0 0
5 6 0 3 0 0
0
)save";
static void solve_fixtures() {
    // The original tangram saves keep loading and playing as before.
    {
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "rainstar-puzzle-solve-legacy.txt";
        {
            std::ofstream file(path);
            file << legacy_solve_save;
        }
        PuzzleGame legacy(PuzzleKind::solve);
        assert(legacy.load(path));
        assert(legacy.state.aux[95] == 3 && legacy.piece_count() == 7);
        assert(legacy.solve_columns() == 4 && legacy.solve_rows() == 4 && legacy.solve_level() == 1);
        int placed = 0;
        for (int i = 0; i < 64; ++i)
            placed += legacy.state.grid[i] != 0;
        assert(placed > 0);
        assert(legacy.solve_frame().size() == 4);
        for (const std::vector<int>& w : legacy.state.solution_paths)
            assert(legacy.place_piece(w[0], w[1], w[2], w[3], w[4]));
        assert(legacy.state.won);
        assert(legacy.save(path));
        PuzzleGame again(PuzzleKind::solve);
        assert(again.load(path) && again.state.aux[95] == 3);
    }
    // Generated games: every level, many seeds. Each is a real tiling whose witness rebuilds
    // the picture through the rules, and each level keeps its promises.
    std::set<int> frames[3], families[3], counts[3], lessons;
    for (int level = 0; level < 3; ++level)
        for (int seed = 1; seed <= 400; ++seed) {
            PuzzleGame game(PuzzleKind::solve);
            game.level = level;
            game.deal(static_cast<std::uint32_t>(seed * 2654435761u));
            assert(game.state.aux[95] == 4 && game.invariant());
            assert(game.solve_level() == level);
            const int count = game.piece_count();
            frames[level].insert(game.state.aux[90]);
            families[level].insert(game.solve_family());
            counts[level].insert(count);
            if (level == 0) {
                assert(count >= 4 && count <= 5 && game.solve_lesson() >= 0);
                lessons.insert(game.solve_lesson());
            } else {
                assert(game.solve_lesson() == -1);
                assert(level == 1 ? count >= 6 && count <= 8 : count >= 9 && count <= 12);
            }
            int yellow = 0, blue = 0, flips = 0, slanted = 0;
            for (int i = 0; i < game.solve_atoms(); ++i) {
                yellow += game.state.secret[i] == 2;
                blue += game.state.secret[i] == 1;
            }
            assert(yellow > 0 && blue > 0);
            for (int p = 0; p < count; ++p) {
                const std::vector<int>& w = game.state.solution_paths[static_cast<std::size_t>(p)];
                flips += w[4];
                const std::vector<PieceCell> cells = game.piece_cells(p, 0, false);
                // One color per piece, at least one cell's area, one simply connected outline.
                assert(cells.size() >= 4);
                for (const PieceCell& c : cells)
                    assert(c.color == cells[0].color);
                const std::vector<Point2> outline = solve_outline(cells);
                assert(outline.size() >= 3);
                bool rectilinear = true;
                for (std::size_t i = 0; i < outline.size(); ++i) {
                    const Point2 a = outline[i], b = outline[(i + 1) % outline.size()];
                    rectilinear = rectilinear && (a.x == b.x || a.y == b.y);
                }
                slanted += rectilinear ? 0 : 1;
                // Tans are triangles and four-sided pieces.
                if (game.solve_family() == 1 || game.solve_family() == 2)
                    assert(outline.size() <= 4);
                if (game.solve_family() == 0)
                    assert(rectilinear);
            }
            if (game.solve_lesson() == 0)
                assert(flips == 0);
            if (game.solve_lesson() == 1)
                assert(flips > 0);
            if (game.solve_lesson() == 2)
                assert(slanted >= 2);
            // Nothing may be placed outside the frame, or over another piece.
            PuzzleGame trial = game;
            for (int p = 0; p < count; ++p)
                for (int x = -1; x <= game.solve_columns(); ++x)
                    for (int y = -1; y <= game.solve_rows(); ++y)
                        for (int r = 0; r < 4; ++r)
                            if (trial.place_piece(p, x, y, r, false)) {
                                for (int i = 0; i < 96; ++i)
                                    assert(!trial.state.grid[i] || trial.solve_inside(i));
                                assert(trial.remove_piece(p));
                            }
            // The witness solves it, and a save keeps the shapes, the picture and the moves.
            for (const std::vector<int>& w : game.state.solution_paths) {
                assert(!game.state.won);
                assert(game.place_piece(w[0], w[1], w[2], w[3], w[4]));
            }
            assert(game.state.won && game.state.over && game.state.moves == count);
            const std::filesystem::path path =
                std::filesystem::temp_directory_path() / "rainstar-puzzle-solve-test.txt";
            assert(game.save(path));
            PuzzleGame restored(PuzzleKind::solve);
            assert(restored.load(path));
            assert(restored.state.grid == game.state.grid &&
                   restored.state.secret == game.state.secret &&
                   restored.state.solution_paths == game.state.solution_paths &&
                   restored.state.won);
            // A damaged witness is refused.
            PuzzleGame damaged = restored;
            damaged.state.solution_paths[0][1] += 1;
            assert(!damaged.invariant());
        }
    // Each level draws on several frames and families; Easy teaches all three lessons.
    assert(frames[0].size() >= 4 && frames[1].size() >= 5 && frames[2].size() >= 6);
    assert(families[1].size() >= 3 && families[2].size() >= 2 && lessons.size() == 3);
    assert(counts[1].size() == 3 && counts[2].size() == 4);
    // Shapes and orientation: a quarter turn of a quarter turn of ... is the identity.
    const std::vector<PieceCell> l_shape{{0, 0, 1, 0}, {0, 1, 1, 1}, {1, 1, 1, 2}};
    for (bool flip : {false, true}) {
        std::vector<PieceCell> turned = solve_transform(l_shape, 0, flip);
        const std::vector<PieceCell> start = turned;
        for (int r = 0; r < 4; ++r)
            turned = solve_transform(turned, 1, false);
        assert(turned.size() == start.size());
        for (std::size_t i = 0; i < start.size(); ++i)
            assert(turned[i].x == start[i].x && turned[i].y == start[i].y &&
                   turned[i].wedge == start[i].wedge);
    }
    // Two cells touching only at a corner have no single outline.
    assert(solve_outline({{0, 0, 1, 0}, {0, 0, 1, 1}, {0, 0, 1, 2}, {0, 0, 1, 3}, {1, 1, 1, 0},
                          {1, 1, 1, 1}, {1, 1, 1, 2}, {1, 1, 1, 3}})
               .empty());
    // A whole cell is a square.
    assert(solve_outline({{2, 3, 1, 0}, {2, 3, 1, 1}, {2, 3, 1, 2}, {2, 3, 1, 3}}).size() == 4);
}
int main() {
    special_fixtures();
    ray_fixtures();
    solve_fixtures();
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
            game.level = seed % 3;
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
                // Five, seven or nine pairs; stones from Medium up; the solution fills the cube.
                assert(game.cube_pairs() == 5 + 2 * (seed % 3));
                int stones = 0;
                for (int cell = 0; cell < 96; ++cell)
                    stones += game.cube_rock(cell);
                assert(stones == (seed % 3 == 0 ? 0 : seed % 3 == 1 ? 3 : 5));
                assert(game.cube_open_tiles() == 0 && game.cube_fill() == (seed % 3 == 2));
            }
            if (game.kind == PuzzleKind::untangle) {
                assert(game.crossings() > 0);
                // Ten, thirteen or sixteen pegs; one more yarn color and more frozen pegs per
                // level.
                const int level = seed % 3;
                assert(static_cast<int>(game.state.nodes.size()) == 10 + 3 * level);
                assert(game.untangle_layers() == level + 1);
                int frozen = 0;
                for (int i = 0; i < static_cast<int>(game.state.nodes.size()); ++i)
                    if (game.untangle_frozen(i)) {
                        ++frozen;
                        // Frozen pegs start where the solution has them, and refuse to move.
                        assert(game.state.nodes[i].x == game.state.embedding[i].x &&
                               game.state.nodes[i].y == game.state.embedding[i].y);
                        PuzzleGame trial = game;
                        assert(!trial.move_node(i, {.5, .5}) && trial.state.moves == 0);
                    }
                assert(frozen == (level == 0 ? 0 : level == 1 ? 2 : 3));
                // The kitten's nudge is not the player's move.
                PuzzleGame nudged = game;
                for (int i = 0; i < static_cast<int>(nudged.state.nodes.size()); ++i)
                    if (!nudged.untangle_frozen(i)) {
                        assert(nudged.move_node(i, {.4, .41}, false) && nudged.state.moves == 0);
                        break;
                    }
                // Moving the free pegs home solves it and thaws every frozen peg.
                for (int i = 0; i < static_cast<int>(game.state.nodes.size()); ++i)
                    if (!game.untangle_frozen(i))
                        game.move_node(i, game.state.embedding[i]);
                assert(game.crossings() == 0 && game.state.won);
                for (int i = 0; i < static_cast<int>(game.state.nodes.size()); ++i)
                    assert(!game.untangle_frozen(i));
            }
            if (game.kind == PuzzleKind::pegs) {
                std::array<int, 4> c;
                for (int i = 0; i < 4; ++i)
                    c[i] = game.state.secret[i];
                assert(game.guess_pegs(c));
                assert(game.state.won);
            }
            if (game.kind == PuzzleKind::solve) {
                assert(game.solve_level() == seed % 3);
                int atoms = 0, frame = 0;
                for (int p = 0; p < game.piece_count(); ++p) {
                    std::vector<PieceCell> original = game.piece_cells(p, 0, false);
                    atoms += original.size();
                    for (int r = 0; r < 4; ++r)
                        for (bool f : {false, true})
                            assert(game.piece_cells(p, r, f).size() == original.size());
                }
                for (int i = 0; i < game.solve_atoms(); ++i)
                    frame += game.solve_inside(i) ? 1 : 0;
                assert(atoms == frame);
                for (const std::vector<int>& w : game.state.solution_paths)
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
