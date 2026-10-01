#include "collection.hpp"
#include "gui_forms/window.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <set>
#include <unistd.h>
namespace gf = gui_forms;
using namespace games;
class NullPainter final : public gf::Painter {
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
class OffsetHost final : public gf::Control {
  public:
    OffsetHost(gf::StableId id, std::shared_ptr<gf::Control> view)
        : Control(std::move(id)), view_(std::move(view)) {}
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree() {
        add_child(view_);
    }
    void arrange(gf::Rect b) override {
        arrange_self(b);
        set_child_layout(view_, {70, 90, b.width - 140, b.height - 180});
    }

  private:
    std::shared_ptr<gf::Control> view_;
};
static void point(gf::Window& window, gf::Control& view, gf::PointerAction action, gf::Point local,
                  gf::PointerButton button = gf::PointerButton::primary) {
    gf::PointerEvent e;
    e.action = action;
    e.button = button;
    e.position = view.point_to_window(local);
    static_cast<void>(window.dispatch_pointer(e));
}
static void click(gf::Window& window, gf::Control& view, gf::Point local,
                  gf::PointerButton button = gf::PointerButton::primary) {
    point(window, view, gf::PointerAction::down, local, button);
    point(window, view, gf::PointerAction::up, local, button);
}
static std::shared_ptr<gf::Button> button(gf::Control& root, const std::string& id) {
    for (const std::shared_ptr<gf::Control>& child : root.children())
        if ((*child).stable_id().value() == id)
            return std::dynamic_pointer_cast<gf::Button>(child);
    return {};
}
int main() {
    std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / ("games-nested-ui-" + std::to_string(getpid()));
    setenv("GAMES_STATE_DIR", scratch.c_str(), 1);
    Cabinet preferences;
    for (int i = 0; i < 4; ++i) {
        preferences.games[i].deal(static_cast<Kind>(i), 42);
        preferences.started[i] = true;
    }
    preferences.sound = false;
    preferences.music = false;
    preferences.reduced = true;
    assert(save_cabinet(cabinet_path(), preferences));
    NullPainter painter;
    {
        std::shared_ptr<Table> view = gf::make_control<Table>(gf::StableId("table"));
        (*view).game.deal(Kind::freecell, 42);
        std::shared_ptr<OffsetHost> host =
            gf::make_control<OffsetHost>(gf::StableId("offset"), view);
        gf::Window window(host, {1320, 980});
        window.perform_layout();
        (*view).activate();
        int card = (*view).game.state.piles[0].back().id;
        point(window, *view, gf::PointerAction::down, {80, 500});
        point(window, *view, gf::PointerAction::move, {80, 180});
        point(window, *view, gf::PointerAction::up, {80, 180});
        assert((*view).game.state.piles[16].size() == 1 &&
               (*view).game.state.piles[16].back().id == card);
    }
    {
        std::shared_ptr<SudokuView> view = gf::make_control<SudokuView>(gf::StableId("sudoku"));
        Sudoku fixture;
        fixture.seed = "nested-click";
        for (int y = 0; y < 9; ++y)
            for (int x = 0; x < 9; ++x)
                fixture.solution[y * 9 + x] = 1 + (x + y * 3 + y / 3 + 4) % 9;
        fixture.puzzle = fixture.solution;
        fixture.grid.values = fixture.solution;
        fixture.puzzle[0] = fixture.grid.values[0] = 0;
        (*view).game = fixture;
        std::shared_ptr<OffsetHost> host =
            gf::make_control<OffsetHost>(gf::StableId("offset"), view);
        gf::Window window(host, {1320, 980});
        window.perform_layout();
        (*view).activate();
        click(window, *view, {342, 122}, gf::PointerButton::secondary);
        assert((*view).game.grid.notes[0] == 1 && (*view).game.errors == 0);
        click(window, *view, {342, 122});
        assert((*view).game.grid.values[0] == 1 && (*view).game.errors == 1);
        assert((*button(*view, "sudoku.1")).perform_click());
        assert((*view).game.grid.values[0] == 0 && (*view).game.grid.notes[0] == 1 &&
               (*view).game.errors == 1);
        Sudoku restored;
        assert(restored.load(cabinet_path().parent_path() / "sudoku-v1.txt") &&
               restored.errors == 1 && restored.grid.notes[0] == 1);
    }
    for (int kind = 0; kind < 8; ++kind) {
        std::shared_ptr<PuzzleView> view =
            gf::make_control<PuzzleView>(gf::StableId("puzzle"), static_cast<PuzzleKind>(kind));
        (*view).game.deal(42);
        std::shared_ptr<OffsetHost> host =
            gf::make_control<OffsetHost>(gf::StableId("offset"), view);
        gf::Window window(host, {1320, 980});
        window.perform_layout();
        (*view).activate();
        static_cast<void>(window.paint(painter));
        PuzzleGame& game = (*view).game;
        if (game.kind == PuzzleKind::gems) {
            std::array<int, 96> before = game.state.grid;
            int a = 0, b = 1;
            bool found = false;
            for (int i = 0; i < 63 && !found; ++i) {
                if (i % 8 == 7)
                    continue;
                PuzzleGame candidate = game;
                if (!candidate.gem_swap(i, i + 1)) {
                    a = i;
                    b = i + 1;
                    found = true;
                }
            }
            assert(found);
            point(window, *view, gf::PointerAction::down,
                  {316 + (a % 8 + .5) * 68.5, 86 + (a / 8 + .5) * 68.5});
            point(window, *view, gf::PointerAction::up,
                  {316 + (b % 8 + .5) * 68.5, 86 + (b / 8 + .5) * 68.5});
            assert(game.state.grid == before && game.state.moves == 0 &&
                   game.message.find("does not") != std::string::npos);
        }
        if (game.kind == PuzzleKind::pegs) {
            static_cast<void>(window.request_focus(view));
            static_cast<void>(window.dispatch_text({"a"}));
            static_cast<void>(window.dispatch_text({"a"}));
            PuzzleGame restored(game.kind);
            assert(restored.load(cabinet_path().parent_path() / "four_pegs-v1.txt"));
            assert(restored.state.marks[0] == 1 && restored.state.marks[1] == 1);
            static_cast<void>(window.dispatch_text({"b"}));
            static_cast<void>(window.dispatch_text({"c"}));
            assert((*button(*view, "four_pegs.action.3")).perform_click());
            assert(game.state.stage == 1 && game.state.grid[0] == 1 && game.state.grid[1] == 1 &&
                   game.state.grid[2] == 2 && game.state.grid[3] == 3);
        }
        if (game.kind == PuzzleKind::atom) {
            click(window, *view, {316 + 548 / 6.0 * 1.5, 86 + 548 / 6.0 * .5});
            assert(game.state.moves == 1 && game.state.aux[0] != -99);
            click(window, *view, {316 + 548 / 6.0 * 1.5, 86 + 548 / 6.0 * 1.5});
            assert(game.state.grid[0] == 1);
        }
        if (game.kind == PuzzleKind::solve) {
            click(window, *view, {326, 96});
            int occupied = 0;
            for (int i = 0; i < 64; ++i)
                occupied += game.state.grid[i] != 0;
            assert(occupied == static_cast<int>(game.piece_cells(0, 0, false).size()) &&
                   game.state.moves == 1);
        }
        if (game.kind == PuzzleKind::sticks) {
            click(window, *view, {316 + 274, 86 + 274});
            assert(game.state.grid[18] == 1);
        }
        assert(game.invariant());
    }
    {
        PuzzleGame cube(PuzzleKind::cube);
        cube.deal(42);
        PuzzleRaster raster;
        raster.resize(300, 300);
        for (double yaw : {.4, .75, 1.1})
            for (double pitch : {-.78, -.56, -.34}) {
                raster.cube(cube, yaw, pitch, -1);
                std::set<int> faces, cells;
                for (int cell : raster.ids)
                    if (cell >= 0) {
                        assert(PuzzleGame::cube_playable(cell));
                        faces.insert(cell / 16);
                        cells.insert(cell);
                    }
                assert((faces == std::set<int>{0, 3, 4}));
                assert(cells.size() == 48);
            }
    }
    {
        auto collection = gf::make_control<Collection>(gf::StableId("collection"));
        gf::Window window(collection, {1180, 800});
        window.perform_layout();
        std::shared_ptr<gf::Control> library, eggy;
        for (const auto& c : collection->children()) {
            if (c->stable_id().value() == "collection.library")
                library = c;
            if (c->stable_id().value() == "collection.eggy")
                eggy = c;
        }
        assert(library && eggy && library->visible() && !eggy->visible());
        assert(!std::filesystem::exists(scratch / "eggy-v1.txt"));
        assert(button(*library, "collection.9")->perform_click());
        assert(library->visible() && !eggy->visible()); // selection is separate from opening
        assert(button(*library, "collection.open")->perform_click());
        assert(!library->visible() && eggy->visible());
        assert(!std::filesystem::exists(scratch / "eggy-v1.txt"));
        click(window, *eggy, {200, 200}); // Begin the first climb, using nested coordinates.
        assert(button(*collection, "collection.command.0")->perform_click());
        assert(library->visible() && !eggy->visible());
        eggy::SaveData restored;
        assert(eggy::load_save(scratch / "eggy-v1.txt", restored));
        assert(button(*library, "collection.0")->perform_click());
        assert(button(*library, "collection.0")->selected());
        assert(button(*library, "collection.category.3")->perform_click());
        window.perform_layout();
        assert(button(*library, "collection.9")->visible());
        assert(!button(*library, "collection.0")->visible());
        assert(button(*library, "collection.9")->selected());
        assert(button(*library, "collection.category.0")->perform_click());
        window.perform_layout();
        assert(button(*library, "collection.0")->visible());
    }
    std::filesystem::remove_all(scratch);
    std::cout << "Nested card drag, Sudoku note/error/undo, Gems rejection, peg draft resume, "
                 "probes and placement passed.\n";
}
