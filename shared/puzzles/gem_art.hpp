#pragma once
// Gems drawn by the render engine. Each stone is a fan of shaded facets (r3d's triangles,
// depth tested, nearest first so the depth test discards what the stone covers) drawn
// into a premultiplied layer that the board shows through; the layer is then laid into
// the window's own buffer, averaged down when it was drawn finer than the window.
#include "r3d.hpp"
#include "target.hpp"

namespace games {

// One gem centred at (x, y) with `radius`, in the layer's pixels. `value` is the board's
// cell value (colour, bomb, star, hypercube), `angle` turns it, `glisten` (0..1) lights
// its sparkle, and `lift` (negative) brings a lifted gem in front of its neighbours.
void draw_gem(render::r3d::Pass& pass, double x, double y, double radius, int value, double angle,
              double glisten, float lift);
// Its sparkle alone, a cross of light growing as `glisten` passes .7 (nothing below): for a
// gem copied from a picture drawn without it (PuzzleView's resting board).
void draw_gem_sparkle(render::r3d::Pass& pass, double x, double y, double radius, double glisten,
                      float lift);

// Lays `layer` (premultiplied, in the target's byte order, `scale` (1 or 2) layer pixels to
// one target pixel, its origin at the target's (left, top)) over the target inside `area`,
// each target pixel the mean of the layer pixels over it.
void lay_layer(const render::Target& target, render::Rect area, const render::Target& layer,
               int left, int top, int scale);

// The outline of a rounded rectangle in device pixels, anti-aliased, at `alpha` (0..1).
void rounded_outline(const render::Target& target, render::Rect clip, double x, double y,
                     double w, double h, double radius, render::Color color, float alpha,
                     double line_width);

} // namespace games
