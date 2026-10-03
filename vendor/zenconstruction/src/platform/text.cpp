#include "text.hpp"
#include "portable_mask_cache.hpp"
namespace zc {
namespace { games::PortableMaskCache<Mask> masks; }
const Mask& text_mask(const std::string& text, Font font, double size, double wrap) {
    return masks.get(text, static_cast<int>(font), size, wrap);
}
void text_cache_trim() { masks.trim(); }
int draw_text(Canvas& canvas, const std::string& text, Font font, double size, int x, int y, Col color, int scale, Col shadow, double wrap) {
    const Mask& mask = text_mask(text, font, size, wrap);
    if (shadow.a > 0) canvas.draw_mask(mask, x + scale, y + scale, shadow, scale);
    canvas.draw_mask(mask, x, y, color, scale);
    return mask.w * scale;
}
}
