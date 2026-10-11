#pragma once
// Live frames under floating controls. GUI.Forms copies a published live frame straight
// to the window everywhere except under overlay-plane controls (a floating command bar, a
// help button, a game's own floating buttons): those pixels belong to retained painting,
// which paints them again only when the overlay itself is invalidated. Without that, a
// soft edge or shadow over an animated game holds whatever frame was there when it last
// painted, and after a resize, the black from before the first new frame. So whoever
// publishes a frame asks the overlays it reaches to paint again over it.
#include "gui_forms/control.hpp"
#include "gui_forms/window.hpp"

#include <memory>

namespace render {
namespace gf = gui_forms;

namespace detail {
inline void refresh_overlays_in(const gf::Control& control, const gf::Rect& damage) {
    for (const gf::Control::Ptr& child : control.children()) {
        if (!child || !(*child).visible())
            continue;
        if ((*child).paint_plane() == gf::PaintPlane::overlay) {
            if (!gf::Rect::intersection((*child).absolute_bounds(), damage).empty())
                (*child).invalidate_subtree(gf::Dirty::paint);
            continue;
        }
        refresh_overlays_in(*child, damage);
    }
}
} // namespace detail

// A frame was published for `owner`, changing `local_damage` (the owner's own points;
// empty for all of it): every overlay over that paints again.
inline void refresh_overlays(const gf::Control& owner, gf::Rect local_damage = {}) {
    gf::Window* window = owner.attached_window();
    if (window == nullptr || !(*window).root())
        return;
    const gf::Rect all = owner.client_rectangle();
    const gf::Rect damage = owner.rectangle_to_window(local_damage.empty() ? gf::Rect{0, 0, all.width, all.height} : local_damage);
    detail::refresh_overlays_in(*(*window).root(), damage);
}

} // namespace render
