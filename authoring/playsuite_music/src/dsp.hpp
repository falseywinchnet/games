// PlaySuite music synthesizer: DSP primitives.
// Self-contained C++20, no third-party dependencies. 48 kHz, double-precision state.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ps {

constexpr double SR = 48000.0;
constexpr double PI = 3.14159265358979323846;
constexpr double TAU = 2.0 * PI;

inline double mtof(double midi) {
    return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
}
inline double db(double decibels) {
    return std::pow(10.0, decibels / 20.0);
}
inline double todb(double gain) {
    return 20.0 * std::log10(std::max(gain, 1e-12));
}
inline double clamp(double v, double lo, double hi) {
    return std::min(std::max(v, lo), hi);
}
inline double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}
// Removes denormals from feedback paths.
inline double undenorm(double v) {
    return std::fabs(v) < 1e-25 ? 0.0 : v;
}
inline double softclip(double x) {
    return std::tanh(x);
}

struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed = 0x9E3779B97F4A7C15ull) : s(seed ? seed : 1) {}
    std::uint64_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
    double uni() {
        return (next() >> 11) * (1.0 / 9007199254740992.0);
    }
    double bip() {
        return uni() * 2.0 - 1.0;
    }
    double range(double a, double b) {
        return a + (b - a) * uni();
    }
    double gauss() {
        double u = std::max(uni(), 1e-12), v = uni();
        return std::sqrt(-2.0 * std::log(u)) * std::cos(TAU * v);
    }
};

// polyBLEP residual for band-limited discontinuities.
inline double blep(double t, double dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

struct Osc {
    double ph = 0.0;
    double saw(double inc) {
        double v = 2.0 * ph - 1.0 - blep(ph, inc);
        ph += inc;
        if (ph >= 1.0)
            ph -= 1.0;
        return v;
    }
    double pulse(double inc, double pw) {
        double v = ph < pw ? 1.0 : -1.0;
        v += blep(ph, inc);
        double t2 = ph - pw;
        if (t2 < 0.0)
            t2 += 1.0;
        v -= blep(t2, inc);
        ph += inc;
        if (ph >= 1.0)
            ph -= 1.0;
        return v - (2.0 * pw - 1.0); // remove DC of asymmetric pulse
    }
    double tri(double inc) {
        double v = ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph;
        ph += inc;
        if (ph >= 1.0)
            ph -= 1.0;
        return v;
    }
    double sine(double inc) {
        double v = std::sin(TAU * ph);
        ph += inc;
        if (ph >= 1.0)
            ph -= 1.0;
        return v;
    }
};

// Recursive sinusoid (cheap partials for additive instruments).
struct Phasor {
    double c = 1.0, s = 0.0, wc = 1.0, ws = 0.0;
    void set(double freq, double phase) {
        c = std::cos(phase);
        s = std::sin(phase);
        wc = std::cos(TAU * freq / SR);
        ws = std::sin(TAU * freq / SR);
    }
    double tick() {
        double nc = c * wc - s * ws;
        double ns = c * ws + s * wc;
        c = nc;
        s = ns;
        return s;
    }
    void renorm() {
        double m = 1.0 / std::sqrt(c * c + s * s);
        c *= m;
        s *= m;
    }
};

// Topology-preserving state-variable filter (Simper). Stable under fast modulation.
struct SVF {
    double ic1 = 0.0, ic2 = 0.0, g = 0.0, k = 1.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
    double lp = 0.0, bp = 0.0, hp = 0.0;
    void set(double cutoff, double q) {
        cutoff = clamp(cutoff, 10.0, SR * 0.47);
        g = std::tan(PI * cutoff / SR);
        k = 1.0 / std::max(q, 0.05);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    double tick(double x) {
        double v3 = x - ic2;
        double v1 = a1 * ic1 + a2 * v3;
        double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = undenorm(2.0 * v1 - ic1);
        ic2 = undenorm(2.0 * v2 - ic2);
        lp = v2;
        bp = v1;
        hp = x - k * v1 - v2;
        return lp;
    }
};

// One-pole TPT lowpass / highpass.
struct OnePole {
    double s = 0.0, g = 0.0;
    void set(double cutoff) {
        double w = std::tan(PI * clamp(cutoff, 5.0, SR * 0.47) / SR);
        g = w / (1.0 + w);
    }
    double lp(double x) {
        double v = (x - s) * g;
        double y = v + s;
        s = undenorm(y + v);
        return y;
    }
    double hp(double x) {
        return x - lp(x);
    }
};

// Four-pole ladder lowpass with saturating feedback (Moog-like character).
struct Ladder {
    double s[4] = {0, 0, 0, 0}, g = 0.0, res = 0.0, last = 0.0;
    void set(double cutoff, double resonance) {
        double w = std::tan(PI * clamp(cutoff, 15.0, 18000.0) / SR);
        g = w / (1.0 + w);
        res = resonance;
    }
    double tick(double x) {
        double in = std::tanh(x - res * 4.0 * last);
        for (int i = 0; i < 4; ++i) {
            double v = (in - s[i]) * g;
            double y = v + s[i];
            s[i] = undenorm(y + v);
            in = y;
        }
        last = in;
        return in * (1.0 + res * 1.5);
    }
};

// Biquad (RBJ) used for analysis, EQ and loudness weighting.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double tick(double x) {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() {
        z1 = z2 = 0;
    }
    static Biquad make(int type, double f, double q, double gain_db = 0.0) {
        Biquad b;
        double A = std::pow(10.0, gain_db / 40.0);
        double w = TAU * f / SR;
        double cw = std::cos(w), sw = std::sin(w), al = sw / (2.0 * q);
        double B0, B1, B2, A0, A1, A2;
        switch (type) {
        case 0:
            B0 = (1 - cw) / 2;
            B1 = 1 - cw;
            B2 = (1 - cw) / 2;
            A0 = 1 + al;
            A1 = -2 * cw;
            A2 = 1 - al;
            break; // LP
        case 1:
            B0 = (1 + cw) / 2;
            B1 = -(1 + cw);
            B2 = (1 + cw) / 2;
            A0 = 1 + al;
            A1 = -2 * cw;
            A2 = 1 - al;
            break; // HP
        case 2:
            B0 = al;
            B1 = 0;
            B2 = -al;
            A0 = 1 + al;
            A1 = -2 * cw;
            A2 = 1 - al;
            break; // BP (0 dB peak)
        case 3:
            B0 = 1 + al * A;
            B1 = -2 * cw;
            B2 = 1 - al * A;
            A0 = 1 + al / A;
            A1 = -2 * cw;
            A2 = 1 - al / A;
            break; // peak
        case 4: {
            double sq = 2 * std::sqrt(A) * al; // low shelf
            B0 = A * ((A + 1) - (A - 1) * cw + sq);
            B1 = 2 * A * ((A - 1) - (A + 1) * cw);
            B2 = A * ((A + 1) - (A - 1) * cw - sq);
            A0 = (A + 1) + (A - 1) * cw + sq;
            A1 = -2 * ((A - 1) + (A + 1) * cw);
            A2 = (A + 1) + (A - 1) * cw - sq;
            break;
        }
        default: {
            double sq = 2 * std::sqrt(A) * al; // high shelf
            B0 = A * ((A + 1) + (A - 1) * cw + sq);
            B1 = -2 * A * ((A - 1) + (A + 1) * cw);
            B2 = A * ((A + 1) + (A - 1) * cw - sq);
            A0 = (A + 1) - (A - 1) * cw + sq;
            A1 = 2 * ((A - 1) - (A + 1) * cw);
            A2 = (A + 1) - (A - 1) * cw - sq;
            break;
        }
        }
        b.b0 = B0 / A0;
        b.b1 = B1 / A0;
        b.b2 = B2 / A0;
        b.a1 = A1 / A0;
        b.a2 = A2 / A0;
        return b;
    }
};

// ADSR with linear attack and exponential decay/release; click-free by construction.
struct ADSR {
    double a = 0.005, d = 0.2, s = 0.7, r = 0.2;
    double level = 0.0;
    int stage = 0;
    double dc = 0.0, rc = 0.0, ainc = 0.0;
    void start(double attack, double decay, double sustain, double release) {
        a = std::max(attack, 0.0008);
        d = std::max(decay, 0.001);
        s = sustain;
        r = std::max(release, 0.003);
        ainc = 1.0 / (a * SR);
        dc = std::exp(-1.0 / (d * SR / 4.6));
        rc = std::exp(-1.0 / (r * SR / 6.9));
        stage = 1; // attack resumes from the current level (click-free retrigger)
    }
    void release() {
        if (stage && stage < 4)
            stage = 4;
    }
    double tick() {
        switch (stage) {
        case 1:
            level += ainc;
            if (level >= 1.0) {
                level = 1.0;
                stage = 2;
            }
            break;
        case 2:
            level = s + (level - s) * dc;
            break;
        case 4:
            level *= rc;
            if (level < 1e-6) {
                level = 0.0;
                stage = 5;
            }
            break;
        default:
            break;
        }
        return level;
    }
    bool done() const {
        return stage == 5;
    }
};

// Equal-power pan, pan in [-1, 1].
inline void panGains(double pan, double& l, double& r) {
    // Centre is unity on each channel; hard pan gives +3 dB on one side (constant power).
    double a = (clamp(pan, -1.0, 1.0) + 1.0) * 0.25 * PI;
    l = std::cos(a) * 1.41421356;
    r = std::sin(a) * 1.41421356;
}

struct Stereo {
    std::vector<float> L, R;
    Stereo() = default;
    explicit Stereo(std::size_t n) : L(n, 0.0f), R(n, 0.0f) {}
    std::size_t size() const {
        return L.size();
    }
    void resize(std::size_t n) {
        L.assign(n, 0.0f);
        R.assign(n, 0.0f);
    }
    void add(std::size_t i, double l, double r) {
        if (i < L.size()) {
            L[i] += float(l);
            R[i] += float(r);
        }
    }
};

// Fractional delay line with linear interpolation.
struct Delay {
    std::vector<double> buf;
    std::size_t w = 0;
    void init(std::size_t n) {
        buf.assign(n + 4, 0.0);
        w = 0;
    }
    void push(double x) {
        buf[w] = x;
        if (++w >= buf.size())
            w = 0;
    }
    double at(double d) const { // d samples ago (>=1)
        double pos = double(w) - d;
        while (pos < 0)
            pos += double(buf.size());
        std::size_t i0 = std::size_t(pos);
        double f = pos - double(i0);
        std::size_t i1 = (i0 + 1) % buf.size();
        return buf[i0 % buf.size()] * (1.0 - f) + buf[i1] * f;
    }
    double tap(std::size_t d) const {
        std::size_t p = (w + buf.size() - d) % buf.size();
        return buf[p];
    }
};

} // namespace ps
