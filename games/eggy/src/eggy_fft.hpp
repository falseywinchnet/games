#pragma once
// Fast discrete Fourier transforms of any length for the music's whole-loop filters
// (a 117 s loop is one 5.6-million-sample circular transform per channel and stage).
// numpy's rfft/irfft conventions, as voice::rfft/irfft, computed by a self-sorting
// (Stockham) mixed-radix transform whose stages are radix 4, 2, 3 and 5, any other
// prime up to 61 directly, and a larger prime factor (a loop length of 34286 samples
// per beat has 17143) by Bluestein's algorithm on that factor alone, so the work stays
// small and local. Twiddles are exact to rounding (each from its own reduced angle), so
// results agree with numpy's pocketfft to about 1e-15 of the signal, as voice_fft does.
//
// A Plan is made once per length and reused; it is not shared between threads.
// Standard library only.
#include "voice_fft.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace eggy::fft {

using voice::Complex;

class ComplexPlan;

// np.fft.rfft(x, n) and np.fft.irfft(X, n) for one length n.
class RealPlan final {
  public:
    explicit RealPlan(std::size_t n);
    ~RealPlan();
    RealPlan(RealPlan&&) noexcept;
    RealPlan& operator=(RealPlan&&) noexcept;
    RealPlan(const RealPlan&) = delete;
    RealPlan& operator=(const RealPlan&) = delete;

    [[nodiscard]] std::size_t size() const {
        return n_;
    }
    // n / 2 + 1 bins of x zero-padded or cut to n samples.
    void forward(std::span<const double> x, std::vector<Complex>& out);
    // n samples from bins 0 .. n / 2 (the imaginary parts of bins 0 and n / 2 ignored).
    void inverse(std::span<const Complex> spectrum, std::vector<double>& out);

  private:
    std::size_t n_{};
    std::unique_ptr<ComplexPlan> plan_{};
    std::vector<Complex> unpack_{};  // exp(-2 pi i k / n) for the even-length packing
};

// synth.py's fft_filter(x, lo, hi, order, pad, circular) on these transforms: the same
// Butterworth magnitudes on the same rfft bins (lo / hi of 0 mean None).
[[nodiscard]] std::vector<double> filter(std::span<const double> x, double lo, double hi, int order = 2,
                                         std::size_t pad = 4096, bool circular = false);

}  // namespace eggy::fft
