#include "mix.hpp"
#include "voice.hpp"
#include <algorithm>
#include <cstdio>
#include <deque>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ps {

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
    void process(const Stereo& in, Stereo& out) {
        Delay preR;
        preR.init(preN * 2 + 8);
        for (std::size_t i = 0; i < in.size(); ++i) {
            double xl = hpL.tick(in.L[i]), xr = hpR.tick(in.R[i]);
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
            out.L[i] += float(lpL.tick(ol));
            out.R[i] += float(lpR.tick(orr));
        }
    }
};

void pingPong(const Stereo& in, Stereo& out, long D, double fb, double lpHz) {
    Delay dl, dr;
    dl.init(std::size_t(D) + 4);
    dr.init(std::size_t(D) + 4);
    OnePole lpl, lpr, hpl, hpr;
    lpl.set(lpHz);
    lpr.set(lpHz);
    hpl.set(250);
    hpr.set(250);
    for (std::size_t i = 0; i < in.size(); ++i) {
        double mono = 0.5 * (in.L[i] + in.R[i]);
        double yl = dl.tap(std::size_t(D)), yr = dr.tap(std::size_t(D));
        dl.push(undenorm(mono + hpl.hp(lpl.lp(yr)) * fb));
        dr.push(undenorm(hpr.hp(lpr.lp(yl)) * fb));
        out.L[i] += float(yl);
        out.R[i] += float(yr);
    }
}

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
                    std::size_t idx = (i + N - std::size_t(k)) % N;
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

SeamStats seamStats(const Stereo& s) {
    SeamStats st;
    const std::size_t N = s.size();
    if (N < 2000)
        return st;
    double jump = 0, d2s = 0;
    for (int ch = 0; ch < 2; ++ch) {
        const std::vector<float>& x = ch ? s.R : s.L;
        jump = std::max(jump, double(std::fabs(x[0] - x[N - 1])));
        d2s = std::max(d2s, double(std::fabs(x[1] - 2 * x[0] + x[N - 1])));
        d2s = std::max(d2s, double(std::fabs(x[0] - 2 * x[N - 1] + x[N - 2])));
    }
    long below = 0, total = 0, below2 = 0;
    for (int ch = 0; ch < 2; ++ch) {
        const std::vector<float>& x = ch ? s.R : s.L;
        for (std::size_t i = 2; i < N; ++i) {
            double d = std::fabs(x[i] - x[i - 1]);
            double d2 = std::fabs(x[i] - 2 * x[i - 1] + x[i - 2]);
            if (d < jump)
                ++below;
            if (d2 < d2s)
                ++below2;
            ++total;
        }
    }
    st.jump = jump;
    st.jump_percentile = 100.0 * below / std::max(1L, total);
    st.d2_percentile = 100.0 * below2 / std::max(1L, total);
    double a = std::sqrt(meanSquare(s, N - 960, N)), b = std::sqrt(meanSquare(s, 0, 960));
    st.rms_step_db = std::fabs(todb(a + 1e-9) - todb(b + 1e-9));
    return st;
}

Stereo renderSong(const Song& song, Report& rep) {
    const long L = song.loopSamples();
    const long T = long(song.tail * SR);
    const std::size_t N = std::size_t(L + T);
    Stereo dry(N), rsend(N), dsend(N);
    // Each track has its own deterministic random stream, so ducking triggers match the rendered
    // hits.
    struct TrackRng {
        const Song& song;
        Rng operator()(std::size_t k) const {
            std::uint64_t h = 1469598103934665603ull;
            for (char c : song.id + "/" + song.tracks[k].name)
                h = (h ^ std::uint8_t(c)) * 1099511628211ull;
            return Rng(h ^ (std::uint64_t(song.seed) * 0x9E3779B97F4A7C15ull) ^
                       (k * 0xBF58476D1CE4E5B9ull));
        }
    };
    TrackRng trackRng{song};
    std::vector<long> trig;
    for (std::size_t k = 0; k < song.tracks.size(); ++k)
        if (song.tracks[k].duck_source) {
            Rng r2 = trackRng(k);
            Stereo none;
            renderTrack(song, song.tracks[k], none, r2, &trig);
        }
    std::vector<long> trig2 = trig;
    if (song.loop)
        for (long t : trig)
            if (t < T)
                trig2.push_back(t + L);
    std::sort(trig2.begin(), trig2.end());
    for (std::size_t k = 0; k < song.tracks.size(); ++k) {
        const Track& tr = song.tracks[k];
        Stereo buf(N);
        Rng rng = trackRng(k);
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
        double tsq = 0, tpk = 0;
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
            tsq += 0.5 * (l * l + r * r);
            tpk = std::max(tpk, std::max(std::fabs(l), std::fabs(r)));
            dry.L[i] += float(l);
            dry.R[i] += float(r);
            rsend.L[i] += float(l * tr.rev);
            rsend.R[i] += float(r * tr.rev);
            dsend.L[i] += float(l * tr.dly);
            dsend.R[i] += float(r * tr.dly);
        }
        std::fprintf(stderr, "  track %-16s rms %6.1f dB  peak %6.1f dB  notes %zu\n",
                     tr.name.c_str(), todb(std::sqrt(tsq / double(N))), todb(tpk), tr.notes.size());
    }
    Stereo dout(N);
    pingPong(dsend, dout, long(std::lround(song.dly_beats * song.spb)), song.dly_fb, song.dly_lp);
    for (std::size_t i = 0; i < N; ++i) {
        rsend.L[i] += dout.L[i] * 0.25f;
        rsend.R[i] += dout.R[i] * 0.25f;
    }
    Stereo wet(N);
    Reverb rv;
    rv.init(song.rev_size, song.rev_rt60, song.rev_damp, song.rev_predelay);
    rv.process(rsend, wet);
    Biquad hl = Biquad::make(1, song.master_hp, 0.707), hr = hl;
    Stereo mix(N);
    for (std::size_t i = 0; i < N; ++i) {
        mix.L[i] = float(hl.tick(double(dry.L[i]) + wet.L[i] + dout.L[i]));
        mix.R[i] = float(hr.tick(double(dry.R[i]) + wet.R[i] + dout.R[i]));
    }
    dry = Stereo();
    rsend = Stereo();
    dsend = Stereo();
    wet = Stereo();
    dout = Stereo();
    for (std::size_t i = N - 4800; i < N; ++i)
        rep.tail_residual =
            std::max<double>(rep.tail_residual, std::max(std::fabs(mix.L[i]), std::fabs(mix.R[i])));
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
    double ceilDb = -2.0, worstGr = 0; // headroom for AAC/Vorbis overshoot
    Stereo base = out;
    double gainDb = song.lufs - integratedLufs(base);
    for (int it = 0; it < 6; ++it) {
        out = base;
        double g = db(gainDb);
        for (std::size_t i = 0; i < out.size(); ++i) {
            out.L[i] = float(out.L[i] * g);
            out.R[i] = float(out.R[i] * g);
        }
        worstGr = limit(out, ceilDb, song.loop);
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
    rep.id = song.id;
    rep.title = song.title;
    rep.loop = song.loop;
    rep.samples = long(out.size());
    rep.seconds = out.size() / SR;
    rep.bpm = song.bpm();
    rep.bars = song.bars;
    rep.lufs = integratedLufs(out);
    rep.true_peak_dbtp = truePeakDb(out);
    rep.gain_reduction_max_db = worstGr;
    double pk = 0;
    long clip = 0;
    double dcm = 0;
    for (int ch = 0; ch < 2; ++ch) {
        const std::vector<float>& x = ch ? out.R : out.L;
        double m = 0;
        for (float v : x) {
            pk = std::max(pk, double(std::fabs(v)));
            if (std::fabs(v) >= 0.9999f)
                ++clip;
            m += v;
        }
        dcm = std::max(dcm, std::fabs(m / double(x.size())));
    }
    rep.peak_dbfs = todb(pk);
    rep.clipped = clip;
    rep.dc_max = dcm;
    rep.start_abs = std::max(std::fabs(out.L[0]), std::fabs(out.R[0]));
    rep.end_abs = std::max(std::fabs(out.L.back()), std::fabs(out.R.back()));
    long cursor = 0;
    rep.sections = song.sections;
    for (const std::pair<std::string, int>& sec : song.sections) {
        long len = long(sec.second) * song.bpb * song.spb;
        long a = std::min<long>(cursor, long(out.size())),
             b = std::min<long>(cursor + len, long(out.size()));
        rep.section_rms_db.push_back(
            {sec.first, todb(std::sqrt(meanSquare(out, std::size_t(a), std::size_t(b))) + 1e-12)});
        cursor += len;
    }
    // rough spectral balance
    const double edges[7] = {20, 100, 300, 1200, 4000, 10000, 20000};
    const char* names[6] = {"sub_20_100", "low_100_300", "lowmid_300_1k2",
                            "mid_1k2_4k", "high_4k_10k", "air_10k_20k"};
    double e[6] = {0}, tot = 0;
    for (int b = 0; b < 6; ++b) {
        for (int ch = 0; ch < 2; ++ch) {
            Biquad h1 = Biquad::make(1, edges[b], 0.707), h2 = h1,
                   l1 = Biquad::make(0, edges[b + 1], 0.707), l2 = l1;
            const std::vector<float>& x = ch ? out.R : out.L;
            for (std::size_t i = 0; i < x.size(); i += 1) {
                double y = l2.tick(l1.tick(h2.tick(h1.tick(x[i]))));
                e[b] += y * y;
            }
        }
        tot += e[b];
    }
    for (int b = 0; b < 6; ++b)
        rep.bands_db.push_back({names[b], 10 * std::log10(e[b] / tot + 1e-20)});
    if (song.loop)
        rep.seam = seamStats(out);
    return out;
}

void writeWav24(const std::string& path, const Stereo& s, std::size_t extraWrap) {
    std::ofstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot write " + path);
    std::size_t frames = s.size() + extraWrap;
    struct Write32 {
        std::ofstream& f;
        void operator()(std::uint32_t v) const {
            f.write(reinterpret_cast<const char*>(&v), 4);
        }
    };
    Write32 u32{f};
    struct Write16 {
        std::ofstream& f;
        void operator()(std::uint16_t v) const {
            f.write(reinterpret_cast<const char*>(&v), 2);
        }
    };
    Write16 u16{f};
    std::uint32_t data = std::uint32_t(frames * 6);
    f.write("RIFF", 4);
    u32(36 + data);
    f.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(2);
    u32(48000);
    u32(48000 * 6);
    u16(6);
    u16(24);
    f.write("data", 4);
    u32(data);
    std::vector<char> buf;
    buf.reserve(frames * 6);
    for (std::size_t i = 0; i < frames; ++i) {
        std::size_t j = i % s.size();
        for (int ch = 0; ch < 2; ++ch) {
            double v = clamp(double(ch ? s.R[j] : s.L[j]), -1.0, 1.0 - 1.0 / 8388608.0);
            std::int32_t q = std::int32_t(std::lround(v * 8388608.0));
            q = std::min(q, 8388607);
            q = std::max(q, -8388608);
            buf.push_back(char(q & 0xFF));
            buf.push_back(char((q >> 8) & 0xFF));
            buf.push_back(char((q >> 16) & 0xFF));
        }
    }
    f.write(buf.data(), std::streamsize(buf.size()));
}

void writeF32(const std::string& path, const Stereo& s) {
    std::ofstream f(path, std::ios::binary);
    std::vector<float> inter(s.size() * 2);
    for (std::size_t i = 0; i < s.size(); ++i) {
        // match the 24-bit master exactly
        for (int ch = 0; ch < 2; ++ch) {
            double v = clamp(double(ch ? s.R[i] : s.L[i]), -1.0, 1.0 - 1.0 / 8388608.0);
            inter[2 * i + ch] = float(std::lround(v * 8388608.0) / 8388608.0);
        }
    }
    f.write(reinterpret_cast<const char*>(inter.data()), std::streamsize(inter.size() * 4));
}

static std::string num(double v, int prec = 3) {
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", prec, v);
    return b;
}

std::string reportJson(const Report& r) {
    std::ostringstream o;
    o << "{\"id\": \"" << r.id << "\", \"title\": \"" << r.title
      << "\", \"loop\": " << (r.loop ? "true" : "false") << ", \"samples\": " << r.samples
      << ", \"seconds\": " << num(r.seconds) << ", \"bpm\": " << num(r.bpm, 4)
      << ", \"bars\": " << r.bars << ", \"lufs_integrated\": " << num(r.lufs, 2)
      << ", \"true_peak_dbtp\": " << num(r.true_peak_dbtp, 2)
      << ", \"peak_dbfs\": " << num(r.peak_dbfs, 2) << ", \"clipped_samples\": " << r.clipped
      << ", \"dc_offset_max\": " << num(r.dc_max, 8)
      << ", \"limiter_max_gr_db\": " << num(r.gain_reduction_max_db, 2)
      << ", \"tail_residual\": " << num(r.tail_residual, 7)
      << ", \"start_abs\": " << num(r.start_abs, 6) << ", \"end_abs\": " << num(r.end_abs, 6)
      << ", \"seam\": {\"jump\": " << num(r.seam.jump, 6)
      << ", \"jump_percentile\": " << num(r.seam.jump_percentile, 1)
      << ", \"d2_percentile\": " << num(r.seam.d2_percentile, 1)
      << ", \"rms_step_db\": " << num(r.seam.rms_step_db, 2) << "}"
      << ", \"sections\": [";
    for (std::size_t i = 0; i < r.sections.size(); ++i)
        o << (i ? ", " : "") << "[\"" << r.sections[i].first << "\", " << r.sections[i].second
          << "]";
    o << "], \"section_rms_db\": [";
    for (std::size_t i = 0; i < r.section_rms_db.size(); ++i)
        o << (i ? ", " : "") << "[\"" << r.section_rms_db[i].first << "\", "
          << num(r.section_rms_db[i].second, 2) << "]";
    o << "], \"bands_db\": {";
    for (std::size_t i = 0; i < r.bands_db.size(); ++i)
        o << (i ? ", " : "") << "\"" << r.bands_db[i].first
          << "\": " << num(r.bands_db[i].second, 1);
    o << "}}";
    return o.str();
}

static std::vector<float> readF32(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("cannot read " + path);
    std::streamsize n = f.tellg();
    f.seekg(0);
    std::vector<float> v(std::size_t(n / 4));
    f.read(reinterpret_cast<char*>(v.data()), n);
    return v;
}

std::string analyseDecoded(const std::string& decodedPath, const std::string& masterPath,
                           long frames, bool loop) {
    std::vector<float> d = readF32(decodedPath), m = readF32(masterPath);
    long dn = long(d.size() / 2), mn = long(m.size() / 2);
    long n = std::min<long>({frames, dn, mn});
    // alignment: correlate a window anchored at the strongest onset (unambiguous for sustained
    // material)
    // Short effects are correlated over their whole length.
    long win = std::min<long>(24000, n), blk = 2400, centre = n > win + 8192 ? n / 2 : 0;
    {
        double prev = 0, bestRise = -1;
        for (long b = 0; b + blk <= n - win - 4096; b += blk) {
            double e = 0;
            for (long i = b; i < b + blk; ++i)
                e += double(m[2 * i]) * m[2 * i] + double(m[2 * i + 1]) * m[2 * i + 1];
            if (b > 0 && e - prev > bestRise) {
                bestRise = e - prev;
                centre = std::max(4096L, b - 2 * blk);
            }
            prev = e;
        }
    }
    int bestOff = 0;
    double best = -1e300;
    for (int off = -2048; off <= 2048; ++off) {
        double acc = 0;
        for (long i = centre; i < centre + win; ++i) {
            long j = i + off;
            if (j < 0 || j >= dn)
                continue;
            acc += double(d[2 * j]) * m[2 * i] + double(d[2 * j + 1]) * m[2 * i + 1];
        }
        if (acc > best) {
            best = acc;
            bestOff = off;
        }
    }
    double sig = 0, err = 0;
    for (long i = 0; i < n; ++i)
        for (int c = 0; c < 2; ++c) {
            double a = m[2 * i + c], b = d[2 * i + c];
            sig += a * a;
            err += (a - b) * (a - b);
        }
    Stereo s{std::size_t(n)};
    for (long i = 0; i < n; ++i) {
        s.L[i] = d[2 * i];
        s.R[i] = d[2 * i + 1];
    }
    std::ostringstream o;
    o << "{\"decoded_frames\": " << dn << ", \"loop_frames\": " << frames
      << ", \"best_offset\": " << bestOff
      << ", \"snr_db\": " << num(10 * std::log10(sig / std::max(err, 1e-30)), 2)
      << ", \"true_peak_dbtp\": " << num(truePeakDb(s), 2)
      << ", \"lufs\": " << num(integratedLufs(s), 2);
    if (loop) {
        SeamStats st = seamStats(s);
        o << ", \"seam\": {\"jump\": " << num(st.jump, 6)
          << ", \"jump_percentile\": " << num(st.jump_percentile, 1)
          << ", \"d2_percentile\": " << num(st.d2_percentile, 1)
          << ", \"rms_step_db\": " << num(st.rms_step_db, 2) << "}";
    }
    o << "}";
    return o.str();
}

} // namespace ps
