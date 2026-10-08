#pragma once
// The window's end of r2d and r3d: a GUI.Forms LiveSurface the game draws into directly,
// with no frame of its own and no copy. The surface is opaque and in the window's native
// byte order, so presenting it is a straight copy of the changed rows.
//
// LiveSurface keeps a small pool of buffers, and each keeps the pixels it was last
// given at a fixed address. So, as with EGL's buffer age, a frame only has to repair
// what changed since *that* buffer was last drawn: this frame's damage plus the
// damage of the frames it missed. The game names what changed; the surface says what to
// redraw.
#include "target.hpp"

#include "gui_forms/control.hpp"
#include "gui_forms/live_surface.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace render {
namespace gf = gui_forms;

class Surface {
public:
    // Sizes the surface in device pixels, creating it on first use. A new size makes
    // every buffer stale. Returns false if GUI.Forms could not provide one.
    bool configure(int width, int height);
    // Presents through `owner` in `window`; true when the window draws it directly
    // (otherwise the owner repaints with draw_live_surface on publish).
    bool attach(gf::Window& window, std::shared_ptr<gf::Control> owner);
    void detach();

    struct Frame {
        Target target;  // the buffer itself
        Rect repair;    // what must be drawn: everything outside is already right
    };
    // Starts a frame in which `damage` changed since the last one published (empty when
    // only a stale buffer needs bringing up to date). Nothing is returned when no buffer
    // is free; the damage is kept for the next try.
    [[nodiscard]] std::optional<Frame> begin(Rect damage);
    // Shows the frame begun last.
    void publish();
    // Gives the frame up: its buffer is left in an unknown state.
    void abandon();
    // Everything is redrawn next frame (lost contents, a new picture).
    void invalidate_all();

    [[nodiscard]] Order order() const {
        return order_;
    }
    [[nodiscard]] int width() const {
        return width_;
    }
    [[nodiscard]] int height() const {
        return height_;
    }
    [[nodiscard]] const std::shared_ptr<gf::LiveSurface>& live() const {
        return surface_;
    }
    [[nodiscard]] bool direct() const {
        return direct_;
    }

private:
    static constexpr std::size_t history = 8;
    struct Kept {
        const void* address = nullptr;  // a buffer's pixels
        std::uint64_t frame = 0;        // the frame it holds
    };
    std::shared_ptr<gf::LiveSurface> surface_;
    gf::LiveSurfaceWriteLease lease_;
    int width_ = 0;
    int height_ = 0;
    Order order_ = Order::bgra;
    bool direct_ = false;
    std::uint64_t frame_ = 0;                 // frames published
    std::array<Rect, history> damage_{};      // damage_[n % history]: what frame n changed
    std::array<Kept, gf::maximum_live_surface_buffer_count> kept_{};
    Rect pending_;                            // damage of frames not yet published
    const void* writing_ = nullptr;

    [[nodiscard]] std::uint64_t held_by(const void* address) const;
    void remember(const void* address, std::uint64_t frame);
};

}  // namespace render
