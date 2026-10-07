#pragma once
// How often an ambient scene redraws, decided from what it actually costs here.
//
// A scene has two rates: frames (creatures, animated light) and sway updates
// (foliage). Each has a preferred and a lowest rate. The governor measures the
// processor time of each kind of work and keeps the total inside a budget: a
// fraction of one core. On a fast machine both rates stay at their preferred
// values; on a slow one, or for a large window, they fall toward their floors,
// foliage first. It never changes what is drawn, only how often.
#include <cstdint>

namespace ambient {

struct CadenceLimits {
    double frames_preferred{24};
    double frames_lowest{12};
    double sway_preferred{8};
    double sway_lowest{3};
    double budget{0.12};  // fraction of one processor core
};

struct Rates {
    double frames{};  // per second
    double sway{};    // per second
};

class Governor final {
  public:
    explicit Governor(CadenceLimits limits = {});
    void set_limits(CadenceLimits limits);
    // Measured processor time of one composed frame and one sway update, seconds.
    void record_frame(double seconds);
    void record_sway(double seconds);
    // Forgets measurements, e.g. after the scene size changes.
    void reset();
    [[nodiscard]] Rates rates() const;
    // Estimated fraction of a core the current rates cost.
    [[nodiscard]] double load() const;
    [[nodiscard]] const CadenceLimits& limits() const {
        return limits_;
    }

  private:
    CadenceLimits limits_{};
    double frame_cost_{};  // smoothed seconds per frame; 0 until measured
    double sway_cost_{};
    std::uint32_t frame_samples_{};
    std::uint32_t sway_samples_{};
};

} // namespace ambient
