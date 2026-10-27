#include "puzzle_render.hpp"
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
void write_preview(const games::PuzzleRaster& raster, const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary);
    output << "P6\n" << raster.width << ' ' << raster.height << "\n255\n";
    for (std::size_t pixel = 0; pixel < raster.ids.size(); ++pixel) {
        const std::array<char, 3> rgb{
            static_cast<char>(raster.pixels[pixel * 4 + 2]),
            static_cast<char>(raster.pixels[pixel * 4 + 1]),
            static_cast<char>(raster.pixels[pixel * 4])};
        output.write(rgb.data(), rgb.size());
    }
    require(static_cast<bool>(output), "Write complete rendered preview");
}
void depth_and_winding() {
    games::PuzzleRaster first{};
    games::PuzzleRaster second{};
    first.resize(80, 80);
    second.resize(80, 80);
    const games::PixelColor red{255, 0, 0, 255};
    const games::PixelColor blue{0, 0, 255, 255};
    const games::RasterVertex a{8, 8, 1, red};
    const games::RasterVertex b{68, 8, 1, red};
    const games::RasterVertex c{8, 68, 1, red};
    const games::RasterVertex d{8, 8, 2, blue};
    const games::RasterVertex e{68, 8, 2, blue};
    const games::RasterVertex f{8, 68, 2, blue};
    first.triangle(d, e, f, 2);
    first.triangle(a, b, c, 1);
    second.triangle(c, b, a, 1);
    second.triangle(f, e, d, 2);
    require(first.ids == second.ids, "Pick coverage is independent of submission order and winding");
    require(first.pixels == second.pixels, "Color is independent of submission order and winding");
    const std::size_t inside = 20 * 80 + 20;
    require(first.ids[inside] == 1 && first.pixels[inside * 4 + 2] == std::byte{255},
            "Front triangle owns the visible pixel and pick target");
    require(first.ids[79 * 80 + 79] == -1, "Uncovered pixels have no pick target");
    games::PuzzleRaster joined{};
    joined.resize(80, 80);
    joined.triangle(a, b, c, 1);
    joined.triangle(b, {68, 68, 1, red}, c, 1);
    for (int y = 8; y < 68; ++y) {
        for (int x = 8; x < 68; ++x) {
            require(joined.ids[static_cast<std::size_t>(y * 80 + x)] == 1,
                    "Adjacent triangles cover their shared diagonal without cracks");
        }
    }
    const std::vector<std::byte> before = first.pixels;
    first.triangle(a, a, a, 9);
    require(first.pixels == before, "Degenerate geometry leaves the image intact");
}
void cube_picking(const std::filesystem::path& output) {
    games::PuzzleGame cube(games::PuzzleKind::cube);
    cube.deal(42);
    games::PuzzleRaster raster{};
    raster.resize(300, 300);
    for (double yaw : {.4, .75, 1.1}) {
        for (double pitch : {-.78, -.56, -.34}) {
            raster.cube(cube, yaw, pitch, -1);
            std::set<int> faces{};
            std::set<int> cells{};
            for (std::size_t pixel = 0; pixel < raster.ids.size(); ++pixel) {
                const int cell = raster.ids[pixel];
                if (cell >= 0) {
                    require(games::PuzzleGame::cube_playable(cell), "Pick map contains only playable cells");
                    require(std::isfinite(raster.depth[pixel]), "Picked pixel has finite depth");
                    faces.insert(cell / 16);
                    cells.insert(cell);
                }
            }
            require(faces == std::set<int>{0, 3, 4}, "All three playable cube faces stay visible");
            require(cells.size() == 48, "All forty-eight cube cells remain pickable across camera tilt");
        }
    }
    if (!output.empty()) { write_preview(raster, output / "nature-cube.ppm"); }
}
}
int main(int argc, char** argv) {
    try {
        require(argc <= 2, "Optional argument is the preview output directory");
        std::filesystem::path output{};
        if (argc == 2) {
            output = argv[1];
            std::filesystem::create_directories(output);
        }
        depth_and_winding();
        cube_picking(output);
        std::cout << "Raster depth, winding and cube picking passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
