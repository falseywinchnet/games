#include "surface.hpp"
#include "overlays.hpp"

#include "gui_forms/window.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace render {

bool Surface::configure(int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);
    if (surface_ && width == width_ && height == height_) {
        return true;
    }
    abandon();
    gf::LiveSurfaceDescription description;
    description.width = static_cast<std::uint32_t>(width);
    description.height = static_cast<std::uint32_t>(height);
    description.pixel_format = gf::native_live_surface_pixel_format();
    description.opaque = true;
    if (surface_) {
        // GUI.Forms keeps presenting the old picture, scaled, until the first frame at this
        // size is published, so a resize never shows an empty surface.
        if (!(*surface_).reconfigure(description)) {
            return false;
        }
    } else {
        surface_ = gf::LiveSurface::create(description);
        if (!surface_) {
            return false;
        }
    }
    width_ = width;
    height_ = height;
    order_ = description.pixel_format == gf::LiveSurfacePixelFormat::rgba32_premultiplied_srgb ? Order::rgba : Order::bgra;
    invalidate_all();
    overlays_seen_ = false;  // the first frame at this size repaints the overlays over it
    return true;
}

namespace {
// The surfaces attached now, on the interface thread.
std::vector<Surface*>& attached_surfaces() {
    static std::vector<Surface*> list;
    return list;
}
void forget(Surface* surface) {
    std::vector<Surface*>& list = attached_surfaces();
    list.erase(std::remove(list.begin(), list.end(), surface), list.end());
}
}  // namespace

Surface::~Surface() {
    forget(this);
}

bool Surface::attach(gf::Window& window, std::shared_ptr<gf::Control> owner) {
    owner_ = owner;
    direct_ = surface_ && window.queue_live_surface_presentation(std::move(owner), surface_);
    forget(this);
    attached_surfaces().push_back(this);
    return direct_;
}

void Surface::detach() {
    abandon();
    direct_ = false;
    forget(this);
}

void Surface::reset() {
    abandon();
    forget(this);
    surface_.reset();
    owner_.reset();
    lease_ = gf::LiveSurfaceWriteLease{};
    width_ = 0;
    height_ = 0;
    order_ = Order::bgra;
    direct_ = false;
    frame_ = 0;
    damage_ = {};
    kept_ = {};
    pending_ = Rect{};
    overlays_.clear();
    overlays_seen_ = false;
    writing_ = nullptr;
}

bool Surface::present_again() {
    if (!surface_ || lease_ || frame_ == 0) {
        return false;
    }
    const void* latest = nullptr;
    for (const Kept& kept : kept_) {
        if (kept.frame == frame_) {
            latest = kept.address;
        }
    }
    if (latest == nullptr) {
        return false;
    }
    gf::LiveSurfaceWriteLease lease = (*surface_).try_acquire_write();
    if (!lease || static_cast<int>(lease.width()) != width_ || static_cast<int>(lease.height()) != height_) {
        lease.abandon();
        return false;
    }
    const std::span<std::byte> pixels = lease.pixels();
    // Buffers of one size share one layout: the last frame copies across as it is.
    if (static_cast<const void*>(pixels.data()) != latest) {
        std::memcpy(pixels.data(), latest, pixels.size());
    }
    ++frame_;
    damage_[frame_ % history] = Rect{};  // the picture is the one before
    static_cast<void>(lease.publish(gf::Rect{0, 0, static_cast<double>(width_), static_cast<double>(height_)}));
    remember(pixels.data(), frame_);
    return true;
}

void present_live_again(gf::Window& window) {
    // A copy: presenting changes nothing in the list, but stays safe if it ever did.
    const std::vector<Surface*> surfaces = attached_surfaces();
    for (Surface* surface : surfaces) {
        const std::shared_ptr<gf::Control> owner = (*surface).owner();
        if (owner && (*owner).attached_window() == &window && (*owner).effectively_visible()) {
            static_cast<void>((*surface).present_again());
        }
    }
}

void Surface::invalidate_all() {
    kept_ = {};
    pending_ = Rect{0, 0, width_, height_};
}

std::uint64_t Surface::held_by(const void* address) const {
    for (const Kept& kept : kept_) {
        if (kept.address == address) {
            return kept.frame;
        }
    }
    return 0;
}

void Surface::remember(const void* address, std::uint64_t frame) {
    // Its own place, else the oldest (an empty place is frame 0).
    Kept* slot = &kept_[0];
    for (Kept& kept : kept_) {
        if (kept.address == address) {
            slot = &kept;
            break;
        }
        if (kept.frame < (*slot).frame) {
            slot = &kept;
        }
    }
    *slot = Kept{address, frame};
}

std::optional<Surface::Frame> Surface::begin(Rect damage) {
    if (!surface_) {
        return std::nullopt;
    }
    abandon();
    pending_ = pending_.united(damage.intersected(Rect{0, 0, width_, height_}));
    lease_ = (*surface_).try_acquire_write();
    if (!lease_ || static_cast<int>(lease_.width()) != width_ || static_cast<int>(lease_.height()) != height_ ||
        lease_.row_bytes() % 4 != 0) {
        lease_.abandon();
        return std::nullopt;
    }
    const std::span<std::byte> pixels = lease_.pixels();
    writing_ = pixels.data();
    // What this buffer missed: the frames published since it was drawn, and this one.
    Rect repair = pending_;
    const std::uint64_t held = held_by(writing_);
    if (held == 0 || frame_ - held >= history) {
        repair = Rect{0, 0, width_, height_};
    } else {
        for (std::uint64_t n = held + 1; n <= frame_; ++n) {
            repair = repair.united(damage_[n % history]);
        }
    }
    Frame frame;
    frame.target.pixels = reinterpret_cast<std::uint32_t*>(pixels.data());
    frame.target.stride = static_cast<int>(lease_.row_bytes() / 4);
    frame.target.width = width_;
    frame.target.height = height_;
    frame.target.order = order_;
    frame.repair = repair;
    return frame;
}

void Surface::publish() {
    if (!lease_) {
        return;
    }
    ++frame_;
    damage_[frame_ % history] = pending_;
    gf::Rect changed = pending_.empty()
                           ? gf::Rect{0, 0, 1, 1}
                           : gf::Rect{static_cast<double>(pending_.x0), static_cast<double>(pending_.y0),
                                      static_cast<double>(pending_.x1 - pending_.x0),
                                      static_cast<double>(pending_.y1 - pending_.y0)};
    // An overlay that moved, folded or went away leaves pixels the host neither repaints (the
    // live surface covers them) nor copies again (outside this frame's damage, it takes the
    // window to hold the picture already): they keep whatever lay under the overlay, an old
    // scene behind a folded capsule. When the overlays change, the whole frame is presented.
    // The overlays themselves were painted over whatever lay beneath them then: before a
    // view's first frame, its plain fill (Catching Thieves opened with a green box behind
    // the capsule until the window was next exposed). On a first frame, after a resize, and
    // when they change, they paint again over this one.
    bool repaint = false;
    if (const std::shared_ptr<gf::Control> owner = owner_.lock()) {
        std::vector<gf::Rect> overlays = render::overlays_over(*owner);
        if (overlays != overlays_ || !overlays_seen_) {
            overlays_ = std::move(overlays);
            overlays_seen_ = true;
            changed = gf::Rect{0, 0, static_cast<double>(width_), static_cast<double>(height_)};
            repaint = true;
        }
    }
    static_cast<void>(lease_.publish(changed));
    remember(writing_, frame_);
    writing_ = nullptr;
    pending_ = Rect{};
    if (repaint) {
        if (const std::shared_ptr<gf::Control> owner = owner_.lock()) {
            render::repaint_overlays(*owner);
        }
    }
    // The overlays over what changed paint again over the new frame (overlays.hpp).
    if (const std::shared_ptr<gf::Control> owner = owner_.lock()) {
        const gf::Rect area = (*owner).client_rectangle();
        const double sx = width_ > 0 && area.width > 0 ? area.width / width_ : 1.0;
        const double sy = height_ > 0 && area.height > 0 ? area.height / height_ : 1.0;
        render::refresh_overlays(*owner, {changed.x * sx, changed.y * sy, changed.width * sx, changed.height * sy});
    }
}

void Surface::abandon() {
    if (lease_) {
        lease_.abandon();
        // Its contents are no longer the frame it held.
        for (Kept& kept : kept_) {
            if (kept.address == writing_) {
                kept = Kept{};
            }
        }
    }
    writing_ = nullptr;
}

}  // namespace render
