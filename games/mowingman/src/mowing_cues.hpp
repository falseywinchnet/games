#pragma once
// Mowing's one-shot effects and the gnome's voice, rendered live by the voice engine
// (shared/voice) instead of being read from assets/audio.
//
// - mm_freeze, mm_shatter, mm_scream, mm_puff, mm_mushroom, mm_bump and mm_granny_bonk:
//   audio_src/make_audio.py ported line for line, each starting from the state its
//   generator (default_rng(20261005)) had reached when the script began it.
// - mm_gnome_hoo, mm_gnome_hoo2, mm_gnome_giggle, mm_gnome_giggle2 and mm_gnome_nyah:
//   audio_src/gnome_voice.mjs and the parts of the monk choir's HDR voice it performs
//   on (gnome_voice.hpp).
//
// The mower, chipper, grill, granny lines and music are the game's live voices and are
// not cues.
#include "voice_bank.hpp"

#include <span>

namespace mm {

// The seven one-shots (stereo); the gnome's five are gnome_cues() (gnome_voice.hpp).
[[nodiscard]] std::span<const voice::Cue> sound_cues();
// Adds all twelve to voice::Bank::shared(), where the shell's clip loader finds them.
void register_sound_cues();

}  // namespace mm
