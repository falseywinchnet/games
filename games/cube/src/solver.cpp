#include "solver.hpp"

#include <algorithm>

namespace ps_cube {
namespace {

constexpr int blocked = -2;
constexpr long wrong_turn_limit = 20000;
constexpr int free_cell = -1;

int face_of(const Puzzle& puzzle, int cell) {
    const int face = cell / (puzzle.side * puzzle.side);
    return face;
}

// The shortest route from a cell to `target` over open cells and through portals.
std::vector<int> route(const Puzzle& puzzle, int from, int target) {
    const int count = puzzle.geometry.cells;
    std::vector<int> previous(static_cast<std::size_t>(count), -2);
    std::vector<int> queue;
    queue.reserve(static_cast<std::size_t>(count));
    queue.push_back(from);
    previous[static_cast<std::size_t>(from)] = -1;
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const int cell = queue[head];
        if (cell == target) {
            break;
        }
        const std::array<int, 4>& around = puzzle.geometry.neighbours[static_cast<std::size_t>(cell)];
        for (int next : around) {
            if (next < 0 || previous[static_cast<std::size_t>(next)] != -2) {
                continue;
            }
            const Tile tile = puzzle.tiles[static_cast<std::size_t>(next)];
            if (tile == Tile::portal) {
                // Stepping into a portal comes out of its partner.
                const int other = partner(puzzle, next);
                if (other >= 0 && previous[static_cast<std::size_t>(other)] == -2) {
                    previous[static_cast<std::size_t>(next)] = cell;
                    previous[static_cast<std::size_t>(other)] = next;
                    queue.push_back(other);
                }
                continue;
            }
            if (next != target && tile != Tile::open) {
                continue;
            }
            previous[static_cast<std::size_t>(next)] = cell;
            queue.push_back(next);
        }
    }
    std::vector<int> path;
    if (previous[static_cast<std::size_t>(target)] == -2) {
        return path;
    }
    for (int cell = target; cell >= 0; cell = previous[static_cast<std::size_t>(cell)]) {
        path.push_back(cell);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

struct PairOrder {
    std::vector<int> distance;
    bool operator()(int a, int b) const {
        const int da = distance[static_cast<std::size_t>(a)];
        const int db = distance[static_cast<std::size_t>(b)];
        if (da != db) {
            return da < db;
        }
        return a < b;
    }
};

// Depth-first search over lines, one pair at a time, from its more constrained end.
class Search {
public:
    Search(const Puzzle& puzzle, const SolveLimits& limits) : puzzle_(puzzle), limits_(limits) {}

    SolveResult run() {
        const std::size_t count = static_cast<std::size_t>(puzzle_.geometry.cells);
        occupied_.assign(count, free_cell);
        component_.assign(count, -1);
        entered_.assign(count, 0);
        queue_.reserve(count);
        for (std::size_t cell = 0; cell < count; ++cell) {
            const Tile tile = puzzle_.tiles[cell];
            if (tile == Tile::stone || (tile == Tile::portal && limits_.portals_as_stones)) {
                occupied_[cell] = blocked;
            } else if (tile == Tile::endpoint) {
                occupied_[cell] = puzzle_.pair_of[cell];
            }
        }
        const std::size_t pairs = puzzle_.ends.size();
        lines_.assign(pairs, std::vector<int>{});
        starts_.assign(pairs, 0);
        PairOrder by_distance;
        by_distance.distance.assign(pairs, 1000);
        for (std::size_t pair = 0; pair < pairs; ++pair) {
            const std::vector<int> shortest = shortest_route(puzzle_, static_cast<int>(pair));
            if (!shortest.empty()) {
                by_distance.distance[pair] = static_cast<int>(shortest.size());
            }
            // Begin at the end with fewer ways out.
            const int a = puzzle_.ends[pair][0];
            const int b = puzzle_.ends[pair][1];
            starts_[pair] = exits(b) < exits(a) ? 1 : 0;
            order_.push_back(static_cast<int>(pair));
        }
        std::sort(order_.begin(), order_.end(), by_distance);
        if (limits_.order_seed != 0) {
            std::uint64_t random = limits_.order_seed;
            for (std::size_t index = order_.size(); index > 1; --index) {
                const std::size_t other =
                    static_cast<std::size_t>(random_below(random, static_cast<int>(index)));
                std::swap(order_[index - 1], order_[other]);
            }
            for (std::size_t pair = 0; pair < pairs; ++pair) {
                starts_[pair] = random_below(random, 2);
            }
        }
        if (feasible(-1)) {
            next_pair(0);
        }
        result_.exact = !stopped_by_budget_;
        return result_;
    }

private:
    const Puzzle& puzzle_;
    const SolveLimits& limits_;
    std::vector<int> occupied_;   // per cell: blocked, free_cell, or the pair holding it
    std::vector<int> component_;  // per cell, scratch for the connectivity check
    std::vector<int> queue_;
    std::vector<std::uint8_t> entered_;  // per cell: a portal some line hopped out of
    std::vector<int> order_;
    std::vector<int> starts_;     // per pair, which end the line starts from
    std::vector<std::vector<int>> lines_;
    SolveResult result_;
    bool stop_ = false;
    bool stopped_by_budget_ = false;

    int exits(int cell) const {
        int ways = 0;
        const std::array<int, 4>& around = puzzle_.geometry.neighbours[static_cast<std::size_t>(cell)];
        for (int next : around) {
            if (next >= 0 && occupied_[static_cast<std::size_t>(next)] == free_cell) {
                ++ways;
            }
        }
        return ways;
    }

    // True when stepping onto `cell` from `head` makes no needless detour: no earlier
    // cell of the line could have stepped straight onto it. A portal the line entered
    // cannot step anywhere but through itself, so it never offers a shortcut.
    bool no_chord(int cell, int pair, int head, int target) const {
        const std::array<int, 4>& around = puzzle_.geometry.neighbours[static_cast<std::size_t>(cell)];
        for (int next : around) {
            if (next < 0 || next == head || next == target) {
                continue;
            }
            const std::size_t at = static_cast<std::size_t>(next);
            if (occupied_[at] == pair && !entered_[at]) {
                return false;
            }
        }
        return true;
    }

    void label() {
        std::fill(component_.begin(), component_.end(), -1);
        int label_count = 0;
        const int count = puzzle_.geometry.cells;
        for (int seed = 0; seed < count; ++seed) {
            if (occupied_[static_cast<std::size_t>(seed)] != free_cell ||
                component_[static_cast<std::size_t>(seed)] >= 0) {
                continue;
            }
            queue_.clear();
            queue_.push_back(seed);
            component_[static_cast<std::size_t>(seed)] = label_count;
            for (std::size_t head = 0; head < queue_.size(); ++head) {
                const int cell = queue_[head];
                const std::array<int, 4>& around =
                    puzzle_.geometry.neighbours[static_cast<std::size_t>(cell)];
                for (int next : around) {
                    if (next >= 0 && occupied_[static_cast<std::size_t>(next)] == free_cell &&
                        component_[static_cast<std::size_t>(next)] < 0) {
                        component_[static_cast<std::size_t>(next)] = label_count;
                        queue_.push_back(next);
                    }
                }
                // A free portal pair joins the regions around its two cells.
                if (puzzle_.tiles[static_cast<std::size_t>(cell)] == Tile::portal) {
                    const int other = partner(puzzle_, cell);
                    if (other >= 0 && occupied_[static_cast<std::size_t>(other)] == free_cell &&
                        component_[static_cast<std::size_t>(other)] < 0) {
                        component_[static_cast<std::size_t>(other)] = label_count;
                        queue_.push_back(other);
                    }
                }
            }
            ++label_count;
        }
    }

    // True when two occupied cells can still be joined through free cells.
    bool joinable(int a, int b) const {
        if (adjacent(puzzle_.geometry, a, b)) {
            return true;
        }
        const std::array<int, 4>& around_a = puzzle_.geometry.neighbours[static_cast<std::size_t>(a)];
        const std::array<int, 4>& around_b = puzzle_.geometry.neighbours[static_cast<std::size_t>(b)];
        for (int x : around_a) {
            if (x < 0 || occupied_[static_cast<std::size_t>(x)] != free_cell) {
                continue;
            }
            const int label_a = component_[static_cast<std::size_t>(x)];
            for (int y : around_b) {
                if (y >= 0 && occupied_[static_cast<std::size_t>(y)] == free_cell &&
                    component_[static_cast<std::size_t>(y)] == label_a) {
                    return true;
                }
            }
        }
        return false;
    }

    // Every unfinished pair from `slot` on can still be joined.
    bool feasible(int slot) {
        if (!limits_.lookahead) {
            return true;
        }
        label();
        const int pairs = static_cast<int>(order_.size());
        for (int index = std::max(0, slot); index < pairs; ++index) {
            const int pair = order_[static_cast<std::size_t>(index)];
            const std::vector<int>& line = lines_[static_cast<std::size_t>(pair)];
            const int start = starts_[static_cast<std::size_t>(pair)];
            const int from = index == slot && !line.empty()
                                 ? line.back()
                                 : puzzle_.ends[static_cast<std::size_t>(pair)][static_cast<std::size_t>(start)];
            const int to = puzzle_.ends[static_cast<std::size_t>(pair)][static_cast<std::size_t>(1 - start)];
            if (!joinable(from, to)) {
                return false;
            }
        }
        return true;
    }

    void found() {
        ++result_.solutions;
        if (result_.first.empty()) {
            result_.first = lines_;
            result_.nodes_to_first = result_.nodes;
        }
        if (static_cast<int>(result_.found.size()) < limits_.keep) {
            result_.found.push_back(lines_);
        }
        if (result_.solutions >= limits_.solution_cap) {
            stop_ = true;
        }
    }

    void next_pair(int slot) {
        if (stop_) {
            return;
        }
        if (slot == static_cast<int>(order_.size())) {
            found();
            return;
        }
        const int pair = order_[static_cast<std::size_t>(slot)];
        const int start = starts_[static_cast<std::size_t>(pair)];
        std::vector<int>& line = lines_[static_cast<std::size_t>(pair)];
        line.assign(1, puzzle_.ends[static_cast<std::size_t>(pair)][static_cast<std::size_t>(start)]);
        step(slot, pair);
        line.clear();
    }

    void occupy(int pair, int cell) {
        occupied_[static_cast<std::size_t>(cell)] = pair;
        lines_[static_cast<std::size_t>(pair)].push_back(cell);
    }

    void vacate(int pair) {
        std::vector<int>& line = lines_[static_cast<std::size_t>(pair)];
        occupied_[static_cast<std::size_t>(line.back())] = free_cell;
        line.pop_back();
    }

    void step(int slot, int pair) {
        if (stop_) {
            return;
        }
        ++result_.nodes;
        if (result_.nodes > limits_.node_budget) {
            stop_ = true;
            stopped_by_budget_ = true;
            return;
        }
        std::vector<int>& line = lines_[static_cast<std::size_t>(pair)];
        const int head = line.back();
        const int start = starts_[static_cast<std::size_t>(pair)];
        const int target = puzzle_.ends[static_cast<std::size_t>(pair)][static_cast<std::size_t>(1 - start)];
        // Beside the target, the only line without a detour steps onto it.
        if (adjacent(puzzle_.geometry, head, target)) {
            line.push_back(target);
            next_pair(slot + 1);
            line.pop_back();
            return;
        }
        const std::array<int, 4> around = puzzle_.geometry.neighbours[static_cast<std::size_t>(head)];
        for (int next : around) {
            if (stop_) {
                return;
            }
            if (next < 0 || occupied_[static_cast<std::size_t>(next)] != free_cell) {
                continue;
            }
            if (puzzle_.tiles[static_cast<std::size_t>(next)] == Tile::portal) {
                const int other = partner(puzzle_, next);
                // Arriving on the far side is a hop, not a step, so only the entry is checked.
                if (other < 0 || occupied_[static_cast<std::size_t>(other)] != free_cell ||
                    !no_chord(next, pair, head, target)) {
                    continue;
                }
                occupy(pair, next);
                occupy(pair, other);
                entered_[static_cast<std::size_t>(next)] = 1;
                if (feasible(slot)) {
                    step(slot, pair);
                }
                entered_[static_cast<std::size_t>(next)] = 0;
                vacate(pair);
                vacate(pair);
                continue;
            }
            if (!no_chord(next, pair, head, target)) {
                continue;
            }
            occupy(pair, next);
            if (feasible(slot)) {
                step(slot, pair);
            }
            vacate(pair);
        }
    }
};

void orient(const Puzzle& puzzle, std::vector<std::vector<int>>& lines) {
    for (std::size_t pair = 0; pair < lines.size(); ++pair) {
        std::vector<int>& line = lines[pair];
        if (!line.empty() && line.front() != puzzle.ends[pair][0]) {
            std::reverse(line.begin(), line.end());
        }
    }
}

}  // namespace

std::vector<int> shortest_route(const Puzzle& puzzle, int pair) {
    if (pair < 0 || pair >= static_cast<int>(puzzle.ends.size())) {
        return {};
    }
    const std::array<int, 2>& ends = puzzle.ends[static_cast<std::size_t>(pair)];
    std::vector<int> path = route(puzzle, ends[0], ends[1]);
    return path;
}

SolveResult solve(const Puzzle& puzzle, const SolveLimits& limits) {
    if (puzzle.ends.empty() || static_cast<int>(puzzle.tiles.size()) != puzzle.geometry.cells) {
        return SolveResult{};
    }
    Search search(puzzle, limits);
    SolveResult result = search.run();
    // Lines are recorded from the end the search began at; report them from ends[0].
    orient(puzzle, result.first);
    for (std::vector<std::vector<int>>& lines : result.found) {
        orient(puzzle, lines);
    }
    return result;
}

Metrics measure(const Puzzle& puzzle, const SolveLimits& limits) {
    const SolveResult count = solve(puzzle, limits);
    bool needed = false;
    if (!puzzle.portals.empty()) {
        SolveLimits without = limits;
        without.portals_as_stones = true;
        without.solution_cap = 1;
        const SolveResult plain = solve(puzzle, without);
        needed = plain.exact && plain.solutions == 0;
    }
    const Metrics metrics = measure_known(puzzle, limits, count, needed);
    return metrics;
}

Metrics measure_known(const Puzzle& puzzle, const SolveLimits& limits, const SolveResult& count,
                      bool portal_needed) {
    Metrics metrics;
    metrics.pairs = static_cast<int>(puzzle.ends.size());
    metrics.portals = static_cast<int>(puzzle.portals.size());
    for (Tile tile : puzzle.tiles) {
        metrics.stones += tile == Tile::stone ? 1 : 0;
    }
    metrics.solutions = count.solutions;
    metrics.solutions_exact = count.exact;
    metrics.portal_needed = portal_needed;
    std::vector<long> wrong_turns;
    for (std::uint64_t order = 1; order <= 5; ++order) {
        SolveLimits ordered = limits;
        ordered.solution_cap = 1;
        // A person who has made this many wrong turns is lost; the measure stops there.
        ordered.node_budget = std::min(limits.node_budget, wrong_turn_limit);
        ordered.order_seed = order * 0x9E3779B97F4A7C15ULL;
        const SolveResult attempt = solve(puzzle, ordered);
        long placed = 0;
        for (const std::vector<int>& line : attempt.first) {
            placed += static_cast<long>(line.size()) - 1;
        }
        wrong_turns.push_back(attempt.solutions > 0 ? attempt.nodes_to_first - placed : attempt.nodes);
    }
    std::sort(wrong_turns.begin(), wrong_turns.end());
    metrics.backtracks = std::max(0L, wrong_turns[wrong_turns.size() / 2]);
    for (std::size_t pair = 0; pair < puzzle.ends.size(); ++pair) {
        for (int end : puzzle.ends[pair]) {
            int ways = 0;
            const std::array<int, 4>& around = puzzle.geometry.neighbours[static_cast<std::size_t>(end)];
            for (int next : around) {
                if (next < 0) {
                    continue;
                }
                const Tile tile = puzzle.tiles[static_cast<std::size_t>(next)];
                const bool own = tile == Tile::endpoint &&
                                 puzzle.pair_of[static_cast<std::size_t>(next)] == static_cast<int>(pair);
                if (tile == Tile::open || tile == Tile::portal || own) {
                    ++ways;
                }
            }
            metrics.forced_starts += ways == 1 ? 1 : 0;
        }
    }
    std::vector<std::vector<int>> routes;
    for (std::size_t pair = 0; pair < puzzle.witness.size(); ++pair) {
        const std::vector<int>& line = puzzle.witness[pair];
        const int length = static_cast<int>(line.size());
        metrics.total_length += length;
        metrics.longest = std::max(metrics.longest, length);
        for (std::size_t index = 1; index < line.size(); ++index) {
            const int a = line[index - 1];
            const int b = line[index];
            const bool hop = !adjacent(puzzle.geometry, a, b);
            if (hop || face_of(puzzle, a) != face_of(puzzle, b)) {
                ++metrics.crossings;
            }
        }
        const std::vector<int> shortest = shortest_route(puzzle, static_cast<int>(pair));
        if (!shortest.empty()) {
            metrics.detour += std::max(0, length - static_cast<int>(shortest.size()));
        }
        routes.push_back(shortest);
    }
    std::vector<int> claimed(static_cast<std::size_t>(puzzle.geometry.cells), 0);
    for (const std::vector<int>& shortest : routes) {
        for (std::size_t index = 1; index + 1 < shortest.size(); ++index) {
            ++claimed[static_cast<std::size_t>(shortest[index])];
        }
    }
    for (const std::vector<int>& shortest : routes) {
        for (std::size_t index = 1; index + 1 < shortest.size(); ++index) {
            if (claimed[static_cast<std::size_t>(shortest[index])] > 1) {
                ++metrics.conflicts;
                break;
            }
        }
    }
    return metrics;
}

}  // namespace ps_cube
