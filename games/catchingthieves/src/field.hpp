#pragma once
// The field around the garden: meadow grass grown blade by blade by the shared grass
// engine (shared/grass) for the season, with its wildflowers, and folded so it wraps
// without a seam. It is a picture of the ground in world units, laid on the lawn.
//
// Growing it takes a few tenths of a second on every core, so the view grows it on a
// worker when the season changes and the garden shows the plain lawn until it is ready.
#include "garden.hpp"
#include "platform/render.hpp"

#include <atomic>

namespace ct {

struct FieldArt {
    Season season = Season::spring;
    Tex tex;             // square, a power of two, wrapping seamlessly, with its mips
    double units = 0;    // world units it covers each way; texture coordinate = world / units
    [[nodiscard]] bool ready() const { return units > 0 && !tex.px.empty(); }
};

// Grows the field for a season. Returns an empty field when `cancel` is set while it grows.
[[nodiscard]] FieldArt make_field(Season season, const std::atomic<bool>* cancel);

}  // namespace ct
