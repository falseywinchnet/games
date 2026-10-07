#pragma once
// The villain's performance. A gesture library of a composed gentleman
// (steepled fingers, an open-palmed presentation, a raised finger, a formal
// bow, consulting his pocket watch...) blended by springs, a stage manager
// that sequences them, and speech. Rules are
// decided before he hears about a guess; he only reacts to what the pins show.
#include "lair.hpp"
#include "script.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace fp {

struct Spring {
    double x = 0, v = 0, target = 0, k = 120, c = 14;
    void step(double dt) { v += (k * (target - x) - c * v) * dt; x += v * dt; }
    void snap(double to) { x = target = to; v = 0; }
};

struct Spring3 {
    V3 x{}, v{}, target{};
    double k = 160, c = 22;
    void step(double dt) { v = v + ((target - x) * k - v * c) * dt; x = x + v * dt; }
};

enum class Gesture {
    steeple,    // fingertips together before the chin: contemplation
    rest,       // gloved hands resting on the desk
    present,    // one palm turned up and out, the other hand at his chest: "behold"
    decree,     // an index finger raised: a pronouncement
    offer,      // an open palm toward the player: "after you"
    finger,     // a polite, admonishing finger beside his face
    inspect,    // leaning in to examine the guess
    grip,       // both hands planted on the desk edge: cold anger
    chuckle,    // a gloved hand to his mouth, shoulders shaking quietly
    bow,        // a formal bow, hand to his chest
    golf_clap,  // restrained applause
    chin,       // thumb and finger at his chin, considering
    tie,        // straightening his bow tie
    watch,      // consulting his pocket watch
    pinch,      // pinching the bridge of his nose
    recoil,     // affronted, hands raised slightly
    triumph     // standing, arms flung high and wide, head thrown back: composure abandoned in victory
};

struct Speech {      // one bubble: what he says, and how he looks while saying it
    std::string text;
    Gesture gesture = Gesture::steeple;
    VFace face;
    double hold = 0; // extra seconds after typing finishes
};

struct VCue {        // something for the view to play
    enum Kind { sound, shake } kind = sound;
    std::string name;
    float gain = 1, rate = 1;
};

class VillainActor {
public:
    explicit VillainActor(std::uint64_t seed = 1);

    void new_game();                              // field reset: he orates about his next plan
    void submitted(Score s, int turns_left, bool won, bool lost);
    bool take_reveal_pins();                      // his inspection is done: light the pins now
    bool take_reveal_secret();                    // the game ended: raise the secret
    bool take_apocalypse();                       // he has won: begin bringing the lair down
    void poked();
    void placed(int slot);                        // a peg went into a socket
    void watch(V3 world, bool valid) { watch_ = world; watch_valid_ = valid; }
    void set_talking(bool talking) { talking_ = talking; }

    void update(double dt, LairState& st);

    std::deque<Speech> speech;                    // the view types these out one at a time
    std::vector<VCue> cues;
    const std::string& plan() const { return target_; }
    bool performing() const { return phase_ != Phase::idle; }  // orating or reacting: hold input lightly
    void set_plan(const std::string& t) { target_ = t; }

private:
    enum class Phase { idle, oration, inspect, verdict, triumph, defeat };

    Script script_;
    std::uint64_t rng_;
    double rand01();

    Phase phase_ = Phase::idle;
    double phase_t_ = 0;
    Gesture gesture_ = Gesture::steeple;
    double gesture_t_ = 0;
    VFace face_;
    double face_hold_ = 0;
    std::string target_;
    Score last_{};
    int best_quality_ = -1;  // the player's best guess so far this game (exact counts most)
    int stall_ = 0;          // guesses in a row without improving on it
    int turns_left_ = kTurns;
    bool won_ = false, lost_ = false;
    bool reveal_pins_ = false, reveal_secret_ = false, apocalypse_ = false, laughing_ = false;
    bool talking_ = false;
    double idle_t_ = 6, blink_t_ = 2, blink_left_ = 0;
    V3 watch_{};
    bool watch_valid_ = false;
    bool slam_hit_ = false;

    Spring lean_, side_, squash_, head_yaw_, head_pitch_, head_roll_, rise_, glow_;
    std::array<Spring3, 2> hand_;
    std::array<Spring, 2> reach_, curl_, point_;
    std::array<Spring3, 2> palm_;
    double t_ = 0;

    double clap_last_ = -1;
    double watch_out_ = 0;
    void set_gesture(Gesture g) { if (g != gesture_) { gesture_ = g; gesture_t_ = 0; slam_hit_ = false; clap_last_ = -1; } }
    void say(const std::string& text, Gesture g, VFace f, double hold = .6);
    void pose_targets(const LairState& st);
    void face_update(double dt, LairState& st);
};

}  // namespace fp
