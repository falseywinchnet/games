#include "mole.hpp"

#include <algorithm>

namespace sbx {

double Mole::rand01() {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 7; rng_ ^= rng_ << 17;
    return static_cast<double>(rng_ >> 11) * (1.0 / 9007199254740992.0);
}

void Mole::start(std::uint64_t seed) {
    rng_ = seed * 0x9E3779B97F4A7C15ULL | 1;
    active_ = true;
    finished_ = false;
    t_ = 0;
    next_ = kLead;
    hits_ = spawned_ = 0;
    up_.fill(0);
}

void Mole::update(double dt) {
    finished_ = false;
    if (!active_) return;
    t_ += dt;
    for (double& u : up_) u = std::max(0.0, u - dt);
    if (t_ >= kLead + kDuration) {
        up_.fill(0);
        active_ = false;
        finished_ = true;
        return;
    }
    // pops come faster and stay up for less time as the half minute runs out
    const double p = std::clamp((t_ - kLead) / kDuration, 0.0, 1.0);
    if (t_ >= next_) {
        int live = 0;
        for (double u : up_) live += u > 0;
        if (live < 3) {
            int pick = static_cast<int>(rand01() * kSwitches);
            for (int k = 0; k < kSwitches && up_[static_cast<size_t>(pick)] > 0; ++k) pick = (pick + 1) % kSwitches;
            if (up_[static_cast<size_t>(pick)] <= 0) {
                up_[static_cast<size_t>(pick)] = 1.05 - .4 * p + .2 * rand01();
                ++spawned_;
            }
        }
        next_ = t_ + .5 - .2 * p + .15 * rand01();
    }
}

bool Mole::hit(int sw) {
    if (!playing() || sw < 0 || sw >= kSwitches || up_[static_cast<size_t>(sw)] <= 0) return false;
    up_[static_cast<size_t>(sw)] = 0;
    ++hits_;
    return true;
}

}  // namespace sbx
