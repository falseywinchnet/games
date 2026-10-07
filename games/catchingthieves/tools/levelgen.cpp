// Level generation from the command line.
//   levelgen sample W H BOXES MINPUSH COUNT [SEED]   print stats (and the levels) for a quick look
//   levelgen tables IN.txt OUT.txt                    the garden tables: the lessons, then IN's gardens measured into tiers
//   levelgen measure TABLE.txt                        every garden's difficulty metrics
//   levelgen timing COUNT [SEED]                      how long fresh gardens take to grow at each tier
#include "gen.hpp"
#include "levelset.hpp"
#include "solver.hpp"
#include "tiers.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <cmath>
#include <set>
#include <sstream>
#include <thread>

using namespace ct;

namespace {
// The tutorial: short gardens made by hand, each showing one mechanism. The
// first three were the old book's openers and keep their ids.
struct Lesson { int id; const char* title; const char* lesson; const char* xsb; };
const Lesson kLessons[] = {
    {0, "The First Burrow", "Walk into a pumpkin to push it. Roll it onto the burrow.",
     "#######\n"
     "#@ $ .#\n"
     "#######\n"},
    {1, "Round the Bend", "To push a pumpkin another way, walk round to its other side.",
     "######\n"
     "#    #\n"
     "# $  #\n"
     "#@ # #\n"
     "#  #.#\n"
     "#    #\n"
     "######\n"},
    {2, "Two of Them", "Every burrow needs a pumpkin. Cover them all to win.",
     "########\n"
     "#  .   #\n"
     "# $##$ #\n"
     "#@   . #\n"
     "########\n"},
    {243, "No Pulling", "The bear can push but never pull. Walk round behind the pumpkin.",
     "########\n"
     "#      #\n"
     "#  .@$ #\n"
     "#      #\n"
     "########\n"},
    {244, "Hedge Trouble", "A pumpkin against a hedge with no burrow along it is stuck for good.",
     "########\n"
     "#      #\n"
     "#.  $  #\n"
     "#   @  #\n"
     "########\n"},
    {245, "Along the Hedge", "A pumpkin against a hedge can still slide along it.",
     "########\n"
     "# $   .#\n"
     "#      #\n"
     "#@     #\n"
     "########\n"},
    {246, "The Far One First", "Fill the far burrow first, or one pumpkin blocks the other.",
     "#########\n"
     "#     ###\n"
     "#@ $ $..#\n"
     "#     ###\n"
     "#########\n"},
    {247, "One at a Time", "The bear can push only one pumpkin. Two in a row won't budge.",
     "########\n"
     "#      #\n"
     "#      #\n"
     "#@$$ ..#\n"
     "#      #\n"
     "#      #\n"
     "########\n"},
    {248, "Into the Corner", "A pumpkin in a corner can never move again, so only a burrow there will do.",
     "#######\n"
     "#.   .#\n"
     "#  $  #\n"
     "# $@  #\n"
     "#     #\n"
     "#######\n"},
    {250, "Take It Back", "Z or Backspace takes back a step. Try a push, then undo it.",
     "#######\n"
     "#.  ###\n"
     "# $   #\n"
     "##$@  #\n"
     "#.    #\n"
     "#######\n"},
    {249, "Ask for a Hint", "Not sure? Hint in the bar shows a good next push.",
     "#######\n"
     "#. #  #\n"
     "# $$  #\n"
     "#.  @ #\n"
     "#  #  #\n"
     "#######\n"},
};

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && !std::strcmp(argv[1], "timing")) {
        // how long a fresh garden takes to grow at each tier, single-threaded
        const int count = std::atoi(argv[2]);
        const std::uint64_t base = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 1;
        for (int d = kEasy; d < kDifficulties; ++d) {
            std::vector<double> times;
            int failed = 0;
            for (int i = 0; i < count; ++i) {
                const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
                const Grown g = grow(d, base + static_cast<std::uint64_t>(i) * 7919);
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                times.push_back(ms);
                if (!g.ok) { ++failed; std::printf("%s seed %d: failed after %d tries (%.0f ms)\n", difficulty_name(d), i, g.tries, ms); continue; }
                const GardenMetrics& m = g.metrics;
                std::printf("%s %d: %.0f ms, %d tries, boxes %d soil %d corridor %d pushes %d moves %d switches %d nodes %lld traps %d/%d score %.1f\n",
                            difficulty_name(d), i, ms, g.tries, m.boxes, m.soil, m.corridor, m.pushes, m.moves, m.switches, m.nodes, m.traps, m.offered, m.score());
                std::fflush(stdout);
            }
            std::sort(times.begin(), times.end());
            std::printf("== %s: median %.0f ms, 90th %.0f ms, worst %.0f ms, %d failed of %d\n", difficulty_name(d), times[times.size() / 2],
                        times[times.size() * 9 / 10], times.back(), failed, count);
        }
        return 0;
    }
    if (argc >= 2 && !std::strcmp(argv[1], "lessons")) {
        // each lesson's first pushes: which wedge a pumpkin at once, which leave it unsolvable
        for (const Lesson& lesson : kLessons) {
            Level level;
            std::string error;
            Level::parse(lesson.xsb, level, &error);
            const SolveResult best = solve(level);
            std::printf("%s: par %d %s\n", lesson.title, best.pushes, best.lurd.c_str());
            Board board;
            board.load(level);
            for (int cell = 0; cell < level.w * level.h; ++cell) {
                if (!level.floor(cell) || board.box_at(cell) >= 0) continue;
                for (int d = 0; d < 4; ++d) {
                    const int n = level.step(cell, d);
                    if (board.box_at(n) < 0) continue;
                    Level here = level;
                    here.player = cell;
                    Board trial;
                    trial.load(here);
                    if (!trial.move(d)) continue;
                    const SolveResult after = solve(trial, 200000);
                    std::printf("   push %c from %d,%d: %s%s\n", kPush[d], cell % level.w, cell / level.w, trial.stuck() ? "stuck " : "",
                                after.solved ? "solvable" : "unsolvable");
                }
            }
        }
        return 0;
    }
    if (argc >= 3 && !std::strcmp(argv[1], "measure")) {
        std::ifstream in(argv[2]);
        std::stringstream text;
        text << in.rdbuf();
        std::vector<LevelEntry> levels;
        std::string error;
        if (!load_levels(text.str(), levels, &error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
        std::printf("index section boxes soil corridor pushes moves switches nodes offered traps share score\n");
        for (size_t i = 0; i < levels.size(); ++i) {
            const GardenMetrics m = measure(levels[i].level);
            std::printf("%zu \"%s\" %d %d %d %d %d %d %lld %d %d %.3f %.1f\n", i, levels[i].section.c_str(), m.boxes, m.soil, m.corridor, m.pushes, m.moves,
                        m.switches, m.nodes, m.offered, m.traps, m.trap_share(), m.score());
            std::fflush(stdout);
        }
        return 0;
    }
    if (argc >= 7 && !std::strcmp(argv[1], "sample")) {
        GenParams p;
        p.w = std::atoi(argv[2]); p.h = std::atoi(argv[3]); p.boxes = std::atoi(argv[4]); p.min_pushes = std::atoi(argv[5]);
        const int count = std::atoi(argv[6]);
        const std::uint64_t seed = argc > 7 ? std::strtoull(argv[7], nullptr, 10) : 1;
        for (int i = 0; i < count; ++i) {
            p.seed = seed + static_cast<std::uint64_t>(i) * 7919;
            const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
            const GenResult r = generate(p);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (!r.ok) { std::printf("seed %llu: failed after %d attempts (%.0f ms): carve %d shallow %d unverified %d deepest %d\n", static_cast<unsigned long long>(p.seed), r.attempts, ms, r.carve_rejects, r.shallow, r.unverified, r.deepest); continue; }
            std::printf("seed %llu: pushes %d moves %d switches %d nodes %lld attempts %d (%.0f ms)\n%s\n", static_cast<unsigned long long>(p.seed), r.pushes, r.moves,
                        r.box_lines, r.solver_nodes, r.attempts, ms, r.level.xsb().c_str());
        }
        return 0;
    }
    if (argc >= 4 && !std::strcmp(argv[1], "tables")) {
        // The garden tables: the hand-made lessons, then every generated garden of IN
        // (the old book or an earlier table) measured and filed under its tier. Ids are kept.
        std::ifstream in(argv[2]);
        std::stringstream text;
        text << in.rdbuf();
        std::vector<LevelEntry> source, all;
        std::string error;
        if (!load_levels(text.str(), source, &error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
        for (const Lesson& lesson : kLessons) {
            LevelEntry e;
            if (!Level::parse(lesson.xsb, e.level, &error)) { std::fprintf(stderr, "lesson %s: %s\n", lesson.title, error.c_str()); return 1; }
            const SolveResult sr = solve(e.level);
            if (!sr.solved) { std::fprintf(stderr, "lesson %s does not solve\n", lesson.title); return 1; }
            e.id = lesson.id;
            e.tier = tier_rule(kTutorial).key;
            e.lesson = lesson.lesson;
            e.level.title = lesson.title;
            e.section = difficulty_name(kTutorial);
            e.par = sr.pushes;
            e.level.solution = sr.lurd;
            e.switches = box_switches(e.level, sr.lurd);
            e.nodes = sr.nodes;
            all.push_back(e);
            std::fprintf(stderr, "lesson %-18s par %d, %lld nodes\n", lesson.title, sr.pushes, sr.nodes);
        }
        int counts[kDifficulties] = {};
        for (size_t i = 0; i < source.size(); ++i) {
            LevelEntry e = source[i];
            if (e.id < 0) e.id = static_cast<int>(i);
            if (e.tier == "tutorial" || e.section == "First Steps") continue;
            const GardenMetrics m = measure(e.level, e.nodes);
            const int d = classify(m);
            if (d < 0) { std::fprintf(stderr, "garden %d (%s) fits no tier: score %.1f\n", e.id, e.level.title.c_str(), m.score()); return 1; }
            e.tier = tier_rule(d).key;
            e.section = difficulty_name(d);
            e.nodes = m.nodes;
            all.push_back(e);
            ++counts[d];
        }
        std::ofstream f(argv[3]);
        f << "; Catching Thieves: the garden tables. Made by tools/levelgen.cpp tables.\n"
             "; Tutorial gardens are hand-made lessons. The rest were generated backwards from solved positions,\n"
             "; solved push-optimally by an independent forward solver, replayed through the rules, measured and\n"
             "; filed under the tier their metrics fall in (src/tiers.cpp). Ids are permanent: saves refer to them.\n\n";
        f << save_levels(all);
        std::fprintf(stderr, "%zu gardens written to %s: easy %d, medium %d, hard %d\n", all.size(), argv[3], counts[kEasy], counts[kMedium], counts[kHard]);
        return 0;
    }
    std::fprintf(stderr, "usage: levelgen sample W H BOXES MINPUSH COUNT [SEED]\n       levelgen tables IN.txt OUT.txt\n"
                         "       levelgen measure TABLE.txt\n       levelgen timing COUNT [SEED]\n");
    return 1;
}
