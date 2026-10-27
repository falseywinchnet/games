#pragma once
// The rules of Specimen Cabinet: pure, deterministic and headless. A game follows from its
// seed and the player's moves; nothing here knows about pixels, time, sound or files.
//
// A cabinet of n x n sockets (n is 4, 5 or 7) and n * n stones: every one of n shapes in
// every one of n materials, once each. A stone is numbered shape * n + material. The
// cabinet is solved when every row and every column holds each shape once and each
// material once: a Graeco-Latin square. Some stones start pinned in the cabinet as clues;
// the rest wait in the tumbler, which drops them one at a time onto a small pile at the
// head of the felt. Any stone not pinned may be set in the cabinet, on the felt, or back.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sc {

inline constexpr int largest = 7;
inline constexpr int most_stones = largest * largest;
inline constexpr std::array<int, 3> sizes{4, 5, 7};
inline constexpr int pile_size = 4;   // the tumbler drops a stone while the pile holds fewer
inline constexpr int style_choices = 6;     // the stones engine's styles
inline constexpr int material_choices = 24;  // and materials

// A splitmix64 step. Integer-only, so results are identical everywhere.
[[nodiscard]] std::uint64_t next_random(std::uint64_t& state);
[[nodiscard]] int next_below(std::uint64_t& state, int bound);

// What a cabinet is: its square, its clues, and what its shapes and materials look like.
struct Puzzle {
    int n = 5;
    std::uint64_t seed = 1;
    std::array<int, most_stones> shape{};     // the solution, by socket (row-major)
    std::array<int, most_stones> material{};
    std::array<bool, most_stones> clue{};     // pinned at the start
    std::array<std::uint32_t, largest> family_seed{};  // each shape's family
    std::array<int, largest> family_style{};
    std::array<int, largest> material_kind{};  // each material: the engine's mineral
    std::array<std::uint32_t, largest> material_seed{};
};

// Where a stone lies.
enum class Place { drum, pile, felt, cabinet };

struct Where {
    Place place = Place::drum;
    int cell = -1;       // its socket, in the cabinet; its spot (0..pile_size-1), on the pile
    double x = 0;        // on the felt: across and down the strip, 0..1
    double y = 0;
};

struct Best {
    int moves = 0;       // 0: never solved
    int seconds = 0;
};

struct Session {
    Puzzle puzzle;
    std::array<Where, most_stones> where{};
    std::array<int, most_stones> turn{};      // each stone's quarter turns and face (looks only)
    std::vector<int> drum;                    // the stones still in the tumbler, next first
    std::vector<int> pile;                    // on the pile, oldest first
    int moves = 0;
    double seconds = 0;
    bool won = false;
    bool playing = false;                     // a cabinet is open; else a size is being chosen
    int next_n = 5;
    std::array<Best, largest + 1> best{};     // by size
};

[[nodiscard]] int stone_shape(const Puzzle& puzzle, int stone);
[[nodiscard]] int stone_material(const Puzzle& puzzle, int stone);
// The stone a socket's solution holds.
[[nodiscard]] int solution_stone(const Puzzle& puzzle, int cell);

// A random Graeco-Latin square of side n, its clues thinned while the answer stays
// unique, and its looks drawn: every puzzle has exactly one solution.
[[nodiscard]] Puzzle make_puzzle(std::uint64_t seed, int n);
// How many ways the cabinet's empty sockets can be filled from the stones not in it, so
// that the square is solved: counted up to `limit`. `stone_at` holds -1 for an empty socket.
[[nodiscard]] int count_solutions(const Puzzle& puzzle, const std::array<int, most_stones>& stone_at, int limit);

// A new cabinet: the clues pinned, the rest in the tumbler in a shuffled order, the pile
// filled from it. Keeps the records.
void new_game(Session& session, std::uint64_t seed, int n);

// The stone in a socket, or -1.
[[nodiscard]] int stone_in(const Session& session, int cell);
[[nodiscard]] bool movable(const Session& session, int stone);

// What a move did, for the view to show it.
struct Outcome {
    bool accepted = false;
    int displaced = -1;          // a stone sent from the socket to where the moved one was
    std::vector<int> dropped;    // stones the tumbler dropped onto the pile, in order
    bool won = false;
    bool new_best = false;
};
// Sets a stone in a socket. An occupied socket's stone (not a clue) trades places with it.
Outcome set_in_cabinet(Session& session, int stone, int cell);
// Lays a stone on the felt at (x, y), each 0..1 of the strip.
Outcome set_on_felt(Session& session, int stone, double x, double y);

// Sockets holding a stone that shares a shape or a material with another in its row or
// column: what the player is shown is wrong.
[[nodiscard]] std::vector<int> clashes(const Session& session);
[[nodiscard]] bool cabinet_full(const Session& session);

// The save body, "key=value" lines; decode leaves `destination` untouched unless the text
// is a session this version made.
[[nodiscard]] std::string encode_session(const Session& session);
[[nodiscard]] bool decode_session(const std::string& text, Session& destination);

}  // namespace sc
