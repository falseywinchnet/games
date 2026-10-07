// Rules, solver, generator and table tests: parsing, moves and undo, the
// solver against replay, the stuck check never crying wolf, generated levels
// carrying optimal solutions, every table garden's solution replayed through
// the rules, the tiers ordered by what the solver measures, fresh gardens
// grown at each tier, the lessons, the raccoons' lines, the save file and its
// migration from the old sequential book.
#include "gen.hpp"
#include "level.hpp"
#include "levelset.hpp"
#include "lines.hpp"
#include "save.hpp"
#include "solver.hpp"
#include "tiers.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

using namespace ct;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static double median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

static std::string envelope(const std::string& body) {
    std::uint64_t checksum = 14695981039346656037ULL;
    for (const unsigned char byte : body) { checksum ^= byte; checksum *= 1099511628211ULL; }
    return "THIEVES1\n" + body + "check=" + std::to_string(checksum) + "\n";
}

static const LevelEntry* by_title(const std::vector<LevelEntry>& levels, const std::string& title) {
    for (const LevelEntry& e : levels)
        if (e.level.title == title) return &e;
    return nullptr;
}

static Level L(const char* xsb) {
    Level lv;
    std::string err;
    if (!Level::parse(xsb, lv, &err)) std::printf("parse error: %s\n", err.c_str());
    return lv;
}

int main(int argc, char** argv) {
    const std::string campaign = argc > 1 ? argv[1] : (std::filesystem::path(__FILE__).parent_path().parent_path() / "assets/levels/gardens.txt").string();
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
    // --- the tables: every garden loads, its solution replays to a win in exactly its par of pushes
    std::vector<LevelEntry> levels;
    {
        std::ifstream f(campaign);
        std::stringstream ss;
        ss << f.rdbuf();
        std::string err;
        CHECK(load_levels(ss.str(), levels, &err));
        if (!err.empty()) std::printf("%s\n", err.c_str());
        CHECK(levels.size() == 251);
        int replayed = 0, resolved = 0;
        const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        std::set<int> ids;
        for (const LevelEntry& e : levels) {
            Board b;
            CHECK(b.load(e.level));
            const bool ok = b.replay(e.level.solution) && b.solved() && b.pushes() == e.par;
            CHECK(ok);
            if (!ok) std::printf("  garden '%s' (%s) fails its replay\n", e.level.title.c_str(), e.section.c_str());
            replayed += ok;
            CHECK(!e.level.title.empty() && difficulty_from_key(e.tier) >= 0);
            CHECK(e.id >= 0 && e.id <= kMaxGardenId && ids.insert(e.id).second);  // ids are permanent and unique
            // par really is the fewest pushes, and the recorded effort is the solver's: re-solved for the quicker ones
            if (e.par <= 30) {
                const SolveResult sr = solve(e.level, 3000000);
                CHECK(sr.solved && sr.pushes == e.par && sr.nodes == e.nodes);
                ++resolved;
            }
        }
        // the old book's 243 gardens all remain, under their old indices, so old saves find them
        for (int id = 0; id < 243; ++id) CHECK(ids.count(id) == 1);
        std::printf("tables: %zu gardens, %d solutions replayed, %d pars re-solved (%.1fs)\n", levels.size(), replayed, resolved,
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        // the text format round-trips
        std::vector<LevelEntry> again;
        CHECK(load_levels(save_levels(levels), again) && again.size() == levels.size());
        bool same = true;
        for (size_t i = 0; i < levels.size() && i < again.size(); ++i)
            same = same && again[i].level.xsb() == levels[i].level.xsb() && again[i].level.solution == levels[i].level.solution &&
                   again[i].par == levels[i].par && again[i].id == levels[i].id && again[i].tier == levels[i].tier &&
                   again[i].lesson == levels[i].lesson && again[i].nodes == levels[i].nodes;
        CHECK(same);
    }
    // --- the tiers: every generated garden measures into its own tier, and the tiers are ordered by every measure
    {
        std::vector<std::vector<GardenMetrics>> tiers(kDifficulties);
        for (const LevelEntry& e : levels) {
            const int d = difficulty_from_key(e.tier);
            if (d == kTutorial) continue;
            const GardenMetrics m = measure(e.level, e.nodes);
            CHECK(classify(m) == d);
            tiers[static_cast<size_t>(d)].push_back(m);
        }
        std::printf("tier      gardens boxes  soil pushes moves switches    nodes traps  score (medians)\n");
        double previous[8] = {};
        for (int d = kEasy; d < kDifficulties; ++d) {
            const std::vector<GardenMetrics>& ms = tiers[static_cast<size_t>(d)];
            CHECK(ms.size() >= 60);
            std::vector<double> col[8];
            double lo = 1e9, hi = -1e9;
            for (const GardenMetrics& m : ms) {
                col[0].push_back(m.boxes); col[1].push_back(m.soil); col[2].push_back(m.pushes); col[3].push_back(m.moves);
                col[4].push_back(m.switches); col[5].push_back(static_cast<double>(m.nodes)); col[6].push_back(m.traps); col[7].push_back(m.score());
                lo = std::min(lo, m.score());
                hi = std::max(hi, m.score());
            }
            double now[8];
            for (int k = 0; k < 8; ++k) now[k] = median(col[k]);
            std::printf("%-8s %8zu %5.0f %5.0f %6.0f %5.0f %8.0f %8.0f %5.0f %6.1f   (score %.1f to %.1f)\n", difficulty_name(d), ms.size(), now[0], now[1],
                        now[2], now[3], now[4], now[5], now[6], now[7], lo, hi);
            // harder by every measure: pumpkins and soil at least as many, everything the solver measures strictly more
            if (d > kEasy) {
                CHECK(now[0] >= previous[0] && now[1] > previous[1]);
                for (int k = 2; k < 8; ++k) CHECK(now[k] > previous[k]);
                CHECK(lo >= tier_rule(d).score_min && tier_rule(d - 1).score_max <= tier_rule(d).score_min);
            }
            for (int k = 0; k < 8; ++k) previous[k] = now[k];
        }
        CHECK(tier_rule(kHard).boxes_max > tier_rule(kEasy).boxes_max && tier_rule(kHard).pushes_min > tier_rule(kMedium).pushes_min);
    }
    // --- fresh gardens: grown at each tier, measured into it, and proven by replaying their witness through the rules
    {
        for (int d = kEasy; d < kDifficulties; ++d) {
            double worst = 0, total = 0;
            const int n = d == kHard ? 3 : 6;
            for (int k = 0; k < n; ++k) {
                const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
                const Grown g = grow(d, 1000 + static_cast<std::uint64_t>(k) * 31);
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                worst = std::max(worst, ms);
                total += ms;
                CHECK(g.ok);
                if (!g.ok) continue;
                CHECK(classify(g.metrics) == d && g.entry.tier == tier_rule(d).key && !g.entry.level.title.empty());
                Board b;
                CHECK(b.load(g.entry.level) && b.on_goal() == 0);
                CHECK(b.replay(g.entry.level.solution) && b.solved() && b.pushes() == g.entry.par);
                if (d < kHard) CHECK(solve(g.entry.level).pushes == g.entry.par);
                CHECK(grow(d, 1000 + static_cast<std::uint64_t>(k) * 31).entry.level.xsb() == g.entry.level.xsb());  // reproducible
            }
            std::printf("grow %-6s: %d gardens, mean %.0f ms, worst %.0f ms (single thread, off the UI thread in play)\n", difficulty_name(d), n,
                        total / n, worst);
        }
        CancellationSource cancelled;
        cancelled.request_stop();
        CHECK(!grow(kHard, 7, cancelled.get_token()).ok);
    }
    // --- the lessons: short, hand-made, one mechanism each, and each shows what it says
    {
        int lessons = 0;
        for (const LevelEntry& e : levels) {
            if (e.tier != "tutorial") continue;
            ++lessons;
            CHECK(!e.lesson.empty() && e.lesson.size() <= 80 && e.par <= 10 && e.level.boxes.size() <= 2);
        }
        CHECK(lessons >= 10);
        const LevelEntry* hedge = by_title(levels, "Hedge Trouble");
        const LevelEntry* far = by_title(levels, "The Far One First");
        const LevelEntry* one = by_title(levels, "One at a Time");
        const LevelEntry* pull = by_title(levels, "No Pulling");
        const LevelEntry* back = by_title(levels, "Take It Back");
        CHECK(hedge && far && one && pull && back);
        if (hedge && far && one && pull && back) {
            Board b;
            b.load((*hedge).level);
            CHECK(b.replay("U") && b.stuck());                         // the obvious push wedges it against the hedge
            CHECK(b.undo() && !b.stuck());                             // and undo takes it back
            b.load((*far).level);
            CHECK(b.replay("rR") && !solve(b, 500000).solved);        // the near pumpkin first blocks the far burrow
            b.load((*one).level);
            CHECK(!b.move(kRight));                                    // two pumpkins in a row won't budge
            b.load((*pull).level);
            CHECK(b.move(kLeft) && b.boxes() == (*pull).level.boxes); // stepping away does not drag the pumpkin
            b.load((*back).level);
            CHECK(b.replay("uL") && b.stuck() && b.undo() && b.undo() && b.moves() == 0);
        }
    }
    // --- the lines: plenty of each, all different, short enough for a bubble, kind, and never repeated within a bag
    {
        std::set<std::string> all;
        int total = 0;
        const char* const unkind[] = {"stupid", "idiot", "loser", "dumb", "useless", "pathetic", "hopeless", "bad at", "rubbish", "failure", "lame"};
        for (int k = 0; k < static_cast<int>(Line::kinds); ++k) {
            const Line kind = static_cast<Line>(k);
            CHECK(Lines::size(kind) >= 6);
            for (int i = 0; i < Lines::size(kind); ++i) {
                const std::string text = Lines::line(kind, i);
                ++total;
                CHECK(all.insert(text).second);          // no line is written twice
                CHECK(!text.empty() && text.size() <= 44);
                std::string lower = text;
                for (char& c : lower) {
                    CHECK(static_cast<unsigned char>(c) < 128);
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                for (const char* word : unkind) CHECK(lower.find(word) == std::string::npos);
            }
            Lines bag(static_cast<std::uint64_t>(k) + 3);
            std::set<std::string> said;
            std::string last;
            for (int i = 0; i < Lines::size(kind); ++i) {
                last = bag.pick(kind);
                CHECK(said.insert(last).second);
            }
            for (int round = 0; round < 3 * Lines::size(kind); ++round) {
                const std::string now = bag.pick(kind);
                CHECK(now != last);
                last = now;
            }
        }
        CHECK(Lines::size(Line::taunt) >= 80 && Lines::size(Line::trapped) >= 25 && Lines::size(Line::idle) >= 15);
        std::printf("lines: %d, in %d kinds\n", total, static_cast<int>(Line::kinds));
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
            std::ofstream f(p, std::ios::binary | std::ios::trunc);
            f << all;
        }
        SaveData bad;
        CHECK(!load_save(p, bad));
        std::filesystem::remove(p);
    }
    // --- the difficulties in the save: round trip, and refusal of nonsense
    {
        const std::filesystem::path p = std::filesystem::temp_directory_path() / "ct_test_save2.txt";
        SaveData d;
        d.format = 2;
        d.level = -1;
        d.endless_xsb = "#######|#@ $ .#|#######";
        d.endless_solution = "rRR";
        d.history = "rR";
        d.difficulty = kHard;
        d.garden_tier = kMedium;
        d.season = 3;
        d.garden_par = 2;
        d.garden_title = "Frosty Lane";
        d.tiers[kEasy] = {12, 5};
        d.tiers[kHard] = {3, 1};
        d.played = {4, 77, 245};
        CHECK(write_save(p, d));
        SaveData e;
        CHECK(load_save(p, e));
        CHECK(e.format == 2 && e.level == -1 && e.difficulty == kHard && e.garden_tier == kMedium && e.season == 3 && e.garden_par == 2 &&
              e.garden_title == "Frosty Lane" && e.tiers[kEasy].cleared == 12 && e.tiers[kEasy].perfect == 5 && e.tiers[kHard].cleared == 3 &&
              e.played == d.played && e.history == "rR" && e.endless_xsb == d.endless_xsb);
        const char* const bad[] = {"format=2\ndifficulty=4\n", "format=2\ntier=1 3 4\n", "format=2\nplayed=1 -3\n", "format=3\n",
                                   "format=2\nseason=5\n", "format=2\ngarden_title=Hi|there\n", "level=243\n", "format=2\nlevel=10000\n"};
        for (const char* body : bad) {
            { std::ofstream f(p, std::ios::binary | std::ios::trunc); f << envelope(body); }
            SaveData x;
            CHECK(!load_save(p, x));
        }
        { std::ofstream f(p, std::ios::binary | std::ios::trunc); f << envelope("format=2\nlevel=250\ndifficulty=0\n"); }
        SaveData lesson;
        CHECK(load_save(p, lesson) && lesson.level == 250);  // an upgraded save may point at the new lessons
        std::filesystem::remove(p);
    }
    // --- migration from the old sequential book: the garden in play resumes, progress maps onto the tiers
    {
        std::map<int, const LevelEntry*> by_id;
        for (const LevelEntry& e : levels) by_id[e.id] = &e;
        const std::filesystem::path p = std::filesystem::temp_directory_path() / "ct_test_old_save.txt";
        // halfway through garden 150 (Autumn Maze in the old book), with five gardens cleared, two of them perfectly
        const LevelEntry& g150 = *by_id[150];
        const std::string half = g150.level.solution.substr(0, g150.level.solution.size() / 2);
        std::string body = "level=150\nhistory=" + half + "\n";
        const int cleared[] = {0, 10, 40, 100, 200};
        for (int id : cleared) {
            const int best = id == 10 || id == 100 ? (*by_id[id]).par : (*by_id[id]).par + 9;
            body += "record=" + std::to_string(id) + " " + std::to_string(best * 4) + " " + std::to_string(best) + "\n";
        }
        body += "endless_season=0\nendless_tier=3\nendless_cleared=4\nsound=1\nmusic=0\n";
        { std::ofstream f(p, std::ios::binary | std::ios::trunc); f << envelope(body); }
        SaveData d;
        CHECK(load_save(p, d) && d.format == 1 && d.level == 150 && d.history == half);
        CHECK(migrate(d, levels));
        const int tier150 = difficulty_from_key(g150.tier);
        CHECK(d.format == 2 && d.level == 150 && d.history == half);              // the garden in play, move for move
        CHECK(d.garden_tier == tier150 && d.difficulty == tier150);              // and New garden carries on at its tier
        CHECK(d.season == 2);                                                    // autumn, as it was in the book
        CHECK(d.records.size() == 5 && d.records[10].best_pushes == (*by_id[10]).par && !d.settings.music);
        int expect_cleared[kDifficulties] = {}, expect_perfect[kDifficulties] = {};
        for (int id : cleared) {
            const int t = difficulty_from_key((*by_id[id]).tier);
            ++expect_cleared[t];
            expect_perfect[t] += id == 10 || id == 100;
        }
        expect_cleared[kHard] += 4;                                              // the old endless winter gardens
        for (int t = 0; t < kDifficulties; ++t)
            CHECK(d.tiers[static_cast<size_t>(t)].cleared == expect_cleared[t] && d.tiers[static_cast<size_t>(t)].perfect == expect_perfect[t]);
        CHECK(d.played.count(0) && d.played.count(10) && d.played.count(200) && d.played.count(150) && d.played.size() == 6);
        CHECK(!migrate(d, levels));                                              // only once
        CHECK(write_save(p, d));
        SaveData again;
        CHECK(load_save(p, again) && again.format == 2 && again.level == 150 && again.history == half && again.tiers[kHard].cleared == d.tiers[kHard].cleared);
        Board resume;
        CHECK(resume.load(g150.level) && resume.replay(again.history));
        // the old endless garden in play: resumes with its par, its season and the tier for its season
        { std::ofstream f(p, std::ios::binary | std::ios::trunc); f << envelope("level=-1\nhistory=rR\nendless=#######|#@ $ .#|#######\nendless_solution=rRR\nendless_tier=1\nendless_cleared=2\n"); }
        SaveData e;
        CHECK(load_save(p, e) && migrate(e, levels));
        CHECK(e.level == -1 && e.history == "rR" && e.garden_par == 2 && e.garden_tier == kMedium && e.difficulty == kMedium && e.season == 1 &&
              e.garden_title == "Summer garden" && e.tiers[kMedium].cleared == 2);
        // a player still in the first steps starts at the tutorial
        { std::ofstream f(p, std::ios::binary | std::ios::trunc); f << envelope("level=1\nhistory=u\n"); }
        SaveData first;
        CHECK(load_save(p, first) && migrate(first, levels) && first.level == 1 && first.difficulty == kTutorial && first.history == "u");
        std::filesystem::remove(p);
        std::printf("migration: garden 150 resumes at %s, %d/%d/%d gardens cleared at Easy/Medium/Hard\n", difficulty_name(tier150),
                    d.tiers[kEasy].cleared, d.tiers[kMedium].cleared, d.tiers[kHard].cleared);
    }
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("all tests passed\n");
    return 0;
}
