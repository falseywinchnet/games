#include "maze_band.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace games::music {
namespace {

constexpr double kRate = sample_rate;
using Style = MazeStyle;
constexpr double kPi = 3.14159265358979323846;
constexpr int kStyles = static_cast<int>(Style::count);
constexpr int kBuses = MazeBand::bus_count;
constexpr int kHistory = 1 << 18;   // frames of the last bars kept for playing them backwards
constexpr int kFade = 1200;         // frames crossfaded where one backwards bar meets the next
constexpr int kEcho = 1 << 17;      // frames of the lead's echo line
constexpr int kRoomLength[4] = {1427, 1777, 2137, 2593};
constexpr int kWallLength[4] = {1913, 2399, 2851, 3371};

using Kind = MazeBand::Kind;
using Bus = MazeBand::Bus;
using Part = MazeBand::Part;
using Note = MazeBand::Note;
using Phrase = MazeBand::Phrase;
using Chord = MazeBand::Chord;
using Line = MazeBand::Line;
using Section = MazeBand::Section;
using Event = MazeBand::Event;
using Voice = MazeBand::Voice;

// ------------------------------------------------------------------ the mix, per style
// Bus order: bass, drums, hats, cymbals, guitar, guitar2, arp, brass, lead, lead2, keys, mallets, mallets2.
// Levels (dB, each bus's loudness over a whole first tune), reverb sends, the room's wetness
// and decay, as the styles were mixed when the owner chose them (audio_src/score.py).
constexpr double kLevel[kStyles][kBuses] = {
    {-15, -16, -27, -27, -24, -24, -25, -22, -17, -25, -22, -22, -22},
    {-17, -19, -28, -28, -22, -22, -22, -22, -16, -23, -22, -21, -26},
    {-15, -16, -26, -28, -22, -22, -22, -21, -17, -24, -22, -22, -22},
};
constexpr double kSend[kStyles][kBuses] = {
    {.1, .12, .1, .1, .08, .08, .3, .25, .25, .3, .1, .1, .1},
    {.1, .1, .1, .1, .1, .1, .1, .2, .3, .3, .1, .2, .25},
    {.1, .1, .1, .1, .1, .1, .1, .2, .25, .3, .2, .1, .1},
};
constexpr double kWet[kStyles] = {.22, .25, .2};
constexpr double kDecay[kStyles] = {1.1, 1.3, 1.0};
// Each bus's loudness at unity gain over a first tune, measured by `maze_preview band-levels`
// (dB; -200 where a style leaves the bus unused), and the master gain that then brings the
// whole to -15 dB RMS. Re-measure after changing an instrument or an arrangement.
constexpr double kRaw[kStyles][kBuses] = {
    {-11.4, -13.7, -38.7, -40.1, -22.3, -22.1, -29.0, -19.7, -14.8, -20.0, -200.0, -200.0, -200.0},
    {-21.8, -17.1, -40.1, -41.2, -200.0, -200.0, -200.0, -21.2, -15.1, -18.7, -200.0, -14.5, -18.6},
    {-12.5, -14.8, -39.3, -41.6, -200.0, -200.0, -200.0, -15.9, -15.8, -21.8, -15.9, -200.0, -200.0},
};
constexpr double kMaster[kStyles] = {-4.5, -3.4, -4.4};
constexpr double kWallGain = .44;  // the blackout's level, through the wall

// ------------------------------------------------------------------ the styles' music
struct ThemeNote {
    int bar;
    double beat;
    int step;
    double beats;
};

// Maze Run, in A: the tune and the middle.
constexpr ThemeNote kRunVerse[] = {
    {0, 0, 0, .25}, {0, .25, 4, .25}, {0, .5, 7, .5}, {0, 1, 12, .75}, {0, 1.75, 11, .25}, {0, 2, 12, .5}, {0, 2.5, 7, .5}, {0, 3, 4, .5}, {0, 3.5, 7, .5},
    {1, 0, 11, 1}, {1, 1, 9, .25}, {1, 1.25, 11, .25}, {1, 1.5, 14, .5}, {1, 2, 16, 1.5}, {1, 3.5, 14, .5},
    {2, 0, 12, .5}, {2, .5, 9, .5}, {2, 1, 5, .5}, {2, 1.5, 9, .5}, {2, 2, 14, 1}, {2, 3, 12, .5}, {2, 3.5, 9, .5},
    {3, 0, 7, .25}, {3, .25, 9, .25}, {3, .5, 11, .25}, {3, .75, 12, .25}, {3, 1, 16, 1.5}, {3, 2.5, 14, .5}, {3, 3, 12, 1},
    {4, 0, 9, .5}, {4, .5, 12, .5}, {4, 1, 16, .5}, {4, 1.5, 14, .5}, {4, 2, 12, .5}, {4, 2.5, 9, .5}, {4, 3, 4, 1},
    {5, 0, 5, .5}, {5, .5, 9, .5}, {5, 1, 12, .5}, {5, 1.5, 17, 1}, {5, 2.5, 14, .5}, {5, 3, 12, .5}, {5, 3.5, 9, .5},
    {6, 0, 11, .75}, {6, .75, 14, .25}, {6, 1, 16, .5}, {6, 1.5, 19, 1.5}, {6, 3, 16, .5}, {6, 3.5, 14, .5},
    {7, 0, 11, .25}, {7, .25, 14, .25}, {7, .5, 16, .25}, {7, .75, 19, .25}, {7, 1, 23, 2}, {7, 3, 19, .5}, {7, 3.5, 16, .5},
};
constexpr ThemeNote kRunMiddle[] = {
    {0, 0, 17, 2}, {0, 2, 16, .5}, {0, 2.5, 14, .5}, {0, 3, 12, 1},
    {1, 0, 15, 1.5}, {1, 1.5, 14, .5}, {1, 2, 10, 1}, {1, 3, 7, 1},
    {2, 0, 14, 2}, {2, 2, 17, 1}, {2, 3, 19, 1},
    {3, 0, 17, .5}, {3, .5, 21, .5}, {3, 1, 24, 2}, {3, 3, 21, 1},
    {4, 0, 12, 1}, {4, 1, 14, .5}, {4, 1.5, 17, .5}, {4, 2, 21, 2},
    {5, 0, 19, 1}, {5, 1, 15, 1}, {5, 2, 10, 2},
    {6, 0, 14, .5}, {6, .5, 17, .5}, {6, 1, 22, 1.5}, {6, 2.5, 19, .5}, {6, 3, 17, 1},
    {7, 0, 21, 2.5}, {7, 2.5, 19, .5}, {7, 3, 16, .5}, {7, 3.5, 14, .5},
};
// Bonus Round, in C.
constexpr ThemeNote kBonusVerse[] = {
    {0, 0, 7, .5}, {0, .5, 12, .5}, {0, 1, 16, .5}, {0, 1.5, 12, .5}, {0, 2, 19, 1}, {0, 3, 16, .5}, {0, 3.5, 12, .5},
    {1, 0, 13, .5}, {1, .5, 16, .5}, {1, 1, 19, .5}, {1, 1.5, 22, .5}, {1, 2, 21, 1.5}, {1, 3.5, 19, .5},
    {2, 0, 17, 1}, {2, 1, 14, .5}, {2, 1.5, 17, .5}, {2, 2, 21, 1}, {2, 3, 17, 1},
    {3, 0, 19, .5}, {3, .5, 17, .5}, {3, 1, 14, .5}, {3, 1.5, 11, .5}, {3, 2, 7, 1}, {3, 3, 11, .5}, {3, 3.5, 14, .5},
    {4, 0, 16, .5}, {4, .5, 19, .5}, {4, 1, 24, 1}, {4, 2, 23, .5}, {4, 2.5, 21, .5}, {4, 3, 19, 1},
    {5, 0, 22, .5}, {5, .5, 21, .5}, {5, 1, 19, .5}, {5, 1.5, 16, .5}, {5, 2, 13, 1}, {5, 3, 16, 1},
    {6, 0, 14, .5}, {6, .5, 17, .5}, {6, 1, 21, .5}, {6, 1.5, 24, .5}, {6, 2, 26, 1}, {6, 3, 24, .5}, {6, 3.5, 21, .5},
    {7, 0, 23, 1}, {7, 1, 19, .5}, {7, 1.5, 17, .5}, {7, 2, 19, 1}, {7, 3, 17, .5}, {7, 3.5, 14, .5},
};
constexpr ThemeNote kBonusMiddle[] = {
    {0, 0, 21, 1.5}, {0, 1.5, 19, .5}, {0, 2, 17, 1}, {0, 3, 21, 1},
    {1, 0, 19, 2}, {1, 2, 16, 1}, {1, 3, 12, 1},
    {2, 0, 18, 1}, {2, 1, 21, 1}, {2, 2, 24, 1.5}, {2, 3.5, 21, .5},
    {3, 0, 23, 2}, {3, 2, 19, 1}, {3, 3, 17, 1},
    {4, 0, 17, .5}, {4, .5, 21, .5}, {4, 1, 24, 1}, {4, 2, 21, 1}, {4, 3, 17, 1},
    {5, 0, 16, .5}, {5, .5, 19, .5}, {5, 1, 24, 1}, {5, 2, 19, 1}, {5, 3, 16, 1},
    {6, 0, 14, 1}, {6, 1, 17, 1}, {6, 2, 19, .5}, {6, 2.5, 21, .5}, {6, 3, 23, 1},
    {7, 0, 24, 2}, {7, 2.5, 19, .5}, {7, 3, 16, .5}, {7, 3.5, 12, .5},
};
// Turbo, in B-flat.
constexpr ThemeNote kTurboVerse[] = {
    {0, 0, 0, .5}, {0, .5, 2, .5}, {0, 1, 4, .5}, {0, 1.5, 7, 1}, {0, 3, 4, .5}, {0, 3.5, 2, .5},
    {1, 0, 0, 1}, {1, 1.5, -2, .5}, {1, 2, 0, 1}, {1, 3.5, 3, .5},
    {2, 0, 5, 1}, {2, 1, 7, .5}, {2, 1.5, 9, .5}, {2, 2, 12, 1}, {2, 3, 9, 1},
    {3, 0, 9, .5}, {3, .5, 12, .5}, {3, 1, 15, 1}, {3, 2, 12, .5}, {3, 2.5, 9, .5}, {3, 3, 7, 1},
    {4, 0, 7, .5}, {4, .5, 4, .5}, {4, 1, 2, .5}, {4, 1.5, 0, 1.5}, {4, 3.5, -2, .5},
    {5, 0, 13, 1}, {5, 1, 16, 1}, {5, 2, 9, 2},
    {6, 0, 2, 1}, {6, 1, 5, .5}, {6, 1.5, 9, .5}, {6, 2, 12, 1.5}, {6, 3.5, 9, .5},
    {7, 0, 11, 1}, {7, 1, 14, 1}, {7, 2, 17, 1}, {7, 3, 14, 1},
};

Phrase phrase_of(const ThemeNote* notes, std::size_t count) {
    Phrase out(8);
    for (std::size_t i = 0; i < count; ++i) {
        const ThemeNote& n = notes[i];
        out[static_cast<std::size_t>(n.bar)].push_back(Note{n.beat, n.step, n.beats});
    }
    return out;
}

Chord chord(int root, int a, int b, int c, int d = -1) {
    Chord out{};
    out.root = root;
    out.tones = {a, b, c, d < 0 ? 0 : d};
    out.count = d < 0 ? 3 : 4;
    return out;
}

std::vector<Chord> bars_of(std::initializer_list<Chord> chords) {
    std::vector<Chord> out(chords);
    return out;
}

Line line(Kind kind, Bus bus, double vel, double pan, int octave = 0, double legato = .92, int mode = 0) {
    Line out{};
    out.kind = kind;
    out.bus = bus;
    out.vel = vel;
    out.pan = pan;
    out.octave = octave;
    out.legato = legato;
    out.mode = mode;
    return out;
}

Section section(Part part, int time, const std::vector<Chord>& bars, const Phrase& tune, std::initializer_list<Line> lines) {
    Section out{};
    out.part = part;
    out.time = time;
    out.bars = bars;
    out.tune = tune;
    out.lines = std::vector<Line>(lines);
    return out;
}

struct EarlierEvent {
    bool operator()(const Event& a, const Event& b) const { return a.at < b.at; }
};

// ------------------------------------------------------------------ the sound
double hz_of(double midi) {
    const double hz = 440.0 * std::exp2((midi - 69.0) / 12.0);
    return hz;
}

double blep(double t, double dt) {
    if (t < dt) {
        const double x = t / dt;
        return x + x - x * x - 1.0;
    }
    if (t > 1.0 - dt) {
        const double x = (t - 1.0) / dt;
        return x * x + x + x + 1.0;
    }
    return 0.0;
}

double saw_wave(double& phase, double hz) {
    const double dt = hz / kRate;
    const double value = 2.0 * phase - 1.0 - blep(phase, dt);
    phase += dt;
    if (phase >= 1.0) phase -= 1.0;
    return value;
}

double square(double& phase, double hz) {
    const double dt = hz / kRate;
    double shifted = phase + 0.5;
    if (shifted >= 1.0) shifted -= 1.0;
    const double value = (phase < 0.5 ? 1.0 : -1.0) + blep(phase, dt) - blep(shifted, dt);
    phase += dt;
    if (phase >= 1.0) phase -= 1.0;
    return value;
}

double sine(double& phase, double hz) {
    const double value = std::sin(2.0 * kPi * phase);
    phase += hz / kRate;
    if (phase >= 1.0) phase -= 1.0;
    return value;
}

// A two-pole low-pass (state-variable, Q 0.707); `a` and `b` are its state.
double low_pass(double x, double cutoff, double& a, double& b) {
    const double w = kPi * std::clamp(cutoff, 20.0, 18000.0) / kRate;
    const double g = w * (15.0 - w * w) / (15.0 - 6.0 * w * w);  // tan(w), closely enough
    const double k = 1.41421356;
    const double a1 = 1.0 / (1.0 + g * (g + k));
    const double a2 = g * a1;
    const double a3 = g * a2;
    const double v3 = x - b;
    const double v1 = a1 * a + a2 * v3;
    const double v2 = b + a2 * a + a3 * v3;
    a = 2.0 * v1 - a;
    b = 2.0 * v2 - b;
    return v2;
}

double pole(double cutoff) {
    const double coefficient = 1.0 - std::exp(-2.0 * kPi * cutoff / kRate);
    return coefficient;
}

// one-pole high-pass: x less its own low part, kept in `state`
double high_pass(double x, double coefficient, double& state) {
    state += (x - state) * coefficient;
    return x - state;
}

const double kPole90 = pole(90);
const double kPole100 = pole(100);
const double kPole120 = pole(120);
const double kPole150 = pole(150);
const double kPole250 = pole(250);
const double kPole800 = pole(800);
const double kPole900 = pole(900);
const double kPole1200 = pole(1200);
const double kPole2000 = pole(2000);
const double kPole3000 = pole(3000);
const double kPole3500 = pole(3500);
const double kPole5000 = pole(5000);
const double kPole6500 = pole(6500);
const double kPole7000 = pole(7000);
const double kRoomDamp = pole(5000);
const double kWallDamp = pole(1100);

double noise(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    const double value = static_cast<double>(state) / 2147483648.0 - 1.0;
    return value;
}

double gate(int age, int length, double attack, double release) {
    const double t = age / kRate;
    double e = 1.0;
    if (t < attack) {
        const double s = std::sin(0.5 * kPi * t / attack);
        e = s * s;
    }
    if (age >= length) {
        const double r = (age - length) / kRate;
        e *= r < release ? 0.5 * (1.0 + std::cos(kPi * r / release)) : 0.0;
    }
    return e;
}

int seconds(double s) {
    const int frames = static_cast<int>(s * kRate);
    return frames;
}

// How long a voice sounds, all told.
int life_of(Kind kind, int length, double hz) {
    switch (kind) {
        case Kind::sqbass: return length + seconds(.04);
        case Kind::lead: return length + seconds(.07);
        case Kind::gtr: return length + seconds(.05);
        case Kind::brass: return length + seconds(.1);
        case Kind::bpluck: return seconds(.5);
        case Kind::slap: return length + seconds(.05);
        case Kind::ep: return std::min(length, seconds(4)) + seconds(.18);
        case Kind::marimba: return seconds(std::min(2.2, .4 + 1.2 * std::sqrt(300.0 / hz)));
        case Kind::pizz: return std::min(length, seconds(.35)) + seconds(.08);
        case Kind::celesta: return seconds(1.8);
        case Kind::vibes: return std::min(length, seconds(3)) + seconds(.35);
        case Kind::clarinet: return length + seconds(.09);
        case Kind::flute: return length + seconds(.12);
        case Kind::kick: return seconds(.35);
        case Kind::snare: return seconds(.32);
        case Kind::hat: return seconds(.1);
        case Kind::ohat: return seconds(.4);
        case Kind::crash: return seconds(2.2);
        case Kind::tom_hi:
        case Kind::tom_mid:
        case Kind::tom_lo: return seconds(.5);
        case Kind::wood_hi:
        case Kind::wood_lo: return seconds(.15);
        case Kind::clap: return seconds(.2);
        case Kind::shaker: return seconds(.2);
        case Kind::clave: return seconds(.1);
    }
    return length;
}

// A feedback-delay room: four lines through a Householder mix, each damped in the loop.
struct RoomLines {
    std::array<std::vector<double>, 4>& lines;
    std::array<int, 4>& at;
    std::array<double, 4>& low;
    const double* feedback;
    double damp;
};

void room_step(RoomLines& room, double in, double& out_l, double& out_r) {
    double outs[4] = {};
    double sum = 0;
    for (int i = 0; i < 4; ++i) {
        const std::size_t line = static_cast<std::size_t>(i);
        const double tap = room.lines[line][static_cast<std::size_t>(room.at[line])];
        outs[i] = tap;
        room.low[line] += (tap - room.low[line]) * room.damp;
        sum += room.low[line];
    }
    const double mix = 0.5 * sum;
    for (int i = 0; i < 4; ++i) {
        const std::size_t line = static_cast<std::size_t>(i);
        std::vector<double>& delay = room.lines[line];
        delay[static_cast<std::size_t>(room.at[line])] = in + room.feedback[i] * (room.low[line] - mix);
        room.at[line] = room.at[line] + 1 >= static_cast<int>(delay.size()) ? 0 : room.at[line] + 1;
    }
    out_l = outs[0] + outs[2];
    out_r = outs[1] + outs[3];
}

void room_feedback(const int* lengths, double decay, double* feedback) {
    for (int i = 0; i < 4; ++i)
        feedback[i] = std::pow(10.0, -3.0 * lengths[i] / (decay * kRate));
}

// The energy of a room's left output for a unit impulse, to scale it to a unit-energy response.
double room_energy(const int* lengths, double decay, double damp) {
    std::array<std::vector<double>, 4> lines{};
    std::array<int, 4> at{};
    std::array<double, 4> low{};
    double feedback[4] = {};
    for (int i = 0; i < 4; ++i) lines[static_cast<std::size_t>(i)].assign(static_cast<std::size_t>(lengths[i]), 0.0);
    room_feedback(lengths, decay, feedback);
    RoomLines room{lines, at, low, feedback, damp};
    double energy = 0;
    const int frames = seconds(decay * 2.5);
    for (int n = 0; n < frames; ++n) {
        double l = 0, r = 0;
        room_step(room, n == 0 ? 1.0 : 0.0, l, r);
        energy += l * l;
    }
    return energy;
}

double db(double value) {
    const double gain = std::pow(10.0, value / 20.0);
    return gain;
}

}  // namespace

// ------------------------------------------------------------------ the band
MazeBand::MazeBand(std::uint32_t seed, Style style)
    : random_(seed | 1u, Random::Bits::whole), style_(style), wanted_style_(static_cast<int>(style)), wanted_seed_(seed) {
    for (int i = 0; i < 4; ++i) {
        room_[static_cast<std::size_t>(i)].assign(static_cast<std::size_t>(kRoomLength[i]), 0.0);
        wall_[static_cast<std::size_t>(i)].assign(static_cast<std::size_t>(kWallLength[i]), 0.0);
    }
    for (int i = 0; i < kStyles; ++i)
        room_scales_[static_cast<std::size_t>(i)] = 1.0 / std::sqrt(room_energy(kRoomLength, kDecay[i], kRoomDamp));
    room_feedback(kWallLength, 3.0, wall_feedback_);
    wall_scale_ = 1.0 / std::sqrt(room_energy(kWallLength, 3.0, kWallDamp));
    echo_l_.assign(kEcho, 0.0);
    history_.assign(static_cast<std::size_t>(kHistory) * 2, 0.0F);
    events_.reserve(512);
    start_tune();
    bar_at_ = bar_samples_;   // the first render plans the first bar
}

void MazeBand::play(Style style, std::uint32_t seed) {
    wanted_style_.store(static_cast<int>(style));
    wanted_seed_.store(seed);
    wanted_change_.fetch_add(1);
}

void MazeBand::muffle(bool on) { wanted_muffle_.store(on); }
void MazeBand::backwards(bool on) { wanted_backwards_.store(on); }

double MazeBand::bus_power(Bus bus) const {
    const int b = static_cast<int>(bus);
    const double power = measured_ > 0 ? bus_power_[static_cast<std::size_t>(b)] / static_cast<double>(measured_) : 0.0;
    return power;
}

double MazeBand::jitter(double spread) {
    const double sum = uniform() + uniform() + uniform() - 1.5;   // roughly normal, deviation 0.5
    return sum * 2.0 * spread;
}

void MazeBand::reverb_setup() {
    room_feedback(kRoomLength, kDecay[static_cast<int>(style_)], room_feedback_);
    room_scale_ = room_scales_[static_cast<std::size_t>(style_)];
}

// A new melody over `bars`, in the rhythms of a style's own (`rhythms`, one bar each), between
// `low` and `high` semitones above the key: a two-bar motif, heard three times, each time
// starting from the chord it lands on, then a free bar and a cadence that settles on the chord.
MazeBand::Phrase MazeBand::make_tune(const std::vector<Chord>& bars, const Phrase& rhythms, int low, int high) {
    static constexpr int kMajor[7] = {0, 2, 4, 5, 7, 9, 11};
    const std::size_t n = bars.size();
    Phrase out(n);
    if (n == 0 || rhythms.empty()) return out;
    const std::size_t cells = rhythms.size();
    const std::size_t motif_a = static_cast<std::size_t>(uniform() * static_cast<double>(cells)) % cells;
    const std::size_t motif_b = static_cast<std::size_t>(uniform() * static_cast<double>(cells)) % cells;
    std::vector<int> shape_a{}, shape_b{};   // the motif's notes, in scale steps from its first note
    int pitch = (low + high) / 2;
    for (std::size_t bar = 0; bar < n; ++bar) {
        const Chord& c = bars[bar];
        const std::size_t place = bar % 8;
        const bool motif = place < 6;
        const bool first_time = place < 2;
        const std::size_t cell = motif ? (place % 2 == 0 ? motif_a : motif_b)
                                       : (place == 7 ? cells - 1 : static_cast<std::size_t>(uniform() * static_cast<double>(cells)) % cells);
        const std::vector<Note>& rhythm = rhythms[cell];
        std::vector<int>& shape = place % 2 == 0 ? shape_a : shape_b;
        // the chord's tones and the scale, as pitch classes above the key
        bool chord_pc[12] = {};
        for (int i = 0; i < c.count; ++i) chord_pc[((c.root + c.tones[static_cast<std::size_t>(i)]) % 12 + 12) % 12] = true;
        bool scale_pc[12] = {};
        for (int degree : kMajor) scale_pc[degree] = true;
        if (style_ == Style::turbo) scale_pc[10] = true;   // the flat seventh of a dominant groove
        int start = pitch;
        for (std::size_t k = 0; k < rhythm.size(); ++k) {
            const Note& r = rhythm[k];
            const bool strong = r.beat == std::floor(r.beat);
            int want = pitch;
            if (motif && !first_time && k < shape.size()) {
                // the motif again: its shape from where this bar starts
                int steps = shape[k];
                want = start;
                while (steps > 0) {
                    ++want;
                    while (!scale_pc[(want % 12 + 12) % 12]) ++want;
                    --steps;
                }
                while (steps < 0) {
                    --want;
                    while (!scale_pc[(want % 12 + 12) % 12]) --want;
                    ++steps;
                }
            } else {
                // a step or two up or down the scale, now and then a leap; rising early in the
                // phrase and falling late, and turned back at the edges of the range
                const double roll = uniform();
                int steps = roll < .35 ? 1 : roll < .6 ? 2 : roll < .8 ? -1 : roll < .92 ? -2 : (uniform() < .5 ? 3 : 4);
                if (place >= 4 && uniform() < .6) steps = -steps;
                if (pitch + 2 * steps > high) steps = -std::abs(steps);
                if (pitch + 2 * steps < low) steps = std::abs(steps);
                want = pitch;
                while (steps > 0) {
                    ++want;
                    while (!scale_pc[(want % 12 + 12) % 12]) ++want;
                    --steps;
                }
                while (steps < 0) {
                    --want;
                    while (!scale_pc[(want % 12 + 12) % 12]) --want;
                    ++steps;
                }
            }
            // a strong beat (and the last note of the cadence) lands on the chord
            const bool land = strong || (place == 7 && k + 1 == rhythm.size());
            if (land) {
                int best = want;
                for (int d = 0; d <= 6; ++d) {
                    if (chord_pc[((want + d) % 12 + 12) % 12]) { best = want + d; break; }
                    if (chord_pc[((want - d) % 12 + 12) % 12]) { best = want - d; break; }
                }
                want = best;
            }
            while (want > high) want -= 12;
            while (want < low) want += 12;
            if (k == 0) start = want;
            out[bar].push_back(Note{r.beat, want, r.beats});
            pitch = want;
        }
        if (first_time) {
            // remember the motif's shape in scale steps from its first note
            shape.clear();
            for (const Note& note : out[bar]) {
                int steps = 0;
                int p = out[bar].front().step;
                while (p < note.step) { ++p; if (scale_pc[(p % 12 + 12) % 12]) ++steps; }
                while (p > note.step) { --p; if (scale_pc[(p % 12 + 12) % 12]) --steps; }
                shape.push_back(steps);
            }
        }
    }
    return out;
}

// The form of a tune in this style. The first tune of a level plays the style's themes;
// later ones make their own over the same chords and change key.
void MazeBand::start_tune() {
    const bool themes = tunes_ == 0;
    if (!themes) {
        static constexpr int kKeys[6] = {2, -2, 3, -3, 5, 0};
        int next = key_;
        while (next == key_) next = kKeys[static_cast<std::size_t>(uniform() * 6.0) % 6];
        key_ = next;
    } else {
        key_ = 0;
    }
    form_.clear();
    section_ = 0;
    section_bar_ = 0;
    if (style_ == Style::maze_run) {
        bpm_ = 98;
        swing_ = 0;
        loose_ = .003;
        const Chord a = chord(0, 0, 4, 7), e = chord(7, 0, 4, 7), d = chord(5, 0, 4, 7), fsm = chord(9, 0, 3, 7);
        const Chord c = chord(3, 0, 4, 7), g = chord(10, 0, 4, 7), f = chord(8, 0, 4, 7);
        const std::vector<Chord> verse = bars_of({a, e, d, a, fsm, d, e, e});
        const std::vector<Chord> middle = bars_of({d, c, g, d, d, c, g, d});
        const Phrase theme_v = phrase_of(kRunVerse, std::size(kRunVerse));
        const Phrase theme_m = phrase_of(kRunMiddle, std::size(kRunMiddle));
        Phrase rhythms = theme_v;
        rhythms.insert(rhythms.end(), theme_m.begin(), theme_m.end());
        const Phrase v = themes ? theme_v : make_tune(verse, rhythms, 0, 24);
        const Phrase m = themes ? theme_m : make_tune(middle, theme_m, 5, 24);
        form_.push_back(section(Part::intro, 0, bars_of({a, a, a, a}), {}, {}));
        form_.push_back(section(Part::verse, 0, verse, v, {line(Kind::lead, Bus::lead, .75, .05)}));
        form_.push_back(section(Part::verse, 1, verse, v, {line(Kind::lead, Bus::lead, .75, .05), line(Kind::lead, Bus::lead2, .45, -.3, 0, .92, 1)}));
        form_.push_back(section(Part::middle, 0, middle, m, {line(Kind::lead, Bus::lead, .75, .05)}));
        form_.push_back(section(Part::breakdown, 0, bars_of({a, d, a, e}), {}, {}));
        form_.push_back(section(Part::verse, 2, verse, v, {line(Kind::lead, Bus::lead, .78, .05), line(Kind::lead, Bus::lead2, .45, -.3, -1)}));
        form_.push_back(section(Part::middle, 1, middle, m, {line(Kind::lead, Bus::lead, .78, .05), line(Kind::lead, Bus::lead2, .4, -.3, -1)}));
        form_.push_back(section(Part::tag, 0, bars_of({f, g, a, a}), {}, {}));
    } else if (style_ == Style::bonus_round) {
        bpm_ = 124;
        swing_ = .08;
        loose_ = .003;
        const Chord c = chord(0, 0, 4, 7), a7 = chord(-3, 0, 4, 7, 10), dm = chord(2, 0, 3, 7), g7 = chord(-5, 0, 4, 7, 10);
        const Chord f = chord(-7, 0, 4, 7), d7 = chord(2, 0, 4, 7, 10), g = chord(-5, 0, 4, 7);
        const std::vector<Chord> verse = bars_of({c, a7, dm, g7, c, a7, dm, g7});
        const std::vector<Chord> middle = bars_of({f, c, d7, g7, f, c, g7, c});
        const Phrase theme_v = phrase_of(kBonusVerse, std::size(kBonusVerse));
        const Phrase theme_m = phrase_of(kBonusMiddle, std::size(kBonusMiddle));
        const Phrase v = themes ? theme_v : make_tune(verse, theme_v, 5, 24);
        const Phrase m = themes ? theme_m : make_tune(middle, theme_m, 7, 24);
        // later tunes pass the melody round in another order
        const Kind leads[3] = {Kind::celesta, Kind::clarinet, Kind::flute};
        const std::size_t turn = themes ? 0 : static_cast<std::size_t>(uniform() * 3.0) % 3;
        const Kind first = leads[turn], second = leads[(turn + 1) % 3], third = leads[(turn + 2) % 3];
        const int first_octave = first == Kind::celesta ? 1 : 0;
        form_.push_back(section(Part::intro, 0, bars_of({c, a7, dm, g7}), {}, {}));
        form_.push_back(section(Part::verse, 0, verse, v, {line(first, Bus::lead, .75, .15, first_octave, .8), line(Kind::marimba, Bus::lead2, .4, -.2, 0, .8)}));
        form_.push_back(section(Part::verse, 1, verse, v, {line(second, Bus::lead, .75, -.1, second == Kind::celesta ? 1 : 0, .85), line(Kind::celesta, Bus::lead2, .45, .35, 1, .92, 2)}));
        form_.push_back(section(Part::middle, 0, middle, m, {line(third, Bus::lead, .75, .1, third == Kind::celesta ? 1 : 0, .88)}));
        form_.push_back(section(Part::verse, 2, verse, v, {line(first, Bus::lead, .75, .15, first_octave, .8), line(Kind::flute, Bus::lead2, .5, -.25, 0, .85)}));
        form_.push_back(section(Part::stop, 0, bars_of({c, f, c, g7, c, f, c, g7}), {}, {}));
        form_.push_back(section(Part::middle, 1, middle, m, {line(second, Bus::lead, .75, -.1, second == Kind::celesta ? 1 : 0, .85), line(Kind::marimba, Bus::lead2, .5, .3, 1, .6)}));
        form_.push_back(section(Part::verse, 3, verse, v, {line(third, Bus::lead, .75, .1, third == Kind::celesta ? 1 : 0, .85), line(Kind::celesta, Bus::lead2, .55, -.25, 1, .8)}));
        form_.push_back(section(Part::tag, 0, bars_of({f, g, f, g7}), {}, {}));
    } else {
        bpm_ = 112;
        swing_ = .1;
        loose_ = .003;
        const Chord bb7 = chord(0, 0, 4, 7, 10), eb7 = chord(-7, 0, 4, 7, 10), g7 = chord(-3, 0, 4, 7, 10), cm7 = chord(2, 0, 3, 7, 10);
        const Chord f7 = chord(-5, 0, 4, 7, 10), eb = chord(-7, 0, 4, 7, 11), bb = chord(0, 0, 4, 7, 11);
        const std::vector<Chord> verse = bars_of({bb7, bb7, eb7, eb7, bb7, g7, cm7, f7});
        const std::vector<Chord> middle = bars_of({eb, eb, bb, bb, cm7, f7, bb, f7});
        const Phrase theme = phrase_of(kTurboVerse, std::size(kTurboVerse));
        const Phrase v = themes ? theme : make_tune(verse, theme, -2, 16);
        const Phrase m = themes ? theme : make_tune(middle, theme, -2, 14);
        form_.push_back(section(Part::intro, 0, bars_of({bb7, bb7, bb7, bb7}), {}, {}));
        form_.push_back(section(Part::verse, 0, verse, {}, {}));
        form_.push_back(section(Part::verse, 1, verse, v, {line(Kind::vibes, Bus::lead, .8, .15, 0, .85)}));
        form_.push_back(section(Part::middle, 0, middle, m, {line(Kind::lead, Bus::lead, .65, .05, 1)}));
        form_.push_back(section(Part::verse, 2, verse, v, {line(Kind::vibes, Bus::lead, .8, .15, 0, .85), line(Kind::lead, Bus::lead2, .45, -.3, 1)}));
        form_.push_back(section(Part::breakdown, 0, bars_of({bb7, bb7, eb7, f7}), {}, {}));
        form_.push_back(section(Part::middle, 1, middle, m, {line(Kind::lead, Bus::lead, .65, .05, 1)}));
        form_.push_back(section(Part::verse, 3, verse, v, {line(Kind::vibes, Bus::lead, .8, .15, 0, .85), line(Kind::flute, Bus::lead2, .5, -.3, 1, .85)}));
    }
    beat_samples_ = static_cast<int>(std::lround(kRate * 60.0 / bpm_));
    bar_samples_ = 4 * beat_samples_;
    echo_length_ = std::min(static_cast<int>(.75 * beat_samples_), kEcho / 4);
    reverb_setup();
}

void MazeBand::put(double beat, Kind kind, double midi, double beats, double vel, double pan, Bus bus, double slide) {
    const bool off_eighth = std::fmod(beat * 2.0, 2.0) == 1.0;
    const double swing = off_eighth ? swing_ * beat_samples_ : 0.0;
    const double at = beat * beat_samples_ + swing + jitter(loose_ * kRate);
    Event e{};
    e.at = std::max(0, static_cast<int>(std::lround(at)));
    e.kind = kind;
    e.midi = midi;
    e.length = std::max(1, static_cast<int>(beats * beat_samples_));
    e.vel = std::clamp(vel * (1.0 + jitter(.05)), .05, 1.0);
    e.pan = pan;
    e.bus = bus;
    e.slide = slide;
    events_.push_back(e);
}

void MazeBand::hit(double beat, Kind kind, double vel, double pan, Bus bus) {
    put(beat, kind, 60, .25, vel, pan, bus);
}

void MazeBand::rock_beat(bool half, bool open_hat) {
    if (half) {
        hit(0, Kind::kick, .9, 0, Bus::drums);
        hit(2, Kind::kick, .9, 0, Bus::drums);
        hit(2, Kind::snare, .8, .05, Bus::drums);
    } else {
        hit(0, Kind::kick, .9, 0, Bus::drums);
        hit(.75, Kind::kick, .7, 0, Bus::drums);
        hit(2, Kind::kick, .9, 0, Bus::drums);
        hit(2.5, Kind::kick, .7, 0, Bus::drums);
        hit(1, Kind::snare, .8, .05, Bus::drums);
        hit(3, Kind::snare, .8, .05, Bus::drums);
    }
    for (int k = 0; k < 16; ++k) {
        if (open_hat && k == 14) {
            hit(3.5, Kind::ohat, .45, .3, Bus::hats);
        } else if (k != 15 || !open_hat) {
            const double vel = k % 4 == 0 ? .42 : k % 2 == 0 ? .3 : .2;
            hit(k / 4.0, Kind::hat, vel, .3, Bus::hats);
        }
    }
}

// A tom fill across the last two beats; the crash lands on the next section's first beat.
void MazeBand::fill() {
    const Kind kinds[8] = {Kind::snare, Kind::snare, Kind::tom_hi, Kind::tom_hi, Kind::tom_mid, Kind::tom_mid, Kind::tom_lo, Kind::tom_lo};
    for (int k = 0; k < 8; ++k)
        hit(2.0 + k * .25, kinds[k], .7 * (.75 + k * .04), .4 - k * .1, Bus::drums);
}

void MazeBand::arrange_run(const Section& s, int index, const Chord& c, bool last) {
    const int root = 33 + key_ + c.root;
    const bool middle = s.part == Part::middle;
    // the bass: driving eighths with octave jumps
    static constexpr int kDrive[8] = {0, 0, 12, 0, 7, 0, 12, 7};
    static constexpr int kOctaves[8] = {0, 12, 0, 12, 0, 12, 7, 12};
    for (int k = 0; k < 8; ++k)
        put(k / 2.0, Kind::sqbass, root + (middle ? kOctaves[k] : kDrive[k]), .42, k % 2 == 0 ? .9 : .75, 0, Bus::bass);
    if (s.part == Part::breakdown) rock_beat(true, false);
    else rock_beat(middle && index % 2 == 0, true);
    if (last) fill();
    if (index == 0) hit(0, Kind::crash, .6, .3, Bus::cymbals);
    const int guitar = root < 40 ? root + 12 : root;
    if (s.part == Part::verse || s.part == Part::tag) {
        for (int k = 0; k < 8; ++k) {
            const double beats = k % 4 != 3 ? .35 : .45;
            put(k / 2.0, Kind::gtr, guitar, beats, k % 4 == 0 ? .8 : .6, -.45, Bus::guitar);
            put(k / 2.0 + .01, Kind::gtr, guitar, beats, .7, .45, Bus::guitar2);
        }
    } else if (middle) {
        put(0, Kind::gtr, guitar, 1.9, .8, -.45, Bus::guitar);
        put(2, Kind::gtr, guitar, 1.9, .75, .45, Bus::guitar2);
    }
    if (s.part == Part::breakdown || s.part == Part::intro) {
        static constexpr int kArp[4] = {0, 7, 12, 7};
        for (int k = 0; k < 16; ++k) {
            const int tone = c.tones[static_cast<std::size_t>((k / 4) % 3)] - c.tones[0];
            put(k / 4.0, Kind::bpluck, root + 36 + kArp[k % 4] + tone, .2, .55, .5 * std::sin(k), Bus::arp);
        }
    }
    if (middle || s.part == Part::tag) {
        static constexpr double kTag[5][2] = {{0, .4}, {.75, .25}, {1.5, .4}, {2.5, .25}, {3, .6}};
        static constexpr double kStab[3][2] = {{1.5, .3}, {2, .3}, {3.5, .4}};
        const int count = s.part == Part::tag ? 5 : 3;
        for (int r = 0; r < count; ++r) {
            const double beat = s.part == Part::tag ? kTag[r][0] : kStab[r][0];
            const double beats = s.part == Part::tag ? kTag[r][1] : kStab[r][1];
            for (int i = 0; i < c.count; ++i) {
                const int tone = c.tones[static_cast<std::size_t>(i)];
                put(beat, Kind::brass, root + 24 + tone, beats, .8, (tone - 4) * .1, Bus::brass);
            }
        }
    }
    if (s.part == Part::tag) {
        const int climb = 69 + key_ + c.root + (c.root == 0 ? 12 : 0);
        put(0, Kind::lead, climb, 3.5, .8, 0, Bus::lead, 2);
    }
}

void MazeBand::arrange_bonus(const Section& s, int index, const Chord& c, bool) {
    const int root = 48 + key_ + c.root;
    if (index == 0) hit(0, Kind::crash, .5, .3, Bus::cymbals);
    if (s.part == Part::stop) {
        // stop-time: a hit on every other bar, woodblocks answering
        if (index % 2 == 0) {
            put(0, Kind::pizz, root - 12, .5, 1, 0, Bus::bass);
            for (int i = 0; i < c.count; ++i) {
                const int tone = c.tones[static_cast<std::size_t>(i)];
                put(0, Kind::marimba, root + 12 + tone, .3, .9, (tone - 5) * .1, Bus::mallets);
                put(0, Kind::brass, root + 12 + tone, .3, .9, (tone - 5) * .1, Bus::brass);
            }
        }
        hit(2, index % 2 ? Kind::wood_hi : Kind::wood_lo, .6, -.3, Bus::drums);
        hit(2.5, Kind::wood_hi, .5, .3, Bus::drums);
        hit(3, Kind::clave, .5, 0, Bus::drums);
        return;
    }
    // oom-pah: pizzicato bass on 1 and 3, a marimba chord on 2 and 4
    put(0, Kind::pizz, root - 12, .9, .95, 0, Bus::bass);
    put(2, Kind::pizz, root - 12 + 7, .9, .85, 0, Bus::bass);
    if (index % 2) put(3.5, Kind::pizz, root - 12 + 4, .4, .6, 0, Bus::bass);
    for (int beat = 1; beat <= 3; beat += 2)
        for (int i = 1; i < c.count; ++i) {
            const int tone = c.tones[static_cast<std::size_t>(i)];
            put(beat, Kind::marimba, root + 12 + tone, .3, .7, (tone - 5) * .12, Bus::mallets);
        }
    if ((s.part == Part::verse && s.time >= 1) || (s.part == Part::middle && s.time >= 1)) {
        static constexpr int kRun[8] = {0, 7, 12, 7, 4, 7, 12, 7};
        for (int k = 0; k < 8; ++k)
            put(k / 2.0, Kind::marimba, root + 24 + kRun[k] - (root > 48 ? 12 : 0), .3, .45, .5, Bus::mallets2);
    }
    if (s.part != Part::intro) {
        hit(0, Kind::kick, .8, 0, Bus::drums);
        hit(2, Kind::kick, .7, 0, Bus::drums);
        hit(1, Kind::snare, .55, .05, Bus::drums);
        hit(3, Kind::snare, .6, .05, Bus::drums);
        hit(3, Kind::clap, .4, -.1, Bus::drums);
        for (int k = 0; k < 8; ++k)
            hit(k / 2.0, s.part == Part::middle ? Kind::shaker : Kind::hat, k % 2 ? .4 : .28, .35, Bus::hats);
        if (index % 4 == 3) {
            hit(3.5, Kind::wood_hi, .55, -.4, Bus::drums);
            hit(3.75, Kind::wood_hi, .45, -.4, Bus::drums);
        }
    } else {
        hit(1, Kind::wood_lo, .5, -.3, Bus::drums);
        hit(3, Kind::wood_hi, .5, .3, Bus::drums);
    }
    if (s.part == Part::middle || s.part == Part::tag) {
        static constexpr double kTag[5][2] = {{0, .3}, {.75, .3}, {1.5, .3}, {2.5, .3}, {3, .8}};
        static constexpr double kHits[2][2] = {{0, .25}, {1.5, .4}};
        const int count = s.part == Part::tag ? 5 : 2;
        for (int r = 0; r < count; ++r) {
            const double beat = s.part == Part::tag ? kTag[r][0] : kHits[r][0];
            const double beats = s.part == Part::tag ? kTag[r][1] : kHits[r][1];
            for (int i = 0; i < 3; ++i) {
                const int tone = c.tones[static_cast<std::size_t>(i)];
                put(beat, Kind::brass, root + 12 + tone, beats, .85, (tone - 4) * .1, Bus::brass);
            }
        }
    }
    if (s.part == Part::tag && index == 3) {   // the tag's last word
        const int top = 60 + key_;
        put(0, Kind::celesta, top + 19, .5, .7, .1, Bus::lead);
        put(.5, Kind::celesta, top + 24, .5, .7, .1, Bus::lead);
        put(1, Kind::celesta, top + 28, 2, .7, .1, Bus::lead);
    }
}

void MazeBand::arrange_turbo(const Section& s, int index, const Chord& c, bool) {
    const int root = 46 + key_ + c.root;
    const bool low = root > 44;
    if (index == 0) hit(0, Kind::crash, .5, .3, Bus::cymbals);
    // the bass: syncopated, popping octaves
    const double pattern[8][4] = {{0, 0, .4, .95}, {.75, 0, .2, .6}, {1, 12, .2, .8}, {1.5, 0, .4, .8},
                                  {2.25, 10, .2, .6}, {2.5, 12, .2, .8}, {3, 7, .4, .8}, {3.5, c.tones[1] == 4 ? 9.0 : 8.0, .4, .75}};
    for (int k = 0; k < 8; ++k)
        put(pattern[k][0], Kind::slap, (low ? root - 12 : root) + pattern[k][1], pattern[k][2], pattern[k][3], 0, Bus::bass);
    // Rhodes stabs on the offbeats
    if (s.part != Part::breakdown) {
        static constexpr double kOdd[4] = {.5, 1.75, 2.5, 3.75};
        static constexpr double kEven[2] = {.5, 2.5};
        const int count = index % 2 ? 4 : 2;
        for (int r = 0; r < count; ++r)
            for (int i = 0; i < c.count; ++i) {
                const int tone = c.tones[static_cast<std::size_t>(i)];
                put(index % 2 ? kOdd[r] : kEven[r], Kind::ep, root + 24 + tone - (low ? 12 : 0), .3, .7, (tone - 5) * .08, Bus::keys);
            }
    }
    // the breakbeat
    hit(0, Kind::kick, .85, 0, Bus::drums);
    hit(index % 2 ? .75 : 1.75, Kind::kick, .7, 0, Bus::drums);
    hit(2.5, Kind::kick, .7, 0, Bus::drums);
    if (s.part != Part::intro || index >= 2) {
        for (int beat = 1; beat <= 3; beat += 2) {
            hit(beat, Kind::snare, .75, .05, Bus::drums);
            hit(beat, Kind::clap, .5, -.05, Bus::drums);
        }
    }
    for (int k = 0; k < 16; ++k) {
        const double vel = k % 4 == 2 ? .38 : k % 2 == 0 ? .28 : .16;
        hit(k / 4.0, k == 6 ? Kind::ohat : Kind::hat, vel, .3, Bus::hats);
    }
    if (s.part == Part::breakdown) {
        hit(3.5, Kind::tom_hi, .6, .3, Bus::drums);
        hit(3.75, Kind::tom_lo, .6, -.3, Bus::drums);
    }
    // the brass section: answers in the later verses, the chords in the middle
    if (s.part == Part::verse && s.time >= 1 && index % 2 == 1) {
        static constexpr double kAnswer[3][2] = {{2.5, .25}, {3, .25}, {3.5, .4}};
        for (int r = 0; r < 3; ++r)
            for (int i = 1; i < c.count; ++i) {
                const int tone = c.tones[static_cast<std::size_t>(i)];
                put(kAnswer[r][0], Kind::brass, root + 24 + tone - (low ? 12 : 0), kAnswer[r][1], .85, (tone - 6) * .08, Bus::brass);
            }
    }
    if (s.part == Part::middle) {
        static constexpr double kHorns[3][2] = {{0, 1.5}, {2.5, .25}, {3, .75}};
        for (int r = 0; r < 3; ++r)
            for (int i = 0; i < c.count; ++i) {
                const int tone = c.tones[static_cast<std::size_t>(i)];
                put(kHorns[r][0], Kind::brass, root + 24 + tone - (low ? 12 : 0), kHorns[r][1], .8, (tone - 6) * .08, Bus::brass);
            }
    }
}

void MazeBand::play_lines(const Section& s, int index) {
    if (s.tune.empty()) return;
    const int base = style_ == Style::maze_run ? 69 : style_ == Style::bonus_round ? 60 : 70;
    const std::vector<Note>& notes = s.tune[static_cast<std::size_t>(index) % s.tune.size()];
    for (const Line& l : s.lines) {
        for (const Note& n : notes) {
            int step = n.step;
            double beat = n.beat;
            double beats = n.beats * l.legato;
            if (l.mode == 1) step += ((step % 12 + 12) % 12 == 0 || (step % 12 + 12) % 12 == 7) ? 4 : 3;
            if (l.mode == 2 && n.beat != std::floor(n.beat)) continue;
            if (l.mode == 3) {
                if (n.beats < 1.5) continue;
                beat += 1;
                beats = .5;
            }
            const double slide = l.kind == Kind::lead && n.beats >= 1 ? 2.0 : 0.0;
            put(beat, l.kind, base + key_ + step + 12 * l.octave, beats, l.vel, l.pan, l.bus, slide);
        }
    }
}

void MazeBand::plan_bar() {
    // keep what spilled over from the last bar (notes placed past its end)
    std::vector<Event> carried{};
    for (std::size_t i = next_event_; i < events_.size(); ++i) {
        Event e = events_[i];
        e.at = std::max(0, e.at - bar_samples_);
        carried.push_back(e);
    }
    events_.clear();
    next_event_ = 0;
    const int change = wanted_change_.load();
    if (change != change_seen_) {
        change_seen_ = change;
        style_ = static_cast<Style>(std::clamp(wanted_style_.load(), 0, kStyles - 1));
        random_.reseed(wanted_seed_.load() | 1u);
        tunes_ = 0;
        start_tune();
    } else if (section_ >= static_cast<int>(form_.size())) {
        ++tunes_;
        start_tune();
    }
    events_ = carried;
    const Section& s = form_[static_cast<std::size_t>(section_)];
    const int index = section_bar_;
    const Chord& c = s.bars[static_cast<std::size_t>(index)];
    const bool last = index + 1 == static_cast<int>(s.bars.size());
    if (style_ == Style::maze_run) arrange_run(s, index, c, last);
    else if (style_ == Style::bonus_round) arrange_bonus(s, index, c, last);
    else arrange_turbo(s, index, c, last);
    play_lines(s, index);
    std::stable_sort(events_.begin(), events_.end(), EarlierEvent{});
    ++section_bar_;
    if (section_bar_ >= static_cast<int>(s.bars.size())) {
        section_bar_ = 0;
        ++section_;
    }
    ++bars_;
}

void MazeBand::start_voice(const Event& e) {
    std::size_t pick = 0;
    int oldest = -1;
    for (std::size_t i = 0; i < voices_.size(); ++i) {
        if (!voices_[i].on) { pick = i; oldest = -2; break; }
        if (voices_[i].age > oldest) { oldest = voices_[i].age; pick = i; }
    }
    Voice& v = voices_[pick];
    v = Voice{};
    v.on = true;
    v.kind = e.kind;
    v.bus = e.bus;
    v.hz = hz_of(e.midi);
    v.vel = e.vel;
    const double angle = (std::clamp(e.pan, -1.0, 1.0) + 1.0) * kPi / 4.0;
    v.gain_l = std::cos(angle);
    v.gain_r = std::sin(angle);
    v.length = e.length;
    v.life = life_of(e.kind, e.length, v.hz);
    v.slide = e.slide;
    v.noise = static_cast<std::uint32_t>(uniform() * 4294967295.0) | 1u;
    for (double& p : v.phase) p = uniform();
    if (e.kind == Kind::gtr) {
        static constexpr double kNotes[3] = {0, 7, 12};
        for (int j = 0; j < 6; ++j)
            v.partial_hz[j] = v.hz * std::exp2(kNotes[j / 2] / 12.0 + (j % 2 ? 7.0 : -7.0) / 1200.0);
    } else if (e.kind == Kind::brass) {
        static constexpr double kCents[3] = {-9, 0, 8};
        for (int j = 0; j < 3; ++j) v.partial_hz[j] = v.hz * std::exp2(kCents[j] / 1200.0);
    } else if (e.kind == Kind::crash) {
        for (int j = 0; j < 8; ++j) v.partial_hz[j] = 3000.0 + 6000.0 * uniform();
    }
}

// One sample of a voice, before its pan.
double MazeBand::run_voice(Voice& v) {
    const double t = v.age / kRate;
    const double f = v.hz;
    const double vel = v.vel;
    double y = 0;
    switch (v.kind) {
        case Kind::sqbass: {
            const double cut = f * 2.2 + vel * (f * 12.0 - f * 2.2) * std::exp(-t / .045);
            y = saw_wave(v.phase[0], f) + .6 * square(v.phase[1], f * 1.002);
            y = low_pass(y, cut, v.s1, v.s2);
            y += 1.1 * sine(v.phase[2], f);
            y = std::tanh(1.4 * y) * gate(v.age, v.length, .002, .04) * (.4 + .6 * vel) * .6;
            break;
        }
        case Kind::lead: {
            const double vib = .006 * std::clamp((t - .18) / .2, 0.0, 1.0) * std::sin(2.0 * kPi * 5.6 * t);
            const double glide = v.slide > 0 ? std::exp2(-v.slide / 12.0 * std::exp(-t / .035)) : 1.0;
            const double hz = f * glide * (1.0 + vib);
            y = .55 * saw_wave(v.phase[0], hz) + .7 * square(v.phase[1], hz * 1.003);
            const double c0 = std::min(f * 5.0, 5000.0);
            y = low_pass(y, c0 + .5 * (7000.0 - c0) * std::exp(-t / .08), v.s1, v.s2);
            y = high_pass(y, kPole120, v.s3);
            y = std::tanh(1.6 * y) * gate(v.age, v.length, .004, .07) * (.4 + .6 * vel) * .45;
            break;
        }
        case Kind::gtr: {
            for (int j = 0; j < 6; ++j) y += saw_wave(v.phase[j], v.partial_hz[j]);
            y = std::tanh(3.5 * y);
            y = high_pass(y, kPole90, v.s3);
            y = low_pass(y, 3400, v.s1, v.s2);
            y *= (1.0 + .6 * std::exp(-t / .03)) * gate(v.age, v.length, .003, .05) * (.35 + .65 * vel) * .22;
            break;
        }
        case Kind::brass: {
            for (int j = 0; j < 3; ++j) y += saw_wave(v.phase[j], v.partial_hz[j]);
            const double swell = std::min(t / .025, 1.0) * (.55 + .45 * std::exp(-t / .12));
            const double c0 = f * 2.5, c1 = std::min(f * 9.0, 6000.0);
            y = low_pass(y, c0 + (c1 - c0) * swell * vel, v.s1, v.s2);
            y = high_pass(y, kPole100, v.s3);
            y *= gate(v.age, v.length, .012, .1) * (.4 + .6 * vel) * .3;
            break;
        }
        case Kind::bpluck: {
            y = saw_wave(v.phase[0], f) + saw_wave(v.phase[1], f * 1.006);
            y = low_pass(y, f * 1.5 + (8000.0 - f * 1.5) * std::exp(-t / .06), v.s1, v.s2);
            y = high_pass(y, kPole150, v.s3);
            y *= std::exp(-t / .16) * (.4 + .6 * vel) * .35;
            break;
        }
        case Kind::slap: {
            const double p = 2.0 * kPi * v.phase[0];
            v.phase[0] += f / kRate;
            if (v.phase[0] >= 1.0) v.phase[0] -= 1.0;
            y = std::sin(p) + .5 * std::sin(2.0 * p) * std::exp(-t / .08) + .35 * std::sin(3.0 * p + 1.0) * std::exp(-t / .03);
            if (t < .006) {
                const double click = low_pass(high_pass(noise(v.noise), kPole800, v.s3), 5000, v.s1, v.s2);
                y += .6 * click * (1.0 - t / .006);
            }
            y = std::tanh(1.3 * y) * gate(v.age, v.length, .001, .05) * (.4 + .6 * vel) * .7;
            break;
        }
        case Kind::ep: {
            const double p = 2.0 * kPi * v.phase[0];
            v.phase[0] += f / kRate;
            if (v.phase[0] >= 1.0) v.phase[0] -= 1.0;
            const double index = (1.2 + 1.5 * vel) * std::exp(-t / .4) + .2;
            y = std::sin(p + index * std::sin(p)) * std::exp(-t / 1.6);
            y += .12 * vel * sine(v.phase[1], f * 7.03) * std::exp(-t / .03);
            y *= gate(v.age, std::min(v.length, seconds(4)), .002, .18) * (.4 + .6 * vel) * .5;
            break;
        }
        case Kind::marimba: {
            const double tau = .55 * std::pow(300.0 / f, .55);
            y = sine(v.phase[0], f) * std::exp(-t / tau) + .35 * vel * sine(v.phase[1], f * 3.93) * std::exp(-t / (tau * .25))
                + .08 * sine(v.phase[2], f * 9.2) * std::exp(-t / .03);
            if (t < .004) y += .15 * noise(v.noise) * std::exp(-t / .002);
            y *= (.4 + .6 * vel) * .6;
            break;
        }
        case Kind::pizz: {
            const double scale = std::pow(220.0 / f, .3);
            for (int k = 1; k <= 6; ++k) {
                const double amp = std::abs(std::sin(kPi * k * .22)) / k;
                y += amp * sine(v.phase[k - 1], f * k) * std::exp(-t * (1.0 + .3 * k) / (.35 * scale));
            }
            if (t < .005) y += .2 * low_pass(noise(v.noise), 3000, v.s1, v.s2);
            y *= gate(v.age, std::min(v.length, seconds(.35)), .001, .08) * (.4 + .6 * vel) * .5;
            break;
        }
        case Kind::celesta: {
            static constexpr double kRatio[5] = {1, 2, 3, 4.02, 6.9};
            static constexpr double kTau[5] = {.75, .4, .12, .1, .03};
            const double amps[5] = {1, .12, .22 * vel, .06, .03 * vel};
            for (int k = 0; k < 5; ++k) y += amps[k] * sine(v.phase[k], f * kRatio[k]) * std::exp(-t / kTau[k]);
            y *= gate(v.age, v.life, .0008, .01) * (.3 + .7 * vel) * .5;
            break;
        }
        case Kind::vibes: {
            y = sine(v.phase[0], f) * std::exp(-t / 1.6) + .2 * vel * sine(v.phase[1], f * 3.98) * std::exp(-t / .35)
                + .06 * sine(v.phase[2], f * 9.9) * std::exp(-t / .08);
            y *= (1.0 + .25 * std::sin(2.0 * kPi * 5.2 * t));
            y *= gate(v.age, std::min(v.length, seconds(3)), .001, .35) * (.4 + .6 * vel) * .5;
            break;
        }
        case Kind::clarinet: {
            const double vib = .003 * std::clamp((t - .18) / .35, 0.0, 1.0) * std::sin(2.0 * kPi * 4.6 * t);
            const double hz = f * (1.0 + vib - .006 * std::exp(-t / .04));
            y = .8 * square(v.phase[0], hz) + .1 * saw_wave(v.phase[1], hz);
            y = low_pass(y, std::min(3200.0, f * 8.0), v.s1, v.s2);
            y *= gate(v.age, v.length, .03, .09) * (.35 + .65 * vel) * .4;
            break;
        }
        case Kind::flute: {
            const double vib = .0045 * std::clamp((t - .18) / .35, 0.0, 1.0) * std::sin(2.0 * kPi * 5.0 * t);
            const double hz = f * (1.0 + vib);
            const double p = 2.0 * kPi * v.phase[0];
            v.phase[0] += hz / kRate;
            if (v.phase[0] >= 1.0) v.phase[0] -= 1.0;
            y = std::sin(p) + .32 * std::sin(2.0 * p) + .11 * std::sin(3.0 * p) + .05 * std::sin(4.0 * p);
            y += .06 * low_pass(noise(v.noise), f * 3.5, v.s1, v.s2);
            y *= gate(v.age, v.length, .045, .12) * (.35 + .65 * vel) * .5;
            break;
        }
        case Kind::kick: {
            y = sine(v.phase[0], 50.0 + 110.0 * std::exp(-t / .025)) * std::exp(-t / .17);
            if (t < .004) y += .5 * high_pass(noise(v.noise), kPole1200, v.s3);
            y = std::tanh(2.0 * y) * vel * .9;
            break;
        }
        case Kind::snare: {
            const double body = sine(v.phase[0], 195.0 + 50.0 * std::exp(-t / .01)) * std::exp(-t / .06);
            const double n = noise(v.noise);
            const double wires = low_pass(high_pass(n, kPole1200, v.s3), 9000, v.s1, v.s2) * std::exp(-t / .13);
            const double crack = low_pass(high_pass(n, kPole2000, v.s4), 6000, v.s5, v.s6) * std::exp(-t / .015);
            y = .6 * body + 1.6 * wires + 1.0 * crack;
            y = std::tanh(1.3 * y) * vel * .75;
            break;
        }
        case Kind::hat:
        case Kind::ohat: {
            const double c = v.kind == Kind::hat ? kPole7000 : kPole6500;
            y = high_pass(high_pass(noise(v.noise), c, v.s1), c, v.s2);
            y *= std::exp(-t / (v.kind == Kind::hat ? .03 : .12)) * vel * 1.0;
            break;
        }
        case Kind::crash: {
            y = low_pass(high_pass(high_pass(noise(v.noise), kPole3500, v.s3), kPole3500, v.s4), 14000, v.s1, v.s2) * std::exp(-t / .7);
            double ring = 0;
            for (int j = 0; j < 8; ++j) ring += sine(v.phase[j], v.partial_hz[j]);
            y += .15 * ring / 8.0 * std::exp(-t / .5);
            y *= std::min(t / .002, 1.0) * vel * .8;
            break;
        }
        case Kind::tom_hi:
        case Kind::tom_mid:
        case Kind::tom_lo: {
            const double pitch = v.kind == Kind::tom_hi ? 180.0 : v.kind == Kind::tom_mid ? 130.0 : 95.0;
            y = sine(v.phase[0], pitch * (1.0 + .5 * std::exp(-t / .03))) * std::exp(-t / .2);
            y = std::tanh(1.5 * y) * vel * .8;
            break;
        }
        case Kind::wood_hi:
        case Kind::wood_lo: {
            const double hz = v.kind == Kind::wood_hi ? 1100.0 : 750.0;
            y = sine(v.phase[0], hz) * std::exp(-t / .03) + .3 * sine(v.phase[1], hz * 2.7) * std::exp(-t / .01);
            y *= vel * .7;
            break;
        }
        case Kind::clap: {
            double e = 0;
            static constexpr double kAt[3] = {0, .009, .017};
            for (int k = 0; k < 3; ++k)
                if (t >= kAt[k]) e += std::exp(-(t - kAt[k]) / (k < 2 ? .006 : .06));
            y = low_pass(high_pass(noise(v.noise), kPole900, v.s3), 5000, v.s1, v.s2) * e * vel * 1.2;
            break;
        }
        case Kind::shaker: {
            y = low_pass(high_pass(noise(v.noise), kPole3000, v.s3), 9000, v.s1, v.s2);
            y *= std::min(t / .01, 1.0) * std::exp(-t / .05) * vel * 1.0;
            break;
        }
        case Kind::clave: {
            y = sine(v.phase[0], 2500) * std::exp(-t / .025) * vel * .6;
            break;
        }
    }
    // every voice ends in a short fade, wherever its envelope has got to
    const int left = v.life - v.age;
    if (left < seconds(.005)) y *= std::max(0, left) / (kRate * .005);
    ++v.age;
    if (v.age >= v.life) v.on = false;
    return y;
}

void MazeBand::render_add(std::span<float> stereo, double gain) noexcept {
    const std::size_t frames = stereo.size() / 2;
    if (frames == 0) return;
    const double muffle_to = wanted_muffle_.load() ? 1.0 : 0.0;
    const double back_to = wanted_backwards_.load() ? 1.0 : 0.0;
    const double glide = 1.0 / (.3 * kRate);
    RoomLines room{room_, room_at_, room_low_, room_feedback_, kRoomDamp};
    RoomLines wall{wall_, wall_at_, wall_low_, wall_feedback_, kWallDamp};
    for (std::size_t frame = 0; frame < frames; ++frame) {
        if (bar_at_ >= bar_samples_) {
            bar_at_ = 0;
            plan_bar();
        }
        const int style_now = static_cast<int>(style_);
        while (next_event_ < events_.size() && events_[next_event_].at <= bar_at_) {
            start_voice(events_[next_event_]);
            ++next_event_;
        }
        ++bar_at_;
        bus_l_.fill(0.0);
        bus_r_.fill(0.0);
        for (Voice& v : voices_) {
            if (!v.on) continue;
            const double y = run_voice(v);
            const std::size_t b = static_cast<std::size_t>(v.bus);
            bus_l_[b] += y * v.gain_l;
            bus_r_[b] += y * v.gain_r;
        }
        // the lead's echo, bouncing left, right, left (Maze Run's)
        if (style_now == static_cast<int>(Style::maze_run)) {
            const std::size_t lead = static_cast<std::size_t>(Bus::lead);
            const double mono = .5 * (bus_l_[lead] + bus_r_[lead]);
            echo_l_[static_cast<std::size_t>(echo_at_)] = mono;
            const int d = echo_length_;
            const double t1 = echo_l_[static_cast<std::size_t>((echo_at_ - d + kEcho) % kEcho)];
            const double t2 = echo_l_[static_cast<std::size_t>((echo_at_ - 2 * d + kEcho) % kEcho)];
            const double t3 = echo_l_[static_cast<std::size_t>((echo_at_ - 3 * d + kEcho) % kEcho)];
            echo_at_ = (echo_at_ + 1) % kEcho;
            double el = .4 * t1 + .064 * t3, er = .16 * t2;
            echo_low_l_ += (el - echo_low_l_) * kPole5000;
            echo_low_r_ += (er - echo_low_r_) * kPole5000;
            el = high_pass(echo_low_l_, kPole250, echo_hp_l_);
            er = high_pass(echo_low_r_, kPole250, echo_hp_r_);
            bus_l_[lead] += .35 * el;
            bus_r_[lead] += .35 * er;
        }
        double l = 0, r = 0, send = 0;
        for (int b = 0; b < kBuses; ++b) {
            const std::size_t i = static_cast<std::size_t>(b);
            const double mono = .5 * (bus_l_[i] + bus_r_[i]);
            bus_power_[i] += mono * mono;
            const double trim = raw_ ? 1.0 : db(kLevel[style_now][b] - kRaw[style_now][b]);
            l += bus_l_[i] * trim;
            r += bus_r_[i] * trim;
            send += mono * trim * kSend[style_now][b];
        }
        ++measured_;
        double room_l = 0, room_r = 0;
        room_step(room, send, room_l, room_r);
        l += room_l * room_scale_ * kWet[style_now];
        r += room_r * room_scale_ * kWet[style_now];
        const double master = raw_ ? 1.0 : db(kMaster[style_now]);
        l = low_pass(l, 15000, top_[0], top_[1]) * master;   // nothing above 15 kHz, as the band was mixed
        r = low_pass(r, 15000, top_[2], top_[3]) * master;
        // the ceiling: the last bar again, backwards, each bar faded into the next
        const long long n = written_;
        const std::size_t slot = static_cast<std::size_t>(n % kHistory) * 2;
        history_[slot] = static_cast<float>(l);
        history_[slot + 1] = static_cast<float>(r);
        written_ = n + 1;
        if (chunk_left_ <= 0) {
            fade_at_ = read_at_;
            read_at_ = n;
            chunk_left_ = bar_samples_;
            fade_left_ = kFade;
        }
        double bl = 0, br = 0;
        if (read_at_ >= 0 && read_at_ > n - kHistory) {
            const std::size_t at = static_cast<std::size_t>(read_at_ % kHistory) * 2;
            bl = history_[at];
            br = history_[at + 1];
        }
        --read_at_;
        --chunk_left_;
        if (fade_left_ > 0) {
            double cl = 0, cr = 0;
            if (fade_at_ >= 0 && fade_at_ > n - kHistory) {
                const std::size_t at = static_cast<std::size_t>(fade_at_ % kHistory) * 2;
                cl = history_[at];
                cr = history_[at + 1];
            }
            --fade_at_;
            const double p = 1.0 - static_cast<double>(fade_left_) / kFade;
            const double in = std::sin(.5 * kPi * p), out = std::cos(.5 * kPi * p);
            bl = in * bl + out * cl;
            br = in * br + out * cr;
            --fade_left_;
        }
        back_ += (back_to - back_) * glide;
        l = (1.0 - back_) * l + back_ * bl;
        r = (1.0 - back_) * r + back_ * br;
        // the blackout: through a wall, dull and far
        muffle_ += (muffle_to - muffle_) * glide;
        if (muffle_ > .0005) {
            double dl = low_pass(l, 700, dull_[0], dull_[1]);
            double dr = low_pass(r, 700, dull_[2], dull_[3]);
            double wl = 0, wr = 0;
            room_step(wall, .25 * (dl + dr), wl, wr);
            dl = (dl + .6 * wl * wall_scale_) * kWallGain;
            dr = (dr + .6 * wr * wall_scale_) * kWallGain;
            l = (1.0 - muffle_) * l + muffle_ * dl;
            r = (1.0 - muffle_) * r + muffle_ * dr;
        }
        if (!raw_) {
            const double ceiling = .84;
            l = ceiling * std::tanh(l / ceiling);
            r = ceiling * std::tanh(r / ceiling);
        }
        const double level = gain * fade_.next();
        stereo[frame * 2] += static_cast<float>(l * level);
        stereo[frame * 2 + 1] += static_cast<float>(r * level);
    }
}

}  // namespace games::music
