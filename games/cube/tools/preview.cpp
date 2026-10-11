// Renders Nature Cube boards to PNG without the PlaySuite window, for looking at.
//   cube_preview out.png width height scale level seed state [seconds] [environment.gpix backdrop.rgb]
// state: start, half (half the witness drawn), solved (every line drawn), arrive
// (`seconds` into the arrival), light (`seconds` into the finale), leave, portal,
// finale (`seconds` into the whole finale: flash, light, unwind, dark), next (`seconds`
// into the next pattern coming up in the darkened cube).
#include "generator.hpp"
#include "png_writer.hpp"
#include "session.hpp"
#include "scene.hpp"
#include "stage.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> read_bytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return bytes;
}

// The backdrop: the lake picture covering the frame, a little darker, as in the window.
void backdrop(std::vector<std::uint8_t>& frame, int width, int height, const std::vector<std::uint8_t>& picture) {
    if (picture.size() < 8) {
        for (std::size_t i = 0; i < frame.size(); i += 4) {
            frame[i] = 90;
            frame[i + 1] = 110;
            frame[i + 2] = 100;
            frame[i + 3] = 255;
        }
        return;
    }
    const int pw = picture[0] | picture[1] << 8 | picture[2] << 16;
    const int ph = picture[4] | picture[5] << 8 | picture[6] << 16;
    const double cover = std::max(static_cast<double>(width) / pw, static_cast<double>(height) / ph);
    const double ox = (pw * cover - width) * .5;
    const double oy = (ph * cover - height) * .5;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int sx = std::clamp(static_cast<int>((x + ox) / cover), 0, pw - 1);
            const int sy = std::clamp(static_cast<int>((y + oy) / cover), 0, ph - 1);
            const std::size_t s = 8 + (static_cast<std::size_t>(sy) * pw + sx) * 3;
            const std::size_t d = (static_cast<std::size_t>(y) * width + x) * 4;
            const double k = 1 - 56.0 / 255;
            frame[d] = static_cast<std::uint8_t>(picture[s + 2] * k + 24 * (1 - k));
            frame[d + 1] = static_cast<std::uint8_t>(picture[s + 1] * k + 24 * (1 - k));
            frame[d + 2] = static_cast<std::uint8_t>(picture[s] * k + 8 * (1 - k));
            frame[d + 3] = 255;
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 8) {
        std::fprintf(stderr, "usage: cube_preview out.png width height scale level seed state [seconds] [env backdrop]\n");
        return 2;
    }
    const std::string out = argv[1];
    const double width_points = std::atof(argv[2]);
    const double height_points = std::atof(argv[3]);
    const double scale = std::atof(argv[4]);
    const int level = std::atoi(argv[5]);
    const std::uint64_t seed = std::strtoull(argv[6], nullptr, 10);
    const std::string state = argv[7];
    const double seconds = argc > 8 ? std::atof(argv[8]) : 0;
    ps_cube::GenerateOptions options;
    if (state == "portal") {
        options.portals = 1;
    }
    const ps_cube::Puzzle puzzle = ps_cube::generate(level, seed, options);
    ps_cube::Play play = ps_cube::fresh_play(puzzle);
    std::vector<std::vector<int>> lines(puzzle.ends.size());
    if (state != "start" && state != "arrive") {
        for (std::size_t pair = 0; pair < puzzle.witness.size(); ++pair) {
            const bool all = state != "half" && state != "portal";
            if (all || pair % 2 == 0 || (state == "portal" && pair < puzzle.witness.size())) {
                lines[pair] = puzzle.witness[pair];
            }
        }
        if (state == "half" || state == "portal") {
            lines.back().clear();
            std::vector<int>& cut = lines[0];
            cut.resize(std::max<std::size_t>(1, cut.size() / 2));
            if (cut.size() >= 2 && puzzle.tiles[static_cast<std::size_t>(cut.back())] == ps_cube::Tile::portal &&
                !ps_cube::adjacent(puzzle.geometry, cut[cut.size() - 2], cut.back()) == false) {
                cut.pop_back();
            }
        }
        if (!ps_cube::replay(puzzle, lines, 3, play)) {
            std::fprintf(stderr, "replay failed\n");
        }
    }
    ps_cube::Motion motion;
    ps_cube::begin_arrival(motion, puzzle, true);
    ps_cube::note_lines(motion, puzzle, play, true);
    if (state == "arrive") {
        ps_cube::begin_arrival(motion, puzzle, false);
        static_cast<void>(ps_cube::advance(motion, puzzle, play, seconds, false));
    } else if (state == "finale" || state == "next") {
        // The finale in the window's own small steps, through to the dark cube.
        ps_cube::begin_completion(motion, false);
        const double until = state == "finale" ? seconds : 60;
        for (double t = 0; t < until && !motion.departed; t += 1.0 / 30) {
            static_cast<void>(ps_cube::advance(motion, puzzle, play, 1.0 / 30, false));
        }
    } else if (state == "light" || state == "leave") {
        ps_cube::begin_completion(motion, false);
        static_cast<void>(ps_cube::advance(motion, puzzle, play, state == "leave" ? ps_cube::lighting_seconds : seconds, false));
        if (state == "leave") {
            static_cast<void>(ps_cube::advance(motion, puzzle, play, seconds, false));
        }
    }
    const int width = static_cast<int>(std::lround(width_points * scale));
    const int height = static_cast<int>(std::lround(height_points * scale));
    std::vector<std::uint8_t> frame(static_cast<std::size_t>(width) * height * 4, 0);
    backdrop(frame, width, height, argc > 10 ? read_bytes(argv[10]) : std::vector<std::uint8_t>{});
    const ps_cube::Layout layout = ps_cube::compute_layout(width_points, height_points);
    // Drawn as the window draws it: straight into the frame at its size.
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * height);
    std::memcpy(pixels.data(), frame.data(), frame.size());
    const render::Target target{pixels.data(), width, width, height, render::Order::bgra};
    render::r3d::Buffers buffers;
    buffers.resize(width, height, false);
    render::r3d::Panorama panorama;
    if (argc > 9) {
        static_cast<void>(panorama.load_gpix(read_bytes(argv[9]), render::Order::bgra, .82F, 35));
    }
    const ps_cube::Box board{layout.board.x * scale, layout.board.y * scale, layout.board.w * scale,
                             layout.board.h * scale};
    ps_cube::Drawing drawing{render::r3d::Pass{target, &buffers, target.bounds()},
                             panorama.empty() ? nullptr : &panorama, board, false};
    std::vector<std::uint32_t> scratch;
    if (state == "next") {
        // The next board comes up in the dark cube.
        const ps_cube::Puzzle next = ps_cube::generate(level, seed + 1, ps_cube::GenerateOptions{});
        const ps_cube::Play fresh = ps_cube::fresh_play(next);
        ps_cube::begin_arrival(motion, next, false);
        for (double t = 0; t < seconds; t += 1.0 / 30) {
            static_cast<void>(ps_cube::advance(motion, next, fresh, 1.0 / 30, false));
        }
        ps_cube::show_cube(drawing, scratch, next, fresh, motion, -1);
    } else {
        ps_cube::show_cube(drawing, scratch, puzzle, play, motion, -1);
    }
    std::memcpy(frame.data(), pixels.data(), frame.size());
    // The top band the capsule floats over, outlined for reference.
    if (!kit::write_png(out, width, height, frame)) {
        return 1;
    }
    std::printf("%s: level %d side %d pairs %zu portals %zu\n", out.c_str(), level, puzzle.side, puzzle.ends.size(),
                puzzle.portals.size());
    return 0;
}
