// Headless Stillwater frames and timings: renders the scene at any size and time
// to a PNG and reports what each layer cost. Look at the picture; do not assume it.
//
//   stillwater_preview archive out.png [width height [time [supersample [frames [settle [pace]]]]]]
//     width, height  scene pixels (default 590 x 380, a 1180 x 760 point window at
//                    one scene pixel per two points)
//     time           scene seconds (default 12)
//     supersample    fixed-layer samples per side (default 2)
//     frames         extra animated frames to time (default 48)
//     settle         foliage that moves less than this many pixels is drawn once (default 0.5)
//     pace           milliseconds idle after each timed frame, as a governed window (default 0)
#include "archive.hpp"
#include "motion.hpp"
#include "png_writer.hpp"
#include "riverscape_look.hpp"
#include "stage.hpp"
#include "tanks.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

double milliseconds_since(Clock::time_point start) {
    const double result = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return result;
}

// The stage's 0xAARRGGBB words, as the BGRA bytes the PNG writer takes.
void write_frame(const std::string& path, int width, int height, const std::vector<std::uint32_t>& pixels) {
    std::vector<std::uint8_t> bgra(pixels.size() * 4U);
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        const std::uint32_t p = pixels[index];
        bgra[index * 4U] = static_cast<std::uint8_t>(p & 255U);
        bgra[index * 4U + 1U] = static_cast<std::uint8_t>((p >> 8U) & 255U);
        bgra[index * 4U + 2U] = static_cast<std::uint8_t>((p >> 16U) & 255U);
        bgra[index * 4U + 3U] = 255U;
    }
    static_cast<void>(kit::write_png(path, width, height, bgra));
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s archive out.png [width height [time [supersample [frames [settle [pace]]]]]]\n", argv[0]);
        return 2;
    }
    const int width = argc > 4 ? std::atoi(argv[3]) : 590;
    const int height = argc > 4 ? std::atoi(argv[4]) : 380;
    const double time = argc > 5 ? std::atof(argv[5]) : 12.0;
    const int supersample = argc > 6 ? std::atoi(argv[6]) : 2;
    const int frames = argc > 7 ? std::atoi(argv[7]) : 48;
    const float settle_pixels = argc > 8 ? static_cast<float>(std::atof(argv[8])) : 0.5F;
    // pace: milliseconds idle between timed frames, as a governed window leaves them (default 0)
    const int pace_ms = argc > 9 ? std::atoi(argv[9]) : 0;
    if (width < 16 || height < 16 || width > 8192 || height > 8192) {
        std::fprintf(stderr, "size out of range\n");
        return 2;
    }

    Clock::time_point start = Clock::now();
    ambient::SceneData scene{};
    std::string error{};
    if (!ambient::load_scene(argv[1], scene, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    // The game dresses the archive with its props (the chest) as it loads; so does
    // this. The tank (and its look) follows the archive's name.
    const sw::Tank tank = sw::tank_for_archive(argv[1]);
    sw::dress_tank(tank, scene);
    const double load_ms = milliseconds_since(start);

    start = Clock::now();
    // SW_NO_CAUSTICS=1 draws the tank without caustics (for before-and-after pictures).
    sw::TankStyle style = sw::tank_style(tank);
    if (std::getenv("SW_NO_CAUSTICS") != nullptr) {
        style.caustic_share = 0;
        style.caustics.size = 0;
    }
    const sw::RiverscapeLook look(scene, style);
    const double look_ms = milliseconds_since(start);

    // SW_CAUSTIC_TILE=file.png writes the caustic pattern's first frame: sharp (left)
    // and soft (right), tiled two by two.
    const char* tile_path = std::getenv("SW_CAUSTIC_TILE");
    if (tile_path != nullptr && !(*look.caustics()).empty()) {
        const ambient::CausticField& field = *look.caustics();
        const int n = field.size();
        std::vector<std::uint32_t> tile(static_cast<std::size_t>(n * 4) * static_cast<std::size_t>(n * 2));
        for (int y = 0; y < n * 2; ++y) {
            for (int x = 0; x < n * 4; ++x) {
                const std::uint16_t texel = field.texel(0, (y % n) * n + (x % n));
                const std::uint32_t value = x < n * 2 ? (texel & 255U) : (texel >> 8U);
                tile[static_cast<std::size_t>(y) * static_cast<std::size_t>(n * 4) + static_cast<std::size_t>(x)] =
                    0xFF000000U | (value << 16U) | (value << 8U) | value;
            }
        }
        write_frame(tile_path, n * 4, n * 2, tile);
    }
    if (std::getenv("SW_CAUSTIC_STATS") != nullptr) {
        for (int level = 0; level <= 8; level += 2) {
            double sum = 0;
            float peak = 0;
            int bright = 0;
            for (int k = 0; k < 10000; ++k) {
                const float s = (*look.caustics()).at({-6.0F + 0.0012F * k, static_cast<float>(level), 0.3F * (k % 37)}, 12.0F);
                sum += s;
                peak = std::max(peak, s);
                bright += s > 0.3F ? 1 : 0;
            }
            std::printf("height %d: mean %.3f peak %.3f bright %.3f\n", level, sum / 10000, static_cast<double>(peak), bright / 10000.0);
        }
    }

    start = Clock::now();
    const ambient::ShadowMap shadows = ambient::build_shadow_map(scene, look, 1024);
    const double shadow_ms = milliseconds_since(start);

    start = Clock::now();
    const ambient::Foliage foliage = ambient::prepare_foliage(scene, look, shadows);
    const double foliage_ms = milliseconds_since(start);

    start = Clock::now();
    ambient::FixedLayer layer =
        ambient::build_fixed_layer(scene, look, shadows, foliage, width, height, supersample, settle_pixels);
    const double fixed_ms = milliseconds_since(start);
    std::size_t settled = 0;
    for (const std::uint8_t flag : layer.settled)
        settled += flag;
    const std::size_t foliage_triangles = layer.settled.size();

    ambient::Stage stage(scene, look, foliage);
    stage.adopt(std::move(layer));
    std::vector<ambient::Creature> creatures = ambient::creatures_from(scene.actors);

    start = Clock::now();
    stage.update_sway(time);
    const double sway_ms = milliseconds_since(start);
    start = Clock::now();
    stage.compose(time, time, creatures);
    const double compose_ms = milliseconds_since(start);
    write_frame(argv[2], width, height, stage.frame());

    // Steady animation: sway every third frame (about 8 Hz at 24 frames a second).
    start = Clock::now();
    double idle_total = 0;
    double sway_total = 0;
    int sway_count = 0;
    for (int frame = 1; frame <= frames; ++frame) {
        const double t = time + frame / 24.0;
        if (frame % 3 == 0) {
            const Clock::time_point sway_start = Clock::now();
            stage.update_sway(t);
            sway_total += milliseconds_since(sway_start);
            ++sway_count;
        }
        stage.compose(t, t, creatures);
        if (pace_ms > 0) {
            const Clock::time_point rest = Clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(pace_ms));
            idle_total += milliseconds_since(rest);
        }
    }
    const double animated_ms = milliseconds_since(start) - idle_total;
    const double compose_mean = frames > 0 ? (animated_ms - sway_total) / frames : 0;
    std::printf("scene %d x %d, fixed supersample %d\n", width, height, supersample);
    std::printf("load %.1f ms, look %.1f ms, shadow map %.1f ms, foliage %.1f ms, fixed layer %.1f ms\n", load_ms,
                look_ms, shadow_ms, foliage_ms, fixed_ms);
    std::printf("foliage settled into the fixed layer: %zu of %zu triangles (threshold %.2f px)\n", settled,
                foliage_triangles, static_cast<double>(settle_pixels));
    std::printf("foliage hidden behind the fixed layer however it sways: %llu triangles\n",
                static_cast<unsigned long long>(stage.counters().hidden_triangles));
    std::printf("first sway %.1f ms, first compose %.1f ms\n", sway_ms, compose_ms);
    std::printf("steady: compose %.2f ms/frame, sway %.2f ms/update (%d), %.1f ms per second at 24 fps\n",
                compose_mean, sway_count > 0 ? sway_total / sway_count : 0.0,
                sway_count, compose_mean * 24 + (sway_count > 0 ? sway_total / sway_count : 0.0) * 8);
    std::printf("fragments/frame %.0f\n",
                frames > 0 ? static_cast<double>(stage.counters().actor_fragments) / (frames + 1) : 0.0);
    return 0;
}
