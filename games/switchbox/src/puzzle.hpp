#pragma once
// Switchbox rules: a hidden order of all six switches, a lamp per switch that
// lights when it is flipped in its turn, and her patience ladder. Pure logic:
// no animation, sound, clock, or files.
#include <array>
#include <cstdint>
#include <string>

namespace sbx {

constexpr int kSwitches = 6;

// What her patience ladder asks the character to do after an attempt ends.
enum class Reaction {
    none,
    hint_first,     // four all-wrong attempts before any progress: she points at the first switch
    forgot,         // all-wrong again after progress: "you forgot?!", points at the first switch
    scold,          // still all-wrong after pointing
    rip_out,        // still all-wrong after scolding: every switch but the first is pulled out
    restore,        // the first switch was flipped during a rip-out: she slots the others back
    stuck_scold,    // some right but stuck: the first time this combination
    stuck_grumble,  // still stuck: an occasional grumble
};

struct FlipResult {
    bool accepted = false;  // false: the switch is missing (ripped out) or the combination is already solved
    bool correct = false;   // its lamp lit
    bool solved = false;    // the whole combination is lit
    int lit = 0;            // lamps lit after the flip
    int depth = 0;          // for a wrong flip: lamps that were lit when the attempt ended
    Reaction reaction = Reaction::none;
};

struct PuzzleState {  // everything needed to save and restore a game in progress
    std::uint64_t rng = 0;
    std::array<int, kSwitches> combo{};
    int progress = 0, steps = 0;
    int zero_streak = 0, best_depth = 0, stall = 0, lost_level = 0, stuck_scolds = 0;
    int last_wrong = -1;  // flipping the same wrong switch again is not a new try (it leads to theft instead)
    bool ripped = false;
};

class Puzzle {
public:
    static constexpr int kHintAfter = 4;     // all-wrong attempts before the first pointing
    static constexpr int kForgotAfter = 3;   // all-wrong attempts after progress before re-pointing
    static constexpr int kEscalateAfter = 3; // further all-wrong attempts per escalation step
    static constexpr int kStuckAfter = 4;    // partial attempts without a new best before scolding

    explicit Puzzle(std::uint64_t seed);

    void new_combination();
    FlipResult flip(int sw);

    bool lamp(int sw) const;
    bool present(int sw) const { return !s_.ripped || sw == s_.combo[0]; }
    bool ripped() const { return s_.ripped; }
    bool solved() const { return s_.progress == kSwitches; }
    int progress() const { return s_.progress; }
    int steps() const { return s_.steps; }
    int first() const { return s_.combo[0]; }
    const std::array<int, kSwitches>& combination() const { return s_.combo; }

    const PuzzleState& state() const { return s_; }
    bool restore(const PuzzleState& st);  // rejects inconsistent states

private:
    PuzzleState s_;
    std::uint64_t next();
    Reaction attempt_ended(int depth, int sw);
};

}  // namespace sbx
