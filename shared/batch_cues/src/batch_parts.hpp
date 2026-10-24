#pragma once
// The cue tables behind register_batch_cues(), one per batch script.
#include "voice_bank.hpp"

#include <span>

namespace batch_cues {

// authoring/audio/work/src/sfx.py: ui_undo_01..02, ui_hint_01..02, ui_invalid_01..02.
[[nodiscard]] std::span<const voice::Cue> sfx_cues();
// authoring/audio/work/src/sfx2.py: the Gems, Sudoku, Untangle, Puzzle Solve and
// Switchbox effects, the shared name-entry and new-game cues, atom_probe_check_wrong
// and Switchbox's win and top-score stingers.
[[nodiscard]] std::span<const voice::Cue> sfx2_cues();
// songs2.py + compose.py + render_music.py --batch2: music_switchbox_loop.
[[nodiscard]] std::span<const voice::Cue> music_cues();

}  // namespace batch_cues
