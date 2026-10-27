#pragma once
// The card table's own pictures of its cards. A card at rest is drawn as one finished
// picture, made here at exact device pixels and copied to the screen whole: the artwork
// printed into the paper it lies on, in the room's light where it lies. A card in motion is
// moved as that same picture, and printed again where it lands (as the Nature Cube draws in
// draft while it turns and sharpens once it settles). Drawing four filtered image layers for
// every card on every frame cost a drag across the columns most of its time.
//
// The paper: the paper engine's copy stock (shared/paper) in the artwork's own cool white,
// its fibres at random; the air-cushion finish, dimples drawn at the card's size and spaced in
// device pixels (finer than about two pixels they average away to tone); the lamp's falloff
// across the card and the rolled edge. The artwork was printed on a flat stock of #FBFCFC: what its ink takes from
// that stock is multiplied into the paper, in linear light. Exposure sets the paper's
// average, highlights roll off toward white, and the room's light (table_light.hpp) is laid
// on in the screen's own values, as the table draws it. The glint is light caught by the
// finish's facets turned toward the lamp, and the lamp's glare in the varnish.
#include "table_light.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace games {

// Card artwork: straight-alpha sRGB bytes (red, green, blue, alpha), row-major.
struct CardArt {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};
// An 8-bit RGB or RGBA, non-interlaced PNG (as every card image in the collection is).
// On failure `out` is left as it was.
[[nodiscard]] bool decode_card_png(std::span<const std::uint8_t> file, CardArt& out);
[[nodiscard]] bool load_card_png(const std::string& path, CardArt& out);
// From premultiplied BGRA (a drawn back).
[[nodiscard]] CardArt card_art_from_bgra(int width, int height, std::span<const std::byte> bgra);
// Reduced (or enlarged) to width x height by area, in linear light with premultiplied alpha:
// made once per card size, so cheap and honest rather than the offline CONV.
void reduce_card_art(const CardArt& source, int width, int height, CardArt& out);
// Premultiplied BGRA, as the window takes images.
[[nodiscard]] std::vector<std::byte> premultiplied_bgra(const CardArt& art);

// The paper every card is printed on, at one card size in device pixels: its light in linear
// terms (three channels a pixel, exposure applied, before the roll-off) and the card's
// coverage of each pixel.
struct CardPaper {
    int width = 0;
    int height = 0;
    std::vector<float> light;   // width * height * 3
    std::vector<float> cover;   // width * height
    std::vector<float> emboss;  // width * height: the finish's light, in standard deviations
};
// `emboss_depth`: the finish's depth, as the standard deviation of its light in sRGB levels;
// `emboss_pitch`: its dimples' spacing in device pixels.
[[nodiscard]] CardPaper make_card_paper(int width, int height, double pixels_per_point, double emboss_depth,
                                        double emboss_pitch);
// The finish's relief, for a card turned in the light rather than printed flat (held_card.hpp):
// the slope of its dimpled surface (rise over run, as the flat print lights it) across and then
// down, averaged over each pixel; width * height pairs, row-major, into `slopes`.
void card_finish_slopes(int width, int height, double emboss_pitch, std::vector<float>& slopes);
// The paper as a layer over a face drawn the old way, for a card whose picture is not made
// yet: black, as deep as the paper is darker than the stock (a multiply; it never lightens).
[[nodiscard]] std::vector<std::byte> paper_overlay(const CardPaper& paper);

// Where a card's picture reads the room's light: the table's light grid, the table's size in
// device pixels, and the card's top-left there.
struct LightAt {
    const TableLight* light = nullptr;
    double table_width = 0;
    double table_height = 0;
    int x = 0;
    int y = 0;
};
// The glint of the card under the pointer. `sparkle` (0 none .. 1): facets of the finish that
// face the lamp flashing white, on the paper, most of them where the glare falls; `grain`: a
// facet's size in device pixels (the finish's own dimples are finer than a pixel, so a glint
// drawn from them only brightens); `glare` (0..1): the lamp mirrored in the varnish, a soft
// band across paper and ink. `seed` scatters each card's facets differently.
struct CardGlint {
    double sparkle = 0;
    double glare = 0;
    double grain = 3;
    std::uint32_t seed = 0;
};
// A card printed: `art` (at the paper's size) on `paper`, in the room's light, glinting as
// asked. Premultiplied BGRA, the paper's size, into `out`.
void print_card(const CardArt& art, const CardPaper& paper, const LightAt& light, const CardGlint& glint,
                std::vector<std::byte>& out);

}  // namespace games
