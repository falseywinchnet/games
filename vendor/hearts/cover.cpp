#include "shelf_art.hpp"
namespace games::modules {
void hearts_cover(gf::Painter& p, gf::Rect r) {
    using namespace shelf_art;
    [[maybe_unused]] const double x=r.x,y=r.y,w=r.width,h=r.height;
    [[maybe_unused]] const double cx=x+w*.5,cy=y+h*.5,s=std::min(w,h);
 {
        double cx = x + w * .5, cy = y + h * .52;
        paint_suit(p, cx + w * .02, cy + h * .04, w * .82, 1, rgb(90, 8, 22, 120));
        paint_suit(p, cx, cy, w * .82, 1, rgb(214, 30, 58));
        disc(p, cx - w * .2, cy - h * .2, w * .08, rgb(255, 255, 255, 120));

    }

}
}
