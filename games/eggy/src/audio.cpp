#include "audio.hpp"
#include "runtime_paths.hpp"
#include "scene_audio.hpp"
#include <chrono>

namespace eggy {
namespace { games::SceneAudio audio{}; }
std::string asset_dir() { return games::asset_directory(); }
double wall_clock() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }
void audio_start(const std::string&) {}
void audio_music(const std::string& track, bool enabled) { audio.music(track, enabled); }
void audio_ambience(float wind, float water, float forest, float rain, float fire, bool enabled) { audio.ambience({wind, water, forest, rain, fire}, enabled); }
void audio_sfx(const std::string& name, float gain, float rate, bool enabled) { audio.effects(name, gain, rate, enabled); }
void audio_duck_music(float amount) { audio.duck(amount); }
void audio_tick(double dt) { audio.tick(dt); }
void audio_stop() { audio.stop(); }
void audio_cabinet(bool foreground, bool music, bool sound) { audio.cabinet(foreground, music, sound); }
}
