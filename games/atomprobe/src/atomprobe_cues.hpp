#pragma once
// Atom Probe's sound, rendered live by the voice engine (shared/voice) instead of being
// read from assets/audio: audio_src/make_sfx.py (the effects) and audio_src/lab_music.py
// (the score "Black Box" and its three stingers) ported cue by cue, each starting from
// the generator states its script had reached when it began that cue, so each renders
// alone and equals the master the script wrote.
#include "voice_bank.hpp"

#include <span>

namespace ap {

// ap_hover ... ap_boot (19 effects), ap_music and ap_stinger_win/miss/topscore, as the
// game names them.
[[nodiscard]] std::span<const voice::Cue> sound_cues();
// Adds them to voice::Bank::shared(), where the shell's clip loader finds them.
void register_sound_cues();

// lab_music.py, one cue each (atomprobe_music.cpp).
[[nodiscard]] voice::Stereo music();
[[nodiscard]] voice::Stereo stinger_win();
[[nodiscard]] voice::Stereo stinger_miss();
[[nodiscard]] voice::Stereo stinger_topscore();

}  // namespace ap
