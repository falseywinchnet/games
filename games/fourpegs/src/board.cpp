#include "board.hpp"

#include <algorithm>

namespace fp {

Score evaluate(const Code& secret, const Code& guess) {
    Score s;
    std::array<int, kColors> left_secret{}, left_guess{};
    for (int i = 0; i < kPegs; ++i) {
        if (secret[static_cast<size_t>(i)] == guess[static_cast<size_t>(i)]) ++s.exact;
        else {
            ++left_secret[static_cast<size_t>(secret[static_cast<size_t>(i)])];
            ++left_guess[static_cast<size_t>(guess[static_cast<size_t>(i)])];
        }
    }
    for (int c = 0; c < kColors; ++c) s.near += std::min(left_secret[static_cast<size_t>(c)], left_guess[static_cast<size_t>(c)]);
    return s;
}

Board::Board(std::uint64_t seed) {
    s_.rng = seed ? seed : 0xF0B5EEDULL;
    new_game();
}

std::uint64_t Board::next() {  // splitmix64
    std::uint64_t z = (s_.rng += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void Board::new_game() {
    for (int& c : s_.secret) c = static_cast<int>(next() % kColors);
    s_.rows.clear();
    s_.draft.fill(-1);
}

bool Board::set(int slot, int color) {
    if (over() || slot < 0 || slot >= kPegs || color < 0 || color >= kColors) return false;
    s_.draft[static_cast<size_t>(slot)] = color;
    return true;
}

bool Board::clear(int slot) {
    if (over() || slot < 0 || slot >= kPegs || s_.draft[static_cast<size_t>(slot)] < 0) return false;
    s_.draft[static_cast<size_t>(slot)] = -1;
    return true;
}

bool Board::complete() const {
    return std::all_of(s_.draft.begin(), s_.draft.end(), [](int c) { return c >= 0; });
}

Score Board::submit() {
    if (!complete() || over()) return {};
    Row r;
    r.guess = s_.draft;
    r.score = evaluate(s_.secret, r.guess);
    s_.rows.push_back(r);
    s_.draft.fill(-1);
    return r.score;
}

bool Board::restore(const BoardState& st) {
    auto valid = [](const Code& c, bool allow_empty) {
        return std::all_of(c.begin(), c.end(), [&](int v) { return (v >= 0 && v < kColors) || (allow_empty && v == -1); });
    };
    if (!valid(st.secret, false) || !valid(st.draft, true) || st.rows.size() > static_cast<size_t>(kTurns)) return false;
    for (size_t i = 0; i < st.rows.size(); ++i) {
        const Row& r = st.rows[i];
        if (!valid(r.guess, false) || !(evaluate(st.secret, r.guess) == r.score)) return false;
        if (r.score.exact == kPegs && i + 1 != st.rows.size()) return false;  // nothing after a win
    }
    s_ = st;
    return true;
}

}  // namespace fp
