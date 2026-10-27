#pragma once
// The rules: pure, deterministic and headless. Nothing here knows about pixels,
// time, sound or files. A board is made from a seed, so the same seed always
// gives the same puzzle on every platform.
//
// The template's placeholder game is "lamps": pressing a lamp turns it and its
// four neighbours over, and the board is won when every lamp is lit. Replace
// this file with your own game; keep its shape (new_game, a move, undo, a
// finished test, and text encoding for the save).
#include <cstdint>
#include <string>
#include <vector>

namespace tg {

inline constexpr int minimum_side = 3;
inline constexpr int maximum_side = 6;

struct Board {
    int side = 4;                    // lamps per row and per column
    std::uint64_t seed = 1;          // the puzzle's identity
    std::vector<std::uint8_t> lit;   // side * side cells, row-major, 1 when lit
    std::vector<int> moves;          // cells pressed, in order (undo and resume)
    int best = 0;                    // fewest presses that solved this side, 0 if never
};

// A splitmix64 step. Integer-only, so results are identical everywhere.
[[nodiscard]] std::uint64_t next_random(std::uint64_t& state);

// A new solvable board: starts from all lit and applies presses chosen by the seed.
[[nodiscard]] Board new_game(int side, std::uint64_t seed);

[[nodiscard]] bool valid_cell(const Board& board, int cell);
// Presses a lamp. Returns false, changing nothing, when the cell is outside the board
// or the board is already solved.
bool press(Board& board, int cell);
// Takes back the last press. Returns false when there is nothing to take back.
bool undo(Board& board);
[[nodiscard]] bool solved(const Board& board);
[[nodiscard]] int lit_count(const Board& board);

// The save body: a few "key=value" lines. decode validates everything it reads and
// leaves `destination` untouched when the text is not a board this version made.
[[nodiscard]] std::string encode(const Board& board);
[[nodiscard]] bool decode(const std::string& text, Board& destination);

// Everything the game remembers between runs: the board in play and the size the
// player chose for the next one.
struct Session {
    Board board;
    int next_side = 4;
};
[[nodiscard]] std::string encode_session(const Session& session);
[[nodiscard]] bool decode_session(const std::string& text, Session& destination);

}  // namespace tg
