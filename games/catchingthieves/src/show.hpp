#pragma once
// The performance: turns the rules' moves into animation. The bear hops from
// square to square and leans into his pushes; pumpkins slide and rock, then
// thunk onto burrows; each raccoon has a mind of its own (peeking, popping up
// to taunt, ducking when the bear comes near, mumbling under a pumpkin,
// springing free when one is pushed off). When the garden is stuck they come
// up laughing; when it is cleared they surrender. Rules always commit first:
// the show only ever catches up with the board.
#include "garden.hpp"
#include "level.hpp"
#include "lines.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace ct {

struct Cue {                // something for the view to play or show
    enum Kind { sound, say } kind = sound;
    std::string text;       // a sound name, or the words of a speech bubble
    float gain = 1, rate = 1;
    int who = -1;           // say: -1 the bear, else the burrow index
};

struct Bubble {
    std::string text;
    int who = -1;           // -1 the bear, else the burrow index
    double age = 0, life = 2.2;
};

class Show {
public:
    explicit Show(std::uint64_t seed = 1);
    void set_level(const Board& board, const Garden& garden, Season season);
    // the rules have just made this move (or undone it); animate toward the new board
    void moved(const Board& board, const Move& m, bool undo);
    void restart(const Board& board);
    void stuck(bool now);        // the board has (or no longer has) a pumpkin that can never be saved
    void won(bool perfect = false);  // perfect: in the fewest pushes possible
    void blocked(int dir);
    void hinted();               // the bear asked for a hint
    void thinking(bool on) { thinking_ = on; }   // pondering a hint: a paw to the chin       // the bear tried to walk into a hedge or an immovable pumpkin
    void update(double dt, const Board& board);
    void settle(const Board& board);

    GardenState& state() { return st_; }
    const GardenState& state() const { return st_; }
    std::vector<Cue> cues;
    std::vector<Bubble> bubbles;
    bool busy() const { return step_t_ < step_len_; }
    double celebration() const { return win_t_; }   // seconds since the win (-1: not won)

private:
    struct Coon {
        enum State { hidden, peeking, up, ducking, trapped, surrender } state = hidden;
        double t = 0, dur = 1;
        int act = 0;          // which taunt
        double rise = 0, rise_v = 0;
        double target = 0;
        double wiggle = 0;
        double say_cool = 0;
        int burrow = -1;      // its cell
    };
    std::uint64_t rng_;
    const Garden* garden_ = nullptr;
    GardenState st_;
    std::vector<Coon> coons_;
    std::vector<int> burrow_cells_;
    // the step in progress
    double step_t_ = 1, step_len_ = .16;
    V3 bear_from_{}, bear_to_{};
    int push_box_ = -1;
    V3 box_from_{}, box_to_{};
    int dir_ = kDown;
    double bear_yaw_ = 0, yaw_target_ = 0;
    double walk_phase_ = 0;
    double idle_ = 0, bump_ = 0, push_pose_ = 0;
    bool stuck_ = false;
    bool thinking_ = false;
    double stuck_t_ = 0;
    double win_t_ = -1;
    double t_ = 0;
    double rand01();
    int rand_int(int n);
    // what is said: see lines.hpp. Taunts share a cooldown so the garden never chatters.
    Lines lines_;
    int mistakes_ = 0;           // pumpkins wedged in this garden: the reactions escalate
    bool perfect_ = false;
    double chatter_ = 0;         // seconds until the next taunt may be said
    double greet_t_ = -1;        // seconds until a raccoon greets a new garden
    int idle_lines_ = 0;         // said while the bear has stood still
    std::vector<double> quiet_;  // per kind of line: seconds until it may be said again
    bool quiet(Line kind, double gap);  // false when said too recently; otherwise starts the gap
    bool can_say(int who) const;
    void say(int who, const std::string& text);
    bool say_coon(int i, Line kind);
    void taunt(int i);
    int any_coon(bool free_only);  // a raccoon to speak, or -1
    void coon_update(int i, double dt, const Board& board);
    void bear_update(double dt);
    void place_all(const Board& board);
};

}  // namespace ct
