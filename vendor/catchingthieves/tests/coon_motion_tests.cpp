// Raccoons move continuously: whatever the frame timing, a raccoon's rise out
// of its burrow and its position change only by bounded amounts per frame and
// stay between hidden and standing on the rim. The view clamps a frame to a
// quarter second and speeds a queued walk by 1.6, so steps reach 0.4 s.
#include "garden.hpp"
#include "show.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
std::uint64_t next(std::uint64_t& s) {
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
    return s;
}
double unit(std::uint64_t& s) { return static_cast<double>(next(s) >> 11) * (1.0 / 9007199254740992.0); }

// Highest a raccoon may stand (1.6 on the rim) with a little spring overshoot.
constexpr double kMaxRise = 2.0;
// Fastest plausible rise: a duck from standing peaks near 20 per second.
constexpr double kMaxRiseSpeed = 25.0;

void run(std::uint64_t seed) {
    ct::Level level;
    require(ct::Level::parse("##########\n#.  $   .#\n#   $@   #\n#.  $  $.#\n#        #\n##########\n", level), "parse fixture");
    ct::Board board;
    require(board.load(level), "load fixture");
    ct::Garden garden;
    garden.set_level(level); garden.resize(300, 200, 88);
    ct::Show show(seed);
    show.set_level(board, garden, ct::Season::summer);
    std::uint64_t r = seed * 2654435761ULL + 1;
    std::vector<ct::CoonPose> last = show.state().coons;
    for (int frame = 0; frame < 6000; ++frame) {
        const double roll = unit(r);
        if (roll < .04 || (!show.busy() && roll < .2)) {
            ct::Move move;
            if (unit(r) < .15 && board.undo(&move)) show.moved(board, move, true);
            else if (board.move(static_cast<int>(next(r) % 4), &move)) show.moved(board, move, false);
            else show.blocked(static_cast<int>(next(r) % 4));
        }
        if (unit(r) < .005) show.stuck(unit(r) < .5);
        // frame timing: mostly smooth, with stalls up to the view's clamp
        double dt = 1.0 / 60.0;
        const double timing = unit(r);
        if (timing < .1) dt = .25 * 1.6 * unit(r);
        else if (timing < .2) dt = .25;
        else if (timing < .25) dt = 0;
        if (unit(r) < .003) { show.settle(board); last = show.state().coons; continue; }
        show.update(dt, board);
        show.cues.clear();
        const std::vector<ct::CoonPose>& now = show.state().coons;
        for (std::size_t i = 0; i < now.size(); ++i) {
            const std::string where = "seed " + std::to_string(seed) + " frame " + std::to_string(frame) + " raccoon " + std::to_string(i);
            require(std::isfinite(now[i].rise) && now[i].rise >= 0 && now[i].rise <= kMaxRise,
                    where + ": rise " + std::to_string(now[i].rise) + " leaves the burrow range");
            const double jump = std::fabs(now[i].rise - last[i].rise);
            // a pumpkin landing on the burrow shuts a raccoon in at once, out of sight
            const bool shut_in = show.state().trapped[i] && now[i].rise == 0;
            require(jump <= kMaxRiseSpeed * dt + 1e-9 || shut_in,
                    where + ": rise jumps " + std::to_string(jump) + " in " + std::to_string(dt) + " s");
            const double moved = std::hypot(now[i].pos.x - last[i].pos.x, now[i].pos.y - last[i].pos.y) + std::fabs(now[i].pos.z - last[i].pos.z);
            // the only reposition is stepping forward to wave the flag, from inside the burrow
            require(moved < 1e-9 || last[i].rise <= .02 || now[i].rise <= .02, where + ": a visible raccoon is moved " + std::to_string(moved));
        }
        last = now;
    }
}
}  // namespace

int main() {
    try {
        for (std::uint64_t seed = 1; seed <= 64; ++seed) run(seed);
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
    std::cout << "Catching Thieves: raccoon motion stays continuous across 64 seeds.\n";
}
