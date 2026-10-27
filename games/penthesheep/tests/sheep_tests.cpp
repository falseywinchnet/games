// Pen the Sheep: the meadow, the sheep, the bot, the levels.
#include "field.hpp"

#include <chrono>
#include <cstdio>
#include <set>

using namespace sh;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static Meadow blank(int n) {
    Meadow m;
    m.w = m.h = n;
    m.cells.assign(static_cast<size_t>(n * n), Cell::grass);
    m.sheep = m.idx(n / 2, n / 2);
    m.head = 0;
    return m;
}

int main() {
    const auto t0 = std::chrono::steady_clock::now();
    // neighbours: symmetric, six inside, fewer at the edges
    {
        const Meadow m = blank(9);
        for (int i = 0; i < 81; ++i) {
            int nb[6];
            const int n = m.neighbours(i, nb);
            CHECK(n <= 6 && n >= 2);
            if (!m.edge(i)) CHECK(n == 6);
            for (int j = 0; j < n; ++j) {
                int back[6];
                const int k = m.neighbours(nb[j], back);
                bool found = false;
                for (int q = 0; q < k; ++q) found = found || back[q] == i;
                CHECK(found);
            }
        }
    }
    // distances and the wall: an open meadow needs six stones round the sheep; a ring of six pens it
    {
        Meadow m = blank(9);
        CHECK(m.distances()[static_cast<size_t>(m.sheep)] == 4);
        CHECK(cut_size(m) == 6);
        int nb[6];
        const int n = m.neighbours(m.sheep, nb);
        for (int j = 0; j < n; ++j) m.cells[static_cast<size_t>(nb[j])] = Cell::stone;
        CHECK(m.penned());
        CHECK(cut_size(m) == 0);
        m.cells[static_cast<size_t>(nb[0])] = Cell::grass;
        CHECK(!m.penned() && cut_size(m) == 1);
    }
    // the head start: three stones before the sheep moves; then it steps toward the edge
    {
        Meadow m = blank(9);
        m.head = kHeadStart;
        const int start = m.sheep;
        for (int k = 0; k < kHeadStart; ++k) {
            CHECK(place(m, k));  // the top row
            CHECK(m.sheep == start);
        }
        CHECK(!place(m, m.sheep));  // never on the sheep
        const int d0 = m.distances()[static_cast<size_t>(m.sheep)];
        SheepMove mv;
        CHECK(place(m, 80, &mv));
        CHECK(mv.kind == SheepMove::step && m.sheep != start);
        CHECK(m.distances()[static_cast<size_t>(m.sheep)] == d0 - 1);
    }
    // reaching an edge patch, it walks straight off
    {
        Meadow m = blank(9);
        m.sheep = m.idx(1, 4);
        SheepMove mv;
        CHECK(place(m, m.idx(8, 8), &mv));
        CHECK(mv.kind == SheepMove::step && m.escaped && m.edge(m.sheep));
    }
    // standing on the edge (an old save, say), it hops off
    {
        Meadow m = blank(9);
        m.sheep = m.idx(0, 4);
        SheepMove mv;
        CHECK(place(m, m.idx(8, 8), &mv));
        CHECK(mv.kind == SheepMove::escape && m.escaped);
        CHECK(!place(m, m.idx(7, 7)));
    }
    // clover: beside the sheep, it goes for it and then spends a turn munching
    {
        Meadow m = blank(9);
        int nb[6];
        m.neighbours(m.sheep, nb);
        m.cells[static_cast<size_t>(nb[5])] = Cell::clover;
        SheepMove mv;
        place(m, m.idx(0, 0), &mv);
        CHECK(mv.kind == SheepMove::step && m.sheep == nb[5] && m.munching);
        CHECK(m.cells[static_cast<size_t>(nb[5])] == Cell::grass);
        const int at = m.sheep;
        place(m, m.idx(1, 0), &mv);
        CHECK(mv.kind == SheepMove::munch && m.sheep == at && !m.munching);
    }
    // the sheep is deterministic: the same meadow, the same answer
    {
        for (int lv : {1, 9, 22}) {
            const Level l = generate(params_for(lv));
            CHECK(sheep_choice(l.start).to == sheep_choice(l.start).to);
        }
    }
    // the campaign: every level's proof replays to a penned sheep; pars sensible; the sheep gets cleverer
    {
        std::set<std::uint64_t> seen;
        int cunning = 0;
        for (int lv = 1; lv <= 36; ++lv) {
            const Level l = generate(params_for(lv));
            Meadow m = l.start;
            CHECK(!m.penned() && m.head == kHeadStart && m.stones == 0);
            for (int s : l.solution) { CHECK(!m.escaped); CHECK(place(m, s)); }
            CHECK(m.penned() && !m.escaped);
            CHECK(l.par == static_cast<int>(l.solution.size()) && l.par >= kHeadStart + 2 && l.par <= 30);
            std::uint64_t h = 0;
            for (size_t i = 0; i < l.start.cells.size(); ++i) h = h * 31 + static_cast<std::uint64_t>(l.start.cells[i]);
            seen.insert(h);
            cunning += l.start.smarts == Smarts::cunning;
        }
        CHECK(seen.size() == 36);
        CHECK(cunning > 0);
        CHECK(params_for(1).smarts == Smarts::dozy && params_for(40).smarts == Smarts::cunning);
    }
    // Easy, Medium and Hard: the right sheep, and every meadow made is one the bot pens
    {
        const Smarts kinds[3] = {Smarts::dozy, Smarts::clever, Smarts::cunning};
        for (int d = 0; d < 3; ++d)
            for (std::uint64_t seed = 1; seed <= 6; ++seed) {
                const Level l = generate(params_for_difficulty(d, seed * 977));
                CHECK(l.start.smarts == kinds[d] && l.start.w == (d == 0 ? 9 : 11));
                Meadow m = l.start;
                for (int c : l.solution) place(m, c);
                CHECK(m.penned() && !m.escaped);
            }
    }
    // a cleverer sheep is harder: against a careless player (stones in random spots near it) it escapes more often
    {
        int escapes[3] = {};
        for (int s = 0; s < 3; ++s)
            for (int g = 0; g < 20; ++g) {
                LevelParams p = params_for(10);
                p.smarts = static_cast<Smarts>(s);
                p.seed = 1000 + g;
                Meadow m = generate(p).start;
                Rng r(g * 7 + 1);
                for (int k = 0; k < 60 && !m.escaped && !m.penned(); ++k) {
                    // half the time the bot's stone, half the time a random one nearby
                    int c = bot_stone(m);
                    if (r.unit() < .5) {
                        for (int tries = 0; tries < 50; ++tries) {
                            const int i = r.range(m.w * m.h);
                            if (m.can_place(i) && std::abs(m.col(i) - m.col(m.sheep)) <= 3 && std::abs(m.row(i) - m.row(m.sheep)) <= 3) { c = i; break; }
                        }
                    }
                    place(m, c);
                }
                escapes[s] += m.escaped;
            }
        std::printf("careless player: the sheep escapes %d/20 (dozy), %d/20 (clever), %d/20 (cunning)\n", escapes[0], escapes[1], escapes[2]);
        CHECK(escapes[2] >= escapes[0]);
    }
    std::printf("%s (%.2fs)\n", fails ? "FAILED" : "all tests passed", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    return fails ? 1 : 0;
}
