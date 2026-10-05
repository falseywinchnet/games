#pragma once
// From a scene raster to the device surface: choosing the scene's size for a view
// and enlarging it, nearest pixel, into the surface the window shows.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ambient {

// Detail chooses the size of a scene pixel. Fine draws smaller pixels and costs
// more; Light draws larger ones and costs less.
enum class Detail : std::uint8_t { light, balanced, fine };

struct SceneSize {
    int width{};
    int height{};
};

// The scene raster for a view of `device_width` x `device_height` pixels at
// `scale` device pixels per point. Each scene pixel covers a whole number of
// device pixels where possible, and the raster never exceeds the detail's pixel
// budget, so a full-screen window costs no more than a large one.
[[nodiscard]] SceneSize scene_size(int device_width, int device_height, double scale, Detail detail);

// Maps each device column to its scene column; reused across frames.
void build_column_map(int scene_width, int device_width, std::vector<std::uint32_t>& columns);

// Enlarges `scene` (scene_width x scene_height, 0xAARRGGBB) to the device surface
// with nearest-pixel sampling. Rows repeated from the previous device row are
// copied whole. `destination` rows are `row_bytes` apart.
void present_nearest(const std::uint32_t* scene, int scene_width, int scene_height,
                     const std::vector<std::uint32_t>& columns, std::byte* destination, std::size_t row_bytes,
                     int device_width, int device_height);

} // namespace ambient
