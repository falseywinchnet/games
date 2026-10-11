#pragma once
// Card backs drawn in code, beside the four painted ones (back_0..3.png): a white
// border, a printed panel and a fine rule round it, at the painted backs' size and
// corner, with each pattern antialiased at the pixels it is made for.
//
//   4  Sapphire lattice  a fine white crosshatch with a small diamond at each crossing
//   5  Ruby rings        interlocking rings, gold on deep red
//   6  Forest tartan     a woven plaid of greens with a thin gold overcheck
//   7  Midnight star     a compass star in a ring of small stars on violet
#include <cstddef>
#include <vector>

namespace games {

inline constexpr int painted_back_count = 4;
inline constexpr int card_art_width = 360;
inline constexpr int card_art_height = 504;

// Premultiplied BGRA, w * h, rows of w * 4 bytes; transparent outside the card.
[[nodiscard]] std::vector<std::byte> make_card_back(int design, int w, int h);

}  // namespace games
