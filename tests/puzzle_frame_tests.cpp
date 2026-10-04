#include "gui_forms/window.hpp"
#include "puzzle_view.hpp"
#include "storage.hpp"
#include "table.hpp"
#include "test_paths.hpp"
#include <cmath>
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
        static_cast<void>(
            window.poll_frame_schedule(gf::FrameClock::now() + std::chrono::milliseconds(30)));
        const gf::Rect damage = window.take_damage().bounds();
        const gf::Rect board = (*view).board();
        require(!damage.empty() && gf::Rect::intersection(damage, board) == damage,
                "Ordinary Gems animation damages only the board");
        require(window.image_resource_snapshot().revision > initial,
                "Bounded damage still publishes animated gem pixels");
        (*view).set_visible(false);
        const std::uint64_t hidden = window.image_resource_snapshot().revision;
        static_cast<void>(
            window.poll_frame_schedule(gf::FrameClock::now() + std::chrono::milliseconds(60)));
        require(window.image_resource_snapshot().revision == hidden, "Hidden Gems must not render");
    } else {
        for (int i = 0; i < 12; ++i) {
            gf::PointerEvent move;
            move.action = gf::PointerAction::move;
            move.position = {450.0 + i * 3, 250};
            static_cast<void>(window.dispatch_pointer(move));
        }
        require(window.image_resource_snapshot().revision == initial,
                "A burst of Cube pointer input is coalesced before rasterization");
        static_cast<void>(
            window.poll_frame_schedule(gf::FrameClock::now() + std::chrono::milliseconds(30)));
        require(window.image_resource_snapshot().revision == initial + 1,
                "Cube publishes one raster for the latest pointer state");
    }
}
void gem_pointer(gf::Window& window, games::PuzzleView& view, gf::PointerAction action, int cell) {
    const gf::Rect board = view.board();
    gf::PointerEvent event;
    event.action = action;
    event.button = gf::PointerButton::primary;
    event.position = view.point_to_window({board.x + (cell % 8 + .5) * board.width / 8,
                                           board.y + (cell / 8 + .5) * board.height / 8});
    static_cast<void>(window.dispatch_pointer(event));
}
void check_gems_input(bool drag) {
    std::shared_ptr<games::PuzzleView> view =
        gf::make_control<games::PuzzleView>(gf::StableId("gems-input"), games::PuzzleKind::gems);
    (*view).game.deal(42);
    games::PuzzleGame expected = (*view).game;
    int from = -1, to = -1;
    for (int cell = 0; cell < 64 && from < 0; ++cell)
        for (int step : {1, 8}) {
            const int neighbor = cell + step;
            if (neighbor >= 64 || (step == 1 && cell / 8 != neighbor / 8))
                continue;
            games::PuzzleGame trial = (*view).game;
            if (trial.gem_swap(cell, neighbor)) {
                from = cell;
                to = neighbor;
                expected = trial;
                break;
            }
        }
    require(from >= 0, "Gems fixture has a legal matching swap");
    gf::Window window(view, {1060, 680});
    window.perform_layout();
    (*view).activate();
    PaintSink painter;
    flush(window, painter);
    // Exercise the animated path; the older collection test uses reduced motion.
    static_cast<void>(
        window.poll_frame_schedule(gf::FrameClock::now() + std::chrono::milliseconds(30)));
    flush(window, painter);
    gem_pointer(window, *view, gf::PointerAction::down, from);
    if (drag)
        gem_pointer(window, *view, gf::PointerAction::move, to);
    else {
        gem_pointer(window, *view, gf::PointerAction::up, from);
        gem_pointer(window, *view, gf::PointerAction::down, to);
    }
    gem_pointer(window, *view, gf::PointerAction::up, to);
    require((*view).game.state.moves == 1 && (*view).game.state.score == expected.state.score &&
                (*view).game.state.grid == expected.state.grid,
            "Animated Gems accepts a legal pointer swap with the correct cascade");
    require(!window.take_damage().empty(), "Successful Gems input schedules visible feedback");
    games::PuzzleGame saved(games::PuzzleKind::gems);
    require(saved.load(games::cabinet_path().parent_path() / "gems-v1.txt") &&
                saved.state.moves == 1 && saved.state.grid == expected.state.grid,
            "Legal pointer swap persists the resulting board");
}
void check_hearts_deadline() {
    std::shared_ptr<games::Table> table = gf::make_control<games::Table>(gf::StableId("hearts"));
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
    static_cast<void>(window.poll_frame_schedule(first_play + std::chrono::milliseconds(20)));
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
void check_untangle_edge_drag() {
    std::shared_ptr<games::PuzzleView> view = gf::make_control<games::PuzzleView>(
        gf::StableId("untangle-edge"), games::PuzzleKind::untangle);
    (*view).game.deal(42);
    (*view).game.state.nodes = {{.15, .2}, {.5, .2}, {.85, .2}};
    (*view).game.state.edges = {{0, 1}, {1, 2}};
    (*view).game.state.marks.fill(0);
    (*view).game.state.aux.fill(0);
    gf::Window window(view, {600, 420});
    window.perform_layout();
    (*view).activate();
    PaintSink painter;
    flush(window, painter);
    const gf::Rect board = (*view).board();
    gf::PointerEvent event;
    event.action = gf::PointerAction::down;
    event.button = gf::PointerButton::primary;
    event.position =
        (*view).point_to_window({board.x + .15 * board.width, board.y + .2 * board.height});
    require(window.dispatch_pointer(event), "Pick up an unfrozen peg");
    flush(window, painter);
    window.reset_activity_metrics();
    event.action = gf::PointerAction::move;
    event.position =
        (*view).point_to_window({board.x + .035 * board.width, board.y + .035 * board.height});
    static_cast<void>(window.dispatch_pointer(event));
    const gf::Rect damage = window.take_damage().bounds();
    require(damage.x < board.x && damage.y < board.y,
            "Dragging at the board edge includes the enlarged head and shadow outside it");
    static_cast<void>(window.paint(painter, damage));
    require(window.metrics().snapshot().display_chunks_rebuilt >= 3,
            "Dragging updates yarn, peg and status together without waiting for a timer");
    event.action = gf::PointerAction::up;
    static_cast<void>(window.dispatch_pointer(event));
    require(std::abs((*view).game.state.nodes[0].x - .035) < 1e-9 &&
                std::abs((*view).game.state.nodes[0].y - .035) < 1e-9,
            "Edge drag commits the displayed position");
}
void check_scene_retention(games::PuzzleKind kind) {
    std::shared_ptr<games::PuzzleView> view =
        gf::make_control<games::PuzzleView>(gf::StableId("retained"), kind);
    (*view).game.deal(42);
    if (kind == games::PuzzleKind::untangle) {
        // A quiet easy board: only the cat animates. No changing yarn or frosted pegs.
        (*view).game.state.nodes = {{.15, .2}, {.5, .2}, {.85, .2}};
        (*view).game.state.edges = {{0, 1}, {1, 2}};
        (*view).game.state.marks.fill(0);
        (*view).game.state.aux.fill(0);
    }
    gf::Window window(view, {1060, 680});
    window.perform_layout();
    (*view).activate();
    PaintSink painter;
    flush(window, painter);
    window.reset_activity_metrics();
    static_cast<void>(
        window.poll_frame_schedule(gf::FrameClock::now() + std::chrono::milliseconds(30)));
    const gf::Rect damage = window.take_damage().bounds();
    require(!damage.empty(), "Animated scene has damage");
    const gf::Rect board = (*view).board();
    if (kind == games::PuzzleKind::untangle)
        require(damage.width * damage.height < board.width * board.height * .3,
                "Cat-only animation must not invalidate the cushion or the whole board");
    require(window.paint(painter, damage).has_value(), "Retained scene paints its damaged area");
    const gf::MetricsSnapshot metrics = window.metrics().snapshot();
    require(metrics.display_chunks_rebuilt == 1,
            "Ordinary animation rebuilds only its own chunk, not scenery or text");
    require(metrics.display_chunks_reused >= 2, "Scenery and foreground text stay retained");
    (*view).run_command("help");
    flush(window, painter);
    (*view).run_command("help");
    flush(window, painter);
    window.reset_activity_metrics();
    static_cast<void>(
        window.poll_frame_schedule(gf::FrameClock::now() + std::chrono::milliseconds(60)));
    flush(window, painter);
    require(window.metrics().snapshot().display_chunks_rebuilt == 1,
            "Closing Help restores independently retained animation");
}
} // namespace
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
        preferences.reduced = false;
        require(games::save_cabinet(games::cabinet_path(), preferences), "Save test preferences");
        check(games::PuzzleKind::gems);
        check(games::PuzzleKind::cube);
        check_gems_input(false);
        check_gems_input(true);
        check_scene_retention(games::PuzzleKind::gems);
        check_scene_retention(games::PuzzleKind::untangle);
        check_untangle_edge_drag();
        preferences.reduced = true;
        require(games::save_cabinet(games::cabinet_path(), preferences),
                "Save reduced motion fixture");
        check_hearts_deadline();
        std::filesystem::remove_all(scratch);
        std::cout << "Board damage, layout reuse, hidden rendering, animated input and scheduling "
                     "passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
