#include "cadence.hpp"

#include <algorithm>
#include <cmath>

namespace ambient {
namespace {

// Exponential smoothing, with a plain mean for the first few samples so the first
// estimate is not dominated by a cold cache.
double smooth(double previous, double sample, std::uint32_t samples) {
    if (samples == 0)
        return sample;
    const double weight = samples < 8 ? 1.0 / static_cast<double>(samples + 1) : 0.125;
    const double result = previous + (sample - previous) * weight;
    return result;
}

bool usable(double seconds) {
    const bool result = std::isfinite(seconds) && seconds >= 0 && seconds < 5;
    return result;
}

} // namespace

Governor::Governor(CadenceLimits limits) : limits_(limits) {}

void Governor::set_limits(CadenceLimits limits) {
    limits_ = limits;
}

void Governor::record_frame(double seconds) {
    if (!usable(seconds))
        return;
    frame_cost_ = smooth(frame_cost_, seconds, frame_samples_);
    if (frame_samples_ < 1000000U)
        ++frame_samples_;
}

void Governor::record_sway(double seconds) {
    if (!usable(seconds))
        return;
    sway_cost_ = smooth(sway_cost_, seconds, sway_samples_);
    if (sway_samples_ < 1000000U)
        ++sway_samples_;
}

void Governor::reset() {
    frame_cost_ = 0;
    sway_cost_ = 0;
    frame_samples_ = 0;
    sway_samples_ = 0;
}

Rates Governor::rates() const {
    Rates rates{limits_.frames_preferred, limits_.sway_preferred};
    const double budget = std::max(limits_.budget, 0.001);
    // Lower the sway rate first, down to its floor; then the frame rate.
    const double at_preferred = frame_cost_ * rates.frames + sway_cost_ * rates.sway;
    if (at_preferred <= budget)
        return rates;
    const double frame_share = frame_cost_ * rates.frames;
    if (sway_cost_ > 0) {
        const double sway = (budget - frame_share) / sway_cost_;
        rates.sway = std::clamp(sway, limits_.sway_lowest, limits_.sway_preferred);
    }
    const double sway_share = sway_cost_ * rates.sway;
    if (frame_cost_ > 0 && frame_share + sway_share > budget) {
        const double frames = (budget - sway_share) / frame_cost_;
        rates.frames = std::clamp(frames, limits_.frames_lowest, limits_.frames_preferred);
    }
    // Foliage never updates more often than frames are drawn.
    rates.sway = std::min(rates.sway, rates.frames);
    return rates;
}

double Governor::load() const {
    const Rates current = rates();
    const double result = frame_cost_ * current.frames + sway_cost_ * current.sway;
    return result;
}

} // namespace ambient
