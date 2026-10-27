#include "shelf_art.hpp"
namespace games::modules {
void koikoi_cover(gf::Painter& p, gf::Rect r) {
    using namespace shelf_art;
    [[maybe_unused]] const double x=r.x,y=r.y,w=r.width,h=r.height;
    [[maybe_unused]] const double cx=x+w*.5,cy=y+h*.5,s=std::min(w,h);
 {
        const gf::Rect card{cx - s * .32, cy - s * .47, s * .64, s * .94};
        p.fill_rounded_rect(card, s * .035, rgb(255, 243, 212));
        disc(p, cx + s * .10, cy - s * .20, s * .17, rgb(197, 47, 36));
        for (int i = 0; i < 3; ++i) {
            const double x = cx - s * .19 + i * s * .16;
            p.draw_line({x, cy + s * .37}, {x + s * .09, cy - s * .03}, rgb(36, 69, 46), s * .035);
            disc(p, x + s * .08, cy - s * .02, s * .11, rgb(48, 100, 61));
        }

    }

}
}
