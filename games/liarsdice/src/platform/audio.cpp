#include "audio.hpp"
#include "runtime_paths.hpp"
#include "bar_music.hpp"
#include <array>
#include <chrono>

namespace ld {
namespace {
// The calm and the tense loop, one bar apart in the same tempo.
constexpr std::array<games::BarTrack, 2> tracks{{{"ld_music", 65454, 1570896}, {"ld_music_tense", 65454, 1570896}}};
games::BarMusic audio{tracks};
}
std::string asset_dir() { const std::string result = games::asset_directory(); return result; }
double wall_clock() {
    const std::chrono::system_clock::duration elapsed = std::chrono::system_clock::now().time_since_epoch();
    const double seconds = std::chrono::duration<double>(elapsed).count();
    return seconds;
}
void audio_start(const std::string&) {}
void audio_music(const std::string& track, bool enabled) { audio.music(track, enabled); }
void audio_sfx(const std::string& name, float gain, float rate, bool enabled) { audio.effects(name, gain, rate, enabled); }
void audio_duck_music(float amount) { audio.duck(amount); }
void audio_tick(double dt) { audio.tick(dt); }
void audio_stop() { audio.stop(); }
void audio_cabinet(bool foreground, bool music, bool sound) { audio.cabinet(foreground, music, sound); }
}

namespace ld {
void audio_music_on_bar(const std::string& track, bool enabled) { audio.music(track, enabled); }
double backing_scale() { return 1.0; }
}
