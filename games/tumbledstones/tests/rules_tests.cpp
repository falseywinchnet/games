// Headless checks of Tumbled Stones' rules, its save and its shapes. No window, no
// toolkit. Deterministic; the shapes' measures are printed so a change to the generator
// can be seen.
#include "rules.hpp"
#include "save.hpp"
#include "shapes.hpp"
#include "stone_render.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <set>
#include <utility>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAILED: " << what << "\n";
        ++failures;
    }
}

// Every stone of a family in the tray, at most five there, one waiting at most, every
// serial once, the tray full.
bool sound(const ts::Board& board) {
    std::set<std::uint32_t> serials;
    int active = 0;
    int waiting = 0;
    for (const ts::Family& family : board.families) {
        if (family.used && family.waiting)
            ++waiting;
        if (family.used && !family.waiting)
            ++active;
    }
    if (active > ts::most_families || waiting > 1)
        return false;
    for (const ts::Stone& stone : board.cells) {
        if (stone.family < 0 || stone.family >= ts::family_slots)
            return false;
        const ts::Family& family = board.families[static_cast<std::size_t>(stone.family)];
        if (!family.used || family.waiting)
            return false;
        if (!serials.insert(stone.serial).second || stone.serial >= board.next_serial)
            return false;
    }
    return ts::families_in_tray(board) <= ts::most_families;
}

// The first family slot in use that is not waiting, counting `skip` of them.
int family_slot(const ts::Board& board, int skip) {
    for (int s = 0; s < ts::family_slots; ++s) {
        const ts::Family& family = board.families[static_cast<std::size_t>(s)];
        if (family.used && !family.waiting) {
            if (skip == 0)
                return s;
            --skip;
        }
    }
    return 0;
}

// A position to test against: stones of family `which` where `cells` says, in one
// orientation, the rest a checkerboard of two other families so nothing else matches.
void lay_pattern(ts::Board& board, const std::vector<int>& cells, int which, int orientation) {
    const int family = family_slot(board, which);
    const int even = family_slot(board, (which + 1) % 3);
    const int odd = family_slot(board, (which + 2) % 3);
    for (int cell = 0; cell < ts::cell_count; ++cell) {
        ts::Stone& stone = board.cells[static_cast<std::size_t>(cell)];
        const int row = cell / ts::side;
        const int column = cell % ts::side;
        stone.family = (row + column) % 2 == 0 ? even : odd;
        stone.orientation = (cell * 3) % 8;
        stone.swapped = false;
    }
    for (int cell : cells) {
        board.cells[static_cast<std::size_t>(cell)].family = family;
        board.cells[static_cast<std::size_t>(cell)].orientation = orientation;
    }
}

void test_generation() {
    for (std::uint64_t seed = 1; seed <= 60; ++seed) {
        const ts::Board first = ts::new_game(seed, 7);
        const ts::Board second = ts::new_game(seed, 7);
        ts::Session a;
        a.board = first;
        ts::Session b;
        b.board = second;
        check(ts::encode_session(a) == ts::encode_session(b), "the same seed gives the same tray");
        check(sound(first), "a new tray is sound");
        check(ts::match_possible(first), "a new tray always has a match to make");
        check(ts::families_in_tray(first) >= 3, "a new tray holds several families");
        check(first.moves == 0 && first.points == 0 && !first.won, "a new game starts at nothing");
    }
    // Pinned: a change here means saved games would not reload as they were left.
    ts::Session pinned;
    pinned.board = ts::new_game(20261015, 9);
    const std::uint64_t digest = ts::checksum(ts::encode_session(pinned));
    std::cout << "seed 20261015 digest " << digest << "\n";
    check(digest == 12016535622324131002ULL, "the generator for a known seed has not drifted");
}

void test_orientations() {
    const double pi = 3.14159265358979323846;
    for (int o = 0; o < 8; ++o) {
        check(ts::flipped(ts::flipped(o)) == o, "two flips undo each other");
        check(ts::turned(ts::turned(ts::turned(ts::turned(o)))) == o, "four turns come round");
        check(ts::flipped(o) / 4 != o / 4, "a flip shows the other face");
        // The rules' orientation and the picture's pose agree: a turn is a quarter turn
        // clockwise about the eye's axis, a flip a half turn about the tray's upright.
        const ts::Pose turned_pose = ts::turned(ts::orientation_pose(o), 2, -pi / 2);
        const ts::Pose flipped_pose = ts::turned(ts::orientation_pose(o), 1, pi);
        const ts::Pose want_turned = ts::orientation_pose(ts::turned(o));
        const ts::Pose want_flipped = ts::orientation_pose(ts::flipped(o));
        double turn_error = 0;
        double flip_error = 0;
        for (int k = 0; k < 9; ++k) {
            turn_error = std::max(turn_error, std::abs(turned_pose.m[k] - want_turned.m[k]));
            flip_error = std::max(flip_error, std::abs(flipped_pose.m[k] - want_flipped.m[k]));
        }
        check(turn_error < 1e-9, "a turn in the rules is a quarter turn of the picture");
        check(flip_error < 1e-9, "a flip in the rules is a half turn about the upright");
    }
    std::set<int> reached{0};
    for (int round = 0; round < 8; ++round) {
        std::set<int> next = reached;
        for (int o : reached) {
            next.insert(ts::flipped(o));
            next.insert(ts::turned(o));
        }
        reached = next;
    }
    check(reached.size() == 8, "every orientation can be reached");
}

void test_points() {
    check(ts::pull_points(2) == 100, "a pair costs 100");
    check(ts::pull_points(3) == 44 && ts::pull_points(4) == 25 && ts::pull_points(5) == 16, "larger pulls cost less");
    check(ts::pull_points(20) == 1, "twenty costs one");
    for (int n = 3; n <= 20; ++n)
        check(ts::pull_points(n) <= ts::pull_points(n - 1), "a larger pull never costs more");
}

void test_set_down() {
    ts::Board board = ts::new_game(5, 7);
    check(!ts::set_down(board, -1, 0).accepted && !ts::set_down(board, 100, 0).accepted, "outside the tray is refused");
    check(!ts::set_down(board, 3, 8).accepted && !ts::set_down(board, 3, -1).accepted, "no ninth orientation");
    // A plus of five, set down as it lies: pulled together.
    lay_pattern(board, {34, 43, 44, 45, 54}, 0, 5);
    const std::uint32_t above = board.cells[24].serial;
    const ts::Outcome plus = ts::set_down(board, 44, 5);
    check(plus.accepted && plus.counted, "setting a stone down into a group is a move");
    check(plus.pull_sizes.size() == 1 && plus.pull_sizes[0] == 5, "the cross is one group of five");
    check(plus.points == ts::pull_points(5) && board.points == ts::pull_points(5), "the pull costs its points");
    check(board.moves == 1 && board.pulls == 1 && board.largest == 5, "the counts follow");
    check(sound(board), "the tray is full and sound after a pull");
    // The column held 34, 44 and 54: the stone above them falls three rows.
    check(plus.juggled || board.cells[54].serial == above, "stones above fall into the gaps");
    // Setting a stone down unchanged with no group costs nothing.
    lay_pattern(board, {}, 0, 0);
    const int moves = board.moves;
    const int lone = board.cells[11].orientation;
    const ts::Outcome nothing = ts::set_down(board, 11, lone);
    check(nothing.accepted && !nothing.counted && board.moves == moves, "picking a stone up and putting it back is free");
    const ts::Outcome turn = ts::set_down(board, 11, ts::turned(lone));
    check(turn.counted && turn.pulled.empty() && board.moves == moves + 1, "a turn alone is a move");
    // Same family, another orientation: no match, until turned to agree.
    lay_pattern(board, {12, 13}, 1, 2);
    board.cells[13].orientation = 6;
    check(ts::set_down(board, 12, 2).pulled.empty(), "the same shape the other way up is no match");
    check(ts::set_down(board, 12, 6).pull_sizes.size() == 1, "turned to agree, it is");
}

void test_win() {
    ts::Board board = ts::new_game(9, 4);
    lay_pattern(board, {0, 1, 2}, 0, 1);
    check(!ts::set_down(board, 0, 1).won, "three is short of four");
    lay_pattern(board, {0, 1, 2, 10}, 0, 1);
    const ts::Outcome win = ts::set_down(board, 10, 1);
    check(win.won && board.won, "a pull reaching the target wins");
    check(!ts::set_down(board, 50, ts::turned(board.cells[50].orientation)).accepted, "a won game takes no more moves");
    check(!ts::juggle_tray(board).accepted, "nor juggles");
    ts::Session session;
    session.board = board;
    check(!ts::record_win(session), "no best for pulls under seven");
    ts::Board big = ts::new_game(11, 7);
    lay_pattern(big, {60, 61, 62, 63, 64, 65, 66}, 2, 3);
    check(ts::set_down(big, 63, 3).won, "seven in a row reaches seven");
    session.board = big;
    check(ts::record_win(session), "the first win for seven is its best");
    check(session.best[7].total == big.moves + big.points, "the best is moves plus points");
    check(!ts::record_win(session), "the same result again is no better");
}

void test_swaps() {
    ts::Board board = ts::new_game(13, 7);
    lay_pattern(board, {}, 0, 0);
    check(!ts::can_swap(board, 0, 2) && !ts::can_swap(board, 0, 11) && !ts::can_swap(board, 9, 10),
          "only orthogonal neighbours swap");
    const std::uint32_t a = board.cells[20].serial;
    const std::uint32_t b = board.cells[21].serial;
    const ts::Outcome swap = ts::swap_stones(board, 20, 21);
    check(swap.accepted && swap.counted && board.moves == 1, "a swap is a move");
    check(board.cells[21].serial == a && board.cells[20].serial == b, "the stones change places");
    check(board.cells[20].swapped && board.cells[21].swapped, "both are marked");
    check(!ts::can_swap(board, 21, 22) && !ts::can_swap(board, 20, 30) && !ts::can_swap(board, 20, 21),
          "a marked stone cannot swap again");
    // A swap that brings a stone into its row pulls the row.
    lay_pattern(board, {41, 42, 50}, 3, 4);
    const ts::Outcome join = ts::swap_stones(board, 50, 40);
    check(join.pull_sizes.size() == 1 && join.pull_sizes[0] == 3, "the swapped stone joins its row");
    for (ts::Stone& stone : board.cells)
        stone.swapped = stone.serial % 3 == 0;
    const ts::Outcome juggle = ts::juggle_tray(board);
    check(juggle.accepted && juggle.counted && juggle.juggled, "a juggle on request is a move");
    bool marked = false;
    for (const ts::Stone& stone : board.cells)
        marked = marked || stone.swapped;
    check(!marked, "a juggle clears every swap mark");
    check(ts::match_possible(board) && sound(board), "after a juggle a match can be made");
}

// A patient player: sets one stone of a neighbouring pair to the other's orientation,
// preferring the larger group, and juggles when nothing is left.
void play_out(std::uint64_t seed, int moves, int& families_seen, int& juggles, bool& stayed_sound) {
    ts::Board board = ts::new_game(seed, 20);
    std::uint64_t random = seed * 7919 + 1;
    for (int m = 0; m < moves && !board.won; ++m) {
        int best_cell = -1;
        int best_orientation = 0;
        int best_score = 0;
        for (int cell = 0; cell < ts::cell_count; ++cell) {
            const int right = cell % ts::side + 1 < ts::side ? cell + 1 : -1;
            const int below = cell + ts::side < ts::cell_count ? cell + ts::side : -1;
            const int near[2] = {right, below};
            for (int other : near) {
                if (other < 0 || board.cells[static_cast<std::size_t>(other)].family !=
                                     board.cells[static_cast<std::size_t>(cell)].family)
                    continue;
                ts::Board trial = board;
                trial.cells[static_cast<std::size_t>(cell)].orientation =
                    trial.cells[static_cast<std::size_t>(other)].orientation;
                const int size = static_cast<int>(ts::group_of(trial, cell).size());
                random = random * 6364136223846793005ULL + 1442695040888963407ULL;
                const int score = size * 8 + static_cast<int>(random >> 61);
                if (score > best_score) {
                    best_score = score;
                    best_cell = cell;
                    best_orientation = trial.cells[static_cast<std::size_t>(cell)].orientation;
                }
            }
        }
        if (best_cell < 0)
            static_cast<void>(ts::juggle_tray(board));
        else
            static_cast<void>(ts::set_down(board, best_cell, best_orientation));
        if (!sound(board) || (!board.won && !ts::match_possible(board)))
            stayed_sound = false;
    }
    families_seen = board.families_seen;
    juggles = board.juggles;
}

void test_long_play() {
    int most_seen = 0;
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
        int families = 0;
        int juggles = 0;
        bool stayed = true;
        play_out(seed * 1009, 400, families, juggles, stayed);
        check(stayed, "the tray stays sound and playable through a long game");
        most_seen = std::max(most_seen, families);
        std::cout << "seed " << seed * 1009 << ": " << families << " families seen, " << juggles << " juggles\n";
    }
    check(most_seen > ts::most_families + 2, "families run out and new ones come in");
}

void test_materials() {
    ts::Board board = ts::new_game(4242, 20);
    std::set<std::pair<int, std::uint32_t>> first;
    for (const ts::Stone& stone : board.cells)
        first.insert({stone.material, stone.material_seed});
    check(first.size() <= static_cast<std::size_t>(ts::material_slots) + 8, "about a dozen materials are in play");
    int used = 0;
    for (const ts::Material& m : board.materials)
        used += m.used ? 1 : 0;
    check(used == ts::material_slots, "the pool holds a dozen materials");
    // Play on: materials run out and new ones come.
    int families = 0;
    int juggles = 0;
    bool stayed = true;
    play_out(4242, 120, families, juggles, stayed);
    std::set<std::uint32_t> seeds_before;
    for (const ts::Material& m : board.materials)
        seeds_before.insert(m.seed);
    for (int k = 0; k < 40; ++k)
        static_cast<void>(ts::juggle_tray(board));
    for (int k = 0; k < 30 && !board.won; ++k) {
        for (int cell = 0; cell + 1 < ts::cell_count; ++cell)
            if (board.cells[static_cast<std::size_t>(cell)].family == board.cells[static_cast<std::size_t>(cell + 1)].family &&
                cell % ts::side + 1 < ts::side) {
                static_cast<void>(ts::set_down(board, cell, board.cells[static_cast<std::size_t>(cell + 1)].orientation));
                break;
            }
    }
    int changed = 0;
    for (const ts::Material& m : board.materials)
        changed += seeds_before.count(m.seed) == 0 ? 1 : 0;
    check(changed > 0, "materials run out and are replaced, faster than families");
}

void test_save() {
    ts::Session session;
    session.board = ts::new_game(77, 9);
    session.playing = true;
    session.next_target = 9;
    session.best[12] = ts::Best{300, 120, 180};
    static_cast<void>(ts::swap_stones(session.board, 0, 1));
    const std::string body = ts::encode_session(session);
    ts::Session back;
    check(ts::decode_session(body, back), "a save reads back");
    check(ts::encode_session(back) == body, "and is the same game");
    const std::string sealed = ts::seal(body);
    std::string unsealed;
    check(ts::unseal(sealed, unsealed) && unsealed == body, "the envelope round-trips");
    std::string damaged = sealed;
    damaged[damaged.size() / 2] = static_cast<char>(damaged[damaged.size() / 2] ^ 1);
    check(!ts::unseal(damaged, unsealed), "one flipped bit is refused");
    check(!ts::unseal("SOMEGAME1\nx=1\ncheck=1\n", unsealed), "a foreign file is refused");
    check(!ts::unseal(std::string(70000, 'x'), unsealed), "an oversized file is refused");
    ts::Session untouched;
    untouched.next_target = 13;
    // Take away the family of the first stone: its stones are then of no family.
    const int family = session.board.cells[0].family;
    std::string orphaned = body;
    const std::string line = "family=" + std::to_string(family) + ",";
    const std::size_t at = orphaned.find(line);
    orphaned.erase(at, orphaned.find('\n', at) - at + 1);
    check(!ts::decode_session(orphaned, untouched), "a stone of no family is refused");
    check(!ts::decode_session(body + "stone=0,0,0,1,1,0\n", untouched), "two stones in one socket are refused");
    check(!ts::decode_session("version=2\n", untouched), "another version is refused");
    check(untouched.next_target == 13, "a refused save leaves the session untouched");
}

void test_shapes() {
    double least_self = 1e9;
    double least_cross = 1e9;
    for (int s = 0; s < ts::style_count; ++s) {
        double style_least = 1e9;
        for (std::uint32_t seed = 1; seed <= 12; ++seed) {
            const ts::Shape shape = ts::Shape::make(seed * 7777U + static_cast<std::uint32_t>(s), static_cast<ts::Style>(s));
            style_least = std::min(style_least, ts::self_distance(shape, 24));
            check(!shape.vertices().empty() && shape.triangles().size() % 3 == 0, "a shape has a surface");
            if (seed <= 4) {
                // A near relation at the most likeness the rules use.
                const ts::Shape relative = ts::Shape::make(seed * 31U + 5U, static_cast<ts::Style>(s), .75, &shape);
                least_cross = std::min(least_cross, ts::cross_distance(shape, relative, 24));
            }
        }
        std::printf("%-16s least distance between its turns %.3f\n", ts::style_name(static_cast<ts::Style>(s)), style_least);
        least_self = std::min(least_self, style_least);
    }
    std::printf("least between a family and its nearest relation %.3f\n", least_cross);
    check(least_self >= ts::least_self_distance, "every family's eight orientations can be told apart");
    check(least_cross > .02, "a near relation is never the same shape");
}

}  // namespace

int main() {
    test_generation();
    test_orientations();
    test_points();
    test_set_down();
    test_win();
    test_swaps();
    test_long_play();
    test_materials();
    test_save();
    test_shapes();
    if (failures > 0) {
        std::cerr << failures << " checks failed\n";
        return 1;
    }
    std::cout << "all rules checks passed\n";
    return 0;
}
