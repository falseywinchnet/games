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
#include <vector>

namespace render {
namespace gf = gui_forms;

// A floating control that is not to be repainted over each new frame: an opaque panel
// the game never shows through, or one meant to show the game as it stood (a dimmed
// sheet). Repainting a large panel and all its text at the game's frame rate costs a
// processor that draws the window itself (a remote desktop) most of its time.
class StillOverlay {
  public:
    virtual ~StillOverlay() = default;
};

namespace detail {
// Where a control lies in the window by the layout last committed. absolute_bounds()
// would run any pending layout first, and a publish must not: laying a view out again
// in the middle of its own frame reconfigures the surface it has just published to.
// A pending layout repaints the overlays itself once it runs.
inline gf::Rect committed_in_window(const gf::Control& control, gf::Rect local) {
    const gf::Rect own = control.committed_arranged_bounds();
    local.x += own.x;
    local.y += own.y;
    for (gf::Control::Ptr at = control.parent(); at; at = (*at).parent()) {
        const gf::Rect placed = (*at).committed_arranged_bounds();
        local.x += placed.x;
        local.y += placed.y;
    }
    return local;
}
inline void refresh_overlays_in(const gf::Control& control, const gf::Rect& damage) {
    for (const gf::Control::Ptr& child : control.children()) {
        if (!child || !(*child).visible())
            continue;
        if (dynamic_cast<const StillOverlay*>(child.get()) != nullptr)
            continue;
        if ((*child).paint_plane() == gf::PaintPlane::overlay) {
            const gf::Rect placed = (*child).committed_arranged_bounds();
            if (!gf::Rect::intersection(committed_in_window(*child, {0, 0, placed.width, placed.height}), damage).empty())
                (*child).invalidate_subtree(gf::Dirty::paint);
            continue;
        }
        refresh_overlays_in(*child, damage);
    }
}
inline void collect_overlays_in(const gf::Control& control, const gf::Rect& area, std::vector<gf::Rect>& out) {
    for (const gf::Control::Ptr& child : control.children()) {
        if (!child || !(*child).visible())
            continue;
        if ((*child).paint_plane() == gf::PaintPlane::overlay) {
            const gf::Rect placed = (*child).committed_arranged_bounds();
            const gf::Rect bounds = committed_in_window(*child, {0, 0, placed.width, placed.height});
            if (!gf::Rect::intersection(bounds, area).empty())
                out.push_back(bounds);
            continue;
        }
        collect_overlays_in(*child, area, out);
    }
}
} // namespace detail

// Where the overlays over `owner` lie, in the window, by the layout last committed. A live
// view compares them from frame to frame: see Surface::publish.
inline std::vector<gf::Rect> overlays_over(const gf::Control& owner) {
    std::vector<gf::Rect> found;
    gf::Window* window = owner.attached_window();
    if (window == nullptr || !(*window).root())
        return found;
    const gf::Rect all = owner.client_rectangle();
    detail::collect_overlays_in(*(*window).root(), detail::committed_in_window(owner, {0, 0, all.width, all.height}), found);
    return found;
}

// Every overlay over `local_damage` of `owner` (its own points; empty for all of it)
// paints again, on every host. For a new picture: another scene, or the first frames
// after a command from the capsule. On macOS the host recomposites overlays inside a
// frame's damage itself, but an overlay painted while the old picture was still showing
// (a capsule button pressed to change the scene) can keep that picture beneath it until
// it is invalidated: Stillwater's capsule showed the last tank's plants over the reef.
inline void repaint_overlays(const gf::Control& owner, gf::Rect local_damage = {}) {
    gf::Window* window = owner.attached_window();
    if (window == nullptr || !(*window).root())
        return;
    const gf::Rect all = owner.client_rectangle();
    const gf::Rect damage =
        detail::committed_in_window(owner, local_damage.empty() ? gf::Rect{0, 0, all.width, all.height} : local_damage);
    detail::refresh_overlays_in(*(*window).root(), damage);
}

// A frame was published for `owner`, changing `local_damage` (the owner's own points;
// empty for all of it): every overlay over that paints again.
inline void refresh_overlays(const gf::Control& owner, gf::Rect local_damage = {}) {
#if defined(__APPLE__)
    // The macOS host redraws a publication's damage itself, the overlays in it included
    // (GUI.Forms 6e9a817). Asking again only added far-apart invalidations (the capsule in
    // one corner, help in another) whose union repainted the whole window every frame.
    static_cast<void>(owner);
    static_cast<void>(local_damage);
    return;
#endif
    repaint_overlays(owner, local_damage);
}

} // namespace render
