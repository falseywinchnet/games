#pragma once
// Catching Thieves' sound, rendered live by the voice engine (shared/voice) instead of
// being read from assets/audio: audio_src/make_sfx.py (the effects) and
// audio_src/garden_music.py (the five seasons of "Pumpkin Patrol" and three stingers)
// ported cue by cue, each starting from the generator states its script had reached
// when it began that cue, so each renders alone and equals the master the script wrote.
#include "voice_bank.hpp"

#include <span>

namespace ct {

// ct_step_01..03, ct_push_01..03, ct_trap, ct_pop, ct_duck, ct_muffle_01..02,
// ct_raspberry, ct_crunch, ct_laugh, ct_bump, ct_undo, ct_restart, ct_hint, ct_click,
// ct_music_spring/summer/autumn/winter/night and ct_stinger_win/stuck/book.
[[nodiscard]] std::span<const voice::Cue> sound_cues();
// Adds them to voice::Bank::shared(), where the shell's clip loader finds them.
void register_sound_cues();

// garden_music.py, one cue each (thieves_music.cpp).
[[nodiscard]] voice::Stereo music_spring();
[[nodiscard]] voice::Stereo music_summer();
[[nodiscard]] voice::Stereo music_autumn();
[[nodiscard]] voice::Stereo music_winter();
[[nodiscard]] voice::Stereo music_night();
[[nodiscard]] voice::Stereo stinger_win();
[[nodiscard]] voice::Stereo stinger_stuck();
[[nodiscard]] voice::Stereo stinger_book();

}  // namespace ct
