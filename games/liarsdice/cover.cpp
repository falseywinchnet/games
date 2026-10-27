#include "shelf_art.hpp"
namespace games::modules {
void liarsdice_cover(gf::Painter& p, gf::Rect r) {
    using namespace shelf_art;
    [[maybe_unused]] const double x=r.x,y=r.y,w=r.width,h=r.height;
    [[maybe_unused]] const double cx=x+w*.5,cy=y+h*.5,s=std::min(w,h);

        p.fill_rounded_rect({cx - s * .34, cy - s * .34, s * .68, s * .68}, s * .08,
                            rgb(248, 232, 195));
        for (int i = -1; i <= 1; ++i)
            disc(p, cx + i * s * .19, cy + i * s * .19, s * .055, rgb(36, 31, 31));
        disc(p, cx - s * .19, cy + s * .19, s * .055, rgb(36, 31, 31));
        disc(p, cx + s * .19, cy - s * .19, s * .055, rgb(36, 31, 31));


}
}
