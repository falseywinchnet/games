// Rules tests for Four Pegs, including an exhaustive check of the evaluator.
#include "board.hpp"

#include <cstdio>

using namespace fp;

static int g_fail = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } \
    } while (0)

// Reference: pair pegs one at a time, exact matches first, then any match.
static Score reference(const Code& secret, const Code& guess) {
    bool used_s[kPegs] = {}, used_g[kPegs] = {};
    Score s;
    for (int i = 0; i < kPegs; ++i)
        if (secret[i] == guess[i]) { ++s.exact; used_s[i] = used_g[i] = true; }
    for (int i = 0; i < kPegs; ++i) {
        if (used_g[i]) continue;
        for (int j = 0; j < kPegs; ++j)
            if (!used_s[j] && secret[j] == guess[i]) { ++s.near; used_s[j] = true; break; }
    }
    return s;
}

static Code from_index(int n) {
    Code c{};
    for (int i = 0; i < kPegs; ++i) { c[static_cast<size_t>(i)] = n % kColors; n /= kColors; }
    return c;
}

static void exhaustive() {
    const int all = kColors * kColors * kColors * kColors;
    long pairs = 0, bad = 0;
    for (int a = 0; a < all; ++a)
        for (int b = 0; b < all; ++b) {
            const Code s = from_index(a), g = from_index(b);
            if (!(evaluate(s, g) == reference(s, g))) ++bad;
            ++pairs;
        }
    CHECK(bad == 0);
    CHECK(pairs == 1679616);
}

static void duplicates() {
    CHECK((evaluate({0, 0, 1, 1}, {1, 1, 0, 0}) == Score{0, 4}));
    CHECK((evaluate({0, 0, 0, 1}, {0, 1, 1, 1}) == Score{2, 0}));
    CHECK((evaluate({0, 1, 2, 3}, {0, 0, 0, 0}) == Score{1, 0}));
    CHECK((evaluate({5, 5, 5, 5}, {5, 5, 5, 5}) == Score{4, 0}));
    CHECK((evaluate({0, 1, 2, 3}, {4, 4, 5, 5}) == Score{0, 0}));
}

static void flow() {
    Board b(7);
    CHECK(!b.complete());
    CHECK(b.submit().exact == 0 && b.turns_used() == 0);  // nothing to judge yet
    for (int t = 0; t < kTurns; ++t) {
        const Code wrong = b.secret()[0] == 0 ? Code{1, 1, 1, 1} : Code{0, 0, 0, 0};
        for (int i = 0; i < kPegs; ++i) b.set(i, wrong[static_cast<size_t>(i)]);
        b.submit();
    }
    CHECK(b.lost() && b.over() && !b.won() && b.turns_left() == 0);
    CHECK(!b.set(0, 0));
    b.new_game();
    CHECK(!b.over() && b.turns_used() == 0);
    for (int i = 0; i < kPegs; ++i) b.set(i, b.secret()[static_cast<size_t>(i)]);
    CHECK(b.submit().exact == kPegs && b.won() && b.turns_used() == 1);
}

static void restore_roundtrip() {
    Board b(11);
    for (int i = 0; i < kPegs; ++i) b.set(i, (b.secret()[static_cast<size_t>(i)] + 1) % kColors);
    b.submit();
    b.set(2, 3);
    Board c(1);
    CHECK(c.restore(b.state()));
    CHECK(c.rows().size() == 1 && c.draft()[2] == 3 && c.secret() == b.secret());
    BoardState bad = b.state();
    bad.rows[0].score.exact = 3;  // a doctored score must not load
    CHECK(!c.restore(bad));
}

int main() {
    exhaustive();
    duplicates();
    flow();
    restore_roundtrip();
    if (g_fail) { std::printf("%d failures\n", g_fail); return 1; }
    std::printf("board tests passed\n");
    return 0;
}
