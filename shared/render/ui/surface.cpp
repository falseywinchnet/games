#include "surface.hpp"

#include "gui_forms/window.hpp"

#include <algorithm>

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
    return true;
}

bool Surface::attach(gf::Window& window, std::shared_ptr<gf::Control> owner) {
    direct_ = surface_ && window.queue_live_surface_presentation(std::move(owner), surface_);
    return direct_;
}

void Surface::detach() {
    abandon();
    direct_ = false;
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
    const gf::Rect changed = pending_.empty()
                                 ? gf::Rect{0, 0, 1, 1}
                                 : gf::Rect{static_cast<double>(pending_.x0), static_cast<double>(pending_.y0),
                                            static_cast<double>(pending_.x1 - pending_.x0),
                                            static_cast<double>(pending_.y1 - pending_.y0)};
    static_cast<void>(lease_.publish(changed));
    remember(writing_, frame_);
    writing_ = nullptr;
    pending_ = Rect{};
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
