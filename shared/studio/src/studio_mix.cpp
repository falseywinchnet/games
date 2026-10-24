#include "studio_mix.hpp"
#include "studio_voice.hpp"
#include <algorithm>
#include <cstdio>
#include <deque>
#include <chrono>
#include <stdexcept>

namespace studio {

namespace {

struct Ev {
    long s0;
    long len;
    double pitch, vel, pan;
    bool glide;
};

std::vector<Ev> schedule(const Song& song, const Track& tr, Rng& rng) {
    std::vector<Ev> evs;
    double totalBeats = double(song.bars) * song.bpb;
    for (const Note& n : tr.notes) {
        double b = n.beat;
        if (b < 0 && song.loop)
            b += totalBeats; // pickups wrap to the loop end
        double pos = b * double(song.spb);
        double frac = b - std::floor(b + 1e-9);
        if (!n.exact) {
            if (song.swing > 0 && std::fabs(frac - 0.5) < 1e-6)
                pos += song.swing * 0.5 * song.spb;
            if (song.swing16 > 0 &&
                (std::fabs(frac - 0.25) < 1e-6 || std::fabs(frac - 0.75) < 1e-6))
                pos += song.swing16 * 0.25 * song.spb;
            if (tr.human_ms > 0)
                pos += clamp(rng.gauss(), -2.5, 2.5) * tr.human_ms * 0.001 * SR;
        }
        double vel = n.vel;
        if (!n.exact && tr.human_vel > 0)
            vel *= 1.0 + clamp(rng.gauss(), -2.5, 2.5) * tr.human_vel;
        Ev e;
        e.s0 = std::max(0L, long(std::lround(pos)));
        e.len = std::max(1L, long(std::lround(n.dur * song.spb)));
        e.pitch = n.pitch;
        e.vel = clamp(vel, 0.03, 1.0);
        e.pan = n.pan;
        e.glide = n.glide;
        evs.push_back(e);
    }
    struct EarlierEvent {
        bool operator()(const Ev& a, const Ev& b) const {
            return a.s0 < b.s0;
        }
    };
    std::stable_sort(evs.begin(), evs.end(), EarlierEvent{});
    return evs;
}

void renderTrack(const Song& song, const Track& tr, Stereo& out, Rng& rng,
                 std::vector<long>* triggers) {
    std::vector<Ev> evs = schedule(song, tr, rng);
    if (triggers)
        for (const Ev& e : evs)
            (*triggers).push_back(e.s0);
    const long N = long(out.size());
    if (isMono(tr.p.inst)) {
        if (evs.empty())
            return;
        Voice v(tr.p, rng);
        long pos = evs[0].s0, offAt = -1, prevEnd = -1000000;
        double pl, pr;
        panGains(tr.pan, pl, pr);
        struct StepVoice {
            long& pos;
            const long& offAt;
            Voice& v;
            long N;
            Stereo& out;
            double pl, pr;
            void operator()() const {
                if (pos == offAt)
                    v.noteOff();
                double l, r;
                v.tick(l, r);
                if (pos >= 0 && pos < N) {
                    out.L[pos] += float(l * pl);
                    out.R[pos] += float(r * pr);
                }
                ++pos;
            }
        };
        StepVoice step{pos, offAt, v, N, out, pl, pr};
        bool started = false;
        for (const Ev& e : evs) {
            while (pos < e.s0)
                step();
            bool legato = started && (e.glide || prevEnd >= e.s0 - 48);
            if (!started) {
                v.noteOn(e.pitch, e.vel, e.len / SR);
                started = true;
            } else if (legato) {
                v.setGlide(e.glide ? tr.p.glide * 3.0 : tr.p.glide);
                v.glideTo(e.pitch, e.vel, e.len / SR);
            } else {
                v.setGlide(tr.p.glide);
                v.retrigger(e.pitch, e.vel, e.len / SR);
            }
            offAt = e.s0 + e.len;
            prevEnd = offAt;
        }
        long stop = offAt + long(v.maxTail() * SR);
        while (pos < stop && pos < N) {
            step();
            if (pos > offAt && v.finished())
                break;
        }
        return;
    }
    for (const Ev& e : evs) {
        Voice v(tr.p, rng);
        v.noteOn(e.pitch, e.vel, e.len / SR);
        double pl, pr;
        panGains(clamp(tr.pan + e.pan, -1.0, 1.0), pl, pr);
        if (tr.bed) {
            double l, r;
            for (long k = 0; k < long(3.5 * SR); ++k)
                v.tick(l, r);
        }
        long maxlen = e.len + long(v.maxTail() * SR);
        for (long i = 0; i < maxlen; ++i) {
            if (i == e.len)
                v.noteOff();
            double l, r;
            v.tick(l, r);
            long idx = e.s0 + i;
            if (idx >= N)
                break;
            out.L[idx] += float(l * pl);
            out.R[idx] += float(r * pr);
            if (i > e.len && v.finished())
                break;
        }
    }
}

struct Allpass {
    std::vector<double> b;
    std::size_t i = 0;
    double g = 0.6;
    void init(std::size_t n, double gain) {
        b.assign(n, 0.0);
        i = 0;
        g = gain;
    }
    double tick(double x) {
        double d = b[i];
        double y = -g * x + d;
        b[i] = undenorm(x + g * y);
        if (++i >= b.size())
            i = 0;
        return y;
    }
};

struct Reverb {
    std::vector<double> line[8];
    std::size_t idx[8]{};
    double g[8]{};
    OnePole damp[8];
    Allpass apL[4], apR[4];
    Delay pre;
    std::size_t preN = 1;
    Biquad hpL, hpR, lpL, lpR;
    void init(double size, double rt60, double dampAmt, double predelay) {
        static const double ms[8] = {31.7, 37.3, 41.9, 46.1, 53.9, 59.3, 67.7, 73.1};
        for (int k = 0; k < 8; ++k) {
            std::size_t n = std::size_t(ms[k] * size * SR / 1000.0) | 1;
            line[k].assign(n, 0.0);
            idx[k] = 0;
            g[k] = std::pow(10.0, -3.0 * double(n) / (rt60 * SR));
            damp[k].set(lerp(11000, 2500, dampAmt));
        }
        static const int apn[4] = {142, 107, 379, 277};
        for (int k = 0; k < 4; ++k) {
            apL[k].init(std::size_t(apn[k] * (0.6 + 0.6 * size)), 0.62);
            apR[k].init(std::size_t(apn[k] * (0.6 + 0.6 * size) * 1.13) | 1, 0.62);
        }
        preN = std::max<std::size_t>(1, std::size_t(predelay * SR));
        pre.init(preN * 2 + 8);
        hpL = Biquad::make(1, 180, 0.7);
        hpR = hpL;
        lpL = Biquad::make(0, 9000, 0.7);
        lpR = lpL;
    }
    // One sample of the original whole-buffer process(): `in` is the send bus sample, and
    // the return values are what process() added to its zeroed output buffer.
    Delay preR;
    void start() {
        preR.init(preN * 2 + 8);
    }
    void step(float inL, float inR, float& outL, float& outR) {
        double xl = hpL.tick(inL), xr = hpR.tick(inR);
        pre.push(xl);
        preR.push(xr);
        xl = pre.tap(preN);
        xr = preR.tap(preN);
        for (int k = 0; k < 4; ++k) {
            xl = apL[k].tick(xl);
            xr = apR[k].tick(xr);
        }
        double y[8];
        for (int k = 0; k < 8; ++k)
            y[k] = line[k][idx[k]];
        // Hadamard mix
        double h[8];
        for (int k = 0; k < 8; ++k)
            h[k] = y[k];
        for (int len = 1; len < 8; len <<= 1)
            for (int s = 0; s < 8; s += len << 1)
                for (int j = s; j < s + len; ++j) {
                    double a = h[j], b = h[j + len];
                    h[j] = a + b;
                    h[j + len] = a - b;
                }
        for (int k = 0; k < 8; ++k) {
            double fb = h[k] * 0.35355339 * g[k];
            double inj = (k < 4 ? xl : xr) * 0.5;
            line[k][idx[k]] = undenorm(damp[k].lp(fb + inj));
            if (++idx[k] >= line[k].size())
                idx[k] = 0;
        }
        double ol = (y[0] - y[1] + y[2] - y[3] + y[4] - y[5] + y[6] - y[7]) * 0.3;
        double orr = (y[0] + y[1] - y[2] - y[3] + y[4] + y[5] - y[6] - y[7]) * 0.3;
        outL = 0.0f + float(lpL.tick(ol));
        outR = 0.0f + float(lpR.tick(orr));
    }
};

// The ping-pong delay, one sample at a time (the original ran it over the whole send bus).
struct PingPong {
    Delay dl, dr;
    OnePole lpl, lpr, hpl, hpr;
    long D = 1;
    double fb = 0;
    void init(long delay, double feedback, double lpHz) {
        D = delay;
        fb = feedback;
        dl.init(std::size_t(D) + 4);
        dr.init(std::size_t(D) + 4);
        lpl.set(lpHz);
        lpr.set(lpHz);
        hpl.set(250);
        hpr.set(250);
    }
    void step(float inL, float inR, float& outL, float& outR) {
        double mono = 0.5 * (inL + inR);
        double yl = dl.tap(std::size_t(D)), yr = dr.tap(std::size_t(D));
        dl.push(undenorm(mono + hpl.hp(lpl.lp(yr)) * fb));
        dr.push(undenorm(hpr.hp(lpr.lp(yl)) * fb));
        outL = 0.0f + float(yl);
        outR = 0.0f + float(yr);
    }
};

double meanSquare(const Stereo& s, std::size_t a, std::size_t b) {
    double acc = 0;
    for (std::size_t i = a; i < b; ++i)
        acc += 0.5 * (double(s.L[i]) * s.L[i] + double(s.R[i]) * s.R[i]);
    return acc / double(std::max<std::size_t>(1, b - a));
}

void compressBus(Stereo& s, double ratio, double relThreshDb, bool circular) {
    std::size_t N = s.size();
    double rms = todb(std::sqrt(meanSquare(s, 0, N)));
    double thr = rms + relThreshDb, knee = 6.0;
    double ac = std::exp(-1.0 / (0.012 * SR)), rc = std::exp(-1.0 / (0.2 * SR)),
           dc = std::exp(-1.0 / (0.025 * SR));
    double ms = 0, gdb = 0;
    struct BusGain {
        Stereo& s;
        double& ms;
        double& gdb;
        double dc, thr, knee, ratio, ac, rc;
        void operator()(std::size_t i, bool write) const {
            double x = 0.5 * (double(s.L[i]) * s.L[i] + double(s.R[i]) * s.R[i]);
            ms = dc * ms + (1 - dc) * x;
            double lv = 10.0 * std::log10(ms + 1e-20);
            double over = lv - thr, red = 0;
            if (over > knee / 2)
                red = over * (1 - 1 / ratio);
            else if (over > -knee / 2) {
                double o = over + knee / 2;
                red = (1 - 1 / ratio) * o * o / (2 * knee);
            }
            double target = -red;
            gdb = target < gdb ? ac * gdb + (1 - ac) * target : rc * gdb + (1 - rc) * target;
            if (write) {
                double g = db(gdb);
                s.L[i] = float(s.L[i] * g);
                s.R[i] = float(s.R[i] * g);
            }
        }
    };
    BusGain gain{s, ms, gdb, dc, thr, knee, ratio, ac, rc};
    if (circular) {
        std::size_t warm = std::min<std::size_t>(N, std::size_t(4 * SR));
        for (std::size_t i = N - warm; i < N; ++i)
            gain(i, false);
    }
    for (std::size_t i = 0; i < N; ++i)
        gain(i, true);
}

// Lookahead brickwall limiter on sample peaks; returns max gain reduction in dB.
double limit(Stereo& s, double ceilingDb, bool circular) {
    const std::size_t N = s.size(), W = 96;
    const double ceil = db(ceilingDb);
    std::vector<float> req(N);
    for (std::size_t i = 0; i < N; ++i) {
        double pk = std::max(std::fabs(s.L[i]), std::fabs(s.R[i]));
        req[i] = float(pk > ceil ? ceil / pk : 1.0);
    }
    struct LimiterSample {
        std::size_t N;
        const std::vector<float>& req;
        bool circular;
        float operator()(std::size_t i) const {
            return i < N ? req[i] : (circular ? req[i % N] : 1.0f);
        }
    };
    LimiterSample at{N, req, circular};
    std::vector<float> m(N);
    std::deque<std::size_t> dq;
    for (std::size_t i = 0; i < N + W; ++i) {
        while (!dq.empty() && at(dq.back()) >= at(i))
            dq.pop_back();
        dq.push_back(i);
        if (i + 1 >= W) {
            std::size_t start = i + 1 - W;
            while (dq.front() < start)
                dq.pop_front();
            if (start < N)
                m[start] = at(dq.front());
        }
    }
    const double rel = std::exp(-1.0 / (0.08 * SR));
    std::vector<float> g(N);
    double gp = 1.0;
    int passes = circular ? 2 : 1;
    for (int p = 0; p < passes; ++p)
        for (std::size_t i = 0; i < N; ++i) {
            double rec = gp + (1.0 - gp) * (1.0 - rel);
            gp = std::min<double>(m[i], rec);
            g[i] = float(gp);
        }
    double sum = 0;
    double worst = 1.0;
    std::vector<float> gs(N);
    for (std::size_t k = 0; k < W; ++k)
        sum += circular ? g[(N - W + k) % N] : 1.0;
    for (std::size_t i = 0; i < N; ++i) {
        std::size_t back = (i + N - W) % N;
        double old = (i >= W || circular) ? g[back] : 1.0;
        sum += g[i] - old;
        gs[i] = float(sum / W);
    }
    for (std::size_t i = 0; i < N; ++i) {
        double gg = std::min<double>(gs[i], 1.0);
        worst = std::min(worst, gg);
        s.L[i] = float(s.L[i] * gg);
        s.R[i] = float(s.R[i] * gg);
    }
    return todb(worst);
}

std::vector<double> oversampleKernel() {
    const int taps = 64, up = 4;
    std::vector<double> h(taps);
    for (int n = 0; n < taps; ++n) {
        double x = (n - (taps - 1) / 2.0) / up;
        double sinc = std::fabs(x) < 1e-9 ? 1.0 : std::sin(PI * x) / (PI * x);
        double w =
            0.42 - 0.5 * std::cos(TAU * n / (taps - 1)) + 0.08 * std::cos(2 * TAU * n / (taps - 1));
        h[n] = sinc * w;
    }
    for (int p = 0; p < up; ++p) {
        double s = 0;
        for (int k = p; k < taps; k += up)
            s += h[k];
        for (int k = p; k < taps; k += up)
            h[k] /= s;
    }
    return h;
}

} // namespace

double truePeakDb(const Stereo& s) {
    static const std::vector<double> h = oversampleKernel();
    const std::size_t N = s.size();
    double peak = 0;
    for (int ch = 0; ch < 2; ++ch) {
        const std::vector<float>& x = ch ? s.R : s.L;
        for (std::size_t i = 0; i < N; ++i) {
            for (int p = 0; p < 4; ++p) {
                double acc = 0;
                for (int k = 0, j = p; j < 64; ++k, j += 4) {
                    const std::size_t back = std::size_t(k);
                    std::size_t idx = i >= back ? i - back : (i + N - back) % N;
                    acc += x[idx] * h[j];
                }
                peak = std::max(peak, std::fabs(acc));
            }
        }
    }
    return todb(peak);
}

double integratedLufs(const Stereo& s) {
    // ITU-R BS.1770-4 K-weighting at 48 kHz, 400 ms blocks with 75% overlap, -70 LUFS / -10 LU
    // gates.
    Biquad k1, k2;
    k1.b0 = 1.53512485958697;
    k1.b1 = -2.69169618940638;
    k1.b2 = 1.19839281085285;
    k1.a1 = -1.69065929318241;
    k1.a2 = 0.73248077421585;
    k2.b0 = 1.0;
    k2.b1 = -2.0;
    k2.b2 = 1.0;
    k2.a1 = -1.99004745483398;
    k2.a2 = 0.99007225036621;
    const std::size_t N = s.size(), hop = 4800, blk = 19200;
    std::vector<double> sq(N);
    for (int ch = 0; ch < 2; ++ch) {
        Biquad a = k1, b = k2;
        const std::vector<float>& x = ch ? s.R : s.L;
        for (std::size_t i = 0; i < N; ++i) {
            double y = b.tick(a.tick(x[i]));
            sq[i] += y * y;
        }
    }
    std::vector<double> z;
    for (std::size_t st = 0; st + blk <= N; st += hop) {
        double acc = 0;
        for (std::size_t i = st; i < st + blk; ++i)
            acc += sq[i];
        z.push_back(acc / blk);
    }
    if (z.empty()) {
        double acc = 0;
        for (double v : sq)
            acc += v;
        z.push_back(acc / std::max<std::size_t>(1, N));
    }
    struct Loudness {

        double operator()(double v) const {
            return -0.691 + 10.0 * std::log10(v + 1e-20);
        }
    };
    Loudness loud{};
    double sum = 0;
    int cnt = 0;
    for (double v : z)
        if (loud(v) > -70) {
            sum += v;
            ++cnt;
        }
    if (!cnt)
        return -100;
    double rel = loud(sum / cnt) - 10;
    sum = 0;
    cnt = 0;
    for (double v : z)
        if (loud(v) > -70 && loud(v) > rel) {
            sum += v;
            ++cnt;
        }
    return cnt ? loud(sum / cnt) : -100;
}

namespace {

double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// Each track has its own deterministic random stream, so ducking triggers match the rendered
// hits.
Rng trackRng(const Song& song, std::size_t k) {
    std::uint64_t h = 1469598103934665603ull;
    for (char c : song.id + "/" + song.tracks[k].name)
        h = (h ^ std::uint8_t(c)) * 1099511628211ull;
    return Rng(h ^ (std::uint64_t(song.seed) * 0x9E3779B97F4A7C15ull) ^
               (k * 0xBF58476D1CE4E5B9ull));
}

} // namespace

std::vector<long> duckTriggers(const Song& song) {
    const long L = song.loopSamples();
    const long T = long(song.tail * SR);
    std::vector<long> trig;
    for (std::size_t k = 0; k < song.tracks.size(); ++k)
        if (song.tracks[k].duck_source) {
            Rng r2 = trackRng(song, k);
            Stereo none;
            renderTrack(song, song.tracks[k], none, r2, &trig);
        }
    std::vector<long> trig2 = trig;
    if (song.loop)
        for (long t : trig)
            if (t < T)
                trig2.push_back(t + L);
    std::sort(trig2.begin(), trig2.end());
    return trig2;
}

namespace {

// Track k, as the original rendered it: into the buses (`stem` null) or into a stem of its own.
void renderTrackBuses(const Song& song, std::size_t k, const std::vector<long>& trig2, std::size_t N,
                      Stereo& busDry, Stereo& busRev, Stereo& busDly, TrackStem* stem) {
    const long L = song.loopSamples();
    const Track& tr = song.tracks[k];
    Stereo own;
    if (!stem)
        own = Stereo(N);
    else
        (*stem).dry = Stereo(N);
    Stereo& buf = stem ? (*stem).dry : own;
    Rng rng = trackRng(song, k);
    renderTrack(song, tr, buf, rng, nullptr);
    if (tr.bed && song.loop) {
        const long X = std::min<long>(long(4.0 * SR), L / 4);
        for (long i = 0; i < long(N); ++i) {
            double g;
            if (i < X)
                g = std::sin(0.5 * PI * double(i) / X);
            else if (i < L)
                g = 1.0;
            else if (i < L + X)
                g = std::cos(0.5 * PI * double(i - L) / X);
            else
                g = 0.0;
            buf.L[i] = float(buf.L[i] * g);
            buf.R[i] = float(buf.R[i] * g);
        }
    }
    std::vector<Biquad> fl, fr;
    if (tr.hp > 0) {
        fl.push_back(Biquad::make(1, tr.hp, 0.707));
    }
    if (tr.lp > 0) {
        fl.push_back(Biquad::make(0, tr.lp, 0.707));
    }
    if (tr.eq_low_db != 0)
        fl.push_back(Biquad::make(4, 200, 0.7, tr.eq_low_db));
    if (tr.eq_high_db != 0)
        fl.push_back(Biquad::make(5, 5000, 0.7, tr.eq_high_db));
    if (tr.eq_mid_db != 0)
        fl.push_back(Biquad::make(3, tr.eq_mid_hz, 0.9, tr.eq_mid_db));
    fr = fl;
    double g = db(tr.gain_db);
    std::size_t ti = 0;
    // The original added float(l), float(l * rev) and float(l * dly) straight into the buses;
    // a stem keeps exactly those floats (a zero send adds only signed zeros, which leave a
    // bus that is never -0 unchanged, so a stem does not keep it).
    if (stem) {
        (*stem).rev = tr.rev != 0;
        (*stem).dly = tr.dly != 0;
        (*stem).rsend = Stereo((*stem).rev ? N : 0);
        (*stem).dsend = Stereo((*stem).dly ? N : 0);
    }
    for (std::size_t i = 0; i < N; ++i) {
        double l = buf.L[i], r = buf.R[i];
        for (Biquad& f : fl)
            l = f.tick(l);
        for (Biquad& f : fr)
            r = f.tick(r);
        double dg = 1.0;
        if (tr.duck > 0 && !trig2.empty()) {
            while (ti + 1 < trig2.size() && trig2[ti + 1] <= long(i))
                ++ti;
            long since = long(i) - trig2[ti];
            if (since >= 0) {
                double ts = since / SR;
                double shape = ts < 0.004 ? ts / 0.004 : std::exp(-(ts - 0.004) / 0.12);
                dg = 1.0 - tr.duck * shape;
            }
        }
        l *= g * dg;
        r *= g * dg;
        if (!stem) {
            busDry.L[i] += float(l);
            busDry.R[i] += float(r);
            busRev.L[i] += float(l * tr.rev);
            busRev.R[i] += float(r * tr.rev);
            busDly.L[i] += float(l * tr.dly);
            busDly.R[i] += float(r * tr.dly);
            continue;
        }
        TrackStem& st = *stem;
        st.dry.L[i] = float(l);
        st.dry.R[i] = float(r);
        if (st.rev) {
            st.rsend.L[i] = float(l * tr.rev);
            st.rsend.R[i] = float(r * tr.rev);
        }
        if (st.dly) {
            st.dsend.L[i] = float(l * tr.dly);
            st.dsend.R[i] = float(r * tr.dly);
        }
    }
}

} // namespace

void renderStem(const Song& song, std::size_t k, const std::vector<long>& triggers, std::size_t frames,
                TrackStem& stem) {
    Stereo none;
    renderTrackBuses(song, k, triggers, frames, none, none, none, &stem);
}

void addStem(const TrackStem& stem, Stereo& dry, Stereo& rsend, Stereo& dsend) {
    const std::size_t N = dry.size();
    for (std::size_t i = 0; i < N; ++i) {
        dry.L[i] += stem.dry.L[i];
        dry.R[i] += stem.dry.R[i];
    }
    if (stem.rev)
        for (std::size_t i = 0; i < N; ++i) {
            rsend.L[i] += stem.rsend.L[i];
            rsend.R[i] += stem.rsend.R[i];
        }
    if (stem.dly)
        for (std::size_t i = 0; i < N; ++i) {
            dsend.L[i] += stem.dsend.L[i];
            dsend.R[i] += stem.dsend.R[i];
        }
}

Stereo renderSong(const Song& song, RenderTimings* timings, const StemRenderer& stems) {
    using clock = std::chrono::steady_clock;
    clock::time_point started = clock::now();
    const long L = song.loopSamples();
    const long T = long(song.tail * SR);
    const std::size_t N = std::size_t(L + T);
    Stereo dry(N), rsend(N), dsend(N);
    const std::vector<long> trig2 = duckTriggers(song);
    if (stems) {
        stems(song, trig2, dry, rsend, dsend);
    } else {
        for (std::size_t k = 0; k < song.tracks.size(); ++k)
            renderTrackBuses(song, k, trig2, N, dry, rsend, dsend, nullptr);
    }
    if (timings)
        (*timings).tracks = seconds_since(started);
    started = clock::now();
    // The delay, reverb and master high-pass in one pass (the original ran each over whole
    // buffers in this order; no stage looks ahead, so the samples are the same). The mix
    // replaces the dry bus in place.
    PingPong pp;
    pp.init(long(std::lround(song.dly_beats * song.spb)), song.dly_fb, song.dly_lp);
    Reverb rv;
    rv.init(song.rev_size, song.rev_rt60, song.rev_damp, song.rev_predelay);
    rv.start();
    Biquad hl = Biquad::make(1, song.master_hp, 0.707), hr = hl;
    for (std::size_t i = 0; i < N; ++i) {
        float doutL, doutR, wetL, wetR;
        pp.step(dsend.L[i], dsend.R[i], doutL, doutR);
        rsend.L[i] += doutL * 0.25f;
        rsend.R[i] += doutR * 0.25f;
        rv.step(rsend.L[i], rsend.R[i], wetL, wetR);
        dry.L[i] = float(hl.tick(double(dry.L[i]) + wetL + doutL));
        dry.R[i] = float(hr.tick(double(dry.R[i]) + wetR + doutR));
    }
    rsend = Stereo();
    dsend = Stereo();
    Stereo mix = std::move(dry);
    if (timings)
        (*timings).buses = seconds_since(started);
    started = clock::now();
    Stereo out;
    if (song.loop) {
        out = Stereo(std::size_t(L));
        for (long i = 0; i < L; ++i) {
            out.L[i] = mix.L[i];
            out.R[i] = mix.R[i];
        }
        for (long i = 0; i < T; ++i) {
            out.L[i % L] += mix.L[L + i];
            out.R[i % L] += mix.R[L + i];
        }
    } else {
        // trim trailing silence, short fade at both ends
        long end = long(N);
        while (end > 1 && std::max(std::fabs(mix.L[end - 1]), std::fabs(mix.R[end - 1])) < 3e-5f)
            --end;
        end = std::min<long>(long(N), end + 2400);
        bool capped = false;
        if (song.max_seconds > 0 && end > long(song.max_seconds * SR)) {
            end = long(song.max_seconds * SR);
            capped = true;
        }
        out = Stereo(std::size_t(end));
        for (long i = 0; i < end; ++i) {
            out.L[i] = mix.L[i];
            out.R[i] = mix.R[i];
        }
        long fo = std::min<long>(end / 4, long((capped ? 0.6 : 0.08) * SR));
        for (long i = 0; i < fo; ++i) {
            double gg = double(i) / fo;
            gg = gg * gg;
            out.L[end - 1 - i] *= float(gg);
            out.R[end - 1 - i] *= float(gg);
        }
        for (long i = 0; i < 96 && i < end; ++i) {
            double gg = double(i) / 96;
            out.L[i] *= float(gg);
            out.R[i] *= float(gg);
        }
    }
    mix = Stereo();
    // DC removal (whole-buffer mean; preserves loop continuity)
    for (int ch = 0; ch < 2; ++ch) {
        std::vector<float>& x = ch ? out.R : out.L;
        double m = 0;
        for (float v : x)
            m += v;
        m /= double(x.size());
        if (song.loop)
            for (float& v : x)
                v = float(v - m);
    }
    compressBus(out, song.comp_ratio, song.comp_thresh_db, song.loop);
    // loudness normalization with true-peak-safe limiting
    double ceilDb = -2.0; // headroom for AAC/Vorbis overshoot
    Stereo base = out;
    double gainDb = song.lufs - integratedLufs(base);
    for (int it = 0; it < 6; ++it) {
        out = base;
        double g = db(gainDb);
        for (std::size_t i = 0; i < out.size(); ++i) {
            out.L[i] = float(out.L[i] * g);
            out.R[i] = float(out.R[i] * g);
        }
        limit(out, ceilDb, song.loop);
        double tp = truePeakDb(out);
        double lu = integratedLufs(out);
        bool ok = true;
        if (tp > -1.5) {
            ceilDb -= (tp + 1.5) + 0.05;
            ok = false;
        }
        if (std::fabs(lu - song.lufs) > 0.25) {
            gainDb += song.lufs - lu;
            ok = false;
        }
        if (ok)
            break;
    }
    if (timings)
        (*timings).master = seconds_since(started);
    return out;
}

} // namespace studio
