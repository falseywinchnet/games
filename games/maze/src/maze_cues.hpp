#pragma once
// Maze 95's effects and its two jingles, rendered live by the voice engine (shared/voice)
// instead of being read from assets/audio: audio_src/make_sfx.py and mall_music.py ported
// cue by cue, each starting from the generator states the script had reached when it began
// that cue, so each renders alone and equals its master. The level music is the live band
// (band.*) and was never a file.
#include "voice_bank.hpp"

#include <span>

namespace mz {

// mz_step_01..03, mz_turn, mz_bump, mz_locked, mz_pad, mz_door, mz_flip, mz_bulb_off,
// mz_bulb_on, mz_elevator, mz_portal, mz_marble_roll, mz_marble_bounce, mz_marble_bump,
// mz_appear, mz_poof, mz_click, the snail's three, the residents' seven (mz_meow, mz_honk,
// mz_bell, mz_burp, mz_keys, mz_ooh, mz_thinker), and the stereo mz_stinger_win and
// mz_start, as the game names them.
[[nodiscard]] std::span<const voice::Cue> sound_cues();
// Adds them to voice::Bank::shared(), where the shell's clip loader finds them.
void register_sound_cues();

}  // namespace mz
