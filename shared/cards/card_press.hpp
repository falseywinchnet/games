#pragma once
// A deck printed as its cards lie on a table, for any card game: the paper (card_print.hpp)
// at the cards' device size and the chosen finish, the room's light over the table
// (table_light.hpp), each card's artwork brought to the paper's size the first time it is
// wanted, the card shadows at that size, and the pictures of a batch of cards printed on
// every core at once. It knows nothing of a game's rules or of a window: the game says
// which cards are out of date and where they lie, and shows the pixels it is given (the
// patience table as window images, Koi-Koi in its own frame).
#include "card_print.hpp"
#include "table_light.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace games {

// The finish the Settings screen chooses (SuiteSettings card_emboss .. card_glare).
struct CardFinish {
    double emboss = .4, emboss_size = .45;          // the paper's dimples: depth and spacing
    double sparkle = 1, grain = .4, glare = .7;     // the glint of the card under the pointer
};

// One card to print: a face by its place in the deck, or a back by its design.
struct PressSheet {
    int face = -1;            // -1 for a back
    int design = -1;          // the back's design (an index into the deck's backs)
    int x = 0, y = 0;         // where it lies: its top-left, device pixels in the table
    bool glint = false;       // under the pointer
    std::uint32_t seed = 0;   // scatters the glint's facets
};

class CardPress {
  public:
    // The deck's artwork: a PNG per face, and per back design a PNG or an empty path for a
    // back drawn here (card_backs.hpp, by its design number).
    void set_deck(std::vector<std::string> faces, std::vector<std::string> backs);
    // The cards' size in device pixels at `scale` device pixels a point, in `finish`. True
    // when the paper was made anew: every picture is then out of date (generation()).
    bool prepare(int width, int height, double scale, const CardFinish& finish);
    // The room's light over a table of `width` x `height` points whose cards are
    // `card_width` points wide. True when it changed (generation() too).
    bool light(double width, double height, double card_width);
    // A new finish for the glint alone (the paper's own is prepare()'s).
    void set_glint(const CardFinish& finish) {
        finish_.sparkle = finish.sparkle;
        finish_.grain = finish.grain;
        finish_.glare = finish.glare;
    }
    [[nodiscard]] const CardPaper& paper() const {
        return paper_;
    }
    [[nodiscard]] const TableLight& light_grid() const {
        return light_;
    }
    [[nodiscard]] double scale() const {
        return scale_;
    }
    // Counts changes of the paper and the light: a picture printed at another generation is
    // out of date.
    [[nodiscard]] int generation() const {
        return generation_;
    }
    [[nodiscard]] bool ready() const {
        return paper_.width > 0 && light_.width > 0;
    }
    // The pictures of `sheets`, premultiplied BGRA at the paper's size, one per sheet in
    // order; empty where the artwork could not be had (the game draws that card the old way).
    [[nodiscard]] std::vector<std::vector<std::byte>> print(std::span<const PressSheet> sheets);
    // A card's shadow at the paper's size, at rest or in the hand (larger, as the card rides
    // higher): premultiplied BGRA of `width` x `height`, about the card with `margin()` all
    // round. Empty without the shadow artwork (card_shadow.png).
    [[nodiscard]] std::vector<std::byte> shadow(bool lifted, int& width, int& height);
    [[nodiscard]] double margin() const {
        return 40.0 * paper_.width / card_art_width_points;
    }
    // Lets go of every picture's artwork and the paper (the window went away).
    void forget();

  private:
    static constexpr double card_art_width_points = 360;  // the artwork's card, whose shadow has 40 px round it
    std::vector<std::string> face_paths_, back_paths_;
    std::vector<CardArt> faces_, backs_;
    std::vector<bool> face_failed_, back_failed_;
    CardArt shadow_art_;
    CardPaper paper_;
    TableLight light_;
    CardFinish finish_;
    double scale_ = 1;
    double table_width_ = 0, table_height_ = 0, card_width_ = 0;
    int generation_ = 0;
};

}  // namespace games
