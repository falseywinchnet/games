#pragma once
// Whack-a-mole: for thirty seconds the switches pop up out of their sockets
// and drop back; click one while it is up to whack it. Pure timing and
// scoring, no drawing.
#include "puzzle.hpp"

#include <array>
#include <cstdint>

namespace sbx {

class Mole {
public:
    static constexpr double kDuration = 30, kLead = 1.4;  // play time, and the "ready" pause before it
    static constexpr int kGreat = 40;                     // hits for her special message

    void start(std::uint64_t seed);
    void update(double dt);
    bool hit(int sw);                // true if it was up (it drops at once)
    bool active() const { return active_; }
    bool playing() const { return active_ && t_ >= kLead; }
    bool finished() const { return finished_; }  // just ended this frame
    bool up(int sw) const { return up_[static_cast<size_t>(sw)] > 0; }
    int hits() const { return hits_; }
    double left() const { return std::max(0.0, kDuration - std::max(0.0, t_ - kLead)); }
    int spawned() const { return spawned_; }

private:
    bool active_ = false, finished_ = false;
    double t_ = 0, next_ = 0;
    int hits_ = 0, spawned_ = 0;
    std::array<double, kSwitches> up_{};  // seconds left above the deck
    std::uint64_t rng_ = 1;
    double rand01();
};

}  // namespace sbx
