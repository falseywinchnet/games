#include "glass_music.hpp"

#include <algorithm>
#include <cmath>

namespace ps_cube {
namespace {

// The modes it plays in, all calm: two bright, one folk, one gently minor.
constexpr Mode modes[4] = {{"lydian", {0, 2, 4, 6, 7, 9, 11}},
                           {"ionian", {0, 2, 4, 5, 7, 9, 11}},
                           {"mixolydian", {0, 2, 4, 5, 7, 9, 10}},
                           {"dorian", {0, 2, 3, 5, 7, 9, 10}}};

// Chord shapes as stacks of scale steps above the root.
constexpr int stacks[7][4] = {{0, 2, 4, -1},  // triad
                              {0, 1, 4, -1},  // sus2
                              {0, 3, 4, -1},  // sus4
                              {0, 2, 4, 8},   // add9
                              {0, 2, 4, 5},   // six
                              {0, 2, 4, 6},   // seventh
                              {0, 4, 7, 8}};  // open fifth, octave and ninth
enum Shape { triad, sus2, sus4, add9, six, seventh, open };

// Shapes moved in parallel, in semitones: fifths and octaves, and stacked fourths.
constexpr int planes[2][3] = {{0, 7, 12}, {0, 5, 10}};

// Heartbeat patterns: (sixteenth, strength), strength 0 ends.
constexpr double heartbeats[4][5][2] = {{{0, 1.0}, {3, .55}, {8, .8}, {11, .45}, {0, 0}},
                                        {{0, 1.0}, {6, .6}, {10, .7}, {0, 0}, {0, 0}},
                                        {{0, .9}, {8, .6}, {0, 0}, {0, 0}, {0, 0}},
                                        {{0, 1.0}, {2, .5}, {8, .85}, {14, .4}, {0, 0}}};

// Motif rhythms over two bars: (start, length) in sixteenths, length 0 ends.
constexpr int rhythms[8][5][2] = {{{0, 12}, {12, 4}, {16, 16}, {0, 0}, {0, 0}},
                                  {{4, 8}, {12, 8}, {20, 12}, {0, 0}, {0, 0}},
                                  {{0, 6}, {6, 6}, {12, 4}, {16, 16}, {0, 0}},
                                  {{8, 8}, {16, 8}, {24, 8}, {0, 0}, {0, 0}},
                                  {{0, 16}, {20, 4}, {24, 8}, {0, 0}, {0, 0}},
                                  {{2, 6}, {8, 8}, {16, 4}, {20, 12}, {0, 0}},
                                  {{0, 8}, {8, 4}, {12, 4}, {16, 6}, {22, 10}},
                                  {{4, 4}, {8, 12}, {24, 8}, {0, 0}, {0, 0}}};

// How the lead uses a section's two bars at a time: motif*4 + variation, -1 rests.
constexpr int lead_plans[6][4] = {{0, -1, 1, -1}, {0, 1, -1, 4}, {-1, 0, -1, 3},
                                  {0, 4, 2, -1},  {0, -1, 4, -1}, {-1, 4, 0, -1}};

int floor_div(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

} // namespace

GlassMusic::GlassMusic(std::uint32_t seed, Harmony* harmony) : dice_(seed), harmony_(harmony) {
    // Home keys sit where the bass and pads speak best: C most often, then F, G or D.
    const int homes[4] = {0, 5, 7, 2};
    tonic_ = homes[dice_.weighted(std::array<double, 4>{4, 2, 2, 1})];
    mode_ = dice_.weighted(std::array<double, 4>{4, 4, 1.5, .5});
    tempo_ = dice_.range(84, 92);
    energy_ = dice_.range(.3, .55);
    bird_wait_ = dice_.range(8, 30);
    bass_filter_.set(200, .7);
    click_filter_.set(1600, .7);
    for (int c = 0; c < 2; ++c) {
        pad_low_[c].set(1000, .6);
        pad_high_[c].set(150, .7);
    }
    kind_ = breath;
    bar_ = -1;
    sixteenth_ = 15;
    clock_ = 0;
    fade_in(4);
    plan_section();
    bar_ = -1;
}

void GlassMusic::fade_in(double seconds) noexcept {
    fade_ = 0;
    fade_rate_ = 1.0 / (seconds * sample_rate);
}

int GlassMusic::scale_pc(int degree) const {
    const int octave = floor_div(degree, 7);
    return modes[mode_].steps[degree - 7 * octave] + 12 * octave;
}

int GlassMusic::degree_midi(int degree) const {
    const int base = tonic_ <= 4 ? 72 + tonic_ : 60 + tonic_;
    return base + scale_pc(degree);
}

bool GlassMusic::usable(int degree) const {
    const int third = scale_pc(degree + 2) - scale_pc(degree);
    const int fifth = scale_pc(degree + 4) - scale_pc(degree);
    return (third == 3 || third == 4) && fifth == 7;
}

GlassMusic::Chord GlassMusic::diatonic(int degree, int shape) const {
    Chord chord{};
    chord.root = ((scale_pc(degree) % 12) + 12) % 12;
    chord.count = 0;
    for (int k = 0; k < 4; ++k) {
        if (stacks[shape][k] < 0)
            break;
        chord.tones[chord.count++] = scale_pc(degree + stacks[shape][k]) - scale_pc(degree);
    }
    return chord;
}

GlassMusic::Chord GlassMusic::planed(int root, int shape) const {
    Chord chord{};
    chord.root = ((root % 12) + 12) % 12;
    chord.count = 3;
    for (int k = 0; k < 3; ++k)
        chord.tones[k] = planes[shape][k];
    return chord;
}

bool GlassMusic::chord_has(int pc) const {
    for (int k = 0; k < current_.count; ++k)
        if ((current_.root + current_.tones[k]) % 12 == ((pc % 12) + 12) % 12)
            return true;
    return false;
}

int GlassMusic::chord_mask() const noexcept {
    int mask = 0;
    for (int k = 0; k < current_.count; ++k)
        mask |= 1 << ((tonic_ + current_.root + current_.tones[k]) % 12);
    return mask;
}

void GlassMusic::publish() {
    if (!harmony_)
        return;
    int scale = 0;
    for (int k = 0; k < 7; ++k)
        scale |= 1 << ((tonic_ + modes[mode_].steps[k]) % 12);
    (*harmony_).tonic.store(tonic_, std::memory_order_relaxed);
    (*harmony_).scale.store(scale, std::memory_order_relaxed);
    (*harmony_).chord.store(chord_mask(), std::memory_order_relaxed);
}

void GlassMusic::change_key() {
    const int moves[7] = {5, -5, 7, -7, 2, -2, 0};
    const int move = moves[dice_.weighted(std::array<double, 7>{3, 3, 2, 2, 1.5, 1.5, 2})];
    const int old_mode = mode_;
    tonic_ = ((tonic_ + move) % 12 + 12) % 12;
    mode_ = dice_.weighted(std::array<double, 4>{4, 4, 1.5, .5});
    if (move == 0 && mode_ == old_mode)
        mode_ = (mode_ + 1 + dice_.pick(3)) % 4;
}

void GlassMusic::plan_section() {
    ++sections_;
    ++key_sections_;
    int next = kind_;
    if (kind_ == glow) {
        next = dice_.uniform() < .65 ? pedal : pulse;
    } else if (kind_ == pedal) {
        // a drone, then the pulse, as the original's two halves; a breath now and then
        next = dice_.weighted(std::array<double, 3>{.12, .73, .15});
    } else if (kind_ == pulse) {
        next = dice_.weighted(std::array<double, 3>{.75, .1, .15});
    } else {
        next = dice_.weighted(std::array<double, 3>{.6, .4, 0});
    }
    if (key_sections_ >= key_due_) {
        change_key();
        key_sections_ = 0;
        key_due_ = 8 + dice_.pick(7);
        if (next == pulse)
            next = pedal;
    }
    kind_ = next;
    energy_ = std::clamp(energy_ + dice_.range(-.22, .22), .15, .85);
    if (kind_ == pulse)
        energy_ = std::max(energy_, .45);
    if (kind_ == breath)
        energy_ = std::min(energy_, .35);
    tempo_ = std::clamp(tempo_ + dice_.range(-2.5, 2.5), 82.0, 95.0);
    if (kind_ == pedal)
        plan_pedal();
    else if (kind_ == pulse)
        plan_pulse();
    else
        plan_breath();
    plan_lead();
}

void GlassMusic::plan_pedal() {
    length_ = 8;
    const int bass_low = 36 + tonic_;
    const bool major = mode_ <= 2;
    const int tonic_shapes[5] = {add9, sus2, six, triad, seventh};
    // Colours over the drone: what the other degrees bring against the tonic.
    int options[6][2] = {};
    int count = 0;
    const int degrees[5] = {1, 3, 5, 6, 4};
    for (int d : degrees) {
        if (d == 4) {
            options[count][0] = 4;
            options[count][1] = dice_.uniform() < .5 ? sus4 : sus2;
            ++count;
        } else if (usable(d)) {
            options[count][0] = d;
            options[count][1] = dice_.pick(3) == 0 ? triad : (dice_.uniform() < .5 ? add9 : sus2);
            ++count;
        }
    }
    const int first = dice_.pick(count);
    int second = dice_.pick(count);
    if (second == first)
        second = (second + 1) % count;
    const Chord home = diatonic(0, tonic_shapes[dice_.pick(major ? 5 : 4)]);
    chords_[0] = chords_[1] = home;
    chords_[2] = chords_[3] = diatonic(options[first][0], options[first][1]);
    chords_[4] = chords_[5] = diatonic(options[second][0], options[second][1]);
    chords_[6] = chords_[7] =
        dice_.uniform() < .55 ? diatonic(0, tonic_shapes[dice_.pick(major ? 5 : 4)]) : home;
    for (int b = 0; b < length_; ++b)
        bass_root_[b] = bass_low;
    bass_style_ = dice_.pick(3);
    beat_style_ = energy_ > .55 && dice_.uniform() < .5 ? 2 : -1;
}

void GlassMusic::plan_pulse() {
    length_ = 8;
    const int openings[4] = {4, 3, 5, 1};
    int degree = openings[dice_.weighted(std::array<double, 4>{3, 3, 2, usable(1) ? 2.0 : 0.0})];
    int path[8]{};
    for (int b = 0; b < 7; ++b) {
        path[b] = degree;
        const int steps[5] = {-1, 1, -2, 2, 0};
        int step = steps[dice_.weighted(std::array<double, 5>{.32, .28, .1, .1, .2})];
        int next = degree + step;
        if (next < 1 || next > 6)
            next = degree - step;
        degree = std::clamp(next, 1, 6);
    }
    const int ends[4] = {3, 4, 6, 1};
    std::array<double, 4> end_weight{3, 2, mode_ >= 2 ? 3.0 : 0.0, mode_ == 0 ? 2.0 : 0.0};
    path[7] = ends[dice_.weighted(end_weight)];
    int roots[8]{};
    for (int b = 0; b < 8; ++b)
        roots[b] = scale_pc(path[b]);
    // Now and then a chord slides through the semitone between its neighbours.
    if (dice_.uniform() < .3) {
        for (int b = 2; b < 6; ++b)
            if (roots[b + 1] - roots[b - 1] == 2 || roots[b + 1] - roots[b - 1] == -2) {
                roots[b] = (roots[b - 1] + roots[b + 1]) / 2;
                break;
            }
    }
    const int shape = dice_.pick(4);
    for (int b = 0; b < 8; ++b) {
        bool diatonic_root = false;
        for (int k = 0; k < 7; ++k)
            if (modes[mode_].steps[k] == ((roots[b] % 12) + 12) % 12)
                diatonic_root = true;
        if (shape < 2 || !diatonic_root)
            chords_[b] = planed(roots[b], shape < 2 ? shape : 0);
        else
            chords_[b] = diatonic(path[b], shape == 2 ? sus2 : add9);
        int bass = 36 + ((tonic_ + roots[b]) % 12 + 12) % 12;
        if (bass < 38)
            bass += 12;
        bass_root_[b] = bass;
    }
    bass_style_ = 3 + dice_.pick(3);
    beat_style_ = energy_ > .4 ? dice_.pick(4) : -1;
}

void GlassMusic::plan_breath() {
    length_ = dice_.uniform() < .5 ? 4 : 8;
    const int choice = dice_.pick(3);
    Chord chord = diatonic(0, add9);
    if (choice == 1 && usable(3))
        chord = diatonic(3, add9);
    if (choice == 2 && usable(5))
        chord = diatonic(5, seventh);
    for (int b = 0; b < length_; ++b) {
        chords_[b] = chord;
        bass_root_[b] = 36 + tonic_;
    }
    if (length_ == 8 && dice_.uniform() < .5)
        for (int b = 4; b < 8; ++b)
            chords_[b] = diatonic(0, sus2);
    bass_style_ = dice_.uniform() < .5 ? 6 : 7;
    beat_style_ = -1;
}

void GlassMusic::plan_glow() {
    kind_ = glow;
    length_ = 8;
    const Chord home = diatonic(0, mode_ <= 2 ? add9 : sus2);
    for (int b = 0; b < length_; ++b) {
        chords_[b] = home;
        bass_root_[b] = 36 + tonic_;
    }
    bass_style_ = 1;
    beat_style_ = -1;
    for (int &slot : lead_plan_)
        slot = -1;
    energy_ = .25;
}

void GlassMusic::new_motif(Motif& motif) {
    const int rhythm = dice_.pick(8);
    const int contour = dice_.pick(4);  // rise, fall, arch, hover
    const int starts[5] = {2, 4, 7, 4, 0};
    int degree = starts[dice_.pick(5)];
    motif.count = 0;
    for (int k = 0; k < 5 && rhythms[rhythm][k][1] > 0; ++k) {
        Note note{};
        note.start = rhythms[rhythm][k][0];
        note.length = rhythms[rhythm][k][1];
        note.degree = degree;
        motif.notes[motif.count++] = note;
        std::array<double, 5> weights{1, 2, 2, 1, .6};  // -2 -1 +1 +2 0
        if (contour == 0 || (contour == 2 && k < 2))
            weights = {.3, .8, 3, 1.4, .4};
        if (contour == 1 || (contour == 2 && k >= 2))
            weights = {1.4, 3, .8, .3, .4};
        const int steps[5] = {-2, -1, 1, 2, 0};
        degree = std::clamp(degree + steps[dice_.weighted(weights)], -3, 10);
    }
}

void GlassMusic::plan_lead() {
    for (int &slot : lead_plan_)
        slot = -1;
    const double chance = kind_ == pedal ? .85 : kind_ == pulse ? .75 : .5;
    if (dice_.uniform() >= chance)
        return;
    if (memory_count_ > 0 && dice_.uniform() < .35)
        motifs_[0] = memory_[dice_.pick(std::min(memory_count_, 4))];
    else
        new_motif(motifs_[0]);
    new_motif(motifs_[1]);
    memory_[memory_count_ % 4] = motifs_[1];
    ++memory_count_;
    const int plan = dice_.pick(6);
    const int slots = length_ / 2;
    for (int s = 0; s < slots && s < 4; ++s)
        lead_plan_[s] = lead_plans[plan][s];
    if (slots == 2 && lead_plan_[0] < 0 && lead_plan_[1] < 0)
        lead_plan_[1] = 0;
}

void GlassMusic::play_chord(const Chord& chord, double attack) {
    current_ = chord;
    // The root sits where it moves least; the other tones stack above it.
    int root = 48 + (tonic_ + chord.root) % 12;
    if (root < 52)
        root += 12;
    if (pad_count_ > 0 && std::abs(root + 12 - last_pad_[0]) < std::abs(root - last_pad_[0]) &&
        root + 12 <= 64)
        root += 12;
    int notes[4]{};
    for (int k = 0; k < chord.count; ++k)
        notes[k] = root + chord.tones[k];
    pad_count_ = chord.count;
    for (int k = 0; k < chord.count; ++k)
        last_pad_[k] = notes[k];
    for (PadVoice& voice : pad_) {
        bool kept = false;
        for (int k = 0; k < chord.count; ++k)
            if (voice.target > 0 && voice.midi == notes[k])
                kept = true;
        if (!kept)
            voice.target = 0;
    }
    for (int k = 0; k < chord.count; ++k) {
        bool sounding = false;
        for (PadVoice& voice : pad_)
            if (voice.target > 0 && voice.midi == notes[k])
                sounding = true;
        if (sounding)
            continue;
        PadVoice* chosen = &pad_[0];
        for (PadVoice& voice : pad_)
            if (voice.target == 0 && voice.level < (*chosen).level)
                chosen = &voice;
        PadVoice& voice = *chosen;
        voice.midi = notes[k];
        const double hz = hz_of(notes[k]);
        const double cents = dice_.range(5, 9);
        voice.inc[0] = hz * std::pow(2.0, cents / 1200) / sample_rate;
        voice.inc[1] = hz * std::pow(2.0, -cents / 1200) / sample_rate;
        voice.phase[0] = dice_.uniform();
        voice.phase[1] = dice_.uniform();
        voice.target = 1;
        voice.attack = rate_for(attack * dice_.range(.8, 1.2));
        voice.release = rate_for(1.4);
        voice.pan = (k - (chord.count - 1) * .5) * .3;
    }
    publish();
}

void GlassMusic::bass_note(int midi, int sixteenths) {
    const double hz = hz_of(midi);
    const double samples = sixteenths * 15.0 / tempo_ * sample_rate;
    // A sounding bass dips briefly before the next note rather than cutting off.
    bass_next_inc_ = hz / sample_rate;
    if (bass_level_ > .05)
        bass_dip_ = static_cast<int>(.014 * sample_rate);
    else
        bass_inc_ = bass_next_inc_;
    bass_gate_ = static_cast<int>(samples);
    bass_target_ = 1;
    bass_filter_.set(hz * 3.2, .75);
}

void GlassMusic::lead_note(const Note& note, int variation) {
    int degree = note.degree;
    if (variation == 1)
        degree += 1;
    if (variation == 2)
        degree -= 2;
    int midi = degree_midi(degree);
    // On the strong beats the line lands on the chord.
    if (note.start % 8 == 0 && !chord_has(midi - tonic_)) {
        const int tries[4] = {-1, 1, -2, 2};
        for (int t : tries)
            if (chord_has(degree_midi(degree + t) - tonic_)) {
                midi = degree_midi(degree + t);
                break;
            }
    }
    while (midi > 88)
        midi -= 12;
    while (midi < 65)
        midi += 12;
    LeadVoice& voice = lead_[next_lead_];
    next_lead_ = (next_lead_ + 1) % 2;
    voice.inc = hz_of(midi) / sample_rate;
    voice.target = 1;
    voice.attack = rate_for(dice_.range(.12, .28));
    voice.release = rate_for(.7);
    voice.gate = static_cast<int>(note.length * 15.0 / tempo_ * sample_rate * .92);
    voice.age = 0;
    voice.pan = next_lead_ ? .22 : -.18;
    voice.tremolo = dice_.uniform();
}

void GlassMusic::bell(int midi, double level, int delay) {
    BellVoice& voice = bell_[next_bell_];
    next_bell_ = (next_bell_ + 1) % static_cast<int>(bell_.size());
    const double hz = hz_of(midi);
    const double ratios[3] = {1.0, 2.76, 5.40};
    const double amps[3] = {1.0, .2, .07};
    const double decays[3] = {1.6, .55, .22};
    for (int p = 0; p < 3; ++p) {
        voice.phase[p] = 0;
        voice.inc[p] = std::min(hz * ratios[p], sample_rate * .45) / sample_rate;
        voice.amp[p] = amps[p] * level;
        voice.decay[p] = decay_for(decays[p]);
    }
    voice.pan = dice_.range(-.55, .55);
    voice.delay = delay;
    voice.on = true;
}

void GlassMusic::beat(double level) {
    BeatVoice& voice = beat_[next_beat_];
    next_beat_ = (next_beat_ + 1) % 2;
    voice.on = true;
    voice.phase = 0;
    voice.freq = 46;
    voice.level = level;
    voice.click = level * .25;
}

void GlassMusic::bird_phrase() {
    const int kind = dice_.pick(3);
    const double pan = dice_.range(-.7, .7);
    const double base = dice_.range(3000, 4200);
    int at = 0;
    int count = kind == 0 ? 4 + dice_.pick(4) : kind == 1 ? 8 + dice_.pick(6) : 2;
    count = std::min(count, static_cast<int>(chirps_.size()));
    for (int k = 0; k < count; ++k) {
        Chirp& chirp = chirps_[k];
        chirp.delay = at;
        chirp.age = 0;
        chirp.pan = pan;
        if (kind == 0) {          // arched chirps
            chirp.length = static_cast<int>(dice_.range(.06, .1) * sample_rate);
            chirp.from = base;
            chirp.to = base * dice_.range(.92, 1.0);
            chirp.peak = base * dice_.range(1.06, 1.14);
            chirp.level = dice_.range(.6, 1);
            at += static_cast<int>(dice_.range(.15, .2) * sample_rate);
        } else if (kind == 1) {   // a trill of falling flicks
            chirp.length = static_cast<int>(dice_.range(.028, .04) * sample_rate);
            chirp.from = base * 1.15;
            chirp.to = base * .75;
            chirp.peak = (chirp.from + chirp.to) * .5;
            chirp.level = .8 * (1 - .4 * k / count);
            at += static_cast<int>(dice_.range(.055, .07) * sample_rate);
        } else {                  // a two-note whistle
            chirp.length = static_cast<int>((k ? .17 : .26) * sample_rate);
            chirp.from = base * (k ? .8 : .9);
            chirp.to = base * (k ? .78 : .98);
            chirp.peak = (chirp.from + chirp.to) * .5;
            chirp.level = .7;
            at += static_cast<int>(.36 * sample_rate);
        }
    }
    for (int k = count; k < static_cast<int>(chirps_.size()); ++k)
        chirps_[k].length = 0;
}

void GlassMusic::cue(Cue cue) noexcept {
    if (cue == Cue::connect) {
        bright_ = std::min(1.0, bright_ + .5);
        sparkle_ = 3 + dice_.pick(2);
    } else if (cue == Cue::win) {
        // Resolve now: the tonic in the pads and the bass, the line stops, the beat rests.
        plan_glow();
        bar_ = -1;
        bright_ = 1;
        sparkle_ = 0;
        play_chord(chords_[0], 1.2);
        bass_note(36 + tonic_, 32);
        for (LeadVoice& voice : lead_)
            voice.gate = 0;
    } else if (cue == Cue::level) {
        restart_ = true;
    }
}

void GlassMusic::step_sixteenth() {
    ++sixteenth_;
    if (sixteenth_ >= 16) {
        sixteenth_ = 0;
        ++bar_;
        if (restart_) {
            restart_ = false;
            if (kind_ == glow)
                kind_ = breath;
            if (dice_.uniform() < .5)
                key_sections_ = key_due_;
            plan_section();
            bar_ = 0;
        } else if (bar_ >= length_) {
            plan_section();
            bar_ = 0;
        }
        const Chord& chord = chords_[bar_];
        bool same = chord.root == current_.root && chord.count == current_.count;
        for (int k = 0; same && k < chord.count; ++k)
            same = chord.tones[k] == current_.tones[k];
        if (!same || bar_ == 0)
            play_chord(chord, kind_ == pulse ? .5 : 1.3);
        const double bar_seconds = 240.0 / tempo_;
        bird_wait_ -= bar_seconds;
        if (bird_wait_ <= 0) {
            bird_wait_ = dice_.range(20, 55);
            if ((kind_ != pulse || energy_ < .5) && dice_.uniform() < .7)
                bird_phrase();
        }
    }
    const int root = bass_root_[bar_];
    const int s = sixteenth_;
    switch (bass_style_) {
    case 0:
        if (s == 0)
            bass_note(root + (bar_ % 2 ? 12 : 0), 16);
        break;
    case 1:
        if (s == 0 && bar_ % 2 == 0)
            bass_note(root, 32);
        if (s == 14 && bar_ % 2 == 1)
            bass_note(root, 2);
        break;
    case 2:
        if (s == 0)
            bass_note(root, 16);
        if (s == 8 && bar_ % 2 == 1)
            bass_note(root + 12, 8);
        break;
    case 3:
        if (s == 0 || s == 8)
            bass_note(root, 5);
        break;
    case 4:
        if (s == 0)
            bass_note(root, 5);
        if (s == 6)
            bass_note(root, 4);
        if (s == 12)
            bass_note(root, 3);
        break;
    case 5:
        if (s == 0)
            bass_note(root, 7);
        if (s == 10)
            bass_note(root, 2);
        if (s == 12)
            bass_note(root + 12, 3);
        break;
    case 7:
        if (s == 0)
            bass_note(root, 16);
        break;
    default:
        break;
    }
    if (beat_style_ >= 0)
        for (int k = 0; k < 5 && heartbeats[beat_style_][k][1] > 0; ++k)
            if (static_cast<int>(heartbeats[beat_style_][k][0]) == s)
                beat(heartbeats[beat_style_][k][1] * (.55 + .45 * energy_));
    if (bar_ >= 0) {
        const int slot = bar_ / 2;
        const int plan = slot < 4 ? lead_plan_[slot] : -1;
        if (plan >= 0) {
            const Motif& motif = motifs_[plan / 4];
            const int at = (bar_ % 2) * 16 + s;
            for (int k = 0; k < motif.count; ++k)
                if (motif.notes[k].start == at) {
                    Note note = motif.notes[k];
                    if (plan % 4 == 3 && k == motif.count - 1)
                        note.degree += note.degree > 3 ? -1 : 1;
                    lead_note(note, plan % 4 == 3 ? 0 : plan % 4);
                }
        }
    }
    if (sparkle_ > 0 && s % 2 == 0) {
        // A rising handful of the chord's own notes, high and soft.
        const int step = 4 - sparkle_;
        const int k = std::clamp(step, 0, 3) % std::max(1, current_.count);
        int midi = 72 + (tonic_ + current_.root + current_.tones[k]) % 12 + 12 * (step / 3);
        if (midi < 74)
            midi += 12;
        bell(std::min(midi, 96), .28 + .06 * step, 0);
        --sparkle_;
    }
}

double GlassMusic::part(int which) const noexcept {
    return solo_ < 0 || solo_ == which ? 1.0 : 0.0;
}

void GlassMusic::render_add(std::span<float> stereo, double gain) noexcept {
    const std::size_t frames = stereo.size() / 2;
    const double pad_gain = .085 * part(pads), bass_gain = .24 * part(bass), lead_gain = .09 * part(lead),
                 bell_gain = .06 * part(bells), beat_gain = .2 * part(heartbeat),
                 bird_gain = .022 * part(birds), breeze_gain = .016 * part(breeze);
    const double lead_vib = 2 * pi * 4.7 / sample_rate, lead_trem = 1.1 / sample_rate;
    std::size_t frame = 0;
    while (frame < frames) {
        // Block-rate controls.
        const std::size_t block = std::min<std::size_t>(64, frames - frame);
        bright_ *= std::pow(decay_for(9.0), static_cast<double>(block));
        const double cutoff = 1700 + 1100 * energy_ + 2200 * bright_;
        for (int c = 0; c < 2; ++c) {
            pad_low_[c].set(cutoff, .62);
            breeze_[c].set(3000 + 1400 * sine_.at(wrap(breeze_phase_ + c * .37)), 1.6);
        }
        for (std::size_t i = 0; i < block; ++i, ++frame) {
            clock_ -= 1;
            if (clock_ <= 0) {
                step_sixteenth();
                clock_ += 15.0 / tempo_ * sample_rate;
            }
            double left = 0, right = 0, send = 0;
            // pads
            double pl = 0, pr = 0;
            for (PadVoice& voice : pad_) {
                if (voice.target == 0 && voice.level < 1e-5)
                    continue;
                voice.level = approach(voice.level, voice.target,
                                       voice.target > voice.level ? voice.attack : voice.release);
                const double a = saw(voice.phase[0], voice.inc[0]);
                const double b = saw(voice.phase[1], voice.inc[1]);
                const double side = .5 + .5 * voice.pan;
                pl += voice.level * (a * side + b * (1 - side));
                pr += voice.level * (a * (1 - side) + b * side);
            }
            pl = pad_low_[0].low(pl);
            pr = pad_low_[1].low(pr);
            pl -= pad_high_[0].low(pl);
            pr -= pad_high_[1].low(pr);
            // the chorus: a slow pair of moving delays, crossed
            chorus_lfo_ += .105 / sample_rate;
            if (chorus_lfo_ >= 1)
                chorus_lfo_ -= 1;
            chorus_[0][chorus_write_] = static_cast<float>(pl);
            chorus_[1][chorus_write_] = static_cast<float>(pr);
            double wet[2];
            for (int c = 0; c < 2; ++c) {
                const double delay =
                    (.013 + .0045 * sine_.at(wrap(chorus_lfo_ + c * .31))) * sample_rate;
                double read = chorus_write_ - delay;
                if (read < 0)
                    read += 4096;
                const int r0 = static_cast<int>(read);
                const double f = read - r0;
                wet[c] = chorus_[c][r0] + (chorus_[c][(r0 + 1) & 4095] - chorus_[c][r0]) * f;
            }
            chorus_write_ = (chorus_write_ + 1) & 4095;
            const double padl = (pl * .85 + wet[1] * .3 + wet[0] * .12) * pad_gain;
            const double padr = (pr * .85 + wet[0] * .3 + wet[1] * .12) * pad_gain;
            left += padl;
            right += padr;
            send += (padl + padr) * .35;
            // bass
            if (bass_gate_ > 0 && --bass_gate_ == 0)
                bass_target_ = 0;
            bass_level_ = approach(bass_level_, bass_target_, bass_target_ > bass_level_ ? .0018 : .0004);
            if (bass_dip_ > 0 && --bass_dip_ == 0)
                bass_inc_ = bass_next_inc_;
            bass_duck_ = approach(bass_duck_, bass_dip_ > 0 ? .2 : 1.0, .005);
            if (bass_level_ > 1e-5) {
                const double raw = saw(bass_phase_, bass_inc_);
                bass_sub_ = wrap(bass_sub_ + bass_inc_);
                const double tone =
                    (bass_filter_.low(raw) + .35 * sine_.at(bass_sub_)) * bass_level_ * bass_duck_ * bass_gain;
                left += tone;
                right += tone;
                send += tone * .08;
            }
            // lead
            for (LeadVoice& voice : lead_) {
                if (voice.gate > 0 && --voice.gate == 0)
                    voice.target = 0;
                if (voice.target == 0 && voice.level < 1e-5)
                    continue;
                voice.level = approach(voice.level, voice.target,
                                       voice.target > voice.level ? voice.attack : voice.release);
                voice.age += 1.0 / sample_rate;
                voice.vibrato += lead_vib;
                if (voice.vibrato > 2 * pi)
                    voice.vibrato -= 2 * pi;
                voice.tremolo = wrap(voice.tremolo + lead_trem);
                const double depth = std::min(1.0, voice.age * 1.6) * .004;
                voice.phase = wrap(voice.phase + voice.inc * (1 + depth * std::sin(voice.vibrato)));
                const double tone = sine_.at(voice.phase) + .16 * sine_.at(wrap(voice.phase * 2)) +
                                    .05 * sine_.at(wrap(voice.phase * 3));
                const double v = tone * voice.level * (1 - .16 * (.5 + .5 * sine_.at(voice.tremolo))) * lead_gain;
                left += v * (.5 - .5 * voice.pan) * 1.4;
                right += v * (.5 + .5 * voice.pan) * 1.4;
                send += v * .6;
            }
            // bells
            for (BellVoice& voice : bell_) {
                if (!voice.on)
                    continue;
                if (voice.delay > 0) {
                    --voice.delay;
                    continue;
                }
                double v = 0;
                for (int p = 0; p < 3; ++p) {
                    voice.phase[p] = wrap(voice.phase[p] + voice.inc[p]);
                    v += voice.amp[p] * sine_.at(voice.phase[p]);
                    voice.amp[p] *= voice.decay[p];
                }
                if (voice.amp[0] < 1e-4)
                    voice.on = false;
                v *= bell_gain;
                left += v * (.5 - .5 * voice.pan) * 1.4;
                right += v * (.5 + .5 * voice.pan) * 1.4;
                send += v * .7;
            }
            // heartbeat
            for (BeatVoice& voice : beat_) {
                if (!voice.on)
                    continue;
                voice.phase = wrap(voice.phase + (46 + voice.freq) / sample_rate);
                voice.freq *= .99905;
                const double click = click_filter_.low(dice_.noise() * voice.click);
                const double v = (sine_.at(voice.phase) * voice.level + click) * beat_gain;
                voice.level *= .99985;
                voice.click *= .9965;
                if (voice.level < 1e-4)
                    voice.on = false;
                left += v;
                right += v;
                send += v * .12;
            }
            // birds
            for (Chirp& chirp : chirps_) {
                if (chirp.length <= 0)
                    continue;
                if (chirp.delay > 0) {
                    --chirp.delay;
                    continue;
                }
                const double t = static_cast<double>(chirp.age) / chirp.length;
                const double shape = std::sin(pi * t);
                const double hz = chirp.from + (chirp.to - chirp.from) * t +
                                  (chirp.peak - (chirp.from + chirp.to) * .5) * shape;
                chirp_phase_ = wrap(chirp_phase_ + hz / sample_rate);
                const double v = sine_.at(chirp_phase_) * shape * shape * chirp.level * bird_gain;
                left += v * (.5 - .5 * chirp.pan) * 1.4;
                right += v * (.5 + .5 * chirp.pan) * 1.4;
                send += v * .9;
                if (++chirp.age >= chirp.length)
                    chirp.length = 0;
                break;  // one bird sings at a time
            }
            // a breeze through the glasshouse: two bands of air that wander and swell
            breeze_phase_ = wrap(breeze_phase_ + .047 / sample_rate);
            breeze_swell_ = wrap(breeze_swell_ + .071 / sample_rate);
            if (breeze_gain > 0) {
                const double swell = .55 + .45 * sine_.at(breeze_swell_);
                breeze_[0].low(dice_.noise());
                breeze_[1].low(dice_.noise());
                left += breeze_[0].band() * swell * breeze_gain;
                right += breeze_[1].band() * swell * breeze_gain;
            }
            room_.process(send * .5, left, right);
            // fade, a narrower image (the ensemble sits close, as on the original), DC and
            // a gentle top
            fade_ = std::min(1.0, fade_ + fade_rate_);
            const double master = .72 * gain * fade_ * fade_;
            const double mid = (left + right) * .5, side = (left - right) * .5 * .55;
            double out[2] = {mid + side, mid - side};
            for (int c = 0; c < 2; ++c) {
                dc_[c] += (out[c] - dc_[c]) * .0004;
                out_low_[c] += (out[c] - dc_[c] - out_low_[c]) * .7;
                stereo[frame * 2 + c] += static_cast<float>(out_low_[c] * master);
            }
        }
    }
}

} // namespace ps_cube
