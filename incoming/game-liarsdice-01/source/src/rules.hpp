#pragma once
// Liar's Dice, the rules. Everyone has five dice under a cup. In turn each
// player raises the bid, a claim about every die on the table ("seven fours":
// at least seven of the dice show a four), or calls the last bidder a liar.
// Ones are wild: they count as any face, and nobody bids on ones. A raise is
// more dice of any face, or the same number of a higher face. On a call every
// cup is lifted and the dice are counted: if the bid stands, the caller loses
// a die; if not, the bidder does. The loser opens the next round (or, if they
// are out, the next player round the table). The last with dice wins.
#include <cstdint>
#include <string>
#include <vector>

namespace ld {

constexpr int kStartDice = 5;

struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ULL + 0x2545F4914F6CDD1DULL) { if (!s) s = 1; next(); }
    std::uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    int range(int n) { return n <= 1 ? 0 : static_cast<int>(next() % static_cast<std::uint64_t>(n)); }
    double unit() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
};

struct Bid {
    int qty = 0;   // 0: no bid yet
    int face = 0;  // 2..6
    bool none() const { return qty <= 0; }
};
bool beats(Bid a, Bid b);         // is `a` a legal raise over `b` (or a legal opening if b is none)?
std::string bid_words(Bid b);     // "seven fours"
std::string face_word(int face, bool plural);
std::string number_word(int n);

struct BidRec {
    int who;
    Bid bid;
};

struct Reveal {
    Bid bid;
    int bidder = -1, caller = -1;
    int count = 0;       // dice showing the face or a one
    bool stood = false;  // the bid was true
    int loser = -1;
};

class Match {
public:
    int players = 0;
    std::vector<std::vector<int>> dice;   // per player, their dice this round (faces 1..6)
    std::vector<int> count;               // dice each player still has
    int turn = 0;                         // whose move
    Bid bid;                              // the standing bid
    int bidder = -1;                      // who made it
    std::vector<BidRec> history;          // this round's bids, in order
    int round = 0;

    void start(int n, int first, Rng& rng);
    void roll(Rng& rng);                  // a fresh round: every cup shaken
    bool alive(int p) const { return count[static_cast<size_t>(p)] > 0; }
    int alive_count() const;
    int total_dice() const;
    int next_alive(int p) const;
    bool over() const { return alive_count() <= 1; }
    int winner() const;
    bool can_call() const { return !bid.none(); }
    bool place(Bid b);                    // the current player raises; false if illegal
    Reveal call();                        // the current player calls the standing bid; a die is lost, the next round is set up (dice not yet rolled)
    int counted(int face) const;          // dice showing the face or a one
};

}  // namespace ld
