#pragma once
// The rules of the garden: a grid of hedges and soil with raccoon burrows
// (goals). The bear walks one square at a time and pushes a pumpkin ahead of
// him when there is room for it; he can never pull. A level is won when every
// burrow has a pumpkin on it. Pure and deterministic: no UI, no clock.
//
// Levels use the standard Sokoban text format (XSB):
//   #  hedge      (space) soil     .  burrow
//   $  pumpkin    *  pumpkin on a burrow
//   @  bear       +  bear on a burrow
//   O  fillable pit   X  pumpkin-loving squirrel   ^ > v <  one-way gate
// Optional !ct2 lines preserve overlays under actors and stable identities.
// Solutions use LURD: lowercase a walk, uppercase a push.
#include <cstdint>
#include <string>
#include <vector>

namespace ct {

enum class Tile : std::uint8_t { outside, wall, floor };
enum Dir { kUp = 0, kRight = 1, kDown = 2, kLeft = 3 };
constexpr int kDX[4] = {0, 1, 0, -1}, kDY[4] = {-1, 0, 1, 0};
constexpr char kWalk[4] = {'u', 'r', 'd', 'l'}, kPush[4] = {'U', 'R', 'D', 'L'};
int dir_of(char c);  // -1 if not a LURD letter

struct Level {
    int w = 0, h = 0;
    std::vector<Tile> tiles;         // w*h
    std::vector<std::uint8_t> goal;  // w*h, 1 on a burrow
    std::vector<int> boxes;          // pumpkin cells
    // Optional overlays; empty vectors mean classic Sokoban. Stable pumpkin IDs.
    std::vector<std::uint8_t> gate;   // cell: 0 unrestricted, 1..4 = Dir + 1
    std::vector<std::uint8_t> pit;    // cell: unfilled pit in the initial state
    std::vector<int> raccoons;        // squirrels; legacy field name retained for saved-map/API compatibility
    int player = -1;
    std::string title;               // optional
    std::string solution;            // LURD, optional (generated levels always carry one)

    int idx(int x, int y) const { return y * w + x; }
    bool in(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    bool floor(int c) const { return c >= 0 && c < w * h && tiles[static_cast<size_t>(c)] == Tile::floor; }
    int step(int c, int d) const;  // the neighbouring cell, or -1 off the grid
    int goals() const;
    bool special() const;
    int gate_at(int c) const { return gate.empty() || c < 0 || c >= w*h ? 0 : gate[static_cast<size_t>(c)]; }
    bool pit_at(int c) const { return !pit.empty() && c >= 0 && c < w*h && pit[static_cast<size_t>(c)]; }
    bool edge(int from, int dir) const; // directed edge, independent of dynamic occupancy
    bool valid() const;

    static bool parse(const std::string& xsb, Level& out, std::string* error = nullptr);
    std::string xsb() const;
};

struct Move {
    int dir = 0;
    bool push = false;
    int box = -1;  // which pumpkin (index into Board::boxes()) moved
    int filled = -1; // a pit filled by this move
    std::vector<int> stolen, thieves, stolen_from; // atomic turn effects, for exact undo
};

class Board {
public:
    bool load(const Level& level);
    const Level& level() const { return lv_; }

    bool move(int dir, Move* out = nullptr);  // false if the way is blocked
    bool undo(Move* out = nullptr);
    void restart();
    bool replay(const std::string& lurd);      // true if every letter applies exactly as written

    int player() const { return player_; }
    const std::vector<int>& boxes() const { return boxes_; }
    int box_at(int cell) const { return cell >= 0 && cell < static_cast<int>(occ_.size()) ? occ_[static_cast<size_t>(cell)] : -1; }
    bool solved() const;
    bool pit_filled(int c) const { return c >= 0 && c < static_cast<int>(filled_.size()) && filled_[static_cast<size_t>(c)]; }
    const std::vector<std::uint8_t>& filled_pits() const { return filled_; }
    const std::vector<std::uint8_t>& departed_raccoons() const { return departed_; }
    bool raccoon_at(int c) const;
    bool can_walk(int from, int dir) const;
    std::string state_key() const; // exact dynamic state, including bear and pumpkin identities
    bool restore_state(const std::string& key); // exact search state from state_key()
    void clear_history() { hist_.clear(); pushes_ = 0; } // search snapshots only
    int on_goal() const;
    int moves() const { return static_cast<int>(hist_.size()); }
    int pushes() const { return pushes_; }
    std::string history() const;  // LURD of the moves made so far
    const std::vector<Move>& moves_made() const { return hist_; }

    // A pumpkin that can never reach a burrow again, or pumpkins wedged
    // together so none can move, with at least one off a burrow. Only certain
    // dead ends are reported; it is never a guess.
    bool stuck() const;
    bool dead_square(int cell) const { return cell >= 0 && cell < static_cast<int>(dead_.size()) && dead_[static_cast<size_t>(cell)]; }

private:
    Level lv_;
    int player_ = -1;
    std::vector<int> boxes_;
    std::vector<std::uint8_t> filled_, departed_;
    std::vector<int> occ_;  // per cell: pumpkin index or -1
    std::vector<Move> hist_;
    int pushes_ = 0;
    std::vector<std::uint8_t> dead_;
    void compute_dead();
};

// Squares from which a lone pumpkin can still be pushed onto some burrow.
std::vector<std::uint8_t> live_squares(const Level& level);

}  // namespace ct
