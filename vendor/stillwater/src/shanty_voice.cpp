#include "shanty_voice.hpp"

#include <algorithm>
#include <cmath>

namespace sw {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr int tonic = 62;  // D4
// D Dorian, as semitones above D.
constexpr int dorian[7] = {0, 2, 3, 5, 7, 9, 10};
constexpr int room_lengths[4] = {1601, 2203, 1789, 2411};
constexpr int room_stride = 2600;

double hz_of(double midi) {
    return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
}

bool in_mode(int midi) {
    const int rel = ((midi - tonic) % 12 + 12) % 12;
    for (int k = 0; k < 7; ++k) {
        if (dorian[k] == rel)
            return true;
    }
    return false;
}

// Moves `steps` notes along the mode (from a note in it).
int mode_step(int midi, int steps) {
    int note = midi;
    const int direction = steps > 0 ? 1 : -1;
    for (int k = 0; k < std::abs(steps); ++k) {
        note += direction;
        while (!in_mode(note))
            note += direction;
    }
    return note;
}

// A naive sawtooth with its step smoothed (polyBLEP): bright like a reed, without
// the aliasing whistle of a bare ramp.
double blep_saw(double phase, double increment) {
    double value = 2.0 * phase - 1.0;
    if (phase < increment) {
        const double t = phase / increment;
        value -= t + t - t * t - 1.0;
    } else if (phase > 1.0 - increment) {
        const double t = (phase - 1.0) / increment;
        value -= t * t + t + t + 1.0;
    }
    return value;
}

// Chord progressions, from the tonic: 0 i (Dm), 10 bVII (C), 5 IV (G, the Dorian
// major fourth), 7 v (Am), 3 bIII (F).
typedef ShantyVoice::Chord C;
const C progressions[][8] = {
    {{0, 1}, {0, 1}, {10, 0}, {10, 0}, {0, 1}, {0, 1}, {7, 1}, {0, 1}},   // what shall we do
    {{3, 0}, {3, 0}, {10, 0}, {10, 0}, {0, 1}, {0, 1}, {7, 1}, {0, 1}},   // the chorus lift
    {{0, 1}, {5, 0}, {0, 1}, {5, 0}, {0, 1}, {10, 0}, {7, 1}, {0, 1}},    // the Dorian swing
    {{0, 1}, {10, 0}, {3, 0}, {10, 0}, {0, 1}, {10, 0}, {7, 1}, {0, 1}},  // down the coast
    {{0, 1}, {0, 1}, {3, 0}, {10, 0}, {0, 1}, {5, 0}, {10, 0}, {0, 1}},   // rolling home
};
constexpr int progression_count = static_cast<int>(sizeof(progressions) / sizeof(progressions[0]));

// Within a half bar (three eighths), where notes start and how long they last.
// 0 dotted quarter, 1 quarter and eighth, 2 three eighths, 3 eighth and quarter.
int note_length(int cell, int position) {
    if (cell == 0)
        return position == 0 ? 3 : 0;
    if (cell == 1)
        return position == 0 ? 2 : (position == 2 ? 1 : 0);
    if (cell == 2)
        return 1;
    return position == 0 ? 1 : (position == 1 ? 2 : 0);
}

} // namespace

ShantyVoice::ShantyVoice(std::uint32_t seed) : state_(seed * 2654435761U + 7U) {
    lead_.detune = 1.0042;  // two reeds a few cents apart: the concertina's beat
    lead_.tone = 0.16;
    lead_.attack = 0.006;
    lead_.release = 0.0022;
    lead_.pan = 0.18F;
    fiddle_.detune = 1.0011;
    fiddle_.tone = 0.30;
    fiddle_.attack = 0.0012;  // the bow takes a moment to bite
    fiddle_.release = 0.0012;
    fiddle_.vibrato_depth = 0.0045;
    fiddle_.vibrato_delay = 0.14;
    fiddle_.pan = -0.3F;
    for (int k = 0; k < 3; ++k) {
        chord_[k].detune = 1.0038;
        chord_[k].tone = 0.09;
        chord_[k].attack = 0.01;
        chord_[k].release = 0.003;
        chord_[k].pan = 0.25F;
    }
    bass_.detune = 1.0;
    bass_.tone = 0.035;
    bass_.attack = 0.02;
    bass_.release = 0.0006;
    bass_.pan = -0.05F;
    for (int k = 0; k < 2; ++k) {
        drone_[k].detune = 1.002;
        drone_[k].tone = 0.012;
        drone_[k].attack = 0.00004;
        drone_[k].release = 0.00003;
        drone_[k].pan = k == 0 ? -0.15F : 0.15F;
    }
    // The fiddle's wooden body: broad resonances near 480 Hz and 2.9 kHz.
    const double freq[2] = {480, 2900};
    const double q[2] = {2.2, 1.6};
    const double g[2] = {1.6, 1.1};
    for (int k = 0; k < 2; ++k) {
        const double w = 2 * pi * freq[k] / sample_rate;
        const double r = 1 - w / (2 * q[k]);
        body_a1_[k] = 2 * r * std::cos(w);
        body_a2_[k] = -r * r;
        body_g_[k] = g[k] * (1 - r);
    }
    room_.assign(static_cast<std::size_t>(room_stride * 4), 0.0F);
    plan_verse();
}

double ShantyVoice::uniform() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return (state_ & 0xFFFFFF) / 16777216.0;
}

int ShantyVoice::pick(int n) {
    return std::min(n - 1, static_cast<int>(uniform() * n));
}

bool ShantyVoice::chord_tone(int midi, const Chord& chord) const {
    const int rel = ((midi - tonic - chord.root) % 12 + 24) % 12;
    return rel == 0 || rel == 7 || rel == (chord.minor != 0 ? 3 : 4);
}

// The chord tone nearest `midi`, preferring `direction` (+1 up, -1 down, 0 either).
int ShantyVoice::nearest_scale(int midi, int direction) const {
    const Chord& chord = verse_.bars[bar_];
    int best = midi;
    int best_score = 1000;
    for (int candidate = 57; candidate <= 76; ++candidate) {
        if (!chord_tone(candidate, chord) || !in_mode(candidate))
            continue;
        int score = std::abs(candidate - midi) * 2;
        if (direction != 0 && (candidate - midi) * direction < 0)
            score += 5;
        if (candidate == midi)
            score += 3;  // a tune moves
        if (score < best_score) {
            best_score = score;
            best = candidate;
        }
    }
    return best;
}

void ShantyVoice::plan_verse() {
    // Styles: mostly jigs, a chorus now and then, a slow verse to rest the ear; never
    // the same style three times running.
    int style = 0;
    const double roll = uniform();
    if (verses_ == 0)
        style = 2;  // the tank opens quietly
    else if (roll < 0.28)
        style = 1;
    else if (roll < 0.48)
        style = 2;
    if (style == last_style_ && style != 0)
        style = 0;
    last_style_ = style;
    const int which = verses_ == 0 ? 0 : pick(progression_count);
    for (int k = 0; k < 8; ++k)
        verse_.bars[k] = progressions[which][k];
    verse_.style = style;
    verse_.tempo = style == 2 ? 54 + uniform() * 6 : 68 + uniform() * 10;
    verse_.loud = style == 2 ? 0.6 : (style == 1 ? 1.0 : 0.82);
    lead_is_fiddle_ = style == 2 ? 1 : (uniform() < 0.3 ? 1 : 0);
    // Rhythm: the motif's two bars, repeated in bars 5 and 6; free bars between; the
    // phrase ends lean on a held note.
    for (int k = 0; k < 4; ++k) {
        const double r = uniform();
        rhythm_[k] = r < 0.42 ? 1 : (r < 0.72 ? 2 : (r < 0.86 ? 3 : 0));
    }
    rhythm_[3] = rhythm_[3] == 0 ? 1 : rhythm_[3];
    for (int half = 0; half < 16; ++half) {
        const int bar = half / 2;
        if (bar == 0 || bar == 1 || bar == 4 || bar == 5) {
            phrase_rhythm_[half] = rhythm_[half % 4];
        } else {
            const double r = uniform();
            phrase_rhythm_[half] = r < 0.45 ? 1 : (r < 0.75 ? 2 : (r < 0.9 ? 3 : 0));
        }
    }
    phrase_rhythm_[7] = 0;   // the question rests on its last beat
    phrase_rhythm_[14] = 1;
    phrase_rhythm_[15] = 0;  // home
    if (style == 2) {
        // A slow verse sings in longer notes.
        for (int half = 0; half < 16; ++half) {
            if (phrase_rhythm_[half] == 2)
                phrase_rhythm_[half] = 1;
        }
    }
    make_motif();
    bar_ = 0;
    eighth_ = 0;
    ++verses_;
}

// Two bars of contour, as mode steps from the note before: a leap up from the strong
// beat and a run back down is the shanty's shape, with now and then a repeated note.
void ShantyVoice::make_motif() {
    const int shapes[4][6] = {
        {2, 1, -1, 1, -1, -1},
        {0, 2, 1, -2, -1, 1},
        {-1, -1, 2, 2, -1, -1},
        {3, -1, -1, 1, -1, -1},
    };
    const int first = pick(4);
    const int second = pick(4);
    for (int k = 0; k < 6; ++k) {
        motif_[k] = shapes[first][k];
        motif_[6 + k] = shapes[second][k] * (uniform() < 0.35 ? -1 : 1);
    }
}

int ShantyVoice::melody_note(int bar, int eighth, bool strong) {
    // Cadences: the question ends on A, the answer comes home to D.
    if (bar == 3 && eighth == 3)
        return std::abs(melody_ - 69) <= std::abs(melody_ - 57) ? 69 : 57;
    if (bar == 7 && eighth == 3)
        return std::abs(melody_ - 62) <= std::abs(melody_ - 74) ? 62 : 74;
    int note = melody_;
    if (bar == 0 || bar == 1 || bar == 4 || bar == 5) {
        const int step = motif_[(bar % 2) * 6 + eighth];
        note = mode_step(melody_, step);
        if (strong && !chord_tone(note, verse_.bars[bar]))
            note = nearest_scale(note, step >= 0 ? 1 : -1);
    } else if (strong) {
        const int direction = melody_ > 68 ? -1 : (melody_ < 60 ? 1 : (uniform() < 0.5 ? 1 : -1));
        note = nearest_scale(mode_step(melody_, direction * (1 + pick(2))), direction);
    } else {
        const double r = uniform();
        const int step = r < 0.4 ? -1 : (r < 0.75 ? 1 : (r < 0.88 ? -2 : 2));
        note = mode_step(melody_, step);
    }
    // Keep the tune in the singer's range.
    while (note > 76)
        note = mode_step(note, -7);
    while (note < 57)
        note = mode_step(note, 7);
    return note;
}

void ShantyVoice::sound_note(Reed& reed, int midi, double seconds, double strength) {
    const double hz = hz_of(midi);
    if (reed.level < 0.02 || reed.hz <= 0)
        reed.hz = hz;
    reed.target_hz = hz;
    reed.hold = seconds;
    reed.peak = strength;
    reed.age = 0;
}

void ShantyVoice::stomp(double strength) {
    stomp_level_ = std::max(stomp_level_, strength);
    stomp_hz_ = 118;
    stomp_phase_ = 0;
    knock_level_ = std::max(knock_level_, strength * 0.55);
}

// The score advances by eighth notes: the melody, the bass on the two beats of the
// bar, the boot, and the concertina's chords on the off-beats.
void ShantyVoice::step_eighth() {
    if (bar_ >= 8)
        plan_verse();
    const Chord& chord = verse_.bars[bar_];
    const double eighth = 60.0 / (tempo_ * 3);
    const int style = verse_.style;
    const int half = bar_ * 2 + eighth_ / 3;
    const int position = eighth_ % 3;
    const int length = note_length(phrase_rhythm_[half], position);
    if (length > 0) {
        const bool strong = position == 0;
        const int note = melody_note(bar_, eighth_, strong);
        melody_ = note;
        const double accent = (eighth_ == 0 ? 1.0 : (eighth_ == 3 ? 0.9 : 0.75)) * loud_;
        const double seconds = eighth * length * (length == 1 ? 0.78 : 0.9);
        // Call and response: the lead sings bars 1-2 and 5-6, the other answers.
        const bool answer = bar_ == 2 || bar_ == 3 || bar_ == 6 || bar_ == 7;
        bool fiddle_sings = lead_is_fiddle_ != 0;
        if (style == 0 && answer)
            fiddle_sings = !fiddle_sings;
        if (style == 1) {
            sound_note(lead_, note, seconds, 0.30 * accent);
            sound_note(fiddle_, note + 12, seconds, 0.17 * accent);
        } else if (fiddle_sings) {
            sound_note(fiddle_, note + (style == 2 ? 0 : 12), seconds * (style == 2 ? 1.05 : 1.0), 0.24 * accent);
        } else {
            sound_note(lead_, note, seconds, 0.32 * accent);
        }
    }
    const int root = tonic - 24 + chord.root - (chord.root > 6 ? 12 : 0);
    if (eighth_ == 0 || eighth_ == 3) {
        const int bass_note = eighth_ == 0 ? root : root + 7;
        sound_note(bass_, bass_note, eighth * (style == 2 ? 2.6 : 1.6), (style == 2 ? 0.30 : 0.42) * loud_);
        if (style != 2)
            stomp((eighth_ == 0 ? 0.9 : 0.6) * loud_);
    }
    // The left hand: short squeezes on the off-beats, or one long chord in a slow verse.
    const int third = chord.minor != 0 ? 3 : 4;
    const int voicing[3] = {tonic - 12 + chord.root, tonic - 12 + chord.root + third, tonic - 12 + chord.root + 7};
    if (style != 2 && (eighth_ == 2 || eighth_ == 5)) {
        for (int k = 0; k < 3; ++k)
            sound_note(chord_[k], voicing[k] - (voicing[k] > 64 ? 12 : 0), eighth * 0.55, 0.075 * loud_);
    } else if (style == 2 && eighth_ == 0) {
        for (int k = 0; k < 3; ++k)
            sound_note(chord_[k], voicing[k] - (voicing[k] > 64 ? 12 : 0), eighth * 5.5, 0.05 * loud_);
    }
    // The drone: D and A under everything, strongest in a slow verse.
    const double drone = style == 2 ? 0.11 : 0.045;
    sound_note(drone_[0], tonic - 24, eighth * 1.5, drone * loud_);
    sound_note(drone_[1], tonic - 17, eighth * 1.5, drone * 0.7 * loud_);

    ++eighth_;
    if (eighth_ >= 6) {
        eighth_ = 0;
        ++bar_;
    }
}

float ShantyVoice::run_reed(Reed& reed) {
    const double dt = 1.0 / sample_rate;
    const bool held = reed.hold > 0;
    reed.hold -= dt;
    reed.age += dt;
    const double goal = held ? reed.peak : 0.0;
    reed.level += (goal - reed.level) * (held ? reed.attack : reed.release);
    if (reed.level < 1e-5 && !held)
        return 0.0F;
    reed.hz += (reed.target_hz - reed.hz) * 0.004;  // a small slide into each note
    double hz = reed.hz;
    if (reed.vibrato_depth > 0) {
        reed.vibrato_phase += 5.6 * dt;
        if (reed.vibrato_phase >= 1)
            reed.vibrato_phase -= 1;
        const double depth = reed.vibrato_depth * std::clamp((reed.age - reed.vibrato_delay) * 4.0, 0.0, 1.0);
        hz *= 1.0 + depth * std::sin(2 * pi * reed.vibrato_phase);
    }
    const double increment_a = hz * dt;
    const double increment_b = hz * reed.detune * dt;
    reed.phase_a += increment_a;
    if (reed.phase_a >= 1)
        reed.phase_a -= 1;
    reed.phase_b += increment_b;
    if (reed.phase_b >= 1)
        reed.phase_b -= 1;
    const double raw = 0.5 * (blep_saw(reed.phase_a, increment_a) + blep_saw(reed.phase_b, increment_b));
    reed.low += (raw - reed.low) * reed.tone;
    const float result = static_cast<float>(reed.low * reed.level);
    return result;
}

void ShantyVoice::render_add(std::span<float> stereo, double gain) {
    const std::size_t frames = stereo.size() / 2;
    const double dt = 1.0 / sample_rate;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        clock_ -= dt;
        if (clock_ <= 0) {
            tempo_ += (verse_.tempo - tempo_) * 0.12;
            loud_ += (verse_.loud - loud_) * 0.15;
            step_eighth();
            // The jig's lilt: the first eighth of each beat a little long.
            const int previous = (eighth_ + 5) % 6;
            const double lilt = previous % 3 == 0 ? 1.1 : 0.95;
            clock_ += 60.0 / (tempo_ * 3) * lilt;
        }
        double left = 0;
        double right = 0;
        // Concertina, both hands.
        const float lead = run_reed(lead_);
        left += lead * (0.5 - lead_.pan * 0.5);
        right += lead * (0.5 + lead_.pan * 0.5);
        for (int k = 0; k < 3; ++k) {
            const float c = run_reed(chord_[k]);
            left += c * (0.5 - chord_[k].pan * 0.5);
            right += c * (0.5 + chord_[k].pan * 0.5);
        }
        // Fiddle through its body.
        const float string = run_reed(fiddle_);
        double body = 0;
        for (int k = 0; k < 2; ++k) {
            const double y = body_g_[k] * string + body_a1_[k] * body_y1_[k] + body_a2_[k] * body_y2_[k];
            body_y2_[k] = body_y1_[k];
            body_y1_[k] = y;
            body += y;
        }
        const double fiddle = body + string * 0.35;
        left += fiddle * (0.5 - fiddle_.pan * 0.5);
        right += fiddle * (0.5 + fiddle_.pan * 0.5);
        // Bass and drone.
        const float bass = run_reed(bass_) * 1.6F;
        left += bass * 0.5;
        right += bass * 0.5;
        for (int k = 0; k < 2; ++k) {
            const float d = run_reed(drone_[k]) * 1.4F;
            left += d * (0.5 - drone_[k].pan * 0.5);
            right += d * (0.5 + drone_[k].pan * 0.5);
        }
        // The boot: a falling thump and a woody knock.
        if (stomp_level_ > 1e-4) {
            stomp_phase_ += stomp_hz_ * dt;
            if (stomp_phase_ >= 1)
                stomp_phase_ -= 1;
            stomp_hz_ += (46 - stomp_hz_) * 0.0016;
            const double thump = std::sin(2 * pi * stomp_phase_) * stomp_level_ * 0.5;
            stomp_level_ *= 0.99955;
            left += thump;
            right += thump;
        }
        if (knock_level_ > 1e-4) {
            const double noise = uniform() * 2 - 1;
            knock_low_ += (noise - knock_low_) * 0.18;
            const double knock = knock_low_ * knock_level_ * 0.35;
            knock_level_ *= 0.9975;
            left += knock;
            right += knock;
        }
        // A low-ceilinged room: four damped feedback delays.
        double wet_left = 0;
        double wet_right = 0;
        const double feed = 0.52;
        for (int k = 0; k < 4; ++k) {
            const std::size_t at = static_cast<std::size_t>(k * room_stride + room_write_[k]);
            const double delayed = room_[at];
            room_low_[k] += (delayed - room_low_[k]) * 0.45;
            const double input = k < 2 ? left : right;
            room_[at] = static_cast<float>(input + room_low_[k] * feed);
            if (k < 2)
                wet_left += delayed;
            else
                wet_right += delayed;
            ++room_write_[k];
            if (room_write_[k] >= room_lengths[k])
                room_write_[k] = 0;
        }
        left += wet_left * 0.11;
        right += wet_right * 0.11;
        // Remove any offset, then a gentle ceiling.
        const double out_left = left - dc_in_left_ + 0.9995 * dc_left_;
        dc_in_left_ = left;
        dc_left_ = out_left;
        const double out_right = right - dc_in_right_ + 0.9995 * dc_right_;
        dc_in_right_ = right;
        dc_right_ = out_right;
        stereo[frame * 2] += static_cast<float>(std::tanh(out_left * 1.2) * gain);
        stereo[frame * 2 + 1] += static_cast<float>(std::tanh(out_right * 1.2) * gain);
    }
}

} // namespace sw
