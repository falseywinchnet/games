#include "banjo_voice.hpp"

#include <algorithm>
#include <cmath>

namespace mm {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr int line_size = 4096;
constexpr int line_mask = line_size - 1;
constexpr int room_size = 2048;
constexpr double dt = 1.0 / BanjoVoice::sample_rate;

// What a slot does with its string.
constexpr int how_pick = 0;
constexpr int how_hammer = 1;  // the fretting hand hammers the ringing string up to the note
constexpr int how_slide = 2;   // picked two frets low and slid up into the note
constexpr int how_pull = 3;    // the finger pulls off, sounding the lower note
constexpr int how_brush = 4;   // clawhammer: the nail brushes down across the top strings

// The banjo's open strings, first to fifth: D4 B3 G3 D3 and the short g4 drone.
constexpr int banjo_open[5] = {62, 59, 55, 50, 67};
// The guitar's, low to high: E2 A2 D3 G3 B3 E4; the mandolin's G3 D4 A4 E5.
constexpr int guitar_open[6] = {40, 45, 50, 55, 59, 64};
constexpr int mandolin_open[4] = {55, 62, 69, 76};

double hz_of(double midi) {
    return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
}

// How each band sounds and plays (BanjoStyle order: scruggs, clawhammer, porch).
struct Flavour {
    double tempo;
    double tempo_spread;
    double loud;
    double hardness;    // how sharp the pick's corner is (1 a metal fingerpick)
    double where;       // where the string is struck, as a fraction from the bridge
    double sustain;     // seconds for a D4 to fall 60 dB
    double bright;      // the string's loop filter: higher keeps the upper partials longer
    double stiff;       // the loop allpass: more negative is more zing
    double twang;       // how sharp a hard pluck starts, fraction of pitch
    double punch;       // how much louder the first instant of a note is
    double click;       // the pick or nail on the string
    double click_hz;
    int body;           // 0 resonator, 1 open back
    double bass;
    double guitar;
    double mandolin;
    bool clawhammer;
    double passages;    // chance of a quiet passage between tunes
    double swing;       // the on-beat sixteenth's share of each pair
    double hammer;      // ornament chances on a melody note
    double slide;
    double pull;
    bool song;          // the lead passes between banjo and fiddle, as on a record
    double fiddle;
};

const Flavour flavours[3] = {
    // scruggs: a resonator banjo with metal picks, driving, and the whole band
    {124, 4, 0.57, 0.92, 0.085, 1.6, 0.86, -0.24, 0.0045, 1.5, 0.55, 4300, 0, 0.5, 0.62, 0.7, false, 0.0, 0.508, 0.22, 0.14, 0.1, true, 0.42},
    // clawhammer: an open-back banjo frailed with the nail, bass and a soft guitar
    {122, 3, 0.88, 0.65, 0.21, 1.0, 0.74, -0.16, 0.0025, 0.8, 0.32, 2700, 1, 0.4, 0.3, 0.0, true, 0.0, 0.54, 0.0, 0.0, 0.0, false, 0.0},
    // porch: Scruggs on a resonator at an easier pace, a quiet band, gentle passages
    {113, 3, 0.69, 0.8, 0.1, 1.4, 0.82, -0.22, 0.0035, 1.15, 0.38, 3900, 0, 0.58, 0.4, 0.0, false, 0.55, 0.515, 0.17, 0.12, 0.08, false, 0.0},
};

// The head's modes. A resonator banjo's head is tight and its back closed, so the
// modes are narrow and high; an open back is looser, broader and darker.
struct Body {
    double freq[7];
    double q[7];
    double gain[7];
    double direct;
    double lift;  // the radiation's rise towards the top
    double top;   // one-pole low-pass coefficient on the banjo's output
};
const Body bodies[2] = {
    {{310, 580, 990, 1480, 2300, 3500, 5300}, {5, 6, 7, 6, 5, 4, 3}, {0.55, 0.75, 1.0, 0.95, 0.8, 0.65, 0.55}, 0.12, 2.5, 0.82},
    {{240, 470, 820, 1250, 1900, 2900, 4300}, {3, 3.5, 4.5, 4, 3.5, 3, 3}, {0.7, 0.85, 1.0, 0.9, 0.7, 0.45, 0.2}, 0.08, 2.0, 0.7},
};

// Rolls: the string each sixteenth of a bar is picked on (1 first .. 5 drone). The
// melody goes on the 3-3-2 accents (sixteenths 0, 3 and 6).
constexpr int roll_count = 7;
constexpr int rolls[roll_count][8] = {
    {3, 2, 1, 5, 2, 1, 5, 1},  // forward, from the thumb
    {2, 1, 5, 2, 1, 5, 2, 1},  // forward, from the index
    {3, 2, 1, 5, 1, 2, 3, 1},  // forward-reverse
    {3, 2, 5, 1, 4, 2, 5, 1},  // alternating thumb
    {1, 2, 5, 1, 2, 5, 1, 2},  // backward
    {2, 1, 5, 1, 2, 1, 5, 1},  // foggy mountain
    {3, 1, 5, 1, 4, 1, 5, 1},  // thumb and middle
};
// Which rolls each part of a tune favours.
constexpr int a_rolls[4] = {0, 1, 3, 5};
constexpr int b_rolls[4] = {2, 4, 0, 6};

// Progressions, eight 2/4 bars a part: {root above the key, kind}.
typedef BanjoVoice::Chord C;
constexpr C I{0, 0};
constexpr C IV{5, 0};
constexpr C V{7, 0};
constexpr C V7{7, 2};
constexpr C vi{9, 1};
constexpr C bVII{10, 0};
const C a_parts[5][8] = {
    {I, I, IV, I, I, I, V, I},
    {I, I, I, V, I, I, V7, I},
    {I, IV, I, V, I, IV, V, I},
    {I, I, V, V, I, I, V7, I},
    {I, I, bVII, bVII, I, I, V, I},  // old-time, mixolydian
};
const C b_parts[5][8] = {
    {IV, IV, I, I, IV, I, V, I},
    {V, V, I, I, V, V7, I, I},
    {vi, vi, IV, IV, I, I, V7, I},
    {I, I, IV, IV, I, I, V, I},
    {bVII, bVII, I, I, bVII, bVII, V, I},
};
// The melody's shape through a part, in scale steps about the part's centre.
constexpr int a_contour[8] = {0, 1, 2, 1, 0, 1, -1, -2};
constexpr int b_contour[8] = {1, 2, 2, 0, 1, 2, 0, -2};

// The G lick over two bars, as {string, fret, how}; frets move up with the key.
constexpr int g_lick[16][3] = {
    {3, 2, how_pick}, {3, 4, how_hammer}, {1, 0, how_pick}, {5, 0, how_pick},
    {2, 0, how_pick}, {3, 2, how_pick}, {3, 0, how_pull}, {4, 0, how_pick},
    {3, 0, how_pick}, {2, 0, how_pick}, {5, 0, how_pick}, {1, 0, how_pick},
    {3, 0, how_pick}, {2, 0, how_pick}, {5, 0, how_pick}, {1, 0, how_pick},
};
// A fill-in: a bluesy run down from the top, pulling off as it goes (over the I).
// The old-time bands take it; the bluegrass record keeps to the major scale.
constexpr int run_fill[8][3] = {
    {1, 5, how_pick}, {1, 3, how_pull}, {1, 0, how_pull}, {2, 3, how_pick},
    {2, 0, how_pull}, {5, 0, how_pick}, {3, 2, how_pick}, {3, 0, how_pull},
};
constexpr int major_fill[8][3] = {
    {1, 5, how_pick}, {1, 2, how_pull}, {1, 0, how_pull}, {2, 3, how_pick},
    {2, 0, how_pull}, {5, 0, how_pick}, {3, 2, how_pick}, {3, 0, how_pull},
};

// The fiddle's body: its main air and wood resonances and the bridge's hill.
constexpr double fiddle_freq[4] = {280, 470, 1150, 2800};
constexpr double fiddle_q[4] = {6, 5, 3, 2.2};
constexpr double fiddle_peak[4] = {0.8, 1.0, 0.6, 0.85};

} // namespace

BanjoVoice::BanjoVoice(std::uint32_t seed, BanjoStyle style) : state_(seed * 2654435761U + 1U), style_(style) {
    for (String& s : banjo_)
        s.line.assign(line_size, 0.0F);
    bass_.line.assign(line_size, 0.0F);
    for (String& s : guitar_)
        s.line.assign(line_size, 0.0F);
    for (String& s : mandolin_)
        s.line.assign(line_size, 0.0F);
    burst_.assign(line_size, 0.0F);
    room_.assign(room_size, 0.0F);
    const Flavour& f = flavours[static_cast<int>(style_)];
    const Body& body = bodies[f.body];
    for (int k = 0; k < head_modes; ++k) {
        const double w = 2 * pi * body.freq[k] / sample_rate;
        const double r = 1 - w / (2 * body.q[k]);
        head_a1_[k] = 2 * r * std::cos(w);
        head_a2_[k] = -r * r;
        // scaled so each mode peaks at its gain
        head_g_[k] = body.gain[k] * (1 - r) * 2 * std::sin(w);
    }
    head_direct_ = body.direct;
    head_top_ = body.top;
    head_lift_ = body.lift;
    {
        const double w = 2 * pi * f.click_hz / sample_rate;
        const double r = 0.82;
        click_a1_ = 2 * r * std::cos(w);
        click_a2_ = -r * r;
    }
    for (int k = 0; k < 4; ++k) {
        const double w = 2 * pi * fiddle_freq[k] / sample_rate;
        const double r = 1 - w / (2 * fiddle_q[k]);
        fiddle_a1_[k] = 2 * r * std::cos(w);
        fiddle_a2_[k] = -r * r;
        fiddle_g_[k] = fiddle_peak[k] * (1 - r) * 2 * std::sin(w);
    }
    swing_ = f.swing;
    plan_tune();
    tempo_ = tune_tempo_;
    loud_ = tune_loud_;
}

double BanjoVoice::uniform() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return (state_ & 0xFFFFFF) / 16777216.0;
}

int BanjoVoice::pick(int n) {
    return std::min(n - 1, static_cast<int>(uniform() * n));
}

// ---- the strings ----

// A pluck. The line is filled with the string's slope just after the pick lets go: a
// step at the point struck, rounded by how hard the pick is. Near the bridge (a
// fingerpick) that is rich in upper partials; further up (the nail over the neck)
// it is rounder. `keep` is how much of the old vibration survives the pick.
void BanjoVoice::pluck(String& s, double midi, double strength, double hardness, double where, double keep) {
    const int n = std::clamp(static_cast<int>(sample_rate / hz_of(midi)), 8, line_size - 8);
    const double at = std::clamp(where * (0.85 + 0.3 * uniform()), 0.03, 0.5);
    const double norm = 0.3 / std::sqrt(at * (1 - at));
    const int corner = std::max(1, static_cast<int>(at * n));
    double low = 0;
    double mean = 0;
    for (int i = 0; i < n; ++i) {
        const double shape = (i < corner ? 1 - at : -at) + 0.04 * (uniform() * 2 - 1);
        low += (shape - low) * hardness;
        burst_[static_cast<std::size_t>(i)] = static_cast<float>(low);
        mean += low;
    }
    mean /= n;
    for (int i = 0; i < n; ++i) {
        const int index = (s.write - n + i + line_size * 2) & line_mask;
        float& cell = s.line[static_cast<std::size_t>(index)];
        cell = cell * static_cast<float>(keep) + static_cast<float>((burst_[static_cast<std::size_t>(i)] - mean) * norm * strength);
    }
    s.quiet = 0;
}

// Tunes the loop to `midi`, at once or as a slide over `seconds`. The read delay
// leaves room for the delay of the loop filter and the stiffness allpass.
void BanjoVoice::set_pitch(String& s, double midi, double seconds) {
    const double comp = (1 - s.bright) + (1 - s.stiff) / (1 + s.stiff);
    s.delay_to = std::clamp(sample_rate / hz_of(midi) - comp, 4.0, line_size - 8.0);
    if (seconds <= 0) {
        s.delay = s.delay_to;
        s.glide = 0;
    } else {
        s.glide = std::min(1.0, 4.0 / (seconds * sample_rate));
    }
    s.sounding = static_cast<int>(std::lround(midi));
    s.quiet = 0;
}

float BanjoVoice::run(String& s) {
    // a string that has died away costs nothing until it is picked again
    if (s.quiet > 9600)
        return 0;
    if (s.glide > 0) {
        s.delay += (s.delay_to - s.delay) * s.glide;
        if (std::abs(s.delay_to - s.delay) < 0.002) {
            s.delay = s.delay_to;
            s.glide = 0;
        }
    }
    double d = s.delay;
    if (s.bend > 1e-6) {
        d *= 1 - s.bend;
        s.bend *= 0.99965;  // the tension settles over a few tens of milliseconds
    }
    // a cubic (Lagrange) read keeps the top end however the delay falls between samples
    const double at = s.write - d;
    const double floor_at = std::floor(at);
    const double f = at - floor_at;
    const int i1 = static_cast<int>(floor_at) & line_mask;
    const float y0 = s.line[static_cast<std::size_t>((i1 - 1) & line_mask)];
    const float y1 = s.line[static_cast<std::size_t>(i1)];
    const float y2 = s.line[static_cast<std::size_t>((i1 + 1) & line_mask)];
    const float y3 = s.line[static_cast<std::size_t>((i1 + 2) & line_mask)];
    const double c0 = -f * (f - 1) * (f - 2) / 6;
    const double c1 = (f + 1) * (f - 1) * (f - 2) / 2;
    const double c2 = -(f + 1) * f * (f - 2) / 2;
    const double c3 = (f + 1) * f * (f - 1) / 6;
    const float out = static_cast<float>(c0 * y0 + c1 * y1 + c2 * y2 + c3 * y3);
    const float filtered = s.loss * (s.bright * out + (1 - s.bright) * s.last);
    s.last = out;
    // the stiffness: an allpass in the loop that lets the upper partials run sharp
    const float stiff = s.stiff * filtered + s.ap_x - s.stiff * s.ap_y;
    s.ap_x = filtered;
    s.ap_y = stiff;
    s.line[static_cast<std::size_t>(s.write)] = stiff;
    s.write = (s.write + 1) & line_mask;
    s.quiet = std::abs(out) < 2e-5F ? s.quiet + 1 : 0;
    if (s.punch > 1e-4F) {
        const float louder = out * (1 + s.punch);
        s.punch *= s.punch_decay;
        return louder;
    }
    return out;
}

// A banjo string picked, with everything that makes it a banjo: the bright steel, the
// zing, the quick loud start and fall, the pick on the string.
void BanjoVoice::pluck_banjo(int string, int midi, double strength, double hardness_scale) {
    const Flavour& f = flavours[static_cast<int>(style_)];
    String& s = banjo_[string - 1];
    const double hz = hz_of(midi);
    // higher notes fall away sooner; the short drone string a little sooner again
    const double t60 = f.sustain * std::pow(293.7 / hz, 0.45) * (string == 5 ? 0.85 : 1.0);
    s.loss = static_cast<float>(std::pow(10.0, -3.0 / (t60 * hz)));
    s.bright = static_cast<float>(f.bright);
    s.stiff = static_cast<float>(f.stiff * (string == 4 ? 0.8 : 1.0));
    s.mute_in = -1;
    set_pitch(s, midi, 0);
    s.bend = f.twang * strength;
    s.punch = static_cast<float>(f.punch * (0.6 + 0.4 * strength));
    s.punch_decay = static_cast<float>(std::exp(-1.0 / (0.03 * sample_rate)));
    pluck(s, midi, strength, f.hardness * hardness_scale, f.where, 0.12);
    click_env_ = std::max(click_env_, strength * f.click);
    ++picked_[part_banjo];
}

// ---- the score ----

bool BanjoVoice::tone_in(int midi, const Chord& chord) const {
    const int root = (key_ + chord.root) % 12;
    const int rel = ((midi - 55 - root) % 12 + 12) % 12;
    if (rel == 0 || rel == 7)
        return true;
    if (rel == (chord.kind == 1 ? 3 : 4))
        return true;
    return chord.kind == 2 && rel == 10;
}

bool BanjoVoice::in_scale(int midi) const {
    const int rel = ((midi - 55 - key_) % 12 + 12) % 12;
    // the old-time tunes (with a flat-seven chord) take the flat seventh throughout
    const bool flat_seven = progression_[0][2].root == 10 || chord_.root == 10;
    return rel == 0 || rel == 2 || rel == 4 || rel == 5 || rel == 7 || rel == 9 || (rel == 11 && !flat_seven) || (rel == 10 && flat_seven);
}

int BanjoVoice::nearest_chord_tone(int midi, const Chord& chord) const {
    for (int d = 0; d < 7; ++d) {
        if (tone_in(midi - d, chord))
            return midi - d;
        if (tone_in(midi + d, chord))
            return midi + d;
    }
    return midi;
}

// `steps` notes of the scale above (or below) `midi`.
int BanjoVoice::step_scale(int midi, int steps) const {
    int note = midi;
    const int dir = steps > 0 ? 1 : -1;
    for (int k = 0; k != steps; k += dir) {
        note += dir;
        while (!in_scale(note))
            note += dir;
    }
    return note;
}

int BanjoVoice::drone() const {
    // retuned up a tone for tunes in D, where g would clash
    return banjo_open[4] + (key_ == 7 ? 2 : 0);
}

// The note a string gives for this chord with the hand at hand_: open if it rings in
// the chord and the hand is low, otherwise the first chord tone under the hand.
// The first string never doubles the second: in a D shape it takes the F#, not the
// open D.
int BanjoVoice::fret_note(int string, const Chord& chord) const {
    const int open = banjo_open[string - 1];
    if (string == 5)
        return drone();
    const int below = string == 1 ? fret_note(2, chord) : -1;
    if (hand_ <= 2 && tone_in(open, chord) && open != below)
        return open;
    for (int fret = std::max(1, hand_); fret <= hand_ + 5; ++fret)
        if (tone_in(open + fret, chord) && open + fret != below)
            return open + fret;
    return open + hand_;
}

// A new tune: its key, its two parts' chords and melodies, its pace.
void BanjoVoice::plan_tune() {
    const Flavour& f = flavours[static_cast<int>(style_)];
    const double roll = uniform();
    key_ = tunes_ < 1 ? 0 : roll < 0.6 ? 0 : roll < 0.8 ? 5 : 7;
    // the old-time progressions belong to the clawhammer more than to bluegrass
    int a = pick(4);
    int b = pick(4);
    if (f.clawhammer && uniform() < 0.45) {
        a = 4;
        b = 4;
    } else if (!f.clawhammer && !f.song && uniform() < 0.12) {
        a = 4;
    }
    for (int k = 0; k < 8; ++k) {
        progression_[0][k] = a_parts[a][k];
        progression_[1][k] = b_parts[b][k];
    }
    // the melodies: a chord tone on each bar's downbeat following the part's shape,
    // and steps between, leaning on to the next downbeat
    for (int p = 0; p < 2; ++p) {
        const int* contour = p == 0 ? a_contour : b_contour;
        chord_ = Chord{};
        const int centre = step_scale((p == 0 ? 62 : 66) - 1, 1);
        int downbeat[9]{};
        for (int bar = 0; bar < 8; ++bar) {
            chord_ = progression_[p][bar];
            const int jitter = pick(3) - 1;
            int want = step_scale(centre, contour[bar] * 2 + jitter);
            if (bar == 7) {
                // home: the tonic at or below the centre
                want = centre;
                while (((want - 55 - key_) % 12 + 12) % 12 != 0)
                    --want;
            }
            int tone = nearest_chord_tone(std::clamp(want, 57, 71), chord_);
            while (tone > 72 || !tone_in(tone, chord_))
                --tone;
            downbeat[bar] = std::max(tone, 55);
        }
        downbeat[8] = downbeat[0];
        for (int bar = 0; bar < 8; ++bar) {
            chord_ = progression_[p][bar];
            const int here = downbeat[bar];
            const int next = downbeat[bar + 1];
            int mid = here;
            int late = here;
            const double shape = uniform();
            if (next > here) {
                mid = shape < 0.6 ? step_scale(here, 1) : nearest_chord_tone(here + 3, chord_);
                late = step_scale(next, -1);
            } else if (next < here) {
                mid = shape < 0.6 ? step_scale(here, -1) : here;
                late = step_scale(next, 1);
            } else {
                mid = shape < 0.5 ? step_scale(here, 1) : step_scale(here, -1);
                late = shape < 0.7 ? here : nearest_chord_tone(here + 4, chord_);
            }
            melody_[p][bar][0] = here;
            // passing notes stay on the scale and in the banjo's reach
            melody_[p][bar][1] = mid > 72 ? step_scale(73, -1) : std::max(mid, 55);
            melody_[p][bar][2] = late > 72 ? step_scale(73, -1) : std::max(late, 55);
        }
    }
    chord_ = progression_[0][0];
    tune_tempo_ = f.tempo + (uniform() * 2 - 1) * f.tempo_spread;
    tune_loud_ = f.loud * (0.92 + 0.08 * uniform());
    section_ = 0;
    part_ = 0;
    pass_ = 0;
    bar_ = 0;
    ++tunes_;
}

// Fills bar_plan_ for the bar about to start.
void BanjoVoice::plan_bar() {
    const Flavour& f = flavours[static_cast<int>(style_)];
    for (Slot& s : bar_plan_)
        s = Slot{};
    if (section_ == 2) {
        // between tunes: a tag (two pinches on the tonic) after a tune, then a breath
        chord_ = Chord{};
        hand_ = 0;
        if (rest_bars_ == 2) {
            bar_plan_[0] = Slot{3, fret_note(3, chord_), 1, fret_note(1, chord_), how_pick, 0.8};
            bar_plan_[4] = Slot{4, fret_note(4, chord_), 1, fret_note(1, chord_), how_pick, 0.62};
        }
        return;
    }
    chord_ = progression_[part_][bar_];
    // On the record the banjo kicks off, the fiddle takes the verse, the fiddle the
    // chorus, and the banjo breaks to finish.
    lead_ = f.song && (part_ == 0) != (pass_ == 0) ? 1 : 0;
    if (section_ == 1) {
        hand_ = 0;
        plan_scruggs_bar(true);
        return;
    }
    // where the fretting hand sits: low, unless the melody climbs past the fifth fret
    {
        const int* melody = melody_[part_][bar_];
        const int high = std::max(melody[0], std::max(melody[1], melody[2]));
        hand_ = high > banjo_open[0] + 5 ? high - banjo_open[0] - 3 : 0;
    }
    if (f.clawhammer) {
        plan_clawhammer_bar();
        return;
    }
    if (lead_ == 1) {
        plan_backup_bar();
        return;
    }
    // the G lick closes the second time through a part, sometimes the first; on the
    // record it closes every banjo break, the last always
    if (bar_ == 6)
        licking_ = f.song ? part_ == 1 || uniform() < 0.6 : pass_ == 1 || uniform() < 0.25;
    if (bar_ >= 6 && licking_) {
        plan_lick(bar_ - 6);
        return;
    }
    if (bar_ == 3 && chord_.root == 0 && uniform() < 0.2) {
        const int(*fill)[3] = f.song ? major_fill : run_fill;
        for (int k = 0; k < 8; ++k) {
            const int string = fill[k][0];
            const int note = string == 5 ? drone() : banjo_open[string - 1] + fill[k][1] + key_;
            bar_plan_[k] = Slot{string, note, 0, 0, fill[k][2], k == 0 ? 0.95 : 0.7};
        }
        return;
    }
    plan_scruggs_bar(false);
}

void BanjoVoice::plan_lick(int half) {
    for (int k = 0; k < 8; ++k) {
        const int* step = g_lick[half * 8 + k];
        const int string = step[0];
        const int note = string == 5 ? drone() : banjo_open[string - 1] + step[1] + key_;
        bar_plan_[k] = Slot{string, note, 0, 0, step[2], (k == 0 || k == 3 || k == 6) ? 0.92 : 0.66};
    }
    if (half == 1) {
        // the lick lands with a pinch on the tonic
        bar_plan_[0].pinch = 1;
        bar_plan_[0].pinch_midi = banjo_open[0] + key_;
    }
}

// The three-finger roll for this bar, with the melody placed on its accents.
void BanjoVoice::plan_scruggs_bar(bool gentle) {
    const int* melody = melody_[part_][bar_];
    if (gentle) {
        // backup: a pinch, the index, the thumb on the drone, room between
        const int pattern[8] = {3, 0, 2, 5, 4, 0, 2, 1};
        for (int k = 0; k < 8; ++k) {
            if (pattern[k] == 0)
                continue;
            bar_plan_[k] = Slot{pattern[k], fret_note(pattern[k], chord_), 0, 0, how_pick, pattern[k] == 5 ? 0.42 : 0.5};
        }
        bar_plan_[0].pinch = 1;
        bar_plan_[0].pinch_midi = fret_note(1, chord_);
        place_melody(0, melody[0], 0.72, false);
        if (bar_plan_[0].string == 1) {
            bar_plan_[0].pinch = 4;
            bar_plan_[0].pinch_midi = fret_note(4, chord_);
        }
        return;
    }
    const int* table = part_ == 0 ? a_rolls : b_rolls;
    const int* roll = rolls[table[pick(4)]];
    for (int k = 0; k < 8; ++k) {
        const int string = roll[k];
        bar_plan_[k] = Slot{string, fret_note(string, chord_), 0, 0, how_pick, string == 5 ? 0.5 : 0.6};
    }
    // the second time through, the same tune with its notes moved about a little
    const bool vary = pass_ == 1 && uniform() < 0.3;
    place_melody(0, melody[0], 1.0, true);
    place_melody(3, vary ? nearest_chord_tone(melody[1], chord_) : melody[1], 0.88, true);
    place_melody(6, melody[2], 0.84, true);
    // a roll never picks one string twice running: move a clash to another string
    for (int k = 1; k < 8; ++k) {
        Slot& s = bar_plan_[k];
        if (s.how != how_pick || k == 3 || k == 6)
            continue;
        const int before = bar_plan_[k - 1].string;
        const int after = k < 7 ? bar_plan_[k + 1].string : 0;
        const int sounded = bar_plan_[k - 1].midi;
        const int coming = k < 7 ? bar_plan_[k + 1].midi : 0;
        if (s.string != before && s.string != after && s.midi != sounded && s.midi != coming)
            continue;
        const int options[5] = {5, 2, 1, 3, 4};
        for (int option : options) {
            const int note = fret_note(option, chord_);
            if (option != before && option != after && note != sounded && note != coming) {
                s.string = option;
                s.midi = note;
                s.strength = option == 5 ? 0.5 : 0.6;
                break;
            }
        }
    }
}

// Puts a melody note on the string that frets it best near the roll's own string,
// and sometimes decorates it with the fretting hand.
void BanjoVoice::place_melody(int slot, int midi, double strength, bool ornament) {
    const Flavour& f = flavours[static_cast<int>(style_)];
    Slot& s = bar_plan_[slot];
    const int before = slot > 0 ? bar_plan_[slot - 1].string : 0;
    const int after = slot < 7 ? bar_plan_[slot + 1].string : 0;
    int best = 0;
    double best_cost = 1e9;
    for (int k = 1; k <= 5; ++k) {
        const int fret = midi - banjo_open[k - 1];
        if (k == 5 && midi != drone())
            continue;
        if (fret < 0 || fret > 12)
            continue;
        if (fret > 0 && (fret < hand_ - 1 || fret > hand_ + 5))
            continue;
        double cost = (k == s.string ? 0 : 1.0) + (fret > 0 ? 0.25 * std::abs(fret - hand_) : -0.2);
        if (k == before || k == after)
            cost += 2.5;
        if (cost < best_cost) {
            best_cost = cost;
            best = k;
        }
    }
    if (best == 0)
        return;
    s.string = best;
    s.midi = midi;
    s.strength = strength * (0.94 + 0.06 * uniform());
    s.how = how_pick;
    if (!ornament || best == 5 || slot == 7)
        return;
    const int fret = midi - banjo_open[best - 1];
    const double o = uniform();
    Slot& next = bar_plan_[slot + 1];
    const bool next_free = slot + 1 != 3 && slot + 1 != 6;
    if (fret >= 2 && next_free && o < f.hammer) {
        // picked a step low, and the note hammered on the next sixteenth
        int low = step_scale(midi, -1);
        if (midi - low > 2 || midi - low < 1)
            low = midi - 2;
        s.midi = low;
        next = Slot{best, midi, 0, 0, how_hammer, strength * 0.6};
    } else if (fret >= 2 && o < f.hammer + f.slide) {
        s.how = how_slide;
    } else if (next_free && o < f.hammer + f.slide + f.pull) {
        const int low = step_scale(midi, -1);
        if (fret >= 1 && low >= banjo_open[best - 1])
            next = Slot{best, low, 0, 0, how_pull, strength * 0.55};
    }
}

// Clawhammer: "bum-ditty". The nail strikes the melody down on the beat, brushes
// across the top strings, and the thumb catches the drone; now and then a hammer,
// a pull-off or the thumb dropped to an inner string puts another note in.
void BanjoVoice::plan_clawhammer_bar() {
    const int* melody = melody_[part_][bar_];
    bar_plan_[0] = Slot{3, melody[0], 0, 0, how_pick, 0.9};
    place_melody(0, melody[0], 0.95, false);
    if (bar_plan_[0].string == 5 || bar_plan_[0].string == 0) {
        // the drone is the thumb's: the finger takes a g on the first string instead
        bar_plan_[0] = Slot{1, banjo_open[0] + 5, 0, 0, how_pick, 0.95};
    }
    if (uniform() < 0.45) {
        // bum-pa-ditty: the next note hammered or pulled on the same string, or struck
        const int string = bar_plan_[0].string;
        const int to = melody[1];
        const int fret = to - banjo_open[string - 1];
        if (string != 5 && fret >= 0 && fret <= hand_ + 5 && std::abs(to - melody[0]) <= 3 && to != melody[0]) {
            bar_plan_[2] = Slot{string, to, 0, 0, to > melody[0] ? how_hammer : how_pull, 0.6};
        } else {
            bar_plan_[2] = Slot{2, fret_note(2, chord_), 0, 0, how_pick, 0.6};
            place_melody(2, to, 0.7, false);
        }
    }
    bar_plan_[4] = Slot{1, 0, 0, 0, how_brush, 0.55};
    // drop-thumb: the thumb takes the melody's next step on an inner string
    bar_plan_[6] = Slot{5, drone(), 0, 0, how_pick, 0.5};
    if (uniform() < 0.15) {
        bar_plan_[6] = Slot{4, fret_note(4, chord_), 0, 0, how_pick, 0.55};
        place_melody(6, melody[2], 0.6, false);
    }
}

// Backup behind the fiddle: the same rolls, quietly, on the chord's own notes, and
// out of the way when the guitar runs at the end of the verse.
void BanjoVoice::plan_backup_bar() {
    if (bar_ == 7) {
        bar_plan_[0] = Slot{3, fret_note(3, chord_), 1, fret_note(1, chord_), how_pick, 0.4};
        return;
    }
    const int* roll = rolls[bar_ % 2 == 0 ? 0 : 5];
    for (int k = 0; k < 8; ++k) {
        const int string = roll[k];
        const double accent = k == 0 || k == 3 || k == 6 ? 0.46 : 0.36;
        bar_plan_[k] = Slot{string, fret_note(string, chord_), 0, 0, how_pick, string == 5 ? 0.3 : accent};
    }
}

// The fiddle's bar. When it has the tune it bows the melody an octave up, in long
// notes that hold at the ends of phrases, often with a double stop under them; when
// the banjo has it, the fiddle holds soft double stops under the chords.
void BanjoVoice::plan_fiddle_bar() {
    const Flavour& f = flavours[static_cast<int>(style_)];
    for (BowPlan& p : fiddle_plan_)
        p = BowPlan{};
    if (f.fiddle <= 0 || section_ != 0)
        return;
    const int* melody = melody_[part_][bar_];
    if (lead_ == 1) {
        const int up[3] = {melody[0] + 12 <= 84 ? melody[0] + 12 : melody[0], melody[1] + 12 <= 84 ? melody[1] + 12 : melody[1],
                           melody[2] + 12 <= 84 ? melody[2] + 12 : melody[2]};
        int at[3] = {0, 3, 6};
        int lengths[3] = {3, 3, 2};
        int notes = 3;
        if (bar_ == 3 || bar_ == 7) {
            // the end of a line: one long note
            lengths[0] = 8;
            notes = 1;
        } else if (uniform() < 0.45) {
            // two half-bar notes, the second the bar's last accent
            at[1] = 4;
            lengths[0] = 4;
            lengths[1] = 4;
            notes = 2;
        }
        for (int k = 0; k < notes; ++k) {
            const int note = notes == 2 && k == 1 ? up[2] : up[k];
            BowPlan& p = fiddle_plan_[at[k]];
            p.midi = note;
            p.sixteenths = lengths[k];
            p.level = k == 0 ? 0.9 : 0.78;
            p.attack = 0.03;
            if (k == 0 && uniform() < 0.65) {
                // a double stop: the chord tone a third to a sixth below
                for (int down = 3; down <= 9; ++down) {
                    if (tone_in(note - down, chord_)) {
                        p.harmony = note - down;
                        break;
                    }
                }
            }
        }
        return;
    }
    // under the banjo's break: a soft double stop on each new chord, held
    const bool change = bar_ == 0 || progression_[part_][bar_ - 1].root != chord_.root;
    if (!change && bar_ % 2 == 1)
        return;
    int top = nearest_chord_tone(69, chord_);
    int low = top - 3;
    while (low > top - 10 && !tone_in(low, chord_))
        --low;
    BowPlan& p = fiddle_plan_[0];
    p.midi = top;
    p.harmony = low;
    p.sixteenths = 16;
    p.level = 0.26;
    p.attack = 0.28;
}

// The bow drawn across the string: the note scoops up from below when it starts
// fresh, or the finger moves under a slur.
void BanjoVoice::bow(Bow& b, int midi, double seconds, double level, double attack_seconds, bool scoop) {
    b.target_hz = hz_of(midi);
    if (scoop || b.env < 0.02) {
        b.hz = hz_of(midi - 1);
        b.glide = 1 - std::exp(-1.0 / (0.035 * sample_rate));
    } else {
        b.glide = 1 - std::exp(-1.0 / (0.008 * sample_rate));
    }
    // the bow changes direction: a breath between notes
    b.env *= 0.55;
    b.age = 0;
    b.level = level;
    b.attack = 1 - std::exp(-1.0 / (attack_seconds * sample_rate));
    b.hold = seconds;
    ++picked_[part_fiddle];
}

float BanjoVoice::run_bow(Bow& b) {
    const bool bowing = b.hold > 0;
    if (!bowing && b.env < 1e-5)
        return 0;
    b.hold -= dt;
    b.age += dt;
    b.env += ((bowing ? b.level : 0) - b.env) * (bowing ? b.attack : 0.0012);
    b.hz += (b.target_hz - b.hz) * b.glide;
    // the vibrato comes in once the note has settled
    const double depth = 0.0065 * std::clamp((b.age - 0.16) / 0.3, 0.0, 1.0);
    b.vibrato_phase += 2 * pi * 5.6 * dt;
    if (b.vibrato_phase > 2 * pi)
        b.vibrato_phase -= 2 * pi;
    const double step = b.hz * (1 + depth * std::sin(b.vibrato_phase)) * dt;
    b.phase += step;
    if (b.phase >= 1)
        b.phase -= 1;
    // a sawtooth with its corner rounded (polyBLEP), so it does not alias
    double saw = 2 * b.phase - 1;
    if (b.phase < step) {
        const double x = b.phase / step;
        saw -= x + x - x * x - 1;
    } else if (b.phase > 1 - step) {
        const double x = (b.phase - 1) / step;
        saw -= x * x + x + x + 1;
    }
    return static_cast<float>(saw * b.env);
}

// One guitar note picked, or hammered on to from the note below.
void BanjoVoice::guitar_note(int string, int midi, double strength, bool hammer) {
    String& s = guitar_[string];
    s.pending = -1;
    s.loss = static_cast<float>(std::pow(10.0, -3.0 / (1.3 * hz_of(midi))));
    s.bright = 0.55F;
    s.stiff = -0.05F;
    s.mute_in = -1;
    if (hammer) {
        set_pitch(s, midi, 0.004);
        pluck(s, midi, strength * 0.3, 0.5, 0.3, 0.85);
    } else {
        set_pitch(s, midi, 0);
        pluck(s, midi, strength, 0.5, 0.17, 0.1);
    }
    ++picked_[part_guitar];
}

void BanjoVoice::play_slot(const Slot& slot) {
    const Flavour& f = flavours[static_cast<int>(style_)];
    if (slot.string == 0)
        return;
    if (slot.how == how_brush) {
        // the back of the nail down across the third, second and first strings, the
        // hand meeting the head as it goes
        int order = 0;
        for (int string = 3; string >= 1; --string) {
            String& s = banjo_[string - 1];
            const int note = fret_note(string, chord_);
            const double hz = hz_of(note);
            s.loss = static_cast<float>(std::pow(10.0, -3.0 / (f.sustain * 0.8 * std::pow(293.7 / hz, 0.45) * hz)));
            s.bright = static_cast<float>(f.bright);
            s.stiff = static_cast<float>(f.stiff);
            s.mute_in = -1;
            set_pitch(s, note, 0);
            s.punch = static_cast<float>(f.punch * 0.5);
            s.pending = static_cast<int>(order * 0.006 * sample_rate);
            s.pending_midi = note;
            s.pending_strength = slot.strength * (string == 3 ? 0.6 : 0.85) * (0.9 + 0.2 * uniform());
            s.pending_hardness = f.hardness * 0.8;
            s.pending_where = f.where;
            ++order;
            ++picked_[part_banjo];
        }
        thump_env_ = std::max(thump_env_, 0.5 * slot.strength);
        click_env_ = std::max(click_env_, 0.6 * f.click * slot.strength);
        return;
    }
    String& s = banjo_[slot.string - 1];
    if (slot.how == how_hammer) {
        // the fretting finger lands: the pitch jumps and the string is knocked anew
        set_pitch(s, slot.midi, 0.004);
        pluck(s, slot.midi, slot.strength * 0.35, 0.5, 0.3, 0.85);
        s.punch = static_cast<float>(f.punch * 0.4);
        s.mute_in = -1;
        return;
    }
    if (slot.how == how_pull) {
        // the finger lets go sideways, plucking the string as it leaves
        set_pitch(s, slot.midi, 0.003);
        pluck(s, slot.midi, slot.strength * 0.6, 0.6, 0.25, 0.7);
        s.punch = static_cast<float>(f.punch * 0.5);
        s.mute_in = -1;
        return;
    }
    if (slot.how == how_slide) {
        pluck_banjo(slot.string, slot.midi - 2, slot.strength, 1.0);
        const double sixteenth = 15.0 / tempo_;
        set_pitch(s, slot.midi, sixteenth * 0.7);
        return;
    }
    pluck_banjo(slot.string, slot.midi, slot.strength, 1.0);
    if (slot.pinch != 0)
        pluck_banjo(slot.pinch, slot.pinch_midi, slot.strength * 0.8, 1.0);
}

// The band behind the banjo, on this sixteenth.
void BanjoVoice::play_band(int slot) {
    const Flavour& f = flavours[static_cast<int>(style_)];
    const bool tag = section_ == 2 && rest_bars_ == 2;
    if (section_ == 2 && !(tag && slot == 0))
        return;
    const int pc = (key_ + chord_.root) % 12;
    int root = 43 + pc;
    while (root > 50)
        root -= 12;
    const int fifth = root + 7 > 52 ? root - 5 : root + 7;
    const bool odd_bar = (bar_ % 2) == 1;
    const bool full = style_ == BanjoStyle::scruggs || f.clawhammer;
    const bool quiet = section_ != 0;
    // The upright bass: root and fifth, on the beat; walking up into a new chord.
    if (f.bass > 0 && (slot == 0 || (slot == 4 && full && !quiet))) {
        int note = slot == 0 ? (odd_bar && !full ? fifth : root) : fifth;
        if (slot == 4 && section_ == 0 && bar_ < 7) {
            const Chord next = progression_[part_][bar_ + 1];
            if (next.root != chord_.root && uniform() < 0.5) {
                int target = 43 + (key_ + next.root) % 12;
                while (target > 50)
                    target -= 12;
                note = target + (target > root ? -1 : 1) * (in_scale(target - 2) ? 2 : 1);
            }
        }
        if (tag)
            note = root;
        String& s = bass_;
        const double hz = hz_of(note);
        s.loss = static_cast<float>(std::pow(10.0, -3.0 / (1.8 * hz)));
        s.bright = 0.2F;
        s.stiff = 0;
        set_pitch(s, note, 0);
        pluck(s, note, 0.9 * (0.92 + 0.08 * uniform()), 0.14, 0.2, 0.2);
        s.mute_in = 60.0 / tempo_ * (full ? 0.42 : 0.8);
        s.mute_loss = 0.985F;
        ++picked_[part_bass];
    }
    // The end of a verse on the record: the guitar's G run, from the flat third
    // hammered up to the third, down through the fifth to the root.
    if (f.song && lead_ == 1 && section_ == 0 && bar_ == 7 && f.guitar > 0) {
        int home = 43 + key_ % 12;
        if (home > 50)
            home -= 12;
        if (slot == 0)
            guitar_note(1, home + 3, 0.85, false);
        else if (slot == 1)
            guitar_note(1, home + 4, 0.85, true);
        else if (slot == 2)
            guitar_note(2, home + 7, 0.8, false);
        else if (slot == 4)
            guitar_note(1, home + 4, 0.8, false);
        else if (slot == 6)
            guitar_note(0, home, 0.9, false);
    }
    // The guitar: boom on the beat, chuck on the off-beat.
    else if (f.guitar > 0 && (!quiet || tag) && (slot == 0 || slot == 4)) {
        if (slot == 0) {
            int note = odd_bar && !tag ? fifth : root;
            if (note < 40)
                note += 12;
            int string = 0;
            for (int k = 0; k < 3; ++k)
                if (note - guitar_open[k] >= 0 && note - guitar_open[k] <= 4)
                    string = k;
            String& s = guitar_[string];
            s.loss = static_cast<float>(std::pow(10.0, -3.0 / (1.4 * hz_of(note))));
            s.bright = 0.5F;
            s.stiff = -0.05F;
            set_pitch(s, note, 0);
            s.pending = 0;
            s.pending_midi = note;
            s.pending_strength = 0.75;
            s.pending_hardness = 0.45;
            s.pending_where = 0.18;
            s.mute_in = 60.0 / tempo_ * 0.9;
            s.mute_loss = 0.97F;
            ++picked_[part_guitar];
        } else {
            for (int k = 2; k < 6; ++k) {
                int note = guitar_open[k];
                for (int fret = 0; fret <= 4; ++fret) {
                    if (tone_in(guitar_open[k] + fret, chord_)) {
                        note = guitar_open[k] + fret;
                        break;
                    }
                }
                String& s = guitar_[k];
                s.loss = static_cast<float>(std::pow(10.0, -3.0 / (1.0 * hz_of(note))));
                s.bright = 0.5F;
                s.stiff = -0.05F;
                set_pitch(s, note, 0);
                s.pending = static_cast<int>((k - 2) * 0.005 * sample_rate);
                s.pending_midi = note;
                s.pending_strength = 0.34 + 0.04 * k;
                s.pending_hardness = 0.4;
                s.pending_where = 0.2;
                s.mute_in = 0.11 + 0.005 * k;
                s.mute_loss = 0.9F;
                ++picked_[part_guitar];
            }
        }
    }
    // The mandolin: a closed chord chopped on the off-beat and stopped at once.
    if (f.mandolin > 0 && !quiet && slot == 4) {
        for (int k = 0; k < 4; ++k) {
            int note = mandolin_open[k] + 2;
            for (int fret = 2; fret <= 6; ++fret) {
                if (tone_in(mandolin_open[k] + fret, chord_)) {
                    note = mandolin_open[k] + fret;
                    break;
                }
            }
            String& s = mandolin_[k];
            s.loss = 0.995F;
            s.bright = 0.7F;
            s.stiff = -0.1F;
            set_pitch(s, note, 0);
            s.pending = static_cast<int>(k * 0.003 * sample_rate);
            s.pending_midi = note;
            s.pending_strength = 0.45;
            s.pending_hardness = 0.75;
            s.pending_where = 0.12;
            s.mute_in = 0.035 + 0.003 * k;
            s.mute_loss = 0.6F;
            ++picked_[part_mandolin];
        }
    }
}

void BanjoVoice::step_slot() {
    const Flavour& f = flavours[static_cast<int>(style_)];
    if (slot_ == 0) {
        plan_bar();
        plan_fiddle_bar();
        // the fretting hand moves: strings left ringing outside the new chord are stopped
        if (section_ != 2) {
            for (int k = 0; k < 4; ++k) {
                String& s = banjo_[k];
                if (s.sounding >= 0 && !tone_in(s.sounding, chord_) && bar_plan_[0].string != k + 1) {
                    s.mute_in = 0.015;
                    s.mute_loss = 0.9F;
                }
            }
        }
    }
    play_slot(bar_plan_[slot_]);
    play_band(slot_);
    {
        const BowPlan& p = fiddle_plan_[slot_];
        if (p.midi > 0) {
            const double seconds = p.sixteenths * 15.0 / tempo_ - 0.02;
            bow(fiddle_[0], p.midi, seconds, p.level, p.attack, lead_ == 1 && slot_ == 0 && uniform() < 0.3);
            if (p.harmony > 0)
                bow(fiddle_[1], p.harmony, seconds, p.level * 0.55, p.attack, false);
        }
    }
    // On through the bar, the part, the tune.
    slot_ = (slot_ + 1) % 8;
    if (slot_ != 0)
        return;
    ++bar_;
    if (section_ == 2) {
        if (--rest_bars_ <= 0)
            plan_tune();
        return;
    }
    if (bar_ < 8)
        return;
    bar_ = 0;
    if (section_ == 1) {
        section_ = 2;
        rest_bars_ = 1;
        return;
    }
    if (pass_ == 0) {
        pass_ = 1;
        return;
    }
    pass_ = 0;
    if (part_ == 0) {
        part_ = 1;
        return;
    }
    // the tune is done: a tag and a breath, sometimes a quiet passage first
    part_ = 0;
    if (f.passages > 0 && uniform() < f.passages) {
        section_ = 1;
        tune_loud_ *= 0.58;
        return;
    }
    section_ = 2;
    rest_bars_ = 2;
}

void BanjoVoice::render_add(std::span<float> stereo, double gain) {
    const Flavour& f = flavours[static_cast<int>(style_)];
    const double banjo_part = part_gain(part_banjo);
    const double bass_part = part_gain(part_bass) * f.bass;
    const double guitar_part = part_gain(part_guitar) * f.guitar;
    const double mandolin_part = part_gain(part_mandolin) * f.mandolin;
    const double fiddle_part = part_gain(part_fiddle) * f.fiddle;
    const double click_decay = std::exp(-1.0 / (0.0025 * sample_rate));
    const double thump_decay = std::exp(-1.0 / (0.03 * sample_rate));
    const double thump_w = 2 * pi * 170 / sample_rate;
    for (std::size_t frame = 0; frame + 1 < stereo.size(); frame += 2) {
        gain_ += (gain - gain_) * 0.00005;
        clock_ -= dt;
        if (clock_ <= 0) {
            tempo_ += (tune_tempo_ - tempo_) * 0.04;
            loud_ += (tune_loud_ - loud_) * 0.03;
            // sixteenths in pairs, the on-beat one a hair longer
            const double pair = 2 * 15.0 / tempo_;
            clock_ += pair * (slot_ % 2 == 0 ? swing_ : 1 - swing_);
            step_slot();
        }
        if (gain_ < 1e-5 && gain < 1e-5)
            continue;
        // the banjo: five strings into the head
        double strings = 0;
        for (String& s : banjo_) {
            if (s.pending >= 0) {
                if (s.pending == 0)
                    pluck(s, s.pending_midi, s.pending_strength, s.pending_hardness, s.pending_where, 0.12);
                --s.pending;
            }
            if (s.mute_in > 0) {
                s.mute_in -= dt;
                if (s.mute_in <= 0)
                    s.loss = s.mute_loss;
            }
            strings += run(s);
        }
        double body = 0;
        for (int k = 0; k < head_modes; ++k) {
            const double y = head_g_[k] * strings + head_a1_[k] * head_y1_[k] + head_a2_[k] * head_y2_[k];
            head_y2_[k] = head_y1_[k];
            head_y1_[k] = y;
            body += y;
        }
        double extra = 0;
        if (click_env_ > 1e-5) {
            const double noise = uniform() * 2 - 1;
            const double y = noise * (1 - 0.82) + click_a1_ * click_y1_ + click_a2_ * click_y2_;
            click_y2_ = click_y1_;
            click_y1_ = y;
            extra += y * click_env_ * 0.9;
            click_env_ *= click_decay;
        }
        if (thump_env_ > 1e-5) {
            // the brushing hand on the head: a soft low knock
            const double noise = uniform() * 2 - 1;
            const double r = 0.995;
            const double y = noise * (1 - r) * 2 * std::sin(thump_w) + 2 * r * std::cos(thump_w) * thump_y1_ - r * r * thump_y2_;
            thump_y2_ = thump_y1_;
            thump_y1_ = y;
            extra += y * thump_env_;
            thump_env_ *= thump_decay;
        }
        // the head radiates as it accelerates, so its sound leans to the top
        const double head = body * 1.4;
        const double radiated = strings * head_direct_ + head + head_lift_ * (head - head_last_);
        head_last_ = head;
        head_tone_ += (radiated + extra - head_tone_) * head_top_;
        const double banjo = head_tone_ * 0.62 * banjo_part;
        double left = banjo * 0.88;
        double right = banjo;
        // the upright bass: round, centred
        {
            if (bass_.mute_in > 0) {
                bass_.mute_in -= dt;
                if (bass_.mute_in <= 0)
                    bass_.loss = bass_.mute_loss;
            }
            bass_tone_ += (run(bass_) - bass_tone_) * 0.12;
            const double b = bass_tone_ * 1.1 * bass_part;
            left += b;
            right += b;
        }
        // the guitar, left, through a warm body (its strings run even when another part
        // is soloed, so a solo is the same performance)
        {
            double g = 0;
            for (String& s : guitar_) {
                if (s.pending >= 0) {
                    if (s.pending == 0)
                        pluck(s, s.pending_midi, s.pending_strength, s.pending_hardness, s.pending_where, 0.1);
                    --s.pending;
                }
                if (s.mute_in > 0) {
                    s.mute_in -= dt;
                    if (s.mute_in <= 0)
                        s.loss = s.mute_loss;
                }
                g += run(s);
            }
            guitar_body_ += (g - guitar_body_) * 0.3;
            const double v = guitar_body_ * 1.5 * guitar_part;
            left += v;
            right += v * 0.5;
        }
        // the mandolin's chop, right
        {
            double m = 0;
            for (String& s : mandolin_) {
                if (s.pending >= 0) {
                    if (s.pending == 0)
                        pluck(s, s.pending_midi, s.pending_strength, s.pending_hardness, s.pending_where, 0.0);
                    --s.pending;
                }
                if (s.mute_in > 0) {
                    s.mute_in -= dt;
                    if (s.mute_in <= 0)
                        s.loss = s.mute_loss;
                }
                m += run(s);
            }
            const double v = m * 1.5 * mandolin_part;
            left += v * 0.55;
            right += v;
        }
        // the fiddle, a little left, through its body
        if (f.fiddle > 0) {
            const double strings_bowed = run_bow(fiddle_[0]) + run_bow(fiddle_[1]);
            double resonant = 0;
            for (int k = 0; k < 4; ++k) {
                const double y = fiddle_g_[k] * strings_bowed + fiddle_a1_[k] * fiddle_y1_[k] + fiddle_a2_[k] * fiddle_y2_[k];
                fiddle_y2_[k] = fiddle_y1_[k];
                fiddle_y1_[k] = y;
                resonant += y;
            }
            fiddle_tone_ += (resonant * 1.3 + strings_bowed * 0.08 - fiddle_tone_) * 0.45;
            const double v = fiddle_tone_ * fiddle_part;
            left += v;
            right += v * 0.7;
        }
        // a small room: early reflections, crossed
        const float mono = static_cast<float>((left + right) * 0.5);
        room_[static_cast<std::size_t>(room_write_)] = mono;
        const double early_left = room_[static_cast<std::size_t>((room_write_ - 731 + room_size) % room_size)];
        const double early_right = room_[static_cast<std::size_t>((room_write_ - 1033 + room_size) % room_size)];
        const double late = room_[static_cast<std::size_t>((room_write_ - 1621 + room_size) % room_size)];
        room_write_ = (room_write_ + 1) % room_size;
        left += early_right * 0.16 + late * 0.08;
        right += early_left * 0.16 + late * 0.08;
        // no DC, and a soft limit
        hp_left_ += (left - hp_left_) * 0.0026;
        hp_right_ += (right - hp_right_) * 0.0026;
        const double scale = 0.5 * loud_ * gain_;
        stereo[frame] += static_cast<float>(std::tanh((left - hp_left_) * scale * 1.4) / 1.4);
        stereo[frame + 1] += static_cast<float>(std::tanh((right - hp_right_) * scale * 1.4) / 1.4);
    }
}

} // namespace mm
