#include "voice_synth.hpp"

#include "voice_fft.hpp"

#include <algorithm>
#include <cmath>

// Each numpy expression is written out in the order numpy evaluates it (left to right,
// one binary operation at a time, Python scalars combined before they meet an array),
// because a reordered sum or product would change the last bits of the result.
namespace voice {
namespace {

constexpr double two_pi = 2 * pi;  // Python's 2 * np.pi
constexpr double sr = sample_rate;

double square(double x) {
    const double result = x * x;
    return result;
}

// numpy's `array ** exponent` for a Python int exponent: 2 is computed as a square.
double integer_power(double x, int exponent) {
    if (exponent == 2) {
        return x * x;
    }
    const double result = std::pow(x, static_cast<double>(exponent));
    return result;
}

double pairwise(const double* a, std::size_t n) {
    if (n < 8) {
        double result = 0.;
        for (std::size_t i = 0; i < n; ++i) {
            result += a[i];
        }
        return result;
    }
    if (n <= 128) {
        double r[8] = {a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]};
        std::size_t i = 8;
        for (; i < n - (n % 8); i += 8) {
            for (std::size_t j = 0; j < 8; ++j) {
                r[j] += a[i + j];
            }
        }
        double result = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
        for (; i < n; ++i) {
            result += a[i];
        }
        return result;
    }
    std::size_t half = n / 2;
    half -= half % 8;
    const double result = pairwise(a, half) + pairwise(a + half, n - half);
    return result;
}

void add_into(Signal& y, std::size_t offset, std::span<const double> x) {
    for (std::size_t i = 0; i < x.size() && offset + i < y.size(); ++i) {
        y[offset + i] += x[i];
    }
}

Signal scaled(std::span<const double> x, double factor) {
    Signal result(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        result[i] = x[i] * factor;
    }
    return result;
}

}  // namespace

// ------------------------------------------------------------------ numpy helpers
Signal tarr(std::size_t n) {
    Signal t(n);
    for (std::size_t i = 0; i < n; ++i) {
        t[i] = static_cast<double>(i) / sr;
    }
    return t;
}

Signal linspace(double start, double stop, std::size_t count) {
    Signal y(count);
    if (count == 0) {
        return y;
    }
    if (count == 1) {
        y[0] = start;
        return y;
    }
    const double divisions = static_cast<double>(count - 1);
    const double delta = stop - start;
    const double step = delta / divisions;
    for (std::size_t i = 0; i < count; ++i) {
        if (step == 0) {
            y[i] = static_cast<double>(i) / divisions * delta + start;
        } else {
            y[i] = static_cast<double>(i) * step + start;
        }
    }
    y[count - 1] = stop;
    return y;
}

double remainder(double a, double b) {
    double mod = std::fmod(a, b);
    if (b == 0) {
        return mod;
    }
    if (mod != 0) {
        if ((b < 0) != (mod < 0)) {
            mod += b;
        }
    } else {
        mod = std::copysign(0.0, b);
    }
    return mod;
}

double sum(std::span<const double> x) {
    // The reduction runs over the iterator's 8192-element buffers, each summed pairwise.
    constexpr std::size_t buffer = 8192;
    double result = 0.0;
    for (std::size_t start = 0; start < x.size(); start += buffer) {
        result += pairwise(x.data() + start, std::min(buffer, x.size() - start));
    }
    return result;
}

double standard_deviation(std::span<const double> x) {
    const double count = static_cast<double>(x.size());
    const double mean = sum(x) / count;
    Signal deviations(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double d = x[i] - mean;
        deviations[i] = d * d;
    }
    const double result = std::sqrt(sum(deviations) / count);
    return result;
}

double peak(std::span<const double> x) {
    double result = 0.0;
    for (const double value : x) {
        result = std::max(result, std::fabs(value));
    }
    return result;
}

double interpolate(std::span<const double> table, double x) {
    const std::size_t last = table.size() - 1;
    if (std::isnan(x)) {
        return x;
    }
    if (x < 0) {
        return table[0];
    }
    if (x >= static_cast<double>(last)) {
        return table[last];
    }
    const std::size_t j = static_cast<std::size_t>(x);
    if (static_cast<double>(j) == x) {
        return table[j];
    }
    const double slope = table[j + 1] - table[j];
    const double result = slope * (x - static_cast<double>(j)) + table[j];
    return result;
}

Signal cumulative_sum(std::span<const double> x) {
    Signal result(x.size());
    double running = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        running = i == 0 ? x[0] : running + x[i];
        result[i] = running;
    }
    return result;
}

double python_round(double x) {
    const double result = std::nearbyint(x);
    return result;
}

long long python_modulo(long long a, long long b) {
    long long result = a % b;
    if (result != 0 && ((result < 0) != (b < 0))) {
        result += b;
    }
    return result;
}

std::size_t samples(double seconds) {
    const std::size_t result = seconds <= 0 ? 0 : static_cast<std::size_t>(seconds * sr);
    return result;
}

// ------------------------------------------------------------------ synth.py helpers
double mtof(double midi) {
    const double result = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
    return result;
}

Signal gate(std::size_t n, double dur, double att, double rel) {
    Signal e(n, 1.0);
    std::size_t a = std::max<std::size_t>(samples(att), 1);
    a = std::min(a, n);
    for (std::size_t i = 0; i < a; ++i) {
        const double s = std::sin(0.5 * pi * static_cast<double>(i) / static_cast<double>(a));
        e[i] = s * s;
    }
    const std::size_t g = samples(dur);
    if (g < n) {
        const std::size_t r = std::max<std::size_t>(samples(rel), 1);
        const std::size_t seg = std::min(r, n - g);
        for (std::size_t i = 0; i < seg; ++i) {
            e[g + i] *= 0.5 * (1 + std::cos(pi * static_cast<double>(i) / static_cast<double>(r)));
        }
        for (std::size_t i = g + seg; i < n; ++i) {
            e[i] = 0;
        }
    }
    return e;
}

Signal fft_filter(std::span<const double> x, double lo, double hi, int order, std::size_t pad,
                  bool circular) {
    const std::size_t n = x.size();
    const std::size_t length = circular ? n : n + pad;
    std::vector<Complex> spectrum{};
    rfft(x, length, spectrum);
    // np.fft.rfftfreq(N, 1 / SR)
    const double spacing = 1.0 / static_cast<double>(sample_rate);
    const double unit = 1.0 / (static_cast<double>(length) * spacing);
    for (std::size_t k = 0; k < spectrum.size(); ++k) {
        const double f = static_cast<double>(k) * unit;
        double h = 1.0;
        if (hi != 0) {
            h /= std::sqrt(1 + integer_power(f / hi, 2 * order));
        }
        if (lo != 0) {
            h /= std::sqrt(1 + integer_power(lo / std::max(f, 1e-3), 2 * order));
        }
        spectrum[k] = Complex(spectrum[k].real() * h, spectrum[k].imag() * h);
    }
    Signal filtered{};
    irfft(spectrum, length, filtered);
    filtered.resize(n);
    return filtered;
}

Signal partials(double f, std::size_t n, std::span<const double> ratios, std::span<const double> amps,
                std::span<const double> taus, std::span<const double> phases) {
    const Signal t = tarr(n);
    Signal y(n, 0.0);
    const std::size_t count = std::min(ratios.size(), std::min(amps.size(), taus.size()));
    for (std::size_t i = 0; i < count; ++i) {
        const double fr = f * ratios[i];
        const double a = amps[i];
        if (fr > 18000 || a == 0) {
            continue;
        }
        const double tau = taus[i];
        const double ph = phases.empty() ? 0.0 : phases[i];
        const double w = two_pi * fr;
        for (std::size_t j = 0; j < n; ++j) {
            y[j] += a * std::exp(-t[j] / tau) * std::sin(w * t[j] + ph);
        }
    }
    return y;
}

Signal seeded_noise(std::size_t n, std::uint64_t seed) {
    Generator generator(seed);
    Signal result(n);
    generator.fill_standard_normal(result);
    return result;
}

Signal wavetable(Wave kind, int harmonics) {
    constexpr std::size_t size = 4096;
    Signal tab(size, 0.0);
    for (int k = 1; k <= harmonics; ++k) {
        double a = 0;
        if (kind == Wave::saw) {
            a = 1 / std::pow(static_cast<double>(k), 1.25);
        } else if (kind == Wave::flute) {
            static constexpr double flute[6] = {1, 0.32, 0.11, 0.05, 0.025, 0.012};
            a = k <= 6 ? flute[k - 1] : 0;
        } else if (kind == Wave::clar) {
            const double base = k % 2 != 0 ? 1 / std::pow(static_cast<double>(k), 0.9) : 0.04 / k;
            a = base * (k < 9 ? 1 : 0.4);
        } else {
            a = 1 / std::pow(static_cast<double>(k), 0.8) * (k % 2 == 0 ? 0.6 : 1);
        }
        if (a == 0) {
            continue;
        }
        const double w = two_pi * k;
        for (std::size_t i = 0; i < size; ++i) {
            const double x = static_cast<double>(i) / static_cast<double>(size);
            tab[i] += a * std::sin(w * x);
        }
    }
    const double top = peak(tab);
    Signal result(size + 1);
    for (std::size_t i = 0; i < size; ++i) {
        result[i] = tab[i] / top;
    }
    result[size] = tab[0] / top;
    return result;
}

std::pair<double, double> pan_gains(double pan) {
    const double th = (pan + 1) * pi / 4;
    const std::pair<double, double> result(std::cos(th), std::sin(th));
    return result;
}

LoopMix::LoopMix(std::size_t length, bool circular) : length_(length), circular_(circular) {}

Stereo& LoopMix::bus(const std::string& name) {
    for (std::pair<std::string, Stereo>& entry : buses_) {
        if (entry.first == name) {
            return entry.second;
        }
    }
    Stereo fresh{Signal(length_, 0.0), Signal(length_, 0.0)};
    buses_.emplace_back(name, std::move(fresh));
    return buses_.back().second;
}

void LoopMix::add(const std::string& name, std::span<const double> y, long long start, double pan,
                  double gain) {
    Stereo& b = bus(name);
    const std::pair<double, double> gains = pan_gains(pan);
    const double left = gains.first * gain;
    const double right = gains.second * gain;
    const long long length = static_cast<long long>(length_);
    const long long n = static_cast<long long>(y.size());
    if (!circular_) {
        const long long s = std::max(start, 0LL);
        const long long skipped = s - start;
        if (skipped >= n) {
            return;
        }
        const long long e = std::min(length, s + (n - skipped));
        for (long long i = s; i < e; ++i) {
            const double value = y[static_cast<std::size_t>(skipped + i - s)];
            b.left[static_cast<std::size_t>(i)] += left * value;
            b.right[static_cast<std::size_t>(i)] += right * value;
        }
        return;
    }
    if (length == 0) {
        return;
    }
    long long s = python_modulo(start, length);
    long long position = 0;
    while (position < n) {
        const long long k = std::min(n - position, length - s);
        for (long long i = 0; i < k; ++i) {
            const double value = y[static_cast<std::size_t>(position + i)];
            b.left[static_cast<std::size_t>(s + i)] += left * value;
            b.right[static_cast<std::size_t>(s + i)] += right * value;
        }
        position += k;
        s = 0;
    }
}

Stereo make_ir(double length, double rt_low, double rt_mid, double rt_high, double predelay,
               std::uint64_t seed) {
    const std::size_t n = samples(length);
    const Signal t = tarr(n);
    Generator r(seed);
    Stereo ir{};
    for (int ch = 0; ch < 2; ++ch) {
        Signal base(n);
        r.fill_standard_normal(base);
        const Signal lo = fft_filter(base, 0, 500);
        const Signal hi = fft_filter(base, 4000, 0);
        Signal y(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double mid = base[i] - lo[i] - hi[i];
            y[i] = lo[i] * std::exp(-6.9 * t[i] / rt_low) + mid * std::exp(-6.9 * t[i] / rt_mid) +
                   0.6 * hi[i] * std::exp(-6.9 * t[i] / rt_high);
            y[i] *= 1 - std::exp(-t[i] / 0.012);
        }
        const std::size_t d = std::min(samples(predelay), n);
        Signal delayed(n, 0.0);
        for (std::size_t i = d; i < n; ++i) {
            delayed[i] = y[i - d];
        }
        y = std::move(delayed);
        static constexpr double reflections[4][2] = {{0.011, 0.35}, {0.019, 0.28}, {0.027, 0.22}, {0.041, 0.16}};
        for (const double* reflection : reflections) {
            const std::size_t k = samples(reflection[0] + (ch != 0 ? 0.002 : 0));
            Signal squares(n);
            for (std::size_t i = 0; i < n; ++i) {
                squares[i] = square(y[i]);
            }
            y[k] += reflection[1] * std::sqrt(sum(squares)) / 30;
        }
        Signal squares(n);
        for (std::size_t i = 0; i < n; ++i) {
            squares[i] = square(y[i]);
        }
        const double norm = std::sqrt(sum(squares));
        Signal& out = ch == 0 ? ir.left : ir.right;
        out.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            out[i] = y[i] / norm;
        }
    }
    return ir;
}

namespace {

// The convolution of `source` with a much shorter `response`, by overlap-add of blocks
// through transforms of `size` (a power of two at least twice the response): circular
// over the source's length, or zero-padded to `out_length`. The same sums as one
// transform of the whole length, which for a long loop of awkward length means a large
// Bluestein transform (several hundred megabytes); these blocks need a few.
Signal block_convolve(std::span<const double> source, std::span<const double> response, std::size_t out_length,
                      bool circular, std::size_t size) {
    const std::size_t r = response.size();
    const std::size_t block = size - r + 1;
    std::vector<Complex> kernel{};
    rfft(response, size, kernel);
    Signal out(out_length, 0.0);
    std::vector<Complex> spectrum{};
    Signal piece{};
    for (std::size_t start = 0; start < source.size(); start += block) {
        const std::size_t count = std::min(block, source.size() - start);
        rfft(source.subspan(start, count), size, spectrum);
        for (std::size_t k = 0; k < spectrum.size(); ++k) {
            const Complex a = spectrum[k];
            const Complex b = kernel[k];
            spectrum[k] = Complex(a.real() * b.real() - a.imag() * b.imag(),
                                  a.real() * b.imag() + a.imag() * b.real());
        }
        irfft(spectrum, size, piece);
        const std::size_t produced = count + r - 1;
        for (std::size_t i = 0; i < produced; ++i) {
            std::size_t index = start + i;
            if (circular) {
                index %= out_length;
            } else if (index >= out_length) {
                break;
            }
            out[index] += piece[i];
        }
    }
    return out;
}

}  // namespace

Stereo conv_reverb(const Stereo& x, const Stereo& ir, bool circular) {
    const std::size_t length = x.left.size();
    const std::size_t n = circular ? length : length + ir.left.size();
    Stereo out{};
    std::size_t block_size = 1;
    while (block_size < 2 * ir.left.size()) {
        block_size *= 2;
    }
    if (!ir.left.empty() && ir.left.size() == ir.right.size() && length >= 4 * block_size) {
        out.left = block_convolve(x.left, ir.left, n, circular, block_size);
        out.right = block_convolve(x.right, ir.right, n, circular, block_size);
        return out;
    }
    for (int ch = 0; ch < 2; ++ch) {
        const Signal& source = ch == 0 ? x.left : x.right;
        const Signal& response = ch == 0 ? ir.left : ir.right;
        Signal h(n, 0.0);
        for (std::size_t i = 0; i < response.size() && i < n; ++i) {
            h[i] = response[i];
        }
        std::vector<Complex> spectrum{};
        std::vector<Complex> kernel{};
        rfft(source, n, spectrum);
        rfft(h, n, kernel);
        for (std::size_t k = 0; k < spectrum.size(); ++k) {
            const Complex a = spectrum[k];
            const Complex b = kernel[k];
            spectrum[k] = Complex(a.real() * b.real() - a.imag() * b.imag(),
                                  a.real() * b.imag() + a.imag() * b.real());
        }
        Signal& destination = ch == 0 ? out.left : out.right;
        irfft(spectrum, n, destination);
    }
    return out;
}

Stereo pingpong(const Stereo& x, long long delay_samples, double fb, int n, double lp) {
    const std::size_t length = x.left.size();
    Stereo out{Signal(length, 0.0), Signal(length, 0.0)};
    if (length == 0) {
        return out;
    }
    Signal mono(length);
    for (std::size_t i = 0; i < length; ++i) {
        mono[i] = (x.left[i] + x.right[i]) / 2;
    }
    double g = 1.0;
    const long long size = static_cast<long long>(length);
    for (int k = 1; k <= n; ++k) {
        g *= fb;
        Signal& channel = (k + 1) % 2 == 0 ? out.left : out.right;
        const long long shift = python_modulo(static_cast<long long>(k) * delay_samples, size);
        for (long long i = 0; i < size; ++i) {
            const long long from = python_modulo(i - shift, size);
            channel[static_cast<std::size_t>(i)] += g * mono[static_cast<std::size_t>(from)];
        }
    }
    out.left = fft_filter(out.left, 250, lp, 2, 4096, true);
    out.right = fft_filter(out.right, 250, lp, 2, 4096, true);
    return out;
}

Stereo autopan(const Stereo& x, double cycles, double depth) {
    const std::size_t length = x.left.size();
    Stereo out = x;
    const double w = two_pi * cycles;
    for (std::size_t i = 0; i < length; ++i) {
        const double lfo = std::sin(w * static_cast<double>(i) / static_cast<double>(length));
        out.left[i] *= 1 + depth * lfo;
        out.right[i] *= 1 - depth * lfo;
    }
    return out;
}

// ------------------------------------------------------------------ the module
bool Synth::NoteKeyLess::operator()(const NoteKey& a, const NoteKey& b) const {
    if (a.inst != b.inst) {
        return a.inst < b.inst;
    }
    if (a.m != b.m) {
        return a.m < b.m;
    }
    if (a.dur != b.dur) {
        return a.dur < b.dur;
    }
    return a.vel < b.vel;
}

bool Synth::DrumKeyLess::operator()(const DrumKey& a, const DrumKey& b) const {
    if (a.name != b.name) {
        return a.name < b.name;
    }
    if (a.vel != b.vel) {
        return a.vel < b.vel;
    }
    return a.rr < b.rr;
}

Synth::Synth() : rng_(12345) {}

Synth::Synth(const GeneratorState& rng_state) : rng_(rng_state) {}

Signal Synth::noise(std::size_t n) {
    Signal result(n);
    rng_.fill_standard_normal(result);
    return result;
}

Signal Synth::noise(std::size_t n, std::optional<std::uint64_t> seed) {
    if (!seed.has_value()) {
        return noise(n);
    }
    const Signal result = seeded_noise(n, *seed);
    return result;
}

const Signal& Synth::table(Wave kind, int harmonics) {
    const std::pair<int, int> key(static_cast<int>(kind), harmonics);
    std::map<std::pair<int, int>, Signal>::iterator found = tables_.find(key);
    if (found == tables_.end()) {
        found = tables_.emplace(key, wavetable(kind, harmonics)).first;
    }
    return (*found).second;
}

Signal Synth::wt(Wave kind, std::span<const double> freq, int maxpart, double phase0) {
    double fmax = freq.empty() ? 0 : freq[0];
    for (const double value : freq) {
        fmax = std::max(fmax, value);
    }
    // int(max(1, min(maxpart, 15000 / fmax)))
    const double limit = std::min(static_cast<double>(maxpart), 15000 / fmax);
    const int harmonics = static_cast<int>(std::max(1.0, limit));
    const Signal& tab = table(kind, harmonics);
    Signal y(freq.size());
    double running = 0.0;
    for (std::size_t i = 0; i < freq.size(); ++i) {
        running = i == 0 ? freq[0] : running + freq[i];
        const double phase = remainder(phase0 + running / sr, 1.0);
        y[i] = interpolate(tab, phase * 4096);
    }
    return y;
}

Signal Synth::ep(double m, double dur, double vel, double bright) {
    const double f = mtof(m);
    const double rel = 0.18;
    const std::size_t n = samples(dur + rel);
    const Signal t = tarr(n);
    const Signal g = gate(n, dur, 0.002, rel);
    const double decay = 1.6 * std::pow(261.6 / f, 0.35);
    const double depth = bright * (0.9 + 1.8 * vel);
    const double floor = 0.3 * bright;
    const double w = two_pi * f;
    const bool bell = f * 7 < 16000;
    const double bell_gain = 0.07 * vel * bright;
    const double w7 = two_pi * f * 7.0;
    const double level = 0.35 + 0.65 * vel;
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double dec = std::exp(-t[i] / decay);
        const double idx = depth * std::exp(-t[i] / 0.3) + floor;
        double v = std::sin(w * t[i] + idx * std::sin(w * t[i])) * dec;
        if (bell) {
            v += bell_gain * std::sin(w7 * t[i]) * std::exp(-t[i] / 0.05);
        }
        y[i] = v * g[i] * level;
    }
    return y;
}

Signal Synth::pluck(double m, double dur, double vel, double pos, double decay, double bright, bool body) {
    const double f = mtof(m);
    const double rel = 0.1;
    const std::size_t n = samples(dur + rel);
    const double t60 = decay * 2.6 * std::pow(196 / f, 0.45);
    const int count = static_cast<int>(std::min(40.0, 12000 / f));
    const std::size_t K = static_cast<std::size_t>(std::max(count, 0));
    Signal amps(K);
    Signal taus(K);
    Signal ratios(K);
    const double rolloff = 3600 * bright * (0.55 + vel);
    for (std::size_t i = 0; i < K; ++i) {
        const double k = static_cast<double>(i + 1);
        amps[i] = std::fabs(std::sin(pi * k * pos)) / std::pow(k, 1.05);
        amps[i] *= std::exp(-(k * f) / rolloff);
        taus[i] = (t60 / 6.9) / (1 + 0.00055 * k * f);
        const double k_squared = static_cast<double>((i + 1) * (i + 1));
        ratios[i] = k * std::sqrt(1 + 8e-6 * k_squared);
    }
    Signal ph(K);
    rng_.fill_uniform(-0.2, 0.2, ph);
    Signal y = partials(f, n, ratios, amps, taus, ph);
    if (body) {
        const std::size_t k = std::min(n, samples(0.03));
        const Signal raw = noise(k);
        Signal nz = fft_filter(raw, 700, 4500);
        const Signal tk = tarr(k);
        for (std::size_t i = 0; i < k; ++i) {
            nz[i] = nz[i] * std::exp(-tk[i] / 0.004);
        }
        const double top = peak(nz) + 1e-9;
        const double amps_max = K == 0 ? 0 : *std::max_element(amps.begin(), amps.end());
        const double scale = 0.25 * vel;
        for (std::size_t i = 0; i < k; ++i) {
            y[i] += scale * nz[i] / top * amps_max;
        }
    }
    const Signal g = gate(n, dur, 0.0008, rel);
    const double level = 0.3 + 0.7 * vel;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] *= g[i];
        y[i] = y[i] * level;
    }
    return y;
}

Signal Synth::marimba(double m, double dur, double vel) {
    static_cast<void>(dur);
    const double f = mtof(m);
    const std::size_t n = samples(std::min(2.2, 0.4 + 1.2 * std::pow(300 / f, 0.5)));
    const double t1 = 0.55 * std::pow(300 / f, 0.55);
    const double ratios[3] = {1, 3.93, 9.24};
    const double amps[3] = {1, 0.3 * (0.5 + vel), 0.08 * vel};
    const double taus[3] = {t1, 0.11, 0.03};
    Signal y = partials(f, n, ratios, amps, taus);
    const std::size_t k = samples(0.006);
    const Signal nz = fft_filter(noise(k), 300, 3000);
    const Signal ramp = linspace(1, 0, k);
    const double scale = 0.06 * vel;
    for (std::size_t i = 0; i < k && i < n; ++i) {
        y[i] += scale * nz[i] * ramp[i];
    }
    const Signal g = gate(n, 10, 0.0007);
    const double level = 0.3 + 0.7 * vel;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * g[i] * level;
    }
    return y;
}

Signal Synth::vibes(double m, double dur, double vel, double trem) {
    const double f = mtof(m);
    const double rel = 0.35;
    const double held = std::min(dur, 3.0);
    const std::size_t n = samples(held + rel);
    const Signal t = tarr(n);
    const double ratios[2] = {4.0, 10.0};
    const double amps[2] = {0.10 * (0.5 + vel), 0.025 * vel};
    const double taus[2] = {0.25, 0.06};
    const Signal upper = partials(f, n, ratios, amps, taus);
    const Signal g = gate(n, held, 0.001, rel);
    const double w = two_pi * f;
    const double wt_rate = two_pi * trem;
    const double level = 0.3 + 0.7 * vel;
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double fund = std::exp(-t[i] / 1.4) * std::sin(w * t[i]) * (1 + 0.22 * std::sin(wt_rate * t[i]));
        y[i] = (fund + upper[i]) * g[i] * level;
    }
    return y;
}

Signal Synth::celesta(double m, double dur, double vel) {
    static_cast<void>(dur);
    const double f = mtof(m);
    const std::size_t n = samples(1.8);
    const double ratios[5] = {1, 2, 3, 4.02, 6.9};
    const double amps[5] = {1, 0.12, 0.22 * vel, 0.06, 0.03 * vel};
    const double taus[5] = {0.75, 0.4, 0.12, 0.1, 0.03};
    Signal y = partials(f, n, ratios, amps, taus);
    const Signal g = gate(n, 10, 0.0006);
    const double level = 0.3 + 0.7 * vel;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * g[i] * level;
    }
    return y;
}

Signal Synth::harp(double m, double dur, double vel) {
    const Signal result = pluck(m, std::max(dur, 1.4), vel, 0.32, 1.25, 0.8, false);
    return result;
}

Signal Synth::pizz(double m, double dur, double vel) {
    const Signal result = pluck(m, std::min(dur, 0.35), vel, 0.22, 0.35, 0.7);
    return result;
}

Signal Synth::bass(double m, double dur, double vel) {
    const double f = mtof(m);
    const double rel = 0.07;
    const std::size_t n = samples(dur + rel);
    const Signal t = tarr(n);
    Signal fr(n);
    for (std::size_t i = 0; i < n; ++i) {
        fr[i] = f * (1 + 0.012 * std::exp(-t[i] / 0.015));
    }
    const Signal cs = cumulative_sum(fr);
    Signal ph(n);
    for (std::size_t i = 0; i < n; ++i) {
        ph[i] = two_pi * cs[i] / sr;
    }
    Signal y(n, 0.0);
    for (int k = 1; k < 9; ++k) {
        if (k * f > 4000) {
            break;
        }
        const double a = 1 / std::pow(static_cast<double>(k), 1.15) * (k == 1 ? 1 : 0.6 + 0.6 * vel);
        const double tau = 1.3 / std::pow(static_cast<double>(k), 0.9);
        for (std::size_t i = 0; i < n; ++i) {
            y[i] += a * std::exp(-t[i] / tau) * std::sin(k * ph[i]);
        }
    }
    const std::size_t kk = std::min(n, samples(0.02));
    const Signal nz = fft_filter(noise(kk), 80, 900);
    const Signal tk = tarr(kk);
    const double scale = 0.12 * vel;
    for (std::size_t i = 0; i < kk; ++i) {
        y[i] += scale * nz[i] * std::exp(-tk[i] / 0.006);
    }
    const Signal g = gate(n, dur, 0.004, rel);
    const double level = 0.4 + 0.6 * vel;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * g[i] * level;
    }
    return y;
}

Signal Synth::pad(double m, double dur, double vel, double att, double rel, double cutoff) {
    const double f = mtof(m);
    const std::size_t n = samples(dur + rel);
    const Signal t = tarr(n);
    Signal y(n, 0.0);
    const int cents[3] = {-7, 0, 6};
    const double w = two_pi * 0.3;
    for (const int c : cents) {
        const double base = f * std::pow(2.0, c / 1200.0);
        Signal fr(n);
        for (std::size_t i = 0; i < n; ++i) {
            fr[i] = base * (1 + 0.0015 * std::sin(w * t[i] + c));
        }
        const double phase0 = rng_.uniform(0.0, 1.0);  // _rng.uniform()
        const Signal voice = wt(Wave::saw, fr, 40, phase0);
        for (std::size_t i = 0; i < n; ++i) {
            y[i] += voice[i];
        }
    }
    y = fft_filter(y, 90, cutoff, 2);
    const Signal g = gate(n, dur, att, rel);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * g[i] * vel / 3;
    }
    return y;
}

Signal Synth::breathy(Wave kind, double m, double dur, double vel, double att, double rel, double vib_rate,
                      double vib_depth, double breath, int maxpart) {
    const double f = mtof(m);
    const std::size_t n = samples(dur + rel);
    const Signal t = tarr(n);
    const double phase = rng_.uniform(0, 6);
    const double w = two_pi * vib_rate;
    Signal vib(n);
    Signal fr(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double ramp = std::clamp((t[i] - 0.18) / 0.35, 0.0, 1.0);
        vib[i] = vib_depth * ramp * std::sin(w * t[i] + phase);
        const double scoop = -0.006 * std::exp(-t[i] / 0.04);
        fr[i] = f * (1 + vib[i] + scoop);
    }
    Signal y = wt(kind, fr, maxpart);
    const Signal g = gate(n, dur, att, rel);
    const double depth = std::max(vib_depth, 1e-9);
    Signal nz = fft_filter(noise(n), f * 0.9, f * 3.5, 1);
    for (std::size_t i = 0; i < n; ++i) {
        nz[i] = nz[i] * breath;
    }
    const double spread = standard_deviation(nz) + 1e-9;
    const double level = 0.35 + 0.65 * vel;
    for (std::size_t i = 0; i < n; ++i) {
        const double amp_env = g[i] * (1 + 0.5 * vib[i] / depth * 0.04);
        y[i] = y[i] + nz[i] / spread * breath;
        y[i] = y[i] * amp_env * level;
    }
    return y;
}

Signal Synth::flute(double m, double dur, double vel) {
    const Signal result = breathy(Wave::flute, m, dur, vel, 0.045, 0.12, 5.0, 0.0045, 0.035, 6);
    return result;
}

Signal Synth::clarinet(double m, double dur, double vel) {
    const Signal y = breathy(Wave::clar, m, dur, vel, 0.03, 0.09, 4.6, 0.003, 0.015, 12);
    const Signal result = fft_filter(y, 0, 3200, 1);
    return result;
}

Signal Synth::reed(double m, double dur, double vel) {
    const double f = mtof(m);
    const std::size_t n = samples(dur + 0.08);
    const Signal low(n, f);
    const Signal high(n, f * std::pow(2.0, 9 / 1200.0));
    const Signal a = wt(Wave::reed, low, 14);
    const Signal b = wt(Wave::reed, high, 14, 0.3);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = a[i] + b[i];
    }
    y = fft_filter(y, 120, 2600, 1);
    const Signal g = gate(n, dur, 0.025, 0.08);
    const double level = 0.35 + 0.65 * vel;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * 0.5 * g[i] * level;
    }
    return y;
}

Signal Synth::slide(double m, double dur, double vel) {
    const double f = mtof(m);
    const double rel = 0.08;
    const std::size_t n = samples(dur + rel);
    const Signal t = tarr(n);
    const double w = two_pi * 6.2;
    Signal fr(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double glide = std::pow(2.0, -3.0 / 12 * std::exp(-t[i] / 0.035));
        const double vib = 1 + 0.008 * std::clamp((t[i] - 0.15) / 0.2, 0.0, 1.0) * std::sin(w * t[i]);
        fr[i] = f * glide * vib;
    }
    Signal y = wt(Wave::flute, fr, 3);
    const Signal br = fft_filter(noise(n), f, f * 3, 1);
    const double spread = standard_deviation(br) + 1e-9;
    const Signal g = gate(n, dur, 0.02, rel);
    const double level = 0.35 + 0.65 * vel;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] += 0.04 * br[i] / spread;
        y[i] = y[i] * g[i] * level;
    }
    return y;
}

Signal Synth::tuba(double m, double dur, double vel) {
    const double f = mtof(m);
    const std::size_t n = samples(dur + 0.06);
    const Signal t = tarr(n);
    Signal fr(n);
    for (std::size_t i = 0; i < n; ++i) {
        fr[i] = f * (1 + 0.01 * std::exp(-t[i] / 0.03)) * 1.0;
    }
    Signal y = wt(Wave::reed, fr, 8);
    y = fft_filter(y, 30, 600 + 500 * vel, 1);
    const Signal g = gate(n, dur, 0.025, 0.06);
    const double level = 0.4 + 0.6 * vel;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * g[i] * level;
    }
    return y;
}

Signal Synth::felt_piano(double m, double dur, double vel) {
    const double f = mtof(m);
    const double rel = 0.25;
    const double held = std::min(dur, 4.0);
    const std::size_t n = samples(held + rel);
    const Signal t = tarr(n);
    const double B = 0.00015 * std::pow(f / 261.6, 0.5);
    Signal y(n, 0.0);
    const double detunes[2] = {-0.6, 0.6};
    for (const double det : detunes) {
        for (int k = 1; k < 16; ++k) {
            const double fk = k * f * std::sqrt(1 + B * k * k) * std::pow(2.0, det / 1200);
            if (fk > 9000) {
                break;
            }
            const double a = std::exp(-(k - 1) * (0.55 - 0.25 * vel)) / std::pow(static_cast<double>(k), 0.3);
            const double tau = 2.2 * std::pow(261.6 / f, 0.5) / (1 + 0.35 * k);
            const double w = two_pi * fk;
            for (std::size_t i = 0; i < n; ++i) {
                y[i] += a * std::exp(-t[i] / tau) * std::sin(w * t[i] + k);
            }
        }
    }
    const std::size_t k = std::min(n, samples(0.03));
    const Signal thump = fft_filter(seeded_noise(k, 3), 60, 600);
    const Signal tk = tarr(k);
    for (std::size_t i = 0; i < k; ++i) {
        y[i] += 0.5 * thump[i] * std::exp(-tk[i] / 0.006);
    }
    y = fft_filter(y, 40, 1800 + 2500 * vel, 1);
    const Signal g = gate(n, held, 0.004, rel);
    const double level = 0.3 + 0.7 * vel;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * g[i] * level * 0.25;
    }
    return y;
}

Signal Synth::glass(double m, double dur, double vel) {
    const double f = mtof(m);
    const double rel = 0.6;
    const std::size_t n = samples(dur + rel);
    const Signal t = tarr(n);
    const Signal g = gate(n, dur, 0.09, rel);
    const double w = two_pi * f;
    const double w_beat = two_pi * 1.3;
    const double w2 = two_pi * f * 2.0;
    const double w3 = two_pi * f * 3.01;
    const double level = 0.3 + 0.7 * vel;
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double v = std::sin(w * t[i]) * (1 + 0.15 * std::sin(w_beat * t[i])) +
                         0.18 * std::sin(w2 * t[i] + 1) + 0.06 * std::sin(w3 * t[i] + 2);
        y[i] = v * g[i] * level * 0.8;
    }
    return y;
}

Signal Synth::musicbox(double m, double dur, double vel) {
    static_cast<void>(dur);
    const double f = mtof(m);
    const std::size_t n = samples(1.6);
    const double ratios[4] = {1, 2.0, 5.4, 9.9};
    const double amps[4] = {1, 0.2, 0.18 * vel, 0.05};
    const double taus[4] = {0.9, 0.35, 0.08, 0.03};
    Signal y = partials(f, n, ratios, amps, taus);
    const Signal g = gate(n, 10, 0.0005);
    const double level = 0.3 + 0.7 * vel;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * g[i] * level;
    }
    return y;
}

Signal Synth::logdrum(double m, double dur, double vel) {
    static_cast<void>(dur);
    const double f = mtof(m);
    const std::size_t n = samples(0.9);
    const Signal t = tarr(n);
    Signal fr(n);
    for (std::size_t i = 0; i < n; ++i) {
        fr[i] = f * (1 + 0.04 * std::exp(-t[i] / 0.01));
    }
    const Signal cs = cumulative_sum(fr);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double ph = two_pi * cs[i] / sr;
        y[i] = std::exp(-t[i] / 0.22) * std::sin(ph) + 0.35 * std::exp(-t[i] / 0.07) * std::sin(2.03 * ph) +
               0.12 * std::exp(-t[i] / 0.03) * std::sin(3.9 * ph);
    }
    const std::size_t k = samples(0.008);
    const Signal nz = fft_filter(seeded_noise(k, 5), 200, 2500);
    const Signal ramp = linspace(1, 0, k);
    for (std::size_t i = 0; i < k && i < n; ++i) {
        y[i] += 0.4 * nz[i] * ramp[i];
    }
    const Signal g = gate(n, 10, 0.001);
    const double level = 0.3 + 0.7 * vel;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * g[i] * level;
    }
    return y;
}

Signal Synth::litho(double m, double dur, double vel) {
    static_cast<void>(dur);
    const double f = mtof(m);
    const std::size_t n = samples(0.7);
    const double ratios[4] = {1, 2.76, 5.40, 8.93};
    const double amps[4] = {1, 0.45, 0.25 * vel, 0.1 * vel};
    const double taus[4] = {0.18, 0.07, 0.03, 0.015};
    Signal y = partials(f, n, ratios, amps, taus);
    const std::size_t k = samples(0.003);
    const Signal nz = fft_filter(seeded_noise(k, 6), 1500, 9000);
    for (std::size_t i = 0; i < k && i < n; ++i) {
        y[i] += 0.8 * nz[i];
    }
    const Signal g = gate(n, 10, 0.0003);
    const double level = 0.3 + 0.7 * vel;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * g[i] * level;
    }
    return y;
}

Signal Synth::analog(double m, double dur, double vel, double bright) {
    const double f = mtof(m);
    const double rel = 0.12;
    const std::size_t n = samples(dur + rel);
    const Signal t = tarr(n);
    const Signal fr(n, f);
    const Signal detuned = scaled(fr, 1.004);
    const Signal a = wt(Wave::saw, fr, 40);
    const Signal b = wt(Wave::saw, detuned, 40, 0.37);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = a[i] + b[i];
    }
    const Signal dark = fft_filter(y, 40, f * 2.0, 2);
    const Signal br = fft_filter(y, 40, f * 6.0, 2);
    const double sweep = bright * vel;
    const Signal g = gate(n, dur, 0.006, rel);
    const double level = 0.35 + 0.65 * vel;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = dark[i] + sweep * (br[i] - dark[i]) * std::exp(-t[i] / 0.12);
        y[i] = y[i] * g[i] * level * 0.4;
    }
    return y;
}

Signal Synth::synthbass(double m, double dur, double vel) {
    const double f = mtof(m);
    const double rel = 0.06;
    const std::size_t n = samples(dur + rel);
    const Signal t = tarr(n);
    const Signal g = gate(n, dur, 0.004, rel);
    const double w = two_pi * f;
    const double level = 0.4 + 0.6 * vel;
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double ph = w * t[i];
        const double v = std::sin(ph) + 0.35 * std::sin(2 * ph) * std::exp(-t[i] / 0.15) +
                         0.12 * std::sin(3 * ph) * std::exp(-t[i] / 0.06);
        y[i] = v * g[i] * level;
    }
    return y;
}

Signal Synth::ping(double m, double dur, double vel) {
    static_cast<void>(dur);
    const double f = mtof(m);
    const std::size_t n = samples(0.8);
    const Signal t = tarr(n);
    Signal fr(n);
    for (std::size_t i = 0; i < n; ++i) {
        fr[i] = f * (1 + 0.02 * std::exp(-t[i] / 0.008));
    }
    const Signal cs = cumulative_sum(fr);
    const Signal g = gate(n, 10, 0.0008);
    const double w3 = two_pi * 3 * f;
    const double level = 0.3 + 0.7 * vel;
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double v = std::sin(two_pi * cs[i] / sr) * std::exp(-t[i] / 0.18) +
                         0.1 * std::sin(w3 * t[i]) * std::exp(-t[i] / 0.04);
        y[i] = v * g[i] * level;
    }
    return y;
}

Signal Synth::kalimba(double m, double dur, double vel) {
    static_cast<void>(dur);
    const double f = mtof(m);
    const std::size_t n = samples(1.3);
    const double ratios[3] = {1, 5.9, 13.2};
    const double amps[3] = {1, 0.12 * vel, 0.04};
    const double taus[3] = {0.5 * std::pow(440 / f, 0.3), 0.05, 0.02};
    Signal y = partials(f, n, ratios, amps, taus);
    const Signal g = gate(n, 10, 0.0008);
    const double level = 0.3 + 0.7 * vel;
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * g[i] * level;
    }
    return y;
}

// ------------------------------------------------------------------ percussion
Signal Synth::kick(double vel, std::optional<std::uint64_t> seed) {
    const std::size_t n = samples(0.35);
    const Signal t = tarr(n);
    Signal fr(n);
    for (std::size_t i = 0; i < n; ++i) {
        fr[i] = 48 + 62 * std::exp(-t[i] / 0.028);
    }
    const Signal cs = cumulative_sum(fr);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = std::sin(two_pi * cs[i] / sr) * std::exp(-t[i] / 0.16);
    }
    const std::size_t k = samples(0.004);
    const Signal click = fft_filter(noise(k, seed), 500, 3000);
    for (std::size_t i = 0; i < k; ++i) {
        y[i] += 0.2 * click[i];
    }
    const Signal g = gate(n, 10, 0.0015);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * g[i] * vel;
    }
    return y;
}

Signal Synth::brush_tap(double vel, std::optional<std::uint64_t> seed) {
    const std::size_t n = samples(0.18);
    const Signal t = tarr(n);
    Signal nz = fft_filter(noise(n, seed), 900, 6500);
    for (std::size_t i = 0; i < n; ++i) {
        nz[i] = nz[i] * std::exp(-t[i] / 0.045);
    }
    const double top = peak(nz);
    const Signal g = gate(n, 10, 0.0015);
    const double w = two_pi * 185;
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        nz[i] /= top;
        const double body = 0.35 * std::sin(w * t[i]) * std::exp(-t[i] / 0.035);
        y[i] = (nz[i] * 0.8 + body) * g[i] * vel;
    }
    return y;
}

Signal Synth::brush_swish(double length, double vel, std::optional<std::uint64_t> seed) {
    const std::size_t n = samples(length);
    const Signal x = linspace(0, 1, n);
    const Signal nz = fft_filter(noise(n, seed), 2500, 9000);
    const double top = peak(nz);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double s = std::sin(pi * std::pow(x[i], 0.6));
        const double env = s * s;
        y[i] = nz[i] / top * env * vel * 0.5;
    }
    return y;
}

Signal Synth::shaker(double vel, std::optional<std::uint64_t> seed) {
    const std::size_t n = samples(0.09);
    const Signal t = tarr(n);
    const Signal nz = fft_filter(noise(n, seed), 5000, 13000);
    const double top = peak(nz);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double env = (1 - std::exp(-t[i] / 0.008)) * std::exp(-t[i] / 0.022);
        y[i] = nz[i] / top * env * vel;
    }
    return y;
}

Signal Synth::hat(double vel, std::optional<std::uint64_t> seed) {
    const std::size_t n = samples(0.08);
    const Signal t = tarr(n);
    Signal nz = fft_filter(noise(n, seed), 7000, 15000);
    for (std::size_t i = 0; i < n; ++i) {
        nz[i] = nz[i] * std::exp(-t[i] / 0.018);
    }
    const double top = peak(nz);
    const Signal g = gate(n, 10, 0.0005);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = nz[i] / top * vel * g[i];
    }
    return y;
}

Signal Synth::rim(double vel, std::optional<std::uint64_t> seed) {
    const std::size_t n = samples(0.08);
    const double ratios[2] = {1720, 2480};
    const double amps[2] = {1, 0.5};
    const double taus[2] = {0.012, 0.008};
    Signal y = partials(1, n, ratios, amps, taus);
    const std::size_t k = samples(0.002);
    const Signal click = fft_filter(noise(k, seed), 2000, 9000);
    for (std::size_t i = 0; i < k; ++i) {
        y[i] += 0.6 * click[i];
    }
    const Signal g = gate(n, 10, 0.0003);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * g[i] * vel;
    }
    return y;
}

Signal Synth::clave(double vel) {
    const std::size_t n = samples(0.12);
    const double ratios[2] = {2450, 6100};
    const double amps[2] = {1, 0.08};
    const double taus[2] = {0.03, 0.01};
    Signal y = partials(1, n, ratios, amps, taus);
    const Signal g = gate(n, 10, 0.0003);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * g[i] * vel;
    }
    return y;
}

Signal Synth::woodblock(double vel, bool high) {
    const std::size_t n = samples(0.12);
    const double b = high ? 1180 : 820;
    const double ratios[2] = {b, b * 2.6};
    const double amps[2] = {1, 0.25};
    const double taus[2] = {0.025, 0.012};
    Signal y = partials(1, n, ratios, amps, taus);
    const Signal g = gate(n, 10, 0.0004);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * g[i] * vel;
    }
    return y;
}

Signal Synth::triangle(double vel) {
    const std::size_t n = samples(1.5);
    const double ratios[4] = {3950, 4720, 6410, 9100};
    const double amps[4] = {1, 0.5, 0.35, 0.2};
    const double taus[4] = {0.6, 0.5, 0.35, 0.25};
    Signal y = partials(1, n, ratios, amps, taus);
    const Signal g = gate(n, 10, 0.0004);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * g[i] * vel;
    }
    return y;
}

Signal Synth::clap(double vel, std::optional<std::uint64_t> seed) {
    const std::size_t n = samples(0.2);
    const Signal t = tarr(n);
    Signal e(n, 0.0);
    const double delays[3] = {0, 0.009, 0.017};
    for (const double d : delays) {
        const double tau = d < 0.017 ? 0.006 : 0.06;
        for (std::size_t i = 0; i < n; ++i) {
            const double burst = std::exp(-(t[i] - d) / tau);
            e[i] += t[i] >= d ? burst : 0;
        }
    }
    const Signal nz = fft_filter(noise(n, seed), 900, 5000);
    const double top = peak(nz);
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = nz[i] / top * e[i] * vel * 0.6;
    }
    return y;
}

Signal Synth::stone_click(double vel, std::uint64_t seed) {
    const std::size_t n = samples(0.08);
    Generator r(seed);
    const double ratios[3] = {2100 * r.uniform(.9, 1.1), 3700, 5200};
    const double amps[3] = {1, .6, .3};
    const double taus[3] = {0.012, 0.007, 0.004};
    Signal y = partials(1, n, ratios, amps, taus);
    const Signal g = gate(n, 10, 0.0002);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * g[i] * vel;
    }
    return y;
}

Signal Synth::rainstick(double vel, std::uint64_t seed) {
    const std::size_t n = samples(1.2);
    Generator r(seed);
    Signal y(n, 0.0);
    const double amps[1] = {1};
    const double taus[1] = {0.002};
    for (int grain = 0; grain < 140; ++grain) {
        const std::size_t s = static_cast<std::size_t>(r.uniform(0, static_cast<double>(n - 400)));
        const double fraction = -static_cast<double>(s) / static_cast<double>(n) * 2;
        const double g = r.uniform(.2, 1) * std::exp(fraction);
        const double ratios[1] = {r.uniform(3000, 8000)};
        const Signal ring = partials(1, 300, ratios, amps, taus);
        const Signal grain_signal = scaled(ring, g);
        add_into(y, s, grain_signal);
    }
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * vel * 0.5;
    }
    return y;
}

Signal Synth::tick(double vel) {
    const std::size_t n = samples(0.04);
    const double ratios[2] = {3200, 5100};
    const double amps[2] = {1, .4};
    const double taus[2] = {0.006, 0.003};
    Signal y = partials(1, n, ratios, amps, taus);
    const Signal g = gate(n, 10, 0.0002);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = y[i] * g[i] * vel;
    }
    return y;
}

// ------------------------------------------------------------------ dispatch and caches
namespace {
constexpr std::string_view instrument_names[] = {"slide", "tuba", "ep", "pluck", "marimba", "vibes",
                                                 "celesta", "harp", "pizz", "bass", "pad", "flute",
                                                 "clarinet", "reed", "felt", "glass", "musicbox", "logdrum",
                                                 "litho", "analog", "synthbass", "ping", "kalimba"};
constexpr std::string_view drum_names[] = {"kick", "tap", "shaker", "hat", "rim", "clave", "wbhi", "wblo",
                                           "triangle", "clap", "stone", "rainstick", "tick", "logkick",
                                           "loghi"};
}  // namespace

bool Synth::has_instrument(std::string_view name) {
    for (const std::string_view known : instrument_names) {
        if (known == name) {
            return true;
        }
    }
    return false;
}

bool Synth::has_drum(std::string_view name) {
    for (const std::string_view known : drum_names) {
        if (known == name) {
            return true;
        }
    }
    return false;
}

Signal Synth::instrument(std::string_view name, double m, double dur, double vel) {
    if (name == "slide") {
        return slide(m, dur, vel);
    }
    if (name == "tuba") {
        return tuba(m, dur, vel);
    }
    if (name == "ep") {
        return ep(m, dur, vel);
    }
    if (name == "pluck") {
        return pluck(m, dur, vel);
    }
    if (name == "marimba") {
        return marimba(m, dur, vel);
    }
    if (name == "vibes") {
        return vibes(m, dur, vel);
    }
    if (name == "celesta") {
        return celesta(m, dur, vel);
    }
    if (name == "harp") {
        return harp(m, dur, vel);
    }
    if (name == "pizz") {
        return pizz(m, dur, vel);
    }
    if (name == "bass") {
        return bass(m, dur, vel);
    }
    if (name == "pad") {
        return pad(m, dur, vel);
    }
    if (name == "flute") {
        return flute(m, dur, vel);
    }
    if (name == "clarinet") {
        return clarinet(m, dur, vel);
    }
    if (name == "reed") {
        return reed(m, dur, vel);
    }
    if (name == "felt") {
        return felt_piano(m, dur, vel);
    }
    if (name == "glass") {
        return glass(m, dur, vel);
    }
    if (name == "musicbox") {
        return musicbox(m, dur, vel);
    }
    if (name == "logdrum") {
        return logdrum(m, dur, vel);
    }
    if (name == "litho") {
        return litho(m, dur, vel);
    }
    if (name == "analog") {
        return analog(m, dur, vel);
    }
    if (name == "synthbass") {
        return synthbass(m, dur, vel);
    }
    if (name == "ping") {
        return ping(m, dur, vel);
    }
    if (name == "kalimba") {
        return kalimba(m, dur, vel);
    }
    return Signal{};
}

Signal Synth::drum(std::string_view name, double vel, std::optional<std::uint64_t> seed) {
    if (name == "kick") {
        return kick(vel, seed);
    }
    if (name == "tap") {
        return brush_tap(vel, seed);
    }
    if (name == "shaker") {
        return shaker(vel, seed);
    }
    if (name == "hat") {
        return hat(vel, seed);
    }
    if (name == "rim") {
        return rim(vel, seed);
    }
    if (name == "clave") {
        return clave(vel);
    }
    if (name == "wbhi") {
        return woodblock(vel, true);
    }
    if (name == "wblo") {
        return woodblock(vel, false);
    }
    if (name == "triangle") {
        return triangle(vel);
    }
    if (name == "clap") {
        return clap(vel, seed);
    }
    if (name == "stone" && seed.has_value()) {
        return stone_click(vel, *seed);
    }
    if (name == "rainstick" && seed.has_value()) {
        return rainstick(vel, *seed);
    }
    if (name == "tick") {
        return tick(vel);
    }
    if (name == "logkick") {
        const Signal result = scaled(logdrum(38, 0.5, vel), 0.9);
        return result;
    }
    if (name == "loghi") {
        const Signal result = scaled(logdrum(50, 0.3, vel), 0.7);
        return result;
    }
    return Signal{};
}

const Signal& Synth::render_note(std::string_view inst, double m, double dur, double vel) {
    const double vq = python_round(vel * 20) / 20;
    const double dq = python_round(dur * 100) / 100;
    NoteKey key{std::string(inst), m, dq, vq};
    std::map<NoteKey, Signal, NoteKeyLess>::iterator found = notes_.find(key);
    if (found == notes_.end()) {
        Signal rendered = instrument(inst, m, dq, vq);
        found = notes_.emplace(std::move(key), std::move(rendered)).first;
    }
    return (*found).second;
}

const Signal& Synth::render_drum(std::string_view name, double vel, long long rr) {
    const double vq = python_round(vel * 20) / 20;
    const long long round_robin = python_modulo(rr, 4);
    DrumKey key{std::string(name), vq, round_robin};
    std::map<DrumKey, Signal, DrumKeyLess>::iterator found = drums_.find(key);
    if (found == drums_.end()) {
        long long letters = 0;
        for (const char c : name) {
            letters += static_cast<unsigned char>(c);
        }
        const std::uint64_t seed = static_cast<std::uint64_t>(1000 + round_robin + letters % 97);
        Signal rendered = drum(name, vq, seed);
        found = drums_.emplace(std::move(key), std::move(rendered)).first;
    }
    return (*found).second;
}

void Synth::prime_note(std::string_view inst, double m, double dur, double vel, const GeneratorState& state) {
    const double vq = python_round(vel * 20) / 20;
    const double dq = python_round(dur * 100) / 100;
    Synth earlier(state);
    NoteKey key{std::string(inst), m, dq, vq};
    notes_[key] = earlier.instrument(inst, m, dq, vq);
}

// ------------------------------------------------------------------ cue scripts
Signal master(std::span<const double> x, double peak_db, double fade_seconds, std::size_t fade_in) {
    const double top = peak(x) + 1e-12;
    const double gain = std::pow(10.0, peak_db / 20);
    Signal y(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        y[i] = x[i] / top * gain;
    }
    const std::size_t fade = std::min(y.size(), samples(fade_seconds));
    const Signal down = linspace(1, 0, fade);
    for (std::size_t i = 0; i < fade; ++i) {
        y[y.size() - fade + i] *= down[i];
    }
    const std::size_t rise = std::min(fade_in, y.size());
    const Signal up = linspace(0, 1, fade_in);
    for (std::size_t i = 0; i < rise; ++i) {
        y[i] *= up[i];
    }
    return y;
}

Signal resonator(std::span<const double> x, double f, double bw) {
    const double r = std::exp(-pi * bw / sr);
    const double c = 2 * r * std::cos(two_pi * f / sr);
    const double g = 1 - r;
    Signal y(x.size());
    double y1 = 0.0;
    double y2 = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double y0 = g * x[i] + c * y1 - r * r * y2;
        y[i] = y0;
        y2 = y1;
        y1 = y0;
    }
    return y;
}

std::vector<std::int16_t> pcm16(std::span<const double> x) {
    std::vector<std::int16_t> result(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        result[i] = static_cast<std::int16_t>(static_cast<int>(x[i] * 32767));
    }
    return result;
}

}  // namespace voice
