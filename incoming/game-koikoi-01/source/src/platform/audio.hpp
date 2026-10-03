#pragma once
// Native audio: crossfading looped music (sample-accurate PCM buffers) and
// pitch-varied sound effects. Missing files stay silent.
#include <string>

namespace kk {

void audio_start(const std::string& asset_dir);
void audio_music(const std::string& track, bool enabled);   // crossfades to track ("" = none)
// Cuts to `track` exactly on the next bar line of the music now playing (bar
// lengths come from the manifest), so tempo and intensity shifts land on the beat.
void audio_music_on_bar(const std::string& track, bool enabled);
void audio_sfx(const std::string& name, float gain = 1.f, float rate = 1.f, bool enabled = true);
void audio_duck_music(float amount);                         // 0..1 temporary music dip (stingers)
void audio_tick(double dt);
void audio_stop();

std::string asset_dir();
double backing_scale();
double wall_clock();   // unix seconds

}  // namespace kk
