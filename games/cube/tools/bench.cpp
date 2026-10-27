// Times the cube's frames as the window draws them: a 1100 x 720 point window at the
// given scale, three buffers in turn, each frame repairing what its buffer missed.
//   cube_bench [environment.gpix] [scale] [turning frames]
// Prints, for a turning cube, a line traced on a resting cube and a hover moving, the
// time and the pixels drawn per frame.
#include "generator.hpp"
#include "r2d.hpp"
#include "scene.hpp"
#include "session.hpp"
#include "stage.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <vector>

namespace {

struct Window {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> backdrop;
    std::array<std::vector<std::uint32_t>, 3> buffers;
    std::array<render::Rect, 3> missed;  // per buffer: what changed since it was drawn
    std::size_t next = 0;
    render::r3d::Buffers depth;
    std::vector<std::uint32_t> scratch;
    render::r3d::Panorama panorama;
    ps_cube::Box board;

    render::Target target(std::vector<std::uint32_t>& pixels) const {
        const render::Target t{pixels.data(), width, width, height, render::Order::rgba};
        return t;
    }
    // One frame: returns the pixels repaired.
    long long frame(render::Rect damage, const ps_cube::Puzzle& puzzle, const ps_cube::Play& play,
                    const ps_cube::Motion& motion, int hover) {
        for (render::Rect& r : missed) {
            r = r.united(damage);
        }
        const render::Rect repair = missed[next];
        missed[next] = render::Rect{};
        const render::Target out = target(buffers[next]);
        next = (next + 1) % buffers.size();
        render::r2d::restore(out, target(backdrop), repair);
        depth.clear(repair);
        ps_cube::Drawing drawing{render::r3d::Pass{out, &depth, repair}, &panorama, board, false};
        ps_cube::show_cube(drawing, scratch, puzzle, play, motion, hover);
        return repair.area();
    }
};

void report(const char* what, double seconds, long long pixels, int frames, long long total) {
    std::printf("  %-26s %6.2f ms, %5.1f%% of the window repaired per frame\n", what, seconds / frames * 1000,
                100.0 * static_cast<double>(pixels) / frames / static_cast<double>(total));
}

}  // namespace

int main(int argc, char** argv) {
    const double scale = argc > 2 ? std::atof(argv[2]) : 2;
    Window window;
    if (argc > 1) {
        std::ifstream file(argv[1], std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        static_cast<void>(window.panorama.load_gpix(bytes, render::Order::rgba, .82F, 35));
    }
    window.width = static_cast<int>(1100 * scale);
    window.height = static_cast<int>(720 * scale);
    const std::size_t count = static_cast<std::size_t>(window.width) * static_cast<std::size_t>(window.height);
    window.backdrop.assign(count, 0xff3c3e28U);
    for (auto& buffer : window.buffers) {
        buffer.assign(count, 0xff3c3e28U);
    }
    window.depth.resize(window.width, window.height, true);
    const ps_cube::Layout layout = ps_cube::compute_layout(1100, 720);
    window.board = ps_cube::Box{layout.board.x * scale, layout.board.y * scale, layout.board.w * scale, layout.board.h * scale};
    const long long total = static_cast<long long>(count);
    std::printf("window %d x %d, cube %.0f px square\n", window.width, window.height, window.board.w);
    for (int level = 1; level <= 2; ++level) {
        const ps_cube::Puzzle puzzle = ps_cube::generate(level, 77, ps_cube::GenerateOptions{});
        std::printf("level %d (%d cells a face)\n", level, puzzle.side * puzzle.side);
        const auto timed = [](const std::function<long long()>& body, double& seconds) {
            const auto a = std::chrono::steady_clock::now();
            const long long pixels = body();
            seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
            return pixels;
        };
        // A solved cube lighting up and spinning away: every frame moves it.
        {
            ps_cube::Play play;
            static_cast<void>(ps_cube::replay(puzzle, puzzle.witness, 1, play));
            ps_cube::Motion motion;
            ps_cube::begin_arrival(motion, puzzle, true);
            ps_cube::note_lines(motion, puzzle, play, true);
            ps_cube::begin_completion(motion, false);
            render::Rect last = ps_cube::cube_bounds(motion, window.board);
            double seconds = 0;
            long long pixels = 0;
            const int frames = argc > 3 ? std::max(1, std::atoi(argv[3])) : 60;
            for (int index = 0; index < frames; ++index) {
                static_cast<void>(ps_cube::advance(motion, puzzle, play, 1.0 / 40, false));
                if (motion.phase != ps_cube::Phase::lighting) {
                    // Long runs (for profiling) light the cube again rather than let it go.
                    ps_cube::begin_completion(motion, false);
                }
                const render::Rect now = ps_cube::cube_bounds(motion, window.board);
                pixels += timed([&] { return window.frame(now.united(last), puzzle, play, motion, -1); }, seconds);
                last = now;
            }
            report("turning", seconds, pixels, frames, total);
        }
        // At rest, the witness traced step by step, each step growing for a few frames.
        {
            ps_cube::Play play = ps_cube::fresh_play(puzzle);
            ps_cube::Motion motion;
            ps_cube::begin_arrival(motion, puzzle, true);
            ps_cube::note_lines(motion, puzzle, play, true);
            for (int k = 0; k < 3; ++k) {
                static_cast<void>(window.frame(render::Rect{0, 0, window.width, window.height}, puzzle, play, motion, -1));
            }
            ps_cube::Play solved;
            static_cast<void>(ps_cube::replay(puzzle, puzzle.witness, 1, solved));
            double seconds = 0;
            long long pixels = 0;
            int frames = 0;
            for (std::size_t pair = 0; pair < solved.paths.size() && frames < 240; ++pair) {
                for (std::size_t step = 1; step <= solved.paths[pair].size() && frames < 240; ++step) {
                    std::vector<int> before = play.paths[pair];
                    play.paths[pair].assign(solved.paths[pair].begin(),
                                            solved.paths[pair].begin() + static_cast<std::ptrdiff_t>(step));
                    ps_cube::note_lines(motion, puzzle, play, false);
                    bool growing = true;
                    while (growing) {
                        growing = ps_cube::advance(motion, puzzle, play, 1.0 / 60, false);
                        std::vector<int> cells = play.paths[pair];
                        cells.insert(cells.end(), before.begin(), before.end());
                        const render::Rect damage = ps_cube::cell_bounds(puzzle, motion, window.board, cells);
                        pixels += timed([&] { return window.frame(damage, puzzle, play, motion, -1); }, seconds);
                        ++frames;
                        before = play.paths[pair];
                    }
                }
            }
            report("tracing a line at rest", seconds, pixels, frames, total);
        }
        // At rest, the hover moving from cell to cell.
        {
            ps_cube::Play play = ps_cube::fresh_play(puzzle);
            ps_cube::Motion motion;
            ps_cube::begin_arrival(motion, puzzle, true);
            ps_cube::note_lines(motion, puzzle, play, true);
            double seconds = 0;
            long long pixels = 0;
            int frames = 0;
            int previous = -1;
            for (int cell = 0; cell < puzzle.geometry.cells && frames < 120; cell += 3, ++frames) {
                const render::Rect damage = ps_cube::cell_bounds(puzzle, motion, window.board, {previous, cell});
                pixels += timed([&] { return window.frame(damage, puzzle, play, motion, cell); }, seconds);
                previous = cell;
            }
            report("hover moving at rest", seconds, pixels, frames, total);
        }
    }
    return 0;
}
