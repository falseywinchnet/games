#include "present.hpp"

#include <algorithm>
#include <cmath>

namespace ambient {
namespace {

// Device pixels per scene pixel at scale 1, and the largest raster, per detail.
struct DetailPolicy {
    double points_per_pixel{};
    double pixel_budget{};
};

DetailPolicy policy_for(Detail detail) {
    DetailPolicy policy{2.0, 260000.0};
    if (detail == Detail::light)
        policy = {3.0, 120000.0};
    if (detail == Detail::fine)
        policy = {1.25, 620000.0};
    return policy;
}

} // namespace

SceneSize scene_size(int device_width, int device_height, double scale, Detail detail) {
    SceneSize size{};
    if (device_width <= 0 || device_height <= 0)
        return size;
    const DetailPolicy policy = policy_for(detail);
    const double safe_scale = std::isfinite(scale) && scale > 0 ? scale : 1.0;
    // Prefer a whole number of device pixels per scene pixel, so enlarging is exact.
    double factor = std::max(1.0, std::round(policy.points_per_pixel * safe_scale));
    double pixels = (static_cast<double>(device_width) / factor) * (static_cast<double>(device_height) / factor);
    if (pixels > policy.pixel_budget)
        factor *= std::sqrt(pixels / policy.pixel_budget);
    size.width = std::max(16, static_cast<int>(std::ceil(static_cast<double>(device_width) / factor)));
    size.height = std::max(16, static_cast<int>(std::ceil(static_cast<double>(device_height) / factor)));
    return size;
}

} // namespace ambient
