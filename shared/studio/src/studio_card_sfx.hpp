// The card handling sounds (authoring/playsuite_music's card_sfx.cpp).
#pragma once
#include "studio_dsp.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace studio {

// Every card sound the original renders, by runtime name (card_pickup_01 ... card_shuffle_02).
std::vector<std::string> cardSfxIds();
// The finished clip the original wrote for `id` (before its 24-bit quantisation); empty when
// `id` is not a card sound.
Stereo renderCardSfx(std::string_view id);

} // namespace studio
