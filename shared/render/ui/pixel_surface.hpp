#pragma once
// The window end of the games drawn in big pixels: their small frame (an r2d::Canvas)
// enlarged by whole window pixels straight into GUI.Forms' live buffers, opaque and in the
// window's own byte order, with the game's crisp text over it at window resolution.
//
// Only what changed is drawn again. The small frame is compared row by row with the one
// last shown (a read of the small frame, a sixteenth or less of what enlarging it writes),
// and the game's list of texts with the last; the window pixels under the changed rows and
// texts, and whatever the buffer in hand missed, are enlarged again and handed back for
// the text.
#include "r2d_canvas.hpp"
#include "surface.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace render {

class PixelSurface {
public:
    // A text the game draws over the frame: what it shows (any key that changes with its
    // words, font, size and colour) and where, in window pixels.
    struct Text {
        std::uint64_t key = 0;
        Rect bounds;
    };

    // Window pixels; a new size makes every buffer stale.
    bool configure(int width, int height) {
        return surface_.configure(width, height);
    }
    bool attach(gf::Window& window, std::shared_ptr<gf::Control> owner) {
        return surface_.attach(window, std::move(owner));
    }
    // Starts a frame showing `low` at `scale` window pixels per game pixel (any positive
    // number; columns and rows are chosen nearest). The returned frame has the enlarged
    // picture already in place over `repair`; the game draws its texts clipped to it, then
    // publishes. Nothing is returned when nothing changed or no buffer is free.
    [[nodiscard]] std::optional<Surface::Frame> begin(const r2d::Canvas& low, double scale,
                                                      const std::vector<Text>& texts);
    void publish() {
        surface_.publish();
        owed_ = Rect{};
    }
    // Gives up the frame begun (its text was not ready, say); what it would have shown is
    // drawn with the next frame.
    void abandon() {
        surface_.abandon();
    }
    // True while changes are waiting to be shown (no buffer was free, or a frame was given
    // up); false when the picture on screen is the last one begun.
    [[nodiscard]] bool owes() const {
        return !owed_.empty();
    }
    // Everything is drawn again next frame.
    void invalidate_all();

    [[nodiscard]] const std::shared_ptr<gf::LiveSurface>& live() const {
        return surface_.live();
    }
    [[nodiscard]] bool direct() const {
        return surface_.direct();
    }
    [[nodiscard]] Order order() const {
        return surface_.order();
    }

private:
    Surface surface_;
    std::vector<std::uint8_t> shown_;  // the small frame last shown
    int shown_w_ = 0;
    int shown_h_ = 0;
    double shown_scale_ = 0;
    std::vector<Text> shown_texts_;
    Rect owed_;  // changes not yet published
    std::vector<int> columns_;  // per window column, its game column
    std::vector<std::uint32_t> row_;  // one game row in the window's byte order
};

}  // namespace render
