#include "puzzles.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
namespace games {
const char* puzzle_title(PuzzleKind k) {
    const char* names[] = {"Gems",      "Nature Cube", "Untangle",     "Atom Probe",
                           "Four Pegs", "Switchbox",   "Puzzle Solve", "Sticks & Stones"};
    return names[static_cast<int>(k)];
}
const char* puzzle_slug(PuzzleKind k) {
    const char* names[] = {"gems",      "cube",      "untangle",     "atom_probe",
                           "four_pegs", "switchbox", "puzzle_solve", "sticks_stones"};
    return names[static_cast<int>(k)];
}
PuzzleGame::PuzzleGame(PuzzleKind k) : kind(k) {}
int PuzzleGame::random(int limit) {
    state.random_state ^= state.random_state << 13;
    state.random_state ^= state.random_state >> 17;
    state.random_state ^= state.random_state << 5;
    return static_cast<int>(state.random_state % static_cast<unsigned>(limit));
}
static int gem_color(int value) {
    return value % 16;
}
static bool adjacent8(int a, int b) {
    return a >= 0 && a < 64 && b >= 0 && b < 64 &&
           std::abs(a / 8 - b / 8) + std::abs(a % 8 - b % 8) == 1;
}
int PuzzleGame::gem_colors() const {
    return std::min(8, 5 + state.score / 1800);
}
void PuzzleGame::deal(std::uint32_t seed) {
    state = PuzzleState{};
    state.seed = seed;
    state.random_state = seed ? seed : 1;
    recorded = false;
    cascade_frames.clear();
    message = "Your game saves automatically.";
    if (kind == PuzzleKind::gems) {
        do {
            for (int i = 0; i < 64; ++i) {
                int color;
                do {
                    color = 1 + random(5);
                } while ((i % 8 >= 2 && state.grid[i - 1] == color && state.grid[i - 2] == color) ||
                         (i >= 16 && state.grid[i - 8] == color && state.grid[i - 16] == color));
                state.grid[i] = color;
            }
        } while (!gem_has_move());
    } else if (kind == PuzzleKind::pegs) {
        for (int i = 0; i < 4; ++i)
            state.secret[i] = 1 + random(6);
        message = "Four places, six symbols. Repeated symbols are allowed.";
    } else if (kind == PuzzleKind::untangle) {
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 4; ++x) {
                state.embedding.push_back({.12 + x * .25, .14 + y * .35});
                int i = y * 4 + x;
                if (x < 3)
                    state.edges.push_back({i, i + 1});
                if (y < 2)
                    state.edges.push_back({i, i + 4});
                if (x < 3 && y < 2 && random(2))
                    state.edges.push_back({i, i + 5});
            }
        std::vector<int> order;
        for (int i = 0; i < 12; ++i)
            order.push_back(i);
        do {
            for (int i = 11; i > 0; --i)
                std::swap(order[i], order[random(i + 1)]);
            state.nodes.clear();
            for (int i = 0; i < 12; ++i) {
                double angle = order[i] * 6.283185307179586 / 12;
                state.nodes.push_back({.5 + .42 * std::cos(angle), .5 + .42 * std::sin(angle)});
            }
        } while (crossings() == 0);
        message = "Drag the points until every line is clear.";
    } else if (kind == PuzzleKind::cube)
        generate_cube();
    else if (kind == PuzzleKind::atom)
        generate_atoms();
    else if (kind == PuzzleKind::solve) {
        // Seven diagonal polyforms partition a 4x4 square. Four exact triangle
        // atoms per square preserve both shape and two-color artwork under D4.
        state.aux[95] = 3;
        for (int p = 0; p < 7; ++p) {
            state.aux[48 + p] = p;
            state.aux[64 + p] = random(4);
            state.aux[72 + p] = random(2);
            state.aux[80 + p] = 1 + random(2);
        }
        for (int p = 6; p > 0; --p)
            std::swap(state.aux[48 + p], state.aux[48 + random(p + 1)]);
        const int origins[7][2] = {{0, 0}, {0, 0}, {2, 2}, {2, 1}, {1, 2}, {3, 0}, {0, 3}};
        for (int p = 0; p < 7; ++p) {
            int source = state.aux[48 + p];
            state.solution_paths.push_back({p, origins[source][0], origins[source][1], 0, 0});
            for (const PieceCell& c : piece_cells(p, 0, false))
                state.secret[((c.y + origins[source][1]) * 4 + c.x + origins[source][0]) * 4 +
                             c.wedge] = c.color;
        }
        message = "Reconstruct the target with seven diagonal, two-color pieces.";
    } else if (kind == PuzzleKind::sticks) {
        std::vector<Point2> cells = hex_cells();
        for (int i = 0; i < static_cast<int>(cells.size()); ++i)
            state.secret[i] = random(3) == 0 ? 1 : 2 + random(6);
        message = "Fill every hex. Each straight line must match its stick and stone clues.";
    }
}
static std::array<bool, 96> gem_matches(const std::array<int, 96>& grid) {
    std::array<bool, 96> found{};
    for (int i = 0; i < 64; ++i)
        for (int direction : {1, 8}) {
            int end = direction == 1 ? i / 8 * 8 + 8 : 64;
            int count = 1, color = gem_color(grid[i]);
            if (!color)
                continue;
            while (i + count * direction < end && gem_color(grid[i + count * direction]) == color)
                ++count;
            if (count >= 3)
                for (int k = 0; k < count; ++k)
                    found[i + k * direction] = true;
        }
    return found;
}
bool PuzzleGame::gem_has_move() const {
    for (int i = 0; i < 64; ++i)
        for (int direction : {1, 8}) {
            int j = i + direction;
            if (!adjacent8(i, j))
                continue;
            if (state.grid[i] == 48 || state.grid[j] == 48)
                return true;
            std::array<int, 96> trial = state.grid;
            std::swap(trial[i], trial[j]);
            std::array<bool, 96> matches = gem_matches(trial);
            if (std::find(matches.begin(), matches.end(), true) != matches.end())
                return true;
        }
    return false;
}
bool PuzzleGame::resolve_gems(int preferred) {
    bool changed = false;
    for (int cascade = 0; cascade < 100; ++cascade) {
        std::array<bool, 96> remove = gem_matches(state.grid);
        if (std::find(remove.begin(), remove.end(), true) == remove.end())
            break;
        changed = true;
        std::array<int, 96> create{};
        std::array<bool, 96> visited{};
        // A connected same-color match creates at most one special.
        for (int i = 0; i < 64; ++i)
            if (remove[i] && !visited[i]) {
                std::vector<int> group{i};
                visited[i] = true;
                for (std::size_t n = 0; n < group.size(); ++n)
                    for (int j = 0; j < 64; ++j)
                        if (remove[j] && !visited[j] && adjacent8(group[n], j) &&
                            gem_color(state.grid[i]) == gem_color(state.grid[j])) {
                            visited[j] = true;
                            group.push_back(j);
                        }
                int longest = 0;
                bool horizontal = false, vertical = false;
                for (int cell : group)
                    for (int direction : {1, 8}) {
                        int count = 1, end = direction == 1 ? cell / 8 * 8 + 8 : 64;
                        while (cell + count * direction < end &&
                               gem_color(state.grid[cell + count * direction]) ==
                                   gem_color(state.grid[cell]))
                            ++count;
                        longest = std::max(longest, count);
                        if (count >= 3) {
                            horizontal = horizontal || direction == 1;
                            vertical = vertical || direction == 8;
                        }
                    }
                int at = group[group.size() / 2];
                if (std::find(group.begin(), group.end(), preferred) != group.end())
                    at = preferred;
                bool existing = false;
                for (int cell : group)
                    existing = existing || state.grid[cell] >= 16;
                if (!existing) {
                    if (longest >= 5)
                        create[at] = 48;
                    else if (horizontal && vertical)
                        create[at] = 32 + gem_color(state.grid[at]);
                    else if (longest == 4)
                        create[at] = 16 + gem_color(state.grid[at]);
                }
            }
        // All triggered specials participate, including those reached by another blast.
        std::array<bool, 96> triggered{};
        bool repeat = true;
        while (repeat) {
            repeat = false;
            for (int i = 0; i < 64; ++i)
                if (remove[i] && !triggered[i] && state.grid[i] >= 16) {
                    triggered[i] = true;
                    repeat = true;
                    int special = state.grid[i] / 16;
                    int hyper_color = special == 3 ? 1 + random(gem_colors()) : 0;
                    for (int j = 0; j < 64; ++j) {
                        bool blast = special == 1   ? std::abs(i / 8 - j / 8) <= 1 &&
                                                          std::abs(i % 8 - j % 8) <= 1
                                     : special == 2 ? (i / 8 == j / 8 || i % 8 == j % 8)
                                                    : gem_color(state.grid[j]) == hyper_color;
                        if (blast) {
                            remove[j] = true;
                            create[j] = 0;
                        }
                    }
                }
        }
        for (int i = 0; i < 64; ++i)
            if (remove[i]) {
                state.score += 10;
                state.grid[i] = create[i];
            }
        cascade_frames.push_back(state.grid);
        for (int x = 0; x < 8; ++x) {
            int destination = 7;
            for (int y = 7; y >= 0; --y)
                if (state.grid[y * 8 + x])
                    state.grid[destination-- * 8 + x] = state.grid[y * 8 + x];
            while (destination >= 0)
                state.grid[destination-- * 8 + x] = 1 + random(gem_colors());
        }
        cascade_frames.push_back(state.grid);
        preferred = -1;
    }
    return changed;
}
bool PuzzleGame::gem_swap(int a, int b) {
    if (state.over || !adjacent8(a, b))
        return false;
    cascade_frames.clear();
    std::swap(state.grid[a], state.grid[b]);
    cascade_frames.push_back(state.grid);
    bool valid = false;
    if (state.grid[a] == 48 || state.grid[b] == 48) {
        int color = gem_color(state.grid[state.grid[a] == 48 ? b : a]);
        std::array<bool, 96> remove{}, triggered{};
        bool both = state.grid[a] == 48 && state.grid[b] == 48;
        for (int i = 0; i < 64; ++i)
            remove[i] = both || gem_color(state.grid[i]) == color || i == a || i == b;
        bool again = true;
        while (again) {
            again = false;
            for (int i = 0; i < 64; ++i)
                if (remove[i] && !triggered[i] && state.grid[i] >= 16 && state.grid[i] < 48) {
                    triggered[i] = true;
                    again = true;
                    for (int j = 0; j < 64; ++j) {
                        bool blast = state.grid[i] < 32 ? std::abs(i / 8 - j / 8) <= 1 &&
                                                              std::abs(i % 8 - j % 8) <= 1
                                                        : i / 8 == j / 8 || i % 8 == j % 8;
                        if (blast)
                            remove[j] = true;
                    }
                }
        }
        for (int i = 0; i < 64; ++i)
            if (remove[i]) {
                state.grid[i] = 0;
                state.score += 10;
            }
        cascade_frames.push_back(state.grid);
        for (int x = 0; x < 8; ++x) {
            int d = 7;
            for (int y = 7; y >= 0; --y)
                if (state.grid[y * 8 + x])
                    state.grid[d-- * 8 + x] = state.grid[y * 8 + x];
            while (d >= 0)
                state.grid[d-- * 8 + x] = 1 + random(gem_colors());
        }
        cascade_frames.push_back(state.grid);
        valid = true;
    }
    valid = resolve_gems(b) || valid;
    if (!valid) {
        std::swap(state.grid[a], state.grid[b]);
        cascade_frames.push_back(state.grid);
        message = "That swap does not make a match.";
        return false;
    }
    ++state.moves;
    state.over = !gem_has_move();
    state.won = state.over;
    message = state.over ? "No more swaps. A lovely run!"
                         : "Match four for a bomb, five for a hypercube, or a T / L for a star.";
    return true;
}
static double orientation(Point2 a, Point2 b, Point2 c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
static bool on_segment(Point2 a, Point2 b, Point2 c) {
    return std::abs(orientation(a, b, c)) < 1e-8 && c.x >= std::min(a.x, b.x) - 1e-8 &&
           c.x <= std::max(a.x, b.x) + 1e-8 && c.y >= std::min(a.y, b.y) - 1e-8 &&
           c.y <= std::max(a.y, b.y) + 1e-8;
}
int PuzzleGame::crossings() const {
    int count = 0;
    for (std::size_t i = 0; i < state.edges.size(); ++i)
        for (std::size_t j = i + 1; j < state.edges.size(); ++j) {
            Edge a = state.edges[i], b = state.edges[j];
            if (a.a == b.a || a.a == b.b || a.b == b.a || a.b == b.b)
                continue;
            Point2 p = state.nodes[a.a], q = state.nodes[a.b], r = state.nodes[b.a],
                   s = state.nodes[b.b];
            if ((orientation(p, q, r) * orientation(p, q, s) < 0 &&
                 orientation(r, s, p) * orientation(r, s, q) < 0) ||
                on_segment(p, q, r) || on_segment(p, q, s) || on_segment(r, s, p) ||
                on_segment(r, s, q))
                ++count;
        }
    for (const Edge& edge : state.edges)
        for (int i = 0; i < static_cast<int>(state.nodes.size()); ++i)
            if (i != edge.a && i != edge.b &&
                on_segment(state.nodes[edge.a], state.nodes[edge.b], state.nodes[i]))
                ++count;
    for (std::size_t i = 0; i < state.nodes.size(); ++i)
        for (std::size_t j = i + 1; j < state.nodes.size(); ++j)
            if (std::hypot(state.nodes[i].x - state.nodes[j].x,
                           state.nodes[i].y - state.nodes[j].y) < .035)
                ++count;
    return count;
}
bool PuzzleGame::move_node(int node, Point2 position) {
    if (state.over || node < 0 || node >= static_cast<int>(state.nodes.size()))
        return false;
    state.nodes[node] = {std::clamp(position.x, .035, .965), std::clamp(position.y, .035, .965)};
    ++state.moves;
    state.over = crossings() == 0;
    state.won = state.over;
    message = state.over ? "Every line is clear. Beautifully untangled."
                         : "Drag a point to clear the crossing lines.";
    return true;
}
PegFeedback PuzzleGame::evaluate_pegs(const std::array<int, 4>& code,
                                      const std::array<int, 4>& guess) {
    PegFeedback result;
    std::array<int, 7> a{}, b{};
    for (int i = 0; i < 4; ++i)
        if (code[i] == guess[i])
            ++result.exact;
        else {
            if (code[i] >= 1 && code[i] <= 6)
                ++a[code[i]];
            if (guess[i] >= 1 && guess[i] <= 6)
                ++b[guess[i]];
        }
    for (int i = 1; i <= 6; ++i)
        result.misplaced += std::min(a[i], b[i]);
    return result;
}
bool PuzzleGame::guess_pegs(const std::array<int, 4>& guess) {
    if (state.over)
        return false;
    for (int v : guess)
        if (v < 1 || v > 6)
            return false;
    std::array<int, 4> code{};
    for (int i = 0; i < 4; ++i) {
        code[i] = state.secret[i];
        state.grid[state.stage * 4 + i] = guess[i];
    }
    PegFeedback feedback = evaluate_pegs(code, guess);
    state.aux[state.stage * 2] = feedback.exact;
    state.aux[state.stage * 2 + 1] = feedback.misplaced;
    ++state.stage;
    ++state.moves;
    state.won = feedback.exact == 4;
    state.over = state.won || state.stage == 10;
    message = state.won    ? "You found all four!"
              : state.over ? "The code is revealed. Try a fresh puzzle."
                           : "Solid marks: exact place. Rings: right symbol, another place.";
    return true;
}
Point3 PuzzleGame::cube_center(int cell) {
    double u = -.75 + (cell % 4) * .5, v = -.75 + ((cell % 16) / 4) * .5;
    switch (cell / 16) {
    case 0:
        return {u, v, 1};
    case 1:
        return {1, v, -u};
    case 2:
        return {-u, v, -1};
    case 3:
        return {-1, v, u};
    case 4:
        return {u, -1, v};
    default:
        return {u, 1, -v};
    }
}
bool PuzzleGame::cube_playable(int cell) {
    return cell >= 0 && cell < 96 && (cell / 16 == 0 || cell / 16 == 3 || cell / 16 == 4);
}
bool PuzzleGame::cube_adjacent(int a, int b) {
    if (a < 0 || b < 0 || a >= 96 || b >= 96 || a == b)
        return false;
    Point3 p = cube_center(a), q = cube_center(b);
    double d = std::abs(p.x - q.x) + std::abs(p.y - q.y) + std::abs(p.z - q.z);
    return std::abs(d - .5) < 1e-8;
}
void PuzzleGame::generate_cube() {
    state.secret.fill(0);
    state.marks.fill(0);
    state.aux[95] = 3;
    for (int pair = 1; pair <= 6; ++pair) {
        std::vector<int> best;
        for (int attempt = 0; attempt < 100; ++attempt) {
            const int faces[] = {0, 3, 4};
            int start = faces[(pair - 1) % 3] * 16 + random(16);
            if (state.secret[start])
                continue;
            std::vector<int> path{start};
            int length = 4 + random(5);
            for (int step = 1; step < length; ++step) {
                std::vector<int> choices;
                for (int j = 0; j < 96; ++j)
                    if (cube_playable(j) && !state.secret[j] && cube_adjacent(path.back(), j) &&
                        std::find(path.begin(), path.end(), j) == path.end())
                        choices.push_back(j);
                if (choices.empty())
                    break;
                path.push_back(choices[random(static_cast<int>(choices.size()))]);
            }
            if (path.size() > best.size())
                best = path;
            if (best.size() >= static_cast<std::size_t>(length))
                break;
        }
        if (best.size() < 2) {
            state.paths.clear();
            state.solution_paths.clear();
            generate_cube();
            return;
        }
        for (int cell : best)
            state.secret[cell] = pair;
        state.marks[best.front()] = pair;
        state.marks[best.back()] = pair;
        state.solution_paths.push_back(best);
        state.paths.push_back({});
    }
    message = "Connect six pairs across the three visible faces. Move the mouse to tilt the cube.";
}
int PuzzleGame::cube_pair(int cell) const {
    return cell >= 0 && cell < 96 ? state.marks[cell] : 0;
}
bool PuzzleGame::cube_start(int cell) {
    int pair = cube_pair(cell);
    if (state.over || !pair || (state.aux[95] == 3 && !cube_playable(cell)))
        return false;
    state.stage = pair;
    state.paths[pair - 1] = {cell};
    ++state.moves;
    return true;
}
bool PuzzleGame::cube_extend(int cell) {
    if (state.over || state.stage < 1 || state.stage > 6 || cell < 0 || cell >= 96 ||
        (state.aux[95] == 3 && !cube_playable(cell)))
        return false;
    std::vector<int>& path = state.paths[state.stage - 1];
    if (path.empty() || !cube_adjacent(path.back(), cell))
        return false;
    std::vector<int>::iterator found = std::find(path.begin(), path.end(), cell);
    if (found != path.end()) {
        path.erase(found + 1, path.end());
        return true;
    }
    if ((path.size() > 1 && cube_pair(path.back())) ||
        (cube_pair(cell) && cube_pair(cell) != state.stage))
        return false;
    for (int i = 0; i < 6; ++i)
        if (i != state.stage - 1 &&
            std::find(state.paths[i].begin(), state.paths[i].end(), cell) != state.paths[i].end())
            return false;
    path.push_back(cell);
    state.won = true;
    for (int i = 0; i < 6; ++i) {
        const std::vector<int>& p = state.paths[i];
        if (p.size() < 2 || cube_pair(p.front()) != i + 1 || cube_pair(p.back()) != i + 1)
            state.won = false;
    }
    state.over = state.won;
    if (state.over)
        message = "Every color found its way home.";
    return true;
}
bool PuzzleGame::cube_witness_valid() const {
    if (state.solution_paths.size() != 6)
        return false;
    std::set<int> occupied;
    for (int i = 0; i < 6; ++i) {
        const std::vector<int>& p = state.solution_paths[i];
        if (p.size() < 2 || cube_pair(p.front()) != i + 1 || cube_pair(p.back()) != i + 1)
            return false;
        for (std::size_t j = 0; j < p.size(); ++j)
            if ((state.aux[95] == 3 && !cube_playable(p[j])) || !occupied.insert(p[j]).second ||
                (j && !cube_adjacent(p[j - 1], p[j])))
                return false;
    }
    return true;
}
// Four by four, three atoms. -1 absorption, -2 reflection, otherwise exit port.
int PuzzleGame::atom_trace(int port, const std::array<int, 96>& board) const {
    int x = 0, y = 0, dx = 0, dy = 0;
    if (port < 0 || port >= 16)
        return -2;
    if (port < 4) {
        x = port;
        y = -1;
        dy = 1;
    } else if (port < 8) {
        x = 4;
        y = port - 4;
        dx = -1;
    } else if (port < 12) {
        x = 11 - port;
        y = 4;
        dy = -1;
    } else {
        x = -1;
        y = 15 - port;
        dx = 1;
    }
    std::set<std::array<int, 4>> visited;
    for (int step = 0; step < 256; ++step) {
        if (!visited.insert({x, y, dx, dy}).second)
            return -2;
        int nx = x + dx, ny = y + dy;
        if (nx >= 0 && nx < 4 && ny >= 0 && ny < 4 && board[ny * 4 + nx])
            return -1;
        int lx = nx - dy, ly = ny + dx, rx = nx + dy, ry = ny - dx;
        bool left = lx >= 0 && lx < 4 && ly >= 0 && ly < 4 && board[ly * 4 + lx];
        bool right = rx >= 0 && rx < 4 && ry >= 0 && ry < 4 && board[ry * 4 + rx];
        if (left || right) {
            if (x < 0 || x >= 4 || y < 0 || y >= 4 || (left && right))
                return -2;
            int old_dx = dx;
            if (left) {
                dx = dy;
                dy = -old_dx;
            } else {
                dx = -dy;
                dy = old_dx;
            }
            continue;
        }
        x = nx;
        y = ny;
        if (x < 0)
            return 15 - y == port ? -2 : 15 - y;
        if (x >= 4)
            return 4 + y == port ? -2 : 4 + y;
        if (y < 0)
            return x == port ? -2 : x;
        if (y >= 4)
            return 11 - x == port ? -2 : 11 - x;
    }
    return -2;
}
static std::array<int, 16> atom_signature(const PuzzleGame& game,
                                          const std::array<int, 96>& board) {
    std::array<int, 16> result{};
    for (int i = 0; i < 16; ++i)
        result[i] = game.atom_trace(i, board);
    return result;
}
void PuzzleGame::generate_atoms() {
    bool unique = false;
    while (!unique) {
        state.secret.fill(0);
        int count = 0;
        while (count < 3) {
            int i = random(16);
            if (!state.secret[i]) {
                state.secret[i] = 1;
                ++count;
            }
        }
        std::array<int, 16> signature = atom_signature(*this, state.secret);
        int matches = 0;
        for (int a = 0; a < 14; ++a)
            for (int b = a + 1; b < 15; ++b)
                for (int c = b + 1; c < 16; ++c) {
                    std::array<int, 96> board{};
                    board[a] = board[b] = board[c] = 1;
                    if (atom_signature(*this, board) == signature)
                        ++matches;
                }
        unique = matches == 1;
    }
    state.aux.fill(-99);
    message = "Three atoms are hidden. Probe the boundary, then mark your hypothesis.";
}
bool PuzzleGame::probe(int port) {
    if (state.over || port < 0 || port >= 16 || state.aux[port] != -99)
        return false;
    state.aux[port] = atom_trace(port, state.secret);
    ++state.moves;
    message = state.aux[port] == -1 ? "Absorbed: an atom lies directly ahead along the ray."
              : state.aux[port] == -2
                  ? "Reflected: the ray returns to its entry."
                  : "The ray emerged at port " + std::to_string(state.aux[port] + 1) + ".";
    return true;
}
bool PuzzleGame::mark_atom(int cell) {
    if (state.over || cell < 0 || cell >= 16)
        return false;
    state.grid[cell] = 1 - state.grid[cell];
    return true;
}
bool PuzzleGame::submit_atoms() {
    if (state.over)
        return false;
    int count = 0;
    for (int i = 0; i < 16; ++i)
        count += state.grid[i];
    if (count != 3) {
        message = "Mark exactly three atoms before submitting.";
        return false;
    }
    state.over = true;
    state.won = true;
    for (int i = 0; i < 16; ++i)
        if (state.grid[i] != state.secret[i])
            state.won = false;
    message = state.won ? "Precisely deduced. All three atoms found."
                        : "The atoms are revealed. Try a new configuration.";
    return true;
}
std::vector<PieceCell> PuzzleGame::piece_cells(int piece, int rotation, bool flip) const {
    if (state.aux[95] == 3) {
        if (piece < 0 || piece >= 7)
            return {};
        static const std::vector<Point2> polygons[] = {
            {{0, 0}, {4, 0}, {2, 2}},         {{0, 0}, {2, 2}, {0, 4}},
            {{4, 2}, {4, 4}, {2, 4}},         {{2, 2}, {3, 1}, {3, 3}},
            {{2, 2}, {3, 3}, {2, 4}, {1, 3}}, {{4, 0}, {4, 2}, {3, 3}, {3, 1}},
            {{0, 4}, {1, 3}, {2, 4}}};
        const auto& poly = polygons[state.aux[48 + piece]];
        const double dx[] = {.5, 5.0 / 6, .5, 1.0 / 6}, dy[] = {1.0 / 6, .5, 5.0 / 6, .5};
        std::vector<PieceCell> result;
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                for (int w = 0; w < 4; ++w) {
                    Point2 q{x + dx[w], y + dy[w]};
                    bool inside = true;
                    for (std::size_t j = 0; j < poly.size(); ++j) {
                        Point2 a = poly[j], b = poly[(j + 1) % poly.size()];
                        if ((b.x - a.x) * (q.y - a.y) - (b.y - a.y) * (q.x - a.x) < -1e-8)
                            inside = false;
                    }
                    if (!inside)
                        continue;
                    // A straight diagonal artwork division remains attached to the piece.
                    int mode = state.aux[64 + piece];
                    double v = mode == 0   ? q.x
                               : mode == 1 ? q.y
                               : mode == 2 ? q.x + q.y
                                           : q.x - q.y;
                    double threshold = mode < 2 ? 2 : mode == 2 ? 4 : 0;
                    int color = v < threshold ? state.aux[80 + piece] : 3 - state.aux[80 + piece];
                    PieceCell c{x, y, color, w};
                    if (flip) {
                        c.x = -c.x - 1;
                        c.wedge = (4 - c.wedge) % 4;
                    }
                    for (int r = 0; r < ((rotation % 4) + 4) % 4; ++r) {
                        int xx = c.x;
                        c.x = -c.y - 1;
                        c.y = xx;
                        c.wedge = (c.wedge + 1) % 4;
                    }
                    result.push_back(c);
                }
        int minx = 10, miny = 10;
        for (const auto& c : result) {
            minx = std::min(minx, c.x);
            miny = std::min(miny, c.y);
        }
        for (auto& c : result) {
            c.x -= minx;
            c.y -= miny;
        }
        return result;
    }
    if (piece < 0 || piece >= 12)
        return {};
    int source = state.aux[95] == 2 ? state.aux[48 + piece] : piece;
    bool straight = state.aux[95] == 2 && state.aux[36 + source / 2];
    std::vector<PieceCell> cells = source % 2 == 0
                                       ? std::vector<PieceCell>{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}
                                       : std::vector<PieceCell>{{1, 1, 0}, {0, 2, 0}, {1, 2, 0}};
    if (straight)
        cells = {{source % 2, 0, 0}, {source % 2, 1, 0}, {source % 2, 2, 0}};
    for (int i = 0; i < 3; ++i) {
        cells[i].color = state.aux[piece * 3 + i];
        if (flip)
            cells[i].x = -cells[i].x;
        for (int r = 0; r < rotation % 4; ++r) {
            int x = cells[i].x;
            cells[i].x = -cells[i].y;
            cells[i].y = x;
        }
    }
    // Canonical generation needs complementary offsets; placement uses normalized forms.
    if (rotation || flip) {
        int minx = 10, miny = 10;
        for (const PieceCell& c : cells) {
            minx = std::min(minx, c.x);
            miny = std::min(miny, c.y);
        }
        for (PieceCell& c : cells) {
            c.x -= minx;
            c.y -= miny;
        }
    }
    return cells;
}
bool PuzzleGame::place_piece(int piece, int x, int y, int rotation, bool flip) {
    if (state.over || piece < 0 || piece >= piece_count())
        return false;
    auto cells = piece_cells(piece, rotation, flip);
    int size = state.aux[95] == 3 ? 4 : 6, atoms = state.aux[95] == 3 ? 4 : 1;
    int minx = 10, miny = 10;
    for (const auto& c : cells) {
        minx = std::min(minx, c.x);
        miny = std::min(miny, c.y);
    }
    for (auto& c : cells) {
        c.x -= minx;
        c.y -= miny;
    }
    for (const auto& c : cells) {
        int xx = x + c.x, yy = y + c.y, i = (yy * size + xx) * atoms + c.wedge;
        if (xx < 0 || xx >= size || yy < 0 || yy >= size ||
            (state.grid[i] && state.grid[i] / 4 != piece + 1))
            return false;
    }
    for (int i = 0; i < size * size * atoms; ++i)
        if (state.grid[i] / 4 == piece + 1)
            state.grid[i] = 0;
    for (const auto& c : cells)
        state.grid[((y + c.y) * size + x + c.x) * atoms + c.wedge] = (piece + 1) * 4 + c.color;
    ++state.moves;
    state.won = true;
    for (int i = 0; i < size * size * atoms; ++i)
        if (state.grid[i] % 4 != state.secret[i])
            state.won = false;
    state.over = state.won;
    message = state.won ? "A perfect reconstruction."
                        : "Rotate, flip, and fit the blue and yellow pieces.";
    return true;
}
bool PuzzleGame::remove_piece(int piece) {
    if (state.over || piece < 0 || piece >= piece_count())
        return false;
    bool changed = false;
    for (int i = 0; i < 96; ++i)
        if (state.grid[i] / 4 == piece + 1) {
            state.grid[i] = 0;
            changed = true;
        }
    return changed;
}
std::vector<Point2> PuzzleGame::hex_cells() {
    std::vector<Point2> cells;
    for (int r = -3; r <= 3; ++r)
        for (int q = -3; q <= 3; ++q)
            if (std::abs(q + r) <= 3)
                cells.push_back({static_cast<double>(q), static_cast<double>(r)});
    return cells;
}
std::array<int, 2> PuzzleGame::hex_count(int axis, int line, bool target) const {
    std::array<int, 2> count{};
    std::vector<Point2> cells = hex_cells();
    for (std::size_t i = 0; i < cells.size(); ++i) {
        int coordinate = static_cast<int>(axis == 0   ? cells[i].x
                                          : axis == 1 ? cells[i].y
                                                      : -cells[i].x - cells[i].y);
        if (coordinate == line) {
            int value = target ? state.secret[i] : state.grid[i];
            if (value)
                ++count[value == 1 ? 1 : 0];
        }
    }
    return count;
}
bool PuzzleGame::set_stick(int cell, int value) {
    if (state.over || cell < 0 || cell >= 37 || value < 0 || value > 7)
        return false;
    if (state.grid[cell] == value)
        return false;
    state.grid[cell] = value;
    ++state.moves;
    state.won = true;
    for (int axis = 0; axis < 3; ++axis)
        for (int line = -3; line <= 3; ++line)
            if (hex_count(axis, line, false) != hex_count(axis, line, true))
                state.won = false;
    state.over = state.won;
    message = state.won ? "Every line balances. The garden is complete."
                        : "Perimeter clues read sticks / stones. Any arrangement that satisfies "
                          "every line wins.";
    return true;
}
int PuzzleGame::result() const {
    return kind == PuzzleKind::gems ? state.score : state.moves;
}
bool PuzzleGame::qualifies() const {
    return state.over && state.won && !recorded &&
           (scores.size() < 10 || (kind == PuzzleKind::gems ? result() > scores.back().value
                                                            : result() < scores.back().value));
}
bool PuzzleGame::record(const std::string& name) {
    if (!qualifies() || !valid_score_name(name))
        return false;
    std::vector<TopScore>::iterator it = scores.begin();
    while (it != scores.end() &&
           (kind == PuzzleKind::gems ? (*it).value >= result() : (*it).value <= result()))
        ++it;
    scores.insert(it, {name, result()});
    if (scores.size() > 10)
        scores.pop_back();
    recorded = true;
    player_name = name;
    return true;
}
bool PuzzleGame::invariant() const {
    if (static_cast<int>(kind) < 0 || static_cast<int>(kind) > 7 || !state.random_state ||
        state.moves < 0 || state.moves > 10000000 || state.score < 0 || state.score > 100000000 ||
        state.stage < 0 || state.stage > 10 || state.progress < 0 || state.progress > 5 ||
        state.won && !state.over)
        return false;
    for (int i = 0; i < 96; ++i)
        if (state.grid[i] < 0 || state.grid[i] > 127 || state.secret[i] < 0 ||
            state.secret[i] > 16 || state.marks[i] < 0 || state.marks[i] > 6 ||
            state.aux[i] < -99 || state.aux[i] > 100)
            return false;
    if (kind == PuzzleKind::cube) {
        if (!cube_witness_valid() || state.paths.size() != 6)
            return false;
        std::set<int> used;
        for (int pair = 0; pair < 6; ++pair) {
            const std::vector<int>& path = state.paths[pair];
            if (!path.empty() && cube_pair(path.front()) != pair + 1)
                return false;
            for (std::size_t j = 0; j < path.size(); ++j)
                if (path[j] < 0 || path[j] >= 96 || !used.insert(path[j]).second ||
                    (j && !cube_adjacent(path[j - 1], path[j])))
                    return false;
        }
    }
    if (kind == PuzzleKind::untangle) {
        if (state.nodes.size() != 12 || state.embedding.size() != 12)
            return false;
        for (Point2 p : state.nodes)
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < 0 || p.x > 1 || p.y < 0 ||
                p.y > 1)
                return false;
        for (Edge e : state.edges)
            if (e.a < 0 || e.b < 0 || e.a >= 12 || e.b >= 12)
                return false;
    }
    if (kind == PuzzleKind::solve && state.aux[95] == 3) {
        std::set<int> sources;
        for (int p = 0; p < 7; ++p) {
            if (state.aux[48 + p] < 0 || state.aux[48 + p] >= 7 ||
                !sources.insert(state.aux[48 + p]).second || state.aux[64 + p] < 0 ||
                state.aux[64 + p] > 3 || state.aux[80 + p] < 1 || state.aux[80 + p] > 2)
                return false;
        }
        for (int i = 0; i < 64; ++i) {
            int v = state.grid[i];
            if (state.secret[i] < 1 || state.secret[i] > 2 ||
                (v && (v / 4 < 1 || v / 4 > 7 || v % 4 < 1 || v % 4 > 2)))
                return false;
        }
        if (state.solution_paths.size() != 7)
            return false;
        PuzzleGame witness = *this;
        witness.state.grid = {};
        witness.state.over = witness.state.won = false;
        std::set<int> pieces;
        for (const auto& w : state.solution_paths) {
            if (w.size() != 5 || !pieces.insert(w[0]).second || w[3] < 0 || w[3] > 3 || w[4] < 0 ||
                w[4] > 1 || !witness.place_piece(w[0], w[1], w[2], w[3], w[4]))
                return false;
        }
        if (!witness.state.won)
            return false;
    }
    if (kind == PuzzleKind::pegs) {
        for (int i = 0; i < 4; ++i)
            if (state.secret[i] < 1 || state.secret[i] > 6)
                return false;
    }
    return true;
}
static std::uint64_t checksum(const std::string& text) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}
bool PuzzleGame::save(const std::filesystem::path& path) const {
    if (!invariant())
        return false;
    std::ostringstream out;
    out << std::setprecision(17) << static_cast<int>(kind) << ' ' << state.seed << ' '
        << state.random_state << ' ' << state.moves << ' ' << state.score << ' ' << state.stage
        << ' ' << state.progress << ' ' << state.over << ' ' << state.won << ' ' << recorded << '\n'
        << std::quoted(player_name) << '\n';
    for (int i = 0; i < 96; ++i)
        out << state.grid[i] << ' ' << state.secret[i] << ' ' << state.marks[i] << ' '
            << state.aux[i] << '\n';
    out << state.nodes.size() << '\n';
    for (Point2 p : state.nodes)
        out << p.x << ' ' << p.y << '\n';
    out << state.embedding.size() << '\n';
    for (Point2 p : state.embedding)
        out << p.x << ' ' << p.y << '\n';
    out << state.edges.size() << '\n';
    for (Edge e : state.edges)
        out << e.a << ' ' << e.b << '\n';
    for (const std::vector<std::vector<int>>& paths : {state.paths, state.solution_paths}) {
        out << paths.size() << '\n';
        for (const std::vector<int>& p : paths) {
            out << p.size();
            for (int cell : p)
                out << ' ' << cell;
            out << '\n';
        }
    }
    out << scores.size() << '\n';
    for (const TopScore& score : scores)
        out << std::quoted(score.name) << ' ' << score.value << '\n';
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        return false;
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::ofstream file(temporary);
    file << "RAINSTAR_PUZZLE 1 " << checksum(out.str()) << '\n' << out.str();
    file.close();
    if (!file)
        return false;
    std::filesystem::rename(temporary, path, error);
    return !error;
}
bool PuzzleGame::load(const std::filesystem::path& path) {
    std::error_code error;
    if (std::filesystem::file_size(path, error) > 65536 || error)
        return false;
    std::ifstream file(path);
    std::string magic;
    int version;
    std::uint64_t hash;
    file >> magic >> version >> hash;
    if (!file || magic != "RAINSTAR_PUZZLE" || version != 1)
        return false;
    file.get();
    std::ostringstream raw;
    raw << file.rdbuf();
    if (checksum(raw.str()) != hash)
        return false;
    std::istringstream in(raw.str());
    PuzzleGame candidate(kind);
    PuzzleState& s = candidate.state;
    int k;
    in >> k >> s.seed >> s.random_state >> s.moves >> s.score >> s.stage >> s.progress >> s.over >>
        s.won >> candidate.recorded >> std::quoted(candidate.player_name);
    if (k != static_cast<int>(kind) || !valid_score_name(candidate.player_name))
        return false;
    for (int i = 0; i < 96; ++i)
        in >> s.grid[i] >> s.secret[i] >> s.marks[i] >> s.aux[i];
    int size = 0;
    in >> size;
    if (size < 0 || size > 96)
        return false;
    for (int i = 0; i < size; ++i) {
        Point2 p;
        in >> p.x >> p.y;
        s.nodes.push_back(p);
    }
    in >> size;
    if (size < 0 || size > 96)
        return false;
    for (int i = 0; i < size; ++i) {
        Point2 p;
        in >> p.x >> p.y;
        s.embedding.push_back(p);
    }
    in >> size;
    if (size < 0 || size > 300)
        return false;
    for (int i = 0; i < size; ++i) {
        Edge e;
        in >> e.a >> e.b;
        s.edges.push_back(e);
    }
    for (int which = 0; which < 2; ++which) {
        int count = 0;
        in >> count;
        if (count < 0 || count > (kind == PuzzleKind::solve ? 7 : 6))
            return false;
        std::vector<std::vector<int>>& paths = which == 0 ? s.paths : s.solution_paths;
        for (int i = 0; i < count; ++i) {
            in >> size;
            if (size < 0 || size > 96)
                return false;
            std::vector<int> p;
            for (int j = 0; j < size; ++j) {
                int cell;
                in >> cell;
                p.push_back(cell);
            }
            paths.push_back(p);
        }
    }
    in >> size;
    if (size < 0 || size > 10)
        return false;
    for (int i = 0; i < size; ++i) {
        TopScore entry;
        in >> std::quoted(entry.name) >> entry.value;
        if (!valid_score_name(entry.name) || entry.value < 0 || entry.value > 100000000)
            return false;
        candidate.scores.push_back(entry);
    }
    if (!in || !candidate.invariant())
        return false;
    in >> std::ws;
    if (!in.eof())
        return false;
    *this = std::move(candidate);
    message = "Welcome back. Your current game was saved.";
    return true;
}
} // namespace games
