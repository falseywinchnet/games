#include "solver.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace ct {

namespace {
struct Node {
    std::vector<int> boxes;  // sorted
    int player;              // an actual cell (where the bear is after the push)
    int parent;
    int from_box, dir;       // the push that led here: the pumpkin's cell before it, and the direction
};

// Walk-reachable cells for the bear, given the pumpkins. Returns the region's smallest cell (its name).
int reach(const Level& lv, const std::vector<std::uint8_t>& occ, int start, std::vector<int>& seen, int stamp) {
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

std::string key_of(const std::vector<int>& boxes, int region) {
    std::string k;
    k.reserve(boxes.size() * 2 + 2);
    for (int b : boxes) { k += static_cast<char>(b & 255); k += static_cast<char>(b >> 8); }
    k += static_cast<char>(region & 255);
    k += static_cast<char>(region >> 8);
    return k;
}

// the walk from a to b avoiding pumpkins, as LURD lowercase
std::string walk(const Level& lv, const std::vector<std::uint8_t>& occ, int a, int b) {
    if (a == b) return {};
    std::vector<int> prev(static_cast<size_t>(lv.w * lv.h), -1);
    std::vector<int> q{a};
    prev[static_cast<size_t>(a)] = a;
    for (size_t k = 0; k < q.size(); ++k) {
        const int c = q[k];
        if (c == b) break;
        for (int d = 0; d < 4; ++d) {
            const int n = lv.step(c, d);
            if (lv.floor(n) && !occ[static_cast<size_t>(n)] && prev[static_cast<size_t>(n)] < 0) { prev[static_cast<size_t>(n)] = c; q.push_back(n); }
        }
    }
    std::string s;
    for (int c = b; c != a; c = prev[static_cast<size_t>(c)]) {
        const int p = prev[static_cast<size_t>(c)];
        if (p < 0) return "?";
        for (int d = 0; d < 4; ++d)
            if (lv.step(p, d) == c) { s += kWalk[d]; break; }
    }
    std::reverse(s.begin(), s.end());
    return s;
}
}  // namespace

SolveResult solve(const Board& board, long long node_limit, std::stop_token stop) {
    const Level& lv = board.level();
    SolveResult res;
    const int N = lv.w * lv.h;
    const std::vector<std::uint8_t> live = live_squares(lv);
    std::vector<Node> nodes;
    std::unordered_map<std::string, int> seen_states;
    std::vector<int> seen(static_cast<size_t>(N), 0);
    std::vector<std::uint8_t> occ(static_cast<size_t>(N), 0);
    int stamp = 0;
    struct IsGoal { const Level& lv;
        bool operator()(const std::vector<int>& bx) const {
            for(int b:bx) if(!lv.goal[static_cast<size_t>(b)]) return false;
            return true;
        }
    };
    const IsGoal is_goal{lv};
    {
        std::vector<int> bx = board.boxes();
        std::sort(bx.begin(), bx.end());
        for (int b : bx) occ[static_cast<size_t>(b)] = 1;
        const int region = reach(lv, occ, board.player(), seen, ++stamp);
        for (int b : bx) occ[static_cast<size_t>(b)] = 0;
        nodes.push_back({bx, board.player(), -1, -1, -1});
        seen_states.emplace(key_of(bx, region), 0);
    }
    int found = is_goal(nodes[0].boxes) ? 0 : -1;
    // breadth-first, one push per layer
    std::vector<int> mine(static_cast<size_t>(N), 0);  // the current node's walkable region
    int mine_stamp = 0;
    for (size_t head = 0; found < 0 && head < nodes.size(); ++head) {
        if (stop.stop_requested()) { res.exhausted = true; return res; }
        if (static_cast<long long>(nodes.size()) > node_limit) { res.exhausted = true; break; }
        const std::vector<int> bx = nodes[head].boxes;
        const int pl = nodes[head].player;
        for (int b : bx) occ[static_cast<size_t>(b)] = 1;
        reach(lv, occ, pl, mine, ++mine_stamp);
        for (size_t i = 0; i < bx.size() && found < 0; ++i) {
            for (int d = 0; d < 4; ++d) {
                const int behind = lv.step(bx[i], (d + 2) % 4), to = lv.step(bx[i], d);
                if (behind < 0 || mine[static_cast<size_t>(behind)] != mine_stamp) continue;
                if (!lv.floor(to) || occ[static_cast<size_t>(to)] || !live[static_cast<size_t>(to)]) continue;
                std::vector<int> nb = bx;
                nb[i] = to;
                occ[static_cast<size_t>(bx[i])] = 0;
                occ[static_cast<size_t>(to)] = 1;
                const int region = reach(lv, occ, bx[i], seen, ++stamp);
                occ[static_cast<size_t>(to)] = 0;
                occ[static_cast<size_t>(bx[i])] = 1;
                std::sort(nb.begin(), nb.end());
                const std::string k = key_of(nb, region);
                if (!seen_states.count(k)) {
                    seen_states.emplace(k, static_cast<int>(nodes.size()));
                    nodes.push_back({nb, bx[i], static_cast<int>(head), bx[i], d});
                    if (is_goal(nb)) { found = static_cast<int>(nodes.size()) - 1; break; }
                }
            }
        }
        for (int b : bx) occ[static_cast<size_t>(b)] = 0;
    }
    res.nodes = static_cast<long long>(nodes.size());
    if (found < 0) return res;
    // rebuild: the chain of pushes, with the walks between them
    std::vector<int> chain;
    for (int n = found; n > 0; n = nodes[static_cast<size_t>(n)].parent) chain.push_back(n);
    std::reverse(chain.begin(), chain.end());
    int pl = board.player();
    std::vector<int> bx = board.boxes();
    std::string out;
    for (int n : chain) {
        const Node& nd = nodes[static_cast<size_t>(n)];
        std::fill(occ.begin(), occ.end(), 0);
        for (int b : bx) occ[static_cast<size_t>(b)] = 1;
        const int behind = lv.step(nd.from_box, (nd.dir + 2) % 4);
        out += walk(lv, occ, pl, behind);
        out += kPush[nd.dir];
        for (int& b : bx)
            if (b == nd.from_box) { b = lv.step(nd.from_box, nd.dir); break; }
        pl = nd.from_box;
    }
    res.solved = true;
    res.pushes = static_cast<int>(chain.size());
    res.lurd = out;
    return res;
}

SolveResult solve(const Level& level, long long node_limit, std::stop_token stop) {
    Board b;
    if (!b.load(level)) return {};
    return solve(b, node_limit, stop);
}

}  // namespace ct
