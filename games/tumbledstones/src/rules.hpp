#pragma once
// The rules of Tumbled Stones: pure, deterministic and headless. Nothing here knows about
// pixels, time, sound or files; a game follows from its seed and the player's moves.
//
// A tray of 10 x 10 sockets holds polished stones. Every stone belongs to a family, one
// solid shape, and lies in one of eight orientations: face up or down, turned 0 to 3
// quarter turns clockwise. Its mineral is chosen apart from its family and never
// matters. A stone is picked up, flipped and turned, and set down; then, and only then,
// the stones joined to it orthogonally that share its family and its orientation exactly
// are its group, and a group of two or more is pulled from the tray. Stones above fall
// into the gaps keeping their orientations, and new stones drop in from the top; what
// they form is not pulled until the player sets a stone down among it.
//
// A stone may also be swapped with a neighbour, once: both are marked until the next
// juggle. When no match can be made, or when the player asks, the tray is juggled: the
// stones are lifted, tumbled and set down in new places and orientations, the swap marks
// cleared, and stones of a family that has run out are turned into the family coming in.
//
// At most five families lie in the tray at once. Each has a run of stones, drawn at
// random; as one runs out another is brought in, by and by a near relation of one already
// there, so the shapes grow harder to tell apart as a game goes on.
//
// The player chose, at the start, the size of one single pull they mean to make (2 to
// 20 stones). The game ends when a pull reaches it. Every move counts one, every pull
// costs points (fewer the larger the pull: 100 for two, 400 / n^2), and the best game for
// a size of 7 or more is the one that got there with the lowest moves plus points.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ts {

inline constexpr int side = 10;
inline constexpr int cell_count = side * side;
inline constexpr int most_families = 5;
inline constexpr int family_slots = most_families + 1;  // the five, and one waiting to come in
inline constexpr int smallest_target = 2;
inline constexpr int largest_target = 20;
inline constexpr int least_recorded = 7;  // smaller pulls keep no best

struct Stone {
    int family = -1;             // a slot in Board::families; -1 an empty socket
    int orientation = 0;         // face * 4 + quarter turns clockwise; face 1 is down
    std::uint32_t mineral = 0;   // the seed its mineral is drawn from
    std::uint32_t serial = 0;    // this stone, for as long as it is in the tray
    bool swapped = false;        // swapped since the last juggle
};

struct Family {
    bool used = false;           // the slot holds a family
    std::uint32_t seed = 0;      // its shape's seed
    int style = 0;               // its shape's style (shapes.hpp)
    std::uint32_t parent = 0;    // the seed it is a near relation of, 0 none
    int likeness = 0;            // how near, percent
    int supply = 0;              // stones still to come
    bool waiting = false;        // brought in, but the tray holds five already
    int number = 0;              // the order families came in, from 1
};

struct Board {
    std::uint64_t seed = 1;
    std::uint64_t random = 1;
    int target = 7;
    std::array<Stone, cell_count> cells{};  // row-major, row 0 at the top
    std::array<Family, family_slots> families{};
    std::uint32_t next_serial = 1;
    int families_seen = 0;
    int moves = 0;
    int points = 0;
    int pulls = 0;
    int juggles = 0;
    int largest = 0;             // the largest pull so far
    bool won = false;
};

// What a move did, for the view to show it.
struct Outcome {
    bool accepted = false;       // false: the move was refused and nothing changed
    bool counted = false;        // it cost a move
    std::vector<int> pulled;     // cells of each pull, one after another
    std::vector<int> pull_sizes; // how many cells each pull took
    int points = 0;
    bool won = false;
    bool juggled = false;
    std::array<Stone, cell_count> before_juggle{};  // the tray after falling and filling
};

[[nodiscard]] int pull_points(int stones);

// A new game for a pull of `target` stones.
[[nodiscard]] Board new_game(std::uint64_t seed, int target);

// Orientation arithmetic: a flip turns a stone over about the tray's upright, so the
// quarter turns it showed run the other way; a turn adds one clockwise.
[[nodiscard]] int flipped(int orientation);
[[nodiscard]] int turned(int orientation);

// The group a stone at `cell` belongs to: itself and the stones joined to it orthogonally
// of its family and orientation. Empty for an empty socket.
[[nodiscard]] std::vector<int> group_of(const Board& board, int cell);

// Sets the stone at `cell` down in `orientation` (after flipping and turning it, or as it
// was: setting a stone down as it lay pulls a group already formed). It costs a move when
// the orientation changed or a group was pulled.
Outcome set_down(Board& board, int cell, int orientation);
// Swaps the stones at two neighbouring cells, neither swapped since the last juggle.
// The stone moved by the player (`from`) is set down first.
Outcome swap_stones(Board& board, int from, int to);
// Juggles on the player's request: one move.
Outcome juggle_tray(Board& board);

[[nodiscard]] bool can_swap(const Board& board, int from, int to);
// Whether any match can be made by setting a stone down or by a swap.
[[nodiscard]] bool match_possible(const Board& board);
[[nodiscard]] int families_in_tray(const Board& board);

// Best results for each target from least_recorded up: 0 when never reached.
struct Best {
    int total = 0;
    int moves = 0;
    int points = 0;
};

struct Session {
    Board board;
    bool playing = false;        // a game is under way; else the next target is being chosen
    int next_target = 7;
    std::array<Best, largest_target + 1> best{};
};

// The save body: "key=value" lines. decode validates everything and leaves `destination`
// untouched when the text is not a session this version made.
[[nodiscard]] std::string encode_session(const Session& session);
[[nodiscard]] bool decode_session(const std::string& text, Session& destination);

// Records a won game; true when it is a new best for its target.
bool record_win(Session& session);

}  // namespace ts
