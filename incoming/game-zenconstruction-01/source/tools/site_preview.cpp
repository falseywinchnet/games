// Headless frames of the whole worksite: site_preview out.ppm [seed] [W H] [yaw pitch distance]
// Pours the bowl, stacks three rocks with the crane, then holds a fourth over the stack.
#include "platform/raster.hpp"
#include "run.hpp"
#include "site.hpp"

#include <chrono>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

void write_ppm(const char* path, const zc::Canvas& c) {
    FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        return;
    }
    std::fprintf(f, "P6 %d %d 255\n", c.w, c.h);
    for (int i = 0; i < c.w * c.h; i += 1) {
        const unsigned char q[3] = {c.px[static_cast<size_t>(i) * 4 + 2], c.px[static_cast<size_t>(i) * 4 + 1], c.px[static_cast<size_t>(i) * 4]};
        std::fwrite(q, 1, 3, f);
    }
    std::fclose(f);
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
    const std::uint32_t seed = argc > 2 ? static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 10)) : 7u;
    const int W = argc > 4 ? std::atoi(argv[3]) : 640;
    const int H = argc > 4 ? std::atoi(argv[4]) : 420;
    zc::OrbitCamera camera;
    if (argc > 7) {
        camera.yaw = std::atof(argv[5]);
        camera.pitch = std::atof(argv[6]);
        camera.distance = std::atof(argv[7]);
    }
    if (std::getenv("ZC_TARGET")) {
        std::sscanf(std::getenv("ZC_TARGET"), "%lf,%lf,%lf", &camera.target.x, &camera.target.y, &camera.target.z);
    }
    zc::Run run;
    run.begin(seed, "Pebble & Sons");
    const zc::phys::Vec3 spot = zc::site_layout().stack_centre;
    for (int k = 0; k < 3; k += 1) {
        const int base = run.base_rock();
        const zc::phys::Vec3 at = base >= 0 ? run.rock_pose(base).p : spot;
        carry(run, at.x, at.y, true);
    }
    carry(run, spot.x - 0.05, spot.y - 0.08, false);
    for (size_t i = 0; i < run.rocks.size(); i += 1) {
        if (run.rocks[i].place != zc::Place::bowl) {
            zc::ensure_near_mesh(run.rocks[i].rock);
        }
    }
    std::printf("stack %d, height %.1f cm, crane mode %d\n", run.stack_count(), run.height() * 100, static_cast<int>(run.crane.mode));
    zc::Site site;
    if (std::getenv("ZC_FOCUS")) {
        // close-ups: the duck, Mina, or the hook and its load
        const std::string focus = std::getenv("ZC_FOCUS");
        const zc::M34& chassis = site.crane().setup().chassis;
        if (focus == "duck") {
            const zc::V3 p = chassis.apply(zc::V3{0.145, -0.036, 0.152});
            camera.target = zc::phys::Vec3{p.x, p.y, p.z};
        } else if (focus == "mina") {
            const zc::V3 pivot = chassis.apply(zc::V3{-0.06, 0, 0.17});
            const double slew = std::atan2(run.crane.hook.y - pivot.y, run.crane.hook.x - pivot.x);
            camera.target = zc::phys::Vec3{pivot.x + 0.021 * std::cos(slew) - 0.05 * std::sin(slew), pivot.y + 0.021 * std::sin(slew) + 0.05 * std::cos(slew), pivot.z};
        } else if (focus == "hook") {
            camera.target = run.crane.hook;
            camera.target.z -= 0.04;
        }
    }
    site.resize(W, H);
    site.set_camera(camera);
    zc::SceneState state;
    state.run = &run;
    state.time = 12.0;
    state.mood = zc::OperatorMood::focused;
    state.hover_rock = argc > 8 ? run.base_rock() : -1;
    state.hover_ok = true;
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    site.render(state, false);
    const std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
    site.render(state, false);
    const std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();
    if (run.crane.attached && run.crane.rock >= 0) {
        // where the held rock's long axis should point on screen
        const zc::phys::Pose pose = run.rock_pose(run.crane.rock);
        const zc::Rock& rock = run.rocks[static_cast<size_t>(run.crane.rock)].rock;
        int longest = 0;
        for (int k = 1; k < 3; k += 1) {
            if (rock.recipe.axes[k] > rock.recipe.axes[longest]) {
                longest = k;
            }
        }
        zc::phys::Vec3 axis{0, 0, 0};
        if (longest == 0) axis.x = 1; else if (longest == 1) axis.y = 1; else axis.z = 1;
        const zc::phys::Vec3 w = zc::phys::rotate(pose.q, axis);
        double ax0, ay0, ax1, ay1;
        site.project(zc::phys::Vec3{pose.p.x - w.x * 0.05, pose.p.y - w.y * 0.05, pose.p.z - w.z * 0.05}, ax0, ay0);
        site.project(zc::phys::Vec3{pose.p.x + w.x * 0.05, pose.p.y + w.y * 0.05, pose.p.z + w.z * 0.05}, ax1, ay1);
        std::printf("held rock long axis on screen: %.1f deg, centre %.0f %.0f\n", std::atan2(ay1 - ay0, ax1 - ax0) * 180 / 3.14159265, 0.5 * (ax0 + ax1), 0.5 * (ay0 + ay1));
        std::printf("axes %.3f %.3f %.3f\n", rock.recipe.axes[0], rock.recipe.axes[1], rock.recipe.axes[2]);
        if (std::getenv("ZC_POINTS")) {
            std::FILE* f = std::fopen(std::getenv("ZC_POINTS"), "w");
            const zc::M34 model = zc::pose_matrix(pose);
            const std::vector<zc::Vtx>& tris = rock.near_mesh.triangles.empty() ? rock.far_mesh.triangles : rock.near_mesh.triangles;
            for (size_t k = 0; k < tris.size(); k += 1) {
                const zc::V3 wp = model.apply(tris[k].p);
                double sx, sy;
                site.project(zc::phys::Vec3{wp.x, wp.y, wp.z}, sx, sy);
                std::fprintf(f, "%.2f %.2f\n", sx, sy);
            }
            std::fclose(f);
        }
    }
    zc::Canvas canvas;
    canvas.resize(W, H);
    site.r.present(canvas, 1, 0, 0, true);
    write_ppm(argv[1], canvas);
    std::printf("first frame %.1f ms (with scenery), next %.1f ms; %lld triangles\n",
                std::chrono::duration<double, std::milli>(t1 - t0).count(), std::chrono::duration<double, std::milli>(t2 - t1).count(), site.triangles());
    return 0;
}
