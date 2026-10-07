#include "shelf_art.hpp"
namespace games::modules {
void parrots_cover(gf::Painter& p, gf::Rect r) {
    using namespace shelf_art;
    [[maybe_unused]] const double x=r.x,y=r.y,w=r.width,h=r.height;
    [[maybe_unused]] const double cx=x+w*.5,cy=y+h*.5,s=std::min(w,h);

        disc(p, cx, cy, s * .30, rgb(65, 161, 100));
        disc(p, cx + s * .08, cy - s * .18, s * .24, rgb(224, 68, 46));
        paint_polygon(p,
                      {{cx + s * .22, cy - s * .15},
                       {cx + s * .44, cy - s * .03},
                       {cx + s * .17, cy + s * .05}},
                      rgb(246, 201, 77));
        disc(p, cx + s * .12, cy - s * .22, s * .04, rgb(22, 24, 26));
        p.draw_line({cx - s * .12, cy + s * .24}, {cx - s * .27, cy + s * .48}, rgb(55, 130, 215),
                    s * .12);


}
}
