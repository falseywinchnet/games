#pragma once
// Liar's Dice's sounds, rendered live by the voice engine (shared/voice) instead of being
// read from assets/audio: audio_src/make_sfx.py (the effects and voices) and
// audio_src/locker_music.py ("The Locker Waltz", its tense arrangement and the stingers)
// ported line by line, each cue starting from the generator states and note cache the
// script had reached when it began that file, so each renders alone and equals its master.
#include "voice_bank.hpp"

#include <span>

namespace ld {

// Every file the game plays, under the names it plays them: the effects (ld_rattle_01..03,
// ld_cup_down, ld_cup_lift, ld_tap, ld_count, ld_plunk, ld_out, ld_nope, ld_click,
// ld_voice_<kind>_01..03, ld_keeper_01..03) as mono cues; the music (ld_music,
// ld_music_tense) and stingers (ld_liar, ld_true, ld_false, ld_stinger_win,
// ld_stinger_lose, ld_stinger_free) as stereo cues.
[[nodiscard]] std::span<const voice::Cue> sound_cues();
// Adds them to voice::Bank::shared(), where the shell's clip loader finds them.
void register_sound_cues();

}  // namespace ld
