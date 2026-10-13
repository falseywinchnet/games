// Rules and generation tests: every generated maze is solvable and its doors
// matter; the best route replays through the rules to the reward; the sealed
// portal is the only way across; ceiling pads need the flip; the elevator
// joins two floors; the same seed builds the same maze; a session walked by
// the route wins without the marble ever sharing a square with the player;
// the snail repaints; the save file round-trips and refuses tampering; the band
// plays each style through its first tune into the next, the same seed the same
// music however it is asked for, quieter and duller through the wall, and backwards.
#include "art.hpp"
#include "band.hpp"
#include "maze.hpp"
#include "png.hpp"
#include "save.hpp"
#include "session.hpp"
#include "textures.hpp"

#include <algorithm>
#include <cmath>
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
    long route_sum[3] = {};
    for (int d = 0; d < 3; ++d)
    for (std::uint64_t run : {1ULL, 77ULL, 31337ULL}) {
        for (int n = 1; n <= 40; ++n) {
            const LevelParams p = params_for(static_cast<Difficulty>(d), n, run * 1000 + static_cast<std::uint64_t>(n));
            const Level lv = generate(p);
            ++mazes;
            CHECK(lv.optimal_steps > 0);
            route_sum[d] += lv.optimal_steps;
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
    // every surface paints, at sizes the renderer wraps, and each list has calm and loud ones
    {
        const auto ta = std::chrono::steady_clock::now();
        bool pow2 = true, calm[3] = {}, loud[3] = {};
        for (int i = 0; i < floor_count(); ++i) {
            const Tex32& t = tex_floor(i);
            pow2 = pow2 && t.w == 256 && t.h == 256 && tex_floor_portal(i).w == 256;
            (floor_calm(i) ? calm[0] : loud[0]) = true;
        }
        for (int i = 0; i < wall_count(); ++i) {
            const Tex32& t = tex_wall(i, 1);
            pow2 = pow2 && (t.w & (t.w - 1)) == 0 && (t.h & (t.h - 1)) == 0 && t.w == 128 * wall_span(i) / (wall_span(i) > 1 ? 1 : 2);
            (wall_calm(i) ? calm[1] : loud[1]) = true;
        }
        for (int i = 0; i < ceiling_count(); ++i) {
            const Tex32& t = tex_ceiling_art(i);
            pow2 = pow2 && t.w == 256 && t.h == 256;
            (ceiling_calm(i) ? calm[2] : loud[2]) = true;
        }
        CHECK(pow2);
        CHECK(calm[0] && loud[0] && calm[1] && loud[1] && calm[2] && loud[2]);
        std::printf("art: %d floors, %d walls, %d ceilings painted in %.2fs\n", floor_count(), wall_count(), ceiling_count(),
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - ta).count());
    }
    // Medium and Hard mix every trick from their first maze; Hard's mazes are the longer walk
    CHECK(params_for(Difficulty::medium, 1, 5).doors > 0 || params_for(Difficulty::medium, 1, 5).sealed_portal);
    CHECK(route_sum[2] > route_sum[1]);
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
            painted += static_cast<int>(s.world.patches.size());
        }
        CHECK(won == sessions);
        CHECK(marble_overlaps == 0);
        std::printf("sessions: %d of %d walked to the reward; the marble never shared a square; the snail painted %d surfaces\n", won, sessions, painted);
    }
    // --- the pictures, as painted: every one decodes, 256 square (posters opaque); a damaged file is refused
    {
        const std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "posters";
        int posters = 0;
        for (;; ++posters) {
            const std::filesystem::path p = dir / ((posters < 10 ? "poster_0" : "poster_") + std::to_string(posters) + ".png");
            if (!std::filesystem::exists(p)) break;
            Tex32 t;
            CHECK(load_png(p.string(), t));
            CHECK(t.w == 256 && t.h == 256);
            CHECK(std::all_of(t.px.begin(), t.px.end(), [](std::uint32_t c) { return c >> 24 == 255; }));
        }
        CHECK(posters >= 25);
        int pieces = 0;
        for (;; ++pieces) {
            const std::filesystem::path p = dir / ((pieces < 10 ? "art_0" : "art_") + std::to_string(pieces) + ".png");
            if (!std::filesystem::exists(p)) break;
            Tex32 t;
            CHECK(load_png(p.string(), t));
            CHECK(t.w == 256 && t.h == 256);
        }
        CHECK(pieces >= 50);
        // (the folder is found from this file's path, which the build may record relative to
        // the build folder: run the tests from there, as CTest does)
        std::ifstream in(dir / "poster_00.png", std::ios::binary);
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(!bytes.empty());
        if (!bytes.empty()) {
            bytes[bytes.size() / 2] ^= 0x5A;
            Tex32 broken;
            CHECK(!decode_png(bytes, broken));
        }
        use_poster_art(dir.string());
        CHECK(poster_count() == posters && art_count() == pieces);
        // every maze hangs one or two posters and three or four pieces of art, the same each time
        for (int level = 1; level <= 40; ++level) {
            Level lv = generate(params_for(level, 99 + static_cast<std::uint64_t>(level)));
            hang_pictures(lv, 7 + static_cast<std::uint64_t>(level), posters, pieces);
            int on_posters = 0, on_art = 0;
            for (const Floor& f : lv.floors)
                for (const Cell& c : f.cells)
                    for (std::uint8_t look : c.face) {
                        on_posters += look >= 1 && look < 64;
                        on_art += look >= kArtLook;
                    }
            CHECK(on_posters >= 1 && on_posters <= 2);
            CHECK(on_art >= 3 && on_art <= 4);
            Level again = generate(params_for(level, 99 + static_cast<std::uint64_t>(level)));
            hang_pictures(again, 7 + static_cast<std::uint64_t>(level), posters, pieces);
            hang_pictures(again, 7 + static_cast<std::uint64_t>(level), posters, pieces);
            for (size_t f = 0; f < lv.floors.size(); ++f)
                for (size_t i = 0; i < lv.floors[f].cells.size(); ++i) CHECK(lv.floors[f].cells[i].face == again.floors[f].cells[i].face);
        }
        std::printf("pictures: %d posters and %d pieces of art decode; each maze hangs 1-2 and 3-4\n", posters, pieces);
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
    // the band
    {
        const int rate = MazeBand::sample_rate;
        for (int style = 0; style < 3; ++style) {
            // past the end of the first tune (the longest is 127 s), in game-sized blocks and in one go
            const std::size_t frames = static_cast<std::size_t>(rate) * 135;
            std::vector<float> blocks(frames * 2, 0.0F), whole(frames * 2, 0.0F);
            MazeBand a(7, static_cast<Style>(style)), b(7, static_cast<Style>(style));
            const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
            for (std::size_t at = 0; at < blocks.size(); at += 1024)
                a.render_add(std::span<float>(blocks.data() + at, std::min<std::size_t>(1024, blocks.size() - at)), 1.0);
            const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            b.render_add(whole, 1.0);
            CHECK(blocks == whole);
            CHECK(a.tunes_played() >= 1);
            double peak = 0, sum = 0, quietest = 0;
            bool finite = true;
            for (std::size_t window = 0; window + static_cast<std::size_t>(rate) * 10 <= frames; window += static_cast<std::size_t>(rate) * 10) {
                double part = 0;
                for (std::size_t i = window; i < window + static_cast<std::size_t>(rate) * 10; ++i) {
                    const double l = blocks[i * 2], r = blocks[i * 2 + 1];
                    finite = finite && std::isfinite(l) && std::isfinite(r);
                    peak = std::max({peak, std::abs(l), std::abs(r)});
                    part += .25 * (l + r) * (l + r);
                }
                sum += part;
                const double level = 10 * std::log10(part / (rate * 10.0) + 1e-20);
                quietest = window == 0 ? level : std::min(quietest, level);
            }
            const double level = 10 * std::log10(sum / static_cast<double>(frames) + 1e-20);
            CHECK(finite);
            CHECK(peak <= .85);
            CHECK(level > -17 && level < -13);
            CHECK(quietest > -30);   // no gaps: every ten seconds has music in it
            std::printf("band style %d: %.1f dB RMS, quietest 10 s %.1f dB, peak %.2f, %d bars, %.0fx real time\n", style, level, quietest, peak,
                        a.bars_played(), 135.0 / took);
        }
        // through the wall: quieter and dull; backwards: as loud as forwards
        const std::size_t frames = static_cast<std::size_t>(rate) * 20;
        std::vector<float> plain(frames * 2, 0.0F), dull(frames * 2, 0.0F), back(frames * 2, 0.0F);
        MazeBand p(3, Style::maze_run), d(3, Style::maze_run), k(3, Style::maze_run);
        d.muffle(true);
        k.backwards(true);
        p.render_add(plain, 1.0);
        d.render_add(dull, 1.0);
        k.render_add(back, 1.0);
        double e_plain = 0, e_dull = 0, e_back = 0, edge_plain = 0, edge_dull = 0;
        for (std::size_t i = static_cast<std::size_t>(rate) * 4; i < frames * 2; ++i) {   // after the wall has closed in (2 s)
            e_plain += plain[i] * plain[i];
            e_dull += dull[i] * dull[i];
            e_back += back[i] * back[i];
            edge_plain += (plain[i] - plain[i - 2]) * (plain[i] - plain[i - 2]);
            edge_dull += (dull[i] - dull[i - 2]) * (dull[i] - dull[i - 2]);
        }
        CHECK(10 * std::log10(e_plain / e_dull) > 4);
        std::printf("band through the wall: %.1f dB quieter, top end %.3f of the plain band's\n", 10 * std::log10(e_plain / e_dull), (edge_dull / e_dull) / (edge_plain / e_plain));
        CHECK(edge_dull / e_dull < .1 * edge_plain / e_plain);   // the top end is gone
        CHECK(std::abs(10 * std::log10(e_back / e_plain)) < 3);
        // a new level's style and tunes take over at the next bar, with no gap
        MazeBand change(5, Style::bonus_round);
        std::vector<float> before(static_cast<std::size_t>(rate) * 4 * 2, 0.0F), after(before.size(), 0.0F);
        change.render_add(before, 1.0);
        change.play(Style::turbo, 11);
        const int bars = change.bars_played();
        change.render_add(after, 1.0);
        CHECK(change.bars_played() > bars);
        CHECK(change.tunes_played() == 0);
        double e_after = 0;
        for (float v : after) e_after += static_cast<double>(v) * v;
        CHECK(e_after > 0);
    }
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("all tests passed\n");
    return 0;
}
