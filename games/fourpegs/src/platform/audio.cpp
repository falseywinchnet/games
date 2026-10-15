#include "audio.hpp"
#include "bar_music.hpp"
#include "runtime_paths.hpp"
#include <array>
#include <chrono>

namespace fp {
namespace {
// Source: assets/audio/fourpegs_audio_manifest.json.
constexpr std::array<games::BarTrack, 3> tracks{{{"fp_music_t1", 99310, 3177931},
                                                 {"fp_music_t2", 90000, 2880000},
                                                 {"fp_music_t3", 80000, 2560000}}};
games::BarMusic audio{tracks};
}
std::string asset_dir() {
    const std::string result = games::asset_directory();
    return result;
}
double wall_clock() {
    const std::chrono::system_clock::duration elapsed = std::chrono::system_clock::now().time_since_epoch();
    const double seconds = std::chrono::duration<double>(elapsed).count();
    return seconds;
}
void audio_start(const std::string&) {}
void audio_music(const std::string& track, bool enabled) { audio.music(track, enabled); }
void audio_music_on_bar(const std::string& track, bool enabled) { audio.music(track, enabled); }
void audio_sfx(const std::string& name, float gain, float rate, bool enabled) { audio.effects(name, gain, rate, enabled); }
void audio_duck_music(float amount) { audio.duck(amount); }
void audio_tick(double dt) { audio.tick(dt); }
void audio_stop() { audio.stop(); }
void audio_cabinet(bool foreground, bool music, bool sound) { audio.cabinet(foreground, music, sound); }
}
