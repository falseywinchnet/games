#include "fourpegs_audio.hpp"
#include "audio_loader.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>

namespace games {
FourPegsTrack fourpegs_track(std::string_view name) {
    // Source: assets/audio/fourpegs_audio_manifest.json. Runtime Vorbis files
    // are trimmed to these exact loop ends by prepare_portable_assets.py.
    const std::array<FourPegsTrack, 5> tracks{{{"fp_music_t1", 99310, 3177931},
                                               {"fp_music_t2", 90000, 2880000},
                                               {"fp_music_t3", 80000, 2560000},
                                               {"ld_music", 65454, 1570896},
                                               {"ld_music_tense", 65454, 1570896}}};
    for (const FourPegsTrack& track : tracks) {
        if (track.name == name) {
            return track;
        }
    }
    return {};
}
void FourPegsAudio::discard_load() {
    cancellation_.request_stop();
    loading_ = {};
    cancellation_ = std::stop_source{};
    loaded_.reset();
    load_failed_ = false;
}
void FourPegsAudio::close_music() {
    if (ready_) {
        const gui_forms::AudioLoopStatus closed = transport_.close();
        if (closed != gui_forms::AudioLoopStatus::ok) {
            std::cerr << "Four Pegs music close: " << static_cast<int>(closed) << '\n';
        }
    }
    transport_ = gui_forms::AudioLoopTransport{};
    ready_ = false;
    paused_ = false;
    submitted_ = false;
    request_id_ = cancel_id_ = 0;
    sent_gain_ = -1;
}
void FourPegsAudio::music(const std::string& name, bool enabled) {
    enabled = enabled && foreground_ && master_music_;
    const FourPegsTrack desired = fourpegs_track(name);
    if (wanted_.name != desired.name) {
        discard_load();
        wanted_ = desired;
        cancel_id_ = request_id_;
        submitted_ = false;
    }
    if (enabled && !enabled_) {
        load_failed_ = false;
    }
    enabled_ = enabled;
    if (wanted_.name.empty()) {
        discard_load();
        close_music();
    }
}
bool FourPegsAudio::accept(gui_forms::AudioLoopCommand command, const char* operation) {
    if (command.status == gui_forms::AudioLoopStatus::ok) {
        return true;
    }
    // Queue saturation is temporary. Keep the desired state and retry next tick.
    if (command.status != gui_forms::AudioLoopStatus::quota_exceeded) {
        std::cerr << "Four Pegs " << operation << ": " << static_cast<int>(command.status) << '\n';
    }
    return false;
}
void FourPegsAudio::tick(double dt) {
    player_.tick();
    if (!std::isfinite(dt)) {
        return;
    }
    dt = std::clamp(dt, 0.0, .5);
    duck_ = std::max(0.0, duck_ - dt * .25);
    if (wanted_.name.empty() || playback_failed_) {
        return;
    }
    if (!ready_) {
        if (!enabled_) {
            return;
        }
        const gui_forms::AudioLoopStatus created = player_.loop_transport(transport_);
        if (created != gui_forms::AudioLoopStatus::ok) {
            return;
        }
        ready_ = true;
    }
    if (request_id_ != 0) {
        // Poll also collects clips retired by the render thread. A stable track
        // must not retain its outgoing decoded buffer until the next tier change.
        const gui_forms::AudioLoopReceipt receipt = transport_.poll(request_id_);
        const gui_forms::AudioStatus engine_status = player_.status();
        if (engine_status != gui_forms::AudioStatus::ok ||
            receipt.status == gui_forms::AudioLoopStatus::backend_error) {
            playback_failed_ = true;
            discard_load();
            close_music();
            std::cerr << "Four Pegs audio backend stopped; reopen the game to retry.\n";
            return;
        }
    }
    if (paused_ == enabled_) {
        gui_forms::AudioLoopCommand command{};
        if (enabled_) {
            command = transport_.resume();
        } else {
            command = transport_.pause();
        }
        const bool accepted = accept(command, "music pause/resume");
        if (!accepted) {
            return;
        }
        paused_ = !enabled_;
    }
    if (cancel_id_ != 0) {
        const gui_forms::AudioLoopCommand command = transport_.cancel(cancel_id_);
        const bool accepted = accept(command, "music cancel");
        if (!accepted) {
            return;
        }
        cancel_id_ = 0;
    }
    if (!enabled_) {
        return;
    }
    const double gain = .42 * (1 - .75 * duck_);
    if (sent_gain_ != gain) {
        const gui_forms::AudioLoopCommand command = transport_.set_gain(gain);
        const bool accepted = accept(command, "music gain");
        if (!accepted) {
            return;
        }
        sent_gain_ = gain;
    }
    if (submitted_) {
        return;
    }
    if (!loaded_ && !loading_.valid() && !load_failed_) {
        loading_ = load_audio_clip(std::string(wanted_.name), cancellation_);
    }
    if (loading_.valid()) {
        const std::future_status readiness = loading_.wait_for(std::chrono::seconds(0));
        if (readiness != std::future_status::ready) {
            return;
        }
        try {
            gui_forms::AudioClipResult result = loading_.get();
            if (result.status == gui_forms::AudioStatus::ok && result.clip &&
                (*result.clip).frames() == wanted_.loop_frames) {
                loaded_ = std::move(result.clip);
            } else {
                load_failed_ = true;
                std::cerr << "Four Pegs music decode/loop length failed: " << wanted_.name << '\n';
            }
        } catch (const std::exception& error) {
            load_failed_ = true;
            std::cerr << "Four Pegs music decode: " << error.what() << '\n';
        }
    }
    if (!loaded_) {
        return;
    }
    // 80 ms scheduling lead and 120 ms outgoing fade, matching the game's
    // authored bar-cut intent. Both are sample counts on the shared 48 kHz clock.
    const gui_forms::AudioLoopCommand command =
        transport_.change(loaded_, {wanted_.bar_frames, 3840, 5760});
    const bool accepted = accept(command, "music change");
    if (accepted) {
        request_id_ = command.id;
        submitted_ = true;
        loaded_.reset(); // Transport retains ownership through its final sample.
    }
}
void FourPegsAudio::effects(const std::string& name, double gain, double rate, bool enabled) {
    if (!enabled || !foreground_ || !master_sound_ || !std::isfinite(gain) ||
        !std::isfinite(rate)) {
        return;
    }
    const std::size_t slot = next_effect_;
    next_effect_ = (next_effect_ + 1) % 24;
    player_.start(slot, name, false, std::clamp(gain, 0.0, 1.0), std::clamp(rate, .5, 2.0));
}
void FourPegsAudio::duck(double amount) {
    if (std::isfinite(amount)) {
        duck_ = std::max(duck_, std::clamp(amount, 0.0, 1.0));
    }
}
void FourPegsAudio::cabinet(bool foreground, bool music, bool sound) {
    foreground_ = foreground;
    master_music_ = music;
    master_sound_ = sound;
    if (!foreground) {
        stop();
        return;
    }
    if (!sound) {
        for (std::size_t index = 0; index < 24; ++index) {
            player_.clear(index);
        }
    }
    if (!music) {
        enabled_ = false;
        tick(0);
    }
}
void FourPegsAudio::stop() {
    discard_load();
    close_music();
    player_.shutdown();
    wanted_ = {};
    enabled_ = false;
    playback_failed_ = false;
    duck_ = 0;
}
FourPegsAudio::~FourPegsAudio() {
    stop();
}
bool FourPegsAudio::pending() const {
    const bool result = loading_.valid() || player_.pending();
    return result;
}
gui_forms::AudioStatus FourPegsAudio::status() const {
    const gui_forms::AudioStatus result = player_.status();
    return result;
}
gui_forms::AudioLoopReceipt FourPegsAudio::music_receipt() {
    const gui_forms::AudioLoopReceipt result = transport_.poll(request_id_);
    return result;
}
gui_forms::AudioStatus FourPegsAudio::render(std::span<float> samples) {
    const gui_forms::AudioStatus result = player_.render(samples);
    return result;
}
} // namespace games
