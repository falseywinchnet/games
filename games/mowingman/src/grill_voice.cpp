#include "grill_voice.hpp"

#include <algorithm>
#include <cmath>

namespace mm {

namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double rate = GrillVoice::sample_rate;

// Partials of a pressed-steel kettle bowl and of its lid (Hz, seconds to die away by
// 60 dB, level): stiff, inharmonic, the low ones ringing longest.
constexpr double bowl_modes[8][3] = {{188, 0.95, 1.0},  {451, 0.80, 0.9},  {797, 0.62, 0.75}, {1214, 0.50, 0.6},
                                     {1702, 0.38, 0.5}, {2268, 0.30, 0.4}, {2981, 0.22, 0.3}, {3861, 0.16, 0.22}};
constexpr double lid_modes[6][3] = {{523, 0.55, 1.0},  {1288, 0.45, 0.8}, {2342, 0.34, 0.6},
                                    {3496, 0.25, 0.45}, {4810, 0.18, 0.3}, {6120, 0.12, 0.2}};

// The level everything is struck at, against the mower it plays over.
constexpr double impact_gain = 0.032;
constexpr double fire_gain = 0.4;

void tune(double frequency, double seconds, double level, double& c1, double& c2, double& gain) {
    const double w = 2 * pi * frequency / rate;
    const double r = std::exp(-6.907755 / (seconds * rate));
    c1 = 2 * r * std::cos(w);
    c2 = r * r;
    gain = std::sin(w) * level;
}

} // namespace

GrillVoice::GrillVoice(std::uint32_t seed) : state_(seed ? seed : 13) {
    for (std::size_t i = 0; i < bowl_.size(); ++i)
        tune(bowl_modes[i][0], bowl_modes[i][1], bowl_modes[i][2], bowl_[i].c1, bowl_[i].c2, bowl_[i].gain);
    for (std::size_t i = 0; i < lid_.size(); ++i)
        tune(lid_modes[i][0], lid_modes[i][1], lid_modes[i][2], lid_[i].c1, lid_[i].c2, lid_[i].gain);
    tune(2230, 0.035, 1.0, coal_[0].c1, coal_[0].c2, coal_[0].gain);
    tune(3640, 0.025, 0.7, coal_[1].c1, coal_[1].c2, coal_[1].gain);
    tune(2400, 0.02, 1.0, pop_ring_.c1, pop_ring_.c2, pop_ring_.gain);
}

double GrillVoice::uniform() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return (state_ & 0xFFFFFF) / 16777216.0;
}

double GrillVoice::normal() {
    return (uniform() + uniform() + uniform() + uniform() - 2) * 1.2;
}

void GrillVoice::knock(double pan) {
    knock_pan_.store(static_cast<float>(pan), std::memory_order_relaxed);
    knocks_.fetch_add(1, std::memory_order_release);
}

void GrillVoice::set_fire(double strength, double pan) {
    fire_.store(static_cast<float>(std::clamp(strength, 0.0, 1.0)), std::memory_order_relaxed);
    fire_pan_.store(static_cast<float>(pan), std::memory_order_relaxed);
}

// How a grill falls: the bowl is hit and tips, the lid flies off and lands first, the bowl
// comes down on its rim, the lid bounces lower and quicker each time and rattles to rest,
// and the coals spray out as the bowl lands.
void GrillVoice::schedule(double pan) {
    pan_ = pan;
    clock_ = 0;
    // A little different every time: the steel's pitch and the timing of the fall.
    const double pitch = 1 + 0.04 * normal();
    for (std::size_t i = 0; i < bowl_.size(); ++i)
        tune(bowl_modes[i][0] * pitch * (1 + 0.01 * normal()), bowl_modes[i][1], bowl_modes[i][2], bowl_[i].c1,
             bowl_[i].c2, bowl_[i].gain);
    for (std::size_t i = 0; i < lid_.size(); ++i)
        tune(lid_modes[i][0] * pitch * (1 + 0.012 * normal()), lid_modes[i][1], lid_modes[i][2], lid_[i].c1,
             lid_[i].c2, lid_[i].gain);
    std::size_t n = 0;
    add(n, 0.0, 1.0, 0.85, 0);
    add(n, 0.0, 0.7, 0.2, 3);
    add(n, 0.035 + 0.01 * uniform(), 0.75, 0.8, 1);
    const double lands = 0.19 + 0.04 * uniform();
    add(n, lands, 0.6, 0.6, 0);
    add(n, lands, 0.9, 0.1, 3);
    // The lid's bounces: each a little lower and sooner.
    double t = 0.3 + 0.03 * uniform(), gap = 0.12, height = 0.55;
    while (height > 0.07 && n < strikes_.size() - 30) {
        add(n, t, height, 0.55 + 0.3 * height, 1);
        t += gap;
        gap *= 0.72;
        height *= 0.66;
    }
    // Then it rolls on its rim: a run of small quick touches dying away.
    for (int i = 0; i < 12 && n < strikes_.size() - 20; ++i) {
        t += 0.016 + 0.012 * uniform();
        add(n, t, 0.07 * (1 - i / 12.0), 0.45, 1);
    }
    // The coals: a spray of small dry ticks, thickest as the bowl lands.
    double c = lands;
    while (n < strikes_.size()) {
        c += 0.004 + 0.03 * uniform() * (1 + (c - lands) * 4);
        if (c > lands + 0.8)
            break;
        const double fade = std::exp(-(c - lands) * 3.5);
        add(n, c, (0.25 + 0.5 * uniform()) * fade, 0.5 + 0.5 * uniform(), 2);
    }
    for (; n < strikes_.size(); ++n)
        strikes_[n].live = false;
}

void GrillVoice::add(std::size_t& n, double at, double strength, double brightness, int what) {
    if (n < strikes_.size())
        strikes_[n++] = {at, strength, brightness, what, true};
}

void GrillVoice::strike(const Strike& s) {
    const double k = 0.06 + 0.85 * s.brightness * s.brightness;
    switch (s.what) {
    case 0:
        bowl_hit_ = {rate * (0.002 + 0.002 * uniform()), s.strength, k, bowl_hit_.a};
        break;
    case 1:
        lid_hit_ = {rate * (0.0012 + 0.0008 * uniform()), s.strength, k, lid_hit_.a};
        break;
    case 2:
        coal_hit_ = {rate * (0.0005 + 0.001 * uniform()), s.strength, k, coal_hit_.a};
        break;
    default:
        ground_hit_ = {rate * 0.03, s.strength, 0.5, ground_hit_.a};
        break;
    }
}

void GrillVoice::render_add(std::span<float> stereo) {
    const int knocks = knocks_.load(std::memory_order_acquire);
    if (knocks != heard_) {
        heard_ = knocks;
        schedule(knock_pan_.load(std::memory_order_relaxed));
    }
    const double fire_target = fire_.load(std::memory_order_relaxed);
    const double fire_pan = fire_pan_.load(std::memory_order_relaxed);
    const double impact_l = std::cos((pan_ + 1) * pi / 4), impact_r = std::sin((pan_ + 1) * pi / 4);
    const std::size_t frames = stereo.size() / 2;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        if ((frame & 31) == 0) {
            for (Strike& s : strikes_)
                if (s.live && s.at <= clock_) {
                    strike(s);
                    s.live = false;
                }
        }
        clock_ += 1 / rate;
        double impact = 0;
        const double bowl_in = bowl_hit_.run(2 * uniform() - 1);
        const double lid_in = lid_hit_.run(2 * uniform() - 1);
        const double coal_in = coal_hit_.run(2 * uniform() - 1);
        for (Mode& mode : bowl_)
            impact += mode.run(bowl_in);
        for (Mode& mode : lid_)
            impact += 0.8 * mode.run(lid_in);
        for (Mode& mode : coal_)
            impact += 0.6 * mode.run(coal_in);
        // The thump of the bowl meeting the turf: heavily muffled noise.
        ground_a_ += (ground_hit_.run(2 * uniform() - 1) - ground_a_) * 0.012;
        ground_b_ += (ground_a_ - ground_b_) * 0.012;
        impact = impact * impact_gain + ground_b_ * 1.8;

        // The fire, gliding to how hard it burns now.
        fire_level_ += (fire_target - fire_level_) * (1 / (0.3 * rate));
        fire_pan_now_ += (fire_pan - fire_pan_now_) * (1 / (0.2 * rate));
        double flame = 0;
        if (fire_level_ > 1e-4) {
            // A low, fluttering roar.
            flutter_clock_ -= 1 / rate;
            if (flutter_clock_ <= 0) {
                flutter_clock_ = 0.05 + 0.1 * uniform();
                flutter_target_ = 0.4 + 0.6 * uniform();
            }
            flutter_ += (flutter_target_ - flutter_) * 0.0008;
            const double noise = 2 * uniform() - 1;
            roar_a_ += (noise - roar_a_) * 0.04;
            roar_b_ += (roar_a_ - roar_b_) * 0.04;
            flame += roar_b_ * 1.6 * flutter_;
            // Dry pops: more of them, and harder, the hotter it is.
            if (pop_left_ <= 0 && uniform() < (2 + 14 * fire_level_) / rate) {
                pop_left_ = 20 + 70 * uniform();
                const double e = -std::log(std::max(1e-6, uniform()));
                pop_level_ = std::min(1.0, 0.12 * e);
            }
            double pop = 0;
            if (pop_left_ > 0) {
                pop_left_ -= 1;
                const double n = 2 * uniform() - 1;
                pop = (n - pop_prev_) * pop_level_;
                pop_prev_ = n;
            }
            flame += pop * 0.5 + pop_ring_.run(pop) * 0.4;
            flame *= fire_gain * fire_level_;
        }
        const double fire_l = std::cos((fire_pan_now_ + 1) * pi / 4), fire_r = std::sin((fire_pan_now_ + 1) * pi / 4);
        stereo[2 * frame] += static_cast<float>(impact * impact_l + flame * fire_l);
        stereo[2 * frame + 1] += static_cast<float>(impact * impact_r + flame * fire_r);
    }
}

} // namespace mm
