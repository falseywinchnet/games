#pragma once
// The suite's music engine: the parts every live score is made of. A score (a Composer)
// writes music as it plays, sample by sample, from these: a seeded random stream, pitch
// and filter arithmetic, oscillators, a state-variable filter, a small room, a fade, and
// the harmony of chords and keys. Each game's score is in styles/ (repertoire.hpp names
// them all); the marimba band's instruments and note queue are in instruments.hpp and
// band.hpp. Pure and portable (no toolkit, no clock, no threads), so the same music
// plays in the game (live_composer.hpp hands a score to the audio device), in a test and
// in the renderer (tools/music_render.cpp).
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace games::music {

constexpr int sample_rate = 48000;
constexpr double pi = 3.14159265358979323846;

inline double mtof(double midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }
// The per-sample coefficient of a one-pole low-pass at `hz`.
inline double lowpass_coefficient(double hz) { return 1 - std::exp(-2 * pi * hz / sample_rate); }
// Equal-power gains for a pan from -1 (left) to 1 (right).
inline void pan_gains(double pan, double& left, double& right) {
    left = std::cos((pan + 1) * pi / 4);
    right = std::sin((pan + 1) * pi / 4);
}

// A score's own random stream (xorshift), so a seed always plays the same music. A draw
// takes 24 bits of the state: the high ones, or (for the scores written that way) the low
// ones (the island band, the bluegrass band) or the whole state (the maze band).
class Random {
  public:
    enum class Bits : std::uint8_t { high, low, whole };
    explicit Random(std::uint32_t state, Bits bits = Bits::high) : state_(state), bits_(bits) {}
    double uniform() noexcept {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        if (bits_ == Bits::whole) return state_ * (1.0 / 4294967296.0);
        const std::uint32_t drawn = bits_ == Bits::high ? state_ >> 8 : state_ & 0xFFFFFFU;
        return drawn * (1.0 / 16777216.0);
    }
    // Starts the stream again from `state` (a new level's tunes, say).
    void reseed(std::uint32_t state) noexcept { state_ = state; }
    int pick(int n) noexcept {
        const int value = static_cast<int>(uniform() * n);
        return value < n ? value : n - 1;
    }
    double range(double lo, double hi) noexcept { return lo + (hi - lo) * uniform(); }
    // An index drawn by weight.
    template <std::size_t N> int weighted(const std::array<double, N>& weights) noexcept {
        double total = 0;
        for (const double w : weights) total += w;
        double x = uniform() * total;
        for (std::size_t i = 0; i < N; ++i) {
            if (x < weights[i]) return static_cast<int>(i);
            x -= weights[i];
        }
        return static_cast<int>(N) - 1;
    }
    float noise() noexcept { return static_cast<float>(uniform() * 2.0 - 1.0); }

  private:
    std::uint32_t state_;
    Bits bits_;
};

// A one-pole step toward a target, `rate` per sample; the rate and the per-sample decay
// that take `seconds` to fall by e.
inline double approach(double value, double target, double rate) noexcept { return value + (target - value) * rate; }
inline double rate_for(double seconds) noexcept { return 1.0 - std::exp(-1.0 / (seconds * sample_rate)); }
inline double decay_for(double seconds) noexcept { return std::exp(-1.0 / (seconds * sample_rate)); }
inline double wrap(double phase) noexcept { return phase - std::floor(phase); }

// A band-limited sawtooth (polyBLEP), phase in cycles.
inline double saw(double& phase, double increment) noexcept {
    const double t = phase;
    phase += increment;
    if (phase >= 1.0) phase -= 1.0;
    double value = 2.0 * t - 1.0;
    if (t < increment) {
        const double x = t / increment;
        value -= x + x - x * x - 1.0;
    } else if (t > 1.0 - increment) {
        const double x = (t - 1.0) / increment;
        value -= x * x + x + x + 1.0;
    }
    return value;
}

// One cycle of a sine, looked up with linear interpolation.
class SineTable final {
  public:
    SineTable() {
        for (int i = 0; i <= size; ++i) table_[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(2.0 * pi * i / size));
    }
    // `phase` in cycles, in [0, 1).
    [[nodiscard]] float at(double phase) const noexcept {
        const double x = phase * size;
        const int i = static_cast<int>(x);
        const float f = static_cast<float>(x - i);
        return table_[static_cast<std::size_t>(i)] + (table_[static_cast<std::size_t>(i) + 1] - table_[static_cast<std::size_t>(i)]) * f;
    }

  private:
    static constexpr int size = 2048;
    std::array<float, size + 1> table_{};
};

// A state-variable filter (topology-preserving). Set it at block rate.
struct Svf {
    double ic1 = 0, ic2 = 0, a1 = 1, a2 = 0, a3 = 0, k = 1.4;
    void set(double cutoff, double q) noexcept {
        const double fc = std::fmin(cutoff, sample_rate * 0.45);
        const double g = std::tan(pi * fc / sample_rate);
        k = 1.0 / q;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    // Returns the low-pass output; band() holds the band-pass of the same sample.
    double low(double v0) noexcept {
        const double v3 = v0 - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1;
        ic2 = 2 * v2 - ic2;
        band_ = v1;
        return v2;
    }
    [[nodiscard]] double band() const noexcept { return band_; }
    double band_ = 0;
};

// A small room: four delay lines mixed by a Hadamard matrix, damped in the loop. The
// lines' lengths set its size and colour; `rt60` is the seconds its tail takes to fall by
// sixty decibels and `damping_hz` how dark each pass round the loop makes it.
class Room final {
  public:
    static constexpr std::array<int, 4> small_room = {1693, 2111, 2593, 3001};
    Room(double rt60, double damping_hz, const std::array<int, 4>& lengths = small_room) {
        for (std::size_t i = 0; i < 4; ++i) {
            length_[i] = lengths[i];
            gain_[i] = std::pow(10.0, -3.0 * lengths[i] / (rt60 * sample_rate));
        }
        damp_ = 1.0 - std::exp(-2.0 * pi * damping_hz / sample_rate);
    }
    // Feeds a sample (the left into two lines, the right into the other two) and adds the
    // room to `left` and `right`.
    void process(double in_l, double in_r, double& left, double& right) noexcept {
        double out[4];
        for (std::size_t i = 0; i < 4; ++i) {
            int read = write_ - length_[i];
            if (read < 0) read += line_size;
            out[i] = lines_[i][static_cast<std::size_t>(read)];
        }
        const double h0 = out[0] + out[1], h1 = out[0] - out[1];
        const double h2 = out[2] + out[3], h3 = out[2] - out[3];
        const double mixed[4] = {(h0 + h2) * 0.5, (h1 + h3) * 0.5, (h0 - h2) * 0.5, (h1 - h3) * 0.5};
        for (std::size_t i = 0; i < 4; ++i) {
            low_[i] += (mixed[i] * gain_[i] - low_[i]) * damp_;
            lines_[i][static_cast<std::size_t>(write_)] = static_cast<float>(low_[i] + (i % 2 ? in_r : in_l));
        }
        write_ = (write_ + 1) % line_size;
        left += out[0] + out[2] * 0.6;
        right += out[1] + out[3] * 0.6;
    }
    // A mono feed.
    void process(double in, double& left, double& right) noexcept { process(in, in, left, right); }

  private:
    static constexpr int line_size = 4096;
    std::array<std::array<float, line_size>, 4> lines_{};
    std::array<int, 4> length_{};
    std::array<double, 4> gain_{};
    std::array<double, 4> low_{};
    double damp_ = 0.3;
    int write_ = 0;
};

// Coming up from silence (when the game is opened again).
class Fade {
  public:
    void start(double seconds) noexcept {
        level_ = 0;
        rate_ = 1.0 / (std::max(.05, seconds) * sample_rate);
    }
    double next() noexcept {
        if (level_ < 1) level_ = std::min(1.0, level_ + rate_);
        return level_;
    }

  private:
    double level_ = 1, rate_ = 0;
};

// Harmony: a chord by its root's pitch class and its kind, and its tones.
enum class Quality : std::uint8_t { maj7, m7, dom7, m7b5, maj6, m6 };
struct Chord {
    int root = 0;
    Quality q = Quality::maj7;
};
constexpr int major_scale[7] = {0, 2, 4, 5, 7, 9, 11};
// The chord's tones above its root: root, third, fifth, seventh (or sixth), ninth.
constexpr int chord_steps[6][5] = {{0, 4, 7, 11, 14}, {0, 3, 7, 10, 14}, {0, 4, 7, 10, 14}, {0, 3, 6, 10, 13}, {0, 4, 7, 9, 14}, {0, 3, 7, 9, 14}};
// The midi note of a chord tone (0 root, 1 third, 2 fifth, 3 seventh, 4 ninth, 5 the octave
// and on up) at or above `floor`.
inline double chord_tone(const Chord& c, int index, int floor) {
    const int pc = (c.root + chord_steps[static_cast<int>(c.q)][index % 5]) % 12;
    const int m = floor + ((pc - floor % 12) % 12 + 12) % 12;
    return m + 12 * (index / 5);
}

// A score's key and chord as pitch-class masks (bit 0 is C), published as it plays for
// sound effects to tune to, so that every chime belongs to what is playing.
struct Harmony {
    std::atomic<int> tonic{0};
    std::atomic<int> scale{0};
    std::atomic<int> chord{0};
};

// A score: music written as it plays.
class Composer {
  public:
    virtual ~Composer() = default;
    // Adds the music to interleaved stereo frames at the given gain.
    virtual void render_add(std::span<float> stereo, double gain) noexcept = 0;
    // Comes up from silence over `seconds`.
    virtual void fade_in(double seconds) noexcept = 0;
    // A game's moment for the music to answer (a score that has none ignores it).
    virtual void cue(int /*cue*/) noexcept {}
    // For a game that hurries (Mowing's mower): play this much faster, easing into it.
    virtual void set_hurry(double /*factor*/) noexcept {}
};

}  // namespace games::music
