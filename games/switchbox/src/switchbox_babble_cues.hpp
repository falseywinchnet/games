#pragma once
// Switchbox's clap and her six babble syllables, rendered live by the voice engine
// (shared/voice) instead of being read from assets/audio. The source is the script
// delivered with the game, incoming/game-switchbox-01/source/audio_src/make_sfx.py,
// ported line for line; it uses no shared generator, so every cue renders alone and
// equals the master that script wrote. Switchbox's other sounds come from the shared
// authoring batches and are not here.
#include "voice_bank.hpp"

#include <span>

namespace sbx {

// switchbox_clap and switchbox_babble_01..06, as the game names them.
[[nodiscard]] std::span<const voice::Cue> babble_cues();
// Adds them to voice::Bank::shared(), where the shell's clip loader finds them.
void register_babble_cues();

}  // namespace sbx
