#include "shelf_art.hpp"
namespace games::modules {
void atomprobe_cover(gf::Painter& p, gf::Rect r) {
    using namespace shelf_art;
    [[maybe_unused]] const double x=r.x,y=r.y,w=r.width,h=r.height;
    [[maybe_unused]] const double cx=x+w*.5,cy=y+h*.5,s=std::min(w,h);
 {
        for (int direction = 0; direction < 3; ++direction) {
            gf::Point previous{};
            for (int k = 0; k <= 48; ++k) {
                double a = k * 6.2831853 / 48, u = std::cos(a) * w * .42, v = std::sin(a) * h * .15,
                       turn = direction * 1.0471976;
                gf::Point q{x + w * .5 + u * std::cos(turn) - v * std::sin(turn),
                            y + h * .5 + u * std::sin(turn) + v * std::cos(turn)};
                if (k)
                    p.draw_line(previous, q, rgb(110, 232, 238), 1.6);
                previous = q;
            }
        }
        disc(p, x + w * .5, y + h * .5, w * .1, rgb(255, 120, 120));
        disc(p, x + w * .47, y + h * .47, w * .035, rgb(255, 230, 230));

    }

}
}
