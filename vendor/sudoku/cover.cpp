#include "shelf_art.hpp"
namespace games::modules {
void sudoku_cover(gf::Painter& p, gf::Rect r) {
    using namespace shelf_art;
    [[maybe_unused]] const double x=r.x,y=r.y,w=r.width,h=r.height;
    [[maybe_unused]] const double cx=x+w*.5,cy=y+h*.5,s=std::min(w,h);
 {
        gf::Rect g{x + w * .1, y + h * .1, w * .8, h * .8};
        p.draw_box_shadow(g, 3, {1, 2}, 3, 0, rgb(0, 0, 0, 80));
        p.fill_rounded_rect(g, 3, rgb(245, 249, 255));
        for (int i = 1; i < 9; ++i) {
            double t = i / 9.0;
            double wide = i % 3 == 0 ? 1.6 : .7;
            gf::Color line = i % 3 == 0 ? rgb(40, 80, 150) : rgb(160, 186, 220);
            p.draw_line({g.x + g.width * t, g.y}, {g.x + g.width * t, g.y + g.height}, line, wide);
            p.draw_line({g.x, g.y + g.height * t}, {g.x + g.width, g.y + g.height * t}, line, wide);
        }
        const char* digits[] = {"5", "3", "7", "9", "1"};
        const int cells[] = {0, 10, 40, 60, 80};
        for (int i = 0; i < 5; ++i) {
            double cxl = g.x + g.width * ((cells[i] % 9) + .22) / 9,
                   cyl = g.y + g.height * ((cells[i] / 9) + .88) / 9;
            p.draw_text_utf8({cxl, cyl}, digits[i],
                             {gf::FontRole::content, g.height / 9 * .9, 700, false},
                             rgb(24, 60, 130));
        }
        p.stroke_rounded_rect(g, 3, rgb(30, 64, 124), 1.4);

    }

}
}
