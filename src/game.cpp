#include "game.hpp"
#include <algorithm>
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
    for (int p = 1; p < 4; ++p) {
        for (int i = 0; i < 13; ++i)
            selected[p].push_back(i);
        for (int i = 0; i < 13; ++i)
            for (int j = i + 1; j < 13; ++j) {
                Card a = state.piles[p][selected[p][i]], b = state.piles[p][selected[p][j]];
                int va = a.rank + (a.suit == 3 ? 15 : 0), vb = b.rank + (b.suit == 3 ? 15 : 0);
                if (vb > va)
                    std::swap(selected[p][i], selected[p][j]);
            }
        selected[p].resize(3);
    }
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
int Game::computer_choice() const {
    int best = -1, value = -1000;
    for (int i = 0; i < static_cast<int>(state.piles[state.turn].size()); ++i)
        if (legal_heart(state.turn, i)) {
            Card c = state.piles[state.turn][i];
            int rank = c.rank == 1 ? 14 : c.rank;
            int score = -rank;
            if (!state.piles[10].empty() && c.suit != state.piles[10].front().suit)
                score = rank + (c.suit == 2 ? 20 : 0) + (c.suit == 3 && c.rank == 12 ? 100 : 0);
            if (score > value) {
                best = i;
                value = score;
            }
        }
    return best;
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
