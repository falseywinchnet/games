#include "sheep_cues.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>

// Pen the Sheep's four music beds and its two little tunes, rendered live: meadow_music.py
// ("Pasture Haze" and the sh_tune_* stingers), long_grass.py ("Long Grass") and
// angel_music.py ("Cloud Choir" and "Morning Bells"), each followed line for line in
// numpy's order of operations. Every output starts from the generator states its script
// had reached when it began it (games/penthesheep/tools/music_reference.py printed them),
// and is the clipped stereo master the script wrote before 16-bit quantisation.
namespace sh {
namespace {

constexpr double two_pi = 2 * voice::pi;
constexpr int sr = voice::sample_rate;

using voice::Signal;
using voice::Stereo;

Stereo silence(std::size_t n) {
    Stereo result{Signal(n, 0.0), Signal(n, 0.0)};
    return result;
}

double square(double x) {
    const double result = x * x;
    return result;
}

// bus_rms_db(b): 20 log10(max(sqrt(mean(b.mean(0) ** 2)), 1e-9))
double rms_db(const Stereo& b) {
    const std::size_t n = b.left.size();
    Signal squares(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double m = (b.left[i] + b.right[i]) / 2;
        squares[i] = m * m;
    }
    const double r = std::sqrt(voice::sum(squares) / static_cast<double>(n));
    const double result = 20 * std::log10(std::max(r, 1e-9));
    return result;
}

// finish(x, target_rms_db, ceiling_db), then write()'s np.clip(x, -1, 1)
Stereo finish(Stereo x, double target_rms_db, double ceiling_db) {
    const double g = std::pow(10.0, (target_rms_db - rms_db(x)) / 20);
    const double c = std::pow(10.0, ceiling_db / 20);
    for (Signal* channel : {&x.left, &x.right}) {
        for (double& v : *channel) {
            v = v * g;
            v = std::tanh(v / c) * c;
            v = std::clamp(v, -1.0, 1.0);
        }
    }
    return x;
}

// np.stack([fft_filter(c, lo, hi, order, circular=circular) for c in x])
Stereo filtered(const Stereo& x, double lo, double hi, int order, bool circular) {
    Stereo result{voice::fft_filter(x.left, lo, hi, order, 4096, circular),
                  voice::fft_filter(x.right, lo, hi, order, 4096, circular)};
    return result;
}

Stereo scaled(const Stereo& x, double g) {
    Stereo result = x;
    for (Signal* channel : {&result.left, &result.right}) {
        for (double& v : *channel) {
            v = v * g;
        }
    }
    return result;
}

// mid, side = (l + r) / 2, (l - r) / 2; np.stack([mid + side * width, mid - side * width])
Stereo widened(const Stereo& x, double width) {
    const std::size_t n = x.left.size();
    Stereo result = silence(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double mid = (x.left[i] + x.right[i]) / 2;
        const double side = (x.left[i] - x.right[i]) / 2;
        result.left[i] = mid + side * width;
        result.right[i] = mid - side * width;
    }
    return result;
}

// out[:, -fade:] *= np.linspace(1, 0, fade)
void fade_out(Stereo& out, double seconds) {
    const std::size_t n = out.left.size();
    const std::size_t fade = voice::samples(seconds);
    const Signal down = voice::linspace(1, 0, fade);
    for (std::size_t i = 0; i < fade; ++i) {
        out.left[n - fade + i] *= down[i];
        out.right[n - fade + i] *= down[i];
    }
}

// A little chirping figure, as both meadow scripts write it: f0 bends by `bend` and
// wavers at 40 Hz, inside a sine-squared swell.
Signal chirp(std::size_t n, double f0, double bend) {
    const Signal tt = voice::tarr(n);
    const double last = tt[n - 1];
    const double w = two_pi * 40;
    Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        f[i] = f0 * (1 + bend * tt[i] / last) * (1 + .05 * std::sin(w * tt[i]));
    }
    const Signal cs = voice::cumulative_sum(f);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = std::sin(two_pi * cs[i] / sr) * square(std::sin(voice::pi * tt[i] / last));
    }
    return y;
}

// ======================================================================== meadow_music.py
namespace meadow {

constexpr double loop_seconds = 48.0;
constexpr int flat = -4;  // cents: the original sits a little flat

double hz(int m, double cents = 0.0) {
    const double result = 440 * std::pow(2.0, ((m - 69) + (cents + flat) / 100) / 12);
    return result;
}

// ensemble_tone: one soft tone as a chorus of detuned near-sines, swelling in, holding, fading
Stereo ensemble_tone(voice::Generator& rng, int m, double attack, double hold, double release, double vel = 1.0) {
    const int voices = 5;
    const double spread = 14;
    const double dur = attack + hold + release;
    const std::size_t n = voice::samples(dur);
    const Signal t = voice::tarr(n);
    Signal env(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double rise = t[i] < attack ? square(std::sin(voice::pi / 2 * t[i] / attack)) : 1.0;
        const double clipped = std::clamp((t[i] - attack - hold) / release, 0.0, 1.0);
        const double fall = t[i] > attack + hold ? square(std::cos(voice::pi / 2 * clipped)) : 1.0;
        env[i] = rise * fall;
    }
    Signal left(n, 0.0);
    Signal right(n, 0.0);
    Signal f(n);
    for (int v = 0; v < voices; ++v) {
        const double c = rng.uniform(-spread, spread);
        const double base = hz(m, c);
        const double drift = two_pi * rng.uniform(.25, .6);
        const double drift_phase = rng.uniform(0, 6.28);
        for (std::size_t i = 0; i < n; ++i) {
            f[i] = base * (1 + .0035 * std::sin(drift * t[i] + drift_phase));
        }
        const Signal cs = voice::cumulative_sum(f);
        const double phase0 = rng.uniform(0, 6.28);
        const double phase2 = rng.uniform(0, 6.28);
        const double p = rng.uniform(-1, 1);
        const double gl = std::cos((p + 1) * voice::pi / 4);
        const double gr = std::sin((p + 1) * voice::pi / 4);
        for (std::size_t i = 0; i < n; ++i) {
            const double ph = two_pi * cs[i] / sr + phase0;
            const double y = std::sin(ph) + .32 * std::sin(2 * ph + phase2) + .06 * std::sin(3 * ph);
            left[i] += y * gl;
            right[i] += y * gr;
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        left[i] = left[i] * env[i] * vel / voices;
        right[i] = right[i] * env[i] * vel / voices;
    }
    Stereo result{std::move(left), std::move(right)};
    return result;
}

struct Section {
    int from;
    int to;
    std::vector<std::pair<int, double>> weights;
    int bass[3];
};

void add_wrapped(Signal& bus, const Signal& seg, std::size_t start, std::size_t length) {
    for (std::size_t k = 0; k < seg.size(); k += length) {
        const std::size_t count = std::min(length, seg.size() - k);
        const std::size_t s = (start + k) % length;
        const std::size_t e = std::min(length, s + count);
        for (std::size_t i = 0; i < e - s; ++i) {
            bus[s + i] += seg[k + i];
        }
        if (e - s < count) {
            for (std::size_t i = 0; i < count - (e - s); ++i) {
                bus[i] += seg[k + (e - s) + i];
            }
        }
    }
}

voice::LoopMix arrange(voice::Generator& rng) {
    const std::size_t length = voice::samples(loop_seconds);
    voice::LoopMix mix(length, true);
    const Section sections[4] = {
        {0, 16, {{60, 2}, {64, 3}, {67, 3}, {69, 2}, {72, 3}, {74, 2}, {76, 3}, {79, 2}, {81, 1}, {84, 1}}, {36, 48, 55}},
        {16, 28, {{62, 1}, {64, 2}, {67, 3}, {69, 3}, {72, 3}, {74, 2}, {76, 3}, {79, 2}, {81, 2}, {84, 1}}, {36, 48, 55}},
        {28, 40, {{60, 2}, {64, 3}, {67, 2}, {69, 3}, {72, 3}, {76, 3}, {79, 2}, {81, 2}, {84, 1}}, {36, 48, 57}},
        {40, 48, {{60, 2}, {64, 3}, {67, 3}, {69, 2}, {72, 3}, {76, 3}, {79, 2}, {81, 1}, {84, 1}}, {36, 48, 55}},
    };
    for (const Section& section : sections) {
        Signal p{};
        for (const std::pair<int, double>& w : section.weights) {
            p.push_back(w.second);
        }
        const double total = voice::sum(p);
        for (double& value : p) {
            value /= total;
        }
        double t = section.from;
        while (t < section.to) {
            const int m = section.weights[rng.choice(p)].first;
            const double hold = rng.uniform(1.2, 3.5);
            const double att = rng.uniform(.5, 1.1);
            const double rel = rng.uniform(1.8, 3.2);
            const double vel = rng.uniform(.5, 1.0);
            Stereo y = ensemble_tone(rng, m, att, hold, rel, vel);
            const double r = rng.uniform(0, 1);
            const int fifth = m - 7;
            const int fifth_class = static_cast<int>(voice::python_modulo(fifth, 12));
            const bool consonant = fifth_class == 0 || fifth_class == 2 || fifth_class == 4 || fifth_class == 7 ||
                                   fifth_class == 9;
            if (r < .25 && m - 12 >= 55) {  // with its octave below
                const Stereo below = ensemble_tone(rng, m - 12, att, hold, rel, vel * .6);
                for (std::size_t i = 0; i < y.left.size(); ++i) {
                    y.left[i] = y.left[i] + below.left[i];
                    y.right[i] = y.right[i] + below.right[i];
                }
            } else if (r < .4 && fifth >= 55 && consonant) {  // with its fifth below
                const Stereo below = ensemble_tone(rng, fifth, att, hold, rel, vel * .55);
                for (std::size_t i = 0; i < y.left.size(); ++i) {
                    y.left[i] = y.left[i] + below.left[i];
                    y.right[i] = y.right[i] + below.right[i];
                }
            }
            const std::size_t start = voice::samples(t);
            Stereo& wash = mix.bus("wash");
            add_wrapped(wash.left, y.left, start, length);
            add_wrapped(wash.right, y.right, start, length);
            t += rng.exponential(1 / 2.3);
        }
        // the bass: held through the stretch, beating slowly (two copies a hair apart)
        for (const int m : section.bass) {
            const int dur = (section.to - section.from) + 3;
            const std::size_t n = static_cast<std::size_t>(dur * sr);
            const Signal tt = voice::tarr(n);
            const double f = hz(m);
            const double gain = m == 48 ? 1 : m == 36 ? .45 : .55;
            Signal y(n);
            for (std::size_t i = 0; i < n; ++i) {
                const double env = std::min(1.0, tt[i] / 1.5) * std::min(1.0, (dur - tt[i]) / 2.0);
                const double tone = std::sin(two_pi * f * tt[i]) + .6 * std::sin(two_pi * (f + .25) * tt[i]) +
                                    .2 * std::sin(4 * voice::pi * f * tt[i]);
                y[i] = tone * env * gain;
            }
            const long long first = static_cast<long long>((section.from - 1.5) * sr);
            const std::size_t start = static_cast<std::size_t>(voice::python_modulo(first, static_cast<long long>(length)));
            Stereo& bass = mix.bus("bass");
            for (int ch = 0; ch < 2; ++ch) {
                Signal& b = ch == 0 ? bass.left : bass.right;
                const double side = ch == 0 ? .9 : 1.0;
                const std::size_t e = std::min(length, start + n);
                for (std::size_t i = 0; i < e - start; ++i) {
                    b[start + i] += y[i] * side;
                }
                if (e - start < n) {
                    for (std::size_t i = 0; i < n - (e - start); ++i) {
                        b[i] += y[(e - start) + i] * side;
                    }
                }
            }
        }
    }
    // birds: little chirping figures, near and far
    for (int j = 0; j < 20; ++j) {
        const double at = rng.uniform(0, loop_seconds);
        const double pan = rng.uniform(-.9, .9);
        const double f0 = rng.uniform(2400, 4400);
        const std::int64_t count = rng.integers(2, 7);
        for (std::int64_t q = 0; q < count; ++q) {
            const std::size_t n = voice::samples(rng.uniform(.04, .1));
            const double bend = rng.uniform(-.3, .35);
            const Signal y = chirp(n, f0, bend);
            const long long start = static_cast<long long>((at + q * rng.uniform(.08, .17)) * sr);
            mix.add("birds", y, voice::python_modulo(start, static_cast<long long>(length)), pan);
        }
    }
    return mix;
}

double level(const std::string& name) {
    const double result = name == "wash" ? -18 : name == "bass" ? -23 : name == "birds" ? -24 : -22;
    return result;
}

Stereo master(const voice::LoopMix& mix) {
    const std::size_t length = mix.length();
    Stereo out = silence(length);
    Stereo birds = silence(length);
    for (const std::pair<std::string, Stereo>& bus : mix.buses()) {
        const double g = std::pow(10.0, (level(bus.first) - rms_db(bus.second)) / 20);
        Stereo& into = bus.first == "birds" ? birds : out;
        for (std::size_t i = 0; i < length; ++i) {
            into.left[i] += bus.second.left[i] * g;
            into.right[i] += bus.second.right[i] * g;
        }
    }
    const Stereo ir = voice::make_ir(3.6, 2.4, 2.8, 2.2);  // airy: the highs ring on
    const Stereo wet = voice::conv_reverb(scaled(out, .5), ir, true);
    for (std::size_t i = 0; i < length; ++i) {
        out.left[i] = out.left[i] + wet.left[i] * .5;
        out.right[i] = out.right[i] + wet.right[i] * .5;
    }
    out = widened(filtered(out, 35, 6000, 2, true), 1.9);  // as wide as the original
    const Stereo air = voice::conv_reverb(scaled(birds, .4), voice::make_ir(1.5), true);
    for (std::size_t i = 0; i < length; ++i) {
        out.left[i] += birds.left[i] + air.left[i] * .3;
        out.right[i] += birds.right[i] + air.right[i] * .3;
    }
    // a gentle breathing of the whole across the loop (one cycle)
    for (std::size_t i = 0; i < length; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(length);
        const double breath = .85 + .15 * square(std::sin(two_pi * t));
        out.left[i] *= breath;
        out.right[i] *= breath;
    }
    return out;
}

// the stingers' put(y, sec): out[:, s:e] += 1.0 * y[:, :e - s]
void put(Stereo& out, const Stereo& y, double sec) {
    const std::size_t n = out.left.size();
    const std::size_t s = voice::samples(sec);
    const std::size_t e = std::min(n, s + y.left.size());
    for (std::size_t i = 0; i < e - s; ++i) {
        out.left[s + i] += 1.0 * y.left[i];
        out.right[s + i] += 1.0 * y.right[i];
    }
}

Stereo tune(voice::Generator& rng, bool penned) {
    const std::size_t n = voice::samples(3.2);
    Stereo out = silence(n);
    if (penned) {  // the wash blooms: C, E, G, C, E swelling in one after another
        const int ms[5] = {60, 64, 67, 72, 76};
        for (int k = 0; k < 5; ++k) {
            put(out, ensemble_tone(rng, ms[k], .25, .8, 1.4, .8), k * .14);
        }
    } else {  // got away: two tones drifting down
        const int ms[2] = {76, 69};
        for (int k = 0; k < 2; ++k) {
            put(out, ensemble_tone(rng, ms[k], .2, .5, 1.2, .7), k * .35);
        }
    }
    const Stereo wet = voice::conv_reverb(scaled(out, .5), voice::make_ir(2.6), false);
    for (std::size_t i = 0; i < n; ++i) {
        out.left[i] = out.left[i] + wet.left[i] * .5;
        out.right[i] = out.right[i] + wet.right[i] * .5;
    }
    fade_out(out, .4);
    return out;
}

constexpr voice::Word128 rng_increment{0xb7cac80e45d618adull, 0xfa58bd1da5e61411ull};
constexpr voice::GeneratorState penned_start{{0x9de71deb5fcc860aull, 0x9c1e9b040771b8bbull}, rng_increment, false, 0xbd17679du};
constexpr voice::GeneratorState away_start{{0xb5c14358f43bb6c1ull, 0x2122a41be7fd6a65ull}, rng_increment, false, 0xbd17679du};
constexpr std::uint64_t seed = 31415;  // rng = np.random.default_rng(31415); sh_music is the script's first output

}  // namespace meadow

Stereo music_meadow() {
    voice::Generator rng(meadow::seed);
    const voice::LoopMix mix = meadow::arrange(rng);
    const Stereo result = finish(meadow::master(mix), -21.0, -3.0);
    return result;
}

Stereo tune_penned() {
    voice::Generator rng(meadow::penned_start);
    const Stereo result = finish(meadow::tune(rng, true), -19, -3);
    return result;
}

Stereo tune_away() {
    voice::Generator rng(meadow::away_start);
    const Stereo result = finish(meadow::tune(rng, false), -19, -3);
    return result;
}

// ======================================================================== long_grass.py
namespace grass {

constexpr double loop_seconds = 64.0;
constexpr int penta[5] = {0, 2, 4, 7, 9};  // C D E G A
constexpr std::uint64_t seed = 2611;       // rng = np.random.default_rng(2611); the script's only output

int pen(int i, int base = 60) {
    const int result = base + penta[voice::python_modulo(i, 5)] + 12 * static_cast<int>((i - voice::python_modulo(i, 5)) / 5);
    return result;
}

Signal note(voice::Synth& synth, const char* inst, int m, double dur, double vel) {
    const Signal result = synth.render_note(inst, m, dur, vel);
    return result;
}

// ocarina: a sine with a whisper of second harmonic, a slow breathy attack and a gentle vibrato
Signal ocarina(voice::Generator& rng, int m, double dur, double vel) {
    const std::size_t n = voice::samples(dur);
    const Signal t = voice::tarr(n);
    const double base = 440 * std::pow(2.0, (m - 69) / 12.0);
    const double w = two_pi * 5;
    Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        f[i] = base * (1 + .004 * std::sin(w * t[i]) * std::min(1.0, t[i] / .6));
    }
    const Signal cs = voice::cumulative_sum(f);
    Signal breath(n);
    for (double& value : breath) {
        value = rng.normal(0, 1);
    }
    const Signal hiss = voice::fft_filter(breath, 800, 3000);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double ph = two_pi * cs[i] / sr;
        const double tone = std::sin(ph) + .08 * std::sin(2 * ph) + .02 * hiss[i] * std::min(1.0, t[i] / .2);
        const double env = std::min(1.0, t[i] / .12) * std::exp(-std::max(0.0, t[i] - dur * .7) / (dur * .25));
        y[i] = tone * env * vel;
    }
    return y;
}

struct Step {
    int step;  // -1: a breath
    double dur;
};

const std::vector<std::vector<Step>>& phrases() {
    static const std::vector<std::vector<Step>> table = {
        {{4, .7}, {5, .7}, {6, 1.3}, {-1, .4}, {7, .8}, {6, .7}, {5, 1.6}},
        {{7, .8}, {8, .8}, {9, 1.4}, {8, .7}, {7, 1.2}, {-1, .5}, {6, 1.8}},
        {{5, .6}, {6, .6}, {7, .9}, {8, 1.4}, {-1, .4}, {7, .8}, {5, .7}, {4, 1.9}},
        {{4, .9}, {5, .7}, {6, 1.2}, {-1, .6}, {5, .8}, {4, .8}, {3, 2.0}},
        {{7, 1.0}, {-1, .3}, {6, .6}, {5, .6}, {6, .8}, {7, .9}, {8, 2.0}},
        {{9, .9}, {8, .8}, {7, .8}, {6, 1.2}, {-1, .4}, {5, 2.2}},
        {{3, .8}, {4, .8}, {5, 1.0}, {7, 1.2}, {6, .9}, {5, 2.0}},
        {{8, 1.1}, {7, .7}, {8, .7}, {9, 1.6}, {-1, .5}, {7, .9}, {5, 2.2}},
    };
    return table;
}

voice::LoopMix arrange(voice::Synth& synth, voice::Generator& rng) {
    const std::size_t length = voice::samples(loop_seconds);
    const long long loop = static_cast<long long>(length);
    voice::LoopMix mix(length, true);
    // the drone: C held throughout, colours drifting over it every 16 seconds
    const int colours[4][4] = {{48, 55, 60, 64}, {48, 55, 57, 64}, {48, 53, 57, 64}, {48, 55, 62, 67}};
    const double seg = loop_seconds / 4;
    for (int k = 0; k < 4; ++k) {
        for (int j = 0; j < 4; ++j) {
            const long long start = static_cast<long long>((k * seg - 1.5) * sr);
            mix.add("pad", note(synth, "pad", colours[k][j], seg + 4, .4), voice::python_modulo(start, loop), (j - 1.5) * .45);
        }
        mix.add("bass", note(synth, "felt", 36, 6.0, .35), static_cast<long long>((k * seg) * sr), 0);
    }
    // the melody: phrases in free time, alternating flute and ocarina, with long breaths between
    double t = 1.0;
    int k = 0;
    while (t < loop_seconds - 4) {
        const std::vector<Step>& ph = phrases()[static_cast<std::size_t>(k % 8)];
        const bool flute = k % 2 == 0;
        const double pan = .45 * std::sin(k * 1.7);
        for (const Step& s : ph) {
            const double d = s.dur * rng.uniform(.9, 1.15);
            if (s.step >= 0) {
                const int m = pen(s.step);
                const Signal y = flute ? note(synth, "flute", m, d * 1.2, .5) : ocarina(rng, m, d * 1.3, .55);
                mix.add("lead", y, static_cast<long long>(t * sr), pan);
            }
            t += d;
        }
        t += rng.uniform(1.8, 3.6);
        ++k;
    }
    // harp sparkles: a few soft pentatonic notes, here and there
    for (int j = 0; j < 14; ++j) {
        const double at = rng.uniform(0, loop_seconds);
        const std::int64_t count = rng.integers(1, 4);
        for (std::int64_t q = 0; q < count; ++q) {
            const int step = static_cast<int>(rng.integers(8, 13));
            const Signal y = note(synth, "harp", pen(step), 2.0, .35);
            const long long start = static_cast<long long>((at + q * .18) * sr);
            const double pan = rng.uniform(-.8, .8);
            mix.add("sparkle", y, voice::python_modulo(start, loop), pan);
        }
    }
    // birds: little chirping figures, far off and near
    for (int j = 0; j < 22; ++j) {
        const double at = rng.uniform(0, loop_seconds);
        const double pan = rng.uniform(-.9, .9);
        const double f0 = rng.uniform(2600, 4200);
        const std::int64_t count = rng.integers(2, 6);
        for (std::int64_t q = 0; q < count; ++q) {
            const std::size_t n = voice::samples(rng.uniform(.04, .09));
            const double bend = rng.uniform(-.25, .35);
            const Signal y = chirp(n, f0, bend);
            const long long start = static_cast<long long>((at + q * rng.uniform(.08, .16)) * sr);
            mix.add("birds", y, voice::python_modulo(start, loop), pan);
        }
    }
    return mix;
}

Stereo master(const voice::LoopMix& mix) {
    const std::size_t length = mix.length();
    Stereo out = silence(length);
    Stereo send = silence(length);
    Stereo birds = silence(length);
    const std::map<std::string, std::pair<double, double>> levels = {
        {"lead", {-17, .55}}, {"pad", {-22, .5}}, {"sparkle", {-27, .7}}, {"birds", {-27, .3}}, {"bass", {-23, .2}}};
    for (const std::pair<std::string, Stereo>& bus : mix.buses()) {
        const std::map<std::string, std::pair<double, double>>::const_iterator found = levels.find(bus.first);
        const double target = found != levels.end() ? (*found).second.first : -22;
        const double coefficient = found != levels.end() ? (*found).second.second : .3;
        const double g = std::pow(10.0, (target - rms_db(bus.second)) / 20);
        for (std::size_t i = 0; i < length; ++i) {
            const double l = bus.second.left[i] * g;
            const double r = bus.second.right[i] * g;
            if (bus.first == "birds") {
                birds.left[i] += l;
                birds.right[i] += r;
                continue;
            }
            out.left[i] += l;
            out.right[i] += r;
            send.left[i] += l * coefficient;
            send.right[i] += r * coefficient;
        }
    }
    const Stereo ir = voice::make_ir(3.2, 2.6, 2.4, 1.2);  // a wide, open, soft space
    const Stereo wet = voice::conv_reverb(send, ir, true);
    for (std::size_t i = 0; i < length; ++i) {
        out.left[i] += wet.left[i] * .45;
        out.right[i] += wet.right[i] * .45;
    }
    out = filtered(out, 40, 2500, 2, true);  // warm and muffled: nothing much above two kilohertz
    for (std::size_t i = 0; i < length; ++i) {  // as wide as the original: lift the sides
        const double mid = (out.left[i] + out.right[i]) / 2;
        const double side = (out.left[i] - out.right[i]) / 2 * 2.4;
        out.left[i] = mid + side;
        out.right[i] = mid - side;
    }
    const Stereo air = voice::conv_reverb(scaled(birds, .4), voice::make_ir(1.5), true);
    for (std::size_t i = 0; i < length; ++i) {
        out.left[i] += birds.left[i] + air.left[i] * .3;
        out.right[i] += birds.right[i] + air.right[i] * .3;
    }
    // the slow swell and fall across the loop (one full cycle)
    for (std::size_t i = 0; i < length; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(length);
        const double swell = .78 + .22 * square(std::sin(two_pi * (t - .25)));
        out.left[i] *= swell;
        out.right[i] *= swell;
    }
    return out;
}

}  // namespace grass

Stereo music_grass() {
    voice::Synth synth;
    voice::Generator rng(grass::seed);
    const voice::LoopMix mix = grass::arrange(synth, rng);
    const Stereo result = finish(grass::master(mix), -21.0, -3.0);
    return result;
}

// ======================================================================== angel_music.py
namespace angel {

constexpr std::uint64_t seed = 777;  // rng = np.random.default_rng(777); the choir is the script's first output
constexpr voice::GeneratorState bells_start{{0x79dfe99141eeddd5ull, 0xe331573d42433ae3ull},
                                            {0xa9b98dc3ff94b063ull, 0x1838032d181f0911ull},
                                            true,
                                            0x8fc2cbd4u};

double hz(int m, double cents = 0.0) {
    const double result = 440 * std::pow(2.0, ((m - 69) + cents / 100) / 12);
    return result;
}

// an "ah": the formants of an open vowel, as gaussians on the harmonic amplitudes
double formant_gain(double f) {
    static constexpr double ah[4][3] = {{750, 90, 1.0}, {1150, 110, .55}, {2700, 170, .18}, {3400, 220, .1}};
    double g = .05;
    for (const double* formant : ah) {
        g += formant[2] * std::exp(-.5 * std::pow((f - formant[0]) / formant[1], 2.0));
    }
    return g;
}

// env = minimum(1, t / attack) ** 2 * minimum(1, maximum(0, dur - t) / release) ** 1.5
Signal swell(const Signal& t, double dur, double attack, double release) {
    Signal env(t.size());
    for (std::size_t i = 0; i < t.size(); ++i) {
        env[i] = square(std::min(1.0, t[i] / attack)) *
                 std::pow(std::min(1.0, std::max(0.0, dur - t[i]) / release), 1.5);
    }
    return env;
}

// choir_voice: a small section singing one note, additive "ah" voices, each a little off pitch
Stereo choir_voice(voice::Generator& rng, int m, double dur, double attack, double release, int voices, double vel) {
    const std::size_t n = voice::samples(dur);
    const Signal t = voice::tarr(n);
    const Signal env = swell(t, dur, attack, release);
    Signal left(n, 0.0);
    Signal right(n, 0.0);
    const double f0 = hz(m);
    std::vector<std::pair<int, double>> harmonics{};  // k and formant_gain(f0 k) / k ** .7
    for (int k = 1; k < 24; ++k) {
        const double fk = f0 * k;
        if (fk > 5200) {
            break;
        }
        harmonics.emplace_back(k, formant_gain(fk) / std::pow(static_cast<double>(k), .7));
    }
    Signal f(n);
    Signal y(n);
    for (int v = 0; v < voices; ++v) {
        const double detune = rng.uniform(-9, 9);
        const double rate = rng.uniform(4.6, 5.4);
        const double vib_phase = rng.uniform(0, 6.28);
        const double base = f0 * std::pow(2.0, detune / 1200);
        const double w = two_pi * rate;
        for (std::size_t i = 0; i < n; ++i) {
            const double vib = 1 + .0045 * std::sin(w * t[i] + vib_phase) * std::min(1.0, t[i] / 1.5);
            f[i] = base * vib;
        }
        const Signal cs = voice::cumulative_sum(f);
        const double phase0 = rng.uniform(0, 6.28);
        std::fill(y.begin(), y.end(), 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            const double phase = two_pi * cs[i] / sr + phase0;
            double sample = 0.0;
            for (const std::pair<int, double>& h : harmonics) {
                sample += h.second * std::sin(h.first * phase);
            }
            y[i] = sample;
        }
        Signal breath(n);
        rng.fill_standard_normal(breath);
        const Signal hiss = voice::fft_filter(breath, 600, 3000);
        const double p = rng.uniform(-.8, .8);
        const double gl = std::cos((p + 1) * voice::pi / 4);
        const double gr = std::sin((p + 1) * voice::pi / 4);
        for (std::size_t i = 0; i < n; ++i) {
            const double sample = y[i] + .015 * hiss[i];
            left[i] += sample * gl;
            right[i] += sample * gr;
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        left[i] = left[i] * env[i] * vel / voices;
        right[i] = right[i] * env[i] * vel / voices;
    }
    Stereo result{std::move(left), std::move(right)};
    return result;
}

// glass: a pure high tone with its octave, swelling and fading
Signal glass_tone(int m, double dur, double vel) {
    const std::size_t n = voice::samples(dur);
    const Signal t = voice::tarr(n);
    const double base = hz(m);
    const double w = two_pi * .3;
    Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        f[i] = base * (1 + .002 * std::sin(w * t[i]));
    }
    const Signal cs = voice::cumulative_sum(f);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double env = square(std::sin(voice::pi * std::clamp(t[i] / dur, 0.0, 1.0)));
        const double ph = two_pi * cs[i] / sr;
        y[i] = (std::sin(ph) + .25 * std::sin(2 * ph)) * env * vel;
    }
    return y;
}

// bell: a struck fundamental with inharmonic partials, the upper ones dying first
Signal bell(voice::Generator& rng, int m, double vel) {
    const double dur = 6.0;
    const std::size_t n = voice::samples(dur);
    const Signal t = voice::tarr(n);
    const double f = hz(m);
    Signal y(n, 0.0);
    static constexpr double partials[5][3] = {{1, 1.0, 2.6}, {2.0, .35, 1.6}, {2.76, .3, 1.1}, {5.4, .12, .5}, {8.93, .05, .25}};
    for (const double* partial : partials) {
        const double w = two_pi * f * partial[0];
        const double phase = rng.uniform(0, 6.28);
        for (std::size_t i = 0; i < n; ++i) {
            y[i] += partial[1] * std::sin(w * t[i] + phase) * std::exp(-t[i] / partial[2]);
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        y[i] *= 1 - std::exp(-t[i] / .002);
        y[i] = y[i] * vel;
    }
    return y;
}

// pad_string: a hushed string section, detuned saws, darkly filtered, slow bow
Signal pad_string(voice::Generator& rng, int m, double dur, double attack, double release, double vel) {
    const std::size_t n = voice::samples(dur);
    const Signal t = voice::tarr(n);
    const Signal env = swell(t, dur, attack, release);
    Signal y(n, 0.0);
    Signal f(n);
    for (int v = 0; v < 5; ++v) {
        const double base = hz(m, rng.uniform(-8, 8));
        const double w = two_pi * rng.uniform(.2, .5);
        for (std::size_t i = 0; i < n; ++i) {
            f[i] = base * (1 + .002 * std::sin(w * t[i]));
        }
        const Signal cs = voice::cumulative_sum(f);
        const double offset = rng.uniform(0, 1);
        for (std::size_t i = 0; i < n; ++i) {
            const double ph = cs[i] / sr + offset;
            y[i] += 2 * (ph - std::floor(ph + .5));
        }
    }
    Signal result = voice::fft_filter(y, 80, 1400, 2);
    for (std::size_t i = 0; i < n; ++i) {
        result[i] = result[i] * env[i] * vel / 5;
    }
    return result;
}

// place(): a stereo signal added round the loop
void place(voice::LoopMix& mix, const std::string& bus, const Stereo& y, long long start) {
    const long long length = static_cast<long long>(mix.length());
    Stereo& b = mix.bus(bus);
    long long s = voice::python_modulo(start, length);
    long long pos = 0;
    const long long n = static_cast<long long>(y.left.size());
    while (pos < n) {
        const long long k = std::min(n - pos, length - s);
        for (long long i = 0; i < k; ++i) {
            b.left[static_cast<std::size_t>(s + i)] += 1.0 * y.left[static_cast<std::size_t>(pos + i)];
            b.right[static_cast<std::size_t>(s + i)] += 1.0 * y.right[static_cast<std::size_t>(pos + i)];
        }
        pos += k;
        s = 0;
    }
}

voice::LoopMix cloud_choir(voice::Generator& rng) {
    const double loop_seconds = 64.0;
    voice::LoopMix mix(voice::samples(loop_seconds), true);
    struct Chord {
        int bass;
        int notes[6];
        int high[4];
    };
    static constexpr Chord chords[4] = {
        {36, {55, 60, 64, 67, 71, 74}, {79, 83, 86, 88}},  // Cmaj9
        {33, {57, 60, 64, 67, 71, 72}, {76, 79, 83, 84}},  // Am9
        {29, {57, 60, 64, 67, 69, 71}, {76, 79, 81, 83}},  // Fmaj9#11
        {31, {55, 60, 62, 64, 67, 69}, {74, 76, 79, 81}},  // G6/9sus
    };
    for (int i = 0; i < 4; ++i) {
        const Chord& chord = chords[i];
        const double at = i * 16.0;
        // the parts come in one after another and sing over the change into the next chord
        for (int j = 0; j < 6; ++j) {
            const int m = chord.notes[j];
            const double start = at - 1.5 + j * rng.uniform(.4, 1.1);
            const double vel = rng.uniform(.7, 1.0) * (m < 60 ? 1.1 : 1.0);
            const Stereo y = choir_voice(rng, m, 20.0, 3.5, 5.0, 6, vel);
            place(mix, "choir", y, static_cast<long long>(start * sr));
        }
        const Stereo low_voice = choir_voice(rng, chord.bass + 12, 20.0, 4.0, 5.0, 4, .8);
        place(mix, "choir", low_voice, static_cast<long long>((at - 2) * sr));
        const std::size_t n = static_cast<std::size_t>(19 * sr);
        const Signal tt = voice::tarr(n);
        const double f = hz(chord.bass);
        Signal low(n);
        for (std::size_t k = 0; k < n; ++k) {
            low[k] = std::sin(two_pi * f * tt[k]) + .4 * std::sin(two_pi * (f + .3) * tt[k]);
            low[k] *= std::min(1.0, tt[k] / 3) * std::min(1.0, (19 - tt[k]) / 4);
        }
        mix.add("bass", low, static_cast<long long>((at - 1.5) * sr), 0);
        // glass tones drifting above
        double t = at + rng.uniform(1, 3);
        while (t < at + 16) {
            const int m = chord.high[rng.choice(4)];
            const double dur = rng.uniform(3, 6);
            const double vel = rng.uniform(.4, .9);
            const Signal y = glass_tone(m, dur, vel);
            const long long start = static_cast<long long>(t * sr);
            mix.add("glass", y, start, rng.uniform(-.9, .9));
            t += rng.uniform(2.0, 4.5);
        }
    }
    return mix;
}

voice::LoopMix morning_bells(voice::Generator& rng) {
    const double loop_seconds = 56.0;
    voice::LoopMix mix(voice::samples(loop_seconds), true);
    struct Chord {
        int bass;
        int notes[5];
    };
    static constexpr Chord chords[4] = {
        {43, {55, 59, 62, 66, 69}},  // Gmaj9
        {40, {55, 59, 62, 66, 67}},  // Em9
        {36, {55, 59, 62, 64, 66}},  // Cmaj9#11
        {38, {57, 62, 64, 67, 69}},  // Dsus, 6/9
    };
    for (int i = 0; i < 4; ++i) {
        const Chord& chord = chords[i];
        const double at = i * 14.0;
        for (const int m : chord.notes) {
            const double vel = rng.uniform(.7, 1.0);
            const Signal y = pad_string(rng, m, 17.0, 3.0, 4.0, vel);
            const long long start = static_cast<long long>((at - 1.2 + rng.uniform(0, .8)) * sr);
            mix.add("pad", y, start, rng.uniform(-.6, .6));
        }
        const std::size_t n = static_cast<std::size_t>(16 * sr);
        const Signal tt = voice::tarr(n);
        const double f = hz(chord.bass);
        Signal low(n);
        for (std::size_t k = 0; k < n; ++k) {
            low[k] = std::sin(two_pi * f * tt[k]) * std::min(1.0, tt[k] / 2) * std::min(1.0, (16 - tt[k]) / 3);
        }
        mix.add("bass", low, static_cast<long long>((at - 1) * sr), 0);
    }
    static constexpr int pentatonic[10] = {67, 69, 71, 74, 76, 79, 81, 83, 86, 88};
    static constexpr double gaps[5] = {1.0, 1.5, 2.0, 2.5, 3.0};
    static constexpr double gap_weights[5] = {.25, .3, .25, .12, .08};
    double t = 0.3;
    int previous = 74;
    while (t < loop_seconds) {
        // small steps mostly, now and then a leap: the comprehension draws once per candidate
        std::vector<int> options{};
        for (const int m : pentatonic) {
            const int reach = rng.uniform(0, 1) < .2 ? 12 : 5;
            if (std::abs(m - previous) <= reach && m != previous) {
                options.push_back(m);
            }
        }
        const int m = options[rng.choice(options.size())];
        previous = m;
        const double vel = rng.uniform(.5, 1.0);
        const Signal y = bell(rng, m, vel);
        const long long start = static_cast<long long>(t * sr);
        mix.add("bells", y, start, rng.uniform(-.7, .7));
        if (rng.uniform(0, 1) < .25) {  // a soft answer an octave up
            const double answer_vel = rng.uniform(.25, .45);
            const Signal answer = bell(rng, m + 12, answer_vel);
            const long long answer_start = static_cast<long long>((t + .18) * sr);
            mix.add("bells", answer, answer_start, rng.uniform(-.8, .8));
        }
        t += gaps[rng.choice(gap_weights)];
    }
    return mix;
}

Stereo master(const voice::LoopMix& mix, const std::map<std::string, double>& levels, const Stereo& ir) {
    const std::size_t length = mix.length();
    Stereo out = silence(length);
    bool bells = false;
    for (const std::pair<std::string, Stereo>& bus : mix.buses()) {
        const std::map<std::string, double>::const_iterator found = levels.find(bus.first);
        const double target = found != levels.end() ? (*found).second : -22;
        const double g = std::pow(10.0, (target - rms_db(bus.second)) / 20);
        for (std::size_t i = 0; i < length; ++i) {
            out.left[i] += bus.second.left[i] * g;
            out.right[i] += bus.second.right[i] * g;
        }
        bells = bells || bus.first == "bells";
    }
    const Stereo wet = voice::conv_reverb(scaled(out, .5), ir, true);
    for (std::size_t i = 0; i < length; ++i) {
        out.left[i] = out.left[i] * .55 + wet.left[i] * .65;
        out.right[i] = out.right[i] * .55 + wet.right[i] * .65;
    }
    if (bells) {  // an echo carrying each bell away, from the other side (rolled, so seamless)
        const std::size_t shift = voice::samples(.42);
        Stereo echo = silence(length);
        for (std::size_t i = 0; i < length; ++i) {
            const std::size_t from = (i + length - shift) % length;
            echo.left[i] = out.right[from] * .22;
            echo.right[i] = out.left[from] * .22;
        }
        const Stereo carried = filtered(echo, 300, 5000, 2, true);
        for (std::size_t i = 0; i < length; ++i) {
            out.left[i] = out.left[i] + carried.left[i];
            out.right[i] = out.right[i] + carried.right[i];
        }
    }
    const Stereo result = widened(filtered(out, 40, 9000, 2, true), 1.6);
    return result;
}

}  // namespace angel

Stereo music_choir() {
    voice::Generator rng(angel::seed);
    const voice::LoopMix mix = angel::cloud_choir(rng);
    const Stereo ir = voice::make_ir(6.0, 4.2, 4.8, 3.6, .03);
    const Stereo result = finish(angel::master(mix, {{"choir", -17}, {"bass", -25}, {"glass", -26}}, ir), -21.0, -3.0);
    return result;
}

Stereo music_bells() {
    voice::Generator rng(angel::bells_start);
    const voice::LoopMix mix = angel::morning_bells(rng);
    const Stereo ir = voice::make_ir(4.5, 3.0, 3.4, 2.6, .025);
    const Stereo result = finish(angel::master(mix, {{"pad", -20}, {"bass", -27}, {"bells", -19}}, ir), -21.0, -3.0);
    return result;
}

constexpr voice::Cue cues[] = {
    {.name = "sh_music", .stereo = music_meadow},       {.name = "sh_music_grass", .stereo = music_grass},
    {.name = "sh_music_choir", .stereo = music_choir},  {.name = "sh_music_bells", .stereo = music_bells},
    {.name = "sh_tune_penned", .stereo = tune_penned},  {.name = "sh_tune_away", .stereo = tune_away},
};

}  // namespace

std::span<const voice::Cue> music_cues() {
    const std::span<const voice::Cue> result(cues);
    return result;
}

}  // namespace sh
