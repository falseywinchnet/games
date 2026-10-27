// Headless stills for look development: preview out.ppm [seed] [W H]
// A gallery of one run's first twelve rocks on a plain ground.
#include "platform/render.hpp"
#include "rocks.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

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

zc::M34 pose_matrix(const zc::phys::Pose& pose) {
    const zc::phys::Quat q = pose.q;
    zc::M34 m;
    m.m[0] = 1 - 2 * (q.y * q.y + q.z * q.z);
    m.m[1] = 2 * (q.x * q.y - q.w * q.z);
    m.m[2] = 2 * (q.x * q.z + q.w * q.y);
    m.m[3] = pose.p.x;
    m.m[4] = 2 * (q.x * q.y + q.w * q.z);
    m.m[5] = 1 - 2 * (q.x * q.x + q.z * q.z);
    m.m[6] = 2 * (q.y * q.z - q.w * q.x);
    m.m[7] = pose.p.y;
    m.m[8] = 2 * (q.x * q.z - q.w * q.y);
    m.m[9] = 2 * (q.y * q.z + q.w * q.x);
    m.m[10] = 1 - 2 * (q.x * q.x + q.y * q.y);
    m.m[11] = pose.p.z;
    return m;
}

}  // namespace

int main(int argc, char** argv) {
    const std::uint32_t seed = argc > 2 ? static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 10)) : 1u;
    const int W = argc > 4 ? std::atoi(argv[3]) : 640;
    const int H = argc > 4 ? std::atoi(argv[4]) : 400;
    zc::R3D r;
    r.resize(W, H);
    r.yaw = 0.35;
    r.pitch = 0.62;
    r.persp = 2.2;
    r.scale = W / 1.15 * (std::getenv("ZOOM") ? std::atof(std::getenv("ZOOM")) : 1.0);
    r.ax = 0.5;
    r.ay = 0.52;
    r.target = zc::V3{0.0, 0.0, 0.04};
    if (std::getenv("AT")) {
        std::sscanf(std::getenv("AT"), "%lf,%lf", &r.target.x, &r.target.y);
    }
    r.light.sun = zc::norm(zc::V3{-0.45, -0.3, 0.84});
    r.light.sun_col = zc::Col{0.86f, 0.8f, 0.68f, 1};
    r.light.amb_col = zc::Col{0.42f, 0.46f, 0.52f, 1};
    r.light.fog_col = zc::Col{0.8f, 0.86f, 0.9f, 1};
    r.light.fog_near = 6;
    r.light.fog_far = 20;
    r.light.hemisphere = true;
    r.light.amb_col = zc::Col{0.5f, 0.56f, 0.66f, 1};
    r.light.amb_ground = zc::Col{0.52f, 0.45f, 0.36f, 1};
    r.light.sun_col = zc::Col{0.86f, 0.8f, 0.68f, 1};
    r.set_camera();
    r.clear_depth();
    for (size_t i = 0; i < r.rgb.size(); i += 3) {
        r.rgb[i] = 0.78f;
        r.rgb[i + 1] = 0.84f;
        r.rgb[i + 2] = 0.9f;
    }
    // the rocks, each resting on its lowest point (good enough for a gallery)
    std::vector<zc::Rock> rocks;
    std::vector<zc::M34> models;
    for (int i = 0; i < 12; i += 1) {
        rocks.push_back(zc::make_rock(seed, i));
        zc::Rock& rock = rocks.back();
        zc::ensure_near_mesh(rock);
        const double x = -0.33 + 0.22 * (i % 4);
        const double y = 0.22 - 0.22 * (i / 4);
        zc::phys::Pose pose;
        pose.q = zc::flat_side_down(rock);
        double low = 1e9;
        const std::vector<zc::phys::Vec3>& hv = rock.shape.hulls[0].vertices;
        for (size_t k = 0; k < hv.size(); k += 1) {
            low = std::min(low, zc::phys::rotate(pose.q, hv[k]).z);
        }
        pose.p = zc::phys::Vec3{x, y, -low};
        models.push_back(pose_matrix(pose));
        std::printf("%2d %s, %s, %.1f cm\n", i, zc::rock_class_name(rock.recipe.kind), zc::stone_name(rock.recipe.stone), rock.diameter * 100);
    }
    r.shadow_begin(zc::V3{0, 0, 0}, 0.7, 1024);
    for (size_t i = 0; i < rocks.size(); i += 1) {
        r.shadow_cast(rocks[i].near_mesh.triangles.data(), rocks[i].near_mesh.triangles.size(), &models[i]);
    }
    r.shadow_use(true);
    // the ground, in small tiles so its affine texture doesn't warp under perspective
    {
        std::vector<zc::Vtx> tiles;
        const double e = 1.6;
        const int n = 16;
        const double step = 2 * e / n;
        for (int j = 0; j < n; j += 1) {
            for (int i = 0; i < n; i += 1) {
                const double xa = -e + i * step;
                const double ya = -e + j * step;
                const double corners[6][2] = {{xa, ya}, {xa + step, ya + step}, {xa + step, ya}, {xa, ya}, {xa, ya + step}, {xa + step, ya + step}};
                for (int k = 0; k < 6; k += 1) {
                    zc::Vtx v;
                    v.p = zc::V3{corners[k][0], corners[k][1], 0};
                    v.n = zc::V3{0, 0, 1};
                    v.c = zc::Col{0.74f, 0.66f, 0.52f, 1};
                    v.s = corners[k][0] * 20;
                    v.t = corners[k][1] * 20;
                    tiles.push_back(v);
                }
            }
        }
        r.draw(tiles.data(), tiles.size(), &zc::stone_grain(), static_cast<std::uint16_t>(zc::double_sided));
    }
    for (size_t i = 0; i < rocks.size(); i += 1) {
        r.draw(rocks[i].near_mesh.triangles.data(), rocks[i].near_mesh.triangles.size(), &zc::rock_texture(rocks[i]), zc::opaque, &models[i]);
    }
    zc::Canvas c;
    c.resize(W, H);
    r.present(c, 1, 0, 0, true);
    write_ppm(argv[1], c);
    std::printf("tris %lld\n", r.tris_drawn);
    return 0;
}
