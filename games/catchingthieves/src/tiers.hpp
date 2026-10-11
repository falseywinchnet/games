#pragma once
// The four difficulties. Tutorial gardens are made by hand, each showing one
// mechanism. Easy, Medium and Hard are grown by construction (research/,
// Astra's graph generator): each garden is built from a tier's action graph with
// its solution and a proof that its par is the fewest pushes possible. The tier
// is what the construction requires, not a measurement afterwards: Easy needs one
// pumpkin task at a time, Medium two at once and Hard four, with marked pits from
// Medium and squirrels and one-way gates at Hard. The book's older gardens were
// filed by tier by what the solver measured along their best solutions
// (gen.hpp's SolvedMetrics, classify below), and stay as they were.
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

// The tier a measured book garden belongs to (kEasy..kHard), or -1 if it fits none.
int classify(const SolvedMetrics& metrics);

// What a grown garden holds: its size and pieces, and the pushes, moves and pumpkin
// switches of its certified solution.
struct GardenMetrics {
    int floors = 0, pumpkins = 0, burrows = 0;
    int pushes = 0, moves = 0, switches = 0;
    int one_way = 0, pits = 0, raccoons = 0;  // raccoons counts squirrels (Astra's name)
    long long nodes = 0;
    bool operator==(const GardenMetrics&) const = default;
};
GardenMetrics measure(const Level& level, long long nodes = 0);

struct Grown {
    bool ok = false;
    LevelEntry entry;            // carries its solution, par and switches
    GardenMetrics metrics;
    int tries = 0;               // always 1: construction never rejects a garden
};

// A garden of the tier, built directly from the seed (microseconds) with its certified
// solution and par; the same seed always gives the same garden. `max_tries` is ignored.
Grown grow(int difficulty, std::uint64_t seed, CancellationToken stop = {}, int max_tries = 40);

}  // namespace ct
