// Timing of a typical session: the pour, steady play with a rock held over a
// small stack (physics), lowering it into contact, and frames of the site at
// the app's scene size, with the camera moving (scenery redrawn) and still.
// bench [seed] [W H]
#include "platform/raster.hpp"
#include "run.hpp"
#include "site.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

double now_ms() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void report(const char* what, std::vector<double>& times) {
    std::sort(times.begin(), times.end());
    double sum = 0;
    for (size_t i = 0; i < times.size(); i += 1) {
        sum += times[i];
    }
    std::printf("%-34s mean %6.2f ms  median %6.2f  p95 %6.2f  max %6.2f  (%zu)\n", what, sum / static_cast<double>(times.size()),
                times[times.size() / 2], times[times.size() * 95 / 100], times.back(), times.size());
}

void step(zc::Run& run, int frames, const zc::CraneInput& input) {
    for (int f = 0; f < frames; f += 1) {
        run.step(input, 0, 0.5);
    }
}

int pick(const zc::Run& run) {
    int best = -1;
    double best_z = -1;
    for (size_t i = 0; i < run.rocks.size(); i += 1) {
        if (run.rocks[i].place == zc::Place::bowl && run.can_fetch(static_cast<int>(i))) {
            const double z = run.rock_pose(static_cast<int>(i)).p.z;
            if (z > best_z) {
                best_z = z;
                best = static_cast<int>(i);
            }
        }
    }
    return best;
}

// fetch a rock and bring it over (x, y); lower and let go if `place`
// Swings the held rock over to (x, y) the way a player would: a little at a
// time, at steering speed, then lets it settle there.
void bring_over(zc::Run& run, double x, double y) {
    const zc::CraneInput idle;
    for (int f = 0; f < 60 * 20; f += 1) {
        const double dx = x - run.crane.target.p.x;
        const double dy = y - run.crane.target.p.y;
        const double gap = std::sqrt(dx * dx + dy * dy);
        if (gap < 1e-4) {
            break;
        }
        const double stride = std::min(gap, 0.11 / 60.0);
        run.crane.target.p.x += dx / gap * stride;
        run.crane.target.p.y += dy / gap * stride;
        run.step(idle, 0, 0.6);
    }
    for (int f = 0; f < 150; f += 1) {
        run.step(idle, 0, 0.6);
    }
}

void carry(zc::Run& run, double x, double y, bool place) {
    const zc::CraneInput idle;
    const int rock = pick(run);
    if (rock < 0 || !run.fetch(rock)) {
        return;
    }
    for (int f = 0; f < 1200 && run.crane.mode != zc::CraneMode::steering; f += 1) {
        step(run, 1, idle);
    }
    run.crane.target.q = zc::flat_side_down(run.rocks[static_cast<size_t>(rock)].rock);
    bring_over(run, x, y);
    if (!place) {
        return;
    }
    zc::CraneInput down;
    down.up = -1;
    for (int f = 0; f < 1800 && run.world().hold_state().tension > 0.05; f += 1) {
        down.fine = run.world().hold_state().tension < 0.97;
        step(run, 1, down);
    }
    run.release();
    for (int f = 0; f < 1200; f += 1) {
        step(run, 1, idle);
        if (run.quiet() && run.crane.mode == zc::CraneMode::parked) {
            break;
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::uint32_t seed = argc > 1 ? static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 10)) : 7u;
    const int W = argc > 3 ? std::atoi(argv[2]) : 733;
    const int H = argc > 3 ? std::atoi(argv[3]) : 507;
    zc::Run run;
    double t0 = now_ms();
    run.begin(seed, "Bench");
    std::printf("%-34s %6.1f ms\n", "pour (50 rocks)", now_ms() - t0);
    const zc::phys::Vec3 spot = zc::site_layout().stack_centre;
    for (int k = 0; k < 4; k += 1) {
        const int base = run.base_rock();
        const zc::phys::Vec3 at = base >= 0 ? run.rock_pose(base).p : spot;
        carry(run, at.x, at.y, true);
    }
    std::printf("stack %d rocks, %.1f cm\n", run.stack_count(), run.height() * 100);
    // a rock held, swung about over the stack
    const int rock = pick(run);
    run.fetch(rock);
    const zc::CraneInput idle;
    for (int f = 0; f < 1500 && run.crane.mode != zc::CraneMode::steering; f += 1) {
        run.step(idle, 0, 0.5);
    }
    std::vector<double> held;
    for (int f = 0; f < 600; f += 1) {
        zc::CraneInput input;
        input.right = std::sin(f * 0.02);
        input.forward = 0.5 * std::cos(f * 0.013);
        input.yaw = f % 200 < 50 ? 1.0 : 0.0;
        const double a = now_ms();
        run.step(input, 0, 0.5);
        held.push_back(now_ms() - a);
    }
    report("physics: held rock, swinging", held);
    // lower it onto the stack until it rests
    {
        const int base = run.base_rock();
        const zc::phys::Vec3 at = base >= 0 ? run.rock_pose(base).p : spot;
        run.crane.target.p.x = at.x;
        run.crane.target.p.y = at.y;
    }
    for (int f = 0; f < 180; f += 1) {
        run.step(idle, 0, 0.5);
    }
    std::vector<double> lowering;
    zc::CraneInput down;
    down.up = -1;
    for (int f = 0; f < 900 && run.world().hold_state().tension > 0.05; f += 1) {
        down.fine = run.world().hold_state().tension < 0.97;
        const double a = now_ms();
        run.step(down, 0, 0.5);
        lowering.push_back(now_ms() - a);
    }
    if (!lowering.empty()) {
        report("physics: lowering into contact", lowering);
    }
    run.release();
    std::vector<double> settling;
    for (int f = 0; f < 300; f += 1) {
        const double a = now_ms();
        run.step(idle, 0, 0.5);
        settling.push_back(now_ms() - a);
    }
    report("physics: released, settling", settling);
    std::vector<double> quiet;
    for (int f = 0; f < 300; f += 1) {
        const double a = now_ms();
        run.step(idle, 0, 0.5);
        quiet.push_back(now_ms() - a);
    }
    report("physics: all quiet", quiet);
    // frames
    for (size_t i = 0; i < run.rocks.size(); i += 1) {
        if (run.rocks[i].place != zc::Place::bowl) {
            zc::ensure_near_mesh(run.rocks[i].rock);
        }
    }
    zc::Site site;
    site.resize(W, H);
    zc::SceneState state;
    state.run = &run;
    std::vector<double> moving;
    std::vector<double> still;
    for (int f = 0; f < 60; f += 1) {
        zc::OrbitCamera camera;
        camera.yaw = 0.01 * f;
        site.set_camera(camera);
        state.time = f / 60.0;
        const double a = now_ms();
        site.render(state, false);
        moving.push_back(now_ms() - a);
    }
    for (int f = 0; f < 60; f += 1) {
        state.time = 1 + f / 60.0;
        const double a = now_ms();
        site.render(state, true);
        still.push_back(now_ms() - a);
    }
    char label[80];
    std::snprintf(label, sizeof label, "render %dx%d, camera moving", W, H);
    report(label, moving);
    std::snprintf(label, sizeof label, "render %dx%d, camera still", W, H);
    report(label, still);
    std::printf("triangles a frame: %lld\n", site.triangles());
    // BENCH_LOOP=n: keep rendering (for a sampling profiler)
    const int loops = std::getenv("BENCH_LOOP") ? std::atoi(std::getenv("BENCH_LOOP")) : 0;
    for (int f = 0; f < loops; f += 1) {
        zc::OrbitCamera camera;
        camera.yaw = 0.01 * (f % 2);
        site.set_camera(camera);
        state.time = f / 60.0;
        site.render(state, false);
    }
    return 0;
}
