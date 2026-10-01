// Headless frame preview: renders the real scene to PNG-able PPM files.
#include "scene.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace eggy;
static void save(const Canvas& c, const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    std::fprintf(f, "P6 %d %d 255\n", c.w, c.h);
    for (int i = 0; i < c.w * c.h; ++i) { unsigned char q[3] = {c.px[i * 4 + 2], c.px[i * 4 + 1], c.px[i * 4]}; std::fwrite(q, 1, 3, f); }
    std::fclose(f);
}
int main(int argc, char** argv) {
    const std::string out = argc > 1 ? argv[1] : ".";
    const double v = argc > 2 ? std::atof(argv[2]) : 40;
    const double hour = argc > 3 ? std::atof(argv[3]) : .35;
    const int seed = argc > 4 ? std::atoi(argv[4]) : 7;
    const int scale = argc > 5 ? std::atoi(argv[5]) : 3;
    const bool storm = argc > 6 && std::atoi(argv[6]);
    Sim s(seed);
    s.place_at(v);
    s.day_offset = hour;
    Scene sc;
    const int W = 1180, H = 800;
    sc.resize(W / scale, H / scale);
    double t = 0;
    for (int i = 0; i < 240; ++i) {  // let Eggy walk for 8 seconds on autopilot
        if (storm) { s.storm = s.storm_target = 1; s.storm_left = 100; }
        s.step(1.0 / 30);
        for (const Event& e : s.events) sc.on_event(e, s);
        s.events.clear();
        sc.render(s, t, 1.0 / 30);
        t += 1.0 / 30;
    }
    Canvas c; c.resize(W, H);
    sc.r.tris_drawn = 0;
    if (storm) sc.on_event(Event{Ev::lightning, 0, .1}, s);
    sc.render(s, t, 1.0 / 30);
    sc.r.present(c, scale, 0, 0, true);
    std::printf("biome %s tris %lld v %.1f\n", biome_name(s.world.row((long long)s.d.v).biome), sc.r.tris_drawn, s.d.v);
    save(c, out);
}
