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
    int player = -1;
    std::string title;               // optional
    std::string solution;            // LURD, optional (generated levels always carry one)

    int idx(int x, int y) const { return y * w + x; }
    bool in(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    bool floor(int c) const { return c >= 0 && c < w * h && tiles[static_cast<size_t>(c)] == Tile::floor; }
    int step(int c, int d) const;  // the neighbouring cell, or -1 off the grid
    int goals() const;

    static bool parse(const std::string& xsb, Level& out, std::string* error = nullptr);
    std::string xsb() const;
};

struct Move {
    int dir = 0;
    bool push = false;
    int box = -1;  // which pumpkin (index into Board::boxes()) moved
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
    std::vector<int> occ_;  // per cell: pumpkin index or -1
    std::vector<Move> hist_;
    int pushes_ = 0;
    std::vector<std::uint8_t> dead_;
    void compute_dead();
};

// Squares from which a lone pumpkin can still be pushed onto some burrow.
std::vector<std::uint8_t> live_squares(const Level& level);

}  // namespace ct
