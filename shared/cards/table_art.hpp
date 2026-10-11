#pragma once
// Pictures every card table shares, made once per run and then kept: the felt
// (shared/felt) and the backs drawn in code (card_backs.hpp). Both are made with
// every core lending a hand, so the first table opens quickly and later ones,
// Koi-Koi's included, take them as they are. Call from the interface thread.
#include <cstddef>
#include <vector>

namespace games {

// The table felt, shared/felt's preset 3 as a seamless tile: opaque BGRA,
// felt_tile_side square, rows of felt_tile_side * 4 bytes.
inline constexpr int felt_tile_side = 256;
[[nodiscard]] const std::vector<std::byte>& felt_tile();

// A drawn back (painted_back_count <= design < card_back_count), card_art_width x
// card_art_height premultiplied BGRA, as make_card_back draws it.
[[nodiscard]] const std::vector<std::byte>& drawn_card_back(int design);

// Draws, all at once, every drawn back not yet drawn.
void draw_card_backs();

}  // namespace games
