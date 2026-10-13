#include "wanderer.hpp"

#include <vector>

namespace mz {

void Wanderer::reset(std::uint64_t seed) {
    state_ = seed * 0x9E3779B97F4A7C15ULL + 0x5EED;
    if (state_ == 0) state_ = 1;
    visits_.clear();
    here_ = 0;
    decided_ = false;
    chosen_ = -1;
    glance_ = -1;
    dawdled_ = false;
    walked_ = 0;
    knows_ = false;
}

double Wanderer::unit() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 7;
    state_ ^= state_ << 17;
    const double value = static_cast<double>(state_ >> 11) / 9007199254740992.0;
    return value;
}

std::uint64_t Wanderer::key_of(const Play& play) {
    const std::uint64_t key = static_cast<std::uint64_t>(play.at.f & 0xFF) | static_cast<std::uint64_t>(play.at.x & 0xFF) << 8 |
                              static_cast<std::uint64_t>(play.at.y & 0xFF) << 16 | static_cast<std::uint64_t>(play.flipped ? 1 : 0) << 24 |
                              static_cast<std::uint64_t>(play.pressed) << 32;
    return key;
}

Wanderer::Move Wanderer::next(const Level& level, const Play& play) {
    Move move{};
    // Wandered long enough: it has the hang of it now.
    const int budget = level.optimal_steps * 5 / 2 + 30;
    if (!knows_ && walked_ > budget) knows_ = true;
    if (knows_) {
        const std::vector<int> route = solve_route(level, play);
        if (route.empty()) {
            move.pause = 1;
            return move;
        }
        move.dir = route.front();
        move.walk = true;
        return move;
    }
    const std::uint64_t key = key_of(play);
    if (!decided_ || key != here_) {
        // Arrived somewhere (or just begun): remember it and choose a way on.
        if (decided_) ++walked_;
        here_ = key;
        decided_ = true;
        dawdled_ = unit() >= .08;   // now and then it will stop a moment here, for no reason it could give
        glance_ = -1;
        ++visits_[key];
        const int behind = (play.dir + 2) % 4;
        double best = 1e18;
        chosen_ = -1;
        std::vector<int> open{};
        for (int d = 0; d < 4; ++d) {
            Play ahead = play;
            Arrival arrival{};
            if (step(level, ahead, d, arrival) != StepResult::moved) continue;
            open.push_back(d);
            const std::unordered_map<std::uint64_t, int>::const_iterator seen = visits_.find(key_of(ahead));
            const int times = seen == visits_.end() ? 0 : (*seen).second;
            // least visited first; straight on is a little preferred, turning back a little avoided
            double score = times * 10.0 + unit() * 4.0;
            if (d == play.dir) score -= 2.5;
            if (d == behind) score += 3.0;
            if (score < best) {
                best = score;
                chosen_ = d;
            }
        }
        if (chosen_ < 0) {
            move.pause = 1;
            return move;
        }
        // a glance down another open way first, now and then, where the maze branches
        if (open.size() >= 3 && unit() < .18) {
            for (int d : open)
                if (d != chosen_ && d != behind && d != play.dir) {
                    glance_ = d;
                    break;
                }
        }
    }
    if (!dawdled_) {
        dawdled_ = true;
        move.pause = .6 + unit() * .8;
        return move;
    }
    if (glance_ >= 0) {
        if (play.dir != glance_) {
            move.dir = glance_;
            return move;
        }
        glance_ = -1;
        dawdled_ = true;
        move.pause = .5 + unit() * .4;
        return move;
    }
    move.dir = chosen_;
    move.walk = true;
    return move;
}

}  // namespace mz
