#include "shelf_art.hpp"
namespace games::modules {
void untangle_cover(gf::Painter& p, gf::Rect r) {
    using namespace shelf_art;
    [[maybe_unused]] const double x=r.x,y=r.y,w=r.width,h=r.height;
    [[maybe_unused]] const double cx=x+w*.5,cy=y+h*.5,s=std::min(w,h);
 {
        gf::Point pts[] = {{x + w * .18, y + h * .22},
                           {x + w * .82, y + h * .2},
                           {x + w * .5, y + h * .5},
                           {x + w * .2, y + h * .8},
                           {x + w * .8, y + h * .78}};
        const int links[][2] = {{0, 1}, {0, 2}, {1, 2}, {2, 3}, {2, 4}, {3, 4}, {0, 3}, {1, 4}};
        for (const int (&l)[2] : links)
            p.draw_line(pts[l[0]], pts[l[1]], rgb(120, 230, 240), 2);
        for (gf::Point q : pts) {
            disc(p, q.x, q.y, w * .085, rgb(10, 50, 70));
            disc(p, q.x, q.y, w * .065, rgb(255, 255, 255));
        }

    }

}
}
