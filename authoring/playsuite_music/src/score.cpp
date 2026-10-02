#include "score.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace ps {

bool isMono(Inst i) {
    return i == Inst::Lead || i == Inst::Flute || i == Inst::Clarinet || i == Inst::MutedTrumpet ||
           i == Inst::Whistle || i == Inst::GlideBass;
}

int pitchClass(const std::string& s) {
    static const int base[7] = {9, 11, 0, 2, 4, 5, 7}; // A B C D E F G
    if (s.empty() || s[0] < 'A' || s[0] > 'G')
        throw std::runtime_error("bad pitch: " + s);
    int pc = base[s[0] - 'A'];
    for (std::size_t i = 1; i < s.size(); ++i) {
        if (s[i] == '#')
            ++pc;
        else if (s[i] == 'b')
            --pc;
        else
            break;
    }
    return (pc % 12 + 12) % 12;
}

double noteNum(const std::string& s) {
    std::size_t i = 1;
    int pc = pitchClass(s);
    int acc = 0;
    while (i < s.size() && (s[i] == '#' || s[i] == 'b')) {
        acc += s[i] == '#' ? 1 : -1;
        ++i;
    }
    if (i >= s.size())
        throw std::runtime_error("pitch needs octave: " + s);
    int octave = std::atoi(s.c_str() + i);
    static const int base[7] = {9, 11, 0, 2, 4, 5, 7};
    int natural = base[s[0] - 'A'];
    (void)pc;
    return 12 * (octave + 1) + natural + acc;
}

static double parseDur(const std::string& d) {
    if (d.empty())
        return 1.0;
    if ((d[0] >= '0' && d[0] <= '9') || d[0] == '.') {
        std::size_t slash = d.find('|');
        if (slash != std::string::npos)
            return std::atof(d.substr(0, slash).c_str()) / std::atof(d.substr(slash + 1).c_str());
        return std::atof(d.c_str());
    }
    double v = 1.0;
    switch (d[0]) {
    case 'w':
        v = 4;
        break;
    case 'h':
        v = 2;
        break;
    case 'q':
        v = 1;
        break;
    case 'e':
        v = 0.5;
        break;
    case 's':
        v = 0.25;
        break;
    case 't':
        v = 1.0 / 3.0;
        break;
    default:
        throw std::runtime_error("bad duration: " + d);
    }
    for (std::size_t i = 1; i < d.size(); ++i) {
        if (d[i] == 'd')
            v *= 1.5;
        else if (d[i] == 't')
            v *= 2.0 / 3.0;
        else if (d[i] == '+') {
            v += parseDur(d.substr(i + 1));
            break;
        }
    }
    return v;
}

double Track::mel(double beat, const std::string& text, double transpose, double vel, double gate) {
    std::istringstream in(text);
    std::string tok;
    while (in >> tok) {
        std::size_t colon = tok.rfind(':');
        std::string head = colon == std::string::npos ? tok : tok.substr(0, colon);
        double dur =
            parseDur(colon == std::string::npos ? std::string("q") : tok.substr(colon + 1));
        bool glide = false, acc = false, soft = false, stac = false, ten = false;
        if (!head.empty() && head[0] == '~') {
            glide = true;
            head = head.substr(1);
        }
        while (!head.empty()) {
            char c = head.back();
            if (c == '!')
                acc = true;
            else if (c == '\'')
                soft = true;
            else if (c == '.')
                stac = true;
            else if (c == '_')
                ten = true;
            else
                break;
            head.pop_back();
        }
        if (head == "r" || head.empty()) {
            beat += dur;
            continue;
        }
        std::vector<double> pitches;
        if (head[0] == '[') {
            std::string inner = head.substr(1, head.size() - 2);
            for (char& c : inner)
                if (c == ',')
                    c = ' ';
            std::istringstream ci(inner);
            std::string p;
            while (ci >> p)
                pitches.push_back(noteNum(p));
        } else {
            pitches.push_back(noteNum(head));
        }
        double v = vel * (acc ? 1.22 : soft ? 0.7 : 1.0);
        double g = stac ? 0.45 : ten ? 1.0 : gate;
        for (double p : pitches) {
            Note x;
            x.beat = beat;
            x.dur = dur * g;
            x.pitch = p + transpose;
            x.vel = std::min(v, 1.0);
            x.glide = glide;
            if (ten)
                x.dur = dur; // legato: touches next note
            notes.push_back(x);
        }
        beat += dur;
    }
    return beat;
}

double Track::pat(double beat, int bars, const std::string& pattern, double pitch, double step,
                  double vel, int bpb) {
    std::string s;
    for (char c : pattern)
        if (c != ' ' && c != '|')
            s += c;
    if (s.empty())
        return beat;
    double total = bars * bpb;
    double t = 0;
    std::size_t i = 0;
    while (t < total - 1e-9) {
        char c = s[i % s.size()];
        double v = 0;
        switch (c) {
        case 'X':
            v = 1.0;
            break;
        case 'x':
            v = 0.8;
            break;
        case 'o':
            v = 0.6;
            break;
        case 'g':
            v = 0.32;
            break;
        default:
            v = 0;
            break;
        }
        if (v > 0)
            n(beat + t, step, pitch, v * vel);
        t += step;
        ++i;
    }
    return beat + total;
}

Chord chord(const std::string& sym) {
    Chord c;
    c.name = sym;
    std::string s = sym;
    std::size_t slash = s.find('/');
    if (slash != std::string::npos) {
        c.bass = pitchClass(s.substr(slash + 1));
        s = s.substr(0, slash);
    }
    std::size_t i = 1;
    while (i < s.size() && (s[i] == '#' || s[i] == 'b'))
        ++i;
    c.root = pitchClass(s.substr(0, i));
    std::string q = s.substr(i);
    using V = std::vector<int>;
    if (q == "" || q == "maj")
        c.tones = V{0, 4, 7};
    else if (q == "m")
        c.tones = V{0, 3, 7};
    else if (q == "7")
        c.tones = V{0, 4, 7, 10};
    else if (q == "maj7")
        c.tones = V{0, 4, 7, 11};
    else if (q == "m7")
        c.tones = V{0, 3, 7, 10};
    else if (q == "mmaj7")
        c.tones = V{0, 3, 7, 11};
    else if (q == "6")
        c.tones = V{0, 4, 7, 9};
    else if (q == "m6")
        c.tones = V{0, 3, 7, 9};
    else if (q == "69")
        c.tones = V{0, 4, 7, 9, 14};
    else if (q == "9")
        c.tones = V{0, 4, 7, 10, 14};
    else if (q == "maj9")
        c.tones = V{0, 4, 7, 11, 14};
    else if (q == "m9")
        c.tones = V{0, 3, 7, 10, 14};
    else if (q == "add9")
        c.tones = V{0, 4, 7, 14};
    else if (q == "madd9")
        c.tones = V{0, 3, 7, 14};
    else if (q == "13")
        c.tones = V{0, 4, 10, 14, 21};
    else if (q == "7b9")
        c.tones = V{0, 4, 7, 10, 13};
    else if (q == "7#9")
        c.tones = V{0, 4, 7, 10, 15};
    else if (q == "dim")
        c.tones = V{0, 3, 6};
    else if (q == "dim7")
        c.tones = V{0, 3, 6, 9};
    else if (q == "m7b5")
        c.tones = V{0, 3, 6, 10};
    else if (q == "aug")
        c.tones = V{0, 4, 8};
    else if (q == "sus4")
        c.tones = V{0, 5, 7};
    else if (q == "sus2")
        c.tones = V{0, 2, 7};
    else if (q == "7sus4")
        c.tones = V{0, 5, 7, 10};
    else if (q == "9sus4")
        c.tones = V{0, 5, 7, 10, 14};
    else if (q == "maj7#11")
        c.tones = V{0, 4, 7, 11, 18};
    else if (q == "5")
        c.tones = V{0, 7};
    else
        throw std::runtime_error("unknown chord quality: " + sym);
    return c;
}

std::vector<ChordAt> prog(double beat, const std::string& text, int transpose) {
    std::vector<ChordAt> out;
    std::istringstream in(text);
    std::string tok;
    while (in >> tok) {
        if (tok == "|")
            continue;
        std::size_t colon = tok.rfind(':');
        double dur = colon == std::string::npos ? 4.0 : std::atof(tok.substr(colon + 1).c_str());
        Chord c = chord(tok.substr(0, colon));
        c.root = (c.root + transpose % 12 + 12) % 12;
        if (c.bass >= 0)
            c.bass = (c.bass + transpose % 12 + 12) % 12;
        out.push_back({beat, dur, c});
        beat += dur;
    }
    return out;
}

int nearestPc(int pc, int around) {
    int best = around;
    for (int d = -6; d <= 6; ++d)
        if (((around + d) % 12 + 12) % 12 == pc) {
            best = around + d;
            break;
        }
    return best;
}

int bassNote(const Chord& c, int lo) {
    int pc = c.bassPc();
    int n = lo;
    while (((n % 12) + 12) % 12 != pc)
        ++n;
    return n;
}

std::vector<int> voice(const Chord& c, int lo, int hi, std::vector<int>& prev, int count,
                       bool omitRoot) {
    // Candidate pitch classes: drop root first (if requested / too many), then fifth.
    std::vector<int> iv = c.tones;
    if (omitRoot && iv.size() > 3)
        iv.erase(iv.begin());
    while (int(iv.size()) > count) {
        std::vector<int>::iterator five = std::find(iv.begin(), iv.end(), 7);
        if (five != iv.end()) {
            iv.erase(five);
            continue;
        }
        if (iv.front() == 0) {
            iv.erase(iv.begin());
            continue;
        }
        iv.pop_back();
    }
    std::vector<int> pcs;
    for (int v : iv)
        pcs.push_back((c.root + v) % 12);
    // if count exceeds tones, double from the bottom
    std::vector<std::vector<int>> cands;
    int k = int(pcs.size());
    for (int rot = 0; rot < k; ++rot) {
        for (int start = lo; start < lo + 12; ++start) {
            if (((start % 12) + 12) % 12 != pcs[rot])
                continue;
            std::vector<int> v{start};
            for (int j = 1; j < count; ++j) {
                int pc = pcs[(rot + j) % k];
                int n = v.back() + 1;
                while (((n % 12) + 12) % 12 != pc)
                    ++n;
                v.push_back(n);
            }
            if (v.back() <= hi)
                cands.push_back(v);
        }
    }
    if (cands.empty()) { // fall back to a tight stack from lo
        std::vector<int> v;
        for (int j = 0; j < count; ++j) {
            int n = j ? v.back() + 1 : lo;
            while (((n % 12) + 12) % 12 != pcs[j % k])
                ++n;
            v.push_back(n);
        }
        prev = v;
        return v;
    }
    double best = 1e9;
    std::vector<int> pick = cands[0];
    double mid = (lo + hi) * 0.5;
    for (std::vector<int>& v : cands) {
        double score = 0;
        if (prev.size() == v.size()) {
            for (std::size_t j = 0; j < v.size(); ++j)
                score += std::abs(v[j] - prev[j]);
        } else {
            double m = 0;
            for (int x : v)
                m += x;
            m /= v.size();
            score = std::abs(m - mid) * 2;
        }
        // gentle preference to stay near the middle of the register
        double m = 0;
        for (int x : v)
            m += x;
        m /= v.size();
        score += std::abs(m - mid) * 0.25;
        if (score < best) {
            best = score;
            pick = v;
        }
    }
    prev = pick;
    return pick;
}

} // namespace ps
