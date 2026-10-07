#pragma once
// The four difficulties. Tutorial gardens are made by hand, each showing one
// mechanism. Easy, Medium and Hard are generated and defined by what the
// solver measures along a garden's best solution (gen.hpp's GardenMetrics),
// not by size alone: each tier is a band of the difficulty score with floors
// on pumpkins, pushes and the solver's effort, and the bands do not overlap, so every
// Medium garden measures harder than every Easy one, and every Hard garden
// harder than every Medium one.
#include "cancellation.hpp"
#include "gen.hpp"
#include "levelset.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ct {

enum Difficulty { kTutorial = 0, kEasy = 1, kMedium = 2, kHard = 3 };
constexpr int kDifficulties = 4;

struct TierRule {
    const char* name;            // "Easy"
    const char* key;             // "easy", as written in the garden table
    // what a garden must measure to belong here
    int boxes_min, boxes_max;
    int pushes_min;
    long long nodes_min;
    double score_min, score_max;  // [min, max)
    // how fresh ones are grown
    int w0, h0, w1, h1;           // plot sizes, chosen between these
    int grow_boxes0, grow_boxes1; // pumpkins, chosen between these
    int min_pushes;
    long long reverse_budget;
    double hedges;                // hedge density (1: the generator's usual)
};

const TierRule& tier_rule(int difficulty);
const char* difficulty_name(int difficulty);
int difficulty_from_key(const std::string& key);  // -1 if unknown

// The tier a measured garden belongs to (kEasy..kHard), or -1 if it fits none.
int classify(const GardenMetrics& metrics);

struct Grown {
    bool ok = false;
    LevelEntry entry;            // carries its solution, par and switches
    GardenMetrics metrics;
    int tries = 0;               // gardens generated until one measured into the tier
};

// Generate gardens until one measures into the tier; stops early when asked.
Grown grow(int difficulty, std::uint64_t seed, CancellationToken stop = {}, int max_tries = 40);

}  // namespace ct
