#include "shelf_art.hpp"
namespace games::modules {
void maze_cover(gf::Painter& p,gf::Rect r) {
    using namespace shelf_art;
    const double x=r.x,y=r.y,w=r.width,h=r.height;
    paint_polygon(p,{{x,y},{x+w*.38,y+h*.35},{x+w*.38,y+h*.75},{x,y+h}},rgb(172,58,114));
    paint_polygon(p,{{x+w,y},{x+w*.62,y+h*.35},{x+w*.62,y+h*.75},{x+w,y+h}},rgb(48,161,176));
    p.fill_rect({x+w*.38,y+h*.35,w*.24,h*.4},rgb(24,27,58));
    for(int i=1;i<5;++i) {
        const double t=i/5.0;
        p.draw_line({x,y+h*t},{x+w*.38,y+h*(.35+.4*t)},rgb(255,156,222),1);
        p.draw_line({x+w,y+h*t},{x+w*.62,y+h*(.35+.4*t)},rgb(161,255,244),1);
    }
    p.fill_rounded_rect({x+w*.44,y+h*.62,w*.12,h*.09},2,rgb(255,221,70));
}
}
