#include "shelf_art.hpp"
namespace games::modules {
void eggy_cover(gf::Painter& p, gf::Rect r) {
    using namespace shelf_art;
    [[maybe_unused]] const double x=r.x,y=r.y,w=r.width,h=r.height;
    [[maybe_unused]] const double cx=x+w*.5,cy=y+h*.5,s=std::min(w,h);

        disc(p, x + w * .44, y + h * .62, w * .3, rgb(237, 199, 91));
        disc(p, x + w * .62, y + h * .38, w * .2, rgb(251, 221, 125));
        paint_polygon(
            p, {{x + w * .78, y + h * .36}, {x + w * .98, y + h * .42}, {x + w * .78, y + h * .48}},
            rgb(214, 128, 52));
        disc(p, x + w * .67, y + h * .34, w * .035, rgb(38, 53, 57));
        p.fill_rounded_rect({x + w * .46, y + h * .14, w * .34, h * .09}, h * .04,
                            rgb(132, 104, 64));
        p.draw_line({x + w * .36, y + h * .88}, {x + w * .32, y + h * .98}, rgb(189, 117, 53), 3);
        p.draw_line({x + w * .54, y + h * .88}, {x + w * .6, y + h * .98}, rgb(189, 117, 53), 3);


}
}
