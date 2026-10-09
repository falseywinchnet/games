#include "idle.hpp"
#include "session.hpp"
#include "rewards.hpp"
#include "cast.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
void render(mz::Soft3D& r,const mz::Session& s,double time) {
    s.camera(r); r.time=time; r.begin(0); mz::draw_world(r,s.lv,s.world,time);
}
}
int main() {
    try {
        mz::Soft3D r; r.resize(96,72); r.begin(0);
        require(mz::visible_box(r,{-.2,3,-.2},{.2,3.5,.2}),"unoccluded bound is visible");
        std::fill(r.zinv.begin(),r.zinv.end(),.5f);
        require(!mz::visible_box(r,{-.2,3,-.2},{.2,3.5,.2}),"opaque wall occludes distant animation");
        require(!mz::visible_box(r,{-.2,-3,-.2},{.2,-2,.2}),"behind-camera bound is invisible");
        require(!mz::visible_box(r,{30,3,-.2},{31,4,.2}),"offscreen bound is invisible");
        require(mz::visible_box(r,{-.2,-.1,-.2},{.2,.1,.2}),"near-plane crossing stays conservative");
        r.zinv[36*96+48]=0;
        require(mz::visible_box(r,{-.2,3,-.2},{.2,3.5,.2}),"one visible pixel prevents culling");
        int skipped=0,animated=0;
        for (int level:{1,5,14}) {
            mz::Session s;
            s.reward_tex=&mz::reward_tex(0); s.reward_name="cheese";
            const mz::CastMember& member=mz::cast_member(0);
            s.cast.push_back({member.name,&mz::cast_tex(0),{"A timed visitor."},member.w,member.h});
            s.start(mz::generate(mz::params_for(level,12345)),9876);
            if (level==1) require(s.next_update_delay()>1,"quiet maze sleeps to timed encounter");
            mz::Soft3D cached,reference; cached.resize(96,72); reference.resize(96,72);
            double time=0; render(cached,s,time);
            for (int frame=0;frame<900;++frame) {
                if (frame>300 && frame%24==0 && s.won_t<0) {
                    const std::vector<int> route=mz::solve_route(s.lv,s.play);
                    if (!route.empty()) {
                        const int dir=route.front();
                        s.command(dir==s.play.dir ? mz::Cmd::forward : dir==(s.play.dir+1)%4 ? mz::Cmd::right : mz::Cmd::left);
                    }
                }
                const bool before=s.camera_animating() || mz::visible_world_animation(cached,s.lv,s.world);
                s.update(.033); time+=.033;
                bool draw=before || s.camera_animating() || mz::visible_world_animation(cached,s.lv,s.world);
                for (const mz::Pos& cell:s.repainted)
                    if (cell.f==s.world.floor && mz::visible_box(cached,{double(cell.x),double(cell.y),double(cell.f)},
                        {cell.x+1.0,cell.y+1.0,cell.f+1.0})) draw=true;
                render(reference,s,time);
                if (draw) { render(cached,s,time); ++animated; }
                else ++skipped;
                require(cached.color==reference.color,"demand rendering matches full rendering pixel for pixel");
            }
        }
        require(skipped>200,"stationary corridors actually reuse their pixels");
        require(animated>200,"movement and visible animation still render");
        std::cout<<"Maze demand renderer: "<<skipped<<" reused and "<<animated<<" animated frames match full rendering exactly.\n";
        mz::Session deadline;
        deadline.start(mz::generate(mz::params_for(1,12345)),9876);
        require(std::isinf(deadline.next_update_delay()),"no actors or pending events need no timer");
        deadline.cast.push_back({"Visitor",&mz::cast_tex(0),{"Still on time."},.7,.9});
        const double due=deadline.next_update_delay();
        require(due>=10 && due<=20,"encounter keeps original deadline");
        deadline.update(due+.001);
        require(deadline.world.visitor.alive || deadline.next_update_delay()<=3.001,"encounter wakes at deadline even without frame polling");
        deadline.command(mz::Cmd::right);
        require(deadline.next_update_delay()<=.033,"input wakes an idle session immediately");
        bool met_visitor=false;
        for (int seed=1;seed<=16 && !met_visitor;++seed) {
            mz::Session encounter;
            encounter.cast.push_back({"Visitor",&mz::cast_tex(0),{"Still on time."},.7,.9});
            encounter.start(mz::generate(mz::params_for(1,seed)),9876);
            encounter.update(encounter.next_update_delay()+.001);
            if (encounter.world.visitor.alive) {
                met_visitor=true;
                require(!encounter.speech.empty() && encounter.speech.front().age==0,
                        "deadline wake does not age a newly arrived visitor's speech");
            }
        }
        require(met_visitor,"deadline regression reaches a real visitor encounter");
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
    return 0;
}
