// Design tests for the Paradoxiary's rules: the formulas' meanings against an independent
// reading, the perching engine's promise (every bird content, every table), and the
// generator's promises over hundreds of tables (one culprit certain from the claims, or
// with a question bird two or three suspects and exactly one question that settles it;
// liars' claims all false, truth-tellers' all true, chatterers silent on the case; two or
// three claims each), and the voices and chatter: every kind worded, lines varied.
#include "logic.hpp"
#include "perching.hpp"
#include "script.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>

using namespace pt;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

// A table's scene: a layout, birds settled on it, a few of them red.
static Scene table_scene(std::uint64_t seed, int birds, std::vector<Bird>& flock, Layout& L) {
    L = make_layout(seed);
    flock.assign(static_cast<size_t>(birds), Bird{});
    std::vector<bool> red(static_cast<size_t>(birds));
    for (int i = 0; i < birds; ++i) red[static_cast<size_t>(i)] = flock[static_cast<size_t>(i)].red = (seed + static_cast<std::uint64_t>(i) * 7) % 5 == 0;
    const std::vector<int> at = settle(L, flock, seed);
    return scene_of(L, at, red);
}

// The same meanings, written out again from the scene directly.
static bool ref(const Formula& f, const World& w, const Scene& s) {
    const Seat& c = s.seats[static_cast<size_t>(w.culprit)];
    const Seat& A = s.seats[static_cast<size_t>(f.a)];
    const Seat& B = s.seats[static_cast<size_t>(f.b)];
    auto lies = [&](int i) { return (w.liars >> i & 1) != 0; };
    switch (f.kind) {
    case Kind::is_liar: return lies(f.a);
    case Kind::is_truthful: return !lies(f.a);
    case Kind::exactly_one_liar: return (lies(f.a) ? 1 : 0) + (lies(f.b) ? 1 : 0) == 1;
    case Kind::same_kind: return lies(f.a) == lies(f.b);
    case Kind::did_it: return w.culprit == f.a;
    case Kind::didnt_do_it: case Kind::not_me: return w.culprit != f.a;
    case Kind::one_of: return w.culprit == f.a || w.culprit == f.b;
    case Kind::if_truthful_did: return !(!lies(f.a) && w.culprit != f.b);
    case Kind::culprit_lies: return lies(w.culprit);
    case Kind::culprit_truthful: return !lies(w.culprit);
    case Kind::culprit_on: return static_cast<int>(c.stuff) == f.n;
    case Kind::culprit_shade: return c.shade;
    case Kind::culprit_sun: return !c.shade;
    case Kind::culprit_high: return c.high;
    case Kind::culprit_low: return c.low;
    case Kind::culprit_red: return c.red;
    case Kind::culprit_not_red: return !c.red;
    case Kind::culprit_beside: return w.culprit != f.a && c.bar == A.bar;
    case Kind::culprit_alone: {
        for (int i = 0; i < s.birds(); ++i)
            if (i != w.culprit && s.seats[static_cast<size_t>(i)].bar == c.bar) return false;
        return true;
    }
    case Kind::culprit_above: return c.rank > A.rank;
    case Kind::culprit_below: return c.rank < A.rank;
    case Kind::sits_on: return static_cast<int>(A.stuff) == f.n;
    case Kind::sits_shade: return A.shade;
    case Kind::sits_sun: return !A.shade;
    case Kind::sits_above: return A.rank > B.rank;
    case Kind::sits_beside: return f.a != f.b && A.bar == B.bar;
    }
    return false;
}

int main() {
    const auto t0 = std::chrono::steady_clock::now();
    // --- the perching engine: every bird content with its likings, 300 tables
    for (std::uint64_t seed = 1; seed <= 300; ++seed) {
        std::vector<Bird> flock;
        Layout L;
        const int birds = 5 + static_cast<int>(seed % 6);
        table_scene(seed, birds, flock, L);
        const std::vector<int> at = settle(L, flock, seed);
        std::set<int> used(at.begin(), at.end());
        CHECK(used.size() == at.size());
        CHECK(L.places.size() >= 18 && L.places.size() <= 26);
        for (int i = 0; i < birds; ++i) {
            CHECK(content(L, flock, at, i));
            CHECK(!flock[static_cast<size_t>(i)].likes.empty() && flock[static_cast<size_t>(i)].likes.size() <= 3);
        }
    }
    // --- semantics, every kind against the reference, over real scenes
    {
        int checked = 0;
        for (std::uint64_t seed = 1; seed <= 6; ++seed) {
            std::vector<Bird> flock;
            Layout L;
            const Scene s = table_scene(seed, 7, flock, L);
            for (int k = 0; k < kKinds; ++k)
                for (int a = 0; a < 7; ++a)
                    for (int b = 0; b < 7; ++b)
                        for (int nn = 0; nn < 5; ++nn)
                            for (unsigned m = 0; m < 128; m += 5)
                                for (int c = 0; c < 7; ++c) {
                                    const Formula f{static_cast<Kind>(k), a, b, nn};
                                    const World w{static_cast<std::uint16_t>(m), c};
                                    ++checked;
                                    CHECK(f.eval(w, s) == ref(f, w, s));
                                }
        }
        std::printf("semantics: %d checks\n", checked);
    }
    // --- the generator's promises
    int tables = 0, with_question = 0, eye_claims = 0, total_claims = 0;
    for (std::uint64_t seed = 1; seed <= 400; ++seed) {
        GenParams g;
        g.birds = 5 + static_cast<int>(seed % 6);
        g.liars = 1 + static_cast<int>(seed % 2);
        g.question = seed % 3 == 0;
        g.choices = 3 + static_cast<int>(seed % 2);
        g.tier = static_cast<int>(seed % 4);
        g.seed = seed * 977;
        std::vector<Bird> flock;
        Layout L;
        const Scene s = table_scene(seed, g.birds, flock, L);
        const Puzzle p = generate(g, s);
        CHECK(p.birds == g.birds);
        if (p.birds != g.birds) continue;
        ++tables;
        const World truth = p.truth();
        int liars = 0, truthful = 0;
        for (int i = 0; i < p.birds; ++i) {
            const Role r = p.roles[static_cast<size_t>(i)];
            liars += r == Role::liar;
            truthful += r == Role::truthful;
            const auto& cl = p.claims[static_cast<size_t>(i)];
            if (r == Role::chatter || i == p.asker) CHECK(cl.empty());
            else CHECK(cl.size() >= 2 && cl.size() <= 3);
            for (const Formula& f : cl) {
                CHECK(f.eval(truth, s) == (r == Role::truthful));
                eye_claims += by_eye(f.kind);
                ++total_claims;
            }
        }
        CHECK(liars == g.liars && truthful == 2);
        const std::vector<World> worlds = consistent(p);
        CHECK(std::any_of(worlds.begin(), worlds.end(), [&](const World& w) { return w.liars == truth.liars && w.culprit == truth.culprit; }));
        const std::vector<int> c = suspects(p);
        if (g.question) {
            ++with_question;
            CHECK(p.asker >= 0 && p.roles[static_cast<size_t>(p.asker)] != Role::chatter);
            CHECK(c.size() >= 2 && c.size() <= 3);
            CHECK(static_cast<int>(p.offers.size()) == g.choices);
            int settling = 0;
            for (size_t k = 0; k < p.offers.size(); ++k) {
                const Asked a{static_cast<int>(k), answer(p, static_cast<int>(k), truth)};
                const std::vector<int> after = suspects(p, &a);
                if (after.size() == 1) {
                    ++settling;
                    CHECK(after[0] == p.culprit);
                    CHECK(static_cast<int>(k) == p.good);
                }
            }
            CHECK(settling == 1);
        } else {
            CHECK(c.size() == 1 && c[0] == p.culprit);
        }
    }
    std::printf("generator: %d tables (%d with a question bird), %d claims, %d checkable by eye\n", tables, with_question, total_claims, eye_claims);
    CHECK(tables == 400);
    // --- the voices: every kind worded, in every manner; chatter varied
    {
        std::vector<Bird> flock;
        Layout L;
        GenParams g;
        g.birds = 8;
        g.liars = 2;
        g.tier = 3;
        g.question = true;
        g.seed = 5;
        const Scene s = table_scene(5, 8, flock, L);
        const Puzzle p = generate(g, s);
        Script sc(11);
        std::vector<std::string> names = {"Ada", "Bram", "Cleo", "Dot", "Ezra", "Fig", "Gus", "Hattie"};
        std::vector<Manner> manners;
        for (int i = 0; i < 8; ++i) manners.push_back(static_cast<Manner>(i % kManners));
        std::vector<int> species = {0, 1, 2, 3, 4, 5, 6, 7};
        sc.cast(names, manners, species, flock, s, 3);
        std::set<std::string> said;
        for (int k = 0; k < kKinds; ++k)
            for (int who = 0; who < 8; ++who) {
                const Formula f{static_cast<Kind>(k), (who + 1) % 8, (who + 2) % 8, static_cast<int>(Stuff::rope)};
                const std::string line = sc.claim(who, f);
                CHECK(!line.empty() && line.find("...") != 0);
                CHECK(!sc.question(p.asker >= 0 ? p.asker : 0, f).empty());
                said.insert(line);
            }
        CHECK(said.size() > static_cast<size_t>(kKinds * 6));
        // a bird rarely repeats itself: in a hundred lines, few come round twice
        int worst = 100;
        for (int who = 0; who < 8; ++who) {
            std::set<std::string> chat;
            for (int k = 0; k < 100; ++k) chat.insert(sc.chatter(who, k % 7));
            worst = std::min(worst, static_cast<int>(chat.size()));
        }
        std::printf("chatter: at least %d different lines in a bird's hundred\n", worst);
        CHECK(worst >= 85);
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%s (%.2f s)\n", failures ? "FAILED" : "all passed", secs);
    return failures ? 1 : 0;
}
