#include "song_band.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace games::music {
namespace {

// A pitch class from a letter and an accidental at `text`; `used` gets the characters read.
int pitch_class(const char* text, int& used) {
    static constexpr int letters[7] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G
    used = 0;
    if (text[0] < 'A' || text[0] > 'G') return -1;
    int pc = letters[text[0] - 'A'];
    used = 1;
    if (text[1] == '#') {
        ++pc;
        ++used;
    } else if (text[1] == 'b') {
        --pc;
        ++used;
    }
    return (pc + 12) % 12;
}

struct Quality {
    const char* suffix;
    int count;
    int tones[5];
};
constexpr Quality qualities[] = {
    {"", 3, {0, 4, 7}},           {"6", 4, {0, 4, 7, 9}},        {"7", 4, {0, 4, 7, 10}},      {"maj7", 4, {0, 4, 7, 11}},
    {"9", 5, {0, 4, 7, 10, 14}},  {"add9", 4, {0, 4, 7, 14}},    {"69", 5, {0, 4, 7, 9, 14}},  {"m", 3, {0, 3, 7}},
    {"m6", 4, {0, 3, 7, 9}},      {"m7", 4, {0, 3, 7, 10}},      {"m9", 5, {0, 3, 7, 10, 14}}, {"sus", 3, {0, 5, 7}},
    {"7sus", 4, {0, 5, 7, 10}},   {"9sus", 5, {0, 5, 7, 10, 14}}, {"dim7", 4, {0, 3, 6, 9}},   {"m7b5", 4, {0, 3, 6, 10}},
};

bool parse_chord(const std::string& symbol, SongChord& chord) {
    int used = 0;
    const int root = pitch_class(symbol.c_str(), used);
    if (root < 0) return false;
    const std::size_t slash = symbol.find('/', static_cast<std::size_t>(used));
    const std::string suffix = symbol.substr(static_cast<std::size_t>(used), slash == std::string::npos ? std::string::npos : slash - static_cast<std::size_t>(used));
    bool known = false;
    for (const Quality& q : qualities) {
        if (suffix != q.suffix) continue;
        chord.count = q.count;
        for (int k = 0; k < q.count; ++k) chord.tones[static_cast<std::size_t>(k)] = q.tones[k];
        known = true;
    }
    chord.root = root;
    chord.bass = root;
    if (slash != std::string::npos) {
        int bass_used = 0;
        const int bass = pitch_class(symbol.c_str() + slash + 1, bass_used);
        if (bass < 0) return false;
        chord.bass = bass;
    }
    return known;
}

// The text's tokens ("head:beats" or "|"), split on spaces.
std::vector<std::string> tokens_of(const char* text) {
    std::vector<std::string> tokens;
    std::string token;
    for (const char* c = text; ; ++c) {
        if (*c == '\0' || *c == ' ' || *c == '\n' || *c == '\t') {
            if (!token.empty()) tokens.push_back(token);
            token.clear();
            if (*c == '\0') break;
        } else {
            token.push_back(*c);
        }
    }
    return tokens;
}

void complain(Song& song, const std::string& where, const std::string& what) { song.problems.push_back(where + ": " + what); }

// Notes written as text, at their beats from the start; returns the beats they fill.
double read_notes(Song& song, const std::string& where, const char* text, std::vector<Written>& out) {
    double beat = 0;
    for (const std::string& token : tokens_of(text)) {
        if (token == "|") {
            if (std::fabs(beat / 4 - std::round(beat / 4)) > 1e-3) complain(song, where, "a bar ends at beat " + std::to_string(beat));
            continue;
        }
        const std::size_t colon = token.find(':');
        if (colon == std::string::npos) {
            complain(song, where, "no length in " + token);
            continue;
        }
        const std::string head = token.substr(0, colon);
        const double length = std::atof(token.c_str() + colon + 1);
        Written w;
        w.beat = beat;
        w.length = length;
        beat += length;
        if (head == "r") continue;
        if (head == "*") w.role = Written::Role::stab;
        else if (head == "^") w.role = Written::Role::up;
        else if (head == "v") w.role = Written::Role::down;
        else if (head.size() == 1 && std::strchr("135678", head[0]) != nullptr) {
            w.role = Written::Role::degree;
            w.value = head[0] - '0';
        } else {
            int used = 0;
            const int pc = pitch_class(head.c_str(), used);
            if (pc < 0 || used >= static_cast<int>(head.size())) {
                complain(song, where, "not a note: " + head);
                continue;
            }
            w.value = (std::atoi(head.c_str() + used) + 1) * 12 + pc;
        }
        out.push_back(w);
    }
    // a written beat a hair either side of a whole one is that beat
    return std::round(beat * 1000) / 1000;
}

// The chord tone at or above `floor` nearest it, of the chord moved by `shift`.
int tone_above(const SongChord& c, int index, int floor, int shift) {
    const int pc = (c.root + shift + c.tones[static_cast<std::size_t>(index)]) % 12;
    return floor + ((pc - floor % 12) % 12 + 12) % 12;
}

}  // namespace

int add_section(Song& song, const char* name, const char* chords, int shift) {
    SongSection s;
    s.name = name;
    s.shift = shift;
    double beat = 0;
    for (const std::string& token : tokens_of(chords)) {
        if (token == "|") {
            if (std::fabs(beat / 4 - std::round(beat / 4)) > 1e-3) complain(song, s.name + " chords", "a bar ends at beat " + std::to_string(beat));
            continue;
        }
        const std::size_t colon = token.find(':');
        SongChord c;
        if (colon == std::string::npos || !parse_chord(token.substr(0, colon), c)) {
            complain(song, s.name + " chords", "not understood: " + token);
            continue;
        }
        const int bar = static_cast<int>(beat / 4 + 1e-6);
        if (static_cast<int>(s.chords.size()) <= bar) s.chords.resize(static_cast<std::size_t>(bar) + 1);
        s.chords[static_cast<std::size_t>(bar)].push_back({beat - bar * 4, c});
        beat += std::atof(token.c_str() + colon + 1);
    }
    s.bars = static_cast<int>(std::round(beat / 4));
    if (std::fabs(beat - s.bars * 4) > 1e-3) complain(song, s.name + " chords", "they fill " + std::to_string(beat) + " beats, not whole bars");
    s.chords.resize(static_cast<std::size_t>(s.bars));
    song.sections.push_back(s);
    return static_cast<int>(song.sections.size()) - 1;
}

void add_part(Song& song, int section, Instrument kind, const char* notes, double vel, double pan, double legato, int harmony, int floor) {
    SongSection& s = song.sections[static_cast<std::size_t>(section)];
    SongPart part;
    part.kind = kind;
    part.vel = vel;
    part.pan = pan;
    part.legato = legato;
    part.harmony = harmony;
    part.floor = floor;
    part.bars.resize(static_cast<std::size_t>(s.bars));
    const std::string where = s.name + " part " + std::to_string(s.parts.size());
    std::vector<Written> flat;
    const double length = read_notes(song, where, notes, flat);
    const double whole = s.bars * 4.0;
    if (length <= 0 || length > whole + 1e-3 || std::fabs(std::fmod(whole, length)) > 1e-3) {
        complain(song, where, "it fills " + std::to_string(length) + " beats, which do not go evenly into " + std::to_string(whole));
    } else {
        // a part shorter than its section repeats
        for (double start = 0; start < whole - 1e-6; start += length) {
            for (const Written& w : flat) {
                Written placed = w;
                const double beat = start + w.beat;
                const int bar = static_cast<int>(beat / 4 + 1e-6);
                if (bar >= s.bars) continue;
                placed.beat = beat - bar * 4;
                part.bars[static_cast<std::size_t>(bar)].push_back(placed);
            }
        }
    }
    s.parts.push_back(part);
}

int add_kit(Song& song) {
    song.kits.emplace_back();
    return static_cast<int>(song.kits.size()) - 1;
}

void add_drums(Song& song, int kit, Instrument kind, const char* hits, double vel, double pan, int pitch) {
    DrumLine line;
    line.kind = kind;
    line.hits = hits;
    line.vel = vel;
    line.pan = pan;
    line.pitch = pitch;
    if (line.hits.size() != 16 && line.hits.size() != 32) complain(song, "drums", "a pattern of " + std::to_string(line.hits.size()) + " sixteenths");
    song.kits[static_cast<std::size_t>(kit)].push_back(line);
}

void set_mix(Song& song, Instrument kind, double level, double send) { song.mix.push_back({kind, level, send}); }

SongBand::SongBand(std::uint32_t seed, Song song)
    : Band(seed * 2654435761U + 0x3C6EF372U, Room(song.room_seconds, song.room_dark_hz)), song_(std::move(song)) {
    beat_ = 60.0 / song_.bpm;
    next_bar_ = static_cast<long>(sample_rate * .2);
    next_plan_ = next_bar_;
    std::size_t parts = 0;
    for (const SongSection& s : song_.sections) parts = std::max(parts, s.parts.size());
    last_.assign(parts, 0.0);
    if (song_.order.empty()) complain(song_, "order", "no sections to play");
    for (const MixLevel& m : song_.mix) mix(m.kind, m.level, m.send);
}

void SongBand::plan() noexcept {
    play_bar();
    next_plan_ = next_bar_;
}

void SongBand::at(double beat, Instrument kind, double midi, double vel, double length, double pan) noexcept {
    double b = beat;
    if (song_.swing != .5) {
        const double whole = std::floor(b + 1e-9);
        if (std::fabs(b - whole - .5) < 1e-6) b = whole + song_.swing;
    }
    note(next_bar_, b * beat_, kind, midi, vel, length * beat_, pan);
}

const SongChord& SongBand::chord_at(const SongSection& s, int bar, double beat) const noexcept {
    const std::vector<ChordAt>& here = s.chords[static_cast<std::size_t>(bar)];
    std::size_t k = 0;
    while (k + 1 < here.size() && here[k + 1].beat <= beat + 1e-6) ++k;
    return here[k].chord;
}

void SongBand::play_bar() noexcept {
    if (song_.order.empty()) {
        next_bar_ += static_cast<long>(4 * beat_ * sample_rate);
        return;
    }
    const SongSection& s = song_.sections[static_cast<std::size_t>(song_.order[static_cast<std::size_t>(place_)])];
    const int bar = bar_;
    const bool last = bar == s.bars - 1;
    const bool stopping = last && s.stop;
    const int shift = s.shift;
    for (std::size_t p = 0; p < s.parts.size(); ++p) {
        const SongPart& part = s.parts[p];
        for (const Written& w : part.bars[static_cast<std::size_t>(bar)]) {
            if (stopping && w.beat >= 1) continue;
            const SongChord& c = chord_at(s, bar, w.beat);
            const double vel = part.vel * (std::fabs(w.beat - std::round(w.beat)) < 1e-6 ? 1.0 : .88);
            const double length = w.length * part.legato;
            switch (w.role) {
            case Written::Role::pitch: {
                const int midi = w.value + shift;
                at(w.beat, part.kind, midi, vel, length, part.pan);
                // chord tones under it, the nearest at least a third below
                int under = midi;
                for (int h = 0; h < part.harmony; ++h) {
                    int best = under - 12;
                    for (int k = 0; k < c.count; ++k) {
                        const int t = tone_above(c, k, under - 12, shift);
                        if (t <= under - 3 && t > best) best = t;
                    }
                    at(w.beat, part.kind, best, vel * .75, length, part.pan - .15 * (h + 1));
                    under = best;
                }
                break;
            }
            case Written::Role::stab: {
                // the chord above the floor, the root left to the bass when there are four tones
                const int from = c.count >= 4 ? 1 : 0;
                for (int k = from; k < c.count; ++k)
                    at(w.beat, part.kind, tone_above(c, k, part.floor, shift), vel * (1 - .04 * (k - from)), length, part.pan - .15 + .1 * (k - from));
                break;
            }
            case Written::Role::degree: {
                const int base = part.floor + ((c.bass + shift - part.floor % 12) % 12 + 12) % 12;
                const int root = part.floor + ((c.root + shift - part.floor % 12) % 12 + 12) % 12;
                int midi = base;
                switch (w.value) {
                case 3: midi = root + c.tones[1]; break;
                case 5: midi = root + c.tones[2]; break;
                case 6: midi = root + 9; break;
                case 7: midi = root + (c.count > 3 && c.tones[3] < 12 ? c.tones[3] : 10); break;
                case 8: midi = base + 12; break;
                default: break;
                }
                while (midi < base) midi += 12;
                at(w.beat, part.kind, midi, vel, length, part.pan);
                break;
            }
            case Written::Role::up: case Written::Role::down: {
                // the next chord tone up (or down) from the part's last, within two octaves of its floor
                const bool up = w.role == Written::Role::up;
                double& was = last_[p];
                if (was < part.floor || was > part.floor + 24) was = up ? part.floor - 1 : part.floor + 25;
                int best = up ? 1000 : -1000;
                for (int octave = 0; octave < 3; ++octave)
                    for (int k = 0; k < c.count; ++k) {
                        const int t = tone_above(c, k, part.floor, shift) + 12 * octave;
                        if (t > part.floor + 24) continue;
                        if (up && t > was && t < best) best = t;
                        if (!up && t < was && t > best) best = t;
                    }
                if (std::abs(best) == 1000) best = up ? tone_above(c, 0, part.floor, shift) : tone_above(c, 0, part.floor + 12, shift);
                was = best;
                at(w.beat, part.kind, best, vel, length, part.pan);
                break;
            }
            }
        }
    }
    // the drums: the kit's patterns, a fill through the last beat, a breath after a stop
    const int kit = song_.kits.empty() ? -1 : std::clamp(s.kit, 0, static_cast<int>(song_.kits.size()) - 1);
    for (std::size_t k = 0; kit >= 0 && k < song_.kits[static_cast<std::size_t>(kit)].size(); ++k) {
        const DrumLine& d = song_.kits[static_cast<std::size_t>(kit)][k];
        const int length = static_cast<int>(d.hits.size());
        if (length != 16 && length != 32) continue;
        for (int x = 0; x < 16; ++x) {
            if (stopping && x > 0) break;
            if (last && s.fill && x >= 12) break;
            const char h = d.hits[static_cast<std::size_t>((bar * 16 + x) % length)];
            const double v = h == 'X' ? 1.0 : h == 'x' ? .75 : h == 'o' ? .4 : 0.0;
            if (v > 0) at(x * .25, d.kind, d.pitch, d.vel * v, d.kind == Instrument::open_hat ? .4 : 0, d.pan);
        }
    }
    if (last && s.fill && !stopping) {
        static constexpr int toms[4] = {55, 52, 48, 45};
        for (int k = 0; k < 4; ++k) at(3 + k * .25, Instrument::tom, toms[k], .6 + .05 * k, 0, .3 - .2 * k);
    }
    if (bar == 0 && s.crash) at(0, Instrument::crash, 0, .6, 0, .25);
    if (stopping) at(0, Instrument::crash, 0, .7, 0, .25);
    ++bars_;
    next_bar_ += static_cast<long>(4 * beat_ * sample_rate);
    if (++bar_ >= s.bars) {
        bar_ = 0;
        if (++place_ >= static_cast<int>(song_.order.size())) place_ = std::clamp(song_.loop_from, 0, static_cast<int>(song_.order.size()) - 1);
    }
}

}  // namespace games::music
