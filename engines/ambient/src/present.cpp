#include "present.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

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

void build_column_map(int scene_width, int device_width, std::vector<std::uint32_t>& columns) {
    columns.resize(static_cast<std::size_t>(std::max(device_width, 0)));
    if (scene_width <= 0 || device_width <= 0)
        return;
    for (int x = 0; x < device_width; ++x) {
        const long long source = static_cast<long long>(x) * scene_width / device_width;
        columns[static_cast<std::size_t>(x)] =
            static_cast<std::uint32_t>(std::min<long long>(source, scene_width - 1));
    }
}

void present_nearest(const std::uint32_t* scene, int scene_width, int scene_height,
                     const std::vector<std::uint32_t>& columns, std::byte* destination, std::size_t row_bytes,
                     int device_width, int device_height) {
    if (scene == nullptr || destination == nullptr || scene_width <= 0 || scene_height <= 0 || device_width <= 0 ||
        device_height <= 0 || columns.size() != static_cast<std::size_t>(device_width))
        return;
    const std::size_t device_row_bytes = static_cast<std::size_t>(device_width) * 4U;
    int previous_source = -1;
    std::byte* previous_row = nullptr;
    for (int y = 0; y < device_height; ++y) {
        const int source_row = std::min(
            scene_height - 1, static_cast<int>(static_cast<long long>(y) * scene_height / device_height));
        std::byte* row = destination + static_cast<std::size_t>(y) * row_bytes;
        if (source_row == previous_source && previous_row != nullptr) {
            std::memcpy(row, previous_row, device_row_bytes);
            continue;
        }
        const std::uint32_t* source = scene + static_cast<std::size_t>(source_row) * static_cast<std::size_t>(scene_width);
        std::uint32_t* out = reinterpret_cast<std::uint32_t*>(row);
        for (int x = 0; x < device_width; ++x)
            out[x] = source[columns[static_cast<std::size_t>(x)]];
        previous_source = source_row;
        previous_row = row;
    }
}

} // namespace ambient
