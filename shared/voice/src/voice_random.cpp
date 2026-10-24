#include "voice_random.hpp"

#include "voice_random_tables.hpp"

#include <cmath>
#include <limits>

namespace voice {
namespace {

// The full 128-bit product of two 64-bit words.
Word128 multiply_wide(std::uint64_t a, std::uint64_t b) {
    const std::uint64_t a_low = a & 0xffffffffull;
    const std::uint64_t a_high = a >> 32;
    const std::uint64_t b_low = b & 0xffffffffull;
    const std::uint64_t b_high = b >> 32;
    const std::uint64_t low_low = a_low * b_low;
    const std::uint64_t high_low = a_high * b_low;
    const std::uint64_t low_high = a_low * b_high;
    const std::uint64_t high_high = a_high * b_high;
    const std::uint64_t middle = (low_low >> 32) + (high_low & 0xffffffffull) + (low_high & 0xffffffffull);
    Word128 result{};
    result.low = (middle << 32) | (low_low & 0xffffffffull);
    result.high = high_high + (high_low >> 32) + (low_high >> 32) + (middle >> 32);
    return result;
}

Word128 multiply(const Word128& a, const Word128& b) {
    Word128 result = multiply_wide(a.low, b.low);
    result.high += a.high * b.low + a.low * b.high;
    return result;
}

Word128 add(const Word128& a, const Word128& b) {
    Word128 result{};
    result.low = a.low + b.low;
    const std::uint64_t carry = result.low < a.low ? 1u : 0u;
    result.high = a.high + b.high + carry;
    return result;
}

// PCG_DEFAULT_MULTIPLIER_128
constexpr Word128 pcg_multiplier{2549297995355413924ull, 4865540595714422341ull};

// SeedSequence's hashing constants (numpy/random/bit_generator.pyx).
constexpr std::uint32_t init_a = 0x43b0d7e5u;
constexpr std::uint32_t mult_a = 0x931e8875u;
constexpr std::uint32_t init_b = 0x8b51f9ddu;
constexpr std::uint32_t mult_b = 0x58f38dedu;
constexpr std::uint32_t mix_mult_l = 0xca01f9ddu;
constexpr std::uint32_t mix_mult_r = 0x4973f715u;
constexpr std::uint32_t xshift = 16;
constexpr std::size_t pool_size = 4;

std::uint32_t hashmix(std::uint32_t value, std::uint32_t& hash_const) {
    value ^= hash_const;
    hash_const *= mult_a;
    value *= hash_const;
    value ^= value >> xshift;
    return value;
}

std::uint32_t mix(std::uint32_t x, std::uint32_t y) {
    std::uint32_t result = mix_mult_l * x - mix_mult_r * y;
    result ^= result >> xshift;
    return result;
}

double next_double_of(std::uint64_t bits) {
    const double result = static_cast<double>(bits >> 11) * (1.0 / 9007199254740992.0);
    return result;
}

// random_loggam (numpy/random/src/distributions/distributions.c).
double loggam(double x) {
    static constexpr double a[10] = {8.333333333333333e-02, -2.777777777777778e-03,
                                     7.936507936507937e-04, -5.952380952380952e-04,
                                     8.417508417508418e-04, -1.917526917526918e-03,
                                     6.410256410256410e-03, -2.955065359477124e-02,
                                     1.796443723688307e-01, -1.39243221690590e+00};
    if (x == 1.0 || x == 2.0) {
        return 0.0;
    }
    std::int64_t n = 0;
    if (x < 7.0) {
        n = static_cast<std::int64_t>(7 - x);
    }
    double x0 = x + static_cast<double>(n);
    const double x2 = (1.0 / x0) * (1.0 / x0);
    const double lg2pi = 1.8378770664093453e+00;
    double gl0 = a[9];
    for (int k = 8; k >= 0; --k) {
        gl0 *= x2;
        gl0 += a[k];
    }
    double gl = gl0 / x0 + 0.5 * lg2pi + (x0 - 0.5) * std::log(x0) - x0;
    if (x < 7.0) {
        for (std::int64_t k = 1; k <= n; ++k) {
            gl -= std::log(x0 - 1.0);
            x0 -= 1.0;
        }
    }
    return gl;
}

}  // namespace

std::vector<std::uint64_t> seed_sequence_words(std::uint64_t entropy, std::size_t words) {
    // _coerce_to_uint32_array: little-endian 32-bit words, [0] for zero.
    std::vector<std::uint32_t> entropy_words{};
    if (entropy == 0) {
        entropy_words.push_back(0);
    }
    while (entropy > 0) {
        entropy_words.push_back(static_cast<std::uint32_t>(entropy & 0xffffffffull));
        entropy >>= 32;
    }
    // mix_entropy
    std::uint32_t pool[pool_size] = {};
    std::uint32_t hash_const = init_a;
    for (std::size_t i = 0; i < pool_size; ++i) {
        const std::uint32_t value = i < entropy_words.size() ? entropy_words[i] : 0u;
        pool[i] = hashmix(value, hash_const);
    }
    for (std::size_t source = 0; source < pool_size; ++source) {
        for (std::size_t destination = 0; destination < pool_size; ++destination) {
            if (source != destination) {
                const std::uint32_t hashed = hashmix(pool[source], hash_const);
                pool[destination] = mix(pool[destination], hashed);
            }
        }
    }
    for (std::size_t source = pool_size; source < entropy_words.size(); ++source) {
        for (std::size_t destination = 0; destination < pool_size; ++destination) {
            const std::uint32_t hashed = hashmix(entropy_words[source], hash_const);
            pool[destination] = mix(pool[destination], hashed);
        }
    }
    // generate_state(words, np.uint64): 32-bit words, paired little-endian.
    std::vector<std::uint64_t> result(words, 0);
    std::uint32_t state_const = init_b;
    for (std::size_t i = 0; i < words * 2; ++i) {
        std::uint32_t value = pool[i % pool_size];
        value ^= state_const;
        state_const *= mult_b;
        value *= state_const;
        value ^= value >> xshift;
        if (i % 2 == 0) {
            result[i / 2] = value;
        } else {
            result[i / 2] |= static_cast<std::uint64_t>(value) << 32;
        }
    }
    return result;
}

Generator::Generator(std::uint64_t seed) {
    const std::vector<std::uint64_t> words = seed_sequence_words(seed, 4);
    const Word128 initial_state{words[0], words[1]};
    const Word128 sequence{words[2], words[3]};
    // pcg_setseq_128_srandom_r
    state_ = Word128{};
    increment_.high = (sequence.high << 1) | (sequence.low >> 63);
    increment_.low = (sequence.low << 1) | 1u;
    step();
    state_ = add(state_, initial_state);
    step();
}

Generator::Generator(const GeneratorState& state)
    : state_(state.state), increment_(state.increment), has_uint32_(state.has_uint32),
      uinteger_(state.uinteger) {}

GeneratorState Generator::state() const {
    const GeneratorState result{state_, increment_, has_uint32_, uinteger_};
    return result;
}

void Generator::step() {
    state_ = add(multiply(state_, pcg_multiplier), increment_);
}

void Generator::advance(std::uint64_t delta) {
    // pcg_advance_lcg_128
    Word128 accumulated_mult{0, 1};
    Word128 accumulated_plus{0, 0};
    Word128 current_mult = pcg_multiplier;
    Word128 current_plus = increment_;
    std::uint64_t remaining = delta;
    while (remaining > 0) {
        if ((remaining & 1u) != 0) {
            accumulated_mult = multiply(accumulated_mult, current_mult);
            accumulated_plus = add(multiply(accumulated_plus, current_mult), current_plus);
        }
        current_plus = multiply(add(current_mult, Word128{0, 1}), current_plus);
        current_mult = multiply(current_mult, current_mult);
        remaining >>= 1;
    }
    state_ = add(multiply(accumulated_mult, state_), accumulated_plus);
    has_uint32_ = false;
    uinteger_ = 0;
    outputs_ += delta;
}

std::uint64_t Generator::next_uint64() {
    step();
    ++outputs_;
    const std::uint64_t folded = state_.high ^ state_.low;
    const unsigned rotation = static_cast<unsigned>(state_.high >> 58);
    const std::uint64_t result =
        rotation == 0 ? folded : (folded >> rotation) | (folded << (64u - rotation));
    return result;
}

std::uint32_t Generator::next_uint32() {
    if (has_uint32_) {
        has_uint32_ = false;
        return uinteger_;
    }
    const std::uint64_t next = next_uint64();
    has_uint32_ = true;
    uinteger_ = static_cast<std::uint32_t>(next >> 32);
    const std::uint32_t result = static_cast<std::uint32_t>(next & 0xffffffffull);
    return result;
}

double Generator::random() {
    const double result = next_double_of(next_uint64());
    return result;
}

double Generator::uniform(double low, double high) {
    const double range = high - low;
    const double result = low + range * random();
    return result;
}

double Generator::standard_normal() {
    for (;;) {
        std::uint64_t r = next_uint64();
        const std::size_t index = static_cast<std::size_t>(r & 0xffu);
        r >>= 8;
        const bool negative = (r & 1u) != 0;
        const std::uint64_t magnitude = (r >> 1) & 0x000fffffffffffffull;
        double x = static_cast<double>(magnitude) * ziggurat::wi_double[index];
        if (negative) {
            x = -x;
        }
        if (magnitude < ziggurat::ki_double[index]) {
            return x;
        }
        if (index == 0) {
            for (;;) {
                const double xx = -ziggurat::normal_inverse_r * std::log1p(-random());
                const double yy = -std::log1p(-random());
                if (yy + yy > xx * xx) {
                    const double tail = ((magnitude >> 8) & 1u) != 0 ? -(ziggurat::normal_r + xx)
                                                                    : ziggurat::normal_r + xx;
                    return tail;
                }
            }
        }
        const double height = (ziggurat::fi_double[index - 1] - ziggurat::fi_double[index]) * random() +
                              ziggurat::fi_double[index];
        if (height < std::exp(-0.5 * x * x)) {
            return x;
        }
    }
}

double Generator::normal(double loc, double scale) {
    const double result = loc + scale * standard_normal();
    return result;
}

double Generator::standard_exponential() {
    for (;;) {
        std::uint64_t r = next_uint64();
        r >>= 3;
        const std::size_t index = static_cast<std::size_t>(r & 0xffu);
        r >>= 8;
        const double x = static_cast<double>(r) * ziggurat::we_double[index];
        if (r < ziggurat::ke_double[index]) {
            return x;
        }
        if (index == 0) {
            const double tail = ziggurat::exponential_r - std::log1p(-random());
            return tail;
        }
        const double height = (ziggurat::fe_double[index - 1] - ziggurat::fe_double[index]) * random() +
                              ziggurat::fe_double[index];
        if (height < std::exp(-x)) {
            return x;
        }
        // NumPy recurses here; looping draws the same numbers.
    }
}

double Generator::exponential(double scale) {
    const double result = scale * standard_exponential();
    return result;
}

std::int64_t Generator::integers(std::int64_t low, std::int64_t high) {
    // random_bounded_uint64_fill with Lemire's method (use_masked=False).
    const std::uint64_t range = static_cast<std::uint64_t>(high) - 1u - static_cast<std::uint64_t>(low);
    const std::uint64_t offset = static_cast<std::uint64_t>(low);
    std::uint64_t drawn = 0;
    if (range == 0) {
        drawn = 0;
    } else if (range <= 0xffffffffull) {
        if (range == 0xffffffffull) {
            drawn = next_uint32();
        } else {
            const std::uint32_t range32 = static_cast<std::uint32_t>(range);
            const std::uint32_t excluded = range32 + 1u;
            std::uint64_t m = static_cast<std::uint64_t>(next_uint32()) * excluded;
            std::uint32_t leftover = static_cast<std::uint32_t>(m & 0xffffffffull);
            if (leftover < excluded) {
                const std::uint32_t threshold = (std::numeric_limits<std::uint32_t>::max() - range32) % excluded;
                while (leftover < threshold) {
                    m = static_cast<std::uint64_t>(next_uint32()) * excluded;
                    leftover = static_cast<std::uint32_t>(m & 0xffffffffull);
                }
            }
            drawn = m >> 32;
        }
    } else if (range == std::numeric_limits<std::uint64_t>::max()) {
        drawn = next_uint64();
    } else {
        const std::uint64_t excluded = range + 1u;
        Word128 m = multiply_wide(next_uint64(), excluded);
        if (m.low < excluded) {
            const std::uint64_t threshold = (std::numeric_limits<std::uint64_t>::max() - range) % excluded;
            while (m.low < threshold) {
                m = multiply_wide(next_uint64(), excluded);
            }
        }
        drawn = m.high;
    }
    const std::int64_t result = static_cast<std::int64_t>(offset + drawn);
    return result;
}

std::int64_t Generator::poisson(double lam) {
    if (lam >= 10) {
        // random_poisson_ptrs
        const double slam = std::sqrt(lam);
        const double loglam = std::log(lam);
        const double b = 0.931 + 2.53 * slam;
        const double a = -0.059 + 0.02483 * b;
        const double invalpha = 1.1239 + 1.1328 / (b - 3.4);
        const double vr = 0.9277 - 3.6224 / (b - 2);
        for (;;) {
            const double u = random() - 0.5;
            const double v = random();
            const double us = 0.5 - std::fabs(u);
            const std::int64_t k = static_cast<std::int64_t>(std::floor((2 * a / us + b) * u + lam + 0.43));
            if (us >= 0.07 && v <= vr) {
                return k;
            }
            if (k < 0 || (us < 0.013 && v > us)) {
                continue;
            }
            if ((std::log(v) + std::log(invalpha) - std::log(a / (us * us) + b)) <=
                (-lam + static_cast<double>(k) * loglam - loggam(static_cast<double>(k + 1)))) {
                return k;
            }
        }
    }
    if (lam == 0) {
        return 0;
    }
    // random_poisson_mult
    const double enlam = std::exp(-lam);
    std::int64_t x = 0;
    double product = 1.0;
    for (;;) {
        const double u = random();
        product *= u;
        if (product > enlam) {
            x += 1;
        } else {
            return x;
        }
    }
}

std::size_t Generator::choice(std::size_t population) {
    const std::size_t result = static_cast<std::size_t>(integers(0, static_cast<std::int64_t>(population)));
    return result;
}

std::size_t Generator::choice(std::span<const double> weights) {
    std::vector<double> cdf(weights.size(), 0.0);
    double running = 0.0;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        running += weights[i];
        cdf[i] = running;
    }
    const double total = cdf.empty() ? 1.0 : cdf.back();
    for (double& value : cdf) {
        value /= total;
    }
    const double u = random();
    std::size_t index = 0;
    while (index < cdf.size() && cdf[index] <= u) {
        ++index;
    }
    return index;
}

void Generator::fill_standard_normal(std::span<double> out) {
    for (double& value : out) {
        value = standard_normal();
    }
}

void Generator::fill_uniform(double low, double high, std::span<double> out) {
    for (double& value : out) {
        value = uniform(low, high);
    }
}

}  // namespace voice
