#pragma once
// The paint snail's repaints. Every one of them moves: most by frames (eyes that glance
// about and blink, plasma that flows, stripes that sway), the debug rain and the patterns
// by the renderer (columns that fall, patterns that creep up the wall), and the great eye
// by watching whoever stands in front of it. The list's order is kept: a painted face
// stores its index.
#include "soft3d.hpp"

#include <cstdint>

namespace mz {

int paint_count();
const Tex32& tex_paint(int i);  // the first frame
// The frame showing at `time` on a face with its own `phase`, so neighbouring faces don't
// move in step. `gaze` (-1 left .. 1 right) is where the viewer stands across the face,
// for a paint that watches.
const Tex32& tex_paint_at(int i, double time, std::uint32_t phase, double gaze);
std::uint32_t paint_flags(int i);  // the Soft3D flags it is drawn with: kGlitch, kRain or kSlide

}  // namespace mz
