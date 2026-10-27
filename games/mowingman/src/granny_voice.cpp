#include "granny_voice.hpp"

#include <algorithm>
#include <cmath>

namespace mm {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double tract = 1.18;        // her vocal tract, against a man's
constexpr double pitch = 1.2;
constexpr double breathiness = 0.2;

// Vowels: F1, F2, F3.
constexpr double AH[3] = {750, 1250, 2600}, EH[3] = {600, 1900, 2700}, EE[3] = {320, 2500, 3100}, OO[3] = {330, 800, 2400}, AW[3] = {640, 1000, 2500},
                 UH[3] = {550, 1300, 2500};

struct Line {
    const double* vowel;
    double f0a, f0b, length, loud, gap;
};

// "OI! My FLOWERS!"
const Line shout[] = {{AW, 260, 380, 0.3, 1.3, 0.1}, {AH, 290, 260, 0.12, 1.0, 0.02}, {EE, 270, 250, 0.1, 0.9, 0.08},
                      {AH, 350, 300, 0.24, 1.25, 0.03}, {OO, 280, 230, 0.14, 1.0, 0.02}, {EH, 260, 190, 0.28, 1.1, 0.0}};
// "Eh-eh-EH! You come BACK here!"
const Line scold[] = {{EH, 250, 240, 0.09, 1.0, 0.04}, {EH, 260, 250, 0.09, 1.05, 0.04}, {EH, 320, 270, 0.22, 1.3, 0.14},
                      {OO, 240, 260, 0.13, 0.9, 0.03}, {UH, 250, 240, 0.11, 0.9, 0.03}, {AH, 330, 390, 0.26, 1.35, 0.04}, {EE, 280, 200, 0.22, 1.0, 0.0}};
const Line shriek[] = {{EE, 380, 560, 0.45, 1.3, 0.0}, {AH, 520, 300, 0.35, 1.1, 0.0}};
// "aah! aah! aah! aah!"
const Line wail[] = {{AH, 400, 260, 0.3, 1.2, 0.18}, {AH, 390, 250, 0.3, 1.15, 0.2}, {AH, 370, 230, 0.34, 1.05, 0.22}, {AH, 350, 210, 0.36, 0.9, 0.0}};

const Line* lines_of(int line, int& count) {
    switch (line) {
    case 0:
        count = static_cast<int>(sizeof shout / sizeof shout[0]);
        return shout;
    case 1:
        count = static_cast<int>(sizeof scold / sizeof scold[0]);
        return scold;
    case 2:
        count = static_cast<int>(sizeof shriek / sizeof shriek[0]);
        return shriek;
    default:
        count = static_cast<int>(sizeof wail / sizeof wail[0]);
        return wail;
    }
}

} // namespace

void GrannyVoice::Res::tune(double hz, double bw) {
    const double r = std::exp(-pi * bw / sample_rate);
    a1 = 2 * r * std::cos(2 * pi * hz / sample_rate);
    a2 = -r * r;
    g = 1 - r;
}

double GrannyVoice::Res::run(double x) {
    const double y = g * x + a1 * y1 + a2 * y2;
    y2 = y1;
    y1 = y;
    return y;
}

GrannyVoice::GrannyVoice(std::uint32_t seed) : state_(seed * 2654435761U + 1U) {
    nasal_.tune(1000 * tract, 120);
}

double GrannyVoice::uniform() {
    state_ += 0x6d2b79f5U;
    std::uint32_t t = (state_ ^ (state_ >> 15)) * (1U | state_);
    t ^= t + (t ^ (t >> 7)) * (61U | t);
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
}

double GrannyVoice::normal() {
    return uniform() + uniform() + uniform() + uniform() - 2;
}

void GrannyVoice::say(GrannyLine line, double pan) {
    pan_.store(static_cast<float>(std::clamp(pan, -1.0, 1.0)), std::memory_order_relaxed);
    asked_[static_cast<int>(line)].fetch_add(1, std::memory_order_relaxed);
}

// One sample of a syllable, `t` of the way through it.
double GrannyVoice::sample(const Syllable& s, double t, double dt) {
    trem_wait_ -= dt;
    if (trem_wait_ <= 0) {
        trem_wait_ = 0.15 + 0.3 * uniform();
        trem_target_ = 4.5 + 2.5 * uniform();
    }
    trem_rate_ += (trem_target_ - trem_rate_) * 4 * dt;
    vib_ += dt * trem_rate_;
    const double creak = t > 0.7 ? (t - 0.7) / 0.3 : 0.0;
    const double f0 = pitch * (s.f0a + (s.f0b - s.f0a) * t) * (1 + 0.07 * std::sin(vib_ * 2 * pi) + 0.03 * normal()) * (1 - 0.15 * creak);
    phase_ += f0 * dt;
    if (phase_ >= 1) {
        phase_ -= 1;
        odd_ = !odd_;
        period_ = 0.7 + 0.3 * uniform();
    }
    const double rough = odd_ ? 0.62 : 1.0;
    const double pulse = std::exp(-phase_ * 34) * rough * period_ * (1 + 0.1 * normal());
    const double source = pulse * 1.3 + normal() * breathiness;
    cf1_ += (s.f1 - cf1_) * 0.0012;
    cf2_ += (s.f2 - cf2_) * 0.0012;
    cf3_ += (s.f3 - cf3_) * 0.0012;
    f1_.tune(cf1_ * tract, 120);
    f2_.tune(cf2_ * tract, 150);
    f3_.tune(cf3_ * tract, 220);
    const double env = std::sin(std::min(1.0, t * 14) * pi / 2) * (t > 0.8 ? (1 - t) / 0.2 : 1.0);
    const double v = (f1_.run(source) * 1.0 + f2_.run(source) * 0.7 + f3_.run(source) * 0.4 + nasal_.run(source) * 0.4) * env * s.loud;
    lp_ += (v - lp_) * 0.85;
    return std::tanh(lp_ * 2.2) * 0.5;
}

void GrannyVoice::render_add(std::span<float> stereo) {
    const double dt = 1.0 / sample_rate;
    const float pan = pan_.load(std::memory_order_relaxed);
    const float left_gain = 1 - std::max(0.0F, pan);
    const float right_gain = 1 + std::min(0.0F, pan);
    for (std::size_t frame = 0; frame + 1 < stereo.size(); frame += 2) {
        if (line_ < 0) {
            // anything asked for since she last spoke: the most urgent first
            for (int k = granny_line_count - 1; k >= 0; --k) {
                if (asked_[k].load(std::memory_order_relaxed) != heard_[k]) {
                    heard_[k] = asked_[k].load(std::memory_order_relaxed);
                    line_ = k;
                    syllable_ = 0;
                    at_ = 0;
                    break;
                }
            }
            if (line_ < 0)
                return;
        }
        int count = 0;
        const Line* lines = lines_of(line_, count);
        const Line& l = lines[syllable_];
        const std::size_t voiced = static_cast<std::size_t>(l.length * sample_rate);
        const std::size_t total = voiced + static_cast<std::size_t>(l.gap * sample_rate);
        if (at_ < voiced) {
            const Syllable s{l.vowel[0], l.vowel[1], l.vowel[2], l.f0a, l.f0b, l.length, l.loud, l.gap};
            const double v = sample(s, static_cast<double>(at_) / static_cast<double>(voiced), dt);
            stereo[frame] += static_cast<float>(v) * left_gain;
            stereo[frame + 1] += static_cast<float>(v) * right_gain;
        }
        if (++at_ >= total) {
            at_ = 0;
            if (++syllable_ >= count) {
                line_ = -1;
                syllable_ = 0;
            }
        }
    }
}

std::size_t GrannyVoice::render_line(GrannyLine line, std::span<float> stereo) {
    std::fill(stereo.begin(), stereo.end(), 0.0F);
    say(line, 0);
    std::size_t frames = 0;
    const std::size_t block = 512;
    while (frames * 2 + block * 2 <= stereo.size()) {
        render_add(stereo.subspan(frames * 2, block * 2));
        frames += block;
        if (line_ < 0 && frames > block)
            break;
    }
    return frames;
}

} // namespace mm
