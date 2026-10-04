#include "thieves_view.hpp"
#include "view_contract_test.hpp"
#include <filesystem>
#include <iostream>

namespace {
void gameplay_contract(const std::string& previews) {
    kit::contract_isolate_saves("catchingthieves-gameplay");
    const ct::Options options{.hosted = true, .dev = true};
    std::string history;
    {
        std::shared_ptr<ct::ThievesView> view = gui_forms::make_control<ct::ThievesView>(gui_forms::StableId("ct.contract"), options);
        (*view).set_cabinet(true, false, false, true);
        gui_forms::Window window(view, {600, 320});
        window.set_active(true); window.perform_layout();
        std::shared_ptr<gui_forms::LiveSurface> surface;
        gui_forms::LiveSurfaceFrame frame = kit::contract_next_frame(window, surface, 0, "minimum-size gameplay");
        frame = {};
        kit::contract_require((*view).campaign_size() == 243, "hosted view loads the complete campaign");
        kit::contract_require((*view).controls_fit(), "minimum-size board controls fit");
        (*view).run_command("map");
        kit::contract_require((*view).controls_fit(), "garden book controls fit at 600 x 320");
        (*view).run_command("map");
        kit::contract_require((*view).scripted_action("lvl0"), "dev harness selects campaign fixture");
        kit::contract_require((*view).scripted_action("sol"), "dev harness replays recorded solution");
        kit::contract_require((*view).solved() && !(*view).move_history().empty(), "reduced motion executes complete solution immediately");
        ct::SaveData completed;
        kit::contract_require(ct::load_save(ct::save_path(true), completed) && completed.records.count(0) == 1,
                              "winning is persisted immediately with its best result");
        (*view).run_command("new");
        kit::contract_require((*view).current_level() == 1 && !(*view).solved(), "New garden advances to a fresh game");
        ct::SaveData current;
        kit::contract_require(ct::load_save(ct::save_path(true), current) && current.level == 1 && current.history.empty(),
                              "new game immediately replaces current autosave while preserving records");
        (*view).scripted_action("sol");
        history = (*view).move_history();
        if (!previews.empty()) {
            (*view).scripted_action("lvl40");
            frame=kit::contract_next_frame(window,surface,(*view).published_frames(),"later campaign garden");
            kit::contract_write_frame(frame,previews,"catchingthieves-garden41-600x320.ppm"); frame={};
            (*view).run_command("map");
            frame=kit::contract_next_frame(window,surface,(*view).published_frames(),"garden book");
            kit::contract_write_frame(frame,previews,"catchingthieves-book-600x320.ppm"); frame={};
            // Return to the persisted fixture used by the resume assertions.
            (*view).scripted_action("lvl1"); (*view).scripted_action("sol");
        }
        (*view).set_cabinet(false, false, false, true);
        const std::uint64_t callbacks = (*view).timer_callbacks(), published = (*view).published_frames();
        kit::contract_require(!(*view).scripted_action("lvl9"), "hidden game rejects test input");
        static_cast<void>(window.poll_frame_schedule(gui_forms::FrameClock::now() + std::chrono::seconds(2)));
        kit::contract_require((*view).timer_callbacks() == callbacks && (*view).published_frames() == published,
                              "hidden view schedules no timer or presentation");
    }
    {
        std::shared_ptr<ct::ThievesView> resumed = gui_forms::make_control<ct::ThievesView>(gui_forms::StableId("ct.resumed"), options);
        kit::contract_require((*resumed).current_level() == 1 && (*resumed).move_history() == history && (*resumed).solved(),
                              "reopening restores completed position exactly");
        ct::SaveData stored;
        kit::contract_require(ct::load_save(ct::save_path(true), stored) && stored.records.size() == 2,
                              "reopening cannot award a duplicate result");
    }
    {
        std::shared_ptr<ct::ThievesView> production = gui_forms::make_control<ct::ThievesView>(gui_forms::StableId("ct.production"), ct::Options{.hosted = true});
        (*production).set_cabinet(true, false, false, true);
        kit::contract_require(!(*production).scripted_action("sol"), "production view refuses dev scripts");
    }
    std::cout << "Catching Thieves gameplay: full campaign, hosted commands, exact autosave/reopen and hidden silence pass.\n";
}
}
int main(int argc, char** argv) {
    kit::ContractOptions contract;
    // The general contract uses reduced motion and must settle completely even
    // though normal play deliberately retains the raccoons and garden weather.
    contract.settles_when_untouched = true;
    if (argc > 1) contract.preview_directory = argv[1];
    const int result = kit::run_view_contract<ct::ThievesView, ct::Options>(
        "catchingthieves", ct::Options{.hosted = true, .dev = true}, contract);
    if (result != 0) return result;
    try { gameplay_contract(contract.preview_directory); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
