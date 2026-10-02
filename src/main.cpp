#include "gui_forms/application.hpp"
#include "collection.hpp"
#include "runtime_paths.hpp"
#include <iostream>
#include <string_view>
struct SmokeState final {
    gui_forms::FrameRequestToken timer{};
    bool completed{};
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
struct FocusTable final {
    std::weak_ptr<games::Collection> table;
    std::shared_ptr<SmokeState> smoke;
    void operator()(gui_forms::Window& window, gui_forms::ApplicationWindowHandle handle) const {
        std::shared_ptr<games::Collection> live = table.lock();
        if (live) {
            static_cast<void>(window.request_focus(live));
            (*live).activate();
            if (smoke) {
                (*smoke).timer = window.schedule_ui_timer(*live, std::chrono::seconds(5),
                    gui_forms::FrameClock::now() + std::chrono::seconds(5), SmokeClose{smoke, handle});
            }
        }
    }
};
int main(int argc, char** argv) {
    try {
        if (argc < 1 || argv[0] == nullptr) { return 1; }
        games::initialize_assets(argv[0]);
        const bool smoke_requested = argc == 2 && std::string_view(argv[1]) == "--smoke-test";
        std::shared_ptr<SmokeState> smoke = smoke_requested ? std::make_shared<SmokeState>() : nullptr;
        std::shared_ptr<games::Collection> table =
            gui_forms::make_control<games::Collection>(gui_forms::StableId("games.table"));
        std::unique_ptr<gui_forms::Window> window =
            std::make_unique<gui_forms::Window>(table, gui_forms::Size{1180, 800});
        gui_forms::ApplicationWindowOptions options;
        options.title = "Games";
        options.initial_size = {1180, 800};
        options.minimum_size = {1000, 740};
        options.print_metrics_on_close = true;
        options.ready = FocusTable{table, smoke};
        gui_forms::ApplicationResult result =
            gui_forms::Application::run(std::move(window), std::move(options));
        if (result.callback_exception)
            std::rethrow_exception(result.callback_exception);
        if (smoke && !(*smoke).completed) { return 1; }
        if (smoke && result.accepted()) { std::cout << "Native Games window smoke passed\n"; }
        return result.accepted() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
