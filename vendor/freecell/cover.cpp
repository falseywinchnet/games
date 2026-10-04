#include "shelf_art.hpp"
namespace games::modules {
void freecell_cover(gf::Painter& p, gf::Rect r) {
    using namespace shelf_art;
    [[maybe_unused]] const double x=r.x,y=r.y,w=r.width,h=r.height;
    [[maybe_unused]] const double cx=x+w*.5,cy=y+h*.5,s=std::min(w,h);

        for (int i = 0; i < 4; ++i)
            p.stroke_rounded_rect({x + w * (.06 + i * .23), y + h * .06, w * .19, h * .26}, 2,
                                  rgb(255, 255, 255, 170), 1.2);
        paint_card(p, {x + w * .52, y + h * .06, w * .19, h * .26}, 1, "");
        paint_card(p, {x + w * .26, y + h * .38, w * .46, h * .58}, 3, "7");


}
}
