#include "rules.hpp"

#include "minerals.hpp"
#include "noise.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>

namespace ts {
namespace {

constexpr int style_choices = 9;  // shapes.hpp's styles

bool valid_cell(int cell) {
    return cell >= 0 && cell < cell_count;
}

bool neighbours(int a, int b) {
    const int ar = a / side;
    const int ac = a % side;
    const int br = b / side;
    const int bc = b % side;
    return std::abs(ar - br) + std::abs(ac - bc) == 1;
}

// The up to four orthogonal neighbours of a cell; returns how many.
int around(int cell, int out[4]) {
    const int row = cell / side;
    const int column = cell % side;
    int count = 0;
    if (row > 0)
        out[count++] = cell - side;
    if (row + 1 < side)
        out[count++] = cell + side;
    if (column > 0)
        out[count++] = cell - 1;
    if (column + 1 < side)
        out[count++] = cell + 1;
    return count;
}

int on_board(const Board& board, int slot) {
    int count = 0;
    for (const Stone& stone : board.cells)
        if (stone.family == slot)
            ++count;
    return count;
}

int active_count(const Board& board) {
    int count = 0;
    for (const Family& family : board.families)
        if (family.used && !family.waiting)
            ++count;
    return count;
}

int waiting_slot(const Board& board) {
    for (int s = 0; s < family_slots; ++s)
        if (board.families[static_cast<std::size_t>(s)].used && board.families[static_cast<std::size_t>(s)].waiting)
            return s;
    return -1;
}

// A new family into a free slot: waiting when five are in the tray already. Later
// families are more and more often near relations of one in the tray.
int introduce(Board& board) {
    int slot = -1;
    for (int s = 0; s < family_slots; ++s)
        if (!board.families[static_cast<std::size_t>(s)].used) {
            slot = s;
            break;
        }
    if (slot < 0)
        return -1;
    ++board.families_seen;
    Family family;
    family.used = true;
    family.number = board.families_seen;
    family.seed = static_cast<std::uint32_t>(next_random(board.random) >> 32) | 1U;
    family.supply = 20 + next_below(board.random, 61);
    family.waiting = active_count(board) >= most_families;
    // Which families are in the tray now, to choose a relation or a style apart from them.
    std::vector<int> present;
    bool style_present[style_choices] = {};
    for (int s = 0; s < family_slots; ++s) {
        const Family& other = board.families[static_cast<std::size_t>(s)];
        if (!other.used || s == slot)
            continue;
        present.push_back(s);
        if (other.style >= 0 && other.style < style_choices)
            style_present[other.style] = true;
    }
    const int later = board.families_seen - most_families;
    const int relation_chance = std::clamp(later * 7, 0, 55);
    if (!present.empty() && next_below(board.random, 100) < relation_chance) {
        const Family& parent = board.families[static_cast<std::size_t>(present[static_cast<std::size_t>(
            next_below(board.random, static_cast<int>(present.size())))])];
        family.style = parent.style;
        family.parent = parent.seed;
        family.likeness = std::min(75, 35 + next_below(board.random, 26) + std::min(15, later));
    } else {
        std::vector<int> fresh;
        for (int s = 0; s < style_choices; ++s)
            if (!style_present[s])
                fresh.push_back(s);
        family.style = fresh.empty() ? next_below(board.random, style_choices)
                                     : fresh[static_cast<std::size_t>(next_below(board.random, static_cast<int>(fresh.size())))];
    }
    board.families[static_cast<std::size_t>(slot)] = family;
    return slot;
}

// Lets go of families gone from the tray and out of stones, brings the waiting one in when
// there is room, and keeps a family waiting while one in the tray has run out.
void settle_families(Board& board) {
    for (int s = 0; s < family_slots; ++s) {
        Family& family = board.families[static_cast<std::size_t>(s)];
        if (family.used && !family.waiting && family.supply == 0 && on_board(board, s) == 0)
            family = Family{};
    }
    const int waiting = waiting_slot(board);
    if (waiting >= 0 && active_count(board) < most_families)
        board.families[static_cast<std::size_t>(waiting)].waiting = false;
    while (active_count(board) < most_families && waiting_slot(board) < 0) {
        if (introduce(board) < 0)
            break;
    }
    bool run_out = false;
    for (const Family& family : board.families)
        if (family.used && !family.waiting && family.supply == 0)
            run_out = true;
    if (run_out && waiting_slot(board) < 0)
        static_cast<void>(introduce(board));
}

// A new material in a slot: a kind drawn by the minerals' shares, avoiding the kinds already
// in play when it can, and a short run.
void bring_material(Board& board, int slot) {
    int total = 0;
    for (int k = 0; k < mineral_count; ++k)
        total += mineral_weight(k);
    int kind = 0;
    for (int attempt = 0; attempt < 6; ++attempt) {
        int pick = next_below(board.random, std::max(1, total));
        kind = 0;
        for (int k = 0; k < mineral_count; ++k) {
            pick -= mineral_weight(k);
            if (pick < 0) {
                kind = k;
                break;
            }
        }
        bool taken = false;
        for (int s = 0; s < material_slots; ++s)
            taken = taken || (s != slot && board.materials[static_cast<std::size_t>(s)].used &&
                              board.materials[static_cast<std::size_t>(s)].kind == kind);
        if (!taken)
            break;
    }
    Material material;
    material.used = true;
    material.kind = kind;
    material.seed = static_cast<std::uint32_t>(next_random(board.random) >> 32) | 1U;
    material.supply = 6 + next_below(board.random, 13);
    board.materials[static_cast<std::size_t>(slot)] = material;
}

// A material for a new stone: any in play, by lot; one that runs out is replaced.
void draw_material(Board& board, Stone& stone) {
    for (int s = 0; s < material_slots; ++s)
        if (!board.materials[static_cast<std::size_t>(s)].used)
            bring_material(board, s);
    const int slot = next_below(board.random, material_slots);
    Material& material = board.materials[static_cast<std::size_t>(slot)];
    stone.material = material.kind;
    stone.material_seed = material.seed;
    if (--material.supply <= 0)
        bring_material(board, slot);
}

// A new stone of a family in the tray that still has stones to come, the families weighed
// by how many they have left. False when none has.
bool draw_stone(Board& board, Stone& out) {
    int total = 0;
    for (const Family& family : board.families)
        if (family.used && !family.waiting)
            total += family.supply;
    if (total <= 0)
        return false;
    int pick = next_below(board.random, total);
    int slot = 0;
    for (int s = 0; s < family_slots; ++s) {
        const Family& family = board.families[static_cast<std::size_t>(s)];
        if (!family.used || family.waiting)
            continue;
        if (pick < family.supply) {
            slot = s;
            break;
        }
        pick -= family.supply;
    }
    Family& family = board.families[static_cast<std::size_t>(slot)];
    --family.supply;
    out = Stone{};
    out.family = slot;
    out.orientation = next_below(board.random, 8);
    out.mineral = static_cast<std::uint32_t>(next_random(board.random) >> 32);
    draw_material(board, out);
    out.serial = board.next_serial++;
    out.swapped = false;
    settle_families(board);
    return true;
}

// Stones fall down their columns into the gaps, keeping their orientations.
void fall(Board& board) {
    for (int column = 0; column < side; ++column) {
        int write = side - 1;
        for (int row = side - 1; row >= 0; --row) {
            const Stone stone = board.cells[static_cast<std::size_t>(row * side + column)];
            if (stone.family < 0)
                continue;
            if (write != row) {
                board.cells[static_cast<std::size_t>(write * side + column)] = stone;
                board.cells[static_cast<std::size_t>(row * side + column)] = Stone{};
            }
            --write;
        }
    }
}

void juggle(Board& board);

// New stones into every empty socket, from the top. A tray whose families have all run
// out while their last stones lie stranded is juggled first: those stones become new
// families' stones.
void fill(Board& board, Outcome& outcome) {
    fall(board);
    for (int cell = 0; cell < cell_count; ++cell) {
        Stone& place = board.cells[static_cast<std::size_t>(cell)];
        if (place.family >= 0)
            continue;
        Stone stone;
        if (!draw_stone(board, stone)) {
            outcome.juggled = true;
            outcome.before_juggle = board.cells;
            juggle(board);
            fill(board, outcome);
            return;
        }
        place = stone;
    }
}

// The tray's stones lifted and set down again at random, in new orientations, their swap
// marks cleared; the stones of families that have run out become the incoming family's.
// Afterwards a match can always be made.
void juggle(Board& board) {
    ++board.juggles;
    for (int pass = 0; pass < family_slots; ++pass) {
        int stranded = -1;
        for (int s = 0; s < family_slots; ++s) {
            const Family& family = board.families[static_cast<std::size_t>(s)];
            if (family.used && !family.waiting && family.supply == 0 && on_board(board, s) > 0) {
                stranded = s;
                break;
            }
        }
        if (stranded < 0)
            break;
        int incoming = waiting_slot(board);
        if (incoming < 0)
            incoming = introduce(board);
        if (incoming < 0)
            break;
        Family& coming = board.families[static_cast<std::size_t>(incoming)];
        for (Stone& stone : board.cells)
            if (stone.family == stranded) {
                stone.family = incoming;
                coming.supply = std::max(0, coming.supply - 1);
            }
        board.families[static_cast<std::size_t>(stranded)] = Family{};
        coming.waiting = false;
        settle_families(board);
    }
    std::vector<Stone> stones;
    for (const Stone& stone : board.cells)
        if (stone.family >= 0)
            stones.push_back(stone);
    for (int attempt = 0; attempt < 64; ++attempt) {
        // Shuffle the stones over the whole tray; gaps fall to the top.
        std::vector<Stone> laid(static_cast<std::size_t>(cell_count));
        for (std::size_t i = 0; i < stones.size(); ++i)
            laid[i] = stones[i];
        for (int i = cell_count - 1; i > 0; --i) {
            const int j = next_below(board.random, i + 1);
            std::swap(laid[static_cast<std::size_t>(i)], laid[static_cast<std::size_t>(j)]);
        }
        for (int cell = 0; cell < cell_count; ++cell) {
            Stone stone = laid[static_cast<std::size_t>(cell)];
            if (stone.family >= 0) {
                stone.orientation = next_below(board.random, 8);
                stone.swapped = false;
            }
            board.cells[static_cast<std::size_t>(cell)] = stone;
        }
        fall(board);
        if (match_possible(board))
            return;
    }
    // Never seen in play: set two stones of one family side by side.
    for (int a = 0; a < cell_count; ++a)
        for (int b = a + 1; b < cell_count; ++b) {
            const Stone& first = board.cells[static_cast<std::size_t>(a)];
            const Stone& second = board.cells[static_cast<std::size_t>(b)];
            if (first.family < 0 || first.family != second.family)
                continue;
            int near[4];
            const int count = around(a, near);
            for (int k = 0; k < count; ++k) {
                if (board.cells[static_cast<std::size_t>(near[k])].family < 0)
                    continue;
                std::swap(board.cells[static_cast<std::size_t>(near[k])], board.cells[static_cast<std::size_t>(b)]);
                return;
            }
        }
}

// Pulls the group at `cell` if it is a match: true when it was.
bool pull(Board& board, int cell, Outcome& outcome) {
    const std::vector<int> group = group_of(board, cell);
    if (group.size() < 2)
        return false;
    const int size = static_cast<int>(group.size());
    for (int at : group) {
        outcome.pulled.push_back(at);
        board.cells[static_cast<std::size_t>(at)] = Stone{};
    }
    outcome.pull_sizes.push_back(size);
    const int points = pull_points(size);
    outcome.points += points;
    board.points += points;
    ++board.pulls;
    board.largest = std::max(board.largest, size);
    if (size >= board.target) {
        board.won = true;
        outcome.won = true;
    }
    settle_families(board);
    return true;
}

// After pulls: the stones fall, the tray fills, and a tray with no match is juggled.
void after_pulls(Board& board, Outcome& outcome) {
    fill(board, outcome);
    if (board.won)
        return;
    if (!match_possible(board)) {
        if (!outcome.juggled)
            outcome.before_juggle = board.cells;
        outcome.juggled = true;
        juggle(board);
        fill(board, outcome);
    }
}

}  // namespace

int pull_points(int stones) {
    if (stones < 2)
        return 0;
    const int square = stones * stones;
    return (400 + square / 2) / square;
}

int flipped(int orientation) {
    const int face = orientation / 4;
    const int turns = orientation % 4;
    return (1 - face) * 4 + (4 - turns) % 4;
}

int turned(int orientation) {
    return (orientation / 4) * 4 + (orientation % 4 + 1) % 4;
}

Board new_game(std::uint64_t seed, int target) {
    Board board;
    board.seed = seed;
    board.random = seed ^ 0x6a09e667f3bcc909ULL;
    board.target = std::clamp(target, smallest_target, largest_target);
    settle_families(board);
    Outcome ignored;
    fill(board, ignored);
    if (!match_possible(board)) {
        juggle(board);
        fill(board, ignored);
    }
    board.juggles = 0;
    return board;
}

std::vector<int> group_of(const Board& board, int cell) {
    std::vector<int> group;
    if (!valid_cell(cell))
        return group;
    const Stone& seed = board.cells[static_cast<std::size_t>(cell)];
    if (seed.family < 0)
        return group;
    std::array<bool, cell_count> seen{};
    group.push_back(cell);
    seen[static_cast<std::size_t>(cell)] = true;
    for (std::size_t i = 0; i < group.size(); ++i) {
        int near[4];
        const int count = around(group[i], near);
        for (int k = 0; k < count; ++k) {
            const int next = near[k];
            const Stone& stone = board.cells[static_cast<std::size_t>(next)];
            if (seen[static_cast<std::size_t>(next)] || stone.family != seed.family || stone.orientation != seed.orientation)
                continue;
            seen[static_cast<std::size_t>(next)] = true;
            group.push_back(next);
        }
    }
    std::sort(group.begin(), group.end());
    return group;
}

Outcome set_down(Board& board, int cell, int orientation) {
    Outcome outcome;
    if (board.won || !valid_cell(cell) || orientation < 0 || orientation > 7)
        return outcome;
    Stone& stone = board.cells[static_cast<std::size_t>(cell)];
    if (stone.family < 0)
        return outcome;
    outcome.accepted = true;
    const bool changed = stone.orientation != orientation;
    stone.orientation = orientation;
    const bool pulled = pull(board, cell, outcome);
    outcome.counted = changed || pulled;
    if (outcome.counted)
        ++board.moves;
    if (pulled)
        after_pulls(board, outcome);
    return outcome;
}

bool can_swap(const Board& board, int from, int to) {
    if (board.won || !valid_cell(from) || !valid_cell(to) || !neighbours(from, to))
        return false;
    const Stone& a = board.cells[static_cast<std::size_t>(from)];
    const Stone& b = board.cells[static_cast<std::size_t>(to)];
    return a.family >= 0 && b.family >= 0 && !a.swapped && !b.swapped;
}

Outcome swap_stones(Board& board, int from, int to) {
    Outcome outcome;
    if (!can_swap(board, from, to))
        return outcome;
    outcome.accepted = true;
    outcome.counted = true;
    ++board.moves;
    std::swap(board.cells[static_cast<std::size_t>(from)], board.cells[static_cast<std::size_t>(to)]);
    board.cells[static_cast<std::size_t>(from)].swapped = true;
    board.cells[static_cast<std::size_t>(to)].swapped = true;
    // The stone the player moved is set down first; whatever it pulls goes first.
    const std::uint32_t other = board.cells[static_cast<std::size_t>(from)].serial;
    bool pulled = pull(board, to, outcome);
    if (board.cells[static_cast<std::size_t>(from)].serial == other && board.cells[static_cast<std::size_t>(from)].family >= 0)
        pulled = pull(board, from, outcome) || pulled;
    if (pulled)
        after_pulls(board, outcome);
    return outcome;
}

Outcome juggle_tray(Board& board) {
    Outcome outcome;
    if (board.won)
        return outcome;
    outcome.accepted = true;
    outcome.counted = true;
    outcome.juggled = true;
    outcome.before_juggle = board.cells;
    ++board.moves;
    juggle(board);
    fill(board, outcome);
    return outcome;
}

bool match_possible(const Board& board) {
    // Two neighbours of one family: either can be turned to the other's orientation.
    for (int cell = 0; cell < cell_count; ++cell) {
        const int family = board.cells[static_cast<std::size_t>(cell)].family;
        if (family < 0)
            continue;
        const int column = cell % side;
        if (column + 1 < side && board.cells[static_cast<std::size_t>(cell + 1)].family == family)
            return true;
        if (cell + side < cell_count && board.cells[static_cast<std::size_t>(cell + side)].family == family)
            return true;
    }
    // A swap that brings two of a family together.
    for (int from = 0; from < cell_count; ++from) {
        int near[4];
        const int count = around(from, near);
        for (int k = 0; k < count; ++k) {
            const int to = near[k];
            if (!can_swap(board, from, to))
                continue;
            const int moving = board.cells[static_cast<std::size_t>(from)].family;
            int beside[4];
            const int around_to = around(to, beside);
            for (int j = 0; j < around_to; ++j)
                if (beside[j] != from && board.cells[static_cast<std::size_t>(beside[j])].family == moving)
                    return true;
        }
    }
    return false;
}

int families_in_tray(const Board& board) {
    std::set<int> present;
    for (const Stone& stone : board.cells)
        if (stone.family >= 0)
            present.insert(stone.family);
    return static_cast<int>(present.size());
}

bool record_win(Session& session) {
    const Board& board = session.board;
    if (!board.won || board.target < least_recorded)
        return false;
    Best& best = session.best[static_cast<std::size_t>(board.target)];
    const int total = board.moves + board.points;
    const bool better = best.total == 0 || total < best.total || (total == best.total && board.moves < best.moves);
    if (better)
        best = Best{total, board.moves, board.points};
    return better;
}

// ---------------------------------------------------------------- the save

namespace {

bool read_number(const std::string& text, long long low, long long high, long long& out) {
    if (text.empty() || text.size() > 19)
        return false;
    std::size_t at = 0;
    bool negative = false;
    if (text[0] == '-') {
        negative = true;
        at = 1;
    }
    if (at >= text.size())
        return false;
    unsigned long long value = 0;
    for (; at < text.size(); ++at) {
        const char c = text[at];
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + static_cast<unsigned long long>(c - '0');
    }
    const long long signed_value = negative ? -static_cast<long long>(value) : static_cast<long long>(value);
    if (signed_value < low || signed_value > high)
        return false;
    out = signed_value;
    return true;
}

bool read_unsigned(const std::string& text, std::uint64_t& out) {
    if (text.empty() || text.size() > 20)
        return false;
    std::uint64_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9')
            return false;
        const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
        if (value > (UINT64_MAX - digit) / 10)
            return false;
        value = value * 10 + digit;
    }
    out = value;
    return true;
}

std::vector<std::string> split(const std::string& text, char by) {
    std::vector<std::string> parts;
    std::string part;
    for (char c : text) {
        if (c == by) {
            parts.push_back(part);
            part.clear();
        } else {
            part.push_back(c);
        }
    }
    parts.push_back(part);
    return parts;
}

}  // namespace

std::string encode_session(const Session& session) {
    std::ostringstream out;
    const Board& b = session.board;
    out << "version=1\n";
    out << "playing=" << (session.playing ? 1 : 0) << "\n";
    out << "next_target=" << session.next_target << "\n";
    for (int t = least_recorded; t <= largest_target; ++t) {
        const Best& best = session.best[static_cast<std::size_t>(t)];
        if (best.total > 0)
            out << "best=" << t << "," << best.total << "," << best.moves << "," << best.points << "\n";
    }
    out << "seed=" << b.seed << "\n";
    out << "random=" << b.random << "\n";
    out << "target=" << b.target << "\n";
    out << "next_serial=" << b.next_serial << "\n";
    out << "families_seen=" << b.families_seen << "\n";
    out << "counts=" << b.moves << "," << b.points << "," << b.pulls << "," << b.juggles << "," << b.largest << ","
        << (b.won ? 1 : 0) << "\n";
    for (int s = 0; s < family_slots; ++s) {
        const Family& f = b.families[static_cast<std::size_t>(s)];
        if (!f.used)
            continue;
        out << "family=" << s << "," << f.seed << "," << f.style << "," << f.parent << "," << f.likeness << ","
            << f.supply << "," << (f.waiting ? 1 : 0) << "," << f.number << "\n";
    }
    for (int s = 0; s < material_slots; ++s) {
        const Material& m = b.materials[static_cast<std::size_t>(s)];
        if (m.used)
            out << "material=" << s << "," << m.kind << "," << m.seed << "," << m.supply << "\n";
    }
    for (int c = 0; c < cell_count; ++c) {
        const Stone& stone = b.cells[static_cast<std::size_t>(c)];
        if (stone.family < 0)
            continue;
        out << "stone=" << c << "," << stone.family << "," << stone.orientation << "," << stone.mineral << ","
            << stone.serial << "," << (stone.swapped ? 1 : 0) << "," << stone.material << "," << stone.material_seed << "\n";
    }
    return out.str();
}

bool decode_session(const std::string& text, Session& destination) {
    if (text.size() > 65536)
        return false;
    Session s;
    Board& b = s.board;
    bool version = false;
    std::set<std::string> singles;
    std::set<int> family_seen_slots;
    std::set<int> stone_cells;
    std::set<std::uint32_t> serials;
    for (const std::string& line : split(text, '\n')) {
        if (line.empty())
            continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos)
            return false;
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        long long n = 0;
        std::uint64_t u = 0;
        if (key != "best" && key != "family" && key != "stone" && key != "material") {
            if (!singles.insert(key).second)
                return false;
        }
        if (key == "version") {
            if (value != "1")
                return false;
            version = true;
        } else if (key == "playing") {
            if (!read_number(value, 0, 1, n))
                return false;
            s.playing = n == 1;
        } else if (key == "next_target") {
            if (!read_number(value, smallest_target, largest_target, n))
                return false;
            s.next_target = static_cast<int>(n);
        } else if (key == "best") {
            const std::vector<std::string> p = split(value, ',');
            long long t = 0, total = 0, moves = 0, points = 0;
            if (p.size() != 4 || !read_number(p[0], least_recorded, largest_target, t) ||
                !read_number(p[1], 1, 100000000, total) || !read_number(p[2], 0, 100000000, moves) ||
                !read_number(p[3], 0, 100000000, points) || moves + points != total)
                return false;
            s.best[static_cast<std::size_t>(t)] = Best{static_cast<int>(total), static_cast<int>(moves), static_cast<int>(points)};
        } else if (key == "seed") {
            if (!read_unsigned(value, u))
                return false;
            b.seed = u;
        } else if (key == "random") {
            if (!read_unsigned(value, u))
                return false;
            b.random = u;
        } else if (key == "target") {
            if (!read_number(value, smallest_target, largest_target, n))
                return false;
            b.target = static_cast<int>(n);
        } else if (key == "next_serial") {
            if (!read_number(value, 1, 4000000000LL, n))
                return false;
            b.next_serial = static_cast<std::uint32_t>(n);
        } else if (key == "families_seen") {
            if (!read_number(value, 0, 1000000, n))
                return false;
            b.families_seen = static_cast<int>(n);
        } else if (key == "counts") {
            const std::vector<std::string> p = split(value, ',');
            long long v[6] = {};
            if (p.size() != 6)
                return false;
            for (int i = 0; i < 6; ++i)
                if (!read_number(p[static_cast<std::size_t>(i)], 0, i == 5 ? 1 : 100000000, v[i]))
                    return false;
            b.moves = static_cast<int>(v[0]);
            b.points = static_cast<int>(v[1]);
            b.pulls = static_cast<int>(v[2]);
            b.juggles = static_cast<int>(v[3]);
            b.largest = static_cast<int>(v[4]);
            b.won = v[5] == 1;
        } else if (key == "family") {
            const std::vector<std::string> p = split(value, ',');
            long long v[8] = {};
            const long long low[8] = {0, 1, 0, 0, 0, 0, 0, 1};
            const long long high[8] = {family_slots - 1, 4294967295LL, style_choices - 1, 4294967295LL, 100, 1000, 1, 1000000};
            if (p.size() != 8)
                return false;
            for (int i = 0; i < 8; ++i)
                if (!read_number(p[static_cast<std::size_t>(i)], low[i], high[i], v[i]))
                    return false;
            if (!family_seen_slots.insert(static_cast<int>(v[0])).second)
                return false;
            Family& f = b.families[static_cast<std::size_t>(v[0])];
            f.used = true;
            f.seed = static_cast<std::uint32_t>(v[1]);
            f.style = static_cast<int>(v[2]);
            f.parent = static_cast<std::uint32_t>(v[3]);
            f.likeness = static_cast<int>(v[4]);
            f.supply = static_cast<int>(v[5]);
            f.waiting = v[6] == 1;
            f.number = static_cast<int>(v[7]);
        } else if (key == "material") {
            const std::vector<std::string> p = split(value, ',');
            long long v[4] = {};
            const long long low[4] = {0, 0, 1, 1};
            const long long high[4] = {material_slots - 1, mineral_count - 1, 4294967295LL, 1000};
            if (p.size() != 4)
                return false;
            for (int i = 0; i < 4; ++i)
                if (!read_number(p[static_cast<std::size_t>(i)], low[i], high[i], v[i]))
                    return false;
            Material& m = b.materials[static_cast<std::size_t>(v[0])];
            if (m.used)
                return false;
            m.used = true;
            m.kind = static_cast<int>(v[1]);
            m.seed = static_cast<std::uint32_t>(v[2]);
            m.supply = static_cast<int>(v[3]);
        } else if (key == "stone") {
            const std::vector<std::string> p = split(value, ',');
            long long v[8] = {};
            const long long low[8] = {0, 0, 0, 0, 1, 0, 0, 0};
            const long long high[8] = {cell_count - 1, family_slots - 1, 7, 4294967295LL, 4000000000LL, 1, mineral_count - 1, 4294967295LL};
            if (p.size() != 8)
                return false;
            for (int i = 0; i < 8; ++i)
                if (!read_number(p[static_cast<std::size_t>(i)], low[i], high[i], v[i]))
                    return false;
            if (!stone_cells.insert(static_cast<int>(v[0])).second || !serials.insert(static_cast<std::uint32_t>(v[4])).second)
                return false;
            Stone& stone = b.cells[static_cast<std::size_t>(v[0])];
            stone.family = static_cast<int>(v[1]);
            stone.orientation = static_cast<int>(v[2]);
            stone.mineral = static_cast<std::uint32_t>(v[3]);
            stone.serial = static_cast<std::uint32_t>(v[4]);
            stone.swapped = v[5] == 1;
            stone.material = static_cast<int>(v[6]);
            stone.material_seed = static_cast<std::uint32_t>(v[7]);
        } else {
            return false;
        }
    }
    if (!version || singles.count("seed") == 0 || singles.count("counts") == 0)
        return false;
    // The position must be one the rules could make: every stone of a family in the tray,
    // at most five families in it, one waiting at most, the serials all issued, a full
    // tray, and a game not won before its target.
    int waiting = 0;
    for (const Family& f : b.families)
        if (f.used && f.waiting)
            ++waiting;
    if (waiting > 1 || active_count(b) > most_families)
        return false;
    for (const Stone& stone : b.cells) {
        if (stone.family < 0)
            return false;
        const Family& f = b.families[static_cast<std::size_t>(stone.family)];
        if (!f.used || f.waiting || stone.serial >= b.next_serial)
            return false;
    }
    if (b.won != (b.largest >= b.target))
        return false;
    destination = s;
    return true;
}

}  // namespace ts
