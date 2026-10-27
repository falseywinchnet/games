#pragma once
// What you have come for. Level one is always the cheese; after that the
// briefing names something else from a long list of iconic odds and ends,
// each painted as a little sprite (no brand marks: the chicken comes in a
// plain striped bucket).
#include "soft3d.hpp"

#include <string>

namespace mz {

int reward_count();
const std::string& reward_name(int i);      // "bucket of fried chicken"
const std::string& reward_article(int i);   // "a", "an", "the", "some"
std::string reward_a(int i);                // "a bucket of fried chicken"
std::string reward_the(int i);              // "the bucket of fried chicken"
const Tex32& reward_tex(int i);

}  // namespace mz
