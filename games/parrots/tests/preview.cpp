// Headless frames for look development.
//   preview table out.ppm [N] [W H]
#include "parlor.hpp"
#include "platform/raster.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace pt;

static void write_ppm(const char* path, const Canvas& c) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6 %d %d 255\n", c.w, c.h);
    for (int i = 0; i < c.w * c.h; ++i) { const unsigned char q[3] = {c.px[i * 4 + 2], c.px[i * 4 + 1], c.px[i * 4]}; std::fwrite(q, 1, 3, f); }
    std::fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: preview table out.ppm [N] [W H] [prop]\n"); return 1; }
    const int n = argc > 3 ? std::atoi(argv[3]) : 7;
    const int W = argc > 5 ? std::atoi(argv[4]) : 550, H = argc > 5 ? std::atoi(argv[5]) : 380;
    Parlor p;
    p.resize(W, H, 96);
    ParlorState s;
    s.prop = argc > 6 ? argv[6] : "plate";
    p.seat(n, s);
    for (int i = 0; i < n; ++i) {
        BirdPose& b = s.birds[static_cast<size_t>(i)];
        b.species = (i * 3) % kSpecies;
        b.manner = static_cast<Manner>(i % kManners);
        b.mark = i % 3;
        b.beak = i == 2 ? .8 : 0;
        b.wing_r = i == 4 ? .8 : 0;
        b.head_tilt = (i % 2 ? .2 : -.15);
        b.head_turn = (i < n / 2 ? .55 : -.55);
        b.tight_beaked = i == 5;
    }
    p.render(s, 1.0);
    Canvas c;
    c.resize(W, H);
    p.r.present(c, 1, 0, 0, true);
    write_ppm(argv[2], c);
    std::printf("tris %lld\n", p.r.tris_drawn);
    return 0;
}
