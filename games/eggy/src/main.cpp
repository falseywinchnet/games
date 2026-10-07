#include "eggy_view.hpp"
#include "gui_forms/application.hpp"

#include <cstring>
#include <iostream>
#include <string>

struct FocusEggy final {
    std::weak_ptr<eggy::EggyView> view;
    void operator()(gui_forms::Window& window, gui_forms::ApplicationWindowHandle) const {
        if (std::shared_ptr<eggy::EggyView> live = view.lock()) {
            static_cast<void>(window.request_focus(live));
            live->activate();
        }
    }
};

int main(int argc, char** argv) {
    try {
        eggy::Options opt;
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--summit") { opt.dev = true; opt.summit = true; }
            else if (a.rfind("--warp=", 0) == 0) { opt.dev = true; opt.warp_v = std::stod(a.substr(7)); }
            else if (a == "--dev") opt.dev = true;
            else if (a == "--storm") { opt.dev = true; opt.storm = true; }
        }
        auto view = gui_forms::make_control<eggy::EggyView>(gui_forms::StableId("eggy.view"), opt);
        auto window = std::make_unique<gui_forms::Window>(view, gui_forms::Size{1180, 800});
        gui_forms::ApplicationWindowOptions options;
        options.title = opt.dev ? "Eggy (dev save)" : "Eggy and the Very, Very Tall Mountain";
        options.initial_size = {1180, 800};
        options.minimum_size = {800, 560};
        options.ready = FocusEggy{view};
        gui_forms::ApplicationResult result = gui_forms::Application::run(std::move(window), std::move(options));
        if (result.callback_exception) std::rethrow_exception(result.callback_exception);
        return result.accepted() ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
