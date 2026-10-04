#include "collection.hpp"
#include "gui_forms/application.hpp"
#include "runtime_paths.hpp"
#include <charconv>
#include <iostream>
#include <stdexcept>
#include <string_view>
struct SmokeState final {
    gui_forms::FrameRequestToken timer{};
    bool completed{};
    bool profiling{};
    bool sampling{};
    gui_forms::FrameTime sample_start{};
    int entry{-1};
};
struct SmokeClose final {
    std::weak_ptr<SmokeState> state;
    gui_forms::ApplicationWindowHandle handle;
    void operator()(gui_forms::FrameTime) const {
        std::shared_ptr<SmokeState> live = state.lock();
        if (live) {
            (*live).completed = true;
            static_cast<void>(handle.request_close());
        }
    }
};
struct ProfileSample final {
    std::weak_ptr<SmokeState> state;
    gui_forms::Window* window{};
    gui_forms::ApplicationWindowHandle handle;
    std::weak_ptr<gui_forms::Control> owner;
    void operator()(gui_forms::FrameTime) const {
        const std::shared_ptr<SmokeState> live = state.lock();
        if (!live)
            return;
        if (!(*live).sampling) {
            (*live).sampling = true;
            (*live).sample_start = gui_forms::FrameClock::now();
            (*window).reset_activity_metrics();
            std::cout << "PROFILE_BEGIN" << std::endl;
            return;
        }
        const double seconds = std::chrono::duration<double>(
            gui_forms::FrameClock::now() - (*live).sample_start).count();
        std::cout << "PROFILE_METRICS {\"seconds\":" << seconds << ",\"window\":"
                  << (*window).metrics_snapshot().to_json() << "}" << std::endl;
        std::cout << "PROFILE_END" << std::endl;
        // Let the external collector read process counters before teardown.
        const std::shared_ptr<gui_forms::Control> timer_owner = owner.lock();
        if (!timer_owner)
            return;
        (*live).timer = (*window).schedule_ui_timer(
            *timer_owner, std::chrono::seconds(1),
            gui_forms::FrameClock::now() + std::chrono::seconds(1), SmokeClose{live, handle});
    }
};
struct FocusTable final {
    std::weak_ptr<games::Collection> table;
    std::shared_ptr<SmokeState> smoke;
    void operator()(gui_forms::Window& window, gui_forms::ApplicationWindowHandle handle) const {
        std::shared_ptr<games::Collection> live = table.lock();
        if (live) {
            if (smoke && (*smoke).profiling) {
                if ((*smoke).entry >= 0)
                    (*live).open_entry(static_cast<games::Entry>((*smoke).entry));
                else
                    (*live).show_shelf();
            }
            static_cast<void>(window.request_focus(live));
            (*live).activate();
            if (smoke && (*smoke).profiling) {
                (*smoke).timer =
                    window.schedule_ui_timer(*live, std::chrono::seconds(10),
                                             gui_forms::FrameClock::now() + std::chrono::seconds(5),
                                             ProfileSample{smoke, &window, handle, live});
            } else if (smoke) {
                (*smoke).timer =
                    window.schedule_ui_timer(*live, std::chrono::seconds(5),
                                             gui_forms::FrameClock::now() + std::chrono::seconds(5),
                                             SmokeClose{smoke, handle});
            }
        }
    }
};
int main(int argc, char** argv) {
    try {
        if (argc < 1 || argv[0] == nullptr) {
            return 1;
        }
        games::initialize_assets(argv[0]);
        const bool smoke_requested = argc == 2 && std::string_view(argv[1]) == "--smoke-test";
        const bool profile_requested = argc == 3 && std::string_view(argv[1]) == "--profile-idle";
        std::shared_ptr<SmokeState> smoke =
            smoke_requested || profile_requested ? std::make_shared<SmokeState>() : nullptr;
        if (profile_requested) {
            (*smoke).profiling = true;
            const std::string_view argument(argv[2]);
            const std::from_chars_result parsed =
                std::from_chars(argument.data(), argument.data() + argument.size(), (*smoke).entry);
            if (parsed.ec != std::errc{} || parsed.ptr != argument.data() + argument.size() ||
                (*smoke).entry < -1 || (*smoke).entry >= games::entry_count)
                throw std::runtime_error("Profile entry must be -1 (shelf) or a game index 0..17");
        }
        std::shared_ptr<games::Collection> table =
            gui_forms::make_control<games::Collection>(gui_forms::StableId("games.table"));
        std::unique_ptr<gui_forms::Window> window =
            std::make_unique<gui_forms::Window>(table, gui_forms::Size{1060, 680});
        gui_forms::ApplicationWindowOptions options;
        options.title = "PlaySuite";
        // Every view lays itself out down to a small laptop window.
        options.initial_size = {1060, 680};
        options.minimum_size = {600, 420};
        options.print_metrics_on_close = true;
        options.ready = FocusTable{table, smoke};
        gui_forms::ApplicationResult result =
            gui_forms::Application::run(std::move(window), std::move(options));
        if (result.callback_exception)
            std::rethrow_exception(result.callback_exception);
        if (smoke && !(*smoke).completed) {
            return 1;
        }
        if (smoke && result.accepted()) {
            std::cout << "Native PlaySuite window smoke passed\n";
        }
        return result.accepted() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
