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
    for (int kind = 0; kind < 2; ++kind) {
        std::vector<double> times;
        for (int frame = 0; frame < 180; ++frame) {
            std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            if (kind == 0)
                raster.cube(cube, .5 + frame * .002, -.35, -1);
            else {
                raster.clear();
                for (int i = 0; i < 64; ++i)
                    raster.gem({(i % 8 + .5) * 102.75, (i / 8 + .5) * 102.75}, 39,
                               gems.state.grid[i], i == 0 ? frame * .01 : 0, .8, i);
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
