#include "ambience_voice.hpp"

#include <algorithm>
#include <cmath>

namespace mm {
namespace {

constexpr double pi = 3.14159265358979323846;

// A note of birdsong: a pitch sweep with its length and the gap after it, all in Hz and seconds.
struct Note {
    double from;
    double to;
    double length;
    double gap;
    double loud;
};

// Each kind's phrase. Robin: a tumbling, varied warble. Blue tit: "tsee tsee tsee tu-tu-tu".
// Blackbird: slow, fluty, falling. Sparrow: plain chirps.
const Note robin[] = {{3200, 2400, 0.09, 0.05, 0.8}, {2600, 3600, 0.08, 0.04, 0.7}, {4100, 2900, 0.12, 0.06, 0.9}, {2500, 2300, 0.07, 0.04, 0.6},
                      {3300, 4300, 0.10, 0.05, 0.8}, {3900, 2700, 0.14, 0.30, 0.9}};
const Note bluetit[] = {{6400, 6200, 0.07, 0.07, 0.8}, {6400, 6200, 0.07, 0.07, 0.8}, {6400, 6200, 0.07, 0.10, 0.8}, {3600, 3300, 0.05, 0.05, 0.9},
                        {3600, 3300, 0.05, 0.05, 0.9}, {3600, 3300, 0.05, 0.05, 0.9}, {3600, 3300, 0.05, 0.40, 0.9}};
const Note blackbird[] = {{2300, 2000, 0.22, 0.10, 0.9}, {2600, 2900, 0.18, 0.08, 0.8}, {2100, 1700, 0.28, 0.12, 0.9}, {2800, 2400, 0.16, 0.10, 0.7},
                          {1900, 1600, 0.32, 0.50, 0.9}};
const Note sparrow[] = {{3800, 3500, 0.06, 0.12, 0.9}, {3900, 3600, 0.06, 0.12, 0.9}, {3700, 3400, 0.06, 0.35, 0.9}};

const Note* phrase_of(int kind, int& count) {
    switch (kind) {
    case 0:
        count = static_cast<int>(sizeof robin / sizeof robin[0]);
        return robin;
    case 1:
        count = static_cast<int>(sizeof bluetit / sizeof bluetit[0]);
        return bluetit;
    case 2:
        count = static_cast<int>(sizeof blackbird / sizeof blackbird[0]);
        return blackbird;
    default:
        count = static_cast<int>(sizeof sparrow / sizeof sparrow[0]);
        return sparrow;
    }
}

} // namespace

void AmbienceVoice::Band::tune(double hz, double q) {
    const double w = 2 * pi * hz / sample_rate;
    const double r = std::exp(-w / (2 * q));
    a1 = 2 * r * std::cos(w);
    a2 = -r * r;
    g = 1 - r;
}

double AmbienceVoice::Band::run(double x) {
    const double y = g * x + a1 * y1 + a2 * y2;
    y2 = y1;
    y1 = y;
    return y;
}

AmbienceVoice::AmbienceVoice(std::uint32_t seed) : state_(seed * 2654435761U + 1U) {
    cricket_wait_ = 0.5;
    frog_wait_ = 6;
    for (Phrase& bird : birds_)
        bird.wait = 0.5 + 2 * uniform();
    flick_.tune(5200, 2.0);
    gurgle_a_.tune(420, 1.2);
    gurgle_b_.tune(760, 1.5);
    rain_.tune(2600, 0.6);
}

double AmbienceVoice::normal() {
    return uniform() + uniform() + uniform() + uniform() - 2;
}

double AmbienceVoice::uniform() {
    state_ += 0x6d2b79f5U;
    std::uint32_t t = (state_ ^ (state_ >> 15)) * (1U | state_);
    t ^= t + (t ^ (t >> 7)) * (61U | t);
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
}

void AmbienceVoice::set(const AmbienceControls& controls) {
    want_ = controls;
}

void AmbienceVoice::sing(int kind, Phrase& phrase, double dt, double& left, double& right) {
    int count = 0;
    const Note* notes = phrase_of(kind, count);
    if (phrase.note < 0) {
        phrase.wait -= dt;
        if (phrase.wait > 0 || !want_.singing[kind])
            return;
        phrase.note = 0;
        phrase.note_time = 0;
    }
    const Note& note = notes[phrase.note];
    phrase.note_time += dt;
    if (phrase.note_time < note.length) {
        const double t = phrase.note_time / note.length;
        const double hz = note.from + (note.to - note.from) * t;
        phrase.phase += hz * dt;
        if (phrase.phase >= 1)
            phrase.phase -= 1;
        const double env = std::sin(std::min(1.0, t * 4) * pi / 2) * (1 - 0.35 * t);
        const double tone = std::sin(2 * pi * phrase.phase) + 0.18 * std::sin(4 * pi * phrase.phase);
        const double s = tone * env * note.loud * (kind == 2 ? 0.07 : 0.045);
        left += s * (1 - std::max(0.0, want_.bird_pan[kind]));
        right += s * (1 + std::min(0.0, want_.bird_pan[kind]));
    } else if (phrase.note_time >= note.length + note.gap) {
        phrase.note += 1;
        phrase.note_time = 0;
        if (phrase.note >= count) {
            phrase.note = -1;
            phrase.wait = 1.2 + 3.0 * uniform();
        }
    }
}

// A bee in flight, as a garden hears it a few metres off. Recordings of real bees show a
// soft, round hum: a strong wingbeat (about 150 Hz in a bumblebee, 230 in a honeybee),
// a second harmonic nearly as strong, and above that the harmonics fall away steeply
// (there is little above 1.5 kHz at any distance). What makes it a bee and not a tone is
// that nothing holds still: every stroke differs a little in length and strength, the
// pitch slides as it turns and climbs, and the loudness surges and sags several times a
// second as the wings swing towards and away from you. A breath of air noise rides the
// strokes. When it lands to feed the wings stop, but now and then it buzzes briefly.
void AmbienceVoice::buzz(BeeVoice& v, const BeeSound& bee, double dt, double& left, double& right) {
    double want_wings = bee.flying ? 1.0 : 0.0;
    if (!bee.flying && bee.id != 0) {
        if (v.burst > 0) {
            v.burst -= dt;
            want_wings = 0.4;
        } else {
            v.burst_wait -= dt;
            if (v.burst_wait <= 0) {
                v.burst = 0.1 + 0.3 * uniform();
                v.burst_wait = 1.0 + 3.0 * uniform();
            }
        }
    }
    v.wings += (want_wings - v.wings) * (want_wings > v.wings ? 0.0008 : 0.0005);
    v.gain += (bee.gain - v.gain) * 0.0004;
    v.pan += (bee.pan - v.pan) * 0.0004;
    v.pitch += (bee.pitch - v.pitch) * 0.0006;
    if (v.wings * v.gain < 1e-6) {
        v.phase = 0;
        return;
    }
    // Slow wander of the wingbeat (turns, climbs) and a quicker surge of loudness.
    v.wander += ((uniform() * 2 - 1) - v.wander) * 0.00012;
    v.harmonic[0] += ((uniform() * 2 - 1) - v.harmonic[0]) * 0.0012;  // loudness surge, a few per second
    v.harmonic[1] += (v.harmonic[0] - v.harmonic[1]) * 0.0012;
    const double hz = v.wingbeat * v.pitch * (1 + 0.07 * v.wander) * (0.8 + 0.2 * v.wings) * (want_wings > 0 && want_wings < 1 ? 1.1 : 1.0) *
                      v.harmonic[3];
    v.phase += hz * dt;
    if (v.phase >= 1) {
        v.phase -= 1;
        // Each stroke its own: a little longer or shorter, a little harder or softer.
        v.cycle_gain = 0.85 + 0.3 * uniform();
        v.harmonic[3] = 1 + 0.025 * (uniform() * 2 - 1);
    }
    const double theta = 2 * pi * v.phase;
    // A few harmonics, falling steeply: 1, 0.8, 0.35, 0.18, 0.09, 0.05.
    const double amps[6] = {1.0, 0.8, 0.35, 0.18, 0.09, 0.05};
    const double c = std::cos(theta);
    double s_prev = 0;
    double s_k = std::sin(theta);
    double sum = 0;
    for (int k = 1; k <= 6; ++k) {
        sum += s_k * amps[k - 1];
        const double next = 2 * c * s_k - s_prev;
        s_prev = s_k;
        s_k = next;
    }
    // Air: soft noise around 600 Hz, pushed out by each stroke.
    const double noise = uniform() * 2 - 1;
    const double w = 2 * pi * 600 / sample_rate;
    const double r = 0.96;
    const double y = noise * (1 - r) * 3 + 2 * r * std::cos(w) * v.flutter_y1 - r * r * v.flutter_y2;
    v.flutter_y2 = v.flutter_y1;
    v.flutter_y1 = y;
    const double stroke = 0.5 + 0.5 * std::cos(theta);
    const double surge = std::clamp(0.7 + 0.9 * v.harmonic[1], 0.25, 1.4);
    double out = (sum * v.cycle_gain * 0.5 + y * stroke * 0.6) * surge * v.wings * v.gain;
    // Distance and grass soften the top: a gentle low-pass near 1.4 kHz, and no rumble.
    v.harmonic[2] += (out - v.harmonic[2]) * (2 * pi * 1400 / sample_rate);
    out = v.harmonic[2];
    v.dc += (out - v.dc) * (2 * pi * 90 / sample_rate);
    out -= v.dc;
    const double s = out * 0.024 * (0.55 + 0.45 * quiet_);  // faint: heard when it is near, never over the garden
    left += s * (1 - std::max(0.0, v.pan));
    right += s * (1 + std::min(0.0, v.pan));
}

void AmbienceVoice::render_add(std::span<float> stereo) {
    const double dt = 1.0 / sample_rate;
    for (std::size_t frame = 0; frame + 1 < stereo.size(); frame += 2) {
        quiet_ += ((want_.engine_on ? 0.0 : 1.0) - quiet_) * 0.00003;
        double left = 0;
        double right = 0;
        // Bees: one voice each, found by the bee's id.
        for (int k = 0; k < AmbienceControls::max_bees; ++k) {
            const BeeSound& bee = want_.bees[k];
            if (bee.id == 0)
                continue;
            BeeVoice* voice = nullptr;
            for (BeeVoice& v : bee_voices_)
                if (v.id == bee.id)
                    voice = &v;
            if (voice == nullptr)
                for (BeeVoice& v : bee_voices_)
                    if (v.id == 0 || v.gain < 1e-4) {
                        v = BeeVoice{};
                        v.id = bee.id;
                        // One in three is a bumblebee (wings near 150 Hz), the rest honeybees (near 235 Hz).
                        const std::uint32_t mix = bee.id * 2654435761U;
                        v.wingbeat = (mix % 3U) == 0U ? 140 + 30 * uniform() : 220 + 30 * uniform();
                        v.burst_wait = 0.5 + uniform();
                        v.harmonic[3] = 1;
                        voice = &v;
                        break;
                    }
            if (voice != nullptr)
                buzz(*voice, bee, dt, left, right);
        }
        // Voices whose bee has gone fade out on their own.
        for (BeeVoice& v : bee_voices_) {
            bool present = false;
            for (const BeeSound& bee : want_.bees)
                present = present || (bee.id != 0 && bee.id == v.id);
            if (v.id != 0 && !present) {
                BeeSound gone{};
                gone.id = v.id;
                gone.pan = v.pan;
                buzz(v, gone, dt, left, right);
                if (v.gain < 1e-5)
                    v.id = 0;
            }
        }
        // Birds sing whether the engine runs or not; they are simply harder to hear over it.
        double song_left = 0;
        double song_right = 0;
        for (int kind = 0; kind < 4; ++kind)
            sing(kind, birds_[kind], dt, song_left, song_right);
        left += song_left * (0.45 + 0.55 * quiet_);
        right += song_right * (0.45 + 0.55 * quiet_);
        if (quiet_ > 0.005) {
            // Wind in the trees. A gust is a slow random swell (quick to rise, slow to die); what it moves is
            // thousands of leaves: a hiss that brightens the harder it blows, the flicks of single leaves
            // turning, and a soft low push of air in the gusts. Nothing in it repeats.
            if (want_.trees > 0) {
                const double w = normal();
                gust_wait_ -= dt;
                if (gust_wait_ <= 0) {
                    gust_wait_ = 1.5 + 5.0 * uniform();
                    gust_target_ = uniform();
                }
                gust_ += (gust_target_ - gust_) * 0.8 * dt;
                const double want = 0.1 + 0.4 * gust_ * want_.trees;  // gusts stay a breeze, never a roar
                gust_level_ += (want - gust_level_) * (want > gust_level_ ? 0.6 : 0.25) * dt;
                const double g = gust_level_;
                hiss_hi_ += (w - hiss_hi_) * (0.35 + 0.4 * g);
                hiss_lo_ += (w - hiss_lo_) * 0.06;
                const double hiss = (w - hiss_hi_) * (0.5 + 0.5 * g) + (hiss_hi_ - hiss_lo_) * 0.5;
                const double fl = uniform() < (20 + 220 * g * g) * dt ? (0.5 + uniform()) * 2.5 : 0.0;
                const double flicks = flick_.run(fl * w) * 1.5;
                push_ += (w - push_) * 0.01;
                const double low = push_ * g * g * 3.0;
                const double level = 0.03 + g * 0.3;
                const double mix = (hiss * 0.5 + flicks * 0.4) * level + low * 0.12;
                wind_left_ += (mix - wind_left_) * 0.9;
                wind_right_ += (mix - wind_right_) * 0.75;
                left += std::tanh(wind_left_ * 1.2) * 0.4;
                right += std::tanh(wind_right_ * 1.25) * 0.4;
            }
            // The fountain: a tiny sprinkler in a basin. Hundreds of very small drops a second, each a bright
            // burst of its own pitch and length, a little rain on the pool, a faint gurgle at the lip, and a
            // breath of fine spray. Very faint.
            if (want_.fountain) {
                const double w = normal();
                if (uniform() < 900.0 * dt) {
                    Drop& d = drops_[drop_slot_];
                    drop_slot_ = (drop_slot_ + 1) % 24;
                    const double size = uniform() * 0.35;
                    d.env = 0.3 + 0.9 * size * size;
                    d.decay = 0.9975 + 0.0022 * size;
                    d.band.tune(1600 + 5000 * (1 - size) * uniform() + 600 * uniform(), 5.0 + 6.0 * uniform());
                }
                double patter = 0;
                for (Drop& d : drops_) {
                    if (d.env < 1e-4)
                        continue;
                    d.env *= d.decay;
                    patter += d.band.run(w * d.env) * 3.0;
                }
                gurgle_wait_ -= dt;
                if (gurgle_wait_ <= 0) {
                    gurgle_wait_ = 0.3 + 1.5 * uniform();
                    gurgle_target_ = uniform();
                }
                gurgle_walk_ += (gurgle_target_ - gurgle_walk_) * 1.5 * dt;
                const double gurgle = (gurgle_a_.run(w) * 0.8 + gurgle_b_.run(w) * 0.5) * (0.4 + 0.6 * gurgle_walk_) * 0.08;
                const double rain = rain_.run(w) * (uniform() < 0.04 ? 1.0 : 0.0) * 1.4;  // scattered larger drops on the water
                hiss_ += (w - hiss_) * 0.3;
                const double spray = (w - hiss_) * 0.03;
                const double mix = (patter * 0.5 + gurgle * 0.7 + rain * 0.5 + spray) * 0.022;
                left += mix * 0.92;
                right += mix;
            }
            // Crickets: a high trill in short chirps.
            if (want_.dusk) {
                cricket_wait_ -= dt;
                if (cricket_wait_ <= 0 && cricket_left_ <= 0) {
                    cricket_wait_ = 0.6 + 1.6 * uniform();
                    cricket_left_ = 0.35 + 0.3 * uniform();
                }
                if (cricket_left_ > 0) {
                    cricket_left_ -= dt;
                    cricket_phase_ += 4300 * dt;
                    if (cricket_phase_ >= 1)
                        cricket_phase_ -= 1;
                    const double pulse = std::max(0.0, std::sin(2 * pi * 32 * cricket_left_));
                    const double s = std::sin(2 * pi * cricket_phase_) * pulse * pulse * 0.012;
                    left += s * 0.6;
                    right += s;
                }
            }
            // A frog, now and then, if there is water about.
            if (want_.water) {
                frog_wait_ -= dt;
                if (frog_wait_ <= 0 && frog_left_ <= 0) {
                    frog_wait_ = 5 + 12 * uniform();
                    frog_left_ = 0.28;
                }
                if (frog_left_ > 0) {
                    frog_left_ -= dt;
                    const double t = 0.28 - frog_left_;
                    frog_phase_ += (140 + 60 * std::sin(t * 30)) * dt;
                    if (frog_phase_ >= 1)
                        frog_phase_ -= 1;
                    const double env = std::sin(std::min(1.0, t / 0.28) * pi);
                    const double pulse = std::max(0.0, std::sin(2 * pi * 22 * t));
                    const double s = (frog_phase_ * 2 - 1) * env * (0.4 + 0.6 * pulse) * 0.05;
                    left += s;
                    right += s * 0.7;
                }
            }
        }
        stereo[frame] += static_cast<float>(left);
        stereo[frame + 1] += static_cast<float>(right);
    }
}

} // namespace mm
