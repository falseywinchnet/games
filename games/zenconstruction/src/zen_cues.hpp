#pragma once
// Rock Stack's sounds, rendered live by the voice engine (shared/voice) instead of being
// read from assets/audio: audio_src/make_sfx.py (the effects and the four looped beds)
// and audio_src/stone_garden.py (the score) ported line for line. Each cue starts from
// the generator states the script had reached when it began that sound, so each renders
// alone and equals the master the script wrote. The shared name-entry clicks
// (ui_name_*) are not here; they belong to the shell's own set.
#include "voice_bank.hpp"

#include <span>

namespace zc {

// zc_clip, zc_unclip, zc_knock_1..3, zc_tumble, zc_whoosh, zc_land, zc_reset, zc_click,
// zc_relay, zc_clunk, zc_best, zc_squeak, zc_bird_1..3, zc_motor, zc_brook, zc_rush,
// zc_reeds, zc_splash_1..4, zc_bed_1..3, zc_frog_1..3 and zc_music, as the game names them.
[[nodiscard]] std::span<const voice::Cue> sound_cues();
// Adds them to voice::Bank::shared(), where the shell's clip loader finds them.
void register_sound_cues();

}  // namespace zc
