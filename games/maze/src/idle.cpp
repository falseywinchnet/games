#include "idle.hpp"
#include "textures.hpp"
#include <algorithm>
#include <limits>

namespace mz {
namespace { constexpr double kH = 1.0; }
bool visible_box(const Soft3D& r, V3 lo, V3 hi) {
    if (r.zinv.empty()) return true;
    double nearest = std::numeric_limits<double>::infinity(), farthest = -nearest;
    double left = nearest, top = nearest, right = -nearest, bottom = -nearest;
    for (int corner = 0; corner < 8; ++corner) {
        const V3 p{corner & 1 ? hi.x : lo.x, corner & 2 ? hi.y : lo.y, corner & 4 ? hi.z : lo.z};
        const double depth = dot(p-r.eye,r.fwd());
        nearest = std::min(nearest,depth); farthest = std::max(farthest,depth);
        double x = 0, y = 0, z = 0;
        if (r.project(p,x,y,z)) {
            left = std::min(left,x); right = std::max(right,x);
            top = std::min(top,y); bottom = std::max(bottom,y);
        }
    }
    if (farthest < r.near_z) return false;
    if (nearest <= r.near_z) return true;
    if (right < 0 || left >= r.W || bottom < 0 || top >= r.H) return false;
    const int x0 = std::max(0,static_cast<int>(std::floor(left))-1);
    const int x1 = std::min(r.W-1,static_cast<int>(std::ceil(right))+1);
    const int y0 = std::max(0,static_cast<int>(std::floor(top))-1);
    const int y1 = std::min(r.H-1,static_cast<int>(std::ceil(bottom))+1);
    const double front = 1 / nearest + 1e-5;
    for (int y=y0;y<=y1;++y)
        for (int x=x0;x<=x1;++x)
            if (r.zinv[static_cast<std::size_t>(y)*r.W+x] <= front) return true;
    return false;
}
namespace {
bool bound(const Soft3D& r,V3 center,V3 radius) { return visible_box(r,center-radius,center+radius); }
}
bool visible_world_animation(const Soft3D& r,const Level& lv,const WorldState& s) {
    if (s.elevator_lift >= 0) return true;
    for (const Thing& thing:lv.things) {
        if (thing.at.f != s.floor) continue;
        // Includes the full bob, rotation and bulb glow, not just the sprite.
        if (bound(r,cell_center(thing.at,.5),{.5,.5,.55})) return true;
    }
    if (lv.goal.f == s.floor && s.reward && bound(r,cell_center(lv.goal,.6),{.31,.31,.34})) return true;
    if (s.marble.alive && s.marble.at.f == s.floor && bound(r,s.marble.world()+V3{0,0,.42},{.43,.43,.43})) return true;
    if (s.snail.alive && s.snail.at.f == s.floor && bound(r,s.snail.world()+V3{0,0,.08},{.18,.18,.09})) return true;
    if (s.visitor.alive && s.visitor.at.f == s.floor && s.visitor.tex) {
        const double w=s.visitor.h*(*s.visitor.tex).w/(*s.visitor.tex).h;
        if (bound(r,cell_center(s.visitor.at,s.visitor.h*.5),{w*.5+.01,w*.5+.01,s.visitor.h*.5+.03})) return true;
    }
    const Floor& floor=lv.floors[static_cast<std::size_t>(s.floor)];
    for (int y=std::max(0,static_cast<int>(r.eye.y)-15);y<std::min(floor.h,static_cast<int>(r.eye.y)+16);++y)
        for (int x=std::max(0,static_cast<int>(r.eye.x)-15);x<std::min(floor.w,static_cast<int>(r.eye.x)+16);++x) {
            const Cell& cell=floor.at(x,y);
            if (cell.block == Block::wall || cell.goal) continue;
            for (int d=0;d<4;++d) {
                const int look=cell.face[static_cast<std::size_t>(d)];
                if (look<64 || !paint_glitches(look-64)) continue;
                const int nx=x+kDX[d],ny=y+kDY[d];
                if (floor.in(nx,ny) && floor.at(nx,ny).block!=Block::wall) continue;
                const V3 center{x+.5+kDX[d]*.5,y+.5+kDY[d]*.5,s.floor*kH+kH*.5};
                if (bound(r,center,{kDX[d] ? .005 : .505,kDY[d] ? .005 : .505,kH*.5+.005})) return true;
            }
        }
    return false;
}
}
