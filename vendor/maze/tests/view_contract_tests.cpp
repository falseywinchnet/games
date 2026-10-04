#include "maze_view.hpp"
#include "view_contract_test.hpp"
#include <iostream>
int main(int argc,char** argv) {
    kit::ContractOptions options;
    // Marble, snail and encounters are live game actors. They continue while
    // the maze is open; hidden views still must stop all callbacks and frames.
    options.settles_when_untouched=false;
    if(argc>1) options.preview_directory=argv[1];
    const int result=kit::run_view_contract<mz::MazeView,mz::Options>(
        "maze",mz::Options{.hosted=true,.dev=true},options);
    if(result) return result;
    try {
        kit::contract_isolate_saves("maze-input");
        std::shared_ptr<mz::MazeView> view=gui_forms::make_control<mz::MazeView>(
            gui_forms::StableId("maze.input"),mz::Options{.hosted=true,.dev=true});
        (*view).set_cabinet(true,false,false,true);
        gui_forms::Window window(view,{600,320});
        window.set_active(true); window.perform_layout();
        std::shared_ptr<gui_forms::LiveSurface> surface;
        gui_forms::LiveSurfaceFrame frame=kit::contract_next_frame(window,surface,0,"minimum-size maze");
        frame={};
        kit::contract_require((*view).controls_fit(),"briefing fits minimum surface");
        (*view).run_command("trophies");
        kit::contract_require((*view).controls_fit(),"all trophy-page controls fit");
        (*view).run_command("credits");
        kit::contract_require((*view).controls_fit(),"credits controls fit");
        kit::contract_require((*view).scripted_action("lvl14"),"test selects flip/elevator maze");
        kit::contract_require((*view).current_level()==14,"maze selector preserves requested level");
        mz::SaveData save;
        kit::contract_require(mz::load_save(mz::save_path(true),save) && save.level==14,
                              "level commits immediately before returning to the shelf");
        if (!options.preview_directory.empty()) {
            (*view).scripted_action("ok");
            frame=kit::contract_next_frame(window,surface,(*view).published_frames(),"level fourteen");
            kit::contract_write_frame(frame,options.preview_directory,"maze-level14-600x320.ppm"); frame={};
            (*view).run_command("trophies");
            frame=kit::contract_next_frame(window,surface,(*view).published_frames(),"trophy page");
            kit::contract_write_frame(frame,options.preview_directory,"maze-trophies-600x320.ppm"); frame={};
        }
        (*view).set_cabinet(false,false,false,true);
        kit::contract_require(!(*view).scripted_action("lvl2"),"hidden game refuses scripted mutation");
        const std::uint64_t callbacks=(*view).timer_callbacks();
        static_cast<void>(window.poll_frame_schedule(gui_forms::FrameClock::now()+std::chrono::seconds(2)));
        kit::contract_require((*view).timer_callbacks()==callbacks,"hidden maze has no callbacks");
        std::shared_ptr<mz::MazeView> production=gui_forms::make_control<mz::MazeView>(
            gui_forms::StableId("maze.production"),mz::Options{});
        kit::contract_require(!(*production).scripted_action("lvl14"),"production view refuses test scripts");
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
    return 0;
}
