#include "shelf_art.hpp"
namespace games::modules {
void fourpegs_cover(gf::Painter& p, gf::Rect r) {
    using namespace shelf_art;
    [[maybe_unused]] const double x=r.x,y=r.y,w=r.width,h=r.height;
    [[maybe_unused]] const double cx=x+w*.5,cy=y+h*.5,s=std::min(w,h);
 {
        const gf::Color colors[] = {rgb(240, 50, 74), rgb(255, 201, 58), rgb(70, 212, 106),
                                    rgb(46, 168, 255)};
        for (int i = 0; i < 4; ++i) {
            double cx = x + w * (.29 + (i % 2) * .42), cy = y + h * (.29 + (i / 2) * .42);
            disc(p, cx + 1, cy + 2, w * .17, rgb(0, 0, 0, 90));
            disc(p, cx, cy, w * .17, colors[i]);
            disc(p, cx - w * .05, cy - w * .05, w * .05, rgb(255, 255, 255, 150));
        }

    }

}
}
