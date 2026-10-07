#include "flora.hpp"
#include "sim.hpp"
#include <cstdio>
#include <cmath>
#include <map>
using namespace eggy;
int main() {
    int failures = 0;
    // 1. determinism + random access
    { World a(42), b(42); for (std::int64_t v : {5LL, 1000LL, 123456789LL, 777LL}) for (int u = 0; u < kWidth; ++u)
        if (a.tile(u, v).feature != b.tile(u, v).feature || a.tile(u, v).z[2] != b.tile(u, v).z[2]) { std::printf("nondeterministic\n"); ++failures; } }
    // 2. autopilot keeps climbing everywhere
    std::map<int, double> rate_by_biome; std::map<int, int> n_by_biome;
    for (std::uint64_t seed : {1ULL, 2ULL, 3ULL}) {
        Sim probe(seed);
        double worst = 1e9;
        for (int trial = 0; trial < 40; ++trial) {
            Sim s(seed);
            double v0 = 50 + trial * (s.world.length() / 41.0);
            if (trial < 6) v0 = 50 + trial * 900;
            s.place_at(v0);
            s.since_input = 1e9;
            double start = s.d.v;
            int b = (int)s.world.row((std::int64_t)s.d.v).biome;
            for (int i = 0; i < 180 * 30; ++i) { s.step(1.0 / 30); s.events.clear(); }
            double rate = (s.d.v - start) / 180.0;
            rate_by_biome[b] += rate; n_by_biome[b]++;
            worst = std::min(worst, rate);
            if (rate < 0.25) { std::printf("seed %llu trial %d v=%.0f biome %s slow rate %.3f\n", (unsigned long long)seed, trial, start, biome_name((Biome)b), rate); ++failures; }
        }
        std::printf("seed %llu length %lld stars %zu worst auto rate %.3f rows/s\n", (unsigned long long)seed, (long long)probe.world.length(), probe.world.stars().size(), worst);
    }
    for (auto& [b, r] : rate_by_biome) std::printf("  %-14s auto %.3f rows/s (n=%d)\n", biome_name((Biome)b), r / n_by_biome[b], n_by_biome[b]);
    // 3. player-speed estimate: hold uphill
    { Sim s(9); s.place_at(500); double t = 0, start = s.d.v;
      for (int i = 0; i < 600 * 30; ++i) { s.in.iv = 1; s.in.iu = 0; s.step(1.0 / 30); s.events.clear(); t += 1.0/30; }
      std::printf("holding up for 600s: %.3f rows/s (naive straight line)\n", (s.d.v - start) / t); }
    { // player via mouse-target toward far uphill lane: realistic helper
      Sim s(9); s.place_at(500); double start = s.d.v;
      for (int i = 0; i < 600 * 30; ++i) { s.in.has_target = true; s.in.tu = s.world.lane(s.d.v + 12); s.in.tv = s.d.v + 12; s.step(1.0 / 30); s.events.clear(); }
      double r = (s.d.v - start) / 600; std::printf("helper (target ahead) 600s: %.3f rows/s -> %.1f years for length %lld\n", r, s.world.length() / r / 3.156e7, (long long)s.world.length()); }
    // 4. trees: deterministic, suited to the climb, and never twins side by side
    {
        const Flora& f = flora();
        if (static_cast<int>(f.tree_models.size()) != kTreeKinds * kTreeForms) { std::printf("tree model count\n"); ++failures; }
        for (const TreeModel& m : f.tree_models)
            if (m.wood.empty() || m.height < .3 || m.height > 2.4 || m.crown > 1.0) { std::printf("tree model out of bounds\n"); ++failures; }
        int twins = 0, pairs = 0;
        for (int bi = 0; bi < static_cast<int>(Biome::count); ++bi)
            for (int site = 0; site < 3; ++site) {
                const Biome b = static_cast<Biome>(bi);
                TreeLook prev = tree_look(b, static_cast<TreeSite>(site), -2.5, 0, 1);
                for (int k = 1; k < 400; ++k) {
                    const double u = -2.5 - (k % 3), v = k / 3;
                    const unsigned h = static_cast<unsigned>(mix64(static_cast<std::uint64_t>(k) * 977 + static_cast<std::uint64_t>(bi * 7 + site)) >> 20);
                    const TreeLook a = tree_look(b, static_cast<TreeSite>(site), u, v, h), again = tree_look(b, static_cast<TreeSite>(site), u, v, h);
                    if (a.kind != again.kind || a.form != again.form || a.yaw != again.yaw || a.size != again.size) { std::printf("tree look not deterministic\n"); ++failures; }
                    const bool high = b == Biome::snow || b == Biome::ice || b == Biome::ridge || b == Biome::summit;
                    if (high && static_cast<int>(a.kind) < static_cast<int>(TreeKind::spruce)) { std::printf("broadleaf above the treeline in %s\n", biome_name(b)); ++failures; }
                    if (site == static_cast<int>(TreeSite::conifer) && static_cast<int>(a.kind) < static_cast<int>(TreeKind::spruce)) { std::printf("broadleaf on a conifer site\n"); ++failures; }
                    ++pairs;
                    if (a.kind == prev.kind && a.form == prev.form && std::fabs(a.size - prev.size) < .02 && std::fabs(a.yaw - prev.yaw) < .2) ++twins;
                    prev = a;
                }
            }
        if (twins * 200 > pairs) { std::printf("too many identical neighbouring trees: %d of %d\n", twins, pairs); ++failures; }
        std::printf("trees: %d species x %d forms, %d near-identical neighbours in %d\n", kTreeKinds, kTreeForms, twins, pairs);
    }
    std::printf("%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
