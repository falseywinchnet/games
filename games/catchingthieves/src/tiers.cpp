#include "tiers.hpp"

#include <algorithm>
#include <cstdio>

namespace ct {

namespace {
// Floors rise and the score bands follow one another without overlapping.
// The tutorial row only names the tier; its gardens are made by hand.
const TierRule kRules[kDifficulties] = {
    {"Tutorial", "tutorial", 1, 2, 1, 0, 0, 30, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {"Easy", "easy", 2, 3, 6, 0, 0, 65, 5, 5, 6, 6, 2, 3, 7, 150000, 1},
    {"Medium", "medium", 2, 5, 14, 50, 65, 100, 7, 6, 8, 7, 3, 4, 18, 400000, 1.1},
    {"Hard", "hard", 3, 6, 26, 500, 100, 1e9, 8, 8, 9, 9, 4, 5, 30, 1200000, 1.2},
};

std::uint64_t mix(std::uint64_t x) {
    x ^= x >> 33; x *= 0xFF51AFD7ED558CCDULL;
    x ^= x >> 33; x *= 0xC4CEB9FE1A85EC53ULL;
    x ^= x >> 33;
    return x;
}

std::string garden_title(std::uint64_t h) {
    static const char* a[] = {"Carrot", "Turnip", "Radish", "Cabbage", "Parsnip", "Pumpkin", "Bean", "Beet", "Leek", "Pea", "Onion", "Marrow", "Squash", "Lettuce",
                              "Cucumber", "Potato", "Sprout", "Clover", "Thistle", "Bramble", "Mossy", "Muddy", "Rainy", "Sunny", "Windy", "Frosty", "Moonlit", "Dewy"};
    static const char* b[] = {"Corner", "Row", "Patch", "Bed", "Lane", "Hollow", "Nook", "Bend", "Plot", "Path", "Trellis", "Hedge", "Gate", "Ditch", "Mound", "Yard",
                              "Furrow", "Arbour", "Barrow", "Steps", "Loop", "Square", "Crossing", "Tangle", "Maze", "Muddle"};
    h = mix(h);
    return std::string(a[(h >> 20) % (sizeof a / sizeof *a)]) + " " + b[(h >> 40) % (sizeof b / sizeof *b)];
}
}  // namespace

const TierRule& tier_rule(int difficulty) { return kRules[std::clamp(difficulty, 0, kDifficulties - 1)]; }

const char* difficulty_name(int difficulty) { return tier_rule(difficulty).name; }

int difficulty_from_key(const std::string& key) {
    for (int d = 0; d < kDifficulties; ++d)
        if (key == kRules[d].key) return d;
    return -1;
}

int classify(const GardenMetrics& m) {
    for (int d = kEasy; d < kDifficulties; ++d) {
        const TierRule& r = kRules[d];
        if (m.boxes < r.boxes_min || m.boxes > r.boxes_max || m.pushes < r.pushes_min || m.nodes < r.nodes_min) continue;
        const double s = m.score();
        if (s >= r.score_min && s < r.score_max) return d;
    }
    return -1;
}

Grown grow(int difficulty, std::uint64_t seed, CancellationToken stop, int max_tries) {
    Grown out;
    if (difficulty < kEasy || difficulty >= kDifficulties) return out;
    const TierRule& r = kRules[difficulty];
    std::uint64_t h = mix(seed + 0x5EED);
    for (int attempt = 0; attempt < max_tries && !stop.stop_requested(); ++attempt) {
        out.tries = attempt + 1;
        h = mix(h + static_cast<std::uint64_t>(attempt) + 1);
        GenParams p;
        p.w = r.w0 + static_cast<int>(h % static_cast<std::uint64_t>(r.w1 - r.w0 + 1));
        p.h = r.h0 + static_cast<int>((h >> 8) % static_cast<std::uint64_t>(r.h1 - r.h0 + 1));
        p.boxes = r.grow_boxes0 + static_cast<int>((h >> 16) % static_cast<std::uint64_t>(r.grow_boxes1 - r.grow_boxes0 + 1));
        p.min_pushes = r.min_pushes;
        p.reverse_budget = r.reverse_budget;
        p.hedges = r.hedges;
        p.attempts = 60;
        p.seed = h >> 24;
        const GenResult g = generate(p, stop);
        if (!g.ok) continue;
        const GardenMetrics m = measure(g.level, g.solver_nodes);
        if (classify(m) != difficulty) continue;
        out.ok = true;
        out.metrics = m;
        out.entry.level = g.level;
        out.entry.level.title = garden_title(h);
        out.entry.par = g.pushes;
        out.entry.switches = g.box_lines;
        out.entry.nodes = g.solver_nodes;
        out.entry.tier = r.key;
        out.entry.section = r.name;
        char text[96];
        std::snprintf(text, sizeof text, "%llu %dx%d %d %.2f", static_cast<unsigned long long>(p.seed), p.w, p.h, p.boxes, p.hedges);
        out.entry.seed = text;
        return out;
    }
    return out;
}

}  // namespace ct
