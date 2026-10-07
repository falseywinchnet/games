// Times the cube's frame work: drawing the raster and laying it over the lake.
//   cube_bench [environment.gpix]
#include "generator.hpp"
#include "raster3d.hpp"
#include "scene.hpp"
#include "session.hpp"
#include "stage.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
    ps_cube::Raster3D raster;
    if (argc > 1) {
        std::ifstream file(argv[1], std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        static_cast<void>(raster.load_environment(bytes));
    }
    for (int level = 1; level <= 2; ++level) {
        const ps_cube::Puzzle puzzle = ps_cube::generate(level, 77, ps_cube::GenerateOptions{});
        ps_cube::Play play;
        static_cast<void>(ps_cube::replay(puzzle, puzzle.witness, 1, play));
        const int frame_width = 1100;
        const int frame_height = 760;
        std::vector<std::uint8_t> frame(static_cast<std::size_t>(frame_width) * frame_height * 4, 40);
        const ps_cube::Layout layout = ps_cube::compute_layout(frame_width, frame_height);
        for (int draft = 1; draft >= 0; --draft) {
            const double density = draft ? .62 : 1.25;
            const int side = static_cast<int>(layout.board.w * density);
            raster.resize(side, side);
            raster.draft = draft == 1;
            ps_cube::Motion motion;
            ps_cube::begin_arrival(motion, puzzle, false);
            ps_cube::note_lines(motion, puzzle, play, true);
            ps_cube::begin_completion(motion, false);
            double draw = 0;
            double lay = 0;
            const int frames = 60;
            for (int index = 0; index < frames; ++index) {
                static_cast<void>(ps_cube::advance(motion, puzzle, play, 1.0 / 30, false));
                const std::chrono::steady_clock::time_point a = std::chrono::steady_clock::now();
                ps_cube::draw_cube(raster, puzzle, play, motion, -1);
                const std::chrono::steady_clock::time_point b = std::chrono::steady_clock::now();
                ps_cube::composite(raster, frame.data(), frame_width, frame_height, frame_width * 4, false,
                                   layout.board.x, layout.board.y, layout.board.w, layout.board.h, 1);
                const std::chrono::steady_clock::time_point c = std::chrono::steady_clock::now();
                draw += std::chrono::duration<double>(b - a).count();
                lay += std::chrono::duration<double>(c - b).count();
            }
            std::printf("level %d %s raster %d: draw %.2f ms, composite %.2f ms per frame\n", level,
                        draft ? "draft" : "full", side, draw / frames * 1000, lay / frames * 1000);
        }
    }
    return 0;
}
