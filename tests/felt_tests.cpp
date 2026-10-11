// The felt drawn with several threads is the felt drawn on one, pixel for pixel, and a
// cancelled render still stops.
#include "carpet.hpp"

#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

struct Crew {
    int threads;
};
struct Round {
    void (*job)(void*, int) noexcept;
    void* context;
    int count;
    std::atomic<int> next{0};
};
void take(Round* round) {
    Round& r = *round;
    for (int i = r.next.fetch_add(1); i < r.count; i = r.next.fetch_add(1))
        r.job(r.context, i);
}
void run(void* runner, int count, void (*job)(void*, int) noexcept, void* context) {
    const Crew& crew = *static_cast<const Crew*>(runner);
    Round round{job, context, count};
    std::vector<std::thread> helpers;
    for (int t = 1; t < crew.threads; ++t)
        helpers.emplace_back(take, &round);
    take(&round);
    for (std::thread& helper : helpers)
        helper.join();
}

int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}
bool same(const paint::Image& a, const paint::Image& b) {
    if (a.width != b.width || a.height != b.height || a.pixels.size() != b.pixels.size())
        return false;
    for (std::size_t i = 0; i < a.pixels.size(); ++i)
        if (!paint::equal(a.pixels[i], b.pixels[i]))
            return false;
    return true;
}

}  // namespace

int main() {
    for (int preset : {3, 0, 4}) {
        const paint::CarpetParameters p = paint::carpet_preset(preset);
        const paint::Image alone = paint::render_carpet_tile(p, 48);
        for (int threads : {2, 3, 8}) {
            Crew crew{threads};
            const paint::Parallel parallel{run, &crew, threads};
            check(same(alone, paint::render_carpet_tile(p, 48, nullptr, &parallel)),
                  "a tile drawn with helpers matches the tile drawn alone");
        }
        Crew crew{4};
        const paint::Parallel parallel{run, &crew, 4};
        check(same(paint::render_carpet(p, 40, 28), paint::render_carpet(p, 40, 28, nullptr, &parallel)),
              "an oblong carpet drawn with helpers matches it drawn alone");
    }
    std::atomic<bool> cancel{true};
    Crew crew{4};
    const paint::Parallel parallel{run, &crew, 4};
    bool stopped = false;
    try {
        static_cast<void>(paint::render_carpet_tile(paint::carpet_preset(3), 48, &cancel, &parallel));
    } catch (const std::runtime_error&) {
        stopped = true;
    }
    check(stopped, "a cancelled render stops");
    if (failures == 0)
        std::printf("felt: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
