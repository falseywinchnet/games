#pragma once
// Discrete Fourier transforms of any length, with NumPy's rfft/irfft conventions:
// rfft(x, n) zero-pads or truncates x to n samples and returns n / 2 + 1 bins;
// irfft(X, n) reads bins 0..n / 2 (ignoring the imaginary parts of the bins that must
// be real) and divides by n. Every length uses a mixed-radix transform; a prime factor
// above 61 is transformed inside it by Bluestein's chirp at that factor's size, so an
// awkward length never needs a power-of-two transform twice its own size. The results
// agree with NumPy's pocketfft to rounding (about 1e-15 of the signal's size), not bit
// for bit.
//
// Standard library only. Twiddle tables and Bluestein kernels of up to 2^20 points are
// kept in a small cache behind a mutex (48 MB at most), so any thread may call.
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
