// The campaign's curve, measured: for each level, the sheep, the rocks, the bot's par, and how long generation took.
#include "field.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
using namespace sh;
int main(int argc, char** argv) {
    const int n = argc > 1 ? std::atoi(argv[1]) : 40;
    for (int lv = 1; lv <= n; ++lv) {
        const auto t0 = std::chrono::steady_clock::now();
        const LevelParams p = params_for(lv);
        const Level l = generate(p);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        int rocks = 0;
        for (Cell c : l.start.cells) rocks += c == Cell::rock;
        std::printf("level %2d  %2dx%-2d %-8s rocks %2d clovers %d  par %2d  %.0f ms\n", lv, p.size, p.size, smarts_name(p.smarts), rocks, p.clovers, l.par, ms);
    }
}
