#include "gem_art.hpp"
#include "puzzle_render.hpp"
#include "storage.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>
using namespace games;
int main() {
    PuzzleRaster raster;
    raster.resize(822, 822);
    raster.load_environment(asset_directory() + "/nature-lake.png");
    PuzzleGame cube(PuzzleKind::cube);
    cube.deal(42);
    PuzzleGame gems(PuzzleKind::gems);
    gems.deal(42);
    std::vector<std::uint32_t> layer(822 * 822);
    render::r3d::Buffers depth;
    depth.resize(822, 822, false);
    for (int kind = 0; kind < 2; ++kind) {
        std::vector<double> times;
        for (int frame = 0; frame < 180; ++frame) {
            std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            if (kind == 0)
                raster.cube(cube, .5 + frame * .002, -.35, -1);
            else {
                // Gems are drawn by the render engine into their own layer.
                std::fill(layer.begin(), layer.end(), 0U);
                depth.clear({0, 0, 822, 822});
                const render::Target target{layer.data(), 822, 822, 822, render::Order::bgra};
                render::r3d::Pass pass{target, &depth, render::Rect{0, 0, 822, 822}};
                for (int i = 0; i < 64; ++i)
                    draw_gem(pass, (i % 8 + .5) * 102.75, (i / 8 + .5) * 102.75, 39,
                             gems.state.grid[i], i == 0 ? frame * .01 : 0, .8, 0);
            }
            double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count();
            if (frame >= 20)
                times.push_back(ms);
        }
        std::sort(times.begin(), times.end());
        std::cout << (kind == 0 ? "cube" : "gems") << " 822x822 CPU raster: median "
                  << times[times.size() / 2] << " ms, p95 " << times[times.size() * 95 / 100]
                  << " ms; no upload, composition, input, or display latency included\n";
    }
}
