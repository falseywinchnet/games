// Nature Cube's rules, solver, generator and saves. Portable: no toolkit, no window.
//   cube_rules_tests [rules|generator|tiers|saves <fixture directory>]
// With no argument every group except the tier sweep runs.
#include "generator.hpp"
#include "session.hpp"
#include "solver.hpp"
#include "stage.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>

namespace {

using namespace ps_cube;

void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

int cell_at(int side, int face, int row, int column) {
    const int cell = face * side * side + row * side + column;
    return cell;
}

// A four-cell-a-side board built by hand: pair 0 along the front's top row, pair 1 on
// the left face, a stone, and a portal pair between the top face and the left face.
Puzzle hand_board() {
    Puzzle puzzle;
    puzzle.side = 4;
    puzzle.ends.push_back({cell_at(4, 0, 0, 0), cell_at(4, 0, 0, 3)});
    puzzle.ends.push_back({cell_at(4, 1, 3, 0), cell_at(4, 2, 3, 3)});
    puzzle.portals.push_back({cell_at(4, 1, 2, 0), cell_at(4, 2, 2, 3)});
    const std::vector<int> stones{cell_at(4, 0, 2, 2)};
    puzzle.witness.push_back({cell_at(4, 0, 0, 0), cell_at(4, 0, 0, 1), cell_at(4, 0, 0, 2),
                              cell_at(4, 0, 0, 3)});
    puzzle.witness.push_back({cell_at(4, 1, 3, 0), cell_at(4, 1, 2, 0), cell_at(4, 2, 2, 3),
                              cell_at(4, 2, 3, 3)});
    require(assemble(puzzle, stones), "the hand-built board assembles");
    return puzzle;
}

void test_geometry() {
    for (int side = minimum_side; side <= maximum_side; ++side) {
        const Geometry geometry = make_geometry(side);
        require(geometry.cells == 3 * side * side, "three faces of side x side cells");
        int links = 0;
        for (int a = 0; a < geometry.cells; ++a) {
            const int count = geometry.neighbour_count[static_cast<std::size_t>(a)];
            require(count >= 2 && count <= 4, "every cell has two to four neighbours");
            links += count;
            for (int b = 0; b < geometry.cells; ++b) {
                require(adjacent(geometry, a, b) == adjacent(geometry, b, a), "adjacency is symmetric");
            }
        }
        // Each face has 2 * side * (side - 1) inner links; three folds join side cells each.
        require(links == 2 * (3 * 2 * side * (side - 1) + 3 * side), "every link, including the folds");
    }
    // The front's top row meets the top face's bottom row across the fold.
    const Geometry four = make_geometry(4);
    require(adjacent(four, cell_at(4, 0, 0, 1), cell_at(4, 2, 3, 1)), "front and top meet");
    require(adjacent(four, cell_at(4, 0, 2, 0), cell_at(4, 1, 2, 3)), "front and left meet");
    require(adjacent(four, cell_at(4, 1, 0, 2), cell_at(4, 2, 2, 0)), "left and top meet");
}

void test_rules() {
    const Puzzle puzzle = hand_board();
    require(witness_valid(puzzle), "the hand witness plays through the rules");
    Play play = fresh_play(puzzle);
    const Play untouched = play;
    // Nothing happens on an open cell, a stone or a portal before a line exists.
    require(!press(puzzle, play, cell_at(4, 0, 1, 1)), "an open cell does not start a line");
    require(!press(puzzle, play, cell_at(4, 0, 2, 2)), "a stone does not start a line");
    require(play.paths == untouched.paths && play.strokes == 0, "refused presses change nothing");
    require(press(puzzle, play, cell_at(4, 0, 0, 0)) && play.active == 0 && play.strokes == 1,
            "an endpoint starts its line");
    require(!extend(puzzle, play, cell_at(4, 0, 0, 2)), "a line steps only to a neighbour");
    require(extend(puzzle, play, cell_at(4, 0, 1, 0)), "a step down");
    require(extend(puzzle, play, cell_at(4, 0, 1, 1)), "a step right");
    require(extend(puzzle, play, cell_at(4, 0, 0, 1)), "a step up");
    require(extend(puzzle, play, cell_at(4, 0, 1, 0)), "tracing back cuts the line");
    require(play.paths[0].size() == 2, "the line was cut back to the cell");
    require(extend(puzzle, play, cell_at(4, 0, 2, 0)) && extend(puzzle, play, cell_at(4, 0, 2, 1)),
            "the line runs on");
    require(!extend(puzzle, play, cell_at(4, 0, 2, 2)), "a stone refuses the line");
    // Across the fold onto the left face, then into another pair's endpoint: refused.
    require(extend(puzzle, play, cell_at(4, 0, 3, 1)) && extend(puzzle, play, cell_at(4, 0, 3, 0)),
            "down to the bottom row");
    require(extend(puzzle, play, cell_at(4, 1, 3, 3)), "the line folds over the edge");
    require(!press(puzzle, play, cell_at(4, 1, 2, 2)), "an untouched open cell is not a line");
    // Start pair 1 and take the portal.
    require(press(puzzle, play, cell_at(4, 1, 3, 0)) && play.active == 1, "pair 1 starts");
    require(extend(puzzle, play, cell_at(4, 1, 2, 0)), "the line enters the portal");
    require(play.paths[1].size() == 3 && play.paths[1].back() == cell_at(4, 2, 2, 3),
            "and comes out of its partner");
    require(portal_owner(puzzle, play, 0) == 1, "the portal pair takes the line's colour");
    require(extend(puzzle, play, cell_at(4, 2, 3, 3)) && complete(puzzle, play.paths[1], 1),
            "pair 1 is joined through the portal");
    require(!play.won, "pair 0 is still open");
    // Pair 0 cannot use the claimed portal.
    require(press(puzzle, play, cell_at(4, 0, 0, 0)), "pair 0 starts again");
    Play before = play;
    require(!extend(puzzle, play, cell_at(4, 0, 0, 0)), "the head is not a step");
    require(play.paths == before.paths, "a refused step changes nothing");
    // Erasing pair 1 frees the portal.
    require(erase(play, 1) && portal_owner(puzzle, play, 0) == -1, "erasing frees the portal");
    // A line entering a portal and cut back over its entry loses the hop.
    require(press(puzzle, play, cell_at(4, 1, 3, 0)) && extend(puzzle, play, cell_at(4, 1, 2, 0)),
            "pair 1 hops again");
    require(extend(puzzle, play, cell_at(4, 1, 3, 0)) && play.paths[1].size() == 1,
            "tracing back to the start removes the hop");
    require(extend(puzzle, play, cell_at(4, 1, 2, 0)) && extend(puzzle, play, cell_at(4, 2, 1, 3)),
            "through the portal and on");
    require(press(puzzle, play, cell_at(4, 1, 2, 0)) && play.paths[1].size() == 3 &&
                play.paths[1].back() == cell_at(4, 2, 2, 3),
            "a press on the portal it entered continues from the far side");
    // A path may never pass over a portal as an ordinary cell.
    std::vector<int> through{cell_at(4, 1, 3, 0), cell_at(4, 1, 2, 0), cell_at(4, 1, 1, 0)};
    require(!path_legal(puzzle, through, 1), "a portal is not an ordinary cell");
    std::vector<int> ends_on_portal{cell_at(4, 1, 3, 0), cell_at(4, 1, 2, 0)};
    require(!path_legal(puzzle, ends_on_portal, 1), "a line cannot stop inside a portal");
    // Finish both: the board is won without covering every open cell.
    Play finish = fresh_play(puzzle);
    for (std::size_t pair = 0; pair < puzzle.witness.size(); ++pair) {
        require(press(puzzle, finish, puzzle.witness[pair].front()), "press the witness start");
        for (std::size_t index = 1; index < puzzle.witness[pair].size(); ++index) {
            if (finish.paths[pair].size() <= index) {
                require(extend(puzzle, finish, puzzle.witness[pair][index]), "follow the witness");
            }
        }
    }
    require(finish.won && play_legal(puzzle, finish), "every pair joined wins");
    int covered = 0;
    for (const std::vector<int>& line : finish.paths) {
        covered += static_cast<int>(line.size());
    }
    require(covered < puzzle.geometry.cells - 1, "most of the board is still open");
    require(!press(puzzle, finish, cell_at(4, 0, 0, 0)), "a won board takes no presses");
}

void test_solver() {
    const Puzzle puzzle = hand_board();
    SolveLimits limits;
    limits.solution_cap = 100000;
    limits.node_budget = 5000000;
    const SolveResult all = solve(puzzle, limits);
    require(all.exact && all.solutions > 1, "the hand board has several solutions");
    require(all.first.size() == 2, "a solution has a line per pair");
    // Every solution found plays through the rules.
    Play play;
    require(replay(puzzle, all.first, 2, play) && play.won, "the solver's solution is legal");
    // Without the portal pair 1 can still walk round, so the portal is not needed here.
    SolveLimits plain = limits;
    plain.portals_as_stones = true;
    const SolveResult without = solve(puzzle, plain);
    require(without.exact && without.solutions >= 1, "the hand board is solvable without the portal");
    require(without.solutions < all.solutions, "the portal adds routes");
    // Wall pair 1 in: only the portal joins it.
    Puzzle walled = puzzle;
    std::vector<int> stones{cell_at(4, 0, 2, 2), cell_at(4, 1, 3, 1), cell_at(4, 1, 2, 1),
                            cell_at(4, 2, 3, 2), cell_at(4, 2, 2, 2), cell_at(4, 0, 3, 0),
                            cell_at(4, 2, 1, 3), cell_at(4, 1, 1, 0)};
    walled.witness[1] = puzzle.witness[1];
    require(assemble(walled, stones) && witness_valid(walled), "the walled board is valid");
    const Metrics metrics = measure(walled, limits);
    require(metrics.portal_needed && metrics.solutions >= 1, "the portal is needed");
    require(metrics.crossings == 1, "the hop counts as a crossing");
}

void test_generator() {
    for (int level = 0; level < levels; ++level) {
        const Tier spec = tier(level);
        for (std::uint64_t seed = 1; seed <= (level == 2 ? 2u : 6u); ++seed) {
            const Puzzle puzzle = generate(level, seed * 7919, GenerateOptions{});
            require(witness_valid(puzzle), "every generated board carries a working witness");
            require(puzzle.side == spec.side && puzzle.level == level, "the board has its level's size");
            require(static_cast<int>(puzzle.ends.size()) == spec.pairs, "the level's number of pairs");
            const Puzzle again = generate(level, seed * 7919, GenerateOptions{});
            require(again.tiles == puzzle.tiles && again.ends == puzzle.ends &&
                        again.portals == puzzle.portals && again.witness == puzzle.witness,
                    "the same seed deals the same board");
            for (std::size_t pair = 0; pair < puzzle.ends.size(); ++pair) {
                require(!adjacent(puzzle.geometry, puzzle.ends[pair][0], puzzle.ends[pair][1]),
                        "no pair starts joined");
            }
            if (level == 0) {
                require(puzzle.portals.empty(), "Easy has no portals");
            }
        }
    }
    // A player's first portal board is gentle: one portal pair, right beside an endpoint
    // of the line that needs it.
    for (int level = 1; level < levels; ++level) {
        GenerateOptions gentle;
        gentle.gentle_portal = true;
        const Puzzle puzzle = generate(level, 4242, gentle);
        require(witness_valid(puzzle) && puzzle.portals.size() == 1, "a gentle board has one portal pair");
        bool beside = false;
        for (std::size_t pair = 0; pair < puzzle.ends.size(); ++pair) {
            for (int end : puzzle.ends[pair]) {
                for (int cell : puzzle.portals[0]) {
                    beside = beside || adjacent(puzzle.geometry, end, cell);
                }
            }
        }
        require(beside, "the first portal stands beside an endpoint");
        SolveLimits limits;
        limits.node_budget = 400000;
        const Metrics metrics = measure(puzzle, limits);
        require(metrics.portal_needed, "and the board needs it");
    }
}

struct Average {
    double solutions = 0;
    double backtracks = 0;
    double length = 0;
    double crossings = 0;
    double forced = 0;
    double detour = 0;
};

Average sample_level(int level, int boards) {
    Average average;
    for (int index = 0; index < boards; ++index) {
        const Puzzle puzzle = generate(level, 50000 + static_cast<std::uint64_t>(index) * 31, GenerateOptions{});
        require(witness_valid(puzzle), "the tier sample has witnesses");
        SolveLimits limits;
        limits.node_budget = 400000;
        limits.solution_cap = 200;
        const Metrics metrics = measure(puzzle, limits);
        if (level == 2) {
            require(metrics.solutions == 1 && metrics.solutions_exact, "every Hard board has one solution");
            require(puzzle.portals.empty() || metrics.portal_needed, "a Hard portal is always needed");
        }
        average.solutions += metrics.solutions;
        average.backtracks += static_cast<double>(metrics.backtracks);
        average.length += metrics.total_length;
        average.crossings += metrics.crossings;
        average.forced += metrics.forced_starts;
        average.detour += metrics.detour;
    }
    average.solutions /= boards;
    average.backtracks /= boards;
    average.length /= boards;
    average.crossings /= boards;
    average.forced /= boards;
    average.detour /= boards;
    std::printf("level %d: solutions %.1f, wrong turns %.1f, witness cells %.1f, folds and hops %.1f, "
                "forced starts %.2f, detour %.1f\n",
                level, average.solutions, average.backtracks, average.length, average.crossings,
                average.forced, average.detour);
    return average;
}

// Each level is measurably harder than the one before, by the solver's measures,
// averaged over a fixed sample of boards. The run prints the table it checked.
void test_tiers() {
    const Average easy = sample_level(0, 10);
    const Average medium = sample_level(1, 10);
    const Average hard = sample_level(2, 5);
    require(easy.solutions > 3 * medium.solutions && medium.solutions > 3 * hard.solutions,
            "solutions fall from level to level");
    require(hard.solutions == 1, "Hard boards are unique");
    require(easy.backtracks < medium.backtracks && medium.backtracks * 2 < hard.backtracks,
            "wrong turns rise from level to level");
    require(easy.length < medium.length && medium.length < hard.length, "lines grow longer");
    require(easy.crossings < medium.crossings && medium.crossings <= hard.crossings,
            "lines fold over more edges and hop through more portals");
    require(easy.forced >= medium.forced && medium.forced >= hard.forced,
            "fewer first moves are handed over");
    require(easy.detour < medium.detour && medium.detour < hard.detour, "detours grow");
}

std::string slurp(const std::filesystem::path& path) {
    std::string text;
    require(read_file(path, text), "read a fixture");
    return text;
}

void test_saves(const std::filesystem::path& fixtures) {
    Session session;
    session.puzzle = generate(2, 99, GenerateOptions{});
    session.play = fresh_play(session.puzzle);
    session.next_level = 1;
    session.portals_met = true;
    record(session.scores, "Ada", 12);
    record(session.scores, "Bo = B", 9);
    session.player = "Bo = B";
    // Draw the first line and half of the second.
    const Puzzle& puzzle = session.puzzle;
    std::vector<std::vector<int>> lines(puzzle.ends.size());
    lines[0] = puzzle.witness[0];
    lines[1].assign(puzzle.witness[1].begin(), puzzle.witness[1].begin() + 2);
    if (lines[1].size() == 2 && puzzle.tiles[static_cast<std::size_t>(lines[1][1])] == Tile::portal) {
        lines[1].push_back(puzzle.witness[1][2]);
    }
    require(replay(puzzle, lines, 3, session.play), "a partial play replays");
    const std::string body = encode_session(session);
    Session restored;
    require(decode_session(body, restored), "a session decodes");
    require(encode_session(restored) == body, "and encodes to the same text");
    require(restored.play.paths == session.play.paths && restored.play.strokes == 3 &&
                restored.scores.size() == 2 && restored.scores[0].name == "Bo = B",
            "the position, strokes and table come back");
    // The envelope refuses damage, foreign files and oversized files, and leaves the
    // destination untouched.
    const std::string sealed = seal(body);
    std::string unsealed;
    require(unseal(sealed, unsealed) && unsealed == body, "the envelope round-trips");
    for (std::size_t bit = 0; bit < sealed.size(); bit += 97) {
        std::string flipped = sealed;
        flipped[bit] = static_cast<char>(flipped[bit] ^ 1);
        std::string out;
        require(!unseal(flipped, out), "a flipped bit is refused");
    }
    std::string out;
    require(!unseal("TEMPLATEGAME1\nlevel=0\ncheck=1\n", out), "a foreign file is refused");
    require(!unseal(sealed + std::string(save_limit_bytes, ' '), out), "an oversized file is refused");
    Session guard = restored;
    std::string impossible = body;
    const std::size_t paths_at = impossible.find("paths=");
    const std::size_t stones_at = impossible.find("stones=");
    const std::size_t stones_end = impossible.find('\n', stones_at);
    const std::string first_stone = impossible.substr(stones_at + 7, impossible.find(' ', stones_at) - stones_at - 7);
    // A line drawn through a stone.
    impossible.insert(paths_at + 6, first_stone + ' ');
    require(stones_end != std::string::npos, "the body has stones");
    require(!decode_session(impossible, guard), "a line through a stone is refused");
    require(encode_session(guard) == body, "a refused load leaves the session untouched");
    std::string unknown = body + "colour=blue\n";
    require(!decode_session(unknown, guard), "an unknown key is refused");
    // Files on disk.
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "nature-cube-rules-test-v3.txt";
    require(write_save(path, body), "write a save");
    std::string read;
    require(read_save(path, read) && read == body, "read it back");
    std::filesystem::remove(path);

    // Saves from before the rework: a Hard game in progress, made by the old engine.
    Session legacy;
    require(import_legacy(slurp(fixtures / "legacy-hard-partial.txt"), true, legacy),
            "an old Hard game in progress loads");
    {
        // The same file as the old engine wrote it on Windows, in text mode.
        std::string windows;
        for (char c : slurp(fixtures / "legacy-hard-partial.txt")) {
            if (c == '\n') {
                windows.push_back('\r');
            }
            windows.push_back(c);
        }
        Session crlf;
        require(import_legacy(windows, true, crlf), "an old save written on Windows loads");
    }
    require(legacy.puzzle.level == 2 && legacy.puzzle.side == 4 && legacy.next_level == 2,
            "on its own level and board");
    require(!legacy.play.won && legacy.play.strokes == 9, "still in progress, strokes kept");
    require(legacy.scores.size() == 2 && legacy.scores[1].name == "Bo \"B\" Lee" && legacy.player == "Player",
            "old top scores and name come across");
    // Finish the last pair along its witness, leaving tiles open: under the new rules it wins.
    const std::size_t last = legacy.puzzle.ends.size() - 1;
    std::vector<int> tail = legacy.puzzle.witness[last];
    const std::vector<int> drawn = legacy.play.paths[last];
    require(!drawn.empty() && press(legacy.puzzle, legacy.play, drawn.back()), "resume the half-drawn line");
    // Straighten the remaining route so the board is left with open tiles.
    for (std::size_t index = 0; index < tail.size() && !legacy.play.won; ++index) {
        if (std::find(legacy.play.paths[last].begin(), legacy.play.paths[last].end(), tail[index]) !=
            legacy.play.paths[last].end()) {
            continue;
        }
        static_cast<void>(extend(legacy.puzzle, legacy.play, tail[index]));
    }
    require(legacy.play.won, "every pair joined wins an old Hard game");
    Session joined;
    require(import_legacy(slurp(fixtures / "legacy-hard-joined-unfilled.txt"), true, joined),
            "an old Hard game waiting to be filled loads");
    int covered = 0;
    for (const std::vector<int>& line : joined.play.paths) {
        covered += static_cast<int>(line.size());
    }
    int stones = 0;
    for (Tile tile : joined.puzzle.tiles) {
        stones += tile == Tile::stone ? 1 : 0;
    }
    require(covered + stones < joined.puzzle.geometry.cells, "with open tiles");
    require(joined.play.won && joined.pending == joined.play.strokes, "it is won, and its result may be entered");
    // It saves in the new format and comes back.
    Session again;
    require(decode_session(encode_session(joined), again) && again.play.won, "the imported game saves anew");
    Session names_only;
    require(import_legacy(slurp(fixtures / "legacy-hard-partial.txt"), false, names_only) &&
                names_only.scores.size() == 2 && names_only.play.paths.empty(),
            "an older file can give just its names and scores");
    std::string damaged = slurp(fixtures / "legacy-hard-partial.txt");
    damaged[damaged.size() / 2] = static_cast<char>(damaged[damaged.size() / 2] ^ 1);
    Session untouched;
    require(!import_legacy(damaged, true, untouched) && untouched.scores.empty(),
            "a damaged old file is refused");
}

void test_stage() {
    // Layout fits every surface from below the minimum to a large display.
    for (double width = 560; width <= 2400; width += 37) {
        for (double height = 300; height <= 1500; height += 41) {
            const Layout layout = compute_layout(width, height);
            require(layout.board.w > 0 && layout.board.x >= 0 && layout.board.y >= 0 &&
                        layout.board.x + layout.board.w <= width + 1e-9 &&
                        layout.board.y + layout.board.h <= height + 1e-9,
                    "the cube stays on the surface");
            require(layout.board.y >= layout.top_clear - 1e-9, "the cube keeps its top margin");
            require(layout.board.w >= std::min(width, height) * .5, "the cube is the largest thing");
            require(!overlap(layout.panel, layout.board) || layout.panel.w > 0, "the panel has a place");
            require(layout.panel.x >= 0 && layout.panel.x + layout.panel.w <= width + 1e-9 &&
                        layout.panel.y + layout.panel.h <= height + 1e-9,
                    "the score card fits");
        }
    }
    // Animation settles and then reports nothing moving; reduced motion lands at once.
    const Puzzle puzzle = generate(1, 77, GenerateOptions{});
    const Play play = fresh_play(puzzle);
    for (int reduced = 0; reduced < 2; ++reduced) {
        Motion motion;
        begin_arrival(motion, puzzle, reduced == 1);
        int frames = 0;
        while (advance(motion, puzzle, play, 1.0 / 60, reduced == 1) && frames < 60 * 20) {
            ++frames;
        }
        require(frames < 60 * 6, "the arrival settles in a few seconds");
        if (reduced == 1) {
            require(frames == 0, "reduced motion arrives at once");
        }
        require(!advance(motion, puzzle, play, 1.0 / 60, reduced == 1), "a settled cube stays still");
        begin_departure(motion, reduced == 1);
        frames = 0;
        while (advance(motion, puzzle, play, 1.0 / 60, reduced == 1) && frames < 60 * 20) {
            ++frames;
        }
        require(frames < 60 * 6 && motion.departed, "the finished cube leaves");
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string group = argc > 1 ? argv[1] : "all";
    if (group == "tiers") {
        test_tiers();
        std::printf("Nature Cube tiers are ordered\n");
        return 0;
    }
    if (group == "all" || group == "rules") {
        test_geometry();
        test_rules();
        test_solver();
        test_stage();
    }
    if (group == "all" || group == "generator") {
        test_generator();
    }
    if (group == "all" || group == "saves") {
        const std::filesystem::path fixtures =
            argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path("tests/fixtures");
        test_saves(fixtures);
    }
    std::printf("Nature Cube %s tests passed\n", group.c_str());
    return 0;
}
