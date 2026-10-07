#include "glass_effects.hpp"

#include <algorithm>
#include <cmath>

namespace ps_cube {
namespace {

// Before the music has said anything: C Lydian, on C with D, E and G.
constexpr int default_scale = (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 11);
constexpr int default_chord = (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7);

int floor_div(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

} // namespace

GlassEffects::GlassEffects(std::uint32_t seed, const Harmony* harmony)
    : dice_(seed), harmony_(harmony) {
    air_band_.set(900, 1.2);
}

GlassEffects::Tone& GlassEffects::voice() {
    // The next free voice, or else the quietest.
    const int size = static_cast<int>(tones_.size());
    int chosen = -1;
    for (int k = 0; k < size && chosen < 0; ++k)
        if (!tones_[(next_ + k) % size].on)
            chosen = (next_ + k) % size;
    if (chosen < 0) {
        chosen = 0;
        for (int i = 1; i < size; ++i)
            if (tones_[i].amp[0] < tones_[chosen].amp[0])
                chosen = i;
    }
    next_ = (chosen + 1) % size;
    Tone& tone = tones_[chosen];
    tone = Tone{};
    tone.on = true;
    return tone;
}

int GlassEffects::scale_note(int index) const {
    const int tonic = harmony_ ? (*harmony_).tonic.load(std::memory_order_relaxed) : 0;
    int scale = harmony_ ? (*harmony_).scale.load(std::memory_order_relaxed) : 0;
    if (scale == 0)
        scale = default_scale;
    // The mode's pentatonic: its first, second, third, fifth and sixth.
    int steps[7]{};
    int count = 0;
    for (int s = 0; s < 12 && count < 7; ++s)
        if (scale & (1 << ((tonic + s) % 12)))
            steps[count++] = s;
    const int degrees[5] = {0, 1, 2, 4, 5};
    const int octave = floor_div(index, 5);
    const int i = index - 5 * octave;
    return 60 + tonic + steps[std::min(degrees[i], count - 1)] + 12 * octave;
}

int GlassEffects::pentatonic_index_near(int midi) const {
    int best = 0, distance = 1000;
    for (int i = -10; i < 30; ++i) {
        const int d = std::abs(scale_note(i) - midi);
        if (d < distance) {
            distance = d;
            best = i;
        }
    }
    return best;
}

int GlassEffects::chord_tone_near(int midi, int avoid) const {
    int chord = harmony_ ? (*harmony_).chord.load(std::memory_order_relaxed) : 0;
    if (chord == 0)
        chord = default_chord;
    for (int d = 0; d <= 7; ++d)
        for (int sign = -1; sign <= 1; sign += 2) {
            const int m = midi + d * sign;
            if ((chord & (1 << (m % 12))) && m != avoid)
                return m;
        }
    return midi;
}

void GlassEffects::glass(int midi, double level, int delay, double length, double pan) {
    Tone& tone = voice();
    const double hz = hz_of(midi);
    const double ratios[3] = {1.0, 2.76, 5.40};
    const double amps[3] = {1.0, .16, .05};
    const double decays[3] = {length, length * .3, length * .12};
    tone.partials = 3;
    for (int p = 0; p < 3; ++p) {
        tone.inc[p] = std::min(hz * ratios[p], sample_rate * .45) / sample_rate;
        tone.amp[p] = amps[p] * level;
        tone.decay[p] = decay_for(decays[p]);
    }
    tone.attack = 0;
    tone.attack_rate = rate_for(.002);
    tone.noise = .18 * level;
    tone.noise_decay = decay_for(.004);
    tone.noise_band.set(std::min(hz * 4.5, 9000.0), 1.4);
    tone.hold = static_cast<int>(length * 2.2 * sample_rate);
    tone.release = decay_for(.12);
    tone.pan = pan;
    tone.send = .35;
    tone.delay = delay;
}

void GlassEffects::droplet(int midi, double level, bool falling) {
    Tone& tone = voice();
    const double hz = hz_of(midi + dice_.range(-.08, .08));
    tone.partials = 2;
    tone.inc[0] = hz / sample_rate;
    tone.inc[1] = hz * 2 / sample_rate;
    const double v = level * dice_.range(.85, 1.1);
    tone.amp[0] = v;
    tone.amp[1] = falling ? 0 : v * .08;
    tone.decay[0] = decay_for(falling ? .07 : .055);
    tone.decay[1] = decay_for(.035);
    // A drop's pitch springs up as it lands; an erased one sinks away.
    tone.glide = falling ? 1.0 : .86;
    tone.glide_to = falling ? .84 : 1.0;
    tone.glide_rate = rate_for(falling ? .05 : .012);
    tone.attack = 0;
    tone.attack_rate = rate_for(.003);
    tone.pan = dice_.range(-.25, .25);
    tone.send = .25;
}

void GlassEffects::bloom(double level) {
    int chord = harmony_ ? (*harmony_).chord.load(std::memory_order_relaxed) : 0;
    if (chord == 0)
        chord = default_chord;
    int pcs[6]{};
    int count = 0;
    for (int pc = 0; pc < 12 && count < 6; ++pc)
        if (chord & (1 << pc))
            pcs[count++] = pc;
    // A different inversion each time, rising from somewhere near middle C.
    const int first = joins_++ % count;
    int notes[4]{};
    int n = 0;
    int previous = 59 + dice_.pick(4);
    for (int k = 0; k < count && n < 4; ++k) {
        const int pc = pcs[(first + k) % count];
        int m = previous + 1;
        while (m % 12 != pc)
            ++m;
        notes[n++] = m;
        previous = m + 2;
    }
    for (int k = 0; k < n; ++k) {
        Tone& tone = voice();
        const double hz = hz_of(notes[k]);
        const double amps[3] = {1.0, .22, .07};
        const double decays[3] = {.45, .2, .1};
        tone.partials = 3;
        for (int p = 0; p < 3; ++p) {
            tone.inc[p] = hz * (p + 1) / sample_rate;
            tone.amp[p] = amps[p] * level;
            tone.decay[p] = decay_for(decays[p]);
        }
        tone.attack = 0;
        tone.attack_rate = rate_for(.015);
        tone.hold = static_cast<int>(.32 * sample_rate);
        tone.release = decay_for(.1);
        tone.pan = (k - (n - 1) * .5) * .3;
        tone.send = .35;
        tone.delay = static_cast<int>(k * .022 * sample_rate);
    }
    // and the root, low and slow
    Tone& low = voice();
    int root = 48 + pcs[0];
    for (int k = 0; k < count; ++k)
        if (pcs[k] == ((harmony_ ? (*harmony_).tonic.load(std::memory_order_relaxed) : 0) + 0) % 12)
            root = 48 + pcs[k];
    low.partials = 2;
    low.inc[0] = hz_of(root) / sample_rate;
    low.inc[1] = hz_of(root) * 2 / sample_rate;
    low.amp[0] = level * .5;
    low.amp[1] = level * .12;
    low.decay[0] = decay_for(.5);
    low.decay[1] = decay_for(.25);
    low.attack = 0;
    low.attack_rate = rate_for(.03);
    low.hold = static_cast<int>(.35 * sample_rate);
    low.release = decay_for(.1);
    low.send = .3;
}

void GlassEffects::bump() {
    Tone& tone = voice();
    const double hz = hz_of(52 + dice_.range(-1.0, 1.0));
    tone.partials = 2;
    tone.inc[0] = hz / sample_rate;
    tone.inc[1] = hz * 2.7 / sample_rate;
    tone.amp[0] = .15;
    tone.amp[1] = .05;
    tone.decay[0] = decay_for(.07);
    tone.decay[1] = decay_for(.025);
    tone.glide = 1.07;
    tone.glide_to = .9;
    tone.glide_rate = rate_for(.04);
    tone.attack = 0;
    tone.attack_rate = rate_for(.002);
    tone.noise = .06;
    tone.noise_decay = decay_for(.008);
    tone.noise_band.set(950, 1.0);
    tone.send = .1;
}

void GlassEffects::chime() {
    const int tonic = harmony_ ? (*harmony_).tonic.load(std::memory_order_relaxed) : 0;
    int root = 60 + tonic;
    if (root > 66)
        root -= 12;
    const int rise[3] = {0, 7, 14};
    for (int k = 0; k < 3; ++k)
        glass(root + 12 + rise[k], .12 - .02 * k, static_cast<int>(k * .08 * sample_rate), .45,
              -.25 + .25 * k);
    const int low[2] = {root - 12, root - 5};
    for (int k = 0; k < 2; ++k) {
        Tone& tone = voice();
        const double hz = hz_of(low[k]);
        tone.partials = 3;
        const double amps[3] = {1.0, .3, .1};
        for (int p = 0; p < 3; ++p) {
            tone.inc[p] = hz * (p + 1) / sample_rate;
            tone.amp[p] = amps[p] * .06;
            tone.decay[p] = decay_for(.6 / (p + 1));
        }
        tone.attack = 0;
        tone.attack_rate = rate_for(.06);
        tone.hold = static_cast<int>(.5 * sample_rate);
        tone.release = decay_for(.15);
        tone.pan = k ? .2 : -.2;
        tone.send = .4;
    }
}

void GlassEffects::fanfare() {
    const int tonic = harmony_ ? (*harmony_).tonic.load(std::memory_order_relaxed) : 0;
    int scale = harmony_ ? (*harmony_).scale.load(std::memory_order_relaxed) : 0;
    if (scale == 0)
        scale = default_scale;
    int root = 48 + tonic;
    if (root > 54)
        root -= 12;
    // An open fifth swells underneath...
    const int under[3] = {root, root + 7, root + 12};
    for (int k = 0; k < 3; ++k) {
        Tone& tone = voice();
        const double hz = hz_of(under[k]);
        const double amps[4] = {1.0, .35, .15, .06};
        const double decays[4] = {2.4, 1.4, .8, .5};
        tone.partials = 4;
        for (int p = 0; p < 4; ++p) {
            tone.inc[p] = hz * (p + 1) / sample_rate;
            tone.amp[p] = amps[p] * .07;
            tone.decay[p] = decay_for(decays[p] * .6);
        }
        tone.attack = 0;
        tone.attack_rate = rate_for(.2);
        tone.hold = static_cast<int>(1.3 * sample_rate);
        tone.release = decay_for(.3);
        tone.pan = (k - 1) * .35;
        tone.send = .45;
    }
    // ...while the key's major seventh (or added ninth) sparkles up two octaves and back.
    const bool seventh = (scale & (1 << ((tonic + 11) % 12))) != 0;
    const int shape[4] = {0, 4, 7, seventh ? 11 : 14};
    int top = root + 24;
    int at = static_cast<int>(.12 * sample_rate);
    for (int k = 0; k < 5; ++k) {
        top = root + 24 + shape[k % 4] + 12 * (k / 4);
        glass(top, .09 + .005 * k, at, .5, -.4 + k * .2);
        at += static_cast<int>(.09 * sample_rate);
    }
}

void GlassEffects::cue(Cue cue) noexcept {
    switch (cue) {
    case Cue::pick: {
        const int m = chord_tone_near(74 + dice_.pick(6), last_pick_);
        glass(m, .2, 0, .22, dice_.range(-.2, .2));
        last_pick_ = m;
        steps_ = 0;
        step_base_ = pentatonic_index_near(m);
        break;
    }
    case Cue::step: {
        ++steps_;
        int index = step_base_ + steps_;
        while (scale_note(index) > 91)
            index -= 10;
        droplet(scale_note(index), .14, false);
        break;
    }
    case Cue::erase: {
        int index = step_base_ + std::max(steps_, 0);
        while (scale_note(index) > 91)
            index -= 10;
        steps_ = std::max(0, steps_ - 1);
        droplet(scale_note(index) - 12, .11, true);
        break;
    }
    case Cue::connect:
        bloom(.08);
        steps_ = 0;
        break;
    case Cue::blocked:
        if (since_bump_ > .18)
            bump();
        since_bump_ = 0;
        break;
    case Cue::turn:
        air_target_ = std::min(1.0, air_target_ + .2);
        break;
    case Cue::level:
        chime();
        break;
    case Cue::win:
        fanfare();
        break;
    default:
        break;
    }
}

bool GlassEffects::quiet() const noexcept {
    for (const Tone& tone : tones_)
        if (tone.on)
            return false;
    return air_ < 1e-4 && air_target_ < 1e-4;
}

void GlassEffects::render_add(std::span<float> stereo, double gain) noexcept {
    const std::size_t frames = stereo.size() / 2;
    since_bump_ += static_cast<double>(frames) / sample_rate;
    // After the last tone the room rings on a little, then the voice rests.
    if (quiet()) {
        if (tail_ <= 0)
            return;
        tail_ -= static_cast<int>(frames);
    } else {
        tail_ = 3 * sample_rate;
    }
    const double master = .5 * gain;
    const double air_fall = decay_for(.25), air_follow = rate_for(.06);
    std::size_t frame = 0;
    while (frame < frames) {
        const std::size_t block = std::min<std::size_t>(64, frames - frame);
        air_band_.set(600 + 1200 * air_, 1.6);
        for (std::size_t i = 0; i < block; ++i, ++frame) {
            double left = 0, right = 0, send = 0;
            for (Tone& tone : tones_) {
                if (!tone.on)
                    continue;
                if (tone.delay > 0) {
                    --tone.delay;
                    continue;
                }
                tone.attack = approach(tone.attack, 1.0, tone.attack_rate);
                if (tone.hold > 0 && --tone.hold == 0)
                    for (int p = 0; p < tone.partials; ++p)
                        tone.decay[p] = std::fmin(tone.decay[p], tone.release);
                tone.glide = approach(tone.glide, tone.glide_to, tone.glide_rate);
                double v = 0;
                for (int p = 0; p < tone.partials; ++p) {
                    tone.phase[p] = wrap(tone.phase[p] + tone.inc[p] * tone.glide);
                    v += tone.amp[p] * sine_.at(tone.phase[p]);
                    tone.amp[p] *= tone.decay[p];
                }
                v *= tone.attack;
                if (tone.noise > 1e-5) {
                    tone.noise_band.low(dice_.noise() * tone.noise);
                    v += tone.noise_band.band();
                    tone.noise *= tone.noise_decay;
                }
                if (tone.amp[0] < 1e-4 && tone.noise < 1e-5)
                    tone.on = false;
                left += v * (.5 - .5 * tone.pan) * 1.4;
                right += v * (.5 + .5 * tone.pan) * 1.4;
                send += v * tone.send;
            }
            air_target_ *= air_fall;
            air_ = approach(air_, air_target_, air_follow);
            if (air_ > 1e-4) {
                air_band_.low(dice_.noise());
                air_phase_ = wrap(air_phase_ + .7 / sample_rate);
                const double a = air_band_.band() * air_ * air_ * .2;
                const double sway = .5 + .3 * sine_.at(air_phase_);
                left += a * sway;
                right += a * (1 - sway);
            }
            room_.process(send * .5, left, right);
            for (int c = 0; c < 2; ++c) {
                const double x = c ? right : left;
                dc_[c] += (x - dc_[c]) * .0005;
                stereo[frame * 2 + c] += static_cast<float>((x - dc_[c]) * master);
            }
        }
    }
}

} // namespace ps_cube
