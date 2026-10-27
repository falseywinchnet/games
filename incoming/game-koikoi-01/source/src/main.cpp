#include "koi_view.hpp"
#include "gui_forms/application.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

struct FocusView final {
    std::weak_ptr<kk::KoiView> view;
    void operator()(gui_forms::Window& window, gui_forms::ApplicationWindowHandle) const {
        if (std::shared_ptr<kk::KoiView> live = view.lock()) {
            static_cast<void>(window.request_focus(live));
            live->activate();
        }
    }
};

int main(int argc, char** argv) {
    try {
        kk::Options opt;
        for (int i = 1; i < argc; ++i)
            if (std::string(argv[i]) == "--dev") opt.dev = true;
        auto view = gui_forms::make_control<kk::KoiView>(gui_forms::StableId("koikoi.view"), opt);
        auto window = std::make_unique<gui_forms::Window>(view, gui_forms::Size{1180, 800});
        gui_forms::ApplicationWindowOptions options;
        options.title = opt.dev ? "Koi-Koi (dev save)" : "Koi-Koi";
        options.initial_size = {1180, 800};
        // dev only: KK_WINDOW=WxH opens at another size (for checking layouts)
        if (const char* ws = std::getenv("KK_WINDOW"); ws && opt.dev) {
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
