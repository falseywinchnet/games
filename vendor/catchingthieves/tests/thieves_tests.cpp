// Rules, solver, generator and campaign tests: parsing, moves and undo, the
// solver against replay, the stuck check never crying wolf, generated levels
// carrying optimal solutions, every campaign level's solution replayed
// through the rules, and the save file.
#include "gen.hpp"
#include "level.hpp"
#include "levelset.hpp"
#include "save.hpp"
#include "solver.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace ct;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static Level L(const char* xsb) {
    Level lv;
    std::string err;
    if (!Level::parse(xsb, lv, &err)) std::printf("parse error: %s\n", err.c_str());
    return lv;
}

int main(int argc, char** argv) {
    const std::string campaign = argc > 1 ? argv[1] : (std::filesystem::path(__FILE__).parent_path().parent_path() / "assets/levels/campaign.txt").string();
    // --- parsing and the text format
    {
        const Level lv = L("#######\n#@ $ .#\n#######\n");
        CHECK(lv.w == 7 && lv.h == 3 && lv.boxes.size() == 1 && lv.goals() == 1 && lv.player == lv.idx(1, 1));
        Level back;
        CHECK(Level::parse(lv.xsb(), back) && back.xsb() == lv.xsb());
        Level bad;
        CHECK(!Level::parse("#####\n#@ $ \n#####\n", bad));      // not closed
        CHECK(!Level::parse("#####\n#@$$.#\n######\n", bad));    // two pumpkins, one burrow
        CHECK(!Level::parse("####\n#  #\n####\n", bad));         // no bear
    }
    // --- moving, pushing, undo
    {
        Board b;
        CHECK(b.load(L("########\n#@ $  .#\n########\n")));
        Move m;
        CHECK(b.move(kRight, &m) && !m.push);
        CHECK(b.move(kRight, &m) && m.push && b.pushes() == 1);
        CHECK(!b.move(kUp));                                     // a hedge
        CHECK(b.replay("RR") && b.solved() && b.moves() == 4);
        CHECK(!b.move(kRight));                                  // the pumpkin is against the hedge now
        CHECK(b.undo() && !b.solved() && b.pushes() == 2);
        while (b.undo()) {}
        CHECK(b.moves() == 0 && b.player() == b.level().player && b.boxes() == b.level().boxes);
        CHECK(!b.replay("rr"));                                  // a push written as a walk is refused
        b.restart();
        CHECK(!b.replay("rRu"));
    }
    // --- the stuck check: certain dead ends only
    {
        Board b;
        b.load(L("######\n#    #\n# $  #\n#@ # #\n#  #.#\n#    #\n######\n"));
        CHECK(!b.stuck());
        b.replay("uRuR");  // the pumpkin up against the top hedge, away from the burrow column
        // whatever the classification, it must agree with the solver whenever it says stuck
        if (b.stuck()) CHECK(!solve(b, 200000).solved);
        Board c;
        c.load(L("#####\n#  .#\n# $ #\n# @ #\n#####\n"));
        CHECK(!c.stuck() && solve(c).solved);
        c.replay("ruL");   // against the side hedge, in a column with no burrow: never coming back
        CHECK(c.stuck());
        CHECK(!solve(c, 200000).solved && !solve(c, 200000).exhausted);
    }
    // --- random play on small generated levels: stuck() never claims a solvable board
    {
        int checks = 0, stuck_seen = 0;
        for (std::uint64_t seed = 1; seed <= 25; ++seed) {
            GenParams p;
            p.w = 6; p.h = 6; p.boxes = 2; p.min_pushes = 6; p.seed = seed;
            const GenResult r = generate(p);
            CHECK(r.ok);
            if (!r.ok) continue;
            std::uint64_t h = seed * 2654435761ULL;
            Board b;
            b.load(r.level);
            for (int step = 0; step < 300; ++step) {
                h ^= h << 13; h ^= h >> 7; h ^= h << 17;
                b.move(static_cast<int>(h % 4));
                if (b.stuck()) {
                    ++stuck_seen;
                    const SolveResult sr = solve(b, 300000);
                    CHECK(!sr.solved);
                    ++checks;
                    b.undo();
                }
            }
        }
        std::printf("stuck check: %d stuck positions met in random play, none solvable\n", stuck_seen);
        static_cast<void>(checks);
    }
    // --- the solver: its answer replays, and it is optimal on hand-checked boards
    {
        const SolveResult a = solve(L("#######\n#@ $ .#\n#######\n"));
        CHECK(a.solved && a.pushes == 2 && a.lurd == "rRR");
        const Level two = L("########\n#  .   #\n# $##$ #\n#@   . #\n########\n");
        const SolveResult b = solve(two);
        CHECK(b.solved && b.pushes == 3);
        Board bb;
        bb.load(two);
        CHECK(bb.replay(b.lurd) && bb.solved() && bb.pushes() == 3);
        const SolveResult none = solve(L("#####\n#@$.#\n# $ #\n#  .#\n#####\n"), 200000);
        CHECK(none.solved || !none.exhausted);
    }
    // --- the generator: solvable by construction, confirmed forwards, and optimal
    {
        const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        for (std::uint64_t seed = 100; seed < 112; ++seed) {
            GenParams p;
            p.w = 7; p.h = 7; p.boxes = 3; p.min_pushes = 14; p.seed = seed;
            const GenResult r = generate(p);
            CHECK(r.ok);
            if (!r.ok) continue;
            Board b;
            b.load(r.level);
            CHECK(b.on_goal() == 0);                       // every pumpkin starts off its burrow
            CHECK(b.replay(r.level.solution) && b.solved() && b.pushes() == r.pushes);
            CHECK(solve(r.level).pushes == r.pushes);      // nothing shorter exists
            GenParams q = p;
            CHECK(generate(q).level.xsb() == r.level.xsb());  // the same seed, the same garden
        }
        std::printf("generator: 12 gardens verified (%.1fs)\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    }
    // --- the campaign: every level loads, its solution replays to a win in exactly its par of pushes
    {
        std::ifstream f(campaign);
        std::stringstream ss;
        ss << f.rdbuf();
        std::vector<LevelEntry> levels;
        std::string err;
        CHECK(load_levels(ss.str(), levels, &err));
        if (!err.empty()) std::printf("%s\n", err.c_str());
        CHECK(levels.size() == 243);
        int replayed = 0, resolved = 0;
        const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        std::string last_section;
        for (const LevelEntry& e : levels) {
            Board b;
            CHECK(b.load(e.level));
            const bool ok = b.replay(e.level.solution) && b.solved() && b.pushes() == e.par;
            CHECK(ok);
            if (!ok) std::printf("  level '%s' (%s) fails its replay\n", e.level.title.c_str(), e.section.c_str());
            replayed += ok;
            CHECK(!e.level.title.empty() && !e.section.empty());
            // par really is the fewest pushes: re-solved from scratch for the quicker sections
            if (e.par <= 30) {
                const SolveResult sr = solve(e.level, 3000000);
                CHECK(sr.solved && sr.pushes == e.par);
                ++resolved;
            }
        }
        std::printf("campaign: %zu levels, %d solutions replayed, %d pars re-solved (%.1fs)\n", levels.size(), replayed, resolved,
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        // the text format round-trips
        std::vector<LevelEntry> again;
        CHECK(load_levels(save_levels(levels), again) && again.size() == levels.size());
        bool same = true;
        for (size_t i = 0; i < levels.size() && i < again.size(); ++i)
            same = same && again[i].level.xsb() == levels[i].level.xsb() && again[i].level.solution == levels[i].level.solution && again[i].par == levels[i].par;
        CHECK(same);
    }
    // --- the save file
    {
        const std::filesystem::path p = std::filesystem::temp_directory_path() / "ct_test_save.txt";
        SaveData d;
        d.level = 12;
        d.history = "rRuL";
        d.records[3] = {40, 12};
        d.records[7] = {80, 30};
        d.endless_xsb = "#####|#@$.#|#####";
        d.endless_tier = 2;
        d.settings.music = false;
        CHECK(write_save(p, d));
        SaveData e;
        CHECK(load_save(p, e));
        CHECK(e.level == 12 && e.history == "rRuL" && e.records.size() == 2 && e.records[3].best_pushes == 12 && e.endless_xsb == d.endless_xsb && !e.settings.music);
        {
            std::string all;
            { std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); all = ss.str(); }
            all.replace(all.find("level=12"), 8, "level=99");
            std::ofstream f(p, std::ios::trunc);
            f << all;
        }
        SaveData bad;
        CHECK(!load_save(p, bad));
        std::filesystem::remove(p);
    }
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("all tests passed\n");
    return 0;
}
