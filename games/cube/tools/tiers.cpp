// Deals boards at each level and prints their measured difficulty, with timings.
//   cube_tiers [boards per level]
#include "generator.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    const int boards = argc > 1 ? std::atoi(argv[1]) : 20;
    for (int level = 0; level < ps_cube::levels; ++level) {
        double slowest = 0;
        double total = 0;
        for (int index = 0; index < boards; ++index) {
            const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            const ps_cube::Puzzle puzzle = ps_cube::generate(level, 1000 + index, ps_cube::GenerateOptions{});
            const double seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            slowest = seconds > slowest ? seconds : slowest;
            total += seconds;
            ps_cube::SolveLimits limits;
            limits.node_budget = 400000;
            limits.solution_cap = 1000;
            const ps_cube::Metrics m = ps_cube::measure(puzzle, limits);
            std::printf("L%d seed %d side %d pairs %d stones %2d portals %d need %d | sol %4d%s back %6ld forced %d len %3d longest %2d cross %2d detour %3d conflicts %d | %.3fs\n",
                        level, 1000 + index, puzzle.side, m.pairs, m.stones, m.portals, m.portal_needed ? 1 : 0,
                        m.solutions, m.solutions_exact ? " " : "+", m.backtracks, m.forced_starts,
                        m.total_length, m.longest, m.crossings, m.detour, m.conflicts, seconds);
        }
        std::printf("level %d: mean %.3fs slowest %.3fs\n", level, total / boards, slowest);
    }
    return 0;
}
