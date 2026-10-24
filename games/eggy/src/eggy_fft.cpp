#include "eggy_fft.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace eggy::fft {
namespace {

constexpr double pi = 3.141592653589793238462643383279502884;
constexpr std::size_t largest_direct_prime = 61;

Complex mul(Complex a, Complex b) {
    return {a.real() * b.real() - a.imag() * b.imag(), a.real() * b.imag() + a.imag() * b.real()};
}

Complex add(Complex a, Complex b) {
    return {a.real() + b.real(), a.imag() + b.imag()};
}

Complex sub(Complex a, Complex b) {
    return {a.real() - b.real(), a.imag() - b.imag()};
}

Complex conj(Complex a) {
    return {a.real(), -a.imag()};
}

// exp(-2 pi i index / length), its angle reduced to the first octant in integers.
Complex twiddle(std::uint64_t index, std::uint64_t length) {
    std::uint64_t p = (index % length) * 8u;
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
        std::swap(c, s);
    }
    if (negate_cosine) {
        c = -c;
    }
    if (negate_sine) {
        s = -s;
    }
    return {c, -s};
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

}  // namespace

// The forward complex DFT of one length (the inverse is conj(forward(conj(x)))).
class ComplexPlan final {
  public:
    explicit ComplexPlan(std::size_t n) : n_(n), factors_(factorize(n)), table_(n) {
        for (std::size_t j = 0; j < n; ++j) {
            table_[j] = twiddle(j, n);
        }
        for (const std::size_t p : factors_) {
            if (p > largest_direct_prime && !bluestein_.count_for(p)) {
                bluestein_.add(p);
            }
        }
        work_.resize(n);
    }

    void forward(std::vector<Complex>& data) {
        if (n_ <= 1) {
            return;
        }
        Complex* x = data.data();
        Complex* y = work_.data();
        std::size_t ns = 1;
        std::vector<Complex> v{};
        std::vector<Complex> w{};
        for (const std::size_t p : factors_) {
            const std::size_t groups = n_ / p;
            const std::size_t step = n_ / (ns * p);
            const std::size_t blocks = groups / ns;
            if (p == 2 || p == 4) {
                for (std::size_t k = 0; k < ns; ++k) {
                    // this k's twiddles: w_{ns p}^{r k}
                    Complex t1(1, 0);
                    Complex t2(1, 0);
                    Complex t3(1, 0);
                    if (k != 0) {
                        t1 = table_[k * step];
                        if (p == 4) {
                            t2 = table_[2 * k * step];
                            t3 = table_[3 * k * step];
                        }
                    }
                    for (std::size_t jb = 0; jb < blocks; ++jb) {
                        const std::size_t j = jb * ns + k;
                        const std::size_t base = jb * ns * p + k;
                        if (p == 2) {
                            const Complex a = x[j];
                            const Complex b = k != 0 ? mul(x[j + groups], t1) : x[j + groups];
                            y[base] = add(a, b);
                            y[base + ns] = sub(a, b);
                        } else {
                            const Complex v0 = x[j];
                            Complex v1 = x[j + groups];
                            Complex v2 = x[j + 2 * groups];
                            Complex v3 = x[j + 3 * groups];
                            if (k != 0) {
                                v1 = mul(v1, t1);
                                v2 = mul(v2, t2);
                                v3 = mul(v3, t3);
                            }
                            const Complex a = add(v0, v2);
                            const Complex b = sub(v0, v2);
                            const Complex c = add(v1, v3);
                            const Complex d = sub(v1, v3);
                            const Complex rotated(d.imag(), -d.real());  // -i d
                            y[base] = add(a, c);
                            y[base + ns] = add(b, rotated);
                            y[base + 2 * ns] = sub(a, c);
                            y[base + 3 * ns] = sub(b, rotated);
                        }
                    }
                }
            } else {
                v.resize(p);
                w.resize(p);
                Bluestein* chirp = p > largest_direct_prime ? bluestein_.find(p) : nullptr;
                if (chirp == nullptr) {
                    // the p x p roots w_p^(q s mod p), once per stage
                    roots_.resize(p * p);
                    const std::size_t root_step = n_ / p;
                    for (std::size_t s2 = 0; s2 < p; ++s2) {
                        for (std::size_t q = 0; q < p; ++q) {
                            roots_[s2 * p + q] = table_[((q * s2) % p) * root_step];
                        }
                    }
                }
                for (std::size_t k = 0; k < ns; ++k) {
                    for (std::size_t r = 0; r < p; ++r) {
                        w[r] = table_[r * k * step];
                    }
                    for (std::size_t jb = 0; jb < blocks; ++jb) {
                        const std::size_t j = jb * ns + k;
                        for (std::size_t r = 0; r < p; ++r) {
                            v[r] = x[j + r * groups];
                        }
                        if (k != 0) {
                            for (std::size_t r = 1; r < p; ++r) {
                                v[r] = mul(v[r], w[r]);
                            }
                        }
                        small_dft(v, p, chirp);
                        const std::size_t base = jb * ns * p + k;
                        for (std::size_t r = 0; r < p; ++r) {
                            y[base + r * ns] = v[r];
                        }
                    }
                }
            }
            std::swap(x, y);
            ns *= p;
        }
        if (x != data.data()) {
            for (std::size_t i = 0; i < n_; ++i) {
                data[i] = x[i];
            }
        }
    }

    void inverse(std::vector<Complex>& data) {
        for (Complex& value : data) {
            value = conj(value);
        }
        forward(data);
        for (Complex& value : data) {
            value = conj(value);
        }
    }

  private:
    // A large prime factor's DFT by Bluestein's chirp, on a power-of-two transform.
    struct Bluestein {
        std::size_t p{};
        std::size_t size{};
        std::vector<Complex> chirp{};
        std::vector<Complex> kernel{};  // the transform of the conjugate chirp
        std::unique_ptr<ComplexPlan> plan{};
        std::vector<Complex> buffer{};
    };
    struct Chirps {
        std::vector<Bluestein> list{};
        [[nodiscard]] bool count_for(std::size_t p) const {
            for (const Bluestein& b : list) {
                if (b.p == p) {
                    return true;
                }
            }
            return false;
        }
        Bluestein* find(std::size_t p) {
            for (Bluestein& b : list) {
                if (b.p == p) {
                    return &b;
                }
            }
            return nullptr;
        }
        void add(std::size_t p) {
            Bluestein b{};
            b.p = p;
            b.size = 1;
            while (b.size < 2 * p - 1) {
                b.size *= 2;
            }
            b.chirp.resize(p);
            for (std::size_t k = 0; k < p; ++k) {
                const std::uint64_t square = (static_cast<std::uint64_t>(k) * k) % (2 * static_cast<std::uint64_t>(p));
                b.chirp[k] = twiddle(square, 2 * static_cast<std::uint64_t>(p));
            }
            b.plan = std::make_unique<ComplexPlan>(b.size);
            b.kernel.assign(b.size, Complex(0, 0));
            b.kernel[0] = conj(b.chirp[0]);
            for (std::size_t k = 1; k < p; ++k) {
                b.kernel[k] = conj(b.chirp[k]);
                b.kernel[b.size - k] = conj(b.chirp[k]);
            }
            (*b.plan).forward(b.kernel);
            b.buffer.resize(b.size);
            list.push_back(std::move(b));
        }
    };

    void small_dft(std::vector<Complex>& v, std::size_t p, Bluestein* chirp) {
        if (p == 2) {
            const Complex a = v[0];
            const Complex b = v[1];
            v[0] = add(a, b);
            v[1] = sub(a, b);
            return;
        }
        if (p == 4) {
            const Complex a = add(v[0], v[2]);
            const Complex b = sub(v[0], v[2]);
            const Complex c = add(v[1], v[3]);
            const Complex d = sub(v[1], v[3]);
            const Complex rotated(d.imag(), -d.real());  // -i d
            v[0] = add(a, c);
            v[1] = add(b, rotated);
            v[2] = sub(a, c);
            v[3] = sub(b, rotated);
            return;
        }
        if (chirp != nullptr) {
            Bluestein& b = *chirp;
            std::vector<Complex>& a = b.buffer;
            for (std::size_t k = 0; k < b.size; ++k) {
                a[k] = k < p ? mul(v[k], b.chirp[k]) : Complex(0, 0);
            }
            (*b.plan).forward(a);
            for (std::size_t k = 0; k < b.size; ++k) {
                a[k] = mul(a[k], b.kernel[k]);
            }
            (*b.plan).inverse(a);
            const double scale = 1.0 / static_cast<double>(b.size);
            for (std::size_t k = 0; k < p; ++k) {
                v[k] = mul(Complex(a[k].real() * scale, a[k].imag() * scale), b.chirp[k]);
            }
            return;
        }
        // a small prime: directly, with the stage's roots
        small_.resize(p);
        for (std::size_t s = 0; s < p; ++s) {
            Complex sum = v[0];
            const Complex* row = roots_.data() + s * p;
            for (std::size_t q = 1; q < p; ++q) {
                sum = add(sum, mul(v[q], row[q]));
            }
            small_[s] = sum;
        }
        for (std::size_t s = 0; s < p; ++s) {
            v[s] = small_[s];
        }
    }

    std::size_t n_{};
    std::vector<std::size_t> factors_{};
    std::vector<Complex> table_{};
    std::vector<Complex> work_{};
    std::vector<Complex> small_{};
    std::vector<Complex> roots_{};
    Chirps bluestein_{};
};

RealPlan::RealPlan(std::size_t n) : n_(n) {
    if (n == 0) {
        return;
    }
    if (n % 2 == 1) {
        plan_ = std::make_unique<ComplexPlan>(n);
        return;
    }
    plan_ = std::make_unique<ComplexPlan>(n / 2);
    unpack_.resize(n / 2);
    for (std::size_t k = 0; k < n / 2; ++k) {
        unpack_[k] = twiddle(k, n);
    }
}

RealPlan::~RealPlan() = default;
RealPlan::RealPlan(RealPlan&&) noexcept = default;
RealPlan& RealPlan::operator=(RealPlan&&) noexcept = default;

void RealPlan::forward(std::span<const double> x, std::vector<Complex>& out) {
    const std::size_t n = n_;
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
        (*plan_).forward(full);
        for (std::size_t k = 0; k <= n / 2; ++k) {
            out[k] = full[k];
        }
        out[0] = Complex(out[0].real(), 0);
        return;
    }
    const std::size_t half = n / 2;
    std::vector<Complex> z(half, Complex(0, 0));
    for (std::size_t j = 0; j < used; ++j) {
        if (j % 2 == 0) {
            z[j / 2] = Complex(x[j], z[j / 2].imag());
        } else {
            z[j / 2] = Complex(z[j / 2].real(), x[j]);
        }
    }
    (*plan_).forward(z);
    out[0] = Complex(z[0].real() + z[0].imag(), 0);
    out[half] = Complex(z[0].real() - z[0].imag(), 0);
    for (std::size_t k = 1; k < half; ++k) {
        const Complex zk = z[k];
        const Complex zm = conj(z[half - k]);
        const Complex even((zk.real() + zm.real()) * 0.5, (zk.imag() + zm.imag()) * 0.5);
        const Complex difference = sub(zk, zm);
        const Complex odd(difference.imag() * 0.5, -difference.real() * 0.5);  // difference / 2i
        out[k] = add(even, mul(unpack_[k], odd));
    }
}

void RealPlan::inverse(std::span<const Complex> spectrum, std::vector<double>& out) {
    const std::size_t n = n_;
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
            full[n - k] = conj(x[k]);
        }
        (*plan_).inverse(full);
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
        const Complex xm = conj(x[half - k]);
        const Complex even((xk.real() + xm.real()) * 0.5, (xk.imag() + xm.imag()) * 0.5);
        const Complex d = sub(xk, xm);
        const Complex odd = mul(Complex(d.real() * 0.5, d.imag() * 0.5), conj(unpack_[k]));
        z[k] = Complex(even.real() - odd.imag(), even.imag() + odd.real());  // even + i odd
    }
    (*plan_).inverse(z);
    const double scale = 1.0 / static_cast<double>(half);
    for (std::size_t j = 0; j < half; ++j) {
        out[2 * j] = z[j].real() * scale;
        out[2 * j + 1] = z[j].imag() * scale;
    }
}

std::vector<double> filter(std::span<const double> x, double lo, double hi, int order, std::size_t pad, bool circular) {
    const std::size_t n = x.size();
    const std::size_t length = circular ? n : n + pad;
    RealPlan plan(length);
    std::vector<Complex> spectrum{};
    plan.forward(x, spectrum);
    const double spacing = 1.0 / 48000.0;
    const double unit = 1.0 / (static_cast<double>(length) * spacing);
    const double exponent = 2.0 * order;
    for (std::size_t k = 0; k < spectrum.size(); ++k) {
        const double f = static_cast<double>(k) * unit;
        double h = 1.0;
        if (hi != 0) {
            const double r = f / hi;
            h /= std::sqrt(1 + (order == 1 ? r * r : std::pow(r, exponent)));
        }
        if (lo != 0) {
            const double r = lo / std::max(f, 1e-3);
            h /= std::sqrt(1 + (order == 1 ? r * r : std::pow(r, exponent)));
        }
        spectrum[k] = Complex(spectrum[k].real() * h, spectrum[k].imag() * h);
    }
    std::vector<double> out{};
    plan.inverse(spectrum, out);
    out.resize(n);
    return out;
}

}  // namespace eggy::fft
