#pragma once
#include <string>
namespace games {
void music_play(const std::string& game, bool enabled);
void sound_play(const std::string& name, bool enabled);
void audio_poll();
[[nodiscard]] bool audio_pending();
} // namespace games
