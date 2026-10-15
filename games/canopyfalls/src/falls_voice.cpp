#include "falls_voice.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace cf {
namespace {

constexpr double rate = FallsVoice::sample_rate;
constexpr double tau = 6.283185307179586;

// ---- Small parts ---------------------------------------------------------------------

struct Random {
    std::uint32_t s = 1;
    std::uint32_t bits() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    // 0..1
    double unit() {
        return (bits() >> 8) * (1.0 / 16777216.0);
    }
    // -1..1
    double signed_unit() {
        return unit() * 2 - 1;
    }
    double between(double low, double high) {
        return low + (high - low) * unit();
    }
};

// One period of a sine, 4096 steps and a guard, read with linear interpolation.
struct SineTable {
    std::array<float, 4097> values{};
    SineTable() {
        for (std::size_t i = 0; i < values.size(); ++i)
            values[i] = static_cast<float>(std::sin(tau * static_cast<double>(i) / 4096));
    }
    // `phase` in cycles.
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

// A second-order filter (the Audio EQ Cookbook's), its coefficients set once.
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;
    float run(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

enum class Shape { low, high, band };

void tune(Biquad& f, Shape shape, double hz, double q) {
    const double w = tau * std::min(hz, rate * .45) / rate;
    const double alpha = std::sin(w) / (2 * q);
    const double c = std::cos(w);
    double b0 = 0, b1 = 0, b2 = 0;
    if (shape == Shape::low) {
        b0 = (1 - c) / 2;
        b1 = 1 - c;
        b2 = (1 - c) / 2;
    } else if (shape == Shape::high) {
        b0 = (1 + c) / 2;
        b1 = -(1 + c);
        b2 = (1 + c) / 2;
    } else {
        b0 = alpha;
        b1 = 0;
        b2 = -alpha;
    }
    const double a0 = 1 + alpha;
    f.b0 = static_cast<float>(b0 / a0);
    f.b1 = static_cast<float>(b1 / a0);
    f.b2 = static_cast<float>(b2 / a0);
    f.a1 = static_cast<float>(-2 * c / a0);
    f.a2 = static_cast<float>((1 - alpha) / a0);
}

struct OnePole {
    float a = 0, y = 0;
    void set(double hz) {
        a = static_cast<float>(1 - std::exp(-tau * hz / rate));
    }
    float run(float x) {
        y += a * (x - y);
        return y;
    }
};

// Constant-power gains for a place from -1 (left) to 1 (right).
void pan_gains(double pan, float& left, float& right) {
    const double angle = (std::clamp(pan, -1.0, 1.0) + 1) * .25 * 3.141592653589793;
    left = static_cast<float>(std::cos(angle));
    right = static_cast<float>(std::sin(angle));
}

// A slow random level that glides from one value to the next, a few seconds apart.
struct Drift {
    double from = .5, to = .5, at = 0, span = 3;
    double low_span = 2, high_span = 5;
    double value(Random& random) {
        at += 1 / rate;
        if (at >= span) {
            at -= span;
            from = to;
            to = random.unit();
            span = random.between(low_span, high_span);
        }
        const double t = at / span;
        const double s = t * t * (3 - 2 * t);
        return from + (to - from) * s;
    }
};

// ---- Bubbles -------------------------------------------------------------------------

// A bubble torn open in the water rings at its Minnaert frequency, dies away at the rate
// van den Doel measured (0.13 f + 0.0072 f^1.5 a second), and rises in pitch as it
// shrinks toward the surface.
struct Grain {
    double phase = 0, step = 0, step_change = 0;
    float amp = 0, decay = 1, left = 0, right = 0;
    int remaining = 0;
};

struct Bubbles {
    double low_hz = 300, high_hz = 4000;
    double low_bias = 1.6;     // above 1 favours the larger, lower bubbles
    double level = 1;
    double centre = 0, spread = 1;
    double rise = .1;          // how far a bubble's pitch climbs as it rings
    bool from_zero = false;    // start each at the wave's zero (no click: drops heard alone)
    double owed = 0;
    std::vector<Grain> grains;
    std::size_t live = 0;

    void spawn(Random& random, double loudness, double pan_shift) {
        if (live >= grains.size())
            return;
        const double u = std::pow(random.unit(), low_bias);
        const double hz = low_hz * std::pow(high_hz / low_hz, u);
        const double damping = .13 * hz + .0072 * std::pow(hz, 1.5);
        Grain& g = grains[live];
        ++live;
        g.phase = from_zero ? 0.0 : random.unit();
        g.step = hz / rate;
        g.step_change = hz * rise * damping / (rate * rate);
        g.decay = static_cast<float>(std::exp(-damping / rate));
        g.remaining = static_cast<int>(6.9 / damping * rate);
        const double size = std::pow(hz / 1000, -.4);
        g.amp = static_cast<float>(level * loudness * size * random.between(.25, 1));
        pan_gains(centre + pan_shift + spread * random.signed_unit(), g.left, g.right);
    }
    void run(const SineTable& sine, float& left, float& right) {
        std::size_t i = 0;
        while (i < live) {
            Grain& g = grains[i];
            const float v = g.amp * sine.at(g.phase);
            left += v * g.left;
            right += v * g.right;
            g.phase += g.step;
            g.step += g.step_change;
            g.amp *= g.decay;
            if (--g.remaining <= 0) {
                g = grains[live - 1];
                --live;
                continue;
            }
            ++i;
        }
    }
};

// ---- The roar's flutter --------------------------------------------------------------

// One band of a moving equalizer: a band of the roar's own sound, raised and lowered
// at random, slowly (each change a second or two), its centre wandering slower still;
// left and right move apart. Added back to the roar, it makes the noise itself breathe and
// shimmer from within rather than laying a pulse over it.
struct FlutterBand {
    double low_hz = 300, high_hz = 600;
    Biquad left{}, right{};
    Drift gain_left{}, gain_right{}, centre{};
    int retune = 0;
    float run(Random& random, float in_left, float in_right, float& out_left, float& out_right) {
        if (--retune <= 0) {
            retune = 256;
            const double hz = low_hz * std::pow(high_hz / low_hz, centre.value(random));
            tune(left, Shape::band, hz, 1.4);
            tune(right, Shape::band, hz * 1.04, 1.4);
        } else {
            static_cast<void>(centre.value(random));
        }
        // Gains within about 1.5 dB either way of the band.
        const float gl = static_cast<float>(-.17 + .36 * gain_left.value(random));
        const float gr = static_cast<float>(-.17 + .36 * gain_right.value(random));
        out_left += left.run(in_left) * gl;
        out_right += right.run(in_right) * gr;
        return gl;
    }
};

// ---- Sloshes -------------------------------------------------------------------------

// The pool heaving where the falls land in it: a slow swell of dark, wet sound a
// second or so long, its tone swinging up and back down as the water does, like water
// sloshing in a drum. Several overlap, each from its own place along the base.
struct Swish {
    bool active = false;
    double t = 0, length = 1, peak = 0;
    double low_hz = 200, high_hz = 700;
    float left = 0, right = 0;
    float y1 = 0, y2 = 0;      // two one-pole stages, per swish
    float a = 0;               // their coefficient, refreshed every few samples
    int refresh = 0;
};

float run_swish(Swish& w, Random& random) {
    w.t += 1 / rate;
    if (w.t >= w.length) {
        w.active = false;
        return 0;
    }
    const double x = w.t / w.length;
    if (--w.refresh <= 0) {
        w.refresh = 32;
        // The tone opens as the water swings up and closes as it falls back.
        const double swing = std::sin(3.141592653589793 * x);
        const double hz = w.low_hz + (w.high_hz - w.low_hz) * swing * swing;
        w.a = static_cast<float>(1 - std::exp(-tau * hz / rate));
    }
    const double s = std::sin(3.141592653589793 * x);
    const double env = s * s * w.peak;
    w.y1 += w.a * (static_cast<float>(random.signed_unit()) - w.y1);
    w.y2 += w.a * (w.y1 - w.y2);
    return static_cast<float>(w.y2 * env);
}

// ---- Splashes and rustles ------------------------------------------------------------

// A burst of noise a few milliseconds long: a drop striking water, a leaf brushing a
// leaf. Thousands of them make the crackling surface of falling water that sets it
// apart from wind; a few, bunched by the gusts, make leaves rustle.
struct Burst {
    float amp = 0, decay = 1, left = 0, right = 0;
    int remaining = 0;
    int age = 0, swell = 0;    // samples so far, and over how many it swells in
};

struct Bursts {
    std::vector<Burst> bursts;
    std::size_t live = 0;
    double low_ms = 1, high_ms = 4;
    double level = 1, centre = 0, spread = 1;
    double swell = 0;          // the share of a burst's length it takes to swell in (0: at once)
    double owed = 0;
    void spawn(Random& random, double loudness) {
        if (live >= bursts.size())
            return;
        Burst& b = bursts[live];
        ++live;
        const double ms = random.between(low_ms, high_ms);
        b.decay = static_cast<float>(std::exp(-1 / (ms * .001 * rate)));
        b.remaining = static_cast<int>(ms * .001 * rate * 5);
        b.age = 0;
        b.swell = static_cast<int>(ms * .001 * rate * swell * 2);
        const double u = random.unit();
        b.amp = static_cast<float>(level * loudness * u * u);
        pan_gains(centre + spread * random.signed_unit(), b.left, b.right);
    }
    void run(Random& random, float& left, float& right) {
        std::size_t i = 0;
        while (i < live) {
            Burst& b = bursts[i];
            float v = b.amp * static_cast<float>(random.signed_unit());
            if (b.age < b.swell) {
                const float x = static_cast<float>(b.age) / static_cast<float>(b.swell);
                v *= x * x * (3 - 2 * x);
            }
            ++b.age;
            left += v * b.left;
            right += v * b.right;
            b.amp *= b.decay;
            if (--b.remaining <= 0) {
                b = bursts[live - 1];
                --live;
                continue;
            }
            ++i;
        }
    }
};

// ---- Calls (birds, frogs) ------------------------------------------------------------

// One note of a call: a pitch gliding from `from` to `to` (in `curve`: 1 straight,
// above 1 late, below early), harmonics, vibrato, and a rasp (pulses at `rasp_hz`).
struct Note {
    double from = 1000, to = 1000, curve = 1;
    double seconds = .1, gap = .1;
    double attack = .01, release = .03;
    double vibrato_hz = 0, vibrato = 0;   // vibrato depth as a fraction of pitch
    double tremolo_hz = 0, tremolo = 0;
    double second = 0, third = 0;         // harmonic levels
    double rasp_hz = 0;                   // 0 for a pure tone
    double level = 1;
};

struct Call {
    std::array<Note, 24> notes{};
    int count = 0;
    void add(const Note& note) {
        if (count < static_cast<int>(notes.size())) {
            notes[static_cast<std::size_t>(count)] = note;
            ++count;
        }
    }
};

// Who calls, from how far, how often.
struct Species {
    int kind = 0;
    double wait_low = 10, wait_high = 30;   // seconds between calls
    double distance_low = 20, distance_high = 80;
    double pan_low = -1, pan_high = 1;
    double level = .1;
    double wait = 5;
};

// A call being sung.
struct Singer {
    bool active = false;
    Call call{};
    int note = 0;
    double t = 0;          // seconds into the note (negative: in the gap before it)
    double phase = 0, vibrato_phase = 0, tremolo_phase = 0, rasp_phase = 0;
    float left = 0, right = 0;
    double gain = 0;
    double pan_from = 0, pan_to = 0, travel = 0, age = 0;  // a flock flying across
    OnePole air{};         // the air's softening with distance
    int part = 0;          // which layer it sings in (birds or frogs)
};

// ---- The space -----------------------------------------------------------------------

// A feedback delay network: eight delays mixed by a Hadamard matrix, each damped and
// decaying to give the clearing's tail.
struct Space {
    static constexpr int lines = 8;
    std::array<std::vector<float>, lines> delay{};
    std::array<int, lines> at{};
    std::array<float, lines> feedback{};
    std::array<OnePole, lines> damp{};
    std::vector<float> early;   // a short predelay
    int early_at = 0;

    void prepare(double seconds) {
        const int lengths[lines] = {2969, 3203, 3449, 3701, 4027, 4363, 4673, 5011};
        for (int i = 0; i < lines; ++i) {
            const std::size_t k = static_cast<std::size_t>(i);
            delay[k].assign(static_cast<std::size_t>(lengths[i]), 0.0f);
            at[k] = 0;
            feedback[k] = static_cast<float>(std::pow(10.0, -3.0 * lengths[i] / (seconds * rate)));
            damp[k].set(5200);
        }
        early.assign(static_cast<std::size_t>(rate * .045), 0.0f);
    }
    void run(float in_left, float in_right, float& out_left, float& out_right) {
        const float input = (in_left + in_right) * .5f;
        const float delayed = early[static_cast<std::size_t>(early_at)];
        early[static_cast<std::size_t>(early_at)] = input;
        early_at = (early_at + 1) % static_cast<int>(early.size());
        std::array<float, lines> out{};
        for (int i = 0; i < lines; ++i) {
            const std::size_t k = static_cast<std::size_t>(i);
            out[k] = delay[k][static_cast<std::size_t>(at[k])];
        }
        // Hadamard mixing, normalized.
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
        const float norm = .35355339f;  // 1 / sqrt(8)
        for (int i = 0; i < lines; ++i) {
            const std::size_t k = static_cast<std::size_t>(i);
            const float fed = damp[k].run(mixed[k] * norm) * feedback[k];
            delay[k][static_cast<std::size_t>(at[k])] = fed + delayed * (i % 2 == 0 ? .5f : -.5f);
            at[k] = (at[k] + 1) % static_cast<int>(delay[k].size());
        }
        out_left = (out[0] + out[2] + out[4] + out[6]) * .5f;
        out_right = (out[1] + out[3] + out[5] + out[7]) * .5f;
    }
};

} // namespace

// ---- The voice's state ---------------------------------------------------------------

struct FallsVoice::State {
    Random random{};
    SineTable sine{};
    int solo = -1;
    unsigned muted = 0;
    double time = 0;

    // roar
    Bubbles body{};          // the curtain's countless bubbles
    Bubbles sparkle{};       // small high ones in the spray
    Bursts splash{};         // drops striking water: the curtain's crackle
    std::array<Swish, 28> swishes{};  // the pool churning at the base
    double swish_wait = .5;
    std::array<FlutterBand, 5> flutter{};
    Biquad splash_l{}, splash_r{};
    Drift surge_left{}, surge_right{};
    Biquad rumble_l[2]{}, rumble_r[2]{};
    Biquad hiss_l{}, hiss_r{};
    OnePole distance_l[2]{}, distance_r[2]{};
    double thump_wait = 2, thump_env = 0, thump_phase = 0, thump_hz = 50;

    // pool
    Biquad lap_l{}, lap_r{};
    double lap_wait = 1, lap_env = 0, lap_target = 0;
    float lap_pan_l = .7f, lap_pan_r = .7f;

    // birds and frogs
    std::array<Species, 9> species{};
    std::array<Singer, 8> singers{};

    // insects
    double cicada_wait = 8, cicada_t = -1, cicada_length = 10;
    Biquad cicada_a{}, cicada_b{};
    float cicada_pan_l = .7f, cicada_pan_r = .7f;
    double cicada_phase = 0;
    struct Cricket {
        double hz = 4800, period = .6, offset = 0, level = 1;
        double phase = 0;
        float left = 0, right = 0;
    };
    std::array<Cricket, 5> crickets{};

    // air
    Drift gust{};
    Biquad wind_l{}, wind_r{}, leaves_l{}, leaves_r{};
    Bursts rustle{};         // leaves brushing leaves
    Bubbles drips{};
    double drip_wait = 1;

    Space space{};
};

namespace {

// ---- Calls, as the forest's species sing them -----------------------------------------

enum Kind : int { piha, tinamou, trogon, toucan, parakeets, songbird, tree_frog, pool_frog, piha_answer };

void sing(Call& call, int kind, Random& r) {
    call.count = 0;
    Note n;
    if (kind == piha || kind == piha_answer) {
        // "wee-wee-WHEE-oo": two quick rising notes, then the ringing sweep up and down.
        for (int i = 0; i < 2; ++i) {
            n = Note{};
            n.from = 1450 * r.between(.97, 1.03);
            n.to = 2250;
            n.seconds = .09;
            n.gap = .13;
            n.second = .15;
            n.level = .55;
            call.add(n);
        }
        n = Note{};
        n.from = 1250;
        n.to = 3350;
        n.curve = 1.6;
        n.seconds = .26;
        n.gap = .0;
        n.second = .2;
        n.release = .01;
        call.add(n);
        n = Note{};
        n.from = 3300;
        n.to = 1900;
        n.curve = .7;
        n.seconds = .32;
        n.gap = .5;
        n.second = .18;
        n.attack = .005;
        call.add(n);
    } else if (kind == tinamou) {
        // Long tremulous whistles, each a little lower.
        const int count = 3 + static_cast<int>(r.unit() * 3);
        double hz = r.between(1350, 1550);
        for (int i = 0; i < count; ++i) {
            n = Note{};
            n.from = hz * 1.02;
            n.to = hz * .97;
            n.seconds = r.between(.75, 1.0);
            n.gap = r.between(.45, .7);
            n.attack = .12;
            n.release = .25;
            n.tremolo_hz = 7;
            n.tremolo = .35;
            n.vibrato_hz = 5;
            n.vibrato = .006;
            call.add(n);
            hz *= .955;
        }
    } else if (kind == trogon) {
        // Soft "cow" notes, quickening.
        const int count = 8 + static_cast<int>(r.unit() * 6);
        double gap = .17;
        for (int i = 0; i < count; ++i) {
            n = Note{};
            n.from = 640;
            n.to = 560;
            n.seconds = .085;
            n.gap = gap;
            n.second = .45;
            n.third = .2;
            n.attack = .015;
            call.add(n);
            gap *= .95;
        }
    } else if (kind == toucan) {
        // Hoarse yelps.
        const int count = 3 + static_cast<int>(r.unit() * 4);
        for (int i = 0; i < count; ++i) {
            n = Note{};
            n.from = 650;
            n.to = 900;
            n.curve = .6;
            n.seconds = .22;
            n.gap = r.between(.3, .45);
            n.second = .6;
            n.third = .35;
            n.rasp_hz = 55;
            n.attack = .02;
            call.add(n);
        }
    } else if (kind == parakeets) {
        // A flock's chatter: many short shrill squeaks.
        const int count = 16 + static_cast<int>(r.unit() * 8);
        for (int i = 0; i < count; ++i) {
            n = Note{};
            n.from = r.between(2600, 4200);
            n.to = n.from * r.between(.75, 1.3);
            n.seconds = r.between(.04, .09);
            n.gap = r.between(.02, .16);
            n.second = .3;
            n.rasp_hz = r.between(80, 160);
            n.attack = .004;
            n.release = .01;
            n.level = r.between(.5, 1);
            call.add(n);
        }
    } else if (kind == songbird) {
        // A bright trill of quick falling notes.
        const int count = 12 + static_cast<int>(r.unit() * 8);
        const double top = r.between(4600, 5600);
        for (int i = 0; i < count; ++i) {
            n = Note{};
            n.from = top;
            n.to = top * .78;
            n.seconds = .045;
            n.gap = .022;
            n.attack = .004;
            n.release = .01;
            n.level = .7 + .3 * std::sin(i * .5);
            call.add(n);
        }
    } else if (kind == tree_frog) {
        // Two quick chirps.
        const int count = 2 + static_cast<int>(r.unit() * 3);
        for (int i = 0; i < count; ++i) {
            n = Note{};
            n.from = 1300;
            n.to = 2300;
            n.seconds = .05;
            n.gap = .09;
            n.second = .3;
            n.rasp_hz = 140;
            n.attack = .004;
            n.release = .012;
            call.add(n);
        }
    } else {
        // A pool frog's "gunk": a low twang that drops as it rings.
        const int count = 1 + static_cast<int>(r.unit() * 2);
        for (int i = 0; i < count; ++i) {
            n = Note{};
            n.from = r.between(200, 240);
            n.to = n.from * .88;
            n.curve = .5;
            n.seconds = .22;
            n.gap = .35;
            n.second = .7;
            n.third = .45;
            n.rasp_hz = 0;
            n.attack = .004;
            n.release = .15;
            call.add(n);
        }
    }
}

void prepare_species(std::array<Species, 9>& s) {
    // kind, wait low/high, distance low/high, pan low/high, level
    const double table[9][8] = {
        {piha, 9, 22, 60, 140, -1, .2, .30},
        {tinamou, 25, 50, 50, 110, -.9, .9, .10},
        {trogon, 18, 35, 25, 60, -1, 1, .10},
        {toucan, 22, 45, 30, 90, -1, 1, .10},
        {parakeets, 40, 80, 20, 60, -1, 1, .06},
        {songbird, 9, 22, 10, 30, -1, 1, .045},
        {tree_frog, 4, 12, 8, 30, -1, 1, .045},
        {pool_frog, 10, 28, 12, 25, -.4, .4, .16},
        {piha_answer, 9, 22, 90, 160, .3, 1, .22},
    };
    for (std::size_t i = 0; i < s.size(); ++i) {
        s[i].kind = static_cast<int>(table[i][0]);
        s[i].wait_low = table[i][1];
        s[i].wait_high = table[i][2];
        s[i].distance_low = table[i][3];
        s[i].distance_high = table[i][4];
        s[i].pan_low = table[i][5];
        s[i].pan_high = table[i][6];
        s[i].level = table[i][7];
        s[i].wait = table[i][1] * (.2 + .8 * static_cast<double>(i) / 9);
    }
}

int species_part(int kind) {
    return kind == tree_frog || kind == pool_frog ? FallsVoice::frogs : FallsVoice::birds;
}

// One sample of a singer, or silence once its call is done.
float run_singer(Singer& s, const SineTable& sine) {
    if (!s.active)
        return 0;
    s.t += 1 / rate;
    s.age += 1 / rate;
    const Note& n = s.call.notes[static_cast<std::size_t>(s.note)];
    if (s.t >= n.seconds + n.gap) {
        ++s.note;
        s.t = 0;
        if (s.note >= s.call.count) {
            s.active = false;
            return 0;
        }
        return 0;
    }
    if (s.t >= n.seconds)
        return 0;
    const double x = std::pow(s.t / n.seconds, n.curve);
    double hz = n.from + (n.to - n.from) * x;
    s.vibrato_phase += n.vibrato_hz / rate;
    hz *= 1 + n.vibrato * sine.at(s.vibrato_phase);
    s.phase += hz / rate;
    double env = std::min(1.0, s.t / n.attack) * std::min(1.0, (n.seconds - s.t) / n.release);
    s.tremolo_phase += n.tremolo_hz / rate;
    env *= 1 - n.tremolo * (.5 + .5 * sine.at(s.tremolo_phase));
    double v = sine.at(s.phase) + n.second * sine.at(s.phase * 2) + n.third * sine.at(s.phase * 3);
    if (n.rasp_hz > 0) {
        s.rasp_phase += n.rasp_hz / rate;
        const double pulse = .5 + .5 * sine.at(s.rasp_phase);
        v *= pulse * pulse * pulse * 1.8;
    }
    return static_cast<float>(v * env * n.level * s.gain);
}

} // namespace

FallsVoice::FallsVoice(std::uint32_t seed) : state_(std::make_unique<State>()) {
    State& s = *state_;
    s.random.s = seed * 2654435761U + 12345U;
    if (s.random.s == 0)
        s.random.s = 1;
    // The curtain: bubbles from a few hundred hertz to the spray's few thousand, the
    // lower ones louder; spread across the falls' width ahead.
    s.body.grains.resize(900);
    s.body.low_hz = 420;
    s.body.high_hz = 7000;
    s.body.low_bias = 1.1;
    s.body.level = .008;
    s.body.spread = .62;
    s.body.rise = .04;
    s.sparkle.grains.resize(300);
    s.sparkle.low_hz = 2500;
    s.sparkle.high_hz = 9000;
    s.sparkle.low_bias = 1.2;
    s.sparkle.level = .006;
    s.sparkle.spread = .7;
    s.sparkle.rise = .15;
    s.surge_left.low_span = 1.5;
    s.surge_left.high_span = 4.5;
    s.surge_right = s.surge_left;
    s.surge_right.to = .4;
    for (int i = 0; i < 2; ++i) {
        tune(s.rumble_l[i], Shape::low, 95, .75);
        tune(s.rumble_r[i], Shape::low, 95, .75);
    }
    tune(s.hiss_l, Shape::band, 3400, .55);
    tune(s.hiss_r, Shape::band, 3600, .55);
    // Distance and mist: the higher the sound, the more of it is lost.
    for (int i = 0; i < 2; ++i) {
        s.distance_l[i].set(3000);
        s.distance_r[i].set(3000);
    }
    s.splash.bursts.resize(160);
    s.splash.low_ms = .6;
    s.splash.high_ms = 3.5;
    s.splash.level = .055;
    s.splash.spread = .65;
    tune(s.splash_l, Shape::band, 2600, .5);
    tune(s.splash_r, Shape::band, 2800, .5);
    const double bands[5][2] = {{230, 420}, {420, 800}, {800, 1500}, {1500, 2800}, {2800, 5200}};
    for (std::size_t i = 0; i < s.flutter.size(); ++i) {
        FlutterBand& b = s.flutter[i];
        b.low_hz = bands[i][0];
        b.high_hz = bands[i][1];
        b.gain_left.low_span = .7;
        b.gain_left.high_span = 2.6;
        b.gain_right = b.gain_left;
        b.gain_right.to = .3;
        b.centre.low_span = 3;
        b.centre.high_span = 9;
        b.retune = static_cast<int>(i) * 50;
    }
    s.rustle.bursts.resize(48);
    s.rustle.low_ms = 4;
    s.rustle.high_ms = 25;
    s.rustle.level = .06;
    s.rustle.spread = .95;
    s.rustle.swell = .6;
    // The pool below: its lap against the piles.
    tune(s.lap_l, Shape::low, 380, .9);
    tune(s.lap_r, Shape::low, 420, .9);
    prepare_species(s.species);
    tune(s.cicada_a, Shape::band, 4900, 4);
    tune(s.cicada_b, Shape::band, 4900, 4);
    for (std::size_t i = 0; i < s.crickets.size(); ++i) {
        State::Cricket& c = s.crickets[i];
        c.hz = s.random.between(4300, 5200);
        c.period = s.random.between(.5, .8);
        c.offset = s.random.unit() * c.period;
        c.level = s.random.between(.5, 1);
        pan_gains(s.random.between(-.9, .9), c.left, c.right);
    }
    s.gust.low_span = 4;
    s.gust.high_span = 11;
    tune(s.wind_l, Shape::band, 900, .45);
    tune(s.wind_r, Shape::band, 1100, .45);
    tune(s.leaves_l, Shape::band, 2400, .6);
    tune(s.leaves_r, Shape::band, 2700, .6);
    s.drips.grains.resize(16);
    s.drips.low_hz = 1500;
    s.drips.high_hz = 3600;
    s.drips.low_bias = 1;
    s.drips.level = .05;
    s.drips.spread = .9;
    s.drips.rise = -2.5;  // a drop's pitch falls as it splashes
    s.drips.from_zero = true;
    s.space.prepare(2.4);
}

FallsVoice::~FallsVoice() = default;

void FallsVoice::solo(int part) {
    (*state_).solo = part;
}

void FallsVoice::mute_roar(unsigned pieces) {
    (*state_).muted = pieces;
}

void FallsVoice::render_add(std::span<float> stereo, double gain) {
    State& s = *state_;
    Random& r = s.random;
    const std::size_t frames = stereo.size() / 2;
    float parts_l[parts] = {}, parts_r[parts] = {};
    const float sends[parts] = {.32f, .16f, .55f, .25f, .3f, .22f};
    for (std::size_t f = 0; f < frames; ++f) {
        s.time += 1 / rate;
        for (int p = 0; p < parts; ++p) {
            parts_l[p] = 0;
            parts_r[p] = 0;
        }
        // -- the roar -----------------------------------------------------------------
        {
            const double surge_l = s.surge_left.value(r);
            const double surge_r = s.surge_right.value(r);
            const double surge = (surge_l + surge_r) * .5;
            // Bubbles come in with the surges, a little to the side that swells.
            s.body.owed += (9000 + 600 * surge) / rate;
            while (s.body.owed >= 1) {
                s.body.owed -= 1;
                s.body.spawn(r, .97 + .06 * surge, (surge_r - surge_l) * .05);
            }
            s.sparkle.owed += (1500 + 2500 * surge) / rate;
            while (s.sparkle.owed >= 1) {
                s.sparkle.owed -= 1;
                s.sparkle.spawn(r, 1, 0);
            }
            const unsigned m = s.muted;
            float bl = 0, br = 0;
            s.body.run(s.sine, bl, br);
            if (m & 1U)
                bl = br = 0;
            float spray_l = 0, spray_r = 0;
            s.sparkle.run(s.sine, spray_l, spray_r);
            if ((m & 2U) == 0) {
                bl += spray_l;
                br += spray_r;
            }
            s.splash.owed += (4500 + 300 * surge) / rate;
            while (s.splash.owed >= 1) {
                s.splash.owed -= 1;
                s.splash.spawn(r, 1);
            }
            float sl = 0, sr = 0;
            s.splash.run(r, sl, sr);
            sl = s.splash_l.run(sl);
            sr = s.splash_r.run(sr);
            if ((m & 4U) == 0) {
                bl += sl;
                br += sr;
            }
            // The pool churning at the base: many faint slow sloshes overlapping
            // evenly, so the churn is steady while it moves within.
            s.swish_wait -= 1 / rate;
            if (s.swish_wait <= 0) {
                s.swish_wait = r.between(.1, .2);
                for (Swish& w : s.swishes) {
                    if (w.active)
                        continue;
                    w.active = true;
                    w.t = 0;
                    w.length = r.between(1.6, 2.6);
                    w.peak = r.between(.75, 1) * .045;
                    w.low_hz = r.between(140, 220);
                    w.high_hz = r.between(500, 900);
                    w.refresh = 0;
                    pan_gains(r.between(-.6, .6), w.left, w.right);
                    break;
                }
            }
            float wl = 0, wr = 0;
            for (Swish& w : s.swishes) {
                if (!w.active)
                    continue;
                const float v = run_swish(w, r);
                wl += v * w.left;
                wr += v * w.right;
            }
            if ((m & 8U) == 0) {
                bl += wl;
                br += wr;
            }
            // The spray's hiss and the impact's rumble, both breathing with the surges.
            const float nl = static_cast<float>(r.signed_unit());
            const float nr = static_cast<float>(r.signed_unit());
            const float hiss_l = s.hiss_l.run(nl) * static_cast<float>(.045 * (.95 + .1 * surge_l));
            const float hiss_r = s.hiss_r.run(nr) * static_cast<float>(.045 * (.95 + .1 * surge_r));
            float rl = s.rumble_l[1].run(s.rumble_l[0].run(nl));
            float rr = s.rumble_r[1].run(s.rumble_r[0].run(nr));
            rl *= static_cast<float>(.16 * (.95 + .1 * surge_l));
            rr *= static_cast<float>(.16 * (.95 + .1 * surge_r));
            // Now and then a great mass of water lands: a deep thump.
            s.thump_wait -= 1 / rate;
            if (s.thump_wait <= 0) {
                s.thump_wait = r.between(.6, 3.5) / (.5 + surge);
                s.thump_env = r.between(.4, 1);
                s.thump_hz = r.between(38, 70);
            }
            s.thump_env *= .99985;
            s.thump_phase += s.thump_hz / rate;
            const float thump = static_cast<float>(s.thump_env * s.thump_env * .035 * s.sine.at(s.thump_phase));
            const float hiss_on = (m & 16U) ? 0.0f : 1.0f;
            const float low_on = (m & 32U) ? 0.0f : 1.0f;
            float left = bl + hiss_l * hiss_on + (rl + thump) * low_on;
            float right = br + hiss_r * hiss_on + (rr + thump) * low_on;
            // The roar's own sound fluttering in its bands.
            float moved_l = left, moved_r = right;
            for (FlutterBand& band : s.flutter)
                static_cast<void>(band.run(r, left, right, moved_l, moved_r));
            if ((m & 64U) == 0) {
                left = moved_l;
                right = moved_r;
            }
            // 85 m of air takes the top off it.
            left = s.distance_l[1].run(s.distance_l[0].run(left));
            right = s.distance_r[1].run(s.distance_r[0].run(right));
            parts_l[roar] = left * 1.45f;
            parts_r[roar] = right * 1.45f;
        }
        // -- the pool below -------------------------------------------------------------
        {
            // From the walkway, high over the pool, its bubbles are lost under the
            // falls; what carries up is the water lapping at the piles.
            float pl = 0, pr = 0;
            // The pool's swell lapping at the piles.
            s.lap_wait -= 1 / rate;
            if (s.lap_wait <= 0) {
                s.lap_wait = r.between(1.6, 4.2);
                s.lap_target = r.between(.4, 1);
                pan_gains(r.between(-.7, .7), s.lap_pan_l, s.lap_pan_r);
            }
            s.lap_env += (s.lap_target - s.lap_env) * (s.lap_target > s.lap_env ? 18 : 2.2) / rate;
            if (s.lap_env > s.lap_target * .97)
                s.lap_target = 0;
            const float lap = static_cast<float>(s.lap_env * s.lap_env * .14);
            pl += s.lap_l.run(static_cast<float>(r.signed_unit())) * lap * s.lap_pan_l;
            pr += s.lap_r.run(static_cast<float>(r.signed_unit())) * lap * s.lap_pan_r;
            parts_l[pool] = pl;
            parts_r[pool] = pr;
        }
        // -- birds and frogs ------------------------------------------------------------
        for (Species& sp : s.species) {
            sp.wait -= 1 / rate;
            if (sp.wait > 0)
                continue;
            sp.wait = r.between(sp.wait_low, sp.wait_high);
            for (Singer& singer : s.singers) {
                if (singer.active)
                    continue;
                singer.active = true;
                sing(singer.call, sp.kind, r);
                singer.note = 0;
                singer.t = 0;
                singer.age = 0;
                singer.phase = r.unit();
                const double distance = r.between(sp.distance_low, sp.distance_high);
                singer.gain = sp.level * 25 / (distance + 15);
                singer.air.set(std::max(2500.0, 16000 - distance * 90));
                singer.air.y = 0;
                singer.pan_from = r.between(sp.pan_low, sp.pan_high);
                singer.pan_to = sp.kind == parakeets ? -singer.pan_from + r.between(-.3, .3) : singer.pan_from;
                singer.travel = sp.kind == parakeets ? 2.5 : 1;
                pan_gains(singer.pan_from, singer.left, singer.right);
                singer.part = species_part(sp.kind);
                break;
            }
        }
        for (Singer& singer : s.singers) {
            if (!singer.active)
                continue;
            const int part = singer.part;
            if (singer.travel > 1) {
                const double t = std::min(1.0, singer.age / singer.travel);
                pan_gains(singer.pan_from + (singer.pan_to - singer.pan_from) * t, singer.left, singer.right);
            }
            const float v = singer.air.run(run_singer(singer, s.sine));
            parts_l[part] += v * singer.left;
            parts_r[part] += v * singer.right;
        }
        // -- insects --------------------------------------------------------------------
        {
            float il = 0, ir = 0;
            // A cicada: swells, sings its pulsed buzz, fades; then quiet a while.
            if (s.cicada_t < 0) {
                s.cicada_wait -= 1 / rate;
                if (s.cicada_wait <= 0) {
                    s.cicada_t = 0;
                    s.cicada_length = r.between(8, 15);
                    pan_gains(r.unit() < .5 ? r.between(-.9, -.4) : r.between(.4, .9), s.cicada_pan_l, s.cicada_pan_r);
                }
            } else {
                s.cicada_t += 1 / rate;
                const double t = s.cicada_t;
                const double swell = std::min(1.0, t / 3.5) * std::min(1.0, std::max(0.0, (s.cicada_length - t) / 2.5));
                s.cicada_phase += 118 / rate;
                const double pulse = std::pow(.5 + .5 * s.sine.at(s.cicada_phase), 4);
                const float buzz = s.cicada_b.run(s.cicada_a.run(static_cast<float>(r.signed_unit())));
                const float c = static_cast<float>(buzz * pulse * swell * swell * .55);
                il += c * s.cicada_pan_l;
                ir += c * s.cicada_pan_r;
                if (t >= s.cicada_length) {
                    s.cicada_t = -1;
                    s.cicada_wait = r.between(14, 40);
                }
            }
            // Crickets: three quick pulses, over and over.
            for (State::Cricket& c : s.crickets) {
                const double in_chirp = std::fmod(s.time + c.offset, c.period);
                if (in_chirp < .1) {
                    const double pulse_at = std::fmod(in_chirp, .034);
                    const double env = pulse_at < .016 ? std::sin(pulse_at / .016 * 3.14159) : 0.0;
                    c.phase += c.hz / rate;
                    const float v = static_cast<float>(env * .0035 * c.level * s.sine.at(c.phase));
                    il += v * c.left;
                    ir += v * c.right;
                }
            }
            parts_l[insects] = il;
            parts_r[insects] = ir;
        }
        // -- the air: wind in the canopy, leaves, drops ---------------------------------
        {
            const double g = s.gust.value(r);
            const double strength = .15 + .85 * g * g;
            const float wl = s.wind_l.run(static_cast<float>(r.signed_unit()));
            const float wr = s.wind_r.run(static_cast<float>(r.signed_unit()));
            // Leaves brush one another at random moments, more of them as a gust builds.
            s.rustle.owed += (25 + 420 * strength * strength) / rate;
            while (s.rustle.owed >= 1) {
                s.rustle.owed -= 1;
                s.rustle.spawn(r, .4 + .6 * strength);
            }
            float rustle_l = 0, rustle_r = 0;
            s.rustle.run(r, rustle_l, rustle_r);
            const float ll = s.leaves_l.run(rustle_l);
            const float lr = s.leaves_r.run(rustle_r);
            float al = static_cast<float>(wl * .11 * strength) + ll;
            float ar = static_cast<float>(wr * .11 * strength) + lr;
            s.drip_wait -= 1 / rate;
            if (s.drip_wait <= 0) {
                s.drip_wait = r.between(.25, 2.2);
                s.drips.spawn(r, r.between(.3, 1), 0);
            }
            s.drips.run(s.sine, al, ar);
            parts_l[air] = al;
            parts_r[air] = ar;
        }
        // -- the mix, the space, out ----------------------------------------------------
        float dry_l = 0, dry_r = 0, send_l = 0, send_r = 0;
        for (int p = 0; p < parts; ++p) {
            if (s.solo >= 0 && s.solo != p)
                continue;
            dry_l += parts_l[p];
            dry_r += parts_r[p];
            send_l += parts_l[p] * sends[p];
            send_r += parts_r[p] * sends[p];
        }
        float wet_l = 0, wet_r = 0;
        s.space.run(send_l, send_r, wet_l, wet_r);
        float out_l = (dry_l + wet_l * .55f) * static_cast<float>(gain);
        float out_r = (dry_r + wet_r * .55f) * static_cast<float>(gain);
        // A gentle ceiling: never a hard clip.
        out_l = out_l / (1 + std::fabs(out_l) * .25f);
        out_r = out_r / (1 + std::fabs(out_r) * .25f);
        stereo[f * 2] += out_l;
        stereo[f * 2 + 1] += out_r;
    }
}

} // namespace cf
