#include "platform/render.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace {
void setup(zc::R3D& r) {
    r.resize(960, 600);
    r.scale = 310;
    r.yaw = .62; r.pitch = .53; r.persp = 2.7; r.height_scale = 1.17;
    r.target = zc::V3{.1, -.2, .07};
    r.set_camera();
    for (int y = 0; y < r.H; y += 1) {
        for (int x = 0; x < r.W; x += 1) r.depth[static_cast<std::size_t>(y) * r.W + x] = (y < 30) ? 1e30f : -.4f + .001f * x + .0003f * y;
    }
}
void caster(zc::R3D& r, double x, double y, double z, double extent) {
    zc::Vtx v[6];
    v[0].p = zc::V3{x - extent, y - extent, z};
    v[1].p = zc::V3{x + extent, y - extent, z};
    v[2].p = zc::V3{x + extent, y + extent, z};
    v[3] = v[0]; v[4] = v[2];
    v[5].p = zc::V3{x - extent, y + extent, z};
    r.shadow_cast(v, 6, nullptr);
}
void base(zc::R3D& r) {
    r.shadow_begin(zc::V3{0, 0, 0}, 1.2, 1024);
    caster(r, -.1, .1, .2, .28);
    r.shadow_save();
}
void shade(zc::R3D& r) {
    std::fill(r.rgb.begin(), r.rgb.end(), 1.f);
    r.shadow_screen_pass(.55f);
}
}
int main(int argc, char** argv) {
    zc::R3D kept, rebuilt;
    setup(kept); setup(rebuilt); base(kept);
    double error = 0;
    for (int f = 0; f < 18; f += 1) {
        kept.shadow_restore();
        base(rebuilt);
        // Move across the map boundary and eventually remove the caster.
        if (f < 16) {
            const double x = -1.7 + f * .23;
            caster(kept, x, -.15, .65, .14);
            caster(rebuilt, x, -.15, .65, .14);
        }
        shade(kept); shade(rebuilt);
        for (std::size_t i = 0; i < kept.rgb.size(); i += 1) error = std::max(error, static_cast<double>(std::abs(kept.rgb[i] - rebuilt.rgb[i])));
    }
    std::printf("%s moving, departing and clipped caster restore: max pixel error %.9g\n", error == 0 ? "PASS" : "FAIL", error);
    // Both projection modes and an empty map remain well defined.
    kept.persp = 0;
    kept.shadow_begin(zc::V3{0, 0, 0}, 1.2, 128);
    shade(kept);
    bool empty_lit = true;
    for (float value : kept.rgb) empty_lit = empty_lit && value == 1.f;
    std::printf("%s empty orthographic map is fully lit\n", empty_lit ? "PASS" : "FAIL");
    if (argc > 1) {
        setup(kept); base(kept);
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        for (int f = 0; f < 180; f += 1) {
            kept.shadow_restore();
            caster(kept, -.2 + .003 * f, -.15, .65, .14);
            shade(kept);
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 180;
        std::printf("shadow restore/cast/filter: %.4f ms/frame at 960x600\n", ms);
        if (argc > 2) {
            std::FILE* file = std::fopen(argv[2], "wb");
            if (file == nullptr) return 2;
            std::fwrite(kept.rgb.data(), sizeof(float), kept.rgb.size(), file);
            std::fclose(file);
        }
    }
    return error == 0 && empty_lit ? 0 : 1;
}
