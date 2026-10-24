#include "voice_fft.hpp"

#include <cmath>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <utility>

namespace voice {
namespace {

constexpr double pi = 3.141592653589793238462643383279502884;
// Above this prime factor a radix goes through Bluestein's algorithm.
constexpr std::size_t largest_direct_prime = 61;

Complex multiply(const Complex& a, const Complex& b) {
    const Complex result(a.real() * b.real() - a.imag() * b.imag(),
                         a.real() * b.imag() + a.imag() * b.real());
    return result;
}

// exp(-2 pi i index / length), reduced to the first octant in integers so that
// the angle handed to cos and sin is small and the symmetric values agree exactly.
Complex twiddle(std::uint64_t index, std::uint64_t length) {
    std::uint64_t p = (index % length) * 8u;  // the circle is 8 * length
    const std::uint64_t n = length;
    bool negate_sine = false;
    bool negate_cosine = false;
    bool swap = false;
    if (p > 4 * n) {
        p = 8 * n - p;
        negate_sine = true;
    }
    if (p > 2 * n) {
        p = 4 * n - p;
        negate_cosine = true;
    }
    if (p > n) {
        p = 2 * n - p;
        swap = true;
    }
    const double angle = (pi / 4) * (static_cast<double>(p) / static_cast<double>(n));
    double c = std::cos(angle);
    double s = std::sin(angle);
    if (swap) {
        const double t = c;
        c = s;
        s = t;
    }
    if (negate_cosine) {
        c = -c;
    }
    if (negate_sine) {
        s = -s;
    }
    const Complex result(c, -s);
    return result;
}

std::vector<std::size_t> factorize(std::size_t n) {
    std::vector<std::size_t> factors{};
    while (n % 4 == 0) {
        factors.push_back(4);
        n /= 4;
    }
    while (n % 2 == 0) {
        factors.push_back(2);
        n /= 2;
    }
    for (std::size_t p = 3; p * p <= n; p += 2) {
        while (n % p == 0) {
            factors.push_back(p);
            n /= p;
        }
    }
    if (n > 1) {
        factors.push_back(n);
    }
    return factors;
}

using Table = std::vector<Complex>;

// The table w[j] = exp(-2 pi i j / n) for the whole transform.
std::shared_ptr<const Table> twiddle_table(std::size_t n) {
    std::shared_ptr<Table> table = std::make_shared<Table>(n);
    for (std::size_t j = 0; j < n; ++j) {
        (*table)[j] = twiddle(j, n);
    }
    return table;
}

// Tables and Bluestein kernels are the same every time for a length, so the recent
// ones are kept (up to `cache_bytes` in all, none larger than `largest_cached`
// entries): a cue's notes reuse a handful of lengths many times. Larger transforms
// (a whole loop) build theirs once per call.
constexpr std::size_t largest_cached = std::size_t{1} << 20;
constexpr std::size_t cache_bytes = std::size_t{48} << 20;

struct Cached {
    std::size_t key{};  // a length, or (with the top bit) a Bluestein kernel's length and direction
    std::shared_ptr<const Table> values{};
};

class Cache final {
  public:
    template <class Make>
    std::shared_ptr<const Table> get(std::size_t key, std::size_t entries, Make make) {
        if (entries > largest_cached) {
            return make();
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (std::list<Cached>::iterator it = entries_.begin(); it != entries_.end(); ++it) {
                if ((*it).key == key) {
                    entries_.splice(entries_.begin(), entries_, it);
                    return entries_.front().values;
                }
            }
        }
        std::shared_ptr<const Table> made = make();
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.push_front({key, made});
        bytes_ += (*made).size() * sizeof(Complex);
        while (bytes_ > cache_bytes && entries_.size() > 1) {
            bytes_ -= (*entries_.back().values).size() * sizeof(Complex);
            entries_.pop_back();
        }
        return made;
    }

  private:
    std::mutex mutex_{};
    std::list<Cached> entries_{};
    std::size_t bytes_{};
};

Cache& cache() {
    static Cache shared{};
    return shared;
}

std::shared_ptr<const Table> table_for(std::size_t n) {
    return cache().get(n, n, [n] { return twiddle_table(n); });
}

// A radix too large to sum directly is transformed by Bluestein's algorithm through a
// power-of-two transform of `size`: the chirp, the kernel's spectrum and that transform's
// own table, as chirp_plan below builds them.
struct Chirp {
    std::size_t size{};
    std::shared_ptr<const Table> chirp{};
    std::shared_ptr<const Table> kernel{};
    std::shared_ptr<const Table> table{};
};

struct Plan {
    std::size_t length{};
    std::vector<std::size_t> factors{};
    const Table* table{};
    bool inverse{};
    std::vector<Chirp> chirps{};  // per level, for a radix above largest_direct_prime
};

// Per level of a transform: the twiddled inputs of one butterfly, the radix's roots, and
// a large radix's transform buffers.
struct Scratch {
    std::vector<Complex> t{};
    std::vector<Complex> roots{};
    std::vector<Complex> work{};
    std::vector<Complex> bins{};
};

Chirp chirp_plan(std::size_t n, bool inverse);
void chirp_transform(const Chirp& c, const Complex* in, Complex* out, std::size_t n, std::vector<Complex>& work);

// Every index a pass asks for is below the length.
Complex root(const Plan& plan, std::size_t index) {
    const Complex w = (*plan.table)[index];
    const Complex result = plan.inverse ? std::conj(w) : w;
    return result;
}

// Decimation in time: `in` holds n values `stride` apart, `out` receives n contiguous bins.
void pass(const Plan& plan, const Complex* in, std::size_t stride, Complex* out, std::size_t n,
          std::size_t level, std::vector<Scratch>& scratch) {
    if (n == 1) {
        out[0] = in[0];
        return;
    }
    const std::size_t p = plan.factors[level];
    const std::size_t m = n / p;
    for (std::size_t q = 0; q < p; ++q) {
        pass(plan, in + q * stride, stride * p, out + q * m, m, level + 1, scratch);
    }
    const std::size_t step = plan.length / n;  // W_n^j = table[j * step]
    Scratch& own = scratch[level];
    Complex* t = own.t.data();
    Complex* roots = own.roots.data();  // W_p^j, for a radix without its own butterfly
    if (p != 2 && p != 4 && p <= largest_direct_prime) {
        const std::size_t root_step = plan.length / p;
        for (std::size_t j = 0; j < p; ++j) {
            roots[j] = root(plan, j * root_step);
        }
    }
    for (std::size_t k = 0; k < m; ++k) {
        t[0] = out[k];
        for (std::size_t q = 1; q < p; ++q) {
            t[q] = multiply(out[q * m + k], root(plan, q * k * step));
        }
        if (p == 2) {
            out[k] = t[0] + t[1];
            out[m + k] = t[0] - t[1];
        } else if (p == 4) {
            const Complex a = t[0] + t[2];
            const Complex b = t[0] - t[2];
            const Complex c = t[1] + t[3];
            const Complex d = t[1] - t[3];
            // -i * d forward, +i * d inverse
            const Complex rotated = plan.inverse ? Complex(-d.imag(), d.real()) : Complex(d.imag(), -d.real());
            out[k] = a + c;
            out[m + k] = b + rotated;
            out[2 * m + k] = a - c;
            out[3 * m + k] = b - rotated;
        } else if (p > largest_direct_prime) {
            chirp_transform(plan.chirps[level], t, own.bins.data(), p, own.work);
            for (std::size_t s = 0; s < p; ++s) {
                out[s * m + k] = own.bins[s];
            }
        } else {
            for (std::size_t s = 0; s < p; ++s) {
                Complex sum = t[0];
                std::size_t index = 0;  // (q * s) % p
                for (std::size_t q = 1; q < p; ++q) {
                    index += s;
                    if (index >= p) {
                        index -= p;
                    }
                    sum += multiply(t[q], roots[index]);
                }
                out[s * m + k] = sum;
            }
        }
    }
}

void mixed_radix(std::vector<Complex>& data, const std::vector<std::size_t>& factors, bool inverse,
                 const Table& table) {
    Plan plan{};
    plan.length = data.size();
    plan.factors = factors;
    plan.table = &table;
    plan.inverse = inverse;
    std::vector<Scratch> scratch(factors.size());
    plan.chirps.resize(factors.size());
    for (std::size_t level = 0; level < factors.size(); ++level) {
        const std::size_t p = factors[level];
        scratch[level].t.resize(p);
        if (p > largest_direct_prime) {
            plan.chirps[level] = chirp_plan(p, inverse);
            scratch[level].bins.resize(p);
        } else if (p != 2 && p != 4) {
            scratch[level].roots.resize(p);
        }
    }
    const std::vector<Complex> input = data;
    pass(plan, input.data(), 1, data.data(), data.size(), 0, scratch);
}

std::size_t bluestein_size(std::size_t n) {
    std::size_t size = 1;
    while (size < 2 * n - 1) {
        size *= 2;
    }
    return size;
}

// chirp[k] = exp(-pi i k^2 / n) = exp(-2 pi i (k^2 mod 2n) / 2n), conjugated for an inverse
std::shared_ptr<const Table> chirp_for(std::size_t n, bool inverse) {
    std::shared_ptr<Table> chirp = std::make_shared<Table>(n);
    for (std::size_t k = 0; k < n; ++k) {
        const std::uint64_t square = (static_cast<std::uint64_t>(k) * k) % (2 * static_cast<std::uint64_t>(n));
        const Complex w = twiddle(square, 2 * static_cast<std::uint64_t>(n));
        (*chirp)[k] = inverse ? std::conj(w) : w;
    }
    return chirp;
}

// The forward transform of the conjugate chirp, wrapped round `size`.
std::shared_ptr<const Table> kernel_for(const Table& chirp, std::size_t size, const Table& table) {
    const std::size_t n = chirp.size();
    std::shared_ptr<Table> b = std::make_shared<Table>(size, Complex(0, 0));
    (*b)[0] = std::conj(chirp[0]);
    for (std::size_t k = 1; k < n; ++k) {
        (*b)[k] = std::conj(chirp[k]);
        (*b)[size - k] = std::conj(chirp[k]);
    }
    mixed_radix(*b, factorize(size), false, table);
    return b;
}

Chirp chirp_plan(std::size_t n, bool inverse) {
    Chirp c{};
    c.size = bluestein_size(n);
    constexpr std::size_t top = std::size_t{1} << (sizeof(std::size_t) * 8 - 1);
    const std::size_t chirp_key = top | (n << 2) | (inverse ? 1u : 0u);
    const std::size_t kernel_key = top | (n << 2) | 2u | (inverse ? 1u : 0u);
    c.table = table_for(c.size);
    c.chirp = cache().get(chirp_key, c.size, [&] { return chirp_for(n, inverse); });
    c.kernel = cache().get(kernel_key, c.size, [&] { return kernel_for(*c.chirp, c.size, *c.table); });
    return c;
}

// The n-point transform of `in` into `out` by Bluestein's algorithm.
void chirp_transform(const Chirp& c, const Complex* in, Complex* out, std::size_t n, std::vector<Complex>& work) {
    const Table& chirp = *c.chirp;
    const Table& kernel = *c.kernel;
    work.assign(c.size, Complex(0, 0));
    for (std::size_t k = 0; k < n; ++k) {
        work[k] = multiply(in[k], chirp[k]);
    }
    const std::vector<std::size_t> factors = factorize(c.size);
    mixed_radix(work, factors, false, *c.table);
    for (std::size_t k = 0; k < c.size; ++k) {
        work[k] = multiply(work[k], kernel[k]);
    }
    mixed_radix(work, factors, true, *c.table);
    const double scale = 1.0 / static_cast<double>(c.size);
    for (std::size_t k = 0; k < n; ++k) {
        out[k] = multiply(work[k] * scale, chirp[k]);
    }
}

}  // namespace

void fft(std::vector<Complex>& data, bool inverse) {
    if (data.size() <= 1) {
        return;
    }
    // A prime factor above largest_direct_prime is transformed inside the passes by
    // Bluestein's algorithm at that factor's size, never the whole length's.
    mixed_radix(data, factorize(data.size()), inverse, *table_for(data.size()));
}

void rfft(std::span<const double> x, std::size_t n, std::vector<Complex>& out) {
    out.assign(n / 2 + 1, Complex(0, 0));
    if (n == 0) {
        return;
    }
    const std::size_t used = x.size() < n ? x.size() : n;
    if (n % 2 == 1) {
        std::vector<Complex> full(n, Complex(0, 0));
        for (std::size_t j = 0; j < used; ++j) {
            full[j] = Complex(x[j], 0);
        }
        fft(full, false);
        for (std::size_t k = 0; k <= n / 2; ++k) {
            out[k] = full[k];
        }
        out[0] = Complex(out[0].real(), 0);
        return;
    }
    // An even length packs pairs into one complex transform of half the length.
    const std::size_t half = n / 2;
    std::vector<Complex> z(half, Complex(0, 0));
    for (std::size_t j = 0; j < used; ++j) {
        if (j % 2 == 0) {
            z[j / 2] = Complex(x[j], z[j / 2].imag());
        } else {
            z[j / 2] = Complex(z[j / 2].real(), x[j]);
        }
    }
    fft(z, false);
    out[0] = Complex(z[0].real() + z[0].imag(), 0);
    out[half] = Complex(z[0].real() - z[0].imag(), 0);
    for (std::size_t k = 1; k < half; ++k) {
        const Complex zk = z[k];
        const Complex zm = std::conj(z[half - k]);
        const Complex even = (zk + zm) * 0.5;
        const Complex difference = zk - zm;
        const Complex odd(difference.imag() * 0.5, -difference.real() * 0.5);  // difference / 2i
        out[k] = even + multiply(twiddle(k, n), odd);
    }
}

void irfft(std::span<const Complex> spectrum, std::size_t n, std::vector<double>& out) {
    out.assign(n, 0.0);
    if (n == 0) {
        return;
    }
    const std::size_t bins = n / 2 + 1;
    std::vector<Complex> x(bins, Complex(0, 0));
    for (std::size_t k = 0; k < bins && k < spectrum.size(); ++k) {
        x[k] = spectrum[k];
    }
    x[0] = Complex(x[0].real(), 0);
    if (n % 2 == 1) {
        std::vector<Complex> full(n, Complex(0, 0));
        full[0] = x[0];
        for (std::size_t k = 1; k < bins; ++k) {
            full[k] = x[k];
            full[n - k] = std::conj(x[k]);
        }
        fft(full, true);
        const double scale = 1.0 / static_cast<double>(n);
        for (std::size_t j = 0; j < n; ++j) {
            out[j] = full[j].real() * scale;
        }
        return;
    }
    const std::size_t half = n / 2;
    x[half] = Complex(x[half].real(), 0);
    std::vector<Complex> z(half, Complex(0, 0));
    for (std::size_t k = 0; k < half; ++k) {
        const Complex xk = x[k];
        const Complex xm = std::conj(x[half - k]);
        const Complex even = (xk + xm) * 0.5;
        const Complex odd = multiply((xk - xm) * 0.5, std::conj(twiddle(k, n)));
        z[k] = Complex(even.real() - odd.imag(), even.imag() + odd.real());  // even + i odd
    }
    fft(z, true);
    const double scale = 1.0 / static_cast<double>(half);
    for (std::size_t j = 0; j < half; ++j) {
        out[2 * j] = z[j].real() * scale;
        out[2 * j + 1] = z[j].imag() * scale;
    }
}

}  // namespace voice
