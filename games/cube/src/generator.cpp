#include "generator.hpp"

#include <algorithm>

namespace ps_cube {
namespace {

using Mask = std::vector<std::uint8_t>;

int cell_of(int side, int face, int row, int column) {
    if (row < 0 || column < 0 || row >= side || column >= side) {
        return -1;
    }
    const int cell = face * side * side + row * side + column;
    return cell;
}

int open_count(const Mask& stone) {
    int count = 0;
    for (std::uint8_t value : stone) {
        count += value ? 0 : 1;
    }
    return count;
}

// True when the open cells form one region.
bool connected(const Geometry& geometry, const Mask& stone) {
    int first = -1;
    for (int cell = 0; cell < geometry.cells; ++cell) {
        if (!stone[static_cast<std::size_t>(cell)]) {
            first = cell;
            break;
        }
    }
    if (first < 0) {
        return false;
    }
    Mask seen(stone.size(), 0);
    std::vector<int> queue{first};
    seen[static_cast<std::size_t>(first)] = 1;
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const std::array<int, 4>& around = geometry.neighbours[static_cast<std::size_t>(queue[head])];
        for (int next : around) {
            if (next >= 0 && !stone[static_cast<std::size_t>(next)] && !seen[static_cast<std::size_t>(next)]) {
                seen[static_cast<std::size_t>(next)] = 1;
                queue.push_back(next);
            }
        }
    }
    const bool whole = static_cast<int>(queue.size()) == open_count(stone);
    return whole;
}

// One section of stone: a straight wall, an L, or a block, kept within one face.
std::vector<int> section(int side, std::uint64_t& random, int length, bool hard) {
    const int face = random_below(random, faces);
    const int shape = random_below(random, hard ? 5 : 3);
    std::vector<int> cells;
    if (shape >= 3 && side >= 5) {
        // A blocked corner or patch: two by two, or two by three on Hard.
        const int height = 2;
        const int width = shape == 4 ? 3 : 2;
        const int row = random_below(random, side - height + 1);
        const int column = random_below(random, side - width + 1);
        for (int r = 0; r < height; ++r) {
            for (int c = 0; c < width; ++c) {
                cells.push_back(cell_of(side, face, row + r, column + c));
            }
        }
        return cells;
    }
    const int directions[4][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
    int direction = random_below(random, 4);
    int row = random_below(random, side);
    int column = random_below(random, side);
    const int turn_at = shape == 2 ? 1 + random_below(random, std::max(1, length - 1)) : -1;
    for (int index = 0; index < length; ++index) {
        const int cell = cell_of(side, face, row, column);
        if (cell < 0) {
            break;
        }
        cells.push_back(cell);
        if (index + 1 == turn_at) {
            direction = (direction + (random_below(random, 2) == 0 ? 1 : 3)) % 4;
        }
        row += directions[direction][0];
        column += directions[direction][1];
    }
    return cells;
}

Mask place_walls(const Geometry& geometry, const Tier& spec, std::uint64_t& random, bool hard) {
    Mask stone(static_cast<std::size_t>(geometry.cells), 0);
    const int walls = spec.walls_min + random_below(random, spec.walls_max - spec.walls_min + 1);
    int placed = 0;
    for (int attempt = 0; attempt < 40 && placed < walls; ++attempt) {
        const int length = 1 + random_below(random, spec.wall_length_max);
        const std::vector<int> cells = section(geometry.side, random, length, hard);
        Mask trial = stone;
        bool fits = !cells.empty();
        for (int cell : cells) {
            fits = fits && cell >= 0 && !trial[static_cast<std::size_t>(cell)];
            if (fits) {
                trial[static_cast<std::size_t>(cell)] = 1;
            }
        }
        if (!fits || !connected(geometry, trial)) {
            continue;
        }
        stone = trial;
        ++placed;
    }
    return stone;
}

// A long self-avoiding walk over the open cells, by Warnsdorff's rule: step to the
// neighbour with the fewest onward choices. A budget keeps a bad start cheap.
struct Walk {
    const Geometry& geometry;
    const Mask& stone;
    std::uint64_t& random;
    Mask used;
    std::vector<int> path;
    std::vector<int> best;
    std::size_t goal = 0;
    long budget = 0;

    bool extend() {
        if (path.size() > best.size()) {
            best = path;
        }
        if (path.size() == goal) {
            return true;
        }
        if (--budget < 0) {
            return false;
        }
        std::vector<std::pair<int, int>> options;
        const std::array<int, 4>& around = geometry.neighbours[static_cast<std::size_t>(path.back())];
        for (int next : around) {
            if (next < 0 || stone[static_cast<std::size_t>(next)] || used[static_cast<std::size_t>(next)]) {
                continue;
            }
            int onward = 0;
            const std::array<int, 4>& further = geometry.neighbours[static_cast<std::size_t>(next)];
            for (int beyond : further) {
                if (beyond >= 0 && !stone[static_cast<std::size_t>(beyond)] &&
                    !used[static_cast<std::size_t>(beyond)]) {
                    ++onward;
                }
            }
            options.push_back({onward * 8 + random_below(random, 8), next});
        }
        std::sort(options.begin(), options.end());
        for (const std::pair<int, int>& option : options) {
            used[static_cast<std::size_t>(option.second)] = 1;
            path.push_back(option.second);
            if (extend()) {
                return true;
            }
            path.pop_back();
            used[static_cast<std::size_t>(option.second)] = 0;
        }
        return false;
    }
};

std::vector<int> long_walk(const Geometry& geometry, const Mask& stone, std::uint64_t& random) {
    std::vector<int> open;
    for (int cell = 0; cell < geometry.cells; ++cell) {
        if (!stone[static_cast<std::size_t>(cell)]) {
            open.push_back(cell);
        }
    }
    Walk walk{geometry, stone, random, Mask(stone.size(), 0), {}, {}, open.size(), 0};
    std::vector<int> tour;
    for (int attempt = 0; attempt < 6 && tour.size() < open.size(); ++attempt) {
        const int start = open[static_cast<std::size_t>(random_below(random, static_cast<int>(open.size())))];
        std::fill(walk.used.begin(), walk.used.end(), 0);
        walk.path.assign(1, start);
        walk.best.clear();
        walk.used[static_cast<std::size_t>(start)] = 1;
        walk.budget = 6000;
        static_cast<void>(walk.extend());
        if (walk.best.size() > tour.size()) {
            tour = walk.best;
        }
    }
    // Backbite moves wander the walk into a less regular shape, and grow it into any
    // open cell beside an end.
    Mask in_tour(stone.size(), 0);
    for (int cell : tour) {
        in_tour[static_cast<std::size_t>(cell)] = 1;
    }
    for (int step = 0; step < 2500 && !tour.empty(); ++step) {
        if (random_below(random, 2) == 0) {
            std::reverse(tour.begin(), tour.end());
        }
        const std::array<int, 4>& around = geometry.neighbours[static_cast<std::size_t>(tour.front())];
        const int pick = around[static_cast<std::size_t>(random_below(random, 4))];
        if (pick < 0 || stone[static_cast<std::size_t>(pick)]) {
            continue;
        }
        if (!in_tour[static_cast<std::size_t>(pick)]) {
            tour.insert(tour.begin(), pick);
            in_tour[static_cast<std::size_t>(pick)] = 1;
            continue;
        }
        const std::size_t at =
            static_cast<std::size_t>(std::find(tour.begin(), tour.end(), pick) - tour.begin());
        if (at > 1) {
            std::reverse(tour.begin(), tour.begin() + static_cast<std::ptrdiff_t>(at));
        }
    }
    return tour;
}

// Cuts the walk into `count` lines. Returns false when no fair cut was found.
bool cut(const Geometry& geometry, const std::vector<int>& tour, int count, int minimum,
         int short_piece, std::uint64_t& random, std::vector<std::vector<int>>& pieces) {
    const int total = static_cast<int>(tour.size());
    for (int attempt = 0; attempt < 60; ++attempt) {
        std::vector<int> lengths(static_cast<std::size_t>(count), minimum);
        if (short_piece >= 0) {
            lengths[static_cast<std::size_t>(short_piece)] = 2;
        }
        int extra = total;
        for (int length : lengths) {
            extra -= length;
        }
        if (extra < 0) {
            return false;
        }
        // Uneven lines: some long, some short, as in a hand-made board.
        std::vector<int> weights(static_cast<std::size_t>(count), 0);
        int weight_sum = 0;
        for (int index = 0; index < count; ++index) {
            if (index == short_piece) {
                continue;
            }
            const int weight = 1 + random_below(random, 6);
            weights[static_cast<std::size_t>(index)] = weight;
            weight_sum += weight;
        }
        int given = 0;
        for (int index = 0; index < count && weight_sum > 0; ++index) {
            const int share = extra * weights[static_cast<std::size_t>(index)] / weight_sum;
            lengths[static_cast<std::size_t>(index)] += share;
            given += share;
        }
        for (int rest = extra - given; rest > 0; --rest) {
            int index = random_below(random, count);
            if (index == short_piece) {
                index = (index + 1) % count;
            }
            ++lengths[static_cast<std::size_t>(index)];
        }
        pieces.clear();
        bool fair = true;
        int at = 0;
        for (int index = 0; index < count && fair; ++index) {
            const int length = lengths[static_cast<std::size_t>(index)];
            std::vector<int> piece(tour.begin() + at, tour.begin() + at + length);
            at += length;
            // Ends that touch would be joined before the player starts. The short piece
            // ends in a portal, not an endpoint.
            fair = index == short_piece || !adjacent(geometry, piece.front(), piece.back());
            pieces.push_back(piece);
        }
        if (fair) {
            return true;
        }
    }
    return false;
}

struct Candidate {
    Puzzle puzzle;
    Metrics metrics;
    bool accepted = false;
    bool filled = false;
    long score = 0;
};

long difficulty_score(const Metrics& metrics) {
    // Wrong turns weigh most; long detours, folds over edges and routes that collide make
    // a board harder; forced first moves make it easier.
    const long score = std::min(metrics.backtracks, 5000L) * 4 + metrics.detour * 10L +
                       metrics.crossings * 20L + metrics.conflicts * 15L -
                       metrics.forced_starts * 30L;
    return score;
}

// Whether a measured board belongs to its level.
bool fits(int level, const Tier& spec, const Puzzle& puzzle, const Metrics& metrics, bool wants_portal) {
    if (puzzle.portals.empty() ? wants_portal : !metrics.portal_needed) {
        return false;
    }
    const int cells = puzzle.geometry.cells;
    if (level == 0) {
        return metrics.solutions >= 20;
    }
    if (!metrics.solutions_exact || metrics.solutions > spec.solution_limit) {
        return false;
    }
    if (level == 1) {
        return metrics.solutions >= 2 && metrics.forced_starts <= 2 && metrics.stones * 5 <= cells &&
               metrics.backtracks >= 20 && metrics.backtracks <= 110;
    }
    return metrics.backtracks >= 100 && metrics.forced_starts <= 1 && metrics.crossings >= 6 && metrics.stones * 10 <= cells * 3;
}

bool build(int level, const Tier& spec, std::uint64_t& random, const GenerateOptions& options,
           Puzzle& puzzle) {
    const Geometry geometry = make_geometry(spec.side);
    const Mask stone = place_walls(geometry, spec, random, level == 2);
    const std::vector<int> tour = long_walk(geometry, stone, random);
    int portal_count = 0;
    if (options.portals > 0 || options.gentle_portal) {
        portal_count = 1;
    } else if (options.portals < 0 && random_below(random, 100) < spec.portal_percent) {
        portal_count = 1;
        if (random_below(random, 100) < spec.second_portal_percent) {
            portal_count = 2;
        }
    }
    const int count = spec.pairs + portal_count;
    // On a gentle board the portal stands right beside the endpoint that needs it.
    const bool gentle = portal_count > 0 && (options.gentle_portal || level == 1);
    const int first_x = portal_count > 0 ? random_below(random, count) : -1;
    std::vector<std::vector<int>> pieces;
    if (!cut(geometry, tour, count, spec.minimum_line, gentle ? first_x : -1, random, pieces)) {
        return false;
    }
    std::vector<int> stones;
    for (int cell = 0; cell < geometry.cells; ++cell) {
        if (stone[static_cast<std::size_t>(cell)]) {
            stones.push_back(cell);
        }
    }
    // Join pieces through portals: X's last cell and Y's first cell become a linked pair,
    // and X then Y become one line. Pieces next to each other in the walk touch, so the
    // joined pieces are always at least two apart.
    std::vector<int> join_to(static_cast<std::size_t>(count), -1);
    std::vector<std::uint8_t> joined(static_cast<std::size_t>(count), 0);
    std::vector<std::array<int, 2>> portals;
    int x = first_x;
    for (int portal = 0; portal < portal_count; ++portal) {
        bool found = false;
        for (int attempt = 0; attempt < 40 && !found; ++attempt) {
            if (x < 0 || joined[static_cast<std::size_t>(x)] || join_to[static_cast<std::size_t>(x)] >= 0) {
                x = random_below(random, count);
                continue;
            }
            const int y = random_below(random, count);
            if (std::abs(x - y) < 2 || joined[static_cast<std::size_t>(y)] ||
                join_to[static_cast<std::size_t>(y)] >= 0 || join_to[static_cast<std::size_t>(x)] >= 0) {
                continue;
            }
            const int a = pieces[static_cast<std::size_t>(x)].back();
            const int b = pieces[static_cast<std::size_t>(y)].front();
            if (adjacent(geometry, a, b)) {
                continue;
            }
            join_to[static_cast<std::size_t>(x)] = y;
            joined[static_cast<std::size_t>(y)] = 1;
            portals.push_back({a, b});
            found = true;
        }
        if (!found) {
            return false;
        }
        x = -1;
    }
    std::vector<std::vector<int>> lines;
    for (int index = 0; index < count; ++index) {
        if (joined[static_cast<std::size_t>(index)]) {
            continue;
        }
        std::vector<int> line = pieces[static_cast<std::size_t>(index)];
        int next = join_to[static_cast<std::size_t>(index)];
        while (next >= 0) {
            const std::vector<int>& more = pieces[static_cast<std::size_t>(next)];
            line.insert(line.end(), more.begin(), more.end());
            next = join_to[static_cast<std::size_t>(next)];
        }
        if (adjacent(geometry, line.front(), line.back())) {
            return false;
        }
        lines.push_back(line);
    }
    // Colours in a random order, so neighbouring lines do not run through the palette.
    for (std::size_t index = lines.size(); index > 1; --index) {
        const std::size_t other = static_cast<std::size_t>(random_below(random, static_cast<int>(index)));
        std::swap(lines[index - 1], lines[other]);
    }
    puzzle = Puzzle{};
    puzzle.side = spec.side;
    puzzle.level = level;
    for (const std::vector<int>& line : lines) {
        puzzle.ends.push_back({line.front(), line.back()});
    }
    puzzle.portals = portals;
    puzzle.witness = lines;
    if (!assemble(puzzle, stones)) {
        return false;
    }
    return witness_valid(puzzle);
}


std::vector<int> stone_cells(const Puzzle& puzzle) {
    std::vector<int> stones;
    for (int cell = 0; cell < puzzle.geometry.cells; ++cell) {
        if (puzzle.tiles[static_cast<std::size_t>(cell)] == Tile::stone) {
            stones.push_back(cell);
        }
    }
    return stones;
}

bool hop_at(const Puzzle& puzzle, const std::vector<int>& line, std::size_t index) {
    // True when line[index] is reached by a hop from line[index - 1].
    if (index == 0) {
        return false;
    }
    const bool hop = !adjacent(puzzle.geometry, line[index - 1], line[index]);
    return hop;
}

// Removes needless detours from the witness: wherever an earlier cell could step straight
// onto a later one, the cells between are dropped. Portals the lines no longer use are
// taken off the board. The solver counts only such lines, so the witness must be one.
void straighten(Puzzle& puzzle) {
    for (std::vector<int>& line : puzzle.witness) {
        bool changed = true;
        while (changed) {
            changed = false;
            for (std::size_t i = 0; i + 2 < line.size() && !changed; ++i) {
                // A portal the line enters can only hop.
                if (i + 1 < line.size() && hop_at(puzzle, line, i + 1)) {
                    continue;
                }
                for (std::size_t j = line.size() - 1; j >= i + 2 && !changed; --j) {
                    if (!adjacent(puzzle.geometry, line[i], line[j]) || hop_at(puzzle, line, j)) {
                        continue;
                    }
                    line.erase(line.begin() + static_cast<std::ptrdiff_t>(i + 1),
                               line.begin() + static_cast<std::ptrdiff_t>(j));
                    changed = true;
                }
            }
        }
    }
    std::vector<std::array<int, 2>> used;
    for (const std::array<int, 2>& portal : puzzle.portals) {
        bool in_use = false;
        for (const std::vector<int>& line : puzzle.witness) {
            in_use = in_use || std::find(line.begin(), line.end(), portal[0]) != line.end();
        }
        if (in_use) {
            used.push_back(portal);
        }
    }
    puzzle.portals = used;
    const std::vector<int> stones = stone_cells(puzzle);
    static_cast<void>(assemble(puzzle, stones));
}

// Adds stone, a cell at a time, until every portal is needed and the board has at most
// the tier's number of solutions. Each stone goes on a cell that a different solution uses and the witness
// does not, preferring cells beside stone already, so blocking sections grow as walls.
bool carve(Puzzle& puzzle, const Tier& spec, std::uint64_t& random, SolveResult& count) {
    // Stone only ever removes routes, so a portal once proved needed stays needed.
    bool portal_needed = puzzle.portals.empty();
    for (int round = 0; round < 90; ++round) {
        SolveLimits limits;
        limits.node_budget = spec.node_budget;
        limits.solution_cap = spec.solution_limit + 1;
        limits.keep = 2;
        // A portal must be needed: first block any way round it.
        SolveResult result;
        if (!portal_needed) {
            SolveLimits without = limits;
            without.portals_as_stones = true;
            without.solution_cap = 1;
            without.keep = 1;
            result = solve(puzzle, without);
            if (!result.exact) {
                return false;
            }
            portal_needed = result.solutions == 0;
        }
        if (result.solutions == 0) {
            result = solve(puzzle, limits);
            if (result.exact && result.solutions <= spec.solution_limit) {
                count = result;
                return true;
            }
        }
        const std::vector<std::vector<int>>* other = nullptr;
        for (const std::vector<std::vector<int>>& lines : result.found) {
            if (lines != puzzle.witness) {
                other = &lines;
                break;
            }
        }
        if (other == nullptr) {
            return false;
        }
        Mask on_witness(static_cast<std::size_t>(puzzle.geometry.cells), 0);
        for (const std::vector<int>& line : puzzle.witness) {
            for (int cell : line) {
                on_witness[static_cast<std::size_t>(cell)] = 1;
            }
        }
        int chosen = -1;
        int chosen_score = -1;
        for (const std::vector<int>& line : *other) {
            for (int cell : line) {
                const std::size_t at = static_cast<std::size_t>(cell);
                if (on_witness[at] || puzzle.tiles[at] != Tile::open) {
                    continue;
                }
                int walls = 4 - puzzle.geometry.neighbour_count[at];
                int beside_end = 0;
                const std::array<int, 4>& around = puzzle.geometry.neighbours[at];
                for (int next : around) {
                    const Tile tile = next >= 0 ? puzzle.tiles[static_cast<std::size_t>(next)] : Tile::open;
                    walls += tile == Tile::stone ? 1 : 0;
                    beside_end += tile == Tile::endpoint ? 1 : 0;
                }
                // Stone that walls an endpoint in only hands the player its first move.
                const int score = std::max(0, walls * 16 - beside_end * 40 + 40) + random_below(random, 16);
                if (score > chosen_score) {
                    chosen = cell;
                    chosen_score = score;
                }
            }
        }
        if (chosen < 0) {
            return false;
        }
        std::vector<int> stones = stone_cells(puzzle);
        stones.push_back(chosen);
        if (!assemble(puzzle, stones)) {
            return false;
        }
    }
    return false;
}

}  // namespace

Tier tier(int level) {
    Tier spec;
    if (level <= 0) {
        spec.side = 4;
        spec.pairs = 5;
        spec.walls_min = 0;
        spec.walls_max = 2;
        spec.wall_length_max = 1;
        spec.minimum_line = 3;
        spec.solution_limit = 1000;
        spec.node_budget = 20000;
        spec.attempts = 1;
    } else if (level == 1) {
        spec.side = 5;
        spec.pairs = 6;
        spec.walls_min = 2;
        spec.walls_max = 3;
        spec.wall_length_max = 3;
        spec.portal_percent = 50;
        spec.minimum_line = 3;
        spec.solution_limit = 40;
        spec.node_budget = 60000;
        spec.attempts = 4;
    } else {
        spec.side = 6;
        spec.pairs = 8;
        spec.walls_min = 3;
        spec.walls_max = 5;
        spec.wall_length_max = 4;
        spec.portal_percent = 60;
        spec.second_portal_percent = 30;
        spec.minimum_line = 4;
        spec.solution_limit = 1;
        spec.node_budget = 200000;
        spec.attempts = 2;
    }
    return spec;
}

Puzzle generate(int level, std::uint64_t seed, const GenerateOptions& options) {
    const int clamped = std::clamp(level, 0, levels - 1);
    const Tier spec = tier(clamped);
    const bool wants_portal = options.portals > 0 || options.gentle_portal;
    std::uint64_t random = seed ^ (0xC0BEULL * static_cast<std::uint64_t>(clamped + 1));
    Candidate best;
    int fitting = 0;
    // Keep the hardest of several boards that fit the level; a board that fits nothing
    // is kept only until one does.
    for (int attempt = 0; attempt < 200 && fitting < spec.attempts; ++attempt) {
        if (options.cancel != nullptr && (*options.cancel).load() && best.filled) {
            break;
        }
        Puzzle puzzle;
        if (!build(clamped, spec, random, options, puzzle)) {
            continue;
        }
        straighten(puzzle);
        SolveLimits limits;
        limits.node_budget = spec.node_budget;
        limits.solution_cap = spec.solution_limit + 1;
        SolveResult count;
        const bool settled = clamped == 0 || carve(puzzle, spec, random, count);
        if (!witness_valid(puzzle)) {
            continue;
        }
        if (!settled && best.filled) {
            continue;
        }
        // Carving already counted the solutions and proved any portal needed.
        const Metrics metrics = clamped == 0 || !settled ? measure(puzzle, limits)
                                                        : measure_known(puzzle, limits, count, !puzzle.portals.empty());
        const bool accepted = settled && fits(clamped, spec, puzzle, metrics, wants_portal);
        fitting += accepted ? 1 : 0;
        const long score = difficulty_score(metrics);
        if ((accepted && !best.accepted) || (accepted == best.accepted && score > best.score) ||
            !best.filled) {
            best.puzzle = puzzle;
            best.metrics = metrics;
            best.accepted = accepted;
            best.score = score;
            best.filled = true;
        }
    }
    best.puzzle.seed = seed;
    return best.puzzle;
}

}  // namespace ps_cube
