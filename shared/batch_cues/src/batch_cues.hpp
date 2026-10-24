#pragma once
// The audio batch 1/2 sounds, rendered live by the voice engine (shared/voice) instead
// of being read from assets/audio: the cues authoring/audio/work/src (sfx.py, sfx2.py,
// songs2.py with compose.py and render_music.py) wrote for the card games, Gems,
// Sudoku, Untangle, Puzzle Solve, Switchbox and the shared name entry, ported cue by
// cue so that each renders alone and equals the master its script wrote. Several games
// play them, so the shell registers them once.
#include "voice_bank.hpp"

#include <span>
#include <vector>

namespace batch_cues {

// Every cue, under the names the games play (_0N variants included).
[[nodiscard]] std::vector<voice::Cue> all_cues();
// Adds them to voice::Bank::shared(), where the shell's clip loader finds them. Adding
// them again changes nothing.
void register_batch_cues();

}  // namespace batch_cues
