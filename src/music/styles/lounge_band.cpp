#include "lounge_band.hpp"

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
using LPhrase = LoungeBand::Phrase;
// A phrase's progression (four bars, two halves each) and how it ends: the marimba band's,
// without the ii-V that carries it into another key.
struct Progression {
    Degree d[8];
    LPhrase ends;
};
constexpr std::array<Progression, 9> progressions = {{
    {{I, I, vi, vi, ii, ii, V, V}, LPhrase::half},
    {{IV, IV, iii, vi, ii, V, I, I}, LPhrase::home},
    {{I, I, VI7, VI7, ii, ii, V, V}, LPhrase::half},
    {{iii, iii, vi, vi, ii, V, vi, vi}, LPhrase::deceptive},
    {{I, I, IV7, IV7, I6, I6, II7, II7}, LPhrase::open},
    {{ii, ii, V, V, I, I, iv6, iv6}, LPhrase::open},
    {{IV, IV, iv6, iv6, iii, vi, ii, V}, LPhrase::half},
    {{I, I, bVII7, bVII7, IV, IV, I, I}, LPhrase::home},
    {{ii, V, iii, VI7, ii, V, I6, I6}, LPhrase::home},
}};
// The motif's rhythms: onsets in eighths across a bar of four (eight eighths)...
constexpr std::array<std::array<int, 7>, 6> rhythms_four = {{
    {0, 2, 3, 5, 7, -1, -1},
    {0, 1, 3, 4, 6, -1, -1},
    {1, 3, 4, 6, -1, -1, -1},
    {0, 2, 4, 5, 7, -1, -1},
    {0, 3, 4, 6, 7, -1, -1},
    {2, 3, 5, 6, -1, -1, -1},
}};
// ...or across the waltz's two measures of three (twelve eighths).
constexpr std::array<std::array<int, 7>, 6> rhythms_waltz = {{
    {0, 4, 6, 8, 10, -1, -1},
    {0, 2, 4, 6, -1, -1, -1},
    {0, 6, 8, 10, -1, -1, -1},
    {0, 3, 4, 6, 10, -1, -1},
    {2, 4, 6, 8, -1, -1, -1},
    {0, 4, 6, 10, -1, -1, -1},
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
// Two-bar patterns in sixteenths.
constexpr const char* batida[2] = {"x..x.x..x..x.x..", ".x..x..x.x..x..."};    // the bossa guitar
constexpr const char* bossa_clave[2] = {"x.....x.....x...", "....x.....x....."};  // the rim
constexpr int montuno[8] = {2, 1, 0, 1, 2, 3, 2, 1};  // the piano montuno's walk over chord tones

constexpr bool hit(const char* pattern, int s) { return pattern[s] == 'x'; }

// Each style's tempo (quarters a minute), metre, swing, room and lead.
struct Look {
    double bpm_low, bpm_high;
    int beats;
    double swing;
    double room_seconds, room_dark_hz;
    LoungeBand::Lead lead;
};
using In = Instrument;
const Look looks[10] = {
    // bossa_lounge
    {116, 124, 4, .5, 1.4, 4500, {In::vibes, 65, .7, .15, 0, false, 3, .35, In::vibes, 0, In::vibes, 0}},
    // smooth_jazz
    {86, 94, 4, .5, 1.8, 5000, {In::sax, 60, .7, .1, .92, false, 0, 0, In::electric_piano, .3, In::sax, 0}},
    // swing_combo
    {128, 138, 4, .66, 1.3, 4000, {In::clarinet, 62, .7, .1, .85, false, 0, 0, In::vibes, .3, In::clarinet, 0}},
    // game_show
    {132, 140, 4, .5, 1.2, 6000, {In::brass, 64, .8, 0, .7, false, 3, .7, In::glock, .2, In::glock, .5}},
    // cocktail_organ
    {104, 112, 4, .64, 1.6, 4000, {In::organ, 62, .7, .1, .9, false, 0, 0, In::vibes, .25, In::organ, 0}},
    // steel_calypso
    {108, 116, 4, .5, 1.2, 5000, {In::steel_pan, 64, .75, .1, 0, true, 3, .4, In::steel_pan, 0, In::steel_pan, 0}},
    // on_hold
    {98, 104, 4, .5, 1.5, 5500, {In::electric_piano, 64, .7, .05, .8, false, 0, 0, In::glock, .15, In::glock, .6}},
    // hawaiian_exotica
    {80, 88, 4, .62, 2.2, 3500, {In::lap_steel, 62, .7, -.1, .95, false, 6, .5, In::vibes, .35, In::lap_steel, 0}},
    // musette_waltz
    {156, 168, 6, .5, 1.4, 4200, {In::musette, 62, .65, .1, .85, false, 3, .5, In::clarinet, .25, In::musette, 0}},
    // cha_cha
    {116, 122, 4, .5, 1.3, 4800, {In::marimba, 67, .75, .12, 0, true, 3, .5, In::marimba, 0, In::marimba, 0}},
};

}  // namespace

LoungeBand::LoungeBand(std::uint32_t seed, LoungeStyle style)
    : Band(seed * 2654435761U + 0x2C1B3C6DU + static_cast<std::uint32_t>(style) * 0x9E3779B9U,
           Room(looks[static_cast<int>(style)].room_seconds, looks[static_cast<int>(style)].room_dark_hz)),
      style_(style), lead_(looks[static_cast<int>(style)].lead) {
    const Look& look = looks[static_cast<int>(style)];
    beats_ = look.beats;
    swing_ = look.swing;
    base_beat_ = 60.0 / (look.bpm_low + random_.uniform() * (look.bpm_high - look.bpm_low));
    beat_ = base_beat_;
    next_bar_ = static_cast<std::int64_t>(sample_rate * .2);
    next_plan_ = next_bar_;
    // the mix: every instrument's level and how much of it reaches the room
    mix(In::kick, .3, .05);
    mix(In::rim, .07, .12);
    mix(In::hat, .045, .05);
    mix(In::shaker, .045, .05);
    mix(In::conga, .16, .08);
    mix(In::marimba, .3, .3);
    mix(In::vibes, .4, .3);
    mix(In::glock, .1, .35);
    mix(In::steel_pan, .22, .3);
    mix(In::piano, .2, .25);
    mix(In::guitar, .2, .2);
    mix(In::ukulele, .2, .2);
    mix(In::upright, .42, .05);
    mix(In::electric_bass, .38, .04);
    mix(In::electric_piano, .14, .25);
    mix(In::organ, .85, .25);
    mix(In::brass, .73, .25);
    mix(In::sax, .84, .3);
    mix(In::clarinet, .67, .3);
    mix(In::lap_steel, .32, .45);
    mix(In::musette, .9, .25);
    mix(In::brush_sweep, .05, .1);
    mix(In::brush_tap, .09, .1);
    mix(In::snare, .3, .12);
    mix(In::ride, .09, .1);
    mix(In::clave, .05, .1);
    mix(In::cowbell, .05, .1);
    mix(In::guiro, .15, .1);
    mix(In::timbale, .07, .1);
    mix(In::clap, .3, .2);
    // and by style, where a part plays a different role (balanced, alone, against the
    // marimba band's: the tune and the bass level, the comping some fifteen decibels under)
    switch (style) {
    case LoungeStyle::on_hold: mix(In::electric_piano, .56, .25); mix(In::guitar, .14, .15); mix(In::piano, .12, .3); break;
    case LoungeStyle::cha_cha: mix(In::piano, .16, .2); break;
    case LoungeStyle::musette_waltz: mix(In::guitar, .26, .2); break;
    default: break;
    }
}

void LoungeBand::plan() noexcept {
    plan_bar();
    next_plan_ = next_bar_;
}

void LoungeBand::at(double beat, Instrument kind, double midi, double vel, double length, double pan) noexcept {
    // swing: an off-beat eighth falls late in its beat
    double b = beat;
    if (swing_ != .5) {
        const double whole = std::floor(b + 1e-9);
        if (std::fabs(b - whole - .5) < 1e-6) b = whole + swing_;
    }
    note(next_bar_, b * beat_, kind, midi, vel, length * beat_, pan);
}

void LoungeBand::roll(double beat, Instrument kind, double midi, double vel, double length, double pan) noexcept {
    // a held note on a struck instrument: the sticks alternating some eleven strokes a second
    const double stroke = .09 / beat_;
    const int strokes = std::max(1, static_cast<int>(length / stroke));
    for (int k = 0; k < strokes; ++k) {
        const double u = static_cast<double>(k) / strokes;
        at(beat + k * stroke, kind, midi, vel * (k % 2 ? .78 : .9) * (.8 + .35 * std::sin(pi * std::min(1.0, u * 1.4))),
           k + 1 < strokes ? stroke * 3 : .6, pan + (k % 2 ? .05 : -.05));
    }
}

const Chord& LoungeBand::chord_at(int bar, double beat) const noexcept {
    return chords_[static_cast<std::size_t>(std::clamp(bar * 2 + (beat >= beats_ * .5 ? 1 : 0), 0, 7))];
}

double LoungeBand::below(double midi, int interval) const noexcept {
    // the scale note a third (three or four semitones) or a sixth (eight or nine) below
    const int lo = interval == 6 ? 8 : 3;
    for (int down = lo; down <= lo + 1; ++down) {
        const int pc = ((static_cast<int>(midi) - down - key_) % 12 + 12) % 12;
        for (const int s : major_scale)
            if (s == pc) return midi - down;
    }
    return midi - lo;
}

double LoungeBand::bass_note(const Chord& c, int index) const noexcept {
    double n = chord_tone(c, index, 36);
    if (n > 48) n -= 12;
    return n;
}

void LoungeBand::chord(double beat, Instrument kind, const Chord& c, int floor, int from, int to, double vel, double length, double spread, double pan) noexcept {
    for (int k = from; k <= to; ++k)
        at(beat + (k - from) * spread / beat_, kind, chord_tone(c, k, floor), vel * (1 - .04 * (k - from)), length, pan - .15 + .1 * (k - from));
}

void LoungeBand::strum(double beat, Instrument kind, const Chord& c, int floor, bool down, double vel, double length, double pan) noexcept {
    for (int k = 0; k < 4; ++k) {
        const int string = down ? k : 3 - k;
        at(beat + k * (down ? .012 : .008) / beat_, kind, chord_tone(c, string, floor), vel * (1 - .05 * k), length, pan);
    }
}

void LoungeBand::walk(int bar, Instrument kind, double vel) noexcept {
    // a walking bass: the root on one, chord tones between, a step into the next chord on four
    for (int q = 0; q < 4; ++q) {
        const Chord& c = chord_at(bar, q);
        double n = 0;
        if (q == 0 || (q == 2 && chord_at(bar, 2).root != chord_at(bar, 0).root)) {
            n = bass_note(c, 0);
        } else if (q == 3) {
            const Chord& next = bar < 3 ? chord_at(bar + 1, 0) : Chord{key_, Quality::maj7};
            n = bass_note(next, 0) + (random_.uniform() < .5 ? -1 : 1);
        } else {
            n = bass_note(c, random_.uniform() < .5 ? 1 : 2);
        }
        at(q, kind, n, vel * (q % 2 ? .85 : 1), .95);
    }
}

// A new phrase: its progression in the key, and now and then a new motif.
void LoungeBand::plan_phrase() noexcept {
    if (phrase_ != Phrase::rest && random_.uniform() < .08) {
        phrase_ = Phrase::rest;
        const Progression& p = progressions[0];
        for (int h = 0; h < 8; ++h) chords_[static_cast<std::size_t>(h)] = {(key_ + p.d[h].step) % 12, static_cast<Quality>(p.d[h].q)};
        chords_[6] = chords_[7] = {key_, Quality::maj7};
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
        const std::array<std::array<int, 7>, 6>& rhythms = beats_ == 6 ? rhythms_waltz : rhythms_four;
        rhythm_ = rhythms[static_cast<std::size_t>(random_.pick(static_cast<int>(rhythms.size())))];
        rhythm_notes_ = 0;
        while (rhythm_notes_ < 7 && rhythm_[static_cast<std::size_t>(rhythm_notes_)] >= 0) ++rhythm_notes_;
        contour_ = contours[static_cast<std::size_t>(random_.pick(static_cast<int>(contours.size())))];
        motif_phrases_ = 2 + random_.pick(3);
    }
    harmony_ = lead_.harmony > 0 && random_.uniform() < lead_.harmony_odds;
    answer_ = random_.uniform() < lead_.answer_odds;
    doubled_ = random_.uniform() < lead_.doubled_odds;
    half_all_ = phrase_ != Phrase::rest && random_.uniform() < .12;
    half_bass_ = half_all_ || random_.uniform() < .2;
    half_comp_ = half_all_ || random_.uniform() < .2;
    half_tune_ = half_all_ || random_.uniform() < .2;
    bar_ = 0;
}

void LoungeBand::tune_note(double beat, double midi, double vel, double length, Instrument kind, double pan) noexcept {
    const bool lead = kind == lead_.kind;
    const bool rolled = lead && lead_.rolls && length >= .9;
    const double sounding = lead && lead_.legato > 0 ? length * lead_.legato : std::min(length, .45);
    if (rolled) roll(beat, kind, midi, vel, length, pan);
    else at(beat, kind, midi, vel, kind == Instrument::vibes || kind == Instrument::glock ? length : sounding, pan);
    if (harmony_ && lead) {
        const double low = below(midi, lead_.harmony);
        if (rolled) roll(beat + .045 / beat_, kind, low, vel * .7, length, pan - .2);
        else at(beat, kind, low, vel * .7, sounding, pan - .2);
    }
    if (doubled_ && lead && lead_.doubled != lead_.kind) at(beat, lead_.doubled, midi + 12, vel * .55, length, pan + .25);
}

// One bar of the tune: bars 0 and 2 state the motif, bar 1 answers it and holds a chord
// tone, bar 3 closes on a note that suits the cadence. A waltz's bar is two measures, so
// every place in it is half again as far.
void LoungeBand::tune_bar(int bar, Instrument kind, int floor, double vel, double pan) noexcept {
    const int eighths = beats_ * 2;
    const double f = beats_ / 4.0;
    if (half_tune_ && bar < 3) {
        const int first = bar == 1 ? eighths : 0;
        for (int n = 0; n < rhythm_notes_; ++n) {
            const int d = rhythm_[static_cast<std::size_t>(n)] * 2;
            if (d < first || d >= first + eighths) continue;
            const int next = n + 1 < rhythm_notes_ ? rhythm_[static_cast<std::size_t>(n + 1)] * 2 : eighths * 2;
            tune_note((d - first) * .5, tone_at(bar, floor, (d - first) * .5, contour_[static_cast<std::size_t>(n)]), vel, (next - d) * .5 * .9, kind, pan);
        }
        return;
    }
    if (bar == 0 || bar == 2) {
        for (int n = 0; n < rhythm_notes_; ++n) {
            const int on = rhythm_[static_cast<std::size_t>(n)];
            const int next = n + 1 < rhythm_notes_ ? rhythm_[static_cast<std::size_t>(n + 1)] : eighths;
            int index = contour_[static_cast<std::size_t>(n)];
            if (bar == 2 && n == rhythm_notes_ - 2 && random_.uniform() < .4) index = std::min(5, index + 1);
            tune_note(on * .5, tone_at(bar, floor, on * .5, index), vel * (on % 2 == 0 ? 1 : .82), (next - on) * .5, kind, pan);
        }
    } else if (bar == 1) {
        const int keep = std::max(2, rhythm_notes_ / 2);
        for (int n = 0; n < keep && n < rhythm_notes_; ++n) {
            const double b = rhythm_[static_cast<std::size_t>(n)] * .5;
            if (b >= beats_ * .5) break;
            tune_note(b, tone_at(bar, floor, b, contour_[static_cast<std::size_t>(n)]), vel * .9, .45, kind, pan);
        }
        static constexpr int held[3] = {1, 3, 4};
        tune_note(beats_ * .5, tone_at(bar, floor, beats_ * .5, held[random_.pick(3)]), vel, beats_ * .5 - .2, kind, pan);
    } else {
        const Chord& c = chord_at(bar, beats_ * .5);
        int target = 1;
        switch (phrase_) {
        case Phrase::half: target = random_.uniform() < .6 ? 1 : 4; break;
        case Phrase::deceptive: target = random_.uniform() < .6 ? 0 : 1; break;
        case Phrase::home: target = random_.uniform() < .25 ? 0 : (random_.uniform() < .5 ? 1 : 2); break;
        case Phrase::open: target = random_.uniform() < .5 ? 1 : 3; break;
        case Phrase::rest: break;
        }
        const double goal = chord_tone(c, target, floor);
        tune_note(0, tone_at(bar, floor, 0, random_.uniform() < .5 ? 2 : 1), vel * .85, .45, kind, pan);
        tune_note(f, goal + (random_.uniform() < .5 ? 2 : -1), vel * .8, .45, kind, pan);
        tune_note(1.5 * f, goal, vel, beats_ - 1.5 * f - .2, kind, pan);
    }
}

// ---- the rhythm sections ----

void LoungeBand::bossa(int bar, int two) noexcept {
    // the bass: root on one, fifth on the "and" of two, again in the second half
    for (int h = 0; h < 2; ++h) {
        const Chord& c = chord_at(bar, h * 2);
        const bool moved = h == 1 && c.root != chord_at(bar, 0).root;
        if (half_bass_) {
            at(h * 2, Instrument::upright, h == 0 || moved ? bass_note(c, 0) : bass_note(c, 2), .8, 1.8);
            continue;
        }
        at(h * 2, Instrument::upright, h == 0 || moved ? bass_note(c, 0) : bass_note(c, 2), .82, 1.3);
        at(h * 2 + 1.5, Instrument::upright, bass_note(c, 2), .55, .45);
    }
    // the guitar's batida: rootless voicings plucked together, a hair spread
    for (int s = 0; s < 16; ++s) {
        if (!hit(batida[two], s) || (half_comp_ && s % 8 != 0 && s % 8 != 3)) continue;
        chord(s * .25, Instrument::guitar, chord_at(bar, s * .25), 55, 1, 4, s % 4 == 0 ? .4 : .32, .4, .006, -.25);
    }
    for (int s = 0; s < 16; ++s)
        if (hit(bossa_clave[two], s)) at(s * .25, Instrument::rim, 0, .5, 0, .15);
    for (int e = 0; e < 8; ++e) at(e * .5, Instrument::shaker, 0, e % 2 ? .28 : .4, 0, .3);
    at(0, Instrument::kick, 0, .5);
    at(2, Instrument::kick, 0, .4);
}

void LoungeBand::smooth(int bar, int two) noexcept {
    const Chord& first = chord_at(bar, 0);
    const Chord& second = chord_at(bar, 2);
    const bool moved = second.root != first.root;
    if (half_bass_) {
        at(0, Instrument::electric_bass, bass_note(first, 0), .85, 1.8);
        at(2, Instrument::electric_bass, moved ? bass_note(second, 0) : bass_note(first, 2), .75, 1.8);
    } else {
        at(0, Instrument::electric_bass, bass_note(first, 0), .85, 1.2);
        at(1.75, Instrument::electric_bass, bass_note(first, 0) + (random_.uniform() < .3 ? 12 : 0), .45, .2);
        at(2, Instrument::electric_bass, moved ? bass_note(second, 0) : bass_note(first, 2), .75, 1.0);
        const Chord& next = bar < 3 ? chord_at(bar + 1, 0) : Chord{key_, Quality::maj7};
        if (random_.uniform() < .6) at(3.5, Instrument::electric_bass, bass_note(next, 0) - 1, .45, .4);
    }
    // the electric piano: soft voicings held, now and then anticipating the next chord
    if (half_comp_) {
        chord(0, Instrument::electric_piano, first, 52, 1, 4, .3, 3.8, .012, -.2);
    } else if (two == 0) {
        chord(0, Instrument::electric_piano, first, 52, 1, 4, .32, 1.8, .012, -.2);
        chord(2, Instrument::electric_piano, second, 52, 1, 4, .28, 1.6, .012, -.2);
    } else {
        chord(0, Instrument::electric_piano, first, 52, 1, 4, .32, 1.4, .012, -.2);
        chord(1.5, Instrument::electric_piano, first, 52, 1, 3, .22, .4, .008, -.2);
        chord(2.75, Instrument::electric_piano, second, 52, 1, 4, .28, 1.1, .012, -.2);
    }
    at(0, Instrument::kick, 0, .55);
    at(2.5, Instrument::kick, 0, .4);
    if (two == 1) at(1.75, Instrument::kick, 0, .3);
    at(1, Instrument::rim, 0, .5, 0, .1);
    at(3, Instrument::rim, 0, .5, 0, .1);
    for (int e = 0; e < 8; ++e) at(e * .5, Instrument::hat, 0, e % 2 ? .22 : .35, 0, .3);
    at(3.75, Instrument::hat, 0, .15, 0, .3);
    for (int s = 0; s < 16; ++s) at(s * .25, Instrument::shaker, 0, s % 2 ? .1 : .16, 0, -.3);
}

void LoungeBand::swing(int bar, int two) noexcept {
    if (half_bass_) {
        at(0, Instrument::upright, bass_note(chord_at(bar, 0), 0), .8, 1.9);
        at(2, Instrument::upright, bass_note(chord_at(bar, 2), chord_at(bar, 2).root != chord_at(bar, 0).root ? 0 : 2), .7, 1.9);
    } else {
        walk(bar, Instrument::upright, .75);
    }
    // the piano comps in shells (third, seventh, ninth): the Charleston, or pushes
    static constexpr double comps[4][2] = {{0, 1.5}, {1.5, 3}, {.5, 2.5}, {1.5, 3.5}};
    const int pattern = half_comp_ ? 0 : random_.pick(4);
    for (int k = 0; k < (half_comp_ ? 1 : 2); ++k) {
        const double b = comps[pattern][k];
        const Chord& c = chord_at(bar, b);
        at(b, Instrument::piano, chord_tone(c, 1, 55), .34, .45, -.2);
        at(b, Instrument::piano, chord_tone(c, 3, 55), .32, .45, -.15);
        at(b, Instrument::piano, chord_tone(c, 4, 60), .28, .45, -.1);
    }
    // the ride's "ding, ding-a ding", the hi-hat's foot on two and four, the brushes swept
    static constexpr double ride[6] = {0, 1, 1.5, 2, 3, 3.5};
    for (int k = 0; k < 6; ++k) at(ride[k], Instrument::ride, 0, k % 3 == 2 ? .22 : .32, 0, .35);
    at(1, Instrument::hat, 0, .35, 0, .3);
    at(3, Instrument::hat, 0, .35, 0, .3);
    at(0, Instrument::brush_sweep, 0, .3, 1.9, -.1);
    at(2, Instrument::brush_sweep, 0, .3, 1.9, -.1);
    if (two == 1 && random_.uniform() < .5) at(3.5, Instrument::brush_tap, 0, .2, 0, -.1);
}

void LoungeBand::show(int bar, int two) noexcept {
    // the bass bounces in octaves, every eighth
    for (int e = 0; e < 8; ++e) {
        if (half_bass_ && e % 2) continue;
        const Chord& c = chord_at(bar, e * .5);
        at(e * .5, Instrument::electric_bass, bass_note(c, 0) + (e % 2 ? 12 : 0), e % 2 ? .55 : .8, half_bass_ ? .9 : .4);
    }
    // the piano stabs
    static constexpr double stabs[2][4] = {{0, 1.5, 3, -1}, {0, 1.5, 2.5, 3.5}};
    for (int k = 0; k < 4; ++k) {
        const double b = stabs[two][k];
        if (b < 0 || (half_comp_ && b != 0 && b != 2.5)) continue;
        chord(b, Instrument::piano, chord_at(bar, b), 60, 0, 3, .45, .3, .004, .2);
    }
    at(0, Instrument::kick, 0, .65);
    at(2, Instrument::kick, 0, .6);
    if (two == 1) at(2.5, Instrument::kick, 0, .4);
    if (!half_all_) {
        at(1, Instrument::snare, 0, .5, 0, .05);
        at(3, Instrument::snare, 0, .55, 0, .05);
    } else {
        at(2, Instrument::snare, 0, .55, 0, .05);
    }
    for (int e = 0; e < 8; ++e) at(e * .5, Instrument::hat, 0, e % 2 ? .22 : .32, 0, .3);
    if (two == 1) at(3, Instrument::clap, 0, .35, 0, -.1);
    if (bar == 0) at(0, Instrument::ride, 0, .65, 0, .3);
    // the brass hits the chord at a phrase's open end
    if (bar == 3 && (phrase_ == Phrase::half || phrase_ == Phrase::open)) chord(3.5, Instrument::brass, chord_at(3, 3.5), 62, 0, 3, .5, .45, 0, 0);
}

void LoungeBand::cocktail(int bar, int two) noexcept {
    static_cast<void>(two);
    if (half_bass_) {
        at(0, Instrument::upright, bass_note(chord_at(bar, 0), 0), .8, 1.9);
        at(2, Instrument::upright, bass_note(chord_at(bar, 2), chord_at(bar, 2).root != chord_at(bar, 0).root ? 0 : 2), .7, 1.9);
    } else {
        walk(bar, Instrument::upright, .72);
    }
    // the guitar chops four to the bar: three-note voicings, short
    for (int q = 0; q < 4; ++q) {
        if (half_comp_ && q % 2) continue;
        const Chord& c = chord_at(bar, q);
        const double v = q % 2 ? .22 : .28;
        at(q, Instrument::guitar, chord_tone(c, 0, 48), v, .3, -.3);
        at(q + .004 / beat_, Instrument::guitar, chord_tone(c, 1, 52), v, .3, -.3);
        at(q + .008 / beat_, Instrument::guitar, chord_tone(c, 3, 55), v, .3, -.3);
    }
    static constexpr double ride[6] = {0, 1, 1.5, 2, 3, 3.5};
    for (int k = 0; k < 6; ++k) at(ride[k], Instrument::ride, 0, k % 3 == 2 ? .18 : .26, 0, .35);
    at(1, Instrument::hat, 0, .32, 0, .3);
    at(3, Instrument::hat, 0, .32, 0, .3);
    at(0, Instrument::brush_sweep, 0, .25, 3.8, -.1);
}

void LoungeBand::calypso(int bar, int two) noexcept {
    const Chord& first = chord_at(bar, 0);
    const Chord& second = chord_at(bar, 2);
    if (half_bass_) {
        at(0, Instrument::electric_bass, bass_note(first, 0), .85, 1.8);
        at(2, Instrument::electric_bass, bass_note(second, second.root != first.root ? 0 : 2), .75, 1.8);
    } else {
        at(0, Instrument::electric_bass, bass_note(first, 0), .85, 1.3);
        at(1.5, Instrument::electric_bass, bass_note(first, 2), .6, .45);
        at(2, Instrument::electric_bass, bass_note(second, second.root != first.root ? 0 : 2), .75, .9);
        at(3, Instrument::electric_bass, bass_note(second, 2), .55, .4);
        const Chord& next = bar < 3 ? chord_at(bar + 1, 0) : Chord{key_, Quality::maj7};
        at(3.5, Instrument::electric_bass, bass_note(next, 0) + (random_.uniform() < .5 ? -1 : 2), .5, .4);
    }
    // the second pan strums the off-beats
    for (int e = 1; e < 8; e += 2) {
        if (half_comp_ && e != 1 && e != 5) continue;
        chord(e * .5, Instrument::steel_pan, chord_at(bar, e * .5), 62, 1, 3, .26, .3, .01, -.35);
    }
    if (two == 1) chord(1, Instrument::steel_pan, first, 62, 1, 3, .2, .3, .01, -.35);
    at(0, Instrument::kick, 0, .5);
    at(2, Instrument::kick, 0, .45);
    for (int q = 0; q < 4; ++q) at(q, Instrument::cowbell, 0, q == 0 ? .4 : .28, 0, .35);
    at(0, Instrument::guiro, 0, .35, .4, -.3);
    at(1, Instrument::guiro, 0, .25, .15, -.3);
    at(1.5, Instrument::guiro, 0, .25, .15, -.3);
    at(1, Instrument::conga, 67, .3, 0, -.4);
    at(3, Instrument::conga, 62, .4, 0, -.4);
    at(3.5, Instrument::conga, 62, .38, 0, -.4);
    if (two == 1) at(2.5, Instrument::conga, 57, .35, 0, -.4);
}

void LoungeBand::hold_line(int bar, int two) noexcept {
    // the bass pumps eighths on the root
    for (int e = 0; e < 8; ++e) {
        if (half_bass_ && e % 2) continue;
        const Chord& c = chord_at(bar, e * .5);
        const double n = e == 7 && random_.uniform() < .5 ? bass_note(c, 2) : bass_note(c, 0);
        at(e * .5, Instrument::electric_bass, n, e % 2 ? .45 : .6, half_bass_ ? .9 : .4);
    }
    // the guitar picks muted eighths; a soft piano holds the chord under it
    for (int e = 0; e < 8; ++e) {
        if (half_comp_ && e % 2) continue;
        const Chord& c = chord_at(bar, e * .5);
        at(e * .5, Instrument::guitar, chord_tone(c, 0, 60), e % 2 ? .18 : .24, .12, .3);
        at(e * .5, Instrument::guitar, chord_tone(c, 2, 60), e % 2 ? .16 : .2, .12, .35);
    }
    chord(0, Instrument::piano, chord_at(bar, 0), 60, 1, 3, .2, 1.9, .01, -.25);
    if (chord_at(bar, 2).root != chord_at(bar, 0).root) chord(2, Instrument::piano, chord_at(bar, 2), 60, 1, 3, .18, 1.9, .01, -.25);
    at(0, Instrument::kick, 0, .55);
    at(2, Instrument::kick, 0, .5);
    if (two == 1) at(2.5, Instrument::kick, 0, .35);
    at(1, Instrument::clap, 0, .3, 0, -.1);
    at(3, Instrument::clap, 0, .32, 0, -.1);
    for (int e = 0; e < 8; ++e) at(e * .5, Instrument::hat, 0, e % 2 ? .14 : .22, 0, .3);
    for (int s = 0; s < 16; ++s) at(s * .25, Instrument::shaker, 0, s % 2 ? .08 : .12, 0, -.3);
}

void LoungeBand::exotica(int bar, int two) noexcept {
    static_cast<void>(two);
    const Chord& first = chord_at(bar, 0);
    const Chord& second = chord_at(bar, 2);
    at(0, Instrument::upright, bass_note(first, 0), .8, 1.8);
    at(2, Instrument::upright, bass_note(second, second.root != first.root ? 0 : 2), .7, 1.8);
    // the ukulele's island strum: down, down-up, up-down-up
    static constexpr double strums[6] = {0, 1, 1.5, 2.5, 3, 3.5};
    static constexpr bool downs[6] = {true, true, false, false, true, false};
    for (int k = 0; k < 6; ++k) {
        if (half_comp_ && k != 0 && k != 4) continue;
        strum(strums[k], Instrument::ukulele, chord_at(bar, strums[k]), 60, downs[k], downs[k] ? .3 : .22, .45, .25);
    }
    for (int e = 0; e < 8; ++e) at(e * .5, Instrument::shaker, 0, e % 2 ? .18 : .3, 0, .3);
    at(1, Instrument::brush_tap, 0, .2, 0, -.1);
    at(3, Instrument::brush_tap, 0, .2, 0, -.1);
    if (random_.uniform() < .3) at(3.5, Instrument::conga, 72, .3, 0, -.45);
}

void LoungeBand::waltz(int bar, int two) noexcept {
    static_cast<void>(two);
    // two measures of three: the bass on one, the guitar and the accordion's left hand on
    // two and three
    for (int m = 0; m < 2; ++m) {
        const int base = m * 3;
        const Chord& c = chord_at(bar, base);
        const bool moved = m == 1 && c.root != chord_at(bar, 0).root;
        at(base, Instrument::upright, m == 0 || moved ? bass_note(c, 0) : bass_note(c, 2), .8, .9);
        for (int k = 1; k <= 2; ++k) {
            if (half_comp_ && k == 2) continue;
            chord(base + k, Instrument::guitar, c, 55, 1, 3, .3, .45, .006, .2);
            chord(base + k, Instrument::musette, c, 55, 1, 3, .12, .4, 0, -.2);
            at(base + k, Instrument::brush_tap, 0, .15, 0, -.1);
        }
        at(base, Instrument::brush_sweep, 0, .2, 2.8, -.1);
    }
}

void LoungeBand::cha_cha(int bar, int two) noexcept {
    const Chord& first = chord_at(bar, 0);
    const Chord& second = chord_at(bar, 2);
    // the bass: root, fifth, and the "cha-cha" on four
    at(0, Instrument::upright, bass_note(first, 0), .82, 1.4);
    if (!half_bass_) {
        at(2, Instrument::upright, bass_note(second, second.root != first.root ? 0 : 2), .7, .9);
        at(3, Instrument::upright, bass_note(second, 0), .6, .4);
        at(3.5, Instrument::upright, bass_note(second, 0), .6, .4);
    } else {
        at(2, Instrument::upright, bass_note(second, second.root != first.root ? 0 : 2), .7, 1.8);
    }
    // the piano montuno: chord tones in octaves, syncopated over two bars
    static constexpr const char* montuno_rhythm[2] = {"x.xx.x.x", ".x.xx.x."};
    int step = two * 4;
    for (int e = 0; e < 8; ++e) {
        if (montuno_rhythm[two][e] != 'x' || (half_comp_ && e % 4 != 0 && e % 4 != 1)) continue;
        const Chord& c = chord_at(bar, e * .5);
        const double n = chord_tone(c, montuno[static_cast<std::size_t>(step++ % 8)], 60);
        at(e * .5, Instrument::piano, n, .32, .3, -.2);
        at(e * .5, Instrument::piano, n + 12, .26, .3, -.15);
    }
    at(0, Instrument::guiro, 0, .35, .35, -.3);
    at(1, Instrument::guiro, 0, .25, .12, -.3);
    at(1.5, Instrument::guiro, 0, .25, .12, -.3);
    at(2, Instrument::guiro, 0, .3, .35, -.3);
    for (int q = 0; q < 4; ++q) at(q, Instrument::cowbell, 0, q == 0 ? .4 : .3, 0, .3);
    at(3, Instrument::timbale, 0, .35, 0, .25);
    at(3.5, Instrument::timbale, 0, .35, 0, .25);
    at(1, Instrument::conga, 67, .3, 0, -.4);
    at(3, Instrument::conga, 62, .38, 0, -.4);
    at(3.5, Instrument::conga, 62, .36, 0, -.4);
    at(0, Instrument::kick, 0, .35);
}

// One bar: the style's rhythm section, then the tune (or its answer, or rest).
void LoungeBand::plan_bar() noexcept {
    if (bar_ >= 4) plan_phrase();
    const int bar = bar_;
    const int two = static_cast<int>(bars_ % 2);
    switch (style_) {
    case LoungeStyle::bossa_lounge: bossa(bar, two); break;
    case LoungeStyle::smooth_jazz: smooth(bar, two); break;
    case LoungeStyle::swing_combo: swing(bar, two); break;
    case LoungeStyle::game_show: show(bar, two); break;
    case LoungeStyle::cocktail_organ: cocktail(bar, two); break;
    case LoungeStyle::steel_calypso: calypso(bar, two); break;
    case LoungeStyle::on_hold: hold_line(bar, two); break;
    case LoungeStyle::hawaiian_exotica: exotica(bar, two); break;
    case LoungeStyle::musette_waltz: waltz(bar, two); break;
    case LoungeStyle::cha_cha: cha_cha(bar, two); break;
    }
    if (phrase_ != Phrase::rest) {
        if (bar == 1 && answer_ && lead_.answer != lead_.kind) tune_bar(bar, lead_.answer, lead_.floor, lead_.vel * .85, -lead_.pan);
        else tune_bar(bar, lead_.kind, lead_.floor, lead_.vel, lead_.pan);
    }
    ++bar_;
    ++bars_;
    next_bar_ += static_cast<std::int64_t>(beats_ * beat_ * sample_rate);
    drift_ = std::clamp(drift_ + (random_.uniform() - .5) * .01, -.02, .02);
    beat_ = base_beat_ * (1 + drift_ + (bar_ == 3 ? .015 : 0));
}

}  // namespace games::music
