#include "shelf_art.hpp"
namespace games::modules {
void solve_cover(gf::Painter& p, gf::Rect r) {
    using namespace shelf_art;
    [[maybe_unused]] const double x=r.x,y=r.y,w=r.width,h=r.height;
    [[maybe_unused]] const double cx=x+w*.5,cy=y+h*.5,s=std::min(w,h);
 {
        gf::Color blue = rgb(70, 150, 225), gold = rgb(250, 205, 75);
        paint_polygon(
            p, {{x + w * .08, y + h * .08}, {x + w * .92, y + h * .08}, {x + w * .5, y + h * .5}},
            gold);
        paint_polygon(
            p, {{x + w * .08, y + h * .08}, {x + w * .5, y + h * .5}, {x + w * .08, y + h * .92}},
            blue);
        paint_polygon(
            p, {{x + w * .54, y + h * .54}, {x + w * .92, y + h * .16}, {x + w * .92, y + h * .92}},
            gold);
        paint_polygon(
            p, {{x + w * .12, y + h * .92}, {x + w * .5, y + h * .54}, {x + w * .88, y + h * .92}},
            blue);

    }

}
}
