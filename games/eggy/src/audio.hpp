#pragma once
// Native audio: crossfading looped music (sample-accurate PCM buffers),
// ambience beds, and pitch-varied sound effects. Missing files stay silent.
#include <string>

namespace eggy {

void audio_start(const std::string& asset_dir);
void audio_music(const std::string& track, bool enabled);   // crossfades to track ("" = none)
void audio_ambience(float wind, float water, float forest, float rain, float fire, bool enabled);
void audio_sfx(const std::string& name, float gain = 1.f, float rate = 1.f, bool enabled = true);
void audio_duck_music(float amount);                         // 0..1 temporary music dip (stingers)
void audio_tick(double dt);
void audio_stop();
void audio_cabinet(bool foreground, bool music, bool sound);

std::string asset_dir();
double wall_clock();   // unix seconds

}  // namespace eggy
