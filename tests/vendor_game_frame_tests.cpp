#ifdef GAMES_FRAME_atomprobe
#include "atomprobe_view.hpp"
#endif
#ifdef GAMES_FRAME_fourpegs
#include "fourpegs_view.hpp"
#endif
#ifdef GAMES_FRAME_switchbox
#include "switchbox_view.hpp"
#endif
#ifdef GAMES_FRAME_eggy
#include "eggy_view.hpp"
#endif
#ifdef GAMES_FRAME_koikoi
#include "koi_view.hpp"
#endif
#ifdef GAMES_FRAME_parrots
#include "table_view.hpp"
#endif
#ifdef GAMES_FRAME_liarsdice
#include "dice_view.hpp"
#endif
#ifdef GAMES_FRAME_penthesheep
#include "sheep_view.hpp"
#endif
#include "test_paths.hpp"

#include <fstream>
#include <iostream>
#include <thread>

namespace {
namespace gf = gui_forms;
class ExposurePainter final : public gf::Painter {
  public:
    std::shared_ptr<gf::LiveSurface> presented;
    gf::Rect destination{};
    void save() override {}
    void restore() override {}
    void translate(gf::Point) override {}
    void clip_rect(gf::Rect) override {}
    void fill_rect(gf::Rect, gf::Color) override {}
    void stroke_rect(gf::Rect, gf::Color, double) override {}
    void draw_line(gf::Point, gf::Point, gf::Color, double) override {}
    void draw_text_utf8(gf::Point, std::string_view, gf::FontSpec, gf::Color) override {}
    void draw_image(gf::ImageId, gf::Rect, double) override {}
    void draw_live_surface(std::shared_ptr<gf::LiveSurface> surface, gf::Rect bounds,
                           double) override {
        presented = std::move(surface);
        destination = bounds;
    }
};
void set_foreground(gf::Control& view, bool foreground) {
#ifdef GAMES_FRAME_fourpegs
    if (fp::FourPegsView* game = dynamic_cast<fp::FourPegsView*>(&view)) (*game).set_cabinet(foreground,false,false,true);
#endif
#ifdef GAMES_FRAME_atomprobe
    if (ap::AtomProbeView* game = dynamic_cast<ap::AtomProbeView*>(&view)) (*game).set_cabinet(foreground,false,false,true);
#endif
#ifdef GAMES_FRAME_switchbox
    if (sbx::SwitchboxView* game = dynamic_cast<sbx::SwitchboxView*>(&view)) (*game).set_cabinet(foreground,false,false,true);
#endif
#ifdef GAMES_FRAME_koikoi
    if (kk::KoiView* game = dynamic_cast<kk::KoiView*>(&view)) (*game).set_cabinet(foreground,false,false,true);
#endif
#ifdef GAMES_FRAME_parrots
    if (pt::TableView* game = dynamic_cast<pt::TableView*>(&view)) (*game).set_cabinet(foreground,false,false,true);
#endif
#ifdef GAMES_FRAME_liarsdice
    if (ld::DiceView* game = dynamic_cast<ld::DiceView*>(&view)) (*game).set_cabinet(foreground,false,false,true);
#endif
#ifdef GAMES_FRAME_penthesheep
    if (sh::SheepView* game = dynamic_cast<sh::SheepView*>(&view)) (*game).set_cabinet(foreground,false,false,true);
#endif
}
void require(const bool condition, const char* const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
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
                frame.row_bytes() <= bytes.size() / frame.height(),
            "Complete frame covers declared rows");
    for (std::uint32_t y = 0; y < frame.height(); ++y) {
        const std::size_t row = y * frame.row_bytes();
        for (std::uint32_t x = 0; x < frame.width(); ++x) {
            const std::size_t offset = row + x * 4;
            const std::array<char, 3> rgb{static_cast<char>(bytes[offset + 2]),
                                          static_cast<char>(bytes[offset + 1]),
                                          static_cast<char>(bytes[offset])};
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
        const std::vector<gf::LiveSurfacePresentation> presentations =
            window.take_live_surface_presentations();
        for (const gf::LiveSurfacePresentation& item : presentations) {
            surface = item.surface;
        }
        if (surface) {
            gf::LiveSurfaceFrame frame = (*surface).acquire_latest();
            if (frame && frame.generation() > after) {
                return frame;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("Complete game frame did not publish before deadline");
}
// The game's own count of ticks run: it advances whether or not the picture changes, so a
// still scene, which rightly publishes nothing, still shows the game is alive.
std::uint64_t ticks(gf::Control& view) {
#ifdef GAMES_FRAME_fourpegs
    if (fp::FourPegsView* game = dynamic_cast<fp::FourPegsView*>(&view)) return (*game).ticks();
#endif
#ifdef GAMES_FRAME_atomprobe
    if (ap::AtomProbeView* game = dynamic_cast<ap::AtomProbeView*>(&view)) return (*game).ticks();
#endif
#ifdef GAMES_FRAME_switchbox
    if (sbx::SwitchboxView* game = dynamic_cast<sbx::SwitchboxView*>(&view)) return (*game).ticks();
#endif
#ifdef GAMES_FRAME_eggy
    if (eggy::EggyView* game = dynamic_cast<eggy::EggyView*>(&view)) return (*game).ticks();
#endif
#ifdef GAMES_FRAME_koikoi
    if (kk::KoiView* game = dynamic_cast<kk::KoiView*>(&view)) return (*game).ticks();
#endif
#ifdef GAMES_FRAME_parrots
    if (pt::TableView* game = dynamic_cast<pt::TableView*>(&view)) return (*game).ticks();
#endif
#ifdef GAMES_FRAME_liarsdice
    if (ld::DiceView* game = dynamic_cast<ld::DiceView*>(&view)) return (*game).ticks();
#endif
#ifdef GAMES_FRAME_penthesheep
    if (sh::SheepView* game = dynamic_cast<sh::SheepView*>(&view)) return (*game).ticks();
#endif
    throw std::runtime_error("Game frame fixture has no tick count");
}
// Runs the window until the game has ticked `count` more times, taking any frames it
// presents meanwhile.
void run_ticks(gf::Window& window, std::shared_ptr<gf::LiveSurface>& surface, gf::Control& view,
               const std::uint64_t count) {
    const std::uint64_t target = ticks(view) + count;
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(12);
    while (std::chrono::steady_clock::now() < deadline) {
        static_cast<void>(window.poll_frame_schedule(std::chrono::steady_clock::now()));
        const std::vector<gf::LiveSurfacePresentation> presentations =
            window.take_live_surface_presentations();
        for (const gf::LiveSurfacePresentation& item : presentations) {
            surface = item.surface;
        }
        if (ticks(view) >= target) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("Game did not tick before deadline");
}
} // namespace
int main(const int argc, char** const argv) {
    try {
        require(argc == 3 || argc == 4, "Pass assets, game name and optional preview directory");
        const std::string game(argv[2]);
        set_assets(argv[1]);
        const std::filesystem::path scratch = games_test::scratch_directory("fourpegs-frame-");
        games_test::isolate_saves(scratch);
        const std::filesystem::path previews = argc == 4 ? std::filesystem::path(argv[3]) : scratch;
        std::filesystem::create_directories(previews);
        std::shared_ptr<gf::Control> view{};
#ifdef GAMES_FRAME_fourpegs
        if (game == "fourpegs") {
            std::shared_ptr<fp::FourPegsView> control=gf::make_control<fp::FourPegsView>(gf::StableId(game),fp::Options{.dev=true});
            (*control).set_cabinet(true,false,false,true);
            view=control;
        }
#endif
#ifdef GAMES_FRAME_atomprobe
        if (game == "atomprobe") {
            std::shared_ptr<ap::AtomProbeView> control=gf::make_control<ap::AtomProbeView>(gf::StableId(game),ap::Options{.dev=true});
            (*control).set_cabinet(true,false,false,true);
            view=control;
        }
#endif
#ifdef GAMES_FRAME_switchbox
        if (game == "switchbox") {
            std::shared_ptr<sbx::SwitchboxView> control=gf::make_control<sbx::SwitchboxView>(gf::StableId(game),sbx::Options{.dev=true});
            (*control).set_cabinet(true,false,false,true);
            view=control;
        }
#endif
#ifdef GAMES_FRAME_eggy
        if (game == "eggy") {
            std::shared_ptr<eggy::EggyView> control=gf::make_control<eggy::EggyView>(gf::StableId(game),eggy::Options{.dev=true});
            (*control).set_cabinet_preferences(false,false,true);
            view=control;
        }
#endif
#ifdef GAMES_FRAME_koikoi
        if (game == "koikoi") {
            std::shared_ptr<kk::KoiView> control=gf::make_control<kk::KoiView>(gf::StableId(game),kk::Options{.hosted=true,.dev=true});
            (*control).set_cabinet(true,false,false,true);
            view=control;
        }
#endif
#ifdef GAMES_FRAME_parrots
        if (game == "parrots") {
            std::shared_ptr<pt::TableView> control=gf::make_control<pt::TableView>(gf::StableId(game),pt::Options{.hosted=true,.dev=true});
            (*control).set_cabinet(true,false,false,true);
            view=control;
        }
#endif
#ifdef GAMES_FRAME_liarsdice
        if (game == "liarsdice") {
            std::shared_ptr<ld::DiceView> control=gf::make_control<ld::DiceView>(gf::StableId(game),ld::Options{.hosted=true,.dev=true});
            (*control).set_cabinet(true,false,false,true);
            view=control;
        }
#endif
#ifdef GAMES_FRAME_penthesheep
        if (game == "penthesheep") {
            std::shared_ptr<sh::SheepView> control=gf::make_control<sh::SheepView>(gf::StableId(game),sh::Options{.hosted=true,.dev=true});
            (*control).set_cabinet(true,false,false,true);
            view=control;
        }
#endif
        if (!view) throw std::runtime_error("Unknown game frame fixture");
        gf::Window window(view, {1180, 800});
        window.set_active(true);
        window.perform_layout();
        static_cast<void>(window.request_focus(view));
        std::shared_ptr<gf::LiveSurface> surface{};
        gf::LiveSurfaceFrame first = advance(window, surface, 0);
#ifdef GAMES_FRAME_penthesheep
        if (game == "penthesheep") {
            const std::shared_ptr<sh::SheepView> sheep =
                std::static_pointer_cast<sh::SheepView>(view);
            const std::chrono::steady_clock::time_point deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (!(*sheep).ready_for_play() && std::chrono::steady_clock::now() < deadline) {
                const std::uint64_t previous = first.generation();
                first = {};
                first = advance(window, surface, previous);
            }
            require((*sheep).ready_for_play(),
                    "Sheep generated a playable meadow, beyond the loading screen");
        }
#endif
        require(first.width() == 1180 && first.height() == 800,
                "Complete game frame has requested dimensions");
        ExposurePainter exposure;
        (*view).on_paint(exposure, (*view).client_rectangle());
        require(exposure.presented == surface && exposure.destination == (*view).client_rectangle(),
                "An ordinary expose replays the latest surface without waiting for publication");
        write_frame(first, previews / (game + "-portable-game.ppm"));
        const std::vector<std::byte> retained(first.pixels().begin(), first.pixels().end());
        run_ticks(window, surface, *view, 10);
        require(std::equal(retained.begin(), retained.end(), first.pixels().begin(),
                           first.pixels().end()),
                "Retained frame is immutable while simulation and text continue");
        first = {};
        // The actor reveals thirty new characters per second. Completing
        // successive frozen attempts must still allow multiple publications.
        std::uint64_t generation = (*surface).snapshot().published_generation;
        for (int index = 0; index < 8; ++index) {
            gf::LiveSurfaceFrame talking = advance(window, surface, generation);
            generation = talking.generation();
            if (index == 7)
                write_frame(talking, previews / (game + "-portable-dialogue.ppm"));
        }
        gf::KeyEvent key{};
        key.action = gf::KeyAction::down;
        if (game == "eggy") {
            key.physical_key = gf::PhysicalKey::enter;
            static_cast<void>(window.dispatch_key(key));
        }
        key.physical_key = gf::PhysicalKey::f1;
        const bool handled = window.dispatch_key(key);
        require(handled, "Actual game opens help through keyboard input");
        const std::uint64_t before_help = (*surface).snapshot().published_generation;
        gf::LiveSurfaceFrame help = advance(window, surface, before_help);
        write_frame(help, previews / (game + "-portable-help.ppm"));
        help = {};
        if (game == "koikoi") {
            const gf::FrameTime deadline = gf::FrameClock::now() + std::chrono::seconds(8);
            while (window.next_wake() && gf::FrameClock::now() < deadline) {
                static_cast<void>(window.poll_frame_schedule(gf::FrameClock::now()));
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            require(!window.next_wake(), "Settled Koi-Koi stops its timer entirely");
            const std::uint64_t settled = (*surface).snapshot().published_generation;
            static_cast<void>(
                window.poll_frame_schedule(gf::FrameClock::now() + std::chrono::seconds(1)));
            require((*surface).snapshot().published_generation == settled,
                    "Static Koi-Koi does not republish identical frames");
        }
        window.set_scale(1.5);
        window.perform_layout();
        const std::uint64_t before_scale = (*surface).snapshot().published_generation;
        gf::LiveSurfaceFrame scaled = advance(window, surface, before_scale);
        require(scaled.width() == 1770 && scaled.height() == 1200,
                "Fractional DPI recreates a complete native-resolution frame");
        write_frame(scaled, previews / (game + "-portable-help-150.ppm"));
        scaled = {};
        if (game == "koikoi" || game == "parrots" || game == "liarsdice" || game == "penthesheep") {
            window.set_scale(1);
            window.resize({600, 320}); // Space left below a wrapped open command capsule.
            window.perform_layout();
            const std::uint64_t before_small = (*surface).snapshot().published_generation;
            gf::LiveSurfaceFrame small = advance(window, surface, before_small);
            require(small.width() == 600 && small.height() == 320,
                    "Compact game frame fits below the open capsule");
            write_frame(small, previews / (game + "-portable-help-small.ppm"));
            small = {};
            key.physical_key = gf::PhysicalKey::escape;
            static_cast<void>(window.dispatch_key(key));
            const std::uint64_t before_board = (*surface).snapshot().published_generation;
            gf::LiveSurfaceFrame board = advance(window, surface, before_board);
            write_frame(board, previews / (game + "-portable-board-small.ppm"));
        }
        if (game != "eggy") {
            set_foreground(*view, false);
            (*view).set_visible(false);
            const std::uint64_t hidden = (*surface).snapshot().published_generation;
            const std::uint64_t callbacks = window.metrics_snapshot().callbacks_emitted;
            static_cast<void>(
                window.poll_frame_schedule(gf::FrameClock::now() + std::chrono::seconds(2)));
            require(window.metrics_snapshot().callbacks_emitted == callbacks,
                    "Hidden game has no polling timer callbacks");
            require((*surface).snapshot().published_generation == hidden,
                    "Hidden game does not render or publish");
            (*view).set_visible(true);
            set_foreground(*view, true);
            run_ticks(window, surface, *view, 1);
            const gf::LiveSurfaceFrame resumed = (*surface).acquire_latest();
            require(resumed.width() > 0, "Hidden game resumes without losing its surface");
        }
        std::cout << game << " game/help frames, idle lifecycle, retained frame and DPI passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
