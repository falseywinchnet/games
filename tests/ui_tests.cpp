#include "gui_forms/window.hpp"
#include "table.hpp"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include "test_paths.hpp"
namespace gf = gui_forms;
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void pointer(gf::Window& window, gf::PointerAction action, gf::Point point) {
    gf::PointerEvent event;
    event.action = action;
    event.button = gf::PointerButton::primary;
    event.position = point;
    static_cast<void>(window.dispatch_pointer(event));
}
int main() {
    std::filesystem::path scratch =
        games_test::scratch_directory("games-ui-test-");
    games_test::isolate_saves(scratch);
    std::shared_ptr<games::Table> table =
        gf::make_control<games::Table>(gf::StableId("test.table"));
    (*table).game.deal(games::Kind::freecell, 42);
    std::unique_ptr<gf::Window> window = std::make_unique<gf::Window>(table, gf::Size{1180, 800});
    (*window).perform_layout();
    // The exposed card in column zero goes to the first empty free cell.
    int id = (*table).game.state.piles[0].back().id;
    pointer(*window, gf::PointerAction::down, {80, 500});
    pointer(*window, gf::PointerAction::move, {80, 180});
    pointer(*window, gf::PointerAction::up, {80, 180});
    require((*table).game.state.piles[16].size() == 1, "native routing: drop onto free cell");
    require((*table).game.state.piles[16].back().id == id, "native routing: correct dragged card");
    require((*table).game.state.moves == 1, "native routing: exactly one move");
    require(!(*table).has_pointer_capture(), "native routing: released pointer capture");
    require((*table).game.invariant(), "native routing: conservation");
    require((*table).game.undo(), "native routing: undo");
    (*table).arrange({0, 0, 1180, 800});
    int other = 1;
    int first_index = static_cast<int>((*table).game.state.piles[0].size()) - 1;
    while (other < 8 && (*table).game.legal({0, first_index, other}))
        ++other;
    require(other < 8, "fixture has another card that is not a legal destination");
    int other_id = (*table).game.state.piles[other].back().id;
    pointer(*window, gf::PointerAction::down, {80, 500});
    pointer(*window, gf::PointerAction::up, {80, 500});
    pointer(*window, gf::PointerAction::down, {80 + other * 140.0, 500});
    pointer(*window, gf::PointerAction::move, {80, 180});
    pointer(*window, gf::PointerAction::up, {80, 180});
    require((*table).game.state.piles[16].size() == 1 &&
                (*table).game.state.piles[16].back().id == other_id,
            "newly clicked card drags immediately even when another card was selected");
    require((*table).game.undo(), "undo first-press drag fixture");
    // Give column zero a black six and column one a red seven, retaining every card.
    auto expose = [&](int rank, int suit, int destination) {
        for (auto& pile : (*table).game.state.piles)
            for (auto& card : pile)
                if (card.rank == rank && card.suit == suit) {
                    std::swap(card, (*table).game.state.piles[destination].back());
                    return;
                }
    };
    expose(6, 0, 0);
    expose(7, 1, 1);
    (*table).arrange({0, 0, 1180, 800});
    int from = -1, destination = -1;
    for (int a = 0; a < 8 && from < 0; ++a)
        for (int b = 0; b < 8; ++b)
            if (a != b && (*table).game.legal(
                              {a, static_cast<int>((*table).game.state.piles[a].size()) - 1, b})) {
                from = a;
                destination = b;
                break;
            }
    require(from >= 0, "fixture has a legal click destination");
    int destination_id = (*table).game.state.piles[destination].back().id;
    pointer(*window, gf::PointerAction::down, {80 + from * 140.0, 500});
    pointer(*window, gf::PointerAction::up, {80 + from * 140.0, 500});
    pointer(*window, gf::PointerAction::down, {80 + destination * 140.0, 500});
    require((*table).game.state.moves == 0,
            "click-to-place waits until release so the new card can drag");
    pointer(*window, gf::PointerAction::move, {80, 180});
    pointer(*window, gf::PointerAction::up, {80, 180});
    require((*table).game.state.piles[16].back().id == destination_id,
            "drag selects the pressed card even if it was a legal destination");
    require((*table).game.undo(), "undo legal-destination drag fixture");

    (*table).arrange({0, 0, 1180, 800});
    require((*window).request_focus(table), "keyboard table focus");
    int before_moves = (*table).game.state.moves;
    require((*table).game.hint().from >= 0, "keyboard fixture has hint");
    static_cast<void>((*window).dispatch_key({gf::KeyAction::down, gf::PhysicalKey::h}));
    static_cast<void>((*window).dispatch_key({gf::KeyAction::down, gf::PhysicalKey::enter}));
    require((*table).game.state.moves == before_moves + 1,
            "keyboard H and Enter commits suggested legal move");
    (*table).game.deal(games::Kind::solitaire, 42);
    (*table).arrange({0, 0, 1180, 800});
    std::size_t stock = (*table).game.state.piles[14].size();
    pointer(*window, gf::PointerAction::down, {100, 160});
    pointer(*window, gf::PointerAction::up, {100, 160});
    require((*table).game.state.piles[14].size() + 1 == stock, "native routing: draw stock");
    // Route a real last move through the UI, then record a named score exactly once.
    (*table).game.deal(games::Kind::freecell, 99);
    for (games::Pile& pile : (*table).game.state.piles)
        pile.clear();
    for (int suit = 0; suit < 4; ++suit)
        for (int rank = 1; rank <= 13; ++rank) {
            games::Card card{rank, suit, suit * 13 + rank - 1, true};
            (*table).game.state.piles[suit == 3 && rank == 13 ? 0 : 10 + suit].push_back(card);
        }
    (*table).arrange({0, 0, 1180, 800});
    static_cast<void>((*window).request_focus(table));
    static_cast<void>((*window).dispatch_key({gf::KeyAction::down, gf::PhysicalKey::h}));
    static_cast<void>((*window).dispatch_key({gf::KeyAction::down, gf::PhysicalKey::enter}));
    require((*table).game.state.over, "last legal move finishes the game");
    std::shared_ptr<gf::TextBox> name;
    for (const std::shared_ptr<gf::Control>& child : (*table).children())
        if ((*child).stable_id().value() == "score.name")
            name = std::dynamic_pointer_cast<gf::TextBox>(child);
    require(name && (*name).visible(), "completion opens named score entry");
    (*name).set_text("Routing Ace");
    pointer(*window, gf::PointerAction::down, {600, 254});
    pointer(*window, gf::PointerAction::up, {600, 254});
    games::Cabinet saved;
    require(games::load_cabinet(games::cabinet_path(), saved), "completed game autosaved");
    require(saved.result_recorded[2] && saved.top_scores[5].size() == 1 &&
                saved.top_scores[5][0].name == "Routing Ace",
            "named score saved");
    require(!(*name).visible(), "entry closes after submission");
    // Repeated clicks cannot create a duplicate score.
    pointer(*window, gf::PointerAction::down, {600, 254});
    pointer(*window, gf::PointerAction::up, {600, 254});
    require(games::load_cabinet(games::cabinet_path(), saved) && saved.top_scores[5].size() == 1,
            "duplicate score prevented");
    // New game from the result window immediately replaces the saved active table.
    pointer(*window, gf::PointerAction::down, {837, 597});
    pointer(*window, gf::PointerAction::up, {837, 597});
    require(games::load_cabinet(games::cabinet_path(), saved) && !saved.games[2].state.over &&
                !saved.result_recorded[2] && saved.games[2].state.seed != 99,
            "new game immediately saved; result latch reset");
    window.reset();
    table.reset();
    std::filesystem::remove_all(scratch);
    std::cout << "Passed: GUI.Forms routed drag/drop, capture release, stock click, conservation "
                 "and undo.\n";
}
