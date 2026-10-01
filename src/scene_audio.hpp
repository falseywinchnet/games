#pragma once
#include "pcm_player.hpp"
#include <array>

namespace games {
// Shared game policy for the two animated controls: two music slots, five
// ambience beds and a bounded 24-voice pool. All calls are on the UI thread.
class SceneAudio final {
public:
    void music(const std::string& name, bool enabled);
    void effects(const std::string& name, double gain, double rate, bool enabled);
    void ambience(const std::array<double, 5>& values, bool enabled);
    void cabinet(bool foreground, bool music, bool sound);
    void duck(double amount);
    void tick(double dt);
    void stop();
private:
    PcmPlayer player_{};
    std::array<double, 2> music_gain_{};
    std::array<double, 5> bed_gain_{}, bed_target_{};
    std::array<bool, 5> bed_started_{};
    std::array<std::string, 24> effect_names_{};
    std::size_t next_effect_{}, front_{};
    std::string wanted_{};
    bool foreground_{true}, master_music_{true}, master_sound_{true}, music_on_{};
    double duck_{};
};
}
