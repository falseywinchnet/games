#include "puzzle.hpp"

#include <algorithm>

namespace sbx {

Puzzle::Puzzle(std::uint64_t seed) {
    s_.rng = seed ? seed : 0x5B0C5EEDULL;
    new_combination();
}

std::uint64_t Puzzle::next() {  // splitmix64
    std::uint64_t z = (s_.rng += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void Puzzle::new_combination() {
    for (int i = 0; i < kSwitches; ++i) s_.combo[static_cast<size_t>(i)] = i;
    for (int i = kSwitches - 1; i > 0; --i) std::swap(s_.combo[static_cast<size_t>(i)], s_.combo[static_cast<size_t>(next() % static_cast<std::uint64_t>(i + 1))]);
    s_.progress = s_.steps = 0;
    s_.zero_streak = s_.best_depth = s_.stall = s_.lost_level = s_.stuck_scolds = 0;
    s_.last_wrong = -1;
    s_.ripped = false;
}

bool Puzzle::lamp(int sw) const {
    for (int i = 0; i < s_.progress; ++i)
        if (s_.combo[static_cast<size_t>(i)] == sw) return true;
    return false;
}

FlipResult Puzzle::flip(int sw) {
    FlipResult r;
    if (sw < 0 || sw >= kSwitches || solved() || !present(sw)) return r;
    r.accepted = true;
    ++s_.steps;
    if (s_.combo[static_cast<size_t>(s_.progress)] == sw) {
        ++s_.progress;
        s_.last_wrong = -1;
        r.correct = true;
        r.solved = solved();
        if (s_.ripped) { s_.ripped = false; r.reaction = Reaction::restore; }
    } else {
        r.depth = s_.progress;
        s_.progress = 0;
        r.reaction = attempt_ended(r.depth, sw);
    }
    r.lit = s_.progress;
    return r;
}

// One attempt ended at `depth` lamps. Two tracks: all-wrong attempts climb
// toward pointing, scolding and ripping out; partial attempts that stop
// improving earn a scold, then occasional grumbles. Beating the best depth
// clears the all-wrong track.
Reaction Puzzle::attempt_ended(int depth, int sw) {
    const bool repeat = depth == 0 && sw == s_.last_wrong;
    s_.last_wrong = depth == 0 ? sw : -1;
    if (repeat) return Reaction::none;
    if (depth > s_.best_depth) {
        s_.best_depth = depth;
        s_.stall = s_.zero_streak = s_.lost_level = 0;
        return Reaction::none;
    }
    if (depth > 0) {
        s_.zero_streak = 0;
        if (++s_.stall < kStuckAfter) return Reaction::none;
        s_.stall = 0;
        return s_.stuck_scolds++ == 0 ? Reaction::stuck_scold : Reaction::stuck_grumble;
    }
    ++s_.zero_streak;
    switch (s_.lost_level) {
        case 0: {
            const bool had_progress = s_.best_depth > 0;
            if (s_.zero_streak < (had_progress ? kForgotAfter : kHintAfter)) return Reaction::none;
            s_.zero_streak = 0;
            s_.lost_level = 1;
            return had_progress ? Reaction::forgot : Reaction::hint_first;
        }
        case 1:
            if (s_.zero_streak < kEscalateAfter) return Reaction::none;
            s_.zero_streak = 0;
            s_.lost_level = 2;
            return Reaction::scold;
        case 2:
            if (s_.zero_streak < kEscalateAfter) return Reaction::none;
            s_.zero_streak = 0;
            s_.lost_level = 3;
            s_.ripped = true;
            return Reaction::rip_out;
        default:
            return Reaction::none;
    }
}

bool Puzzle::restore(const PuzzleState& st) {
    std::array<bool, kSwitches> seen{};
    for (int v : st.combo) {
        if (v < 0 || v >= kSwitches || seen[static_cast<size_t>(v)]) return false;
        seen[static_cast<size_t>(v)] = true;
    }
    if (st.progress < 0 || st.progress >= kSwitches || st.steps < st.progress || st.lost_level < 0 || st.lost_level > 3) return false;
    if (st.ripped && st.progress != 0) return false;
    s_ = st;
    return true;
}

}  // namespace sbx
