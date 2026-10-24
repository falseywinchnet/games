#pragma once
// Koi-Koi's sound, rendered live by the voice engine (shared/voice) instead of being
// read from assets/audio: audio_src/koikoi_audio.py ported cue by cue, each starting
// from the generator states the script had reached when it began that cue, so each
// renders alone and equals the master the script wrote. The music, "Twelve Months",
// is the same script's 32-bar loop, rendered whole (see koikoi_cues.cpp for its cost).
#include "voice_bank.hpp"

#include <span>

namespace kk {

// kk_place, kk_take, kk_flip, kk_shuffle, kk_set, kk_koikoi, kk_stop, kk_win, kk_lose,
// kk_click and kk_music, as the game names them.
[[nodiscard]] std::span<const voice::Cue> sound_cues();
// Adds them to voice::Bank::shared(), where the shell's clip loader finds them.
void register_sound_cues();

}  // namespace kk
