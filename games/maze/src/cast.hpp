#pragma once
// Who you might meet: characters who are in the public domain in the United
// States, drawn here after their original illustrators and given lines of
// their own (and, here and there, their own words from the original books).
// Tux is Larry Ewing's, drawn with thanks and credit. "{R}" in a line becomes
// the reward being sought.
#include "soft3d.hpp"

#include <string>
#include <vector>

namespace mz {

struct CastMember {
    std::string name;
    std::string source;            // where they come from, for the credits
    std::vector<std::string> lines;
    double w = .7, h = .95;        // size in the maze (world units)
};

int cast_count();
const CastMember& cast_member(int i);
const Tex32& cast_tex(int i);

}  // namespace mz
