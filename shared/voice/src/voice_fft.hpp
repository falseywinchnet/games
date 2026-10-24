#pragma once
// Discrete Fourier transforms of any length, with NumPy's rfft/irfft conventions:
// rfft(x, n) zero-pads or truncates x to n samples and returns n / 2 + 1 bins;
// irfft(X, n) reads bins 0..n / 2 (ignoring the imaginary parts of the bins that must
// be real) and divides by n. Lengths with only small prime factors use a mixed-radix
// transform; a length with a large prime factor goes through Bluestein's chirp. The
// results agree with NumPy's pocketfft to rounding (about 1e-15 of the signal's size),
// not bit for bit.
//
// Standard library only; no state is shared between calls, so any thread may call.
#include <complex>
#include <cstddef>
#include <span>
#include <vector>

namespace voice {

using Complex = std::complex<double>;

// The complex DFT of `data` in place: X[k] = sum x[j] exp(-+2 pi i j k / n), unscaled.
// `inverse` uses the positive exponent. Any length; 0 and 1 are identities.
void fft(std::vector<Complex>& data, bool inverse);

// np.fft.rfft(x, n): `out` receives n / 2 + 1 bins.
void rfft(std::span<const double> x, std::size_t n, std::vector<Complex>& out);

// np.fft.irfft(spectrum, n): `out` receives n samples.
void irfft(std::span<const Complex> spectrum, std::size_t n, std::vector<double>& out);

}  // namespace voice
