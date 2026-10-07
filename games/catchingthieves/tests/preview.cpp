// Headless frame renders for look development: writes PPM images.
//   preview cast out.ppm      the bear and the raccoons in a row of poses and expressions
#include "critters.hpp"
#include "garden.hpp"
#include "levelset.hpp"

#include <fstream>
#include <filesystem>
#include <sstream>
#include "platform/mesh.hpp"
#include "platform/raster.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace ct;

static void write_ppm(const char* path, const Canvas& c) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6 %d %d 255\n", c.w, c.h);
    for (int i = 0; i < c.w * c.h; ++i) {
        const unsigned char q[3] = {c.px[i * 4 + 2], c.px[i * 4 + 1], c.px[i * 4]};
        std::fwrite(q, 1, 3, f);
    }
    std::fclose(f);
}

static int garden(const char* out, int index, double t, int W, int H) {
    std::ifstream f(std::filesystem::path(__FILE__).parent_path().parent_path() / "assets/levels/campaign.txt");
    std::stringstream ss;
    ss << f.rdbuf();
    std::vector<LevelEntry> levels;
    std::string err;
    if (!load_levels(ss.str(), levels, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    const LevelEntry& e = levels[static_cast<size_t>(std::clamp(index, 0, static_cast<int>(levels.size()) - 1))];
    Garden g;
    g.set_level(e.level);
    g.resize(W, H, 120);
    GardenState s;
    s.season = season_for(e.section);
    const Level& lv = e.level;
    for (int b : lv.boxes) {
        PumpkinView pv;
        pv.pos = g.cell_pos(b);
        pv.seed = b;
        pv.on_burrow = lv.goal[static_cast<size_t>(b)];
        s.pumpkins.push_back(pv);
    }
    int k = 0;
    for (int i = 0; i < lv.w * lv.h; ++i) {
        if (!lv.goal[static_cast<size_t>(i)]) continue;
        CoonPose c;
        c.pos = g.cell_pos(i);
        c.rise = k % 3 == 0 ? 1.6 : k % 3 == 1 ? 1.0 : .55;
        c.prop = k % 3 == 0 ? 2 : k % 3 == 1 ? 1 : 0;
        c.prop_t = t;
        c.tail = t * 3;
        c.face.eyes = k % 2 ? CEyes::sly : CEyes::laugh;
        c.face.mouth = k % 2 ? CMouth::smirk : CMouth::laugh;
        s.coons.push_back(c);
        s.trapped.push_back(0);
        s.paw_wiggle.push_back(t * 5);
        ++k;
    }
    s.bear.pos = g.cell_pos(lv.player);
    s.bear.face.mouth = BMouth::smile;
    g.render(s, t);
    if (std::getenv("BENCH")) {
        const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 30; ++i) g.render(s, t + i * .033);
        std::printf("render %.2f ms/frame\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 30);
    }
    Canvas c;
    c.resize(W, H);
    g.r.present(c, 1, 0, 0, true);
    write_ppm(out, c);
    std::printf("%s (%s) %dx%d tris %lld\n", e.level.title.c_str(), e.section.c_str(), lv.w, lv.h, g.r.tris_drawn);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: preview cast out.ppm | preview garden out.ppm INDEX [t]\n"); return 1; }
    if (!std::strcmp(argv[1], "garden")) return garden(argv[2], argc > 3 ? std::atoi(argv[3]) : 0, argc > 4 ? std::atof(argv[4]) : 1.0, argc > 5 ? std::atoi(argv[5]) : 550, argc > 6 ? std::atoi(argv[6]) : 380);
    const int W = 640, H = 300;
    const double zoom = argc > 4 ? std::atof(argv[4]) : 1.0;
    R3D r;
    r.resize(W, H);
    r.yaw = 0;
    r.pitch = .8;
    r.persp = 0;
    r.scale = 70 * zoom;
    r.ax = .5;
    r.ay = .62;
    r.target = {0, 0, .4};
    r.light.sun = norm({-.5, -.6, .75});
    r.light.sun_col = {.6f, .58f, .52f, 1};
    r.light.amb_col = {.55f, .55f, .6f, 1};
    r.light.fog_near = 1e9;
    r.light.fog_far = 2e9;
    r.light.toon_edge = .12f;
    r.light.toon_soft = .1f;
    r.set_camera();
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) { float* o = &r.rgb[(static_cast<size_t>(y) * W + x) * 3]; o[0] = .62f; o[1] = .74f; o[2] = .46f; }
    r.clear_depth();
    // ground
    Vtx g[6] = {{{-6, -3, 0}, {0, 0, 1}, 0, 0, hex(0x8FB85A)}, {{6, -3, 0}, {0, 0, 1}, 0, 0, hex(0x8FB85A)}, {{6, 3, 0}, {0, 0, 1}, 0, 0, hex(0x8FB85A)},
                {{-6, -3, 0}, {0, 0, 1}, 0, 0, hex(0x8FB85A)}, {{6, 3, 0}, {0, 0, 1}, 0, 0, hex(0x8FB85A)}, {{-6, 3, 0}, {0, 0, 1}, 0, 0, hex(0x8FB85A)}};
    r.draw(g, 6, nullptr, unlit);
    const double t = argc > 3 ? std::atof(argv[3]) : 1.0;
    // the bear: standing, walking right, pushing up, facepalm
    BearPose b;
    b.pos = {-3.6, 0, 0};
    draw_bear(r, b, t);
    b = BearPose{}; b.pos = {-2.4, 0, 0}; b.yaw = M_PI / 2; b.walk = 1.2; b.face.mouth = BMouth::grin; b.face.eyes = BEyes::happy;
    draw_bear(r, b, t);
    b = BearPose{}; b.pos = {-1.2, 0, 0}; b.yaw = M_PI; b.lean = .3; b.left.hand = {-.15, -.38, .5}; b.right.hand = {.15, -.38, .5}; b.face.mouth = BMouth::effort; b.face.eyes = BEyes::shut_tight;
    draw_bear(r, b, t);
    b = BearPose{}; b.pos = {0, 0, 0}; b.facepalm = true; b.right.hand = {.08, -.3, .82}; b.face.mouth = BMouth::frown; b.face.eyes = BEyes::closed; b.head_nod = .2;
    draw_bear(r, b, t);
    // raccoons: peeking, shoulders out with a carrot, standing laughing, juggling, white flag
    const CEyes eyes[] = {CEyes::sly, CEyes::open, CEyes::laugh, CEyes::wide, CEyes::teary};
    const CMouth mouths[] = {CMouth::smirk, CMouth::raspberry, CMouth::laugh, CMouth::grin, CMouth::wobble};
    const double rises[] = {.55, 1.0, 1.6, 1.6, 1.6};
    const int props[] = {0, 1, 0, 2, 3};
    for (int i = 0; i < 5; ++i) {
        CoonPose c;
        c.pos = {1.1 + i * 1.0, 0, 0};
        c.rise = rises[i];
        c.prop = props[i];
        c.prop_t = t;
        c.tail = t * 3;
        c.face.eyes = eyes[i];
        c.face.mouth = mouths[i];
        if (i == 2) { c.left.hand = {-.15, -.15, -.1}; c.right.hand = {.2, -.2, .35}; }
        // a burrow under each
        Mesh d = disc_mesh(20);
        draw_mesh(r, d, M34::translate(c.pos.x, c.pos.y, .005) * M34::scale(.36, .3, 1), nullptr, hex(0x2A1A10), unlit);
        draw_mesh(r, torus_mesh(20, 6, .25), M34::translate(c.pos.x, c.pos.y, .0) * M34::scale(.4, .34, .3), nullptr, hex(0x8A5A34), toon);
        draw_raccoon(r, c, t);
    }
    Canvas out;
    out.resize(W, H);
    r.present(out, 1, 0, 0, true);
    write_ppm(argv[2], out);
    return 0;
}
