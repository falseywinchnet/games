#pragma once
// The library's byte coder: LZ77 with a window as large as the text, its literals,
// lengths and distances coded bit by bit with adaptive probabilities by a range coder,
// in the manner of LZMA. Decoding is a few table lookups a byte, so a whole book opens
// in milliseconds. Integer arithmetic only: the same bytes give the same code on every
// platform. It knows nothing of books; the container decides what a block holds.
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace library {

// Codes `text`, appending to `out`. `effort` bounds the match search (positions tried
// per byte); more is smaller and slower to encode, never slower to decode.
void lz_encode(std::span<const std::uint8_t> text, std::vector<std::uint8_t>& out, int effort = 256);

// Decodes `length` bytes of `code` into `out` (replaced). False, with `out` cleared,
// when the code is damaged: it runs out, or a distance points before the text.
[[nodiscard]] bool lz_decode(std::span<const std::uint8_t> code, std::size_t length,
                             std::vector<std::uint8_t>& out);

}  // namespace library
