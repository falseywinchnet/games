// Logic tests: the semantics checked against an independent truth table, the
// generator's promises (one world exactly once the right question is asked,
// none before when questions are due, a real choice among the questions), the
// grader finishing every no-question puzzle, and the voices covering every kind.
#include "logic.hpp"
#include "script.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <set>

using namespace pt;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

// The same meanings, written out again independently.
static bool ref(const Formula& f, unsigned liars, int culprit, int n) {
    auto lies = [&](int i) { return (liars >> i & 1) != 0; };
    int count = 0;
    for (int i = 0; i < n; ++i) count += lies(i);
    switch (f.kind) {
        case Kind::is_liar: return lies(f.a);
        case Kind::is_honest: return !lies(f.a);
        case Kind::both_liars: return lies(f.a) && lies(f.b);
        case Kind::both_honest: return !lies(f.a) && !lies(f.b);
        case Kind::exactly_one_liar: return (lies(f.a) ? 1 : 0) + (lies(f.b) ? 1 : 0) == 1;
        case Kind::same_kind: return lies(f.a) == lies(f.b);
        case Kind::did_it: return culprit == f.a;
        case Kind::didnt_do_it: return culprit != f.a;
        case Kind::one_of: return culprit == f.a || culprit == f.b;
        case Kind::if_honest_did: return lies(f.a) || culprit == f.b;
        case Kind::culprit_lies: return lies(culprit);
        case Kind::culprit_honest: return !lies(culprit);
        case Kind::count_liars: return count == f.n;
        case Kind::at_least_liars: return count >= f.n;
        case Kind::neighbour_did: return (f.a > 0 && culprit == f.a - 1) || (f.a + 1 < n && culprit == f.a + 1);
        case Kind::not_me: return culprit != f.a;
    }
    return false;
}

int main() {
    // --- semantics, every kind over every world for 5 parrots
    {
        int checked = 0;
        for (int k = 0; k <= static_cast<int>(Kind::not_me); ++k)
            for (int a = 0; a < 5; ++a)
                for (int b = 0; b < 5; ++b) {
                    if (a == b) continue;
                    for (int nn = 1; nn <= 4; ++nn) {
                        const Formula f{static_cast<Kind>(k), a, b, nn};
                        for (unsigned m = 0; m < 32; ++m)
                            for (int c = 0; c < 5; ++c) {
                                ++checked;
                                CHECK(f.eval(World{static_cast<std::uint8_t>(m), c}, 5) == ref(f, m, c, 5));
                            }
                    }
                }
        std::printf("semantics: %d evaluations agree with the reference\n", checked);
    }
    // --- the generator's promises
    {
        const auto t0 = std::chrono::steady_clock::now();
        int puzzles = 0, with_q = 0, graded = 0;
        struct C { int n, silent, asks, tier; bool twins; };
        const C cs[] = {{4, 0, 0, 0, false}, {5, 0, 0, 1, true}, {5, 1, 1, 1, false}, {6, 0, 0, 2, true}, {6, 1, 1, 2, true}, {6, 2, 2, 2, false}, {7, 1, 1, 3, true}, {7, 2, 2, 3, true}};
        for (const C& c : cs)
            for (std::uint64_t seed = 1; seed <= 60; ++seed) {
                GenParams g;
                g.parrots = c.n; g.silent = c.silent; g.asks = c.asks; g.tier = c.tier; g.twins = c.twins; g.seed = seed * 31 + static_cast<std::uint64_t>(c.n);
                const Puzzle p = generate(g);
                ++puzzles;
                CHECK(std::popcount(static_cast<unsigned>(p.truth.liars)) == p.liar_count);
                // every statement is true exactly when its speaker is honest
                for (const Statement& s : p.said) CHECK(s.f.eval(p.truth, p.parrots) == p.truth.honest(s.speaker));
                for (const Statement& s : p.said) CHECK(std::find(p.silent.begin(), p.silent.end(), s.speaker) == p.silent.end());
                const std::vector<World> ws = consistent(p, {});
                bool truth_in = false;
                for (const World& w : ws) truth_in = truth_in || (w.liars == p.truth.liars && w.culprit == p.truth.culprit);
                CHECK(truth_in);
                if (p.silent.empty()) {
                    CHECK(ws.size() == 1);
                    int st = 0, sp = 0;
                    CHECK(grade(p, {}, st, sp));
                    ++graded;
                } else {
                    ++with_q;
                    CHECK(ws.size() >= 2);
                    // some sequence of asks settles it, and not every offered question does
                    int settle = 0, total = 0;
                    for (const auto& offer : p.offers)
                        for (const Question& q : offer) {
                            CHECK(q.to >= 0 && std::find(p.silent.begin(), p.silent.end(), q.to) != p.silent.end());
                            settle += settles(p, {}, q);
                            ++total;
                        }
                    bool solvable = settle > 0;
                    if (!solvable && p.asks >= 2)
                        for (const auto& oa : p.offers)
                            for (const Question& qa : oa)
                                for (const auto& ob : p.offers)
                                    for (const Question& qb : ob)
                                        if (&qa != &qb && settles(p, {{qa, answer(qa, p.truth, p.parrots)}}, qb)) solvable = true;
                    CHECK(solvable);
                    CHECK(settle < total);
                }
            }
        std::printf("generator: %d puzzles (%d needing questions, %d graded by the step solver) in %.2fs\n", puzzles, with_q, graded,
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    }
    // --- the voices: every kind has words, names appear, and phrasing varies
    {
        Script sc(5);
        sc.cast({"Ada", "Bram", "Cleo", "Dot", "Ezra"}, {Manner::posh, Manner::salty, Manner::nervous, Manner::gossip, Manner::scholar}, 1, 2, 0);
        std::set<std::string> seen;
        for (int k = 0; k <= static_cast<int>(Kind::not_me); ++k)
            for (int rep = 0; rep < 6; ++rep) {
                const Statement s{0, Formula{static_cast<Kind>(k), 3, 4, 2}};
                const std::string line = sc.statement(s);
                CHECK(!line.empty() && line.find(" ?") == std::string::npos && line.find("?.") == std::string::npos);  // "?" alone would be an unknown name
                seen.insert(line);
            }
        CHECK(seen.size() > 60);
        const std::string q = sc.question({2, Formula{Kind::is_liar, 4, 0, 0}});
        CHECK(q.find("Cleo") == 0 && q.back() == '?');
        std::printf("voices: %zu distinct lines from %d kinds x 6\n", seen.size(), static_cast<int>(Kind::not_me) + 1);
    }
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("all tests passed\n");
    return 0;
}
