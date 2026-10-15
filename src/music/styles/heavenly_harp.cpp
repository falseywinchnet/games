#include "heavenly_harp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace games::music {
namespace heavenly {

constexpr double rate = 48000;
constexpr double tau = 6.283185307179586;
constexpr double pi = 3.141592653589793;
constexpr double tempo = 66;                 // beats a minute
constexpr double beat_seconds = 60 / tempo;
constexpr int beats_per_bar = 3;
constexpr int bars_per_phrase = 4;

double midi_hz(double midi) {
    return 440 * std::pow(2.0, (midi - 69) / 12);
}

struct Random {
    std::uint32_t s = 1;
    std::uint32_t bits() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    double unit() {
        return (bits() >> 8) * (1.0 / 16777216.0);
    }
    double between(double low, double high) {
        return low + (high - low) * unit();
    }
    int pick(int n) {
        return static_cast<int>(unit() * n) % n;
    }
};

struct SineTable {
    std::array<float, 4097> values{};
    SineTable() {
        for (std::size_t i = 0; i < values.size(); ++i)
            values[i] = static_cast<float>(std::sin(tau * static_cast<double>(i) / 4096));
    }
    float at(double phase) const {
        const double cycles = phase - std::floor(phase);
        const double position = cycles * 4096;
        const int index = static_cast<int>(position);
        const float fraction = static_cast<float>(position - index);
        const float a = values[static_cast<std::size_t>(index)];
        const float b = values[static_cast<std::size_t>(index) + 1];
        return a + (b - a) * fraction;
    }
};

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float run(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// A band-pass of 0 dB peak at `hz`, `width` hertz wide.
void band(Biquad& f, double hz, double width) {
    const double w = tau * std::min(hz, rate * .45) / rate;
    const double q = std::max(.3, hz / width);
    const double alpha = std::sin(w) / (2 * q);
    const double c = std::cos(w);
    const double a0 = 1 + alpha;
    f.b0 = static_cast<float>(alpha / a0);
    f.b1 = 0;
    f.b2 = static_cast<float>(-alpha / a0);
    f.a1 = static_cast<float>(-2 * c / a0);
    f.a2 = static_cast<float>((1 - alpha) / a0);
}

void pan_gains(double pan, float& left, float& right) {
    const double angle = (std::clamp(pan, -1.0, 1.0) + 1) * .25 * pi;
    left = static_cast<float>(std::cos(angle));
    right = static_cast<float>(std::sin(angle));
}

// ---- Struck and plucked voices -------------------------------------------------------

// One partial: its own pitch, level and decay.
struct Partial {
    double phase = 0, step = 0;
    float amp = 0, decay = 1;
};

// A plucked string, a struck bell or a struck tube: a set of partials, a little pitch
// settling after the attack, the touch of finger or hammer or clapper (a short tick of
// noise), and for a hanging tube, the sway of its swing.
struct Struck {
    bool on = false;
    int voice = 0;
    std::array<Partial, 32> partials{};
    int count = 0;
    float attack = 1, attack_step = 1;
    double bend = 0, bend_decay = 1;        // the pitch's extra share, settling to 0
    float tick = 0, tick_decay = 0, tick_low = 0, tick_a = 0;
    double swing_phase = 0, swing_step = 0;
    float swing_depth = 0;
    float left = 0, right = 0;
    int remaining = 0;
    int prune = 0;                          // samples until the dead partials are dropped
};

void begin(Struck& s, int voice, double pan, double attack_seconds) {
    s.on = true;
    s.voice = voice;
    s.count = 0;
    s.attack = 0;
    s.attack_step = static_cast<float>(1 / (attack_seconds * rate));
    s.bend = 0;
    s.bend_decay = 1;
    s.tick = 0;
    s.swing_depth = 0;
    s.remaining = 0;
    pan_gains(pan, s.left, s.right);
}

void add_partial(Struck& s, double hz, double amp, double seconds) {
    if (s.count >= static_cast<int>(s.partials.size()) || hz > rate * .45 || amp <= 0)
        return;
    Partial& p = s.partials[static_cast<std::size_t>(s.count)];
    ++s.count;
    p.phase = 0;
    p.step = hz / rate;
    p.amp = static_cast<float>(amp);
    p.decay = static_cast<float>(std::exp(-6.9 / (seconds * rate)));
    s.remaining = std::max(s.remaining, static_cast<int>(seconds * rate));
}

void add_tick(Struck& s, double level, double seconds, double brightness_hz) {
    s.tick = static_cast<float>(level);
    s.tick_decay = static_cast<float>(std::exp(-1 / (seconds * rate)));
    s.tick_low = 0;
    s.tick_a = static_cast<float>(1 - std::exp(-tau * brightness_hz / rate));
}

// A concert harp's string: plucked with the pad of the finger near its middle, so mostly
// odd harmonics, falling as 1/n^2 and softened further by the finger; ringing in two
// planes a hair apart (the second fades sooner), so the tone blooms and wavers; a touch
// sharp as it is plucked, settling in a few hundredths of a second.
void pluck_harp(Struck& s, double hz, double velocity, double pan, Random& r) {
    begin(s, HeavenlyHarp::harp, pan, .0018);
    const double point = r.between(.42, .52);
    const double finger = 1150 + 1300 * velocity;
    const double ring = std::clamp(13 * std::pow(hz / 65, -.62), 1.1, 16.0);
    const double apart = r.between(.00022, .00045);
    for (int n = 1; n <= 14; ++n) {
        const double fn = n * hz * std::sqrt(1 + .00008 * n * n);
        const double shape = std::fabs(std::sin(n * pi * point)) / (n * n);
        const double soft = 1 / (1 + (fn / finger) * (fn / finger));
        const double amp = shape * soft * velocity * 1.6;
        const double seconds = ring / (1 + .18 * (n - 1) + 2 * (fn / 4000) * (fn / 4000));
        add_partial(s, fn * (1 - apart), amp * .62, seconds);
        add_partial(s, fn * (1 + apart), amp * .38, seconds * .55);
    }
    s.bend = .0028 * (.5 + velocity);
    s.bend_decay = std::exp(-1 / (.035 * rate));
    add_tick(s, .035 * velocity, .0012, 3200);
}

void strike_celesta(Struck& s, double hz, double velocity, double pan) {
    begin(s, HeavenlyHarp::celesta, pan, .001);
    const double scale = std::pow(hz / 1046.5, -.3);
    add_partial(s, hz, velocity, 2.2 * scale);
    add_partial(s, hz * 1.0007, velocity * .25, 1.6 * scale);
    add_partial(s, hz * 3.97, velocity * .22, .35 * scale);
    add_partial(s, hz * 9.1, velocity * .05, .12 * scale);
    add_tick(s, .02 * velocity, .0008, 5000);
}

// An aluminium chime tube struck by the clapper: the free tube's modes (1, 2.76, 5.40,
// 8.93 times the lowest), each a pair a hertz or so apart that beat as they ring, the
// higher ones brighter for a harder strike and shorter-lived; the clapper's tick; the
// tube swaying on its cord.
void strike_chime(Struck& s, double hz, double strength, double pan, Random& r) {
    begin(s, HeavenlyHarp::chimes, pan, .0006);
    const double ratios[4] = {1, 2.756, 5.404, 8.933};
    const double levels[4] = {1, .6, .38, .2};
    const double ring = 14 * std::pow(hz / 880, -.8);
    for (int k = 0; k < 4; ++k) {
        const std::size_t i = static_cast<std::size_t>(k);
        const double fk = hz * ratios[i];
        const double bright = k == 0 ? 1.0 : std::pow(strength, .6 * k);
        const double amp = levels[i] * bright * strength;
        const double seconds = ring / (1 + 1.4 * k);
        const double beat = r.between(.5, 1.6);
        add_partial(s, fk - beat / 2, amp * .55, seconds);
        add_partial(s, fk + beat / 2, amp * .45, seconds * .85);
    }
    add_tick(s, .05 * strength, .0006, 7000);
    s.swing_phase = r.unit();
    s.swing_step = r.between(.7, 1.3) / rate;
    s.swing_depth = .14f;
}

float run_struck(Struck& s, const SineTable& sine, Random& r) {
    const double bend = 1 + s.bend;
    s.bend *= s.bend_decay;
    float v = 0;
    for (int k = 0; k < s.count; ++k) {
        Partial& p = s.partials[static_cast<std::size_t>(k)];
        v += p.amp * sine.at(p.phase);
        p.phase += p.step * bend;
        p.amp *= p.decay;
    }
    if (s.attack < 1) {
        s.attack = std::min(1.0f, s.attack + s.attack_step);
        v *= s.attack * (2 - s.attack);
    }
    if (s.tick > 1e-5f) {
        s.tick_low += s.tick_a * (static_cast<float>(r.unit() * 2 - 1) - s.tick_low);
        v += s.tick_low * s.tick;
        s.tick *= s.tick_decay;
    }
    if (s.swing_depth > 0) {
        s.swing_phase += s.swing_step;
        v *= 1 - s.swing_depth * (.5f + .5f * sine.at(s.swing_phase));
    }
    // Partials that have died away below hearing are dropped; with none left, the note is.
    if (--s.prune <= 0) {
        s.prune = 512;
        int k = 0;
        while (k < s.count) {
            Partial& p = s.partials[static_cast<std::size_t>(k)];
            if (std::fabs(p.amp) < 2e-5f) {
                p = s.partials[static_cast<std::size_t>(s.count - 1)];
                --s.count;
                continue;
            }
            ++k;
        }
        if (s.count == 0 && s.tick < 1e-5f)
            s.on = false;
    }
    if (--s.remaining <= 0)
        s.on = false;
    return v;
}

// ---- The pad -------------------------------------------------------------------------

// One chord tone of the pad: four saw waves a few cents apart (band-limited at their
// edges, so they do not alias), spread left to right, through a two-pole low-pass whose
// opening breathes slowly; a pure tone an octave up, faint, for air.
struct PadNote {
    bool on = false;
    double phase[4]{}, step[4]{};
    float left[4]{}, right[4]{};
    double air_phase = 0, air_step = 0;
    double env = 0, target = 0;
    double low_l = 0, band_l = 0, low_r = 0, band_r = 0;  // the filter's state
    double breath_phase = 0;
};

// A saw wave with its jump smoothed over a sample either side (polyBLEP).
double saw(double phase, double step) {
    double v = 2 * phase - 1;
    if (phase < step) {
        const double t = phase / step;
        v -= t + t - t * t - 1;
    } else if (phase > 1 - step) {
        const double t = (phase - 1) / step;
        v -= t * t + t + t + 1;
    }
    return v;
}

void pad_start(PadNote& n, double hz, Random& r) {
    const double cents[4] = {-9, -3, 4, 10};
    const double place[4] = {-.7, -.2, .25, .7};
    if (!n.on) {
        n.env = 0;
        n.low_l = n.band_l = n.low_r = n.band_r = 0;
    }
    n.on = true;
    n.target = 1;
    for (int k = 0; k < 4; ++k) {
        const std::size_t i = static_cast<std::size_t>(k);
        n.phase[i] = r.unit();
        n.step[i] = hz * std::pow(2.0, cents[i] / 1200) / rate;
        pan_gains(place[i], n.left[i], n.right[i]);
    }
    n.air_step = hz * 2 / rate;
    n.breath_phase = r.unit();
}

void run_pad(PadNote& n, const SineTable& sine, float& out_l, float& out_r) {
    if (!n.on)
        return;
    // Two seconds to swell in, three to let go.
    const double speed = n.target > n.env ? 1 / (2.0 * rate) : 1 / (3.0 * rate);
    n.env += (n.target - n.env) * speed * 4;
    if (n.target == 0 && n.env < .0005) {
        n.on = false;
        return;
    }
    double l = 0, r = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        n.phase[i] += n.step[i];
        if (n.phase[i] >= 1)
            n.phase[i] -= 1;
        const double v = saw(n.phase[i], n.step[i]);
        l += v * n.left[i];
        r += v * n.right[i];
    }
    // The filter breathes between about 700 and 1400 Hz over some seconds.
    n.breath_phase += .09 / rate;
    const double cutoff = 1050 + 350 * sine.at(n.breath_phase);
    const double f = 2 * std::sin(pi * cutoff / rate);
    const double damping = 1.3;
    n.low_l += f * n.band_l;
    n.band_l += f * (l * .25 - n.low_l - damping * n.band_l);
    n.low_r += f * n.band_r;
    n.band_r += f * (r * .25 - n.low_r - damping * n.band_r);
    n.air_phase += n.air_step;
    const double air = .06 * sine.at(n.air_phase);
    out_l += static_cast<float>((n.low_l + air) * n.env);
    out_r += static_cast<float>((n.low_r + air) * n.env);
}

// ---- The hall ------------------------------------------------------------------------

struct Hall {
    static constexpr int lines = 8;
    std::array<std::vector<float>, lines> delay{};
    std::array<int, lines> at{};
    std::array<float, lines> feedback{}, low{};
    float damp = .2f;
    void prepare(double seconds) {
        const int lengths[lines] = {3407, 3803, 4211, 4597, 5003, 5419, 5821, 6229};
        for (int i = 0; i < lines; ++i) {
            const std::size_t k = static_cast<std::size_t>(i);
            delay[k].assign(static_cast<std::size_t>(lengths[i]), 0.0f);
            feedback[k] = static_cast<float>(std::pow(10.0, -3.0 * lengths[i] / (seconds * rate)));
        }
        damp = static_cast<float>(1 - std::exp(-tau * 6500 / rate));
    }
    void run(float in_l, float in_r, float& out_l, float& out_r) {
        std::array<float, lines> out{};
        for (int i = 0; i < lines; ++i) {
            const std::size_t k = static_cast<std::size_t>(i);
            out[k] = delay[k][static_cast<std::size_t>(at[k])];
        }
        std::array<float, lines> mixed = out;
        for (int size = 1; size < lines; size *= 2)
            for (int i = 0; i < lines; i += size * 2)
                for (int j = i; j < i + size; ++j) {
                    const std::size_t a = static_cast<std::size_t>(j);
                    const std::size_t b = static_cast<std::size_t>(j + size);
                    const float x = mixed[a], y = mixed[b];
                    mixed[a] = x + y;
                    mixed[b] = x - y;
                }
        for (int i = 0; i < lines; ++i) {
            const std::size_t k = static_cast<std::size_t>(i);
            low[k] += damp * (mixed[k] * .35355339f - low[k]);
            const float input = (i % 2 == 0) ? in_l : in_r;
            delay[k][static_cast<std::size_t>(at[k])] = low[k] * feedback[k] + input * .5f;
            at[k] = (at[k] + 1) % static_cast<int>(delay[k].size());
        }
        out_l = (out[0] + out[2] + out[4] + out[6]) * .5f;
        out_r = (out[1] + out[3] + out[5] + out[7]) * .5f;
    }
};

// ---- What is to be played --------------------------------------------------------------

struct Event {
    long at = 0;                // sample
    int voice = 0;
    double midi = 60, velocity = .5, pan = 0;
    int slot = 0;               // for the pad: which of its chord tones
};

struct EarlierFirst {
    bool operator()(const Event& a, const Event& b) const {
        return a.at < b.at;
    }
};

constexpr int major[7] = {0, 2, 4, 5, 7, 9, 11};

struct Chord {
    int degree = 0;              // 0 = I
    bool lydian = false;         // IV with its raised fourth (the scale's #11)
};

// The chime's six tubes: A5, B5, D6, E6, F#6, A6.
constexpr double tubes[6] = {81, 83, 86, 88, 90, 93};

struct Score {
    Random random{};
    SineTable sine{};
    int solo = -1;
    long clock = 0;
    long phrase_end = 0;
    long phrases = 0;
    int tonic = 62;              // D
    int home = 62;
    double last_tune = 79;
    std::vector<Event> events;  // reserved once: planning a phrase allocates nothing
    std::size_t next_event = 0;
    std::array<Struck, 128> struck{};
    std::size_t struck_next = 0;
    std::array<Biquad, 3> body_l{}, body_r{};   // the harp's soundboard
    std::array<PadNote, 8> pad{};                // two sets of four: one fading, one coming
    int pad_set = 0;
    Hall hall{};
    // The breeze and the chime's clapper.
    double gust = .3, gust_target = .3, gust_wait = 3;
    double clapper_wait = 2;
    int run_left = 0;
    double run_gap = 0;
    int last_tube = 2;
};



double step_midi(int tonic, int step) {
    const int octave = static_cast<int>(std::floor(step / 7.0));
    const int within = step - octave * 7;
    return tonic + 12 * octave + major[within];
}

// The chord's tones as scale steps from the key's tonic: root, third, fifth, ninth.
double chord_tone(int tonic, const Chord& chord, int which, int octave_shift) {
    const int steps[4] = {chord.degree, chord.degree + 2, chord.degree + 4, chord.degree + 8};
    double midi = step_midi(tonic, steps[which]) + 12 * octave_shift;
    if (chord.lydian && chord.degree == 3 && which == 3)
        midi = step_midi(tonic, 3 + 4) + 12 * octave_shift + 1;  // the #11 over IV
    return midi;
}

void add(Score& s, double beat, int voice, double midi, double velocity, double pan) {
    Event e;
    e.at = s.phrase_end + static_cast<long>(beat * beat_seconds * rate);
    e.voice = voice;
    e.midi = midi;
    e.velocity = velocity;
    e.pan = pan;
    s.events.push_back(e);
}

// Pan a harp note by its string: low strings left, high right, as the instrument stands.
double harp_pan(double midi) {
    return std::clamp((midi - 62) / 40, -.45, .45);
}

double nearest_chord_tone(int tonic, const Chord& chord, double midi, double low, double high) {
    double best = midi, distance = 1e9;
    for (int octave = -1; octave <= 3; ++octave)
        for (int which = 0; which < 3; ++which) {
            const double m = chord_tone(tonic, chord, which, octave);
            if (m < low || m > high)
                continue;
            if (std::fabs(m - midi) < distance) {
                distance = std::fabs(m - midi);
                best = m;
            }
        }
    return best;
}

double scale_move(int tonic, double midi, int steps) {
    int index = 0;
    double best = 1e9;
    for (int i = -14; i <= 35; ++i) {
        const double m = step_midi(tonic, i);
        if (std::fabs(m - midi) < best) {
            best = std::fabs(m - midi);
            index = i;
        }
    }
    return step_midi(tonic, index + steps);
}

void plan_phrase(Score& s) {
    Random& r = s.random;
    s.events.clear();
    s.next_event = 0;
    if (s.phrases > 0 && s.phrases % 4 == 0)
        s.tonic = s.tonic == s.home ? (r.unit() < .5 ? s.home + 5 : s.home - 5) : s.home;
    const int tonic = s.tonic;
    const int progressions[8][4] = {{0, 3, 5, 4}, {0, 5, 3, 4}, {3, 0, 1, 4}, {5, 3, 0, 4},
                                    {0, 2, 3, 0}, {3, 4, 2, 5}, {0, 3, 0, 4}, {5, 4, 3, 0}};
    const int which = r.pick(8);
    std::array<Chord, 4> chords{};
    for (int b = 0; b < 4; ++b) {
        chords[static_cast<std::size_t>(b)].degree = progressions[which][b];
        chords[static_cast<std::size_t>(b)].lydian = progressions[which][b] == 3 && r.unit() < .45;
    }
    const int harp = HeavenlyHarp::harp;
    // The harp is stroked, softly: the pedals set so every string sounds the chord (its
    // root, third, fifth and ninth), the fingers drawn across a span of strings, up or
    // down, the stroke swelling in its middle and falling away at its ends. One or two
    // a bar, their rings overlapping.
    for (int b = 0; b < 4; ++b) {
        const Chord& c = chords[static_cast<std::size_t>(b)];
        const double bar = b * beats_per_bar;
        // Under each chord, very softly, its root low down.
        const double root = chord_tone(tonic, c, 0, -2);
        add(s, bar + r.between(0, .08), harp, root, .2, harp_pan(root));
        const int strokes = (b == 3 || r.unit() < .35) ? 1 : 2;
        for (int k = 0; k < strokes; ++k) {
            // The chord's strings from a low one to a high one.
            std::array<double, 48> strings{};
            std::size_t n = 0;
            const int from_octave = -1 + r.pick(2);
            const int span = 1 + r.pick(3);  // octaves
            for (int octave = from_octave; octave < from_octave + span + 1; ++octave)
                for (int which = 0; which < 4; ++which) {
                    const double m = chord_tone(tonic, c, which == 3 ? 3 : which, octave - (which == 3 ? 1 : 0));
                    if (m >= 48 && m <= 96 && n < strings.size()) {
                        strings[n] = m;
                        ++n;
                    }
                }
            std::sort(strings.begin(), strings.begin() + static_cast<std::ptrdiff_t>(n));
            n = static_cast<std::size_t>(std::unique(strings.begin(), strings.begin() + static_cast<std::ptrdiff_t>(n)) - strings.begin());
            if (n < 3)
                continue;
            const bool upward = r.unit() < .65;
            const double length = r.between(.6, 1.8) / beat_seconds;  // in beats
            const double start_at = bar + (k == 0 ? r.between(.05, .6) : r.between(1.5, 2.2));
            const double peak = r.between(.11, .19);
            for (std::size_t i = 0; i < n; ++i) {
                const double t = static_cast<double>(i) / static_cast<double>(n - 1);
                // An unhurried start and finish, quicker through the middle.
                const double when = start_at + length * (t < .5 ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t));
                const double swell = std::sin(pi * (.15 + .7 * t));
                const double m = upward ? strings[i] : strings[n - 1 - i];
                add(s, when, harp, m, peak * (.45 + .55 * swell) * r.between(.85, 1.1), harp_pan(m));
            }
        }
    }
    // The pad holds each bar's chord: its root, fifth, third and ninth, close and low.
    for (int b = 0; b < 4; ++b) {
        const Chord& c = chords[static_cast<std::size_t>(b)];
        const int order[4] = {0, 2, 1, 3};
        for (int v = 0; v < 4; ++v) {
            double m = chord_tone(tonic, c, order[v], v == 3 ? -1 : 0);
            while (m < 50 + 3 * v)
                m += 12;
            while (m > 62 + 3 * v)
                m -= 12;
            Event e;
            e.at = s.phrase_end + static_cast<long>((b * beats_per_bar - .4) * beat_seconds * rate);
            e.voice = HeavenlyHarp::pad;
            e.midi = m;
            e.slot = v;
            s.events.push_back(e);
        }
    }
    // Raindrops: now and then a single light note high up, lilting, out of time; sometimes
    // two or three falling together, as drops do off a leaf.
    double drop = s.last_tune;
    double at = r.between(.3, 1.2);
    while (at < 11.6) {
        const Chord& c = chords[static_cast<std::size_t>(std::min(3, static_cast<int>(at / beats_per_bar)))];
        const int run = r.unit() < .3 ? 2 + r.pick(2) : 1;
        drop = nearest_chord_tone(tonic, c, drop + r.between(-3, 3.5), 76, 93);
        for (int i = 0; i < run; ++i) {
            const double m = i == 0 ? drop : scale_move(tonic, drop, -i * (1 + r.pick(2)));
            add(s, at + i * r.between(.18, .32), harp, m, r.between(.13, .24) * (1 - .15 * i), harp_pan(m) + .1);
        }
        at += r.between(.9, 2.8);
    }
    s.last_tune = drop;
    // The celesta's sparkle in the breathing bar: the chord falling from high up.
    {
        const Chord& c = chords[3];
        for (int q = 0; q < 6; ++q)
            add(s, 9.6 + q * .25, HeavenlyHarp::celesta, chord_tone(tonic, c, (3 - q % 4 + 4) % 4, 2 - q / 4), .28 - .03 * q, .35);
    }
    std::stable_sort(s.events.begin(), s.events.end(), EarlierFirst{});
    s.phrase_end += static_cast<long>(bars_per_phrase * beats_per_bar * beat_seconds * rate);
    ++s.phrases;
}

Struck& free_struck(Score& s) {
    for (std::size_t i = 0; i < s.struck.size(); ++i) {
        Struck& candidate = s.struck[(s.struck_next + i) % s.struck.size()];
        if (!candidate.on) {
            s.struck_next = (s.struck_next + i + 1) % s.struck.size();
            return candidate;
        }
    }
    // All sounding: take the quietest.
    std::size_t quietest = 0;
    float level = 1e9f;
    for (std::size_t i = 0; i < s.struck.size(); ++i) {
        const float a = s.struck[i].count > 0 ? s.struck[i].partials[0].amp : 0.0f;
        if (a < level) {
            level = a;
            quietest = i;
        }
    }
    return s.struck[quietest];
}

void start(Score& s, const Event& e) {
    const double hz = midi_hz(e.midi);
    if (e.voice == HeavenlyHarp::pad) {
        // A new chord: the old set lets go as the other swells in.
        if (e.slot == 0) {
            for (int i = 0; i < 4; ++i)
                s.pad[static_cast<std::size_t>(s.pad_set * 4 + i)].target = 0;
            s.pad_set = 1 - s.pad_set;
        }
        pad_start(s.pad[static_cast<std::size_t>(s.pad_set * 4 + e.slot)], hz, s.random);
        return;
    }
    if (e.voice == HeavenlyHarp::harp)
        pluck_harp(free_struck(s), hz, e.velocity, e.pan, s.random);
    else
        strike_celesta(free_struck(s), hz, e.velocity, e.pan);
}

// The breeze swings the clapper: now and then it strikes a tube, and in a gust it bounces
// among neighbouring tubes in a little run before the swing dies.
void run_chimes(Score& s) {
    Random& r = s.random;
    s.gust_wait -= 1 / rate;
    if (s.gust_wait <= 0) {
        s.gust_wait = r.between(3, 9);
        s.gust_target = std::pow(r.unit(), 1.5);
    }
    s.gust += (s.gust_target - s.gust) * .00003;
    if (s.run_left > 0) {
        s.run_gap -= 1 / rate;
        if (s.run_gap > 0)
            return;
    } else {
        s.clapper_wait -= 1 / rate;
        if (s.clapper_wait > 0)
            return;
        s.clapper_wait = r.between(2, 9) / (.25 + 2.5 * s.gust);
        s.run_left = 1 + static_cast<int>(r.unit() * (1 + 4 * s.gust));
    }
    // Strike: a neighbour of the last tube more often than not.
    const int step = r.unit() < .7 ? (r.unit() < .5 ? -1 : 1) * (1 + r.pick(2)) : r.pick(6) - 2;
    s.last_tube = std::clamp(s.last_tube + step, 0, 5);
    const double strength = (.25 + .75 * s.gust) * r.between(.45, 1);
    const double pan = .35 + .05 * (s.last_tube - 2.5);
    strike_chime(free_struck(s), midi_hz(tubes[s.last_tube]), strength, pan, r);
    --s.run_left;
    s.run_gap = r.between(.07, .26);
}

void prepare(Score& s, std::uint32_t seed) {
    s.events.reserve(1024);
    s.random.s = seed * 747796405U + 2891336453U;
    if (s.random.s == 0)
        s.random.s = 1;
    s.hall.prepare(3.8);
    // The soundboard's warmth: the body's resonances.
    band(s.body_l[0], 230, 90);
    band(s.body_r[0], 240, 90);
    band(s.body_l[1], 480, 160);
    band(s.body_r[1], 500, 160);
    band(s.body_l[2], 1050, 420);
    band(s.body_r[2], 1100, 420);
    s.phrase_end = static_cast<long>(1.6 * rate);  // a breath before the first phrase
}

void render(Score& s, std::span<float> stereo, double gain, Fade& fade) {
    constexpr int voices = HeavenlyHarp::voices;
    constexpr int harp = HeavenlyHarp::harp;
    constexpr int pad = HeavenlyHarp::pad;
    const std::size_t frames = stereo.size() / 2;
    const float levels[voices] = {.2f, .38f, .12f, .04f};
    const float sends[voices] = {.42f, .5f, .38f, .45f};
    const float body[3] = {.55f, .3f, .14f};
    for (std::size_t f = 0; f < frames; ++f) {
        ++s.clock;
        if (s.next_event >= s.events.size() && s.clock >= s.phrase_end - static_cast<long>(1.4 * beat_seconds * rate))
            plan_phrase(s);
        while (s.next_event < s.events.size() && s.events[s.next_event].at <= s.clock) {
            start(s, s.events[s.next_event]);
            ++s.next_event;
        }
        run_chimes(s);
        float voice_l[voices] = {}, voice_r[voices] = {};
        for (Struck& st : s.struck) {
            if (!st.on)
                continue;
            const float v = run_struck(st, s.sine, s.random);
            voice_l[st.voice] += v * st.left;
            voice_r[st.voice] += v * st.right;
        }
        for (PadNote& n : s.pad)
            run_pad(n, s.sine, voice_l[pad], voice_r[pad]);
        // The harp through its soundboard.
        {
            float add_l = 0, add_r = 0;
            for (std::size_t i = 0; i < 3; ++i) {
                add_l += s.body_l[i].run(voice_l[harp]) * body[i];
                add_r += s.body_r[i].run(voice_r[harp]) * body[i];
            }
            voice_l[harp] += add_l;
            voice_r[harp] += add_r;
        }
        float dry_l = 0, dry_r = 0, send_l = 0, send_r = 0;
        for (int v = 0; v < voices; ++v) {
            if (s.solo >= 0 && s.solo != v)
                continue;
            dry_l += voice_l[v] * levels[v];
            dry_r += voice_r[v] * levels[v];
            send_l += voice_l[v] * levels[v] * sends[v];
            send_r += voice_r[v] * levels[v] * sends[v];
        }
        float wet_l = 0, wet_r = 0;
        s.hall.run(send_l, send_r, wet_l, wet_r);
        const float level = static_cast<float>(gain * fade.next());
        float out_l = (dry_l + wet_l * .6f) * level;
        float out_r = (dry_r + wet_r * .6f) * level;
        out_l = out_l / (1 + std::fabs(out_l) * .3f);
        out_r = out_r / (1 + std::fabs(out_r) * .3f);
        stereo[f * 2] += out_l;
        stereo[f * 2 + 1] += out_r;
    }
}

} // namespace heavenly

HeavenlyHarp::HeavenlyHarp(std::uint32_t seed) : score_(std::make_unique<heavenly::Score>()) {
    heavenly::prepare(*score_, seed);
}

HeavenlyHarp::~HeavenlyHarp() = default;

void HeavenlyHarp::solo(int voice) noexcept {
    (*score_).solo = voice;
}

long HeavenlyHarp::phrases() const noexcept {
    return (*score_).phrases;
}

void HeavenlyHarp::render_add(std::span<float> stereo, double gain) noexcept {
    heavenly::render(*score_, stereo, gain, fade_);
}

} // namespace games::music
