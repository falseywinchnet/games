#pragma once
// Pen the Sheep's effects, rendered live by the voice engine (shared/voice) instead of
// being read from assets/audio: audio_src/make_sfx.py ported cue by cue, each starting
// from the generator states the script had reached when it began that cue, so each
// renders alone and equals its master. The four music beds and the sh_tune_* stingers
// (meadow_music.py, long_grass.py, angel_music.py) are rendered the same way
// (sheep_music.cpp).
#include "voice_bank.hpp"

#include <span>

namespace sh {

// sh_place, sh_hop, sh_baa_01..04, sh_munch, sh_hint, sh_undo, sh_click, sh_nope,
// sh_penned and sh_escape, as the game names them.
[[nodiscard]] std::span<const voice::Cue> sound_cues();
// sh_music, sh_music_grass, sh_music_choir, sh_music_bells, sh_tune_penned and
// sh_tune_away: stereo masters, the loops seamless.
[[nodiscard]] std::span<const voice::Cue> music_cues();
// Adds both to voice::Bank::shared(), where the shell's clip loader finds them.
void register_sound_cues();

}  // namespace sh
