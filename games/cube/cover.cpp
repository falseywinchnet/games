#include "shelf_art.hpp"
namespace games::modules {
void cube_cover(gf::Painter& p, gf::Rect r) {
    using namespace shelf_art;
    [[maybe_unused]] const double x=r.x,y=r.y,w=r.width,h=r.height;
    [[maybe_unused]] const double cx=x+w*.5,cy=y+h*.5,s=std::min(w,h);
 {
        double cx = x + w * .5, cy = y + h * .5, s = w * .4;
        gf::Point top{cx, cy - s}, left{cx - s * .87, cy - s * .5},
            right{cx + s * .87, cy - s * .5}, mid{cx, cy}, bl{cx - s * .87, cy + s * .5},
            br{cx + s * .87, cy + s * .5}, bottom{cx, cy + s};
        paint_polygon(p, {top, right, mid, left}, rgb(214, 240, 228));
        paint_polygon(p, {left, mid, bottom, bl}, rgb(96, 168, 140));
        paint_polygon(p, {mid, right, br, bottom}, rgb(150, 206, 184));
        p.draw_line({left.x + s * .3, left.y + s * .3}, {mid.x - s * .1, mid.y + s * .44},
                    rgb(232, 70, 80), 3);
        p.draw_line({mid.x + s * .2, mid.y + s * .5}, {right.x - s * .2, right.y + s * .6},
                    rgb(250, 200, 60), 3);
        p.draw_line({top.x - s * .3, top.y + s * .4}, {top.x + s * .3, top.y + s * .5},
                    rgb(70, 140, 230), 3);
        p.draw_line(top, mid, rgb(255, 255, 255, 140), 1);

    }

}
}
