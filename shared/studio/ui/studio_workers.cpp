// The studio's render crew in the shell: GUI.Forms Workers, started for one render and
// joined at its end (studio_crew.hpp). With them a music loop's tracks render on every core.
#include "studio_cues.hpp"

#include "gui_forms/threading.hpp"

#include <algorithm>
#include <memory>
#include <thread>
#include <vector>

namespace studio {
namespace {

struct Helpers {
    void (*entry)(void*) = nullptr;
    void* context = nullptr;
    std::vector<std::unique_ptr<gui_forms::Worker>> workers{};  // after what they use
};

void helper(const gui_forms::CancellationFlag&, void* helpers) {
    Helpers& h = *static_cast<Helpers*>(helpers);
    h.entry(h.context);
}

void* start(int count, void (*entry)(void*), void* context) {
    std::unique_ptr<Helpers> h = std::make_unique<Helpers>();
    (*h).entry = entry;
    (*h).context = context;
    try {
        for (int i = 0; i < count; ++i) {
            (*h).workers.push_back(std::make_unique<gui_forms::Worker>(helper, h.get()));
        }
    } catch (...) {
        // fewer helpers than hoped: the render's own thread and those started do the work
    }
    return h.release();
}

void join(void* helpers) {
    std::unique_ptr<Helpers> h(static_cast<Helpers*>(helpers));
    for (std::unique_ptr<gui_forms::Worker>& worker : (*h).workers) {
        (*worker).join();
    }
}

}  // namespace

Crew worker_crew() {
    const unsigned cores = std::max(1U, std::thread::hardware_concurrency());
    Crew crew{};
    crew.start = start;
    crew.join = join;
    crew.helpers = static_cast<int>(std::min(cores, 8U)) - 1;
    return crew;
}

void register_studio_cues() {
    set_render_crew(worker_crew());
    add_studio_cues();
}

}  // namespace studio
