// The hosted-view contract (new-games/kit/view_contract_test.hpp) run against
// Stillwater's real control, then Stillwater's choice of tanks. Built with the
// PlaySuite application.
//
// Stillwater is ambient: its fish and grass move while it is visible, so it never
// settles untouched (see HANDOFF.md). It must still stop completely when hidden.
#include "stillwater_view.hpp"
#include "view_contract_test.hpp"

#include <iterator>

namespace {

namespace gf = ambient::gf;

struct Average {
    double red{};
    double green{};
    double blue{};
};

Average average(const gf::LiveSurfaceFrame& frame) {
    double red = 0;
    double green = 0;
    double blue = 0;
    const std::span<const std::byte> bytes = frame.pixels();
    const std::uint32_t step = 4;
    std::size_t count = 0;
    for (std::uint32_t y = 0; y < frame.height(); y += step) {
        const std::size_t row = static_cast<std::size_t>(y) * frame.row_bytes();
        for (std::uint32_t x = 0; x < frame.width(); x += step) {
            const std::size_t offset = row + static_cast<std::size_t>(x) * 4;
            blue += static_cast<double>(bytes[offset]);
            green += static_cast<double>(bytes[offset + 1]);
            red += static_cast<double>(bytes[offset + 2]);
            ++count;
        }
    }
    const double n = static_cast<double>(std::max<std::size_t>(count, 1));
    const Average result{red / n, green / n, blue / n};
    return result;
}

// The frame's colour says which tank is showing: the planted tank is green, the reef
// blue, the river pool brown. Red and blue are taken in either byte order.
enum class Seen { backdrop, planted, reef, pool };
Seen seen(const Average& a) {
    const double warm = std::max(a.red, a.blue);
    const double cool = std::min(a.red, a.blue);
    if (a.green > warm + 4)
        return Seen::planted;
    if (warm > a.green + 8 && warm > cool + 25)
        return Seen::reef;
    if (a.green > cool + 4 && warm > cool + 4)
        return Seen::pool;
    return Seen::backdrop;
}

const char* name_of(Seen value) {
    if (value == Seen::planted)
        return "planted";
    if (value == Seen::reef)
        return "reef";
    if (value == Seen::pool)
        return "pool";
    return "backdrop";
}

// Pumps frames until the picture shows `wanted` (a fade passes through dark first).
std::uint64_t wait_for(gf::Window& window, std::shared_ptr<gf::LiveSurface>& surface, std::uint64_t after,
                       Seen wanted, const char* what) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(20);
    Seen last = Seen::backdrop;
    Average colour{};
    while (std::chrono::steady_clock::now() < deadline) {
        gf::LiveSurfaceFrame frame = kit::contract_next_frame(window, surface, after, what);
        after = frame.generation();
        colour = average(frame);
        last = seen(colour);
        if (last == wanted)
            return after;
    }
    throw std::runtime_error(std::string(what) + ": still showing " + name_of(last) + " (" +
                             std::to_string(colour.red) + ", " + std::to_string(colour.green) + ", " +
                             std::to_string(colour.blue) + ")");
}

std::string label_of(const sw::StillwaterView& view, const char* id) {
    const std::vector<games::GameCommand> commands = view.commands();
    const games::GameCommand* found = kit::contract_find(commands, id);
    const std::string result = found != nullptr ? (*found).label : std::string();
    return result;
}

std::filesystem::path save_path() {
    const char* directory = std::getenv("GAMES_STATE_DIR");
    const std::filesystem::path result =
        std::filesystem::path(directory != nullptr ? directory : ".") / "stillwater-v1-dev.txt";
    return result;
}

std::string saved_text() {
    std::ifstream input(save_path());
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return text;
}

std::shared_ptr<sw::StillwaterView> open_view() {
    const ambient::ViewOptions view_options{.hosted = true, .dev = true};
    std::shared_ptr<sw::StillwaterView> view =
        gf::make_control<sw::StillwaterView>(gf::StableId("stillwater"), view_options);
    (*view).set_cabinet(true, false, false, false);
    return view;
}

// The Scene command cycles the tanks, fading through dark even while paused; the
// choice is saved and reopened; a save from before the choice opens the planted tank.
int run_scene_checks() {
    try {
        kit::contract_isolate_saves("stillwater-scenes");
        {
            // A save written before there was a choice of tanks.
            std::ofstream old(save_path());
            old << "ambient-settings 1\npaused 0\ndetail balanced\n";
        }
        {
            std::shared_ptr<sw::StillwaterView> view = open_view();
            gf::Window window(view, {600, 420});
            window.set_active(true);
            window.perform_layout();
            std::shared_ptr<gf::LiveSurface> surface{};
            std::uint64_t at = wait_for(window, surface, 0, Seen::planted, "an old save opens the planted tank");
            kit::contract_require(label_of(*view, "scene") == "Scene: Planted", "the Scene command names the tank");
            (*view).run_command("pause");
            (*view).run_command("scene");
            kit::contract_require(label_of(*view, "scene") == "Scene: Reef", "Scene moves on to the reef");
            at = wait_for(window, surface, at, Seen::reef, "the reef fades in while paused");
            kit::contract_require(saved_text().find("scene reef") != std::string::npos, "the choice is saved");
            (*view).run_command("pause");
            (*view).run_command("scene");
            kit::contract_require(label_of(*view, "scene") == "Scene: River pool", "then the river pool");
            at = wait_for(window, surface, at, Seen::pool, "the river pool fades in");
            // Twice quickly: the change in flight is redirected, not queued.
            (*view).run_command("scene");
            (*view).run_command("scene");
            kit::contract_require(label_of(*view, "scene") == "Scene: Reef", "two presses land on the reef");
            static_cast<void>(wait_for(window, surface, at, Seen::reef, "the second press wins"));
        }
        {
            std::shared_ptr<sw::StillwaterView> view = open_view();
            gf::Window window(view, {600, 420});
            window.set_active(true);
            window.perform_layout();
            std::shared_ptr<gf::LiveSurface> surface{};
            static_cast<void>(wait_for(window, surface, 0, Seen::reef, "the saved tank reopens"));
            kit::contract_require(label_of(*view, "scene") == "Scene: Reef", "and is named");
        }
        std::cout << "stillwater scenes: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "stillwater scenes: FAILED: " << error.what() << "\n";
        return 1;
    }
}

} // namespace

int main(int argc, char** argv) {
    kit::ContractOptions options;
    options.settles_when_untouched = false;
    if (argc > 1) {
        options.preview_directory = argv[1];
    }
    const ambient::ViewOptions view_options{.hosted = true, .dev = true};
    const int result =
        kit::run_view_contract<sw::StillwaterView, ambient::ViewOptions>("stillwater", view_options, options);
    if (result != 0)
        return result;
    return run_scene_checks();
}
