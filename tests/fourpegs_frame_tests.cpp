#include "fourpegs_view.hpp"
#include "test_paths.hpp"
#include <fstream>
#include <iostream>
#include <thread>

namespace {
namespace gf = gui_forms;
void require(const bool condition, const char* const message) {
    if (!condition) { throw std::runtime_error(message); }
}
void set_assets(const char* const path) {
#ifdef _WIN32
    const int result = _putenv_s("GAMES_ASSET_DIR", path);
#else
    const int result = setenv("GAMES_ASSET_DIR", path, 1);
#endif
    require(result == 0, "Set isolated game asset path");
}
void write_frame(const gf::LiveSurfaceFrame& frame, const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary);
    output << "P6\n" << frame.width() << ' ' << frame.height() << "\n255\n";
    const std::span<const std::byte> bytes = frame.pixels();
    require(frame.row_bytes() >= frame.width() * 4 &&
        frame.row_bytes() <= bytes.size() / frame.height(), "Complete frame covers declared rows");
    for (std::uint32_t y = 0; y < frame.height(); ++y) {
        const std::size_t row = y * frame.row_bytes();
        for (std::uint32_t x = 0; x < frame.width(); ++x) {
            const std::size_t offset = row + x * 4;
            const std::array<char, 3> rgb{static_cast<char>(bytes[offset + 2]),
                static_cast<char>(bytes[offset + 1]), static_cast<char>(bytes[offset])};
            output.write(rgb.data(), 3);
        }
    }
    require(static_cast<bool>(output), "Write complete game frame preview");
}
gf::LiveSurfaceFrame advance(gf::Window& window, std::shared_ptr<gf::LiveSurface>& surface,
    const std::uint64_t after) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(12);
    while (std::chrono::steady_clock::now() < deadline) {
        static_cast<void>(window.poll_frame_schedule(std::chrono::steady_clock::now()));
        const std::vector<gf::LiveSurfacePresentation> presentations = window.take_live_surface_presentations();
        for (const gf::LiveSurfacePresentation& item : presentations) { surface = item.surface; }
        if (surface) {
            gf::LiveSurfaceFrame frame = (*surface).acquire_latest();
            if (frame && frame.generation() > after) { return frame; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("Complete Four Pegs frame did not publish before deadline");
}
}
int main(const int argc, char** const argv) {
    try {
        require(argc == 2 || argc == 3, "Pass assets and optional preview directory");
        set_assets(argv[1]);
        const std::filesystem::path scratch = games_test::scratch_directory("fourpegs-frame-");
        games_test::isolate_saves(scratch);
        const std::filesystem::path previews = argc == 3 ? std::filesystem::path(argv[2]) : scratch;
        std::filesystem::create_directories(previews);
        std::shared_ptr<fp::FourPegsView> view = gf::make_control<fp::FourPegsView>(gf::StableId("fourpegs"), fp::Options{.dev = true});
        (*view).set_cabinet(true, false, false, true);
        gf::Window window(view, {1180, 800});
        window.set_active(true);
        window.perform_layout();
        (*view).activate();
        std::shared_ptr<gf::LiveSurface> surface{};
        gf::LiveSurfaceFrame first = advance(window, surface, 0);
        require(first.width() == 1180 && first.height() == 800, "Complete game frame has requested dimensions");
        write_frame(first, previews / "fourpegs-portable-game.ppm");
        const std::vector<std::byte> retained(first.pixels().begin(), first.pixels().end());
        const std::uint64_t initial = first.generation();
        gf::LiveSurfaceFrame second = advance(window, surface, initial);
        require(std::equal(retained.begin(), retained.end(), first.pixels().begin(), first.pixels().end()),
            "Retained frame is immutable while simulation and text continue");
        second = {};
        first = {};
        // The actor reveals thirty new characters per second. Completing
        // successive frozen attempts must still allow multiple publications.
        std::uint64_t generation = (*surface).snapshot().published_generation;
        for (int index = 0; index < 8; ++index) {
            gf::LiveSurfaceFrame talking = advance(window, surface, generation);
            generation = talking.generation();
            if (index == 7) write_frame(talking, previews / "fourpegs-portable-dialogue.ppm");
        }
        gf::KeyEvent key{};
        key.action = gf::KeyAction::down;
        key.physical_key = gf::PhysicalKey::f1;
        const bool handled = window.dispatch_key(key);
        require(handled, "Actual game opens help through keyboard input");
        const std::uint64_t before_help = (*surface).snapshot().published_generation;
        gf::LiveSurfaceFrame help = advance(window, surface, before_help);
        write_frame(help, previews / "fourpegs-portable-help.ppm");
        help = {};
        window.set_scale(1.5);
        window.perform_layout();
        const std::uint64_t before_scale = (*surface).snapshot().published_generation;
        gf::LiveSurfaceFrame scaled = advance(window, surface, before_scale);
        require(scaled.width() == 1770 && scaled.height() == 1200, "Fractional DPI recreates a complete native-resolution frame");
        write_frame(scaled, previews / "fourpegs-portable-help-150.ppm");
        std::cout << "Four Pegs real game, help, retained frame and 150% DPI passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
