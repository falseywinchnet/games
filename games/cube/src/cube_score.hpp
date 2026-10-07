#pragma once
// Nature Cube's live score: the improvising music (glass_music.hpp) and the synthesized
// effects (glass_effects.hpp), offered to the shell's audio path as a games::LiveScore.
// The puzzle view keeps calling sound_play() with the cube's event names; the score
// answers them, so the game needs no audio code of its own.
#include "audio.hpp"

#include <memory>

namespace ps_cube {

// A new score, or nothing when the toolkit cannot play live sources.
[[nodiscard]] std::shared_ptr<games::LiveScore> make_score();

} // namespace ps_cube
