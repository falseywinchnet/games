#include "solver.hpp"

#include <algorithm>
#include <deque>
#include <limits>
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
// Pits change connectivity only when a pumpkin is pushed. Without gates or
// sentries, ordinary walking is still an undirected, effect-free equivalence
// relation. Retain filled pits and consumed pumpkin identities in the key.
SolveResult solve_pits(const Board& initial,long long limit,CancellationToken stop) {
    SolveResult out;const auto& lv=initial.level();
    // Relax every pit to ordinary floor and ignore all other pumpkins. Every
    // real goal-bound trajectory is included in this reverse-pull reachability.
    // A spare pumpkin may be irretrievably parked, so count candidates rather
    // than declaring any single off-goal dead square a losing position.
    const auto goal_live=live_squares(lv);const int goals=lv.goals();
    auto enough_candidates=[&](const Board& b) {
        int candidates=0;for(int box:b.boxes())candidates+=box>=0&&goal_live[box];return candidates>=goals;
    };
    struct State {std::string key,edge;int parent,cost;};
    std::vector<State> nodes{{initial.state_key(),{},-1,0}};
    Board work=initial;
    auto player_key=[](std::string key,int player) {
        unsigned value=static_cast<unsigned>(player+2);key[0]=static_cast<char>(value&255);key[1]=static_cast<char>(value>>8);return key;
    };
    auto flood=[&](std::vector<int>& previous,std::vector<int>& direction) {
        previous.assign(lv.tiles.size(),-1);direction.assign(lv.tiles.size(),-1);
        std::vector<int> queue{work.player()};previous[work.player()]=work.player();int region=work.player();
        for(std::size_t head=0;head<queue.size();++head)for(int d=0;d<4;++d)if(work.can_walk(queue[head],d)) {
            int to=lv.step(queue[head],d);if(previous[to]>=0)continue;
            previous[to]=queue[head];direction[to]=d;queue.push_back(to);region=std::min(region,to);
        }
        return region;
    };
    std::vector<int> previous,direction,scratch_previous,scratch_direction;
    std::unordered_map<std::string,int> seen{{player_key(nodes[0].key,flood(previous,direction)),0}};
    int found=-1;
    for(std::size_t head=0;head<nodes.size()&&found<0;++head) {
        if(stop.stop_requested()){out.exhausted=true;out.nodes=nodes.size();return out;}
        const auto current=nodes[head];work.restore_state(current.key);
        if(work.solved()){found=static_cast<int>(head);break;}
        if(work.stuck()||!enough_candidates(work))continue;
        flood(previous,direction);const auto boxes=work.boxes();const int player=work.player();
        for(int box:boxes)if(box>=0)for(int d=0;d<4&&found<0;++d) {
            int support=lv.step(box,(d+2)%4);if(support<0||previous[support]<0)continue;
            work.restore_state(player_key(current.key,support));Move move;
            if(!work.move(d,&move)||!move.push)continue;
            if(!enough_candidates(work))continue;
            auto key=work.state_key(),canonical=player_key(key,flood(scratch_previous,scratch_direction));
            if(seen.count(canonical))continue;
            if(static_cast<long long>(nodes.size())>=limit){out.exhausted=true;out.nodes=nodes.size();return out;}
            std::string edge;
            for(int cell=support;cell!=player;cell=previous[cell])edge+=kWalk[direction[cell]];
            std::reverse(edge.begin(),edge.end());edge+=kPush[d];
            int id=static_cast<int>(nodes.size());seen.emplace(std::move(canonical),id);
            nodes.push_back({std::move(key),std::move(edge),static_cast<int>(head),current.cost+1});
            if(work.solved())found=id;
        }
    }
    out.nodes=nodes.size();if(found<0)return out;
    out.solved=true;out.pushes=nodes[found].cost;std::vector<int> chain;
    for(int id=found;nodes[id].parent>=0;id=nodes[id].parent)chain.push_back(id);
    for(auto it=chain.rbegin();it!=chain.rend();++it)out.lurd+=nodes[*it].edge;
    return out;
}

// Close over effect-free directed walking, then search only pushes and theft
// events. Exact post-event player positions remain in the key: one-way reachable
// sets are not equivalence classes. Every stored edge is an actual walk + event.
SolveResult solve_extended(const Board& initial, long long limit, CancellationToken stop) {
    SolveResult out; const auto& lv=initial.level();
    const auto goal_live=live_squares(lv); const int goals=lv.goals();
    auto enough_candidates=[&](const Board& b) {
        int count=0; for(int c:b.boxes()) count+=c>=0&&goal_live[c]; return count>=goals;
    };
    struct State { std::string key,edge; int cost,parent; };
    std::vector<State> nodes{{initial.state_key(),{},0,-1}};
    std::unordered_map<std::string,int> seen{{nodes[0].key,0}};
    std::deque<std::pair<int,int>> queue{{0,0}};
    Board work=initial;
    std::vector<int> previous(lv.tiles.size()),direction(lv.tiles.size()),reachable,victims;
    std::vector<std::uint8_t> safe(lv.tiles.size());
    reachable.reserve(lv.tiles.size());
    int found=-1;
    while (!queue.empty()) {
        if (stop.stop_requested()) { out.exhausted=true; out.nodes=nodes.size(); return out; }
        auto [id,cost]=queue.front(); queue.pop_front();
        if (cost!=nodes[id].cost) continue;
        const auto current=nodes[id].key;
        work.restore_state(current);
        if (work.solved()) { found=id; break; }
        if (!enough_candidates(work)) continue;
        // A walk is effect-free precisely when its destination guards every
        // exposed pumpkin. Sequential theft order matters only on event edges,
        // which are always executed by Board::move, never approximated here.
        victims.clear();
        for(size_t r=0;r<lv.raccoons.size();++r) if(!work.departed_raccoons()[r])
            for(int d=0;d<4;++d) {
                int c=lv.step(lv.raccoons[r],d);
                if(work.box_at(c)>=0&&!lv.goal[c]) victims.push_back(c);
            }
        std::fill(safe.begin(),safe.end(),1);
        for(int c: victims) for(int p=0;p<lv.w*lv.h;++p)
            if(std::max(std::abs(c%lv.w-p%lv.w),std::abs(c/lv.w-p/lv.w))>1) safe[p]=0;
        std::fill(previous.begin(),previous.end(),-1);
        const int player=work.player(); reachable.clear(); reachable.push_back(player); previous[player]=player;
        for(size_t h=0;h<reachable.size();++h) for(int d=0;d<4;++d) if(work.can_walk(reachable[h],d)) {
            int to=lv.step(reachable[h],d);
            if(!safe[to]||previous[to]>=0) continue;
            previous[to]=reachable[h]; direction[to]=d; reachable.push_back(to);
        }
        for(int from:reachable) {
            unsigned events=0;
            for(int d=0;d<4;++d) {
                int to=lv.step(from,d);
                if(work.box_at(to)>=0||(work.can_walk(from,d)&&!safe[to])) events|=1u<<d;
            }
            if(!events)continue;
            auto support=current; unsigned value=static_cast<unsigned>(from+2);
            support[0]=static_cast<char>(value&255); support[1]=static_cast<char>(value>>8);
            work.restore_state(support);
            for(int d=0;d<4;++d) {
                if(!(events&(1u<<d)))continue;
                Move m; if(!work.move(d,&m)) continue;
                bool viable=enough_candidates(work);
                auto key=work.state_key(); work.undo();
                if(!viable) continue;
                int next_cost=cost+static_cast<int>(m.push),next;
                auto it=seen.find(key);
                if(it==seen.end()) {
                    if(static_cast<long long>(nodes.size())>=limit) { out.exhausted=true;out.nodes=nodes.size();return out; }
                    next=static_cast<int>(nodes.size());seen.emplace(key,next);
                    nodes.push_back({std::move(key),{},std::numeric_limits<int>::max(),-1});
                } else next=it->second;
                if(next_cost>=nodes[next].cost) continue;
                std::string edge;
                for(int c=from;c!=player;c=previous[c]) edge+=kWalk[direction[c]];
                std::reverse(edge.begin(),edge.end());edge+=m.push?kPush[d]:kWalk[d];
                nodes[next].cost=next_cost;nodes[next].parent=id;nodes[next].edge=std::move(edge);
                if(m.push) queue.emplace_back(next,next_cost);else queue.emplace_front(next,next_cost);
            }
        }
    }
    out.nodes=nodes.size();if(found<0)return out;
    out.solved=true;out.pushes=nodes[found].cost;
    std::vector<int> chain;
    for(int id=found;nodes[id].parent>=0;id=nodes[id].parent)chain.push_back(id);
    for(auto it=chain.rbegin();it!=chain.rend();++it)out.lurd+=nodes[*it].edge;
    return out;
}
}  // namespace

SolveResult solve(const Board& board, long long node_limit, CancellationToken stop) {
    const Level& lv = board.level();
    if (lv.special()) {
        if(lv.raccoons.empty()&&std::none_of(lv.gate.begin(),lv.gate.end(),[](auto gate){return gate!=0;}))return solve_pits(board,node_limit,stop);
        return solve_extended(board,node_limit,stop);
    }
    SolveResult res;
    const int N = lv.w * lv.h;
    const auto live = live_squares(lv);
    std::vector<Node> nodes;
    std::unordered_map<std::string, int> seen_states;
    std::vector<int> seen(static_cast<size_t>(N), 0);
    std::vector<std::uint8_t> occ(static_cast<size_t>(N), 0);
    int stamp = 0;
    auto is_goal = [&](const std::vector<int>& bx) {
        for (int b : bx)
            if (!lv.goal[static_cast<size_t>(b)]) return false;
        return true;
    };
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
        if (static_cast<long long>(nodes.size()) > node_limit || stop.stop_requested()) { res.exhausted = true; break; }
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

SolveResult solve(const Level& level, long long node_limit, CancellationToken stop) {
    Board b;
    if (!b.load(level)) return {};
    return solve(b, node_limit, stop);
}

}  // namespace ct
