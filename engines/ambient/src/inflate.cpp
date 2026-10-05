#include "inflate.hpp"

#include <array>
#include <cstring>

namespace ambient {
namespace {

constexpr int fast_bits = 10;
constexpr int max_bits = 15;

// Canonical Huffman decoding tables for one alphabet.
struct Huffman {
    std::array<std::uint16_t, max_bits + 1> count{};  // codes of each length
    std::array<std::uint16_t, 320> symbol{};          // symbols ordered by code
    // fast[code bits, LSB first] = symbol << 4 | length, or 0 when longer than fast_bits.
    std::array<std::uint16_t, 1U << fast_bits> fast{};
};

class BitReader final {
  public:
    explicit BitReader(std::span<const std::uint8_t> source) : source_(source) {}
    // Returns false once the input is exhausted; reads past the end yield zero bits.
    [[nodiscard]] bool ok() const {
        return overrun_ == false;
    }
    std::uint32_t bits(int count) {
        refill(count);
        const std::uint32_t mask = (1U << count) - 1U;
        const std::uint32_t result = static_cast<std::uint32_t>(buffer_) & mask;
        buffer_ >>= count;
        available_ -= count;
        return result;
    }
    std::uint32_t peek(int count) {
        refill(count);
        const std::uint32_t mask = (1U << count) - 1U;
        const std::uint32_t result = static_cast<std::uint32_t>(buffer_) & mask;
        return result;
    }
    void skip(int count) {
        buffer_ >>= count;
        available_ -= count;
    }
    void align_to_byte() {
        const int extra = available_ % 8;
        buffer_ >>= extra;
        available_ -= extra;
    }
    // Bytes after byte alignment; consumes buffered whole bytes first.
    bool copy_bytes(std::uint8_t* destination, std::size_t count) {
        while (count > 0 && available_ >= 8) {
            *destination = static_cast<std::uint8_t>(buffer_ & 0xFFU);
            ++destination;
            buffer_ >>= 8;
            available_ -= 8;
            --count;
        }
        if (count > source_.size() - position_) {
            overrun_ = true;
            return false;
        }
        std::memcpy(destination, source_.data() + position_, count);
        position_ += count;
        return true;
    }
    std::size_t position_after_alignment() const {
        return position_ - static_cast<std::size_t>(available_ / 8);
    }

  private:
    void refill(int count) {
        while (available_ < count) {
            std::uint64_t next = 0;
            if (position_ < source_.size()) {
                next = source_[position_];
                ++position_;
            } else {
                overrun_ = true;
            }
            buffer_ |= next << available_;
            available_ += 8;
        }
    }
    std::span<const std::uint8_t> source_;
    std::size_t position_{};
    std::uint64_t buffer_{};
    int available_{};
    bool overrun_{};
};

std::uint32_t reverse_bits(std::uint32_t code, int length) {
    std::uint32_t result = 0;
    for (int bit = 0; bit < length; ++bit) {
        result = (result << 1U) | (code & 1U);
        code >>= 1U;
    }
    return result;
}

// Builds tables from code lengths. Incomplete codes are allowed only for the
// single-code distance alphabet the format permits; oversubscribed codes fail.
bool build(Huffman& table, const std::uint8_t* lengths, int symbols) {
    table.count.fill(0);
    table.fast.fill(0);
    for (int index = 0; index < symbols; ++index)
        ++table.count[lengths[index]];
    table.count[0] = 0;
    int left = 1;
    for (int length = 1; length <= max_bits; ++length) {
        left <<= 1;
        left -= table.count[static_cast<std::size_t>(length)];
        if (left < 0)
            return false;
    }
    std::array<std::uint16_t, max_bits + 2> offsets{};
    for (int length = 1; length <= max_bits; ++length)
        offsets[static_cast<std::size_t>(length + 1)] = static_cast<std::uint16_t>(
            offsets[static_cast<std::size_t>(length)] + table.count[static_cast<std::size_t>(length)]);
    for (int index = 0; index < symbols; ++index) {
        const int length = lengths[index];
        if (length != 0) {
            table.symbol[offsets[static_cast<std::size_t>(length)]] = static_cast<std::uint16_t>(index);
            ++offsets[static_cast<std::size_t>(length)];
        }
    }
    // Assign canonical codes and fill the fast table for the short ones.
    std::uint32_t code = 0;
    int position = 0;
    for (int length = 1; length <= max_bits; ++length) {
        const int count = table.count[static_cast<std::size_t>(length)];
        for (int k = 0; k < count; ++k) {
            if (length <= fast_bits) {
                const std::uint32_t reversed = reverse_bits(code, length);
                const std::uint16_t entry = static_cast<std::uint16_t>(
                    (table.symbol[static_cast<std::size_t>(position)] << 4U) | static_cast<std::uint32_t>(length));
                for (std::uint32_t fill = reversed; fill < (1U << fast_bits); fill += 1U << length)
                    table.fast[fill] = entry;
            }
            ++code;
            ++position;
        }
        code <<= 1U;
    }
    return true;
}

// Returns the decoded symbol, or -1 for an invalid code.
int decode(BitReader& reader, const Huffman& table) {
    const std::uint32_t peeked = reader.peek(fast_bits);
    const std::uint16_t entry = table.fast[peeked];
    if (entry != 0) {
        reader.skip(entry & 15U);
        return entry >> 4U;
    }
    // Canonical decoding, one bit at a time, for codes longer than fast_bits.
    int code = 0;
    int first = 0;
    int index = 0;
    for (int length = 1; length <= max_bits; ++length) {
        code |= static_cast<int>(reader.bits(1));
        const int count = table.count[static_cast<std::size_t>(length)];
        if (code - count < first)
            return table.symbol[static_cast<std::size_t>(index + (code - first))];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

constexpr std::array<std::uint16_t, 29> length_base{3,  4,  5,  6,  7,  8,  9,  10, 11,  13,
                                                    15, 17, 19, 23, 27, 31, 35, 43, 51,  59,
                                                    67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<std::uint8_t, 29> length_extra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<std::uint16_t, 30> distance_base{1,    2,    3,    4,    5,    7,     9,     13,
                                                      17,   25,   33,   49,   65,   97,    129,   193,
                                                      257,  385,  513,  769,  1025, 1537,  2049,  3073,
                                                      4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<std::uint8_t, 30> distance_extra{0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                      6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool inflate_block(BitReader& reader, const Huffman& lengths, const Huffman& distances,
                   std::uint8_t* output, std::size_t capacity, std::size_t& written) {
    for (;;) {
        const int symbol = decode(reader, lengths);
        if (symbol < 0 || !reader.ok())
            return false;
        if (symbol < 256) {
            if (written >= capacity)
                return false;
            output[written] = static_cast<std::uint8_t>(symbol);
            ++written;
            continue;
        }
        if (symbol == 256)
            return true;
        const int length_index = symbol - 257;
        if (length_index >= 29)
            return false;
        const std::size_t length = length_base[static_cast<std::size_t>(length_index)] +
                                   reader.bits(length_extra[static_cast<std::size_t>(length_index)]);
        const int distance_symbol = decode(reader, distances);
        if (distance_symbol < 0 || distance_symbol >= 30)
            return false;
        const std::size_t distance = distance_base[static_cast<std::size_t>(distance_symbol)] +
                                     reader.bits(distance_extra[static_cast<std::size_t>(distance_symbol)]);
        if (distance > written || length > capacity - written)
            return false;
        // Byte-wise copy: overlapping references repeat recent output by design.
        const std::uint8_t* from = output + written - distance;
        std::uint8_t* to = output + written;
        for (std::size_t k = 0; k < length; ++k)
            to[k] = from[k];
        written += length;
    }
}

bool fixed_tables(Huffman& lengths, Huffman& distances) {
    std::array<std::uint8_t, 288> length_codes{};
    for (std::size_t index = 0; index < 288; ++index) {
        std::uint8_t bits = 8;
        if (index >= 144 && index < 256)
            bits = 9;
        if (index >= 256 && index < 280)
            bits = 7;
        length_codes[index] = bits;
    }
    std::array<std::uint8_t, 30> distance_codes{};
    distance_codes.fill(5);
    const bool ok = build(lengths, length_codes.data(), 288) && build(distances, distance_codes.data(), 30);
    return ok;
}

bool dynamic_tables(BitReader& reader, Huffman& lengths, Huffman& distances) {
    const int literal_count = static_cast<int>(reader.bits(5)) + 257;
    const int distance_count = static_cast<int>(reader.bits(5)) + 1;
    const int code_count = static_cast<int>(reader.bits(4)) + 4;
    if (literal_count > 286 || distance_count > 30)
        return false;
    constexpr std::array<std::uint8_t, 19> order{16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    std::array<std::uint8_t, 19> code_lengths{};
    for (int index = 0; index < code_count; ++index)
        code_lengths[order[static_cast<std::size_t>(index)]] = static_cast<std::uint8_t>(reader.bits(3));
    Huffman code_table{};
    if (!build(code_table, code_lengths.data(), 19))
        return false;
    std::array<std::uint8_t, 320> all{};
    int index = 0;
    while (index < literal_count + distance_count) {
        const int symbol = decode(reader, code_table);
        if (symbol < 0 || !reader.ok())
            return false;
        if (symbol < 16) {
            all[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(symbol);
            ++index;
            continue;
        }
        std::uint8_t value = 0;
        int repeat = 0;
        if (symbol == 16) {
            if (index == 0)
                return false;
            value = all[static_cast<std::size_t>(index - 1)];
            repeat = 3 + static_cast<int>(reader.bits(2));
        } else if (symbol == 17) {
            repeat = 3 + static_cast<int>(reader.bits(3));
        } else {
            repeat = 11 + static_cast<int>(reader.bits(7));
        }
        if (index + repeat > literal_count + distance_count)
            return false;
        for (int k = 0; k < repeat; ++k) {
            all[static_cast<std::size_t>(index)] = value;
            ++index;
        }
    }
    if (all[256] == 0)
        return false;
    const bool ok = build(lengths, all.data(), literal_count) &&
                    build(distances, all.data() + literal_count, distance_count);
    return ok;
}

std::uint32_t adler32(const std::uint8_t* bytes, std::size_t count) {
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    std::size_t index = 0;
    while (index < count) {
        const std::size_t chunk = std::min<std::size_t>(count - index, 5552);
        for (std::size_t k = 0; k < chunk; ++k) {
            a += bytes[index + k];
            b += a;
        }
        a %= 65521U;
        b %= 65521U;
        index += chunk;
    }
    const std::uint32_t result = (b << 16U) | a;
    return result;
}

} // namespace

bool inflate_zlib(std::span<const std::uint8_t> source, std::size_t expected_bytes,
                  std::vector<std::uint8_t>& destination) {
    destination.clear();
    if (source.size() < 6)
        return false;
    const std::uint32_t method = source[0];
    const std::uint32_t flags = source[1];
    if ((method & 15U) != 8U || (method >> 4U) > 7U || ((method << 8U) | flags) % 31U != 0U ||
        (flags & 32U) != 0U)
        return false;
    std::vector<std::uint8_t> output(expected_bytes);
    BitReader reader(source.subspan(2));
    std::size_t written = 0;
    bool last = false;
    Huffman lengths{};
    Huffman distances{};
    while (!last) {
        last = reader.bits(1) != 0;
        const std::uint32_t type = reader.bits(2);
        if (type == 0) {
            reader.align_to_byte();
            const std::uint32_t length = reader.bits(16);
            const std::uint32_t complement = reader.bits(16);
            if ((length ^ 0xFFFFU) != complement || length > expected_bytes - written)
                return false;
            if (!reader.copy_bytes(output.data() + written, length))
                return false;
            written += length;
        } else if (type == 1 || type == 2) {
            const bool tables = type == 1 ? fixed_tables(lengths, distances)
                                          : dynamic_tables(reader, lengths, distances);
            if (!tables || !inflate_block(reader, lengths, distances, output.data(), expected_bytes, written))
                return false;
        } else {
            return false;
        }
        if (!reader.ok())
            return false;
    }
    if (written != expected_bytes)
        return false;
    reader.align_to_byte();
    std::array<std::uint8_t, 4> trailer{};
    if (!reader.copy_bytes(trailer.data(), 4))
        return false;
    const std::uint32_t stored = (static_cast<std::uint32_t>(trailer[0]) << 24U) |
                                 (static_cast<std::uint32_t>(trailer[1]) << 16U) |
                                 (static_cast<std::uint32_t>(trailer[2]) << 8U) | trailer[3];
    if (stored != adler32(output.data(), output.size()))
        return false;
    destination = std::move(output);
    return true;
}

std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t index = 0; index < 256; ++index) {
        std::uint32_t value = index;
        for (int bit = 0; bit < 8; ++bit)
            value = (value & 1U) != 0 ? 0xEDB88320U ^ (value >> 1U) : value >> 1U;
        table[index] = value;
    }
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::uint8_t byte : bytes)
        crc = table[(crc ^ byte) & 0xFFU] ^ (crc >> 8U);
    const std::uint32_t result = crc ^ 0xFFFFFFFFU;
    return result;
}

} // namespace ambient
