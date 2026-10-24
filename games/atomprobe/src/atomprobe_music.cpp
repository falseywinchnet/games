#include "atomprobe_cues.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// audio_src/lab_music.py, "Black Box": a cool, dark synth theme in E minor at 100 BPM,
// one seamless 32-bar loop of 3,686,400 samples (76.8 s), and three stingers, ported
// line for line in numpy's order of operations. The script draws from synth._rng
// (seeded 12345: pads, kicks, hats, snares, swells) and its own rng (seeded 1964: the
// arpeggio's plucks and velocities); the stingers start from the states the script
// had reached after the loop, as shared/voice/tools/score_reference.py printed them.
namespace ap {
namespace {

using voice::Signal;
using voice::Stereo;

constexpr double two_pi = 2 * voice::pi;
constexpr double sr = voice::sample_rate;

constexpr voice::Word128 synth_increment{0xc9c7353e6e2b1f28ull, 0x7d761f2d4027fae7ull};

constexpr voice::GeneratorState synth_at(std::uint64_t high, std::uint64_t low) {
    const voice::GeneratorState state{{high, low}, synth_increment, false, 0u};
    return state;
}

constexpr voice::Word128 script_increment{0xcedf282e7d5088f5ull, 0x8ffd09b802b56e93ull};  // default_rng(1964)

constexpr voice::GeneratorState script_at(std::uint64_t high, std::uint64_t low) {
    const voice::GeneratorState state{{high, low}, script_increment, false, 0u};
    return state;
}

// After the loop (the music starts from the module as imported and default_rng(1964)).
constexpr voice::GeneratorState win_start = synth_at(0xb2baa6106a52c0a0ull, 0x7ecc4786ff08d5aeull);
constexpr voice::GeneratorState win_rng_start = script_at(0xad1fd6339bd26685ull, 0xc65c456a34d37a8aull);
constexpr voice::GeneratorState miss_start = synth_at(0x753c849ede3253a5ull, 0xa7fe2fb980b31a3cull);
constexpr voice::GeneratorState miss_rng_start = script_at(0x70507baa4867c1d4ull, 0x3368c952483b099eull);
constexpr voice::GeneratorState topscore_start = synth_at(0x46e1269c3d827cdcull, 0xd60e7fa47ca5118cull);
constexpr voice::GeneratorState topscore_rng_start = script_at(0xe7d37bc8683642d3ull, 0xcfd483c40e941e70ull);

constexpr long long spb = 48000 * 60 / 100;  // SPB = SR * 60 // BPM: 28800
constexpr int bars = 32;
constexpr std::size_t loop_length = static_cast<std::size_t>(bars * 4 * spb);

double sign(double x) {
    const double result = x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0);
    return result;
}

// One run of the script: its two generators and synth.py's caches.
struct Score {
    voice::Synth synth;
    voice::Generator rng;
};

// pluck_syn: a glassy synth pluck, detuned saw and square through a falling low-pass.
Signal pluck_syn(Score& s, double m, double dur, double vel = .6, double bright = 1.0) {
    const double f = voice::mtof(m);
    const std::size_t n = voice::samples(dur + .25);
    const Signal t = voice::tarr(n);
    const Signal fr(n, f);
    Signal detuned(n);
    for (std::size_t i = 0; i < n; ++i) {
        detuned[i] = fr[i] * 1.006;
    }
    const double phase_a = s.rng.uniform(0, 1);
    const Signal a = s.synth.wt(voice::Wave::saw, fr, 30, phase_a);
    const double phase_b = s.rng.uniform(0, 1);
    const Signal b = s.synth.wt(voice::Wave::saw, detuned, 30, phase_b);
    const double phase_c = s.rng.uniform(0, 6);
    const double w = two_pi * f;
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = a[i] * .6 + b[i] * .6;
        y[i] = y[i] + sign(std::sin(w * t[i] + phase_c)) * .25;
    }
    const Signal dark = voice::fft_filter(y, 120, f * 1.5, 2);
    const Signal br = voice::fft_filter(y, 120, f * 7 * bright, 2);
    const Signal g = voice::gate(n, 10, .002);
    const double level = .3 + .7 * vel;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = dark[i] + (br[i] - dark[i]) * std::exp(-t[i] / .05);
        y[i] = y[i] * std::exp(-t[i] / .22) * g[i] * level * .5;
    }
    return y;
}

// bass_syn: a rounded saw bass with a plucky filter envelope and a sine sub.
Signal bass_syn(Score& s, double m, double dur, double vel = .7) {
    const double f = voice::mtof(m);
    const double rel = .04;
    const std::size_t n = voice::samples(dur + rel);
    const Signal t = voice::tarr(n);
    const Signal fr(n, f);
    Signal detuned(n);
    for (std::size_t i = 0; i < n; ++i) {
        detuned[i] = fr[i] * 1.003;
    }
    const Signal a = s.synth.wt(voice::Wave::saw, fr, 40);
    const Signal b = s.synth.wt(voice::Wave::saw, detuned, 40, .3);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = a[i] + b[i];
    }
    const Signal dark = voice::fft_filter(y, 30, f * 2.2, 2);
    const Signal br = voice::fft_filter(y, 30, f * 9, 2);
    const Signal g = voice::gate(n, dur, .003, rel);
    const double w = two_pi * f;
    const double level = .4 + .6 * vel;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = dark[i] + (br[i] - dark[i]) * std::exp(-t[i] / .07) * .8;
        y[i] = y[i] * .45 + std::sin(w * t[i]) * .8;
        y[i] = y[i] * g[i] * level;
    }
    return y;
}

// lead_syn: a searching lead, two detuned saws, slow vibrato, soft attack, low-passed.
Signal lead_syn(Score& s, double m, double dur, double vel = .6) {
    const double f = voice::mtof(m);
    const double rel = .35;
    const std::size_t n = voice::samples(dur + rel);
    const Signal t = voice::tarr(n);
    const double w = two_pi * 5.2;
    Signal fr(n);
    Signal detuned(n);
    Signal half(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double vib = .006 * std::clamp((t[i] - .25) / .4, 0.0, 1.0) * std::sin(w * t[i]);
        const double glide = -.012 * std::exp(-t[i] / .05);
        fr[i] = f * (1 + vib + glide);
        detuned[i] = fr[i] * 1.005;
        half[i] = fr[i] * .5;
    }
    const Signal a = s.synth.wt(voice::Wave::saw, fr, 30);
    const Signal b = s.synth.wt(voice::Wave::saw, detuned, 30, .41);
    const Signal cs = voice::cumulative_sum(half);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = a[i] + b[i] + .5 * std::sin(two_pi * cs[i] / sr);
    }
    y = voice::fft_filter(y, 150, 2600, 2);
    const Signal g = voice::gate(n, dur, .06, rel);
    const double level = .35 + .65 * vel;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * g[i] * level * .35;
    }
    return y;
}

// snare: a gated electronic snare, a tuned body and a bright noise burst.
Signal snare(Score& s, double vel = .7) {
    const std::size_t n = voice::samples(.45);
    const Signal t = voice::tarr(n);
    Signal nz = voice::fft_filter(s.synth.noise(n), 1200, 9000);
    for (std::size_t i = 0; i < n; ++i) {
        nz[i] = nz[i] * std::exp(-t[i] / .12);
    }
    const double spread = voice::standard_deviation(std::span<const double>(nz).first(2000)) + 1e-9;
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double body = std::sin(two_pi * (185 + 60 * std::exp(-t[i] / .01)) * t[i]) * std::exp(-t[i] / .07);
        y[i] = body * .6 + nz[i] / spread * .25;
        const double gate = t[i] < .2 ? 1.0 : std::exp(-(t[i] - .2) / .02);
        y[i] = y[i] * gate;
        y[i] = y[i] * vel;
    }
    return y;
}

Signal open_hat(Score& s, double vel = .4) {
    const std::size_t n = voice::samples(.4);
    const Signal t = voice::tarr(n);
    Signal y = voice::fft_filter(s.synth.noise(n), 7000, 16000);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * std::exp(-t[i] / .12);
    }
    const double spread = voice::standard_deviation(std::span<const double>(y).first(3000)) + 1e-9;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] / spread * .2 * vel;
    }
    return y;
}

// sonar: a distant ping, a pure tone with a long shimmering tail.
Signal sonar(double m, double vel = .5) {
    const double f = voice::mtof(m);
    const std::size_t n = voice::samples(2.4);
    const Signal t = voice::tarr(n);
    const Signal g = voice::gate(n, 10, .004);
    const double w = two_pi * f;
    const double w2 = two_pi * f * 2.01;
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = std::sin(w * t[i]) * std::exp(-t[i] / .6) + .2 * std::sin(w2 * t[i]) * std::exp(-t[i] / .3);
        y[i] = y[i] * g[i] * vel;
    }
    return y;
}

// swell: filtered noise rising into a bar line.
Signal swell(Score& s, double dur, double vel = .4) {
    const std::size_t n = voice::samples(dur);
    const Signal t = voice::tarr(n);
    Signal y = voice::fft_filter(s.synth.noise(n), 400, 5000);
    for (std::size_t i = 0; i < n; ++i) {
        const double u = t[i] / dur;
        y[i] = y[i] * (u * u);
    }
    const double spread = voice::standard_deviation(y) + 1e-9;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] / spread * vel * .2;
    }
    return y;
}

// ------------------------------------------------------------------ the tune
struct Chord {
    int root;
    std::vector<int> tones;
};

const Chord& chord(const std::string& name) {
    static const std::map<std::string, Chord> chords = {
        {"Em9", {40, {52, 55, 59, 62, 66}}}, {"Cmaj7", {36, {48, 52, 55, 59, 62}}}, {"Am9", {33, {45, 48, 52, 55, 59}}},
        {"B7sus4", {35, {47, 52, 54, 57}}},  {"B7", {35, {47, 51, 54, 57}}},         {"D6", {38, {50, 54, 57, 59}}},
        {"Bm7", {35, {47, 50, 54, 57}}},
    };
    return chords.at(name);
}

const std::vector<std::string>& form() {
    static const std::vector<std::string> a = {"Em9", "Cmaj7", "Am9", "B7sus4", "Em9", "Cmaj7", "Am9", "B7"};
    static const std::vector<std::string> b = {"Cmaj7", "D6", "Em9", "Em9", "Am9", "Bm7", "Cmaj7", "B7"};
    static const std::vector<std::string> whole = [] {
        std::vector<std::string> f{};
        for (const std::vector<std::string>* part : {&a, &b, &a, &b}) {
            f.insert(f.end(), (*part).begin(), (*part).end());
        }
        return f;
    }();
    return whole;
}

struct LeadNote {
    double beat;
    int midi;
    double beats;
};

const std::vector<LeadNote>& lead_bar(int bar) {
    static const std::map<int, std::vector<LeadNote>> lead = {
        {16, {{0, 71, 1.5}, {1.5, 74, .5}, {2, 76, 2}}},
        {17, {{0, 79, 1}, {1, 76, 1}, {2, 74, 1.5}, {3.5, 71, .5}}},
        {18, {{0, 72, 3}, {3, 71, 1}}},
        {19, {{0, 69, 2}, {2, 71, 2}}},
        {20, {{0, 71, 1.5}, {1.5, 74, .5}, {2, 76, 1}, {3, 79, 1}}},
        {21, {{0, 81, 2}, {2, 79, 1}, {3, 76, 1}}},
        {22, {{0, 74, 2}, {2, 72, 1}, {3, 74, 1}}},
        {23, {{0, 71, 3.5}}},
        {24, {{0, 76, 2}, {2, 79, 2}}},
        {25, {{0, 78, 2}, {2, 74, 2}}},
        {26, {{0, 79, 3}, {3, 78, 1}}},
        {27, {{0, 76, 4}}},
        {28, {{0, 72, 2}, {2, 76, 2}}},
        {29, {{0, 74, 2}, {2, 78, 2}}},
        {30, {{0, 79, 2}, {2, 76, 1}, {3, 74, 1}}},
        {31, {{0, 75, 2}, {2, 78, 2}}},
    };
    static const std::vector<LeadNote> none{};
    const std::map<int, std::vector<LeadNote>>::const_iterator found = lead.find(bar);
    return found == lead.end() ? none : (*found).second;
}

long long at(int bar, double beat = 0.0) {
    const long long result = static_cast<long long>(voice::python_round((bar * 4 + beat) * static_cast<double>(spb)));
    return result;
}

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

Stereo scaled(const Stereo& x, double g) {
    Stereo y{Signal(x.left.size()), Signal(x.right.size())};
    for (std::size_t i = 0; i < x.left.size(); ++i) {
        y.left[i] = x.left[i] * g;
        y.right[i] = x.right[i] * g;
    }
    return y;
}

void accumulate(Stereo& into, const Stereo& x, double g = 1.0) {
    for (std::size_t i = 0; i < into.left.size(); ++i) {
        into.left[i] += g == 1.0 ? x.left[i] : x.left[i] * g;
        into.right[i] += g == 1.0 ? x.right[i] : x.right[i] * g;
    }
}

// finish(x, target_rms_db, ceiling_db): level to a loudness, then a soft ceiling.
Stereo finish(Stereo x, double target_rms_db = -19.0, double ceiling_db = -3.0) {
    const double g = std::pow(10.0, (target_rms_db - rms_db(x)) / 20);
    const double c = std::pow(10.0, ceiling_db / 20);
    for (Signal* channel : {&x.left, &x.right}) {
        for (double& value : *channel) {
            value = value * g;
            value = std::tanh(value / c) * c;
        }
    }
    return x;
}

}  // namespace

Stereo music() {
    Score s{voice::Synth(), voice::Generator(1964)};
    voice::LoopMix mix(loop_length, true);
    Signal pump(loop_length, 1.0);
    Signal duck(static_cast<std::size_t>(spb));
    {
        const Signal tt = voice::tarr(static_cast<std::size_t>(spb));
        for (std::size_t i = 0; i < duck.size(); ++i) {
            duck[i] = 1 - .45 * std::exp(-tt[i] / .1);
        }
    }
    static constexpr int arp_order[16] = {0, 2, 1, 3, 2, 4, 3, 5, 4, 6, 5, 3, 4, 2, 3, 1};
    static constexpr int bass_pattern[8] = {0, 0, 12, 0, 0, 0, 12, 7};
    const std::vector<std::string>& bars_form = form();
    for (int bar = 0; bar < bars; ++bar) {
        const Chord& c = chord(bars_form[static_cast<std::size_t>(bar)]);
        const bool intro = bar < 8;
        const std::size_t tones = c.tones.size();
        for (std::size_t k = 0; k < tones; ++k) {
            const Signal note = s.synth.pad(c.tones[k], 4 * spb / sr * .98, .5, .35, 1.2, intro ? 1600 : 2200);
            mix.add("pad", note, at(bar, .02), (static_cast<double>(k) - 2) * .25);
        }
        std::vector<int> seq = c.tones;
        seq.push_back(c.tones[1] + 12);
        seq.push_back(c.tones[2] + 12);
        for (int i = 0; i < 16; ++i) {
            const int m = seq[static_cast<std::size_t>(arp_order[i]) % seq.size()] + 12;
            const double v = .55 + (i % 4 == 0 ? .25 : 0.0) + s.rng.uniform(-.05, .05);
            const Signal note = pluck_syn(s, m, .16, v, intro ? .7 : 1.0);
            mix.add("arp", note, at(bar, i * .25), .35 * std::sin(i * .9));
        }
        if (bar >= 4) {
            for (int i = 0; i < 8; ++i) {
                const Signal note = bass_syn(s, c.root + bass_pattern[i], .22, i % 2 == 0 ? .75 : .6);
                mix.add("bass", note, at(bar, i * .5), 0);
            }
        }
        if (bar >= 8) {
            for (const double b : {0.0, 2.0}) {
                mix.add("drums", s.synth.kick(.9), at(bar, b), 0);
            }
            if (bar % 4 == 3) {
                mix.add("drums", s.synth.kick(.7), at(bar, 2.75), 0);
            }
            for (const double b : {1.0, 3.0}) {
                mix.add("snare", snare(s, .8), at(bar, b), 0);
            }
            for (int i = 0; i < 8; ++i) {
                mix.add("hats", s.synth.hat(i % 2 ? .5 : .3), at(bar, i * .5 + (i % 2 ? .04 : 0.0)), .3);
            }
            mix.add("hats", open_hat(s, .6), at(bar, 3.5), -.3);
            for (const double b : {0.0, 2.0}) {
                const long long start = at(bar, b);
                for (std::size_t i = 0; i < duck.size(); ++i) {
                    const std::size_t index = static_cast<std::size_t>(
                        voice::python_modulo(start + static_cast<long long>(i), static_cast<long long>(loop_length)));
                    pump[index] = std::min(pump[index], duck[i]);
                }
            }
        } else if (bar >= 4) {
            mix.add("drums", s.synth.kick(.6), at(bar, 0), 0);
            for (int i = 0; i < 4; ++i) {
                mix.add("hats", s.synth.hat(.25), at(bar, i + .5), .3);
            }
        }
        if (bar % 8 == 0) {
            mix.add("fx", sonar(88, .4), at(bar, 0), -.5);
        }
        if (bar % 8 == 4) {
            mix.add("fx", sonar(83, .3), at(bar, 2), .5);
        }
        if (bar % 8 == 7) {
            mix.add("fx", swell(s, 2 * spb / sr, .6), at(bar, 2), 0);
        }
        for (const LeadNote& note : lead_bar(bar)) {
            mix.add("lead", lead_syn(s, note.midi, note.beats * static_cast<double>(spb) / sr * .95, .7),
                    at(bar, note.beat), .05);
        }
    }
    // master(mix, pump, wet=.26)
    static const std::map<std::string, double> levels = {{"pad", -21},   {"arp", -20},  {"bass", -15},
                                                         {"drums", -15}, {"snare", -19}, {"hats", -26},
                                                         {"fx", -27},    {"lead", -17}};
    static const std::map<std::string, double> sends = {
        {"snare", .6}, {"lead", .4}, {"pad", .3}, {"arp", .25}, {"fx", .5}};
    Stereo out{Signal(loop_length, 0.0), Signal(loop_length, 0.0)};
    Stereo send{Signal(loop_length, 0.0), Signal(loop_length, 0.0)};
    for (const std::pair<std::string, Stereo>& bus : mix.buses()) {
        const std::string& name = bus.first;
        const std::map<std::string, double>::const_iterator level = levels.find(name);
        const double g = std::pow(10.0, ((level == levels.end() ? -20 : (*level).second) - rms_db(bus.second)) / 20);
        Stereo b = scaled(bus.second, g);
        if (name == "pad" || name == "arp" || name == "bass" || name == "lead") {
            for (std::size_t i = 0; i < loop_length; ++i) {
                b.left[i] = b.left[i] * pump[i];
                b.right[i] = b.right[i] * pump[i];
            }
        }
        if (name == "arp") {
            accumulate(b, voice::pingpong(b, static_cast<long long>(spb * .75), .42, 5, 4200), .7);
        }
        if (name == "lead") {
            accumulate(b, voice::pingpong(b, static_cast<long long>(spb * 1.5), .35, 4, 3000), .45);
        }
        accumulate(out, b);
        const std::map<std::string, double>::const_iterator amount = sends.find(name);
        accumulate(send, b, amount == sends.end() ? .08 : (*amount).second);
    }
    const Stereo wet = voice::conv_reverb(send, voice::make_ir(2.8, 2.2, 1.8, .9), true);
    accumulate(out, wet, .26);
    out.left = voice::fft_filter(out.left, 28, 0, 2, 4096, true);
    out.right = voice::fft_filter(out.right, 28, 0, 2, 4096, true);
    return finish(std::move(out));
}

namespace {

// stinger(kind): 4.5 s, buses summed with fixed gains, a short room and a fade.
class Stinger final {
  public:
    Stinger() : mix_(length, false) {}
    void put(const std::string& bus, std::span<const double> y, double beat, double pan = 0.0) {
        mix_.add(bus, y, static_cast<long long>(beat * static_cast<double>(spb)), pan);
    }
    Stereo finish_mix() const {
        static const std::map<std::string, double> gains = {{"pad", .5},   {"arp", .9},   {"lead", 1.0},
                                                            {"fx", .5},    {"drums", .9}, {"snare", .5}};
        Stereo out{Signal(length, 0.0), Signal(length, 0.0)};
        Stereo send{Signal(length, 0.0), Signal(length, 0.0)};
        for (const std::pair<std::string, Stereo>& bus : mix_.buses()) {
            const std::map<std::string, double>::const_iterator gain = gains.find(bus.first);
            const double g = gain == gains.end() ? .7 : (*gain).second;
            for (std::size_t i = 0; i < length; ++i) {
                const double left = bus.second.left[i] * g;
                const double right = bus.second.right[i] * g;
                out.left[i] += left;
                out.right[i] += right;
                send.left[i] += left * .3;
                send.right[i] += right * .3;
            }
        }
        const Stereo tail = voice::conv_reverb(send, voice::make_ir(2.4), false);
        const std::size_t fade = voice::samples(.4);
        const Signal down = voice::linspace(1, 0, fade);
        for (std::size_t i = 0; i < length; ++i) {
            out.left[i] += tail.left[i] * .35;
            out.right[i] += tail.right[i] * .35;
        }
        for (std::size_t i = 0; i < fade; ++i) {
            out.left[length - fade + i] *= down[i];
            out.right[length - fade + i] *= down[i];
        }
        return finish(out, -18, -2.5);
    }
    static constexpr std::size_t length = 216000;  // int(4.5 * SR)

  private:
    voice::LoopMix mix_;
};

}  // namespace

// every atom found: the arpeggio climbs into E major and blooms
Stereo stinger_win() {
    Score s{voice::Synth(win_start), voice::Generator(win_rng_start)};
    Stinger st{};
    static constexpr int seq[12] = {52, 56, 59, 63, 64, 68, 71, 75, 76, 80, 83, 88};
    for (int i = 0; i < 12; ++i) {
        st.put("arp", pluck_syn(s, seq[i], .2, .7), i * .125, .4 * std::sin(static_cast<double>(i)));
    }
    for (const int m : {40, 52, 56, 59, 63, 66}) {
        st.put("pad", s.synth.pad(m, 3.0, .6, .08, 1.4, 3200), 1.5);
    }
    st.put("lead", lead_syn(s, 83, 2.2, .8), 1.5);
    st.put("fx", sonar(88, .5), 1.5);
    st.put("drums", s.synth.kick(.9), 1.5);
    return st.finish_mix();
}

// atoms missed: the light drains out, down into the low E
Stereo stinger_miss() {
    Score s{voice::Synth(miss_start), voice::Generator(miss_rng_start)};
    Stinger st{};
    static constexpr int seq[6] = {71, 67, 64, 59, 55, 52};
    for (int i = 0; i < 6; ++i) {
        st.put("arp", pluck_syn(s, seq[i], .3, .6, .6), i * .25, -.3 + .12 * i);
    }
    for (const int m : {40, 47, 52, 55}) {
        st.put("pad", s.synth.pad(m, 2.6, .55, .2, 1.4, 1100), 1.5);
    }
    st.put("lead", lead_syn(s, 64, .8, .6), 1.5);
    st.put("lead", lead_syn(s, 63, 1.6, .5), 2.25);
    st.put("drums", s.synth.kick(.7), 1.5);
    return st.finish_mix();
}

// top score: a fanfare of the theme, in the major
Stereo stinger_topscore() {
    Score s{voice::Synth(topscore_start), voice::Generator(topscore_rng_start)};
    Stinger st{};
    static constexpr int fanfare[4] = {71, 76, 80, 83};
    static constexpr double lengths[4] = {.5, .5, .5, 2.2};
    static constexpr double beats[4] = {0, .5, 1, 1.5};
    for (int i = 0; i < 4; ++i) {
        st.put("lead", lead_syn(s, fanfare[i], lengths[i] * static_cast<double>(spb) / sr, .85), beats[i]);
    }
    for (const int m : {40, 52, 56, 59, 64, 68}) {
        st.put("pad", s.synth.pad(m, 3.0, .6, .05, 1.4, 3500), 1.5);
    }
    static constexpr int arp[4] = {64, 68, 71, 76};
    for (int i = 0; i < 16; ++i) {
        st.put("arp", pluck_syn(s, arp[i % 4] + 12, .15, .6), 1.5 + i * .125, .4 * std::sin(static_cast<double>(i)));
    }
    st.put("drums", s.synth.kick(.9), 1.5);
    st.put("snare", snare(s, .8), 2.5);
    return st.finish_mix();
}

}  // namespace ap
