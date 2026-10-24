#include "koikoi_cues.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

// Each cue follows audio_src/koikoi_audio.py line for line, in numpy's order of
// operations. The states are what the script's generators held when it began each cue
// (synth._rng, seeded 12345, is shared by every cue in script order; the script's own
// rng, seeded 1868, is used by the shuffle and the music), as printed by
// shared/voice/tools/score_reference.py. The script computes kk_take from kk_place's
// slap, so both start from the same state.
namespace kk {
namespace {

using voice::Signal;
using voice::Stereo;

constexpr double two_pi = 2 * voice::pi;

constexpr voice::Word128 synth_increment{0xc9c7353e6e2b1f28ull, 0x7d761f2d4027fae7ull};

constexpr voice::GeneratorState synth_at(std::uint64_t high, std::uint64_t low) {
    const voice::GeneratorState state{{high, low}, synth_increment, false, 0u};
    return state;
}

constexpr voice::GeneratorState place_start = synth_at(0x1905e0335aae9634ull, 0x9199b0d09775add5ull);  // as imported
constexpr voice::GeneratorState flip_start = synth_at(0xdee3564711faa317ull, 0xa974303db5aea12aull);
constexpr voice::GeneratorState shuffle_start = synth_at(0xc78137b43d4bf5b2ull, 0xb27beae067555baaull);
constexpr voice::GeneratorState set_start = synth_at(0xad5cec153fdfd16full, 0x9b558e3833deb274ull);
constexpr voice::GeneratorState koikoi_start = synth_at(0xa87b6abecb3fbb57ull, 0x0a6780e642479c89ull);
constexpr voice::GeneratorState stop_start = synth_at(0x89792f1ae94d9488ull, 0x963f93055c5eb3b4ull);
constexpr voice::GeneratorState win_start = synth_at(0x2989d5f3ece7eb14ull, 0xcd795bfe3117feb4ull);
constexpr voice::GeneratorState lose_start = synth_at(0x1d09a831f70d7840ull, 0x1ee50ab4cf157258ull);
constexpr voice::GeneratorState music_start = synth_at(0x79011425b22c022eull, 0x6b081a05f434681dull);
constexpr std::uint64_t shuffle_rng_start = 1868;  // rng = np.random.default_rng(1868), first used by the shuffle
constexpr voice::GeneratorState music_rng_start{
    {0x405466dbce8977c3ull, 0x8b95f32ed1baa072ull}, {0x46966e760c91411bull, 0xc95e3ba38476ada7ull}, false, 0u};

constexpr int yo_steps[5] = {0, 2, 5, 7, 9};  // D E G A B from D
constexpr int tonic = 62;                     // D4

int yo(int i, int base = tonic) {
    const int result = base + yo_steps[voice::python_modulo(i, 5)] + 12 * static_cast<int>(std::floor(i / 5.0));
    return result;
}

double hz(int m) {
    const double result = 440 * std::pow(2.0, (m - 69) / 12.0);
    return result;
}

void add_clipped(Signal& y, std::size_t at, const Signal& v) {
    for (std::size_t i = 0; i < v.size() && at + i < y.size(); ++i) {
        y[at + i] += v[i];
    }
}

// koto(m, dur, vel, bend): a plucked silk string, its pitch rising and settling.
Signal koto(voice::Synth& synth, int m, double dur, double vel = .7, bool bend = true) {
    const std::size_t n = voice::samples(dur);
    const Signal t = voice::tarr(n);
    const double base = hz(m);
    const double w5 = two_pi * 5;
    Signal f0(n);
    for (std::size_t i = 0; i < n; ++i) {
        double inner = bend ? 1 + .012 * std::exp(-t[i] / .08) : 1.0;
        inner = inner + .002 * std::sin(w5 * t[i]) * std::min(1.0, t[i] / .4);
        f0[i] = base * inner;
    }
    const Signal cs = voice::cumulative_sum(f0);
    Signal ph(n);
    for (std::size_t i = 0; i < n; ++i) {
        ph[i] = two_pi * cs[i] / voice::sample_rate;
    }
    static constexpr double amps[6] = {1, .55, .32, .2, .12, .07};
    Signal y(n, 0.0);
    for (int k = 1; k <= 6; ++k) {
        const double a = amps[k - 1];
        const double shift = k * .3;
        const double tau = 1.6 / (1 + .7 * (k - 1));
        for (std::size_t i = 0; i < n; ++i) {
            y[i] += a * std::sin(k * ph[i] + shift) * std::exp(-t[i] / tau);
        }
    }
    const Signal pick = voice::fft_filter(synth.noise(n), 2000, 8000);
    Signal result(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double att = pick[i] * std::exp(-t[i] / .006) * .3;
        result[i] = (y[i] * std::min(1.0, t[i] / .002) + att) * vel;
    }
    return result;
}

// flute(m, dur, vel): a breathy bamboo flute.
Signal flute(voice::Synth& synth, int m, double dur, double vel = .6) {
    const std::size_t n = voice::samples(dur);
    const Signal t = voice::tarr(n);
    const double base = hz(m);
    const double w = two_pi * 4.6;
    Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double vib = .006 * std::sin(w * t[i]) * std::clamp((t[i] - .3) / .6, 0.0, 1.0);
        f[i] = base * (1 + vib);
    }
    const Signal cs = voice::cumulative_sum(f);
    const Signal hiss = voice::fft_filter(synth.noise(n), 1200, 5000);
    const double hold = dur * .7;
    const double fall = dur * .25;
    Signal result(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double ph = two_pi * cs[i] / voice::sample_rate;
        const double y = std::sin(ph) + .18 * std::sin(2 * ph) + .05 * std::sin(3 * ph);
        const double breath = hiss[i] * (.18 + .1 * std::exp(-t[i] / .15));
        const double env = std::min(1.0, t[i] / .18) * std::exp(-std::max(0.0, t[i] - hold) / fall);
        result[i] = (y + breath) * env * vel;
    }
    return result;
}

// room(x, wet): the little room every slap and chord plays in.
Signal room(const Signal& x, double wet) {
    const std::size_t n = x.size();
    Stereo half{Signal(n), Signal(n)};
    for (std::size_t i = 0; i < n; ++i) {
        half.left[i] = x[i] * .5;
        half.right[i] = x[i] * .5;
    }
    const Stereo tail = voice::conv_reverb(half, voice::make_ir(1.4), false);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double left = x[i] + tail.left[i] * wet;
        const double right = x[i] + tail.right[i] * wet;
        y[i] = (left + right) / 2;
    }
    return y;
}

// the card slap: a sharp crack of thick card on felt, and a low thump
struct Slap {
    Signal crack{};
    Signal thump{};
};

Slap slap(voice::Synth& synth) {
    const std::size_t n = voice::samples(.22);
    const Signal t = voice::tarr(n);
    Slap s{voice::fft_filter(synth.noise(n), 1800, 9000), Signal(n)};
    for (std::size_t i = 0; i < n; ++i) {
        s.crack[i] = s.crack[i] * std::exp(-t[i] / .006);
        s.thump[i] = std::sin(two_pi * (140 + 120 * std::exp(-t[i] / .01)) * t[i]) * std::exp(-t[i] / .03) * .7;
    }
    return s;
}

Signal place() {
    voice::Synth synth(place_start);
    const Slap s = slap(synth);
    Signal x(s.crack.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = s.crack[i] + s.thump[i];
    }
    const Signal result = voice::master(room(x, .15), -10);
    return result;
}

// a capture: two slaps, the second landing on the first
Signal take() {
    voice::Synth synth(place_start);
    const Slap s = slap(synth);
    const std::size_t n = s.crack.size();
    Signal y(voice::samples(.3), 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] += s.crack[i] + s.thump[i];
    }
    const std::size_t at = voice::samples(.06);
    for (std::size_t i = 0; i < n; ++i) {
        y[at + i] += s.crack[i] * .8 + s.thump[i] * .5;
    }
    const Signal result = voice::master(room(y, .15), -9);
    return result;
}

// a card turned over: a soft swish and a light tap
Signal flip() {
    voice::Synth synth(flip_start);
    const std::size_t n = voice::samples(.18);
    const Signal t = voice::tarr(n);
    const Signal swish = voice::fft_filter(synth.noise(n), 1500, 7000);
    const double w = two_pi * 400;
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double s = std::sin(voice::pi * t[i] / t[n - 1]);
        const double a = swish[i] * (s * s) * .6;
        const double b = std::sin(w * t[i]) * std::exp(-std::max(0.0, t[i] - .12) / .01) * (t[i] > .12 ? 1.0 : 0.0) * .3;
        x[i] = a + b;
    }
    const Signal result = voice::master(x, -16);
    return result;
}

// the shuffle: a riffle of many little cracks
Signal shuffle() {
    voice::Synth synth(shuffle_start);
    voice::Generator rng(shuffle_rng_start);
    Signal y(voice::samples(1.1), 0.0);
    const std::size_t m = voice::samples(.02);
    const Signal tm = voice::tarr(m);
    for (int k = 0; k < 40; ++k) {
        const double jitter = rng.uniform(0, .01);
        const std::size_t at = voice::samples(.05 + k * .022 + jitter);
        const Signal crack = voice::fft_filter(synth.noise(m), 2000, 8000);
        const double loudness = rng.uniform(.4, 1);
        for (std::size_t i = 0; i < m && at + i < y.size(); ++i) {
            y[at + i] += crack[i] * std::exp(-tm[i] / .004) * loudness;
        }
    }
    const Signal result = voice::master(y, -15);
    return result;
}

// koto notes on yo steps, `spacing` seconds apart, in a buffer of `seconds`
Signal strum(voice::Synth& synth, Signal y, std::span<const int> steps, double spacing, double dur, double vel) {
    for (std::size_t k = 0; k < steps.size(); ++k) {
        const Signal v = koto(synth, yo(steps[k]), dur, vel);
        add_clipped(y, voice::samples(static_cast<double>(k) * spacing), v);
    }
    return y;
}

// a set made: a koto flourish up the scale
Signal set() {
    voice::Synth synth(set_start);
    static constexpr int steps[5] = {5, 6, 7, 8, 10};
    const Signal y = strum(synth, Signal(voice::samples(2.2), 0.0), steps, .07, 1.6, .7);
    const Signal result = voice::master(room(y, .35), -10);
    return result;
}

// koi-koi: a taiko and a rising run
Signal koikoi() {
    voice::Synth synth(koikoi_start);
    const std::size_t n = voice::samples(2.4);
    const Signal t = voice::tarr(n);
    const Signal boom = voice::fft_filter(synth.noise(n), 100, 900);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = std::sin(two_pi * (60 + 50 * std::exp(-t[i] / .05)) * t[i]) * std::exp(-t[i] / .35) +
               boom[i] * std::exp(-t[i] / .06) * .4;
    }
    for (int k = 0; k < 8; ++k) {
        const Signal v = koto(synth, yo(3 + k), 1.2, .5);
        add_clipped(y, voice::samples(.25 + k * .05), v);
    }
    const Signal result = voice::master(room(y, .3), -8);
    return result;
}

// stopping: hyoshigi, two wooden clappers struck twice
Signal stop() {
    voice::Synth synth(stop_start);
    Signal y(voice::samples(.9), 0.0);
    const std::size_t m = voice::samples(.3);
    const Signal tt = voice::tarr(m);
    static constexpr double modes[3][3] = {{1850, 1, .05}, {2960, .5, .03}, {720, .4, .04}};
    for (const double at : {0.0, .22}) {
        Signal clap(m, 0.0);
        for (const double* mode : modes) {
            const double w = two_pi * mode[0];
            for (std::size_t i = 0; i < m; ++i) {
                clap[i] = clap[i] + mode[1] * std::sin(w * tt[i]) * std::exp(-tt[i] / mode[2]);
            }
        }
        const Signal knock = voice::fft_filter(synth.noise(m), 1500, 6000);
        for (std::size_t i = 0; i < m; ++i) {
            clap[i] = clap[i] + knock[i] * std::exp(-tt[i] / .005);
        }
        add_clipped(y, voice::samples(at), clap);
    }
    const Signal result = voice::master(room(y, .3), -9);
    return result;
}

// a round won: a bright koto chord; lost: a falling pair
Signal win() {
    voice::Synth synth(win_start);
    static constexpr int steps[4] = {0, 2, 4, 5};
    const Signal y = strum(synth, Signal(voice::samples(2.6), 0.0), steps, .04, 2.2, .6);
    const Signal result = voice::master(room(y, .35), -10);
    return result;
}

Signal lose() {
    voice::Synth synth(lose_start);
    static constexpr int steps[2] = {4, 2};
    const Signal y = strum(synth, Signal(voice::samples(2.2), 0.0), steps, .3, 1.8, .55);
    const Signal result = voice::master(room(y, .35), -12);
    return result;
}

Signal click() {
    const std::size_t n = voice::samples(.04);
    const Signal t = voice::tarr(n);
    const double w = two_pi * 1500;
    Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = std::sin(w * t[i]) * std::exp(-t[i] / .006);
    }
    const Signal result = voice::master(x, -22);
    return result;
}

// ------------------------------------------------------------------ the music
// "Twelve Months": the yo pentatonic at 66 BPM, one seamless 32-bar loop of
// 5,585,455 samples (116.4 s).
struct Phrase {
    int step;  // -1 breathes
    double seconds;
};
constexpr Phrase phrase_0[] = {{3, 1.4}, {4, .9}, {5, 2.2}, {-1, .6}, {4, 1.0}, {3, 2.4}};
constexpr Phrase phrase_1[] = {{5, 1.2}, {6, .9}, {7, 1.8}, {6, 1.0}, {5, 2.6}};
constexpr Phrase phrase_2[] = {{2, 1.0}, {3, 1.0}, {4, 1.6}, {-1, .5}, {3, .9}, {2, 1.0}, {0, 2.6}};
constexpr Phrase phrase_3[] = {{7, 1.6}, {6, .8}, {5, .8}, {4, 1.4}, {-1, .5}, {5, 2.8}};
constexpr std::span<const Phrase> phrases[4] = {phrase_0, phrase_1, phrase_2, phrase_3};

double bus_rms(const Stereo& b) {
    const std::size_t n = b.left.size();
    Signal squares(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double m = (b.left[i] + b.right[i]) / 2;
        squares[i] = m * m;
    }
    const double result = std::sqrt(voice::sum(squares) / static_cast<double>(n));
    return result;
}

Stereo music() {
    voice::Synth synth(music_start);
    voice::Generator rng(music_rng_start);
    const double spb = 48000 * 60 / 66.0;
    const std::size_t length = static_cast<std::size_t>(voice::python_round(128 * spb));
    voice::LoopMix mix(length, true);
    static constexpr int pattern[8] = {0, 2, 4, 5, 4, 2, 3, 1};
    static constexpr int shifts[8] = {0, 0, 1, 0, -1, 0, 2, 0};
    for (int bar = 0; bar < 32; ++bar) {
        const int shift = shifts[bar % 8];
        for (int k = 0; k < 8; ++k) {
            if ((bar % 4 == 3 && k >= 6) || rng.uniform(0, 1) < .08) {
                continue;
            }
            const double jitter = rng.uniform(-.01, .01);
            const long long at = static_cast<long long>((bar * 4 + k * .5) * spb + jitter * voice::sample_rate);
            const int m = yo(pattern[k] + shift + 2, tonic - 12);
            const Signal note = koto(synth, m, 2.2, .55 + (k == 0 ? .15 : 0.0));
            mix.add("koto", note, at, -.3 + .1 * k);
        }
        if (bar % 2 == 0) {
            for (const std::pair<int, double> drone : {std::pair<int, double>{38, -.2}, std::pair<int, double>{45, .2}}) {
                const Signal note = koto(synth, drone.first, 6.0, .5, false);
                mix.add("drone", note, static_cast<long long>(bar * 4 * spb), drone.second);
            }
        }
    }
    double t = 2.0;
    const double end = static_cast<double>(length) / voice::sample_rate - 6;
    for (int k = 0; t < end; ++k) {
        for (const Phrase& p : phrases[k % 4]) {
            if (p.step >= 0) {
                const Signal note = flute(synth, yo(p.step), p.seconds * 1.15, .5);
                mix.add("flute", note, static_cast<long long>(t * voice::sample_rate), .15);
            }
            t += p.seconds;
        }
        t += rng.uniform(4, 7);
    }
    Stereo out{Signal(length, 0.0), Signal(length, 0.0)};
    for (const std::pair<std::string, Stereo>& bus : mix.buses()) {
        const double level = bus.first == "koto" ? -19 : bus.first == "drone" ? -25 : -19;
        const double g = std::pow(10.0, (level - 20 * std::log10(std::max(bus_rms(bus.second), 1e-9))) / 20);
        for (std::size_t i = 0; i < length; ++i) {
            out.left[i] += bus.second.left[i] * g;
            out.right[i] += bus.second.right[i] * g;
        }
    }
    {
        Stereo send{Signal(length), Signal(length)};
        for (std::size_t i = 0; i < length; ++i) {
            send.left[i] = out.left[i] * .4;
            send.right[i] = out.right[i] * .4;
        }
        const Stereo wet = voice::conv_reverb(send, voice::make_ir(2.8, 2.0, 2.2, 1.4), true);
        for (std::size_t i = 0; i < length; ++i) {
            out.left[i] += wet.left[i] * .4;
            out.right[i] += wet.right[i] * .4;
        }
    }
    out.left = voice::fft_filter(out.left, 40, 9000, 2, 4096, true);
    out.right = voice::fft_filter(out.right, 40, 9000, 2, 4096, true);
    const double gain = std::pow(10.0, (-21 - 20 * std::log10(bus_rms(out))) / 20);
    for (std::size_t i = 0; i < length; ++i) {
        out.left[i] = std::tanh(out.left[i] * gain / .7) * .7;
        out.right[i] = std::tanh(out.right[i] * gain / .7) * .7;
    }
    return out;
}

constexpr voice::Cue cues[] = {
    {.name = "kk_place", .mono = place},   {.name = "kk_take", .mono = take},  {.name = "kk_flip", .mono = flip},
    {.name = "kk_shuffle", .mono = shuffle}, {.name = "kk_set", .mono = set},  {.name = "kk_koikoi", .mono = koikoi},
    {.name = "kk_stop", .mono = stop},     {.name = "kk_win", .mono = win},    {.name = "kk_lose", .mono = lose},
    {.name = "kk_click", .mono = click},   {.name = "kk_music", .stereo = music},
};

}  // namespace

std::span<const voice::Cue> sound_cues() {
    const std::span<const voice::Cue> result(cues);
    return result;
}

void register_sound_cues() {
    voice::Bank::shared().add(cues);
}

}  // namespace kk
