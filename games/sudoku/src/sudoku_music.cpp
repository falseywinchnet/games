#include "sudoku_music.hpp"

#include <algorithm>
#include <cmath>

namespace ps_sudoku {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr int line_size = 2048;   // the lowest string's period fits many times over
constexpr int room_size = 4096;

double mtof(double midi) {
    return 440.0 * std::pow(2.0, (midi - 69) / 12.0);
}
double rate_for(double seconds) {
    return 1.0 - std::exp(-1.0 / (seconds * GardenMusic::sample_rate));
}

// The thirteen strings, as semitones above the tonic (D), and the scale's pitch classes.
// Hira-joshi for the in scale; the same layout through the yo scale by day.
constexpr std::array<int, 13> night_strings = {0, 5, 7, 8, 12, 13, 17, 19, 20, 24, 25, 29, 31};
constexpr std::array<int, 13> day_strings = {0, 5, 7, 9, 12, 14, 17, 19, 21, 24, 26, 29, 31};
constexpr std::array<int, 5> night_scale = {0, 1, 5, 7, 8};
constexpr std::array<int, 5> day_scale = {0, 2, 5, 7, 9};
constexpr int tonic = 50;  // D3

}  // namespace

GardenMusic::GardenMusic(std::uint32_t seed, bool night)
    : state_(seed * 2654435761U + 0x6D2B79F5U), night_(night) {
    const std::array<int, 13>& layout = night ? night_strings : day_strings;
    scale_ = night ? night_scale : day_scale;
    for (std::size_t i = 0; i < strings_.size(); ++i) {
        tuning_[i] = tonic + layout[i];
        String& s = strings_[i];
        s.line.assign(line_size, 0.0F);
        s.delay = sample_rate / mtof(tuning_[i]);
        s.pan = -.45 + .9 * static_cast<double>(i) / 12.0;
    }
    // A slow pulse: some seventy beats a minute by day, sixty at night.
    beat_ = 60.0 / (night ? 58.0 + uniform() * 6 : 68.0 + uniform() * 8);
    // The body's resonances: the long box's low air and a wood mode above it.
    const double freq[2] = {235, 810};
    const double q[2] = {2.6, 3.4};
    for (int k = 0; k < 2; ++k) {
        const double w = 2 * pi * freq[k] / sample_rate;
        const double r = 1 - w / (2 * q[k]);
        body_a1_[k] = 2 * r * std::cos(w);
        body_a2_[k] = -r * r;
        body_g_[k] = (1 - r) * 2 * std::sin(w);
    }
    const int lengths[4] = {1693, 2111, 2593, 3001};
    for (int k = 0; k < 4; ++k) {
        lines_[static_cast<std::size_t>(k)].assign(room_size, 0.0F);
        lengths_[static_cast<std::size_t>(k)] = lengths[k];
        feedback_[static_cast<std::size_t>(k)] = std::pow(10.0, -3.0 * lengths[k] / (2.4 * sample_rate));
    }
    // The first phrase after a breath of quiet.
    next_phrase_ = static_cast<long>(sample_rate * (1.2 + uniform()));
}

double GardenMusic::uniform() noexcept {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return (state_ >> 8) * (1.0 / 16777216.0);
}

int GardenMusic::pick(int n) noexcept {
    const int value = static_cast<int>(uniform() * n);
    return value < n ? value : n - 1;
}

void GardenMusic::fade_in(double seconds) noexcept {
    fade_ = 0;
    fade_rate_ = 1.0 / (std::max(.05, seconds) * sample_rate);
}

// The pick strikes near the end of the string: a short, bright burst, softer for a
// gentler stroke. A press behind the bridge comes a moment after the stroke.
void GardenMusic::pluck(int index, double strength, double press) noexcept {
    String& s = strings_[static_cast<std::size_t>(index)];
    const int period = std::clamp(static_cast<int>(s.delay), 8, line_size - 2);
    double low = 0;
    const double hardness = .35 + .5 * strength;
    for (int k = 0; k < period; ++k) {
        const double noise = uniform() * 2 - 1;
        low += (noise - low) * hardness;
        // Struck near its end: the pick's position leaves a gentle shape across the period.
        const double shape = std::sin(pi * (k + .5) / period);
        const int at = (s.write - 1 - k + line_size * 4) % line_size;
        s.line[static_cast<std::size_t>(at)] =
            static_cast<float>(s.line[static_cast<std::size_t>(at)] * .3 + low * shape * strength);
    }
    // Lower strings ring longer; silk on a wooden body, not steel.
    const double hz = mtof(tuning_[static_cast<std::size_t>(index)]);
    const double ring = 3.6 - 1.8 * static_cast<double>(index) / 12.0;
    s.loss = static_cast<float>(std::pow(10.0, -3.0 / (ring * hz)));
    s.bright = static_cast<float>(.62 + .25 * strength);
    s.bend = s.bend_to = 1;
    s.bend_rate = 0;
    s.press_in = -1;
    s.release_in = -1;
    if (press > 0) {
        s.press_in = static_cast<int>(sample_rate * (.18 + uniform() * .14));
        s.press_to = std::pow(2.0, press / 12.0);
        // Sometimes the press is let go again before the note dies.
        if (uniform() < .4)
            s.release_in = s.press_in + static_cast<int>(sample_rate * (.45 + uniform() * .3));
    }
    s.quiet = 0;
    ++plucked_;
}

float GardenMusic::run(String& s) noexcept {
    if (s.press_in >= 0 && s.press_in-- == 0) {
        s.bend_to = s.press_to;
        s.bend_rate = rate_for(.09);
    }
    if (s.release_in >= 0 && s.release_in-- == 0) {
        s.bend_to = 1;
        s.bend_rate = rate_for(.14);
    }
    if (s.bend_rate > 0)
        s.bend += (s.bend_to - s.bend) * s.bend_rate;
    const double delay = std::clamp(s.delay / s.bend, 2.0, static_cast<double>(line_size - 2));
    double read = s.write - delay;
    while (read < 0)
        read += line_size;
    const int i0 = static_cast<int>(read);
    const double f = read - i0;
    const float a = s.line[static_cast<std::size_t>(i0 % line_size)];
    const float b = s.line[static_cast<std::size_t>((i0 + 1) % line_size)];
    const float y = static_cast<float>(a + (b - a) * f);
    const float out = s.bright * y + (1 - s.bright) * s.last;
    s.last = out;
    s.line[static_cast<std::size_t>(s.write)] = out * s.loss;
    s.write = (s.write + 1) % line_size;
    s.quiet = std::abs(out) < 1e-4F ? s.quiet + 1 : 0;
    return out;
}

// A phrase: a handful of notes moving mostly to the next string, closing on the tonic or
// the fifth, then room before the next.
void GardenMusic::plan_phrase() noexcept {
    ++phrases_;
    const long spb = static_cast<long>(beat_ * sample_rate);
    long t = clock_ + spb / 8;
    // Now and then the phrase opens with a sweep down the strings.
    if (uniform() < .16) {
        const int top = 8 + pick(5);
        const int count = 5 + pick(3);
        for (int k = 0; k < count && top - k >= 0; ++k)
            events_.push_back({t + static_cast<long>(k * .045 * sample_rate), top - k, .55 - .05 * k, 0});
        t += spb + static_cast<long>(count * .045 * sample_rate);
        last_string_ = std::max(1, top - count + 1);
    }
    int current = std::clamp(last_string_ + pick(3) - 1, 2, 10);
    const int notes = 4 + pick(5);
    for (int n = 0; n < notes; ++n) {
        const bool last = n == notes - 1;
        if (last) {
            // Close on the tonic or the fifth nearest where the line is.
            int best = current;
            int best_distance = 99;
            for (int i = 0; i < 13; ++i) {
                const int pc = (tuning_[static_cast<std::size_t>(i)] - tonic) % 12;
                if ((pc == 0 || pc == 7) && std::abs(i - current) < best_distance) {
                    best = i;
                    best_distance = std::abs(i - current);
                }
            }
            current = best;
        } else if (n > 0) {
            const double roll = uniform();
            const int step = roll < .7 ? 1 : roll < .9 ? 2 : 0;
            int direction = uniform() < .5 ? -1 : 1;
            // Drift back toward the middle of the instrument.
            if (current > 9)
                direction = -1;
            if (current < 3)
                direction = 1;
            current = std::clamp(current + direction * step, 0, 12);
        }
        double press = 0;
        if (!last && current < 12 && uniform() < .2) {
            const int up = tuning_[static_cast<std::size_t>(current + 1)] - tuning_[static_cast<std::size_t>(current)];
            if (up <= 2)
                press = up;
        }
        const double strength = .45 + uniform() * .35 + (n == 0 ? .1 : 0);
        events_.push_back({t, current, strength, press});
        // Two strings together at the start of a phrase: the octave above.
        if (n == 0 && uniform() < .25) {
            for (int j = current + 1; j < 13; ++j)
                if (tuning_[static_cast<std::size_t>(j)] == tuning_[static_cast<std::size_t>(current)] + 12) {
                    events_.push_back({t + static_cast<long>(.012 * sample_rate), j, strength * .7, 0});
                    break;
                }
        }
        const double lengths[6] = {1, 1, .5, .5, 1.5, 2};
        double beats = last ? 2 + uniform() * 2 : lengths[pick(6)];
        // The note after a press waits for the press to sound.
        if (press > 0)
            beats = std::max(beats, 1.0);
        t += static_cast<long>(beats * spb);
    }
    last_string_ = current;
    // Ma: the quiet between phrases, longer at night.
    const double rest = (night_ ? 2.0 : 1.2) + uniform() * (night_ ? 3.0 : 2.0);
    next_phrase_ = t + static_cast<long>(rest * spb);
    std::sort(events_.begin(), events_.end(), [](const Event& a, const Event& b) { return a.at < b.at; });
    // Now and then the flute breathes a long tone over it.
    if (!flute_.on && uniform() < (night_ ? .45 : .25))
        breathe();
}

void GardenMusic::breathe() noexcept {
    // A tone of the scale in the flute's middle register.
    int midi = 0;
    for (int tries = 0; tries < 16; ++tries) {
        const int pc = scale_[static_cast<std::size_t>(pick(5))];
        const int octave = 1 + pick(2);
        midi = tonic + 12 * octave + pc;
        if (midi >= 62 && midi <= 81)
            break;
    }
    flute_.on = true;
    flute_.age = 0;
    flute_.target_hz = mtof(midi);
    // Meri: it comes up into the note from a little below.
    flute_.hz = flute_.target_hz * std::pow(2.0, -1.0 / 12);
    flute_.level = .1 + uniform() * .05;
    flute_.length = 2.6 + uniform() * 2.2;
    ++breaths_;
}

double GardenMusic::flute_sample() noexcept {
    Flute& f = flute_;
    if (!f.on)
        return 0;
    const double dt = 1.0 / sample_rate;
    f.age += dt;
    // Up into the note, then held; at the end a little fall as the breath runs out.
    double target = f.target_hz;
    if (f.age > f.length)
        target *= std::pow(2.0, -.3 / 12);
    f.hz += (target - f.hz) * rate_for(f.age < .4 ? .12 : .3);
    const double want = f.age < f.length ? f.level : 0;
    f.env += (want - f.env) * rate_for(f.age < f.length ? .22 : .6);
    if (f.age > f.length + 2.5) {
        f.on = false;
        f.env = 0;
        return 0;
    }
    // A slow vibrato once the tone has settled.
    const double depth = .006 * std::clamp((f.age - 1.1) / 1.2, 0.0, 1.0);
    f.vibrato += 2 * pi * 5.1 * dt;
    const double hz = f.hz * (1 + depth * std::sin(f.vibrato));
    f.phase += hz * dt;
    f.phase -= std::floor(f.phase);
    const double w = 2 * pi * f.phase;
    const double tone = std::sin(w) + .28 * std::sin(2 * w) + .1 * std::sin(3 * w);
    // Breath: noise in a band around the second harmonic, strongest at the start.
    const double noise = uniform() * 2 - 1;
    const double g = std::tan(pi * std::min(hz * 2, 9000.0) / sample_rate);
    const double k = 1 / 1.6;
    const double high = (noise - (k + g) * f.band_mid - f.band_low) / (1 + g * (g + k));
    const double band = g * high + f.band_mid;
    f.band_low += g * band;
    f.band_mid = g * high + band;
    const double chiff = std::exp(-f.age / .09) * .9;
    return f.env * (tone * .8 + band * (.35 + chiff * 2.2));
}

void GardenMusic::room(double in, double& left, double& right) noexcept {
    double out[4];
    for (int i = 0; i < 4; ++i) {
        int read = room_write_ - lengths_[static_cast<std::size_t>(i)];
        if (read < 0)
            read += room_size;
        out[i] = lines_[static_cast<std::size_t>(i)][static_cast<std::size_t>(read)];
    }
    const double h0 = out[0] + out[1], h1 = out[0] - out[1];
    const double h2 = out[2] + out[3], h3 = out[2] - out[3];
    const double mixed[4] = {(h0 + h2) * .5, (h1 + h3) * .5, (h0 - h2) * .5, (h1 - h3) * .5};
    const double damp = 1.0 - std::exp(-2.0 * pi * 4200 / sample_rate);
    for (int i = 0; i < 4; ++i) {
        low_[static_cast<std::size_t>(i)] += (mixed[i] * feedback_[static_cast<std::size_t>(i)] - low_[static_cast<std::size_t>(i)]) * damp;
        lines_[static_cast<std::size_t>(i)][static_cast<std::size_t>(room_write_)] =
            static_cast<float>(low_[static_cast<std::size_t>(i)] + in);
    }
    room_write_ = (room_write_ + 1) % room_size;
    left += out[0] + out[2] * .6;
    right += out[1] + out[3] * .6;
}

void GardenMusic::render_add(std::span<float> stereo, double gain) noexcept {
    std::size_t next_event = 0;
    for (std::size_t frame = 0; frame + 1 < stereo.size(); frame += 2) {
        gain_ += (gain - gain_) * .0002;
        if (fade_ < 1)
            fade_ = std::min(1.0, fade_ + fade_rate_);
        if (events_.empty() && clock_ >= next_phrase_)
            plan_phrase();
        while (next_event < events_.size() && events_[next_event].at <= clock_) {
            const Event& e = events_[next_event];
            pluck(e.string, e.strength, e.press);
            ++next_event;
        }
        if (next_event > 0 && next_event == events_.size()) {
            events_.clear();
            next_event = 0;
        }
        double left = 0, right = 0, mono = 0;
        for (String& s : strings_) {
            if (s.quiet > sample_rate / 4)
                continue;
            const double v = run(s);
            left += v * (1 - s.pan) * .5;
            right += v * (1 + s.pan) * .5;
            mono += v;
        }
        // The body sounds the strings' low air and wood.
        double body = 0;
        for (int k = 0; k < 2; ++k) {
            const double y = body_g_[k] * mono + body_a1_[k] * body_y1_[k] + body_a2_[k] * body_y2_[k];
            body_y2_[k] = body_y1_[k];
            body_y1_[k] = y;
            body += y;
        }
        const double koto = .5;
        left = (left + body * .18) * koto;
        right = (right + body * .18) * koto;
        const double flute = flute_sample();
        left += flute * .85;
        right += flute * .95;
        double wet_l = 0, wet_r = 0;
        room((left + right) * .5 * .32, wet_l, wet_r);
        const double level = gain_ * fade_;
        stereo[frame] += static_cast<float>((left + wet_l * .5) * level);
        stereo[frame + 1] += static_cast<float>((right + wet_r * .5) * level);
        ++clock_;
    }
    if (next_event > 0) {
        events_.erase(events_.begin(), events_.begin() + static_cast<std::ptrdiff_t>(next_event));
    }
}

}  // namespace ps_sudoku
