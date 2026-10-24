#pragma once
// NumPy's random numbers, reproduced exactly: np.random.default_rng(seed) is a PCG64
// (XSL-RR 128/64) seeded through SeedSequence, and the distributions below are
// NumPy 1.26's own algorithms (the 256-entry ziggurats for normal and exponential
// draws, 53-bit doubles, Lemire's bounded integers with the 32-bit half buffer).
// The same seed and the same calls in the same order give the same numbers as the
// Python sound scripts, bit for bit, so a cue rendered here matches its master.
//
// Not thread-safe: one Generator belongs to one renderer.
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace voice {

// An unsigned 128-bit value as two 64-bit words (MSVC has no __int128).
struct Word128 {
    std::uint64_t high{};
    std::uint64_t low{};
};

// The complete state of a NumPy PCG64 bit generator, as Python reports it in
// `generator.bit_generator.state`: state and inc are 128-bit integers, and a
// 32-bit half of the last 64-bit output may be buffered for integer draws.
struct GeneratorState {
    Word128 state{};
    Word128 increment{};
    bool has_uint32{};
    std::uint32_t uinteger{};
};

// SeedSequence(entropy).generate_state(words, uint64) for a non-negative integer seed.
[[nodiscard]] std::vector<std::uint64_t> seed_sequence_words(std::uint64_t entropy,
                                                             std::size_t words);

// np.random.Generator(np.random.PCG64(seed)), i.e. np.random.default_rng(seed).
class Generator final {
  public:
    explicit Generator(std::uint64_t seed);
    explicit Generator(const GeneratorState& state);

    [[nodiscard]] GeneratorState state() const;
    // 64-bit outputs drawn since construction (the 32-bit buffer is not counted).
    [[nodiscard]] std::uint64_t outputs() const {
        return outputs_;
    }
    // bit_generator.advance(delta): skips delta 64-bit outputs and empties the buffer.
    void advance(std::uint64_t delta);

    // bit_generator.random_raw()
    std::uint64_t next_uint64();
    // The bit generator's buffered 32-bit draw (low half first, then high half).
    std::uint32_t next_uint32();

    // generator.random(): (next >> 11) / 2^53.
    double random();
    // generator.uniform(low, high): low + (high - low) * random().
    double uniform(double low, double high);
    double standard_normal();
    // generator.normal(loc, scale): loc + scale * standard_normal().
    double normal(double loc, double scale);
    double standard_exponential();
    double exponential(double scale);
    // generator.integers(low, high) with int64 results, high excluded.
    std::int64_t integers(std::int64_t low, std::int64_t high);
    // generator.poisson(lam).
    std::int64_t poisson(double lam);
    // generator.choice(population) without p: an index in [0, population).
    std::size_t choice(std::size_t population);
    // generator.choice(len(p), p=p): an index drawn by the weights (cumsum, searchsorted right).
    std::size_t choice(std::span<const double> weights);

    // generator.standard_normal(n) / uniform(low, high, n) into `out`, in draw order.
    void fill_standard_normal(std::span<double> out);
    void fill_uniform(double low, double high, std::span<double> out);

  private:
    Word128 state_{};
    Word128 increment_{};
    bool has_uint32_{};
    std::uint32_t uinteger_{};
    std::uint64_t outputs_{};
    void step();
};

}  // namespace voice
