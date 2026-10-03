#include "table_view.hpp"
#include "gui_forms/application.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

struct FocusView final {
    std::weak_ptr<pt::TableView> view;
    void operator()(gui_forms::Window& window, gui_forms::ApplicationWindowHandle) const {
        if (std::shared_ptr<pt::TableView> live = view.lock()) {
            static_cast<void>(window.request_focus(live));
            live->activate();
        }
    }
};

int main(int argc, char** argv) {
    try {
        pt::Options opt;
        for (int i = 1; i < argc; ++i)
            if (std::string(argv[i]) == "--dev") opt.dev = true;
        auto view = gui_forms::make_control<pt::TableView>(gui_forms::StableId("parrots.view"), opt);
        auto window = std::make_unique<gui_forms::Window>(view, gui_forms::Size{1100, 780});
        gui_forms::ApplicationWindowOptions options;
        options.title = opt.dev ? "The Parrot's Table (dev save)" : "The Parrot's Table";
        options.initial_size = {1100, 780};
        // dev only: PT_WINDOW=WxH opens at another size (for checking layouts)
        if (const char* ws = std::getenv("PT_WINDOW"); ws && opt.dev) {
            int ww = 0, hh = 0;
            if (std::sscanf(ws, "%dx%d", &ww, &hh) == 2 && ww > 0 && hh > 0) options.initial_size = {static_cast<double>(ww), static_cast<double>(hh)};
        }
        options.minimum_size = {600, 370};  // the cabinet: 600 x 420 less its 50-point rail
        options.ready = FocusView{view};
        gui_forms::ApplicationResult result = gui_forms::Application::run(std::move(window), std::move(options));
        if (result.callback_exception) std::rethrow_exception(result.callback_exception);
        return result.accepted() ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
