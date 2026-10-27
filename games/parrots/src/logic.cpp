#include "logic.hpp"

#include <algorithm>
#include <bit>
#include <numeric>

namespace pt {

namespace {
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ULL + 0xD1B54A32D192ED03ULL) {}
    std::uint64_t next() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 0x2545F4914F6CDD1DULL;
    }
    int below(int n) { return n > 0 ? static_cast<int>(next() % static_cast<std::uint64_t>(n)) : 0; }
    double u() { return static_cast<double>(next() >> 11) / 9007199254740992.0; }
};

const Seat& seat(const Scene& s, int i) { return s.seats[static_cast<size_t>(i)]; }

// The kinds a table may use, by tier.
bool allowed(Kind k, int tier) {
    switch (k) {
    case Kind::is_liar: case Kind::is_truthful: case Kind::did_it: case Kind::didnt_do_it: case Kind::not_me:
    case Kind::culprit_on: case Kind::culprit_shade: case Kind::culprit_sun: case Kind::culprit_high: case Kind::culprit_low:
    case Kind::sits_on: case Kind::sits_shade: case Kind::sits_sun: case Kind::sits_above: case Kind::sits_beside:
        return true;
    case Kind::one_of: case Kind::culprit_lies: case Kind::culprit_truthful: case Kind::culprit_red: case Kind::culprit_not_red: case Kind::culprit_beside:
        return tier >= 1;
    case Kind::exactly_one_liar: case Kind::same_kind: case Kind::culprit_above: case Kind::culprit_below: case Kind::culprit_alone:
        return tier >= 2;
    case Kind::if_truthful_did:
        return tier >= 3;
    }
    return false;
}

// Every formula worth saying at this table, by `speaker` (-1: put as a question).
std::vector<Formula> candidates(const Scene& s, int speaker, int tier) {
    const int n = s.birds();
    std::vector<Formula> out;
    std::vector<int> stuffs;
    for (const Seat& st : s.seats)
        if (std::find(stuffs.begin(), stuffs.end(), static_cast<int>(st.stuff)) == stuffs.end()) stuffs.push_back(static_cast<int>(st.stuff));
    for (int k = 0; k < kKinds; ++k) {
        const Kind kind = static_cast<Kind>(k);
        if (!allowed(kind, tier)) continue;
        switch (kind) {
        case Kind::not_me:
            if (speaker >= 0) out.push_back({kind, speaker, 0, 0});
            break;
        case Kind::culprit_lies: case Kind::culprit_truthful: case Kind::culprit_shade: case Kind::culprit_sun: case Kind::culprit_high:
        case Kind::culprit_low: case Kind::culprit_red: case Kind::culprit_not_red: case Kind::culprit_alone:
            out.push_back({kind, 0, 0, 0});
            break;
        case Kind::culprit_on:
            for (int st : stuffs) out.push_back({kind, 0, 0, st});
            break;
        case Kind::sits_on:
            for (int a = 0; a < n; ++a)
                for (int st : stuffs) out.push_back({kind, a, 0, st});
            break;
        case Kind::exactly_one_liar: case Kind::same_kind: case Kind::one_of: case Kind::if_truthful_did: case Kind::sits_above: case Kind::sits_beside:
            for (int a = 0; a < n; ++a)
                for (int b = 0; b < n; ++b)
                    if (a != b && (kind == Kind::if_truthful_did || kind == Kind::sits_above || a < b)) out.push_back({kind, a, b, 0});
            break;
        default:
            for (int a = 0; a < n; ++a) {
                // nobody vouches for or against themself by name; "I didn't" is not_me
                if (a == speaker && (kind == Kind::is_liar || kind == Kind::is_truthful || kind == Kind::did_it || kind == Kind::didnt_do_it)) continue;
                out.push_back({kind, a, 0, 0});
            }
        }
    }
    return out;
}

std::vector<int> culprits_of(const std::vector<World>& worlds) {
    std::vector<int> c;
    for (const World& w : worlds)
        if (std::find(c.begin(), c.end(), w.culprit) == c.end()) c.push_back(w.culprit);
    std::sort(c.begin(), c.end());
    return c;
}
}  // namespace

Scene scene_of(const Layout& layout, const std::vector<int>& at, const std::vector<bool>& red) {
    Scene s;
    for (size_t i = 0; i < at.size(); ++i) {
        const Place& pl = layout.places[static_cast<size_t>(at[i])];
        Seat st;
        st.place = at[i];
        st.bar = pl.bar;
        st.stuff = pl.stuff;
        st.shade = pl.shade;
        st.high = layout.high(at[i]);
        st.low = layout.low(at[i]);
        st.rank = pl.rank;
        st.red = i < red.size() && red[i];
        s.seats.push_back(st);
    }
    return s;
}

bool Scene::alone(int a) const {
    for (int b = 0; b < birds(); ++b)
        if (beside(a, b)) return false;
    return true;
}

bool by_eye(Kind k) { return k == Kind::sits_on || k == Kind::sits_shade || k == Kind::sits_sun || k == Kind::sits_above || k == Kind::sits_beside; }

bool Formula::eval(const World& w, const Scene& s) const {
    const int c = w.culprit;
    switch (kind) {
    case Kind::is_liar: return w.lies(a);
    case Kind::is_truthful: return !w.lies(a);
    case Kind::exactly_one_liar: return w.lies(a) != w.lies(b);
    case Kind::same_kind: return w.lies(a) == w.lies(b);
    case Kind::did_it: return c == a;
    case Kind::didnt_do_it: case Kind::not_me: return c != a;
    case Kind::one_of: return c == a || c == b;
    case Kind::if_truthful_did: return w.lies(a) || c == b;
    case Kind::culprit_lies: return w.lies(c);
    case Kind::culprit_truthful: return !w.lies(c);
    case Kind::culprit_on: return static_cast<int>(seat(s, c).stuff) == n;
    case Kind::culprit_shade: return seat(s, c).shade;
    case Kind::culprit_sun: return !seat(s, c).shade;
    case Kind::culprit_high: return seat(s, c).high;
    case Kind::culprit_low: return seat(s, c).low;
    case Kind::culprit_red: return seat(s, c).red;
    case Kind::culprit_not_red: return !seat(s, c).red;
    case Kind::culprit_beside: return s.beside(c, a);
    case Kind::culprit_alone: return s.alone(c);
    case Kind::culprit_above: return c != a && s.higher(c, a);
    case Kind::culprit_below: return c != a && s.higher(a, c);
    case Kind::sits_on: return static_cast<int>(seat(s, a).stuff) == n;
    case Kind::sits_shade: return seat(s, a).shade;
    case Kind::sits_sun: return !seat(s, a).shade;
    case Kind::sits_above: return s.higher(a, b);
    case Kind::sits_beside: return s.beside(a, b);
    }
    return false;
}

std::vector<int> Puzzle::playing() const {
    std::vector<int> p;
    for (int i = 0; i < birds; ++i)
        if (roles[static_cast<size_t>(i)] != Role::chatter) p.push_back(i);
    return p;
}

World Puzzle::truth() const {
    World w;
    w.culprit = culprit;
    for (int i = 0; i < birds; ++i)
        if (roles[static_cast<size_t>(i)] == Role::liar) w.liars = static_cast<std::uint16_t>(w.liars | 1u << i);
    return w;
}

bool answer(const Puzzle& p, int q, const World& w) {
    const bool holds = p.offers[static_cast<size_t>(q)].eval(w, p.scene);
    return w.lies(p.asker) ? !holds : holds;
}

std::vector<World> consistent(const Puzzle& p, const Asked* asked) {
    const std::vector<int> play = p.playing();
    const int m = static_cast<int>(play.size());
    std::vector<World> out;
    for (unsigned sub = 0; sub < (1u << m); ++sub) {
        if (std::popcount(sub) != p.liar_count) continue;
        World w;
        for (int k = 0; k < m; ++k)
            if (sub >> k & 1) w.liars = static_cast<std::uint16_t>(w.liars | 1u << play[static_cast<size_t>(k)]);
        for (int c = 0; c < p.birds; ++c) {
            w.culprit = c;
            bool ok = true;
            for (int i = 0; i < p.birds && ok; ++i)
                for (const Formula& f : p.claims[static_cast<size_t>(i)])
                    if (f.eval(w, p.scene) == w.lies(i)) { ok = false; break; }
            if (ok && asked && asked->offer >= 0 && answer(p, asked->offer, w) != asked->yes) ok = false;
            if (ok) out.push_back(w);
        }
    }
    return out;
}

std::vector<int> suspects(const Puzzle& p, const Asked* asked) { return culprits_of(consistent(p, asked)); }

Puzzle generate(const GenParams& g, const Scene& scene) {
    Rng r(g.seed);
    const int n = scene.birds();
    const int liars = std::clamp(g.liars, 1, 2);
    Puzzle best;
    for (int attempt = 0; attempt < 400; ++attempt) {
        Puzzle p;
        p.birds = n;
        p.liar_count = liars;
        p.scene = scene;
        p.roles.assign(static_cast<size_t>(n), Role::chatter);
        p.claims.assign(static_cast<size_t>(n), {});
        std::vector<int> order(static_cast<size_t>(n));
        std::iota(order.begin(), order.end(), 0);
        for (int i = n - 1; i > 0; --i) std::swap(order[static_cast<size_t>(i)], order[static_cast<size_t>(r.below(i + 1))]);
        for (int k = 0; k < liars + 2; ++k) p.roles[static_cast<size_t>(order[static_cast<size_t>(k)])] = k < liars ? Role::liar : Role::truthful;
        // the culprit: a liar now and then, but anyone may have done it
        p.culprit = r.u() < .3 ? order[static_cast<size_t>(r.below(liars))] : r.below(n);
        const World truth = p.truth();
        const std::vector<int> play = p.playing();
        if (g.question) p.asker = play[static_cast<size_t>(r.below(static_cast<int>(play.size())))];
        std::vector<int> speakers;
        for (int i : play)
            if (i != p.asker) speakers.push_back(i);
        // what each speaker could say: true for the truthful, false for liars
        std::vector<std::vector<Formula>> pool(static_cast<size_t>(n));
        for (int i : speakers)
            for (const Formula& f : candidates(scene, i, g.tier))
                if (f.eval(truth, scene) == (p.roles[static_cast<size_t>(i)] == Role::truthful)) pool[static_cast<size_t>(i)].push_back(f);
        int eye = 0;
        const int eye_most = g.tier == 0 ? 2 : 1;
        auto pick_for = [&](int i, const std::vector<Formula>& from) -> const Formula* {
            if (from.empty()) return nullptr;
            for (int tries = 0; tries < 30; ++tries) {
                const Formula& f = from[static_cast<size_t>(r.below(static_cast<int>(from.size())))];
                if (by_eye(f.kind) && eye >= eye_most) continue;
                const auto& mine = p.claims[static_cast<size_t>(i)];
                if (std::find(mine.begin(), mine.end(), f) != mine.end()) continue;
                return &f;
            }
            return nullptr;
        };
        // one claim each to begin
        bool ok = true;
        for (int i : speakers) {
            const Formula* f = pick_for(i, pool[static_cast<size_t>(i)]);
            if (!f) { ok = false; break; }
            eye += by_eye(f->kind);
            p.claims[static_cast<size_t>(i)].push_back(*f);
        }
        if (!ok) continue;
        // then claims that narrow the suspects, until one is left (or, with a question
        // bird, two or three, for the question to settle)
        const size_t goal_most = p.asker >= 0 ? 3 : 1;
        for (int step = 0; step < 14; ++step) {
            const std::vector<int> c = suspects(p);
            if (c.size() <= goal_most) break;
            // the claim (to a speaker with room) that leaves fewest suspects, among a sample
            int best_i = -1;
            Formula best_f;
            size_t best_c = c.size();
            for (int s = 0; s < 40; ++s) {
                const int i = speakers[static_cast<size_t>(r.below(static_cast<int>(speakers.size())))];
                if (p.claims[static_cast<size_t>(i)].size() >= 3) continue;
                const Formula* f = pick_for(i, pool[static_cast<size_t>(i)]);
                if (!f) continue;
                p.claims[static_cast<size_t>(i)].push_back(*f);
                const size_t left = suspects(p).size();
                p.claims[static_cast<size_t>(i)].pop_back();
                const bool fine = p.asker < 0 || left >= 2;
                if (fine && (left < best_c || (left == best_c && r.u() < .3))) {
                    best_c = left;
                    best_i = i;
                    best_f = *f;
                }
            }
            if (best_i < 0) break;
            eye += by_eye(best_f.kind);
            p.claims[static_cast<size_t>(best_i)].push_back(best_f);
        }
        std::vector<int> c = suspects(p);
        if (p.asker < 0 ? c.size() != 1 : (c.size() < 2 || c.size() > 3)) continue;
        // everyone who speaks says two things at least; padding never narrows a question
        // table's suspects (it cannot widen anyone's)
        for (int i : speakers)
            for (int s = 0; s < 30 && p.claims[static_cast<size_t>(i)].size() < 2; ++s) {
                const Formula* f = pick_for(i, pool[static_cast<size_t>(i)]);
                if (!f) continue;
                p.claims[static_cast<size_t>(i)].push_back(*f);
                if (p.asker >= 0 && suspects(p).size() < 2) p.claims[static_cast<size_t>(i)].pop_back();
                else eye += by_eye(f->kind);
            }
        bool two = true;
        for (int i : speakers) two = two && p.claims[static_cast<size_t>(i)].size() >= 2;
        if (!two) continue;
        // the questions: exactly one settles it
        if (p.asker >= 0) {
            std::vector<Formula> good, bad;
            for (const Formula& f : candidates(scene, -1, std::max(1, g.tier))) {
                if (by_eye(f.kind) || f.kind == Kind::not_me) continue;
                p.offers = {f};
                const Asked a{0, answer(p, 0, truth)};
                const size_t left = suspects(p, &a).size();
                if (left == 1) good.push_back(f);
                else if (left >= 2) bad.push_back(f);
            }
            if (good.empty() || static_cast<int>(bad.size()) < g.choices - 1) continue;
            p.offers.clear();
            p.offers.push_back(good[static_cast<size_t>(r.below(static_cast<int>(good.size())))]);
            while (static_cast<int>(p.offers.size()) < g.choices) {
                const Formula& f = bad[static_cast<size_t>(r.below(static_cast<int>(bad.size())))];
                if (std::find(p.offers.begin(), p.offers.end(), f) == p.offers.end()) p.offers.push_back(f);
            }
            for (int i = static_cast<int>(p.offers.size()) - 1; i > 0; --i) std::swap(p.offers[static_cast<size_t>(i)], p.offers[static_cast<size_t>(r.below(i + 1))]);
            for (size_t k = 0; k < p.offers.size(); ++k) {
                const Asked a{static_cast<int>(k), answer(p, static_cast<int>(k), truth)};
                if (suspects(p, &a).size() == 1) p.good = static_cast<int>(k);
            }
        }
        // effort: claims to weigh, worlds to rule out, lies to see through
        int claims = 0;
        for (const auto& cl : p.claims) claims += static_cast<int>(cl.size());
        p.difficulty = claims * 3 + static_cast<int>(consistent(p).size()) + liars * 4 + (p.asker >= 0 ? 8 : 0) + n;
        return p;
    }
    return best;
}

}  // namespace pt
