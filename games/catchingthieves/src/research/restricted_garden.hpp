#pragma once
#include "garden_score.hpp"
namespace ct::research {
struct RestrictedGardenScore {
    GardenScore source;
    StageDrawing drawing;
    std::string trace;
    int removed=0;
};
// Carve before Level allocation. Source par/width/departure lower bounds
// survive restriction; its attaining push performance is preserved exactly.
// Input is a tier recipe from compose_garden_score; bounded scratch storage
// specializes that domain. Generic graphs use WitnessSpace instead.
RestrictedGardenScore restrict_garden_score(GardenScore,std::uint64_t spatial_seed);
RestrictedGardenScore compose_restricted_garden(int difficulty,std::uint64_t seed);
Level compile_restricted_garden(const RestrictedGardenScore&);
Grown restricted_grow(int difficulty,std::uint64_t seed,CancellationToken stop={},int max_tries=40);
}
