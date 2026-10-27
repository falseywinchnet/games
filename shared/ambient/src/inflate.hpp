#pragma once
// A zlib (RFC 1950) / DEFLATE (RFC 1951) decoder for scene archives, so the engine
// needs no compression library. Decoding is table driven for codes up to ten bits
// and falls back to canonical decoding for longer ones.
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ambient {

// Decodes a complete zlib stream whose decompressed size is known in advance.
// On success `destination` holds exactly `expected_bytes`; on failure it is cleared.
// The Adler-32 trailer is verified.
[[nodiscard]] bool inflate_zlib(std::span<const std::uint8_t> source, std::size_t expected_bytes,
                                std::vector<std::uint8_t>& destination);

// CRC-32 (IEEE 802.3, as zlib computes it) of a byte range.
[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> bytes);

} // namespace ambient
