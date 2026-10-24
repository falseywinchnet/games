#pragma once
// eggy_music.py's stages, exposed for the tests and for measuring: the master before
// the script's levelling, ffmpeg's integrated loudness of it, and the finished loop.
#include "voice_synth.hpp"

#include <string_view>

namespace eggy {

// compose.master's output scaled to a 0.5 peak (what the script first writes to WAV).
[[nodiscard]] voice::Stereo render_master(std::string_view id);
// ffmpeg's ebur128 integrated loudness (LUFS) of that master written as 24-bit PCM.
[[nodiscard]] double integrated_loudness(const voice::Stereo& x);
// The value as ffmpeg prints it ("%5.1f") and the script reads it back.
[[nodiscard]] double printed_loudness(double integrated);
// The finished loop: levelled to -19 LUFS by that reading and soft-limited.
[[nodiscard]] voice::Stereo render_track(std::string_view id);

}  // namespace eggy
