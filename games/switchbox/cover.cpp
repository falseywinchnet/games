#include "shelf_art.hpp"
namespace games::modules {
void switchbox_cover(gf::Painter& p, gf::Rect r) {
    using namespace shelf_art;
    [[maybe_unused]] const double x=r.x,y=r.y,w=r.width,h=r.height;
    [[maybe_unused]] const double cx=x+w*.5,cy=y+h*.5,s=std::min(w,h);
 {
        gf::Rect panel{x + w * .06, y + h * .28, w * .88, h * .46};
        p.fill_rounded_rect(panel, 5, rgb(236, 153, 187));
        p.stroke_rounded_rect(panel, 5, rgb(255, 220, 236), 1.2);
        for (int i = 0; i < 5; ++i) {
            gf::Rect slot{x + w * (.13 + i * .155), y + h * .36, w * .1, h * .3};
            p.fill_rounded_rect(slot, 2, rgb(31, 54, 58));
            bool on = i == 0 || i == 2 || i == 3;
            p.fill_rounded_rect({slot.x + 1.5, slot.y + (on ? 1.5 : slot.height * .5),
                                 slot.width - 3, slot.height * .5 - 1.5},
                                1.5, on ? rgb(255, 228, 110) : rgb(120, 130, 136));
        }

    }

}
}
