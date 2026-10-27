#pragma once
// A scene's sound: music (the shell's crossfading slots), one-shot effects, and up
// to eight looping beds whose gain and playback rate follow the scene from moment
// to moment (an engine's pitch with its speed, a crunch with the grass it cuts).
// Beds are smoothed so a scene may set them every frame; beds that share a rate
// stay in tune with each other, because rate shifts every component equally.
//
// Music follows the Music master; effects and beds follow the Sound master. Nothing
// plays while the scene is not in front.
#include "pcm_player.hpp"
#include "scene_audio.hpp"

#include <array>
#include <memory>
#include <string>

namespace ambient {

class SoundDesk final {
  public:
    static constexpr std::size_t bed_count = 8;
    SoundDesk();
    SoundDesk(const SoundDesk&) = delete;
    SoundDesk& operator=(const SoundDesk&) = delete;

    void music(const std::string& name, bool on);
    void effect(const std::string& name, double gain, double rate = 1, double pan = 0);
    // Sets a bed's target gain (0 silences it) and playback rate (0.5..2).
    void bed(const std::string& name, double gain, double rate = 1);
#ifdef GUI_FORMS_AUDIO_GENERATOR
    // One voice the scene synthesizes itself as it plays (an engine that answers the
    // moment it is asked). It follows the Sound master and fades like a bed; while
    // silent it is paused, which freezes the source where it was.
    void live(const std::shared_ptr<gui_forms::AudioGenerator>& source, double gain);
    // Music the scene composes as it plays: like the live voice, but it follows the
    // Music master instead of the Sound master.
    void live_music(const std::shared_ptr<gui_forms::AudioGenerator>& source, double gain);
#endif
    // Fades every bed and the live voice to silence (a paused or settled scene).
    void quiet_beds();
    void tick(double seconds);
    [[nodiscard]] bool needs_tick() const;
    void cabinet(bool foreground, bool music, bool sound);
    void stop();

  private:
    struct Bed {
        std::string name{};
        double gain{};
        double target{};
        double rate{1};
        double target_rate{1};
        bool started{};
    };
    games::SceneAudio score_{};
    games::PcmPlayer beds_{};
    std::array<Bed, bed_count> channels_{};
    Bed live_{};        // the live voice, in the slot after the beds
    Bed live_music_{};  // live music, in the slot after that
#ifdef GUI_FORMS_AUDIO_GENERATOR
    std::shared_ptr<gui_forms::AudioGenerator> live_source_{};
    std::shared_ptr<gui_forms::AudioGenerator> live_music_source_{};
#endif
    bool music_on_{true};
    bool foreground_{true};
    bool sound_on_{true};
};

} // namespace ambient
