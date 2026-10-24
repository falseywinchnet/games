#include "gnome_math.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

// V8's Math functions (v8/src/base/ieee754.cc), which are fdlibm 5.3 / FreeBSD msun:
// e_exp.c, s_expm1.c, s_tanh.c, k_sin.c, k_cos.c, k_tan.c, e_rem_pio2.c, s_sin.c,
// s_cos.c, s_tan.c and e_log2.c (with k_log.h). Original copyright:
//
//   Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
//   Developed at SunSoft, a Sun Microsystems, Inc. business.
//   Permission to use, copy, modify, and distribute this software is freely granted,
//   provided that this notice is preserved.
//
// Node's V8 for macOS on arm64 is compiled by clang with its default -ffp-contract=on,
// which fuses each a * b + c inside one expression into a fused multiply-add; the
// references were rendered there. So every multiply-add clang fused is written here as
// std::fma (exact on every platform, in hardware or software), and the file is
// compiled with -ffp-contract=off like the rest of mm_core: the results are V8's, bit
// for bit, natively and in the browser build (checked over 2.6 million arguments).
// Math.pow is not fdlibm in that V8: it is the platform's pow, so pow() here is
// std::pow (V8's exactly on macOS; see pow() below for elsewhere).
// Only the paths finite arguments take matter to the gnome; the special cases are kept
// so each function is the whole routine.
namespace mm::js {
namespace {

std::uint32_t high_word(double x) {
    std::uint64_t u = 0;
    std::memcpy(&u, &x, sizeof u);
    return static_cast<std::uint32_t>(u >> 32);
}

std::uint32_t low_word(double x) {
    std::uint64_t u = 0;
    std::memcpy(&u, &x, sizeof u);
    return static_cast<std::uint32_t>(u);
}

double from_words(std::uint32_t high, std::uint32_t low) {
    const std::uint64_t u = (static_cast<std::uint64_t>(high) << 32) | low;
    double x = 0;
    std::memcpy(&x, &u, sizeof x);
    return x;
}

double with_high(double x, std::uint32_t high) {
    return from_words(high, low_word(x));
}

double with_low(double x, std::uint32_t low) {
    return from_words(high_word(x), low);
}

std::int32_t signed_high(double x) {
    return static_cast<std::int32_t>(high_word(x));
}

// ------------------------------------------------------------------ k_sin, k_cos, k_tan
double kernel_sin(double x, double y, int iy) {
    constexpr double half = 5.00000000000000000000e-01;
    constexpr double S1 = -1.66666666666666324348e-01;
    constexpr double S2 = 8.33333333332248946124e-03;
    constexpr double S3 = -1.98412698298579493134e-04;
    constexpr double S4 = 2.75573137070700676789e-06;
    constexpr double S5 = -2.50507602534068634195e-08;
    constexpr double S6 = 1.58969099521155010221e-10;
    const std::int32_t ix = signed_high(x) & 0x7FFFFFFF;
    if (ix < 0x3E400000) {
        if (static_cast<int>(x) == 0) {
            return x;
        }
    }
    const double z = x * x;
    const double v = z * x;
    const double r = std::fma(z, std::fma(z, std::fma(z, std::fma(z, S6, S5), S4), S3), S2);
    if (iy == 0) {
        return std::fma(v, std::fma(z, r, S1), x);
    }
    return x - std::fma(-v, S1, std::fma(z, std::fma(half, y, -(v * r)), -y));
}

double kernel_cos(double x, double y) {
    constexpr double one = 1.00000000000000000000e+00;
    constexpr double C1 = 4.16666666666666019037e-02;
    constexpr double C2 = -1.38888888888741095749e-03;
    constexpr double C3 = 2.48015872894767294178e-05;
    constexpr double C4 = -2.75573143513906633035e-07;
    constexpr double C5 = 2.08757232129817482790e-09;
    constexpr double C6 = -1.13596475577881948265e-11;
    const std::int32_t ix = signed_high(x) & 0x7FFFFFFF;
    if (ix < 0x3E400000) {
        if (static_cast<int>(x) == 0) {
            return one;
        }
    }
    const double z = x * x;
    const double r = z * std::fma(z, std::fma(z, std::fma(z, std::fma(z, std::fma(z, C6, C5), C4), C3), C2), C1);
    if (ix < 0x3FD33333) {
        return one - std::fma(0.5, z, -std::fma(z, r, -(x * y)));
    }
    double qx = 0;
    if (ix > 0x3FE90000) {
        qx = 0.28125;
    } else {
        qx = from_words(static_cast<std::uint32_t>(ix - 0x00200000), 0);
    }
    const double iz = std::fma(0.5, z, -qx);
    const double a = one - qx;
    return a - (iz - std::fma(z, r, -(x * y)));
}

double kernel_tan(double x, double y, int iy) {
    constexpr double xxx[] = {
        3.33333333333334091986e-01,  1.33333333333201242699e-01,  5.39682539762260521377e-02,
        2.18694882948595424599e-02,  8.86323982359930005737e-03,  3.59207910759131235356e-03,
        1.45620945432529025516e-03,  5.88041240820264096874e-04,  2.46463134818469906812e-04,
        7.81794442939557092300e-05,  7.14072491382608190305e-05,  -1.85586374855275456654e-05,
        2.59073051863633712884e-05,
        7.85398163397448278999e-01,  // pio4
        3.06161699786838301793e-17,  // pio4lo
    };
    const double one = 1.0;
    const double pio4 = xxx[13];
    const double pio4lo = xxx[14];
    const auto T = [&](int i) { return xxx[i]; };
    double z = 0, r = 0, v = 0, w = 0, s = 0;
    const std::int32_t hx = signed_high(x);
    const std::int32_t ix = hx & 0x7FFFFFFF;
    if (ix < 0x3E300000) {
        if (static_cast<int>(x) == 0) {
            const std::uint32_t low = low_word(x);
            if (((static_cast<std::uint32_t>(ix) | low) | static_cast<std::uint32_t>(iy + 1)) == 0) {
                return one / std::fabs(x);
            }
            if (iy == 1) {
                return x;
            }
            // compute -1 / (x + y) carefully
            double a = 0, t = 0;
            z = w = x + y;
            z = with_low(z, 0);
            v = y - (z - x);
            t = a = -one / w;
            t = with_low(t, 0);
            s = std::fma(t, z, one);
            return std::fma(a, std::fma(t, v, s), t);
        }
    }
    if (ix >= 0x3FE59428) {
        if (hx < 0) {
            x = -x;
            y = -y;
        }
        z = pio4 - x;
        w = pio4lo - y;
        x = z + w;
        y = 0.0;
    }
    z = x * x;
    w = z * z;
    r = std::fma(w, std::fma(w, std::fma(w, std::fma(w, std::fma(w, T(11), T(9)), T(7)), T(5)), T(3)), T(1));
    v = z * std::fma(w, std::fma(w, std::fma(w, std::fma(w, std::fma(w, T(12), T(10)), T(8)), T(6)), T(4)), T(2));
    s = z * x;
    r = std::fma(z, std::fma(s, r + v, y), y);
    r = std::fma(T(0), s, r);
    w = x + r;
    if (ix >= 0x3FE59428) {
        v = iy;
        return (1 - ((hx >> 30) & 2)) * std::fma(-2.0, x - (w * w / (w + v) - r), v);
    }
    if (iy == 1) {
        return w;
    }
    // compute -1.0 / (x + r) accurately
    double a = 0, t = 0;
    z = w;
    z = with_low(z, 0);
    v = r - (z - x);
    t = a = -1.0 / w;
    t = with_low(t, 0);
    s = std::fma(t, z, 1.0);
    return std::fma(a, std::fma(t, v, s), t);
}

// ------------------------------------------------------------------ e_rem_pio2
constexpr std::int32_t npio2_hw[] = {
    0x3FF921FB, 0x400921FB, 0x4012D97C, 0x401921FB, 0x401F6A7A, 0x4022D97C, 0x4025FDBB, 0x402921FB,
    0x402C463A, 0x402F6A7A, 0x4031475C, 0x4032D97C, 0x40346B9C, 0x4035FDBB, 0x40378FDB, 0x403921FB,
    0x403AB41B, 0x403C463A, 0x403DD85A, 0x403F6A7A, 0x40407E4C, 0x4041475C, 0x4042106C, 0x4042D97C,
    0x4043A28C, 0x40446B9C, 0x404534AC, 0x4045FDBB, 0x4046C6CB, 0x40478FDB, 0x404858EB, 0x404921FB,
};

// |x| up to 2^19 pi/2 (every argument the gnome's voice produces is far below). Larger
// arguments need fdlibm's __kernel_rem_pio2, which is not ported; they fall back to
// the platform's remainder (never reached here).
std::int32_t rem_pio2(double x, double* y) {
    constexpr double zero = 0.00000000000000000000e+00;
    constexpr double half = 5.00000000000000000000e-01;
    constexpr double invpio2 = 6.36619772367581382433e-01;
    constexpr double pio2_1 = 1.57079632673412561417e+00;
    constexpr double pio2_1t = 6.07710050650619224932e-11;
    constexpr double pio2_2 = 6.07710050630396597660e-11;
    constexpr double pio2_2t = 2.02226624879595063154e-21;
    constexpr double pio2_3 = 2.02226624871116645580e-21;
    constexpr double pio2_3t = 8.47842766036889956997e-32;
    double z = 0, w = 0, t = 0, r = 0, fn = 0;
    const std::int32_t hx = signed_high(x);
    const std::int32_t ix = hx & 0x7FFFFFFF;
    if (ix <= 0x3FE921FB) {
        y[0] = x;
        y[1] = 0;
        return 0;
    }
    if (ix < 0x4002D97C) {
        if (hx > 0) {
            z = x - pio2_1;
            if (ix != 0x3FF921FB) {
                y[0] = z - pio2_1t;
                y[1] = (z - y[0]) - pio2_1t;
            } else {
                z -= pio2_2;
                y[0] = z - pio2_2t;
                y[1] = (z - y[0]) - pio2_2t;
            }
            return 1;
        }
        z = x + pio2_1;
        if (ix != 0x3FF921FB) {
            y[0] = z + pio2_1t;
            y[1] = (z - y[0]) + pio2_1t;
        } else {
            z += pio2_2;
            y[0] = z + pio2_2t;
            y[1] = (z - y[0]) + pio2_2t;
        }
        return -1;
    }
    if (ix <= 0x413921FB) {
        t = std::fabs(x);
        const auto n = static_cast<std::int32_t>(std::fma(t, invpio2, half));
        fn = static_cast<double>(n);
        r = std::fma(-fn, pio2_1, t);
        w = fn * pio2_1t;
        if (n < 32 && ix != npio2_hw[n - 1]) {
            y[0] = r - w;
        } else {
            const std::int32_t j = ix >> 20;
            y[0] = r - w;
            std::uint32_t high = high_word(y[0]);
            std::int32_t i = j - static_cast<std::int32_t>((high >> 20) & 0x7FF);
            if (i > 16) {
                t = r;
                w = fn * pio2_2;
                r = t - w;
                w = std::fma(fn, pio2_2t, -((t - r) - w));
                y[0] = r - w;
                high = high_word(y[0]);
                i = j - static_cast<std::int32_t>((high >> 20) & 0x7FF);
                if (i > 49) {
                    t = r;
                    w = fn * pio2_3;
                    r = t - w;
                    w = std::fma(fn, pio2_3t, -((t - r) - w));
                    y[0] = r - w;
                }
            }
        }
        y[1] = (r - y[0]) - w;
        if (hx < 0) {
            y[0] = -y[0];
            y[1] = -y[1];
            return -n;
        }
        return n;
    }
    if (ix >= 0x7FF00000) {
        y[0] = y[1] = x - x;
        return 0;
    }
    const double quadrants = std::nearbyint(x * invpio2);
    y[0] = std::remainder(x, 1.57079632679489661923);
    y[1] = zero;
    return static_cast<std::int32_t>(std::fmod(quadrants, 8.0));
}

}  // namespace

// ------------------------------------------------------------------ s_sin, s_cos, s_tan
double sin(double x) {
    double y[2] = {0, 0};
    const double z = 0.0;
    const std::int32_t ix = signed_high(x) & 0x7FFFFFFF;
    if (ix <= 0x3FE921FB) {
        return kernel_sin(x, z, 0);
    }
    if (ix >= 0x7FF00000) {
        return x - x;
    }
    const std::int32_t n = rem_pio2(x, y);
    switch (n & 3) {
        case 0:
            return kernel_sin(y[0], y[1], 1);
        case 1:
            return kernel_cos(y[0], y[1]);
        case 2:
            return -kernel_sin(y[0], y[1], 1);
        default:
            return -kernel_cos(y[0], y[1]);
    }
}

double cos(double x) {
    double y[2] = {0, 0};
    const double z = 0.0;
    const std::int32_t ix = signed_high(x) & 0x7FFFFFFF;
    if (ix <= 0x3FE921FB) {
        return kernel_cos(x, z);
    }
    if (ix >= 0x7FF00000) {
        return x - x;
    }
    const std::int32_t n = rem_pio2(x, y);
    switch (n & 3) {
        case 0:
            return kernel_cos(y[0], y[1]);
        case 1:
            return -kernel_sin(y[0], y[1], 1);
        case 2:
            return -kernel_cos(y[0], y[1]);
        default:
            return kernel_sin(y[0], y[1], 1);
    }
}

double tan(double x) {
    double y[2] = {0, 0};
    const double z = 0.0;
    const std::int32_t ix = signed_high(x) & 0x7FFFFFFF;
    if (ix <= 0x3FE921FB) {
        return kernel_tan(x, z, 1);
    }
    if (ix >= 0x7FF00000) {
        return x - x;
    }
    const std::int32_t n = rem_pio2(x, y);
    return kernel_tan(y[0], y[1], 1 - ((n & 1) << 1));
}

// ------------------------------------------------------------------ e_exp
double exp(double x) {
    constexpr double one = 1.0;
    constexpr double halF[2] = {0.5, -0.5};
    constexpr double o_threshold = 7.09782712893383973096e+02;
    constexpr double u_threshold = -7.45133219101941108420e+02;
    constexpr double ln2HI[2] = {6.93147180369123816490e-01, -6.93147180369123816490e-01};
    constexpr double ln2LO[2] = {1.90821492927058770002e-10, -1.90821492927058770002e-10};
    constexpr double invln2 = 1.44269504088896338700e+00;
    constexpr double P1 = 1.66666666666666019037e-01;
    constexpr double P2 = -2.77777777770155933842e-03;
    constexpr double P3 = 6.61375632143793436117e-05;
    constexpr double P4 = -1.65339022054652515390e-06;
    constexpr double P5 = 4.13813679705723846039e-08;
    constexpr double E = 2.718281828459045;
    constexpr double huge = 1.0e+300;
    constexpr double twom1000 = 9.33263618503218878990e-302;
    constexpr double two1023 = 8.988465674311579539e307;
    double y = 0, hi = 0.0, lo = 0.0, c = 0, t = 0, twopk = 0;
    std::int32_t k = 0;
    std::uint32_t hx = high_word(x);
    const auto xsb = static_cast<std::int32_t>((hx >> 31) & 1);
    hx &= 0x7fffffff;
    if (hx >= 0x40862E42) {
        if (hx >= 0x7ff00000) {
            const std::uint32_t lx = low_word(x);
            if (((hx & 0xfffff) | lx) != 0) {
                return x + x;
            }
            return (xsb == 0) ? x : 0.0;
        }
        if (x > o_threshold) {
            return huge * huge;
        }
        if (x < u_threshold) {
            return twom1000 * twom1000;
        }
    }
    if (hx > 0x3fd62e42) {
        if (hx < 0x3FF0A2B2) {
            if (x == 1.0) {
                return E;
            }
            hi = x - ln2HI[xsb];
            lo = ln2LO[xsb];
            k = 1 - xsb - xsb;
        } else {
            k = static_cast<std::int32_t>(std::fma(invln2, x, halF[xsb]));
            t = k;
            hi = std::fma(-t, ln2HI[0], x);
            lo = t * ln2LO[0];
        }
        x = hi - lo;
    } else if (hx < 0x3e300000) {
        if (huge + x > one) {
            return one + x;
        }
    } else {
        k = 0;
    }
    t = x * x;
    if (k >= -1021) {
        twopk = from_words(static_cast<std::uint32_t>(0x3ff00000 + (k << 20)), 0);
    } else {
        twopk = from_words(static_cast<std::uint32_t>(0x3ff00000 + ((k + 1000) << 20)), 0);
    }
    c = std::fma(-t, std::fma(t, std::fma(t, std::fma(t, std::fma(t, P5, P4), P3), P2), P1), x);
    if (k == 0) {
        return one - ((x * c) / (c - 2.0) - x);
    }
    y = one - ((lo - (x * c) / (2.0 - c)) - hi);
    if (k >= -1021) {
        if (k == 1024) {
            return y * 2.0 * two1023;
        }
        return y * twopk;
    }
    return y * twopk * twom1000;
}

// ------------------------------------------------------------------ s_expm1
double expm1(double x) {
    constexpr double one = 1.0;
    constexpr double tiny = 1.0e-300;
    constexpr double o_threshold = 7.09782712893383973096e+02;
    constexpr double ln2_hi = 6.93147180369123816490e-01;
    constexpr double ln2_lo = 1.90821492927058770002e-10;
    constexpr double invln2 = 1.44269504088896338700e+00;
    constexpr double Q[] = {1.0, -3.33333333333331316428e-02, 1.58730158725481460165e-03,
                            -7.93650757867487942473e-05, 4.00821782732936239552e-06,
                            -2.01099218183624371326e-07};
    constexpr double huge = 1.0e+300;
    double y = 0, hi = 0, lo = 0, c = 0, t = 0, e = 0, hxs = 0, hfx = 0, r1 = 0, twopk = 0;
    std::int32_t k = 0;
    std::uint32_t hx = high_word(x);
    const std::uint32_t xsb = hx & 0x80000000;
    hx &= 0x7fffffff;
    if (hx >= 0x4043687A) {
        if (hx >= 0x40862E42) {
            if (hx >= 0x7ff00000) {
                const std::uint32_t low = low_word(x);
                if (((hx & 0xfffff) | low) != 0) {
                    return x + x;
                }
                return (xsb == 0) ? x : -1.0;
            }
            if (x > o_threshold) {
                return huge * huge;
            }
        }
        if (xsb != 0) {
            if (x + tiny < 0.0) {
                return tiny - one;
            }
        }
    }
    if (hx > 0x3fd62e42) {
        if (hx < 0x3FF0A2B2) {
            if (xsb == 0) {
                hi = x - ln2_hi;
                lo = ln2_lo;
                k = 1;
            } else {
                hi = x + ln2_hi;
                lo = -ln2_lo;
                k = -1;
            }
        } else {
            k = static_cast<std::int32_t>(std::fma(invln2, x, ((xsb == 0) ? 0.5 : -0.5)));
            t = k;
            hi = std::fma(-t, ln2_hi, x);
            lo = t * ln2_lo;
        }
        x = hi - lo;
        c = (hi - x) - lo;
    } else if (hx < 0x3c900000) {
        t = huge + x;
        return x - (t - (huge + x));
    } else {
        k = 0;
    }
    hfx = 0.5 * x;
    hxs = x * hfx;
    r1 = std::fma(hxs, std::fma(hxs, std::fma(hxs, std::fma(hxs, std::fma(hxs, Q[5], Q[4]), Q[3]), Q[2]), Q[1]), one);
    t = std::fma(-r1, hfx, 3.0);
    e = hxs * ((r1 - t) / std::fma(-x, t, 6.0));
    if (k == 0) {
        return x - std::fma(x, e, -hxs);
    }
    twopk = from_words(static_cast<std::uint32_t>(0x3ff00000 + (k << 20)), 0);
    e = std::fma(x, e - c, -c);
    e -= hxs;
    if (k == -1) {
        return std::fma(0.5, x - e, -0.5);
    }
    if (k == 1) {
        if (x < -0.25) {
            return -2.0 * (e - (x + 0.5));
        }
        return std::fma(2.0, x - e, one);
    }
    if (k <= -2 || k > 56) {
        y = one - (e - x);
        if (k == 1024) {
            y = y * 2.0 * 8.988465674311579539e307;
        } else {
            y = y * twopk;
        }
        return y - one;
    }
    t = one;
    if (k < 20) {
        t = with_high(t, static_cast<std::uint32_t>(0x3ff00000 - (0x200000 >> k)));
        y = t - (e - x);
        y = y * twopk;
    } else {
        t = with_high(t, static_cast<std::uint32_t>((0x3ff - k) << 20));
        y = x - (e + t);
        y += one;
        y = y * twopk;
    }
    return y;
}

// ------------------------------------------------------------------ s_tanh
double tanh(double x) {
    constexpr double one = 1.0, two = 2.0, huge = 1.0e300, tiny = 1.0e-300;
    double t = 0, z = 0;
    const std::int32_t jx = signed_high(x);
    const std::int32_t ix = jx & 0x7fffffff;
    if (ix >= 0x7ff00000) {
        if (jx >= 0) {
            return one / x + one;
        }
        return one / x - one;
    }
    if (ix < 0x40360000) {
        if (ix < 0x3e300000) {
            if (huge + x > one) {
                return x;
            }
        }
        if (ix >= 0x3ff00000) {
            t = expm1(two * std::fabs(x));
            z = one - two / (t + two);
        } else {
            t = expm1(-two * std::fabs(x));
            z = -t / (t + two);
        }
    } else {
        z = one - tiny;
    }
    return (jx >= 0) ? z : -z;
}

// ------------------------------------------------------------------ pow
// V8 hands Math.pow to the platform's pow: identical to macOS libm over 8 million
// arguments in the gnome's ranges (2^((note - 69) / 12) and u^1.6), while fdlibm's
// e_pow, musl's (Arm's) pow and a correctly rounded pow each differ in 0.1-10% of them.
// macOS's pow is not correctly rounded (it misses by up to about 0.502 ulp near
// halfway cases) and its algorithm is not public, so no portable pow reproduces it;
// pow() is therefore the platform's. Natively on macOS the clips equal Node's bit for
// bit; elsewhere a last-bit pow difference can move one glottal cycle's start by a
// tick, which leaves that clip within about 3e-3 of its peak (mowing_cues_tests).
double pow(double x, double y) {
    return std::pow(x, y);
}

// ------------------------------------------------------------------ e_log2 (k_log.h)
namespace {
double k_log1p(double f) {
    constexpr double Lg1 = 6.666666666666735130e-01, Lg2 = 3.999999999940941908e-01,
                     Lg3 = 2.857142874366239149e-01, Lg4 = 2.222219843214978396e-01,
                     Lg5 = 1.818357216161805012e-01, Lg6 = 1.531383769920937332e-01,
                     Lg7 = 1.479819860511658591e-01;
    const double s = f / (2.0 + f);
    const double z = s * s;
    const double w = z * z;
    const double t1 = w * std::fma(w, std::fma(w, Lg6, Lg4), Lg2);
    const double t2 = z * std::fma(w, std::fma(w, std::fma(w, Lg7, Lg5), Lg3), Lg1);
    const double R = t2 + t1;
    const double hfsq = 0.5 * f * f;
    return s * (hfsq + R);
}
}  // namespace

double log2(double x) {
    constexpr double two54 = 1.80143985094819840000e+16;
    constexpr double ivln2hi = 1.44269504072144627571e+00;
    constexpr double ivln2lo = 1.67517131648865118353e-10;
    double f = 0, hfsq = 0, hi = 0, lo = 0, r = 0, val_hi = 0, val_lo = 0, w = 0, y = 0;
    std::int32_t i = 0, k = 0;
    std::int32_t hx = signed_high(x);
    const std::uint32_t lx = low_word(x);
    k = 0;
    if (hx < 0x00100000) {
        if (((hx & 0x7fffffff) | static_cast<std::int32_t>(lx)) == 0) {
            return -INFINITY;
        }
        if (hx < 0) {
            return NAN;
        }
        k -= 54;
        x *= two54;
        hx = signed_high(x);
    }
    if (hx >= 0x7ff00000) {
        return x + x;
    }
    if (hx == 0x3ff00000 && lx == 0) {
        return 0.0;
    }
    k += (hx >> 20) - 1023;
    hx &= 0x000fffff;
    i = (hx + 0x95f64) & 0x100000;
    x = with_high(x, static_cast<std::uint32_t>(hx | (i ^ 0x3ff00000)));
    k += (i >> 20);
    y = static_cast<double>(k);
    f = x - 1.0;
    hfsq = 0.5 * f * f;
    r = k_log1p(f);
    hi = f - hfsq;
    hi = with_low(hi, 0);
    lo = (f - hi) - hfsq + r;
    val_hi = hi * ivln2hi;
    val_lo = std::fma(lo + hi, ivln2lo, lo * ivln2hi);
    w = y + val_hi;
    val_lo += (y - w) + val_hi;
    val_hi = w;
    return val_lo + val_hi;
}

double round(double x) {
    const double r = std::floor(x);
    return (x - r >= 0.5) ? r + 1.0 : r;
}

}  // namespace mm::js
