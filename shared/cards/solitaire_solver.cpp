#include "solitaire_solver.hpp"
#include <algorithm>
#include <cstring>
#include <utility>

// Solver design
// -------------
// Each game has a small engine over a packed state (columns stored back to back
// in one byte array, cards coded suit * 13 + rank - 1). Column indices equal the
// Game pile indices, so tableau moves translate one to one; foundation and free
// cell slots are canonical (counts per suit, sorted cells) and are mapped onto the
// real piles when a line is replayed through Game.
//
// Search is a ladder of beam searches over a per-game heuristic (Spider alternates
// two weightings between rungs) with a transposition set of 64-bit hashes (column
// order does not matter for the hash, free cells are sorted) and a budget of
// expansions. Moves are atomic: FreeCell sequence moves up to the
// Game::legal capacity, Spider run moves, and Klondike "draw until card X is on
// the waste, then play it" macros that model the stock and unlimited recycling
// exactly for draw 1 and draw 3. Safe foundation moves are played automatically
// (and appear in the solution as ordinary moves).
//
// Pruning that can only make the solver weaker, never wrong: Klondike never moves
// a king-led stack from one empty column to another or a foundation card of rank
// below 3 back down; Spider splits a same-suit run only onto its own suit to build
// a longer run, or into an empty column when the empty columns could not otherwise
// all be filled before a deal; empty targets are tried in the first empty column.
//
// Difficulty
// ----------
// Two player models measure a deal:
//  * the casual player: plays by fixed human priorities (foundation moves,
//    uncovering face-down cards, building down in suit, filling empty columns),
//    only looks at face-up cards (FreeCell is fully open, so there it compares the
//    positions one move ahead), never undoes and never returns to a position;
//  * the solver's beam ladder: beam searches of width 10, 20, 40, ... (each keeps
//    that many best positions per move) until one wins. The width that wins is how
//    many alternatives a player has to keep in mind.
// easy   the casual player wins, or the beam width needed is at most easy_width;
// medium the beam width needed is at most medium_width;
// hard   a wider beam is needed (up to the node budget).
// grading_limits() holds the per-bucket widths, calibrated on seeds 1-200 so each
// bucket gets a sensible share (casual-player wins of all deals: Klondike draw 1
// 45%, draw 3 12%, Spider 1 suit 68%, 2 suits <1%, 4 suits 0%, FreeCell 4%).
// Klondike and Spider 1 suit use the casual player alone for Easy; Spider 2/4 suits
// and FreeCell, where it almost never wins, also admit the narrowest beams.

namespace games {
namespace {

using u8 = std::uint8_t;
using u64 = std::uint64_t;

inline int rank_of(u8 c) {
    return c % 13 + 1;
}
inline int suit_of(u8 c) {
    return c / 13;
}
inline bool red_of(u8 c) {
    int s = c / 13;
    return s == 1 || s == 2;
}
inline u8 code_of(const Card& c) {
    return static_cast<u8>(c.suit * 13 + c.rank - 1);
}
inline u64 mix64(u64 x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}
inline u64 fnv(const u8* p, int n, u64 h) {
    for (int i = 0; i < n; ++i)
        h = (h ^ p[i]) * 0x100000001b3ULL;
    return h;
}

class SeenSet {
  public:
    SeenSet() : table_(1u << 12, 0) {}
    bool insert(u64 key) {
        if (key == 0)
            key = 1;
        if ((count_ + 1) * 2 > table_.size())
            grow();
        return place(key);
    }
    bool contains(u64 key) const {
        if (key == 0)
            key = 1;
        std::size_t mask = table_.size() - 1;
        for (std::size_t i = key & mask; table_[i]; i = (i + 1) & mask)
            if (table_[i] == key)
                return true;
        return false;
    }

  private:
    bool place(u64 key) {
        std::size_t mask = table_.size() - 1;
        std::size_t i = key & mask;
        while (table_[i]) {
            if (table_[i] == key)
                return false;
            i = (i + 1) & mask;
        }
        table_[i] = key;
        ++count_;
        return true;
    }
    void grow() {
        std::vector<u64> old;
        old.swap(table_);
        table_.assign(old.size() * 2, 0);
        count_ = 0;
        for (u64 k : old)
            if (k)
                place(k);
    }
    std::vector<u64> table_;
    std::size_t count_ = 0;
};

// Columns packed back to back; hid[c] face-down cards at the bottom of column c.
template <int N, int C> struct Columns {
    u8 len[C]{};
    u8 hid[C]{};
    u8 cards[N]{};
    int start(int c) const {
        int s = 0;
        for (int i = 0; i < c; ++i)
            s += len[i];
        return s;
    }
    int total() const {
        return start(C);
    }
    u8 at(int c, int i) const {
        return cards[start(c) + i];
    }
    u8 top(int c) const {
        return cards[start(c) + len[c] - 1];
    }
    void push(int c, u8 v) {
        int t = total();
        int e = start(c) + len[c];
        std::memmove(cards + e + 1, cards + e, t - e);
        cards[e] = v;
        ++len[c];
    }
    u8 pop(int c) {
        int t = total();
        int e = start(c) + len[c] - 1;
        u8 v = cards[e];
        std::memmove(cards + e, cards + e + 1, t - e - 1);
        --len[c];
        return v;
    }
    void drop(int c, int k) {
        int t = total();
        int e = start(c) + len[c];
        std::memmove(cards + e - k, cards + e, t - e);
        len[c] = static_cast<u8>(len[c] - k);
    }
    void move_tail(int a, int k, int b) {
        u8 tmp[N];
        int t = total();
        int ea = start(a) + len[a];
        std::memcpy(tmp, cards + ea - k, k);
        std::memmove(cards + ea - k, cards + ea, t - ea);
        len[a] = static_cast<u8>(len[a] - k);
        t -= k;
        int eb = start(b) + len[b];
        std::memmove(cards + eb + k, cards + eb, t - eb);
        std::memcpy(cards + eb, tmp, k);
        len[b] = static_cast<u8>(len[b] + k);
    }
    bool reveal(int c) {
        if (len[c] && hid[c] == len[c]) {
            --hid[c];
            return true;
        }
        return false;
    }
    // Independent of column order.
    u64 hash() const {
        u64 h = 0;
        int s = 0;
        for (int c = 0; c < C; ++c) {
            u64 x = fnv(cards + s, len[c], 0xcbf29ce484222325ULL ^ (u64(hid[c]) << 8) ^ len[c]);
            h += mix64(x);
            s += len[c];
        }
        return h;
    }
};

enum : u8 { T2F, T2T, W2F, W2T, F2T, C2F, C2T, T2C, DEAL };
// T2T: a source column, b first index moved, c target column.
// W2F/W2T: a waste size after drawing (card talon[a-1]), d draws needed, c target.
// F2T: a suit, c target. C2F/C2T: a card code, c target. T2F/T2C: a column.
struct SMove {
    u8 type = 0, a = 0, b = 0, c = 0, d = 0;
};

// Replay helper: executes one action on the Game and records it.
struct Replay {
    Game game;
    std::vector<SolverStep>* out = nullptr;
    int actions = 0;
    bool move(Move m) {
        if (!game.move(m))
            return false;
        if (out)
            (*out).push_back({false, m});
        ++actions;
        return true;
    }
    bool draw() {
        if (!game.draw())
            return false;
        if (out)
            (*out).push_back({true, {}});
        ++actions;
        return true;
    }
    int foundation_for(int suit, int rank) const {
        for (int i = 10; i < 14; ++i)
            if (!game.state.piles[i].empty() && game.state.piles[i].back().suit == suit)
                return i;
        if (rank == 1)
            for (int i = 10; i < 14; ++i)
                if (game.state.piles[i].empty())
                    return i;
        return -1;
    }
    int top_index(int pile) const {
        return static_cast<int>(game.state.piles[pile].size()) - 1;
    }
};

// ---------------------------------------------------------------- Klondike

struct KState {
    Columns<52, 7> t;
    u8 found[4]{};
    u8 tn = 0, tp = 0; // talon size; waste size (talon[0..tp) is the waste, top tp-1)
    u8 talon[24]{};    // in draw order: waste bottom..top, then stock top..bottom
};

struct Klondike {
    using S = KState;
    int draw = 1;
    int profile = 0;

    static bool won(const S& s) {
        return s.found[0] + s.found[1] + s.found[2] + s.found[3] == 52;
    }
    static bool fits_found(const S& s, u8 c) {
        return s.found[suit_of(c)] == rank_of(c) - 1;
    }
    static bool fits_on(u8 below, u8 c) {
        return rank_of(below) == rank_of(c) + 1 && red_of(below) != red_of(c);
    }
    static bool safe(const S& s, u8 c) {
        if (!fits_found(s, c))
            return false;
        int r = rank_of(c);
        if (r <= 2)
            return true;
        int su = suit_of(c);
        bool red = red_of(c);
        int o1 = red ? 0 : 1, o2 = red ? 3 : 2, same = su == 0 ? 3 : su == 3 ? 0 : su == 1 ? 2 : 1;
        return s.found[o1] >= r - 1 && s.found[o2] >= r - 1 && s.found[same] >= r - 2;
    }
    // Waste sizes reachable by drawing (with recycling), and the draws needed.
    int positions(const S& s, u8* q, u8* dr) const {
        int n = 0, N = s.tn, p = s.tp;
        if (N == 0)
            return 0;
        std::uint32_t seen = 0;
        struct Add {
            std::uint32_t& seen;
            u8* q;
            u8* dr;
            int& n;
            void operator()(int pos, int d) const {
                if (pos >= 1 && !((seen >> pos) & 1u)) {
                    seen |= 1u << pos;
                    q[n] = static_cast<u8>(pos);
                    dr[n] = static_cast<u8>(d);
                    ++n;
                }
            }
        };
        const Add add{seen, q, dr, n};
        add(p, 0);
        int pos = p, d = 0;
        while (pos < N) {
            pos = std::min(pos + draw, N);
            add(pos, ++d);
        }
        if (p > 0) {
            ++d; // recycle the waste
            pos = 0;
            while (pos < N) {
                pos = std::min(pos + draw, N);
                add(pos, ++d);
            }
        }
        return n;
    }
    void gen(const S& s, std::vector<SMove>& out) const {
        int st[8];
        st[0] = 0;
        for (int c = 0; c < 7; ++c)
            st[c + 1] = st[c] + s.t.len[c];
        int first_empty = -1;
        for (int c = 0; c < 7; ++c)
            if (!s.t.len[c]) {
                first_empty = c;
                break;
            }
        for (int a = 0; a < 7; ++a) {
            int L = s.t.len[a];
            if (!L)
                continue;
            const u8* col = s.t.cards + st[a];
            if (fits_found(s, col[L - 1]))
                out.push_back({T2F, u8(a), 0, 0, 0});
            for (int i = s.t.hid[a]; i < L; ++i) {
                u8 c = col[i];
                if (rank_of(c) == 13) {
                    if (first_empty >= 0 && i > 0)
                        out.push_back({T2T, u8(a), u8(i), u8(first_empty), 0});
                    continue;
                }
                for (int b = 0; b < 7; ++b)
                    if (b != a && s.t.len[b] && fits_on(s.t.cards[st[b + 1] - 1], c))
                        out.push_back({T2T, u8(a), u8(i), u8(b), 0});
            }
        }
        u8 q[26], dr[26];
        int n = positions(s, q, dr);
        for (int k = 0; k < n; ++k) {
            u8 c = s.talon[q[k] - 1];
            if (fits_found(s, c))
                out.push_back({W2F, q[k], 0, 0, dr[k]});
            if (rank_of(c) == 13) {
                if (first_empty >= 0)
                    out.push_back({W2T, q[k], 0, u8(first_empty), dr[k]});
                continue;
            }
            for (int b = 0; b < 7; ++b)
                if (s.t.len[b] && fits_on(s.t.cards[st[b + 1] - 1], c))
                    out.push_back({W2T, q[k], 0, u8(b), dr[k]});
        }
        for (int su = 0; su < 4; ++su) {
            int f = s.found[su];
            if (f < 3 || f == 13)
                continue;
            u8 c = static_cast<u8>(su * 13 + f - 1);
            for (int b = 0; b < 7; ++b)
                if (s.t.len[b] && fits_on(s.t.cards[st[b + 1] - 1], c))
                    out.push_back({F2T, u8(su), 0, u8(b), 0});
        }
    }
    void apply_raw(S& s, const SMove& m) const {
        switch (m.type) {
        case T2F: {
            u8 c = s.t.pop(m.a);
            ++s.found[suit_of(c)];
            s.t.reveal(m.a);
            break;
        }
        case T2T:
            s.t.move_tail(m.a, s.t.len[m.a] - m.b, m.c);
            s.t.reveal(m.a);
            break;
        case W2F:
        case W2T: {
            int q = m.a;
            u8 c = s.talon[q - 1];
            std::memmove(s.talon + q - 1, s.talon + q, s.tn - q);
            --s.tn;
            s.tp = static_cast<u8>(q - 1);
            if (m.type == W2F)
                ++s.found[suit_of(c)];
            else
                s.t.push(m.c, c);
            break;
        }
        case F2T: {
            u8 c = static_cast<u8>(m.a * 13 + s.found[m.a] - 1);
            --s.found[m.a];
            s.t.push(m.c, c);
            break;
        }
        default:
            break;
        }
    }
    void autos(S& s, std::vector<SMove>* log) const {
        for (bool any = true; any;) {
            any = false;
            for (int a = 0; a < 7; ++a)
                if (s.t.len[a] && safe(s, s.t.top(a))) {
                    apply_raw(s, {T2F, u8(a), 0, 0, 0});
                    if (log)
                        (*log).push_back({T2F, u8(a), 0, 0, 0});
                    any = true;
                }
        }
    }
    void apply(S& s, const SMove& m, std::vector<SMove>* log) const {
        apply_raw(s, m);
        autos(s, log);
    }
    u64 hash(const S& s) const {
        u64 h = s.t.hash();
        h += mix64(fnv(s.found, 4, 0x9e3779b97f4a7c15ULL));
        // With draw 1 every waste size is reachable from every other, so the
        // position does not distinguish states.
        u64 seed = 0x51ed2701ULL + s.tn * 977u + (draw == 1 ? 0 : s.tp * 131u);
        h += mix64(fnv(s.talon, s.tn, seed));
        return mix64(h);
    }
    int h(const S& s) const {
        int hidden = 0;
        for (int c = 0; c < 7; ++c)
            hidden += s.t.hid[c];
        int f = s.found[0] + s.found[1] + s.found[2] + s.found[3];
        return 2 * (52 - f) + 6 * hidden + s.tn;
    }
    static S from_state(const State& st) {
        S s;
        for (int c = 0; c < 7; ++c)
            for (const Card& card : st.piles[c]) {
                s.t.push(c, code_of(card));
                if (!card.up)
                    ++s.t.hid[c];
            }
        for (int i = 10; i < 14; ++i)
            if (!st.piles[i].empty())
                s.found[st.piles[i].back().suit] = static_cast<u8>(st.piles[i].size());
        for (const Card& card : st.piles[15])
            s.talon[s.tn++] = code_of(card);
        s.tp = s.tn;
        for (Pile::const_reverse_iterator it = st.piles[14].rbegin(); it != st.piles[14].rend(); ++it)
            s.talon[s.tn++] = code_of(*it);
        return s;
    }
    bool emit(Replay& r, const S& s, const SMove& m) const {
        const std::array<Pile, 20>& piles = r.game.state.piles;
        switch (m.type) {
        case T2F: {
            Card c = piles[m.a].back();
            return r.move({m.a, r.top_index(m.a), r.foundation_for(c.suit, c.rank)});
        }
        case T2T:
            return r.move({m.a, m.b, m.c});
        case W2F:
        case W2T: {
            for (int i = 0; i < m.d; ++i)
                if (!r.draw())
                    return false;
            if (piles[15].empty() || code_of(piles[15].back()) != s.talon[m.a - 1])
                return false;
            Card c = piles[15].back();
            int to = m.type == W2F ? r.foundation_for(c.suit, c.rank) : m.c;
            return r.move({15, r.top_index(15), to});
        }
        case F2T: {
            int from = r.foundation_for(m.a, 0);
            return from >= 0 && r.move({from, r.top_index(from), m.c});
        }
        default:
            return false;
        }
    }

    // Casual player priorities (face-up information only). 0 = not considered.
    int casual(const S& s, const SMove& m) const {
        switch (m.type) {
        case T2F: {
            int L = s.t.len[m.a];
            return L - 1 == s.t.hid[m.a] && s.t.hid[m.a] > 0 ? 1000 : 800;
        }
        case T2T: {
            int hid = s.t.hid[m.a];
            if (m.b == hid && hid > 0)
                return 900 + hid;
            if (m.b == 0) {
                // Emptying a column is worth it when a king is waiting somewhere visible.
                for (int c = 0; c < 7; ++c)
                    for (int i = std::max<int>(1, s.t.hid[c]); i < s.t.len[c]; ++i)
                        if (rank_of(s.t.at(c, i)) == 13)
                            return 300;
                for (int i = 0; i < s.tn; ++i)
                    if (rank_of(s.talon[i]) == 13)
                        return 300;
                return 0;
            }
            if (m.b > hid && fits_found(s, s.t.at(m.a, m.b - 1)))
                return 400;
            return 0;
        }
        case W2F:
            return 700 - m.d;
        case W2T:
            return (rank_of(s.talon[m.a - 1]) == 13 ? 500 : 600) - m.d;
        default:
            return 0;
        }
    }
};

// ---------------------------------------------------------------- FreeCell

struct FState {
    Columns<52, 8> t;
    u8 found[4]{};
    u8 cell[4]{0xFF, 0xFF, 0xFF, 0xFF}; // sorted, empty (0xFF) last
};

struct FreeCellE {
    using S = FState;
    int profile = 0;
    static bool won(const S& s) {
        return s.found[0] + s.found[1] + s.found[2] + s.found[3] == 52;
    }
    static bool fits_found(const S& s, u8 c) {
        return s.found[suit_of(c)] == rank_of(c) - 1;
    }
    static bool fits_on(u8 below, u8 c) {
        return rank_of(below) == rank_of(c) + 1 && red_of(below) != red_of(c);
    }
    static bool safe(const S& s, u8 c) {
        if (!fits_found(s, c))
            return false;
        int r = rank_of(c);
        if (r <= 2)
            return true;
        bool red = red_of(c);
        return s.found[red ? 0 : 1] >= r - 1 && s.found[red ? 3 : 2] >= r - 1;
    }
    static void sort_cells(S& s) {
        std::sort(s.cell, s.cell + 4);
    }
    void gen(const S& s, std::vector<SMove>& out) const {
        int st[9];
        st[0] = 0;
        for (int c = 0; c < 8; ++c)
            st[c + 1] = st[c] + s.t.len[c];
        int free_cells = 0, empties = 0, first_empty = -1;
        for (int i = 0; i < 4; ++i)
            free_cells += s.cell[i] == 0xFF;
        for (int c = 0; c < 8; ++c)
            if (!s.t.len[c]) {
                ++empties;
                if (first_empty < 0)
                    first_empty = c;
            }
        for (int i = 0; i < 4; ++i) {
            u8 c = s.cell[i];
            if (c == 0xFF)
                break;
            if (fits_found(s, c))
                out.push_back({C2F, c, 0, 0, 0});
            for (int b = 0; b < 8; ++b)
                if (s.t.len[b] && fits_on(s.t.cards[st[b + 1] - 1], c))
                    out.push_back({C2T, c, 0, u8(b), 0});
            if (first_empty >= 0)
                out.push_back({C2T, c, 0, u8(first_empty), 0});
        }
        for (int a = 0; a < 8; ++a) {
            int L = s.t.len[a];
            if (!L)
                continue;
            const u8* col = s.t.cards + st[a];
            u8 top = col[L - 1];
            if (fits_found(s, top))
                out.push_back({T2F, u8(a), 0, 0, 0});
            int run = 1;
            while (run < L && fits_on(col[L - 1 - run], col[L - run]))
                ++run;
            for (int b = 0; b < 8; ++b) {
                if (b == a)
                    continue;
                if (s.t.len[b]) {
                    u8 target = s.t.cards[st[b + 1] - 1];
                    int k = rank_of(target) - rank_of(top);
                    int capacity = (1 + free_cells) << empties;
                    if (k >= 1 && k <= run && k <= capacity && fits_on(target, col[L - k]))
                        out.push_back({T2T, u8(a), u8(L - k), u8(b), 0});
                } else if (b == first_empty) {
                    int capacity = (1 + free_cells) << (empties - 1);
                    for (int k = 1; k <= run && k <= capacity && k < L; ++k)
                        out.push_back({T2T, u8(a), u8(L - k), u8(b), 0});
                }
            }
            if (free_cells)
                out.push_back({T2C, u8(a), 0, 0, 0});
        }
    }
    static void take_cell(S& s, u8 c) {
        for (int i = 0; i < 4; ++i)
            if (s.cell[i] == c) {
                s.cell[i] = 0xFF;
                break;
            }
        sort_cells(s);
    }
    void apply_raw(S& s, const SMove& m) const {
        switch (m.type) {
        case T2F:
            ++s.found[suit_of(s.t.pop(m.a))];
            break;
        case C2F:
            take_cell(s, m.a);
            ++s.found[suit_of(m.a)];
            break;
        case C2T:
            take_cell(s, m.a);
            s.t.push(m.c, m.a);
            break;
        case T2C:
            s.cell[3] = s.t.pop(m.a); // the last cell is free when any is
            sort_cells(s);
            break;
        case T2T:
            s.t.move_tail(m.a, s.t.len[m.a] - m.b, m.c);
            break;
        default:
            break;
        }
    }
    void autos(S& s, std::vector<SMove>* log) const {
        for (bool any = true; any;) {
            any = false;
            for (int a = 0; a < 8; ++a)
                if (s.t.len[a] && safe(s, s.t.top(a))) {
                    SMove m{T2F, u8(a), 0, 0, 0};
                    apply_raw(s, m);
                    if (log)
                        (*log).push_back(m);
                    any = true;
                }
            for (int i = 0; i < 4; ++i)
                if (s.cell[i] != 0xFF && safe(s, s.cell[i])) {
                    SMove m{C2F, s.cell[i], 0, 0, 0};
                    apply_raw(s, m);
                    if (log)
                        (*log).push_back(m);
                    any = true;
                    break;
                }
        }
    }
    void apply(S& s, const SMove& m, std::vector<SMove>* log) const {
        apply_raw(s, m);
        autos(s, log);
    }
    u64 hash(const S& s) const {
        u64 h = s.t.hash();
        h += mix64(fnv(s.found, 4, 0x9e3779b97f4a7c15ULL));
        h += mix64(fnv(s.cell, 4, 0x1234567ULL));
        return mix64(h);
    }
    int h(const S& s) const {
        int f = s.found[0] + s.found[1] + s.found[2] + s.found[3];
        int blocking = 0, empties = 0, cells = 0;
        for (int i = 0; i < 4; ++i)
            cells += s.cell[i] != 0xFF;
        int st = 0;
        for (int c = 0; c < 8; ++c) {
            int L = s.t.len[c];
            if (!L)
                ++empties;
            int low = 99;
            for (int i = 0; i < L; ++i) {
                int r = rank_of(s.t.cards[st + i]);
                if (r > low)
                    ++blocking;
                else
                    low = r;
            }
            st += L;
        }
        return 3 * (52 - f) + 2 * blocking + cells - empties;
    }
    static S from_state(const State& st) {
        S s;
        for (int c = 0; c < 8; ++c)
            for (const Card& card : st.piles[c])
                s.t.push(c, code_of(card));
        for (int i = 10; i < 14; ++i)
            if (!st.piles[i].empty())
                s.found[st.piles[i].back().suit] = static_cast<u8>(st.piles[i].size());
        int n = 0;
        for (int i = 16; i < 20; ++i)
            if (!st.piles[i].empty())
                s.cell[n++] = code_of(st.piles[i].back());
        sort_cells(s);
        return s;
    }
    static int cell_slot(const Replay& r, u8 c) {
        for (int i = 16; i < 20; ++i)
            if (!r.game.state.piles[i].empty() && code_of(r.game.state.piles[i].back()) == c)
                return i;
        return -1;
    }
    bool emit(Replay& r, const S&, const SMove& m) const {
        const std::array<Pile, 20>& piles = r.game.state.piles;
        switch (m.type) {
        case T2F: {
            Card c = piles[m.a].back();
            return r.move({m.a, r.top_index(m.a), r.foundation_for(c.suit, c.rank)});
        }
        case C2F: {
            int from = cell_slot(r, m.a);
            return from >= 0 && r.move({from, 0, r.foundation_for(suit_of(m.a), rank_of(m.a))});
        }
        case C2T: {
            int from = cell_slot(r, m.a);
            return from >= 0 && r.move({from, 0, m.c});
        }
        case T2C:
            for (int i = 16; i < 20; ++i)
                if (piles[i].empty())
                    return r.move({m.a, r.top_index(m.a), i});
            return false;
        case T2T:
            return r.move({m.a, m.b, m.c});
        default:
            return false;
        }
    }
};

// ---------------------------------------------------------------- Spider

struct SState {
    Columns<104, 10> t;
    u8 rows = 0; // stock rows left
    u8 done = 0; // runs removed
};

struct SpiderE {
    using S = SState;
    int profile = 0;
    u8 stock[50]{}; // Game order: back is dealt first
    static bool won(const S& s) {
        return s.done == 8;
    }
    // Same-suit descending run length at the top of column a.
    static int run_length(const S& s, int a, const u8* col) {
        int L = s.t.len[a], up = L - s.t.hid[a], k = 1;
        while (k < up && suit_of(col[L - 1 - k]) == suit_of(col[L - k]) &&
               rank_of(col[L - 1 - k]) == rank_of(col[L - k]) + 1)
            ++k;
        return k;
    }
    void gen(const S& s, std::vector<SMove>& out) const {
        int st[11];
        st[0] = 0;
        for (int c = 0; c < 10; ++c)
            st[c + 1] = st[c] + s.t.len[c];
        int first_empty = -1, empties = 0, fillers = 0;
        for (int c = 0; c < 10; ++c) {
            if (!s.t.len[c]) {
                if (first_empty < 0)
                    first_empty = c;
                ++empties;
            } else if (run_length(s, c, s.t.cards + st[c]) < s.t.len[c])
                ++fillers;
        }
        // Splitting a run into an empty column is only allowed when the empty
        // columns could not otherwise all be filled for the next deal.
        bool split_to_empty = s.rows && fillers < empties;
        for (int a = 0; a < 10; ++a) {
            int L = s.t.len[a];
            if (!L)
                continue;
            const u8* col = s.t.cards + st[a];
            int m = run_length(s, a, col);
            for (int k = 1; k <= m; ++k) {
                u8 c = col[L - k];
                for (int b = 0; b < 10; ++b) {
                    if (b == a || !s.t.len[b])
                        continue;
                    u8 target = s.t.cards[st[b + 1] - 1];
                    if (rank_of(target) != rank_of(c) + 1)
                        continue;
                    if (k < m) {
                        // Split a run only onto its own suit, and only to build a longer run.
                        if (suit_of(target) != suit_of(c))
                            continue;
                        if (run_length(s, b, s.t.cards + st[b]) + k <= m)
                            continue;
                    }
                    out.push_back({T2T, u8(a), u8(L - k), u8(b), 0});
                }
                if (first_empty >= 0 && k < L && (k == m || split_to_empty))
                    out.push_back({T2T, u8(a), u8(L - k), u8(first_empty), 0});
            }
        }
        if (s.rows && first_empty < 0)
            out.push_back({DEAL, 0, 0, 0, 0});
    }
    static void complete(S& s, int c) {
        int L = s.t.len[c];
        if (L - s.t.hid[c] < 13)
            return;
        int st = s.t.start(c);
        u8 su = suit_of(s.t.cards[st + L - 1]);
        for (int j = 0; j < 13; ++j) {
            u8 v = s.t.cards[st + L - 13 + j];
            if (rank_of(v) != 13 - j || suit_of(v) != su)
                return;
        }
        s.t.drop(c, 13);
        ++s.done;
        s.t.reveal(c);
    }
    void apply_raw(S& s, const SMove& m) const {
        if (m.type == T2T) {
            s.t.move_tail(m.a, s.t.len[m.a] - m.b, m.c);
            s.t.reveal(m.a);
            // Game::settle checks every column: a dealt card lifted off a finished
            // K..A run also completes the source.
            complete(s, m.a);
            complete(s, m.c);
        } else if (m.type == DEAL) {
            int R = s.rows;
            for (int i = 0; i < 10; ++i)
                s.t.push(i, stock[10 * R - 1 - i]);
            --s.rows;
            for (int i = 0; i < 10; ++i)
                complete(s, i);
        }
    }
    void autos(S&, std::vector<SMove>*) const {}
    void apply(S& s, const SMove& m, std::vector<SMove>*) const {
        apply_raw(s, m);
    }
    u64 hash(const S& s) const {
        return mix64(s.t.hash() + mix64(0x777ULL + s.rows * 31u + s.done * 1009u));
    }
    int h(const S& s) const {
        int hidden = 0, offsuit = 0, breaks = 0, empties = 0, st = 0;
        for (int c = 0; c < 10; ++c) {
            int L = s.t.len[c], hd = s.t.hid[c];
            hidden += hd;
            if (!L)
                ++empties;
            for (int j = st + hd + 1; j < st + L; ++j) {
                u8 lo = s.t.cards[j - 1], up = s.t.cards[j];
                if (rank_of(lo) == rank_of(up) + 1)
                    offsuit += suit_of(lo) == suit_of(up) ? 0 : 1;
                else
                    breaks += 1;
            }
            st += L;
        }
        // Two weightings; the beam ladder alternates them, which makes the search
        // far less sensitive to one heuristic's blind spots.
        const int w_hidden = profile ? 30 : 20, w_rows = profile ? 40 : 55;
        return w_hidden * hidden + 5 * offsuit + 6 * breaks + w_rows * s.rows + 60 * (8 - s.done) -
               30 * empties;
    }
    S from_state(const State& st) {
        S s;
        for (int c = 0; c < 10; ++c)
            for (const Card& card : st.piles[c]) {
                s.t.push(c, code_of(card));
                if (!card.up)
                    ++s.t.hid[c];
            }
        const Pile& stock_pile = st.piles[14];
        for (std::size_t i = 0; i < stock_pile.size() && i < 50; ++i)
            stock[i] = code_of(stock_pile[i]);
        s.rows = static_cast<u8>(stock_pile.size() / 10);
        s.done = static_cast<u8>(st.completed);
        return s;
    }
    bool emit(Replay& r, const S&, const SMove& m) const {
        if (m.type == DEAL)
            return r.draw();
        return m.type == T2T && r.move({m.a, m.b, m.c});
    }

    int casual(const S& s, const SMove& m) const {
        if (m.type == DEAL)
            return 10;
        int L = s.t.len[m.a], hd = s.t.hid[m.a], k = L - m.b;
        int st = s.t.start(m.a);
        const u8* col = s.t.cards + st;
        u8 c = col[m.b];
        int run = run_length(s, m.a, col);
        bool reveals = m.b == hd && hd > 0;
        bool empties = m.b == 0;
        {
            S after = s;
            apply_raw(after, m);
            if (after.done > s.done)
                return 1000;
        }
        if (!s.t.len[m.c]) {
            if (reveals)
                return 150;
            if (k == run && m.b > hd && rank_of(col[m.b - 1]) != rank_of(c) + 1)
                return 20;         // lift a run off a card it does not belong on
            return s.rows ? 5 : 0; // empty columns must be filled before dealing
        }
        if (k < run)
            return 300; // generated only when it joins into a longer same-suit run
        u8 target = s.t.top(m.c);
        if (m.b > hd && rank_of(col[m.b - 1]) == rank_of(c) + 1) {
            // Already sits on a natural (off-suit) card: only move onto its own suit.
            if (suit_of(target) != suit_of(c) || suit_of(col[m.b - 1]) == suit_of(c))
                return 0;
        }
        int score = suit_of(target) == suit_of(c) ? 500 : 200;
        if (reveals)
            score += 100;
        if (empties)
            score += 50;
        return score;
    }
};

// ---------------------------------------------------------------- search

struct SearchOut {
    bool solved = false, exhausted = false;
    long nodes = 0;
    std::vector<SMove> path; // includes automatic moves
};

template <class E>
SearchOut beam_search(const E& e, typename E::S root, std::size_t width, long budget) {
    using S = typename E::S;
    SearchOut out;
    std::vector<SMove> rootlog;
    e.autos(root, &rootlog);
    if (e.won(root)) {
        out.solved = true;
        out.path = rootlog;
        return out;
    }
    struct Rec {
        std::int32_t parent;
        SMove m;
    };
    struct Cand {
        int h;
        std::uint32_t order;
        std::int32_t parent;
        SMove m;
        u64 hash;
        S s;
    };
    std::vector<Rec> recs{{-1, {}}};
    std::vector<std::pair<S, std::int32_t>> level{{root, 0}}, next;
    std::vector<Cand> cands;
    SeenSet seen;
    seen.insert(e.hash(root));
    std::vector<SMove> moves;
    bool truncated = false;
    std::int32_t found = -1;
    SMove last{};
    while (found < 0) {
        cands.clear();
        SeenSet local;
        std::uint32_t order = 0;
        for (const std::pair<S, std::int32_t>& item : level) {
            const S& s = item.first;
            const std::int32_t ri = item.second;
            if (out.nodes >= budget)
                return out;
            ++out.nodes;
            moves.clear();
            e.gen(s, moves);
            for (const SMove& m : moves) {
                S c = s;
                e.apply(c, m, nullptr);
                u64 hc = e.hash(c);
                if (seen.contains(hc) || !local.insert(hc))
                    continue;
                if (e.won(c)) {
                    found = ri;
                    last = m;
                    break;
                }
                cands.push_back({e.h(c), order++, ri, m, hc, c});
            }
            if (found >= 0)
                break;
        }
        if (found >= 0)
            break;
        if (cands.empty()) {
            out.exhausted = !truncated;
            return out;
        }
        struct Better {
            bool operator()(const Cand& x, const Cand& y) const {
                return x.h != y.h ? x.h < y.h : x.order < y.order;
            }
        };
        const Better better{};
        if (cands.size() > width) {
            std::nth_element(cands.begin(), cands.begin() + static_cast<std::ptrdiff_t>(width),
                             cands.end(), better);
            cands.resize(width);
            truncated = true;
        }
        // Fixed order (nth_element leaves it library-specific) keeps grading identical
        // on every platform.
        std::sort(cands.begin(), cands.end(), better);
        next.clear();
        for (const Cand& c : cands) {
            seen.insert(c.hash);
            recs.push_back({c.parent, c.m});
            next.emplace_back(c.s, static_cast<std::int32_t>(recs.size() - 1));
        }
        level.swap(next);
    }
    std::vector<SMove> chain{last};
    for (std::int32_t i = found; recs[i].parent >= 0; i = recs[i].parent)
        chain.push_back(recs[i].m);
    std::reverse(chain.begin(), chain.end());
    out.path = rootlog;
    S s = root;
    for (const SMove& m : chain) {
        out.path.push_back(m);
        e.apply(s, m, &out.path);
    }
    out.solved = e.won(s);
    return out;
}

// Replays a solver line through Game; true when the game ends won.
template <class E>
bool replay(const E& e, typename E::S s, const State& start, const std::vector<SMove>& path,
            std::vector<SolverStep>* steps, int& actions) {
    Replay r;
    r.game.state = start;
    r.out = steps;
    for (const SMove& m : path) {
        if (!e.emit(r, s, m))
            return false;
        e.apply_raw(s, m);
    }
    actions = r.actions;
    return r.game.state.over;
}

template <class E>
SolveReport solve_with(const E& e, const typename E::S& root, const State& start, long budget,
                       std::vector<SolverStep>* solution) {
    SolveReport report;
    // Beam ladder: widths 10, 20, 40, ... until the expansion budget is spent. The
    // cumulative expansions needed are the difficulty measure.
    SearchOut out;
    long used = 0;
    int rung = 0;
    std::size_t width = 10;
    for (; used < budget; width *= 2, ++rung) {
        E engine = e;
        engine.profile = rung % 2;
        out = beam_search(engine, root, width, budget - used);
        used += out.nodes;
        if (out.solved || out.exhausted)
            break;
    }
    out.nodes = used;
    report.nodes = out.nodes;
    report.exhausted = out.exhausted;
    if (out.solved) {
        std::vector<SolverStep> steps;
        int actions = 0;
        if (replay(e, root, start, out.path, &steps, actions)) {
            report.solved = true;
            report.moves = actions;
            report.width = static_cast<int>(width);
            if (solution)
                *solution = std::move(steps);
        }
    }
    return report;
}

// Casual player: picks the highest-priority move whose result is new, never undoes.
template <class E, class Score>
bool casual_play(const E& e, typename E::S s, Score score, std::vector<SMove>& path) {
    using S = typename E::S;
    e.autos(s, &path);
    SeenSet visited;
    std::vector<SMove> moves;
    for (int step = 0; step < 4000; ++step) {
        if (e.won(s))
            return true;
        visited.insert(e.hash(s));
        moves.clear();
        e.gen(s, moves);
        int best = 0;
        SMove choice{};
        for (const SMove& m : moves) {
            int v = score(s, m);
            if (v <= best)
                continue;
            S c = s;
            e.apply(c, m, nullptr);
            if (visited.contains(e.hash(c)))
                continue;
            best = v;
            choice = m;
        }
        if (best <= 0)
            return false;
        path.push_back(choice);
        e.apply(s, choice, &path);
    }
    return false;
}

Game dealt(Kind kind, int option, std::uint32_t seed) {
    Game game;
    game.deal(kind, seed, normalized_option(kind, option));
    return game;
}

} // namespace

int normalized_option(Kind kind, int option) {
    switch (kind) {
    case Kind::solitaire:
        return option == 3 ? 3 : 1;
    case Kind::spider:
        return option == 4 ? 4 : option == 2 ? 2 : 1;
    default:
        return 0;
    }
}

SolveReport solve_state(const State& state, long node_budget, std::vector<SolverStep>* solution) {
    switch (state.kind) {
    case Kind::solitaire: {
        Klondike e;
        e.draw = state.draw_count == 3 ? 3 : 1;
        return solve_with(e, Klondike::from_state(state), state, node_budget, solution);
    }
    case Kind::freecell: {
        FreeCellE e;
        return solve_with(e, FreeCellE::from_state(state), state, node_budget, solution);
    }
    case Kind::spider: {
        SpiderE e;
        SState root = e.from_state(state);
        return solve_with(e, root, state, node_budget, solution);
    }
    default:
        return {};
    }
}

SolveReport solve_deal(Kind kind, int option, std::uint32_t seed, long node_budget,
                       std::vector<SolverStep>* solution) {
    Game game = dealt(kind, option, seed);
    return solve_state(game.state, node_budget, solution);
}

// Scores a casual player's candidate move with the engine's own priorities.
template <class Engine, class Position> struct CasualScore {
    Engine& e;
    int operator()(const Position& s, const SMove& m) const {
        return e.casual(s, m);
    }
};
// FreeCell's casual player looks one move ahead.
struct LookAhead {
    FreeCellE& e;
    int operator()(const FState& s, const SMove& m) const {
        FState c = s;
        e.apply(c, m, nullptr);
        return 100000 - e.h(c);
    }
};
bool greedy_wins(Kind kind, int option, std::uint32_t seed, std::vector<SolverStep>* solution) {
    Game game = dealt(kind, option, seed);
    const State& start = game.state;
    std::vector<SMove> path;
    int actions = 0;
    switch (kind) {
    case Kind::solitaire: {
        Klondike e;
        e.draw = start.draw_count == 3 ? 3 : 1;
        KState root = Klondike::from_state(start);
        const CasualScore<Klondike, KState> score{e};
        bool won = casual_play(e, root, score, path);
        return replay(e, root, start, path, solution, actions) && won;
    }
    case Kind::spider: {
        SpiderE e;
        SState root = e.from_state(start);
        const CasualScore<SpiderE, SState> score{e};
        bool won = casual_play(e, root, score, path);
        return replay(e, root, start, path, solution, actions) && won;
    }
    case Kind::freecell: {
        // Everything is face up, so the casual player compares positions one move ahead.
        FreeCellE e;
        FState root = FreeCellE::from_state(start);
        const LookAhead score{e};
        bool won = casual_play(e, root, score, path);
        return replay(e, root, start, path, solution, actions) && won;
    }
    default:
        return false;
    }
}

GradingLimits grading_limits(Kind kind, int option) {
    option = normalized_option(kind, option);
    switch (kind) {
    case Kind::solitaire:
        // Easy only through the casual player; hard needs a beam of 40 or more.
        return {200000, 0, 20};
    case Kind::spider:
        if (option == 1)
            return {200000, 0, 10};
        if (option == 2)
            return {200000, 10, 20};
        return {300000, 20, 80};
    case Kind::freecell:
        return {200000, 10, 40};
    default:
        return {0, 0, 0};
    }
}

DealGrade grade_deal(Kind kind, int option, std::uint32_t seed) {
    DealGrade grade;
    option = normalized_option(kind, option);
    GradingLimits limits = grading_limits(kind, option);
    grade.greedy = greedy_wins(kind, option, seed);
    if (grade.greedy) {
        grade.winnable = true;
        grade.difficulty = Difficulty::easy;
        return grade;
    }
    grade.report = solve_deal(kind, option, seed, limits.node_budget);
    if (!grade.report.solved)
        return grade;
    grade.winnable = true;
    int width = grade.report.width;
    grade.difficulty = width <= limits.easy_width     ? Difficulty::easy
                       : width <= limits.medium_width ? Difficulty::medium
                                                      : Difficulty::hard;
    return grade;
}

const char* difficulty_name(Difficulty difficulty) {
    switch (difficulty) {
    case Difficulty::easy:
        return "Easy";
    case Difficulty::medium:
        return "Medium";
    default:
        return "Hard";
    }
}

namespace {
const detail::DealTable* find_table(Kind kind, int option, Difficulty difficulty) {
    option = normalized_option(kind, option);
    for (int i = 0; i < detail::deal_table_count; ++i) {
        const detail::DealTable& t = detail::deal_tables[i];
        if (t.kind == kind && t.option == option && t.difficulty == difficulty)
            return &t;
    }
    return nullptr;
}
} // namespace

int graded_count(Kind kind, int option, Difficulty difficulty) {
    const detail::DealTable* t = find_table(kind, option, difficulty);
    return t ? (*t).count : 0;
}

std::uint32_t graded_seed(Kind kind, int option, Difficulty difficulty, std::uint32_t pick) {
    const detail::DealTable* t = find_table(kind, option, difficulty);
    if (!t || (*t).count <= 0)
        return pick + 1;
    return (*t).seeds[pick % static_cast<std::uint32_t>((*t).count)];
}

} // namespace games
