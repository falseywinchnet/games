#include "puzzle_view.hpp"
#include "storage.hpp"
#include "table.hpp"
#include "test_paths.hpp"
#include "gui_forms/window.hpp"
#include <iostream>
#include <stdexcept>

namespace {
namespace gf = gui_forms;
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
class PaintSink final : public gf::Painter {
  public:
    void save() override {}
    void restore() override {}
    void translate(gf::Point) override {}
    void clip_rect(gf::Rect) override {}
    void fill_rect(gf::Rect, gf::Color) override {}
    void stroke_rect(gf::Rect, gf::Color, double) override {}
    void draw_line(gf::Point, gf::Point, gf::Color, double) override {}
    void draw_text_utf8(gf::Point, std::string_view, gf::FontSpec, gf::Color) override {}
    void draw_image(gf::ImageId, gf::Rect, double) override {}
};
void flush(gf::Window& window, PaintSink& painter) {
    static_cast<void>(window.paint(painter));
    static_cast<void>(window.take_damage());
}
void check(games::PuzzleKind kind) {
    std::shared_ptr<games::PuzzleView> view =
        gf::make_control<games::PuzzleView>(gf::StableId("puzzle"), kind);
    (*view).game.deal(42);
    gf::Window window(view, {1060, 680});
    window.perform_layout();
    (*view).activate();
    PaintSink painter;
    flush(window, painter);
    const std::uint64_t initial = window.image_resource_snapshot().revision;
    (*view).arrange({0, 0, 1060, 680});
    require(window.image_resource_snapshot().revision == initial,
            "Unchanged layout must not rerasterize the board");
    flush(window, painter);
    if (kind == games::PuzzleKind::gems) {
        static_cast<void>(window.poll_frame_schedule(gf::FrameClock::now() +
                                                    std::chrono::milliseconds(30)));
        const gf::Rect damage = window.take_damage().bounds();
        const gf::Rect board = (*view).board();
        require(!damage.empty() && gf::Rect::intersection(damage, board) == damage,
                "Ordinary Gems animation damages only the board");
        require(window.image_resource_snapshot().revision > initial,
                "Bounded damage still publishes animated gem pixels");
        (*view).set_visible(false);
        const std::uint64_t hidden = window.image_resource_snapshot().revision;
        static_cast<void>(window.poll_frame_schedule(gf::FrameClock::now() +
                                                    std::chrono::milliseconds(60)));
        require(window.image_resource_snapshot().revision == hidden,
                "Hidden Gems must not render");
    } else {
        for (int i = 0; i < 12; ++i) {
            gf::PointerEvent move;
            move.action = gf::PointerAction::move;
            move.position = {450.0 + i * 3, 250};
            static_cast<void>(window.dispatch_pointer(move));
        }
        require(window.image_resource_snapshot().revision == initial,
                "A burst of Cube pointer input is coalesced before rasterization");
        static_cast<void>(window.poll_frame_schedule(gf::FrameClock::now() +
                                                    std::chrono::milliseconds(30)));
        require(window.image_resource_snapshot().revision == initial + 1,
                "Cube publishes one raster for the latest pointer state");
    }
}
void check_hearts_deadline() {
    std::shared_ptr<games::Table> table =
        gf::make_control<games::Table>(gf::StableId("hearts"));
    for (std::uint32_t seed = 1; seed <= 100; ++seed) {
        (*table).game.deal(games::Kind::hearts, seed);
        require((*table).game.pass({0, 1, 2}), "Complete fixture's opening pass");
        if ((*table).game.state.turn == 1 || (*table).game.state.turn == 2)
            break;
    }
    require((*table).game.state.turn == 1 || (*table).game.state.turn == 2,
            "Two consecutive computer turns in Hearts fixture");
    gf::Window window(table, {1060, 680});
    window.perform_layout();
    (*table).activate();
    PaintSink painter;
    flush(window, painter);
    const gf::FrameTime first_play = gf::FrameClock::now();
    static_cast<void>(window.poll_frame_schedule(first_play +
                                                std::chrono::milliseconds(20)));
    flush(window, painter);
    require((*table).game.state.piles[10].size() == 1 && (*table).game.invariant(),
            "First computer card is played normally");
    const std::optional<gf::FrameTime> wake = window.next_wake();
    require(wake && *wake > first_play + std::chrono::milliseconds(400),
            "Resting Hearts waits for its play deadline instead of polling at 125 Hz");
    (*table).activate();
    flush(window, painter);
    const std::optional<gf::FrameTime> resumed = window.next_wake();
    require(resumed && *resumed < gf::FrameClock::now() + std::chrono::milliseconds(30),
            "Fresh activity wakes a table waiting for the computer");
}
}
int main() {
    try {
        const std::filesystem::path scratch = games_test::scratch_directory("puzzle-frames-");
        games_test::isolate_saves(scratch);
        games::Cabinet preferences;
        for (int i = 0; i < 4; ++i) {
            preferences.games[i].deal(static_cast<games::Kind>(i), 42);
            preferences.started[i] = true;
        }
        preferences.sound = preferences.music = false;
        require(games::save_cabinet(games::cabinet_path(), preferences), "Save test preferences");
        check(games::PuzzleKind::gems);
        check(games::PuzzleKind::cube);
        preferences.reduced = true;
        require(games::save_cabinet(games::cabinet_path(), preferences), "Save reduced motion fixture");
        check_hearts_deadline();
        std::filesystem::remove_all(scratch);
        std::cout << "Board damage, layout reuse, hidden rendering and input coalescing passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
