#include "gui_forms/application.hpp"
#include "collection.hpp"
#include "runtime_paths.hpp"
#include <iostream>
struct FocusTable final {
    std::weak_ptr<games::Collection> table;
    void operator()(gui_forms::Window& window, gui_forms::ApplicationWindowHandle) const {
        std::shared_ptr<games::Collection> live = table.lock();
        if (live) {
            static_cast<void>(window.request_focus(live));
            (*live).activate();
        }
    }
};
int main(int argc, char** argv) {
    try {
        if (argc < 1 || argv[0] == nullptr) { return 1; }
        games::initialize_assets(argv[0]);
        std::shared_ptr<games::Collection> table =
            gui_forms::make_control<games::Collection>(gui_forms::StableId("games.table"));
        std::unique_ptr<gui_forms::Window> window =
            std::make_unique<gui_forms::Window>(table, gui_forms::Size{1180, 800});
        gui_forms::ApplicationWindowOptions options;
        options.title = "Games";
        options.initial_size = {1180, 800};
        options.minimum_size = {1000, 740};
        options.print_metrics_on_close = true;
        options.ready = FocusTable{table};
        gui_forms::ApplicationResult result =
            gui_forms::Application::run(std::move(window), std::move(options));
        if (result.callback_exception)
            std::rethrow_exception(result.callback_exception);
        return result.accepted() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
