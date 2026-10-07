#include "shelf_art.hpp"
namespace games::modules {
void spider_cover(gf::Painter& p, gf::Rect r) {
    using namespace shelf_art;
    [[maybe_unused]] const double x=r.x,y=r.y,w=r.width,h=r.height;
    [[maybe_unused]] const double cx=x+w*.5,cy=y+h*.5,s=std::min(w,h);

        for (int i = 0; i < 3; ++i)
            paint_card(p, {x + w * (.08 + i * .16), y + h * (.06 + i * .1), w * .5, h * .66}, 0,
                       i == 2   ? "K"
                       : i == 1 ? "Q"
                                : "J");
        {
            double cx = x + w * .8, cy = y + h * .82, s = w * .13;
            for (int i = 0; i < 4; ++i) {
                double a = -.9 + i * .6;
                p.draw_line({cx, cy}, {cx - s * 1.6, cy + std::sin(a) * s * 1.4}, rgb(20, 20, 26),
                            1.4);
                p.draw_line({cx, cy}, {cx + s * 1.6, cy + std::sin(a) * s * 1.4}, rgb(20, 20, 26),
                            1.4);
            }
            disc(p, cx, cy, s * .55, rgb(20, 20, 26));
            disc(p, cx, cy - s * .6, s * .35, rgb(20, 20, 26));
        }


}
}
