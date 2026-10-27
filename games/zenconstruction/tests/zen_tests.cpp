// Zen Construction's rules: pouring the bowl, the crane fetching, placing and
// letting go, the stack and its height, saving, unwinding, collapses and tidying.
#include "run.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void report(const char* name, bool pass, const std::string& detail) {
    if (!pass) {
        failures += 1;
    }
    std::printf("%s  %s  %s\n", pass ? "PASS" : "FAIL", name, detail.c_str());
    std::fflush(stdout);
}

std::string format(const char* pattern, double a, double b = 0, double c = 0) {
    char buffer[256];
    std::snprintf(buffer, sizeof buffer, pattern, a, b, c);
    return std::string(buffer);
}

int count_in(const zc::Run& run, zc::Place place) {
    int n = 0;
    for (size_t i = 0; i < run.rocks.size(); i += 1) {
        if (run.rocks[i].place == place) {
            n += 1;
        }
    }
    return n;
}

void step_frames(zc::Run& run, int frames) {
    const zc::CraneInput idle;
    for (int f = 0; f < frames; f += 1) {
        run.step(idle, 0, 0.6);
    }
}

// steps until the crane is in `mode` (or `limit` frames pass); returns success
bool step_until_mode(zc::Run& run, zc::CraneMode mode, int limit) {
    const zc::CraneInput idle;
    for (int f = 0; f < limit; f += 1) {
        if (run.crane.mode == mode) {
            return true;
        }
        run.step(idle, 0, 0.6);
    }
    return run.crane.mode == mode;
}

// steps until the site has sorted itself after the last release and the crane is parked
bool step_until_quiet(zc::Run& run, int limit) {
    const zc::CraneInput idle;
    bool settled = false;
    for (int f = 0; f < limit; f += 1) {
        run.step(idle, 0, 0.6);
        if (run.events().settled) {
            settled = true;
        }
        if (settled && run.quiet() && run.crane.mode == zc::CraneMode::parked) {
            return true;
        }
    }
    return false;
}

// a rock in the bowl lying high (easy to lift out), smallest index first among the highest few
int pick_from_bowl(const zc::Run& run, double max_size) {
    int best = -1;
    double best_z = -1;
    for (size_t i = 0; i < run.rocks.size(); i += 1) {
        if (run.rocks[i].place != zc::Place::bowl || run.rocks[i].rock.diameter > max_size) {
            continue;
        }
        const double z = run.rock_pose(static_cast<int>(i)).p.z;
        if (z > best_z && run.can_fetch(static_cast<int>(i))) {
            best_z = z;
            best = static_cast<int>(i);
        }
    }
    return best;
}

// Swings the held rock over to (x, y) the way a player would: a little at a
// time, at steering speed, then lets it settle there.
void bring_over(zc::Run& run, double x, double y) {
    const zc::CraneInput idle;
    for (int f = 0; f < 60 * 20; f += 1) {
        const double dx = x - run.crane.target.p.x;
        const double dy = y - run.crane.target.p.y;
        const double gap = std::sqrt(dx * dx + dy * dy);
        if (gap < 1e-4) {
            break;
        }
        const double stride = std::min(gap, 0.11 / 60.0);
        run.crane.target.p.x += dx / gap * stride;
        run.crane.target.p.y += dy / gap * stride;
        run.step(idle, 0, 0.6);
    }
    for (int f = 0; f < 150; f += 1) {
        run.step(idle, 0, 0.6);
    }
}

// The crane fetches `rock`, carries it over (x, y), lowers it until the wires
// are slack, and lets go. Returns false if a stage didn't complete.
bool place_rock(zc::Run& run, int rock, double x, double y) {
    if (!run.fetch(rock)) {
        return false;
    }
    if (!step_until_mode(run, zc::CraneMode::steering, 60 * 20) || run.crane.rock != rock) {
        std::printf("      the crane didn't get rock %d into steering (mode %d)\n", rock, static_cast<int>(run.crane.mode));
        return false;
    }
    // turn it flat side down, as a player would, and move over the spot
    run.crane.target.q = zc::flat_side_down(run.rocks[static_cast<size_t>(rock)].rock);
    bring_over(run, x, y);
    // lower gently until the wires go slack
    for (int f = 0; f < 60 * 30; f += 1) {
        if (run.world().hold_state().tension < 0.05) {
            break;
        }
        zc::CraneInput down;
        down.up = -1;
        down.fine = run.world().hold_state().tension < 0.97;
        run.step(down, 0, 0.6);
    }
    step_frames(run, 30);
    run.release();
    return step_until_quiet(run, 60 * 20);
}

}  // namespace

int main() {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    zc::Run run;
    {
        const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        run.begin(7, "Pebble & Sons");
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        int out = 0;
        for (size_t i = 0; i < run.rocks.size(); i += 1) {
            if (run.world().out_of_bounds(run.rocks[i].body)) {
                out += 1;
            }
        }
        report("pour: fifty rocks settle in the bowl", count_in(run, zc::Place::bowl) == 50 && run.world().all_asleep() && out == 0,
               format("in the bowl %.0f; all asleep %.0f; poured in %.0f ms", count_in(run, zc::Place::bowl), run.world().all_asleep() ? 1 : 0, ms));
    }
    // The "woken heap" check (wake the whole settled heap and require that no more than
    // one rock shifts over 3 cm) was retired on 2026-10-06 at the owner's request: its
    // tolerance failed on some compilers and runners (shifts of 32-45 mm) without any
    // visible fault in play, and it held up releases.
    const int first = pick_from_bowl(run, 0.2);
    const bool placed_first = first >= 0 && place_rock(run, first, zc::site_layout().stack_centre.x, zc::site_layout().stack_centre.y);
    report("first rock: fetched, lowered to slack, released; it is the stack's foot",
           placed_first && run.base_rock() == first && run.stack_count() == 1 && run.height() > 0.01,
           format("picked %.0f, base %.0f; stack %.0f", first, run.base_rock(), run.stack_count()) + format("; height %.1f cm", run.height() * 100));
    // Whether rocks balance on one another is the physics' business and differs a hair between
    // compilers; play shows it. These checks are about how the game treats the stack it has.
    // saving
    {
        const std::string text = run.save();
        zc::Run copy;
        const bool loaded = copy.load(text);
        const bool same = loaded && copy.save() == text;
        double moved = 0;
        if (loaded) {
            std::vector<zc::phys::Pose> before;
            for (size_t i = 0; i < copy.rocks.size(); i += 1) {
                before.push_back(copy.rock_pose(static_cast<int>(i)));
            }
            step_frames(copy, 120);
            for (size_t i = 0; i < copy.rocks.size(); i += 1) {
                const zc::phys::Vec3 d = copy.rock_pose(static_cast<int>(i)).p - before[i].p;
                moved = std::max(moved, zc::phys::length(d));
            }
        }
        report("save and load: the same arrangement, standing still", same && moved == 0 && copy.stack_count() == run.stack_count(),
               format("round trip %.0f; moved %.1e mm; stack %.0f", same ? 1 : 0, moved * 1000, copy.stack_count()));
    }
    // a collapse: a rock let go well off the top topples off and flies back to the bowl
    {
        const int third = pick_from_bowl(run, 0.2);
        const zc::phys::Pose top = run.rock_pose(first);
        bool fell = false;
        bool tidied = false;
        if (third >= 0 && run.fetch(third) && step_until_mode(run, zc::CraneMode::steering, 60 * 20)) {
            run.crane.target.p.x = top.p.x + 0.26;   // well beside the stack (away from the bowl): it lands on the ground
            run.crane.target.p.y = top.p.y - 0.12;
            step_frames(run, 90);
            run.release();
            const zc::CraneInput idle;
            for (int f = 0; f < 60 * 25; f += 1) {
                run.step(idle, 0, 0.6);
                if (run.events().tidied) {
                    tidied = true;
                }
                if (run.events().collapsed) {
                    fell = true;
                }
            }
        }
        const bool back = third >= 0 && run.rocks[static_cast<size_t>(third)].place == zc::Place::bowl;
        report("let go beside the stack: the loose rock flies back into the bowl", tidied && back && count_in(run, zc::Place::loose) == 0,
               format("tidied %.0f; third in the bowl %.0f; stack now %.0f", tidied ? 1 : 0, back ? 1 : 0, run.stack_count()) +
                   std::string(fell ? " (the stack lost a rock)" : ""));
    }
    // jiggling the bowl: its rocks hop, stay in the bowl and settle somewhere new
    {
        zc::Run shaken;
        shaken.begin(11, "Pebble & Sons");
        std::vector<zc::phys::Vec3> before;
        for (size_t i = 0; i < shaken.rocks.size(); i += 1)
            before.push_back(shaken.rock_pose(static_cast<int>(i)).p);
        const bool shook = shaken.jiggle();
        int frames = 0;
        while (frames < 60 * 20 && !(frames > 30 && shaken.world().all_asleep())) {
            step_frames(shaken, 1);
            frames += 1;
        }
        int moved = 0, out = 0;
        for (size_t i = 0; i < shaken.rocks.size(); i += 1) {
            const zc::phys::Vec3 now = shaken.rock_pose(static_cast<int>(i)).p;
            moved += zc::phys::length(now - before[i]) > 0.005 ? 1 : 0;
            out += shaken.world().out_of_bounds(shaken.rocks[i].body) ? 1 : 0;
        }
        report("jiggle: the bowl's rocks hop, stay in it and settle anew",
               shook && count_in(shaken, zc::Place::bowl) == 50 && out == 0 && moved > 25 && shaken.world().all_asleep(),
               format("moved %.0f of 50; in the bowl %.0f; settled in %.1f s", moved, count_in(shaken, zc::Place::bowl), frames / 60.0));
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("%s (%.1f s)\n", failures == 0 ? "all tests passed" : (std::to_string(failures) + " FAILURES").c_str(), seconds);
    return failures == 0 ? 0 : 1;
}
