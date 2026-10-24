#pragma once
// The Parrot's Table's sounds, rendered live by the voice engine (shared/voice) instead of
// being read from assets/audio: audio_src/make_sfx.py's effects and parlor_music.py's
// waltz loop and stingers, ported line by line, each cue starting from the generator
// states its script had reached when it began that cue, so each renders alone and
// equals its master.
#include "voice_bank.hpp"

#include <span>

namespace pt {

// pt_voice_01..06, pt_bell, pt_mark_honest, pt_mark_liar, pt_unmark, pt_contradiction,
// pt_ask, pt_gavel, pt_flap, pt_cheer, pt_tick, pt_click (mono), and pt_music,
// pt_stinger_win, pt_stinger_lose (stereo), as the game names them.
[[nodiscard]] std::span<const voice::Cue> sound_cues();
// Adds them to voice::Bank::shared(), where the shell's clip loader finds them.
void register_sound_cues();

}  // namespace pt
