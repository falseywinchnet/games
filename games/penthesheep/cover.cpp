#include "shelf_art.hpp"
namespace games::modules {
void penthesheep_cover(gf::Painter& p, gf::Rect r) {
    using namespace shelf_art;
    [[maybe_unused]] const double x=r.x,y=r.y,w=r.width,h=r.height;
    [[maybe_unused]] const double cx=x+w*.5,cy=y+h*.5,s=std::min(w,h);

        for (int i = 0; i < 6; ++i) {
            const double a = i * 6.283185307179586 / 6;
            disc(p, cx + std::cos(a) * s * .20, cy + std::sin(a) * s * .15, s * .18,
                 rgb(255, 247, 225));
        }
        disc(p, cx + s * .26, cy + s * .02, s * .15, rgb(66, 60, 54));
        disc(p, cx + s * .30, cy - s * .01, s * .025, rgb(255, 250, 235));
        p.draw_line({cx - s * .16, cy + s * .22}, {cx - s * .16, cy + s * .40}, rgb(66, 60, 54),
                    s * .07);
        p.draw_line({cx + s * .12, cy + s * .22}, {cx + s * .12, cy + s * .40}, rgb(66, 60, 54),
                    s * .07);


}
}
