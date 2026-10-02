#pragma once
#include "scores.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <random>
#include <string>
#include <vector>
namespace games {
enum class PuzzleKind { gems, cube, untangle, atom, pegs, switches, solve, sticks };
struct Point2 {
    double x = 0, y = 0;
};
struct Point3 {
    double x = 0, y = 0, z = 0;
};
struct Edge {
    int a = 0, b = 0;
};
struct PegFeedback {
    int exact = 0, misplaced = 0;
};
struct PieceCell {
    int x = 0, y = 0, color = 0, wedge = 0;
};
struct PuzzleState {
    std::array<int, 96> grid{}, secret{}, marks{}, aux{};
    std::vector<Point2> nodes, embedding;
    std::vector<Edge> edges;
    std::vector<std::vector<int>> paths, solution_paths;
    std::uint32_t seed = 1, random_state = 1;
    int moves = 0, score = 0, stage = 0, progress = 0;
    bool over = false, won = false;
};
class PuzzleGame {
  public:
    explicit PuzzleGame(PuzzleKind kind = PuzzleKind::gems);
    PuzzleKind kind;
    PuzzleState state;
    std::vector<TopScore> scores;
    std::string player_name = "Player", message;
    bool recorded = false;
    // Difficulty for the next deal, 0 easy to 2 hard (Nature Cube and Untangle).
    int level = 1;
    std::vector<std::array<int, 96>> cascade_frames;
    void deal(std::uint32_t seed);
    bool gem_swap(int a, int b);
    bool gem_has_move() const;
    int gem_colors() const;
    // `by_player` false is the kitten's doing: it moves the peg without counting a move.
    bool move_node(int node, Point2 position, bool by_player = true);
    int crossings() const;
    // Untangle threads come in colored layers: a thread may cross another color, never its own.
    [[nodiscard]] int untangle_layer(int edge) const {
        return edge >= 0 && edge < 90 ? std::clamp(state.aux[edge], 0, 2) : 0;
    }
    [[nodiscard]] int untangle_layers() const;
    // Frozen pegs (marks 1) cannot move until every thread tied to them is clear; then they
    // thaw for good (marks 2).
    [[nodiscard]] bool untangle_frozen(int node) const {
        return node >= 0 && node < 96 && state.marks[node] == 1;
    }
    [[nodiscard]] bool threads_cross(int e, int f, const std::vector<Point2>& at) const;
    [[nodiscard]] bool node_clear(int node, const std::vector<Point2>& at) const;
    bool guess_pegs(const std::array<int, 4>& guess);
    static PegFeedback evaluate_pegs(const std::array<int, 4>& code,
                                     const std::array<int, 4>& guess);
    bool probe(int port);
    int atom_trace(int port, const std::array<int, 96>& board) const;
    bool mark_atom(int cell);
    bool submit_atoms();
    bool cube_start(int cell);
    bool cube_extend(int cell);
    static Point3 cube_center(int cell);
    static bool cube_playable(int cell);
    static bool cube_adjacent(int a, int b);
    int cube_pair(int cell) const;
    [[nodiscard]] int cube_pairs() const {
        return static_cast<int>(state.solution_paths.size());
    }
    // Mossy stones that no path may cross (Medium and Hard).
    [[nodiscard]] bool cube_rock(int cell) const {
        return cell >= 0 && cell < 96 && state.grid[cell] == 1;
    }
    // Hard asks for every open tile to be covered.
    [[nodiscard]] bool cube_fill() const {
        return state.aux[94] == 2;
    }
    [[nodiscard]] int cube_open_tiles() const;
    bool cube_witness_valid() const;
    std::vector<PieceCell> piece_cells(int piece, int rotation, bool flip) const;
    int piece_count() const {
        return state.aux[95] == 3 ? 7 : 12;
    }
    bool place_piece(int piece, int x, int y, int rotation, bool flip);
    bool remove_piece(int piece);
    bool set_stick(int cell, int value);
    static std::vector<Point2> hex_cells();
    std::array<int, 2> hex_count(int axis, int line, bool target) const;
    bool qualifies() const;
    bool record(const std::string& name);
    int result() const;
    bool invariant() const;
    bool save(const std::filesystem::path& path) const;
    bool load(const std::filesystem::path& path);

  private:
    int random(int limit);
    bool resolve_gems(int preferred);
    void generate_cube();
    void generate_untangle();
    void thaw();
    void generate_atoms();
};
const char* puzzle_title(PuzzleKind kind);
const char* puzzle_slug(PuzzleKind kind);
} // namespace games
