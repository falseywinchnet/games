#include "audio.hpp"
#include "live_composer.hpp"
#include "repertoire.hpp"
#include "runtime_paths.hpp"
#include "scene_audio.hpp"

#include "gui_forms/audio/audio.hpp"

#include <chrono>
#include <memory>

namespace pt {
namespace {
games::SceneAudio audio{};
#ifdef GUI_FORMS_AUDIO_GENERATOR
// The aviary's music, the suite's marimba band composing as it plays (src/music); it comes
// up from silence each time it is heard again.
std::shared_ptr<games::LiveComposer> band{};
bool band_on = false;
#endif
}  // namespace
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
void audio_stop() {
    audio.stop();
#ifdef GUI_FORMS_AUDIO_GENERATOR
    band_on = false;
#endif
}
void audio_live_music(bool enabled) {
#ifdef GUI_FORMS_AUDIO_GENERATOR
    if (!band) {
        const std::int64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::uint32_t seed = static_cast<std::uint32_t>(static_cast<std::uint64_t>(now) * 2654435761ULL >> 9);
        band = std::make_shared<games::LiveComposer>(games::music::make_score("marimba_band", seed), games::music::score_level("marimba_band"));
    }
    if (enabled && !band_on) (*band).restart();
    band_on = enabled;
    audio.live_music(band, enabled);
#else
    audio.music(enabled ? "pt_music" : "", enabled);  // no live voices here: the recorded track
#endif
}
void audio_cabinet(bool foreground, bool music, bool sound) {
    audio.cabinet(foreground, music, sound);
#ifdef GUI_FORMS_AUDIO_GENERATOR
    if (!foreground || !music) band_on = false;
#endif
}
}

namespace pt {
void audio_music_on_bar(const std::string& track, bool enabled) { audio.music(track, enabled); }
double backing_scale() { return 1.0; }
}
