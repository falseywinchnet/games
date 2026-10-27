// Hosted text: the shared GUI.Forms text-mask service through PlaySuite's cache.
// Compiled only into the PlaySuite application (cmake/Application.cmake).
#include "text.hpp"

#include "portable_mask_cache.hpp"

namespace ts {
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

}  // namespace ts
