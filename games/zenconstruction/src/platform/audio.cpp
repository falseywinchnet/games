#include "audio.hpp"
#include "pcm_player.hpp"
#include "runtime_paths.hpp"
#include "scene_audio.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

namespace zc {
namespace {
games::SceneAudio score;
games::PcmPlayer beds;
struct Bed {
    const char* name;
    double gain = 0, target = 0, rate = 1, target_rate = 1;
    bool started = false;
};
std::array<Bed, 4> channels{{{"zc_rush"}, {"zc_brook"}, {"zc_reeds"}, {"zc_motor"}}};
}
std::string asset_dir() { return games::asset_directory(); }
double wall_clock() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}
double backing_scale() { return 1; }
void audio_start(const std::string&) {}
void audio_music(const std::string& name, bool on) { score.music(name, on); }
void audio_music_on_bar(const std::string& name, bool on) { score.music(name, on); }
void audio_sfx(const std::string& name, float gain, float rate, bool on, float pan) {
    score.effects(name, gain, rate, on, pan);
}
void audio_bed(const std::string& name, float gain, float rate, bool on) {
    for (std::size_t i = 0; i < channels.size(); ++i) {
        Bed& channel = channels[i];
        if (name != channel.name) continue;
        channel.target = on ? std::clamp(static_cast<double>(gain), 0.0, 1.0) : 0;
        channel.target_rate = std::clamp(static_cast<double>(rate), .5, 2.0);
        if (channel.target > 0 && !channel.started) {
            beds.start(i, name, true, 0, channel.target_rate);
            channel.rate = channel.target_rate;
            channel.started = true;
        }
    }
}
void audio_duck_music(float amount) { score.duck(amount); }
void audio_tick(double dt) {
    score.tick(dt);
    beds.tick();
    dt = std::clamp(dt, 0.0, .25);
    for (std::size_t i = 0; i < channels.size(); ++i) {
        Bed& channel = channels[i];
        if (!channel.started) continue;
        const double tau = channel.target > channel.gain ? .08 : .35;
        channel.gain += (channel.target - channel.gain) * (1 - std::exp(-dt / tau));
        channel.rate += (channel.target_rate - channel.rate) * (1 - std::exp(-dt / .22));
        beds.gain(i, channel.gain);
        beds.rate(i, channel.rate);
        if (channel.target == 0 && channel.gain < .0001) beds.pause(i);
        else if (!beds.playing(i)) beds.resume(i);
    }
}
void audio_stop() {
    score.stop();
    beds.shutdown();
    for (Bed& channel : channels) {
        channel.gain = channel.target = 0;
        channel.started = false;
    }
}
}
