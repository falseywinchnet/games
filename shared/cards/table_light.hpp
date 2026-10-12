#pragma once
// The light on a card's face. A room was traced once, offline (authoring/table_light): a
// large room, the table near a wall of glass ahead of the player, the sun coming in from the
// side, ceiling lights and everything in the room passing light on. The cards are paper
// under a varnish. Because the room never changes and the cards lie flat, what it does to a
// card depends only on where the card lies before the player's eyes, and comes to
//
//     seen = face x level + gloss            (in the screen's own sRGB values)
//
// `level` is the light on the paper, through its varnish: 1 where it is best lit, a little
// less towards the far side, where the varnish is seen at a grazing angle and lets less
// through. `gloss` is the room the varnish mirrors towards the eyes: it lifts dark ink, and
// most at the far side and towards the glass. The table draws it as one overlay over each
// card, clipped to the card, so a card moved across the table changes its gloss.
#include <cstddef>
#include <vector>

namespace games {

struct TableLight {
    int width = 0, height = 0;    // cells
    std::vector<std::byte> face;  // premultiplied BGRA: alpha 1 - level, colour the gloss
};

// For a table `width` x `height` points whose cards are `card_width` points wide (a card is
// 63 mm across, which sets the scale), the player's eyes centred 30 cm behind its near edge
// and 42 cm above it; cells of about `cell` points.
[[nodiscard]] TableLight light_table(double width, double height, double card_width,
                                     double cell = 8);

namespace detail {
// The bake (table_light_bake.cpp, generated): a grid 1.2 m across and 1 m deep before the
// eyes, row by row from the far side, `light_level` one value a point, `light_gloss` three.
extern const float light_level[];
extern const float light_gloss[];
}  // namespace detail

}  // namespace games
