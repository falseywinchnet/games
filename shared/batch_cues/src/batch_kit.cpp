#include "batch_kit.hpp"

#include <algorithm>
#include <cmath>

namespace batch_cues {
namespace {

constexpr double two_pi = 2 * voice::pi;

double quantised_duration(double dur) {
    const double result = voice::python_round(dur * 100) / 100;
    return result;
}

double quantised_velocity(double vel) {
    const double result = voice::python_round(vel * 20) / 20;
    return result;
}

// IR = make_ir(1.6, 1.2, 1.0, 0.5, 0.01, seed=11), made once when sfx.py is imported.
const voice::Stereo& room() {
    static const voice::Stereo ir = voice::make_ir(1.6, 1.2, 1.0, 0.5, 0.01, 11);
    return ir;
}

}  // namespace

Notes::Notes(std::span<const NoteStart> script) : script_(script), primed_(script.size(), false) {}

const voice::Signal& Notes::note(std::string_view inst, double m, double dur, double vel) {
    const double dq = quantised_duration(dur);
    const double vq = quantised_velocity(vel);
    for (std::size_t i = 0; i < script_.size(); ++i) {
        const NoteStart& start = script_[i];
        const bool same = start.inst == inst && start.m == m && quantised_duration(start.dur) == dq &&
                          quantised_velocity(start.vel) == vq;
        if (same) {
            if (!primed_[i]) {
                synth_.prime_note(inst, start.m, start.dur, start.vel, start.state);
                primed_[i] = true;
            }
            break;
        }
    }
    const voice::Signal& result = synth_.render_note(inst, m, dur, vel);
    return result;
}

voice::Signal scaled(std::span<const double> x, double gain) {
    voice::Signal y(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        y[i] = x[i] * gain;
    }
    return y;
}

voice::Signal nz(std::size_t n, std::uint64_t seed, double lo, double hi, int order) {
    const voice::Signal noise = voice::seeded_noise(n, seed);
    voice::Signal x = voice::fft_filter(noise, lo, hi, order);
    const double top = voice::peak(x) + 1e-12;
    for (double& value : x) {
        value = value / top;
    }
    return x;
}

voice::Signal env(std::size_t n, double att, double tau) {
    const voice::Signal t = voice::tarr(n);
    const double a = std::max(att, 1e-4);
    voice::Signal e(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double rise = 1 - std::exp(-t[i] / a);
        e[i] = rise * std::exp(-t[i] / tau);
    }
    return e;
}

void place_at(voice::Signal& buf, std::span<const double> y, double sec) {
    const std::size_t s = voice::samples(sec);
    const std::size_t e = std::min(buf.size(), s + y.size());
    for (std::size_t i = s; i < e; ++i) {
        buf[i] += y[i - s];
    }
}

voice::Signal verb(std::span<const double> x, double amt, double tail) {
    const std::size_t length = x.size() + voice::samples(tail);
    voice::Stereo st{voice::Signal(length, 0.0), voice::Signal(length, 0.0)};
    std::copy(x.begin(), x.end(), st.left.begin());
    std::copy(x.begin(), x.end(), st.right.begin());
    const voice::Stereo w = voice::conv_reverb(st, room(), false);
    voice::Signal y(length);
    for (std::size_t i = 0; i < length; ++i) {
        const double mean = (w.left[i] + w.right[i]) / 2;
        y[i] = st.left[i] + amt * mean;
    }
    return y;
}

voice::Signal finish(std::span<const double> x, double peak_db, double fade) {
    const std::size_t n = x.size();
    const double mean = voice::sum(x) / static_cast<double>(n);
    voice::Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = x[i] - mean;
    }
    const std::size_t k = voice::samples(fade);
    const voice::Signal down = voice::linspace(1, 0, k);
    for (std::size_t j = 0; j < k; ++j) {
        y[n - k + j] *= down[j] * down[j];
    }
    const voice::Signal up = voice::linspace(0, 1, 16);
    for (std::size_t j = 0; j < 16; ++j) {
        y[j] *= up[j];
    }
    // trim trailing silence below -70 dB
    const double threshold = std::pow(10.0, -70.0 / 20) * voice::peak(y);
    std::size_t last = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (std::fabs(y[i]) > threshold) {
            last = i;
        }
    }
    const std::size_t end = std::min(n, last + k);
    y.resize(end);
    for (std::size_t j = 0; j < k; ++j) {
        y[end - k + j] *= down[j];
    }
    const double top = voice::peak(y);
    const double gain = std::pow(10.0, peak_db / 20);
    for (double& value : y) {
        value = value / top * gain;
    }
    return y;
}

voice::Signal chime(Notes& notes, std::span<const double> midi, std::string_view inst, double vel, double gap,
                    double dur) {
    const double count = static_cast<double>(midi.size());
    voice::Signal x(voice::samples(gap * count + 2.0), 0.0);
    for (std::size_t i = 0; i < midi.size(); ++i) {
        const voice::Signal& y = notes.note(inst, midi[i], dur, vel);
        place_at(x, y, static_cast<double>(i) * gap);
    }
    return x;
}

voice::Signal card_tick(std::uint64_t seed, double bright) {
    const std::size_t n = voice::samples(0.03);
    const voice::Signal band = nz(n, seed, 1800 * bright, 7000 * bright);
    const voice::Signal e = env(n, 0.0004, 0.004);
    voice::Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = band[i] * e[i];
    }
    return y;
}

voice::Signal seq(Notes& notes, std::span<const double> midi, std::string_view inst, double gap, double vel,
                  double dur, double tail) {
    const double last = static_cast<double>(midi.size() - 1) * gap;
    voice::Signal y(voice::samples(last + tail + dur), 0.0);
    for (std::size_t i = 0; i < midi.size(); ++i) {
        const voice::Signal& note = notes.note(inst, midi[i], dur, vel);
        place_at(y, note, static_cast<double>(i) * gap);
    }
    return y;
}

voice::Signal mix(std::span<const Part> parts) {
    std::size_t n = 0;
    for (const Part& part : parts) {
        n = std::max(n, part.signal.size() + voice::samples(part.offset));
    }
    voice::Signal y(n, 0.0);
    for (const Part& part : parts) {
        const voice::Signal gained = scaled(part.signal, part.gain);
        place_at(y, gained, part.offset);
    }
    return y;
}

voice::Signal swoosh(double dur, double lo, double hi, std::uint64_t seed, bool rise, double amp) {
    const std::size_t n = voice::samples(dur);
    const voice::Signal t = voice::tarr(n);
    const double exponent = rise ? 0.6 : 1.6;
    const voice::Signal band = nz(n, seed, lo, hi);
    voice::Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double u = t[i] / dur;
        const double s = std::sin(voice::pi * std::pow(u, exponent));
        const double shape = s * s;
        y[i] = band[i] * shape * amp;
    }
    return y;
}

voice::Signal glide(voice::Synth& synth, double f0, double f1, double dur, GlideKind kind, double amp, double tau) {
    const std::size_t n = voice::samples(dur);
    const voice::Signal t = voice::tarr(n);
    const double ratio = f1 / f0;
    voice::Signal f(n);
    for (std::size_t i = 0; i < n; ++i) {
        f[i] = f0 * std::pow(ratio, t[i] / dur);
    }
    voice::Signal y{};
    if (kind == GlideKind::sine) {
        const voice::Signal cs = voice::cumulative_sum(f);
        y.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            y[i] = std::sin(two_pi * cs[i] / voice::sample_rate);
        }
    } else {
        y = synth.wt(voice::Wave::flute, f, 6);
    }
    const voice::Signal e = env(n, .004, tau != 0 ? tau : dur / 3);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * e[i] * amp;
    }
    return y;
}

}  // namespace batch_cues
