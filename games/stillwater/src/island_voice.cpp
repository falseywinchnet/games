#include "island_voice.hpp"

#include <algorithm>
#include <cmath>

namespace sw {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double sr = IslandVoice::sample_rate;
constexpr int pluck_line = 2048;

double hz_of(double midi) {
    return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
}

// Per-sample multiplier for a fall of 60 dB in `seconds`.
double decay_for(double seconds) {
    return std::pow(10.0, -3.0 / (seconds * sr));
}

// Per-sample approach coefficient with time constant `seconds`.
double approach(double seconds) {
    return 1.0 - std::exp(-1.0 / (seconds * sr));
}

// The four notes of each kind of chord, in semitones above its root.
constexpr int chord_tones[5][4] = {
    {0, 4, 7, 9},   // major sixth: the island chord
    {0, 4, 7, 11},  // major seventh
    {0, 4, 7, 10},  // dominant seventh
    {0, 3, 7, 10},  // minor seventh
    {0, 3, 7, 9},   // minor sixth
};
constexpr int major_scale[7] = {0, 2, 4, 5, 7, 9, 11};

// The sections. Roots are numbered from the key: 0 I, 2 II, 4 III, 5 IV, 7 V, 9 VI.
const IslandVoice::Section sections[] = {
    {"aloha", {{0, 0}, {0, 0}, {5, 0}, {5, 4}, {0, 0}, {9, 2}, {2, 2}, {7, 2}}, 0, 90, 0.95},
    {"lagoon", {{0, 1}, {0, 1}, {2, 2}, {2, 2}, {7, 2}, {7, 2}, {0, 0}, {7, 2}}, 1, 96, 0.9},
    {"tiki", {{5, 0}, {5, 0}, {0, 0}, {0, 0}, {2, 2}, {7, 2}, {0, 0}, {0, 0}}, 2, 88, 0.9},
    {"moonlight", {{9, 3}, {2, 2}, {7, 2}, {0, 1}, {9, 3}, {2, 2}, {7, 2}, {0, 0}}, 0, 84, 0.8},
    {"drift", {{0, 0}, {4, 2}, {5, 0}, {5, 4}, {0, 0}, {9, 2}, {2, 2}, {7, 2}}, 2, 92, 0.9},
    {"reef", {{0, 0}, {9, 3}, {2, 3}, {7, 2}, {0, 0}, {9, 3}, {2, 3}, {7, 2}}, 1, 100, 0.95},
};
constexpr int section_count = static_cast<int>(sizeof(sections) / sizeof(sections[0]));

// Melody rhythms for a bar: where notes start and how many eighths each lasts.
constexpr int rhythms[6][8] = {
    {8, 0, 0, 0, 0, 0, 0, 0},  // one long note
    {4, 0, 0, 0, 4, 0, 0, 0},  // two halves
    {3, 0, 0, 1, 4, 0, 0, 0},  // dotted, a quick one, a half
    {0, 0, 2, 0, 4, 0, 0, 0},  // a pickup into beat three
    {2, 0, 2, 0, 2, 0, 2, 0},  // four quarters
    {3, 0, 0, 3, 0, 0, 2, 0},  // the island lilt: three, three, two
};

// The ukulele's strings, top (as it is held) to bottom: re-entrant G C E A.
constexpr int uke_open[4] = {67, 60, 64, 69};

// Island strums: per eighth, 0 none, 1 down, 2 up, 3 a muted chunk.
constexpr int strums[3][8] = {
    {1, 0, 1, 2, 0, 2, 1, 2},  // down, down-up, up-down-up
    {0, 0, 3, 0, 0, 0, 3, 0},  // chunks on two and four
    {1, 0, 3, 2, 1, 0, 3, 2},  // a busier lope
};

} // namespace

IslandVoice::IslandVoice(std::uint32_t seed) : state_(seed * 2654435761U + 0x9E3779B9U) {
    for (int k = 0; k < 4; ++k) {
        uke_[k].line.assign(pluck_line, 0.0F);
        uke_[k].pan = 0.18F + 0.04F * static_cast<float>(k);
    }
    scratch_.assign(pluck_line, 0.0F);
    steel_[0].pan = -0.12F;
    steel_[1].pan = -0.2F;
    for (int k = 0; k < 6; ++k)
        mallets_[k].pan = 0.3F;
    const int allpass_length[4] = {142, 107, 379, 277};
    for (int k = 0; k < 4; ++k)
        allpass_[k].assign(static_cast<std::size_t>(allpass_length[k]), 0.0F);
    spring_[0].assign(1531, 0.0F);
    spring_[1].assign(1867, 0.0F);
    tape_[0].assign(1024, 0.0F);
    tape_[1].assign(1024, 0.0F);
    plan_section();
}

double IslandVoice::uniform() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return (state_ & 0xFFFFFF) / 16777216.0;
}

int IslandVoice::pick(int n) {
    return std::min(n - 1, static_cast<int>(uniform() * n));
}

int IslandVoice::chord_pc(const Chord& chord, int index) const {
    return (key_ + chord.root + chord_tones[chord.kind][index]) % 12;
}

bool IslandVoice::chord_tone(int midi, const Chord& chord) const {
    const int pc = ((midi % 12) + 12) % 12;
    for (int k = 0; k < 4; ++k)
        if (chord_pc(chord, k) == pc)
            return true;
    return false;
}

// Next melody note: a chord tone on the strong beats, any note of the key (or of
// the chord) between; near the last one, rarely the same, kept in the steel's
// sweet range.
int IslandVoice::lead_note(int previous, const Chord& chord, bool strong) {
    int best = previous;
    double best_score = -1e9;
    for (int cand = 65; cand <= 84; ++cand) {
        const int rel = ((cand - key_) % 12 + 12) % 12;
        bool in_key = false;
        for (int d : major_scale)
            in_key = in_key || rel == d;
        const bool tone = chord_tone(cand, chord);
        if (!in_key && !tone)
            continue;
        const int step = std::abs(cand - previous);
        double score = -0.45 * step - (step == 0 ? 2.0 : 0.0) + (step > 7 ? -2.0 : 0.0);
        score += tone ? (strong ? 3.0 : 1.0) : (strong ? -2.5 : 0.0);
        score -= std::abs(cand - 74) * 0.08;
        score += uniform() * 1.8;
        if (score > best_score) {
            best_score = score;
            best = cand;
        }
    }
    return best;
}

// The lap steel through a small warm amplifier: a round tone, mostly fundamental,
// its few overtones gone within moments of the pick, slid into its note with the
// bar, with a slow wide vibrato once it rings. A swell is the volume knob rolled up
// after the pick.
void IslandVoice::steel(Tone& tone, int midi, double seconds, double strength, double slide, bool swell) {
    const double amps[6] = {1.0, 0.38, 0.16, 0.06, 0.025, 0.01};
    tone.partials = 6;
    for (int k = 0; k < 6; ++k) {
        tone.ratio[k] = k + 1;
        tone.amp[k] = amps[k] * strength * (0.9 + 0.2 * uniform());
        tone.decay[k] = decay_for(k == 0 ? 4.5 : 1.6 / std::pow(k + 1.0, 1.2));
    }
    tone.target_hz = hz_of(midi);
    tone.hz = tone.target_hz * std::pow(2.0, -slide / 12.0);
    tone.glide = slide > 0 ? approach(0.035 + 0.03 * slide) : 1.0;
    tone.attack = approach(swell ? 0.22 : 0.012);
    tone.env = 0;
    tone.age = 0;
    tone.vibrato_depth = 0.006 + 0.003 * uniform();
    tone.vibrato_delay = std::min(0.35, seconds * 0.3);
    tone.tremolo_depth = 0;
    tone.release_in = seconds;
    tone.damp = decay_for(0.35);
    tone.level = 1;
}

// A vibraphone bar (the motor turning) or a marimba bar: a sine with its tuned
// overtones, quick to strike, the marimba's short and woody.
void IslandVoice::vibes(Tone& tone, int midi, double strength, bool marimba) {
    tone.partials = 3;
    tone.ratio[0] = 1;
    tone.ratio[1] = marimba ? 3.93 : 4.0;
    tone.ratio[2] = marimba ? 9.9 : 10.0;
    tone.amp[0] = strength;
    tone.amp[1] = strength * (marimba ? 0.22 : 0.28);
    tone.amp[2] = strength * 0.05;
    tone.decay[0] = decay_for(marimba ? 0.65 : 2.6);
    tone.decay[1] = decay_for(marimba ? 0.22 : 0.8);
    tone.decay[2] = decay_for(marimba ? 0.08 : 0.25);
    for (int k = 0; k < 3; ++k)
        tone.phase[k] = 0;
    tone.hz = hz_of(midi);
    tone.target_hz = tone.hz;
    tone.glide = 1;
    tone.attack = approach(0.0015);
    tone.env = 0;
    tone.age = 0;
    tone.vibrato_depth = 0;
    tone.tremolo_depth = marimba ? 0.0 : 0.38;
    tone.release_in = -1;
    tone.level = 1;
}

float IslandVoice::run_tone(Tone& tone) {
    if (tone.level < 1e-5 || tone.partials == 0)
        return 0.0F;
    const double dt = 1.0 / sr;
    tone.age += dt;
    tone.hz += (tone.target_hz - tone.hz) * tone.glide;
    tone.env += (1.0 - tone.env) * tone.attack;
    double vib = 0;
    if (tone.vibrato_depth > 0 && tone.age > tone.vibrato_delay) {
        tone.vibrato_phase += 5.3 * dt;
        const double grow = std::min(1.0, (tone.age - tone.vibrato_delay) / 0.4);
        vib = tone.vibrato_depth * grow * std::sin(2 * pi * tone.vibrato_phase);
    }
    const double freq = tone.hz * (1.0 + vib);
    double sum = 0;
    for (int k = 0; k < tone.partials; ++k) {
        const double f = freq * tone.ratio[k];
        if (f > 9000)
            continue;
        tone.phase[k] += f * dt;
        tone.phase[k] -= std::floor(tone.phase[k]);
        tone.amp[k] *= tone.decay[k];
        sum += tone.amp[k] * std::sin(2 * pi * tone.phase[k]);
    }
    if (tone.tremolo_depth > 0) {
        tone.tremolo_phase += 5.6 * dt;
        sum *= 1.0 - tone.tremolo_depth * (0.5 - 0.5 * std::cos(2 * pi * tone.tremolo_phase));
    }
    if (tone.release_in >= 0) {
        tone.release_in -= dt;
        if (tone.release_in < 0)
            tone.release_in = -2;
    }
    if (tone.release_in == -2)
        tone.level *= tone.damp;
    if (tone.amp[0] < 1e-5)
        tone.level = 0;
    return static_cast<float>(sum * tone.env * tone.level);
}

void IslandVoice::pluck_now(Pluck& string, double midi, double strength) {
    const double period = sr / hz_of(midi);
    string.delay = std::clamp(period - 0.5, 4.0, pluck_line - 8.0);
    if (string.pending != 0)
        string.mute_in = -1;  // a strummed string keeps the mute its strum asked for
    // A nylon string pushed aside by the flesh of a finger: a triangle peaked where it is
    // struck, rounded (a finger is soft), with only a trace of roughness.
    const int n = static_cast<int>(string.delay);
    const double where = 0.16 + 0.08 * uniform();
    double low = 0;
    double mean = 0;
    for (int i = 0; i < n; ++i) {
        const double x = (i + 0.5) / n;
        const double shape = x < where ? x / where : (1 - x) / (1 - where);
        low += (shape + 0.04 * (uniform() * 2 - 1) - low) * 0.18;
        scratch_[static_cast<std::size_t>(i)] = static_cast<float>(low);
        mean += low;
    }
    mean /= n;
    for (int i = 0; i < n; ++i) {
        const int at = (string.write - n + i + pluck_line * 2) % pluck_line;
        string.line[static_cast<std::size_t>(at)] =
            string.line[static_cast<std::size_t>(at)] * 0.3F + static_cast<float>((scratch_[static_cast<std::size_t>(i)] - mean) * strength);
    }
}

float IslandVoice::run_pluck(Pluck& string) {
    if (string.pending >= 0) {
        if (string.pending == 0)
            pluck_now(string, string.pending_midi, string.pending_strength);
        --string.pending;
    }
    if (string.mute_in > 0) {
        string.mute_in -= 1.0 / sr;
        if (string.mute_in <= 0)
            string.loss = 0.93F;
    }
    const double at = string.write - string.delay;
    const double floor_at = std::floor(at);
    const double frac = at - floor_at;
    const int i0 = (static_cast<int>(floor_at) + pluck_line * 4) % pluck_line;
    const int i1 = (i0 + 1) % pluck_line;
    const float out = static_cast<float>(string.line[static_cast<std::size_t>(i0)] * (1 - frac) + string.line[static_cast<std::size_t>(i1)] * frac);
    string.line[static_cast<std::size_t>(string.write)] = string.loss * (string.bright * out + (1 - string.bright) * string.last);
    string.last = out;
    string.write = (string.write + 1) % pluck_line;
    return out;
}

// A strum across the ukulele: each string takes the lowest fret under the hand that
// rings in the chord, the strings sounding a few milliseconds apart.
void IslandVoice::strum(const Chord& chord, bool down, double strength) {
    const double spread = (down ? 0.009 : 0.006) * (0.75 + 0.6 * uniform());
    brush_env_ = strength * (down ? 0.5 : 0.35);
    brush_hz_ = down ? 2600 : 3400;
    for (int k = 0; k < 4; ++k) {
        // an up-strum catches the A, E and often the C, rarely the G
        if (!down && k == 0 && uniform() < 0.8)
            continue;
        int note = uke_open[k];
        for (int fret = uke_pos_; fret <= uke_pos_ + 5; ++fret) {
            if (chord_tone(uke_open[k] + fret, chord)) {
                note = uke_open[k] + fret;
                break;
            }
        }
        Pluck& string = uke_[k];
        const int order = down ? k : 3 - k;
        string.pending = static_cast<int>(order * spread * sr);
        string.pending_midi = note;
        string.pending_strength = strength * (0.85 + 0.3 * uniform());
        string.loss = static_cast<float>(std::pow(10.0, -3.0 / (0.75 * hz_of(note))));
        string.bright = 0.38F;
        string.mute_in = -1;
    }
}

// The upright bass: a round fundamental with a little of its octave and twelfth,
// both gone in a moment, plucked with the side of the finger and stopped by the
// hand when its time is up.
void IslandVoice::upright(int midi, double seconds) {
    const double amps[4] = {1.0, 0.42, 0.16, 0.05};
    const double rings[4] = {1.8, 0.5, 0.22, 0.12};
    bass_.partials = 4;
    for (int k = 0; k < 4; ++k) {
        bass_.ratio[k] = k + 1;
        bass_.amp[k] = amps[k] * (0.9 + 0.15 * uniform());
        bass_.decay[k] = decay_for(rings[k]);
    }
    bass_.hz = hz_of(midi);
    bass_.target_hz = bass_.hz;
    bass_.glide = 1;
    bass_.attack = approach(0.006);
    // the phases run on from the last note, so a new one never clicks; from silence it
    // rises over a few milliseconds like a finger leaving the string
    if (bass_.level * bass_.amp[0] < 0.05)
        bass_.env = 0;
    bass_.age = 0;
    bass_.vibrato_depth = 0;
    bass_.tremolo_depth = 0;
    bass_.release_in = seconds;
    bass_.damp = decay_for(0.12);
    bass_.level = 1;
    thump_env_ = 0.5;
}

void IslandVoice::bubble() {
    bubble_hz_ = 280 + 520 * uniform();
    bubble_rise_ = std::pow(2.2 + uniform(), 1.0 / (0.07 * sr));
    bubble_env_ = 0.6 + 0.4 * uniform();
    bubble_phase_ = 0;
    bubble_pan_ = uniform() * 1.4 - 0.7;
}

void IslandVoice::plan_section() {
    int next = last_section_;
    for (int tries = 0; tries < 10 && next == last_section_; ++tries)
        next = pick(section_count);
    last_section_ = next;
    section_ = sections[next];
    section_.tempo += (uniform() * 2 - 1) * 4;
    // The vamp leads into the next key: mostly home in C, or up to F or G, or Bb.
    const double roll = uniform();
    next_key_ = verses_ < 2 ? 0 : roll < 0.5 ? 0 : roll < 0.72 ? 5 : roll < 0.9 ? 7 : 10;
    strum_pattern_ = section_.lead == 1 ? 2 : pick(3);
    uke_pos_ = uniform() < 0.55 ? 0 : uniform() < 0.6 ? 2 : 4;  // open chords, or up the neck
    vamp_ = verses_ > 0;
    if (!vamp_)
        key_ = next_key_;
    bar_ = 0;
    ++verses_;
}

void IslandVoice::step_eighth() {
    const int e = eighth_;
    Chord chord{};
    if (vamp_) {
        key_ = next_key_;
        chord = bar_ == 0 ? Chord{2, 2} : Chord{7, 2};
    } else {
        chord = section_.bars[bar_];
    }
    const bool phrase_end = !vamp_ && (bar_ == 3 || bar_ == 7);

    // --- the melody: plan the bar on its first eighth
    if (e == 0) {
        for (int k = 0; k < 8; ++k)
            lead_len_[k] = 0;
        if (vamp_) {
            // the steel's swoop over the vamp: a held note, then a long slide up
            lead_len_[0] = bar_ == 0 ? 4 : 8;
            if (bar_ == 0)
                lead_len_[4] = 4;
        } else if (phrase_end) {
            lead_len_[0] = 4;  // a held note; the mallets answer in the second half
        } else if ((bar_ == 1 || bar_ == 5) && uniform() < 0.2) {
            // a bar left open: the uke and the bass carry it
        } else {
            const int which = section_.lead == 1 ? (uniform() < 0.6 ? 4 : 5) : pick(6);
            for (int k = 0; k < 8; ++k)
                lead_len_[k] = rhythms[which][k];
        }
    }
    const double beat = 60.0 / tempo_;
    if (lead_len_[e] > 0) {
        const bool strong = e == 0 || e == 4;
        const int note = lead_note(melody_, chord, strong);
        melody_ = note;
        const double seconds = lead_len_[e] * beat * 0.5;
        if (section_.lead == 1 && !vamp_) {
            Tone& bar = mallets_[mallet_next_];
            mallet_next_ = (mallet_next_ + 1) % 6;
            vibes(bar, note, 0.55 + 0.15 * uniform(), false);
            // the steel holds the chord underneath, swelled in, now and then
            if (e == 0 && bar_ % 2 == 0) {
                int low = note - 7;
                while (!chord_tone(low, chord))
                    --low;
                steel(steel_[0], low, beat * 4, 0.35, 0.0, true);
            }
        } else {
            const double slide = vamp_ && bar_ == 1 ? 7.0 : (uniform() < (lead_len_[e] >= 4 ? 0.7 : 0.4) ? 1.0 + pick(2) : 0.0);
            const bool swell = lead_len_[e] >= 4 && uniform() < 0.3;
            steel(steel_[0], note, seconds, 0.9, slide, swell);
            // the steel's sixths: a second bar note below, slid together
            if (section_.lead == 2 || (uniform() < 0.25 && lead_len_[e] >= 3)) {
                int low = note - 8;
                while (!chord_tone(low, chord) && low > note - 10)
                    --low;
                if (chord_tone(low, chord))
                    steel(steel_[1], low, seconds, 0.6, slide, swell);
            }
        }
    }
    // --- the mallets answer a held phrase end, up the chord
    if (phrase_end && e >= 4 && section_.lead != 1) {
        const bool marimba = uniform() < 0.4;
        const int base = 72 + key_ % 12 > 79 ? 60 : 72;
        int up = base;
        for (int k = 0; k < e - 4; ++k) {
            ++up;
            while (!chord_tone(up, chord))
                ++up;
        }
        while (!chord_tone(up, chord))
            ++up;
        Tone& bar = mallets_[mallet_next_];
        mallet_next_ = (mallet_next_ + 1) % 6;
        vibes(bar, up, 0.45, marimba);
    }
    // --- the ukulele
    const int s = strums[strum_pattern_][e];
    if (s != 0) {
        strum(chord, s != 2, (s == 2 ? 0.32 : 0.48) * (e == 0 ? 1.1 : 1.0));
        if (s == 3) {
            // a chunk: strummed and stopped at once by the palm, mostly the slap of it
            for (Pluck& string : uke_)
                string.mute_in = 0.055;
            brush_env_ *= 1.6;
            brush_hz_ = 1500;
        }
    }
    // --- the bass: root and fifth, walking into the next bar at phrase ends
    {
        const int pc = (key_ + chord.root) % 12;
        int root = 36 + pc;
        if (root > 45)
            root -= 12;
        int note = -1;
        if (e == 0)
            note = root;
        else if (e == 4)
            note = root + 7 > 50 ? root - 5 : root + 7;
        else if (e == 6 && (bar_ % 2 == 1 || vamp_)) {
            int next_root = 0;
            if (vamp_ && bar_ == 1)
                next_root = (key_ + section_.bars[0].root) % 12;
            else if (vamp_)
                next_root = (key_ + 7) % 12;
            else
                next_root = (key_ + section_.bars[std::min(7, bar_ + 1)].root) % 12;
            int target = 36 + next_root;
            if (target > 45)
                target -= 12;
            note = target + (uniform() < 0.5 ? -1 : 1);
        }
        if (note > 0)
            upright(note, beat * (e == 6 ? 0.8 : 1.6));
    }
    // --- the shaker: every eighth, leaning on the off-beats
    shake_env_ = (e % 2 == 1 ? 0.55 : 0.32) * (0.85 + 0.3 * uniform());

    eighth_ = (eighth_ + 1) % 8;
    if (eighth_ == 0) {
        ++bar_;
        if (vamp_ && bar_ >= 2) {
            vamp_ = false;
            bar_ = 0;
        } else if (!vamp_ && bar_ >= 8) {
            plan_section();
        }
    }
}

void IslandVoice::render_add(std::span<float> stereo, double gain) {
    const double dt = 1.0 / sr;
    const double hp = approach(1.0 / (2 * pi * 110));
    const double lp = 1.0 - std::exp(-2 * pi * 6200 / sr);
    for (std::size_t frame = 0; frame + 1 < stereo.size(); frame += 2) {
        clock_ -= dt;
        if (clock_ <= 0) {
            tempo_ += (section_.tempo - tempo_) * 0.05;
            loud_ += (section_.loud - loud_) * 0.04;
            const double beat = 60.0 / tempo_;
            // a lazy island swing: the on-beat eighth well longer than the off-beat
            clock_ += beat * (eighth_ % 2 == 0 ? 0.62 : 0.38);
            step_eighth();
        }
        double left = 0;
        double right = 0;
        double send = 0;
        // the steel
        for (Tone& tone : steel_) {
            const double v = run_tone(tone) * 0.2 * part_gain(0);
            left += v * (1 - tone.pan);
            right += v * (1 + tone.pan);
            send += v * 0.9;
        }
        // the mallets
        for (Tone& tone : mallets_) {
            const double v = run_tone(tone) * 0.3 * part_gain(1);
            left += v * (1 - tone.pan);
            right += v * (1 + tone.pan);
            send += v * 0.7;
        }
        // the ukulele: the strings ring through its little wooden body (the air inside
        // near 230 Hz, the top near 520 Hz), and each strum brushes the strings
        {
            double strings = 0;
            for (Pluck& string : uke_)
                strings += run_pluck(string);
            brush_env_ *= 0.9988;
            const double noise = uniform() * 2 - 1;
            const double w = 2 * pi * brush_hz_ / sr;
            const double rr = 0.93;
            const double brush = noise * (1 - rr) + 2 * rr * std::cos(w) * brush_y1_ - rr * rr * brush_y2_;
            brush_y2_ = brush_y1_;
            brush_y1_ = brush;
            const double body_hz[2] = {230, 520};
            const double body_q[2] = {3.5, 2.5};
            double body = 0;
            for (int k = 0; k < 2; ++k) {
                const double bw = 2 * pi * body_hz[k] / sr;
                const double br = 1 - bw / (2 * body_q[k]);
                // input scaled so the resonance peaks at unity gain
                const double y = strings * (1 - br) * 2 * std::sin(bw) + 2 * br * std::cos(bw) * uke_body_[k][0] - br * br * uke_body_[k][1];
                uke_body_[k][1] = uke_body_[k][0];
                uke_body_[k][0] = y;
                body += y;
            }
            uke_tone_ += (strings * 0.55 + body * 1.6 - uke_tone_) * 0.42;  // nylon: soft on top
            const double v = (uke_tone_ + brush * brush_env_ * 0.6) * 0.4 * part_gain(2);
            left += v * 0.8;
            right += v * 1.2;
            send += v * 0.3;
        }
        // the bass: its tone, and the soft thump of the finger
        {
            thump_env_ *= 0.9965;
            thump_low_ += ((uniform() * 2 - 1) * thump_env_ - thump_low_) * 0.02;
            const double v = (run_tone(bass_) * 0.32 + thump_low_ * 0.5) * part_gain(3);
            left += v;
            right += v;
        }
        // the shaker: bright noise in short swishes
        {
            const double noise = uniform() * 2 - 1;
            shake_hp_ = noise - shake_prev_;
            shake_prev_ = noise;
            shake_env_ *= 0.99925;
            const double v = shake_hp_ * shake_env_ * 0.05 * part_gain(4);
            left += v * 0.8;
            right += v * 1.2;
        }
        // bubbles: little rising chirps, a few at a time
        bubble_wait_ -= dt;
        if (bubble_wait_ <= 0) {
            if (bubble_more_ <= 0) {
                bubble_more_ = 1 + pick(4);
                bubble_gap_ = 0;
            }
            bubble_gap_ -= dt;
            if (bubble_gap_ <= 0) {
                bubble();
                --bubble_more_;
                bubble_gap_ = 0.05 + 0.12 * uniform();
                if (bubble_more_ <= 0)
                    bubble_wait_ = 4 + 7 * uniform();
            }
        }
        if (bubble_env_ > 1e-4) {
            bubble_hz_ *= bubble_rise_;
            bubble_phase_ += bubble_hz_ * dt;
            bubble_env_ *= 0.99965;
            const double v = std::sin(2 * pi * bubble_phase_) * bubble_env_ * 0.05 * part_gain(4);
            left += v * (1 - bubble_pan_);
            right += v * (1 + bubble_pan_);
            send += v * 0.5;
        }
        // the spring: dispersion, then two damped loops
        double x = send;
        for (int k = 0; k < 4; ++k) {
            std::vector<float>& line = allpass_[k];
            const double held = line[static_cast<std::size_t>(allpass_at_[k])];
            const double out = held - 0.62 * x;
            line[static_cast<std::size_t>(allpass_at_[k])] = static_cast<float>(x + 0.62 * out);
            allpass_at_[k] = (allpass_at_[k] + 1) % static_cast<int>(line.size());
            x = out;
        }
        double wet[2]{};
        for (int k = 0; k < 2; ++k) {
            std::vector<float>& line = spring_[k];
            const double held = line[static_cast<std::size_t>(spring_at_[k])];
            spring_low_[k] += (held - spring_low_[k]) * 0.35;
            line[static_cast<std::size_t>(spring_at_[k])] = static_cast<float>(x + spring_low_[k] * 0.7);
            spring_at_[k] = (spring_at_[k] + 1) % static_cast<int>(line.size());
            wet[k] = held;
        }
        left += wet[0] * 0.32;
        right += wet[1] * 0.32;
        // the tape: a slowly wandering delay (wow and flutter)
        tape_[0][static_cast<std::size_t>(tape_at_)] = static_cast<float>(left);
        tape_[1][static_cast<std::size_t>(tape_at_)] = static_cast<float>(right);
        wow_phase_ += 0.55 * dt;
        flutter_phase_ += 7.3 * dt;
        const double lag = 240 + 22 * std::sin(2 * pi * wow_phase_) + 2.5 * std::sin(2 * pi * flutter_phase_);
        const double at = tape_at_ - lag;
        const double floor_at = std::floor(at);
        const double frac = at - floor_at;
        const int i0 = (static_cast<int>(floor_at) + 1024 * 4) % 1024;
        const int i1 = (i0 + 1) % 1024;
        tape_at_ = (tape_at_ + 1) % 1024;
        double side[2]{};
        for (int k = 0; k < 2; ++k) {
            double v = tape_[k][static_cast<std::size_t>(i0)] * (1 - frac) + tape_[k][static_cast<std::size_t>(i1)] * frac;
            // its band: no deep lows, no air at the top
            hp_[k] += (v - hp_[k]) * hp;
            v -= hp_[k];
            lp1_[k] += (v - lp1_[k]) * lp;
            lp2_[k] += (lp1_[k] - lp2_[k]) * lp;
            side[k] = lp2_[k];
        }
        // nearly mono, as the old records were, a little saturated, a little hiss
        hiss_low_ += ((uniform() * 2 - 1) - hiss_low_) * 0.3;
        const double mid = (side[0] + side[1]) * 0.5;
        const double wide = (side[0] - side[1]) * 0.5 * 0.45;
        // the tape only rounds the loudest peaks: driven harder it buzzes like a saw
        const double scale = 2.4 * loud_;
        const double out_left = std::tanh((mid + wide) * scale * 0.45) / 0.45 + hiss_low_ * 0.0015;
        const double out_right = std::tanh((mid - wide) * scale * 0.45) / 0.45 + hiss_low_ * 0.0015;
        stereo[frame] += static_cast<float>(out_left * gain);
        stereo[frame + 1] += static_cast<float>(out_right * gain);
    }
}

} // namespace sw
