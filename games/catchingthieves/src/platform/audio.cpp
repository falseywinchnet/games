#include "audio.hpp"
#include "runtime_paths.hpp"
#include "scene_audio.hpp"
#include <chrono>

namespace ct {
namespace { games::SceneAudio audio{}; }
std::string asset_dir() { const std::string result = games::asset_directory(); return result; }
double wall_clock() {
    const std::chrono::system_clock::duration elapsed = std::chrono::system_clock::now().time_since_epoch();
    const double seconds = std::chrono::duration<double>(elapsed).count();
    return seconds;
}
// SceneAudio opens the shared device lazily when playback is first requested.
void audio_start(const std::string&) {}
void audio_music(const std::string& track, bool enabled) { audio.music(track, enabled); }
void audio_sfx(const std::string& name, float gain, float rate, bool enabled) { audio.effects(name, gain, rate, enabled); }
void audio_duck_music(float amount) { audio.duck(amount); }
void audio_tick(double dt) { audio.tick(dt); }
void audio_stop() { audio.stop(); }
void audio_cabinet(bool foreground, bool music, bool sound) { audio.cabinet(foreground, music, sound); }
}

namespace ct {
bool audio_needs_tick() { return audio.needs_tick(); }
}
