#include "switchbox_view.hpp"
#include "gui_forms/application.hpp"

#include <iostream>
#include <string>

struct FocusView final {
    std::weak_ptr<sbx::SwitchboxView> view;
    void operator()(gui_forms::Window& window, gui_forms::ApplicationWindowHandle) const {
        if (std::shared_ptr<sbx::SwitchboxView> live = view.lock()) {
            static_cast<void>(window.request_focus(live));
            live->activate();
        }
    }
};

int main(int argc, char** argv) {
    try {
        sbx::Options opt;
        for (int i = 1; i < argc; ++i)
            if (std::string(argv[i]) == "--dev") opt.dev = true;
        auto view = gui_forms::make_control<sbx::SwitchboxView>(gui_forms::StableId("switchbox.view"), opt);
        auto window = std::make_unique<gui_forms::Window>(view, gui_forms::Size{1100, 760});
        gui_forms::ApplicationWindowOptions options;
        options.title = opt.dev ? "Switchbox (dev save)" : "Switchbox";
        options.initial_size = {1100, 760};
        options.minimum_size = {760, 520};
        options.ready = FocusView{view};
        gui_forms::ApplicationResult result = gui_forms::Application::run(std::move(window), std::move(options));
        if (result.callback_exception) std::rethrow_exception(result.callback_exception);
        return result.accepted() ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
