#include "dice_cues.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

// Each cue follows audio_src/make_sfx.py or audio_src/locker_music.py line for line, in
// numpy's order of operations. The states are what shared/voice/tools/numpy_reference.py
// (effects) and games/penthesheep/tools/music_reference.py (music) printed for those
// scripts: synth._rng (seeded 12345) runs through each script in file order; make_sfx.py's
// own rng is seeded 1809 and locker_music.py's 1715 (used only by the calm tune).
namespace ld {
namespace {

constexpr double two_pi = 2 * voice::pi;

constexpr voice::Word128 synth_increment{0xc9c7353e6e2b1f28ull, 0x7d761f2d4027fae7ull};
constexpr voice::Word128 sfx_increment{0xe8ca1c7b77cb5b01ull, 0x952a075b455b47b7ull};
constexpr voice::Word128 music_increment{0x3188db17cf17f483ull, 0x71d2b84ec8510b1full};

constexpr voice::GeneratorState synth_at(std::uint64_t high, std::uint64_t low) {
    const voice::GeneratorState state{{high, low}, synth_increment, false, 0u};
    return state;
}

constexpr voice::GeneratorState sfx_at(std::uint64_t high, std::uint64_t low) {
    const voice::GeneratorState state{{high, low}, sfx_increment, false, 0u};
    return state;
}

constexpr voice::GeneratorState music_at(std::uint64_t high, std::uint64_t low) {
    const voice::GeneratorState state{{high, low}, music_increment, false, 0u};
    return state;
}

// ------------------------------------------------------------------ make_sfx.py states
constexpr voice::GeneratorState rattle_synth[3] = {synth_at(0x1905e0335aae9634ull, 0x9199b0d09775add5ull),
                                                   synth_at(0x1861fca8b7327d0aull, 0x193000889ef48027ull),
                                                   synth_at(0x1888492ba5890fd5ull, 0xbb1df3baa73dc69full)};
constexpr voice::GeneratorState rattle_rng[3] = {sfx_at(0x10943fd236f63149ull, 0x80a0befbf988ef2dull),
                                                 sfx_at(0x697e37785c32b24dull, 0x605cb0c88667312full),
                                                 sfx_at(0x964921bd0f642c49ull, 0x2c8eac86834f1cfdull)};
constexpr voice::GeneratorState cup_down_synth = synth_at(0xd88cf2e5d1bc4143ull, 0xa925d99f307c7d7cull);
constexpr voice::GeneratorState cup_lift_synth = synth_at(0x298ec7016fb43498ull, 0x72940206fc1bb6b4ull);
constexpr voice::GeneratorState tap_synth = synth_at(0xcfc4653168e96716ull, 0xe31c3812eeee6341ull);
constexpr voice::GeneratorState plunk_rng = sfx_at(0x89effe54700465e2ull, 0x803a3162291b39e9ull);
constexpr voice::GeneratorState out_rng = sfx_at(0x74d156222a1cb23cull, 0xb441e459d7471691ull);
// ld_voice_<kind>_<j> for kinds 0..7 and j 1..3, then ld_keeper_01..03
constexpr voice::GeneratorState voice_synth[8][3] = {
    {synth_at(0xf4e387a928998db7ull, 0x0695059eb4b667a8ull), synth_at(0x8c96ece3672ae46cull, 0xe2b7691942beecadull),
     synth_at(0x5e418ee451318df2ull, 0x4ed96d71fb15f000ull)},
    {synth_at(0xf4f632407c47c9feull, 0x023f235b03141cd7ull), synth_at(0x8dd54ed3b3e33147ull, 0x4e86ad0f3f4e92cbull),
     synth_at(0x5be271ca77f8810dull, 0xe0385ebe36d664e9ull)},
    {synth_at(0x434d12f1b492683cull, 0x1a1554730a37b8f3ull), synth_at(0xed5ef1b5be445abfull, 0x991466c9671a4335ull),
     synth_at(0x656eead1dbccb06bull, 0xa88c9937467c4e4dull)},
    {synth_at(0x39d7ea9a6f7eafe9ull, 0x5283c77c6171bd32ull), synth_at(0xb61222808ce630baull, 0xe5261d0011e676d5ull),
     synth_at(0x224cf064cf5001caull, 0xdca817a1c361cb09ull)},
    {synth_at(0xa22fef01c915a703ull, 0x76241a2f66c317ebull), synth_at(0x99d1dac696ad49adull, 0x8a530f7f49974970ull),
     synth_at(0xb8855e64b375add7ull, 0x28cd39b3dd310318ull)},
    {synth_at(0x4b5612995bff6919ull, 0x6e1f909fc1b92549ull), synth_at(0x3d3f81bfdc7b4edfull, 0x5ef2d71fe5151ca1ull),
     synth_at(0xd3c94bcfdfdb45beull, 0xe71145cea7fc7395ull)},
    {synth_at(0x576bc5cf70aa9296ull, 0x35a9d83aedb905d5ull), synth_at(0x29f4048f954d9742ull, 0x3f7527ffee431abdull),
     synth_at(0x4ebd28d80fe484ebull, 0x99e901b39b4f6d19ull)},
    {synth_at(0x96b02e750f8069e0ull, 0x32d94f063451493dull), synth_at(0xd848080692b77225ull, 0x5c75322e076108abull),
     synth_at(0xfb6c31d71a84366bull, 0xe679ade9290c7b35ull)},
};
constexpr voice::GeneratorState keeper_synth[3] = {synth_at(0xf4a107437a323bf5ull, 0xa246fd9d724b9068ull),
                                                   synth_at(0x15010001a20a3dd2ull, 0x3b528654574beda1ull),
                                                   synth_at(0x1c1481f2e742607full, 0xedd0c47e219e1ab1ull)};

// ------------------------------------------------------------------ make_sfx.py helpers
voice::Signal sine_decay(std::size_t n_len, double f, double decay, double amp = 1.0) {
    const voice::Signal t = voice::tarr(n_len);
    const double w = two_pi * f;
    voice::Signal y(n_len);
    for (std::size_t i = 0; i < n_len; ++i) {
        y[i] = amp * std::sin(w * t[i]) * std::exp(-t[i] / decay);
    }
    return y;
}

// np.hanning(m): 0.5 + 0.5 cos(pi n / (m - 1)) over n = 1 - m, 3 - m, ... m - 1.
voice::Signal hanning(std::size_t m) {
    voice::Signal w(m, 1.0);
    if (m < 2) {
        return w;
    }
    const double last = static_cast<double>(m - 1);
    for (std::size_t k = 0; k < m; ++k) {
        const double n = 1.0 - static_cast<double>(m) + 2.0 * static_cast<double>(k);
        w[k] = 0.5 + 0.5 * std::cos(voice::pi * n / last);
    }
    return w;
}

// sin(2 pi cumsum(f) / SR)
voice::Signal phase_sine(const voice::Signal& f) {
    const voice::Signal cs = voice::cumulative_sum(f);
    voice::Signal y(f.size());
    for (std::size_t i = 0; i < f.size(); ++i) {
        y[i] = std::sin(two_pi * cs[i] / voice::sample_rate);
    }
    return y;
}

// voice(): a gibberish syllable through two formants, with grit, wobble and breath
voice::Signal syllable(voice::Synth& synth, double f0, double vowel_low, double vowel_high, double dur, double buzz,
                       double rasp, double wobble, double breath, double hook) {
    const std::size_t n = voice::samples(dur);
    const voice::Signal t = voice::tarr(n);
    const double w = two_pi * 7;
    voice::Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double u = t[i] / dur;
        f[i] = f0 * (1 + hook * std::sin(voice::pi * std::clamp(u * 1.2, 0.0, 1.0)) - .15 * u) *
               (1 + wobble * std::sin(w * t[i]));
    }
    const voice::Signal cs = voice::cumulative_sum(f);
    voice::Signal ph(n);
    voice::Signal src(n);
    for (std::size_t i = 0; i < n; ++i) {
        ph[i] = two_pi * cs[i] / voice::sample_rate;
        const double pulse = voice::remainder(ph[i] / two_pi, 1) < .35 ? 1.0 : 0.0;
        src[i] = buzz * (pulse - .35) + (1 - buzz) * std::sin(ph[i]);
    }
    const voice::Signal grit = voice::fft_filter(synth.noise(n), 1200, 6000);
    const voice::Signal air = voice::fft_filter(synth.noise(n), 800, 5000);
    for (std::size_t i = 0; i < n; ++i) {
        src[i] = src[i] + rasp * grit[i] * (1 + std::sin(ph[i])) + breath * air[i];
    }
    const voice::Signal first = voice::resonator(src, vowel_low, 140);
    const voice::Signal second = voice::resonator(src, vowel_high, 200);
    voice::Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double env = std::min(1.0, t[i] / .015) * std::exp(-std::max(0.0, t[i] - dur * .55) / (dur * .2));
        y[i] = (first[i] + .7 * second[i]) * env;
    }
    return y;
}

// ------------------------------------------------------------------ make_sfx.py cues
// dice in a leather cup: bursts of little clacks, dulled by leather, with a shuffle of the cup itself
template <int K>
voice::Signal rattle() {
    voice::Synth synth(rattle_synth[K]);
    voice::Generator rng(rattle_rng[K]);
    const std::size_t n = voice::samples(.9);
    voice::Signal y(n, 0.0);
    for (int j = 0; j < 22 + 4 * K; ++j) {
        const double spot = std::fabs(rng.normal(.4, .2));
        const long long at = voice::python_modulo(static_cast<long long>(spot * voice::sample_rate),
                                                  static_cast<long long>(n) - 3000);
        const double f = rng.uniform(2200, 4200);
        const double amp = rng.uniform(.3, 1);
        const voice::Signal c = sine_decay(2400, f, .004, amp);
        for (std::size_t i = 0; i < c.size(); ++i) {
            y[static_cast<std::size_t>(at) + i] += c[i];
        }
    }
    const voice::Signal t = voice::tarr(n);
    const voice::Signal shuffle_noise = voice::fft_filter(synth.noise(n), 200, 1500);
    const double w = two_pi * 7;
    voice::Signal shuffle(n);
    for (std::size_t i = 0; i < n; ++i) {
        shuffle[i] = shuffle_noise[i] * (.5 + .5 * std::sin(w * t[i])) * .25;
    }
    voice::Signal x = voice::fft_filter(y, 300, 6000);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = x[i] + shuffle[i];
        x[i] *= std::sqrt(std::sin(voice::pi * std::clamp(t[i] / .9, 0.0, 1.0)));
    }
    const voice::Signal result = voice::master(x, -13);
    return result;
}

// the cup slapped down on baize: a soft thump, then the dice settling
voice::Signal cup_down() {
    voice::Synth synth(cup_down_synth);
    const std::size_t n = voice::samples(.35);
    const voice::Signal t = voice::tarr(n);
    const voice::Signal felt = voice::fft_filter(synth.noise(n), 300, 2000);
    voice::Signal thump(n);
    for (std::size_t i = 0; i < n; ++i) {
        thump[i] = std::sin(two_pi * (90 + 60 * std::exp(-t[i] / .02)) * t[i]) * std::exp(-t[i] / .06) +
                   felt[i] * std::exp(-t[i] / .02) * .5;
    }
    for (int j = 0; j < 4; ++j) {
        const std::size_t at = voice::samples(.03 + .025 * j);
        const voice::Signal c = sine_decay(1500, 3000 + 300 * j, .003, .25);
        for (std::size_t i = 0; i < c.size(); ++i) {
            thump[at + i] += c[i];
        }
    }
    const voice::Signal result = voice::master(thump, -12);
    return result;
}

// a cup lifted: a leathery swish
voice::Signal cup_lift() {
    voice::Synth synth(cup_lift_synth);
    const std::size_t n = voice::samples(.3);
    const voice::Signal t = voice::tarr(n);
    const voice::Signal swish = voice::fft_filter(synth.noise(n), 400, 3500);
    voice::Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double s = std::sin(voice::pi * t[i] / .3);
        x[i] = swish[i] * (s * s);
    }
    const voice::Signal result = voice::master(x, -18);
    return result;
}

// a knuckle tap on the table (bidding)
voice::Signal tap() {
    voice::Synth synth(tap_synth);
    const std::size_t n = voice::samples(.12);
    const voice::Signal t = voice::tarr(n);
    const voice::Signal knuckle = voice::fft_filter(synth.noise(n), 800, 4000);
    const double w = two_pi * 180;
    voice::Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = std::sin(w * t[i]) * std::exp(-t[i] / .025) + .3 * knuckle[i] * std::exp(-t[i] / .006);
    }
    const voice::Signal result = voice::master(x, -16);
    return result;
}

// a die counted: a glassy tick
voice::Signal count() {
    const std::size_t n = voice::samples(.25);
    const voice::Signal t = voice::tarr(n);
    const double w1 = two_pi * 1760;
    const double w2 = two_pi * 4400;
    voice::Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = (std::sin(w1 * t[i]) + .3 * std::sin(w2 * t[i])) * std::exp(-t[i] / .05);
    }
    const voice::Signal result = voice::master(x, -17);
    return result;
}

// a die into the jar: a glass clink, a plop and a gurgle of bubbles
voice::Signal plunk() {
    voice::Generator rng(plunk_rng);
    const std::size_t n = voice::samples(1.0);
    const voice::Signal t = voice::tarr(n);
    static constexpr double partials[3][3] = {{2100, 1, .15}, {3350, .6, .1}, {5200, .3, .06}};
    voice::Signal clink(n, 0.0);
    for (const double* p : partials) {
        const double w = two_pi * p[0];
        for (std::size_t i = 0; i < n; ++i) {
            clink[i] += p[1] * std::sin(w * t[i]) * std::exp(-t[i] / p[2]);
        }
    }
    voice::Signal plop(n, 0.0);
    const std::size_t s = voice::samples(.12);
    const std::size_t m = n - s;
    const voice::Signal tt = voice::tarr(m);
    voice::Signal sweep(m);
    for (std::size_t i = 0; i < m; ++i) {
        sweep[i] = 300 + 900 * std::exp(-tt[i] / .03);
    }
    const voice::Signal drop = phase_sine(sweep);
    for (std::size_t i = 0; i < m; ++i) {
        plop[s + i] = drop[i] * std::exp(-tt[i] / .08) * .6;
    }
    voice::Signal gurgle(n, 0.0);
    const std::size_t bubble = voice::samples(.05);
    const voice::Signal window = hanning(bubble);
    for (int j = 0; j < 8; ++j) {
        const std::size_t at = voice::samples(.25 + j * .07 + rng.uniform(0, .03));
        const voice::Signal b = phase_sine(voice::linspace(500 + 80 * j, 1100 + 60 * j, bubble));
        for (std::size_t i = 0; i < bubble && at + i < n; ++i) {
            gurgle[at + i] += b[i] * window[i] * .3;
        }
    }
    voice::Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = clink[i] * .6 + plop[i] + gurgle[i];
    }
    const voice::Signal result = voice::master(x, -11);
    return result;
}

// going out: a long sigh of bubbles
voice::Signal going_out() {
    voice::Generator rng(out_rng);
    const std::size_t n = voice::samples(1.3);
    voice::Signal y(n, 0.0);
    const std::size_t length = voice::samples(.06);
    const voice::Signal window = hanning(length);
    for (int j = 0; j < 14; ++j) {
        const double jitter = rng.uniform(0, .03);
        const std::size_t at = static_cast<std::size_t>(j * .08 * voice::sample_rate + jitter * voice::sample_rate);
        const voice::Signal b = phase_sine(voice::linspace(300 + 30 * j, 700 + 40 * j, length));
        const double fade = 1 - j / 16.0;
        for (std::size_t i = 0; i < length && at + i < n; ++i) {
            y[at + i] += b[i] * window[i] * fade;
        }
    }
    const voice::Signal result = voice::master(y, -15);
    return result;
}

// a refused bid; a click
voice::Signal nope() {
    const voice::Signal result = voice::master(sine_decay(voice::samples(.15), 110, .05), -18);
    return result;
}

voice::Signal click() {
    const voice::Signal result = voice::master(sine_decay(voice::samples(.04), 1600, .006), -22);
    return result;
}

// voices, by kind: crab, octopus, skeleton, grouper, eel, turtle, shark, ghost
struct Kind {
    double f0, buzz, rasp, hook, wobble, breath, dur;
};
constexpr Kind kinds[8] = {
    {420, .9, .35, .4, .0, 0, .07},    {230, .3, .05, .3, .08, .1, .11}, {180, 1., .5, .1, 0, 0, .08},
    {120, .4, .1, .2, .05, .05, .13},  {300, .5, .15, .5, 0, .5, .09},  {110, .5, .15, .05, 0, .1, .16},
    {140, .9, .6, .15, .02, .05, .1},  {380, .1, .0, .6, .12, .4, .15},
};
constexpr double vowels[3][2] = {{800, 1400}, {500, 1900}, {400, 900}};

template <int K, int J>
voice::Signal kind_voice() {
    voice::Synth synth(voice_synth[K][J]);
    const Kind& kd = kinds[K];
    voice::Signal y = syllable(synth, kd.f0 * (1 + .06 * J), vowels[J][0], vowels[J][1], kd.dur, kd.buzz, kd.rasp,
                               kd.wobble, kd.breath, kd.hook);
    if (K == 2) {  // the skeleton's jaw clacks at each syllable
        const voice::Signal jaw = sine_decay(600, 1200, .003, 2);
        for (std::size_t i = 0; i < jaw.size(); ++i) {
            y[i] += jaw[i];
        }
    }
    const voice::Signal result = voice::master(voice::fft_filter(y, 70, 7000, 2), -17);
    return result;
}

// the Keeper: deep, slow and wet
template <int J>
voice::Signal keeper() {
    voice::Synth synth(keeper_synth[J]);
    voice::Signal y = syllable(synth, 70 + 6 * J, vowels[J][0], vowels[J][1], .2, .6, .2, .04, .15, .1);
    const voice::Signal wet = voice::fft_filter(synth.noise(y.size()), 100, 600);
    const voice::Signal window = hanning(y.size());
    for (std::size_t i = 0; i < y.size(); ++i) {
        y[i] = y[i] + .3 * wet[i] * window[i];
    }
    const voice::Signal result = voice::master(voice::fft_filter(y, 40, 3000, 2), -14);
    return result;
}

// ------------------------------------------------------------------ locker_music.py
constexpr int minor[7] = {0, 2, 3, 5, 7, 8, 10};
constexpr int tonic = 62;  // D
constexpr int bpm = 132;
constexpr int bars = 24;

int deg(int d, int base = tonic) {
    const int result = base + minor[voice::python_modulo(d, 7)] + 12 * static_cast<int>(std::floor(d / 7.0));
    return result;
}

constexpr int chords[bars] = {0, 0, 3, 3, 4, 4, 0, 4, 5, 5, 2, 2, 3, 3, 4, 4, 0, 0, 3, 3, 4, 4, 0, 0};
struct TuneNote {
    int degree;  // rest_degree for a rest
    double beats;
};
constexpr int rest = -100;
struct TuneBar {
    int count;
    TuneNote notes[3];
};
constexpr TuneBar tune[bars] = {
    {3, {{4, 1}, {2, 1}, {0, 1}}},  {3, {{4, 2}, {5, .5}, {4, .5}}},   {3, {{3, 1}, {5, 1}, {7, 1}}},
    {2, {{5, 2}, {rest, 1}}},       {3, {{4, 1}, {6, 1}, {8, 1}}},     {3, {{7, 1.5}, {6, .5}, {4, 1}}},
    {3, {{2, 1}, {4, 1}, {2, 1}}},  {2, {{1, 2}, {rest, 1}}},          {3, {{5, 1}, {7, 1}, {9, 1}}},
    {2, {{9, 2}, {8, 1}}},          {3, {{7, 1}, {5, 1}, {4, 1}}},     {2, {{2, 2}, {rest, 1}}},
    {3, {{3, 1}, {5, 1}, {3, 1}}},  {3, {{2, 1}, {0, 1}, {2, 1}}},     {3, {{4, 1.5}, {5, .5}, {4, 1}}},
    {2, {{1, 2}, {rest, 1}}},       {3, {{4, 1}, {2, 1}, {0, 1}}},     {3, {{4, 2}, {5, .5}, {4, .5}}},
    {3, {{3, 1}, {5, 1}, {7, 1}}},  {2, {{5, 2}, {rest, 1}}},          {3, {{4, 1}, {6, 1}, {8, 1}}},
    {3, {{7, 1.5}, {6, .5}, {4, 1}}}, {3, {{2, 1}, {1, 1}, {-1, 1}}}, {2, {{0, 2}, {rest, 1}}},
};

struct Level {
    const char* bus;
    double db;
    double send;
};
constexpr Level levels[] = {{"lead", -16, .35},   {"chords", -22, .3}, {"bass", -17, .08},
                            {"shimmer", -27, .6}, {"pad", -25, .5},    {"drums", -26, .15},
                            {"pulse", -19, .2},   {"low", -20, .4},    {"tremolo", -26, .6}};

const Level* level_of(const std::string& name) {
    for (const Level& level : levels) {
        if (name == level.bus) {
            return &level;
        }
    }
    return nullptr;
}

voice::Signal note(voice::Synth& synth, const char* inst, int m, double dur, double vel) {
    const voice::Signal result = synth.render_note(inst, m, dur, vel);
    return result;
}

std::vector<int> chord_notes(int root, int base) {
    std::vector<int> ms{deg(root, base), deg(root + 2, base), deg(root + 4, base)};
    if (voice::python_modulo(root, 7) == 4) {  // the dominant, with its leading note raised
        ms[1] += 1;
    }
    return ms;
}

// bus_rms_db: the RMS of the channels' mean, in dB
double rms_db(const voice::Stereo& b) {
    const std::size_t n = b.left.size();
    voice::Signal squares(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double m = (b.left[i] + b.right[i]) / 2;
        squares[i] = m * m;
    }
    const double r = std::sqrt(voice::sum(squares) / static_cast<double>(n));
    const double result = 20 * std::log10(std::max(r, 1e-9));
    return result;
}

// arrange()'s at(bar, beat): the sample a beat falls on, the second beat a touch early
struct BeatClock {
    double spb;
    long long operator()(int bar, double beat) const {
        const double lilt = std::fmod(beat, 3.0) == 1 ? -.05 * spb : 0;
        const long long result = static_cast<long long>(voice::python_round((bar * 3 + beat) * spb + lilt));
        return result;
    }
};

voice::LoopMix arrange(voice::Synth& synth, voice::Generator& rng, bool calm, long long spb) {
    const std::size_t length = static_cast<std::size_t>(bars * 3 * spb);
    voice::LoopMix mix(length, true);
    const double spbd = static_cast<double>(spb);
    const BeatClock at{spbd};
    for (int bar = 0; bar < bars; ++bar) {
        const int root = chords[bar];
        const std::vector<int> ch = chord_notes(root, tonic - 12);
        const int r = deg(root, tonic - 24);
        if (calm) {
            double beat = 0.0;
            for (int k = 0; k < tune[bar].count; ++k) {
                const TuneNote& tn = tune[bar].notes[k];
                if (tn.degree != rest) {
                    int m = deg(tn.degree);
                    if (voice::python_modulo(root, 7) == 4 && voice::python_modulo(tn.degree, 7) == 6) {
                        m += 1;  // the leading note
                    }
                    const double dur = tn.beats * spbd / voice::sample_rate * .9;
                    const double vel = .7 + rng.uniform(-.05, .05);
                    const voice::Signal y = note(synth, "reed", m, dur, vel);
                    mix.add("lead", y, at(bar, beat), .15);
                }
                beat += tn.beats;
            }
            mix.add("bass", note(synth, "tuba", r, .8 * spbd / voice::sample_rate, .85), at(bar, 0), -.1);
            for (const int b2 : {1, 2}) {
                for (std::size_t k = 0; k < ch.size(); ++k) {
                    const voice::Signal y =
                        note(synth, "pluck", ch[k], .4 * spbd / voice::sample_rate, b2 == 1 ? .5 : .42);
                    mix.add("chords", y, at(bar, b2) + static_cast<long long>(k) * 70, .3 - .2 * static_cast<double>(k));
                }
            }
            if (bar % 4 == 3) {
                for (std::size_t k = 0; k < ch.size(); ++k) {
                    const voice::Signal y = note(synth, "glass", ch[k] + 24, 1.4, .4);
                    mix.add("shimmer", y,
                            at(bar, 1) + static_cast<long long>(k) * static_cast<long long>(voice::samples(.1)),
                            .5 - .4 * static_cast<double>(k));
                }
            }
            mix.add("drums", synth.render_drum("wblo", .3, bar), at(bar, 0), -.3);
            mix.add("drums", synth.render_drum("wbhi", .22, bar), at(bar, 2), -.3);
        } else {
            // the heartbeat: lub-dub on the downbeat
            mix.add("drums", synth.render_drum("kick", .8, bar), at(bar, 0), 0);
            mix.add("drums", synth.render_drum("kick", .55, bar + 1), at(bar, .45), 0);
            // a low pizzicato ostinato, the chord's root and fifth
            const int pulse[3][2] = {{0, r + 12}, {1, deg(root + 4, tonic - 12)}, {2, r + 12}};
            for (const int* p : pulse) {
                mix.add("pulse", note(synth, "pizz", p[1], .45 * spbd / voice::sample_rate, .7), at(bar, p[0]), -.15);
            }
            // a long low clarinet line, a note every two bars
            if (bar % 2 == 0) {
                mix.add("low", note(synth, "clarinet", deg(root, tonic - 12), 5.6 * spbd / voice::sample_rate, .5),
                        at(bar, 0), .2);
            }
            // a trembling glass
            for (int k = 0; k < 6; ++k) {
                mix.add("tremolo", note(synth, "glass", ch[2] + 24, .18, .35), at(bar, k * .5), .4);
            }
        }
        for (std::size_t k = 0; k < ch.size(); ++k) {
            const voice::Signal y = note(synth, "pad", ch[k] - (calm ? 0 : 12), 3 * spbd / voice::sample_rate, .35);
            mix.add("pad", y, at(bar, 0), (static_cast<double>(k) - 1) * .5);
        }
    }
    return mix;
}

voice::Stereo master_bus(const voice::LoopMix& mix, double wet = .32) {
    const std::size_t length = mix.length();
    voice::Stereo out{voice::Signal(length, 0.0), voice::Signal(length, 0.0)};
    voice::Stereo send{voice::Signal(length, 0.0), voice::Signal(length, 0.0)};
    for (const std::pair<std::string, voice::Stereo>& bus : mix.buses()) {
        const voice::Stereo& b = bus.second;
        const Level* level = level_of(bus.first);
        const double target = level != nullptr ? (*level).db : -20;
        const double coefficient = level != nullptr ? (*level).send : .1;
        const double g = std::pow(10.0, (target - rms_db(b)) / 20);
        for (std::size_t i = 0; i < length; ++i) {
            const double l = b.left[i] * g;
            const double r = b.right[i] * g;
            out.left[i] += l;
            out.right[i] += r;
            send.left[i] += l * coefficient;
            send.right[i] += r * coefficient;
        }
    }
    const voice::Stereo ir = voice::make_ir(2.6, 2.2, 1.8, .8);  // a big wet hull
    const voice::Stereo reverb = voice::conv_reverb(send, ir, true);
    for (std::size_t i = 0; i < length; ++i) {
        out.left[i] += reverb.left[i] * wet;
        out.right[i] += reverb.right[i] * wet;
    }
    voice::Stereo result{voice::fft_filter(out.left, 35, 9000, 2, 4096, true),
                         voice::fft_filter(out.right, 35, 9000, 2, 4096, true)};  // the sea takes the top off
    return result;
}

// finish(x, target_rms_db, ceiling_db), then write()'s np.clip(x, -1, 1)
voice::Stereo finish(voice::Stereo x, double target_rms_db, double ceiling_db) {
    const double g = std::pow(10.0, (target_rms_db - rms_db(x)) / 20);
    const double c = std::pow(10.0, ceiling_db / 20);
    for (voice::Signal* channel : {&x.left, &x.right}) {
        for (double& v : *channel) {
            v = v * g;
            v = std::tanh(v / c) * c;
            v = std::clamp(v, -1.0, 1.0);
        }
    }
    return x;
}

constexpr voice::GeneratorState calm_synth = synth_at(0x1905e0335aae9634ull, 0x9199b0d09775add5ull);
constexpr voice::GeneratorState calm_rng = music_at(0x5b6029750a0cd604ull, 0x135012c99d4017a1ull);
constexpr voice::GeneratorState tense_synth = synth_at(0x269e75a08b2642e8ull, 0x44d9df5d5b1e18b3ull);
constexpr voice::GeneratorState tense_rng = music_at(0x139602d6615737dbull, 0x016f74c51fa624d6ull);

long long samples_per_beat() {
    const long long result = static_cast<long long>(voice::python_round(voice::sample_rate * 60.0 / bpm));
    return result;
}

voice::Stereo music_calm() {
    voice::Synth synth(calm_synth);
    voice::Generator rng(calm_rng);
    const voice::LoopMix mix = arrange(synth, rng, true, samples_per_beat());
    const voice::Stereo result = finish(master_bus(mix), -20, -3);
    return result;
}

voice::Stereo music_tense() {
    voice::Synth synth(tense_synth);
    voice::Generator rng(tense_rng);
    // the calm arrangement rendered these pad notes first; the tense one finds them cached
    synth.prime_note("pad", 50, 1.36, .35, synth_at(0x01e9b07dc0bedf42ull, 0xced7822e8c081cadull));
    synth.prime_note("pad", 53, 1.36, .35, synth_at(0xad9999e79880b9d6ull, 0x3cec3d61e38d9172ull));
    const voice::LoopMix mix = arrange(synth, rng, false, samples_per_beat());
    const voice::Stereo result = finish(master_bus(mix), -21, -3);
    return result;
}

enum class Sting { liar, stands, lie, win, lose, free };

constexpr voice::GeneratorState liar_synth = synth_at(0x5f827be9612742a0ull, 0xe983cf24ea784147ull);
constexpr voice::GeneratorState true_synth = synth_at(0x5f827be9612742a0ull, 0xe983cf24ea784147ull);
constexpr voice::GeneratorState false_synth = synth_at(0x5df7ae8e4b6d5a9dull, 0x5a7dc6261cd6df55ull);
constexpr voice::GeneratorState win_synth = synth_at(0x529e50b7b957fab6ull, 0xa57cde7b16af380full);
constexpr voice::GeneratorState lose_synth = synth_at(0x8214421ea37828ebull, 0xade11f64775cfcbdull);
constexpr voice::GeneratorState free_synth = synth_at(0x8214421ea37828ebull, 0xade11f64775cfcbdull);

// stinger()'s put(y, beat, pan, g): a note placed on a beat, panned, cut at the end
struct Placer {
    voice::Stereo& out;
    double spb;
    void operator()(const voice::Signal& y, double beat, double pan = 0.0, double g = 1.0) const {
        const std::size_t n = out.left.size();
        const std::size_t s = static_cast<std::size_t>(beat * spb);
        const std::size_t e = std::min(n, s + y.size());
        if (e <= s) {
            return;
        }
        const double gl = std::cos((pan + 1) * voice::pi / 4);
        const double gr = std::sin((pan + 1) * voice::pi / 4);
        for (std::size_t i = 0; i < e - s; ++i) {
            out.left[s + i] += gl * g * y[i];
            out.right[s + i] += gr * g * y[i];
        }
    }
};

voice::Stereo stinger(Sting kind, const voice::GeneratorState& state) {
    voice::Synth synth(state);
    const bool long_one = kind == Sting::win || kind == Sting::lose || kind == Sting::free;
    const std::size_t n = voice::samples(long_one ? 4.2 : 1.8);
    voice::Stereo out{voice::Signal(n, 0.0), voice::Signal(n, 0.0)};
    const double spb = voice::sample_rate * 60.0 / bpm;
    const Placer put{out, spb};
    if (kind == Sting::liar) {  // a stab: tuba and reed on a diminished chord, a thump
        for (const int m : {38, 50, 53, 56, 59}) {
            put(note(synth, m > 45 ? "reed" : "tuba", m, .9, .9), 0, .1 * (m % 3 - 1));
        }
        put(synth.render_drum("kick", 1, 0), 0);
        put(note(synth, "glass", 86, 1.2, .5), .1, .4);
    } else if (kind == Sting::stands) {  // the bid stands: a plain major chord, plucked
        const int ms[4] = {62, 66, 69, 74};
        for (int k = 0; k < 4; ++k) {
            put(note(synth, "pluck", ms[k], 1.0, .7), k * .12, .2 * (k - 1.5));
        }
    } else if (kind == Sting::lie) {  // a lie found out: a descending minor plunk
        const int ms[4] = {69, 65, 62, 57};
        for (int k = 0; k < 4; ++k) {
            put(note(synth, "pluck", ms[k], .7, .7), k * .18, .2 * (k - 1.5));
        }
        put(note(synth, "tuba", 38, .8, .7), .6);
    } else if (kind == Sting::win) {  // a jolly cadence, concertina and banjo, in D major
        const int steps[4] = {0, 4, 7, 12};
        for (int k = 0; k < 4; ++k) {
            put(note(synth, "reed", 62 + steps[k], .35, .8), k * .5, .1);
        }
        for (const int m : {50, 62, 66, 69, 74}) {
            put(note(synth, "pluck", m, 2.0, .7), 2.0, 0, .7);
        }
        put(note(synth, "reed", 78, 1.6, .8), 2.0);
        put(note(synth, "tuba", 38, 1.4, .8), 2.0);
        put(synth.render_drum("triangle", .5, 1), 2.0, -.3);
        for (int k = 0; k < 4; ++k) {
            put(note(synth, "glass", 86 + steps[k], .6, .45), 2.2 + k * .25, .4);
        }
    } else if (kind == Sting::lose) {  // the tuba's sorrowful slide down; a door closing on the locker
        const int ms[4] = {45, 44, 43, 38};
        for (int k = 0; k < 4; ++k) {
            put(note(synth, "tuba", ms[k], k < 3 ? .6 : 1.8, .8), k * .7);
        }
        for (const int m : {50, 53, 57}) {
            put(note(synth, "reed", m, 1.8, .55), 2.1, .2, .7);
        }
        put(synth.render_drum("kick", .9, 2), 2.1);
    } else {  // freedom: the tune itself, in the major, bright, with everything
        static constexpr int major[7] = {62, 64, 66, 67, 69, 71, 73};
        static constexpr int line[5][2] = {{4, 1}, {2, 1}, {0, 1}, {4, 2}, {7, 1}};
        double beat = 0.0;
        for (const int* step : line) {
            const int d = step[0];
            const double b = step[1];
            put(note(synth, "reed", major[d % 7] + 12 * (d / 7) + 12, b * spb / voice::sample_rate, .85), beat, .1);
            beat += b;
        }
        for (const int m : {50, 62, 66, 69, 74}) {
            put(note(synth, "pluck", m, 2.4, .7), 4, 0, .7);
        }
        put(note(synth, "glass", 98, 2.0, .5), 4, .4);
    }
    voice::Stereo send{out.left, out.right};
    for (voice::Signal* channel : {&send.left, &send.right}) {
        for (double& v : *channel) {
            v = v * .35;
        }
    }
    const voice::Stereo ir = voice::make_ir(2.4);
    const voice::Stereo reverb = voice::conv_reverb(send, ir, false);
    for (std::size_t i = 0; i < n; ++i) {
        out.left[i] = out.left[i] + reverb.left[i] * .35;
        out.right[i] = out.right[i] + reverb.right[i] * .35;
    }
    const std::size_t fade = voice::samples(.3);
    const voice::Signal down = voice::linspace(1, 0, fade);
    for (std::size_t i = 0; i < fade; ++i) {
        out.left[n - fade + i] *= down[i];
        out.right[n - fade + i] *= down[i];
    }
    const voice::Stereo result = finish(out, -18, -2.5);
    return result;
}

voice::Stereo liar() {
    return stinger(Sting::liar, liar_synth);
}
voice::Stereo stands() {
    return stinger(Sting::stands, true_synth);
}
voice::Stereo lie() {
    return stinger(Sting::lie, false_synth);
}
voice::Stereo win() {
    return stinger(Sting::win, win_synth);
}
voice::Stereo lose() {
    return stinger(Sting::lose, lose_synth);
}
voice::Stereo freedom() {
    return stinger(Sting::free, free_synth);
}

constexpr voice::Cue cues[] = {
    {.name = "ld_rattle_01", .mono = rattle<0>},
    {.name = "ld_rattle_02", .mono = rattle<1>},
    {.name = "ld_rattle_03", .mono = rattle<2>},
    {.name = "ld_cup_down", .mono = cup_down},
    {.name = "ld_cup_lift", .mono = cup_lift},
    {.name = "ld_tap", .mono = tap},
    {.name = "ld_count", .mono = count},
    {.name = "ld_plunk", .mono = plunk},
    {.name = "ld_out", .mono = going_out},
    {.name = "ld_nope", .mono = nope},
    {.name = "ld_click", .mono = click},
    {.name = "ld_voice_0_01", .mono = kind_voice<0, 0>},
    {.name = "ld_voice_0_02", .mono = kind_voice<0, 1>},
    {.name = "ld_voice_0_03", .mono = kind_voice<0, 2>},
    {.name = "ld_voice_1_01", .mono = kind_voice<1, 0>},
    {.name = "ld_voice_1_02", .mono = kind_voice<1, 1>},
    {.name = "ld_voice_1_03", .mono = kind_voice<1, 2>},
    {.name = "ld_voice_2_01", .mono = kind_voice<2, 0>},
    {.name = "ld_voice_2_02", .mono = kind_voice<2, 1>},
    {.name = "ld_voice_2_03", .mono = kind_voice<2, 2>},
    {.name = "ld_voice_3_01", .mono = kind_voice<3, 0>},
    {.name = "ld_voice_3_02", .mono = kind_voice<3, 1>},
    {.name = "ld_voice_3_03", .mono = kind_voice<3, 2>},
    {.name = "ld_voice_4_01", .mono = kind_voice<4, 0>},
    {.name = "ld_voice_4_02", .mono = kind_voice<4, 1>},
    {.name = "ld_voice_4_03", .mono = kind_voice<4, 2>},
    {.name = "ld_voice_5_01", .mono = kind_voice<5, 0>},
    {.name = "ld_voice_5_02", .mono = kind_voice<5, 1>},
    {.name = "ld_voice_5_03", .mono = kind_voice<5, 2>},
    {.name = "ld_voice_6_01", .mono = kind_voice<6, 0>},
    {.name = "ld_voice_6_02", .mono = kind_voice<6, 1>},
    {.name = "ld_voice_6_03", .mono = kind_voice<6, 2>},
    {.name = "ld_voice_7_01", .mono = kind_voice<7, 0>},
    {.name = "ld_voice_7_02", .mono = kind_voice<7, 1>},
    {.name = "ld_voice_7_03", .mono = kind_voice<7, 2>},
    {.name = "ld_keeper_01", .mono = keeper<0>},
    {.name = "ld_keeper_02", .mono = keeper<1>},
    {.name = "ld_keeper_03", .mono = keeper<2>},
    {.name = "ld_music", .stereo = music_calm},
    {.name = "ld_music_tense", .stereo = music_tense},
    {.name = "ld_liar", .stereo = liar},
    {.name = "ld_true", .stereo = stands},
    {.name = "ld_false", .stereo = lie},
    {.name = "ld_stinger_win", .stereo = win},
    {.name = "ld_stinger_lose", .stereo = lose},
    {.name = "ld_stinger_free", .stereo = freedom},
};

}  // namespace

std::span<const voice::Cue> sound_cues() {
    const std::span<const voice::Cue> result(cues);
    return result;
}

void register_sound_cues() {
    voice::Bank::shared().add(cues);
}

}  // namespace ld
