// Rules tests: hand-worked Black Box cases, an independently written tracer
// compared on every arrangement of four atoms and every port, the fairness of
// generated boxes, scoring and save state.
#include "box.hpp"
#include "save.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>

using namespace ap;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static Atoms at(std::initializer_list<std::pair<int, int>> cells) {
    Atoms a = 0;
    for (auto [x, y] : cells) a |= 1ULL << (y * kN + x);
    return a;
}

// A second tracer, written from the rule text without sharing code: a padded
// 10x10 board, compass directions, and "look at the three squares in front".
namespace ref {
const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};  // N E S W
int run(Atoms atoms, int p) {
    bool b[10][10] = {};
    for (int c = 0; c < 64; ++c)
        if (atoms >> c & 1) b[c / 8 + 1][c % 8 + 1] = true;
    int x, y, d;  // padded coordinates
    if (p < 8) { x = p + 1; y = 0; d = 2; }
    else if (p < 16) { x = 9; y = p - 8 + 1; d = 3; }
    else if (p < 24) { x = 23 - p + 1; y = 9; d = 0; }
    else { x = 0; y = 31 - p + 1; d = 1; }
    const int sx = x, sy = y;
    bool entered = false;
    for (int guard = 0; guard < 1000; ++guard) {
        const int fx = x + DX[d], fy = y + DY[d];
        if (fx < 0 || fx > 9 || fy < 0 || fy > 9) return -2;  // should not happen
        if (b[fy][fx]) return 32;
        const int l = (d + 3) % 4, r = (d + 1) % 4;
        const bool L = b[fy + DY[l]][fx + DX[l]], R = b[fy + DY[r]][fx + DX[r]];
        if ((L || R) && !entered) return 33;
        if (L && R) { d = (d + 2) % 4; continue; }
        if (L) { d = r; continue; }
        if (R) { d = l; continue; }
        x = fx;
        y = fy;
        entered = true;
        if (x == 0 || x == 9 || y == 0 || y == 9) {
            if (x == sx && y == sy) return 33;
            if (y == 0) return x - 1;
            if (x == 9) return 8 + y - 1;
            if (y == 9) return 23 - (x - 1);
            return 31 - (y - 1);
        }
    }
    return -1;
}
}  // namespace ref

int main() {
    // --- hand-worked cases
    {
        const Atoms a = at({{3, 3}});
        CHECK(trace(a, 3).kind == Outcome::hit);                       // straight into it
        const Trace t = trace(a, 2);                                   // passes it diagonally: turned away, out the left
        CHECK(t.kind == Outcome::exit && t.exit == 29);
        CHECK(t.path.size() == 3 && t.path[1].x == 2 && t.path[1].y == 2);
        CHECK(trace(a, 4).kind == Outcome::exit && trace(a, 4).exit == 10);  // the mirror image: out the right at row 2
    }
    {
        const Atoms a = at({{1, 0}});
        CHECK(trace(a, 0).kind == Outcome::reflect);  // an atom beside the entry square
        CHECK(trace(a, 2).kind == Outcome::reflect);
        CHECK(trace(a, 1).kind == Outcome::hit);
    }
    {
        const Atoms a = at({{2, 3}, {4, 3}});
        CHECK(trace(a, 3).kind == Outcome::reflect);  // two diagonal atoms send it back the way it came
    }
    {
        const Atoms a = 0;
        for (int p = 0; p < kPorts; ++p) {
            const Trace t = trace(a, p);
            CHECK(t.kind == Outcome::exit);
            CHECK(trace(a, t.exit).exit == p);  // straight through and back
        }
    }

    // --- the independent tracer on every arrangement and every port
    {
        const auto t0 = std::chrono::steady_clock::now();
        long long n = 0, bad = 0;
        for (int a = 0; a < 64; ++a)
            for (int b = a + 1; b < 64; ++b)
                for (int c = b + 1; c < 64; ++c)
                    for (int d = c + 1; d < 64; ++d) {
                        const Atoms at4 = (1ULL << a) | (1ULL << b) | (1ULL << c) | (1ULL << d);
                        const auto sig = signature(at4);
                        for (int p = 0; p < kPorts; ++p) {
                            ++n;
                            if (ref::run(at4, p) != sig[static_cast<size_t>(p)]) {
                                if (bad++ < 5) std::printf("mismatch atoms=%016llx port=%d ours=%d ref=%d\n", static_cast<unsigned long long>(at4), p,
                                                           sig[static_cast<size_t>(p)], ref::run(at4, p));
                            }
                            // the path-recording tracer agrees too (sampled)
                            if ((n & 31) == 0) {
                                const Trace t = trace(at4, p);
                                const int code = t.kind == Outcome::hit ? 32 : t.kind == Outcome::reflect ? 33 : t.exit;
                                if (code != sig[static_cast<size_t>(p)]) ++bad;
                                // its path starts at the port and ends outside (or on the atom that absorbed it)
                                const Pt e = t.path.back();
                                const int ex = static_cast<int>(e.x), ey = static_cast<int>(e.y);
                                if (t.kind == Outcome::hit ? !has(at4, ex, ey) : (t.kind == Outcome::exit && port_at(ex, ey) != t.exit)) ++bad;
                            }
                            // reversibility: a detour run backwards comes out where it went in
                            if (sig[static_cast<size_t>(p)] < 32 && sig[sig[static_cast<size_t>(p)]] != p) ++bad;
                        }
                    }
        CHECK(bad == 0);
        std::printf("tracer: %lld probes compared, %lld mismatches (%.1fs)\n", n, bad,
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    }

    // --- fairness
    {
        const auto t0 = std::chrono::steady_clock::now();
        const bool any = deducible(at({{0, 0}, {7, 7}, {3, 4}, {5, 1}}));
        static_cast<void>(any);
        std::printf("deducibility table built in %.2fs\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        long long unique = 0, total = 0;
        for (int a = 0; a < 64; ++a)
            for (int b = a + 1; b < 64; ++b)
                for (int c = b + 1; c < 64; ++c)
                    for (int d = c + 1; d < 64; d += 7) {
                        ++total;
                        unique += deducible((1ULL << a) | (1ULL << b) | (1ULL << c) | (1ULL << d));
                    }
        std::printf("deducible: %lld of %lld sampled arrangements (%.1f%%)\n", unique, total, 100.0 * unique / total);
        Box box(1234);
        for (int i = 0; i < 50; ++i) {
            box.new_box();
            CHECK(std::popcount(box.atoms()) == kAtoms);
            CHECK(deducible(box.atoms()));
            // brute force: no other arrangement matches all 32 reports
            if (i < 3) {
                const auto s = signature(box.atoms());
                int same = 0;
                for (int a = 0; a < 64; ++a)
                    for (int b = a + 1; b < 64; ++b)
                        for (int c = b + 1; c < 64; ++c)
                            for (int d = c + 1; d < 64; ++d)
                                same += signature((1ULL << a) | (1ULL << b) | (1ULL << c) | (1ULL << d)) == s;
                CHECK(same == 1);
            }
        }
    }

    // --- scoring, free repeats, marking, opening
    {
        Box box(7);
        box.set_atoms(at({{3, 3}, {6, 6}, {0, 7}, {7, 0}}));
        bool fresh = false;
        const Probe p1 = box.fire(2, fresh);
        CHECK(fresh && p1.kind == Outcome::exit && p1.pair == 1);
        box.fire(p1.exit, fresh);
        CHECK(!fresh);                     // the far end of a known detour costs nothing
        box.fire(2, fresh);
        CHECK(!fresh);
        CHECK(box.points() == 2);
        box.fire(3, fresh);
        CHECK(fresh && box.probes().back().kind == Outcome::hit && box.points() == 3);
        CHECK(box.toggle_mark(27) && box.toggle_mark(54) && box.toggle_mark(56) && box.toggle_mark(1));
        CHECK(!box.toggle_mark(9));        // only four markers
        CHECK(box.can_open());
        box.open();
        CHECK(box.opened() && box.found() == 3 && box.missed() == 1 && box.total() == 3 + 5);
        CHECK(!box.toggle_mark(1));        // nothing changes once it is open
        // save state round trip
        Box b2(99);
        CHECK(b2.restore(box.state()));
        CHECK(b2.points() == box.points() && b2.found() == box.found() && b2.opened() && b2.probes().size() == box.probes().size());
        BoxState bad = box.state();
        bad.atoms |= 1;  // five atoms
        CHECK(!b2.restore(bad));
        bad = box.state();
        bad.fired.push_back(2);  // a repeat would never have been recorded
        CHECK(!b2.restore(bad));
    }

    // --- the save file: round trip, tamper detection, score order
    {
        const std::filesystem::path p = std::filesystem::temp_directory_path() / "atom_probe_test-v2.txt";
        Box box(5);
        box.new_box();
        bool fresh;
        box.fire(0, fresh);
        box.fire(17, fresh);
        box.toggle_mark(9);
        box.toggle_empty(10);
        SaveData d;
        d.box = box.state();
        d.has_box = true;
        d.solved = 3;
        d.settings.player_name = "ADA";
        add_score(d.scores, {"B", 14, 1});
        add_score(d.scores, {"A", 9, 2});
        CHECK(d.scores.front().points == 9);
        CHECK(write_save(p, d));
        SaveData e;
        CHECK(load_save(p, e));
        Box b2(1);
        CHECK(e.has_box && b2.restore(e.box) && b2.atoms() == box.atoms() && b2.points() == box.points() && b2.marked(9) && b2.empty_mark(10));
        CHECK(e.solved == 3 && e.settings.player_name == "ADA" && e.scores.size() == 2 && e.scores[0].name == "A");
        {
            std::string all;
            { std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); all = ss.str(); }
            all.replace(all.find("solved=3"), 8, "solved=9");
            std::ofstream f(p, std::ios::trunc); f << all;
        }
        SaveData bad;
        CHECK(!load_save(p, bad));  // the checksum catches an edited file
        std::filesystem::remove(p);
    }

    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("all box tests passed\n");
    return 0;
}
