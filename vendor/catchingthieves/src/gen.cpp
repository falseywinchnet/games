#include "gen.hpp"

#include "solver.hpp"

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace ct {

namespace {
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ULL + 0x632BE59BD9B4E019ULL) { next(); }
    std::uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    int range(int lo, int hi) { return lo + static_cast<int>(next() % static_cast<std::uint64_t>(hi - lo + 1)); }
    double unit() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
};

// --- carving the garden
bool carve(Level& lv, int w, int h, int boxes, Rng& rng) {
    lv = Level{};
    lv.w = w + 2;
    lv.h = h + 2;
    const int N = lv.w * lv.h;
    lv.tiles.assign(static_cast<size_t>(N), Tile::wall);
    lv.goal.assign(static_cast<size_t>(N), 0);
    for (int y = 1; y <= h; ++y)
        for (int x = 1; x <= w; ++x) lv.tiles[static_cast<size_t>(lv.idx(x, y))] = Tile::floor;
    struct SetTile { Level& lv; int w,h;
        void operator()(int x,int y,Tile t) const { if(x>=1 && y>=1 && x<=w && y<=h) lv.tiles[static_cast<size_t>(lv.idx(x,y))]=t; }
    };
    const SetTile set{lv,w,h};
    // an irregular outline: bite a corner or two out of the rectangle
    const int bites = rng.range(0, 2);
    for (int b = 0; b < bites; ++b) {
        const int cw = rng.range(1, std::max(1, w / 2 - 1)), chh = rng.range(1, std::max(1, h / 2 - 1));
        const int corner = rng.range(0, 3);
        for (int y = 0; y < chh; ++y)
            for (int x = 0; x < cw; ++x) set(corner % 2 ? w - x : 1 + x, corner / 2 ? h - y : 1 + y, Tile::wall);
    }
    // hedges scattered inside: singles, pairs and corners
    static const int shapes[][3][2] = {{{0, 0}, {0, 0}, {0, 0}}, {{0, 0}, {1, 0}, {1, 0}}, {{0, 0}, {0, 1}, {0, 1}},
                                       {{0, 0}, {1, 0}, {0, 1}}, {{0, 0}, {1, 0}, {1, 1}}, {{0, 0}, {0, 1}, {1, 1}}};
    const int pieces = static_cast<int>(w * h * (.07 + .05 * rng.unit() + .004 * std::max(0, w * h - 49) / 8.0));
    for (int k = 0; k < pieces; ++k) {
        const int (&sh)[3][2] = shapes[rng.range(0, 5)];
        const int x = rng.range(1, w), y = rng.range(1, h);
        for (const int (&c)[2] : sh) set(x + c[0], y + c[1], Tile::wall);
    }
    // keep the largest connected patch of soil
    std::vector<int> comp(static_cast<size_t>(N), -1);
    int best = -1, best_size = 0, nc = 0;
    for (int i = 0; i < N; ++i) {
        if (!lv.floor(i) || comp[static_cast<size_t>(i)] >= 0) continue;
        std::vector<int> st{i};
        comp[static_cast<size_t>(i)] = nc;
        int size = 0;
        while (!st.empty()) {
            const int c = st.back();
            st.pop_back();
            ++size;
            for (int d = 0; d < 4; ++d) {
                const int n = lv.step(c, d);
                if (lv.floor(n) && comp[static_cast<size_t>(n)] < 0) { comp[static_cast<size_t>(n)] = nc; st.push_back(n); }
            }
        }
        if (size > best_size) { best_size = size; best = nc; }
        ++nc;
    }
    for (int i = 0; i < N; ++i)
        if (lv.floor(i) && comp[static_cast<size_t>(i)] != best) lv.tiles[static_cast<size_t>(i)] = Tile::wall;
    // fill dead ends: a pumpkin can never use them
    for (bool again = true; again;) {
        again = false;
        for (int i = 0; i < N; ++i) {
            if (!lv.floor(i)) continue;
            int walls = 0;
            for (int d = 0; d < 4; ++d) walls += !lv.floor(lv.step(i, d));
            if (walls >= 3) { lv.tiles[static_cast<size_t>(i)] = Tile::wall; again = true; }
        }
    }
    int floor = 0;
    for (int i = 0; i < N; ++i) floor += lv.floor(i);
    if (floor < boxes * 3 + 8 || floor > w * h) return false;
    // big open fields make dull levels: allow only a few 3x3 blocks of soil
    int open = 0;
    for (int y = 1; y + 2 <= h; ++y)
        for (int x = 1; x + 2 <= w; ++x) {
            bool all = true;
            for (int k = 0; k < 9 && all; ++k) all = lv.floor(lv.idx(x + k % 3, y + k / 3));
            open += all;
        }
    if (open > std::max(1, w * h / 30)) return false;
    // hedges far from any soil are not part of the garden
    for (int y = 0; y < lv.h; ++y)
        for (int x = 0; x < lv.w; ++x) {
            const int i = lv.idx(x, y);
            if (lv.tiles[static_cast<size_t>(i)] != Tile::wall) continue;
            bool near = false;
            for (int dy = -1; dy <= 1 && !near; ++dy)
                for (int dx = -1; dx <= 1 && !near; ++dx) near = lv.in(x + dx, y + dy) && lv.floor(lv.idx(x + dx, y + dy));
            if (!near) lv.tiles[static_cast<size_t>(i)] = Tile::outside;
        }
    return true;
}

// --- the backwards search
struct RNode {
    std::uint32_t at;   // offset of its sorted pumpkin cells in the shared array
    int player;
    int parent;
    int moved;    // the cell the pulled pumpkin arrived at
    int from;     // the cell it was pulled from
    int depth;
};

int region_of(const Level& lv, const std::vector<std::uint8_t>& occ, int start, std::vector<int>& seen, int stamp) {
    std::vector<int> st{start};
    seen[static_cast<size_t>(start)] = stamp;
    int lo = start;
    while (!st.empty()) {
        const int c = st.back();
        st.pop_back();
        lo = std::min(lo, c);
        for (int d = 0; d < 4; ++d) {
            const int n = lv.step(c, d);
            if (lv.floor(n) && !occ[static_cast<size_t>(n)] && seen[static_cast<size_t>(n)] != stamp) { seen[static_cast<size_t>(n)] = stamp; st.push_back(n); }
        }
    }
    return lo;
}

std::string rkey(const std::vector<std::uint8_t>& b, int region) {
    std::string k(b.begin(), b.end());
    k += static_cast<char>(region & 255);
    k += static_cast<char>(region >> 8);
    return k;
}
}  // namespace

int box_switches(const Level& level, const std::string& lurd) {
    Board b;
    if (!b.load(level)) return 0;
    int last = -1, n = 0;
    for (char c : lurd) {
        Move m;
        if (dir_of(c) < 0 || !b.move(dir_of(c), &m)) break;
        if (m.push) { if (m.box != last) ++n; last = m.box; }
    }
    return n;
}

GenResult generate(const GenParams& p, std::stop_token stop) {
    GenResult res;
    Rng rng(p.seed);
    for (int attempt = 1; attempt <= p.attempts; ++attempt) {
        if (stop.stop_requested()) return res;
        res.attempts = attempt;
        Level lv;
        if (!carve(lv, p.w, p.h, p.boxes, rng)) { ++res.carve_rejects; continue; }
        const int N = lv.w * lv.h;
        std::vector<int> floors;
        for (int i = 0; i < N; ++i)
            if (lv.floor(i)) floors.push_back(i);
        // burrows, each with its pumpkin sitting on it
        std::vector<std::uint8_t> goal_cells;
        while (static_cast<int>(goal_cells.size()) < p.boxes) {
            const int c = floors[static_cast<size_t>(rng.range(0, static_cast<int>(floors.size()) - 1))];
            if (std::find(goal_cells.begin(), goal_cells.end(), c) == goal_cells.end()) goal_cells.push_back(static_cast<std::uint8_t>(c));
        }
        std::sort(goal_cells.begin(), goal_cells.end());
        if (N > 255) return res;  // cells are bytes: keep gardens under 16x16 overall
        for (std::uint8_t g : goal_cells) lv.goal[g] = 1;
        // every place the bear could have finished
        std::vector<RNode> nodes;
        std::vector<std::uint8_t> flat;  // every node's pumpkin cells, p.boxes per node
        const size_t K = static_cast<size_t>(p.boxes);
        struct AddNode { std::vector<RNode>& nodes; std::vector<std::uint8_t>& flat;
            void operator()(const std::vector<std::uint8_t>& bx,int player,int parent,int moved,int from,int depth) const {
                nodes.push_back({static_cast<std::uint32_t>(flat.size()),player,parent,moved,from,depth});
                flat.insert(flat.end(),bx.begin(),bx.end());
            }
        };
        const AddNode add{nodes,flat};
        struct Boxes { const std::vector<std::uint8_t>& flat; size_t count;
            std::vector<std::uint8_t> operator()(const RNode& n) const { return {flat.begin()+n.at,flat.begin()+n.at+static_cast<long>(count)}; }
        };
        const Boxes boxes_of{flat,K};
        std::unordered_set<std::string> seen_states;
        std::vector<std::uint8_t> occ(static_cast<size_t>(N), 0);
        std::vector<int> seen(static_cast<size_t>(N), 0), mine(static_cast<size_t>(N), 0);
        int stamp = 0, mstamp = 0;
        for (std::uint8_t g : goal_cells) occ[g] = 1;
        for (int c : floors) {
            if (occ[static_cast<size_t>(c)]) continue;
            const int r = region_of(lv, occ, c, seen, ++stamp);
            if (seen_states.insert(rkey(goal_cells, r)).second) add(goal_cells, c, -1, -1, -1, 0);
        }
        for (std::uint8_t g : goal_cells) occ[g] = 0;
        // pull, breadth-first: depth is exactly the fewest pushes to solve from there
        for (size_t head = 0; head < nodes.size() && static_cast<long long>(nodes.size()) < p.reverse_budget; ++head) {
            if (stop.stop_requested()) return res;
            const std::vector<std::uint8_t> bx = boxes_of(nodes[head]);
            for (std::uint8_t b : bx) occ[b] = 1;
            region_of(lv, occ, nodes[head].player, mine, ++mstamp);
            for (size_t i = 0; i < bx.size(); ++i)
                for (int d = 0; d < 4; ++d) {
                    const int pc = lv.step(bx[i], d);          // the bear, beside the pumpkin
                    const int back = pc >= 0 ? lv.step(pc, d) : -1;  // where he steps back to
                    if (pc < 0 || mine[static_cast<size_t>(pc)] != mstamp) continue;
                    if (!lv.floor(back) || occ[static_cast<size_t>(back)]) continue;
                    std::vector<std::uint8_t> nb = bx;
                    nb[i] = static_cast<std::uint8_t>(pc);
                    occ[bx[i]] = 0;
                    occ[static_cast<size_t>(pc)] = 1;
                    const int r = region_of(lv, occ, back, seen, ++stamp);
                    occ[static_cast<size_t>(pc)] = 0;
                    occ[bx[i]] = 1;
                    std::sort(nb.begin(), nb.end());
                    if (seen_states.insert(rkey(nb, r)).second) add(nb, back, static_cast<int>(head), pc, bx[i], nodes[head].depth + 1);
                }
            for (std::uint8_t b : bx) occ[b] = 0;
        }
        // the best start: deep, every pumpkin off its burrow, and many switches between pumpkins on the way
        int best = -1;
        double best_score = -1;
        const int maxd = nodes.empty() ? 0 : nodes.back().depth;
        for (size_t i = nodes.size(); i-- > 0;) {
            const RNode& n = nodes[i];
            if (n.depth < std::max(p.min_pushes, maxd - 8)) break;
            if (n.depth > p.max_pushes) continue;
            bool off = true;
            for (std::uint8_t b : boxes_of(n)) off = off && !lv.goal[b];
            if (!off) continue;
            // played forwards, each push undoes one pull, newest first; the same pumpkin
            // carries on when a push starts where the last one left it
            int sw = 0, prev_from = -1;
            for (int k = static_cast<int>(i); nodes[static_cast<size_t>(k)].parent >= 0; k = nodes[static_cast<size_t>(k)].parent) {
                const RNode& nk = nodes[static_cast<size_t>(k)];
                if (nk.moved != prev_from) ++sw;
                prev_from = nk.from;
            }
            const double score = n.depth + 1.5 * sw + rng.unit();
            if (score > best_score) { best_score = score; best = static_cast<int>(i); }
        }
        res.deepest = std::max(res.deepest, maxd);
        if (best < 0) { ++res.shallow; continue; }
        const RNode& start = nodes[static_cast<size_t>(best)];
        { const std::vector<std::uint8_t> sb = boxes_of(start); lv.boxes.assign(sb.begin(), sb.end()); }
        lv.player = start.player;
        // the independent check: solve it forwards from scratch
        const SolveResult sr = solve(lv, p.solve_budget, stop);
        if (!sr.solved || sr.pushes != start.depth) { ++res.unverified; continue; }
        Board b;
        b.load(lv);
        if (!b.replay(sr.lurd) || !b.solved()) continue;
        lv.solution = sr.lurd;
        res.ok = true;
        res.level = lv;
        res.pushes = sr.pushes;
        res.moves = static_cast<int>(sr.lurd.size());
        res.box_lines = box_switches(lv, sr.lurd);
        res.solver_nodes = sr.nodes;
        return res;
    }
    return res;
}

}  // namespace ct
