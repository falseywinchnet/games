// Hosted text: the shared GUI.Forms text-mask service through PlaySuite's cache.
// Compiled only into the PlaySuite application (cmake/Application.cmake).
#include "text.hpp"

#include "portable_mask_cache.hpp"

namespace tg {
namespace {
games::PortableMaskCache<Mask> masks;
}

const Mask& text_mask(const std::string& utf8, Font font, double size, double wrap_width) {
    const Mask& mask = masks.get(utf8, static_cast<int>(font), size, wrap_width);
    return mask;
}

void text_cache_trim() {
    masks.trim();
}

int draw_text(Canvas& canvas, const std::string& utf8, Font font, double size, int x, int y,
              Col color, int scale, Col shadow, double wrap_width) {
    const Mask& mask = text_mask(utf8, font, size, wrap_width);
    if (shadow.a > 0) {
        canvas.draw_mask(mask, x + scale, y + scale, shadow, scale);
    }
    canvas.draw_mask(mask, x, y, color, scale);
    const int width = mask.w * scale;
    return width;
}

}  // namespace tg
