#include "studio_mix.hpp"
#include "studio_voice.hpp"
#include "studio_crew.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <utility>

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
    // The gain curve reuses the requests' memory (they are not read again), and the smoothed
    // gain is applied as it is made, so a loop's limiter holds two buffers instead of four.
    std::vector<float>& g = req;
    double gp = 1.0;
    int passes = circular ? 2 : 1;
    for (int p = 0; p < passes; ++p)
        for (std::size_t i = 0; i < N; ++i) {
            double rec = gp + (1.0 - gp) * (1.0 - rel);
            gp = std::min<double>(m[i], rec);
            g[i] = float(gp);
        }
    m = std::vector<float>();
    double sum = 0;
    double worst = 1.0;
    for (std::size_t k = 0; k < W; ++k)
        sum += circular ? g[(N - W + k) % N] : 1.0;
    for (std::size_t i = 0; i < N; ++i) {
        std::size_t back = (i + N - W) % N;
        double old = (i >= W || circular) ? g[back] : 1.0;
        sum += g[i] - old;
        const float gs = float(sum / W);
        double gg = std::min<double>(gs, 1.0);
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

namespace {

// The 4x oversampled peak of samples [from, to) of one channel (the loop wraps at N).
double truePeakSpan(const std::vector<float>& x, std::size_t from, std::size_t to) {
    static const std::vector<double> h = oversampleKernel();
    const std::size_t N = x.size();
    double peak = 0;
    for (std::size_t i = from; i < to; ++i) {
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
    return peak;
}

struct PeakJobs {
    const Stereo* s;
    std::size_t parts;
    std::vector<double> peaks;
};

void peakJob(void* context, std::size_t index) {
    PeakJobs& jobs = *static_cast<PeakJobs*>(context);
    const std::vector<float>& x = index % 2 ? (*jobs.s).R : (*jobs.s).L;
    const std::size_t part = index / 2, N = x.size();
    jobs.peaks[index] = truePeakSpan(x, N * part / jobs.parts, N * (part + 1) / jobs.parts);
}

// truePeakDb with the work shared by a team: the largest of the parts' peaks is the peak.
double truePeakDb(const Stereo& s, Team& team) {
    PeakJobs jobs{&s, 8, std::vector<double>(16, 0.0)};
    team.run(jobs.peaks.size(), peakJob, &jobs);
    double peak = 0;
    for (double p : jobs.peaks)
        peak = std::max(peak, p);
    return todb(peak);
}

} // namespace

double truePeakDb(const Stereo& s) {
    double peak = 0;
    peak = std::max(peak, truePeakSpan(s.L, 0, s.size()));
    peak = std::max(peak, truePeakSpan(s.R, 0, s.size()));
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

// Whether an instrument's voice draws from the track's random stream while it sounds (noise
// in its tick). Every voice draws when its note starts; one that draws while it sounds must
// finish before the track's next note starts, as the original rendered them.
bool drawsWhileSounding(Inst i) {
    switch (i) {
    case Inst::Organ:
    case Inst::Glock:
    case Inst::Celesta:
    case Inst::MusicBox:
    case Inst::Vibes:
    case Inst::Marimba:
    case Inst::Timpani:
    case Inst::Kalimba:
    case Inst::Piano:
    case Inst::FunkBass:
    case Inst::SlapBass:
    case Inst::Flute:
    case Inst::Clarinet:
    case Inst::Whistle:
    case Inst::Kick:
    case Inst::Snare:
    case Inst::Clap:
    case Inst::HatC:
    case Inst::HatO:
    case Inst::Ride:
    case Inst::Crash:
    case Inst::Swell:
    case Inst::Tom:
    case Inst::Shaker:
    case Inst::Tamb:
    case Inst::Rim:
    case Inst::WoodBlock:
    case Inst::Brush:
    case Inst::BrushSweep:
    case Inst::Snap:
    case Inst::Water:
    case Inst::Air:
        return true;
    default:
        return false;
    }
}

// A track's raw voice output (the original's whole-length `buf`) for the samples not yet
// filtered: a ring that grows when a note writes further ahead than it holds. Samples it has
// not been given are zero, as in the original's zeroed buffer.
class Window {
  public:
    void reserveUntil(long end) {
        if (end - base_ <= long(cap_))
            return;
        std::size_t cap = cap_ ? cap_ * 2 : 65536;
        while (long(cap) < end - base_)
            cap *= 2;
        std::vector<float> l(cap, 0.0f), r(cap, 0.0f);
        for (std::size_t i = 0; i < cap_; ++i) {
            l[(std::size_t(base_) + i) & (cap - 1)] = L_[(std::size_t(base_) + i) & (cap_ - 1)];
            r[(std::size_t(base_) + i) & (cap - 1)] = R_[(std::size_t(base_) + i) & (cap_ - 1)];
        }
        L_.swap(l);
        R_.swap(r);
        cap_ = cap;
    }
    float& l(long i) {
        return L_[std::size_t(i) & (cap_ - 1)];
    }
    float& r(long i) {
        return R_[std::size_t(i) & (cap_ - 1)];
    }
    // Samples before `end` are finished with: they read as zero again on the ring's next lap.
    void consume(long end) {
        for (long i = base_; i < end; ++i) {
            l(i) = 0.0f;
            r(i) = 0.0f;
        }
        base_ = end;
    }

  private:
    std::vector<float> L_, R_;
    std::size_t cap_ = 0;
    long base_ = 0;
};

// One poly note: the original's per-note loop, able to stop before a sample and resume.
struct NoteRun {
    std::unique_ptr<Voice> v;
    Ev e{};
    double pl = 0, pr = 0;
    long i = 0, maxlen = 0;
    bool pretick = false, done = false;

    void begin(const Track& tr, const Ev& ev, Rng& rng) {
        v = std::make_unique<Voice>(tr.p, rng);
        e = ev;
        (*v).noteOn(e.pitch, e.vel, e.len / SR);
        panGains(clamp(tr.pan + e.pan, -1.0, 1.0), pl, pr);
        pretick = tr.bed;
        maxlen = e.len + long((*v).maxTail() * SR);
    }
    // Runs the note until its next sample would be at or after `until` (or it ends), calling
    // put(idx, l, r) with the floats the original added at idx.
    template <class Put>
    void run(long until, long N, Put&& put) {
        if (pretick) {
            double l, r;
            for (long k = 0; k < long(3.5 * SR); ++k)
                (*v).tick(l, r);
            pretick = false;
        }
        while (!done) {
            if (i >= maxlen) {
                done = true;
                break;
            }
            const long idx = e.s0 + i;
            if (idx >= until && idx < N)
                return;
            if (i == e.len)
                (*v).noteOff();
            double l, r;
            (*v).tick(l, r);
            if (idx >= N) {
                done = true;
                break;
            }
            put(idx, float(l * pl), float(r * pr));
            const bool ended = i > e.len && (*v).finished();
            ++i;
            if (ended)
                done = true;
        }
        v.reset();
    }
};

// A sounding note of an instrument that does not draw while it sounds: rendered a segment at
// a time on any thread into its own slice, then added into its track in note order.
struct NoteSlot {
    NoteRun run;
    long sliceStart = 0;
    long written = 0; // slice samples written this segment
    std::vector<float> L, R;
};

struct TrackRun {
    const Song* song = nullptr;
    const Track* tr = nullptr;
    Rng rng;
    std::vector<Ev> evs;
    std::size_t next = 0;
    bool mono = false, serial = false;
    long N = 0, L = 0;
    Window win;
    // serial poly tracks: the note sounding now
    NoteRun cur;
    bool hasCur = false;
    // mono tracks: the original's single voice and its loop position
    std::unique_ptr<Voice> mv;
    long pos = 0, offAt = -1, prevEnd = -1000000, stop = 0;
    bool started = false, inTail = false, monoDone = false;
    double mpl = 0, mpr = 0;
    // other poly tracks: the notes sounding, in order
    std::deque<NoteSlot> active;
    // filters, gain and ducking
    std::vector<Biquad> fl, fr;
    double g = 1;
    std::size_t ti = 0;
    // this segment's contribution to the buses: exactly the floats the original added
    std::vector<float> dL, dR, rL, rR, yL, yR;

    explicit TrackRun(Rng r) : rng(r) {}

    void stepMono() {
        if (pos == offAt)
            (*mv).noteOff();
        double l, r;
        (*mv).tick(l, r);
        if (pos >= 0 && pos < N) {
            win.l(pos) += float(l * mpl);
            win.r(pos) += float(r * mpr);
        }
        ++pos;
    }

    // The original's mono loop, paused before sample `until`.
    void advanceMono(long until) {
        while (!monoDone) {
            if (!inTail) {
                if (next < evs.size()) {
                    const Ev& e = evs[next];
                    while (pos < e.s0) {
                        if (pos >= until)
                            return;
                        stepMono();
                    }
                    bool legato = started && (e.glide || prevEnd >= e.s0 - 48);
                    if (!started) {
                        (*mv).noteOn(e.pitch, e.vel, e.len / SR);
                        started = true;
                    } else if (legato) {
                        (*mv).setGlide(e.glide ? (*tr).p.glide * 3.0 : (*tr).p.glide);
                        (*mv).glideTo(e.pitch, e.vel, e.len / SR);
                    } else {
                        (*mv).setGlide((*tr).p.glide);
                        (*mv).retrigger(e.pitch, e.vel, e.len / SR);
                    }
                    offAt = e.s0 + e.len;
                    prevEnd = offAt;
                    ++next;
                } else {
                    inTail = true;
                    stop = offAt + long((*mv).maxTail() * SR);
                }
            } else {
                if (!(pos < stop && pos < N)) {
                    monoDone = true;
                    break;
                }
                if (pos >= until)
                    return;
                stepMono();
                if (pos > offAt && (*mv).finished())
                    monoDone = true;
            }
        }
    }

    // A poly track whose voices draw while sounding: each note runs to its end before the next
    // starts, so the stream is drawn in the original's order; a note pauses at `until` only
    // while the next note starts later.
    void advanceSerial(long until) {
        for (;;) {
            if (hasCur) {
                const bool nextDue = next < evs.size() && evs[next].s0 < until;
                const long limit = nextDue ? std::numeric_limits<long>::max() : until;
                // a note that must finish now may write past the segment
                if (nextDue)
                    win.reserveUntil(std::min(cur.e.s0 + cur.maxlen, N));
                cur.run(limit, N, [this](long idx, float l, float r) {
                    win.l(idx) += l;
                    win.r(idx) += r;
                });
                if (!cur.done)
                    return;
                hasCur = false;
            }
            if (next < evs.size() && evs[next].s0 < until) {
                cur = NoteRun{};
                cur.begin(*tr, evs[next], rng);
                ++next;
                hasCur = true;
                continue;
            }
            return;
        }
    }

    // Filters, gain and ducking for [a, b), as the original applied them to its whole buffer.
    void finishSegment(long a, long b, const std::vector<long>& trig2) {
        const Track& t = *tr;
        const std::size_t n = std::size_t(b - a);
        dL.resize(n);
        dR.resize(n);
        rL.resize(n);
        rR.resize(n);
        yL.resize(n);
        yR.resize(n);
        const bool bedFade = t.bed && (*song).loop;
        const long X = std::min<long>(long(4.0 * SR), L / 4);
        for (long i = a; i < b; ++i) {
            float bl = win.l(i), br = win.r(i);
            if (bedFade) {
                double gg;
                if (i < X)
                    gg = std::sin(0.5 * PI * double(i) / X);
                else if (i < L)
                    gg = 1.0;
                else if (i < L + X)
                    gg = std::cos(0.5 * PI * double(i - L) / X);
                else
                    gg = 0.0;
                bl = float(bl * gg);
                br = float(br * gg);
            }
            double l = bl, r = br;
            for (Biquad& f : fl)
                l = f.tick(l);
            for (Biquad& f : fr)
                r = f.tick(r);
            double dg = 1.0;
            if (t.duck > 0 && !trig2.empty()) {
                while (ti + 1 < trig2.size() && trig2[ti + 1] <= long(i))
                    ++ti;
                long since = long(i) - trig2[ti];
                if (since >= 0) {
                    double ts = since / SR;
                    double shape = ts < 0.004 ? ts / 0.004 : std::exp(-(ts - 0.004) / 0.12);
                    dg = 1.0 - t.duck * shape;
                }
            }
            l *= g * dg;
            r *= g * dg;
            const std::size_t k = std::size_t(i - a);
            dL[k] = float(l);
            dR[k] = float(r);
            rL[k] = float(l * t.rev);
            rR[k] = float(r * t.rev);
            yL[k] = float(l * t.dly);
            yR[k] = float(r * t.dly);
        }
        win.consume(b);
    }
};

// One segment's work, shared out as jobs.
struct Segment {
    std::vector<TrackRun>* tracks = nullptr;
    const std::vector<long>* trig2 = nullptr;
    long a = 0, b = 0;
    std::vector<std::size_t> serial; // serial poly and mono tracks, one job each
    std::vector<NoteSlot*> slots;    // then one job per sounding note of the other tracks
};

void voicesJob(void* context, std::size_t index) {
    Segment& s = *static_cast<Segment*>(context);
    if (index < s.serial.size()) {
        TrackRun& t = (*s.tracks)[s.serial[index]];
        if (t.mono)
            t.advanceMono(s.b);
        else
            t.advanceSerial(s.b);
        return;
    }
    NoteSlot& slot = *s.slots[index - s.serial.size()];
    slot.sliceStart = s.a;
    slot.written = 0;
    slot.L.resize(std::size_t(s.b - s.a));
    slot.R.resize(std::size_t(s.b - s.a));
    const long N = (*s.tracks)[0].N;
    slot.run.run(s.b, N, [&slot](long idx, float l, float r) {
        const std::size_t k = std::size_t(idx - slot.sliceStart);
        // a note writes its samples in order and leaves none out once it has begun
        slot.L[k] = l;
        slot.R[k] = r;
        slot.written = long(k) + 1;
    });
}

void finishJob(void* context, std::size_t index) {
    Segment& s = *static_cast<Segment*>(context);
    TrackRun& t = (*s.tracks)[index];
    if (!t.serial && !t.mono) {
        // the notes' slices, added in note order as the original added whole notes
        for (NoteSlot& slot : t.active) {
            long first = 0;
            if (slot.run.e.s0 > slot.sliceStart)
                first = slot.run.e.s0 - slot.sliceStart;
            for (long k = first; k < slot.written; ++k) {
                t.win.l(slot.sliceStart + k) += slot.L[std::size_t(k)];
                t.win.r(slot.sliceStart + k) += slot.R[std::size_t(k)];
            }
        }
        for (std::size_t k = 0; k < t.active.size();)
            if (t.active[k].run.done)
                t.active.erase(t.active.begin() + std::ptrdiff_t(k));
            else
                ++k;
    }
    t.finishSegment(s.a, s.b, *s.trig2);
}

// Frames per segment: every track's voices for a segment are rendered at once, then summed
// into the buses in track order.
constexpr long segmentFrames = 48000;

} // namespace

Stereo renderSong(const Song& song, RenderTimings* timings, const Crew& crew) {
    using clock = std::chrono::steady_clock;
    clock::time_point started = clock::now();
    const long L = song.loopSamples();
    const long T = long(song.tail * SR);
    const std::size_t N = std::size_t(L + T);
    // ducking triggers: every duck source's note starts (its schedule, from a fresh stream)
    std::vector<long> trig;
    for (std::size_t k = 0; k < song.tracks.size(); ++k)
        if (song.tracks[k].duck_source) {
            Rng r2 = trackRng(song, k);
            for (const Ev& e : schedule(song, song.tracks[k], r2))
                trig.push_back(e.s0);
        }
    std::vector<long> trig2 = trig;
    if (song.loop)
        for (long t : trig)
            if (t < T)
                trig2.push_back(t + L);
    std::sort(trig2.begin(), trig2.end());

    std::vector<TrackRun> tracks;
    tracks.reserve(song.tracks.size());
    for (std::size_t k = 0; k < song.tracks.size(); ++k) {
        const Track& tr = song.tracks[k];
        tracks.emplace_back(trackRng(song, k));
        TrackRun& t = tracks.back();
        t.song = &song;
        t.tr = &tr;
        t.N = long(N);
        t.L = L;
        t.evs = schedule(song, tr, t.rng);
        t.mono = isMono(tr.p.inst);
        t.serial = !t.mono && drawsWhileSounding(tr.p.inst);
        if (t.mono) {
            if (t.evs.empty()) {
                t.monoDone = true;
            } else {
                t.mv = std::make_unique<Voice>(tr.p, t.rng);
                t.pos = t.evs[0].s0;
                panGains(tr.pan, t.mpl, t.mpr);
            }
        }
        if (tr.hp > 0) {
            t.fl.push_back(Biquad::make(1, tr.hp, 0.707));
        }
        if (tr.lp > 0) {
            t.fl.push_back(Biquad::make(0, tr.lp, 0.707));
        }
        if (tr.eq_low_db != 0)
            t.fl.push_back(Biquad::make(4, 200, 0.7, tr.eq_low_db));
        if (tr.eq_high_db != 0)
            t.fl.push_back(Biquad::make(5, 5000, 0.7, tr.eq_high_db));
        if (tr.eq_mid_db != 0)
            t.fl.push_back(Biquad::make(3, tr.eq_mid_hz, 0.9, tr.eq_mid_db));
        t.fr = t.fl;
        t.g = db(tr.gain_db);
    }

    // The delay, reverb and master high-pass run sample by sample behind the tracks (the
    // original ran each over whole buffers in this order; none looks ahead).
    PingPong pp;
    pp.init(long(std::lround(song.dly_beats * song.spb)), song.dly_fb, song.dly_lp);
    Reverb rv;
    rv.init(song.rev_size, song.rev_rt60, song.rev_damp, song.rev_predelay);
    rv.start();
    Biquad hl = Biquad::make(1, song.master_hp, 0.707), hr = hl;
    Stereo mix(N);
    double busSeconds = 0;
    Team team(crew);
    {
        Segment seg;
        std::vector<float> sum[6];
        seg.tracks = &tracks;
        seg.trig2 = &trig2;
        for (long a = 0; a < long(N); a += segmentFrames) {
            const long b = std::min<long>(a + segmentFrames, long(N));
            seg.a = a;
            seg.b = b;
            seg.serial.clear();
            seg.slots.clear();
            for (std::size_t k = 0; k < tracks.size(); ++k) {
                TrackRun& t = tracks[k];
                t.win.reserveUntil(b);
                if (t.mono || t.serial) {
                    seg.serial.push_back(k);
                    continue;
                }
                // notes start in order, drawing from the track's stream as the original did
                while (t.next < t.evs.size() && t.evs[t.next].s0 < b) {
                    t.active.emplace_back();
                    t.active.back().run.begin(*t.tr, t.evs[t.next], t.rng);
                    ++t.next;
                }
                for (NoteSlot& slot : t.active)
                    seg.slots.push_back(&slot);
            }
            team.run(seg.serial.size() + seg.slots.size(), voicesJob, &seg);
            team.run(tracks.size(), finishJob, &seg);
            const clock::time_point busStart = clock::now();
            // the buses, summed track after track as the original summed them
            const std::size_t n = std::size_t(b - a);
            for (std::vector<float>* bus : {&sum[0], &sum[1], &sum[2], &sum[3], &sum[4], &sum[5]})
                (*bus).assign(n, 0.0f);
            for (const TrackRun& t : tracks) {
                const std::vector<float>* from[6] = {&t.dL, &t.dR, &t.rL, &t.rR, &t.yL, &t.yR};
                for (int c = 0; c < 6; ++c) {
                    float* to = sum[c].data();
                    const float* add = (*from[c]).data();
                    for (std::size_t k = 0; k < n; ++k)
                        to[k] += add[k];
                }
            }
            for (long i = a; i < b; ++i) {
                const std::size_t k = std::size_t(i - a);
                float doutL, doutR, wetL, wetR;
                pp.step(sum[4][k], sum[5][k], doutL, doutR);
                float revL = sum[2][k], revR = sum[3][k];
                revL += doutL * 0.25f;
                revR += doutR * 0.25f;
                rv.step(revL, revR, wetL, wetR);
                mix.L[std::size_t(i)] = float(hl.tick(double(sum[0][k]) + wetL + doutL));
                mix.R[std::size_t(i)] = float(hr.tick(double(sum[1][k]) + wetR + doutR));
            }
            busSeconds += seconds_since(busStart);
        }
    }
    tracks.clear();
    if (timings) {
        (*timings).buses = busSeconds;
        (*timings).tracks = seconds_since(started) - busSeconds;
    }
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
        double tp = truePeakDb(out, team);
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
