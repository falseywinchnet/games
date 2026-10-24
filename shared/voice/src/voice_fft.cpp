#include "voice_fft.hpp"

#include <cmath>
#include <cstdint>

namespace voice {
namespace {

constexpr double pi = 3.141592653589793238462643383279502884;
// Above this prime factor a length goes through Bluestein's algorithm.
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

// The table w[j] = exp(-2 pi i j / n) for the whole transform.
std::vector<Complex> twiddle_table(std::size_t n) {
    std::vector<Complex> table(n);
    for (std::size_t j = 0; j < n; ++j) {
        table[j] = twiddle(j, n);
    }
    return table;
}

struct Plan {
    std::size_t length{};
    std::vector<std::size_t> factors{};
    std::vector<Complex> table{};
    bool inverse{};
};

Complex root(const Plan& plan, std::size_t index) {
    const Complex w = plan.table[index % plan.length];
    const Complex result = plan.inverse ? std::conj(w) : w;
    return result;
}

// Decimation in time: `in` holds n values `stride` apart, `out` receives n contiguous bins.
void pass(const Plan& plan, const Complex* in, std::size_t stride, Complex* out, std::size_t n,
          std::size_t level, std::vector<Complex>& scratch) {
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
    Complex* t = scratch.data() + level * largest_direct_prime;
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
        } else {
            const std::size_t root_step = plan.length / p;
            for (std::size_t s = 0; s < p; ++s) {
                Complex sum = t[0];
                for (std::size_t q = 1; q < p; ++q) {
                    sum += multiply(t[q], root(plan, ((q * s) % p) * root_step));
                }
                out[s * m + k] = sum;
            }
        }
    }
}

void mixed_radix(std::vector<Complex>& data, const std::vector<std::size_t>& factors, bool inverse) {
    Plan plan{};
    plan.length = data.size();
    plan.factors = factors;
    plan.table = twiddle_table(data.size());
    plan.inverse = inverse;
    const std::vector<Complex> input = data;
    std::vector<Complex> scratch(largest_direct_prime * (factors.size() + 1));
    pass(plan, input.data(), 1, data.data(), data.size(), 0, scratch);
}

void bluestein(std::vector<Complex>& data, bool inverse) {
    const std::size_t n = data.size();
    std::size_t size = 1;
    while (size < 2 * n - 1) {
        size *= 2;
    }
    // chirp[k] = exp(-pi i k^2 / n) = exp(-2 pi i (k^2 mod 2n) / 2n)
    std::vector<Complex> chirp(n);
    for (std::size_t k = 0; k < n; ++k) {
        const std::uint64_t square = (static_cast<std::uint64_t>(k) * k) % (2 * static_cast<std::uint64_t>(n));
        const Complex w = twiddle(square, 2 * static_cast<std::uint64_t>(n));
        chirp[k] = inverse ? std::conj(w) : w;
    }
    std::vector<Complex> a(size, Complex(0, 0));
    std::vector<Complex> b(size, Complex(0, 0));
    for (std::size_t k = 0; k < n; ++k) {
        a[k] = multiply(data[k], chirp[k]);
    }
    b[0] = std::conj(chirp[0]);
    for (std::size_t k = 1; k < n; ++k) {
        b[k] = std::conj(chirp[k]);
        b[size - k] = std::conj(chirp[k]);
    }
    const std::vector<std::size_t> factors = factorize(size);
    mixed_radix(a, factors, false);
    mixed_radix(b, factors, false);
    for (std::size_t k = 0; k < size; ++k) {
        a[k] = multiply(a[k], b[k]);
    }
    mixed_radix(a, factors, true);
    const double scale = 1.0 / static_cast<double>(size);
    for (std::size_t k = 0; k < n; ++k) {
        data[k] = multiply(a[k] * scale, chirp[k]);
    }
}

}  // namespace

void fft(std::vector<Complex>& data, bool inverse) {
    if (data.size() <= 1) {
        return;
    }
    const std::vector<std::size_t> factors = factorize(data.size());
    if (factors.back() > largest_direct_prime && factors.back() > 4) {
        bluestein(data, inverse);
        return;
    }
    mixed_radix(data, factors, inverse);
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
