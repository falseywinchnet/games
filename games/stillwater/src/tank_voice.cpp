#include "tank_voice.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace sw {
namespace {

constexpr double tau = 6.28318530717958647692;
// The original ran at 22,050 Hz. Its filters are carried over by frequency, and its
// white noise is scaled so the same band holds the same power at 48 kHz.
constexpr double original_rate = 22050;
constexpr double rate = TankVoice::sample_rate;
const double noise_scale = std::sqrt(rate / original_rate);

// The 48 kHz coefficient of the original's one-pole `y += a (x - y)` at 22,050 Hz.
double carried(double coefficient) {
    const double result = 1.0 - std::pow(1.0 - coefficient, original_rate / rate);
    return result;
}

const double motor_air_rate = carried(0.025);  // about 89 Hz
const double water_rate = carried(0.16);       // about 612 Hz
const double water_low_rate = carried(0.012);  // about 42 Hz
const double bubble_soften = carried(0.22);    // about 872 Hz
const double touch_rate = carried(0.12);       // about 449 Hz
const double touch_low_rate = carried(0.015);  // about 53 Hz
// The mix glides to a new tank's over about a second.
const double glide = 1.0 - std::exp(-1.0 / (0.35 * rate));
// The flow drifts toward each new target over several seconds.
const double flow_glide = 1.0 - std::exp(-1.0 / (6.0 * rate));

// Motor modes: 60, 120, 180, 240 and 300 Hz, strongest at 120 Hz.
constexpr double motor_gains[5] = {0.0020, 0.0045, 0.0014, 0.0007, 0.0003};
constexpr double water_gain = 0.004;
constexpr double knock_seconds = 0.2;
// A posted tap older than this is not played (the device was paused meanwhile).
constexpr std::int64_t stale_nanoseconds = 330000000;

std::int64_t now_nanoseconds() {
    const std::int64_t result =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    return result;
}

TankMix planted_mix() {
    // The original's mix. Its bubbles were panned a little left of centre; the air
    // stone's column rises on the right of the picture, so they sound from there.
    TankMix mix{};
    mix.pan_low = 0.58;
    mix.pan_span = 0.27;
    return mix;
}

TankMix reef_mix() {
    // The same air stone, under a reef's stronger circulation.
    TankMix mix = planted_mix();
    mix.water = 1.6;
    return mix;
}

TankMix pool_mix() {
    // A far, quieter filter under the river's current, and instead of an air stone the
    // odd larger bubble of gas from the silt, anywhere across the bed.
    TankMix mix{};
    mix.motor = 0.55;
    mix.water = 2.1;
    mix.bubbles = 1.3;
    mix.flow_bubbles = 0.7;
    mix.radius_low = 0.0018;
    mix.radius_span = 0.0032;
    mix.bubble_gain = 1.25;
    mix.pan_low = 0.22;
    mix.pan_span = 0.56;
    return mix;
}

void glide_toward(TankMix& mix, const TankMix& target) {
    mix.motor += (target.motor - mix.motor) * glide;
    mix.water += (target.water - mix.water) * glide;
    mix.bubbles += (target.bubbles - mix.bubbles) * glide;
    mix.flow_bubbles += (target.flow_bubbles - mix.flow_bubbles) * glide;
    mix.radius_low += (target.radius_low - mix.radius_low) * glide;
    mix.radius_span += (target.radius_span - mix.radius_span) * glide;
    mix.bubble_gain += (target.bubble_gain - mix.bubble_gain) * glide;
    mix.pan_low += (target.pan_low - mix.pan_low) * glide;
    mix.pan_span += (target.pan_span - mix.pan_span) * glide;
}

} // namespace

const TankMix& tank_mix(int tank) {
    static const TankMix planted = planted_mix();
    static const TankMix reef = reef_mix();
    static const TankMix pool = pool_mix();
    if (tank == 1)
        return reef;
    if (tank == 2)
        return pool;
    return planted;
}

TankVoice::TankVoice(std::uint32_t seed, int tank) : state_(seed), knock_state_(seed ^ 0x5EED7A9U) {
    step_cos_ = std::cos(tau * 60.0 / rate);
    step_sin_ = std::sin(tau * 60.0 / rate);
    tank_ = std::clamp(tank, 0, 2);
    wanted_tank_.store(tank_, std::memory_order_relaxed);
    mix_ = tank_mix(tank_);
    target_ = mix_;
}

double TankVoice::random() {
    // The original's generator.
    state_ = state_ * 1664525U + 1013904223U;
    const double result = (static_cast<double>(state_) + 0.5) / 4294967296.0;
    return result;
}

double TankVoice::knock_random() {
    knock_state_ = knock_state_ * 1664525U + 1013904223U;
    const double result = (static_cast<double>(knock_state_) + 0.5) / 4294967296.0;
    return result;
}

void TankVoice::set_tank(int tank) {
    wanted_tank_.store(std::clamp(tank, 0, 2), std::memory_order_relaxed);
}

void TankVoice::knock(double pan) {
    const std::uint32_t index = posted_.load(std::memory_order_relaxed);
    PendingKnock& slot = pending_[index % 4];
    slot.pan.store(static_cast<float>(std::clamp(pan, -1.0, 1.0)), std::memory_order_relaxed);
    slot.at.store(now_nanoseconds(), std::memory_order_relaxed);
    posted_.store(index + 1, std::memory_order_release);
}

void TankVoice::bubble() {
    for (Resonance& voice : bubbles_) {
        if (voice.remaining != 0)
            continue;
        // Minnaert's shallow-water relation, f ~= 3.26 / r(m): each formation rings
        // at its own fixed pitch for a moment, never a musical sweep.
        const double radius = mix_.radius_low + mix_.radius_span * std::pow(random(), 2);
        const double frequency = std::min(3.26 / radius, rate * 0.45);
        const double damping = 75 + 180 * random();
        const double step = tau * frequency / rate;
        const double decay = std::exp(-damping / rate);
        voice.coefficient = 2 * decay * std::cos(step);
        voice.decay_squared = decay * decay;
        voice.previous = 0;
        voice.current = std::sin(step);
        voice.gain = (0.003 + 0.009 * random() * random()) * mix_.bubble_gain;
        voice.pan = mix_.pan_low + mix_.pan_span * random();
        voice.remaining = static_cast<unsigned int>(rate * 0.10);
        break;
    }
    // Exponential waiting times, paced by the wandering flow.
    const double per_second = std::max(0.2, mix_.bubbles + mix_.flow_bubbles * flow_);
    until_bubble_ = 1U + static_cast<unsigned int>(-std::log(random()) * rate / per_second);
}

void TankVoice::start_knock(double pan) {
    for (Knock& knock : knocks_) {
        if (knock.active)
            continue;
        knock.active = true;
        knock.frame = 0;
        knock.length = static_cast<unsigned int>(rate * knock_seconds);
        knock.left = std::sqrt(0.5 * (1.0 - pan));
        knock.right = std::sqrt(0.5 * (1.0 + pan));
        // No two fingertips land alike: the glass and its load answer a little higher
        // or lower, a little louder or softer, and the skin's noise is new each time.
        const double pitch = 0.94 + 0.12 * knock_random();
        const double shimmer = pitch * (0.97 + 0.06 * knock_random());
        knock.gain = 0.88 + 0.2 * knock_random();
        knock.body_step = tau * 220.0 * pitch / rate;
        knock.glass_step = tau * 920.0 * shimmer / rate;
        knock.bright_step = tau * 1770.0 * shimmer / rate;
        knock.contact = 0;
        knock.low = 0;
        knock.noise = static_cast<std::uint32_t>(knock_random() * 4294967295.0) | 1U;
        ++knocks_played_;
        return;
    }
}

void TankVoice::take_knocks() {
    const std::uint32_t posted = posted_.load(std::memory_order_acquire);
    if (posted == taken_)
        return;
    if (posted - taken_ > 4)
        taken_ = posted - 4;
    const std::int64_t now = now_nanoseconds();
    while (taken_ != posted) {
        const PendingKnock& slot = pending_[taken_ % 4];
        const std::int64_t at = slot.at.load(std::memory_order_relaxed);
        if (now - at < stale_nanoseconds)
            start_knock(static_cast<double>(slot.pan.load(std::memory_order_relaxed)));
        ++taken_;
    }
}

void TankVoice::render_add(std::span<float> stereo, double gain) {
    const int wanted = wanted_tank_.load(std::memory_order_relaxed);
    if (wanted != tank_) {
        tank_ = wanted;
        target_ = tank_mix(tank_);
        // The first tank is set before the voice is heard: no glide into it.
        if (first_tank_)
            mix_ = target_;
    }
    first_tank_ = false;
    take_knocks();
    const std::size_t frames = stereo.size() / 2;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        glide_toward(mix_, target_);
        const double left_noise = (2 * random() - 1) * noise_scale;
        const double right_noise = (2 * random() - 1) * noise_scale;
        // The flow wanders between targets held for ten to forty seconds.
        if (flow_hold_ == 0) {
            flow_target_ = 2 * random() - 1;
            flow_hold_ = static_cast<unsigned int>(rate * (10 + 30 * random()));
        }
        --flow_hold_;
        flow_ += (flow_target_ - flow_) * flow_glide;
        if (until_bubble_ == 0)
            bubble();
        --until_bubble_;
        double bubbles_left = 0;
        double bubbles_right = 0;
        for (Resonance& voice : bubbles_) {
            if (voice.remaining == 0)
                continue;
            const double value = voice.current * voice.gain;
            bubbles_left += value * (1 - voice.pan);
            bubbles_right += value * voice.pan;
            const double next = voice.coefficient * voice.current - voice.decay_squared * voice.previous;
            voice.previous = voice.current;
            voice.current = next;
            --voice.remaining;
        }
        // The motor's harmonics from its fundamental's rotation: sin(k theta) by recurrence.
        const double c2 = 2 * motor_cos_;
        const double s1 = motor_sin_;
        const double s2 = c2 * s1;
        const double s3 = c2 * s2 - s1;
        const double s4 = c2 * s3 - s2;
        const double s5 = c2 * s4 - s3;
        const double pump = (s1 * motor_gains[0] + s2 * motor_gains[1] + s3 * motor_gains[2] +
                             s4 * motor_gains[3] + s5 * motor_gains[4]) * mix_.motor;
        const double turned_cos = motor_cos_ * step_cos_ - motor_sin_ * step_sin_;
        motor_sin_ = motor_sin_ * step_cos_ + motor_cos_ * step_sin_;
        motor_cos_ = turned_cos;
        if (++renormalize_ >= 4096) {
            renormalize_ = 0;
            const double length = std::sqrt(motor_cos_ * motor_cos_ + motor_sin_ * motor_sin_);
            motor_cos_ /= length;
            motor_sin_ /= length;
        }
        motor_air_ += motor_air_rate * (left_noise - motor_air_);
        water_left_ += water_rate * (left_noise - water_left_);
        water_right_ += water_rate * (right_noise - water_right_);
        water_low_left_ += water_low_rate * (water_left_ - water_low_left_);
        water_low_right_ += water_low_rate * (water_right_ - water_low_right_);
        bubble_left_ += bubble_soften * (bubbles_left - bubble_left_);
        bubble_right_ += bubble_soften * (bubbles_right - bubble_right_);
        // The continuous low return flow; a stronger flow is a little louder.
        const double water = water_gain * mix_.water * (1 + 0.12 * flow_);
        const double motor = pump * (1 + 0.08 * motor_air_) + 0.004 * mix_.motor * motor_air_;
        double left = fade_ * (motor + bubble_left_ + water * (water_left_ - water_low_left_));
        double right = fade_ * (motor * 0.94 + bubble_right_ + water * (water_right_ - water_low_right_));
        fade_ = std::min(1.0, fade_ + 1.0 / (rate * 0.5));
        for (Knock& knock : knocks_) {
            if (!knock.active)
                continue;
            const double time = knock.frame / rate;
            knock.noise = knock.noise * 1664525U + 1013904223U;
            const double noise = (static_cast<double>(knock.noise) / 2147483648.0 - 1) * noise_scale;
            knock.contact += touch_rate * (noise - knock.contact);
            knock.low += touch_low_rate * (knock.contact - knock.low);
            const double attack = 1.0 - std::exp(-time * 2200.0);
            // A fingertip against water-loaded glass: brief, dull and close to the
            // ambient level, with no bright panel ring or low slam.
            const double frame_d = static_cast<double>(knock.frame);
            const double body = 0.009 * std::exp(-time * 55.0) * std::sin(knock.body_step * frame_d);
            const double glass = 0.002 * std::exp(-time * 120.0) * std::sin(knock.glass_step * frame_d) +
                                 0.001 * std::exp(-time * 160.0) * std::sin(knock.bright_step * frame_d);
            const double touch = 0.012 * (knock.contact - knock.low) * std::exp(-time * 95.0);
            const double tail = std::min(1.0, (knock_seconds - time) / 0.0116);
            const double value = (body + glass + touch) * attack * tail * knock.gain;
            left += value * knock.left;
            right += value * knock.right;
            if (++knock.frame >= knock.length)
                knock.active = false;
        }
        stereo[frame * 2] += static_cast<float>(left * gain);
        stereo[frame * 2 + 1] += static_cast<float>(right * gain);
    }
}

} // namespace sw
