#include "dsp.hpp"
#include "songs.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace ps {

namespace {
const ChordAt* chordAt(const std::vector<ChordAt>& cs, double beat) {
    for (const ChordAt& c : cs)
        if (beat >= c.beat - 1e-9 && beat < c.beat + c.dur - 1e-9)
            return &c;
    return nullptr;
}
std::string clean(const std::string& p) {
    std::string s;
    for (char c : p)
        if (c != ' ' && c != '|')
            s += c;
    return s;
}
} // namespace

void padChords(Track& t, const std::vector<ChordAt>& cs, int lo, int hi, int count, double vel,
               double gate) {
    std::vector<int> prev;
    for (const ChordAt& c : cs) {
        std::vector<int> v = voice(c.c, lo, hi, prev, count);
        for (std::size_t i = 0; i < v.size(); ++i)
            t.n(c.beat, c.dur * gate, v[i], vel,
                (double(i) / std::max<std::size_t>(1, v.size() - 1) - 0.5) * 0.3);
    }
}

void compChords(Track& t, const std::vector<ChordAt>& cs, int lo, int hi,
                const std::string& pattern, int count, double vel, double gate, double step,
                bool omitRoot) {
    std::string p = clean(pattern);
    if (cs.empty() || p.empty())
        return;
    std::vector<int> prev;
    const ChordAt* last = nullptr;
    std::vector<int> v;
    double start = cs.front().beat, end = cs.back().beat + cs.back().dur;
    long idx = 0;
    for (double b = start; b < end - 1e-9; b += step, ++idx) {
        char ch = p[std::size_t(idx) % p.size()];
        const ChordAt* c = chordAt(cs, b);
        if (!c)
            continue;
        if (c != last) {
            v = voice((*c).c, lo, hi, prev, count, omitRoot);
            last = c;
        }
        double vv = ch == 'X' ? 1.0 : ch == 'x' ? 0.82 : ch == 'o' ? 0.62 : ch == 'g' ? 0.4 : 0.0;
        if (ch == '=')
            continue; // sustain marker, handled by the preceding hit
        if (vv <= 0)
            continue;
        // count following '=' to lengthen the hit
        double len = step;
        for (std::size_t k = 1; k < p.size(); ++k) {
            if (p[(std::size_t(idx) + k) % p.size()] == '=')
                len += step;
            else
                break;
        }
        for (std::size_t i = 0; i < v.size(); ++i)
            t.n(b, len * gate, v[i], vel * vv,
                (double(i) / std::max<std::size_t>(1, v.size() - 1) - 0.5) * 0.25);
    }
}

void arpChords(Track& t, const std::vector<ChordAt>& cs, int lo, int hi, const std::string& pattern,
               double step, int count, double vel, double gate) {
    std::string p = clean(pattern);
    if (cs.empty() || p.empty())
        return;
    std::vector<int> prev;
    const ChordAt* last = nullptr;
    std::vector<int> v;
    double start = cs.front().beat, end = cs.back().beat + cs.back().dur;
    long idx = 0;
    for (double b = start; b < end - 1e-9; b += step, ++idx) {
        char ch = p[std::size_t(idx) % p.size()];
        const ChordAt* c = chordAt(cs, b);
        if (!c)
            continue;
        if (c != last) {
            v = voice((*c).c, lo, hi, prev, count);
            last = c;
        }
        if (ch < '0' || ch > '9')
            continue;
        int k = ch - '0';
        int pitch = v[std::size_t(k) % v.size()] + 12 * int(std::size_t(k) / v.size());
        double accent = (std::fmod(b - start, 1.0) < 1e-6) ? 1.0 : 0.85;
        t.n(b, step * gate, pitch, vel * accent, 0);
    }
}

void bassLine(Track& t, const std::vector<ChordAt>& cs, int lo, const std::string& pattern,
              double step, double vel, double gate) {
    std::string p = clean(pattern);
    if (cs.empty() || p.empty())
        return;
    double start = cs.front().beat, end = cs.back().beat + cs.back().dur;
    long idx = 0;
    for (double b = start; b < end - 1e-9; b += step, ++idx) {
        char ch = p[std::size_t(idx) % p.size()];
        const ChordAt* c = chordAt(cs, b);
        if (!c || ch == '.' || ch == '-')
            continue;
        int root = bassNote((*c).c, lo);
        int rootPc = (*c).c.root;
        struct BassTone {
            int root, rootPc, lo;
            const ChordAt* c;
            int operator()(int iv) const {
                int n = root - (((*c).c.bassPc() - rootPc + 12) % 12) + iv;
                while (n < lo)
                    n += 12;
                return n;
            }
        };
        BassTone tone{root, rootPc, lo, c};
        bool minor = std::find((*c).c.tones.begin(), (*c).c.tones.end(), 3) != (*c).c.tones.end();
        bool dom = std::find((*c).c.tones.begin(), (*c).c.tones.end(), 10) != (*c).c.tones.end();
        int fifth =
            std::find((*c).c.tones.begin(), (*c).c.tones.end(), 6) != (*c).c.tones.end() ? 6 : 7;
        int pitch = root;
        double v = vel;
        switch (ch) {
        case 'R':
            pitch = root;
            break;
        case 'r':
            pitch = root;
            v *= 0.7;
            break;
        case 'O':
            pitch = root + 12;
            break;
        case 'o':
            pitch = root + 12;
            v *= 0.7;
            break;
        case '5':
            pitch = tone(fifth);
            break;
        case 'L':
            pitch = tone(fifth) - 12;
            if (pitch < lo - 5)
                pitch += 12;
            break;
        case '3':
            pitch = tone(minor ? 3 : 4);
            break;
        case '7':
            pitch = tone(dom || minor ? 10 : 11);
            break;
        case '6':
            pitch = tone(9);
            break;
        case '2':
            pitch = tone(2);
            break;
        case '4':
            pitch = tone(5);
            break;
        case 'A': {
            const ChordAt* n = chordAt(cs, b + step + 1e-6);
            if (!n || n == c) {
                n = chordAt(cs, (*c).beat + (*c).dur + 1e-6);
            }
            if (!n)
                n = &cs.front();
            int target = bassNote((*n).c, lo);
            pitch = target - 1;
            break;
        }
        case 'B': { // approach from above
            const ChordAt* n = chordAt(cs, (*c).beat + (*c).dur + 1e-6);
            if (!n)
                n = &cs.front();
            pitch = bassNote((*n).c, lo) + 1;
            break;
        }
        default:
            continue;
        }
        // tie: count following '-'
        double len = step;
        for (std::size_t k = 1; k < p.size(); ++k) {
            if (p[(std::size_t(idx) + k) % p.size()] == '-')
                len += step;
            else
                break;
        }
        double accent = std::fmod(b - start, 1.0) < 1e-6 ? 1.0 : 0.85;
        t.n(b, len * gate, pitch, v * accent);
    }
}

void walkingBass(Track& t, const std::vector<ChordAt>& cs, int lo, int hi, unsigned seed,
                 double vel) {
    Rng rng(seed * 2654435761u + 17);
    int prev = (lo + hi) / 2;
    for (std::size_t ci = 0; ci < cs.size(); ++ci) {
        const ChordAt& c = cs[ci];
        const ChordAt& nx = cs[(ci + 1) % cs.size()];
        int beats = int(std::lround(c.dur));
        std::vector<int> tones;
        for (int iv : c.c.tones)
            tones.push_back((c.c.root + iv) % 12);
        int n = nearestPc(c.c.bassPc(), prev);
        if (n < lo)
            n += 12;
        if (n > hi)
            n -= 12;
        int target = nearestPc(nx.c.bassPc(), n);
        if (target < lo)
            target += 12;
        if (target > hi)
            target -= 12;
        for (int k = 0; k < beats; ++k) {
            int pitch;
            if (k == 0)
                pitch = n;
            else if (k == beats - 1) {
                pitch = target +
                        (rng.uni() < 0.6 ? (target > prev ? -1 : 1) : (rng.uni() < 0.5 ? -1 : 1));
                if (rng.uni() < 0.2) {
                    int fifth = 7;
                    for (int iv : nx.c.tones)
                        if (iv == 6 || iv == 8)
                            fifth = iv;
                    pitch = target + fifth - 12 * (target + fifth > hi);
                }
            } else {
                // move toward target through chord tones
                int dir = target > prev ? 1 : -1;
                int best = -1;
                int bestScore = 99;
                for (int cand = prev - 7; cand <= prev + 7; ++cand) {
                    if (cand == prev || cand < lo || cand > hi)
                        continue;
                    int pc = ((cand % 12) + 12) % 12;
                    bool ct = std::find(tones.begin(), tones.end(), pc) != tones.end();
                    if (!ct)
                        continue;
                    int score = std::abs(cand - (prev + dir * 3)) + (rng.uni() < 0.3 ? 1 : 0);
                    if (score < bestScore) {
                        bestScore = score;
                        best = cand;
                    }
                }
                if (best <
                    0) { // no chord tone in the preferred window: nearest chord tone in range
                    for (int cand = lo; cand <= hi; ++cand) {
                        int pc = ((cand % 12) + 12) % 12;
                        if (std::find(tones.begin(), tones.end(), pc) == tones.end() ||
                            cand == prev)
                            continue;
                        int score = std::abs(cand - prev);
                        if (best < 0 || score < bestScore) {
                            bestScore = score;
                            best = cand;
                        }
                    }
                }
                pitch = best;
            }
            pitch = std::max(lo - 2, std::min(hi + 2, pitch));
            double accent = (k == 0 ? 1.0 : 0.82) * (1.0 + 0.05 * rng.bip());
            t.n(c.beat + k, 0.92, pitch, vel * accent);
            prev = pitch;
        }
    }
}

double blockHarmony(Track& t, const std::vector<ChordAt>& cs, double beat, const std::string& mel,
                    int voices, double transpose, double vel, double gate, int floorPitch) {
    Track tmp;
    double end = tmp.mel(beat, mel, transpose, vel, gate);
    for (const Note& n : tmp.notes) {
        t.notes.push_back(n);
        const ChordAt* c = chordAt(cs, n.beat);
        if (!c)
            continue;
        std::vector<int> pcs;
        for (int iv : (*c).c.tones) {
            int pc = ((*c).c.root + iv) % 12;
            if (std::find(pcs.begin(), pcs.end(), pc) == pcs.end())
                pcs.push_back(pc);
        }
        int melPc = ((int(std::lround(n.pitch)) % 12) + 12) % 12;
        int q = int(std::lround(n.pitch));
        for (int k = 1; k < voices; ++k) {
            do {
                --q;
            } while (q > floorPitch - 1 &&
                     (std::find(pcs.begin(), pcs.end(), ((q % 12) + 12) % 12) == pcs.end() ||
                      ((q % 12) + 12) % 12 == melPc));
            if (q < floorPitch)
                break;
            Note h = n;
            h.pitch = q;
            h.vel = n.vel * 0.88;
            h.pan = (k % 2 ? -0.25 : 0.25) * k * 0.5;
            t.notes.push_back(h);
        }
    }
    return end;
}

void expectBeat(double got, double want, const char* what) {
    if (std::fabs(got - want) > 1e-6) {
        throw std::runtime_error(std::string("phrase length mismatch in ") + what + ": ended at " +
                                 std::to_string(got) + ", expected " + std::to_string(want));
    }
}

void copyNotes(Track& t, double b0, double b1, double shift, double transpose, double velScale) {
    std::vector<Note> add;
    for (const Note& n : t.notes)
        if (n.beat >= b0 - 1e-9 && n.beat < b1 - 1e-9) {
            Note m = n;
            m.beat += shift;
            m.pitch += transpose;
            m.vel = std::min(1.0, m.vel * velScale);
            add.push_back(m);
        }
    t.notes.insert(t.notes.end(), add.begin(), add.end());
}

std::vector<SongFactory> allSongs() {
    std::vector<SongFactory> s = {
        {"music_menu_loop", songMenu},
        {"music_klondike_loop", songKlondike},
        {"music_spider_loop", songSpider},
        {"music_freecell_loop", songFreecell},
        {"music_hearts_loop", songHearts},
        {"music_sudoku_day_loop", songSudokuDay},
        {"music_sudoku_night_loop", songSudokuNight},
        {"music_gems_loop", songGems},
        {"music_nature_cube_loop", songNatureCube},
        {"music_untangle_loop", songUntangle},
        {"music_puzzle_solve_loop", songPuzzleSolve},
    };
    for (const SongFactory& f : stingers())
        s.push_back(f);
    s.push_back({"instrument_test", songInstrumentTest}); // rendered only when requested by id
    return s;
}

} // namespace ps
