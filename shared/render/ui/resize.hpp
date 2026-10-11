#pragma once
// A live surface that takes a new size starts with empty buffers, and the window shows
// whatever the surface holds: until the game's first frame at the new size arrives (a
// tick later, or later still while its text is prepared), that is black. Resizing a game
// by its corner flashed black at every step. So a surface given a new size publishes its
// last picture at once, scaled to fit, and the game's own frame replaces it.
#include "gui_forms/live_surface.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace render {
namespace gf = gui_forms;

// Reconfigures `surface` to `description`, then shows its previous picture enlarged or
// reduced (nearest pixel) to the new size. False when the surface could not be resized.
inline bool reconfigure_keeping_picture(gf::LiveSurface& surface,
                                        const gf::LiveSurfaceDescription& description) {
    const gf::LiveSurfaceFrame before = surface.acquire_latest();
    if (!surface.reconfigure(description))
        return false;
    if (!before || before.width() == 0 || before.height() == 0 ||
        before.pixel_format() != description.pixel_format)
        return true;
    gf::LiveSurfaceWriteLease lease = surface.try_acquire_write();
    if (!lease)
        return true;
    const std::uint32_t width = lease.width(), height = lease.height();
    const std::uint32_t from_width = before.width(), from_height = before.height();
    const std::span<const std::byte> source = before.pixels();
    const std::span<std::byte> target = lease.pixels();
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint32_t sy = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(y) * from_height + from_height / 2) / height);
        const std::uint32_t* in = reinterpret_cast<const std::uint32_t*>(
            source.data() + static_cast<std::size_t>(std::min(sy, from_height - 1)) * before.row_bytes());
        std::uint32_t* out = reinterpret_cast<std::uint32_t*>(
            target.data() + static_cast<std::size_t>(y) * lease.row_bytes());
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint32_t sx = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(x) * from_width + from_width / 2) / width);
            out[x] = in[std::min(sx, from_width - 1)];
        }
    }
    static_cast<void>(lease.publish());
    return true;
}

} // namespace render
