// Win rates of the whole crew in random three-handed matches, with their numbers: for tuning.
#include "brain.hpp"
#include <cstdio>
#include <algorithm>
using namespace ld;
int main(int argc, char** argv) {
    const int G = argc > 1 ? std::atoi(argv[1]) : 20000;
    const auto& c = cast();
    std::vector<int> wins(32), seats(32);
    for (int g = 0; g < G; ++g) {
        Rng rng(g * 7 + 1);
        std::vector<int> s;
        while (s.size() < 3) { int k = rng.range(32); if (std::find(s.begin(), s.end(), k) == s.end()) s.push_back(k); }
        Match m; m.start(3, rng.range(3), rng);
        Reading r; r.bluff.assign(3, kTypicalBluff);
        while (!m.over()) {
            const Decision d = decide(m, m.turn, c[static_cast<size_t>(s[static_cast<size_t>(m.turn)])], r, rng);
            if (d.call) { m.call(); if (!m.over()) m.roll(rng); } else m.place(d.bid);
        }
        for (int k : s) ++seats[static_cast<size_t>(k)];
        ++wins[static_cast<size_t>(s[static_cast<size_t>(m.winner())])];
    }
    std::vector<int> order(32); for (int i = 0; i < 32; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return wins[a] * 1.0 / seats[a] > wins[b] * 1.0 / seats[b]; });
    if (argc > 2) { for (int i = 0; i < 32; ++i) std::printf("    %.3f,  // %s\n", wins[i] * 1.0 / seats[i], c[i].name.c_str()); return 0; }
    for (int i : order) { const auto& ch = c[i]; std::printf("%-18s %.3f d%d bluff %.2f nerve %.2f greed %.2f skill %.2f adapt %.2f\n", ch.name.c_str(), wins[i] * 1.0 / seats[i], ch.danger, ch.bluff, ch.nerve, ch.greed, ch.skill, ch.adapt); }
}
