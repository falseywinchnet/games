#pragma once
// The bookcase: PlaySuite's own shelf (its walnut wall and oak plank, src/wood.hpp) with
// the library's books standing on it spine out, each bound in its cloth or leather and
// lettered in gold. Pure drawing into the view's canvas; the view keeps the state.
#include "library.hpp"
#include "platform/raster.hpp"
#include "printer.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace pl {

// The shell's two woods, made at one display scale: premultiplied BGRA tiles.
struct WoodTiles {
    double scale = 0;
    int wall_width = 0, wall_height = 0;
    int plank_width = 0, plank_height = 0;
    std::vector<std::byte> wall;
    std::vector<std::byte> plank;
};
// Makes the tiles for `scale` (device pixels a point), unless they already are.
void make_wood_tiles(double scale, WoodTiles& tiles);

// Fills a rectangle of device pixels with a tile repeated from the canvas's origin,
// darkened by `shade` (1 leaves it as it is).
void fill_with_tile(Canvas& canvas, const std::vector<std::byte>& tile, int tile_width, int tile_height, int x,
                    int y, int width, int height, float shade);

// Where each book stands, in points.
struct SpineBox {
    double x = 0, y = 0, width = 0, height = 0;
};
struct ShelfLayout {
    double width = 0, height = 0;  // the view, in points
    double plank = 0;              // the plank's top, where the books stand
    std::vector<SpineBox> spines;  // one per shelf book, in shelf order
};

[[nodiscard]] ShelfLayout lay_out_shelf(double width, double height, const std::vector<ShelfBook>& books);
// The book under a point, or -1.
[[nodiscard]] int spine_at(const ShelfLayout& layout, double x, double y);

// `lifted` is the book drawn a little raised (under the pointer or the keyboard), or -1.
void draw_shelf(Canvas& canvas, double scale, const ShelfLayout& layout, const std::vector<ShelfBook>& books,
                int lifted, const WoodTiles& wood, Printer& printer);

// The game's own help, a card of the library's paper over whatever is showing: shown
// when the game runs outside the shell, whose help book it otherwise opens.
void draw_help_card(Canvas& canvas, double scale, Printer& printer, const std::vector<std::string>& paragraphs);
[[nodiscard]] std::vector<std::string> help_paragraphs();

}  // namespace pl
