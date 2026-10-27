// Headless text for this game's tests and previews. Not part of PlaySuite: the
// application links src/platform/text.cpp instead. Face choice mirrors
// games::PortableMaskCache so previews use the fonts the application will.
#include "platform/text.hpp"

#include "headless_text.hpp"

#include <map>
#include <tuple>

namespace tg {
namespace {
using Key = std::tuple<std::string, int, double, double>;
std::map<Key, Mask> masks;
}

const Mask& text_mask(const std::string& utf8, Font font, double size, double wrap_width) {
    const int number = static_cast<int>(font);
    const Key key{utf8, number, size, wrap_width};
    std::map<Key, Mask>::const_iterator cached = masks.find(key);
    if (cached != masks.end()) {
        return (*cached).second;
    }
    const bool bold = number == 1 || number == 3 || number == 4;
    const bool mono = number == 2 || number == 3;
    kit::Face face = kit::Face::serif;
    if (mono) {
        face = bold ? kit::Face::mono_bold : kit::Face::mono;
    } else if (bold) {
        face = kit::Face::serif_bold;
    }
    kit::TextMask rendered = kit::render_text(utf8, face, size, wrap_width, mono);
    Mask mask;
    mask.w = rendered.w;
    mask.h = rendered.h;
    mask.a = std::move(rendered.a);
    return (*masks.emplace(key, std::move(mask)).first).second;
}

void text_cache_trim() {
    if (masks.size() > 700) {
        masks.clear();
    }
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
