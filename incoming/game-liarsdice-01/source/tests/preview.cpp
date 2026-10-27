// Headless frames for look development.
//   preview table out.ppm [opponents] [W H] [first-cast-index] [reveal]
//   preview lineup out.ppm [W H] [first]   eight of the crew in a row, for checking the models
#include "cabin.hpp"
#include "platform/raster.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace ld;

static void write_ppm(const char* path, const Canvas& c) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6 %d %d 255\n", c.w, c.h);
    for (int i = 0; i < c.w * c.h; ++i) { const unsigned char q[3] = {c.px[i * 4 + 2], c.px[i * 4 + 1], c.px[i * 4]}; std::fwrite(q, 1, 3, f); }
    std::fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: preview table|lineup out.ppm ...\n"); return 1; }
    const bool lineup = std::strcmp(argv[1], "lineup") == 0;
    const int n = lineup ? 3 : (argc > 3 ? std::atoi(argv[3]) : 3);
    const int W = argc > 5 ? std::atoi(argv[4]) : 550, H = argc > 5 ? std::atoi(argv[5]) : 380;
    const int first = argc > 6 ? std::atoi(argv[6]) : 0;
    const bool reveal = argc > 7 && std::strcmp(argv[7], "tell") != 0;
    const bool tell = argc > 7 && std::strcmp(argv[7], "tell") == 0;
    Cabin cab;
    cab.resize(W, H, 96);
    CabinState s;
    cab.seat(n, s);
    for (int i = 1; i <= n; ++i) {
        CrewPose& p = s.crew[static_cast<size_t>(i)];
        p.who = (first + (i - 1) * 9) % 32;
        p.mouth = i == 2 ? .8 : 0;
        p.head_turn = i == 1 ? -.3 : i == 3 ? .3 : 0;
        if (tell) { p.mouth = 0; apply_tell(p, cast()[static_cast<size_t>(p.who)].tell, .7, 1.03); }
        else p.mouth = 0;
    }
    for (int seat = 0; seat <= n; ++seat) {
        s.dice[static_cast<size_t>(seat)].resize(5);
        for (int k = 0; k < 5; ++k) s.dice[static_cast<size_t>(seat)][static_cast<size_t>(k)].value = 1 + (seat * 3 + k * 2) % 6;
        s.cups[static_cast<size_t>(seat)].lift = seat == 0 ? 1 : reveal ? 1 : 0;
        if (reveal) for (auto& d : s.dice[static_cast<size_t>(seat)]) d.glow = d.value == 4 || d.value == 1;
    }
    s.jar = 4;
    cab.render(s, 1.0);
    Canvas c;
    c.resize(W, H);
    cab.r.present(c, 1, 0, 0, true);
    write_ppm(argv[2], c);
    std::printf("tris %lld\n", cab.r.tris_drawn);
    return 0;
}
