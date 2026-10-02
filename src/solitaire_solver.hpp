#pragma once
// Solver, difficulty grading and verified deal tables for Solitaire (Klondike),
// Spider and FreeCell. The solver works on its own compact state but follows
// exactly the rules of games::Game (game.cpp); every solution it reports has
// been replayed through Game::move/Game::draw and ends with state.over.
#include "game.hpp"
#include <cstdint>
#include <vector>

namespace games {

enum class Difficulty { easy, medium, hard };

// One action on the real Game: Game::draw() when `draw` is set, otherwise
// Game::move(move). Foundation and free-cell slots refer to the actual piles.
struct SolverStep {
    bool draw = false;
    Move move{};
};

struct SolveReport {
    bool solved = false;    // a winning line was found and verified through Game
    bool exhausted = false; // every state reachable with the solver's move set was searched; no win
    long nodes = 0;         // search expansions used (all ladder rungs)
    int moves = 0;          // Game actions (moves plus draws) in the winning line
    int width = 0;          // beam width of the ladder rung that won (10, 20, 40, ...)
};

// Solves any Solitaire/Spider/FreeCell position (for example a game in progress,
// as the basis for a hint). The solver sees face-down cards and the stock order.
SolveReport solve_state(const State& state, long node_budget,
                        std::vector<SolverStep>* solution = nullptr);
SolveReport solve_deal(Kind kind, int option, std::uint32_t seed, long node_budget,
                       std::vector<SolverStep>* solution = nullptr);
// The "casual player" model used for Easy: plays obvious moves by fixed priorities
// using only face-up information, never backtracks. True when it wins the deal.
bool greedy_wins(Kind kind, int option, std::uint32_t seed,
                 std::vector<SolverStep>* solution = nullptr);

// Difficulty, as defined in solitaire_solver.cpp (widths per bucket in
// grading_limits): the solver runs beam searches of width 10, 20, 40, ...;
//   easy   - the casual player wins, or a beam of at most easy_width wins;
//   medium - a beam of at most medium_width wins;
//   hard   - a wider beam is needed, within the node budget.
struct DealGrade {
    bool winnable = false; // proven by a verified winning line
    Difficulty difficulty = Difficulty::medium;
    bool greedy = false;
    SolveReport report; // solver result; not run when the casual player already won
};
DealGrade grade_deal(Kind kind, int option, std::uint32_t seed);
struct GradingLimits {
    long node_budget = 0; // solver expansions; deals not won within it are left out
    int easy_width = 0;   // 0: Easy only through the casual player
    int medium_width = 0;
};
GradingLimits grading_limits(Kind kind, int option);

// Option as Game::deal interprets it: Solitaire 1|3 (draw), Spider 1|2|4 (suits),
// FreeCell 0 (unused). Other values map to the game's default.
int normalized_option(Kind kind, int option);

// A verified winnable seed for game.deal(kind, seed, option) at the requested
// difficulty. `pick` indexes the table and wraps around. Returns pick + 1 when the
// table is empty (Hearts or an unsupported option).
std::uint32_t graded_seed(Kind kind, int option, Difficulty difficulty, std::uint32_t pick);
int graded_count(Kind kind, int option, Difficulty difficulty);
const char* difficulty_name(Difficulty difficulty);

namespace detail {
struct DealTable {
    Kind kind;
    int option;
    Difficulty difficulty;
    const std::uint32_t* seeds;
    int count;
};
// Defined in the generated deal_tables.cpp.
extern const DealTable deal_tables[];
extern const int deal_table_count;
} // namespace detail

} // namespace games
