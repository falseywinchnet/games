// Headless frames for look development: writes PPM images.
//   preview view out.ppm LEVEL [steps] [W H]   first-person, after walking the solution `steps` squares
#include "maze.hpp"
#include "soft3d.hpp"
#include "textures.hpp"
#include "world.hpp"
#include "rewards.hpp"
#include "cast.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace mz;

static void write_ppm(const char* path, const Soft3D& r) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6 %d %d 255\n", r.W, r.H);
    for (std::uint32_t c : r.color) { const unsigned char q[3] = {static_cast<unsigned char>(c >> 16), static_cast<unsigned char>(c >> 8), static_cast<unsigned char>(c)}; std::fwrite(q, 1, 3, f); }
    std::fclose(f);
}

static void sheet(const char* path, int count, const Tex32& (*get)(int), int tw, int th) {
    const int cols = 8, rows = (count + cols - 1) / cols;
    Soft3D r;
    r.resize(cols * tw, rows * th);
    std::fill(r.color.begin(), r.color.end(), 0xFF6A8AA0u);
    for (int i = 0; i < count; ++i) {
        const Tex32& t = get(i);
        for (int y = 0; y < th; ++y)
            for (int x = 0; x < tw; ++x) {
                const std::uint32_t c = t.at(x * t.w / tw, y * t.h / th);
                if ((c >> 24) < 128) continue;
                r.color[static_cast<size_t>((i / cols * th + y) * r.W + i % cols * tw + x)] = c | 0xFF000000u;
            }
    }
    write_ppm(path, r);
}

int main(int argc, char** argv) {
    if (argc >= 3 && !std::strcmp(argv[1], "rewards")) { sheet(argv[2], reward_count(), &reward_tex, 64, 64); return 0; }
    if (argc >= 3 && !std::strcmp(argv[1], "cast")) { sheet(argv[2], cast_count(), &cast_tex, 128, 128); return 0; }
    if (argc >= 3 && !std::strcmp(argv[1], "posters")) { sheet(argv[2], poster_count(), &tex_poster, 64, 64); return 0; }
    if (argc < 4) { std::fprintf(stderr, "usage: preview view out.ppm LEVEL [yawdeg] [W H] [blackout] [roll]\n"); return 1; }
    const int n = std::atoi(argv[3]);
    const double yawdeg = argc > 4 ? std::atof(argv[4]) : -1;
    const int W = argc > 6 ? std::atoi(argv[5]) : 366, H = argc > 6 ? std::atoi(argv[6]) : 240;
    const auto t0 = std::chrono::steady_clock::now();
    Level lv = generate(params_for(n, 1234 + n));
    std::printf("level %d: %dx%d floors %zu doors %d steps %d (gen %.0f ms)\n", n, lv.floors[0].w, lv.floors[0].h, lv.floors.size(), lv.params.doors, lv.optimal_steps,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    Soft3D r;
    r.resize(W, H);
    r.eye = cell_center(lv.start, .5);
    r.yaw = (yawdeg >= 0 ? yawdeg : lv.start_dir * 90.0) * M_PI / 180;
    r.blackout = argc > 7 && std::atoi(argv[7]);
    r.roll = argc > 8 ? std::atof(argv[8]) : 0;
    r.fog_rgb = 0x000000;
    WorldState s;
    s.door_open.assign(8, 0);
    s.door_open[0] = .5;
    static Tex32 cheese;
    cheese.make(4, 4);
    s.reward = &cheese;
    const auto t1 = std::chrono::steady_clock::now();
    for (int k = 0; k < 20; ++k) { r.begin(0x000000); draw_world(r, lv, s, 1.0); }
    std::printf("render %.2f ms/frame, %lld tris\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count() / 20, r.tris);
    write_ppm(argv[2], r);
    // and the map, as text
    const Floor& fl = lv.floors[0];
    for (int y = fl.h - 1; y >= 0; --y) {
        for (int x = 0; x < fl.w; ++x) {
            const Cell& c = fl.at(x, y);
            char ch = c.block == Block::wall ? '#' : c.block == Block::door ? 'D' : ' ';
            if (c.pad >= 0) ch = 'p';
            if (c.cpad >= 0) ch = 'c';
            if (c.portal >= 0) ch = 'O';
            if (c.elevator) ch = 'E';
            if (c.goal) ch = 'g';
            if (lv.thing_at({0, x, y}) >= 0) ch = lv.things[static_cast<size_t>(lv.thing_at({0, x, y}))].kind == ThingKind::flip ? 'F' : 'b';
            if (Pos{0, x, y} == lv.start) ch = 'S';
            if (Pos{0, x, y} == lv.goal) ch = 'G';
            std::putchar(ch);
        }
        std::putchar('\n');
    }
    return 0;
}
