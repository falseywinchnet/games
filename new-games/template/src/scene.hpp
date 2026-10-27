#pragma once
// Draws one complete frame of the game into a canvas. It reads the board, the
// layout and the visual state and changes none of them, so the same inputs give
// the same pixels: the view publishes the result, the preview tool writes it to
// a PNG, and neither needs the other.
#include "platform/raster.hpp"
#include "rules.hpp"
#include "stage.hpp"

#include <string>
#include <vector>

namespace tg {

// `scale` is device pixels per point. The canvas must already be sized to the
// surface in device pixels (the layout's size times the scale, rounded).
void draw_scene(Canvas& canvas, double scale, const Layout& layout, const Board& board,
                const Visual& visual);

// The help card's paragraphs, first the title. Kept here so tests can check the words.
[[nodiscard]] std::vector<std::string> help_text();

}  // namespace tg
