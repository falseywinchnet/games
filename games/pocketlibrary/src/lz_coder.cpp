#include "lz_coder.hpp"

#include <algorithm>
#include <array>

namespace pl {
namespace {

// Probabilities are 11-bit, starting at one half; each bit moves one a 32nd of the way.
constexpr int probability_bits = 11;
constexpr std::uint16_t probability_half = 1 << (probability_bits - 1);
constexpr int move_bits = 5;
constexpr std::uint32_t top = 1U << 24;

constexpr int state_count = 12;
constexpr int literal_contexts = 256;  // the whole previous byte
constexpr int minimum_length = 2;
constexpr int maximum_length = minimum_length + 8 + 8 + 256 - 1;  // 273
constexpr int length_states = 4;
constexpr int slot_bits = 6;
constexpr int end_tree_slot = 14;  // slots from here code their middle bits directly
constexpr int align_bits = 4;
constexpr int full_distances = 1 << (end_tree_slot >> 1);  // 128

int after_literal(int state) {
    return state < 4 ? 0 : (state < 10 ? state - 3 : state - 6);
}
int after_match(int state) {
    return state < 7 ? 7 : 10;
}
int after_rep(int state) {
    return state < 7 ? 8 : 11;
}
int after_short_rep(int state) {
    return state < 7 ? 9 : 11;
}

struct LengthModel {
    std::uint16_t choice = probability_half;
    std::uint16_t choice2 = probability_half;
    std::array<std::uint16_t, 8> low{};
    std::array<std::uint16_t, 8> middle{};
    std::array<std::uint16_t, 256> high{};
    LengthModel() {
        low.fill(probability_half);
        middle.fill(probability_half);
        high.fill(probability_half);
    }
};

// Every probability of the coder; the encoder and decoder each hold one and move it in
// the same steps.
struct Model {
    std::vector<std::uint16_t> literal;  // literal_contexts * 0x300
    std::array<std::uint16_t, state_count> is_match{};
    std::array<std::uint16_t, state_count> is_rep{};
    std::array<std::uint16_t, state_count> is_rep_g0{};
    std::array<std::uint16_t, state_count> is_rep_g1{};
    std::array<std::uint16_t, state_count> is_rep_g2{};
    std::array<std::uint16_t, state_count> is_rep0_long{};
    std::array<std::array<std::uint16_t, 1 << slot_bits>, length_states> slot{};
    std::array<std::uint16_t, full_distances - end_tree_slot + 1> special{};
    std::array<std::uint16_t, 1 << align_bits> align{};
    LengthModel length;
    LengthModel rep_length;
    Model() : literal(static_cast<std::size_t>(literal_contexts) * 0x300, probability_half) {
        is_match.fill(probability_half);
        is_rep.fill(probability_half);
        is_rep_g0.fill(probability_half);
        is_rep_g1.fill(probability_half);
        is_rep_g2.fill(probability_half);
        is_rep0_long.fill(probability_half);
        for (std::array<std::uint16_t, 1 << slot_bits>& row : slot)
            row.fill(probability_half);
        special.fill(probability_half);
        align.fill(probability_half);
    }
};

int slot_of(std::uint32_t distance) {
    if (distance < 4)
        return static_cast<int>(distance);
    int highest = 31;
    while ((distance >> highest) == 0)
        --highest;
    return highest * 2 + static_cast<int>((distance >> (highest - 1)) & 1U);
}

// ---- Encoding ------------------------------------------------------------------------

class RangeEncoder {
  public:
    explicit RangeEncoder(std::vector<std::uint8_t>& out) : out_(out) {}

    void bit(std::uint16_t& probability, int value) {
        const std::uint32_t bound = (range_ >> probability_bits) * probability;
        if (value == 0) {
            range_ = bound;
            probability = static_cast<std::uint16_t>(probability + (((1 << probability_bits) - probability) >> move_bits));
        } else {
            low_ += bound;
            range_ -= bound;
            probability = static_cast<std::uint16_t>(probability - (probability >> move_bits));
        }
        while (range_ < top) {
            range_ <<= 8;
            shift_low();
        }
    }

    void direct(std::uint32_t value, int count) {
        for (int i = count - 1; i >= 0; --i) {
            range_ >>= 1;
            if (((value >> i) & 1U) != 0)
                low_ += range_;
            while (range_ < top) {
                range_ <<= 8;
                shift_low();
            }
        }
    }

    void tree(std::uint16_t* probabilities, int bits, std::uint32_t value) {
        std::uint32_t node = 1;
        for (int i = bits - 1; i >= 0; --i) {
            const int b = static_cast<int>((value >> i) & 1U);
            bit(probabilities[node], b);
            node = (node << 1) | static_cast<std::uint32_t>(b);
        }
    }

    void reverse_tree(std::uint16_t* probabilities, int bits, std::uint32_t value) {
        std::uint32_t node = 1;
        for (int i = 0; i < bits; ++i) {
            const int b = static_cast<int>(value & 1U);
            value >>= 1;
            bit(probabilities[node], b);
            node = (node << 1) | static_cast<std::uint32_t>(b);
        }
    }

    void finish() {
        for (int i = 0; i < 5; ++i)
            shift_low();
    }

  private:
    void shift_low() {
        if (static_cast<std::uint32_t>(low_) < 0xFF000000U || (low_ >> 32) != 0) {
            const std::uint8_t carry = static_cast<std::uint8_t>(low_ >> 32);
            std::uint8_t pending = cache_;
            while (true) {
                out_.push_back(static_cast<std::uint8_t>(pending + carry));
                pending = 0xFF;
                --cache_size_;
                if (cache_size_ == 0)
                    break;
            }
            cache_ = static_cast<std::uint8_t>((low_ >> 24) & 0xFFU);
        }
        ++cache_size_;
        low_ = (low_ & 0x00FFFFFFU) << 8;
    }

    std::vector<std::uint8_t>& out_;
    std::uint64_t low_ = 0;
    std::uint32_t range_ = 0xFFFFFFFFU;
    std::uint8_t cache_ = 0;
    std::uint64_t cache_size_ = 1;
};

void encode_length(RangeEncoder& coder, LengthModel& model, int length) {
    const int value = length - minimum_length;
    if (value < 8) {
        coder.bit(model.choice, 0);
        coder.tree(model.low.data(), 3, static_cast<std::uint32_t>(value));
    } else if (value < 16) {
        coder.bit(model.choice, 1);
        coder.bit(model.choice2, 0);
        coder.tree(model.middle.data(), 3, static_cast<std::uint32_t>(value - 8));
    } else {
        coder.bit(model.choice, 1);
        coder.bit(model.choice2, 1);
        coder.tree(model.high.data(), 8, static_cast<std::uint32_t>(value - 16));
    }
}

void encode_distance(RangeEncoder& coder, Model& model, std::uint32_t distance, int length) {
    const int length_state = std::min(length - minimum_length, length_states - 1);
    const int slot = slot_of(distance);
    coder.tree(model.slot[static_cast<std::size_t>(length_state)].data(), slot_bits, static_cast<std::uint32_t>(slot));
    if (slot < 4)
        return;
    const int footer = (slot >> 1) - 1;
    const std::uint32_t base = (2U | (static_cast<std::uint32_t>(slot) & 1U)) << footer;
    const std::uint32_t reduced = distance - base;
    if (slot < end_tree_slot) {
        coder.reverse_tree(model.special.data() + base - static_cast<std::uint32_t>(slot) - 1, footer, reduced);
    } else {
        coder.direct(reduced >> align_bits, footer - align_bits);
        coder.reverse_tree(model.align.data(), align_bits, reduced & ((1U << align_bits) - 1));
    }
}

void encode_literal(RangeEncoder& coder, Model& model, std::uint8_t previous, std::uint8_t byte, bool matched,
                    std::uint8_t match_byte) {
    std::uint16_t* probabilities = model.literal.data() + static_cast<std::size_t>(previous) * 0x300;
    std::uint32_t symbol = 1;
    if (!matched) {
        for (int i = 7; i >= 0; --i) {
            const int b = (byte >> i) & 1;
            coder.bit(probabilities[symbol], b);
            symbol = (symbol << 1) | static_cast<std::uint32_t>(b);
        }
        return;
    }
    std::uint32_t offset = 0x100;
    std::uint32_t match = match_byte;
    for (int i = 7; i >= 0; --i) {
        match <<= 1;
        const std::uint32_t match_bit = match & offset;
        const int b = (byte >> i) & 1;
        coder.bit(probabilities[offset + match_bit + symbol], b);
        symbol = (symbol << 1) | static_cast<std::uint32_t>(b);
        offset = b != 0 ? (offset & match_bit) : (offset & ~match_bit);
    }
}

// The match finder: a hash of three bytes leads to a chain of earlier positions.
struct Finder {
    static constexpr int hash_bits = 20;
    std::span<const std::uint8_t> text;
    std::vector<std::int32_t> head;
    std::vector<std::int32_t> previous;
    int effort = 0;

    Finder(std::span<const std::uint8_t> source, int chosen_effort)
        : text(source), head(static_cast<std::size_t>(1) << hash_bits, -1), previous(source.size(), -1),
          effort(chosen_effort) {}

    std::uint32_t hash_at(std::size_t position) const {
        const std::uint32_t value = static_cast<std::uint32_t>(text[position]) |
                                    (static_cast<std::uint32_t>(text[position + 1]) << 8) |
                                    (static_cast<std::uint32_t>(text[position + 2]) << 16);
        return (value * 2654435761U) >> (32 - hash_bits);
    }

    void insert(std::size_t position) {
        if (position + 3 > text.size())
            return;
        const std::uint32_t h = hash_at(position);
        previous[position] = head[h];
        head[h] = static_cast<std::int32_t>(position);
    }

    // The longest earlier match at `position`: its length (0 for none) and distance.
    int longest(std::size_t position, std::uint32_t& distance) const {
        int best = 0;
        if (position + 3 > text.size())
            return 0;
        const int limit = static_cast<int>(std::min<std::size_t>(maximum_length, text.size() - position));
        std::int32_t candidate = head[hash_at(position)];
        int tries = effort;
        while (candidate >= 0 && tries > 0) {
            const std::size_t at = static_cast<std::size_t>(candidate);
            if (text[at + static_cast<std::size_t>(best)] == text[position + static_cast<std::size_t>(best)]) {
                int length = 0;
                while (length < limit && text[at + static_cast<std::size_t>(length)] ==
                                             text[position + static_cast<std::size_t>(length)])
                    ++length;
                if (length > best) {
                    best = length;
                    distance = static_cast<std::uint32_t>(position - at - 1);
                    if (length == limit)
                        break;
                }
            }
            candidate = previous[at];
            --tries;
        }
        return best;
    }
};

int length_at(std::span<const std::uint8_t> text, std::size_t position, std::uint32_t distance) {
    if (static_cast<std::size_t>(distance) + 1 > position)
        return 0;
    const std::size_t from = position - distance - 1;
    const int limit = static_cast<int>(std::min<std::size_t>(maximum_length, text.size() - position));
    int length = 0;
    while (length < limit && text[from + static_cast<std::size_t>(length)] == text[position + static_cast<std::size_t>(length)])
        ++length;
    return length;
}

// Whether a match of `length` at `distance` is worth more than its bytes as literals.
bool worth(int length, std::uint32_t distance) {
    if (length < 2)
        return false;
    if (length == 2)
        return distance < 128;
    if (length == 3)
        return distance < (1U << 14);
    if (length == 4)
        return distance < (1U << 20);
    return true;
}

}  // namespace

void lz_encode(std::span<const std::uint8_t> text, std::vector<std::uint8_t>& out, int effort) {
    Model model;
    RangeEncoder coder(out);
    Finder finder(text, effort);
    std::array<std::uint32_t, 4> reps{};
    int state = 0;
    std::size_t position = 0;
    while (position < text.size()) {
        const std::uint8_t previous = position > 0 ? text[position - 1] : 0;
        std::uint32_t distance = 0;
        const int length = finder.longest(position, distance);
        int rep_index = -1;
        int rep_length = 0;
        for (int r = 0; r < 4; ++r) {
            const int candidate = length_at(text, position, reps[static_cast<std::size_t>(r)]);
            if (candidate > rep_length) {
                rep_length = candidate;
                rep_index = r;
            }
        }
        // One step of lazy evaluation: a longer match one byte on wins over this one.
        bool take_match = worth(length, distance);
        if (take_match && position + 1 < text.size() && length < 64) {
            std::uint32_t next_distance = 0;
            const int next_length = finder.longest(position + 1, next_distance);
            if (next_length > length + 1 && worth(next_length, next_distance) && rep_length < length)
                take_match = false;
        }
        const bool take_rep = rep_length >= 2 && (rep_length + 1 >= length || !take_match);
        if (take_rep) {
            coder.bit(model.is_match[static_cast<std::size_t>(state)], 1);
            coder.bit(model.is_rep[static_cast<std::size_t>(state)], 1);
            if (rep_index == 0) {
                coder.bit(model.is_rep_g0[static_cast<std::size_t>(state)], 0);
                coder.bit(model.is_rep0_long[static_cast<std::size_t>(state)], 1);
            } else {
                coder.bit(model.is_rep_g0[static_cast<std::size_t>(state)], 1);
                if (rep_index == 1) {
                    coder.bit(model.is_rep_g1[static_cast<std::size_t>(state)], 0);
                } else {
                    coder.bit(model.is_rep_g1[static_cast<std::size_t>(state)], 1);
                    coder.bit(model.is_rep_g2[static_cast<std::size_t>(state)], rep_index == 3 ? 1 : 0);
                }
                const std::uint32_t chosen = reps[static_cast<std::size_t>(rep_index)];
                for (int r = rep_index; r > 0; --r)
                    reps[static_cast<std::size_t>(r)] = reps[static_cast<std::size_t>(r - 1)];
                reps[0] = chosen;
            }
            encode_length(coder, model.rep_length, rep_length);
            state = after_rep(state);
            for (int i = 0; i < rep_length; ++i)
                finder.insert(position + static_cast<std::size_t>(i));
            position += static_cast<std::size_t>(rep_length);
        } else if (take_match) {
            coder.bit(model.is_match[static_cast<std::size_t>(state)], 1);
            coder.bit(model.is_rep[static_cast<std::size_t>(state)], 0);
            encode_length(coder, model.length, length);
            encode_distance(coder, model, distance, length);
            reps[3] = reps[2];
            reps[2] = reps[1];
            reps[1] = reps[0];
            reps[0] = distance;
            state = after_match(state);
            for (int i = 0; i < length; ++i)
                finder.insert(position + static_cast<std::size_t>(i));
            position += static_cast<std::size_t>(length);
        } else if (position > reps[0] && text[position] == text[position - reps[0] - 1] && state >= 7) {
            coder.bit(model.is_match[static_cast<std::size_t>(state)], 1);
            coder.bit(model.is_rep[static_cast<std::size_t>(state)], 1);
            coder.bit(model.is_rep_g0[static_cast<std::size_t>(state)], 0);
            coder.bit(model.is_rep0_long[static_cast<std::size_t>(state)], 0);
            state = after_short_rep(state);
            finder.insert(position);
            ++position;
        } else {
            coder.bit(model.is_match[static_cast<std::size_t>(state)], 0);
            const bool matched = state >= 7 && position > reps[0];
            const std::uint8_t match_byte = matched ? text[position - reps[0] - 1] : 0;
            encode_literal(coder, model, previous, text[position], matched, match_byte);
            state = after_literal(state);
            finder.insert(position);
            ++position;
        }
    }
    coder.finish();
}

// ---- Decoding ------------------------------------------------------------------------

namespace {

class RangeDecoder {
  public:
    explicit RangeDecoder(std::span<const std::uint8_t> code) : code_(code) {
        for (int i = 0; i < 5; ++i)
            value_ = (value_ << 8) | next();
    }

    int bit(std::uint16_t& probability) {
        const std::uint32_t bound = (range_ >> probability_bits) * probability;
        int result = 0;
        if (value_ < bound) {
            range_ = bound;
            probability = static_cast<std::uint16_t>(probability + (((1 << probability_bits) - probability) >> move_bits));
        } else {
            value_ -= bound;
            range_ -= bound;
            probability = static_cast<std::uint16_t>(probability - (probability >> move_bits));
            result = 1;
        }
        if (range_ < top) {
            range_ <<= 8;
            value_ = (value_ << 8) | next();
        }
        return result;
    }

    std::uint32_t direct(int count) {
        std::uint32_t result = 0;
        for (int i = 0; i < count; ++i) {
            range_ >>= 1;
            std::uint32_t b = 0;
            if (value_ >= range_) {
                value_ -= range_;
                b = 1;
            }
            result = (result << 1) | b;
            if (range_ < top) {
                range_ <<= 8;
                value_ = (value_ << 8) | next();
            }
        }
        return result;
    }

    std::uint32_t tree(std::uint16_t* probabilities, int bits) {
        std::uint32_t node = 1;
        for (int i = 0; i < bits; ++i)
            node = (node << 1) | static_cast<std::uint32_t>(bit(probabilities[node]));
        return node - (1U << bits);
    }

    std::uint32_t reverse_tree(std::uint16_t* probabilities, int bits) {
        std::uint32_t node = 1;
        std::uint32_t result = 0;
        for (int i = 0; i < bits; ++i) {
            const int b = bit(probabilities[node]);
            node = (node << 1) | static_cast<std::uint32_t>(b);
            result |= static_cast<std::uint32_t>(b) << i;
        }
        return result;
    }

    [[nodiscard]] bool overrun() const {
        return position_ > code_.size() + 4;
    }

  private:
    std::uint32_t next() {
        const std::uint32_t byte = position_ < code_.size() ? code_[position_] : 0U;
        ++position_;
        return byte;
    }

    std::span<const std::uint8_t> code_;
    std::size_t position_ = 0;
    std::uint32_t range_ = 0xFFFFFFFFU;
    std::uint32_t value_ = 0;
};

int decode_length(RangeDecoder& coder, LengthModel& model) {
    if (coder.bit(model.choice) == 0)
        return minimum_length + static_cast<int>(coder.tree(model.low.data(), 3));
    if (coder.bit(model.choice2) == 0)
        return minimum_length + 8 + static_cast<int>(coder.tree(model.middle.data(), 3));
    return minimum_length + 16 + static_cast<int>(coder.tree(model.high.data(), 8));
}

std::uint32_t decode_distance(RangeDecoder& coder, Model& model, int length) {
    const int length_state = std::min(length - minimum_length, length_states - 1);
    const int slot = static_cast<int>(coder.tree(model.slot[static_cast<std::size_t>(length_state)].data(), slot_bits));
    if (slot < 4)
        return static_cast<std::uint32_t>(slot);
    const int footer = (slot >> 1) - 1;
    const std::uint32_t base = (2U | (static_cast<std::uint32_t>(slot) & 1U)) << footer;
    if (slot < end_tree_slot)
        return base + coder.reverse_tree(model.special.data() + base - static_cast<std::uint32_t>(slot) - 1, footer);
    const std::uint32_t middle = coder.direct(footer - align_bits) << align_bits;
    return base + middle + coder.reverse_tree(model.align.data(), align_bits);
}

}  // namespace

bool lz_decode(std::span<const std::uint8_t> code, std::size_t length, std::vector<std::uint8_t>& out) {
    out.clear();
    out.reserve(length);
    Model model;
    RangeDecoder coder(code);
    std::array<std::uint32_t, 4> reps{};
    int state = 0;
    while (out.size() < length) {
        const std::size_t position = out.size();
        if (coder.bit(model.is_match[static_cast<std::size_t>(state)]) == 0) {
            const std::uint8_t previous = position > 0 ? out[position - 1] : 0;
            std::uint16_t* probabilities = model.literal.data() + static_cast<std::size_t>(previous) * 0x300;
            std::uint32_t symbol = 1;
            if (state >= 7 && position > reps[0]) {
                std::uint32_t match = out[position - reps[0] - 1];
                std::uint32_t offset = 0x100;
                while (symbol < 0x100) {
                    match <<= 1;
                    const std::uint32_t match_bit = match & offset;
                    const int b = coder.bit(probabilities[offset + match_bit + symbol]);
                    symbol = (symbol << 1) | static_cast<std::uint32_t>(b);
                    offset = b != 0 ? (offset & match_bit) : (offset & ~match_bit);
                }
            } else {
                while (symbol < 0x100)
                    symbol = (symbol << 1) | static_cast<std::uint32_t>(coder.bit(probabilities[symbol]));
            }
            out.push_back(static_cast<std::uint8_t>(symbol & 0xFFU));
            state = after_literal(state);
        } else {
            int run = 0;
            if (coder.bit(model.is_rep[static_cast<std::size_t>(state)]) != 0) {
                if (coder.bit(model.is_rep_g0[static_cast<std::size_t>(state)]) == 0) {
                    if (coder.bit(model.is_rep0_long[static_cast<std::size_t>(state)]) == 0) {
                        if (position <= reps[0]) {
                            out.clear();
                            return false;
                        }
                        out.push_back(out[position - reps[0] - 1]);
                        state = after_short_rep(state);
                        continue;
                    }
                } else {
                    int index = 1;
                    if (coder.bit(model.is_rep_g1[static_cast<std::size_t>(state)]) != 0)
                        index = coder.bit(model.is_rep_g2[static_cast<std::size_t>(state)]) != 0 ? 3 : 2;
                    const std::uint32_t chosen = reps[static_cast<std::size_t>(index)];
                    for (int r = index; r > 0; --r)
                        reps[static_cast<std::size_t>(r)] = reps[static_cast<std::size_t>(r - 1)];
                    reps[0] = chosen;
                }
                run = decode_length(coder, model.rep_length);
                state = after_rep(state);
            } else {
                run = decode_length(coder, model.length);
                const std::uint32_t distance = decode_distance(coder, model, run);
                reps[3] = reps[2];
                reps[2] = reps[1];
                reps[1] = reps[0];
                reps[0] = distance;
                state = after_match(state);
            }
            if (position <= reps[0] || position + static_cast<std::size_t>(run) > length) {
                out.clear();
                return false;
            }
            const std::size_t from = position - reps[0] - 1;
            for (int i = 0; i < run; ++i)
                out.push_back(out[from + static_cast<std::size_t>(i)]);
        }
        if (coder.overrun()) {
            out.clear();
            return false;
        }
    }
    return true;
}

}  // namespace pl
