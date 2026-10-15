#pragma once
// One card held up in the room and turned by hand, for any card the suite shows in three
// dimensions (Rainstar Software's business card is the first). The card is a slab of stock;
// each face is a set of maps made once at a size (the stock's colour, its ink, its relief and
// its glint facets) at every level of detail. Every time the card moves its picture is made
// anew a device pixel at a time: the eye's ray through the pixel is met with the slab, the face
// it strikes is read at the level its footprint wants, and that point is lit as it now faces
// the lamp and the room. So the dimples of the finish and the raised ink catch the lamp as the
// card turns, its facets flash one after another, the varnish's sheen slides across it, the cut
// edge of the stock (two plies, pressed together) shows when it is turned edge-on, and its
// shadow, when it is over a page, falls on the page beneath. card_print.hpp prints a card lying
// still on the table; this is the same stock in the hand.
//
// A pixel well inside a face is one ray; four rays are spent only on the card's outline and its
// edges, and pixels outside the slab's outline are not traced at all.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace games {

// A rotation, as a unit quaternion. Space is the screen's: x across, y down, z toward the eye.
struct Turn {
    double w = 1;
    double x = 0;
    double y = 0;
    double z = 0;
};
// About the axis (x, y, z), which need not be of unit length, by `angle` radians.
[[nodiscard]] Turn turn_about(double x, double y, double z, double angle);
// `first`, and then `then`.
[[nodiscard]] Turn turn_then(const Turn& first, const Turn& then);
// `amount` (0..1) of the way from `from` to `to`, the short way round.
[[nodiscard]] Turn turn_toward(const Turn& from, const Turn& to, double amount);
// The angle between two turns, radians (0..pi).
[[nodiscard]] double turn_apart(const Turn& a, const Turn& b);
// Whether a card turned so shows the eye its front.
[[nodiscard]] bool front_toward_eye(const Turn& turn);

// One texel of a face as it is made, seen from that face's own side.
struct CardTexel {
    float red = 0;
    float green = 0;
    float blue = 0;      // the stock, linear light
    float ink = 0;       // 0 bare stock .. 1 ink
    float foil = 0;      // 0 .. 1: of the ink here, how much turns to gold foil when asked
    float slope_x = 0;
    float slope_y = 0;   // the surface's rise over run, across and down
    float facet_x = 0;
    float facet_y = 0;   // a glint facet's tilt (rise over run) across and down
    float facet = 0;     // and its weight: 0 where there is none
};
// A face's maps as made: `width` x `height` texels, row-major.
struct CardFaceMap {
    int width = 0;
    int height = 0;
    std::vector<CardTexel> texels;
};
// A texel as kept: sixteen bytes, so a card as wide as the window costs tens of megabytes, not
// hundreds. Colour in linear light out of 65535; ink, foil and the facet's weight out of 255;
// slopes and facet tilts in fixed steps (held_card.cpp, "Packing").
struct PackedTexel {
    std::uint16_t red = 0;
    std::uint16_t green = 0;
    std::uint16_t blue = 0;
    std::uint8_t ink = 0;
    std::uint8_t foil = 0;
    std::int16_t slope_x = 0;
    std::int16_t slope_y = 0;
    std::int8_t facet_x = 0;
    std::int8_t facet_y = 0;
    std::uint8_t facet = 0;
    std::uint8_t spare = 0;
};
static_assert(sizeof(PackedTexel) == 16);
struct CardFaceLevel {
    int width = 0;
    int height = 0;
    std::vector<PackedTexel> texels;
};
// A face at every level of detail, each half the size of the one before.
struct CardFace {
    std::vector<CardFaceLevel> levels;
};
// `full` kept, and the levels below it.
[[nodiscard]] CardFace card_face_levels(const CardFaceMap& full);
// Facets of the glint scattered over `map`: in cells `grain` texels across, some hold one
// facet, a small round dot tilted at random; `seed` scatters them.
void scatter_card_facets(CardFaceMap& map, double grain, std::uint32_t seed);

// What the faces are printed in.
struct CardMaterial {
    double ink[3] = {.010, .009, .008};  // the ink's colour, linear light: a warm black
    double foil[3] = {1.0, .62, .13};    // gold foil's reflectance, linear light
    double edge[3] = {.55, .52, .45};    // the stock's cut edge, linear light
    double seam = .35;                   // how much darker the line is where the two plies meet
    bool gilt_edge = false;              // the edges painted gold
    double relief = 1;                   // scales every slope of the faces
    double sparkle = 1;                  // scales the glint facets' flashes (the card table's Sparkle)
    double sheen = 1;                    // scales the varnish's sheen and glare (its Glare)
};

// How the card is held. Lengths are points; the card's middle is at (`centre_x`, `centre_y`) in
// the picture, `lift` points above the page the picture lies on.
struct CardPose {
    double width = 252;
    double height = 144;
    double thickness = 2.3;
    double centre_x = 0;
    double centre_y = 0;
    double lift = 12;
    double eye = 1600;   // from the eye to the card's middle
    Turn turn;
    double foil = 0;     // 0 .. 1: how far the foil ink has turned to gold
    double shadow = 1;   // how strongly it shades the page: 0 where there is no page
};

// Where the card is drawn: premultiplied BGRA, `width` x `height` device pixels, `stride` bytes a
// row, `scale` device pixels a point.
struct CardPicture {
    std::byte* pixels = nullptr;
    int width = 0;
    int height = 0;
    std::size_t stride = 0;
    double scale = 1;
};
// A box of device pixels, half-open.
struct PixelBox {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
};
[[nodiscard]] bool box_empty(const PixelBox& box);
[[nodiscard]] PixelBox box_union(const PixelBox& a, const PixelBox& b);

// The pixels the card and its shadow may touch, clipped to the picture.
[[nodiscard]] PixelBox held_card_box(const CardPose& pose, const CardPicture& picture);
// Clears `box` of `picture` and draws there the card's shadow on the page and the card over it.
void draw_held_card(const CardFace& front, const CardFace& back, const CardMaterial& material,
                    const CardPose& pose, const CardPicture& picture, PixelBox box);

// Where the eye's ray through the picture's point (x, y), in points, meets the card: which face,
// and where on it, `u` across and `v` down that face as seen from its own side (0..1 each).
struct CardHit {
    bool hit = false;
    bool front = false;
    double u = 0;
    double v = 0;
};
[[nodiscard]] CardHit held_card_hit(const CardPose& pose, double x, double y);

}  // namespace games
