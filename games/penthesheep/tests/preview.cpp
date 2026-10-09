// Headless frames for look development:  preview out.ppm [level] [W H] [mode: play|sit|hint]
#include "pasture.hpp"
#include "platform/render.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace sh;
static void write_ppm(const char* path, const Canvas& c) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6 %d %d 255\n", c.w, c.h);
    for (int i = 0; i < c.w * c.h; ++i) { const unsigned char q[3] = {c.px[i * 4 + 2], c.px[i * 4 + 1], c.px[i * 4]}; std::fwrite(q, 1, 3, f); }
    std::fclose(f);
}
int main(int argc, char** argv) {
    const int lv = argc > 2 ? std::atoi(argv[2]) : 8;
    const int W = argc > 4 ? std::atoi(argv[3]) : 550, H = argc > 4 ? std::atoi(argv[4]) : 380;
    const char* mode = argc > 5 ? argv[5] : "play";
    Level l = generate(params_for(lv));
    Meadow m = l.start;
    for (size_t k = 0; k < l.solution.size() && k < 4; ++k) place(m, l.solution[k]);
    Pasture p;
    p.resize(W, H, 30, 60);
    p.frame(m.w);
    PastureState s;
    s.meadow = &m;
    s.sheep.pos = p.cell_pos(m.sheep);
    s.sheep.yaw = .6;
    s.sheep.chew = .5;
    if (!std::strcmp(mode, "sit")) { s.sheep.sit = 1; s.celebrate = 1; }
    if (!std::strcmp(mode, "hint")) { s.hint = bot_stone(m); s.hover = (m.sheep + 2) % (m.w * m.h); s.hover_ok = true; }
    p.render(s, 1.3);
    Canvas c;
    c.resize(W, H);
    p.r.present(c, 1, 0, 0, true);
    write_ppm(argv[1], c);
    std::printf("tris %lld\n", p.r.tris_drawn);
}
