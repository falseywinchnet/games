#pragma once
// The scene raster's size for a view: a detail chooses the size of a scene pixel, and
// the renderer's PixelSurface enlarges the raster into the window (scene_view.cpp).
#include <cstdint>

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

} // namespace ambient
