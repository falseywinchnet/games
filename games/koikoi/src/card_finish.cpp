// The card table's card finish, copied verbatim from PlaySuite shared/cards/table.cpp (card_finish) so the
// hanafuda wear exactly the same surface as the other decks. On the card table itself, Koi-Koi
// uses that original; this copy is only for the Mac preview table.
#include "card_finish.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace games {

// The finish of a real playing card, as a translucent layer for the printed artwork: a fine
// air-cushion dimple texture lit from the upper left, a rolled edge that catches the light,
// and the falloff of a lamp over the table. The artwork's card is a 360 x 504 image whose
// rounded rectangle starts 2 px in with an 18 px corner radius.
std::vector<std::byte> card_finish(int w, int h) {
    std::vector<std::byte> out(static_cast<std::size_t>(w) * h * 4);
    const double s = w / 360.0;
    const double inset = 2.4 * s, radius = 18 * s;
    const double half_w = w * .5 - inset, half_h = h * .5 - inset;
    // Signed distance to the card's rounded outline (negative inside).
    struct Outline {
        double half_w, half_h, radius, cx, cy;
        double operator()(double x, double y) const {
            const double qx = std::abs(x - cx) - (half_w - radius),
                         qy = std::abs(y - cy) - (half_h - radius);
            const double ox = std::max(qx, 0.0), oy = std::max(qy, 0.0);
            return std::hypot(ox, oy) + std::min(std::max(qx, qy), 0.0) - radius;
        }
    };
    const Outline outline{half_w, half_h, radius, w * .5, h * .5};
    // Dimples on a hexagonal lattice, a few device pixels apart, each a little different.
    const double pitch = std::clamp(w / 64.0, 2.6, 4.2), row_h = pitch * .866;
    struct Dimples {
        double pitch, row_h;
        double operator()(double x, double y) const {
            const int row = static_cast<int>(std::floor(y / row_h));
            double depth = 0;
            for (int r = row - 1; r <= row + 1; ++r) {
                const double shift = (r & 1) ? pitch * .5 : 0;
                const int col = static_cast<int>(std::floor((x - shift) / pitch));
                for (int c = col - 1; c <= col + 1; ++c) {
                    std::uint32_t k = static_cast<std::uint32_t>(r * 73856093) ^
                                      static_cast<std::uint32_t>(c * 19349663);
                    k = (k ^ (k >> 13)) * 0x5bd1e995U;
                    const double jitter = ((k >> 8) & 255) / 255.0;
                    const double cx = c * pitch + shift + pitch * .5 + (jitter - .5) * pitch * .2,
                                 cy = r * row_h + row_h * .5;
                    const double d2 =
                        ((x - cx) * (x - cx) + (y - cy) * (y - cy)) / (pitch * pitch * .2);
                    if (d2 < 1)
                        depth -= (1 - d2) * (1 - d2) * (.75 + .5 * jitter);
                }
            }
            return depth;
        }
    };
    const Dimples dimples{pitch, row_h};
    const double lx = -.5, ly = -.7; // toward the lamp, in the card plane
    const double rim = std::max(1.6, 3.2 * s);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const double px = x + .5, py = y + .5;
            const double d = outline(px, py);
            const double cover = std::clamp(.5 - d, 0.0, 1.0);
            if (cover <= 0)
                continue;
            double light = 0, dark = 0;
            // Texture: slope of the dimpled surface toward the lamp.
            const double gx = dimples(px + .5, py) - dimples(px - .5, py),
                         gy = dimples(px, py + .5) - dimples(px, py - .5);
            const double facing = -(gx * lx + gy * ly) * 2.2;
            if (facing > 0)
                light += std::min(.1, facing * .1);
            else
                dark += std::min(.06, -facing * .06);
            // Rolled edge: the outline's outward normal, lit or shaded.
            const double depth_in = -d;
            if (depth_in < rim) {
                const double nx = outline(px + .5, py) - outline(px - .5, py),
                             ny = outline(px, py + .5) - outline(px, py - .5);
                const double toward = nx * lx + ny * ly;
                const double band = 1 - depth_in / rim;
                if (toward > 0)
                    light += .42 * toward * band * band;
                else
                    dark += .3 * -toward * band * band;
            }
            // Lamp: brighter toward the upper left, a soft gloss band, a little shade below.
            const double u = px / w, v = py / h, diagonal = u * .45 + v * .55;
            light += .08 * (1 - diagonal) * (1 - diagonal) +
                     .05 * std::exp(-std::pow((u + v - .62) / .16, 2));
            dark += .045 * diagonal * diagonal * diagonal;
            light = std::min(light, .6);
            dark = std::min(dark, .5);
            // White light composited over dark shade, premultiplied.
            const double alpha = (light + dark * (1 - light)) * cover;
            const double white = light * cover * 255;
            const std::size_t n = (static_cast<std::size_t>(y) * w + x) * 4;
            out[n] = out[n + 1] = out[n + 2] = static_cast<std::byte>(std::lround(white));
            out[n + 3] = static_cast<std::byte>(std::lround(alpha * 255));
        }
    return out;
}

}  // namespace games
