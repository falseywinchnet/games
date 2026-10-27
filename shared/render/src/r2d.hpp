#pragma once
// r2d: the few pixel operations a game needs around its 3D picture, written straight
// into the target in its own byte order. Text comes from GUI.Forms' text service as
// coverage masks; anything richer belongs to GUI.Forms' painter. Every operation is
// bounded by a clip rectangle, so a frame touches only what it repairs.
#include "target.hpp"

#include <cstdint>
#include <span>

namespace render::r2d {

// Copies `area` from `source` (the same size and order as the target): the cheapest
// way to put back a kept layer under something that moved.
void restore(const Target& target, const Target& source, Rect area);
// Fills `area` with one opaque colour.
void fill(const Target& target, Rect area, Color color);
// Lays `color` over `area` at `alpha` (0..1): dimming under a panel.
void tint(const Target& target, Rect area, Color color, float alpha);
// Blends `color` through an 8-bit coverage mask placed at (x, y).
void mask(const Target& target, Rect clip, int x, int y, int width, int height, std::span<const std::uint8_t> coverage,
          Color color);
// A rounded rectangle in device pixels, anti-aliased, filled with `fill` at `alpha` and,
// when `line_width` > 0, outlined with `line`.
void rounded(const Target& target, Rect clip, double x, double y, double w, double h, double radius, Color fill,
             float alpha, Color line, double line_width);
// Lays a premultiplied layer (same size and order) over the target inside `area` at
// `opacity`: something fading in or out, drawn on its own first.
void over(const Target& target, const Target& layer, Rect area, float opacity);

}  // namespace render::r2d
