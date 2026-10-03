#include "solitaire_solver.hpp"
#include <chrono>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <string>

using namespace games;

namespace {
void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::string label(Kind kind, int option, Difficulty d, std::uint32_t seed) {
    return std::string(game_name(kind)) + " option " + std::to_string(option) + " " +
           difficulty_name(d) + " seed " + std::to_string(seed);
}

// The proof that solver and game agree: play the steps on a fresh Game and finish won.
int replay(Kind kind, int option, std::uint32_t seed, const std::vector<SolverStep>& steps,
           const std::string& what) {
    Game game;
    game.deal(kind, seed, option);
    int draws = 0;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        require(!game.state.over, what + ": won before the line ended");
        bool ok = steps[i].draw ? game.draw() : game.move(steps[i].move);
        require(ok, what + ": step " + std::to_string(i) + " rejected by Game");
        require(game.invariant(), what + ": card conservation");
        draws += steps[i].draw;
    }
    require(game.state.over, what + ": line does not win");
    return draws;
}

struct Group {
    Kind kind;
    int option;
};
const Group groups[] = {{Kind::solitaire, 1}, {Kind::solitaire, 3}, {Kind::spider, 1},
                        {Kind::spider, 2},    {Kind::spider, 4},    {Kind::freecell, 0}};
const Difficulty levels[] = {Difficulty::easy, Difficulty::medium, Difficulty::hard};
} // namespace

int main() {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    require(std::string(difficulty_name(Difficulty::easy)) == "Easy", "names");
    require(normalized_option(Kind::solitaire, 2) == 1 && normalized_option(Kind::spider, 3) == 1 &&
                normalized_option(Kind::spider, 4) == 4 &&
                normalized_option(Kind::freecell, 7) == 0,
            "options");
    require(graded_count(Kind::hearts, 0, Difficulty::easy) == 0, "hearts has no table");
    require(graded_seed(Kind::hearts, 0, Difficulty::easy, 4) == 5, "fallback seed");

    int klondike3_recycles = 0;
    for (const Group& g : groups) {
        std::set<std::uint32_t> seen;
        for (Difficulty d : levels) {
            int count = graded_count(g.kind, g.option, d);
            require(count >= 100, label(g.kind, g.option, d, 0) + ": table too small");
            for (int i = 0; i < count; ++i) {
                std::uint32_t seed =
                    graded_seed(g.kind, g.option, d, static_cast<std::uint32_t>(i));
                require(seen.insert(seed).second,
                        label(g.kind, g.option, d, seed) + ": seed in two buckets");
            }
            require(graded_seed(g.kind, g.option, d, static_cast<std::uint32_t>(count)) ==
                        graded_seed(g.kind, g.option, d, 0),
                    "pick wraps around");

            // Sample eight entries spread over the table: each is solved and its line
            // replayed through Game; the first two are also re-graded from scratch.
            const int samples = 8;
            for (int p = 0; p < samples; ++p) {
                std::uint32_t pick = static_cast<std::uint32_t>(p * (count - 1) / (samples - 1));
                std::uint32_t seed = graded_seed(g.kind, g.option, d, pick);
                std::string what = label(g.kind, g.option, d, seed);
                std::vector<SolverStep> steps;
                if (p < 2) {
                    DealGrade grade = grade_deal(g.kind, g.option, seed);
                    require(grade.winnable && grade.difficulty == d,
                            what + ": grade differs from table");
                }
                GradingLimits limits = grading_limits(g.kind, g.option);
                bool casual = greedy_wins(g.kind, g.option, seed, &steps);
                require(!casual || d == Difficulty::easy, what + ": casual win outside Easy");
                if (casual) {
                    replay(g.kind, g.option, seed, steps, what + " (casual line)");
                    steps.clear();
                    if (g.kind == Kind::spider && g.option == 4)
                        continue; // the casual player never wins 4 suits; nothing more to check
                }
                SolveReport report = solve_deal(g.kind, g.option, seed, limits.node_budget, &steps);
                if (casual && !report.solved)
                    continue; // the casual line above is the proof
                require(report.solved, what + ": solver did not win");
                require(report.moves == static_cast<int>(steps.size()), what + ": move count");
                if (!casual) {
                    int band = report.width <= limits.easy_width     ? 0
                               : report.width <= limits.medium_width ? 1
                                                                     : 2;
                    require(band == static_cast<int>(d), what + ": beam width outside its band");
                }
                int draws = replay(g.kind, g.option, seed, steps, what);
                if (g.kind == Kind::solitaire && g.option == 3 && draws > 8)
                    ++klondike3_recycles;
            }
        }
    }
    // Draw-3 lines that use more than the eight draws of one stock pass prove the
    // recycling model against Game::draw.
    require(klondike3_recycles > 0, "no draw-3 line exercised stock recycling");

    // Positions in progress: make a few legal moves, then solve from there.
    for (std::uint32_t seed = 1; seed <= 3; ++seed) {
        Game game;
        game.deal(Kind::freecell, graded_seed(Kind::freecell, 0, Difficulty::medium, seed), 0);
        for (int i = 0; i < 6; ++i) {
            Move m = game.hint();
            if (m.from < 0 || !game.move(m))
                break;
        }
        std::vector<SolverStep> steps;
        SolveReport report = solve_state(game.state, 200000, &steps);
        require(report.solved, "FreeCell mid-game solve");
        for (const SolverStep& s : steps)
            require(s.draw ? game.draw() : game.move(s.move), "FreeCell mid-game step");
        require(game.state.over, "FreeCell mid-game line wins");
    }
    {
        Game game;
        game.deal(Kind::solitaire, graded_seed(Kind::solitaire, 3, Difficulty::easy, 0), 3);
        for (int i = 0; i < 4; ++i)
            game.draw(); // waste and stock both non-empty
        std::vector<SolverStep> steps;
        require(solve_state(game.state, 200000, &steps).solved, "Klondike mid-game solve");
        for (const SolverStep& s : steps)
            require(s.draw ? game.draw() : game.move(s.move), "Klondike mid-game step");
        require(game.state.over, "Klondike mid-game line wins");
    }

    double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("solver tests passed in %.1f s\n", seconds);
    return 0;
}
