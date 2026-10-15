#include "perching.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace pt {

namespace {
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ULL + 0x632BE59BD9B4E019ULL) {}
    std::uint64_t next() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 0x2545F4914F6CDD1DULL;
    }
    double u() { return static_cast<double>(next() >> 11) / 9007199254740992.0; }
    double in(double a, double b) { return a + (b - a) * u(); }
    int below(int n) { return n > 0 ? static_cast<int>(next() % static_cast<std::uint64_t>(n)) : 0; }
};

void add_bar(Layout& L, V3 a, V3 b, Stuff stuff, int places) {
    const int bar = static_cast<int>(L.bars.size());
    L.bars.push_back({a, b, stuff, static_cast<int>(L.stands.size()) - 1});
    for (int k = 0; k < places; ++k) {
        const double f = (k + .5) / places;
        Place p;
        p.pos = a + (b - a) * f;
        p.pos.z += .035;  // on top of the bar
        p.bar = bar;
        p.stuff = stuff;
        L.places.push_back(p);
    }
}

int neighbour_count(const Layout& L, const std::vector<int>& at, int i) {
    int n = 0;
    for (size_t j = 0; j < at.size(); ++j)
        if (static_cast<int>(j) != i && L.neighbours(at[static_cast<size_t>(i)], at[j])) ++n;
    return n;
}
bool red_beside(const Layout& L, const std::vector<Bird>& birds, const std::vector<int>& at, int i) {
    for (size_t j = 0; j < at.size(); ++j)
        if (static_cast<int>(j) != i && birds[j].red && L.neighbours(at[static_cast<size_t>(i)], at[j])) return true;
    return false;
}
bool holds(const Layout& L, const std::vector<Bird>& birds, const std::vector<int>& at, int i, Liking l) {
    const int p = at[static_cast<size_t>(i)];
    const Place& pl = L.places[static_cast<size_t>(p)];
    switch (l) {
    case Liking::high: return L.high(p);
    case Liking::low: return L.low(p);
    case Liking::wood: return pl.stuff == Stuff::wood;
    case Liking::rope: return pl.stuff == Stuff::rope;
    case Liking::brass: return pl.stuff == Stuff::brass;
    case Liking::iron: return pl.stuff == Stuff::iron;
    case Liking::stone: return pl.stuff == Stuff::stone;
    case Liking::shade: return pl.shade;
    case Liking::sun: return !pl.shade;
    case Liking::company: return neighbour_count(L, at, i) >= 1;
    case Liking::one_neighbour: return neighbour_count(L, at, i) == 1;
    case Liking::alone: return neighbour_count(L, at, i) == 0;
    case Liking::no_red_beside: return !red_beside(L, birds, at, i);
    }
    return false;
}
// Likings in the same family say the same sort of thing; a bird has at most one of each.
int family(Liking l) {
    switch (l) {
    case Liking::high: case Liking::low: return 0;
    case Liking::wood: case Liking::rope: case Liking::brass: case Liking::iron: case Liking::stone: return 1;
    case Liking::shade: case Liking::sun: return 2;
    case Liking::company: case Liking::one_neighbour: case Liking::alone: return 3;
    case Liking::no_red_beside: return 4;
    }
    return 5;
}
}  // namespace

const char* stuff_name(Stuff s) {
    switch (s) {
    case Stuff::wood: return "wood";
    case Stuff::rope: return "rope";
    case Stuff::brass: return "brass";
    case Stuff::iron: return "iron";
    case Stuff::stone: return "stone";
    }
    return "wood";
}

const char* liking_name(Liking l) {
    switch (l) {
    case Liking::high: return "likes to sit high";
    case Liking::low: return "keeps near the floor";
    case Liking::wood: return "prefers wood";
    case Liking::rope: return "prefers rope";
    case Liking::brass: return "likes the brass";
    case Liking::iron: return "likes the ironwork";
    case Liking::stone: return "likes stone";
    case Liking::shade: return "wants shade";
    case Liking::sun: return "wants the sun";
    case Liking::company: return "wants company";
    case Liking::one_neighbour: return "wants exactly one neighbour";
    case Liking::alone: return "wants a bar to itself";
    case Liking::no_red_beside: return "won't sit beside a red bird";
    }
    return "";
}

bool Layout::high(int p) const {
    return places[static_cast<size_t>(p)].rank * 3 >= static_cast<int>(places.size()) * 2;
}
bool Layout::low(int p) const {
    return places[static_cast<size_t>(p)].rank * 3 < static_cast<int>(places.size());
}

// Is p in a palm's shade: the line from it toward the sun passes under a crown?
// Would this tree stand between the player's eye and a bird at p: its trunk, or its crown?
static bool hides(const Canopy& c, V3 p) {
    const V3 e = eye_at();
    for (V3 target : {p + V3{0, 0, .4}, p + V3{0, 0, 1.0}})
        for (int k = 1; k < 60; ++k) {
            const double t = k / 60.0;
            const V3 q = e + (target - e) * t;
            if (len(target - q) < .5) break;
            // the crown: a flattened ball over the disc
            const double h = std::hypot(q.x - c.crown.x, q.y - c.crown.y) / c.radius, dz = (q.z - c.crown.z) / 1.3;
            if (h * h + dz * dz < 1) return true;
            // the trunk
            if (q.z < c.crown.z) {
                const double u = q.z / std::max(.1, c.crown.z);
                const V3 axis = c.foot + (c.crown - c.foot) * u;
                if (std::hypot(q.x - axis.x, q.y - axis.y) < .6) return true;
            }
        }
    return false;
}

static bool shaded(const Layout& L, V3 p) {
    const V3 sun = sun_dir();
    for (const Canopy& c : L.canopies) {
        if (p.z > c.crown.z - .3) continue;
        const double t = (c.crown.z - p.z) / sun.z;
        const V3 q = p + sun * t;
        if (std::hypot(q.x - c.crown.x, q.y - c.crown.y) < c.radius * .8) return true;
    }
    return false;
}

// A bar centred on `mid` along `dir` with room for `places` birds.
void bar_at(Layout& L, V3 mid, V3 dir, Stuff stuff, int places) {
    const double half = places * kSeatGap / 2;
    add_bar(L, mid - dir * half, mid + dir * half, stuff, places);
}

Layout make_layout(std::uint64_t seed) {
    Rng g(seed);
    Layout L;
    auto j = [&](double v, double by = .25) { return v + g.in(-by, by); };
    const bool flip = g.u() < .5;  // the house's left and right swap from table to table
    const double fx = flip ? -1 : 1;
    auto X = [&](double x) { return x * fx; };
    auto across = [&](double spread) {
        const double az = g.in(-spread, spread);
        return V3{std::cos(az), std::sin(az), 0};
    };
    // The bare perch tree, its branches reaching every way.
    {
        const V3 t{X(j(-3.0, .4)), j(-.4, .5), 0};
        L.stands.push_back({StandKind::tree, t, j(7.4, .4)});
        struct Branch { double az, z, out; int places; };
        const Branch br[] = {{-.5, j(2.3, .2), .5, 2}, {2.5, j(3.7, .2), .45, 1}, {.6, j(5.1, .2), .45, 1}, {-2.3, j(6.4, .2), .4, 1}};
        for (const Branch& b : br) {
            const double az = (fx > 0 ? b.az : 3.14159265358979 - b.az) + g.in(-.25, .25);
            const V3 dir{std::cos(az), std::sin(az), 0};
            const double length = b.places * kSeatGap;
            const V3 a = V3{t.x, t.y, b.z} + dir * b.out;
            add_bar(L, a, a + dir * length + V3{0, 0, .1}, Stuff::wood, b.places);
        }
    }
    // Bamboo poles, each with a crossbar at the top.
    L.stands.push_back({StandKind::bamboo, {X(j(2.9, .3)), j(-1.6, .4), 0}, j(3.6, .25)});
    bar_at(L, {L.stands.back().base.x, L.stands.back().base.y, L.stands.back().reach}, across(.5), Stuff::wood, 2);
    L.stands.push_back({StandKind::bamboo, {X(j(1.7, .3)), j(2.0, .4), 0}, j(6.0, .3)});
    bar_at(L, {L.stands.back().base.x, L.stands.back().base.y, L.stands.back().reach}, across(.5), Stuff::wood, 2);
    // An iron lamp standard at the clearing's edge, its crossarm holding two lanterns.
    L.stands.push_back({StandKind::lamp, {X(j(-1.1, .3)), j(2.4, .4), 0}, j(4.3, .2)});
    bar_at(L, {L.stands.back().base.x, L.stands.back().base.y, L.stands.back().reach}, across(.3), Stuff::iron, 2);
    // Rope swings from the ribs, at different depths and heights.
    L.stands.push_back({StandKind::swing, {X(j(.9, .4)), j(-2.2, .3), 0}, j(4.2, .25)});
    bar_at(L, {L.stands.back().base.x, L.stands.back().base.y, L.stands.back().reach}, {1, 0, 0}, Stuff::rope, 2);
    L.stands.push_back({StandKind::swing, {X(j(-.7, .4)), j(3.0, .4), 0}, j(6.9, .3)});
    bar_at(L, {L.stands.back().base.x, L.stands.back().base.y, L.stands.back().reach}, {1, 0, 0}, Stuff::rope, 2);
    // A brass hoop hung high.
    L.stands.push_back({StandKind::hoop, {X(j(.3, .4)), j(.4, .4), 0}, j(8.2, .3)});
    bar_at(L, {L.stands.back().base.x, L.stands.back().base.y, L.stands.back().reach}, {1, 0, 0}, Stuff::brass, 1);
    // The fountain: two places on the near side of its rim.
    {
        const V3 c{X(j(-.4, .3)), j(5.0, .4), 0};
        L.stands.push_back({StandKind::fountain, c, 1.5});
        bar_at(L, {c.x, c.y - 1.36, .72}, {1, 0, 0}, Stuff::stone, 2);
    }
    // A stone bird bath in the clearing: two places on the near side of its bowl.
    {
        const V3 c{X(j(1.0, .3)), j(-.2, .3), 0};
        L.stands.push_back({StandKind::bath, c, .95});
        bar_at(L, {c.x, c.y - .62, 1.02}, {1, 0, 0}, Stuff::stone, 2);
    }
    // A driftwood log near the front.
    L.stands.push_back({StandKind::log, {X(j(-1.4, .4)), j(-1.8, .3), 0}, 1});
    bar_at(L, {L.stands.back().base.x, L.stands.back().base.y, .36}, across(.35), Stuff::wood, 2);
    // The jungle: broadleaf trees round about, their crowns closing over all but a
    // clearing in the middle, and palms among them. A tree never stands on a stand, and
    // never between the player and a bird's place: one that would is moved out, or left out.
    {
        struct Spot { double x, y, z, r; bool palm; };
        const Spot spots[] = {{-7.6, 1.2, 8.6, 4.0, false}, {-5.4, 7.6, 9.4, 4.2, false}, {-1.2, 9.6, 10.2, 4.0, false}, {3.4, 9.2, 9.0, 3.8, false},
                              {7.6, 3.2, 8.2, 4.0, false}, {-7.0, -4.8, 6.2, 3.2, false}, {6.6, -5.4, 5.8, 3.0, false}, {-6.0, 5.0, 10.6, 3.8, true},
                              {4.3, -3.0, 5.4, 2.6, true}, {6.6, 6.5, 11.4, 3.4, true}, {8.8, -1.4, 7.4, 3.0, true}};
        for (const Spot& sp : spots) {
            const V3 foot0{X(j(sp.x, .5)), j(sp.y, .5), 0};
            const V3 lean{g.in(-.8, .8), g.in(-.6, .6), j(sp.z, .5)};
            const double radius = j(sp.r, .3);
            for (int tries = 0; tries < 5; ++tries) {
                // out from the middle a little further each try, and a little smaller
                const double out = 1 + .12 * tries;
                const V3 foot{foot0.x * out, foot0.y * out + (tries ? 1.0 : 0), 0};
                const Canopy c{foot + lean, foot, radius * (1 - .08 * tries), sp.palm};
                bool ok = std::hypot(foot.x, foot.y) < kDomeR - 1.2;
                for (const Stand& st : L.stands)
                    if (std::hypot(foot.x - st.base.x, foot.y - st.base.y) < 1.6) ok = false;
                for (const Place& p : L.places)
                    if (ok && hides(c, p.pos)) ok = false;
                if (ok) {
                    L.canopies.push_back(c);
                    break;
                }
            }
        }
    }
    for (Place& p : L.places) p.shade = shaded(L, p.pos);
    std::vector<int> order(L.places.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return L.places[static_cast<size_t>(a)].pos.z < L.places[static_cast<size_t>(b)].pos.z; });
    for (size_t r = 0; r < order.size(); ++r) L.places[static_cast<size_t>(order[r])].rank = static_cast<int>(r);
    return L;
}

bool content(const Layout& L, const std::vector<Bird>& birds, const std::vector<int>& at, int i) {
    for (Liking l : birds[static_cast<size_t>(i)].likes)
        if (!holds(L, birds, at, i, l)) return false;
    return true;
}

std::vector<int> settle(const Layout& L, std::vector<Bird>& birds, std::uint64_t seed) {
    Rng g(seed ^ 0xA5A5F00DULL);
    const int n = static_cast<int>(birds.size());
    // The seating: the flock fills bars together, a bird or two perhaps on their own.
    std::vector<int> bars(L.bars.size());
    std::iota(bars.begin(), bars.end(), 0);
    for (int i = static_cast<int>(bars.size()) - 1; i > 0; --i) std::swap(bars[static_cast<size_t>(i)], bars[static_cast<size_t>(g.below(i + 1))]);
    std::vector<std::vector<int>> free(L.bars.size());
    for (size_t p = 0; p < L.places.size(); ++p) free[static_cast<size_t>(L.places[p].bar)].push_back(static_cast<int>(p));
    std::vector<int> seats;
    int loners = g.below(3);  // birds who will have a bar to themselves
    for (int bar : bars) {
        const int left = n - static_cast<int>(seats.size());
        if (left <= 0) break;
        std::vector<int>& f = free[static_cast<size_t>(bar)];
        for (int i = static_cast<int>(f.size()) - 1; i > 0; --i) std::swap(f[static_cast<size_t>(i)], f[static_cast<size_t>(g.below(i + 1))]);
        int take = static_cast<int>(f.size());
        if (take > 1 && loners > 0 && g.u() < .4) { take = 1; --loners; }
        take = std::min(take, left);
        seats.insert(seats.end(), f.begin(), f.begin() + take);
        f.erase(f.begin(), f.begin() + take);
    }
    for (std::vector<int>& f : free)  // only if the bars ran out
        while (!f.empty() && static_cast<int>(seats.size()) < n) { seats.push_back(f.back()); f.pop_back(); }
    // Who sits where, and their likings, true of that seating.
    std::vector<int> at(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) at[static_cast<size_t>(i)] = seats[static_cast<size_t>(i)];
    for (int i = n - 1; i > 0; --i) std::swap(at[static_cast<size_t>(i)], at[static_cast<size_t>(g.below(i + 1))]);
    const bool any_red = std::any_of(birds.begin(), birds.end(), [](const Bird& b) { return b.red; });
    for (int i = 0; i < n; ++i) {
        Bird& b = birds[static_cast<size_t>(i)];
        b.likes.clear();
        std::vector<Liking> true_ones;
        for (int l = 0; l <= static_cast<int>(Liking::no_red_beside); ++l) {
            const Liking k = static_cast<Liking>(l);
            if (k == Liking::no_red_beside && (!any_red || b.red)) continue;
            if (holds(L, birds, at, i, k)) true_ones.push_back(k);
        }
        for (int s = static_cast<int>(true_ones.size()) - 1; s > 0; --s) std::swap(true_ones[static_cast<size_t>(s)], true_ones[static_cast<size_t>(g.below(s + 1))]);
        const int want = 1 + g.below(3);
        std::vector<int> used;
        for (Liking k : true_ones) {
            if (static_cast<int>(b.likes.size()) >= want) break;
            if (std::find(used.begin(), used.end(), family(k)) != used.end()) continue;
            used.push_back(family(k));
            b.likes.push_back(k);
        }
    }
    return at;
}

std::vector<Landing> landing_order(const Layout& L, const std::vector<Bird>& birds, const std::vector<int>& at, std::uint64_t seed) {
    Rng g(seed ^ 0x5EA7ULL);
    const int n = static_cast<int>(birds.size());
    std::vector<Landing> out;
    std::vector<int> order(static_cast<size_t>(n));
    std::iota(order.begin(), order.end(), 0);
    for (int i = n - 1; i > 0; --i) std::swap(order[static_cast<size_t>(i)], order[static_cast<size_t>(g.below(i + 1))]);
    std::vector<bool> taken(L.places.size());
    for (int i : at) taken[static_cast<size_t>(i)] = true;
    for (int i : order) {
        Landing l{i, -1, at[static_cast<size_t>(i)]};
        // A few try somewhere they don't like first: a free place that breaks one of their likings.
        if (g.u() < .45) {
            std::vector<int> wrong;
            for (size_t p = 0; p < L.places.size(); ++p) {
                if (taken[p]) continue;
                std::vector<int> trial = at;
                trial[static_cast<size_t>(i)] = static_cast<int>(p);
                if (!content(L, birds, trial, i)) wrong.push_back(static_cast<int>(p));
            }
            if (!wrong.empty()) l.first_try = wrong[static_cast<size_t>(g.below(static_cast<int>(wrong.size())))];
        }
        out.push_back(l);
    }
    return out;
}

}  // namespace pt
