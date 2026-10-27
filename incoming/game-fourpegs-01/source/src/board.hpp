#pragma once
// Four Pegs rules: a secret of four pegs from six colours (repeats allowed),
// ten guesses, and feedback counting pegs in the exact place and pegs that
// belong elsewhere. Pure logic: no drawing, sound, clock, or files.
#include <array>
#include <cstdint>
#include <vector>

namespace fp {

constexpr int kPegs = 4, kColors = 6, kTurns = 10;
using Code = std::array<int, kPegs>;  // colours 0..5; -1 marks an empty slot in a draft

struct Score {
    int exact = 0;   // right colour, right place
    int near = 0;    // right colour, wrong place
    bool operator==(const Score& o) const { return exact == o.exact && near == o.near; }
};

Score evaluate(const Code& secret, const Code& guess);

struct Row {
    Code guess{};
    Score score;
};

struct BoardState {  // everything needed to save and restore a game in progress
    std::uint64_t rng = 0;
    Code secret{};
    std::vector<Row> rows;
    Code draft{-1, -1, -1, -1};
};

class Board {
public:
    explicit Board(std::uint64_t seed);

    void new_game();
    bool set(int slot, int color);   // place a colour in the draft
    bool clear(int slot);
    bool complete() const;           // every draft slot filled
    Score submit();                  // judge the draft; requires complete() && !over()

    const Code& draft() const { return s_.draft; }
    const Code& secret() const { return s_.secret; }
    const std::vector<Row>& rows() const { return s_.rows; }
    int turns_used() const { return static_cast<int>(s_.rows.size()); }
    int turns_left() const { return kTurns - turns_used(); }
    bool won() const { return !s_.rows.empty() && s_.rows.back().score.exact == kPegs; }
    bool lost() const { return !won() && turns_used() >= kTurns; }
    bool over() const { return won() || lost(); }

    const BoardState& state() const { return s_; }
    bool restore(const BoardState& st);  // rejects inconsistent states

private:
    BoardState s_;
    std::uint64_t next();
};

}  // namespace fp
