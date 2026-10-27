// The gnome at work, frame by frame: a garden is mown until he comes up, then a frame
// is written every `step` seconds while he dances, dives and pops up again.
//
//   mm_gnome_film out_prefix [frames [step [seed [width height]]]]
#include "png_writer.hpp"
#include "sim.hpp"
#include "yard.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s out_prefix [frames [step [seed [width height]]]]\n", argv[0]);
        return 2;
    }
    const std::string prefix = argv[1];
    const int frames = argc > 2 ? std::atoi(argv[2]) : 8;
    const double step = argc > 3 ? std::atof(argv[3]) : 0.5;
    const std::uint64_t seed = argc > 4 ? static_cast<std::uint64_t>(std::atoll(argv[4])) : 3;
    const int width = argc > 6 ? std::atoi(argv[5]) : 1400;
    const int height = argc > 6 ? std::atoi(argv[6]) : 900;
    const mm::GrassArt art = mm::make_grass_art(mm::make_garden(seed), nullptr);
    mm::Mowing mowing(seed, mm::Livery::orange_h);
    double time = 0;
    while (mowing.gnome().state != mm::GnomeState::looking && time < 600) {
        const mm::GnomeState before = mowing.gnome().state;
        mowing.advance(1.0 / 30, false);
        if (before == mm::GnomeState::hidden && mowing.gnome().state != mm::GnomeState::hidden)
            std::printf("up at %.2f s: gnome (%.1f, %.1f), mower (%.1f, %.1f) heading %.2f\n", time, mowing.gnome().x, mowing.gnome().y, mowing.mower().pose().x,
                        mowing.mower().pose().y, mowing.mower().pose().heading);
        static_cast<void>(mowing.take_cues());
        time += 1.0 / 30;
    }
    mm::Yard yard{};
    yard.build(width, height, art, mowing);
    for (int k = 0; k < frames; ++k) {
        for (double t = 0; t < step; t += 1.0 / 30) {
            mowing.advance(1.0 / 30, false);
            for (const mm::Cue& cue : mowing.take_cues())
                if (cue.name.rfind("mm_gnome", 0) == 0)
                    std::printf("  %.2f s: %s\n", time, cue.name.c_str());
            yard.refresh(art, mowing, mowing.take_dirty());
            time += 1.0 / 30;
        }
        const mm::MowerPose pose{mowing.mower().pose().x, mowing.mower().pose().y, mowing.mower().pose().heading, 0.5, 0.3, false};
        const std::vector<std::uint8_t>& picture = yard.compose(mowing, pose, time);
        const mm::Gnome& g = mowing.gnome();
        std::printf("frame %d at %.2f s: gnome state %d dance %d pops %d, %.1f m from the mower\n", k, time, static_cast<int>(g.state), g.dance, g.pops,
                    std::hypot(g.x - pose.x, g.y - pose.y));
        const std::string name = prefix + std::to_string(k) + ".png";
        static_cast<void>(kit::write_png(name.c_str(), width, height, picture));
    }
    return 0;
}
