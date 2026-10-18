#include "rules.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace sc {
namespace {

// The two base squares, orthogonal: over the integers mod a prime n, (r + c) and
// (2r + c); over the field of four, r + c and alpha r + c, where adding is exclusive or.
int base_shape(int n, int r, int c) {
    return n == 4 ? (r ^ c) : (r + c) % n;
}

int base_material(int n, int r, int c) {
    static constexpr int alpha_times[4] = {0, 2, 3, 1};
    return n == 4 ? (alpha_times[r] ^ c) : (2 * r + c) % n;
}

void shuffle(std::vector<int>& values, std::uint64_t& random) {
    for (int i = static_cast<int>(values.size()) - 1; i > 0; --i) {
        const int j = next_below(random, i + 1);
        std::swap(values[static_cast<std::size_t>(i)], values[static_cast<std::size_t>(j)]);
    }
}

std::vector<int> order(int count) {
    std::vector<int> values(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        values[static_cast<std::size_t>(i)] = i;
    return values;
}

// The exact search behind uniqueness: fills the empty sockets one at a time, always the
// one with fewest stones left that fit, and counts the complete squares it reaches.
struct Search {
    int n = 0;
    std::array<int, largest> row_shape{}, row_material{}, column_shape{}, column_material{};
    std::uint64_t used = 0;                   // stones already placed, by number
    std::array<bool, most_stones> filled{};
    int empty = 0;
    int found = 0;
    int limit = 2;
    long long steps = 0;
    long long budget = 4000000;               // beyond this, the answer is not proven
};

bool fits(const Search& s, int cell, int shape, int material) {
    const int r = cell / s.n;
    const int c = cell % s.n;
    const int stone = shape * s.n + material;
    return ((s.row_shape[r] | s.column_shape[c]) & (1 << shape)) == 0 &&
           ((s.row_material[r] | s.column_material[c]) & (1 << material)) == 0 && (s.used & (1ULL << stone)) == 0;
}

void place(Search& s, int cell, int shape, int material, bool on) {
    const int r = cell / s.n;
    const int c = cell % s.n;
    const int stone = shape * s.n + material;
    s.row_shape[r] ^= 1 << shape;
    s.column_shape[c] ^= 1 << shape;
    s.row_material[r] ^= 1 << material;
    s.column_material[c] ^= 1 << material;
    s.used ^= 1ULL << stone;
    s.filled[static_cast<std::size_t>(cell)] = on;
    s.empty += on ? -1 : 1;
}

void search(Search& s) {
    if (s.found >= s.limit || s.steps > s.budget)
        return;
    ++s.steps;
    if (s.empty == 0) {
        ++s.found;
        return;
    }
    // The empty socket with fewest stones that fit.
    int best_cell = -1;
    int best_count = 1 << 30;
    for (int cell = 0; cell < s.n * s.n; ++cell) {
        if (s.filled[static_cast<std::size_t>(cell)])
            continue;
        int count = 0;
        for (int shape = 0; shape < s.n; ++shape)
            for (int material = 0; material < s.n; ++material)
                if (fits(s, cell, shape, material))
                    ++count;
        if (count < best_count) {
            best_count = count;
            best_cell = cell;
            if (count == 0)
                return;
        }
    }
    for (int shape = 0; shape < s.n; ++shape)
        for (int material = 0; material < s.n; ++material) {
            if (!fits(s, best_cell, shape, material))
                continue;
            place(s, best_cell, shape, material, true);
            search(s);
            place(s, best_cell, shape, material, false);
            if (s.found >= s.limit || s.steps > s.budget)
                return;
        }
}

// The fewest clues a cabinet keeps: a floor under the thinning, so the start has a hold.
int clue_floor(int n) {
    return n == 4 ? 4 : n == 5 ? 6 : 13;
}

void fill_pile(Session& session, Outcome& outcome) {
    while (static_cast<int>(session.pile.size()) < pile_size && !session.drum.empty()) {
        const int stone = session.drum.front();
        session.drum.erase(session.drum.begin());
        session.pile.push_back(stone);
        session.where[static_cast<std::size_t>(stone)] = Where{Place::pile, -1, 0, 0};
        outcome.dropped.push_back(stone);
    }
}

void leave_pile(Session& session, int stone) {
    std::vector<int>::iterator found = std::find(session.pile.begin(), session.pile.end(), stone);
    if (found != session.pile.end())
        session.pile.erase(found);
}

int stone_count(const Session& session) {
    return session.puzzle.n * session.puzzle.n;
}

void finish(Session& session, Outcome& outcome) {
    if (!cabinet_full(session) || !clashes(session).empty())
        return;
    session.won = true;
    outcome.won = true;
    Best& best = session.best[static_cast<std::size_t>(session.puzzle.n)];
    const int seconds = static_cast<int>(std::lround(session.seconds));
    if (best.moves == 0 || session.moves < best.moves || (session.moves == best.moves && seconds < best.seconds)) {
        best.moves = session.moves;
        best.seconds = seconds;
        outcome.new_best = true;
    }
}

bool valid_size(int n) {
    return std::find(sizes.begin(), sizes.end(), n) != sizes.end();
}

// Every row and column holds each shape and each material once, and each stone is in
// the square once.
bool graeco_latin(const Puzzle& p) {
    const int n = p.n;
    std::uint64_t stones = 0;
    for (int i = 0; i < n; ++i) {
        int row_shape = 0, row_material = 0, column_shape = 0, column_material = 0;
        for (int j = 0; j < n; ++j) {
            row_shape |= 1 << p.shape[static_cast<std::size_t>(i * n + j)];
            row_material |= 1 << p.material[static_cast<std::size_t>(i * n + j)];
            column_shape |= 1 << p.shape[static_cast<std::size_t>(j * n + i)];
            column_material |= 1 << p.material[static_cast<std::size_t>(j * n + i)];
            stones |= 1ULL << solution_stone(p, i * n + j);
        }
        const int all = (1 << n) - 1;
        if (row_shape != all || row_material != all || column_shape != all || column_material != all)
            return false;
    }
    return stones == (n * n == 64 ? ~0ULL : (1ULL << (n * n)) - 1);
}

}  // namespace

std::uint64_t next_random(std::uint64_t& state) {
    state += 0x9e3779b97f4a7c15ULL;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

int next_below(std::uint64_t& state, int bound) {
    return bound <= 1 ? 0 : static_cast<int>(next_random(state) % static_cast<std::uint64_t>(bound));
}

int stone_shape(const Puzzle& puzzle, int stone) {
    return stone / puzzle.n;
}

int stone_material(const Puzzle& puzzle, int stone) {
    return stone % puzzle.n;
}

int solution_stone(const Puzzle& puzzle, int cell) {
    return puzzle.shape[static_cast<std::size_t>(cell)] * puzzle.n + puzzle.material[static_cast<std::size_t>(cell)];
}

int count_solutions(const Puzzle& puzzle, const std::array<int, most_stones>& stone_at, int limit) {
    Search s;
    s.n = puzzle.n;
    s.limit = limit;
    s.empty = puzzle.n * puzzle.n;
    for (int cell = 0; cell < puzzle.n * puzzle.n; ++cell) {
        const int stone = stone_at[static_cast<std::size_t>(cell)];
        if (stone < 0)
            continue;
        const int shape = stone_shape(puzzle, stone);
        const int material = stone_material(puzzle, stone);
        if (!fits(s, cell, shape, material))
            return 0;
        place(s, cell, shape, material, true);
    }
    search(s);
    // Beyond the budget nothing is proven: say there may be more than one.
    return s.steps > s.budget ? limit : s.found;
}

Puzzle make_puzzle(std::uint64_t seed, int n) {
    Puzzle p;
    p.n = valid_size(n) ? n : 5;
    p.seed = seed;
    n = p.n;
    std::uint64_t random = seed ^ 0x5bd1e9955bd1e995ULL;
    std::vector<int> rows = order(n);
    std::vector<int> columns = order(n);
    std::vector<int> shapes = order(n);
    std::vector<int> materials = order(n);
    shuffle(rows, random);
    shuffle(columns, random);
    shuffle(shapes, random);
    shuffle(materials, random);
    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c) {
            const int br = rows[static_cast<std::size_t>(r)];
            const int bc = columns[static_cast<std::size_t>(c)];
            p.shape[static_cast<std::size_t>(r * n + c)] = shapes[static_cast<std::size_t>(base_shape(n, br, bc))];
            p.material[static_cast<std::size_t>(r * n + c)] = materials[static_cast<std::size_t>(base_material(n, br, bc))];
        }
    // Thin the clues while the answer stays the only one.
    std::array<int, most_stones> stone_at{};
    stone_at.fill(-1);
    for (int cell = 0; cell < n * n; ++cell) {
        p.clue[static_cast<std::size_t>(cell)] = true;
        stone_at[static_cast<std::size_t>(cell)] = solution_stone(p, cell);
    }
    std::vector<int> cells = order(n * n);
    shuffle(cells, random);
    int clues = n * n;
    for (int cell : cells) {
        if (clues <= clue_floor(n))
            break;
        stone_at[static_cast<std::size_t>(cell)] = -1;
        if (count_solutions(p, stone_at, 2) == 1) {
            p.clue[static_cast<std::size_t>(cell)] = false;
            --clues;
        } else {
            stone_at[static_cast<std::size_t>(cell)] = solution_stone(p, cell);
        }
    }
    // The looks: every style once before any repeats, and n different materials.
    std::vector<int> styles = order(style_choices);
    shuffle(styles, random);
    std::vector<int> kinds = order(material_choices);
    shuffle(kinds, random);
    for (int k = 0; k < n; ++k) {
        p.family_style[static_cast<std::size_t>(k)] = styles[static_cast<std::size_t>(k % style_choices)];
        p.family_seed[static_cast<std::size_t>(k)] = static_cast<std::uint32_t>(next_random(random) & 0x7fffffffU) | 1U;
        p.material_kind[static_cast<std::size_t>(k)] = kinds[static_cast<std::size_t>(k)];
        p.material_seed[static_cast<std::size_t>(k)] = static_cast<std::uint32_t>(next_random(random) & 0x7fffffffU) | 1U;
    }
    return p;
}

void new_game(Session& session, std::uint64_t seed, int n) {
    const std::array<Best, largest + 1> best = session.best;
    session = Session{};
    session.best = best;
    session.puzzle = make_puzzle(seed, n);
    session.next_n = session.puzzle.n;
    session.playing = true;
    const Puzzle& p = session.puzzle;
    std::uint64_t random = seed ^ 0x2545f4914f6cdd1dULL;
    std::vector<int> rest;
    for (int cell = 0; cell < p.n * p.n; ++cell) {
        const int stone = solution_stone(p, cell);
        if (p.clue[static_cast<std::size_t>(cell)])
            session.where[static_cast<std::size_t>(stone)] = Where{Place::cabinet, cell, 0, 0};
        else
            rest.push_back(stone);
    }
    shuffle(rest, random);
    session.drum = rest;
    for (int stone = 0; stone < p.n * p.n; ++stone)
        session.turn[static_cast<std::size_t>(stone)] = next_below(random, 8);
    Outcome ignored;
    fill_pile(session, ignored);
}

int stone_in(const Session& session, int cell) {
    for (int stone = 0; stone < stone_count(session); ++stone) {
        const Where& w = session.where[static_cast<std::size_t>(stone)];
        if (w.place == Place::cabinet && w.cell == cell)
            return stone;
    }
    return -1;
}

bool movable(const Session& session, int stone) {
    if (stone < 0 || stone >= stone_count(session) || session.won)
        return false;
    const Where& w = session.where[static_cast<std::size_t>(stone)];
    if (w.place == Place::drum)
        return false;
    return !(w.place == Place::cabinet && session.puzzle.clue[static_cast<std::size_t>(w.cell)]);
}

Outcome set_in_cabinet(Session& session, int stone, int cell) {
    Outcome outcome;
    const int n = session.puzzle.n;
    if (!movable(session, stone) || cell < 0 || cell >= n * n)
        return outcome;
    const int occupant = stone_in(session, cell);
    if (occupant == stone || (occupant >= 0 && session.puzzle.clue[static_cast<std::size_t>(cell)]))
        return outcome;
    const Where from = session.where[static_cast<std::size_t>(stone)];
    outcome.accepted = true;
    if (occupant >= 0) {
        // The stone there goes where this one came from.
        outcome.displaced = occupant;
        session.where[static_cast<std::size_t>(occupant)] = from;
        if (from.place == Place::pile)
            std::replace(session.pile.begin(), session.pile.end(), stone, occupant);
    } else if (from.place == Place::pile) {
        leave_pile(session, stone);
    }
    session.where[static_cast<std::size_t>(stone)] = Where{Place::cabinet, cell, 0, 0};
    ++session.moves;
    fill_pile(session, outcome);
    finish(session, outcome);
    return outcome;
}

Outcome set_on_felt(Session& session, int stone, double x, double y) {
    Outcome outcome;
    if (!movable(session, stone))
        return outcome;
    outcome.accepted = true;
    if (session.where[static_cast<std::size_t>(stone)].place == Place::pile)
        leave_pile(session, stone);
    session.where[static_cast<std::size_t>(stone)] = Where{Place::felt, -1, std::clamp(x, 0.0, 1.0), std::clamp(y, 0.0, 1.0)};
    fill_pile(session, outcome);
    return outcome;
}

std::vector<int> clashes(const Session& session) {
    const Puzzle& p = session.puzzle;
    const int n = p.n;
    std::array<int, most_stones> at{};
    at.fill(-1);
    for (int stone = 0; stone < n * n; ++stone) {
        const Where& w = session.where[static_cast<std::size_t>(stone)];
        if (w.place == Place::cabinet)
            at[static_cast<std::size_t>(w.cell)] = stone;
    }
    std::array<bool, most_stones> bad{};
    for (int a = 0; a < n * n; ++a) {
        const int sa = at[static_cast<std::size_t>(a)];
        if (sa < 0)
            continue;
        for (int b = a + 1; b < n * n; ++b) {
            const int sb = at[static_cast<std::size_t>(b)];
            if (sb < 0 || (a / n != b / n && a % n != b % n))
                continue;
            if (stone_shape(p, sa) == stone_shape(p, sb) || stone_material(p, sa) == stone_material(p, sb)) {
                bad[static_cast<std::size_t>(a)] = true;
                bad[static_cast<std::size_t>(b)] = true;
            }
        }
    }
    std::vector<int> cells;
    for (int cell = 0; cell < n * n; ++cell)
        if (bad[static_cast<std::size_t>(cell)])
            cells.push_back(cell);
    return cells;
}

bool cabinet_full(const Session& session) {
    for (int stone = 0; stone < stone_count(session); ++stone)
        if (session.where[static_cast<std::size_t>(stone)].place != Place::cabinet)
            return false;
    return true;
}

std::string encode_session(const Session& session) {
    std::ostringstream out;
    out << "version=1\n";
    out << "n=" << session.puzzle.n << "\nseed=" << session.puzzle.seed << "\n";
    out << "playing=" << (session.playing ? 1 : 0) << "\nwon=" << (session.won ? 1 : 0) << "\n";
    out << "moves=" << session.moves << "\nseconds=" << static_cast<long long>(std::lround(session.seconds)) << "\n";
    out << "next=" << session.next_n << "\n";
    // The puzzle itself, so a reload never has to make it again.
    const Puzzle& p = session.puzzle;
    out << "square=";
    for (int cell = 0; cell < p.n * p.n; ++cell)
        out << (cell ? "," : "") << p.shape[static_cast<std::size_t>(cell)] * 100 + p.material[static_cast<std::size_t>(cell)] * 10 +
                                       (p.clue[static_cast<std::size_t>(cell)] ? 1 : 0);
    out << "\nlooks=";
    for (int k = 0; k < p.n; ++k)
        out << (k ? "," : "") << p.family_seed[static_cast<std::size_t>(k)] << "," << p.family_style[static_cast<std::size_t>(k)] << ","
            << p.material_kind[static_cast<std::size_t>(k)] << "," << p.material_seed[static_cast<std::size_t>(k)];
    out << "\n";
    for (int size : sizes) {
        const Best& b = session.best[static_cast<std::size_t>(size)];
        out << "best=" << size << "," << b.moves << "," << b.seconds << "\n";
    }
    for (int stone = 0; stone < stone_count(session); ++stone) {
        const Where& w = session.where[static_cast<std::size_t>(stone)];
        out << "stone=" << stone << "," << static_cast<int>(w.place) << "," << w.cell << ","
            << static_cast<long long>(std::lround(w.x * 10000)) << "," << static_cast<long long>(std::lround(w.y * 10000)) << ","
            << session.turn[static_cast<std::size_t>(stone)] << "\n";
    }
    out << "drum=";
    for (std::size_t i = 0; i < session.drum.size(); ++i)
        out << (i ? "," : "") << session.drum[i];
    out << "\npile=";
    for (std::size_t i = 0; i < session.pile.size(); ++i)
        out << (i ? "," : "") << session.pile[i];
    out << "\n";
    return out.str();
}

namespace {

bool parse_numbers(const std::string& text, std::vector<long long>& out) {
    out.clear();
    if (text.empty())
        return true;
    std::istringstream in(text);
    std::string item;
    while (std::getline(in, item, ',')) {
        if (item.empty() || item.size() > 19)
            return false;
        std::size_t used = 0;
        long long value = 0;
        try {
            value = std::stoll(item, &used);
        } catch (...) {
            return false;
        }
        if (used != item.size())
            return false;
        out.push_back(value);
    }
    return true;
}

}  // namespace

bool decode_session(const std::string& text, Session& destination) {
    Session s;
    std::istringstream in(text);
    std::string line;
    long long n = 0;
    long long seed = -1;
    bool version = false;
    std::vector<std::vector<long long>> stones;
    std::vector<long long> drum;
    std::vector<long long> pile;
    std::vector<long long> square;
    std::vector<long long> looks;
    std::vector<long long> v;
    while (std::getline(in, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos)
            return false;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (!parse_numbers(value, v))
            return false;
        if (key == "version") {
            version = v.size() == 1 && v[0] == 1;
        } else if (key == "n" && v.size() == 1) {
            n = v[0];
        } else if (key == "seed" && v.size() == 1) {
            seed = v[0];
        } else if (key == "playing" && v.size() == 1) {
            s.playing = v[0] == 1;
        } else if (key == "won" && v.size() == 1) {
            s.won = v[0] == 1;
        } else if (key == "moves" && v.size() == 1 && v[0] >= 0 && v[0] < 1000000) {
            s.moves = static_cast<int>(v[0]);
        } else if (key == "seconds" && v.size() == 1 && v[0] >= 0 && v[0] < 100000000) {
            s.seconds = static_cast<double>(v[0]);
        } else if (key == "next" && v.size() == 1 && valid_size(static_cast<int>(v[0]))) {
            s.next_n = static_cast<int>(v[0]);
        } else if (key == "best" && v.size() == 3 && valid_size(static_cast<int>(v[0])) && v[1] >= 0 && v[2] >= 0 &&
                   v[1] < 1000000 && v[2] < 100000000) {
            s.best[static_cast<std::size_t>(v[0])] = Best{static_cast<int>(v[1]), static_cast<int>(v[2])};
        } else if (key == "stone" && v.size() == 6) {
            stones.push_back(v);
        } else if (key == "square") {
            square = v;
        } else if (key == "looks") {
            looks = v;
        } else if (key == "drum") {
            drum = v;
        } else if (key == "pile") {
            pile = v;
        } else {
            return false;
        }
    }
    if (!version || !valid_size(static_cast<int>(n)) || seed < 0)
        return false;
    const int count = static_cast<int>(n * n);
    // The puzzle as saved: a Graeco-Latin square, its clues, and looks the engine has.
    if (static_cast<int>(square.size()) != count || static_cast<int>(looks.size()) != 4 * n)
        return false;
    s.puzzle.n = static_cast<int>(n);
    s.puzzle.seed = static_cast<std::uint64_t>(seed);
    for (int cell = 0; cell < count; ++cell) {
        const long long code = square[static_cast<std::size_t>(cell)];
        const long long shape = code / 100;
        const long long material = (code / 10) % 10;
        const long long clue = code % 10;
        if (code < 0 || shape >= n || material >= n || clue > 1)
            return false;
        s.puzzle.shape[static_cast<std::size_t>(cell)] = static_cast<int>(shape);
        s.puzzle.material[static_cast<std::size_t>(cell)] = static_cast<int>(material);
        s.puzzle.clue[static_cast<std::size_t>(cell)] = clue == 1;
    }
    for (int k = 0; k < n; ++k) {
        const long long family_seed = looks[static_cast<std::size_t>(k * 4)];
        const long long style = looks[static_cast<std::size_t>(k * 4 + 1)];
        const long long kind = looks[static_cast<std::size_t>(k * 4 + 2)];
        const long long material_seed = looks[static_cast<std::size_t>(k * 4 + 3)];
        if (family_seed < 0 || family_seed > 0xffffffffLL || style < 0 || style >= style_choices || kind < 0 ||
            kind >= material_choices || material_seed < 0 || material_seed > 0xffffffffLL)
            return false;
        s.puzzle.family_seed[static_cast<std::size_t>(k)] = static_cast<std::uint32_t>(family_seed);
        s.puzzle.family_style[static_cast<std::size_t>(k)] = static_cast<int>(style);
        s.puzzle.material_kind[static_cast<std::size_t>(k)] = static_cast<int>(kind);
        s.puzzle.material_seed[static_cast<std::size_t>(k)] = static_cast<std::uint32_t>(material_seed);
    }
    if (!graeco_latin(s.puzzle))
        return false;
    if (static_cast<int>(stones.size()) != count || static_cast<int>(pile.size()) > pile_size)
        return false;
    // Every stone once, every socket at most once, the clues where they belong, and the
    // tumbler and the pile holding exactly the stones that say they are there.
    std::array<bool, most_stones> seen{};
    std::array<int, most_stones> socket{};
    socket.fill(-1);
    for (const std::vector<long long>& e : stones) {
        const long long stone = e[0];
        const long long place = e[1];
        if (stone < 0 || stone >= count || seen[static_cast<std::size_t>(stone)] || place < 0 || place > 3 ||
            e[3] < 0 || e[3] > 10000 || e[4] < 0 || e[4] > 10000 || e[5] < 0 || e[5] > 7)
            return false;
        seen[static_cast<std::size_t>(stone)] = true;
        Where w;
        w.place = static_cast<Place>(place);
        w.x = static_cast<double>(e[3]) / 10000;
        w.y = static_cast<double>(e[4]) / 10000;
        if (w.place == Place::cabinet) {
            if (e[2] < 0 || e[2] >= count || socket[static_cast<std::size_t>(e[2])] >= 0)
                return false;
            w.cell = static_cast<int>(e[2]);
            socket[static_cast<std::size_t>(e[2])] = static_cast<int>(stone);
        }
        s.where[static_cast<std::size_t>(stone)] = w;
        s.turn[static_cast<std::size_t>(stone)] = static_cast<int>(e[5]);
    }
    for (int cell = 0; cell < count; ++cell)
        if (s.puzzle.clue[static_cast<std::size_t>(cell)] && socket[static_cast<std::size_t>(cell)] != solution_stone(s.puzzle, cell))
            return false;
    std::array<bool, most_stones> listed{};
    for (long long stone : drum) {
        if (stone < 0 || stone >= count || s.where[static_cast<std::size_t>(stone)].place != Place::drum ||
            listed[static_cast<std::size_t>(stone)])
            return false;
        listed[static_cast<std::size_t>(stone)] = true;
        s.drum.push_back(static_cast<int>(stone));
    }
    for (long long stone : pile) {
        if (stone < 0 || stone >= count || s.where[static_cast<std::size_t>(stone)].place != Place::pile ||
            listed[static_cast<std::size_t>(stone)])
            return false;
        listed[static_cast<std::size_t>(stone)] = true;
        s.pile.push_back(static_cast<int>(stone));
    }
    int in_drum = 0;
    int in_pile = 0;
    for (int stone = 0; stone < count; ++stone) {
        in_drum += s.where[static_cast<std::size_t>(stone)].place == Place::drum ? 1 : 0;
        in_pile += s.where[static_cast<std::size_t>(stone)].place == Place::pile ? 1 : 0;
    }
    if (in_drum != static_cast<int>(s.drum.size()) || in_pile != static_cast<int>(s.pile.size()))
        return false;
    destination = s;
    return true;
}

}  // namespace sc
