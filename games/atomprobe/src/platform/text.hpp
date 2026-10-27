#pragma once
// Text rasterised to alpha masks with CoreText, cached by content.
// `pixel` text is drawn without anti-aliasing at a small size and is meant
// to be blitted at 2x or 3x for a chunky 1990s look.
#include "render.hpp"

#include <string>

namespace ap {

enum class Font { speech, speech_bold, pixel, pixel_bold, title, ui };

const Mask& text_mask(const std::string& utf8, Font font, double size, double wrap_width = 0);
void text_cache_trim();

// Draw text with an optional 1-pixel-per-scale drop shadow. Returns the drawn width.
int draw_text(Canvas& c, const std::string& s, Font f, double size, int x, int y, Col col,
              int scale = 1, Col shadow = {0, 0, 0, 0}, double wrap = 0);

}  // namespace ap
