#pragma once
// The repertoire: every score the suite plays, by name. A game asks for its music here,
// and the renderer (tools/music_render.cpp) can play any of them to a file.
#include "music_core.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace games::music {

// A new score of the named style, from `seed`; null for a name that is not one.
std::unique_ptr<Composer> make_score(std::string_view style, std::uint32_t seed);
// The gain its game plays the style at (the level its mix was made for); 0 for a name
// that is not one.
double score_level(std::string_view style);
// Every style's name.
std::vector<std::string_view> score_names();

}  // namespace games::music
