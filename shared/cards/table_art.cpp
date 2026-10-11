#include "table_art.hpp"
#include "card_backs.hpp"
#include "carpet.hpp"
#include "suite_settings.hpp"

#include "gui_forms/threading.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace games {
namespace gf = gui_forms;
namespace {

// A few heavy jobs at a time, lent to shared/felt (which builds without GUI.Forms) as a
// paint::Parallel. GUI.Forms' AtomicThreadPool runs a batch this small on the caller
// alone (it wants over 64 tasks per thread; see docs/TOOLKIT_REQUESTS.md), so each
// batch is shared by GUI.Forms Workers and the caller, all taking jobs from one count.
struct Crew {
    int threads;
};
struct Round {
    void (*job)(void*, int) noexcept;
    void* context;
    int count;
    std::atomic<int> next{0};
};
void take_jobs(Round& round) noexcept {
    for (int i = round.next.fetch_add(1); i < round.count; i = round.next.fetch_add(1))
        round.job(round.context, i);
}
void crew_member(const gf::CancellationFlag&, void* context) {
    take_jobs(*static_cast<Round*>(context));
}
int crew_size() {
    return static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1U, 8U));
}
void crew_run(void* runner, int count, void (*job)(void*, int) noexcept, void* context) {
    const Crew& crew = *static_cast<const Crew*>(runner);
    Round round{job, context, count};
    std::vector<std::unique_ptr<gf::Worker>> helpers;
    const int wanted = std::min(crew.threads, count) - 1;
    try {
        for (int i = 0; i < wanted; ++i)
            helpers.push_back(std::make_unique<gf::Worker>(crew_member, &round));
    } catch (...) {
        // fewer helpers than hoped: the caller and those started do the work
    }
    take_jobs(round);
    for (std::unique_ptr<gf::Worker>& helper : helpers)
        (*helper).join();
}

struct Kept {
    std::mutex lock;
    std::vector<std::byte> felt;
    std::array<std::vector<std::byte>, card_back_count> backs;
};
Kept& kept() {
    static Kept art;
    return art;
}

struct BackJob {
    std::array<std::vector<std::byte>, card_back_count>* backs;
};
void draw_back(void* context, int index) noexcept {
    BackJob& job = *static_cast<BackJob*>(context);
    std::vector<std::byte>& back = (*job.backs)[static_cast<std::size_t>(painted_back_count + index)];
    if (!back.empty())
        return;
    try {
        back = make_card_back(painted_back_count + index, card_art_width, card_art_height);
    } catch (...) {
        back.clear();  // drawn alone later, where a failure can be reported
    }
}

}  // namespace

const std::vector<std::byte>& felt_tile() {
    Kept& art = kept();
    const std::lock_guard<std::mutex> guard(art.lock);
    if (art.felt.empty()) {
        Crew crew{crew_size()};
        const paint::Parallel parallel{crew_run, &crew, crew.threads};
        const paint::Image tile =
            paint::render_carpet_tile(paint::carpet_preset(3), felt_tile_side, nullptr, &parallel);
        std::vector<std::byte> pixels(tile.pixels.size() * 4);
        for (std::size_t i = 0; i < tile.pixels.size(); ++i) {
            pixels[4 * i] = static_cast<std::byte>(tile.pixels[i].b);
            pixels[4 * i + 1] = static_cast<std::byte>(tile.pixels[i].g);
            pixels[4 * i + 2] = static_cast<std::byte>(tile.pixels[i].r);
            pixels[4 * i + 3] = std::byte{255};
        }
        art.felt = std::move(pixels);
    }
    return art.felt;
}

const std::vector<std::byte>& drawn_card_back(int design) {
    Kept& art = kept();
    const std::lock_guard<std::mutex> guard(art.lock);
    const int d = std::clamp(design, painted_back_count, classic_back - 1);
    std::vector<std::byte>& back = art.backs[static_cast<std::size_t>(d)];
    if (back.empty())
        back = make_card_back(d, card_art_width, card_art_height);
    return back;
}

void draw_card_backs() {
    Kept& art = kept();
    const std::lock_guard<std::mutex> guard(art.lock);
    BackJob job{&art.backs};
    Crew crew{crew_size()};
    crew_run(&crew, drawn_back_count, draw_back, &job);
}

}  // namespace games
