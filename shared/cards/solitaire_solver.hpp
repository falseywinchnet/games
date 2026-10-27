#pragma once
// Solver and dealer for Solitaire (Klondike), Spider and FreeCell. The solver works on its
// own compact state but follows exactly the rules of games::Game (game.cpp); every line it
// reports has been replayed through Game::move/Game::draw and ends with state.over. The
// dealer makes deals as they are wanted, each winnable and of the level asked for
// (solitaire_solver.cpp, "Dealing").
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

// The reference player plays each deal this many times; its wins set the deal's level.
constexpr int reference_games = 20;

// The reference player's record on a deal, and the proof it can be won: a line of Game
// actions (a winning game of the reference player's, or the solver's when it never wins).
struct DealRecord {
    int wins = 0;
    bool winnable = false;
    std::vector<SolverStep> line;
};
DealRecord measure_deal(Kind kind, int option, std::uint32_t seed);
// Whether that many wins of reference_games put a deal at the level.
bool fits(Difficulty difficulty, int wins);

// A new deal for game.deal(kind, seed, option) at the level: random deals are shuffled from
// `luck` until one fits and is proven winnable. Between games `stop(context)` is asked
// whether to give up; then found is false.
struct DealPick {
    std::uint32_t seed = 0;
    int wins = 0;     // of reference_games
    int examined = 0; // deals shuffled
    bool found = false;
};
DealPick generate_deal(Kind kind, int option, Difficulty difficulty, std::uint64_t luck,
                       bool (*stop)(const void*) = nullptr, const void* context = nullptr);

// Option as Game::deal interprets it: Solitaire 1|3 (draw), Spider 1|2|4 (suits),
// FreeCell 0 (unused). Other values map to the game's default.
int normalized_option(Kind kind, int option);
const char* difficulty_name(Difficulty difficulty);

} // namespace games
