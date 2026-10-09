// Times the shared 3D renderer on a scene like the games': a textured ground, toon-lit
// characters with outlines, plain props, translucent glows, then the dithered present.
//   r3d_bench [frames]
// Prints milliseconds per frame for each stage and a checksum of the picture, so a
// change can be checked to draw the same pixels in less time.
#include "r2d_canvas.hpp"
#include "r3d_mesh.hpp"
#include "r3d_renderer.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

using namespace render::r3d;
using render::r2d::Canvas;
using render::r2d::Col;

double since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

M34 place(double x, double y, double z, double s) {
    return M34::translate(x, y, z) * M34::scale(s, s, s);
}

}  // namespace

int main(int argc, char** argv) {
    const int frames = argc > 1 ? std::max(1, std::atoi(argv[1])) : 200;
    Renderer r;
    r.resize(550, 360);
    r.scale = 30;
    r.persp = 14;
    r.set_camera();
    Tex grass;
    grass.make(64, 64);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const std::uint32_t g = 120 + static_cast<std::uint32_t>((x * 7 + y * 13) % 60);
            grass.at(x, y) = 0xFF000000U | (40U << 16) | (g << 8) | 30U;
        }
    }
    grass.build_mips();
    Mesh ground;
    for (int gy = -6; gy < 6; ++gy) {
        for (int gx = -8; gx < 8; ++gx) {
            const double ax = gx, ay = gy, bx = gx + 1, by = gy + 1;
            const V3 n{0, 0, 1};
            ground.push_back({{ax, ay, 0}, n, ax, ay});
            ground.push_back({{bx, ay, 0}, n, bx, ay});
            ground.push_back({{bx, by, 0}, n, bx, by});
            ground.push_back({{ax, ay, 0}, n, ax, ay});
            ground.push_back({{bx, by, 0}, n, bx, by});
            ground.push_back({{ax, by, 0}, n, ax, by});
        }
    }
    Canvas out;
    out.resize(550, 360);
    double t_clear = 0, t_ground = 0, t_props = 0, t_glow = 0, t_present = 0;
    for (int f = 0; f < frames; ++f) {
        std::chrono::steady_clock::time_point a = std::chrono::steady_clock::now();
        std::fill(r.rgb.begin(), r.rgb.end(), .5f);
        r.clear_depth();
        t_clear += since(a);
        a = std::chrono::steady_clock::now();
        r.draw(ground.data(), ground.size(), &grass, opaque);
        t_ground += since(a);
        a = std::chrono::steady_clock::now();
        for (int k = 0; k < 12; ++k) {
            const double x = -5 + k, y = (k % 3) - 1;
            const M34 body = place(x, y, .5, .45);
            draw_mesh(r, sphere_mesh(14, 10), body, nullptr, Col{.9f, .9f, .85f, 1}, toon);
            draw_outline(r, sphere_mesh(14, 10), body, .03, Col{.1f, .1f, .1f, 1});
            draw_mesh(r, cylinder_mesh(10), place(x + .3, y - .6, 0, .2), nullptr, Col{.5f, .35f, .2f, 1}, opaque);
            draw_mesh(r, box_mesh(), place(x - .3, y + .6, 0, .25), nullptr, Col{.6f, .6f, .65f, 1}, opaque);
        }
        t_props += since(a);
        a = std::chrono::steady_clock::now();
        for (int k = 0; k < 20; ++k) {
            r.billboard(V3{-6 + k * .6, .5, .8}, .6, .6, nullptr, Col{1, .9f, .5f, .5f},
                        static_cast<std::uint16_t>(additive | unlit | no_depth_write));
        }
        t_glow += since(a);
        a = std::chrono::steady_clock::now();
        r.present(out, 1, 0, 0, true);
        t_present += since(a);
    }
    std::uint64_t sum = 1469598103934665603ULL;
    for (std::uint8_t byte : out.px) {
        sum = (sum ^ byte) * 1099511628211ULL;
    }
    const double n = frames;
    std::printf("per frame: clear %.3f  ground %.3f  props %.3f  glows %.3f  present %.3f  total %.3f ms\n",
                t_clear / n, t_ground / n, t_props / n, t_glow / n, t_present / n,
                (t_clear + t_ground + t_props + t_glow + t_present) / n);
    std::printf("picture %016llx\n", static_cast<unsigned long long>(sum));
    return 0;
}
