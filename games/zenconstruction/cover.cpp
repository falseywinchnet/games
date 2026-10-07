#include "shelf_art.hpp"
namespace games::modules {
void zenconstruction_cover(gf::Painter& p, gf::Rect r) {
    using namespace shelf_art;
    [[maybe_unused]] const double x=r.x,y=r.y,w=r.width,h=r.height;
    [[maybe_unused]] const double cx=x+w*.5,cy=y+h*.5,s=std::min(w,h);

        for (int level = 0; level < 4; ++level) {
            const double rw = s * (.65 - level * .12);
            p.fill_rounded_rect({cx - rw * .5, cy + s * .28 - level * s * .19,
                                 rw, s * .17}, s * .07,
                                rgb(182 + level * 12, 173 + level * 8, 148 + level * 10));
        }


}
}
