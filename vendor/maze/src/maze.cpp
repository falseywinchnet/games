#include "maze.hpp"

#include <algorithm>
#include <deque>
#include <unordered_map>

namespace mz {

namespace {
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ULL + 0x2545F4914F6CDD1DULL) { next(); next(); }
    std::uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    int range(int n) { return n <= 1 ? 0 : static_cast<int>(next() % static_cast<std::uint64_t>(n)); }
    double unit() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
};

// A perfect maze by depth-first carving, then some dead ends knocked through into loops.
Floor carve(int cells, int root_x, int root_y, double braid, Rng& rng) {
    Floor f;
    f.w = f.h = 2 * cells + 1;
    f.cells.assign(static_cast<size_t>(f.w * f.h), Cell{});
    std::vector<std::pair<int, int>> st{{root_x, root_y}};
    f.at(root_x, root_y).block = Block::open;
    while (!st.empty()) {
        auto [x, y] = st.back();
        int dirs[4] = {0, 1, 2, 3};
        for (int i = 3; i > 0; --i) std::swap(dirs[i], dirs[rng.range(i + 1)]);
        bool moved = false;
        for (int d : dirs) {
            const int nx = x + 2 * kDX[d], ny = y + 2 * kDY[d];
            if (nx <= 0 || ny <= 0 || nx >= f.w - 1 || ny >= f.h - 1 || f.at(nx, ny).block == Block::open) continue;
            f.at(x + kDX[d], y + kDY[d]).block = Block::open;
            f.at(nx, ny).block = Block::open;
            st.push_back({nx, ny});
            moved = true;
            break;
        }
        if (!moved) st.pop_back();
    }
    for (int y = 1; y < f.h - 1; y += 2)
        for (int x = 1; x < f.w - 1; x += 2) {
            int exits = 0;
            for (int d = 0; d < 4; ++d) exits += f.open(x + kDX[d], y + kDY[d]);
            if (exits != 1 || rng.unit() > braid) continue;
            int dirs[4] = {0, 1, 2, 3};
            for (int i = 3; i > 0; --i) std::swap(dirs[i], dirs[rng.range(i + 1)]);
            for (int d : dirs) {
                const int wx = x + kDX[d], wy = y + kDY[d], nx = x + 2 * kDX[d], ny = y + 2 * kDY[d];
                if (nx <= 0 || ny <= 0 || nx >= f.w - 1 || ny >= f.h - 1 || f.open(wx, wy)) continue;
                f.at(wx, wy).block = Block::open;
                break;
            }
        }
    return f;
}

std::uint64_t key(const Pos& p, bool flipped, std::uint32_t pressed) {
    return static_cast<std::uint64_t>(p.f) << 40 | static_cast<std::uint64_t>(p.x) << 28 | static_cast<std::uint64_t>(p.y) << 16 |
           static_cast<std::uint64_t>(flipped) << 8 | pressed;
}

// Breadth-first over every square reachable with only `open_doors` open and
// nothing pressed or flipped (the generator's view of "the region before a door").
std::vector<Pos> region(const Level& lv, Pos from, std::uint32_t open_doors, bool portals = true) {
    std::vector<Pos> out;
    std::vector<std::vector<char>> seen(lv.floors.size());
    for (size_t i = 0; i < lv.floors.size(); ++i) seen[i].assign(lv.floors[i].cells.size(), 0);
    std::deque<Pos> q{from};
    seen[static_cast<size_t>(from.f)][static_cast<size_t>(from.y * lv.floors[static_cast<size_t>(from.f)].w + from.x)] = 1;
    while (!q.empty()) {
        const Pos p = q.front();
        q.pop_front();
        out.push_back(p);
        auto visit = [&](Pos n) {
            if (n.f < 0 || n.f >= static_cast<int>(lv.floors.size())) return;
            const Floor& fl = lv.floors[static_cast<size_t>(n.f)];
            if (!fl.in(n.x, n.y)) return;
            const Cell& c = fl.at(n.x, n.y);
            if (c.block == Block::wall || (c.block == Block::door && !(open_doors >> c.door & 1))) return;
            char& s = seen[static_cast<size_t>(n.f)][static_cast<size_t>(n.y * fl.w + n.x)];
            if (s) return;
            s = 1;
            q.push_back(n);
        };
        for (int d = 0; d < 4; ++d) visit({p.f, p.x + kDX[d], p.y + kDY[d]});
        const Cell& c = lv.cell(p);
        if (c.elevator) visit({1 - p.f, p.x, p.y});
        if (portals && lv.has_portal(p)) visit(lv.portal_twin(p));
    }
    return out;
}

// The shortest path from a to b treating every door as open (doors are placed on it).
std::vector<Pos> path(const Level& lv, Pos a, Pos b) {
    std::unordered_map<std::uint64_t, std::uint64_t> prev;
    auto k = [](Pos p) { return static_cast<std::uint64_t>(p.f) << 32 | static_cast<std::uint64_t>(p.x) << 16 | static_cast<std::uint64_t>(p.y); };
    std::deque<Pos> q{a};
    prev[k(a)] = k(a);
    while (!q.empty()) {
        const Pos p = q.front();
        q.pop_front();
        if (p == b) break;
        auto visit = [&](Pos n) {
            if (n.f < 0 || n.f >= static_cast<int>(lv.floors.size()) || !lv.floors[static_cast<size_t>(n.f)].in(n.x, n.y)) return;
            if (lv.cell(n).block == Block::wall || prev.count(k(n))) return;
            prev[k(n)] = k(p);
            q.push_back(n);
        };
        for (int d = 0; d < 4; ++d) visit({p.f, p.x + kDX[d], p.y + kDY[d]});
        if (lv.cell(p).elevator) visit({1 - p.f, p.x, p.y});
        if (lv.has_portal(p)) visit(lv.portal_twin(p));
    }
    std::vector<Pos> out;
    if (!prev.count(k(b))) return out;
    for (std::uint64_t c = k(b);; c = prev[c]) {
        out.push_back({static_cast<int>(c >> 32), static_cast<int>((c >> 16) & 0xFFFF), static_cast<int>(c & 0xFFFF)});
        if (c == k(a)) break;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

bool is_dead_end(const Level& lv, Pos p) {
    if (!lv.open(p)) return false;
    int n = 0;
    for (int d = 0; d < 4; ++d) n += lv.cell(p).block != Block::wall && lv.floors[static_cast<size_t>(p.f)].in(p.x + kDX[d], p.y + kDY[d]) &&
                                    lv.floors[static_cast<size_t>(p.f)].at(p.x + kDX[d], p.y + kDY[d]).block != Block::wall;
    return n == 1;
}
bool plain(const Level& lv, Pos p) {
    if (!lv.open(p) || p == lv.start || p == lv.goal) return false;
    const Cell& c = lv.cell(p);
    return c.pad < 0 && c.cpad < 0 && c.portal < 0 && !c.elevator && !c.goal && lv.thing_at(p) < 0;
}
}  // namespace

bool Level::has_portal(Pos p) const { return open(p) && cell(p).portal >= 0; }
Pos Level::portal_twin(Pos p) const {
    const auto& pr = portals[static_cast<size_t>(cell(p).portal)];
    return pr.first == p ? pr.second : pr.first;
}
int Level::thing_at(Pos p) const {
    for (size_t i = 0; i < things.size(); ++i)
        if (things[i].at == p) return static_cast<int>(i);
    return -1;
}

bool passable(const Level& lv, const Play& play, Pos p) {
    if (p.f < 0 || p.f >= static_cast<int>(lv.floors.size()) || !lv.floors[static_cast<size_t>(p.f)].in(p.x, p.y)) return false;
    const Cell& c = lv.cell(p);
    if (c.block == Block::open) return true;
    return c.block == Block::door && play.door_open(c.door);
}

StepResult step(const Level& lv, Play& play, int d, Arrival& arr) {
    arr = Arrival{};
    const Pos to{play.at.f, play.at.x + kDX[d], play.at.y + kDY[d]};
    if (!lv.floors[static_cast<size_t>(to.f)].in(to.x, to.y)) return StepResult::blocked_wall;
    const Cell& c = lv.cell(to);
    if (c.block == Block::wall) return StepResult::blocked_wall;
    if (c.block == Block::door && !play.door_open(c.door)) return StepResult::blocked_door;
    play.at = to;
    ++play.steps;
    // pads underfoot: on the floor, or overhead when the world is upside down
    const int pad = play.flipped ? c.cpad : c.pad;
    if (pad >= 0 && !(play.pressed >> pad & 1)) { play.pressed |= 1u << pad; arr.pressed = true; arr.pad = pad; }
    const int th = lv.thing_at(to);
    if (th >= 0) {
        if (lv.things[static_cast<size_t>(th)].kind == ThingKind::flip) { play.flipped = !play.flipped; arr.flipped = true; }
        else { play.blackout = !play.blackout; arr.bulb = true; }
    }
    // floor fixtures only work underfoot
    if (!play.flipped && !arr.flipped) {
        if (c.elevator && lv.floors.size() > 1) { arr.elevator = true; arr.from_floor = to.f; play.at = {1 - to.f, to.x, to.y}; }
        else if (c.portal >= 0) { arr.portal = true; arr.portal_from = to; play.at = lv.portal_twin(to); }
    }
    if (play.at == lv.goal) { play.won = true; arr.won = true; play.blackout = false; }
    return StepResult::moved;
}

int solve_steps(const Level& lv) {
    Play p0;
    p0.at = lv.start;
    std::unordered_map<std::uint64_t, int> dist;
    std::deque<Play> q{p0};
    dist[key(p0.at, false, 0)] = 0;
    while (!q.empty()) {
        Play p = q.front();
        q.pop_front();
        const int dd = dist[key(p.at, p.flipped, p.pressed)];
        for (int d = 0; d < 4; ++d) {
            Play n = p;
            Arrival arr;
            if (step(lv, n, d, arr) != StepResult::moved) continue;
            if (n.won) return dd + 1;
            const std::uint64_t k = key(n.at, n.flipped, n.pressed);
            if (dist.count(k)) continue;
            dist[k] = dd + 1;
            q.push_back(n);
        }
    }
    return -1;
}

std::vector<int> solve_route(const Level& lv, const Play& start) {
    struct Node { Play p; int parent; int dir; };
    std::vector<Node> nodes{{start, -1, -1}};
    std::unordered_map<std::uint64_t, int> seen;
    seen[key(start.at, start.flipped, start.pressed)] = 0;
    for (size_t head = 0; head < nodes.size(); ++head) {
        for (int d = 0; d < 4; ++d) {
            Play n = nodes[head].p;
            Arrival arr;
            if (step(lv, n, d, arr) != StepResult::moved) continue;
            const std::uint64_t k = key(n.at, n.flipped, n.pressed);
            if (!n.won && seen.count(k)) continue;
            seen[k] = static_cast<int>(nodes.size());
            nodes.push_back({n, static_cast<int>(head), d});
            if (n.won) {
                std::vector<int> out;
                for (int i = static_cast<int>(nodes.size()) - 1; nodes[static_cast<size_t>(i)].parent >= 0; i = nodes[static_cast<size_t>(i)].parent) out.push_back(nodes[static_cast<size_t>(i)].dir);
                std::reverse(out.begin(), out.end());
                return out;
            }
        }
    }
    return {};
}

LevelParams params_for(int n, std::uint64_t seed) {
    // the maze grows, and each new trick arrives on its own before they start to mix
    LevelParams p;
    p.seed = seed;
    p.cells = std::min(13, 5 + n / 2);
    p.braid = .2 + .02 * std::min(10, n);
    if (n >= 2) p.doors = std::min(4, 1 + (n - 2) / 4);
    if (n >= 4) p.portals = n % 3 == 1 ? 1 : (n >= 9 ? 1 + (n % 2) : 0);
    if (n == 4 || (n >= 12 && n % 5 == 2)) { p.sealed_portal = true; p.doors = 0; p.ceiling_doors = 0; }  // a portal level: the portal is the puzzle
    if (n >= 6) p.marble = n % 2 == 0 || n == 6;
    if (n >= 8) p.bulbs = n % 4 == 0 ? 2 : (n % 4 == 1 ? 1 : 0);
    if (n >= 10) p.snail = n % 3 != 1;
    if (n >= 12) p.floors = n % 3 == 0 ? 2 : 1;
    if (n >= 15) p.ceiling_doors = std::min(p.doors, n % 4 == 3 ? 1 : (n >= 22 && n % 4 == 0 ? 2 : 0));
    if (n == 14) { p.doors = std::max(p.doors, 1); p.ceiling_doors = 1; p.sealed_portal = false; }  // the flip stone's first appearance
    if (p.sealed_portal) { p.doors = 0; p.ceiling_doors = 0; }
    if (n == 10) p.floors = 1;
    return p;
}

Level generate(const LevelParams& in) {
    for (std::uint64_t attempt = 0;; ++attempt) {
        Rng rng(in.seed * 1000003ULL + attempt);
        Level lv;
        lv.params = in;
        lv.carpet = rng.range(4);
        lv.ceiling = rng.range(3);
        const int n = in.cells;
        lv.floors.push_back(carve(n, 1, 1, in.braid, rng));
        lv.start = {0, 1, 1};
        lv.start_dir = lv.floors[0].open(1, 2) ? kN : kE;
        // a second maze directly above, joined by an elevator far from the start
        if (in.floors > 1) {
            std::vector<Pos> r0 = region(lv, lv.start, 0);
            Pos e = r0.back();  // breadth-first: the last is among the farthest
            for (int k = static_cast<int>(r0.size()) - 1; k >= 0; --k)
                if ((r0[static_cast<size_t>(k)].x % 2) && (r0[static_cast<size_t>(k)].y % 2)) { e = r0[static_cast<size_t>(k)]; break; }
            lv.floors.push_back(carve(n, e.x, e.y, in.braid, rng));
            lv.floors[0].at(e.x, e.y).elevator = true;
            lv.floors[1].at(e.x, e.y).elevator = true;
        }
        // the reward room, as far from the way in as the maze allows
        {
            const int gf = static_cast<int>(lv.floors.size()) - 1;
            Pos entry = gf == 0 ? lv.start : Pos{1, 0, 0};
            if (gf == 1)
                for (int y = 0; y < lv.floors[1].h; ++y)
                    for (int x = 0; x < lv.floors[1].w; ++x)
                        if (lv.floors[1].at(x, y).elevator) entry = {1, x, y};
            std::vector<Pos> r = region(lv, entry, 0);
            Pos g = r.back();
            for (int k = static_cast<int>(r.size()) - 1; k >= 0; --k) {
                const Pos q = r[static_cast<size_t>(k)];
                if (q.f == gf && q.x % 2 && q.y % 2 && !lv.cell(q).elevator) { g = q; break; }
            }
            Floor& fl = lv.floors[static_cast<size_t>(gf)];
            // open a 3x3 room around it (kept inside the outer wall)
            const int cx = std::clamp(g.x, 2, fl.w - 3), cy = std::clamp(g.y, 2, fl.h - 3);
            for (int y = cy - 1; y <= cy + 1; ++y)
                for (int x = cx - 1; x <= cx + 1; ++x) {
                    if (fl.at(x, y).elevator) continue;
                    fl.at(x, y).block = Block::open;
                    fl.at(x, y).goal = true;
                }
            lv.goal = {gf, cx, cy};
            if (lv.cell(lv.goal).elevator) continue;
        }
        // locked doors along the way, each with its pad somewhere it can be reached first
        std::vector<Pos> route = path(lv, lv.start, lv.goal);
        if (route.empty()) continue;
        bool ok = true;
        std::uint32_t opened = 0;
        int ceiling_left = in.ceiling_doors;
        std::vector<int> door_idx;
        for (int k = 0; k < in.doors; ++k) {
            // a corridor square on the route: open on exactly two opposite sides
            const int lo = static_cast<int>(route.size()) * (k + 1) / (in.doors + 2), hi = static_cast<int>(route.size()) * (k + 2) / (in.doors + 2);
            int pick = -1;
            for (int t = 0; t < 40 && pick < 0; ++t) {
                const int i = lo + rng.range(std::max(1, hi - lo));
                if (i <= 1 || i >= static_cast<int>(route.size()) - 2) continue;
                const Pos p = route[static_cast<size_t>(i)];
                if (!plain(lv, p) || route[static_cast<size_t>(i - 1)].f != p.f || route[static_cast<size_t>(i + 1)].f != p.f) continue;
                const Floor& fl = lv.floors[static_cast<size_t>(p.f)];
                const bool ns = fl.open(p.x, p.y + 1) && fl.open(p.x, p.y - 1) && !fl.open(p.x + 1, p.y) && !fl.open(p.x - 1, p.y);
                const bool ew = fl.open(p.x + 1, p.y) && fl.open(p.x - 1, p.y) && !fl.open(p.x, p.y + 1) && !fl.open(p.x, p.y - 1);
                if (ns || ew) pick = i;
            }
            if (pick < 0) { ok = false; break; }
            const Pos dp = route[static_cast<size_t>(pick)];
            Cell& dc = lv.cell(dp);
            dc.block = Block::door;
            dc.door = static_cast<std::int8_t>(k);
            door_idx.push_back(pick);
            // its pad: in what can be reached with the earlier doors open, preferably a dead end off the route
            std::vector<Pos> reach = region(lv, lv.start, opened);
            std::vector<Pos> cands;
            for (const Pos& q : reach)
                if (plain(lv, q) && std::find(route.begin(), route.end(), q) == route.end()) cands.push_back(q);
            std::vector<Pos> dead;
            for (const Pos& q : cands)
                if (is_dead_end(lv, q)) dead.push_back(q);
            const std::vector<Pos>& pool = !dead.empty() ? dead : cands;
            if (pool.empty()) { ok = false; break; }
            // farther along the pool (breadth-first order) is farther away: prefer the back half
            const Pos padp = pool[static_cast<size_t>(pool.size() / 2 + rng.range(static_cast<int>((pool.size() + 1) / 2)))];
            if (ceiling_left > 0) {
                lv.cell(padp).cpad = static_cast<std::int8_t>(k);
                --ceiling_left;
                // a flip stone somewhere reachable, so the ceiling can be walked
                std::vector<Pos> fc;
                for (const Pos& q : reach)
                    if (plain(lv, q) && !(q == padp) && std::find(route.begin(), route.end(), q) == route.end()) fc.push_back(q);
                if (fc.empty()) { ok = false; break; }
                lv.things.push_back({ThingKind::flip, fc[static_cast<size_t>(rng.range(static_cast<int>(fc.size())))]});
            } else {
                lv.cell(padp).pad = static_cast<std::int8_t>(k);
            }
            opened |= 1u << k;
        }
        if (!ok) continue;
        // a portal pair for the shortcut-minded
        auto plain_cells = [&](int f) {
            std::vector<Pos> v;
            const Floor& fl = lv.floors[static_cast<size_t>(f)];
            for (int y = 1; y < fl.h - 1; ++y)
                for (int x = 1; x < fl.w - 1; ++x)
                    if (plain(lv, {f, x, y})) v.push_back({f, x, y});
            return v;
        };
        if (in.sealed_portal && in.doors == 0) {
            // wall the route shut halfway, and give the far side a portal back
            if (route.size() < 5) continue;
            const size_t i = route.size() / 2;
            const Pos cut = route[i];
            if (plain(lv, cut) && cut.f == route[i - 1].f && cut.f == route[i + 1].f) {
                lv.cell(cut).block = Block::wall;
                const std::vector<Pos> near = region(lv, lv.start, ~0u, false);
                if (std::find(near.begin(), near.end(), lv.goal) != near.end()) continue;  // the loops go round the cut: try again
                std::vector<Pos> far_side;
                for (const Pos& q : plain_cells(cut.f))
                    if (std::find(near.begin(), near.end(), q) == near.end() && !lv.cell(q).goal) far_side.push_back(q);
                std::vector<Pos> near_side;
                for (const Pos& q : near)
                    if (plain(lv, q) && !(q == lv.start)) near_side.push_back(q);
                if (far_side.empty() || near_side.empty()) continue;
                const Pos a = near_side[static_cast<size_t>(near_side.size() / 2 + rng.range(static_cast<int>((near_side.size() + 1) / 2)))];  // the far half of what's near
                const Pos b = far_side[static_cast<size_t>(rng.range(static_cast<int>(far_side.size())))];
                lv.cell(a).portal = lv.cell(b).portal = static_cast<std::int8_t>(lv.portals.size());
                lv.portals.push_back({a, b});
            }
        }
        for (int k = 0; k < in.portals; ++k) {
            const int f = rng.range(static_cast<int>(lv.floors.size()));
            std::vector<Pos> v = plain_cells(f);
            if (v.size() < 8) break;
            const Pos a = v[static_cast<size_t>(rng.range(static_cast<int>(v.size())))];
            Pos b = a;
            for (int t = 0; t < 30 && (std::abs(b.x - a.x) + std::abs(b.y - a.y) < n); ++t) b = v[static_cast<size_t>(rng.range(static_cast<int>(v.size())))];
            if (a == b) continue;
            lv.cell(a).portal = lv.cell(b).portal = static_cast<std::int8_t>(lv.portals.size());
            lv.portals.push_back({a, b});
        }
        // light bulbs in quiet corners
        for (int k = 0; k < in.bulbs; ++k) {
            std::vector<Pos> v = plain_cells(rng.range(static_cast<int>(lv.floors.size())));
            std::vector<Pos> dead;
            for (const Pos& q : v)
                if (is_dead_end(lv, q)) dead.push_back(q);
            const std::vector<Pos>& pool = dead.empty() ? v : dead;
            if (pool.empty()) break;
            lv.things.push_back({ThingKind::bulb, pool[static_cast<size_t>(rng.range(static_cast<int>(pool.size())))]});
        }
        // the marble and the snail start somewhere well away from the player
        auto far_cell = [&](int f) {
            std::vector<Pos> v = plain_cells(f);
            std::vector<Pos> far;
            for (const Pos& q : v)
                if (std::abs(q.x - lv.start.x) + std::abs(q.y - lv.start.y) > n) far.push_back(q);
            const std::vector<Pos>& pool = far.empty() ? v : far;
            return pool.empty() ? Pos{-1, 0, 0} : pool[static_cast<size_t>(rng.range(static_cast<int>(pool.size())))];
        };
        if (in.marble) lv.marble = far_cell(0);
        if (in.snail) lv.snail = far_cell(rng.range(static_cast<int>(lv.floors.size())));
        // posters on a few wall faces
        for (size_t f = 0; f < lv.floors.size(); ++f) {
            Floor& fl = lv.floors[f];
            for (int y = 0; y < fl.h; ++y)
                for (int x = 0; x < fl.w; ++x) {
                    Cell& c = fl.at(x, y);
                    if (c.block == Block::wall) continue;
                    for (int d = 0; d < 4; ++d)
                        if (fl.in(x + kDX[d], y + kDY[d]) && fl.at(x + kDX[d], y + kDY[d]).block == Block::wall && rng.unit() < .07)
                            c.face[static_cast<size_t>(d)] = static_cast<std::uint8_t>(1 + rng.range(63));
                }
        }
        // every door must matter on its own: with all the others open, it still stands in the way (portals included)
        bool needed = true;
        for (int k = 0; k < in.doors && needed; ++k) {
            const std::vector<Pos> shut = region(lv, lv.start, ~(1u << k));
            needed = std::find(shut.begin(), shut.end(), lv.goal) == shut.end();
        }
        if (!needed) continue;
        // and a sealed level's portal is the only way across
        if (in.sealed_portal && !lv.portals.empty()) {
            const std::vector<Pos> walk = region(lv, lv.start, ~0u, false);
            if (std::find(walk.begin(), walk.end(), lv.goal) != walk.end()) continue;
        }
        lv.optimal_steps = solve_steps(lv);
        if (lv.optimal_steps < 0) continue;
        // a proper walk, not a stroll: the reward lies well away from the start (relaxed if the maze keeps refusing)
        const int want = attempt < 200 ? 3 * n + 4 : n;
        if (lv.optimal_steps < want) continue;
        return lv;
    }
}

}  // namespace mz
