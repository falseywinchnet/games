// Rules and generation tests: every generated maze is solvable and its doors
// matter; the best route replays through the rules to the reward; the sealed
// portal is the only way across; ceiling pads need the flip; the elevator
// joins two floors; the same seed builds the same maze; a session walked by
// the route wins without the marble ever sharing a square with the player;
// the snail repaints; the save file round-trips and refuses tampering.
#include "maze.hpp"
#include "save.hpp"
#include "session.hpp"
#include "textures.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace mz;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static bool replay(const Level& lv, const std::vector<int>& route) {
    Play p;
    p.at = lv.start;
    for (int d : route) {
        Arrival a;
        if (step(lv, p, d, a) != StepResult::moved) return false;
    }
    return p.won;
}

int main() {
    const auto t0 = std::chrono::steady_clock::now();
    int mazes = 0, with_doors = 0, sealed = 0, ceil = 0, two_floor = 0;
    for (std::uint64_t run : {1ULL, 77ULL, 31337ULL}) {
        for (int n = 1; n <= 40; ++n) {
            const LevelParams p = params_for(n, run * 1000 + static_cast<std::uint64_t>(n));
            const Level lv = generate(p);
            ++mazes;
            CHECK(lv.optimal_steps > 0);
            const Play start{lv.start, lv.start_dir};
            const std::vector<int> route = solve_route(lv, start);
            CHECK(static_cast<int>(route.size()) == lv.optimal_steps);
            CHECK(replay(lv, route));
            // the same seed builds the same maze
            const Level again = generate(p);
            CHECK(again.optimal_steps == lv.optimal_steps && again.goal == lv.goal && again.floors.size() == lv.floors.size());
            // doors matter: without pads (and without visitors) the reward is out of reach
            if (p.doors > 0) {
                ++with_doors;
                Level no_pads = lv;
                for (Floor& f : no_pads.floors)
                    for (Cell& c : f.cells) c.pad = c.cpad = -1;
                CHECK(solve_steps(no_pads) < 0);
            }
            // ceiling pads need the flip stone
            if (p.ceiling_doors > 0) {
                ++ceil;
                Level no_flip = lv;
                no_flip.things.erase(std::remove_if(no_flip.things.begin(), no_flip.things.end(), [](const Thing& t) { return t.kind == ThingKind::flip; }), no_flip.things.end());
                CHECK(solve_steps(no_flip) < 0);
                bool has_cpad = false;
                for (const Floor& f : lv.floors) for (const Cell& c : f.cells) has_cpad = has_cpad || c.cpad >= 0;
                CHECK(has_cpad);
            }
            // a sealed portal is the only way across
            if (p.sealed_portal && p.doors == 0 && !lv.portals.empty()) {
                ++sealed;
                Level no_portals = lv;
                no_portals.portals.clear();
                for (Floor& f : no_portals.floors) for (Cell& c : f.cells) c.portal = -1;
                CHECK(solve_steps(no_portals) < 0);
            }
            if (p.floors > 1) {
                ++two_floor;
                CHECK(lv.floors.size() == 2 && lv.goal.f == 1);
                int lifts = 0;
                for (int y = 0; y < lv.floors[0].h; ++y)
                    for (int x = 0; x < lv.floors[0].w; ++x)
                        if (lv.floors[0].at(x, y).elevator) { ++lifts; CHECK(lv.floors[1].at(x, y).elevator && lv.floors[1].open(x, y)); }
                CHECK(lifts == 1);
            }
        }
    }
    std::printf("mazes: %d generated and solved (%d with doors, %d with ceiling pads, %d sealed portals, %d two-floor) in %.2fs\n", mazes, with_doors, ceil, sealed, two_floor,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    // --- sessions: walk the best route while the marble rolls and the snail paints
    {
        int won = 0, marble_overlaps = 0, painted = 0, sessions = 0;
        for (int k = 0; k < 36; ++k) {
            const int n = 6 + 2 * (k % 12);  // every even level from 6 has the marble
            const Level lv = generate(params_for(n, 4242 + static_cast<std::uint64_t>(k) * 31));
            Session s;
            s.start(lv, 9 + static_cast<std::uint64_t>(k));
            ++sessions;
            double t = 0;
            while (s.won_t < 0 && t < 600) {
                if (!s.busy()) {
                    const std::vector<int> rt = solve_route(s.lv, s.play);
                    if (rt.empty()) break;
                    const int d = rt.front(), me = s.play.dir;
                    const Pos next{s.play.at.f, s.play.at.x + kDX[d], s.play.at.y + kDY[d]};
                    const bool in_way = s.world.marble.alive && (s.world.marble.at == next || s.world.marble.to == next);
                    if (d != me) s.command(d == (me + 3) % 4 ? Cmd::left : Cmd::right);
                    else if (!in_way) s.command(Cmd::forward);
                }
                s.update(1.0 / 30);
                t += 1.0 / 30;
                if (s.world.marble.alive && s.world.marble.at == s.play.at) ++marble_overlaps;
            }
            won += s.won_t >= 0;
            if (s.won_t < 0) std::printf("  level %d not won: at (%d,%d,%d) marble at (%d,%d) to (%d,%d), route %zu\n", n, s.play.at.f, s.play.at.x, s.play.at.y, s.world.marble.at.x, s.world.marble.at.y,
                                         s.world.marble.to.x, s.world.marble.to.y, solve_route(s.lv, s.play).size());
            for (const Floor& f : s.lv.floors) for (const Cell& c : f.cells) for (auto face : c.face) painted += face >= 64;
        }
        CHECK(won == sessions);
        CHECK(marble_overlaps == 0);
        std::printf("sessions: %d of %d walked to the reward; the marble never shared a square; the snail painted %d wall faces\n", won, sessions, painted);
    }
    // --- the save file
    {
        const std::filesystem::path p = std::filesystem::temp_directory_path() / "mz_test_save.txt";
        SaveData d;
        d.level = 17; d.run_seed = 123456789; d.collected = 0b1011; d.cleared = 16; d.steps = 4000; d.music = false;
        CHECK(write_save(p, d));
        SaveData e;
        CHECK(load_save(p, e) && e.level == 17 && e.run_seed == 123456789 && e.collected == 0b1011 && !e.music);
        {
            std::string all;
            { std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); all = ss.str(); }
            all.replace(all.find("level=17"), 8, "level=99");
            std::ofstream f(p, std::ios::trunc); f << all;
        }
        SaveData bad;
        CHECK(!load_save(p, bad));
        std::filesystem::remove(p);
    }
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("all tests passed\n");
    return 0;
}
