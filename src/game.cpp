#include "game.hpp"
#include <algorithm>
#include <cmath>
#include <random>
namespace games {
bool red(Card card) {
    return card.suit == 1 || card.suit == 2;
}
const char* game_name(Kind kind) {
    const char* names[] = {"Solitaire", "Spider Solitaire", "FreeCell", "Hearts"};
    return names[static_cast<int>(kind)];
}
std::string card_name(Card card) {
    const char* ranks[] = {"", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};
    const char* suits[] = {"♣", "♦", "♥", "♠"};
    return std::string(ranks[card.rank]) + suits[card.suit];
}
void Game::remember() {
    if (history.size() == 512)
        history.erase(history.begin());
    history.push_back(state);
}
void Game::deal(Kind kind, std::uint32_t seed, int option) {
    state = State{};
    state.kind = kind;
    state.seed = seed;
    state.draw_count = option == 3 ? 3 : 1;
    state.spider_suits = option == 4 ? 4 : option == 2 ? 2 : 1;
    history.clear();
    Pile deck;
    int count = kind == Kind::spider ? 104 : 52;
    for (int i = 0; i < count; ++i) {
        int suit = (i % 52) / 13;
        if (kind == Kind::spider)
            suit = state.spider_suits == 1   ? 3
                   : state.spider_suits == 2 ? (suit % 2 == 0 ? 2 : 3)
                                             : suit;
        deck.push_back({i % 13 + 1, suit, i, true});
    }
    std::mt19937 random(seed);
    // Specified Fisher-Yates with rejection avoids library-dependent shuffle output.
    for (int i = count - 1; i > 0; --i) {
        std::uint32_t n = static_cast<std::uint32_t>(i + 1);
        std::uint32_t limit = UINT32_MAX - (UINT32_MAX % n);
        std::uint32_t sample = 0;
        do {
            sample = static_cast<std::uint32_t>(random());
        } while (sample >= limit);
        int j = static_cast<int>(sample % n);
        std::swap(deck[i], deck[j]);
    }
    if (kind == Kind::solitaire) {
        for (int col = 0; col < 7; ++col)
            for (int row = 0; row <= col; ++row) {
                Card card = deck.back();
                deck.pop_back();
                card.up = row == col;
                state.piles[col].push_back(card);
            }
        state.piles[14] = deck;
    }
    if (kind == Kind::spider) {
        for (int i = 0; i < 54; ++i) {
            Card card = deck.back();
            deck.pop_back();
            card.up = i >= 44;
            state.piles[i % 10].push_back(card);
        }
        state.piles[14] = deck;
    }
    if (kind == Kind::freecell) {
        for (int i = 0; i < 52; ++i)
            state.piles[i % 8].push_back(deck[i]);
    }
    if (kind == Kind::hearts) {
        for (int i = 0; i < 52; ++i)
            state.piles[i % 4].push_back(deck[i]);
        for (int p = 0; p < 4; ++p) {
            for (int i = 0; i < 13; ++i)
                for (int j = i + 1; j < 13; ++j)
                    if (state.piles[p][j].suit * 13 + state.piles[p][j].rank <
                        state.piles[p][i].suit * 13 + state.piles[p][i].rank)
                        std::swap(state.piles[p][i], state.piles[p][j]);
        }
    }
    message = kind == Kind::hearts ? "Choose three cards to pass left."
                                   : "Your table is ready. Take your time.";
}
bool Game::legal(Move m) const {
    if (state.kind == Kind::hearts || state.over || m.from < 0 || m.from >= 20 || m.to < 0 ||
        m.to >= 20 || m.from == m.to)
        return false;
    const Pile& from = state.piles[m.from];
    const Pile& to = state.piles[m.to];
    if (m.index < 0 || m.index >= static_cast<int>(from.size()) || !from[m.index].up)
        return false;
    int columns = state.kind == Kind::spider ? 10 : state.kind == Kind::freecell ? 8 : 7;
    bool source =
        m.from < columns ||
        (state.kind == Kind::solitaire && (m.from == 15 || (m.from >= 10 && m.from < 14))) ||
        (state.kind == Kind::freecell && m.from >= 16);
    if (!source)
        return false;
    if (m.from >= 10 && m.index != static_cast<int>(from.size()) - 1)
        return false;
    int count = static_cast<int>(from.size()) - m.index;
    Card card = from[m.index];
    for (int i = m.index + 1; i < static_cast<int>(from.size()); ++i) {
        if (!from[i].up || from[i - 1].rank != from[i].rank + 1)
            return false;
        if (state.kind == Kind::spider ? from[i - 1].suit != from[i].suit
                                       : red(from[i - 1]) == red(from[i]))
            return false;
    }
    if (m.to >= 10 && m.to < 14 && state.kind != Kind::spider) {
        if (count != 1)
            return false;
        return to.empty() ? card.rank == 1
                          : to.back().suit == card.suit && to.back().rank + 1 == card.rank;
    }
    if (m.to >= 16 && state.kind == Kind::freecell)
        return count == 1 && to.empty();
    if (m.to >= columns)
        return false;
    if (to.empty()) {
        if (state.kind == Kind::solitaire && card.rank != 13)
            return false;
    } else {
        if (to.back().rank != card.rank + 1)
            return false;
        if (state.kind != Kind::spider && red(to.back()) == red(card))
            return false;
    }
    if (state.kind == Kind::freecell) {
        int capacity = 1;
        for (int i = 16; i < 20; ++i)
            if (state.piles[i].empty())
                ++capacity;
        for (int i = 0; i < 8; ++i)
            if (i != m.from && i != m.to && state.piles[i].empty())
                capacity *= 2;
        if (count > capacity)
            return false;
    }
    return true;
}
void Game::settle() {
    int columns = state.kind == Kind::spider ? 10 : state.kind == Kind::freecell ? 8 : 7;
    for (int i = 0; i < columns; ++i) {
        Pile& pile = state.piles[i];
        if (!pile.empty() && !pile.back().up) {
            pile.back().up = true;
            state.score += 5;
        }
        if (state.kind == Kind::spider && pile.size() >= 13) {
            int start = static_cast<int>(pile.size()) - 13;
            bool complete = true;
            for (int j = 0; j < 13; ++j)
                if (!pile[start + j].up || pile[start + j].rank != 13 - j ||
                    pile[start + j].suit != pile.back().suit)
                    complete = false;
            if (complete) {
                state.piles[10].insert(state.piles[10].end(), pile.begin() + start, pile.end());
                pile.resize(start);
                ++state.completed;
                state.score += 100;
                if (!pile.empty())
                    pile.back().up = true;
            }
        }
    }
    if (state.kind == Kind::spider)
        state.over = state.completed == 8;
    else {
        int n = 0;
        for (int i = 10; i < 14; ++i)
            n += static_cast<int>(state.piles[i].size());
        state.over = n == 52;
    }
    if (state.over)
        message = "Beautifully played. You cleared the table!";
}
bool Game::move(Move m) {
    if (!legal(m)) {
        message = "That move does not fit the rules. Try another pile.";
        return false;
    }
    remember();
    Pile& from = state.piles[m.from];
    Pile& to = state.piles[m.to];
    to.insert(to.end(), from.begin() + m.index, from.end());
    from.resize(m.index);
    ++state.moves;
    if (m.to >= 10 && m.to < 14)
        state.score += 10;
    if (m.from >= 10 && m.from < 14)
        state.score -= 15;
    message = "Nicely placed.";
    settle();
    return true;
}
bool Game::draw() {
    if (state.over || (state.kind != Kind::solitaire && state.kind != Kind::spider))
        return false;
    Pile& stock = state.piles[14];
    if (state.kind == Kind::spider) {
        if (stock.empty())
            return false;
        for (int i = 0; i < 10; ++i)
            if (state.piles[i].empty()) {
                message = "Fill every empty column before dealing a new row.";
                return false;
            }
        remember();
        for (int i = 0; i < 10; ++i) {
            Card c = stock.back();
            stock.pop_back();
            c.up = true;
            state.piles[i].push_back(c);
        }
    } else {
        Pile& waste = state.piles[15];
        if (stock.empty() && waste.empty())
            return false;
        remember();
        if (stock.empty()) {
            while (!waste.empty()) {
                stock.push_back(waste.back());
                waste.pop_back();
            }
        } else
            for (int i = 0; i < state.draw_count && !stock.empty(); ++i) {
                Card c = stock.back();
                stock.pop_back();
                c.up = true;
                waste.push_back(c);
            }
    }
    ++state.moves;
    message = "Fresh possibilities.";
    settle();
    return true;
}
bool Game::undo() {
    if (history.empty())
        return false;
    state = history.back();
    history.pop_back();
    message = "Move undone.";
    return true;
}
Move Game::hint() const {
    for (int to = 10; to < 14; ++to)
        for (int from = 0; from < 20; ++from) {
            Move m{from, static_cast<int>(state.piles[from].size()) - 1, to};
            if (!(from >= 10 && from < 14) && legal(m))
                return m;
        }
    for (int from = 0; from < 20; ++from)
        for (int i = 0; i < static_cast<int>(state.piles[from].size()); ++i)
            for (int to = 0; to < 10; ++to) {
                Move m{from, i, to};
                if (!(from >= 10 && from < 14) && legal(m) &&
                    !(from < 10 && i == 0 && state.piles[to].empty()))
                    return m;
            }
    if (state.kind == Kind::freecell) {
        for (int to = 16; to < 20; ++to)
            for (int from = 0; from < 8; ++from) {
                Move move{from, static_cast<int>(state.piles[from].size()) - 1, to};
                if (legal(move))
                    return move;
            }
    }
    return {};
}
bool Game::pass(const std::vector<int>& indices) {
    if (state.kind != Kind::hearts || !state.passing || indices.size() != 3)
        return false;
    std::array<Pile, 4> gifts;
    std::array<std::vector<int>, 4> selected;
    selected[0] = indices;
    for (int p = 1; p < 4; ++p)
        selected[p] = hearts_pass_choice(state, p);
    for (int i = 0; i < 3; ++i) {
        if (indices[i] < 0 || indices[i] >= 13)
            return false;
        for (int j = 0; j < i; ++j)
            if (indices[i] == indices[j])
                return false;
    }
    remember();
    for (int p = 0; p < 4; ++p) {
        std::sort(selected[p].begin(), selected[p].end());
        for (int i = 2; i >= 0; --i) {
            int index = selected[p][i];
            gifts[p].push_back(state.piles[p][index]);
            state.piles[p].erase(state.piles[p].begin() + index);
        }
    }
    int direction = state.round % 4 == 0 ? 1 : state.round % 4 == 1 ? 3 : 2;
    for (int p = 0; p < 4; ++p) {
        Pile& hand = state.piles[(p + direction) % 4];
        hand.insert(hand.end(), gifts[p].begin(), gifts[p].end());
    }
    for (Pile& hand : state.piles) {
        for (int i = 0; i < static_cast<int>(hand.size()); ++i)
            for (int j = i + 1; j < static_cast<int>(hand.size()); ++j)
                if (hand[j].suit * 13 + hand[j].rank < hand[i].suit * 13 + hand[i].rank)
                    std::swap(hand[i], hand[j]);
    }
    state.passing = false;
    for (int p = 0; p < 4; ++p)
        for (const Card& c : state.piles[p])
            if (c.suit == 0 && c.rank == 2)
                state.turn = p;
    state.leader = state.turn;
    message = "The two of clubs leads. Avoid hearts and the queen of spades.";
    return true;
}
bool Game::legal_heart(int player, int index) const {
    if (state.kind != Kind::hearts || state.passing || state.over || state.piles[10].size() == 4 ||
        player != state.turn || index < 0 || index >= static_cast<int>(state.piles[player].size()))
        return false;
    const Pile& hand = state.piles[player];
    Card c = hand[index];
    const Pile& trick = state.piles[10];
    if (state.trick_number == 0 && trick.empty())
        return c.suit == 0 && c.rank == 2;
    if (!trick.empty()) {
        bool follows = false;
        for (const Card& v : hand)
            if (v.suit == trick.front().suit)
                follows = true;
        if (follows)
            return c.suit == trick.front().suit;
    }
    if (state.trick_number == 0 && (c.suit == 2 || (c.suit == 3 && c.rank == 12))) {
        for (const Card& v : hand)
            if (v.suit != 2 && !(v.suit == 3 && v.rank == 12))
                return false;
    }
    if (trick.empty() && !state.hearts_broken && c.suit == 2) {
        for (const Card& v : hand)
            if (v.suit != 2)
                return false;
    }
    return true;
}
bool Game::play(int index) {
    if (!legal_heart(state.turn, index)) {
        message = "Follow suit if you can; hearts cannot lead until broken.";
        return false;
    }
    remember();
    Pile& hand = state.piles[state.turn];
    Card c = hand[index];
    state.piles[10].push_back(c);
    hand.erase(hand.begin() + index);
    if (c.suit == 2)
        state.hearts_broken = true;
    state.turn = (state.turn + 1) % 4;
    ++state.moves;
    message = "Trick in play.";
    return true;
}
namespace {
const char* const presidents[] = {
    "Washington", "Adams",   "Jefferson", "Madison",  "Monroe",   "Jackson",   "Van Buren",
    "Harrison",   "Tyler",   "Polk",      "Taylor",   "Fillmore", "Pierce",    "Buchanan",
    "Lincoln",    "Grant",   "Hayes",     "Garfield", "Arthur",   "Cleveland", "McKinley",
    "Roosevelt",  "Taft",    "Wilson",    "Harding",  "Coolidge", "Hoover",    "Truman",
    "Eisenhower", "Kennedy", "Johnson",   "Nixon",    "Ford",     "Reagan"};
constexpr int president_count = static_cast<int>(sizeof(presidents) / sizeof(presidents[0]));
std::uint32_t mix(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
int high(Card c) {
    return c.rank == 1 ? 14 : c.rank;
}
bool queen(Card c) {
    return c.suit == 3 && c.rank == 12;
}
int penalty(Card c) {
    return c.suit == 2 ? 1 : queen(c) ? 13 : 0;
}
// A compact hand of Hearts for looking ahead. It follows the same rules as Game.
struct HeartsSim {
    std::array<std::vector<Card>, 4> hands;
    std::vector<Card> trick;
    std::array<int, 4> points{};
    std::array<bool, 52> played{};
    int leader = 0, turn = 0, tricks = 0;
    bool broken = false;
    [[nodiscard]] bool legal(int player, Card c) const {
        const std::vector<Card>& hand = hands[player];
        if (tricks == 0 && trick.empty())
            return c.suit == 0 && c.rank == 2;
        if (!trick.empty()) {
            for (const Card& v : hand)
                if (v.suit == trick.front().suit)
                    return c.suit == trick.front().suit;
        }
        if (tricks == 0 && penalty(c)) {
            for (const Card& v : hand)
                if (!penalty(v))
                    return false;
        }
        if (trick.empty() && !broken && c.suit == 2) {
            for (const Card& v : hand)
                if (v.suit != 2)
                    return false;
        }
        return true;
    }
    [[nodiscard]] bool queen_out() const {
        return !played[3 * 13 + 11];
    }
    void play(std::size_t index) {
        std::vector<Card>& hand = hands[turn];
        const Card c = hand[index];
        hand.erase(hand.begin() + static_cast<std::ptrdiff_t>(index));
        trick.push_back(c);
        played[c.suit * 13 + c.rank - 1] = true;
        broken = broken || c.suit == 2;
        turn = (turn + 1) % 4;
        if (trick.size() < 4)
            return;
        int winner = 0, best = 0, taken = 0;
        for (int i = 0; i < 4; ++i) {
            if (trick[i].suit == trick.front().suit && high(trick[i]) > best) {
                best = high(trick[i]);
                winner = i;
            }
            taken += penalty(trick[i]);
        }
        leader = turn = (leader + winner) % 4;
        points[leader] += taken;
        trick.clear();
        ++tricks;
    }
};
// A sensible rule-of-thumb card: duck under the winner when possible, shed danger when void,
// and lead low from safe suits. Lookahead uses it for every seat.
std::size_t rule_of_thumb(const HeartsSim& sim) {
    const std::vector<Card>& hand = sim.hands[sim.turn];
    std::vector<std::size_t> legal;
    for (std::size_t i = 0; i < hand.size(); ++i)
        if (sim.legal(sim.turn, hand[i]))
            legal.push_back(i);
    if (legal.size() == 1)
        return legal.front();
    bool holds_queen = false;
    std::array<int, 4> counts{};
    for (const Card& c : hand) {
        holds_queen = holds_queen || queen(c);
        ++counts[c.suit];
    }
    const bool danger = sim.queen_out() && !holds_queen;
    std::size_t best = legal.front();
    double best_score = 1e9;
    if (sim.trick.empty()) {
        for (std::size_t i : legal) {
            const Card c = hand[i];
            double score = high(c) + counts[c.suit] * .4;
            if (c.suit == 2)
                score += 9;
            if (c.suit == 3) {
                if (holds_queen)
                    score += 12;
                else if (danger && high(c) >= 13)
                    score += 30;
                else if (danger)
                    score -= 3; // flush the queen out with a low spade
            }
            if (score < best_score) {
                best_score = score;
                best = i;
            }
        }
        return best;
    }
    const int led = sim.trick.front().suit;
    int winning = 0, taken = 0;
    for (const Card& c : sim.trick) {
        if (c.suit == led)
            winning = std::max(winning, high(c));
        taken += penalty(c);
    }
    const bool last = sim.trick.size() == 3;
    if (hand[legal.front()].suit == led) {
        // Duck with the highest card that still loses the trick.
        int duck = -1;
        for (std::size_t i : legal)
            if (high(hand[i]) < winning && (duck < 0 || high(hand[i]) > high(hand[duck])))
                duck = static_cast<int>(i);
        if (duck >= 0)
            return static_cast<std::size_t>(duck);
        // This trick is ours anyway: win it with the biggest card, but never the queen,
        // and keep spade honors back when the queen could still fall on them.
        for (std::size_t i : legal) {
            const Card c = hand[i];
            double score = -high(c);
            if (queen(c))
                score += 100;
            if (!last && led == 3 && danger && high(c) >= 13)
                score += 40;
            if (score < best_score) {
                best_score = score;
                best = i;
            }
        }
        return best;
    }
    // Void in the led suit: unload the most dangerous card.
    for (std::size_t i : legal) {
        const Card c = hand[i];
        double score = -high(c) - (4 - std::min(4, counts[c.suit])) * .5;
        if (queen(c))
            score -= 200;
        else if (c.suit == 3 && high(c) >= 13 && sim.queen_out())
            score -= 120;
        else if (c.suit == 2)
            score -= 40;
        if (score < best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}
} // namespace
std::string hearts_name(const State& state, int player) {
    if (player <= 0 || player > 3)
        return "You";
    // The match keeps its seats from hand to hand: next_round deals seed + 1 and round + 1.
    const std::uint32_t match = state.seed - static_cast<std::uint32_t>(state.round);
    std::array<int, 3> seats{};
    for (int p = 0; p < 3; ++p) {
        int pick = static_cast<int>(mix(match * 3U + static_cast<std::uint32_t>(p) + 77U) %
                                    president_count);
        bool clash = true;
        while (clash) {
            clash = false;
            for (int q = 0; q < p; ++q)
                if (seats[q] == pick) {
                    pick = (pick + 1) % president_count;
                    clash = true;
                }
        }
        seats[p] = pick;
    }
    return presidents[seats[player - 1]];
}
HeartsSkill hearts_skill(const State& state, int player) {
    if (player <= 0 || player > 3)
        return HeartsSkill::sharp;
    // A fresh draw each hand: one sharp seat, one forgetful seat, one steady seat.
    const std::uint32_t roll = mix(state.seed * 2654435761U + 0x5bd1e995U);
    const int sharp = 1 + static_cast<int>(roll % 3);
    const int other_a = sharp % 3 + 1, other_b = other_a % 3 + 1;
    const int forgetful = (roll >> 9) & 1 ? other_a : other_b;
    return player == sharp       ? HeartsSkill::sharp
           : player == forgetful ? HeartsSkill::forgetful
                                 : HeartsSkill::steady;
}
std::vector<int> hearts_pass_choice(const State& state, int player) {
    const Pile& hand = state.piles[player];
    const int n = static_cast<int>(hand.size());
    std::vector<int> order(n);
    for (int i = 0; i < n; ++i)
        order[i] = i;
    std::vector<double> value(n);
    std::array<int, 4> counts{};
    int low_spades = 0;
    for (const Card& c : hand) {
        ++counts[c.suit];
        if (c.suit == 3 && high(c) < 12)
            ++low_spades;
    }
    const bool simple = hearts_skill(state, player) == HeartsSkill::forgetful;
    for (int i = 0; i < n; ++i) {
        const Card c = hand[i];
        if (simple) {
            value[i] = high(c) + (c.suit == 3 ? 15 : 0);
            continue;
        }
        double v = high(c);
        if (queen(c))
            v += low_spades >= 4 ? -4 : 90; // a well-guarded queen can stay
        else if (c.suit == 3 && high(c) >= 13)
            v += low_spades >= 4 ? 5 : 70;
        else if (c.suit == 2)
            v += high(c) >= 10 ? 30 : 4;
        else if (counts[c.suit] <= 2 && !(c.suit == 0 && c.rank == 2))
            v += 22 + (3 - counts[c.suit]) * 6; // shorten a suit to discard later
        if (c.suit == 0 && c.rank == 2)
            v -= 10;
        value[i] = v;
    }
    struct ByValue {
        const std::vector<double>& value;
        bool operator()(int a, int b) const {
            return value[a] > value[b];
        }
    };
    std::stable_sort(order.begin(), order.end(), ByValue{value});
    order.resize(std::min(3, n));
    return order;
}
int Game::computer_choice() const {
    const int me = state.turn;
    const Pile& mine = state.piles[me];
    std::vector<int> legal;
    for (int i = 0; i < static_cast<int>(mine.size()); ++i)
        if (legal_heart(me, i))
            legal.push_back(i);
    if (legal.size() <= 1)
        return legal.empty() ? -1 : legal.front();
    const HeartsSkill skill = hearts_skill(state, me);
    const double memory = skill == HeartsSkill::sharp    ? 1.0
                          : skill == HeartsSkill::steady ? .75
                                                         : .4;
    const int samples = skill == HeartsSkill::sharp ? 40 : skill == HeartsSkill::steady ? 14 : 4;
    std::mt19937 random(mix(state.seed * 31U + static_cast<std::uint32_t>(state.moves) * 7919U +
                            static_cast<std::uint32_t>(me)));
    std::uniform_real_distribution<double> unit(0, 1);
    // What this player knows: the trick on the table, the cards it remembers being played,
    // and which suits it remembers each player failing to follow.
    const Pile& trick = state.piles[10];
    const Pile& history = state.piles[11];
    std::array<bool, 52> seen{};
    std::array<std::array<bool, 4>, 4> void_in{};
    const int done = static_cast<int>(history.size()) / 4;
    int lead = state.leader;
    std::vector<int> leaders(done);
    for (int t = done - 1; t >= 0; --t) {
        int winner = 0, best = 0;
        for (int k = 0; k < 4; ++k) {
            const Card c = history[t * 4 + k];
            if (c.suit == history[t * 4].suit && high(c) > best) {
                best = high(c);
                winner = k;
            }
        }
        lead = ((lead - winner) % 4 + 4) % 4;
        leaders[t] = lead;
    }
    for (int t = 0; t < done; ++t)
        for (int k = 0; k < 4; ++k) {
            const Card c = history[t * 4 + k];
            const std::uint32_t roll = mix(static_cast<std::uint32_t>(c.id) * 131U +
                                           static_cast<std::uint32_t>(me) * 7U + state.seed);
            if (roll % 1000 < memory * 1000)
                seen[c.suit * 13 + c.rank - 1] = true;
            if (c.suit != history[t * 4].suit && (roll >> 12) % 1000 < memory * 1000)
                void_in[(leaders[t] + k) % 4][history[t * 4].suit] = true;
        }
    for (std::size_t k = 0; k < trick.size(); ++k)
        if (trick[k].suit != trick.front().suit)
            void_in[(state.leader + static_cast<int>(k)) % 4][trick.front().suit] = true;
    std::vector<Card> pool;
    std::array<bool, 52> visible{};
    for (const Card& c : mine)
        visible[c.suit * 13 + c.rank - 1] = true;
    for (const Card& c : trick)
        visible[c.suit * 13 + c.rank - 1] = true;
    for (int s = 0; s < 4; ++s)
        for (int r = 1; r <= 13; ++r)
            if (!visible[s * 13 + r - 1] && !seen[s * 13 + r - 1])
                pool.push_back({r, s, s * 13 + r - 1, true});
    HeartsSim base;
    base.hands[me] = mine;
    base.trick = trick;
    base.leader = state.leader;
    base.turn = me;
    base.tricks = state.trick_number;
    base.broken = state.hearts_broken;
    for (const Card& c : history)
        base.played[c.suit * 13 + c.rank - 1] = true;
    for (const Card& c : trick)
        base.played[c.suit * 13 + c.rank - 1] = true;
    // Rule-of-thumb choice, plus a forgetful player's occasional slip.
    HeartsSim plain = base;
    std::size_t habit = rule_of_thumb(plain);
    if (skill == HeartsSkill::forgetful && unit(random) < .18)
        habit = static_cast<std::size_t>(legal[random() % legal.size()]);
    // Guess the hidden hands several times, play each candidate out with rules of thumb,
    // and keep the card that leaves this player with the fewest points on average.
    std::vector<double> cost(legal.size(), 0);
    for (int s = 0; s < samples; ++s) {
        std::shuffle(pool.begin(), pool.end(), random);
        HeartsSim guess = base;
        std::array<int, 4> room{};
        for (int p = 0; p < 4; ++p)
            room[p] = p == me ? 0 : static_cast<int>(state.piles[p].size());
        std::vector<Card> spare;
        for (const Card& c : pool) {
            int options[3], count = 0;
            for (int p = 0; p < 4; ++p)
                if (room[p] > 0 && !void_in[p][c.suit])
                    options[count++] = p;
            if (count == 0) {
                spare.push_back(c);
                continue;
            }
            const int p = options[random() % count];
            guess.hands[p].push_back(c);
            --room[p];
        }
        // Remaining seats take whatever is left, rules about voids notwithstanding.
        for (int p = 0; p < 4; ++p)
            while (room[p] > 0 && !spare.empty()) {
                guess.hands[p].push_back(spare.back());
                spare.pop_back();
                --room[p];
            }
        for (std::size_t m = 0; m < legal.size(); ++m) {
            HeartsSim sim = guess;
            sim.play(static_cast<std::size_t>(legal[m]));
            while (sim.tricks < 13) {
                bool empty = sim.hands[sim.turn].empty();
                if (empty)
                    break; // a short guess; score what has been played
                sim.play(rule_of_thumb(sim));
            }
            std::array<int, 4> total{};
            int moon = -1;
            for (int p = 0; p < 4; ++p) {
                total[p] = state.points[p] + sim.points[p];
                if (total[p] == 26)
                    moon = p;
            }
            double mine_cost = moon < 0 ? total[me] : moon == me ? -6 : 26;
            // A little credit for loading points onto whoever leads the match.
            cost[m] +=
                mine_cost - .04 * (total[(me + 1) % 4] + total[(me + 2) % 4] + total[(me + 3) % 4]);
        }
    }
    std::size_t best = 0;
    double best_cost = 1e18;
    for (std::size_t m = 0; m < legal.size(); ++m) {
        double c = cost[m] / std::max(1, samples);
        if (static_cast<std::size_t>(legal[m]) == habit)
            c -= skill == HeartsSkill::forgetful ? 1.5 : .05;
        if (c < best_cost) {
            best_cost = c;
            best = m;
        }
    }
    return legal[best];
}
bool Game::advance_trick() {
    Pile& trick = state.piles[10];
    if (state.kind != Kind::hearts || trick.size() != 4)
        return false;
    int winner = 0, high = 0, points = 0;
    for (int i = 0; i < 4; ++i) {
        Card c = trick[i];
        int rank = c.rank == 1 ? 14 : c.rank;
        if (c.suit == trick.front().suit && rank > high) {
            high = rank;
            winner = i;
        }
        if (c.suit == 2)
            ++points;
        if (c.suit == 3 && c.rank == 12)
            points += 13;
    }
    state.turn = (state.leader + winner) % 4;
    state.leader = state.turn;
    state.points[state.turn] += points;
    state.piles[11].insert(state.piles[11].end(), trick.begin(), trick.end());
    trick.clear();
    ++state.trick_number;
    message = state.turn == 0 ? "You lead the next trick." : "Your opponents are thinking…";
    if (state.trick_number == 13) {
        for (int p = 0; p < 4; ++p)
            if (state.points[p] == 26) {
                for (int q = 0; q < 4; ++q)
                    state.points[q] = q == p ? 0 : 26;
                break;
            }
        for (int p = 0; p < 4; ++p) {
            state.totals[p] += state.points[p];
            if (state.totals[p] >= 100)
                state.over = true;
        }
        message = state.over ? "Match complete. The lowest score wins."
                             : "Hand complete. Click Next hand to continue.";
    }
    return true;
}
void Game::next_round() {
    if (state.kind != Kind::hearts || state.trick_number != 13 || state.over)
        return;
    std::array<int, 4> totals = state.totals;
    int round = state.round + 1;
    std::uint32_t seed = state.seed + 1;
    deal(Kind::hearts, seed);
    state.totals = totals;
    state.round = round;
    if (round % 4 == 3) {
        state.passing = false;
        for (int p = 0; p < 4; ++p)
            for (const Card& c : state.piles[p])
                if (c.suit == 0 && c.rank == 2)
                    state.turn = p;
        state.leader = state.turn;
        message = "Hold hand: no passing this round.";
    } else
        message = round % 4 == 1 ? "Choose three cards to pass right."
                                 : "Choose three cards to pass across.";
}
bool Game::invariant() const {
    std::array<bool, 104> seen{};
    int count = 0;
    for (const Pile& pile : state.piles)
        for (const Card& c : pile) {
            if (c.id < 0 || c.id >= (state.kind == Kind::spider ? 104 : 52) || seen[c.id] ||
                c.rank < 1 || c.rank > 13 || c.suit < 0 || c.suit > 3)
                return false;
            seen[c.id] = true;
            ++count;
        }
    return count == (state.kind == Kind::spider ? 104 : 52);
}
} // namespace games
