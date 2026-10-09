#include "maze_view.hpp"
#include "view_contract_test.hpp"
#include <iostream>
int main(int argc,char** argv) {
    kit::ContractOptions options;
    // The briefing is static; it must settle even though live maze actors move.
    options.settles_when_untouched=true;
    options.settle_seconds=1;
    if(argc>1) options.preview_directory=argv[1];
    const int result=kit::run_view_contract<mz::MazeView,mz::Options>(
        "maze",mz::Options{.hosted=true,.dev=true},options);
    if(result) return result;
    try {
        kit::contract_isolate_saves("maze-input");
        mz::SaveData fixture;
        fixture.run_seed=12345;
        kit::contract_require(mz::write_save(mz::save_path(true),fixture),"save deterministic idle fixture");
        std::shared_ptr<mz::MazeView> view=gui_forms::make_control<mz::MazeView>(
            gui_forms::StableId("maze.input"),mz::Options{.hosted=true,.dev=true});
        (*view).set_cabinet(true,false,false,true);
        gui_forms::Window window(view,{600,320});
        window.set_active(true); window.perform_layout();
        std::shared_ptr<gui_forms::LiveSurface> surface;
        gui_forms::LiveSurfaceFrame frame=kit::contract_next_frame(window,surface,0,"minimum-size maze");
        frame={};
        kit::contract_require((*view).controls_fit(),"briefing fits minimum surface");
        kit::contract_require(kit::contract_settle(window,.2),"static briefing stops its timer");
        (*view).scripted_action("ok");
        (*view).scripted_action("r");
        (*view).scripted_action("r");
        frame=kit::contract_next_frame(window,surface,(*view).published_frames(),"turn toward quiet wall");
        frame={};
        kit::contract_require(kit::contract_settle(window,1),"untouched live corridor sleeps until its encounter");
        const std::uint64_t quiet_frames=(*view).published_frames();
        const std::uint64_t quiet_callbacks=(*view).timer_callbacks();
        const gui_forms::FrameTime quiet_until=gui_forms::FrameClock::now()+std::chrono::milliseconds(200);
        while (gui_forms::FrameClock::now()<quiet_until) {
            static_cast<void>(window.poll_frame_schedule(gui_forms::FrameClock::now()));
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        kit::contract_require((*view).published_frames()==quiet_frames && (*view).timer_callbacks()==quiet_callbacks,
                              "quiet live maze neither redraws nor polls");
        (*view).scripted_action("l");
        frame=kit::contract_next_frame(window,surface,quiet_frames,"input wakes sleeping maze");
        frame={};
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
