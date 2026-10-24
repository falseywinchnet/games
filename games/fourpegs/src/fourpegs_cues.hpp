#pragma once
// Four Pegs' sound, rendered live by the voice engine (shared/voice) instead of being
// read from assets/audio: audio_src/make_sfx.py (the effects and the butler's voice) and
// audio_src/villain_music.py (the three music tiers of "The Mastermind's Console" and
// three stingers) ported cue by cue, each starting from the generator states its script
// had reached when it began that cue, so each renders alone and equals the master the
// script wrote. The music plays through fourpegs_audio, whose clips come from the same
// loader, so the tiers' loop points are unchanged.
#include "voice_bank.hpp"

#include <span>

namespace fp {

// fp_voice_01..08, fp_voice_hm, fp_chuckle, fp_laugh, fp_rumble, fp_crash,
// fp_console_boot, fp_pick, fp_place_01..03, fp_remove, fp_check, fp_pin_exact,
// fp_pin_near, fp_pin_none, fp_invalid, fp_desk, fp_clap, fp_music_t1..t3 and
// fp_stinger_defeat/triumph/topscore, as the game names them.
[[nodiscard]] std::span<const voice::Cue> sound_cues();
// Adds them to voice::Bank::shared(), where the shell's clip loader finds them.
void register_sound_cues();

// villain_music.py, one cue each (fourpegs_music.cpp).
[[nodiscard]] voice::Stereo music_tier1();
[[nodiscard]] voice::Stereo music_tier2();
[[nodiscard]] voice::Stereo music_tier3();
[[nodiscard]] voice::Stereo stinger_defeat();
[[nodiscard]] voice::Stereo stinger_triumph();
[[nodiscard]] voice::Stereo stinger_topscore();

}  // namespace fp
