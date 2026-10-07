#include "rules.hpp"

#include <algorithm>

namespace ld {

bool beats(Bid a, Bid b) {
    if (a.qty <= 0 || a.face < 2 || a.face > 6) return false;
    if (b.none()) return true;
    return a.qty > b.qty || (a.qty == b.qty && a.face > b.face);
}

std::string number_word(int n) {
    static const char* w[] = {"no", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve",
                              "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen", "twenty"};
    if (n >= 0 && n <= 20) return w[n];
    return std::to_string(n);
}

std::string face_word(int face, bool plural) {
    static const char* one[] = {"", "one", "two", "three", "four", "five", "six"};
    static const char* many[] = {"", "ones", "twos", "threes", "fours", "fives", "sixes"};
    return face >= 1 && face <= 6 ? (plural ? many[face] : one[face]) : "?";
}

std::string bid_words(Bid b) {
    if (b.none()) return "no bid";
    return number_word(b.qty) + " " + face_word(b.face, b.qty != 1);
}

void Match::start(int n, int first, Rng& rng) {
    players = n;
    count.assign(static_cast<size_t>(n), kStartDice);
    dice.assign(static_cast<size_t>(n), {});
    turn = std::clamp(first, 0, n - 1);
    round = 0;
    roll(rng);
}

void Match::roll(Rng& rng) {
    for (int p = 0; p < players; ++p) {
        auto& d = dice[static_cast<size_t>(p)];
        d.resize(static_cast<size_t>(count[static_cast<size_t>(p)]));
        for (int& v : d) v = 1 + rng.range(6);
        std::sort(d.begin(), d.end());
    }
    bid = {};
    bidder = -1;
    history.clear();
    ++round;
    if (!alive(turn)) turn = next_alive(turn);
}

int Match::alive_count() const {
    int k = 0;
    for (int c : count) k += c > 0;
    return k;
}

int Match::total_dice() const {
    int k = 0;
    for (int c : count) k += c;
    return k;
}

int Match::next_alive(int p) const {
    for (int k = 1; k <= players; ++k) {
        const int q = (p + k) % players;
        if (alive(q)) return q;
    }
    return p;
}

int Match::winner() const {
    if (alive_count() != 1) return -1;
    for (int p = 0; p < players; ++p)
        if (alive(p)) return p;
    return -1;
}

bool Match::place(Bid b) {
    if (!beats(b, bid) || b.qty > total_dice()) return false;
    bid = b;
    bidder = turn;
    history.push_back({turn, b});
    turn = next_alive(turn);
    return true;
}

int Match::counted(int face) const {
    int k = 0;
    for (const auto& d : dice)
        for (int v : d) k += v == face || v == 1;
    return k;
}

Reveal Match::call() {
    Reveal r;
    r.bid = bid;
    r.bidder = bidder;
    r.caller = turn;
    r.count = counted(bid.face);
    r.stood = r.count >= bid.qty;
    r.loser = r.stood ? r.caller : r.bidder;
    --count[static_cast<size_t>(r.loser)];
    // the loser opens the next round, or the next player round the table if they're out
    turn = alive(r.loser) ? r.loser : next_alive(r.loser);
    return r;
}

}  // namespace ld
