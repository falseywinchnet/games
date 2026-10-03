#include "collection.hpp"
#include "gui_forms/window.hpp"
#include "test_paths.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <set>
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
    std::filesystem::path scratch = games_test::scratch_directory("games-nested-ui-");
    games_test::isolate_saves(scratch);
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
        (*view).run_command("undo"); // Undo lives in the PlaySuite capsule.
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
                  {(*view).board().x + (a % 8 + .5) * (*view).board().width / 8,
                   (*view).board().y + (a / 8 + .5) * (*view).board().height / 8});
            point(window, *view, gf::PointerAction::up,
                  {(*view).board().x + (b % 8 + .5) * (*view).board().width / 8,
                   (*view).board().y + (b / 8 + .5) * (*view).board().height / 8});
            assert(game.state.grid == before && game.state.moves == 0 &&
                   game.message.find("does not") != std::string::npos);
            // A click picks a gem; a click on its neighbor swaps the two.
            const gf::Point at_a{(*view).board().x + (a % 8 + .5) * (*view).board().width / 8,
                                 (*view).board().y + (a / 8 + .5) * (*view).board().height / 8};
            const gf::Point at_b{(*view).board().x + (b % 8 + .5) * (*view).board().width / 8,
                                 (*view).board().y + (b / 8 + .5) * (*view).board().height / 8};
            game.message.clear();
            point(window, *view, gf::PointerAction::down, at_a);
            point(window, *view, gf::PointerAction::up, at_a);
            assert(game.message.empty());
            point(window, *view, gf::PointerAction::down, at_b);
            assert(game.state.grid == before && game.message.find("does not") != std::string::npos);
            point(window, *view, gf::PointerAction::up, at_b);
            // Dragging the gem itself most of a cell toward its neighbor commits the swap.
            game.message.clear();
            point(window, *view, gf::PointerAction::down, at_a);
            point(window, *view, gf::PointerAction::move, {at_a.x + 4, at_a.y});
            assert(game.message.empty());
            point(window, *view, gf::PointerAction::move,
                  {at_a.x + (*view).board().width / 8 * .8, at_a.y});
            assert(game.message.find("does not") != std::string::npos);
            point(window, *view, gf::PointerAction::up,
                  {at_a.x + (*view).board().width / 8 * .8, at_a.y});
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
            const gf::Rect board = (*view).board();
            click(window, *view,
                  {board.x + board.width / 6 * 1.5, board.y + board.height / 6 * .5});
            assert(game.state.moves == 1 && game.state.aux[0] != -99);
            click(window, *view,
                  {board.x + board.width / 6 * 1.5, board.y + board.height / 6 * 1.5});
            assert(game.state.grid[0] == 1);
        }
        if (game.kind == PuzzleKind::solve) {
            click(window, *view, {(*view).board().x + 10, (*view).board().y + 10});
            int occupied = 0;
            for (int i = 0; i < 64; ++i)
                occupied += game.state.grid[i] != 0;
            assert(occupied == static_cast<int>(game.piece_cells(0, 0, false).size()) &&
                   game.state.moves == 1);
        }
        if (game.kind == PuzzleKind::cube) {
            // Trace from a colored source onto a neighboring cell with real pointer events,
            // including a step that folds across the cube's edge.
            int source = -1, next = -1;
            for (int c = 0; c < 96 && next < 0; ++c) {
                if (!PuzzleGame::cube_playable(c) || !game.state.marks[c] ||
                    !(*view).cube_cell_point(c))
                    continue;
                for (int n = 0; n < 96; ++n)
                    if (PuzzleGame::cube_playable(n) && !game.state.marks[n] &&
                        PuzzleGame::cube_adjacent(c, n) && (*view).cube_cell_point(n)) {
                        source = c;
                        next = n;
                        break;
                    }
            }
            assert(source >= 0 && next >= 0);
            const gf::Point a = *(*view).cube_cell_point(source),
                            b = *(*view).cube_cell_point(next);
            point(window, *view, gf::PointerAction::down, a);
            for (int k = 1; k <= 8; ++k)
                point(window, *view, gf::PointerAction::move,
                      {a.x + (b.x - a.x) * k / 8, a.y + (b.y - a.y) * k / 8});
            point(window, *view, gf::PointerAction::up, b);
            const int pair = game.cube_pair(source);
            assert(pair > 0 && game.state.paths[pair - 1].size() == 2 &&
                   game.state.paths[pair - 1][1] == next);
        }
        if (game.kind == PuzzleKind::sticks) {
            click(window, *view,
                  {(*view).board().x + (*view).board().width / 2,
                   (*view).board().y + (*view).board().height / 2});
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
        std::shared_ptr<Collection> collection =
            gf::make_control<Collection>(gf::StableId("collection"));
        gf::Window window(collection, {1180, 800});
        window.perform_layout();
        struct FindChild {

            std::shared_ptr<gf::Control> operator()(gf::Control& root,
                                                    const std::string& id) const {
                for (const std::shared_ptr<gf::Control>& c : root.children())
                    if ((*c).stable_id().value() == id)
                        return c;
                return {};
            }
        };
        FindChild child{};
        std::shared_ptr<gf::Control> shelf = child(*collection, "collection.shelf"),
                                     capsule = child(*collection, "collection.capsule"),
                                     eggy = child(*collection, "collection.eggy"),
                                     cards = child(*collection, "collection.cards");
        assert(shelf && capsule && !eggy && !cards);
        assert((*collection).children().size() == 2);
        assert((*shelf).visible() && !(*capsule).visible());
        // PlaySuite lists all thirteen games in one place, with no categories.
        for (int i = 0; i < entry_count; ++i)
            assert(button(*shelf, "shelf.box." + std::to_string(i)) &&
                   (*button(*shelf, "shelf.box." + std::to_string(i))).visible());
        assert(!button(*shelf, "collection.category.0"));
        assert(!std::filesystem::exists(scratch / "eggy-v1.txt"));
        const std::string eggy_box = "shelf.box." + std::to_string(static_cast<int>(Entry::eggy));
        assert((*button(*shelf, eggy_box)).perform_click()); // one click opens the box
        assert((*button(*shelf, eggy_box)).selected());
        eggy = child(*collection, "collection.eggy");
        assert(eggy);
        window.perform_layout();
        assert(!(*shelf).visible() && (*eggy).visible() && (*capsule).visible());
        assert(!std::filesystem::exists(scratch / "eggy-v1.txt"));
        click(window, *eggy, {200, 200}); // Begin the first climb, using nested coordinates.
        assert((*button(*capsule, "capsule.back")).perform_click());
        assert((*shelf).visible() && !(*eggy).visible() && !(*capsule).visible());
        eggy::SaveData restored;
        assert(eggy::load_save(scratch / "eggy-v1.txt", restored));
        // Each card game is its own box and opens the shared table on that game.
        for (Entry e : {Entry::spider, Entry::freecell, Entry::hearts, Entry::solitaire}) {
            (*collection).open_entry(e);
            cards = child(*collection, "collection.cards");
            window.perform_layout();
            assert((*cards).visible() && !(*shelf).visible());
            assert(static_cast<int>((*std::static_pointer_cast<Table>(cards)).game.state.kind) ==
                   static_cast<int>(e));
            assert(button(*capsule, "capsule.cmd.new") && button(*capsule, "capsule.cmd.undo"));
            // Patience games offer Easy, Medium and Hard deals; Hearts has no levels.
            Table& table = *std::static_pointer_cast<Table>(cards);
            struct LevelLabel {
                const Table& table;
                std::string operator()() const {
                    for (const GameCommand& command : table.commands())
                        if (command.id == "level")
                            return command.label;
                    return "";
                }
            };
            const LevelLabel level{table};
            if (e == Entry::hearts)
                assert(level().empty());
            else {
                const std::string before = level();
                assert(before.rfind("Next: ", 0) == 0);
                table.run_command("level");
                assert(level() != before && level().rfind("Next: ", 0) == 0);
                table.run_command("level");
                table.run_command("level");
                assert(level() == before);
            }
            (*collection).show_shelf();
        }
        // Hovering opens the capsule; tests advance its opening directly.
        struct OpenCapsule {
            const std::shared_ptr<gf::Control>& capsule;
            const std::shared_ptr<Collection>& collection;
            gf::Window& window;
            void operator()() const {
                (*std::static_pointer_cast<CommandCapsule>(capsule)).step(1.0, true, true);
                (*collection).invalidate(gf::Dirty::layout);
                window.perform_layout();
            }
        };
        OpenCapsule open_capsule{capsule, collection, window};
        struct VisitGame {
            const FindChild& child;
            const std::shared_ptr<Collection>& collection;
            const std::shared_ptr<gf::Control>& shelf;
            gf::Window& window;
            std::shared_ptr<gf::Control> operator()(Entry e, const std::string& id) const {
                std::shared_ptr<gf::Control> view = child(*collection, id);
                assert(!view || !(*view).visible());
                const std::string box = "shelf.box." + std::to_string(static_cast<int>(e));
                assert((*button(*shelf, box)).perform_click());
                window.perform_layout();
                view = child(*collection, id);
                assert(view && !(*shelf).visible() && (*view).visible());
                return view;
            }
        };
        VisitGame visit{child, collection, shelf, window};
        std::shared_ptr<gf::Control> switchbox = visit(Entry::switchbox, "switchbox.view");
        assert((*button(*capsule, "capsule.back")).perform_click());
        assert((*shelf).visible() && !(*switchbox).visible());
        // The retired puzzle-view versions of Four Pegs and Atom Probe are never offered.
        assert(!child(*collection, "collection.puzzle.4") &&
               !child(*collection, "collection.puzzle.3"));
        std::shared_ptr<gf::Control> fourpegs = visit(Entry::pegs, "fourpegs.view");
        assert(!(*switchbox).visible());
        // Capsule commands reach hosted games: Help opens and closes the game's own panel.
        open_capsule();
        assert((*button(*capsule, "capsule.cmd.help")).perform_click());
        assert((*std::static_pointer_cast<fp::FourPegsView>(fourpegs)).host_panel() == "help");
        assert((*button(*capsule, "capsule.cmd.help")).perform_click());
        assert((*std::static_pointer_cast<fp::FourPegsView>(fourpegs)).host_panel().empty());
        assert((*button(*capsule, "capsule.back")).perform_click());
        assert((*shelf).visible() && !(*fourpegs).visible());
        std::shared_ptr<gf::Control> atomprobe = visit(Entry::atom, "atomprobe.view");
        assert(!(*fourpegs).visible());
        open_capsule();
        assert((*button(*capsule, "capsule.cmd.scores")).perform_click());
        assert((*std::static_pointer_cast<ap::AtomProbeView>(atomprobe)).host_panel() == "scores");
        assert((*button(*capsule, "capsule.back")).perform_click());
        assert((*shelf).visible() && !(*atomprobe).visible());
        for (Entry entry : {Entry::koikoi, Entry::parrots, Entry::liarsdice, Entry::penthesheep, Entry::rockstack}) {
            (*collection).open_entry(entry);
            window.perform_layout();
            open_capsule();
            const std::string id = entry == Entry::koikoi ? "rules" : "help";
            assert((*button(*capsule, "capsule.cmd." + id)).perform_click());
            CommandSource* source = nullptr;
            for (const std::shared_ptr<gf::Control>& candidate : (*collection).children())
                if ((*candidate).visible()) {
                    CommandSource* match = dynamic_cast<CommandSource*>(candidate.get());
                    if (match)
                        source = match;
                }
            assert(source);
            bool checked = false;
            for (const GameCommand& command : (*source).commands())
                if (command.id == id)
                    checked = command.checked;
            assert(checked);
            assert((*button(*capsule, "capsule.cmd." + id)).perform_click());
            for (const GameCommand& command : (*source).commands())
                if (command.id == id)
                    assert(!command.checked);
            assert((*button(*capsule, "capsule.back")).perform_click());
        }
        (*collection).open_entry(Entry::atom);
        (*collection).show_shelf();
    }
    {
        // Reopening resumes where the player left: in a game, or on the shelf.
        std::shared_ptr<Collection> collection =
            gf::make_control<Collection>(gf::StableId("collection.resume"));
        assert((*collection).shelf_open() && (*collection).active() == Entry::atom);
        (*collection).open_entry(Entry::gems);
    }
    {
        std::shared_ptr<Collection> collection =
            gf::make_control<Collection>(gf::StableId("collection.resume2"));
        assert(!(*collection).shelf_open() && (*collection).active() == Entry::gems);
    }
    {
        // Every game must keep its commands usable at the supported minimum size.
        std::shared_ptr<Collection> collection =
            gf::make_control<Collection>(gf::StableId("collection.small"));
        gf::Window window(collection, {600, 420});
        std::shared_ptr<CommandCapsule> capsule;
        for (const std::shared_ptr<gf::Control>& child : (*collection).children())
            if ((*child).stable_id().value() == "collection.capsule")
                capsule = std::static_pointer_cast<CommandCapsule>(child);
        assert(capsule);
        for (int i = 0; i < entry_count; ++i) {
            (*collection).open_entry(static_cast<Entry>(i));
            window.perform_layout();
            // Invoke through hit-testing, not perform_click(): a game painted
            // above the capsule must not steal its Back button's pointer input.
            const std::shared_ptr<gf::Button> back = button(*capsule, "capsule.back");
            assert(back && (*back).visible());
            gf::Rect back_bounds = (*back).client_rectangle();
            click(window, *back, {back_bounds.width * .5, back_bounds.height * .5});
            assert((*collection).shelf_open());
            (*collection).open_entry(static_cast<Entry>(i));
            window.perform_layout();
            const gf::Point folded_back = (*back).point_to_window({0, 0});
            (*capsule).step(1.0, true, true);
            (*collection).invalidate(gf::Dirty::layout);
            window.perform_layout();
            const gf::Point expanded_back = (*back).point_to_window({0, 0});
            assert(folded_back.x == expanded_back.x && folded_back.y == expanded_back.y);
            const gf::Rect bounds = (*capsule).client_rectangle();
            for (const std::shared_ptr<gf::Control>& child : (*capsule).children()) {
                assert((*child).visible());
                const gf::Rect rect = (*child).client_rectangle();
                const gf::Point bottom = (*child).point_to_window({rect.width, rect.height});
                const gf::Point top = (*child).point_to_window({0, 0});
                const gf::Point parent_bottom =
                    (*capsule).point_to_window({bounds.width, bounds.height});
                assert(top.x >= 0 && top.y >= 0 && bottom.x <= 600 && bottom.y <= 420);
                assert(bottom.y <= parent_bottom.y);
            }
            back_bounds = (*back).client_rectangle();
            click(window, *back, {back_bounds.width * .5, back_bounds.height * .5});
            assert((*collection).shelf_open());
        }
    }
    std::filesystem::remove_all(scratch);
    std::cout << "Nested card drag, Sudoku note/error/undo, Gems rejection, peg draft resume, "
                 "probes and placement passed.\n";
}
