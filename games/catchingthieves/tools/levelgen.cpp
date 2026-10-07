// Level generation from the command line.
//   levelgen sample W H BOXES MINPUSH COUNT [SEED]   print stats (and the levels) for a quick look
//   levelgen campaign OUT.txt [PER_SECTION]           build the campaign: tutorials, then eight seasonal sections
#include "gen.hpp"
#include "levelset.hpp"
#include "solver.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <cmath>
#include <set>
#include <thread>

using namespace ct;

namespace {
struct Section {
    const char* name;
    int w0, h0, w1, h1;   // garden sizes, varied across the section
    int boxes0, boxes1;
    int min_pushes;
    long long budget;
};
const Section kSections[] = {
    {"Spring Sprouts", 5, 5, 6, 6, 2, 2, 7, 150000},
    {"Spring Rows", 6, 6, 7, 6, 2, 3, 12, 200000},
    {"Summer Patch", 7, 6, 7, 7, 3, 3, 16, 250000},
    {"Summer Orchard", 7, 7, 8, 7, 3, 3, 22, 300000},
    {"Autumn Field", 8, 7, 8, 8, 3, 4, 26, 400000},
    {"Autumn Maze", 8, 8, 9, 8, 4, 4, 30, 600000},
    {"Winter Frost", 9, 8, 9, 9, 4, 5, 33, 900000},
    {"Winter Night", 9, 9, 10, 9, 5, 6, 36, 1200000},
};

// three gentle openers, made by hand
const char* kTutorials[] = {
    "; title: The First Burrow\n"
    "#######\n"
    "#@ $ .#\n"
    "#######\n",
    "; title: Round the Bend\n"
    "######\n"
    "#    #\n"
    "# $  #\n"
    "#@ # #\n"
    "#  #.#\n"
    "#    #\n"
    "######\n",
    "; title: Two of Them\n"
    "########\n"
    "#  .   #\n"
    "# $##$ #\n"
    "#@   . #\n"
    "########\n",
};

std::string title_for(std::uint64_t h) {
    static const char* a[] = {"Carrot", "Turnip", "Radish", "Cabbage", "Parsnip", "Pumpkin", "Bean", "Beet", "Leek", "Pea", "Onion", "Marrow", "Squash", "Lettuce",
                              "Cucumber", "Potato", "Sprout", "Clover", "Thistle", "Bramble", "Mossy", "Muddy", "Rainy", "Sunny", "Windy", "Frosty", "Moonlit", "Dewy"};
    static const char* b[] = {"Corner", "Row", "Patch", "Bed", "Lane", "Hollow", "Nook", "Bend", "Plot", "Path", "Trellis", "Hedge", "Gate", "Ditch", "Mound", "Yard",
                              "Furrow", "Arbour", "Barrow", "Steps", "Loop", "Square", "Crossing", "Tangle", "Maze", "Muddle"};
    h = h * 0x9E3779B97F4A7C15ULL;
    return std::string(a[(h >> 20) % (sizeof a / sizeof *a)]) + " " + b[(h >> 40) % (sizeof b / sizeof *b)];
}
}  // namespace

int main(int argc, char** argv) {
    if (argc >= 7 && !std::strcmp(argv[1], "sample")) {
        GenParams p;
        p.w = std::atoi(argv[2]); p.h = std::atoi(argv[3]); p.boxes = std::atoi(argv[4]); p.min_pushes = std::atoi(argv[5]);
        const int count = std::atoi(argv[6]);
        const std::uint64_t seed = argc > 7 ? std::strtoull(argv[7], nullptr, 10) : 1;
        for (int i = 0; i < count; ++i) {
            p.seed = seed + static_cast<std::uint64_t>(i) * 7919;
            const auto t0 = std::chrono::steady_clock::now();
            const GenResult r = generate(p);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (!r.ok) { std::printf("seed %llu: failed after %d attempts (%.0f ms): carve %d shallow %d unverified %d deepest %d\n", static_cast<unsigned long long>(p.seed), r.attempts, ms, r.carve_rejects, r.shallow, r.unverified, r.deepest); continue; }
            std::printf("seed %llu: pushes %d moves %d switches %d nodes %lld attempts %d (%.0f ms)\n%s\n", static_cast<unsigned long long>(p.seed), r.pushes, r.moves,
                        r.box_lines, r.solver_nodes, r.attempts, ms, r.level.xsb().c_str());
        }
        return 0;
    }
    if (argc >= 3 && !std::strcmp(argv[1], "campaign")) {
        const int per = argc > 3 ? std::atoi(argv[3]) : 30;
        std::vector<LevelEntry> all;
        std::set<std::string> boards;
        for (const char* t : kTutorials) {
            std::vector<LevelEntry> one;
            std::string err;
            if (!load_levels(t, one, &err) || one.size() != 1) { std::fprintf(stderr, "tutorial: %s\n", err.c_str()); return 1; }
            LevelEntry e = one[0];
            const SolveResult sr = solve(e.level);
            if (!sr.solved) { std::fprintf(stderr, "tutorial %s does not solve\n", e.level.title.c_str()); return 1; }
            e.section = "First Steps";
            e.par = sr.pushes;
            e.level.solution = sr.lurd;
            e.switches = box_switches(e.level, sr.lurd);
            all.push_back(e);
        }
        int si = 0;
        for (const Section& s : kSections) {
            ++si;
            std::vector<std::pair<double, LevelEntry>> got;
            const auto t0 = std::chrono::steady_clock::now();
            // candidates in parallel, in batches; kept in seed order so the result never depends on thread timing
            const int threads = std::max(2u, std::thread::hardware_concurrency());
            for (int base = 0; static_cast<int>(got.size()) < per && base < per * 4; base += threads) {
                std::vector<GenResult> res(static_cast<size_t>(threads));
                std::vector<GenParams> ps(static_cast<size_t>(threads));
                std::vector<std::thread> pool;
                for (int k = 0; k < threads; ++k) {
                    const int i = base + k;
                    GenParams& p = ps[static_cast<size_t>(k)];
                    const double f = std::min(1.0, per > 1 ? static_cast<double>(i) / (per * 1.3) : 0);
                    p.w = s.w0 + static_cast<int>(std::lround((s.w1 - s.w0) * f));
                    p.h = s.h0 + static_cast<int>(std::lround((s.h1 - s.h0) * f));
                    p.boxes = s.boxes0 + static_cast<int>(std::lround((s.boxes1 - s.boxes0) * f));
                    p.min_pushes = s.min_pushes;
                    p.reverse_budget = s.budget;
                    p.seed = static_cast<std::uint64_t>(si) * 1000003ULL + static_cast<std::uint64_t>(i) * 7919ULL;
                    pool.emplace_back([&res, &ps, k] { res[static_cast<size_t>(k)] = generate(ps[static_cast<size_t>(k)]); });
                }
                for (std::thread& t : pool) t.join();
                for (int k = 0; k < threads && static_cast<int>(got.size()) < per; ++k) {
                    const GenResult& r = res[static_cast<size_t>(k)];
                    const GenParams& p = ps[static_cast<size_t>(k)];
                    if (!r.ok) continue;
                    const std::string key = r.level.xsb();
                    if (!boards.insert(key).second) continue;
                    LevelEntry e;
                    e.level = r.level;
                    e.section = s.name;
                    e.par = r.pushes;
                    e.switches = r.box_lines;
                    char seed[96];
                    std::snprintf(seed, sizeof seed, "%llu %dx%d %d", static_cast<unsigned long long>(p.seed), p.w, p.h, p.boxes);
                    e.seed = seed;
                    // difficulty: the solver's effort, the par and how much it switches between pumpkins
                    const double d = std::log2(1.0 + static_cast<double>(r.solver_nodes)) * 3 + r.pushes + 1.5 * r.box_lines;
                    got.push_back({d, e});
                }
            }
            std::stable_sort(got.begin(), got.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            int k = 0;
            for (auto& [d, e] : got) {
                e.level.title = title_for(std::hash<std::string>{}(e.level.xsb()) ^ static_cast<std::uint64_t>(++k));
                all.push_back(e);
            }
            std::fprintf(stderr, "%-16s %2zu levels  (%.1f s)\n", s.name, got.size(), std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        }
        std::ofstream f(argv[2]);
        f << "; Catching Thieves: the campaign. Generated by tools/levelgen.cpp (deterministic seeds).\n"
             "; Every level was solved push-optimally by an independent forward solver and its solution replayed through the rules.\n\n";
        f << save_levels(all);
        std::fprintf(stderr, "%zu levels written to %s\n", all.size(), argv[2]);
        return 0;
    }
    std::fprintf(stderr, "usage: levelgen sample W H BOXES MINPUSH COUNT [SEED]\n       levelgen campaign OUT.txt [PER_SECTION]\n");
    return 1;
}
