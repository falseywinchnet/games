#pragma once
// Eggy's sounds, rendered live by the voice engine (shared/voice) instead of being read
// from assets/audio: authoring/eggy/audio_src ported line for line.
// - effect_cues(): eggy_sfx.py's 36 effects and its five 20 s ambience beds, each
//   starting from the states synth._rng and the script's rng had reached when the
//   script began it.
// - music_cues(): eggy_music.py's eight region loops (stereo; eggy_music.cpp).
#include "voice_bank.hpp"

#include <span>

namespace eggy {

[[nodiscard]] std::span<const voice::Cue> effect_cues();
[[nodiscard]] std::span<const voice::Cue> music_cues();
// Adds all of them to voice::Bank::shared(), where the shell's clip loader finds them.
void register_sound_cues();

}  // namespace eggy
