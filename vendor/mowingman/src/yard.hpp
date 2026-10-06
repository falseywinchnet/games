#pragma once
// The picture of the garden. Retained layers keep the work proportional to what
// changes:
//
//   base   the terrace, the edging and the shaded lawn: built once per size or
//          garden, then re-shaded only inside the rectangle the deck just touched
//   beds   flower beds and the birdbath, a transparent layer over the lawn
//   frame  base, then the small things still standing, the gnome, the mower and
//          flying bits, drawn every frame
#include "art.hpp"
#include "grass_art.hpp"
#include "platform/raster.hpp"
#include "sim.hpp"

#include <cstdint>
#include <vector>

namespace mm {

class Yard final {
  public:
    // Lays the garden out in a raster of width x height and shades all of it.
    void build(int width, int height, const GrassArt& art, const Mowing& mowing);
    // Re-shades the part of the lawn inside `dirty` (lawn metres).
    void refresh(const GrassArt& art, const Mowing& mowing, const Dirty& dirty);
    // Draws a frame: the base, then everything that moves.
    const std::vector<std::uint8_t>& compose(const Mowing& mowing, const MowerPose& pose, double time);
    [[nodiscard]] const Frame& frame() const {
        return frame_;
    }
    [[nodiscard]] int width() const {
        return width_;
    }
    [[nodiscard]] int height() const {
        return height_;
    }
    [[nodiscard]] bool built() const {
        return width_ > 0;
    }
    // Converts raster pixels to lawn metres.
    [[nodiscard]] double lawn_x(double px) const {
        return (px - frame_.ox) / frame_.ppm;
    }
    [[nodiscard]] double lawn_y(double py) const {
        return (py - frame_.oy) / (frame_.ppm * frame_.tilt);
    }

  private:
    void shade(const GrassArt& art, const Mowing& mowing, int x0, int y0, int x1, int y1);
    void wall_shadows(int x0, int y0, int x1, int y1);

    int width_{};
    int height_{};
    Frame frame_{};
    Canvas surround_{};  // terrace and edging (opaque)
    Canvas beds_{};      // beds over the lawn (transparent)
    Canvas base_{};      // surround + lawn + beds
    Canvas picture_{};   // the composed frame
    // Tree canopies, scaled to this raster once, laid over each frame.
    struct Crown {
        int x{};
        int y{};
        int width{};
        int height{};
        std::vector<std::uint32_t> px{};  // premultiplied 0xAARRGGBB
    };
    std::vector<Crown> crowns_{};
};

} // namespace mm
