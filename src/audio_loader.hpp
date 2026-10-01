#pragma once
#include "gui_forms/audio/audio.hpp"
#include <future>
#include <stop_token>
#include <string>

namespace games {
// One process-wide worker, at most 32 queued reads. Cancellation is observed
// during Vorbis decoding and before publishing a completed WAV read.
std::future<gui_forms::AudioClipResult> load_audio_clip(const std::string& name, std::stop_source cancellation);
}
