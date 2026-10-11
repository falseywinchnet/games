#include "tiers.hpp"
#include "research/restricted_garden.hpp"

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

int classify(const SolvedMetrics& m) {
    for (int d = kEasy; d < kDifficulties; ++d) {
        const TierRule& r = kRules[d];
        if (m.boxes < r.boxes_min || m.boxes > r.boxes_max || m.pushes < r.pushes_min || m.nodes < r.nodes_min) continue;
        const double s = m.score();
        if (s >= r.score_min && s < r.score_max) return d;
    }
    return -1;
}

GardenMetrics measure(const Level& level, long long nodes) {
    GardenMetrics m;
    m.nodes = nodes;
    m.pumpkins = static_cast<int>(level.boxes.size());
    m.burrows = level.goals();
    m.raccoons = static_cast<int>(level.raccoons.size());
    for (int c = 0; c < level.w * level.h; ++c) {
        m.floors += level.floor(c);
        m.one_way += level.gate_at(c) != 0;
        m.pits += level.pit_at(c);
    }
    m.moves = static_cast<int>(level.solution.size());
    for (const char c : level.solution)
        m.pushes += c >= 'A' && c <= 'Z';
    m.switches = box_switches(level, level.solution);
    return m;
}

Grown grow(int difficulty, std::uint64_t seed, CancellationToken stop, int max_tries) {
    Grown out = research::restricted_grow(difficulty, seed, stop, max_tries);
    if (!out.ok)
        return out;
    // The construction names every garden of a tier alike; the book gives each its own.
    const TierRule& r = kRules[difficulty];
    out.tries = 1;
    out.entry.level.title = garden_title(mix(seed * 4 + static_cast<std::uint64_t>(difficulty)));
    out.entry.tier = r.key;
    out.entry.section = r.name;
    out.entry.id = -1;
    return out;
}

}  // namespace ct
