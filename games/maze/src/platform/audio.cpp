#include "audio.hpp"
#include "pcm_player.hpp"
#include "runtime_paths.hpp"
#include "scene_audio.hpp"
#include <algorithm>
#include <chrono>
#include <memory>

namespace mz {
namespace {
games::SceneAudio audio{};

#ifdef GUI_FORMS_AUDIO_GENERATOR
// Live music in a player of its own, under the Music master: it fades in and out as the
// recorded music did, and is paused (frozen where it was) while silent.
struct BandDesk {
    games::PcmPlayer player{};
    std::shared_ptr<gui_forms::AudioGenerator> source{};
    bool started{};
    double gain{};
    double target{};
    bool foreground{true};
    bool music{true};
};
BandDesk desk{};
constexpr double kBandLevel = .42;
#endif
}  // namespace
std::string asset_dir() { const std::string result = games::asset_directory(); return result; }
double wall_clock() {
    const std::chrono::system_clock::duration elapsed = std::chrono::system_clock::now().time_since_epoch();
    const double seconds = std::chrono::duration<double>(elapsed).count();
    return seconds;
}
// SceneAudio opens the shared device lazily when playback is first requested.
void audio_start(const std::string&) {}
#ifdef GUI_FORMS_AUDIO_GENERATOR
void audio_live_music(const std::shared_ptr<gui_forms::AudioGenerator>& source, bool enabled) {
    if (!source) return;
    desk.target = enabled && desk.foreground && desk.music ? kBandLevel : 0.0;
    if (desk.source != source) {
        desk.source = source;
        desk.started = false;
        desk.gain = 0;
    }
    if (desk.target > 0 && !desk.started) {
        desk.player.route(0, games::AudioBus::music);
        desk.player.generate(0, desk.source, 0);
        desk.started = true;
    }
}
#endif
void audio_sfx(const std::string& name, float gain, float rate, bool enabled) { audio.effects(name, gain, rate, enabled); }
void audio_duck_music(float amount) { audio.duck(amount); }
void audio_tick(double dt) {
    audio.tick(dt);
#ifdef GUI_FORMS_AUDIO_GENERATOR
    if (!desk.started) return;
    const double step = std::clamp(dt, 0.0, 0.25) / 3.5;
    desk.gain += std::clamp(desk.target - desk.gain, -step, step);
    desk.player.gain(0, desk.gain);
    if (desk.target == 0 && desk.gain == 0) desk.player.pause(0);
    else if (!desk.player.playing(0)) desk.player.resume(0);
    desk.player.tick();
#endif
}
void audio_stop() {
    audio.stop();
#ifdef GUI_FORMS_AUDIO_GENERATOR
    desk.player.shutdown();
    desk.started = false;
    desk.gain = 0;
    desk.target = 0;
#endif
}
void audio_cabinet(bool foreground, bool music, bool sound) {
    audio.cabinet(foreground, music, sound);
#ifdef GUI_FORMS_AUDIO_GENERATOR
    desk.foreground = foreground;
    desk.music = music;
    if (desk.started && (!foreground || !music)) {
        // hidden or switched off: silent at once (no frames will come to fade it)
        desk.target = 0;
        desk.gain = 0;
        desk.player.gain(0, 0);
        desk.player.pause(0);
    }
#endif
}
}

namespace mz {
bool audio_needs_tick() {
#ifdef GUI_FORMS_AUDIO_GENERATOR
    if (desk.started && desk.gain != desk.target) return true;
#endif
    return audio.needs_tick();
}
}
