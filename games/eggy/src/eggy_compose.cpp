#include "eggy_compose.hpp"

#include "eggy_fft.hpp"
#include "voice_fft.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>

// compose.py (and songs2.py's helpers) line for line. Python's integer % is
// python_modulo, its float % is py_mod, round() is half to even, min(..., key=) keeps
// the first of equal keys, and sorted() is stable.
namespace eggy::compose {
namespace {

constexpr double sr = voice::sample_rate;

int imod(int a, int b) {
    return static_cast<int>(voice::python_modulo(a, b));
}

// Python's float %: fmod, moved into the divisor's sign.
double py_mod(double a, double b) {
    double m = std::fmod(a, b);
    if (m != 0 && ((b < 0) != (m < 0))) {
        m += b;
    }
    return m;
}

int sign(int x) {
    return (x > 0) - (x < 0);
}

// np.mean of a short list of ints (exact sums, then one division)
double mean(const std::vector<int>& v) {
    double total = 0;
    for (const int x : v) {
        total += x;
    }
    return total / static_cast<double>(v.size());
}

const std::map<std::string, std::vector<int>>& qualities() {
    static const std::map<std::string, std::vector<int>> q{
        {"", {0, 4, 7}},           {"m", {0, 3, 7}},           {"7", {0, 4, 7, 10}},        {"maj7", {0, 4, 7, 11}},
        {"m7", {0, 3, 7, 10}},     {"m7b5", {0, 3, 6, 10}},    {"dim", {0, 3, 6}},          {"dim7", {0, 3, 6, 9}},
        {"sus4", {0, 5, 7}},       {"sus2", {0, 2, 7}},        {"6", {0, 4, 7, 9}},         {"m6", {0, 3, 7, 9}},
        {"9", {0, 4, 7, 10, 14}},  {"maj9", {0, 4, 7, 11, 14}}, {"m9", {0, 3, 7, 10, 14}}, {"add9", {0, 4, 7, 14}},
        {"7sus4", {0, 5, 7, 10}},  {"69", {0, 4, 7, 9, 14}},   {"aug", {0, 4, 8}},          {"7b9", {0, 4, 7, 10, 13}},
        {"13", {0, 4, 7, 10, 21}}, {"m11", {0, 3, 7, 10, 17}}};
    return q;
}

int pcname(const std::string& s) {
    constexpr std::array<int, 7> note{9, 11, 0, 2, 4, 5, 7};  // A B C D E F G
    int v = note[static_cast<std::size_t>(s[0] - 'A')];
    for (std::size_t i = 1; i < s.size(); ++i) {
        v += s[i] == '#' ? 1 : -1;
    }
    return imod(v, 12);
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> parts{};
    std::string word{};
    for (const char c : s) {
        if (c == ' ') {
            if (!word.empty()) {
                parts.push_back(word);
            }
            word.clear();
        } else {
            word += c;
        }
    }
    if (!word.empty()) {
        parts.push_back(word);
    }
    return parts;
}

// STEPW: the melody's step choices and their weights
constexpr std::array<int, 9> step_choices{-1, 1, -2, 2, 0, 3, -3, 4, -4};
constexpr std::array<double, 9> step_weights{0.24, 0.24, 0.12, 0.12, 0.08, 0.07, 0.07, 0.03, 0.03};

std::vector<int> contour(voice::Generator& rng, std::size_t n) {
    std::vector<int> out{};
    int run = 0;
    for (std::size_t i = 0; i < n; ++i) {
        int s = step_choices[rng.choice(step_weights)];
        if (run >= 2 && s * sign(out.back()) > 0) {
            s = -s;
        }
        run = (!out.empty() && sign(s) == sign(out.back()) && s != 0) ? run + 1 : 0;
        out.push_back(s);
    }
    return out;
}

bool contains(const std::vector<int>& v, int x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

int realize(Ctx& ctx, double t0, const Rhythm& rhythm, const std::vector<int>& cont, int target, int lo, int hi,
            const std::string& inst, double vel, double art, double pan, const std::string& bus,
            const std::vector<int>* end_on = nullptr) {
    int p = 0;
    const int strong_div = ctx.bpb == 4 ? 2 : 3;
    for (std::size_t i = 0; i < rhythm.size(); ++i) {
        const auto [on, du] = rhythm[i];
        const double t = t0 + on;
        const Chord& ch = ctx.chord_at(t).chord;
        const std::vector<int> sc = ctx.scale_at(t);
        if (i == 0) {
            bool first = true;
            for (const int pc : ch.pcs) {
                const int c = near(pc, target);
                if (first || std::abs(c - target) < std::abs(p - target)) {
                    p = c;
                    first = false;
                }
            }
        } else {
            const int k = cont[(i - 1) % cont.size()];
            int q = scale_step(p, k, sc);
            if (q < lo || q > hi) {
                q = scale_step(p, -k, sc);
            }
            p = q;
        }
        const double r = voice::python_round(on);
        const bool strong = (std::fabs(on - r) < 1e-6 && imod(static_cast<int>(r), strong_div) == 0) || du >= 1.5;
        if (strong && !contains(ch.pcs, imod(p, 12))) {
            int best = 0;
            bool first = true;
            for (const int pc : ch.pcs) {
                const int c = near(pc, p);
                // key (abs(x - p), -x)
                if (first || std::abs(c - p) < std::abs(best - p) || (std::abs(c - p) == std::abs(best - p) && -c < -best)) {
                    best = c;
                    first = false;
                }
            }
            p = best;
        }
        if (i == rhythm.size() - 1 && end_on != nullptr) {
            int best = 0;
            bool first = true;
            for (const int x : *end_on) {
                const int c = near(imod(ch.root + x, 12), p);
                if (first || std::abs(c - p) < std::abs(best - p)) {
                    best = c;
                    first = false;
                }
            }
            p = best;
        }
        p = std::clamp(p, lo, hi);
        const double v = vel * (std::fabs(py_mod(on, strong_div)) < 1e-6 ? 1.08 : 0.94) + ctx.rng.uniform(-0.04, 0.04);
        ctx.note(t, inst, p, du * art, v, pan, bus);
    }
    return p;
}

}  // namespace

Chord parse_chord(const std::string& text) {
    std::string s = text;
    std::optional<int> bass{};
    const std::size_t slash = s.find('/');
    if (slash != std::string::npos) {
        bass = pcname(s.substr(slash + 1));
        s = s.substr(0, slash);
    }
    std::size_t i = 1;
    while (i < s.size() && (s[i] == '#' || s[i] == 'b')) {
        ++i;
    }
    const int root = pcname(s.substr(0, i));
    Chord c{};
    c.root = root;
    c.q = s.substr(i);
    c.iv = qualities().at(c.q);
    c.bass = bass.has_value() ? *bass : root;
    std::set<int> pcs{};
    for (const int x : c.iv) {
        pcs.insert(imod(root + x, 12));
    }
    c.pcs.assign(pcs.begin(), pcs.end());
    return c;
}

int near(int pc, int target) {
    const int m = target - imod(target - pc, 12);
    return target - m <= 6 ? m : m + 12;
}

int scale_step(int m, int k, const std::vector<int>& sc) {
    std::vector<int> notes{};
    for (int x = m - 30; x < m + 31; ++x) {
        if (contains(sc, imod(x, 12))) {
            notes.push_back(x);
        }
    }
    std::size_t i = 0;
    for (std::size_t j = 1; j < notes.size(); ++j) {
        if (std::abs(notes[j] - m) < std::abs(notes[i] - m)) {
            i = j;
        }
    }
    const long long index = std::clamp<long long>(static_cast<long long>(i) + k, 0, static_cast<long long>(notes.size()) - 1);
    return notes[static_cast<std::size_t>(index)];
}

std::optional<std::vector<int>> voicing(const Chord& ch, const std::optional<std::vector<int>>& prev, int lo, int hi,
                                        bool rootless, bool add9, std::size_t maxn) {
    std::vector<int> pcs{};
    for (const int x : ch.iv) {
        pcs.push_back(imod(ch.root + x, 12));
    }
    if (rootless && pcs.size() >= 4) {
        pcs.erase(pcs.begin());
    }
    if (add9 && (ch.q == "maj7" || ch.q == "m7" || ch.q == "6" || ch.q == "m6") && pcs.size() < maxn) {
        pcs.push_back(imod(ch.root + 2, 12));
    }
    const std::set<int> unique(pcs.begin(), pcs.end());
    pcs.assign(unique.begin(), unique.end());
    if (pcs.size() > maxn) {
        pcs.resize(maxn);
    }
    std::optional<std::vector<int>> best{};
    double bs = 1e9;
    const double center = (lo + hi) / 2.0;
    for (std::size_t r = 0; r < pcs.size(); ++r) {
        std::vector<int> rot(pcs.begin() + static_cast<std::ptrdiff_t>(r), pcs.end());
        rot.insert(rot.end(), pcs.begin(), pcs.begin() + static_cast<std::ptrdiff_t>(r));
        for (int base = lo - 12; base < hi; ++base) {
            std::vector<int> v{};
            int m = base;
            for (const int p : rot) {
                m = m + imod(p - m, 12);
                v.push_back(m);
            }
            if (v.front() < lo || v.back() > hi) {
                continue;
            }
            double sc = std::fabs(mean(v) - center) * 0.35;
            if (prev.has_value()) {
                sc += std::fabs(mean(v) - mean(*prev)) + 0.3 * std::abs(v[0] - (*prev)[0]);
            }
            if (sc < bs) {
                best = v;
                bs = sc;
            }
        }
    }
    return best;
}

Ctx::Ctx(const Song& song) : s(song), bpb(song.bpb), rng(static_cast<std::uint64_t>(song.seed)),
                             mrng(static_cast<std::uint64_t>(song.seed + 1)) {
    double b = 0.0;
    for (std::size_t si = 0; si < song.sections.size(); ++si) {
        const Section& sec = song.sections[si];
        secs.push_back({b, static_cast<int>(sec.bars.size()), &sec});
        for (const std::string& bar : sec.bars) {
            const std::vector<std::string> parts = split(bar);
            const double d = static_cast<double>(bpb) / static_cast<double>(parts.size());
            for (std::size_t k = 0; k < parts.size(); ++k) {
                chords.push_back({b + static_cast<double>(k) * d, d, parse_chord(parts[k]), static_cast<int>(si)});
            }
            b += bpb;
        }
    }
    total = b;
    for (const ChordSpan& c : chords) {
        starts.push_back(c.start);
    }
}

std::size_t Ctx::chord_index(double t) const {
    t = py_mod(t, total);
    const double probe = t + 1e-6;
    const auto at = std::upper_bound(starts.begin(), starts.end(), probe);  // searchsorted 'right'
    const long long i = static_cast<long long>(at - starts.begin()) - 1;
    // Python indexes -1 as the last chord
    return i < 0 ? chords.size() - 1 : static_cast<std::size_t>(i);
}

std::vector<int> Ctx::scale_at(double t) const {
    const Chord& ch = chord_at(t).chord;
    std::set<int> sc{};
    for (const int x : s.scale) {
        sc.insert(imod(s.key + x, 12));
    }
    for (const int c : ch.pcs) {
        if (sc.count(c) == 0) {
            const std::vector<int> keys(sc.begin(), sc.end());
            for (const int k : keys) {
                if (std::min(imod(k - c, 12), imod(c - k, 12)) == 1 && !contains(ch.pcs, k)) {
                    sc.erase(k);
                }
            }
            sc.insert(c);
        }
    }
    return {sc.begin(), sc.end()};
}

void Ctx::note(double t, const std::string& inst, int m, double d, double v, double pan, const std::string& bus) {
    ev.push_back({'n', t, inst, m, d, std::clamp(v, 0.05, 1.0), pan, bus.empty() ? inst : bus});
}

void Ctx::drum(double t, const std::string& name, double v, double pan, const std::string& bus) {
    ev.push_back({'d', t, name, 0, 0, std::clamp(v, 0.02, 1.0), pan, bus});
}

void melody(Ctx& ctx, double sec_start, int nbars, const std::string& inst, int center, const std::vector<Rhythm>& rhythms,
            const std::vector<Rhythm>& cadences, double vel, double art) {
    voice::Generator& rng = ctx.mrng;
    const double pan = 0.08;
    const std::string bus = "lead";
    const int unit = 2 * ctx.bpb;
    const int units = nbars / 2;
    const int lo = center - 9;
    const int hi = center + 10;
    const Rhythm& ra = rhythms[static_cast<std::size_t>(rng.integers(0, static_cast<std::int64_t>(rhythms.size())))];
    const Rhythm& rb = rhythms[static_cast<std::size_t>(rng.integers(0, static_cast<std::int64_t>(rhythms.size())))];
    const std::vector<int> ca = contour(rng, ra.size() + 2);
    const std::vector<int> cb = contour(rng, rb.size() + 2);
    int last = center;
    static const std::array<std::string, 4> roles{"A", "var", "shift", "half"};
    static const std::vector<int> cadence_end{0, 4, 3};
    static const std::vector<int> half_end{7, 4, 3, 2};
    for (int u = 0; u < units; ++u) {
        const double t0 = sec_start + u * unit;
        std::string role = u == units - 1 ? "cad" : roles[static_cast<std::size_t>(u % 4)];
        if (units >= 8 && u % 8 >= 4 && (role == "A" || role == "shift")) {
            role = "B";
        }
        const int tgt = static_cast<int>(0.5 * center + 0.5 * last);
        if (role == "A") {
            last = realize(ctx, t0, ra, ca, tgt, lo, hi, inst, vel, art, pan, bus);
        } else if (role == "var") {
            std::vector<int> c(ca.begin(), ca.end() - 2);
            const std::vector<int> tail = contour(rng, 2);
            c.insert(c.end(), tail.begin(), tail.end());
            last = realize(ctx, t0, ra, c, tgt, lo, hi, inst, vel, art, pan, bus);
        } else if (role == "shift") {
            last = realize(ctx, t0, ra, ca, tgt + 3, lo, hi, inst, vel * 1.04, art, pan, bus);
        } else if (role == "B") {
            last = realize(ctx, t0, rb, cb, tgt + 2, lo, hi, inst, vel * 1.03, art, pan, bus);
        } else {
            const Rhythm& cad = cadences[static_cast<std::size_t>(rng.integers(0, static_cast<std::int64_t>(cadences.size())))];
            static constexpr std::array<int, 5> steps{-1, -1, -2, 1, 0};
            std::vector<int> c{};
            for (std::size_t k = 0; k < cad.size(); ++k) {
                c.push_back(steps[rng.choice(steps.size())]);
            }
            const std::vector<int>& end = role == "cad" ? cadence_end : half_end;
            last = realize(ctx, t0, cad, c, tgt, lo, hi, inst, vel * 0.96, art, pan, bus, &end);
        }
    }
}

void counter(Ctx& ctx, double s0, int nbars, const std::string& inst, int center, double vel, double pan,
             const std::string& bus, double delay, double art) {
    int prev = center;
    double t = s0;
    const double end = s0 + nbars * ctx.bpb;
    while (t < end - 1e-6) {
        const ChordSpan& span = ctx.chord_at(t);
        const Chord& ch = span.chord;
        std::vector<int> tones{};
        for (std::size_t i = 1; i < ch.iv.size(); ++i) {
            tones.push_back(imod(ch.root + ch.iv[i], 12));
        }
        const std::vector<int> two{tones[0], tones.size() > 2 ? tones[2] : tones.back()};
        int p = 0;
        double best = 0;
        bool first = true;
        for (const int pc : two) {
            const int c = near(pc, prev);
            const double key = std::abs(c - prev) + 0.2 * std::abs(c - center);
            if (first || key < best) {
                p = c;
                best = key;
                first = false;
            }
        }
        const double dd = std::min(span.dur, end - t);
        ctx.note(t + delay, inst, p, (dd - delay) * art, vel + ctx.rng.uniform(-.04, .04), pan, bus);
        prev = p;
        t += dd;
    }
}

void bass_line(Ctx& ctx, double s0, int nbars, const std::string& kind, int lo, double vel, const std::string& bus) {
    const int bpb = ctx.bpb;
    voice::Generator& rng = ctx.rng;
    int prev = lo + 7;
    for (int bar = 0; bar < nbars; ++bar) {
        const double b0 = s0 + bar * bpb;
        const std::size_t ci = ctx.chord_index(b0);
        const Chord& ch = ctx.chords[ci].chord;
        int root = near(ch.bass, prev);
        root = root < lo ? root + 12 : (root > lo + 14 ? root - 12 : root);
        const int fifth = root + 7 <= lo + 19 ? root + 7 : root - 5;
        if (kind == "walk") {
            for (int beat = 0; beat < bpb; ++beat) {
                const double t = b0 + beat;
                const Chord& c = ctx.chord_at(t).chord;
                int p = 0;
                if (beat == 0 || std::fabs(ctx.chord_at(t).start - t) < 1e-6) {
                    p = near(c.bass, prev);
                    p = p < lo ? p + 12 : (p > lo + 16 ? p - 12 : p);
                } else if (beat == bpb - 1 || std::fabs(ctx.chord_at(t + 1).start - (t + 1)) < 1e-6) {
                    int nr = near(ctx.chord_at(t + 1).chord.bass, prev);
                    nr = nr < lo ? nr + 12 : (nr > lo + 16 ? nr - 12 : nr);
                    if (rng.random() < 0.7) {
                        static constexpr std::array<int, 2> pm{-1, 1};
                        p = nr + pm[rng.choice(pm.size())];
                    } else {
                        p = nr + (nr + 2 <= lo + 18 ? 2 : -2);
                    }
                } else {
                    std::vector<int> cand{};
                    const std::size_t stop = std::min<std::size_t>(c.iv.size(), 4);
                    for (std::size_t x = 1; x < stop; ++x) {
                        static constexpr std::array<int, 2> jump{-3, 3};
                        cand.push_back(near(imod(c.root + c.iv[x], 12), prev + jump[rng.choice(jump.size())]));
                    }
                    int best = 0;
                    int best_key = 0;
                    bool first = true;
                    for (const int x : cand) {
                        const int key = x != prev ? std::abs(x - prev) : 99;
                        if (first || key < best_key) {
                            best = x;
                            best_key = key;
                            first = false;
                        }
                    }
                    p = best;
                }
                ctx.note(t, "bass", p, 0.88, vel * (beat % 2 == 0 ? 1.05 : 0.92) + rng.uniform(-.04, .04), 0, bus);
                prev = p;
            }
        } else if (kind == "two") {
            ctx.note(b0, ctx.s.bass_inst, root, bpb / 2.0 * 0.6, vel, 0, bus);
            const std::size_t c2 = ctx.chord_index(b0 + bpb / 2.0);
            const int p2 = c2 == ci ? fifth : near(ctx.chords[c2].chord.bass, root);
            ctx.note(b0 + bpb / 2.0, ctx.s.bass_inst, p2, bpb / 2.0 * 0.6, vel * 0.9, 0, bus);
            prev = root;
        }
    }
}

void comp(Ctx& ctx, double s0, int nbars, const std::string& kind, const std::string& inst, int lo, int hi, double vel,
          double pan, const std::string& bus, bool rootless) {
    const int bpb = ctx.bpb;
    voice::Generator& rng = ctx.rng;
    std::optional<std::vector<int>> prev{};
    for (int bar = 0; bar < nbars; ++bar) {
        const double b0 = s0 + bar * bpb;
        if (kind == "sustain") {
            for (const ChordSpan& c : ctx.chords) {
                if (!(b0 - 1e-6 <= c.start && c.start < b0 + bpb - 1e-6)) {
                    continue;
                }
                const std::optional<std::vector<int>> v = voicing(c.chord, prev, lo, hi, rootless);
                for (std::size_t k = 0; k < (*v).size(); ++k) {
                    ctx.note(c.start + static_cast<double>(k) * 0.008, inst, (*v)[k], c.dur * 0.97, vel + rng.uniform(-.05, .05),
                             pan, bus);
                }
                prev = v;
            }
        } else if (kind == "chunk") {
            for (const int beat : {1, 3}) {
                const Chord& c = ctx.chord_at(b0 + beat).chord;
                const std::optional<std::vector<int>> v = voicing(c, prev, lo, hi, false, false, 4);
                for (std::size_t k = 0; k < (*v).size(); ++k) {
                    const double kk = static_cast<double>(k);
                    ctx.note(b0 + beat + kk * 0.012, inst, (*v)[k], 0.32, vel * (0.85 + .1 * kk / 3) + rng.uniform(-.05, .05),
                             pan, bus);
                }
                prev = v;
            }
        }
    }
}

void arp(Ctx& ctx, double s0, int nbars, const std::string& inst, int lo, int hi, double vel, const std::vector<int>& pattern,
         double step, double pan, const std::string& bus, double dur) {
    voice::Generator& rng = ctx.rng;
    std::optional<std::vector<int>> prev{};
    const auto n_steps = static_cast<long long>(nbars * ctx.bpb / step);
    for (long long i = 0; i < n_steps; ++i) {
        const double t = s0 + static_cast<double>(i) * step;
        const Chord& c = ctx.chord_at(t).chord;
        const std::optional<std::vector<int>> v = voicing(c, prev, lo, hi, false, true, 4);
        prev = v;
        std::set<int> tone_set((*v).begin(), (*v).end());
        tone_set.insert((*v)[0] - 12 + imod(c.root - (*v)[0], 12));
        const std::vector<int> tones(tone_set.begin(), tone_set.end());
        const int idx = pattern[static_cast<std::size_t>(i) % pattern.size()];
        const int count = static_cast<int>(tones.size());
        const int m = tones[static_cast<std::size_t>(imod(idx, count))] + 12 * static_cast<int>(std::floor(static_cast<double>(idx) / count));
        const double acc = std::fabs(py_mod(t - s0, ctx.bpb)) < 1e-6 ? 1.0 : 0.88;
        const double v_jit = vel * acc + rng.uniform(-.05, .05);
        const double pan_jit = pan + rng.uniform(-.1, .1);
        ctx.note(t, inst, m, dur != 0 ? dur : step * 1.6, v_jit, pan_jit, bus);
    }
}

void pdrums(Ctx& ctx, double s0, int nb, const std::vector<DrumBar>& bars, double e, const DrumBar* fill) {
    for (int b = 0; b < nb; ++b) {
        const DrumBar& pat = (fill != nullptr && b == nb - 1) ? *fill : bars[static_cast<std::size_t>(b) % bars.size()];
        for (const Hit& h : pat) {
            ctx.drum(s0 + b * ctx.bpb + h.t, h.name, h.v * e + ctx.rng.uniform(-.02, .02), h.p);
        }
    }
}

void bass_oct(Ctx& ctx, double s0, int nb, const std::string& inst, int lo, double vel, const std::vector<OctaveStep>& pat) {
    for (int b = 0; b < nb; ++b) {
        const double b0 = s0 + b * ctx.bpb;
        for (const OctaveStep& step : pat) {
            const Chord& c = ctx.chord_at(b0 + step.t).chord;
            const int r = near(c.bass, lo + 6);
            ctx.note(b0 + step.t, inst, r + step.iv, step.d, vel * (step.iv == 0 ? 1 : .8) + ctx.rng.uniform(-.04, .04), 0,
                     "bass");
        }
    }
}

void scatter(Ctx& ctx, double s0, int nb, const std::string& inst, int lo, int hi, double rate, double vel,
             const std::string& bus, const std::vector<int>* scale) {
    const std::int64_t n = ctx.rng.poisson(rate * nb);
    for (std::int64_t i = 0; i < n; ++i) {
        double t = s0 + ctx.rng.uniform(0, nb * ctx.bpb);
        t = voice::python_round(t * 4) / 4;
        const Chord& c = ctx.chord_at(t).chord;
        std::vector<int> pcs{};
        if (scale == nullptr) {
            pcs = c.pcs;
        } else {
            for (const int x : *scale) {
                pcs.push_back(imod(ctx.s.key + x, 12));
            }
        }
        const int pc = pcs[ctx.rng.choice(pcs.size())];
        const auto target = static_cast<int>(ctx.rng.integers(lo, hi));
        const int m = near(pc, target);
        const double v = vel * ctx.rng.uniform(.6, 1);
        const double p = ctx.rng.uniform(-.6, .6);
        ctx.note(t, inst, m, .5, v, p, bus);
    }
}

namespace {

double swing_pos(double t, double swing) {
    const double beat = std::floor(t);
    const double p = t - beat;
    const double sp = 0.5 + swing / 6.0;
    const double q = p < 0.5 ? p * 2 * sp : sp + (p - 0.5) * 2 * (1 - sp);
    return beat + q;
}

const Bus* bus_config(const Song& song, const std::string& name) {
    for (const Bus& b : song.bus) {
        if (b.name == name) {
            return &b;
        }
    }
    return nullptr;
}

double active_rms_db(const voice::Stereo& x) {
    const std::size_t n = x.left.size();
    voice::Signal mono(n);
    for (std::size_t i = 0; i < n; ++i) {
        mono[i] = (x.left[i] + x.right[i]) / 2;
    }
    const std::size_t w = voice::sample_rate;
    std::vector<double> rms{};
    voice::Signal square(w);
    for (std::size_t i = 0; i + w < n; i += w) {
        for (std::size_t j = 0; j < w; ++j) {
            square[j] = mono[i + j] * mono[i + j];
        }
        const double r = std::sqrt(voice::sum(square) / static_cast<double>(w));
        if (r > 1e-6) {
            rms.push_back(r);
        }
    }
    if (rms.empty()) {
        return -120;
    }
    std::vector<double> sorted = rms;
    std::sort(sorted.begin(), sorted.end());
    std::vector<double> top(sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2), sorted.end());
    for (double& value : top) {
        value = value * value;
    }
    return 20 * std::log10(std::sqrt(voice::sum(top) / static_cast<double>(top.size())));
}

}  // namespace

// render() and master() as one pass that keeps the loop small and quick:
// - The events are rendered in the script's order (so every note draws from _rng
//   exactly as it did there) into placements; then each bus is mixed alone, levelled,
//   and added to the dry and reverb sums, so one bus buffer exists at a time instead
//   of every bus at once.
// - master()'s circular filters are all multiplications of one loop-length spectrum:
//   the ping-pong echo's band filter, the room's convolution, the 28 Hz high-pass and
//   the presence shelf. Each channel is therefore transformed once per summed signal
//   and returned by one inverse transform (eggy_fft), instead of a forward and an
//   inverse transform around every stage. The responses are the script's, bin for
//   bin; the reordering changes only rounding (about 1e-15 of the signal).
voice::Stereo mixdown(const Song& song, const Ctx& ctx, EggySynth& synth) {
    const auto spb = static_cast<long long>(voice::python_round(sr * 60 / song.bpm));
    const auto length = static_cast<std::size_t>(voice::python_round(ctx.total * static_cast<double>(spb)));
    const auto size = static_cast<long long>(length);
    voice::Generator rng(static_cast<std::uint64_t>(song.seed + 99));
    const double sec_beat = static_cast<double>(spb) / sr;

    struct Placement {
        const voice::Signal* y;
        long long start;
        double left;
        double right;
    };
    std::vector<std::string> bus_names{};
    std::vector<std::vector<Placement>> placements{};
    std::map<std::string, long long> rr{};
    std::vector<const Event*> order{};
    for (const Event& e : ctx.ev) {
        order.push_back(&e);
    }
    std::stable_sort(order.begin(), order.end(), [](const Event* a, const Event* b) { return a->t < b->t; });
    for (const Event* e : order) {
        const double jit = rng.normal(0, e->kind == 'n' ? 0.0025 : 0.0015);
        const double tt = swing_pos(e->t, song.swing);
        const auto s = static_cast<long long>(voice::python_round(tt * static_cast<double>(spb) + jit * sr));
        const voice::Signal* y = nullptr;
        if (e->kind == 'n') {
            y = &synth.render_note(e->name, e->m, e->d * sec_beat, e->v);
        } else {
            rr[e->name] += 1;
            y = &synth.render_drum(e->name, e->v, rr[e->name]);
        }
        std::size_t b = 0;
        while (b < bus_names.size() && bus_names[b] != e->bus) {
            ++b;
        }
        if (b == bus_names.size()) {
            bus_names.push_back(e->bus);
            placements.emplace_back();
        }
        const auto [gl, gr] = voice::pan_gains(e->pan);
        placements[b].push_back({y, voice::python_modulo(s, size), gl * 1.0, gr * 1.0});  // gain 1.0: no bus sets one
    }

    const std::size_t bins = length / 2 + 1;
    const double spacing = 1.0 / sr;
    const double unit = 1.0 / (static_cast<double>(length) * spacing);
    voice::Stereo dry{voice::Signal(length, 0.0), voice::Signal(length, 0.0)};
    voice::Stereo send{voice::Signal(length, 0.0), voice::Signal(length, 0.0)};
    voice::Stereo taps{};  // the ping-pong echo's taps, before its band filter
    double echo_rev = 0;
    double echo_mix = 0;
    voice::Stereo b{voice::Signal(length), voice::Signal(length)};
    for (std::size_t bi = 0; bi < bus_names.size(); ++bi) {
        std::fill(b.left.begin(), b.left.end(), 0.0);
        std::fill(b.right.begin(), b.right.end(), 0.0);
        for (const Placement& p : placements[bi]) {  // LoopMix.add, circular
            const auto n = static_cast<long long>((*p.y).size());
            long long s = p.start;
            long long position = 0;
            while (position < n) {
                const long long k = std::min(n - position, size - s);
                for (long long i = 0; i < k; ++i) {
                    const double value = (*p.y)[static_cast<std::size_t>(position + i)];
                    b.left[static_cast<std::size_t>(s + i)] += p.left * value;
                    b.right[static_cast<std::size_t>(s + i)] += p.right * value;
                }
                position += k;
                s = 0;
            }
        }
        const Bus* cfg = bus_config(song, bus_names[bi]);
        if (cfg != nullptr && (*cfg).level.has_value()) {
            const double cur = active_rms_db(b);
            const double g = std::pow(10.0, (*(*cfg).level - cur) / 20);
            for (voice::Signal* c : {&b.left, &b.right}) {
                for (double& value : *c) {
                    value = value * g;
                }
            }
        }
        const double rev = cfg != nullptr ? (*cfg).rev : 0.15;
        if (cfg != nullptr && (*cfg).delay.has_value()) {
            // pingpong(b, round(beats * spb), fb): taps of the mono bus, alternating sides
            const auto [beats, fb] = *(*cfg).delay;
            const auto delay = static_cast<long long>(voice::python_round(beats * static_cast<double>(spb)));
            voice::Signal mono(length);
            for (std::size_t i = 0; i < length; ++i) {
                mono[i] = (b.left[i] + b.right[i]) / 2;
            }
            taps = voice::Stereo{voice::Signal(length, 0.0), voice::Signal(length, 0.0)};
            double g = 1.0;
            for (int k = 1; k <= 5; ++k) {
                g *= fb;
                voice::Signal& channel = (k + 1) % 2 == 0 ? taps.left : taps.right;
                const long long shift = voice::python_modulo(static_cast<long long>(k) * delay, size);
                for (long long i = 0; i < size; ++i) {
                    channel[static_cast<std::size_t>(i)] += g * mono[static_cast<std::size_t>(voice::python_modulo(i - shift, size))];
                }
            }
            echo_mix = (*cfg).delay_mix;
            echo_rev = rev;
        }
        for (std::size_t i = 0; i < length; ++i) {
            dry.left[i] += b.left[i];
            dry.right[i] += b.right[i];
            send.left[i] += b.left[i] * rev;
            send.right[i] += b.right[i] * rev;
        }
    }
    b = voice::Stereo{};
    placements.clear();
    synth.clear_caches();  // every note is in the sums now; the transforms need the room

    fft::RealPlan plan(length);
    std::array<std::vector<voice::Complex>, 2> echo{};  // rfft of the taps, band-filtered and mixed
    const bool has_echo = !taps.left.empty();
    for (std::size_t ch = 0; ch < 2 && has_echo; ++ch) {
        voice::Signal& t = ch == 0 ? taps.left : taps.right;
        plan.forward(t, echo[ch]);
        voice::Signal().swap(t);
        for (std::size_t k = 0; k < bins; ++k) {
            // fft_filter(.., 250, 3500, circular), then * delay_mix
            const double f = static_cast<double>(k) * unit;
            double h = 1.0;
            h /= std::sqrt(1 + std::pow(f / 3500, 4.0));
            h /= std::sqrt(1 + std::pow(250 / std::max(f, 1e-3), 4.0));
            voice::Complex& z = echo[ch][k];
            z = voice::Complex(z.real() * h * echo_mix, z.imag() * h * echo_mix);
        }
    }
    const voice::Stereo ir = voice::make_ir(2.6, song.ir.rt_low, song.ir.rt_mid, song.ir.rt_high);
    voice::Stereo out{};
    std::vector<voice::Complex> d{};
    std::vector<voice::Complex> w{};
    std::vector<voice::Complex> room{};
    for (int ch = 0; ch < 2; ++ch) {
        voice::Signal& dry_ch = ch == 0 ? dry.left : dry.right;
        voice::Signal& send_ch = ch == 0 ? send.left : send.right;
        const std::vector<voice::Complex>& e = echo[static_cast<std::size_t>(ch)];
        // the reverb send through the room: (S + E rev) * rfft(ir)
        plan.forward(send_ch, w);
        voice::Signal().swap(send_ch);
        plan.forward(ch == 0 ? ir.left : ir.right, room);
        for (std::size_t k = 0; k < bins; ++k) {
            voice::Complex sk = w[k];
            if (!e.empty()) {
                sk = voice::Complex(sk.real() + e[k].real() * echo_rev, sk.imag() + e[k].imag() * echo_rev);
            }
            w[k] = voice::Complex(sk.real() * room[k].real() - sk.imag() * room[k].imag(),
                                  sk.real() * room[k].imag() + sk.imag() * room[k].real());
        }
        std::vector<voice::Complex>().swap(room);
        plan.forward(dry_ch, d);
        voice::Signal().swap(dry_ch);
        for (std::size_t k = 0; k < bins; ++k) {
            voice::Complex dk = d[k];
            if (!e.empty()) {
                dk = voice::Complex(dk.real() + e[k].real(), dk.imag() + e[k].imag());
            }
            const voice::Complex wet = w[k];
            const double f = static_cast<double>(k) * unit;
            // fft_filter(c, 28, None, 2, circular=True)
            const double hp = 1.0 / std::sqrt(1 + std::pow(28 / std::max(f, 1e-3), 4.0));
            // presence(c, presence_db)
            const double r = 2500.0 / std::max(f, 1.0);
            const double shelf = std::pow(10.0, song.presence_db / 20 * (1 / (1 + r * r)));
            const double g = hp * shelf;
            d[k] = voice::Complex((dk.real() + wet.real() * song.wet) * g, (dk.imag() + wet.imag() * song.wet) * g);
        }
        plan.inverse(d, ch == 0 ? out.left : out.right);
        std::vector<voice::Complex>().swap(echo[static_cast<std::size_t>(ch)]);
    }
    return out;
}

}  // namespace eggy::compose
