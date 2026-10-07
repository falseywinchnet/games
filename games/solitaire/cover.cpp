#include "shelf_art.hpp"
namespace games::modules {
void solitaire_cover(gf::Painter& p, gf::Rect r) {
    using namespace shelf_art;
    [[maybe_unused]] const double x=r.x,y=r.y,w=r.width,h=r.height;
    [[maybe_unused]] const double cx=x+w*.5,cy=y+h*.5,s=std::min(w,h);

        paint_card_back(p, {x + w * .08, y + h * .14, w * .5, h * .7}, rgb(150, 34, 54));
        paint_card(p, {x + w * .4, y + h * .08, w * .5, h * .7}, 0, "A");


}
}
