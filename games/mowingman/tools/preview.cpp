// Headless Mowing frames and timings. Look at the picture; do not assume it.
//
//   mm_preview out.png [width height [seconds [seed [livery [gnome]]]]]
//     seconds  how long the mower has been working (default 60)
//     livery   0 orange H, 1 red T, 2 green JD
//     gnome    1 to show the gnome looking about (frozen) near the mower's path
//     chase    1 to drive the mower through the first bed and show the old lady a few seconds in
#include "grass_art.hpp"
#include "png_writer.hpp"
#include "sim.hpp"
#include "yard.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

using Clock = std::chrono::steady_clock;

double milliseconds_since(Clock::time_point start) {
    const double result = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return result;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s out.png [width height [seconds [seed [livery [gnome]]]]]\n", argv[0]);
        return 2;
    }
    const int width = argc > 3 ? std::atoi(argv[2]) : 1100;
    const int height = argc > 3 ? std::atoi(argv[3]) : 700;
    const double seconds = argc > 4 ? std::atof(argv[4]) : 60.0;
    const std::uint64_t seed = argc > 5 ? static_cast<std::uint64_t>(std::atoll(argv[5])) : 3;
    const int livery = argc > 6 ? std::atoi(argv[6]) : 0;
    const bool gnome = argc > 7 && std::atoi(argv[7]) != 0;
    const bool chase = argc > 8 && std::atoi(argv[8]) != 0;

    Clock::time_point start = Clock::now();
    const mm::GrassArt art = mm::make_grass_art(mm::make_garden(seed), nullptr);
    const double art_ms = milliseconds_since(start);

    mm::Mowing mowing(seed, static_cast<mm::Livery>(std::clamp(livery, 0, mm::livery_count - 1)));
    start = Clock::now();
    for (double t = 0; t < seconds; t += 1.0 / 30)
        mowing.advance(1.0 / 30, false);
    const double sim_ms = milliseconds_since(start);
    if (gnome) {
        // Show the gnome standing in the grass a little way ahead.
        for (int k = 0; k < 4000 && mowing.gnome().state != mm::GnomeState::looking; ++k)
            mowing.advance(1.0 / 30, false);
        static_cast<void>(mowing.poke(mowing.gnome().x, mowing.gnome().y));
    }

    if (chase && !mowing.garden().beds.empty()) {
        const mm::Bed& bed = mowing.garden().beds[0];
        static_cast<void>(mowing.grab(mowing.mower().pose().x, mowing.mower().pose().y));
        for (int k = 0; k < 900 && mowing.granny().state == mm::GrannyState::away; ++k) {
            mowing.steer(bed.x, bed.y);
            mowing.advance(1.0 / 30, false);
        }
        for (int k = 0; k < 150; ++k) {
            mowing.steer(bed.x + 3.5, bed.y + 1.5);
            mowing.advance(1.0 / 30, false);
        }
    }
    mm::Yard yard{};
    start = Clock::now();
    yard.build(width, height, art, mowing);
    const double build_ms = milliseconds_since(start);
    mm::MowerPose pose{mowing.mower().pose().x, mowing.mower().pose().y, mowing.mower().pose().heading, 0.5, 0.3, false};
    start = Clock::now();
    int frames = 0;
    for (; frames < (chase ? 6 : 60); ++frames) {
        mowing.advance(1.0 / 30, false);
        yard.refresh(art, mowing, mowing.take_dirty());
        pose = {mowing.mower().pose().x, mowing.mower().pose().y, mowing.mower().pose().heading, 0.5, 0.3, false};
        static_cast<void>(yard.compose(mowing, pose, mowing.time()));
    }
    const double frame_ms = milliseconds_since(start) / frames;
    const std::vector<std::uint8_t>& picture = yard.compose(mowing, pose, mowing.time());
    static_cast<void>(kit::write_png(argv[1], width, height, picture));
    std::printf("grass art %.0f ms, %.0f s of mowing simulated in %.0f ms, lawn build %.1f ms, frame %.2f ms\n", art_ms,
                seconds, sim_ms, build_ms, frame_ms);
    std::printf("progress %.1f%%, phase %d, gnome state %d\n", mowing.mower().progress() * 100,
                static_cast<int>(mowing.mower().phase()), static_cast<int>(mowing.gnome().state));
    return 0;
}
