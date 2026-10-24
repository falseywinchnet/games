#include "sheep_cues.hpp"

#include <algorithm>
#include <cmath>

// Each cue follows audio_src/make_sfx.py line for line, in numpy's order of operations.
// The states are what tools/numpy_reference.py (shared/voice) printed for that script:
// synth._rng (seeded 12345) is shared by every cue in script order, and the script's own
// rng (seeded 1977) is used only by the munching.
namespace sh {
namespace {

constexpr double two_pi = 2 * voice::pi;

constexpr voice::Word128 synth_increment{0xc9c7353e6e2b1f28ull, 0x7d761f2d4027fae7ull};

constexpr voice::GeneratorState synth_at(std::uint64_t high, std::uint64_t low) {
    const voice::GeneratorState state{{high, low}, synth_increment, false, 0u};
    return state;
}

constexpr voice::GeneratorState place_start = synth_at(0x1905e0335aae9634ull, 0x9199b0d09775add5ull);
constexpr voice::GeneratorState hop_start = synth_at(0xc78137b43d4bf5b2ull, 0xb27beae067555baaull);
constexpr voice::GeneratorState baa_starts[4] = {
    synth_at(0x873a59ca22bcf992ull, 0x6090ce4266377056ull), synth_at(0xcd0307b3ea26792bull, 0xb0dbf25e0a5a2515ull),
    synth_at(0x0a12eac63332930dull, 0xd7eef4c20dbe848dull), synth_at(0xe7b0218457c7c512ull, 0x050b3146e2f63f24ull)};
constexpr voice::GeneratorState munch_start = synth_at(0x4f3111c018b0da27ull, 0x0da31f44ad063446ull);
constexpr voice::GeneratorState undo_start = synth_at(0x1182b19155e83fffull, 0x9d3befe3b59eb7cfull);
constexpr voice::GeneratorState penned_start = synth_at(0x9cd471234a12b8acull, 0x60f71d2bb9cb23ecull);
constexpr voice::GeneratorState escape_start = synth_at(0xefa9256c64faa43bull, 0x9aecf923b8332942ull);
constexpr std::uint64_t script_seed = 1977;  // rng = np.random.default_rng(1977), first used by the munching

// bleat(f0, dur, wobble, depth): a nasal pulse with a quavering vibrato through an 'aa' vowel.
voice::Signal bleat(voice::Synth& synth, double f0, double dur, double wobble = 7.0, double depth = .06) {
    const double vowel_low = 700;
    const double vowel_high = 1200;
    const double bright = 1.0;
    const std::size_t n = voice::samples(dur);
    const voice::Signal t = voice::tarr(n);
    const double w = two_pi * wobble;
    voice::Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double u = t[i] / dur;
        const double contour = 1 + .12 * std::sin(voice::pi * std::clamp(u * 1.5, 0.0, 1.0)) - .1 * u;
        const double quaver = 1 + depth * std::sin(w * t[i]) * std::min(1.0, t[i] / .08);
        f[i] = f0 * contour * quaver;
    }
    const voice::Signal cs = voice::cumulative_sum(f);
    const voice::Signal hiss = voice::fft_filter(synth.noise(n), 1000, 4000);
    voice::Signal src(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double ph = two_pi * cs[i] / voice::sample_rate;
        const double pulse = voice::remainder(ph / two_pi, 1) < .3 ? 1.0 : 0.0;
        src[i] = (pulse - .3) + .2 * hiss[i];
    }
    const voice::Signal first = voice::resonator(src, vowel_low, 120);
    const voice::Signal second = voice::resonator(src, vowel_high, 160);
    const voice::Signal third = voice::resonator(src, 2600, 300);
    voice::Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double env = std::min(1.0, t[i] / .03) * std::exp(-std::max(0.0, t[i] - dur * .6) / (dur * .2));
        y[i] = first[i] + .8 * second[i] + .3 * bright * third[i];
        y[i] = y[i] * env;
    }
    const voice::Signal result = voice::fft_filter(y, 150, 5000);
    return result;
}

// a stone set in the grass: a soft, earthy thunk and a rustle
voice::Signal place() {
    voice::Synth synth(place_start);
    const std::size_t n = voice::samples(.4);
    const voice::Signal t = voice::tarr(n);
    const voice::Signal rustle = voice::fft_filter(synth.noise(n), 1500, 6000);
    const double w_clack = two_pi * 900;
    voice::Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double thunk = std::sin(two_pi * (110 + 80 * std::exp(-t[i] / .02)) * t[i]) * std::exp(-t[i] / .07);
        const double leaves = rustle[i] * std::exp(-t[i] / .05) * .35;
        const double clack = std::sin(w_clack * t[i]) * std::exp(-t[i] / .01) * .2;
        x[i] = thunk + leaves + clack;
    }
    const voice::Signal result = voice::master(x, -12);
    return result;
}

// a hop: a soft double patter on turf
voice::Signal hop() {
    voice::Synth synth(hop_start);
    const std::size_t n = voice::samples(.25);
    voice::Signal y(n, 0.0);
    const double starts[2] = {0, .07};
    const double w = two_pi * 160;
    for (const double at : starts) {
        const std::size_t s = voice::samples(at);
        const std::size_t m = voice::samples(.08);
        const voice::Signal tt = voice::tarr(m);
        const voice::Signal patter = voice::fft_filter(synth.noise(m), 500, 3000);
        for (std::size_t i = 0; i < m && s + i < n; ++i) {
            const double thud = std::sin(w * tt[i]) * std::exp(-tt[i] / .02);
            y[s + i] += thud + patter[i] * std::exp(-tt[i] / .015) * .4;
        }
    }
    const voice::Signal result = voice::master(y, -20);
    return result;
}

// bleats: four sheep voices, from a lamb to an old ewe
template <int Voice>
voice::Signal baa() {
    static constexpr double voices[4][3] = {{420, .6, 7.5}, {330, .75, 6.5}, {260, .8, 6.0}, {520, .45, 8.5}};
    voice::Synth synth(baa_starts[Voice]);
    const voice::Signal y = bleat(synth, voices[Voice][0], voices[Voice][1], voices[Voice][2]);
    const voice::Signal result = voice::master(y, -13);
    return result;
}

// munching clover: crisp tearing and chewing
voice::Signal munch() {
    voice::Synth synth(munch_start);
    voice::Generator rng(script_seed);
    const std::size_t n = voice::samples(.9);
    voice::Signal y(n, 0.0);
    for (int j = 0; j < 6; ++j) {
        const double jitter = rng.uniform(0, .03);
        const std::size_t at = voice::samples(.05 + j * .13 + jitter);
        const std::size_t m = voice::samples(.07);
        const voice::Signal tt = voice::tarr(m);
        const voice::Signal tear = voice::fft_filter(synth.noise(m), 1200, 7000);
        const double loudness = rng.uniform(.6, 1);
        for (std::size_t i = 0; i < m && at + i < n; ++i) {
            y[at + i] += tear[i] * std::exp(-tt[i] / .02) * loudness;
        }
    }
    const voice::Signal result = voice::master(y, -17);
    return result;
}

// the hint: a little chime on the pentatonic
voice::Signal hint() {
    const std::size_t n = voice::samples(.9);
    const voice::Signal t = voice::tarr(n);
    static constexpr double bells[3][3] = {{1046.5, 1, .35}, {1318.5, .6, .3}, {1568, .4, .25}};
    voice::Signal x(n, 0.0);
    for (const double* bell : bells) {
        const double w = two_pi * bell[0];
        for (std::size_t i = 0; i < n; ++i) {
            x[i] += bell[1] * std::sin(w * t[i]) * std::exp(-t[i] / bell[2]);
        }
    }
    const voice::Signal result = voice::master(x, -17);
    return result;
}

// undo: a soft backwards swish
voice::Signal undo() {
    voice::Synth synth(undo_start);
    const std::size_t n = voice::samples(.18);
    const voice::Signal t = voice::tarr(n);
    const voice::Signal swish = voice::fft_filter(synth.noise(n), 800, 4000);
    voice::Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double s = std::sin(voice::pi * t[i] / t[n - 1]);
        x[i] = swish[i] * (s * s);
    }
    const voice::Signal result = voice::master(x, -22);
    return result;
}

voice::Signal decaying_tone(double seconds, double frequency, double decay, double peak_db) {
    const std::size_t n = voice::samples(seconds);
    const voice::Signal t = voice::tarr(n);
    const double w = two_pi * frequency;
    voice::Signal x(n);
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = std::sin(w * t[i]) * std::exp(-t[i] / decay);
    }
    const voice::Signal result = voice::master(x, peak_db);
    return result;
}

// a click; a refused patch
voice::Signal click() {
    const voice::Signal result = decaying_tone(.04, 1500, .006, -23);
    return result;
}

voice::Signal nope() {
    const voice::Signal result = decaying_tone(.14, 180, .04, -20);
    return result;
}

// penned: a contented little bleat (the tune plays with it); got away: a cheeky one, rising
voice::Signal penned() {
    voice::Synth synth(penned_start);
    voice::Signal x(voice::samples(.4), 0.0);
    const voice::Signal y = bleat(synth, 300, .9, 5.5, .08);
    x.insert(x.end(), y.begin(), y.end());
    const voice::Signal result = voice::master(x, -14);
    return result;
}

voice::Signal escape() {
    voice::Synth synth(escape_start);
    const voice::Signal y = bleat(synth, 480, .5, 9, .05);
    const voice::Signal result = voice::master(y, -13);
    return result;
}

constexpr voice::Cue cues[] = {
    {.name = "sh_place", .mono = place},   {.name = "sh_hop", .mono = hop},
    {.name = "sh_baa_01", .mono = baa<0>}, {.name = "sh_baa_02", .mono = baa<1>},
    {.name = "sh_baa_03", .mono = baa<2>}, {.name = "sh_baa_04", .mono = baa<3>},
    {.name = "sh_munch", .mono = munch},   {.name = "sh_hint", .mono = hint},
    {.name = "sh_undo", .mono = undo},     {.name = "sh_click", .mono = click},
    {.name = "sh_nope", .mono = nope},     {.name = "sh_penned", .mono = penned},
    {.name = "sh_escape", .mono = escape},
};

}  // namespace

std::span<const voice::Cue> sound_cues() {
    const std::span<const voice::Cue> result(cues);
    return result;
}

void register_sound_cues() {
    voice::Bank::shared().add(cues);
    voice::Bank::shared().add(music_cues());
}

}  // namespace sh
