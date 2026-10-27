// M4-only review diagnostic: wall time, process CPU, exact serial/threaded images,
// and pixels that change while physics is quiet. No saves or simulation changes.
#include "site.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sys/resource.h>

namespace {
using Clock = std::chrono::steady_clock;
double cpu_seconds() {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_utime.tv_sec + usage.ru_utime.tv_usec * 1e-6 +
           usage.ru_stime.tv_sec + usage.ru_stime.tv_usec * 1e-6;
}
std::uint64_t checksum(const zc::Canvas& canvas) {
    std::uint64_t sum = 1469598103934665603ULL;
    for (std::uint8_t byte : canvas.px) sum = (sum ^ byte) * 1099511628211ULL;
    return sum;
}
}

int main(int argc, char** argv) {
    const int width = argc > 1 ? std::atoi(argv[1]) : 550;
    const int height = argc > 2 ? std::atoi(argv[2]) : 360;
    const int frames = argc > 3 ? std::atoi(argv[3]) : 120;
    if (width < 16 || height < 16 || frames < 1) return 2;
    zc::Run run;
    run.begin(7, "Engine review");
    for (int frame = 0; frame < 3600; ++frame) run.step(zc::CraneInput{}, 0, .5);
    std::printf("quiet %d busy %d rocks %zu\n", run.quiet(), run.busy(), run.rocks.size());
    const std::string saved = run.save();
    zc::Site site;
    site.resize(width, height);
    site.set_camera(zc::OrbitCamera{});
    zc::SceneState state;
    state.run = &run;
    state.time = 12;
    site.render(state, true);
    zc::Canvas out;
    out.resize(width, height);
    // Interleaved repeats reduce drift from a shared host. Warm the same scene first.
    for (int repeat = 0; repeat < 3; ++repeat) {
        for (int mode = 0; mode < 3; ++mode) {
            site.r.threads = (mode != 0);
            const Clock::time_point start = Clock::now();
            const double cpu = cpu_seconds();
            for (int frame = 0; frame < frames; ++frame) {
                state.time = 12 + frame * .1;
                site.render(state, mode == 2);
            }
            const double spent = cpu_seconds() - cpu;
            const double wall = std::chrono::duration<double>(Clock::now() - start).count();
            site.r.present(out, 1, 0, 0, true);
            std::printf("repeat %d mode %d %dx%d wall %.4f cpu %.4f ms/frame picture %016llx\n",
                        repeat + 1, mode, width, height, wall * 1000 / frames, spent * 1000 / frames,
                        static_cast<unsigned long long>(checksum(out)));
        }
    }
    int mismatched = 0;
    std::size_t changed_pixels = 0;
    std::vector<std::uint8_t> previous;
    for (int frame = 0; frame < 12; ++frame) {
        state.time = 12 + frame * .1;
        site.r.threads = false;
        site.render(state, false);
        const std::vector<float> rgb = site.r.rgb;
        const std::vector<float> depth = site.r.depth;
        site.r.threads = true;
        site.render(state, false);
        mismatched += rgb != site.r.rgb || depth != site.r.depth;
        site.render(state, true);
        mismatched += rgb != site.r.rgb || depth != site.r.depth;
        site.r.present(out, 1, 0, 0, true);
        if (!previous.empty()) {
            for (std::size_t i = 0; i < out.px.size(); i += 4) {
                changed_pixels += out.px[i] != previous[i] || out.px[i + 1] != previous[i + 1] ||
                                  out.px[i + 2] != previous[i + 2];
            }
        }
        previous = out.px;
    }
    std::printf("serial/threaded/quiet mismatched comparisons %d/24; mean quiet changed pixels %.1f\n",
                mismatched, changed_pixels / 11.0);
    // Camera/cache transitions, hover and mood must only affect scheduling, never pixels.
    int transition_errors = 0;
    for (int frame = 0; frame < 8; ++frame) {
        zc::OrbitCamera camera;
        camera.yaw = -.7 + .2 * frame;
        camera.pitch = .35 + .04 * frame;
        site.resize(frame % 2 ? 300 : width, frame % 2 ? 210 : height);
        site.set_camera(camera);
        state.time = frame % 2 ? 0 : 19.75;
        state.hover_rock = frame % 3 ? -1 : 0;
        state.hover_ok = frame % 2 != 0;
        state.mood = frame % 2 ? zc::OperatorMood::cheering : zc::OperatorMood::focused;
        site.render(state, true);  // cache miss: keep the requested threading
        const std::vector<float> rgb = site.r.rgb;
        const std::vector<float> depth = site.r.depth;
        site.render(state, true);  // cache hit: quiet path
        transition_errors += rgb != site.r.rgb || depth != site.r.depth || !site.r.threads;
        site.render(state, false); // interaction path
        transition_errors += rgb != site.r.rgb || depth != site.r.depth || !site.r.threads;
    }
    std::printf("cache/resize/camera/hover/mood mismatched comparisons %d/16; save unchanged %d\n",
                transition_errors, saved == run.save());
    return mismatched == 0 && transition_errors == 0 && saved == run.save() ? 0 : 1;
}
