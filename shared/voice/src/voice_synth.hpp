#pragma once
// The PlaySuite sound engine, synth.py, in C++: the numpy synthesis engine shared by
// authoring/audio/work/src and every games/*/audio_src/engine copy (all byte-identical),
// ported expression by expression so that a cue built here equals the master the
// Python script rendered (to about 1e-12 of full scale; see ../README.md).
//
// Free functions are synth.py's pure helpers. `Synth` is one imported synth module: it
// owns the module-level `_rng` (np.random.default_rng(12345)), the note and drum render
// caches and the wavetables, so a cue script's calls consume random numbers in the same
// order as in Python. Signals are mono doubles at 48 kHz, as numpy's float64 arrays.
//
// Not thread-safe: a Synth belongs to one renderer. Standard library only.
#include "voice_random.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace voice {

using Signal = std::vector<double>;

inline constexpr int sample_rate = 48000;
inline constexpr double pi = 3.141592653589793;

// ------------------------------------------------------------------ numpy helpers
// np.arange(n) / SR
[[nodiscard]] Signal tarr(std::size_t n);
// np.linspace(start, stop, count) with endpoint, including its exact last value.
[[nodiscard]] Signal linspace(double start, double stop, std::size_t count);
// np.mod / np.remainder for doubles: the result takes the divisor's sign.
[[nodiscard]] double remainder(double a, double b);
// np.sum of a float64 array (numpy's pairwise summation).
[[nodiscard]] double sum(std::span<const double> x);
// np.std (population).
[[nodiscard]] double standard_deviation(std::span<const double> x);
// np.abs(x).max(); 0 for an empty signal.
[[nodiscard]] double peak(std::span<const double> x);
// np.interp(x, np.arange(table.size()), table)
[[nodiscard]] double interpolate(std::span<const double> table, double x);
// np.cumsum
[[nodiscard]] Signal cumulative_sum(std::span<const double> x);
// Python's round(x) (half to even) and the % of Python integers (non-negative for b > 0).
[[nodiscard]] double python_round(double x);
[[nodiscard]] long long python_modulo(long long a, long long b);
// int(x) for a sample count: truncation toward zero.
[[nodiscard]] std::size_t samples(double seconds);

// ------------------------------------------------------------------ synth.py helpers
[[nodiscard]] double mtof(double midi);
// gate(n, dur, att=0.003, rel=0.1)
[[nodiscard]] Signal gate(std::size_t n, double dur, double att = 0.003, double rel = 0.1);
// fft_filter(x, lo, hi, order, pad, circular): `lo` / `hi` of 0 mean None.
[[nodiscard]] Signal fft_filter(std::span<const double> x, double lo, double hi, int order = 2,
                                std::size_t pad = 4096, bool circular = false);
// _partials(f, n, ratios, amps, taus, phases)
[[nodiscard]] Signal partials(double f, std::size_t n, std::span<const double> ratios,
                              std::span<const double> amps, std::span<const double> taus,
                              std::span<const double> phases = {});
// np.random.default_rng(seed).standard_normal(n)
[[nodiscard]] Signal seeded_noise(std::size_t n, std::uint64_t seed);

enum class Wave { saw, flute, clar, reed };
// _table(kind, K): 4097 entries, the last repeating the first.
[[nodiscard]] Signal wavetable(Wave kind, int harmonics);

// pan_gains(p): (left, right)
[[nodiscard]] std::pair<double, double> pan_gains(double pan);

// A stereo pair of equal-length channels, as numpy's (2, L) arrays.
struct Stereo {
    Signal left{};
    Signal right{};
};

// LoopMix: named stereo buses of exactly `length` samples, in creation order.
class LoopMix final {
  public:
    explicit LoopMix(std::size_t length, bool circular = true);
    Stereo& bus(const std::string& name);
    void add(const std::string& name, std::span<const double> y, long long start, double pan = 0.0,
             double gain = 1.0);
    [[nodiscard]] std::size_t length() const {
        return length_;
    }
    [[nodiscard]] const std::vector<std::pair<std::string, Stereo>>& buses() const {
        return buses_;
    }

  private:
    std::size_t length_{};
    bool circular_{};
    std::vector<std::pair<std::string, Stereo>> buses_{};
};

// make_ir(length, rt_low, rt_mid, rt_high, predelay, seed)
[[nodiscard]] Stereo make_ir(double length = 2.6, double rt_low = 2.0, double rt_mid = 1.6,
                             double rt_high = 0.8, double predelay = 0.018, std::uint64_t seed = 7);
// conv_reverb(x, ir, circular)
[[nodiscard]] Stereo conv_reverb(const Stereo& x, const Stereo& ir, bool circular = true);
// pingpong(x, delay_samples, fb, n, lp)
[[nodiscard]] Stereo pingpong(const Stereo& x, long long delay_samples, double fb = 0.35, int n = 5,
                              double lp = 3500);
// autopan(x, cycles, depth)
[[nodiscard]] Stereo autopan(const Stereo& x, double cycles, double depth = 0.35);

// ------------------------------------------------------------------ the module
class Synth final {
  public:
    // The module as imported: _rng = np.random.default_rng(12345).
    Synth();
    // The module with `_rng` at a recorded state (a cue that starts mid-script).
    explicit Synth(const GeneratorState& rng_state);

    // synth._rng
    [[nodiscard]] Generator& rng() {
        return rng_;
    }
    // noise(n): from _rng; noise(n, seed): from a fresh default_rng(seed).
    [[nodiscard]] Signal noise(std::size_t n);
    [[nodiscard]] Signal noise(std::size_t n, std::optional<std::uint64_t> seed);
    // wt(kind, freq, maxpart, phase0)
    [[nodiscard]] Signal wt(Wave kind, std::span<const double> freq, int maxpart = 40,
                            double phase0 = 0.0);

    // Tonal instruments (INSTR), with synth.py's defaults.
    [[nodiscard]] Signal ep(double m, double dur, double vel = 0.7, double bright = 1.0);
    [[nodiscard]] Signal pluck(double m, double dur, double vel = 0.7, double pos = 0.18,
                               double decay = 1.0, double bright = 1.0, bool body = true);
    [[nodiscard]] Signal marimba(double m, double dur, double vel = 0.7);
    [[nodiscard]] Signal vibes(double m, double dur, double vel = 0.7, double trem = 5.2);
    [[nodiscard]] Signal celesta(double m, double dur, double vel = 0.7);
    [[nodiscard]] Signal harp(double m, double dur, double vel = 0.7);
    [[nodiscard]] Signal pizz(double m, double dur, double vel = 0.7);
    [[nodiscard]] Signal bass(double m, double dur, double vel = 0.7);
    [[nodiscard]] Signal pad(double m, double dur, double vel = 0.6, double att = 0.5, double rel = 1.0,
                             double cutoff = 2200);
    [[nodiscard]] Signal flute(double m, double dur, double vel = 0.7);
    [[nodiscard]] Signal clarinet(double m, double dur, double vel = 0.7);
    [[nodiscard]] Signal reed(double m, double dur, double vel = 0.7);
    [[nodiscard]] Signal slide(double m, double dur, double vel = 0.7);
    [[nodiscard]] Signal tuba(double m, double dur, double vel = 0.7);
    [[nodiscard]] Signal felt_piano(double m, double dur, double vel = 0.6);
    [[nodiscard]] Signal glass(double m, double dur, double vel = 0.6);
    [[nodiscard]] Signal musicbox(double m, double dur, double vel = 0.6);
    [[nodiscard]] Signal logdrum(double m, double dur, double vel = 0.7);
    [[nodiscard]] Signal litho(double m, double dur, double vel = 0.7);
    [[nodiscard]] Signal analog(double m, double dur, double vel = 0.6, double bright = 0.6);
    [[nodiscard]] Signal synthbass(double m, double dur, double vel = 0.7);
    [[nodiscard]] Signal ping(double m, double dur, double vel = 0.6);
    [[nodiscard]] Signal kalimba(double m, double dur, double vel = 0.6);

    // Percussion (DRUMS and brush_swish). A missing seed draws from _rng.
    [[nodiscard]] Signal kick(double vel = 0.7, std::optional<std::uint64_t> seed = std::nullopt);
    [[nodiscard]] Signal brush_tap(double vel = 0.6, std::optional<std::uint64_t> seed = std::nullopt);
    [[nodiscard]] Signal brush_swish(double length = 0.5, double vel = 0.4,
                                     std::optional<std::uint64_t> seed = std::nullopt);
    [[nodiscard]] Signal shaker(double vel = 0.4, std::optional<std::uint64_t> seed = std::nullopt);
    [[nodiscard]] Signal hat(double vel = 0.4, std::optional<std::uint64_t> seed = std::nullopt);
    [[nodiscard]] Signal rim(double vel = 0.5, std::optional<std::uint64_t> seed = std::nullopt);
    [[nodiscard]] Signal clave(double vel = 0.5);
    [[nodiscard]] Signal woodblock(double vel = 0.5, bool high = true);
    [[nodiscard]] Signal triangle(double vel = 0.4);
    [[nodiscard]] Signal clap(double vel = 0.5, std::optional<std::uint64_t> seed = std::nullopt);
    // stone_click and rainstick build their own default_rng(seed); Python's seed=None
    // (fresh entropy) has no reproducible equivalent, so a seed is required.
    [[nodiscard]] Signal stone_click(double vel, std::uint64_t seed);
    [[nodiscard]] Signal rainstick(double vel, std::uint64_t seed);
    [[nodiscard]] Signal tick(double vel = 0.4);

    // INSTR[name](m, dur, vel) and DRUMS[name](vel, seed=seed); unknown names give an
    // empty signal. Instrument names: slide tuba ep pluck marimba vibes celesta harp pizz
    // bass pad flute clarinet reed felt glass musicbox logdrum litho analog synthbass
    // ping kalimba. Drum names: kick tap shaker hat rim clave wbhi wblo triangle clap
    // stone rainstick tick logkick loghi.
    [[nodiscard]] Signal instrument(std::string_view name, double m, double dur, double vel);
    [[nodiscard]] Signal drum(std::string_view name, double vel, std::optional<std::uint64_t> seed);
    [[nodiscard]] static bool has_instrument(std::string_view name);
    [[nodiscard]] static bool has_drum(std::string_view name);

    // render_note / render_drum: quantised and cached exactly as synth.py does, so a
    // repeated note draws nothing from _rng. The reference stays valid while this
    // Synth lives.
    const Signal& render_note(std::string_view inst, double m, double dur, double vel);
    const Signal& render_drum(std::string_view name, double vel, long long rr);
    // Puts a note in the render_note cache as if it had been rendered earlier in the
    // script, from _rng at `state`, without touching this module's _rng. A cue that
    // starts mid-script uses this for every note the script had already cached.
    void prime_note(std::string_view inst, double m, double dur, double vel,
                    const GeneratorState& state);

  private:
    struct NoteKey {
        std::string inst{};
        double m{};
        double dur{};
        double vel{};
    };
    struct NoteKeyLess {
        bool operator()(const NoteKey& a, const NoteKey& b) const;
    };
    struct DrumKey {
        std::string name{};
        double vel{};
        long long rr{};
    };
    struct DrumKeyLess {
        bool operator()(const DrumKey& a, const DrumKey& b) const;
    };
    Generator rng_;
    std::map<NoteKey, Signal, NoteKeyLess> notes_{};
    std::map<DrumKey, Signal, DrumKeyLess> drums_{};
    std::map<std::pair<int, int>, Signal> tables_{};
    const Signal& table(Wave kind, int harmonics);
    Signal breathy(Wave kind, double m, double dur, double vel, double att, double rel,
                   double vib_rate, double vib_depth, double breath, int maxpart);
};

// ------------------------------------------------------------------ cue scripts
// The write() of most games/*/audio_src/make_sfx.py scripts, before quantisation:
// x / (max|x| + 1e-12) * 10^(peak_db / 20), the last `fade_seconds` faded out and the
// first `fade_in` samples faded in (0 for scripts without that line).
[[nodiscard]] Signal master(std::span<const double> x, double peak_db, double fade_seconds = .004,
                            std::size_t fade_in = 8);
// The two-pole resonator those scripts share: y = (1 - r) x + 2 r cos(w) y1 - r^2 y2.
[[nodiscard]] Signal resonator(std::span<const double> x, double f, double bw);
// (x * 32767).astype('<i2'): truncation toward zero, as the 16-bit masters were written.
[[nodiscard]] std::vector<std::int16_t> pcm16(std::span<const double> x);

}  // namespace voice
