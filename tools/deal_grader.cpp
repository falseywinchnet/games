// Offline deal grader for Solitaire, Spider and FreeCell.
//
//   deal_grader stats <kind> <option> <first-seed> <count>
//       kind = solitaire | spider | freecell. Prints per-seed casual-player and
//       solver results plus a summary used to calibrate the node limits.
//   deal_grader generate <output deal_tables.cpp> [per-bucket] [minutes-per-group]
//       Scans seeds 1, 2, 3, ... for each game/option, grades each deal with
//       games::grade_deal and writes the verified seed tables. Single threaded.
//
// Regenerate shared/cards/deal_tables.cpp (after changing the solver, the grading limits or
// the rules in game.cpp) from a portable-core build:
//   cmake --build .build/portable-core --target deal_grader -j 1
//   .build/portable-core/deal_grader generate shared/cards/deal_tables.cpp
#include "solitaire_solver.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace games;
using Clock = std::chrono::steady_clock;

namespace {

double seconds_since(Clock::time_point t) {
    return std::chrono::duration<double>(Clock::now() - t).count();
}

bool parse_kind(const char* text, Kind& kind) {
    if (!std::strcmp(text, "solitaire") || !std::strcmp(text, "klondike"))
        kind = Kind::solitaire;
    else if (!std::strcmp(text, "spider"))
        kind = Kind::spider;
    else if (!std::strcmp(text, "freecell"))
        kind = Kind::freecell;
    else
        return false;
    return true;
}

long percentile(std::vector<long> v, double p) {
    if (v.empty())
        return 0;
    std::sort(v.begin(), v.end());
    std::size_t i = static_cast<std::size_t>(p * static_cast<double>(v.size() - 1) + 0.5);
    return v[std::min(i, v.size() - 1)];
}

int stats(Kind kind, int option, std::uint32_t first, int count) {
    option = normalized_option(kind, option);
    GradingLimits limits = grading_limits(kind, option);
    long budget = limits.node_budget;
    int greedy = 0, solved = 0, exhausted = 0, unknown = 0;
    int grades[3] = {};
    std::vector<long> nodes, nodes_nongreedy;
    std::vector<double> times;
    std::map<int, std::array<int, 2>> widths;
    double total_time = 0;
    for (int i = 0; i < count; ++i) {
        std::uint32_t seed = first + static_cast<std::uint32_t>(i);
        Clock::time_point t0 = Clock::now();
        bool g = greedy_wins(kind, option, seed);
        double tg = seconds_since(t0);
        Clock::time_point t1 = Clock::now();
        SolveReport r = solve_deal(kind, option, seed, budget);
        double ts = seconds_since(t1);
        total_time += tg + ts;
        greedy += g;
        if (r.solved) {
            ++solved;
            nodes.push_back(r.nodes);
            times.push_back(ts);
            if (!g)
                nodes_nongreedy.push_back(r.nodes);
        } else if (r.exhausted)
            ++exhausted;
        else
            ++unknown;
        if (g)
            ++grades[0];
        else if (r.solved)
            ++grades[r.width <= limits.easy_width ? 0 : r.width <= limits.medium_width ? 1 : 2];
        if (r.solved)
            ++widths[r.width][g ? 1 : 0];
        std::printf("seed %u greedy %d solved %d exhausted %d width %d nodes %ld moves %d "
                    "greedy_ms %.1f solve_ms %.1f\n",
                    seed, g ? 1 : 0, r.solved ? 1 : 0, r.exhausted ? 1 : 0, r.width, r.nodes,
                    r.moves, tg * 1000, ts * 1000);
        std::fflush(stdout);
    }
    std::printf(
        "\n%s option %d: %d deals, greedy wins %d, solver wins %d, exhausted %d, unknown %d\n",
        game_name(kind), option, count, greedy, solved, exhausted, unknown);
    std::printf("grades (widths %d/%d): easy %d medium %d hard %d\n", limits.easy_width,
                limits.medium_width, grades[0], grades[1], grades[2]);
    std::printf("solver nodes on wins p10 %ld p25 %ld p50 %ld p75 %ld p90 %ld max %ld\n",
                percentile(nodes, 0.1), percentile(nodes, 0.25), percentile(nodes, 0.5),
                percentile(nodes, 0.75), percentile(nodes, 0.9), percentile(nodes, 1.0));
    std::printf("non-greedy wins nodes p25 %ld p50 %ld p75 %ld\n",
                percentile(nodes_nongreedy, 0.25), percentile(nodes_nongreedy, 0.5),
                percentile(nodes_nongreedy, 0.75));
    for (const std::pair<const int, std::array<int, 2>>& item : widths)
        std::printf("solved at width %d: %d not casual, %d casual\n", item.first, item.second[0], item.second[1]);
    std::printf("total %.1f s, %.1f ms per deal\n", total_time,
                total_time * 1000 / std::max(1, count));
    return 0;
}

struct Group {
    Kind kind;
    int option;
    const char* name;
};

struct Bucket {
    std::vector<std::uint32_t> seeds;
};

int generate(const char* path, int per_bucket, double minutes) {
    const Group groups[] = {
        {Kind::solitaire, 1, "klondike_draw1"}, {Kind::solitaire, 3, "klondike_draw3"},
        {Kind::spider, 1, "spider_1suit"},      {Kind::spider, 2, "spider_2suit"},
        {Kind::spider, 4, "spider_4suit"},      {Kind::freecell, 0, "freecell"}};
    std::string body, index, summary;
    Clock::time_point all_start = Clock::now();
    for (const Group& g : groups) {
        Bucket buckets[3];
        int scanned = 0, evaluated = 0, greedy = 0, solved = 0, exhausted = 0, unknown = 0;
        int sample_grades[3] = {};
        Clock::time_point t0 = Clock::now();
        double solve_seconds = 0;
        long solve_count = 0;
        for (std::uint32_t seed = 1;; ++seed) {
            bool full = true;
            for (const Bucket& b : buckets)
                full = full && static_cast<int>(b.seeds.size()) >= per_bucket;
            if (full || seconds_since(t0) > minutes * 60)
                break;
            ++scanned;
            // Every seed is fully graded (this keeps the statistics unbiased); only
            // full buckets stop accepting.
            Clock::time_point ts = Clock::now();
            DealGrade grade = grade_deal(g.kind, g.option, seed);
            if (!grade.greedy) {
                solve_seconds += seconds_since(ts);
                ++solve_count;
            }
            ++evaluated;
            greedy += grade.greedy;
            if (!grade.greedy) {
                if (grade.report.solved)
                    ++solved;
                else if (grade.report.exhausted)
                    ++exhausted;
                else
                    ++unknown;
            }
            if (!grade.winnable)
                continue;
            int d = static_cast<int>(grade.difficulty);
            ++sample_grades[d];
            if (static_cast<int>(buckets[d].seeds.size()) < per_bucket)
                buckets[d].seeds.push_back(seed);
        }
        double elapsed = seconds_since(t0);
        char line[512];
        std::snprintf(
            line, sizeof line,
            "// %s: %d seeds scanned in %.0f s; casual-player wins %d, solver-only wins %d, "
            "no win found %d (exhausted %d, budget %d); easy %d medium %d hard %d; "
            "kept %zu/%zu/%zu; %.1f ms per solver run\n",
            g.name, scanned, elapsed, greedy, solved, exhausted + unknown, exhausted, unknown,
            sample_grades[0], sample_grades[1], sample_grades[2], buckets[0].seeds.size(),
            buckets[1].seeds.size(), buckets[2].seeds.size(),
            solve_count ? solve_seconds * 1000 / static_cast<double>(solve_count) : 0.0);
        summary += line;
        std::fputs(line, stdout);
        std::fflush(stdout);
        const char* names[3] = {"easy", "medium", "hard"};
        const char* enums[3] = {"Difficulty::easy", "Difficulty::medium", "Difficulty::hard"};
        const char* kinds[3] = {"Kind::solitaire", "Kind::spider", "Kind::freecell"};
        for (int d = 0; d < 3; ++d) {
            std::string array = std::string(g.name) + "_" + names[d];
            body += "const std::uint32_t " + array + "[] = {";
            const std::vector<std::uint32_t>& seeds = buckets[d].seeds;
            if (seeds.empty())
                body += "0";
            for (std::size_t i = 0; i < seeds.size(); ++i) {
                if (i % 12 == 0)
                    body += "\n   ";
                body += " " + std::to_string(seeds[i]) + (i + 1 < seeds.size() ? "," : "");
            }
            body += "\n};\n";
            index += std::string("    {") + kinds[static_cast<int>(g.kind)] + ", " +
                     std::to_string(g.option) + ", " + enums[d] + ", " + array + ", " +
                     std::to_string(seeds.size()) + "},\n";
        }
    }
    char total[128];
    std::snprintf(total, sizeof total, "// Total generation time %.0f s (single thread).\n",
                  seconds_since(all_start));
    FILE* f = std::fopen(path, "wb");
    if (!f) {
        std::perror(path);
        return 1;
    }
    std::fputs("// GENERATED by tools/deal_grader.cpp - do not edit by hand.\n"
               "// Verified winnable seeds for games::Game::deal(kind, seed, option), graded\n"
               "// Easy/Medium/Hard by games::grade_deal (see solitaire_solver.cpp).\n"
               "// Regenerate from a portable-core build:\n"
               "//   cmake --build .build/portable-core --target deal_grader -j 1\n"
               "//   .build/portable-core/deal_grader generate shared/cards/deal_tables.cpp\n",
               f);
    std::fprintf(f, "// Per-bucket target %d seeds.\n", per_bucket);
    std::fputs(summary.c_str(), f);
    std::fputs(total, f);
    std::fputs("#include \"solitaire_solver.hpp\"\n\nnamespace games::detail {\nnamespace {\n", f);
    std::fputs(body.c_str(), f);
    std::fputs("} // namespace\n\nconst DealTable deal_tables[] = {\n", f);
    std::fputs(index.c_str(), f);
    std::fputs("};\nconst int deal_table_count = static_cast<int>(sizeof deal_tables / sizeof "
               "deal_tables[0]);\n"
               "} // namespace games::detail\n",
               f);
    std::fclose(f);
    std::printf("%s", total);
    return 0;
}

int trace(Kind kind, int option, std::uint32_t seed) {
    std::vector<SolverStep> steps;
    bool won = greedy_wins(kind, option, seed, &steps);
    Game game;
    game.deal(kind, seed, normalized_option(kind, option));
    for (const SolverStep& step : steps)
        if (!(step.draw ? game.draw() : game.move(step.move)))
            std::printf("step failed\n");
    std::printf("casual %s after %zu steps, completed %d\n", won ? "won" : "stuck", steps.size(),
                game.state.completed);
    for (int p = 0; p < 20; ++p) {
        if (game.state.piles[p].empty())
            continue;
        std::printf("%2d:", p);
        for (const Card& c : game.state.piles[p])
            std::printf(" %s%s", c.up ? "" : "#", card_name(c).c_str());
        std::printf("\n");
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 5 && !std::strcmp(argv[1], "trace")) {
        Kind kind;
        if (!parse_kind(argv[2], kind))
            return 2;
        return trace(kind, std::atoi(argv[3]),
                     static_cast<std::uint32_t>(std::strtoul(argv[4], nullptr, 10)));
    }
    if (argc >= 6 && !std::strcmp(argv[1], "stats")) {
        Kind kind;
        if (!parse_kind(argv[2], kind))
            return 2;
        return stats(kind, std::atoi(argv[3]),
                     static_cast<std::uint32_t>(std::strtoul(argv[4], nullptr, 10)),
                     std::atoi(argv[5]));
    }
    if (argc >= 3 && !std::strcmp(argv[1], "generate"))
        return generate(argv[2], argc >= 4 ? std::atoi(argv[3]) : 300,
                        argc >= 5 ? std::atof(argv[4]) : 8.0);
    std::fprintf(
        stderr, "usage: deal_grader stats <solitaire|spider|freecell> <option> <first> <count>\n"
                "       deal_grader generate <deal_tables.cpp> [per-bucket] [minutes-per-group]\n");
    return 2;
}
