#include "field.hpp"

#include <algorithm>
#include <climits>

namespace sh {

namespace {
std::uint64_t mix64(std::uint64_t x) {
    x ^= x >> 33; x *= 0xFF51AFD7ED558CCDULL; x ^= x >> 33; x *= 0xC4CEB9FE1A85EC53ULL; x ^= x >> 33;
    return x;
}
// a hash of the meadow, for the sheep's tie-breaks: the same meadow always gets the same answer
std::uint64_t meadow_hash(const Meadow& m) {
    std::uint64_t h = m.salt * 0x9E3779B97F4A7C15ULL + static_cast<std::uint64_t>(m.sheep);
    for (size_t i = 0; i < m.cells.size(); ++i)
        if (m.cells[i] != Cell::grass) h = mix64(h ^ (i * 4 + static_cast<std::uint64_t>(m.cells[i])));
    return mix64(h);
}
// Work space for the searches, kept between calls: the solver runs thousands of them for
// every meadow it proves, and allocating each one cost more than the search.
// Fetched once per search: reaching a thread_local costs a call each time.
struct Space {
    std::vector<int> queue, mark;
    std::vector<int> edge_to, edge_cap, edge_next, node_head, node_from, node_edge;
};
Space& space() {
    thread_local Space s;
    return s;
}
}  // namespace

int Meadow::neighbours(int i, int out[6]) const {
    const int c = col(i), r = row(i);
    const int odd = r & 1;
    static constexpr int dc[2][6][2] = {{{-1, 0}, {1, 0}, {-1, -1}, {0, -1}, {-1, 1}, {0, 1}}, {{-1, 0}, {1, 0}, {0, -1}, {1, -1}, {0, 1}, {1, 1}}};
    int n = 0;
    for (const auto& d : dc[odd]) {
        const int cc = c + d[0], rr = r + d[1];
        if (cc >= 0 && rr >= 0 && cc < w && rr < h) out[n++] = idx(cc, rr);
    }
    return n;
}

std::vector<int> Meadow::distances() const {
    std::vector<int> d(static_cast<size_t>(w * h), -1);
    std::vector<int>& q = space().queue;
    q.clear();
    for (int i = 0; i < w * h; ++i)
        if (edge(i) && (open(i) || i == sheep)) { d[static_cast<size_t>(i)] = 0; q.push_back(i); }
    for (size_t k = 0; k < q.size(); ++k) {
        int nb[6];
        const int n = neighbours(q[k], nb);
        for (int j = 0; j < n; ++j) {
            const int v = nb[j];
            if (d[static_cast<size_t>(v)] < 0 && (open(v) || v == sheep)) { d[static_cast<size_t>(v)] = d[static_cast<size_t>(q[k])] + 1; q.push_back(v); }
        }
    }
    return d;
}

int Meadow::distance_out(int from) const {
    // the same steps distances() finds for `from`, searched outward from it alone: the
    // first open edge patch reached is the nearest
    if (!(open(from) || from == sheep)) return -1;
    Space& s = space();
    std::vector<int>& d = s.mark;
    d.assign(static_cast<size_t>(w * h), -1);
    std::vector<int>& q = s.queue;
    q.clear();
    d[static_cast<size_t>(from)] = 0;
    q.push_back(from);
    for (size_t k = 0; k < q.size(); ++k) {
        const int u = q[k];
        if (edge(u)) return d[static_cast<size_t>(u)];
        int nb[6];
        const int n = neighbours(u, nb);
        for (int j = 0; j < n; ++j) {
            const int v = nb[j];
            if (d[static_cast<size_t>(v)] < 0 && (open(v) || v == sheep)) { d[static_cast<size_t>(v)] = d[static_cast<size_t>(u)] + 1; q.push_back(v); }
        }
    }
    return -1;
}

std::vector<int> Meadow::routes(const std::vector<int>& d) const {
    // count shortest paths outward, from the edge in
    std::vector<int> order;
    for (int i = 0; i < w * h; ++i)
        if (d[static_cast<size_t>(i)] >= 0) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return d[static_cast<size_t>(a)] < d[static_cast<size_t>(b)]; });
    std::vector<int> rt(static_cast<size_t>(w * h), 0);
    for (int i : order) {
        const int di = d[static_cast<size_t>(i)];
        if (di == 0) { rt[static_cast<size_t>(i)] = 1; continue; }
        int nb[6];
        const int n = neighbours(i, nb);
        long long s = 0;
        for (int j = 0; j < n; ++j)
            if (d[static_cast<size_t>(nb[j])] == di - 1) s += rt[static_cast<size_t>(nb[j])];
        rt[static_cast<size_t>(i)] = static_cast<int>(std::min<long long>(s, 1000000));
    }
    return rt;
}

bool Meadow::penned() const { return distance_out(sheep) < 0; }

const char* smarts_name(Smarts s) {
    switch (s) {
        case Smarts::dozy: return "dozy";
        case Smarts::clever: return "clever";
        case Smarts::cunning: return "cunning";
    }
    return "?";
}

SheepMove sheep_choice(const Meadow& m) {
    SheepMove mv;
    if (m.munching) { mv.kind = SheepMove::munch; mv.to = m.sheep; return mv; }
    const auto d = m.distances();
    if (d[static_cast<size_t>(m.sheep)] == 0) { mv.kind = SheepMove::escape; mv.to = m.sheep; return mv; }
    int nb[6];
    const int n = m.neighbours(m.sheep, nb);
    std::vector<int> free;
    for (int j = 0; j < n; ++j) if (m.open(nb[j])) free.push_back(nb[j]);
    if (free.empty() || d[static_cast<size_t>(m.sheep)] < 0) {
        // walled in: it may still shuffle about inside its pen, but it's beaten
        mv.kind = SheepMove::stuck;
        mv.to = m.sheep;
        return mv;
    }
    const std::uint64_t h = meadow_hash(m);
    auto tie = [&](int cell) { return mix64(h ^ static_cast<std::uint64_t>(cell) * 0x632BE59BD9B4E019ULL); };
    int best_d = INT_MAX;
    for (int c : free) if (d[static_cast<size_t>(c)] >= 0) best_d = std::min(best_d, d[static_cast<size_t>(c)]);
    // a clover right beside it: misled, every time (the cunning sheep only if it costs at most a step)
    for (int c : free)
        if (m.cells[static_cast<size_t>(c)] == Cell::clover && d[static_cast<size_t>(c)] >= 0 &&
            (m.smarts != Smarts::cunning || d[static_cast<size_t>(c)] <= best_d + 1)) { mv.to = c; return mv; }
    const auto rt = m.routes(d);
    auto free_count = [&](int c) { int nn[6]; const int k = m.neighbours(c, nn); int f = 0; for (int j = 0; j < k; ++j) f += m.open(nn[j]); return f; };
    int pick = -1;
    if (m.smarts == Smarts::dozy) {
        // the dozy sheep dawdles: now and then it takes any way that's no more than a step longer
        std::vector<int> ok;
        const bool dawdle = (h >> 7) % 4 == 0;
        for (int c : free)
            if (d[static_cast<size_t>(c)] >= 0 && d[static_cast<size_t>(c)] <= best_d + (dawdle ? 1 : 0)) ok.push_back(c);
        std::sort(ok.begin(), ok.end(), [&](int a, int b) { return tie(a) < tie(b); });
        pick = ok.empty() ? -1 : ok.front();
    } else if (m.smarts == Smarts::clever) {
        // the shortest way, and of those the one with the most routes onward
        long long bs = LLONG_MIN;
        for (int c : free) {
            if (d[static_cast<size_t>(c)] != best_d) continue;
            const long long s = static_cast<long long>(std::min(rt[static_cast<size_t>(c)], 999)) * 1000 + free_count(c) * 10 + static_cast<long long>(tie(c) % 10);
            if (s > bs) { bs = s; pick = c; }
        }
    } else {
        // the cunning sheep asks of each step: if they then put a stone in the worst place, how far am I from out?
        long long bs = LLONG_MIN;
        for (int c : free) {
            if (d[static_cast<size_t>(c)] < 0 || d[static_cast<size_t>(c)] > best_d + 1) continue;
            Meadow t = m;
            t.sheep = c;
            if (t.cells[static_cast<size_t>(c)] == Cell::clover) t.cells[static_cast<size_t>(c)] = Cell::grass;
            const auto td = t.distances();
            int worst = td[static_cast<size_t>(c)];
            // the stones that matter: those on a shortest way out from c, near it
            std::vector<int> cand;
            for (int i = 0; i < t.w * t.h; ++i)
                if (t.can_place(i) && td[static_cast<size_t>(i)] >= 0 && td[static_cast<size_t>(i)] < td[static_cast<size_t>(c)] && td[static_cast<size_t>(i)] >= td[static_cast<size_t>(c)] - 2) cand.push_back(i);
            for (int b : cand) {
                t.cells[static_cast<size_t>(b)] = Cell::stone;
                const int nd = t.distance_out(c);
                t.cells[static_cast<size_t>(b)] = Cell::grass;
                if (nd < 0) { worst = 1000; break; }
                worst = std::max(worst, nd);
            }
            const long long s = -static_cast<long long>(worst) * 100000 - d[static_cast<size_t>(c)] * 1000 + std::min(rt[static_cast<size_t>(c)], 99) * 10 + static_cast<long long>(tie(c) % 10);
            if (s > bs) { bs = s; pick = c; }
        }
    }
    if (pick < 0) { mv.kind = SheepMove::stuck; mv.to = m.sheep; return mv; }
    mv.to = pick;
    return mv;
}

void sheep_apply(Meadow& m, const SheepMove& mv) {
    switch (mv.kind) {
        case SheepMove::munch: m.munching = false; break;
        case SheepMove::escape: m.escaped = true; break;
        case SheepMove::stuck: break;
        case SheepMove::step:
            m.sheep = mv.to;
            if (m.edge(mv.to)) { m.escaped = true; break; }  // it reached the edge: it walks straight off
            if (m.cells[static_cast<size_t>(mv.to)] == Cell::clover) {
                m.cells[static_cast<size_t>(mv.to)] = Cell::grass;
                m.munching = true;
            }
            break;
    }
}

bool place(Meadow& m, int cell, SheepMove* answer) {
    if (m.escaped || !m.can_place(cell)) return false;
    m.cells[static_cast<size_t>(cell)] = Cell::stone;
    ++m.stones;
    SheepMove mv;
    mv.kind = SheepMove::stuck;
    mv.to = m.sheep;
    if (m.head > 0) {
        --m.head;
        mv.kind = SheepMove::munch;  // it waits, watching
        if (m.penned()) mv.kind = SheepMove::stuck;
    } else if (m.penned()) {
        mv.kind = SheepMove::stuck;
    } else {
        mv = sheep_choice(m);
        sheep_apply(m, mv);
    }
    if (answer) *answer = mv;
    return true;
}

namespace {
// hex distance between two cells (offset rows to cube coordinates)
int hex_dist(const Meadow& m, int a, int b) {
    auto cube = [&](int i, int& x, int& y, int& z) { const int c = m.col(i), r = m.row(i); x = c - (r - (r & 1)) / 2; z = r; y = -x - z; };
    int ax, ay, az, bx, by, bz;
    cube(a, ax, ay, az);
    cube(b, bx, by, bz);
    return (std::abs(ax - bx) + std::abs(ay - by) + std::abs(az - bz)) / 2;
}
}  // namespace

int cut_size(const Meadow& m) {
    // the fewest stones that would wall the sheep in: unit vertex capacities, sheep to "outside" (beyond the edge cells)
    if (m.edge(m.sheep)) return 100;
    const int n = m.w * m.h;
    // node i_in = 2i, i_out = 2i+1; outside = 2n. Edges in flat lists (each with its
    // reverse at index ^ 1), so a search allocates nothing.
    const int N = 2 * n + 1, T = 2 * n;
    Space& s = space();
    std::vector<int>& edge_to = s.edge_to;
    std::vector<int>& edge_cap = s.edge_cap;
    std::vector<int>& edge_next = s.edge_next;
    std::vector<int>& node_head = s.node_head;
    // at most 8 edges leave a patch (in to out, six neighbours, off the edge), each with its reverse
    const size_t most = static_cast<size_t>(16 * n);
    if (edge_to.size() < most) {
        edge_to.resize(most);
        edge_cap.resize(most);
        edge_next.resize(most);
    }
    node_head.assign(static_cast<size_t>(N), -1);
    int* const to = edge_to.data();
    int* const cap = edge_cap.data();
    int* const next = edge_next.data();
    int* const head = node_head.data();
    int edges = 0;
    auto add = [&](int u, int v, int c) {
        to[edges] = v; cap[edges] = c; next[edges] = head[u]; head[u] = edges++;
        to[edges] = u; cap[edges] = 0; next[edges] = head[v]; head[v] = edges++;
    };
    for (int i = 0; i < n; ++i) {
        if (!(m.open(i) || i == m.sheep)) continue;
        add(2 * i, 2 * i + 1, i == m.sheep ? 100 : 1);
        int nb[6];
        const int k = m.neighbours(i, nb);
        for (int j = 0; j < k; ++j)
            if (m.open(nb[j]) || nb[j] == m.sheep) add(2 * i + 1, 2 * nb[j], 100);
        if (m.edge(i)) add(2 * i + 1, T, 100);
    }
    const int S = 2 * m.sheep + 1;
    int flow = 0;
    std::vector<int>& pu = s.node_from;
    std::vector<int>& pe = s.node_edge;
    pe.resize(static_cast<size_t>(N));
    std::vector<int>& q = s.queue;
    while (flow < 7) {
        pu.assign(static_cast<size_t>(N), -1);
        q.clear();
        q.push_back(S);
        pu[static_cast<size_t>(S)] = S;
        for (size_t h = 0; h < q.size() && pu[static_cast<size_t>(T)] < 0; ++h) {
            const int u = q[h];
            for (int e = head[u]; e >= 0; e = next[e]) {
                const int v = to[e];
                if (pu[static_cast<size_t>(v)] < 0 && cap[e] > 0) { pu[static_cast<size_t>(v)] = u; pe[static_cast<size_t>(v)] = e; q.push_back(v); }
            }
        }
        if (pu[static_cast<size_t>(T)] < 0) break;
        for (int v = T; v != S; v = pu[static_cast<size_t>(v)]) {
            const int e = pe[static_cast<size_t>(v)];
            --cap[e];
            ++cap[e ^ 1];
        }
        ++flow;
    }
    return flow;
}

namespace {
// how good a meadow is for you: penned is best; otherwise few stones short of a wall, and the sheep far from out
long long judge(const Meadow& m) {
    if (m.escaped) return LLONG_MIN / 2;
    const auto d = m.distances();
    const int dp = d[static_cast<size_t>(m.sheep)];
    if (dp < 0) return LLONG_MAX / 2 - m.stones;
    const int cut = cut_size(m);
    const auto rt = m.routes(d);
    // the race: the sheep needs dp steps; you need `cut` stones (a munching sheep gives you one)
    const int slack = dp + (m.munching ? 1 : 0) - cut;
    return static_cast<long long>(slack) * 100000 - cut * 3000 + dp * 500 - std::min(rt[static_cast<size_t>(m.sheep)], 99) * 5;
}

std::vector<int> candidates(const Meadow& m, int radius) {
    const auto d = m.distances();
    const int dp = d[static_cast<size_t>(m.sheep)];
    std::vector<int> c;
    for (int i = 0; i < m.w * m.h; ++i) {
        if (!m.can_place(i)) continue;
        if (hex_dist(m, i, m.sheep) > radius) continue;
        if (dp >= 0 && (d[static_cast<size_t>(i)] < 0 || d[static_cast<size_t>(i)] >= dp + 1)) continue;
        c.push_back(i);
    }
    if (c.empty())
        for (int i = 0; i < m.w * m.h; ++i) if (m.can_place(i) && hex_dist(m, i, m.sheep) <= radius + 2) c.push_back(i);
    if (c.empty())
        for (int i = 0; i < m.w * m.h; ++i) if (m.can_place(i)) { c.push_back(i); break; }
    return c;
}
}  // namespace

int bot_stone(const Meadow& m) {
    const auto cand = candidates(m, 4);
    if (cand.empty()) return -1;
    // first look: every candidate, the sheep's answer, judged
    std::vector<std::pair<long long, int>> first;
    for (int c : cand) {
        Meadow t = m;
        place(t, c);
        first.push_back({judge(t), c});
    }
    std::sort(first.begin(), first.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if (first.front().first >= LLONG_MAX / 4) return first.front().second;  // pens it now
    // second look, at the most promising few: the best follow-up after the sheep's answer
    long long bs = LLONG_MIN;
    int best = first.front().second;
    for (size_t k = 0; k < first.size() && k < 8; ++k) {
        const int c = first[k].second;
        Meadow t = m;
        place(t, c);
        long long s = first[k].first;
        if (!t.escaped && !t.penned()) {
            long long s2 = LLONG_MIN;
            for (int c2 : candidates(t, 3)) {
                Meadow u = t;
                place(u, c2);
                s2 = std::max(s2, judge(u));
            }
            if (s2 > LLONG_MIN) s = s2 + s / 8;
        }
        if (s > bs) { bs = s; best = c; }
    }
    return best;
}

LevelParams params_for(int level) {
    LevelParams p;
    level = std::max(1, level);
    p.size = level <= 4 ? 9 : 11;
    p.smarts = level <= 6 ? Smarts::dozy : level <= 18 ? Smarts::clever : Smarts::cunning;
    // rocks thin out as the levels climb, and come back a little with each new kind of sheep
    const int phase = level <= 6 ? level : level <= 18 ? level - 6 : level - 18;
    p.rocks = std::max(4, (p.size == 9 ? 12 : 16) - phase / 2);
    p.clovers = level >= 5 && level % 3 == 2 ? 1 + (level >= 20 ? 1 : 0) : 0;
    p.seed = static_cast<std::uint64_t>(level) * 7919 + 17;
    return p;
}

LevelParams params_for_difficulty(int difficulty, std::uint64_t seed) {
    LevelParams p;
    Rng r(seed);
    switch (std::clamp(difficulty, 0, 2)) {
    case 0:
        p.size = 9;
        p.smarts = Smarts::dozy;
        p.rocks = 10 + r.range(3);
        p.clovers = 0;
        break;
    case 1:
        p.size = 11;
        p.smarts = Smarts::clever;
        p.rocks = 11 + r.range(4);
        p.clovers = r.range(2);
        break;
    default:
        p.size = 11;
        p.smarts = Smarts::cunning;
        p.rocks = 7 + r.range(4);
        p.clovers = 1 + r.range(2);
        break;
    }
    p.seed = seed;
    return p;
}

Level generate(const LevelParams& p) {
    for (std::uint64_t attempt = 0;; ++attempt) {
        if (attempt == 120) {
            // a sheep too slippery for this many rocks: scatter a couple more and try again
            LevelParams q = p;
            q.rocks += 2;
            q.seed = p.seed * 31 + 7;
            Level lv = generate(q);
            lv.params = p;
            return lv;
        }
        Rng r(p.seed * 131 + attempt);
        Meadow m;
        m.w = m.h = p.size;
        m.cells.assign(static_cast<size_t>(m.w * m.h), Cell::grass);
        m.sheep = m.idx(m.w / 2, m.h / 2);
        m.smarts = p.smarts;
        m.salt = r.next() | 1;
        // rocks scattered, never right next to the sheep
        int nb[6];
        const int nn = m.neighbours(m.sheep, nb);
        auto near_sheep = [&](int i) { if (i == m.sheep) return true; for (int j = 0; j < nn; ++j) if (nb[j] == i) return true; return false; };
        for (int k = 0; k < p.rocks; ++k) {
            int i;
            int guard = 0;
            do i = r.range(m.w * m.h); while ((near_sheep(i) || m.cells[static_cast<size_t>(i)] != Cell::grass) && ++guard < 200);
            if (guard < 200) m.cells[static_cast<size_t>(i)] = Cell::rock;
        }
        // clovers a few steps from the sheep: bait
        for (int k = 0; k < p.clovers; ++k) {
            const auto d = m.distances();
            std::vector<int> spots;
            for (int i = 0; i < m.w * m.h; ++i) {
                const int dc = std::abs(m.col(i) - m.col(m.sheep)), dr = std::abs(m.row(i) - m.row(m.sheep));
                if (m.cells[static_cast<size_t>(i)] == Cell::grass && !near_sheep(i) && dc + dr >= 2 && dc + dr <= 4 && d[static_cast<size_t>(i)] > 0) spots.push_back(i);
            }
            if (!spots.empty()) m.cells[static_cast<size_t>(spots[static_cast<size_t>(r.range(static_cast<int>(spots.size())))])] = Cell::clover;
        }
        if (m.penned()) continue;
        // the proof: the bot must pen the sheep
        Level lv;
        lv.start = m;
        lv.params = p;
        Meadow t = m;
        bool won = false;
        for (int guard = 0; guard < m.w * m.h; ++guard) {
            const int s = bot_stone(t);
            if (s < 0) break;
            SheepMove mv;
            place(t, s, &mv);
            lv.solution.push_back(s);
            if (t.escaped) break;
            if (t.penned()) { won = true; break; }
        }
        if (!won) continue;
        // and not too easy: the start must take some doing
        if (static_cast<int>(lv.solution.size()) < kHeadStart + 2) continue;
        lv.par = static_cast<int>(lv.solution.size());
        return lv;
    }
}

}  // namespace sh
