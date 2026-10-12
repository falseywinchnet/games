#include "solitaire_solver.hpp"
#include <chrono>
#include <cstdio>
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

bool stop_at_once(const void*) {
    return true;
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
    // Every game, option and level: deals made from fixed luck fit their level when measured
    // again from scratch, and the proof that comes with them wins when replayed through Game.
    int klondike3_recycles = 0;
    std::uint32_t freecell_seed = 0, klondike3_easy = 0;
    for (const Group& g : groups)
        for (Difficulty d : levels)
            for (std::uint64_t luck : {7ULL, 8ULL}) {
                const DealPick pick = generate_deal(g.kind, g.option, d, luck);
                const std::string what = label(g.kind, g.option, d, pick.seed);
                require(pick.found && pick.seed != 0, what + ": no deal made");
                require(fits(d, pick.wins), what + ": outside its level");
                const DealRecord record = measure_deal(g.kind, g.option, pick.seed);
                require(record.wins == pick.wins, what + ": measured differently a second time");
                require(record.winnable, what + ": not proven winnable");
                const int draws = replay(g.kind, g.option, pick.seed, record.line, what);
                if (g.kind == Kind::solitaire && g.option == 3 && draws > 8)
                    ++klondike3_recycles;
                require(generate_deal(g.kind, g.option, d, luck).seed == pick.seed,
                        what + ": the same luck made another deal");
                if (g.kind == Kind::freecell && !freecell_seed)
                    freecell_seed = pick.seed;
                if (g.kind == Kind::solitaire && g.option == 3 && d == Difficulty::easy)
                    klondike3_easy = pick.seed;
            }
    // Draw-3 lines that use more than the eight draws of one stock pass prove the
    // recycling model against Game::draw.
    require(klondike3_recycles > 0, "no draw-3 line exercised stock recycling");

    // Asked to stop, the dealer stops at once and says it made nothing.
    {
        const DealPick stopped =
            generate_deal(Kind::spider, 4, Difficulty::easy, 1, stop_at_once, nullptr);
        require(!stopped.found, "the dealer did not stop");
    }

    // Positions in progress: make a few legal moves, then solve from there.
    for (std::uint32_t seed = 1; seed <= 3; ++seed) {
        Game game;
        game.deal(Kind::freecell, freecell_seed + seed, 0);
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
        game.deal(Kind::solitaire, klondike3_easy, 3);
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
