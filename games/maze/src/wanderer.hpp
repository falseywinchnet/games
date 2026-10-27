#pragma once
// The passive player: walks the maze on its own, as a screensaver would, and finds the
// reward somewhat dumbly. It goes where it has been least (its memory is the square, which
// way up it is and which pads it has pressed, so a pressed pad or the flip opens the maze
// afresh), keeps on straight when it can, and dawdles: now and then it stops, or glances
// down another way before taking the one it chose. If it has wandered far longer than the
// best route would take, it gets the hang of it and takes the best route from where it
// stands, so every maze ends. It only decides; the view turns and walks, and waits for
// whoever is in the way.
#include "maze.hpp"

#include <cstdint>
#include <unordered_map>

namespace mz {

class Wanderer {
  public:
    struct Move {
        int dir = -1;       // the world direction to face, -1 for none
        bool walk = false;  // and once facing it, walk on
        double pause = 0;   // or stand still this long (seconds)
    };

    // A new maze, or a fresh start in this one.
    void reset(std::uint64_t seed);

    // What to do next from `play`. Asked again whenever the walker is free; it keeps its
    // choice until it has moved, so a marble in the way only makes it wait.
    [[nodiscard]] Move next(const Level& level, const Play& play);

    [[nodiscard]] int walked() const { return walked_; }           // squares since reset
    [[nodiscard]] bool knows_the_way() const { return knows_; }    // following the best route now

  private:
    [[nodiscard]] double unit();
    [[nodiscard]] static std::uint64_t key_of(const Play& play);

    std::uint64_t state_ = 1;
    std::unordered_map<std::uint64_t, int> visits_{};   // times it has stood in each state
    std::uint64_t here_ = 0;     // the state it last decided in
    bool decided_ = false;
    int chosen_ = -1;            // the way it chose from here
    int glance_ = -1;            // a way it glances down first, or -1
    bool dawdled_ = false;       // false while a moment's stop here is still to come
    int walked_ = 0;
    bool knows_ = false;
};

}  // namespace mz
