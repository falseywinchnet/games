#include "fourpegs_view.hpp"
#include "test_paths.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace gf = gui_forms;
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void pointer(gf::Window& window, gf::Control& view, gf::PointerAction action, gf::Point local) {
    gf::PointerEvent event;
    event.action = action;
    event.button = gf::PointerButton::primary;
    event.position = view.point_to_window(local);
    static_cast<void>(window.dispatch_pointer(event));
}
void click(gf::Window& window, gf::Control& view, gf::Point local) {
    pointer(window, view, gf::PointerAction::down, local);
    pointer(window, view, gf::PointerAction::up, local);
}
gf::Point projected(const fp::Lair& scene, fp::V3 location) {
    double x = 0, y = 0;
    scene.to_screen(location + fp::V3{0, 0, .12}, x, y);
    return {x * 2, y * 2};
}
}
int main() {
    try {
        games_test::isolate_saves(games_test::scratch_directory("fourpegs-pointer-"));
        std::shared_ptr<fp::FourPegsView> view = gf::make_control<fp::FourPegsView>(
            gf::StableId("pegs"), fp::Options{.dev = true, .hosted = true});
        gf::Window window(view, {1000, 660});
        window.perform_layout();
        (*view).set_cabinet(true, false, false, true);
        fp::Lair scene;
        scene.resize(500, 330);
        scene.r.ax = .42;
        scene.r.set_camera();
        click(window, *view, projected(scene, fp::Lair::palette(0)));
        require((*view).board().draft()[0] == 0, "Palette click places an orb through real pointer routing");
        pointer(window, *view, gf::PointerAction::down, projected(scene, fp::Lair::palette(1)));
        pointer(window, *view, gf::PointerAction::move, projected(scene, fp::Lair::socket(1)));
        pointer(window, *view, gf::PointerAction::up, projected(scene, fp::Lair::socket(1)));
        require((*view).board().draft()[1] == 1, "Drag commits after normal capture release");
        click(window, *view, projected(scene, fp::Lair::palette(2)));
        click(window, *view, projected(scene, fp::Lair::socket(3)));
        require((*view).board().draft()[3] == 2 && (*view).board().draft()[2] < 0,
                "Palette then socket retargets the selected orb");
        click(window, *view, projected(scene, fp::Lair::socket(2)));
        click(window, *view, projected(scene, fp::Lair::palette(3)));
        require((*view).board().draft()[2] == 3, "Socket then palette places in the chosen socket");
        click(window, *view, projected(scene, fp::Lair::check_button()));
        require((*view).board().turns_used() == 1, "Check button submits the complete mouse-entered guess");
        std::cout << "Four Pegs pointer routing passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
