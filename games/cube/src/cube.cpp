#include "cube.hpp"

#include <algorithm>
#include <cmath>

namespace ps_cube {
namespace {

double distance1(Point3 a, Point3 b) {
    const double d = std::fabs(a.x - b.x) + std::fabs(a.y - b.y) + std::fabs(a.z - b.z);
    return d;
}

bool valid_cell(const Puzzle& puzzle, int cell) {
    const bool valid = cell >= 0 && cell < puzzle.geometry.cells;
    return valid;
}

// The index of a cell in a line, or -1.
int position(const std::vector<int>& path, int cell) {
    for (std::size_t index = 0; index < path.size(); ++index) {
        if (path[index] == cell) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

// True when the cell at `index` is a portal the line entered (and hops out of next).
bool entered_portal(const Puzzle& puzzle, const std::vector<int>& path, int index) {
    const std::size_t at = static_cast<std::size_t>(index);
    if (index < 1 || at + 1 >= path.size()) {
        return false;
    }
    const int cell = path[at];
    if (puzzle.tiles[static_cast<std::size_t>(cell)] != Tile::portal) {
        return false;
    }
    const bool entered = path[at + 1] == partner(puzzle, cell) &&
                         adjacent(puzzle.geometry, path[at - 1], cell);
    return entered;
}

void check_won(const Puzzle& puzzle, Play& play) {
    play.won = all_connected(puzzle, play);
    if (play.won) {
        play.active = -1;
    }
}

}  // namespace

std::uint64_t next_random(std::uint64_t& state) {
    state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    const std::uint64_t value = z ^ (z >> 31);
    return value;
}

int random_below(std::uint64_t& state, int limit) {
    if (limit <= 1) {
        return 0;
    }
    const std::uint64_t value = next_random(state) % static_cast<std::uint64_t>(limit);
    return static_cast<int>(value);
}

Point3 face_normal(int face) {
    if (face == 0) {
        return {0, 0, 1};
    }
    if (face == 1) {
        return {-1, 0, 0};
    }
    return {0, -1, 0};
}

void face_axes(int face, Point3& u, Point3& v) {
    if (face == 0) {
        u = {1, 0, 0};
        v = {0, 1, 0};
    } else if (face == 1) {
        u = {0, 0, 1};
        v = {0, 1, 0};
    } else {
        u = {1, 0, 0};
        v = {0, 0, 1};
    }
}

Point3 cell_center(int side, int cell) {
    const int per_face = side * side;
    const int face = cell / per_face;
    const int row = (cell % per_face) / side;
    const int column = cell % side;
    const double u = -1 + (column + .5) * 2.0 / side;
    const double v = -1 + (row + .5) * 2.0 / side;
    Point3 axis_u;
    Point3 axis_v;
    face_axes(face, axis_u, axis_v);
    const Point3 normal = face_normal(face);
    const Point3 center{normal.x + axis_u.x * u + axis_v.x * v,
                        normal.y + axis_u.y * u + axis_v.y * v,
                        normal.z + axis_u.z * u + axis_v.z * v};
    return center;
}

Geometry make_geometry(int side) {
    Geometry geometry;
    geometry.side = std::clamp(side, minimum_side, maximum_side);
    geometry.cells = faces * geometry.side * geometry.side;
    const std::size_t count = static_cast<std::size_t>(geometry.cells);
    geometry.neighbours.assign(count, std::array<int, 4>{-1, -1, -1, -1});
    geometry.neighbour_count.assign(count, 0);
    std::vector<Point3> centers(count);
    for (int cell = 0; cell < geometry.cells; ++cell) {
        centers[static_cast<std::size_t>(cell)] = cell_center(geometry.side, cell);
    }
    // Neighbours are one cell apart along the surface, including across a fold.
    const double step = 2.0 / geometry.side;
    for (int a = 0; a < geometry.cells; ++a) {
        const std::size_t ia = static_cast<std::size_t>(a);
        for (int b = 0; b < geometry.cells; ++b) {
            if (a == b) {
                continue;
            }
            const double d = distance1(centers[ia], centers[static_cast<std::size_t>(b)]);
            if (std::fabs(d - step) < 1e-9 && geometry.neighbour_count[ia] < 4) {
                geometry.neighbours[ia][static_cast<std::size_t>(geometry.neighbour_count[ia])] = b;
                ++geometry.neighbour_count[ia];
            }
        }
    }
    return geometry;
}

bool adjacent(const Geometry& geometry, int a, int b) {
    if (a < 0 || a >= geometry.cells || b < 0 || b >= geometry.cells) {
        return false;
    }
    const std::array<int, 4>& around = geometry.neighbours[static_cast<std::size_t>(a)];
    const bool found = around[0] == b || around[1] == b || around[2] == b || around[3] == b;
    return found;
}

bool assemble(Puzzle& puzzle, const std::vector<int>& stones) {
    if (puzzle.side < minimum_side || puzzle.side > maximum_side ||
        puzzle.ends.size() > static_cast<std::size_t>(maximum_pairs) ||
        puzzle.portals.size() > static_cast<std::size_t>(maximum_portals)) {
        return false;
    }
    puzzle.geometry = make_geometry(puzzle.side);
    const std::size_t count = static_cast<std::size_t>(puzzle.geometry.cells);
    puzzle.tiles.assign(count, Tile::open);
    puzzle.pair_of.assign(count, -1);
    puzzle.portal_of.assign(count, -1);
    for (int cell : stones) {
        if (!valid_cell(puzzle, cell) || puzzle.tiles[static_cast<std::size_t>(cell)] != Tile::open) {
            return false;
        }
        puzzle.tiles[static_cast<std::size_t>(cell)] = Tile::stone;
    }
    for (std::size_t pair = 0; pair < puzzle.ends.size(); ++pair) {
        for (int end : puzzle.ends[pair]) {
            if (!valid_cell(puzzle, end) || puzzle.tiles[static_cast<std::size_t>(end)] != Tile::open) {
                return false;
            }
            puzzle.tiles[static_cast<std::size_t>(end)] = Tile::endpoint;
            puzzle.pair_of[static_cast<std::size_t>(end)] = static_cast<int>(pair);
        }
    }
    for (std::size_t portal = 0; portal < puzzle.portals.size(); ++portal) {
        for (int cell : puzzle.portals[portal]) {
            if (!valid_cell(puzzle, cell) || puzzle.tiles[static_cast<std::size_t>(cell)] != Tile::open) {
                return false;
            }
            puzzle.tiles[static_cast<std::size_t>(cell)] = Tile::portal;
            puzzle.portal_of[static_cast<std::size_t>(cell)] = static_cast<int>(portal);
        }
        // Linked cells side by side would be an ordinary step.
        if (adjacent(puzzle.geometry, puzzle.portals[portal][0], puzzle.portals[portal][1])) {
            return false;
        }
    }
    return true;
}

int partner(const Puzzle& puzzle, int portal_cell) {
    if (!valid_cell(puzzle, portal_cell)) {
        return -1;
    }
    const int portal = puzzle.portal_of[static_cast<std::size_t>(portal_cell)];
    if (portal < 0) {
        return -1;
    }
    const std::array<int, 2>& cells = puzzle.portals[static_cast<std::size_t>(portal)];
    const int other = cells[0] == portal_cell ? cells[1] : cells[0];
    return other;
}

Play fresh_play(const Puzzle& puzzle) {
    Play play;
    play.paths.assign(puzzle.ends.size(), std::vector<int>{});
    return play;
}

int owner(const Play& play, int cell) {
    for (std::size_t pair = 0; pair < play.paths.size(); ++pair) {
        if (position(play.paths[pair], cell) >= 0) {
            return static_cast<int>(pair);
        }
    }
    return -1;
}

int portal_owner(const Puzzle& puzzle, const Play& play, int portal) {
    if (portal < 0 || portal >= static_cast<int>(puzzle.portals.size())) {
        return -1;
    }
    const int pair = owner(play, puzzle.portals[static_cast<std::size_t>(portal)][0]);
    return pair;
}

bool complete(const Puzzle& puzzle, const std::vector<int>& path, int pair) {
    if (pair < 0 || pair >= static_cast<int>(puzzle.ends.size()) || path.size() < 2) {
        return false;
    }
    const std::array<int, 2>& ends = puzzle.ends[static_cast<std::size_t>(pair)];
    const bool joined = (path.front() == ends[0] && path.back() == ends[1]) ||
                        (path.front() == ends[1] && path.back() == ends[0]);
    return joined && path_legal(puzzle, path, pair);
}

bool all_connected(const Puzzle& puzzle, const Play& play) {
    if (play.paths.size() != puzzle.ends.size() || puzzle.ends.empty()) {
        return false;
    }
    for (std::size_t pair = 0; pair < puzzle.ends.size(); ++pair) {
        if (!complete(puzzle, play.paths[pair], static_cast<int>(pair))) {
            return false;
        }
    }
    return true;
}

bool press(const Puzzle& puzzle, Play& play, int cell) {
    if (play.won || !valid_cell(puzzle, cell) || play.paths.size() != puzzle.ends.size()) {
        return false;
    }
    const int pair = puzzle.pair_of[static_cast<std::size_t>(cell)];
    if (pair >= 0) {
        play.paths[static_cast<std::size_t>(pair)] = {cell};
        play.active = pair;
        ++play.strokes;
        return true;
    }
    const int line = owner(play, cell);
    if (line < 0) {
        return false;
    }
    std::vector<int>& path = play.paths[static_cast<std::size_t>(line)];
    const int index = position(path, cell);
    const bool at_head = static_cast<std::size_t>(index) + 1 == path.size();
    if (complete(puzzle, path, line) && at_head) {
        return false;
    }
    // A press on a portal the line entered keeps the hop: the line continues from the far side.
    const int keep = entered_portal(puzzle, path, index) ? index + 1 : index;
    path.resize(static_cast<std::size_t>(keep) + 1);
    play.active = line;
    if (!at_head) {
        ++play.strokes;
    }
    return true;
}

bool extend(const Puzzle& puzzle, Play& play, int cell) {
    if (play.won || play.active < 0 || play.active >= static_cast<int>(play.paths.size()) ||
        !valid_cell(puzzle, cell)) {
        return false;
    }
    const int pair = play.active;
    std::vector<int>& path = play.paths[static_cast<std::size_t>(pair)];
    if (path.empty()) {
        return false;
    }
    const int index = position(path, cell);
    if (index >= 0) {
        if (static_cast<std::size_t>(index) + 1 == path.size()) {
            return false;
        }
        // Tracing back over the cell where the line entered a portal undoes the hop.
        const int keep = entered_portal(puzzle, path, index) ? index - 1 : index;
        path.resize(static_cast<std::size_t>(keep) + 1);
        return true;
    }
    if (complete(puzzle, path, pair) || !adjacent(puzzle.geometry, path.back(), cell)) {
        return false;
    }
    const Tile tile = puzzle.tiles[static_cast<std::size_t>(cell)];
    if (tile == Tile::stone || owner(play, cell) >= 0) {
        return false;
    }
    if (tile == Tile::endpoint) {
        if (puzzle.pair_of[static_cast<std::size_t>(cell)] != pair) {
            return false;
        }
        path.push_back(cell);
        check_won(puzzle, play);
        return true;
    }
    if (tile == Tile::portal) {
        const int other = partner(puzzle, cell);
        if (other < 0 || owner(play, other) >= 0) {
            return false;
        }
        path.push_back(cell);
        path.push_back(other);
        return true;
    }
    path.push_back(cell);
    return true;
}

void release(Play& play) {
    play.active = -1;
}

bool erase(Play& play, int pair) {
    if (play.won || pair < 0 || pair >= static_cast<int>(play.paths.size()) ||
        play.paths[static_cast<std::size_t>(pair)].empty()) {
        return false;
    }
    play.paths[static_cast<std::size_t>(pair)].clear();
    if (play.active == pair) {
        play.active = -1;
    }
    return true;
}

bool path_legal(const Puzzle& puzzle, const std::vector<int>& path, int pair) {
    if (pair < 0 || pair >= static_cast<int>(puzzle.ends.size())) {
        return false;
    }
    if (path.empty()) {
        return true;
    }
    const std::array<int, 2>& ends = puzzle.ends[static_cast<std::size_t>(pair)];
    if (path.front() != ends[0] && path.front() != ends[1]) {
        return false;
    }
    std::vector<std::uint8_t> seen(static_cast<std::size_t>(puzzle.geometry.cells), 0);
    seen[static_cast<std::size_t>(path.front())] = 1;
    int hop_to = -1;
    for (std::size_t index = 1; index < path.size(); ++index) {
        const int cell = path[index];
        if (!valid_cell(puzzle, cell) || seen[static_cast<std::size_t>(cell)]) {
            return false;
        }
        seen[static_cast<std::size_t>(cell)] = 1;
        if (hop_to >= 0) {
            // The far side of a portal: arrived by the hop, left by an ordinary step.
            if (cell != hop_to) {
                return false;
            }
            hop_to = -1;
            continue;
        }
        if (!adjacent(puzzle.geometry, path[index - 1], cell)) {
            return false;
        }
        const Tile tile = puzzle.tiles[static_cast<std::size_t>(cell)];
        if (tile == Tile::stone) {
            return false;
        }
        if (tile == Tile::endpoint &&
            (puzzle.pair_of[static_cast<std::size_t>(cell)] != pair || index + 1 != path.size())) {
            return false;
        }
        if (tile == Tile::portal) {
            if (index + 1 == path.size()) {
                return false;
            }
            hop_to = partner(puzzle, cell);
        }
    }
    return hop_to < 0;
}

bool play_legal(const Puzzle& puzzle, const Play& play) {
    if (play.paths.size() != puzzle.ends.size() || play.active < -1 ||
        play.active >= static_cast<int>(play.paths.size()) || play.strokes < 0) {
        return false;
    }
    std::vector<std::uint8_t> used(static_cast<std::size_t>(puzzle.geometry.cells), 0);
    for (std::size_t pair = 0; pair < play.paths.size(); ++pair) {
        const std::vector<int>& path = play.paths[pair];
        if (!path_legal(puzzle, path, static_cast<int>(pair))) {
            return false;
        }
        for (int cell : path) {
            if (used[static_cast<std::size_t>(cell)]) {
                return false;
            }
            used[static_cast<std::size_t>(cell)] = 1;
        }
    }
    return play.won == all_connected(puzzle, play);
}

bool witness_valid(const Puzzle& puzzle) {
    if (puzzle.ends.empty() || puzzle.witness.size() != puzzle.ends.size() ||
        static_cast<int>(puzzle.tiles.size()) != puzzle.geometry.cells ||
        puzzle.geometry.side != puzzle.side) {
        return false;
    }
    Play play = fresh_play(puzzle);
    for (std::size_t pair = 0; pair < puzzle.witness.size(); ++pair) {
        const std::vector<int>& line = puzzle.witness[pair];
        if (line.size() < 2 || !press(puzzle, play, line.front())) {
            return false;
        }
        for (std::size_t index = 1; index < line.size(); ++index) {
            const int previous = line[index - 1];
            const bool hop = puzzle.tiles[static_cast<std::size_t>(previous)] == Tile::portal &&
                             line[index] == partner(puzzle, previous) &&
                             play.paths[pair].size() > index;
            if (hop) {
                continue;
            }
            if (!extend(puzzle, play, line[index])) {
                return false;
            }
        }
        if (play.paths[pair] != line) {
            return false;
        }
        release(play);
    }
    return play.won && play_legal(puzzle, play);
}

}  // namespace ps_cube
