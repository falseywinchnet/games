#pragma once
// Pen the Sheep, the rules. A meadow of hexagons; a sheep in the middle who
// wants out. You place a stone on any free patch of grass; then the sheep
// trots one patch toward the edge. The moment it reaches an edge patch it
// walks straight off the field and is gone. Wall it in so that no path leads out and it gives up, sits down and
// sulks: you win. You get three stones before it starts.
//
// The sheep takes the shortest way out, and gets cleverer as the levels go on:
// a dozy sheep dawdles; a clever one prefers the way with the most routes
// out; a cunning one thinks one stone ahead. Every sheep can be misled by a
// clover: one beside it and it can't resist, and eating takes it a turn.
//
// The sheep is deterministic (ties broken by a hash of the meadow), so a
// generated level comes with a proven solution: a solver bot plays it to a
// win before the level is accepted, and its stone count is the par.
#include <cstdint>
#include <string>
#include <vector>

namespace sh {

enum class Cell : std::uint8_t { grass, rock, stone, clover };
enum class Smarts : std::uint8_t { dozy, clever, cunning };
constexpr int kHeadStart = 3;

struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ULL + 0x2545F4914F6CDD1DULL) { if (!s) s = 1; next(); }
    std::uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    int range(int n) { return n <= 1 ? 0 : static_cast<int>(next() % static_cast<std::uint64_t>(n)); }
    double unit() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
};

struct Meadow {
    int w = 11, h = 11;
    std::vector<Cell> cells;   // w*h, row-major; odd rows sit half a patch to the right
    int sheep = 0;             // the sheep's cell
    Smarts smarts = Smarts::clever;
    int stones = 0;            // stones you've placed
    int head = kHeadStart;     // stones still to place before the sheep starts
    bool munching = false;     // the sheep is eating a clover: it skips its next move
    bool escaped = false;
    std::uint64_t salt = 1;    // for the sheep's tie-breaks

    int idx(int c, int r) const { return r * w + c; }
    int col(int i) const { return i % w; }
    int row(int i) const { return i / w; }
    bool edge(int i) const { const int c = col(i), r = row(i); return c == 0 || r == 0 || c == w - 1 || r == h - 1; }
    bool open(int i) const { return cells[static_cast<size_t>(i)] == Cell::grass || cells[static_cast<size_t>(i)] == Cell::clover; }
    int neighbours(int i, int out[6]) const;
    // steps to get off the meadow from each cell (edge cells 0; -1 if walled in)
    std::vector<int> distances() const;
    // number of different shortest ways out from each cell (capped)
    std::vector<int> routes(const std::vector<int>& dist) const;
    bool penned() const;                 // no way out for the sheep
    bool can_place(int i) const { return i >= 0 && i < w * h && i != sheep && cells[static_cast<size_t>(i)] == Cell::grass; }
};

struct SheepMove {
    enum Kind { step, munch, escape, stuck } kind = step;
    int to = -1;
};
// What the sheep does now (it doesn't move it). Deterministic for a given meadow.
SheepMove sheep_choice(const Meadow& m);
void sheep_apply(Meadow& m, const SheepMove& mv);

// Place a stone, then (once the head start is used) the sheep answers. Returns the sheep's move (kind stuck if penned).
bool place(Meadow& m, int cell, SheepMove* answer = nullptr);

// The fewest stones that would wall the sheep in now (vertex cut to the edge; capped at 7).
int cut_size(const Meadow& m);

// The solver's choice of where to put the next stone.
int bot_stone(const Meadow& m);

struct LevelParams {
    int size = 11;
    int rocks = 10;
    int clovers = 0;
    Smarts smarts = Smarts::clever;
    std::uint64_t seed = 1;
};
struct Level {
    Meadow start;
    std::vector<int> solution;  // the bot's stones, a proven win
    int par = 0;
    LevelParams params;
};
LevelParams params_for(int level);         // the old campaign's curve (tools and tests)
// A new meadow at a difficulty (0 easy, 1 medium, 2 hard) from a seed:
//   Easy    9x9, a dozy sheep, plenty of old fences.
//   Medium  11x11, a clever sheep, fewer fences, sometimes a clover.
//   Hard    11x11, a cunning sheep, few fences, clover to use.
LevelParams params_for_difficulty(int difficulty, std::uint64_t seed);
Level generate(const LevelParams& p);      // retries seeds until the bot can pen the sheep
const char* smarts_name(Smarts s);

}  // namespace sh
