#pragma once
// Small building blocks shared by Nature Cube's music and effects: the cues the game
// sends, the harmony the music publishes for the effects to tune to, and a few cheap
// oscillators, filters and a little room. Nothing here allocates once constructed.
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace ps_cube {

constexpr int sample_rate = 48000;
constexpr double pi = 3.14159265358979323846;

// What just happened in the game. Posted from the interface thread, taken on the audio
// thread.
enum class Cue : int { none, pick, step, erase, connect, blocked, turn, level, win };

// The cue for a sound name the puzzle view sends, or Cue::none when it is not one of the
// cube's: those still play from the shared sound files.
inline Cue cue_for(std::string_view name) {
    if (name == "nature_cube_pick")
        return Cue::pick;
    if (name == "nature_cube_trace")
        return Cue::step;
    if (name == "nature_cube_erase")
        return Cue::erase;
    if (name == "nature_cube_connect")
        return Cue::connect;
    if (name == "nature_cube_blocked")
        return Cue::blocked;
    if (name == "nature_cube_turn")
        return Cue::turn;
    if (name == "ui_new_game")
        return Cue::level;
    if (name == "stinger_win_nature_cube" || name == "stinger_topscore_nature_cube")
        return Cue::win;
    return Cue::none;
}

// A single-producer, single-consumer ring of cues. A full ring drops the newest cue.
class CueRing final {
  public:
    void push(Cue cue) noexcept {
        const unsigned head = head_.load(std::memory_order_relaxed);
        const unsigned next = (head + 1) % size;
        if (next == tail_.load(std::memory_order_acquire))
            return;
        cues_[head] = cue;
        head_.store(next, std::memory_order_release);
    }
    bool pop(Cue& cue) noexcept {
        const unsigned tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return false;
        cue = cues_[tail];
        tail_.store((tail + 1) % size, std::memory_order_release);
        return true;
    }

  private:
    static constexpr unsigned size = 64;
    std::array<Cue, size> cues_{};
    std::atomic<unsigned> head_{0};
    std::atomic<unsigned> tail_{0};
};

// The music's key and chord, as pitch-class masks (bit 0 is C). The effects read it so
// that every chime belongs to what is playing.
struct Harmony {
    std::atomic<int> tonic{0};
    std::atomic<int> scale{0};
    std::atomic<int> chord{0};
};

inline double hz_of(double midi) {
    return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
}

// A small deterministic generator, so a seed always plays the same piece.
class Dice final {
  public:
    explicit Dice(std::uint32_t seed) : state_(seed * 2654435761U + 0x6D2B79F5U) {}
    double uniform() noexcept {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return (state_ >> 8) * (1.0 / 16777216.0);
    }
    int pick(int n) noexcept {
        const int value = static_cast<int>(uniform() * n);
        return value < n ? value : n - 1;
    }
    double range(double lo, double hi) noexcept {
        return lo + (hi - lo) * uniform();
    }
    // An index drawn by weight.
    template <std::size_t N> int weighted(const std::array<double, N>& weights) noexcept {
        double total = 0;
        for (double w : weights)
            total += w;
        double x = uniform() * total;
        for (std::size_t i = 0; i < N; ++i) {
            if (x < weights[i])
                return static_cast<int>(i);
            x -= weights[i];
        }
        return static_cast<int>(N) - 1;
    }
    float noise() noexcept {
        return static_cast<float>(uniform() * 2.0 - 1.0);
    }

  private:
    std::uint32_t state_;
};

// One cycle of a sine, looked up with linear interpolation.
class SineTable final {
  public:
    SineTable() {
        for (int i = 0; i <= size; ++i)
            table_[i] = static_cast<float>(std::sin(2.0 * pi * i / size));
    }
    // phase in cycles, any value in [0, 1)
    float at(double phase) const noexcept {
        const double x = phase * size;
        const int i = static_cast<int>(x);
        const float f = static_cast<float>(x - i);
        return table_[i] + (table_[i + 1] - table_[i]) * f;
    }

  private:
    static constexpr int size = 2048;
    std::array<float, size + 1> table_{};
};

inline double wrap(double phase) noexcept {
    return phase - std::floor(phase);
}

// A band-limited sawtooth (polyBLEP), phase in cycles.
inline double saw(double& phase, double increment) noexcept {
    const double t = phase;
    phase += increment;
    if (phase >= 1.0)
        phase -= 1.0;
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
    double band() const noexcept {
        return band_;
    }
    double band_ = 0;
};

// A one-pole step toward a target, rate per sample.
inline double approach(double value, double target, double rate) noexcept {
    return value + (target - value) * rate;
}
inline double rate_for(double seconds) noexcept {
    return 1.0 - std::exp(-1.0 / (seconds * sample_rate));
}
inline double decay_for(double seconds) noexcept {
    return std::exp(-1.0 / (seconds * sample_rate));
}

// A small, soft room: four delay lines mixed by a Hadamard matrix, damped in the loop.
class Room final {
  public:
    explicit Room(double seconds, double damping_hz) {
        const int lengths[4] = {1693, 2111, 2593, 3001};
        for (int i = 0; i < 4; ++i) {
            length_[i] = lengths[i];
            gain_[i] = std::pow(10.0, -3.0 * lengths[i] / (seconds * sample_rate));
        }
        damp_ = 1.0 - std::exp(-2.0 * pi * damping_hz / sample_rate);
    }
    // Feeds one mono sample and adds the room to left and right.
    void process(double in, double& left, double& right) noexcept {
        double out[4];
        for (int i = 0; i < 4; ++i) {
            int read = write_ - length_[i];
            if (read < 0)
                read += line_size;
            out[i] = lines_[i][read];
        }
        const double h0 = out[0] + out[1], h1 = out[0] - out[1];
        const double h2 = out[2] + out[3], h3 = out[2] - out[3];
        const double mixed[4] = {(h0 + h2) * 0.5, (h1 + h3) * 0.5, (h0 - h2) * 0.5,
                                 (h1 - h3) * 0.5};
        for (int i = 0; i < 4; ++i) {
            low_[i] += (mixed[i] * gain_[i] - low_[i]) * damp_;
            lines_[i][write_] = static_cast<float>(low_[i] + in);
        }
        write_ = (write_ + 1) % line_size;
        left += out[0] + out[2] * 0.6;
        right += out[1] + out[3] * 0.6;
    }

  private:
    static constexpr int line_size = 4096;
    std::array<std::array<float, line_size>, 4> lines_{};
    int length_[4]{};
    double gain_[4]{};
    double low_[4]{};
    double damp_ = 0.3;
    int write_ = 0;
};

// Pitch classes of a mode, as semitones above the tonic.
struct Mode {
    const char* name;
    int steps[7];
};

} // namespace ps_cube
