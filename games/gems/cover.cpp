#include "shelf_art.hpp"
namespace games::modules {
void gems_cover(gf::Painter& p, gf::Rect r) {
    using namespace shelf_art;
    [[maybe_unused]] const double x=r.x,y=r.y,w=r.width,h=r.height;
    [[maybe_unused]] const double cx=x+w*.5,cy=y+h*.5,s=std::min(w,h);
 {
        struct PaintBoxGem {
            gf::Painter& p;
            void operator()(double cx, double cy, double s, gf::Color dark, gf::Color mid,
                            gf::Color light) const {
                paint_polygon(p,
                              {{cx - s, cy - s * .3},
                               {cx - s * .55, cy - s * .85},
                               {cx + s * .55, cy - s * .85},
                               {cx + s, cy - s * .3},
                               {cx, cy + s}},
                              dark);
                paint_polygon(p, {{cx - s, cy - s * .3}, {cx + s, cy - s * .3}, {cx, cy + s}}, mid);
                paint_polygon(
                    p, {{cx - s * .45, cy - s * .3}, {cx + s * .45, cy - s * .3}, {cx, cy + s}},
                    light);
                paint_polygon(p,
                              {{cx - s * .55, cy - s * .85},
                               {cx + s * .55, cy - s * .85},
                               {cx + s * .3, cy - s * .3},
                               {cx - s * .3, cy - s * .3}},
                              mix_color(light, rgb(255, 255, 255), .4));
            }
        };
        PaintBoxGem gem{p};
        gem(x + w * .3, y + h * .66, w * .2, rgb(14, 90, 60), rgb(40, 180, 110),
            rgb(150, 240, 190));
        gem(x + w * .72, y + h * .68, w * .18, rgb(130, 70, 0), rgb(240, 170, 20),
            rgb(255, 230, 140));
        gem(x + w * .5, y + h * .38, w * .3, rgb(80, 20, 120), rgb(160, 70, 220),
            rgb(232, 196, 255));
        disc(p, x + w * .44, y + h * .2, w * .03, rgb(255, 255, 255));
        p.draw_line({x + w * .44, y + h * .13}, {x + w * .44, y + h * .27}, rgb(255, 255, 255, 200),
                    1);
        p.draw_line({x + w * .37, y + h * .2}, {x + w * .51, y + h * .2}, rgb(255, 255, 255, 200),
                    1);

    }

}
}
