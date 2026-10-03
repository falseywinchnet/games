#pragma once
#include <cstddef>
#include <vector>
namespace games {
// A translucent BGRA-premultiplied layer laid over a card image of w x h device pixels.
std::vector<std::byte> card_finish(int w, int h);
}  // namespace games
