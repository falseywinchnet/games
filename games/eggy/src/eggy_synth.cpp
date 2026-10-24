#include "eggy_synth.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

// eggy_synth.py line for line, in numpy's order of operations.
namespace eggy {
namespace {

constexpr double sr = voice::sample_rate;
constexpr double two_pi = 2 * voice::pi;

double clip(double x, double lo, double hi) {
    return std::min(std::max(x, lo), hi);
}

}  // namespace

EggySynth::EggySynth() = default;

EggySynth::EggySynth(const voice::GeneratorState& rng_state) : synth_(rng_state) {}

// Field snare: crisp head tone + wire rattle.
voice::Signal EggySynth::snare(double vel, std::optional<std::uint64_t> seed) {
    const std::size_t n = voice::samples(.25);
    const voice::Signal t = voice::tarr(n);
    voice::Signal wires = voice::fft_filter(synth_.noise(n, seed), 1800, 9000);
    for (std::size_t i = 0; i < n; ++i) {
        wires[i] = wires[i] * std::exp(-t[i] / .07);
    }
    const double top = voice::peak(wires) + 1e-9;
    for (double& value : wires) {
        value /= top;
    }
    const voice::Signal g = voice::gate(n, 10, .0008);
    voice::Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double head = std::sin(two_pi * 190 * t[i]) * std::exp(-t[i] / .035) * .5 +
                            std::sin(two_pi * 330 * t[i]) * std::exp(-t[i] / .02) * .25;
        y[i] = (head + wires[i] * .7) * g[i] * vel;
    }
    return y;
}

voice::Signal EggySynth::snare_ghost(double vel, std::optional<std::uint64_t> seed) {
    voice::Signal y = snare(vel, seed);
    const voice::Signal t = voice::tarr(voice::samples(.25));
    for (std::size_t i = 0; i < y.size(); ++i) {
        y[i] = y[i] * std::exp(-t[i] / .03);
    }
    return y;
}

voice::Signal EggySynth::anvil(double vel, std::optional<std::uint64_t> seed) {
    const std::size_t n = voice::samples(1.2);
    constexpr std::array<double, 5> ratios{1580, 2410, 3920, 5230, 7100};
    constexpr std::array<double, 5> amps{1, .6, .5, .3, .2};
    constexpr std::array<double, 5> taus{.5, .35, .25, .15, .1};
    voice::Signal y = voice::partials(1, n, ratios, amps, taus);
    const std::size_t k = voice::samples(.004);
    const voice::Signal ring = voice::fft_filter(synth_.noise(k, seed), 3000, 12000);
    for (std::size_t i = 0; i < k; ++i) {
        y[i] += .6 * ring[i];
    }
    const voice::Signal g = voice::gate(n, 10, .0003);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * g[i] * vel;
    }
    return y;
}

voice::Signal EggySynth::frame_drum(double vel, std::optional<std::uint64_t> seed) {
    const std::size_t n = voice::samples(.6);
    const voice::Signal t = voice::tarr(n);
    voice::Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        f[i] = 70 * (1 + .25 * std::exp(-t[i] / .03));
    }
    const voice::Signal cs = voice::cumulative_sum(f);
    const voice::Signal skin = voice::fft_filter(synth_.noise(n, seed), 100, 900);
    const voice::Signal g = voice::gate(n, 10, .002);
    voice::Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double body = std::sin(two_pi * cs[i] / sr) * std::exp(-t[i] / .25) + .2 * skin[i] * std::exp(-t[i] / .05);
        y[i] = body * g[i] * vel;
    }
    return y;
}

voice::Signal EggySynth::washboard(double vel, std::optional<std::uint64_t> seed) {
    const std::size_t n = voice::samples(.06);
    const voice::Signal t = voice::tarr(n);
    voice::Signal y = voice::fft_filter(synth_.noise(n, seed), 2500, 9000);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * (std::sin(two_pi * 180 * t[i]) > 0 ? 1.0 : 0.0) * std::exp(-t[i] / .02);
    }
    const double top = voice::peak(y) + 1e-9;
    for (double& value : y) {
        value = value / top * vel;
    }
    return y;
}

// Singing bowl: inharmonic, very long, slow beating.
voice::Signal EggySynth::bowl(double m, double dur, double vel) {
    const double f = voice::mtof(m);
    const std::size_t n = voice::samples(std::min(dur, 6.0) + 2.5);
    const voice::Signal t = voice::tarr(n);
    voice::Signal y(n, 0.0);
    constexpr std::array<std::array<double, 4>, 4> modes{
        {{1, 1, 4.0, .9}, {2.71, .45, 2.5, 1.7}, {5.1, .2, 1.4, 2.3}, {8.3, .08, .8, 3.1}}};
    for (const auto& [r, a, tau, beat] : modes) {
        if (f * r > 16000) {
            continue;
        }
        const double w = two_pi * f * r;
        const double wb = two_pi * beat;
        for (std::size_t i = 0; i < n; ++i) {
            y[i] += a * std::exp(-t[i] / tau) * std::sin(w * t[i]) * (1 + .35 * std::sin(wb * t[i]));
        }
    }
    const voice::Signal g = voice::gate(n, 10, .004);
    const double level = .3 + .7 * vel;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * g[i] * level * .6;
    }
    return y;
}

// Lonesome whistle: near sine, slow vibrato, slight breath.
voice::Signal EggySynth::whistle(double m, double dur, double vel) {
    const double f = voice::mtof(m);
    const double rel = .12;
    const std::size_t n = voice::samples(dur + rel);
    const voice::Signal t = voice::tarr(n);
    voice::Signal pitch(n);
    voice::Signal plain(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double vib = 1 + .006 * clip((t[i] - .2) / .3, 0, 1) * std::sin(two_pi * 5.5 * t[i]);
        const double scoop = 1 - .02 * std::exp(-t[i] / .05);
        pitch[i] = f * vib * scoop;
        plain[i] = f * vib;
    }
    const voice::Signal cs = voice::cumulative_sum(pitch);
    const voice::Signal cs2 = voice::cumulative_sum(plain);
    voice::Signal y(n);
    const double w2 = 2 * 2 * voice::pi;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = std::sin(two_pi * cs[i] / sr) + .06 * std::sin(w2 * cs2[i] / sr);
    }
    const voice::Signal br = voice::fft_filter(synth_.noise(n), f * .8, f * 2.5, 1);
    const double spread = voice::standard_deviation(br) + 1e-9;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] += .03 * br[i] / spread;
    }
    const voice::Signal g = voice::gate(n, dur, .05, rel);
    const double level = .35 + .65 * vel;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * g[i] * level;
    }
    return y;
}

voice::Signal EggySynth::banjo(double m, double dur, double vel) {
    return synth_.pluck(m, std::min(dur, .5), vel, .08, .45, 1.8);
}

// Reverb-drenched twangy guitar with tremolo.
voice::Signal EggySynth::twang(double m, double dur, double vel) {
    voice::Signal y = synth_.pluck(m, std::max(dur, .8), vel, .12, 1.6, 1.5);
    const voice::Signal t = voice::tarr(y.size());
    for (std::size_t i = 0; i < y.size(); ++i) {
        y[i] = y[i] * (1 + .3 * std::sin(two_pi * 6 * t[i]));
    }
    return y;
}

// Soft cute trumpet-ish (filtered saw with a little blat).
voice::Signal EggySynth::brass(double m, double dur, double vel) {
    const double f = voice::mtof(m);
    const double rel = .08;
    const std::size_t n = voice::samples(dur + rel);
    const voice::Signal t = voice::tarr(n);
    voice::Signal fr(n);
    for (std::size_t i = 0; i < n; ++i) {
        fr[i] = f * (1 - .015 * std::exp(-t[i] / .04)) * (1 + .004 * std::sin(two_pi * 5 * t[i]) * clip(t[i] - .25, 0, 1));
    }
    voice::Signal y = synth_.wt(voice::Wave::saw, fr, 30);
    y = voice::fft_filter(y, 150, 2500 + 2000 * vel, 1);
    const voice::Signal g = voice::gate(n, dur, .03, rel);
    const double level = .35 + .65 * vel;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * g[i] * level * .5;
    }
    return y;
}

// Low monastery drone with a vowel-ish formant.
voice::Signal EggySynth::drone(double m, double dur, double vel) {
    const double f = voice::mtof(m);
    const std::size_t n = voice::samples(dur + 1.0);
    const voice::Signal low(n, f * 1.0);
    const voice::Signal high(n, f * 1.003 * 1.0);
    const voice::Signal a = synth_.wt(voice::Wave::saw, low, 40);
    const voice::Signal b = synth_.wt(voice::Wave::saw, high, 40, .3);
    voice::Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = a[i] + b[i];
    }
    const voice::Signal body = voice::fft_filter(y, 40, 900, 1);
    const voice::Signal formant = voice::fft_filter(y, 600, 800, 3);
    const voice::Signal g = voice::gate(n, dur, .8, 1.0);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = (body[i] + .3 * formant[i]) * g[i] * vel * .25;
    }
    return y;
}

voice::Signal EggySynth::instrument(std::string_view name, double m, double dur, double vel) {
    if (name == "bowl") {
        return bowl(m, dur, vel);
    }
    if (name == "whistle") {
        return whistle(m, dur, vel);
    }
    if (name == "banjo") {
        return banjo(m, dur, vel);
    }
    if (name == "twang") {
        return twang(m, dur, vel);
    }
    if (name == "brass") {
        return brass(m, dur, vel);
    }
    if (name == "drone") {
        return drone(m, dur, vel);
    }
    return synth_.instrument(name, m, dur, vel);
}

voice::Signal EggySynth::drum(std::string_view name, double vel, std::optional<std::uint64_t> seed) {
    if (name == "snare") {
        return snare(vel, seed);
    }
    if (name == "ghost") {
        return snare_ghost(vel, seed);
    }
    if (name == "anvil") {
        return anvil(vel, seed);
    }
    if (name == "frame") {
        return frame_drum(vel, seed);
    }
    if (name == "washboard") {
        return washboard(vel, seed);
    }
    return synth_.drum(name, vel, seed);
}

const voice::Signal& EggySynth::render_note(std::string_view inst, double m, double dur, double vel) {
    const double vq = voice::python_round(vel * 20) / 20;
    const double dq = voice::python_round(dur * 100) / 100;
    NoteKey key{std::string(inst), m, dq, vq};
    auto found = notes_.find(key);
    if (found == notes_.end()) {
        voice::Signal y = instrument(inst, m, dq, vq);
        found = notes_.emplace(std::move(key), std::move(y)).first;
    }
    return found->second;
}

const voice::Signal& EggySynth::render_drum(std::string_view name, double vel, long long rr) {
    const double vq = voice::python_round(vel * 20) / 20;
    const long long r = voice::python_modulo(rr, 4);
    DrumKey key{std::string(name), vq, r};
    auto found = drums_.find(key);
    if (found == drums_.end()) {
        long long letters = 0;
        for (const char c : name) {
            letters += static_cast<unsigned char>(c);
        }
        const auto seed = static_cast<std::uint64_t>(1000 + r + letters % 97);
        voice::Signal y = drum(name, vq, seed);
        found = drums_.emplace(std::move(key), std::move(y)).first;
    }
    return found->second;
}

std::size_t EggySynth::cached_samples() const {
    std::size_t total = 0;
    for (const auto& [key, y] : notes_) {
        total += y.size();
    }
    for (const auto& [key, y] : drums_) {
        total += y.size();
    }
    return total;
}

void EggySynth::clear_caches() {
    notes_.clear();
    drums_.clear();
}

void EggySynth::prime_note(std::string_view inst, double m, double dur, double vel, const voice::GeneratorState& state) {
    const double vq = voice::python_round(vel * 20) / 20;
    const double dq = voice::python_round(dur * 100) / 100;
    EggySynth earlier(state);
    notes_[NoteKey{std::string(inst), m, dq, vq}] = earlier.instrument(inst, m, dq, vq);
}

}  // namespace eggy
