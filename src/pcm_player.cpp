#include "pcm_player.hpp"
#include "audio_loader.hpp"
#include <chrono>
#include <iostream>
#include <algorithm>
#include <vector>

namespace games {
void PcmPlayer::report(gui_forms::AudioStatus status, const std::string& operation) {
    if (status != gui_forms::AudioStatus::ok) {
        std::cerr << "Audio " << operation << ": status " << static_cast<int>(status) << '\n';
    }
}
void PcmPlayer::create_voice(Slot& slot) {
    if (!slot.clip || engine_.status() != gui_forms::AudioStatus::ok) { return; }
    std::shared_ptr<const gui_forms::AudioClip> playback = slot.clip;
    if (slot.pan != 0) {
        const std::span<const float> source = (*slot.clip).samples();
        std::vector<float> stereo(source.begin(), source.end());
        const float left = static_cast<float>(1 - std::max(0.0, slot.pan));
        const float right = static_cast<float>(1 + std::min(0.0, slot.pan));
        for (std::size_t sample = 0; sample + 1 < stereo.size(); sample += 2) {
            stereo[sample] *= left;
            stereo[sample + 1] *= right;
        }
        const gui_forms::AudioClipResult balanced = gui_forms::AudioClip::copy(stereo);
        if (!balanced.clip) { report(balanced.status, "pan"); return; }
        playback = balanced.clip;
    }
    const gui_forms::AudioStatus admitted = engine_.voice(playback, slot.loop, slot.voice, !slot.loop);
    report(admitted, slot.name);
    slot.voice_ready = admitted == gui_forms::AudioStatus::ok;
    if (!slot.voice_ready) { return; }
    report(slot.voice.set_gain(slot.gain), "gain");
    report(slot.voice.set_rate(slot.rate), "rate");
    if (!slot.paused) { report(slot.voice.play(), "play"); }
}
void PcmPlayer::open() {
    if (attempted_) { return; }
    attempted_ = true;
    const gui_forms::AudioStatus opened = engine_.open(offline_);
    report(opened, "device open");
}
#ifdef GUI_FORMS_AUDIO_LOOP_TRANSPORT
gui_forms::AudioLoopStatus PcmPlayer::loop_transport(gui_forms::AudioLoopTransport& output) {
    open();
    const gui_forms::AudioLoopStatus result = engine_.loop_transport(output);
    return result;
}
#endif
void PcmPlayer::start(std::size_t index, const std::string& name, bool loop, double gain, double rate, double pan) {
    if (index >= slots_.size() || name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos) { return; }
    open();
    if (engine_.status() != gui_forms::AudioStatus::ok) { return; }
    Slot& slot = slots_[index];
    if (slot.name != name || (!slot.clip && !slot.pending.valid())) {
        clear(index);
        slot.name = name;
        slot.pending = load_audio_clip(name, slot.cancellation);
    }
    slot.loop = loop;
    slot.gain = gain;
    slot.rate = rate;
    slot.pan = std::clamp(pan, -1.0, 1.0);
    slot.paused = false;
    if (slot.clip) {
        slot.voice = gui_forms::AudioVoice{};
        create_voice(slot);
    }
}
void PcmPlayer::gain(std::size_t index, double value) {
    Slot& slot = slots_.at(index);
    slot.gain = value;
    if (slot.voice_ready && engine_.status() == gui_forms::AudioStatus::ok) { report(slot.voice.set_gain(value), "gain"); }
}
void PcmPlayer::rate(std::size_t index, double value) {
    Slot& slot = slots_.at(index);
    slot.rate = value;
    if (slot.voice_ready && engine_.status() == gui_forms::AudioStatus::ok)
        report(slot.voice.set_rate(value), "rate");
}
void PcmPlayer::pause(std::size_t index) {
    Slot& slot = slots_.at(index);
    slot.paused = true;
    if (slot.voice_ready && engine_.status() == gui_forms::AudioStatus::ok) { report(slot.voice.pause(), "pause"); }
}
void PcmPlayer::resume(std::size_t index) {
    Slot& slot = slots_.at(index);
    slot.paused = false;
    if (slot.voice_ready && engine_.status() == gui_forms::AudioStatus::ok) { report(slot.voice.play(), "resume"); }
}
void PcmPlayer::clear(std::size_t index) {
    Slot& slot = slots_.at(index);
    slot.cancellation.request_stop();
    slot = Slot{};
}
bool PcmPlayer::playing(std::size_t index) const {
    const Slot& slot = slots_.at(index);
    const bool active = !slot.paused && (slot.pending.valid() || slot.voice.playing());
    return active;
}
void PcmPlayer::tick() {
    for (Slot& slot : slots_) {
        if (!slot.pending.valid()) { continue; }
        const std::future_status readiness = slot.pending.wait_for(std::chrono::seconds(0));
        if (readiness != std::future_status::ready) { continue; }
        try {
            gui_forms::AudioClipResult result = slot.pending.get();
            report(result.status, slot.name);
            slot.clip = std::move(result.clip);
            create_voice(slot);
        } catch (const std::exception& error) {
            std::cerr << "Audio " << slot.name << ": " << error.what() << '\n';
        }
    }
}
void PcmPlayer::shutdown() {
    for (std::size_t i = 0; i < slots_.size(); ++i) { clear(i); }
    engine_.shutdown();
    attempted_ = false;
}
PcmPlayer::~PcmPlayer() { shutdown(); }
bool PcmPlayer::pending() const {
    for (const Slot& slot : slots_) { if (slot.pending.valid()) { return true; } }
    return false;
}
gui_forms::AudioStatus PcmPlayer::status() const {
    const gui_forms::AudioStatus result = engine_.status();
    return result;
}
gui_forms::AudioStatus PcmPlayer::render(std::span<float> samples) {
    const gui_forms::AudioStatus result = engine_.render(samples);
    return result;
}
}
