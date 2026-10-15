#pragma once
// Native audio: the live band (src/music/styles/maze_band.hpp) and pitch-varied sound
// effects. Missing files stay silent.
#include "gui_forms/audio/audio.hpp"
#include <memory>
#include <string>

namespace mz {

void audio_start(const std::string& asset_dir);
#ifdef GUI_FORMS_AUDIO_GENERATOR
// Music the game synthesizes as it plays (the band), under the Music master; it fades out
// when not enabled and is paused once silent.
void audio_live_music(const std::shared_ptr<gui_forms::AudioGenerator>& source, bool enabled);
#endif
void audio_sfx(const std::string& name, float gain = 1.f, float rate = 1.f, bool enabled = true);
void audio_duck_music(float amount);                         // 0..1 temporary music dip (stingers)
void audio_tick(double dt);
void audio_stop();

std::string asset_dir();
void audio_cabinet(bool foreground, bool music, bool sound);
bool audio_needs_tick();
double wall_clock();   // unix seconds

}  // namespace mz
