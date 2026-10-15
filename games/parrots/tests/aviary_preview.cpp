// Headless frames of the aviary for look development.
//   aviary_preview out.ppm [look] [seed] [birds] [W H]
#include "aviary.hpp"
#include "perching.hpp"
#include "platform/render.hpp"

#include <cstdio>
#include <chrono>
#include <cmath>
#include <cstdlib>

using namespace pt;

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

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: aviary_preview out.ppm [look] [seed] [birds] [W H]\n");
        return 1;
    }
    const int look = argc > 2 ? std::atoi(argv[2]) : 0;
    const std::uint64_t seed = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 1;
    const int n = argc > 4 ? std::atoi(argv[4]) : 8;
    const int W = argc > 6 ? std::atoi(argv[5]) : 550, H = argc > 6 ? std::atoi(argv[6]) : 360;
    const Layout L = make_layout(seed);
    std::vector<Bird> birds(static_cast<size_t>(n));
    std::vector<int> species(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        species[static_cast<size_t>(i)] = static_cast<int>((seed * 7 + static_cast<std::uint64_t>(i) * 3) % kSpecies);
        birds[static_cast<size_t>(i)].red = species[static_cast<size_t>(i)] == 0;
    }
    const std::vector<int> at = settle(L, birds, seed);
    Aviary a;
    a.resize(W, H, 22, 0);
    a.set(L, look, seed);
    AviaryState s;
    for (int i = 0; i < n; ++i) {
        BirdPose b;
        const Place& p = L.places[static_cast<size_t>(at[static_cast<size_t>(i)])];
        b.pos = p.pos;
        b.species = species[static_cast<size_t>(i)];
        b.manner = static_cast<Manner>(i % kManners);
        const V3 e = Aviary::eye();
        b.yaw = std::atan2(e.x - p.pos.x, -(e.y - p.pos.y)) * .8;
        b.size = kBirdSize;
        b.head_turn = (i % 3 - 1) * .3;
        b.head_tilt = i % 2 ? .15 : -.1;
        s.birds.push_back(b);
        s.shaded.push_back(p.shade);
        std::printf("bird %d species %s at place %d (%s%s%s, %s):", i, species_name(b.species), at[static_cast<size_t>(i)], stuff_name(p.stuff),
                    p.shade ? ", shade" : "", L.high(at[static_cast<size_t>(i)]) ? ", high" : L.low(at[static_cast<size_t>(i)]) ? ", low" : "",
                    content(L, birds, at, i) ? "content" : "UNHAPPY");
        for (Liking k : birds[static_cast<size_t>(i)].likes) std::printf(" [%s]", liking_name(k));
        std::printf("\n");
    }
    const auto t0 = std::chrono::steady_clock::now();
    a.render(s, 1.0);
    const auto t1 = std::chrono::steady_clock::now();
    a.resize(W, H, 22, 0);  // a new size: the kept scene drawn again
    a.render(s, 1.0);
    const auto t2 = std::chrono::steady_clock::now();
    a.render(s, 1.1);       // a frame: the still put back, the birds drawn
    const auto t3 = std::chrono::steady_clock::now();
    auto ms = [](auto a0, auto b0) { return std::chrono::duration<double, std::milli>(b0 - a0).count(); };
    std::printf("first %.0f ms, new size %.0f ms, frame %.1f ms\n", ms(t0, t1), ms(t1, t2), ms(t2, t3));
    // a frame where one bird turns its head is drawn in part; it must match a frame drawn whole
    {
        s.birds[0].head_turn += .4;
        s.birds[n - 1].blink = 1;
        const auto p0 = std::chrono::steady_clock::now();
        const bool drew = a.render(s, 1.1);
        const auto p1 = std::chrono::steady_clock::now();
        const bool again = a.render(s, 1.1);
        Aviary fresh;
        fresh.resize(W, H, 22, 0);
        fresh.set(L, look, seed);
        fresh.render(s, 1.1);
        size_t differ = 0;
        for (size_t i = 0; i < a.r.rgb.size(); ++i) differ += a.r.rgb[i] != fresh.r.rgb[i];
        std::printf("partial frame %.2f ms (drawn %d, unchanged frame drawn %d): %zu values differ from a whole frame\n", ms(p0, p1), drew, again, differ);
    }
    Canvas c;
    c.resize(W, H);
    a.r.present(c, 1, 0, 0, true);
    write_ppm(argv[1], c);
    std::printf("%s: places %zu, tris %lld\n", aviary_look(look).name, L.places.size(), a.r.tris_drawn);
    return 0;
}
