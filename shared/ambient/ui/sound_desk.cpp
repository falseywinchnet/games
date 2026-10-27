#include "sound_desk.hpp"

#include <algorithm>
#include <cmath>

namespace ambient {

SoundDesk::SoundDesk() {
    beds_.route(bed_count + 1, games::AudioBus::music);  // live music: the Music volume
}

void SoundDesk::music(const std::string& name, bool on) {
    score_.music(on ? name : std::string(), on);
}

void SoundDesk::effect(const std::string& name, double gain, double rate, double pan) {
    if (name.empty() || !foreground_ || !sound_on_)
        return;
    score_.effects(name, gain, rate, true, std::clamp(pan, -1.0, 1.0));
}

void SoundDesk::bed(const std::string& name, double gain, double rate) {
    if (name.empty())
        return;
    std::size_t chosen = bed_count;
    for (std::size_t index = 0; index < bed_count; ++index) {
        if (channels_[index].name == name) {
            chosen = index;
            break;
        }
        if (chosen == bed_count && channels_[index].name.empty())
            chosen = index;
    }
    if (chosen == bed_count)
        return;
    Bed& channel = channels_[chosen];
    channel.name = name;
    const bool audible = foreground_ && sound_on_;
    channel.target = audible ? std::clamp(gain, 0.0, 1.0) : 0;
    channel.target_rate = std::clamp(rate, 0.5, 2.0);
    if (channel.target > 0 && !channel.started) {
        beds_.start(chosen, name, true, 0, channel.target_rate);
        channel.rate = channel.target_rate;
        channel.started = true;
    }
}

#ifdef GUI_FORMS_AUDIO_GENERATOR
void SoundDesk::live(const std::shared_ptr<gui_forms::AudioGenerator>& source, double gain) {
    if (!source)
        return;
    const bool audible = foreground_ && sound_on_;
    live_.target = audible ? std::clamp(gain, 0.0, 1.0) : 0;
    if (live_source_ != source) {
        live_source_ = source;
        live_.started = false;
        live_.gain = 0;
    }
    if (live_.target > 0 && !live_.started) {
        beds_.generate(bed_count, source, 0);
        live_.started = true;
    }
}

void SoundDesk::live_music(const std::shared_ptr<gui_forms::AudioGenerator>& source, double gain) {
    if (!source)
        return;
    const bool audible = foreground_ && music_on_;
    live_music_.target = audible ? std::clamp(gain, 0.0, 1.0) : 0;
    if (live_music_source_ != source) {
        live_music_source_ = source;
        live_music_.started = false;
        live_music_.gain = 0;
    }
    if (live_music_.target > 0 && !live_music_.started) {
        beds_.generate(bed_count + 1, source, 0);
        live_music_.started = true;
    }
}
#endif

void SoundDesk::quiet_beds() {
    for (Bed& channel : channels_)
        channel.target = 0;
    live_.target = 0;
    live_music_.target = 0;
}

void SoundDesk::tick(double seconds) {
    score_.tick(seconds);
    beds_.tick();
    const double dt = std::clamp(seconds, 0.0, 0.25);
    for (std::size_t index = 0; index < bed_count; ++index) {
        Bed& channel = channels_[index];
        if (!channel.started)
            continue;
        // Quick to rise, slower to fall; the rate glides like an engine's inertia.
        const double response = channel.target > channel.gain ? 0.08 : 0.3;
        channel.gain += (channel.target - channel.gain) * (1 - std::exp(-dt / response));
        channel.rate += (channel.target_rate - channel.rate) * (1 - std::exp(-dt / 0.12));
        beds_.gain(index, channel.gain);
        beds_.rate(index, channel.rate);
        if (channel.target == 0 && channel.gain < 0.0005) {
            channel.gain = 0;
            beds_.pause(index);
        } else if (!beds_.playing(index)) {
            beds_.resume(index);
        }
    }
    Bed* const lives[2] = {&live_, &live_music_};
    for (int k = 0; k < 2; ++k) {
        Bed& live = *lives[k];
        const std::size_t slot = bed_count + static_cast<std::size_t>(k);
        if (!live.started)
            continue;
        const double response = live.target > live.gain ? 0.08 : 0.3;
        live.gain += (live.target - live.gain) * (1 - std::exp(-dt / response));
        beds_.gain(slot, live.gain);
        if (live.target == 0 && live.gain < 0.0005) {
            live.gain = 0;
            beds_.pause(slot);
        } else if (!beds_.playing(slot)) {
            beds_.resume(slot);
        }
    }
}

bool SoundDesk::needs_tick() const {
    if (score_.needs_tick() || beds_.pending())
        return true;
    for (const Bed& channel : channels_) {
        if (channel.started && (channel.gain > 0.0005 || channel.target > 0))
            return true;
    }
    return (live_.started && (live_.gain > 0.0005 || live_.target > 0)) || (live_music_.started && (live_music_.gain > 0.0005 || live_music_.target > 0));
}

void SoundDesk::cabinet(bool foreground, bool music, bool sound) {
    foreground_ = foreground;
    sound_on_ = sound;
    music_on_ = music;
    score_.cabinet(foreground, music, sound);
    if (!music)
        live_music_.target = 0;
    if (!foreground || !sound)
        quiet_beds();
    if (!foreground) {
        // Behind the shelf: silence at once, no fade left running.
        for (std::size_t index = 0; index < bed_count; ++index) {
            if (channels_[index].started) {
                channels_[index].gain = 0;
                beds_.gain(index, 0);
                beds_.pause(index);
            }
        }
        if (live_.started) {
            live_.gain = 0;
            beds_.gain(bed_count, 0);
            beds_.pause(bed_count);
        }
        if (live_music_.started) {
            live_music_.gain = 0;
            beds_.gain(bed_count + 1, 0);
            beds_.pause(bed_count + 1);
        }
    }
}

void SoundDesk::stop() {
    score_.stop();
    beds_.shutdown();
    for (Bed& channel : channels_)
        channel = Bed{};
    live_ = Bed{};
    live_music_ = Bed{};
#ifdef GUI_FORMS_AUDIO_GENERATOR
    live_source_.reset();
    live_music_source_.reset();
#endif
}

} // namespace ambient
