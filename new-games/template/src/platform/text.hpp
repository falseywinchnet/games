#pragma once
// Text as alpha masks, cached by content. The game asks for a mask and blends it
// with Canvas::draw_mask. Inside PlaySuite the masks come from the shared
// GUI.Forms text service and the bundled fonts (text.cpp). In the headless
// harness they come from the same font files through the kit's small rasteriser
// (dev/text_headless.cpp), so previews show real type.
//
// Sizes and wrap widths are in device pixels: multiply points by the scale first.
#include "raster.hpp"

#include <string>

namespace tg {

// speech: Libre Baskerville. pixel: Cousine, drawn without anti-aliasing.
// title: Libre Baskerville Bold. ui: Libre Baskerville.
enum class Font { speech, speech_bold, pixel, pixel_bold, title, ui };

// The reference stays valid until text_cache_trim(). wrap_width 0 means one line.
const Mask& text_mask(const std::string& utf8, Font font, double size, double wrap_width = 0);
// Call once at the end of a frame.
void text_cache_trim();

// Draws text with an optional drop shadow. Returns the drawn width in device pixels.
int draw_text(Canvas& canvas, const std::string& utf8, Font font, double size, int x, int y,
              Col color, int scale = 1, Col shadow = {0, 0, 0, 0}, double wrap_width = 0);

}  // namespace tg
