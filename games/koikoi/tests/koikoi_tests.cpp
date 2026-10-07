// Koi-Koi rules: the deck, the sets, the scoring, whole matches between computer players, saving.
#include "koikoi.hpp"

#include <chrono>
#include <cstdio>
#include <set>

using namespace games::koi;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static bool named(const std::vector<Yaku>& ys, const char* n, int pts = -1) {
    for (const Yaku& y : ys) if (y.name == n && (pts < 0 || y.points == pts)) return true;
    return false;
}

// play one whole match; `random_player` makes seat 0 play its first legal card and always stop
struct Tally { int won[2] = {}; int koi[2] = {}; int sets[2] = {}; int rounds = 0; int draws = 0; };
static void play_match(std::uint32_t seed, int opp, bool random_player, Tally& t) {
    KoiState s;
    new_match(s, seed, opp);
    for (int guard = 0; guard < 2000; ++guard) {
        CHECK(invariant(s));
        if (s.phase == Phase::match_over) break;
        if (s.phase == Phase::round_over) {
            ++t.rounds;
            if (s.round_winner < 0) ++t.draws; else ++t.sets[s.round_winner];
            next_round(s);
            continue;
        }
        const bool me = s.turn == 0;
        if (s.phase == Phase::play) CHECK(play(s, me && random_player ? 0 : ai_play(s)));
        else if (s.phase == Phase::draw) CHECK(draw(s));
        else if (s.phase == Phase::choose_hand || s.phase == Phase::choose_draw) CHECK(choose(s, me && random_player ? s.choices[0] : ai_choose(s)));
        else if (s.phase == Phase::decide) {
            const bool k = me && random_player ? false : ai_koikoi(s);
            t.koi[s.turn] += k;
            CHECK(decide(s, k));
        }
    }
    CHECK(s.phase == Phase::match_over);
    CHECK(s.rounds_won_points.size() == 12);
    t.won[s.totals[0] > s.totals[1] ? 0 : 1] += s.totals[0] != s.totals[1];
}

int main() {
    const auto t0 = std::chrono::steady_clock::now();
    // the deck: 48 cards, 4 per month; 5 brights, 9 animals, 10 ribbons, 24 plains
    {
        int counts[4] = {}, month[12] = {}, poem = 0, blue = 0;
        for (int c = 0; c < 48; ++c) {
            ++counts[static_cast<int>(info(c).type)];
            ++month[info(c).month];
            CHECK(info(c).month == month_of(c));
            poem += info(c).ribbon == Ribbon::poem;
            blue += info(c).ribbon == Ribbon::blue;
        }
        CHECK(counts[0] == 5 && counts[1] == 9 && counts[2] == 10 && counts[3] == 24);
        for (int m : month) CHECK(m == 4);
        CHECK(poem == 3 && blue == 3);
        CHECK(info(card::crane).type == Type::bright && info(card::sake).type == Type::animal && info(card::lightning).type == Type::plain);
    }
    // the sets
    {
        using namespace card;
        CHECK(named(yaku({crane, curtain, moon, rain_man, phoenix}), "Five Brights", 10));
        CHECK(named(yaku({crane, curtain, moon, phoenix}), "Four Brights", 8));
        CHECK(named(yaku({crane, curtain, moon, rain_man}), "Rainy Four Brights", 7));
        CHECK(named(yaku({crane, curtain, moon}), "Three Brights", 5));
        CHECK(yaku({crane, curtain, rain_man}).empty());  // three brights with the rain man is nothing
        CHECK(named(yaku({curtain, sake}), "Viewing the Blossoms", 5));
        CHECK(named(yaku({moon, sake}), "Viewing the Moon", 5));
        CHECK(named(yaku({boar, deer, butterflies}), "Boar, Deer and Butterflies", 5));
        CHECK(named(yaku({boar, deer, butterflies, geese}), "Boar, Deer and Butterflies", 6));
        CHECK(named(yaku({boar, deer, butterflies, geese, swallow}), "Animals", 1));
        CHECK(named(yaku({1, 5, 9}), "Red Poem Ribbons", 5));
        CHECK(named(yaku({21, 33, 37}), "Blue Ribbons", 5));
        CHECK(named(yaku({1, 5, 9, 21, 33, 37}), "Red and Blue Poem Ribbons", 10));
        CHECK(named(yaku({1, 5, 9, 13, 17}), "Ribbons", 1) && named(yaku({1, 5, 9, 13, 17}), "Red Poem Ribbons", 7));
        std::vector<int> plains = {2, 3, 6, 7, 10, 11, 14, 15, 18};
        CHECK(yaku(plains).empty());
        plains.push_back(sake);  // the sake cup counts as a plain too
        CHECK(named(yaku(plains), "Plains", 1));
    }
    // the quick count agrees with the named sets, on many random piles
    {
        unsigned r = 99;
        for (int k = 0; k < 20000; ++k) {
            std::vector<int> pile;
            for (int c = 0; c < 48; ++c) { r = r * 1664525u + 1013904223u; if ((r >> 24) < 90) pile.push_back(c); }
            int named_total = 0;
            for (const Yaku& y : yaku(pile)) named_total += y.points;
            CHECK(named_total == yaku_points(pile));
        }
    }
    // scoring: doubled at seven or more, and doubled again if the other player called koi-koi
    {
        KoiState s;
        s.captured[0] = {card::crane, card::curtain, card::moon};  // 5
        CHECK(round_value(s, 0) == 5);
        s.koi[1] = 1;
        CHECK(round_value(s, 0) == 10);
        s.koi[1] = 0;
        s.captured[0] = {card::crane, card::curtain, card::moon, card::phoenix};  // 8 -> 16
        CHECK(round_value(s, 0) == 16);
    }
    // the deal: 8, 8, 8 and 24, never four of a month on the field
    for (std::uint32_t seed = 1; seed <= 300; ++seed) {
        KoiState s;
        new_match(s, seed, 0);
        CHECK(invariant(s));
        if (s.phase == Phase::round_over) continue;  // an instant win
        CHECK(s.hand[0].size() == 8 && s.hand[1].size() == 8 && s.field.size() == 8 && s.deck.size() == 24);
        int fm[12] = {};
        for (int c : s.field) ++fm[month_of(c)];
        for (int v : fm) CHECK(v < 4);
    }
    // matching: a card with one match takes it; with two, it waits for a choice
    {
        KoiState s;
        new_match(s, 5, 0);
        while (s.phase == Phase::round_over) { s.seed++; new_match(s, s.seed, 0); }
        s.turn = 0;
        s.hand[0] = {card::crane};
        s.field = {2, 30};
        s.deck = {};
        // rebuild a consistent state for the test: put every other card in the opponent's captures
        s.captured = {};
        s.hand[1].clear();
        for (int c = 0; c < 48; ++c) if (c != card::crane && c != 2 && c != 30) s.captured[1].push_back(c);
        CHECK(invariant(s));
        CHECK(play(s, 0));
        CHECK(s.captured[0].size() == 2 && s.field.size() == 1 && s.phase == Phase::draw);
        KoiState u = s;
        u.phase = Phase::play;
        u.turn = 0;
        u.captured[0].clear();
        u.captured[1].clear();
        u.hand[0] = {card::crane};
        u.field = {2, 3, 30};
        u.hand[1].clear();
        for (int c = 0; c < 48; ++c) if (c != card::crane && c != 2 && c != 3 && c != 30) u.captured[1].push_back(c);
        CHECK(invariant(u));
        CHECK(play(u, 0));
        CHECK(u.phase == Phase::choose_hand && u.choices.size() == 2 && u.pending == card::crane);
        CHECK(!choose(u, 30));
        CHECK(choose(u, 3));
        CHECK(u.captured[0].size() == 2 && invariant(u));
    }
    // whole matches between the computer and itself, and against a careless player
    {
        Tally t;
        for (std::uint32_t seed = 1; seed <= 200; ++seed) play_match(seed, static_cast<int>(seed % 4), false, t);
        std::printf("ai vs ai: %d rounds, %d drawn, sets made by seat 0/1: %d/%d, koi-koi calls %d/%d\n", t.rounds, t.draws, t.sets[0], t.sets[1], t.koi[0], t.koi[1]);
        CHECK(t.rounds >= 200 * 12);
        Tally c;
        for (std::uint32_t seed = 1; seed <= 200; ++seed) play_match(seed + 1000, static_cast<int>(seed % 4), true, c);
        std::printf("careless player vs the computer: matches won %d / %d\n", c.won[0], c.won[0] + c.won[1]);
        CHECK(c.won[1] > c.won[0] * 2);
        // temperaments: the captain calls koi-koi far more than grandmother
        Tally cap, gran;
        for (std::uint32_t seed = 1; seed <= 150; ++seed) { play_match(seed + 5000, 1, false, cap); play_match(seed + 5000, 0, false, gran); }
        std::printf("koi-koi calls: Captain Ishida %d, Grandmother Hana %d\n", cap.koi[1], gran.koi[1]);
        CHECK(cap.koi[1] > gran.koi[1] * 2);
    }
    // Master Kiri, who plays each choice out against guesses at the hidden cards, is the strongest at the table
    {
        int kw = 0, kl = 0;
        for (std::uint32_t seed = 1; seed <= 120; ++seed) {
            KoiState s;
            new_match(s, seed * 13 + 3, 3);
            for (int g = 0; g < 3000 && s.phase != Phase::match_over; ++g) {
                if (s.phase == Phase::round_over) { next_round(s); continue; }
                if (s.phase == Phase::play) play(s, ai_play(s));
                else if (s.phase == Phase::draw) draw(s);
                else if (s.phase == Phase::choose_hand || s.phase == Phase::choose_draw) choose(s, ai_choose(s));
                else if (s.phase == Phase::decide) decide(s, ai_koikoi(s));
            }
            kw += s.totals[1] > s.totals[0];
            kl += s.totals[1] < s.totals[0];
        }
        std::printf("Master Kiri against the plain computer: wins %d, loses %d\n", kw, kl);
        CHECK(kw > kl);
    }
    // saving: round trip mid-round, and nonsense refused
    {
        KoiState s;
        new_match(s, 77, 2);
        for (int k = 0; k < 7 && (s.phase == Phase::play || s.phase == Phase::draw); ++k) { if (s.phase == Phase::draw) draw(s); else play(s, ai_play(s)); }
        KoiState b;
        CHECK(load(save(s), b));
        CHECK(save(b) == save(s));
        CHECK(!load("koikoi=1\nhand0=1,1,1\n", b));
        CHECK(!load("nonsense", b));
    }
    std::printf("%s (%.2fs)\n", fails ? "FAILED" : "all tests passed", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    return fails ? 1 : 0;
}
