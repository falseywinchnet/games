#pragma once
// What the raccoons and the bear say. The raccoons are cheeky, theatrical
// garden bandits who love vegetables and never mean any harm; the bear is
// gentle and polite. Nobody makes fun of the player: the raccoons tease the
// bear's luck and boast about carrots, and when the same mistake keeps
// happening they turn kind and point at Undo.
//
// Each kind of line is a shuffled bag: no line is said twice until every line
// of its kind has been said, so a session does not repeat itself.
#include <cstdint>
#include <vector>

namespace ct {

enum class Line : int {
    taunt,                                   // popped up, showing off
    spring, summer, autumn, winter, night,   // showing off about the season
    greet,                                   // a new garden
    trapped,                                 // mumbled from under a pumpkin
    freed,                                   // a pumpkin pushed off a burrow
    duck,                                    // the bear came too close
    close,                                   // a pumpkin pushed right next to a free burrow
    last_one,                                // the last raccoon not yet caught
    laugh,                                   // a pumpkin wedged for good
    laugh_again,                             // ... a second time in this garden
    laugh_soft,                              // ... a third time or more: kindly
    unstuck,                                 // undo took the wedged pumpkin back
    undo,                                    // the bear undid a step
    restart,                                 // the bear started the garden over
    hint,                                    // the bear asked for a hint
    idle,                                    // nothing has happened for a while
    give_up,                                 // the garden is cleared
    give_up_perfect,                         // ... in the fewest pushes possible
    bear_catch,                              // the bear covers a burrow
    bear_stuck, bear_stuck_again, bear_stuck_calm,
    bear_bump,                               // walked into a hedge
    bear_idle,
    bear_win, bear_perfect,
    kinds
};

class Lines {
public:
    explicit Lines(std::uint64_t seed = 1);
    const char* pick(Line kind);
    static int size(Line kind);
    static const char* line(Line kind, int index);

private:
    std::uint64_t rng_;
    std::vector<std::vector<int>> bags_;  // per kind: line indices still to say, last first
    std::vector<int> last_;               // per kind: the line said last
    int rand_int(int n);
};

}  // namespace ct
