#include "marimba_band.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace games::music {
namespace {

constexpr int maj7 = 0, m7 = 1, dom7 = 2, maj6 = 4, m6 = 5;


// A chord by its place in the key: semitones above the tonic, and its kind.
struct Degree {
    int step;
    int q;
};
constexpr Degree I{0, maj7}, I6{0, maj6}, ii{2, m7}, II7{2, dom7}, iii{4, m7}, IV{5, maj7}, IV7{5, dom7}, iv6{5, m6}, V{7, dom7}, vi{9, m7},
    VI7{9, dom7}, bVII7{10, dom7};
using JPhrase = MarimbaBand::Phrase;
// A phrase's progression (four bars, two halves each) and how it ends.
struct Progression {
    Degree d[8];
    JPhrase ends;
};
constexpr std::array<Progression, 9> progressions = {{
    {{I, I, vi, vi, ii, ii, V, V}, JPhrase::half},
    {{IV, IV, iii, vi, ii, V, I, I}, JPhrase::home},
    {{I, I, VI7, VI7, ii, ii, V, V}, JPhrase::half},
    {{iii, iii, vi, vi, ii, V, vi, vi}, JPhrase::deceptive},
    {{I, I, IV7, IV7, I6, I6, II7, II7}, JPhrase::open},
    {{ii, ii, V, V, I, I, iv6, iv6}, JPhrase::open},
    {{IV, IV, iv6, iv6, iii, vi, ii, V}, JPhrase::half},
    {{I, I, bVII7, bVII7, IV, IV, I, I}, JPhrase::home},
    {{ii, V, iii, VI7, ii, V, I6, I6}, JPhrase::home},
}};
// The motif's rhythms: onsets in eighths across a bar of eight, a little syncopated.
constexpr std::array<std::array<int, 7>, 6> rhythms = {{
    {0, 2, 3, 5, 7, -1, -1},
    {0, 1, 3, 4, 6, -1, -1},
    {1, 3, 4, 6, -1, -1, -1},
    {0, 2, 4, 5, 7, -1, -1},
    {0, 3, 4, 6, 7, -1, -1},
    {2, 3, 5, 6, -1, -1, -1},
}};
// Contours over chord tones (0 root, 1 third, 2 fifth, 3 seventh, 4 ninth, 5 the octave).
constexpr std::array<std::array<int, 7>, 6> contours = {{
    {2, 3, 4, 3, 1, 2, 1},
    {1, 2, 3, 2, 4, 3, 1},
    {4, 3, 2, 1, 2, 3, 2},
    {3, 2, 1, 2, 4, 5, 3},
    {1, 3, 2, 4, 3, 2, 1},
    {2, 1, 3, 2, 4, 3, 2},
}};
// The bossa's two-bar patterns in sixteenths: the comping, and the rim click.
constexpr const char* comp_pattern[2] = {"x..x..x...x..x..", "..x..x...x..x..."};
constexpr const char* rim_pattern[2] = {"x..x..x.....x...", "..x...x..x......"};

// The midi note of a chord tone at or above `floor`.
}  // namespace

namespace {
// A warm, wooden room: a second and a half of tail, each pass round the loop losing its top
// (a one-pole at some 4.6 kHz, .45 a sample).
Room marimba_room() { return Room(1.5, -std::log(1 - .45) * sample_rate / (2 * pi), {1777, 2203, 2687, 3109}); }
}  // namespace

MarimbaBand::MarimbaBand(std::uint32_t seed) : Band(seed * 2654435761U + 0x51A3E7C1U, marimba_room()) {
    base_beat_ = 60.0 / (103.0 + random_.uniform() * 6);
    beat_ = base_beat_;
    const int keys[4] = {7, 0, 2, 5};  // G, C, D or F to begin
    key_ = next_key_ = anchor_ = keys[random_.pick(4)];
    next_bar_ = static_cast<std::int64_t>(sample_rate * .2);
    next_plan_ = next_bar_;
    mix(Instrument::marimba, .3, .3);
    mix(Instrument::comp_marimba, .16, .25);
    mix(Instrument::bass_marimba, .6, .05);
    mix(Instrument::squeeze, .12, .25);
    mix(Instrument::kick, .3, .05);
    mix(Instrument::rim, .07, .12);
    mix(Instrument::hat, .04, .05);
    mix(Instrument::shaker, .045, .05);
    mix(Instrument::conga, .18, .05);
    mix(Instrument::bird, .035, .8);
}

void MarimbaBand::plan() noexcept {
    plan_bar();
    next_plan_ = next_bar_;
}

void MarimbaBand::roll(double beat, Instrument kind, double midi, double vel, double length, double pan) noexcept {
    // a marimba's held note: the mallets alternating some eleven strokes a second, swelling a
    // little and falling away
    const double stroke = .09 / beat_;
    const int strokes = std::max(1, static_cast<int>(length / stroke));
    for (int k = 0; k < strokes; ++k) {
        const double u = static_cast<double>(k) / strokes;
        // each stroke is stilled as the next few land, as the bar's ring is taken up by the roll
        at(beat + k * stroke, kind, midi, vel * (k % 2 ? .78 : .9) * (.8 + .35 * std::sin(pi * std::min(1.0, u * 1.4))),
           k + 1 < strokes ? stroke * 3 : .6, pan + (k % 2 ? .05 : -.05));
    }
}

const Chord& MarimbaBand::chord_at(int bar, double beat) const noexcept {
    return chords_[static_cast<std::size_t>(std::clamp(bar * 2 + (beat >= 2 ? 1 : 0), 0, 7))];
}

double MarimbaBand::third_below(double midi) const noexcept {
    // the scale note a third below in the key: three or four semitones down
    for (int down = 3; down <= 4; ++down) {
        const int pc = ((static_cast<int>(midi) - down - key_) % 12 + 12) % 12;
        for (int s : major_scale)
            if (s == pc) return midi - down;
    }
    return midi - 3;
}

// A new phrase: its progression in the key (or carrying to a new one), and its motif.
void MarimbaBand::plan_phrase() noexcept {
    key_ = next_key_;
    ++key_phrases_;
    // a rest now and then; otherwise a progression, home if it has been a while
    if (phrase_ != Phrase::rest && random_.uniform() < .1) {
        phrase_ = Phrase::rest;
        const Progression& p = progressions[0];
        for (int h = 0; h < 8; ++h) chords_[static_cast<std::size_t>(h)] = {(key_ + p.d[h].step) % 12, static_cast<Quality>(p.d[h].q)};
        chords_[6] = chords_[7] = {key_, Quality::maj7};
    } else if (key_phrases_ >= 3 && random_.uniform() < .5) {
        // travel: the first half here, then a ii-V into a new key
        // to a neighbouring key, never far from where the piece began: the anchor, or a
        // fourth or a fifth either side of it
        static constexpr int near[4] = {0, 5, 7, 2};
        do next_key_ = (anchor_ + near[random_.pick(4)]) % 12;
        while (next_key_ == key_);
        phrase_ = Phrase::travel;
        const Degree first[4] = {I, I, vi, vi};
        for (int h = 0; h < 4; ++h) chords_[static_cast<std::size_t>(h)] = {(key_ + first[h].step) % 12, static_cast<Quality>(first[h].q)};
        chords_[4] = chords_[5] = {(next_key_ + 2) % 12, Quality::m7};
        chords_[6] = chords_[7] = {(next_key_ + 7) % 12, Quality::dom7};
        key_phrases_ = 0;
    } else {
        std::array<int, 9> fits{};
        int count = 0;
        for (int k = 0; k < static_cast<int>(progressions.size()); ++k)
            if (since_home_ < 3 || progressions[static_cast<std::size_t>(k)].ends == Phrase::home) fits[static_cast<std::size_t>(count++)] = k;
        const Progression& p = progressions[static_cast<std::size_t>(fits[static_cast<std::size_t>(random_.pick(count))])];
        for (int h = 0; h < 8; ++h) chords_[static_cast<std::size_t>(h)] = {(key_ + p.d[h].step) % 12, static_cast<Quality>(p.d[h].q)};
        phrase_ = p.ends;
    }
    since_home_ = phrase_ == Phrase::home ? 0 : since_home_ + 1;
    if (--motif_phrases_ <= 0) {
        rhythm_ = rhythms[static_cast<std::size_t>(random_.pick(static_cast<int>(rhythms.size())))];
        rhythm_notes_ = 0;
        while (rhythm_notes_ < 7 && rhythm_[static_cast<std::size_t>(rhythm_notes_)] >= 0) ++rhythm_notes_;
        contour_ = contours[static_cast<std::size_t>(random_.pick(static_cast<int>(contours.size())))];
        motif_phrases_ = 2 + random_.pick(3);
    }
    thirds_ = random_.uniform() < .45;
    squeeze_answer_ = random_.uniform() < .15;
    // variety: now and then the whole band at half time; more often one or two parts cross
    // the others at half their pace
    half_all_ = phrase_ != Phrase::rest && random_.uniform() < .15;
    half_bass_ = half_all_ || random_.uniform() < .2;
    half_comp_ = half_all_ || random_.uniform() < .2;
    half_tune_ = half_all_ || random_.uniform() < .2;
    bar_ = 0;
}

void MarimbaBand::tune_note(double beat, double midi, double vel, double length, Instrument lead, double pan) noexcept {
    const bool long_note = length >= .9 && lead == Instrument::marimba;
    if (long_note) roll(beat, lead, midi, vel, length, pan);
    else at(beat, lead, midi, vel, std::min(length, .45), pan);
    if (thirds_ && lead == Instrument::marimba) {
        const double low = third_below(midi);
        if (long_note) roll(beat + .045 / beat_, lead, low, vel * .7, length, pan - .2);
        else at(beat, lead, low, vel * .7, std::min(length, .45), pan - .2);
    }
}

// One bar of the tune. Bars 0 and 2 state the motif (bar 2 on the new chords); bar 1
// answers it and holds a chord tone; bar 3 closes on a note that suits the cadence: the
// dominant's third or ninth when it ends open, the sixth's root on a deceptive close, the
// home chord's third or fifth (now and then its root) at home.
void MarimbaBand::tune_bar(int bar, Instrument lead, double vel, double pan) noexcept {
    const int floor = lead == Instrument::squeeze ? 62 : 67;
    if (half_tune_ && bar < 3) {
        // the motif at half its pace: bars 0 and 1 carry it once, bar 2 starts it again
        const int first = bar == 1 ? 8 : 0;
        for (int n = 0; n < rhythm_notes_; ++n) {
            const int d = rhythm_[static_cast<std::size_t>(n)] * 2;
            if (d < first || d >= first + 8) continue;
            const int next = n + 1 < rhythm_notes_ ? rhythm_[static_cast<std::size_t>(n + 1)] * 2 : 16;
            tune_note((d - first) * .5, tone_at(bar, floor, (d - first) * .5, contour_[static_cast<std::size_t>(n)]), vel, (next - d) * .5 * .9, lead, pan);
        }
        return;
    }
    if (bar == 0 || bar == 2) {
        for (int n = 0; n < rhythm_notes_; ++n) {
            const int on = rhythm_[static_cast<std::size_t>(n)];
            const int next = n + 1 < rhythm_notes_ ? rhythm_[static_cast<std::size_t>(n + 1)] : 8;
            int index = contour_[static_cast<std::size_t>(n)];
            if (bar == 2 && n == rhythm_notes_ - 2 && random_.uniform() < .4) index = std::min(5, index + 1);
            tune_note(on * .5, tone_at(bar, floor, on * .5, index), vel * (on % 2 == 0 ? 1 : .82), (next - on) * .5, lead, pan);
        }
    } else if (bar == 1) {
        const int keep = std::max(2, rhythm_notes_ / 2);
        for (int n = 0; n < keep && n < rhythm_notes_; ++n)
            tune_note(rhythm_[static_cast<std::size_t>(n)] * .5, tone_at(bar, floor, rhythm_[static_cast<std::size_t>(n)] * .5, contour_[static_cast<std::size_t>(n)]), vel * .9, .45, lead, pan);
        static constexpr int held[3] = {1, 3, 4};
        tune_note(2, tone_at(bar, floor, 2, held[random_.pick(3)]), vel, 1.8, lead, pan);
    } else {
        // the close: a step toward the target from above or below, then the target, held
        const Chord& c = chord_at(bar, 2);
        int target = 1;
        switch (phrase_) {
        case Phrase::half: target = random_.uniform() < .6 ? 1 : 4; break;
        case Phrase::deceptive: target = random_.uniform() < .6 ? 0 : 1; break;
        case Phrase::home: target = random_.uniform() < .25 ? 0 : (random_.uniform() < .5 ? 1 : 2); break;
        case Phrase::open: target = random_.uniform() < .5 ? 1 : 3; break;
        case Phrase::travel: target = random_.uniform() < .5 ? 1 : 4; break;
        case Phrase::rest: break;
        }
        const double goal = tone_of(c, target, floor);
        tune_note(0, tone_at(bar, floor, 0, random_.uniform() < .5 ? 2 : 1), vel * .85, .45, lead, pan);
        tune_note(1, goal + (random_.uniform() < .5 ? 2 : -1), vel * .8, .45, lead, pan);
        tune_note(1.5, goal, vel, 2.3, lead, pan);
    }
}

// One bar: the bass marimba, the comping, the hand drums, and the tune.
void MarimbaBand::plan_bar() noexcept {
    if (bar_ >= 4) plan_phrase();
    const int bar = bar_;
    const int two = static_cast<int>(bars_ % 2);
    // the bass marimba, bossa style: the root on one, the fifth on the "and" of two and on
    // three, a step into the next chord on the "and" of four; at half pace, half notes
    for (int half = 0; half < 2; ++half) {
        const Chord& c = chord_at(bar, half * 2);
        double root = tone_of(c, 0, 33), fifth = tone_of(c, 2, 33);
        if (root > 45) root -= 12;
        if (fifth > 47) fifth -= 12;
        if (half_bass_) {
            at(half * 2, Instrument::bass_marimba, half == 0 || chord_at(bar, 2).root != chord_at(bar, 0).root ? root : fifth, .8, 1.8);
            continue;
        }
        if (half == 0) {
            at(0, Instrument::bass_marimba, root, .85, 1.3);
            at(1.5, Instrument::bass_marimba, fifth, .55, .4);
        } else {
            at(2, Instrument::bass_marimba, chord_at(bar, 2).root != chord_at(bar, 0).root ? root : fifth, .7, 1.3);
            const Chord& n = bar < 3 ? chord_at(bar + 1, 0) : chords_[7];
            double into = tone_of(n, 0, 33) + (random_.uniform() < .5 ? -1 : 2);
            if (into > 46) into -= 12;
            if (random_.uniform() < .6) at(3.5, Instrument::bass_marimba, into, .5, .4);
        }
    }
    // the second marimba comps in rootless voicings: third, fifth, seventh, ninth
    if (phrase_ != Phrase::rest || bar % 2 == 0) {
        const char* pattern = comp_pattern[two];
        for (int s = 0; s < 16; ++s) {
            if (pattern[s] != 'x') continue;
            if (half_comp_ && s % 8 != 0 && s % 8 != 2) continue;
            const double b = s * .25;
            const Chord& c = chord_at(bar, b);
            for (int k = 1; k <= 4; ++k)
                at(b + (k - 1) * .008, Instrument::comp_marimba, tone_of(c, k, 57), .36 - .03 * k, .25, -.3 + .1 * k);
        }
    } else {
        // in a rest, the chord rolled with four mallets, softly
        const Chord& c = chord_at(bar, 0);
        for (int k = 1; k <= 4; ++k) roll(0, Instrument::comp_marimba, tone_of(c, k, 57), .22, 3.6, -.3 + .1 * k);
    }
    // the drums: a rim click on the bossa's pattern, a soft shaker, the kick with the bass,
    // the hi-hat's foot on two and four
    const char* rims = rim_pattern[two];
    for (int s = 0; s < 16; ++s)
        if (rims[s] == 'x' && !(half_all_ && s % 4 != 0)) at(s * .25, Instrument::rim, 0, .5, 0, .15);
    for (int e = 0; e < 8; ++e) at(e * .5, Instrument::shaker, 0, e % 2 ? .3 : .42, 0, .3);
    at(0, Instrument::kick, 0, .6);
    if (!half_all_) at(2, Instrument::kick, 0, .45);
    at(1, Instrument::hat, 0, .4, 0, .3);
    at(3, Instrument::hat, 0, .4, 0, .3);
    if (random_.uniform() < .25) at(3.5, Instrument::conga, 62, .4, 0, -.4);
    // the tune; now and then the squeezebox takes the answer
    if (phrase_ != Phrase::rest) {
        if (bar == 1 && squeeze_answer_) tune_bar(bar, Instrument::squeeze, .5, .25);
        else tune_bar(bar, Instrument::marimba, .75, .12);
    }
    if (random_.uniform() < .1) at(random_.uniform() * 3.5, Instrument::bird, 88 + random_.pick(8), .45, 0, random_.uniform() * 1.6 - .8);
    ++bar_;
    ++bars_;
    next_bar_ += static_cast<std::int64_t>(4 * beat_ * sample_rate);
    // the tempo breathes: a slow wander of a percent or so, and a little ease at a phrase's end
    drift_ = std::clamp(drift_ + (random_.uniform() - .5) * .01, -.02, .02);
    beat_ = base_beat_ * (1 + drift_ + (bar_ == 3 ? .015 : 0));
}

}  // namespace games::music
