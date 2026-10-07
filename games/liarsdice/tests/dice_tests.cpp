// Liar's Dice: rules, the cast, the minds at the table, the wagers.
#include "brain.hpp"
#include "rules.hpp"
#include "save.hpp"
#include "wager.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

using namespace ld;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

// a scripted stand-in for the human: plays by the odds, bluffing at a set rate
static Decision human(const Match& m, int me, double bluff, Rng& rng) {
    Character h;
    h.bluff = bluff;
    h.nerve = .42;
    h.greed = .3;
    h.skill = .7;
    h.tell_bluff = h.tell_honest = 0;
    Reading r;
    r.bluff.assign(static_cast<size_t>(m.players), kTypicalBluff);
    Decision d = decide(m, me, h, r, rng);
    if (!d.call && rng.unit() < bluff) {
        // a deliberate bluff: the face they hold least of, at the smallest legal raise
        int best = 2, low = 99;
        for (int f = 2; f <= 6; ++f) {
            int s = 0;
            for (int v : m.dice[static_cast<size_t>(me)]) s += v == f || v == 1;
            if (s < low) { low = s; best = f; }
        }
        Bid b{m.bid.none() ? 1 + m.total_dice() / 4 : (best > m.bid.face ? m.bid.qty : m.bid.qty + 1), best};
        if (b.qty <= m.total_dice()) { d.bid = b; d.bluffing = true; }
    }
    return d;
}

// play one match; seat 0 is `human` when hbluff >= 0. Returns the winner; counts calls on each seat's bids.
struct Stats { int calls_on[4] = {}; int bids[4] = {}; int bluffs[4] = {}; int tells_bluff[4] = {}; int tells_honest[4] = {}; int honest[4] = {}; };
static int play(const std::vector<int>& seats, double hbluff, std::uint64_t seed, Stats* st, PlayerRecord* rec) {
    Rng rng(seed);
    Match m;
    const int n = static_cast<int>(seats.size()) + (hbluff >= 0 ? 1 : 0);
    m.start(n, rng.range(n), rng);
    for (int guard = 0; guard < 2000 && !m.over(); ++guard) {
        const int me = m.turn;
        Decision d;
        if (hbluff >= 0 && me == 0) d = human(m, me, hbluff, rng);
        else {
            const Character& c = cast()[static_cast<size_t>(seats[static_cast<size_t>(me - (hbluff >= 0 ? 1 : 0))])];
            Reading r;
            r.bluff.assign(static_cast<size_t>(n), kTypicalBluff);
            if (hbluff >= 0 && rec) r.bluff[0] = read_player(c, *rec);
            d = decide(m, me, c, r, rng);
            if (st && !d.call) {
                if (d.bluffing) { ++st->bluffs[me]; st->tells_bluff[me] += d.tell; }
                else { ++st->honest[me]; st->tells_honest[me] += d.tell; }
            }
        }
        if (d.call) {
            CHECK(m.can_call());
            const auto before = m.dice;
            const auto hist = m.history;
            const int total = m.total_dice();
            if (st) ++st->calls_on[m.bidder];
            const Reveal rv = m.call();
            CHECK(rv.count == [&] { int k = 0; for (auto& dd : before) for (int v : dd) k += v == rv.bid.face || v == 1; return k; }());
            CHECK(rv.loser == (rv.stood ? rv.caller : rv.bidder));
            if (rec && hbluff >= 0)
                for (const BidRec& h : hist)
                    if (h.who == 0) rec->observe(was_bluff(before[0], h.bid, total));
            if (!m.over()) m.roll(rng);
        } else {
            const Bid prev = m.bid;
            CHECK(beats(d.bid, prev));
            CHECK(m.place(d.bid));
            if (st) ++st->bids[me];
        }
    }
    CHECK(m.over());
    return m.winner();
}

int main() {
    const auto t0 = std::chrono::steady_clock::now();
    // rules
    CHECK(beats({1, 2}, {}));
    CHECK(beats({3, 5}, {3, 4}));
    CHECK(!beats({3, 4}, {3, 4}));
    CHECK(!beats({3, 3}, {3, 4}));
    CHECK(beats({4, 2}, {3, 6}));
    CHECK(!beats({3, 1}, {}));  // nobody bids on ones
    CHECK(bid_words({7, 4}) == "seven fours");
    CHECK(bid_words({1, 6}) == "one six");
    {
        Rng r(5);
        Match m;
        m.start(3, 0, r);
        m.dice = {{1, 2, 2, 5, 6}, {3, 3, 4, 4, 4}, {1, 1, 6, 6, 6}};
        CHECK(m.counted(4) == 6);   // three fours and three ones
        CHECK(m.counted(6) == 7);
        CHECK(m.place({6, 4}));
        CHECK(!m.place({6, 3}));
        CHECK(m.place({7, 4}));
        const Reveal rv = m.call();  // seat 2 calls seat 1's seven fours: only six
        CHECK(!rv.stood && rv.loser == 1 && rv.caller == 2);
        CHECK(m.count[1] == 4 && m.turn == 1);
    }
    // the cast: 32, fixed, every kind-and-tell pair unique, names unique
    {
        const auto& c = cast();
        CHECK(c.size() == 32);
        std::set<std::pair<int, int>> pairs;
        std::set<std::string> names;
        int danger[4] = {};
        for (const Character& ch : c) {
            pairs.insert({static_cast<int>(ch.species), static_cast<int>(ch.tell)});
            names.insert(ch.name);
            ++danger[ch.danger];
            CHECK(ch.tell_bluff > ch.tell_honest * 3);
        }
        CHECK(pairs.size() == 32 && names.size() == 32);
        std::printf("cast: danger 1/2/3 = %d/%d/%d\n", danger[1], danger[2], danger[3]);
        CHECK(danger[1] >= 4 && danger[3] >= 4);
    }
    // chance_true agrees with simulation
    {
        Rng r(77);
        Match m;
        m.start(3, 0, r);
        m.dice[0] = {1, 4, 4, 2, 6};
        Reading rd;
        rd.bluff.assign(3, kTypicalBluff);
        const double p = chance_true(m, 0, {7, 4}, rd, 0);
        int hit = 0, N = 200000;
        for (int k = 0; k < N; ++k) {
            int c = 3;
            for (int d = 0; d < 10; ++d) { const int v = 1 + r.range(6); c += v == 4 || v == 1; }
            hit += c >= 7;
        }
        std::printf("chance: model %.4f, simulated %.4f\n", p, hit / double(N));
        CHECK(std::fabs(p - hit / double(N)) < .01);
    }
    // a thousand matches among the crew: always legal, always finishes; skill tells
    {
        const auto& c = cast();
        int wins_by_danger[4] = {}, seats_by_danger[4] = {};
        Stats st;
        for (int g = 0; g < 1500; ++g) {
            Rng pick(g * 31 + 7);
            std::vector<int> seats;
            while (seats.size() < 3) { const int k = pick.range(32); if (std::find(seats.begin(), seats.end(), k) == seats.end()) seats.push_back(k); }
            const int w = play(seats, -1, 1000 + g, &st, nullptr);
            for (int s : seats) ++seats_by_danger[c[static_cast<size_t>(s)].danger];
            ++wins_by_danger[c[static_cast<size_t>(seats[static_cast<size_t>(w)])].danger];
        }
        std::printf("crew: win rate by danger 1/2/3 = %.3f %.3f %.3f (fair share .333)\n", wins_by_danger[1] / double(seats_by_danger[1]),
                    wins_by_danger[2] / double(seats_by_danger[2]), wins_by_danger[3] / double(seats_by_danger[3]));
        CHECK(wins_by_danger[3] / double(seats_by_danger[3]) > wins_by_danger[1] / double(seats_by_danger[1]) + .05);
        int tb = 0, b = 0, th = 0, h = 0;
        for (int s = 0; s < 3; ++s) { tb += st.tells_bluff[s]; b += st.bluffs[s]; th += st.tells_honest[s]; h += st.honest[s]; }
        std::printf("tells: shown on %.1f%% of bluffs, %.1f%% of honest bids; %.1f%% of bids are bluffs\n", 100.0 * tb / b, 100.0 * th / h, 100.0 * b / (b + h));
        CHECK(tb / double(b) > 3 * th / double(h));
    }
    // adaptation: a heavy bluffer is caught out more by quick learners than slow ones
    {
        const auto& c = cast();
        std::vector<int> quick, slow;
        for (int i = 0; i < 32; ++i) (c[static_cast<size_t>(i)].adapt > .6 ? quick : slow).push_back(i);
        std::sort(slow.begin(), slow.end(), [&](int a, int b) { return c[static_cast<size_t>(a)].adapt < c[static_cast<size_t>(b)].adapt; });
        slow.resize(std::min(slow.size(), quick.size()));
        auto run = [&](const std::vector<int>& pool, double hb) {
            PlayerRecord rec;
            int won = 0, G = 1500;
            for (int g = 0; g < G; ++g) {
                std::vector<int> seats{pool[static_cast<size_t>(g % pool.size())], pool[static_cast<size_t>((g / pool.size() + g + 1) % pool.size())]};
                if (seats[0] == seats[1]) seats[1] = pool[static_cast<size_t>((g + 3) % pool.size())];
                won += play(seats, hb, 50000 + g, nullptr, &rec) == 0;
            }
            return std::make_pair(won / double(G), rec.estimate());
        };
        const auto qb = run(quick, .35), sb = run(slow, .35), qh = run(quick, 0);
        std::printf("adapt: bluffer vs quick learners wins %.3f (they think %.2f), vs slow %.3f; honest player vs quick %.3f (they think %.2f)\n",
                    qb.first, qb.second, sb.first, qh.first, qh.second);
        CHECK(qb.second > qh.second + .1);
    }
    // wagers
    {
        std::set<std::string> pitches;
        for (int s = 0; s < 200; ++s) {
            const auto w = offer(s, 100, {});
            CHECK(w.size() == 3);
            for (const Wager& x : w) {
                CHECK(x.seats.size() >= 2 && x.seats.size() <= 3);
                CHECK(x.years_lose >= 1 && x.years_win >= 1);
                pitches.insert(x.pitch);
            }
            CHECK(w[0].years_lose < w[2].years_lose);
            std::set<int> all;
            for (const Wager& x : w) for (int k : x.seats) all.insert(k);
            CHECK(all.size() == w[0].seats.size() + w[1].seats.size() + w[2].seats.size());
        }
        const auto back = offer(3, 50, {"your shadow"});
        std::printf("wager: %zu distinct pitches; e.g. \"%s\"\n", pitches.size(), back[2].pitch.c_str());
    }
    // the ledger: round trip, game in progress and all; tampering and nonsense rejected
    {
        Ledger l;
        l.owed = 37;
        l.jar = {"your shadow", "your laugh"};
        l.trophies = {"a kraken's tooth"};
        l.won = 3; l.lost = 5; l.served = 40; l.struck = 13; l.freed = false;
        l.rec.observe(true); l.rec.observe(false);
        l.notes[7] = {4, 1, 20, 5, 2};
        l.offer_seed = 123456789;
        l.in_match = true;
        l.wager = offer(9, 37, l.jar)[1];
        l.wager.seats.resize(2);
        Rng r(4);
        l.match.start(3, 1, r);
        l.match.place({3, 4});
        l.rng = 99;
        const std::string text = serialize(l);
        Ledger b;
        CHECK(parse(text, b));
        CHECK(b.owed == 37 && b.jar == l.jar && b.trophies == l.trophies && b.won == 3 && b.lost == 5 && b.served == 40 && b.struck == 13);
        CHECK(std::fabs(b.rec.estimate() - l.rec.estimate()) < 1e-3);
        CHECK(b.notes[7].bids_seen == 20 && b.notes[7].tells_caught == 2);
        CHECK(b.in_match && b.wager.seats == l.wager.seats && b.wager.pitch == l.wager.pitch && b.wager.years_lose == l.wager.years_lose);
        CHECK(b.match.dice == l.match.dice && b.match.count == l.match.count && b.match.turn == l.match.turn && b.match.bid.qty == 3 && b.match.bid.face == 4);
        CHECK(b.match.history.size() == 1 && b.match.history[0].who == 1 && b.rng == 99);
        CHECK(serialize(b) == text);
        std::string bad = text;
        bad[text.find("owed=37") + 5] = '9';
        CHECK(!parse(bad, b));
        CHECK(!parse("nonsense", b));
        // a game whose dice don't match its counts is dropped, the ledger kept
        Ledger c = l;
        c.match.dice[1].pop_back();
        Ledger d;
        CHECK(parse(serialize(c), d) && !d.in_match && d.owed == 37);
    }
    std::printf("%s (%.2fs)\n", fails ? "FAILED" : "all tests passed", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    return fails ? 1 : 0;
}
