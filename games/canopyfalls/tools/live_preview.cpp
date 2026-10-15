// The live falls drawn headless, as the window draws them: bakes a size (timed), runs
// the water, draws frames (timed) and writes some to PNG, to look at and to measure.
//
//   falls_live out_dir [width height [frames [density [samples]]]]
#include "png_writer.hpp"
#include "still.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

bool write(const std::string& path, const std::vector<std::uint32_t>& words, int width, int height) {
    std::vector<std::uint8_t> bgra(words.size() * 4);
    for (std::size_t i = 0; i < words.size(); ++i) {
        bgra[i * 4] = static_cast<std::uint8_t>(words[i]);
        bgra[i * 4 + 1] = static_cast<std::uint8_t>(words[i] >> 8);
        bgra[i * 4 + 2] = static_cast<std::uint8_t>(words[i] >> 16);
        bgra[i * 4 + 3] = 255;
    }
    return kit::write_png(path, width, height, bgra);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s out_dir [width height [frames [density [samples]]]]\n", argv[0]);
        return 2;
    }
    const std::string directory = argv[1];
    const int width = argc > 3 ? std::atoi(argv[2]) : 550;
    const int height = argc > 3 ? std::atoi(argv[3]) : 380;
    const int frames = argc > 4 ? std::atoi(argv[4]) : 120;
    const double density = argc > 5 ? std::atof(argv[5]) : 1;
    const int samples = argc > 6 ? std::atoi(argv[6]) : 2;
    if (width < 16 || height < 16 || frames < 1 || width > 4000 || height > 4000)
        return 2;
    cf::Look look;
    look.sky.sun_elevation = 46 * cf::pi / 180;
    look.sky.haze = 2.0;
    look.sky.haze_forward = .76;
    cf::LiveFalls falls(look, cf::Camera{});
    falls.set_density(density);
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    falls.prepare_once();
    const double prepared = seconds_since(start);
    start = std::chrono::steady_clock::now();
    falls.begin_size(width, height, samples);
    for (int band = 0; band < falls.bands(); ++band)
        falls.bake_band(band);
    falls.finish_size();
    falls.adopt();
    const double baked = seconds_since(start);
    std::vector<std::uint32_t> picture;
    double drawing = 0, advancing = 0;
    for (int f = 0; f < frames; ++f) {
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        falls.advance(1.0 / 24);
        advancing += seconds_since(t0);
        t0 = std::chrono::steady_clock::now();
        falls.draw(picture);
        drawing += seconds_since(t0);
        if (f == 0 || f == frames / 2 || f == frames - 1) {
            const std::string path = directory + "/live-" + std::to_string(f) + ".png";
            if (!write(path, picture, width, height))
                return 1;
        }
    }
    std::printf("%dx%d: prepared %.2fs, baked %.2fs (%d bands, one thread)\n", width, height, prepared, baked, falls.bands());
    std::printf("per frame: advance %.2f ms, draw %.2f ms, %zu particles\n", advancing / frames * 1000,
                drawing / frames * 1000, falls.particle_count());
    double stages[6] = {};
    falls.stage_costs(stages);
    std::printf("  copy %.2f, sheet %.2f, glitter %.2f, shade %.2f, particles %.2f, tone %.2f ms\n", stages[0] * 1000,
                stages[1] * 1000, stages[2] * 1000, stages[3] * 1000, stages[4] * 1000, stages[5] * 1000);
    return 0;
}
