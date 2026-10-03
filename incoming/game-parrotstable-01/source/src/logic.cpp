#include "logic.hpp"

#include <algorithm>
#include <bit>

namespace pt {

namespace {
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ULL + 0x6A09E667F3BCC909ULL) { next(); next(); }
    std::uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    int range(int n) { return n <= 1 ? 0 : static_cast<int>(next() % static_cast<std::uint64_t>(n)); }
    double unit() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
};

bool world_ok(const Puzzle& p, const std::vector<Asked>& asked, const World& w) {
    if (std::popcount(static_cast<unsigned>(w.liars)) != p.liar_count) return false;
    for (const Statement& s : p.said)
        if (s.f.eval(w, p.parrots) != w.honest(s.speaker)) return false;
    for (const Asked& a : asked)
        if (answer(a.q, w, p.parrots) != a.yes) return false;
    return true;
}
}  // namespace

bool Formula::eval(const World& w, int parrots) const {
    const auto h = [&](int i) { return w.honest(i); };
    switch (kind) {
        case Kind::is_liar: return !h(a);
        case Kind::is_honest: return h(a);
        case Kind::both_liars: return !h(a) && !h(b);
        case Kind::both_honest: return h(a) && h(b);
        case Kind::exactly_one_liar: return h(a) != h(b);
        case Kind::same_kind: return h(a) == h(b);
        case Kind::did_it: return w.culprit == a;
        case Kind::didnt_do_it: return w.culprit != a;
        case Kind::one_of: return w.culprit == a || w.culprit == b;
        case Kind::if_honest_did: return !h(a) || w.culprit == b;
        case Kind::culprit_lies: return !h(w.culprit);
        case Kind::culprit_honest: return h(w.culprit);
        case Kind::count_liars: return std::popcount(static_cast<unsigned>(w.liars)) == n;
        case Kind::at_least_liars: return std::popcount(static_cast<unsigned>(w.liars)) >= n;
        case Kind::neighbour_did: return (w.culprit == a - 1 || w.culprit == a + 1) && w.culprit >= 0 && w.culprit < parrots;  // the birds either side in the row
        case Kind::not_me: return w.culprit != a;
    }
    return false;
}

bool answer(const Question& q, const World& w, int parrots) {
    const bool t = q.f.eval(w, parrots);
    return w.honest(q.to) ? t : !t;
}

std::vector<World> consistent(const Puzzle& p, const std::vector<Asked>& asked) {
    std::vector<World> out;
    for (unsigned m = 0; m < (1u << p.parrots); ++m)
        for (int c = 0; c < p.parrots; ++c) {
            const World w{static_cast<std::uint8_t>(m), c};
            if (world_ok(p, asked, w)) out.push_back(w);
        }
    return out;
}

bool settles(const Puzzle& p, const std::vector<Asked>& asked, const Question& q) {
    std::vector<Asked> more = asked;
    more.push_back({q, answer(q, p.truth, p.parrots)});
    return consistent(p, more).size() == 1;
}

// ------------------------------------------------------------------ the grader
namespace {
// What a careful solver knows: for each parrot, whether honest / lying is still possible, and who could still be the culprit.
struct Know {
    std::uint8_t can_honest = 0x7F, can_lie = 0x7F;
    std::uint8_t suspects = 0x7F;
    bool fits(const World& w, int n) const {
        for (int i = 0; i < n; ++i) {
            if (w.honest(i) && !(can_honest >> i & 1)) return false;
            if (!w.honest(i) && !(can_lie >> i & 1)) return false;
        }
        return suspects >> w.culprit & 1;
    }
    bool dead(int n) const {
        for (int i = 0; i < n; ++i)
            if (!(can_honest >> i & 1) && !(can_lie >> i & 1)) return true;
        return (suspects & ((1u << n) - 1)) == 0;
    }
    bool solved(int n) const {
        for (int i = 0; i < n; ++i)
            if ((can_honest >> i & 1) && (can_lie >> i & 1)) return false;
        return std::popcount(static_cast<unsigned>(suspects & ((1u << n) - 1))) == 1;
    }
    int open(int n) const {
        int k = 0;
        for (int i = 0; i < n; ++i) k += (can_honest >> i & 1) && (can_lie >> i & 1);
        return k + std::popcount(static_cast<unsigned>(suspects & ((1u << n) - 1))) - 1;
    }
};

// One constraint at a time: a value with no supporting world (given what is known) is struck out.
// Constraint -1 is the liar count; 0.. are the statements; then the answers.
int propagate(const Puzzle& p, const std::vector<Asked>& asked, Know& k) {
    const int n = p.parrots;
    const int total = 1 + static_cast<int>(p.said.size()) + static_cast<int>(asked.size());
    int steps = 0;
    for (bool again = true; again && !k.dead(n);) {
        again = false;
        for (int ci = 0; ci < total && !k.dead(n); ++ci) {
            std::uint8_t sup_h = 0, sup_l = 0, sup_c = 0;
            for (unsigned m = 0; m < (1u << n); ++m)
                for (int c = 0; c < n; ++c) {
                    const World w{static_cast<std::uint8_t>(m), c};
                    if (!k.fits(w, n)) continue;
                    bool ok;
                    if (ci == 0) ok = std::popcount(m) == p.liar_count;
                    else if (ci <= static_cast<int>(p.said.size())) {
                        const Statement& s = p.said[static_cast<size_t>(ci - 1)];
                        ok = s.f.eval(w, n) == w.honest(s.speaker);
                    } else {
                        const Asked& a = asked[static_cast<size_t>(ci - 1 - static_cast<int>(p.said.size()))];
                        ok = answer(a.q, w, n) == a.yes;
                    }
                    if (!ok) continue;
                    sup_h |= static_cast<std::uint8_t>(~m & 0x7F);
                    sup_l |= static_cast<std::uint8_t>(m);
                    sup_c |= static_cast<std::uint8_t>(1u << c);
                }
            const std::uint8_t nh = k.can_honest & sup_h, nl = k.can_lie & sup_l, nc = k.suspects & sup_c;
            const int gained = std::popcount(static_cast<unsigned>(k.can_honest ^ nh)) + std::popcount(static_cast<unsigned>(k.can_lie ^ nl)) +
                               std::popcount(static_cast<unsigned>(k.suspects ^ nc));
            if (gained) {
                steps += gained;
                k.can_honest = nh;
                k.can_lie = nl;
                k.suspects = nc;
                again = true;
            }
        }
    }
    return steps;
}
}  // namespace

bool grade(const Puzzle& p, const std::vector<Asked>& asked, int& steps, int& suppositions) {
    const int n = p.parrots;
    Know k;
    steps = propagate(p, asked, k);
    suppositions = 0;
    while (!k.solved(n)) {
        if (k.dead(n)) return false;
        // stuck: suppose something, follow it, and strike it out if it ends in absurdity
        bool progress = false;
        for (int v = 0; v < 3 * n && !progress; ++v) {
            Know t = k;
            const int i = v % n, kind = v / n;
            if (kind == 0) { if (!(t.can_honest >> i & 1) || !(t.can_lie >> i & 1)) continue; t.can_lie &= static_cast<std::uint8_t>(~(1u << i)); }
            else if (kind == 1) { if (!(t.can_honest >> i & 1) || !(t.can_lie >> i & 1)) continue; t.can_honest &= static_cast<std::uint8_t>(~(1u << i)); }
            else { if (!(t.suspects >> i & 1) || std::popcount(static_cast<unsigned>(t.suspects)) < 2) continue; t.suspects = static_cast<std::uint8_t>(1u << i); }
            const int inner = propagate(p, asked, t);
            if (!t.dead(n)) continue;
            ++suppositions;
            steps += inner;
            if (kind == 0) k.can_honest &= static_cast<std::uint8_t>(~(1u << i));
            else if (kind == 1) k.can_lie &= static_cast<std::uint8_t>(~(1u << i));
            else k.suspects &= static_cast<std::uint8_t>(~(1u << i));
            steps += 1 + propagate(p, asked, k);
            progress = true;
        }
        if (!progress) return false;
    }
    return true;
}

// ------------------------------------------------------------------ the generator
namespace {
Formula random_formula(Rng& r, int n, int speaker, int tier, const GenParams& g, int twins_a, int twins_b) {
    // kinds allowed grow with the tier
    static const Kind t0[] = {Kind::is_liar, Kind::is_honest, Kind::did_it, Kind::didnt_do_it, Kind::not_me};
    static const Kind t1[] = {Kind::both_liars, Kind::both_honest, Kind::one_of, Kind::exactly_one_liar, Kind::count_liars};
    static const Kind t2[] = {Kind::same_kind, Kind::if_honest_did, Kind::culprit_lies, Kind::culprit_honest, Kind::neighbour_did};
    static const Kind t3[] = {Kind::at_least_liars};
    std::vector<Kind> pool(std::begin(t0), std::end(t0));
    if (tier >= 1) pool.insert(pool.end(), std::begin(t1), std::end(t1));
    if (tier >= 2) pool.insert(pool.end(), std::begin(t2), std::end(t2));
    if (tier >= 3) pool.insert(pool.end(), std::begin(t3), std::end(t3));
    Formula f;
    f.kind = pool[static_cast<size_t>(r.range(static_cast<int>(pool.size())))];
    auto other = [&](int not1, int not2) {
        for (;;) {
            const int x = r.range(n);
            if (x != not1 && x != not2) return x;
        }
    };
    f.a = other(speaker, -1);
    f.b = other(speaker, f.a);
    if (f.kind == Kind::not_me) f.a = speaker;
    if (f.kind == Kind::neighbour_did) f.a = speaker;  // "one of my neighbours did it"
    if (f.kind == Kind::count_liars || f.kind == Kind::at_least_liars) f.n = 1 + r.range(std::max(1, n - 1));
    // the twins come up in pair statements
    if (g.twins && twins_a >= 0 && (f.kind == Kind::exactly_one_liar || f.kind == Kind::both_liars || f.kind == Kind::same_kind) && speaker != twins_a && speaker != twins_b && r.unit() < .6) {
        f.a = twins_a;
        f.b = twins_b;
    }
    return f;
}

bool same_formula(const Formula& x, const Formula& y) {
    if (x.kind != y.kind || x.n != y.n) return false;
    return (x.a == y.a && x.b == y.b) || (x.a == y.b && x.b == y.a);
}
}  // namespace

Puzzle generate(const GenParams& g) {
    const int n = std::clamp(g.parrots, 3, kMaxParrots);
    for (std::uint64_t attempt = 0;; ++attempt) {
        Rng r(g.seed * 7919 + attempt);
        Puzzle p;
        p.parrots = n;
        // the hidden truth
        p.liar_count = 1 + r.range(std::max(1, n - 2));
        unsigned m = 0;
        while (std::popcount(m) < p.liar_count) m |= 1u << r.range(n);
        p.truth = {static_cast<std::uint8_t>(m), r.range(n)};
        if (g.twins) { p.twins_a = r.range(n); do p.twins_b = r.range(n); while (p.twins_b == p.twins_a); }
        // who keeps quiet
        std::vector<int> order(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) order[static_cast<size_t>(i)] = i;
        for (int i = n - 1; i > 0; --i) std::swap(order[static_cast<size_t>(i)], order[static_cast<size_t>(r.range(i + 1))]);
        const int silent = std::clamp(g.silent, 0, n - 2);
        p.silent.assign(order.begin(), order.begin() + silent);
        std::sort(p.silent.begin(), p.silent.end());
        // statements: each speaker says something true if honest, false if a liar
        auto deal = [&](int speaker) {
            for (int t = 0; t < 200; ++t) {
                const Formula f = random_formula(r, n, speaker, g.tier, g, p.twins_a, p.twins_b);
                if (f.eval(p.truth, n) != p.truth.honest(speaker)) continue;
                bool dup = false;
                for (const Statement& s : p.said) dup = dup || same_formula(s.f, f);
                if (dup) continue;
                p.said.push_back({speaker, f});
                return true;
            }
            return false;
        };
        bool ok = true;
        for (int i = 0; i < n && ok; ++i)
            if (std::find(p.silent.begin(), p.silent.end(), i) == p.silent.end()) ok = deal(i);
        if (!ok) continue;
        std::vector<World> ws = consistent(p, {});
        // add a second remark from someone if the table is too vague (it never settles things alone when questions are due)
        for (int extra = 0; extra < 3 && ws.size() > (silent ? 4u : 1u); ++extra) {
            int sp;
            do sp = r.range(n); while (std::find(p.silent.begin(), p.silent.end(), sp) != p.silent.end());
            if (!deal(sp)) break;
            ws = consistent(p, {});
        }
        if (ws.empty()) continue;
        if (silent == 0) {
            if (ws.size() != 1) continue;
        } else {
            // the table alone must leave doubt, which a well-chosen question can settle
            if (ws.size() < 2 || ws.size() > 6) continue;
            p.asks = std::max(1, g.asks);
            p.offers.assign(static_cast<size_t>(silent), {});
            int settling = 0, total = 0;
            for (size_t si = 0; si < p.silent.size(); ++si) {
                const int to = p.silent[si];
                std::vector<Question> pool;
                for (int t = 0; t < 60 && static_cast<int>(pool.size()) < 12; ++t) {
                    Question q{to, random_formula(r, n, to, std::max(1, g.tier), g, p.twins_a, p.twins_b)};
                    bool dup = false;
                    for (const Question& o : pool) dup = dup || same_formula(o.f, q.f);
                    if (!dup) pool.push_back(q);
                }
                // offer a mix: at least one that settles it (if any exists here), and some that don't
                std::vector<Question> good, poor;
                for (const Question& q : pool) (settles(p, {}, q) ? good : poor).push_back(q);
                std::vector<Question>& offer = p.offers[si];
                if (!good.empty()) offer.push_back(good[static_cast<size_t>(r.range(static_cast<int>(good.size())))]);
                while (static_cast<int>(offer.size()) < g.choices && !poor.empty()) {
                    const int k = r.range(static_cast<int>(poor.size()));
                    offer.push_back(poor[static_cast<size_t>(k)]);
                    poor.erase(poor.begin() + k);
                }
                for (int i = static_cast<int>(offer.size()) - 1; i > 0; --i) std::swap(offer[static_cast<size_t>(i)], offer[static_cast<size_t>(r.range(i + 1))]);
                for (const Question& q : offer) { settling += settles(p, {}, q); ++total; }
            }
            if (p.asks >= 2 && silent >= 2) {
                // with two questions, a good pair is enough even if no single question settles it
                bool pair = settling > 0;
                for (size_t a = 0; a < p.offers.size() && !pair; ++a)
                    for (const Question& qa : p.offers[a])
                        for (size_t b = 0; b < p.offers.size() && !pair; ++b)
                            for (const Question& qb : p.offers[b]) {
                                if (&qa == &qb) continue;
                                std::vector<Asked> one{{qa, answer(qa, p.truth, n)}};
                                if (settles(p, one, qb)) { pair = true; break; }
                            }
                if (!pair) continue;
            } else if (settling == 0) {
                continue;
            }
            if (settling == total) continue;  // the choice must matter
        }
        // grade it with the settling question asked (if one is needed)
        std::vector<Asked> best;
        if (silent > 0)
            for (const auto& offer : p.offers)
                for (const Question& q : offer)
                    if (best.empty() && settles(p, {}, q)) best.push_back({q, answer(q, p.truth, n)});
        int steps = 0, sup = 0;
        if (!grade(p, best, steps, sup) && silent == 0) continue;  // a human must be able to do it without guesswork
        p.steps = steps;
        p.suppositions = sup;
        p.difficulty = steps + 6 * sup + 8 * silent;
        return p;
    }
}

}  // namespace pt
