#include "tank_voice.hpp"

#include <algorithm>
#include <cmath>

namespace sw {
namespace {

constexpr double tau = 6.28318530717958647692;
// make_audio.py normalizes its loop to a 0.3 peak; this is that factor, so the live
// tank sits at the level the loop did.
constexpr double level = 6.15;
constexpr double bubble_seconds = 0.16;

} // namespace

TankVoice::TankVoice(std::uint32_t seed) : state_(seed * 2654435761U + 3U) {}

double TankVoice::uniform() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return (state_ & 0xFFFFFF) / 16777216.0;
}

void TankVoice::start_bubble() {
    for (Bubble& bubble : bubbles_) {
        if (bubble.age >= 0)
            continue;
        bubble.age = 0;
        bubble.phase = 0;
        bubble.frequency = 440 + uniform() * 900;
        bubble.gain = 0.022 + uniform() * 0.012;
        const double pan = 0.25 + uniform() * 0.5;
        bubble.left = std::sqrt(1 - pan);
        bubble.right = std::sqrt(pan);
        return;
    }
}

void TankVoice::render_add(std::span<float> stereo, double gain) {
    const std::size_t frames = stereo.size() / 2;
    const double dt = 1.0 / sample_rate;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        time_ += dt;
        if (time_ > 4096)
            time_ -= 4096;  // a whole number of every swell period
        pump_phase_ += 55 * dt;
        if (pump_phase_ >= 1)
            pump_phase_ -= 1;
        const double pump = 0.012 * std::sin(tau * pump_phase_) + 0.005 * std::sin(tau * 2 * pump_phase_);
        const double swell = 0.8 + 0.2 * std::sin(tau * time_ / 16) * std::sin(tau * time_ / 64 + 1.3);
        water_left_ = 0.965 * water_left_ + 0.035 * (uniform() - 0.5);
        water_right_ = 0.965 * water_right_ + 0.035 * (uniform() - 0.5);
        double left = pump + 0.17 * water_left_ * swell;
        double right = pump + 0.16 * water_right_ * swell;
        next_bubble_ -= dt;
        if (next_bubble_ <= 0) {
            start_bubble();
            // Mostly a steady trickle, now and then a little cluster.
            next_bubble_ += uniform() < 0.25 ? 0.07 + uniform() * 0.11 : 0.35 + uniform() * 0.45;
        }
        for (Bubble& bubble : bubbles_) {
            if (bubble.age < 0)
                continue;
            const double age = bubble.age;
            // A rising chirp: frequency + 1600 age Hz, as the loop's 800 age^2 phase term.
            bubble.phase += (bubble.frequency + 1600 * age) * dt;
            if (bubble.phase >= 1)
                bubble.phase -= 1;
            const double value =
                bubble.gain * (1 - std::exp(-age * 700)) * std::exp(-age * 36) * std::sin(tau * bubble.phase);
            left += value * bubble.left;
            right += value * bubble.right;
            bubble.age += dt;
            if (bubble.age >= bubble_seconds)
                bubble.age = -1;
        }
        stereo[frame * 2] += static_cast<float>(left * level * gain);
        stereo[frame * 2 + 1] += static_cast<float>(right * level * gain);
    }
}

} // namespace sw
