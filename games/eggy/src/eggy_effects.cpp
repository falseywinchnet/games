#include "eggy_cues.hpp"
#include "eggy_fft.hpp"
#include "eggy_synth.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

// authoring/eggy/audio_src/eggy_sfx.py line for line, in numpy's order of operations,
// with the helpers it imports from the shared batch scripts (sfx.py: nz, env, place_at,
// verb, finish; sfx2.py: seq, mix, swoosh, glide). The script runs every cue in one
// process: synth._rng and its own rng (default_rng(2026)) carry on from cue to cue, so
// each cue here starts from the states they had where the script began that cue
// (captured from the script itself). No cue reuses a note an earlier cue cached.
// The beds' long circular filters and many short crackles go through eggy_fft's
// transforms (the same filter, about 1e-15 apart). A master is what main() handed to write_wav: finish()'s output for the effects, the
// peak-normalised stereo for the beds.
namespace eggy {
namespace {

using voice::Signal;
constexpr double sr = voice::sample_rate;
constexpr double two_pi = 2 * voice::pi;

constexpr voice::Word128 synth_increment{0xc9c7353e6e2b1f28ull, 0x7d761f2d4027fae7ull};
constexpr voice::Word128 script_increment{0xbec6782ecb0472d8ull, 0xdd766bd09854840bull};

struct Start {
    std::string_view name;
    voice::GeneratorState synth;
    voice::GeneratorState rng;
};

// clang-format off
constexpr Start starts[] = {
#include "eggy_effect_states.inc"
};
// clang-format on

// The module and the script's generator as the script had them when it began `name`.
struct Script {
    EggySynth synth;
    voice::Generator rng;
    explicit Script(std::string_view name) : synth(find(name).synth), rng(find(name).rng) {}
    static const Start& find(std::string_view name) {
        for (const Start& s : starts) {
            if (s.name == name) {
                return s;
            }
        }
        return starts[0];
    }
};

// ------------------------------------------------------------------ sfx.py / sfx2.py helpers

// nz(n, seed, lo, hi) = nzb(n, lo, hi, seed): seeded band noise normalised to its peak
Signal nzb(std::size_t n, double lo, double hi, std::uint64_t seed) {
    Signal x = voice::fft_filter(voice::seeded_noise(n, seed), lo, hi);
    const double top = voice::peak(x) + 1e-12;
    for (double& value : x) {
        value /= top;
    }
    return x;
}

Signal env(std::size_t n, double att, double tau) {
    const Signal t = voice::tarr(n);
    Signal e(n);
    for (std::size_t i = 0; i < n; ++i) {
        e[i] = (1 - std::exp(-t[i] / std::max(att, 1e-4))) * std::exp(-t[i] / tau);
    }
    return e;
}

void place_at(Signal& buf, const Signal& y, double sec) {
    const std::size_t s = voice::samples(sec);
    if (s >= buf.size()) {
        return;
    }
    const std::size_t e = std::min(buf.size(), s + y.size());
    for (std::size_t i = s; i < e; ++i) {
        buf[i] += y[i - s];
    }
}

Signal scaled(Signal y, double g) {
    for (double& value : y) {
        value = value * g;
    }
    return y;
}

Signal times(const Signal& a, const Signal& b) {
    Signal y(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        y[i] = a[i] * b[i];
    }
    return y;
}

// IR = make_ir(1.6, 1.2, 1.0, 0.5, 0.01, seed=11), made once when sfx.py is imported
const voice::Stereo& room() {
    static const voice::Stereo ir = voice::make_ir(1.6, 1.2, 1.0, 0.5, 0.01, 11);
    return ir;
}

// verb(x, amt, tail): the dry signal plus the mean of the room's two channels
Signal verb(const Signal& x, double amt = .15, double tail = .6) {
    Signal st = x;
    st.resize(x.size() + voice::samples(tail), 0.0);
    const voice::Stereo w = voice::conv_reverb(voice::Stereo{st, st}, room(), false);
    Signal y(st.size());
    for (std::size_t i = 0; i < st.size(); ++i) {
        y[i] = st[i] + amt * ((w.left[i] + w.right[i]) / 2);
    }
    return y;
}

// finish(x, peak_db, fade=0.01)
Signal finish(Signal x, double peak_db) {
    const double mean = voice::sum(x) / static_cast<double>(x.size());
    for (double& value : x) {
        value = value - mean;
    }
    const std::size_t k = voice::samples(.01);
    const Signal down = voice::linspace(1, 0, k);
    for (std::size_t i = 0; i < k; ++i) {
        x[x.size() - k + i] *= down[i] * down[i];
    }
    const Signal up = voice::linspace(0, 1, 16);
    for (std::size_t i = 0; i < 16; ++i) {
        x[i] *= up[i];
    }
    const double thr = std::pow(10.0, -70.0 / 20) * voice::peak(x);
    std::size_t last = 0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        if (std::fabs(x[i]) > thr) {
            last = i;
        }
    }
    x.resize(std::min(x.size(), last + k));
    for (std::size_t i = 0; i < k; ++i) {
        x[x.size() - k + i] *= down[i];
    }
    const double top = voice::peak(x);
    const double gain = std::pow(10.0, peak_db / 20);
    for (double& value : x) {
        value = value / top * gain;
    }
    return x;
}

struct Note {
    double m;
    double t;
};

// seq(notes, inst, gap, vel, dur, tail)
Signal seq(EggySynth& synth, const std::vector<Note>& ev, std::string_view inst, double vel = .6, double dur = .4,
           double tail = 1.5) {
    double last = ev[0].t;
    for (const Note& e : ev) {
        last = std::max(last, e.t);
    }
    Signal y(voice::samples(last + tail + dur), 0.0);
    for (const Note& e : ev) {
        place_at(y, synth.render_note(inst, e.m, dur, vel), e.t);
    }
    return y;
}

std::vector<Note> spaced(std::initializer_list<double> notes, double gap) {
    std::vector<Note> ev{};
    int i = 0;
    for (const double m : notes) {
        ev.push_back({m, i * gap});
        ++i;
    }
    return ev;
}

struct Part {
    Signal p;
    double o;
    double g;
};

// mix(*parts)
Signal mix(const std::vector<Part>& parts) {
    std::size_t n = 0;
    for (const Part& part : parts) {
        n = std::max(n, part.p.size() + voice::samples(part.o));
    }
    Signal y(n, 0.0);
    for (const Part& part : parts) {
        place_at(y, scaled(part.p, part.g), part.o);
    }
    return y;
}

// swoosh(dur, lo, hi, seed, rise, amp)
Signal swoosh(double dur, double lo, double hi, std::uint64_t seed, bool rise = true, double amp = .3) {
    const std::size_t n = voice::samples(dur);
    const Signal t = voice::tarr(n);
    const Signal x = nzb(n, lo, hi, seed);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double u = t[i] / dur;
        const double s = std::sin(voice::pi * std::pow(u, rise ? 0.6 : 1.6));
        y[i] = x[i] * (s * s) * amp;
    }
    return y;
}

// glide(f0, f1, dur, kind, amp, tau): an exponential sweep, a sine or a wavetable
Signal glide(EggySynth* synth, double f0, double f1, double dur, std::optional<voice::Wave> kind = std::nullopt,
             double amp = .4, double tau = 0) {
    const std::size_t n = voice::samples(dur);
    const Signal t = voice::tarr(n);
    const double ratio = f1 / f0;
    Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        f[i] = f0 * std::pow(ratio, t[i] / dur);
    }
    Signal y(n);
    if (!kind.has_value()) {
        const Signal cs = voice::cumulative_sum(f);
        for (std::size_t i = 0; i < n; ++i) {
            y[i] = std::sin(two_pi * cs[i] / sr);
        }
    } else {
        y = (*synth).base().wt(*kind, f, 6);
    }
    const Signal e = env(n, .004, tau != 0 ? tau : dur / 3);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * e[i] * amp;
    }
    return y;
}

Signal sine_glide(double f0, double f1, double dur, double amp, double tau) {
    return glide(nullptr, f0, f1, dur, std::nullopt, amp, tau);
}

// peep(f0, dur, up, down, amp): one duckling peep, a quick rise then fall in pitch
Signal peep(double f0, double dur, double up, double down) {
    const std::size_t n = voice::samples(dur);
    const Signal t0 = voice::tarr(n);
    Signal t(n);
    Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        t[i] = t0[i] / dur;
        f[i] = f0 * (t[i] < .35 ? 1 + (up - 1) * (t[i] / .35) : up + (down - up) * ((t[i] - .35) / .65));
    }
    const Signal cs = voice::cumulative_sum(f);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double ph = two_pi * cs[i] / sr;
        const double e = std::pow(std::sin(voice::pi * std::clamp(t[i], 0.0, 1.0)), 1.5);
        y[i] = (std::sin(ph) + .18 * std::sin(2 * ph) + .05 * std::sin(3 * ph)) * e * 1.0;
    }
    return y;
}

// ------------------------------------------------------------------ chirps
struct Peep {
    double f;
    double d;
    double t0;
};

template <std::size_t I>
Signal chirp() {
    static constexpr std::array<std::array<Peep, 3>, 6> specs{{{{{3000, .09, 0}}},
                                                               {{{3300, .07, 0}, {3500, .06, .1}}},
                                                               {{{2800, .12, 0}}},
                                                               {{{3600, .05, 0}, {3800, .05, .07}, {4000, .05, .14}}},
                                                               {{{3100, .1, 0}}},
                                                               {{{2600, .08, 0}, {3400, .08, .11}}}}};
    static constexpr std::array<std::string_view, 6> names{"eggy_chirp_01", "eggy_chirp_02", "eggy_chirp_03",
                                                           "eggy_chirp_04", "eggy_chirp_05", "eggy_chirp_06"};
    Script s(names[I]);
    Signal y(voice::samples(.4), 0.0);
    for (const Peep& p : specs[I]) {
        if (p.f == 0) {
            break;
        }
        const double f0 = p.f * s.rng.uniform(.95, 1.05);
        const double up = s.rng.uniform(1.15, 1.35);
        const double down = s.rng.uniform(.82, .95);
        place_at(y, peep(f0, p.d, up, down), p.t0);
    }
    return finish(verb(y, .08, .15), -17);
}

// ------------------------------------------------------------------ steps
Signal step_grass() {
    const std::size_t n = voice::samples(.08);
    const Signal a = nzb(n, 300, 2500, 1);
    const Signal ea = env(n, .002, .015);
    const Signal b = nzb(n, 4000, 9000, 2);
    const Signal eb = env(n, .001, .008);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = a[i] * ea[i] * .8 + b[i] * eb[i] * .3;
    }
    return finish(y, -27);
}

Signal step_snow() {
    Script s("eggy_step_snow");
    Signal y(voice::samples(.09), 0.0);
    for (int k = 0; k < 6; ++k) {
        const std::size_t g = voice::samples(.006);
        const Signal grain = times(nzb(g, 800, 5000, static_cast<std::uint64_t>(10 + k)), env(g, .0005, .002));
        place_at(y, scaled(grain, s.rng.uniform(.4, 1)), k * .012);
    }
    return finish(y, -25);
}

Signal step_rock() {
    Script s("eggy_step_rock");
    const Signal stone = s.synth.render_note("litho", 96, .1, .3);
    const std::size_t c = voice::samples(.01);
    Signal click = times(nzb(c, 2000, 9000, 3), env(c, .0003, .002));
    click.resize(c + voice::samples(.69), 0.0);
    Signal y(stone.size());
    for (std::size_t i = 0; i < y.size(); ++i) {
        y[i] = stone[i] * .4 + click[i];
    }
    return finish(y, -27);
}

Signal step_ice() {
    const std::size_t n = voice::samples(.08);
    const Signal t = voice::tarr(n);
    const Signal a = env(n, .0005, .02);
    const Signal b = env(n, .0005, .01);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = std::sin(two_pi * 3100 * t[i]) * a[i] + .3 * std::sin(two_pi * 4700 * t[i]) * b[i];
    }
    return finish(y, -28);
}

Signal step_water() {
    Signal y(voice::samples(.25), 0.0);
    const std::size_t m = voice::samples(.06);
    place_at(y, times(nzb(m, 500, 4000, 4), env(m, .002, .015)), 0);
    place_at(y, sine_glide(600, 1300, .06, .4, .03), .03);
    return finish(y, -23);
}

// ------------------------------------------------------------------ bigger
Signal hop() {
    return finish(mix({{sine_glide(520, 950, .1, .5, .06), 0, 1}, {swoosh(.12, 1500, 6000, 21), 0, .25}}), -19);
}

Signal land() {
    const std::size_t n = voice::samples(.2);
    const Signal t = voice::tarr(n);
    const Signal a = env(n, .002, .03);
    const Signal b = nzb(n, 200, 1500, 5);
    const Signal eb = env(n, .001, .02);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = std::sin(two_pi * 120 * t[i]) * a[i] + .4 * b[i] * eb[i];
    }
    return finish(y, -19);
}

Signal splash() {
    Script s("eggy_splash");
    Signal y(voice::samples(1.0), 0.0);
    const std::size_t m = voice::samples(.3);
    place_at(y, times(nzb(m, 400, 6000, 6), env(m, .003, .07)), 0);
    for (int k = 0; k < 6; ++k) {
        const double f0 = s.rng.uniform(500, 900);
        const double f1 = s.rng.uniform(1100, 1800);
        const Signal g = sine_glide(f0, f1, .05, .25, .025);
        place_at(y, g, .05 + k * .07 + s.rng.uniform(0, .03));
    }
    return finish(verb(y, .1, .3), -15);
}

Signal drink() {
    Signal y(voice::samples(1.3), 0.0);
    for (int k = 0; k < 3; ++k) {
        place_at(y, sine_glide(720, 340, .12, .6, .06), .1 + k * .32);
    }
    return finish(verb(y, .1, .3), -17);
}

Signal refresh() {
    Script s("eggy_refresh");
    const Signal a = seq(s.synth, spaced({84, 88, 91, 96}, .07), "celesta", .55, .6);
    const Signal b = s.synth.render_note("glass", 84, .8, .4);
    return finish(verb(mix({{a, 0, .5}, {b, .25, .25}}), .35, .9), -12);
}

Signal pant() {
    Signal y(voice::samples(1.6), 0.0);
    for (int k = 0; k < 4; ++k) {
        const std::size_t hn = voice::samples(.12);
        const Signal x = nzb(hn, 900, 2200, static_cast<std::uint64_t>(30 + k));
        const Signal u = voice::linspace(0, 1, hn);
        Signal h(hn);
        for (std::size_t i = 0; i < hn; ++i) {
            const double s = std::sin(voice::pi * u[i]);
            h[i] = x[i] * (s * s) * (.9 - .12 * k);
        }
        place_at(y, h, .05 + k * .33);
    }
    return finish(y, -21);
}

Signal bonk() {
    Script s("eggy_bonk");
    const std::size_t whirr_n = voice::samples(.8);
    const Signal wt = voice::tarr(whirr_n);
    Signal f(whirr_n);
    for (std::size_t i = 0; i < whirr_n; ++i) {
        f[i] = 1400 + 400 * std::sin(two_pi * 9 * wt[i]);
    }
    const Signal cs = voice::cumulative_sum(f);
    const Signal e = env(whirr_n, .02, .25);
    Signal whirr(whirr_n);
    for (std::size_t i = 0; i < whirr_n; ++i) {
        whirr[i] = std::sin(two_pi * cs[i] / sr) * e[i] * .25;
    }
    const Signal block = s.synth.base().woodblock(.6, false);
    const Signal fall = sine_glide(300, 160, .15, .7, .05);
    return finish(verb(mix({{block, 0, .7}, {fall, 0, 1}, {whirr, .12, 1}}), .15, .4), -12);
}

Signal star() {
    Script s("eggy_star");
    Signal sh(voice::samples(2.2), 0.0);
    constexpr std::array<double, 6> pings{88, 91, 93, 95, 98, 100};
    for (int k = 0; k < 10; ++k) {
        const double m = pings[s.rng.choice(pings.size())];
        place_at(sh, scaled(s.synth.render_note("ping", m, .1, .35), .3), .2 + k * .06);
    }
    const Signal run = seq(s.synth, spaced({79, 83, 86, 91, 95}, .06), "celesta", .6, .5);
    return finish(verb(mix({{run, 0, .6}, {sh, 0, 1}}), .4, 1.2), -10);
}

Signal gust() {
    const std::size_t gn = voice::samples(2.2);
    const Signal t = voice::tarr(gn);
    Signal g = voice::fft_filter(voice::seeded_noise(gn, 40), 300, 3000);
    for (std::size_t i = 0; i < gn; ++i) {
        g[i] = g[i] * std::pow(std::sin(voice::pi * (t[i] / 2.2)), 1.5);
    }
    g = voice::fft_filter(g, 200, 2500);
    const double top = voice::peak(g);
    for (double& value : g) {
        value /= top;
    }
    return finish(g, -15);
}

Signal slide() {
    const std::size_t sn = voice::samples(.9);
    const Signal x = nzb(sn, 3000, 11000, 41);
    const Signal u = voice::linspace(0, 1, sn);
    Signal hiss(sn);
    for (std::size_t i = 0; i < sn; ++i) {
        hiss[i] = x[i] * std::sin(voice::pi * u[i]) * .4;
    }
    return finish(mix({{hiss, 0, 1}, {sine_glide(1600, 700, .6, .25, .4), .1, 1}}), -17);
}

Signal preen() {
    Script s("eggy_preen");
    Signal y(voice::samples(.8), 0.0);
    for (int k = 0; k < 5; ++k) {
        const std::size_t bn = voice::samples(.07);
        const Signal x = times(nzb(bn, 2000, 7000, static_cast<std::uint64_t>(50 + k)), env(bn, .005, .02));
        const Signal b = scaled(x, s.rng.uniform(.5, 1));
        place_at(y, b, k * .13 + s.rng.uniform(0, .04));
    }
    return finish(y, -23);
}

Signal flap() {
    Signal y(voice::samples(.7), 0.0);
    for (int k = 0; k < 4; ++k) {
        place_at(y, swoosh(.09, 800, 4000, static_cast<std::uint64_t>(60 + k), true, .5), k * .1);
    }
    return finish(y, -19);
}

Signal milestone() {
    Script s("eggy_milestone");
    const Signal y = seq(s.synth, {{74, 0}, {78, .14}, {81, .28}, {86, .42}}, "brass", .6, .3);
    return finish(verb(y, .3, .8), -12);
}

Signal biome() {
    Script s("eggy_biome");
    const Signal a = s.synth.render_note("vibes", 81, 1.2, .4);
    const Signal b = s.synth.render_note("bowl", 69, 1.5, .4);
    return finish(verb(mix({{a, 0, .5}, {b, 0, .4}}), .4, 1.2), -16);
}

Signal tent() {
    Signal y(voice::samples(.8), 0.0);
    for (int k = 0; k < 18; ++k) {
        const std::size_t tn = voice::samples(.012);
        place_at(y, times(nzb(tn, 1500, 6000, static_cast<std::uint64_t>(70 + k)), env(tn, .0005, .004)), .05 + k * .03);
    }
    return finish(verb(y, .1, .3), -15);
}

Signal salute() {
    Script s("eggy_salute");
    const Signal drum = s.synth.drum("snare", .6, 1);
    const Signal horn = s.synth.render_note("brass", 81, .25, .55);
    return finish(verb(mix({{drum, 0, .6}, {horn, .02, .4}}), .2, .5), -12);
}

Signal medal() {
    Script s("eggy_medal");
    const Signal a = s.synth.render_note("vibes", 93, .6, .6);
    const Signal b = s.synth.drum("anvil", .3, 2);
    const Signal c = seq(s.synth, spaced({96, 100, 103}, .05), "celesta", .4);
    return finish(verb(mix({{a, 0, .6}, {b, 0, .25}, {c, .1, .3}}), .35, 1.0), -10);
}

Signal ui_click() {
    voice::Synth synth{};
    return finish(scaled(synth.woodblock(.4, true), .6), -19);
}

Signal ui_key() {
    voice::Synth synth{};
    return finish(scaled(synth.tick(.5), .7), -21);
}

// ------------------------------------------------------------------ main()'s own cues
Signal thunder(bool near) {
    const std::size_t n = voice::samples(near ? 5.5 : 7.0);
    const Signal t = voice::tarr(n);
    const Signal rumble = voice::fft_filter(voice::seeded_noise(n, near ? 301 : 300), 25, near ? 260 : 160);
    const double top = voice::peak(rumble);
    const double decay = near ? 1.6 : 2.6;
    const double rise = near ? .02 : .35;
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double swell = std::exp(-t[i] / decay) * (1 - std::exp(-t[i] / rise));
        const double roll = 1 + .5 * std::sin(two_pi * .7 * t[i]) * std::sin(two_pi * 1.9 * t[i] + 1);
        y[i] = rumble[i] / top * swell * roll;
    }
    if (near) {
        const std::size_t k = voice::samples(.25);
        const Signal crack = nzb(k, 1500, 9000, 310);
        const Signal e = env(k, .001, .04);
        for (std::size_t i = 0; i < k; ++i) {
            y[i] += 1.2 * crack[i] * e[i];
        }
    }
    return verb(y, .2, 1.0);
}

Signal fanfare() {
    Script s("eggy_fanfare");
    Signal y(voice::samples(6.5), 0.0);
    constexpr std::array<std::array<double, 3>, 7> notes{
        {{62, 0, .2}, {62, .25, .2}, {62, .5, .2}, {69, .75, .6}, {66, 1.4, .25}, {69, 1.65, .25}, {74, 1.9, 1.6}}};
    for (const auto& [m, t, d] : notes) {
        place_at(y, scaled(s.synth.render_note("brass", m, d, .7), .45), t);
        place_at(y, scaled(s.synth.render_note("flute", m + 12, d, .5), .2), t);
    }
    for (const double m : {50.0, 57.0, 62.0, 66.0, 69.0}) {
        place_at(y, scaled(s.synth.render_note("brass", m, 2.6, .5), .14), 1.9);
    }
    place_at(y, scaled(s.synth.render_note("tuba", 38, 2.6, .7), .5), 1.9);
    for (int k = 0; k < 24; ++k) {
        place_at(y, scaled(s.synth.drum("ghost", .12 + .02 * k, static_cast<std::uint64_t>(k)), .8), k * .075);
    }
    place_at(y, s.synth.drum("snare", .6, 3), 1.9);
    place_at(y, s.synth.base().triangle(.25), 1.9);
    place_at(y, scaled(s.synth.render_note("bowl", 62, 3, .5), .4), 1.9);
    return finish(verb(y, .35, 2.0), -6);
}

Signal thunder_near() {
    return finish(thunder(true), -5);
}

Signal thunder_far() {
    return finish(thunder(false), -9);
}

Signal tree_fall() {
    Script s("eggy_tree_fall");
    Signal y(voice::samples(4.0), 0.0);
    for (int k = 0; k < 10; ++k) {  // creaks
        const double f0 = 140 + 30 * k;
        place_at(y, scaled(glide(&s.synth, f0, f0 * 1.4, .15, voice::Wave::reed, .12, .08), .4 + k * .06), .1 + k * .2);
    }
    const std::size_t cn = voice::samples(1.4);
    const Signal ct = voice::tarr(cn);
    const Signal rumble = voice::fft_filter(voice::seeded_noise(cn, 320), 60, 1800);
    Signal crash(cn);
    for (std::size_t i = 0; i < cn; ++i) {
        crash[i] = rumble[i] * std::exp(-ct[i] / .3) + std::sin(two_pi * 55 * ct[i]) * std::exp(-ct[i] / .25) * .8;
    }
    const double top = voice::peak(crash);
    for (double& value : crash) {
        value /= top;
    }
    place_at(y, crash, 2.35);
    for (int k = 0; k < 8; ++k) {  // leaves and branches settling
        const std::size_t m = voice::samples(.06);
        place_at(y, scaled(times(nzb(m, 1500, 7000, static_cast<std::uint64_t>(330 + k)), env(m, .003, .02)), .3),
                 2.5 + k * .09);
    }
    return finish(verb(y, .3, 1.0), -10);
}

Signal ribbit() {
    Signal y(voice::samples(.6), 0.0);
    for (int k = 0; k < 2; ++k) {
        const std::size_t n = voice::samples(.11);
        const Signal t = voice::tarr(n);
        Signal f(n);
        for (std::size_t i = 0; i < n; ++i) {
            f[i] = 220 * (1 + .25 * std::sin(two_pi * 30 * t[i]));
        }
        const Signal cs = voice::cumulative_sum(f);
        const Signal u = voice::linspace(0, 1, n);
        Signal croak(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double v = std::sin(two_pi * cs[i] / sr);
            const double sign = v > 0 ? 1.0 : (v < 0 ? -1.0 : 0.0);
            const double s = std::sin(voice::pi * u[i]);
            croak[i] = sign * .3 * (s * s);
        }
        place_at(y, croak, k * .16);
    }
    return finish(voice::fft_filter(verb(y, .1, .2), 120, 2500), -20);
}

Signal fish() {
    const std::size_t fn = voice::samples(.3);
    const Signal x = nzb(fn, 800, 6000, 340);
    const Signal e = env(fn, .002, .03);
    Signal plop(fn);
    for (std::size_t i = 0; i < fn; ++i) {
        plop[i] = x[i] * e[i] * .6;
    }
    return finish(verb(mix({{plop, 0, 1}, {sine_glide(900, 1700, .05, .3, .02), .02, 1}}), .1, .2), -22);
}

// ------------------------------------------------------------------ ambience(): seamless 20 s stereo beds
constexpr std::size_t bed_length = 20 * voice::sample_rate;

Signal circular_band(std::uint64_t seed, double lo, double hi, double gain) {
    Signal x = fft::filter(voice::seeded_noise(bed_length, seed), lo, hi, 2, 4096, true);
    if (gain != 1) {
        for (double& value : x) {
            value = value * gain;
        }
    }
    return x;
}

// x / np.abs(x).max() * 10 ** (-12 / 20)
voice::Stereo bed_level(voice::Stereo x) {
    const double top = std::max(voice::peak(x.left), voice::peak(x.right));
    const double gain = std::pow(10.0, -12.0 / 20);
    for (Signal* c : {&x.left, &x.right}) {
        for (double& value : *c) {
            value = value / top * gain;
        }
    }
    return x;
}

voice::Stereo amb_wind() {
    voice::Stereo w{circular_band(81, 150, 1800, 1), circular_band(82, 150, 1800, 1)};
    const Signal t = voice::tarr(bed_length);
    for (std::size_t i = 0; i < bed_length; ++i) {
        const double sw = .55 + .45 * std::sin(two_pi * 3 * t[i] / 20) * std::sin(two_pi * 2 * t[i] / 20 + 1);
        w.left[i] = w.left[i] * sw;
        w.right[i] = w.right[i] * sw;
    }
    return bed_level(std::move(w));
}

voice::Stereo amb_brook() {
    Script s("eggy_amb_brook");
    voice::Stereo b{circular_band(91, 600, 5000, .35), circular_band(92, 600, 5000, .35)};
    for (int k = 0; k < 900; ++k) {
        const auto s0 = static_cast<std::size_t>(s.rng.uniform(0, static_cast<double>(bed_length)));
        const double f0 = s.rng.uniform(500, 1600);
        const double d = s.rng.uniform(.015, .05);
        const double f1 = f0 * s.rng.uniform(1.2, 1.8);
        const double amp = s.rng.uniform(.1, .35);
        const Signal bl = sine_glide(f0, f1, d, amp, d / 2);
        Signal& ch = k % 2 == 0 ? b.left : b.right;
        for (std::size_t i = 0; i < bl.size(); ++i) {
            ch[(s0 + i) % bed_length] += bl[i];
        }
    }
    return bed_level(std::move(b));
}

voice::Stereo amb_forest() {
    voice::Stereo f{circular_band(101, 1500, 7000, 1), circular_band(102, 1500, 7000, 1)};
    const Signal t = voice::tarr(bed_length);
    for (std::size_t i = 0; i < bed_length; ++i) {
        const double a = two_pi * 5 * t[i] / 20;
        f.left[i] = f.left[i] * (.5 + .5 * std::sin(a + 0.0)) * .6;
        f.right[i] = f.right[i] * (.5 + .5 * std::sin(a + 1.3)) * .6;
    }
    return bed_level(std::move(f));
}

voice::Stereo amb_rain() {
    Script s("eggy_amb_rain");
    voice::Stereo rn{circular_band(111, 400, 9000, .5), circular_band(112, 400, 9000, .5)};
    const std::size_t dn = voice::samples(.004);
    const Signal fade = voice::linspace(1, 0, dn);
    std::array<Signal, 50> drops{};  // fft_filter(noise(dn, 2000 + k % 50)) repeats every 50 drops
    for (int k = 0; k < 6000; ++k) {
        const auto s0 = static_cast<std::size_t>(s.rng.uniform(0, static_cast<double>(bed_length)));
        Signal& filtered = drops[static_cast<std::size_t>(k % 50)];
        if (filtered.empty()) {
            filtered = fft::filter(voice::seeded_noise(dn, static_cast<std::uint64_t>(2000 + k % 50)), 1500, 8000);
        }
        const double level = s.rng.uniform(.2, .8);
        Signal& ch = k % 2 == 0 ? rn.left : rn.right;
        for (std::size_t i = 0; i < dn; ++i) {
            ch[(s0 + i) % bed_length] += filtered[i] * fade[i] * level;
        }
    }
    return bed_level(std::move(rn));
}

voice::Stereo amb_fire() {
    Script s("eggy_amb_fire");
    voice::Stereo fr{circular_band(121, 80, 900, .25), circular_band(122, 80, 900, .25)};
    for (int k = 0; k < 700; ++k) {
        const auto s0 = static_cast<std::size_t>(s.rng.uniform(0, static_cast<double>(bed_length)));
        const std::size_t cn = voice::samples(s.rng.uniform(.002, .012));
        const Signal crackle = fft::filter(voice::seeded_noise(cn, static_cast<std::uint64_t>(4000 + k % 60)), 1000, 7000);
        const double third = static_cast<double>(cn) / 3;
        const double level = s.rng.uniform(.3, 1.2);
        Signal& ch = k % 2 == 0 ? fr.left : fr.right;
        for (std::size_t i = 0; i < cn; ++i) {
            ch[(s0 + i) % bed_length] += crackle[i] * std::exp(-static_cast<double>(i) / third) * level;
        }
    }
    return bed_level(std::move(fr));
}

constexpr voice::Cue cues[] = {
    {.name = "eggy_chirp_01", .mono = chirp<0>},
    {.name = "eggy_chirp_02", .mono = chirp<1>},
    {.name = "eggy_chirp_03", .mono = chirp<2>},
    {.name = "eggy_chirp_04", .mono = chirp<3>},
    {.name = "eggy_chirp_05", .mono = chirp<4>},
    {.name = "eggy_chirp_06", .mono = chirp<5>},
    {.name = "eggy_step_grass", .mono = step_grass},
    {.name = "eggy_step_snow", .mono = step_snow},
    {.name = "eggy_step_rock", .mono = step_rock},
    {.name = "eggy_step_ice", .mono = step_ice},
    {.name = "eggy_step_water", .mono = step_water},
    {.name = "eggy_hop", .mono = hop},
    {.name = "eggy_land", .mono = land},
    {.name = "eggy_splash", .mono = splash},
    {.name = "eggy_drink", .mono = drink},
    {.name = "eggy_refresh", .mono = refresh},
    {.name = "eggy_pant", .mono = pant},
    {.name = "eggy_bonk", .mono = bonk},
    {.name = "eggy_star", .mono = star},
    {.name = "eggy_gust", .mono = gust},
    {.name = "eggy_slide", .mono = slide},
    {.name = "eggy_preen", .mono = preen},
    {.name = "eggy_flap", .mono = flap},
    {.name = "eggy_milestone", .mono = milestone},
    {.name = "eggy_biome", .mono = biome},
    {.name = "eggy_tent", .mono = tent},
    {.name = "eggy_salute", .mono = salute},
    {.name = "eggy_medal", .mono = medal},
    {.name = "eggy_ui_click", .mono = ui_click},
    {.name = "eggy_ui_key", .mono = ui_key},
    {.name = "eggy_fanfare", .mono = fanfare},
    {.name = "eggy_thunder_near", .mono = thunder_near},
    {.name = "eggy_thunder_far", .mono = thunder_far},
    {.name = "eggy_tree_fall", .mono = tree_fall},
    {.name = "eggy_ribbit", .mono = ribbit},
    {.name = "eggy_fish", .mono = fish},
    {.name = "eggy_amb_wind", .stereo = amb_wind},
    {.name = "eggy_amb_brook", .stereo = amb_brook},
    {.name = "eggy_amb_forest", .stereo = amb_forest},
    {.name = "eggy_amb_rain", .stereo = amb_rain},
    {.name = "eggy_amb_fire", .stereo = amb_fire},
};

}  // namespace

std::span<const voice::Cue> effect_cues() {
    const std::span<const voice::Cue> result(cues);
    return result;
}

}  // namespace eggy
