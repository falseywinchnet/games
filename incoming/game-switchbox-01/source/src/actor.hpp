#pragma once
// Her behaviour and animation. Every body channel is a damped spring chasing
// a target, so motion overshoots and settles with a bounce. The brain on top
// sets the targets: pop up, reach, press, linger, duck, peek, celebrate.
// Rules are decided before the Actor hears about a flip; she only reacts.
#include "stage.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace sbx {

struct Spring {
    double x = 0, v = 0, target = 0, k = 120, c = 14;
    void step(double dt) { v += (k * (target - x) - c * v) * dt; x += v * dt; }
    void snap(double to) { x = target = to; v = 0; }
};

struct Spring3 {
    V3 x{}, v{}, target{};
    double k = 300, c = 30;
    void step(double dt) {
        v = v + ((target - x) * k - v * c) * dt;
        x = x + v * dt;
    }
};

struct Cue {             // something for the view to play or show
    enum Kind { sound, line, special } kind = sound;  // special: her big "you're great" message
    std::string name;    // sound file or line category
    float gain = 1, rate = 1;
};

class Actor {
public:
    explicit Actor(std::uint64_t seed = 1);

    // the player flipped switch `sw` up; `solved` when it completed the combination
    void flipped(int sw, bool correct, bool solved);
    // a rip-out is under way or switches are rising back: the view must not accept clicks on them
    bool rearranging() const { return !rip_queue_.empty() || !restore_queue_.empty(); }
    void head_clicked();
    // mischief
    void set_hold(int sw) { hold_sw_ = sw; }   // the switch the mouse is holding down (-1: none)
    bool resting_on_hold() const;              // her hand is on the player's pointer
    bool take_forced_release();                // she lowered the held switch anyway
    bool carrying_pointer() const { return carry_; }   // she has lifted the player's pointer off the switch
    V3 pointer_at() const { return carry_at_; }        // where the pointer's tip is while she carries it
    bool take_pointer_drop(V3& where);         // she let go of the pointer here: put the real cursor there
    void steal(int sw);                        // the player keeps flipping this one: she takes it
    void hole_clicked(int sw);                 // the player clicked where her stolen switch was
    int stolen() const { return stolen_; }
    bool take_mole_request();                  // twenty bonks: whack-a-mole
    void mole_begin();
    void mole_watch(int sw) { mole_look_ = sw; }
    void mole_end(bool great);
    int bonks() const { return static_cast<int>(bonks_); }
    // her patience ladder answered an attempt; `first` is the combination's first switch
    void react(Reaction r, int first);
    // the cursor over the box, for her eyes (valid=false when outside)
    void set_cursor(V3 world, bool valid) { cursor_ = world; cursor_valid_ = valid; }

    void update(double dt, StageState& st);

    std::vector<Cue> cues;          // drained by the view each frame
    bool take_reset_request();      // the celebration finished: start a new combination
    bool celebrating() const { return mode_ == Mode::celebrate; }
    bool hidden() const { return mode_ == Mode::hidden && rise_.x < kZPeek - .05; }
    double t() const { return t_; }

    static constexpr double kZHidden = -1.05, kZPeek = -.2, kZUp = .78, kZHigh = 1.12;

private:
    enum class Mode { hidden, peek, up, duck, celebrate, taunt, mole, great };
    enum class HandState { idle, reach, press, retract, clap, point, wait, grab, carry };
    struct Hand {
        HandState state = HandState::idle;
        int job = -1;
        double timer = 0;
        Spring3 pos;
        Spring reach;
    };

    Mode mode_ = Mode::hidden;
    double t_ = 0, mode_t_ = 0;
    std::uint64_t rng_;
    double rand01();

    // body channels
    Spring rise_, slide_, lean_, side_, squash_, head_yaw_, head_pitch_, head_roll_, lid_, sway_;
    std::array<Hand, 2> hands_;
    std::deque<int> jobs_;           // switches that are up and still to be pushed down
    std::array<bool, kSwitches> claimed_{};
    double react_ = 0;               // delay before reacting to a flip from hiding
    double linger_ = 0;              // time to stay up once the work is done
    double next_peek_ = 6;
    double blink_t_ = 2, blink_left_ = 0;
    double eep_ = 0;                 // a head-bonk reaction timer
    int celebrate_stage_ = 0;
    double stage_t_ = 0;
    int last_switch_ = -1;
    bool reset_request_ = false;
    V3 cursor_{};
    bool cursor_valid_ = false;
    V3 head_prev_{}, head_vel_{};
    Spring3 hair_;
    Spring ahoge_;
    Face face_;
    double say_cool_ = 0;
    bool first_pop_ = true;
    int mood_ = 0;  // 0 calm, 1 nervous (a lamp lit), 2 smug (a miss), 3 cross, 4 scolding, 5 exasperated, 6 helpful
    int point_sw_ = -1;              // the switch she is pointing at for a hint
    double point_t_ = 0;
    bool sweep_ = false;
    bool restore_pending_ = false;             // push down every switch that is up (after a miss, after a win)
    std::deque<int> rip_queue_;      // switches to yank down into the box, one by one
    std::deque<int> restore_queue_;  // switches to push back up from inside
    double rip_t_ = 0;               // time to the next yank or return
    int rip_cur_ = -1, restore_cur_ = -1;
    double rip_anim_ = 0, restore_anim_ = 0;
    void rearrange(double dt, StageState& st);
    // holding
    int hold_sw_ = -1;
    double hold_t_ = 0;
    int hold_phase_ = 0;                 // 0 none, 1 calm, 2 cross, 3 raging
    bool forced_ = false;
    bool carry_ = false, dropped_ = false;
    V3 carry_at_{}, carry_from_{};
    // stealing
    int steal_sw_ = -1;                  // the next press of this switch is a theft
    int stolen_ = -1;
    double stolen_t_ = 0;
    bool returning_ = false;
    // bonks on the head, and whack-a-mole
    double bonks_ = 0, bonk_idle_ = 0;
    bool mole_request_ = false;
    int mole_look_ = -1;
    bool mole_great_ = false;
    Fx fx_;
    int bonk_level() const;
    void fx_update(double dt, StageState& st);
    double mood_t_ = 0;
    bool clap_closed_ = false;

    void brain(double dt, StageState& st);
    void hands_update(double dt, StageState& st);
    void choose_lean(const StageState& st);
    void face_update(double dt, StageState& st);
    void pose_out(StageState& st);
    void emit(const std::string& sound, float gain = 1, float rate = 1);
    void say(const std::string& category, double chance = 1);
    V3 rest_world(const StageState& st, int side) const;
    static double lid_clearance(V3 head, double scale);
};

}  // namespace sbx
