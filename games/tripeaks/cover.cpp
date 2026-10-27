#include "shelf_art.hpp"
namespace games::modules {
// Three peaks: two backs leaning on each side, a face on each top.
void tripeaks_cover(gf::Painter& p, gf::Rect r) {
    using namespace shelf_art;
    const double x = r.x, y = r.y, w = r.width, h = r.height;
    const double cw = w * .26, ch = cw * 1.4;
    const char* tops[] = {"K", "A", "Q"};
    for (int peak = 0; peak < 3; ++peak) {
        const double px = x + w * (.02 + peak * .25), py = y + h * (.36 - (peak == 1 ? .12 : 0));
        paint_card_back(p, {px, py + ch * .45, cw, ch}, rgb(58, 96, 150));
        paint_card_back(p, {px + cw * .62, py + ch * .45, cw, ch}, rgb(58, 96, 150));
        paint_card(p, {px + cw * .31, py, cw, ch}, peak == 1 ? 3 : peak * 2, tops[peak]);
    }
}
}
