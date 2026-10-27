#pragma once
// A stone's picture as it lies in the tray, lit by the room (room.hpp): drawn with the
// render engine's triangle rasteriser into depth and triangle ids, then shaded pixel by
// pixel from those: its mineral (minerals.hpp), the room's light on its body, the room
// mirrored in its polish, the sun's glint, light seen through the clear minerals, and
// the shade it casts on the wood around it, all from the same few light directions so
// stone and shadow agree.
//
// The picture is two layers over whatever lies beneath: a multiplier (the shadow, and
// the tint of what is seen through a clear stone) and light added on top. Laid as
//     seen = beneath x keep + add
// per channel, a stone of rock crystal shows the wood through it and throws a coloured
// shadow, as glass does.
#include "minerals.hpp"
#include "shapes.hpp"

#include "r3d.hpp"

#include <cstdint>
#include <vector>

namespace stones {

// The stone's turn in space: object to tray (x right, y up the screen, z up), rows of a
// rotation matrix.
struct Pose {
    double m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};
// Face up (0) or down (1), turned `turns` quarter turns clockwise.
[[nodiscard]] Pose orientation_pose(int orientation);
// A turn of `angle` radians about the tray's axis (0 x, 1 y, 2 z), then `pose`.
[[nodiscard]] Pose turned(const Pose& pose, int axis, double angle);

struct StonePicture {
    int width = 0, height = 0;
    double origin_x = 0, origin_y = 0;  // where the stone's centre lies in the picture
    std::vector<std::uint32_t> keep;    // r | g << 8 | b << 16, 255 keeps all
    std::vector<std::uint32_t> add;     // r | g << 8 | b << 16 | coverage << 24
    // When asked for: the surface as seen, for a glint drawn later: the normal's x and y
    // (128 + 127 n), its polish (0..255) and how far it is clear (0..255); empty otherwise.
    std::vector<std::uint32_t> shine;
};

// Scratch for drawing, kept by whoever draws so storage is reused from stone to stone;
// one per thread.
struct StoneWork {
    std::vector<std::uint32_t> colour;        // the rasteriser's own target, unused after
    render::r3d::Buffers buffers;             // depth and triangle ids, fine samples
    std::vector<float> corners;               // posed vertices: screen x, y, height
    std::vector<float> normals;               // posed normals
    std::vector<float> height;                // fine samples: height above the wood, -1 none
    std::vector<float> shade;                 // coarse: the room's light reaching each point
    std::vector<float> sun;                   // coarse: the sun reaching each point
    std::vector<float> keep;                  // fine samples: 3 a sample
    std::vector<float> add;
    std::vector<float> shine;                 // fine samples: normal x, y, polish
};

struct StoneLook {
    const Shape* shape = nullptr;
    const Finish* finish = nullptr;
    const MineralPicture* picture = nullptr;
};

struct DrawStone {
    double radius = 30;      // the outline's unit circle, in device pixels
    double lift = 0;         // 0 resting .. 1 held up off the wood
    int fineness = 2;        // samples across a pixel: 2 at rest, 1 in motion
    bool shadow = true;      // the shade on the wood around it
    bool shine = false;      // keep the surface for a glint (StonePicture::shine)
};

// The stone's picture, sized to hold it and its shadow.
void draw_stone(const StoneLook& look, const Pose& pose, const DrawStone& how, StoneWork& work,
                StonePicture& out);

// A glint on a laid picture with `shine`: a small bright light at (light_x, light_y) device
// pixels, `height` above the tray, caught by the stone's own facets as polish allows.
void lay_glint(const StonePicture& picture, std::uint32_t* pixels, int width, int height, int stride, double x,
               double y, double light_x, double light_y, double light_height, double strength, bool bgra);

// Lays `picture` scaled by `scale`, turned `spin` radians in the tray's plane, squashed
// across by `squash` (a stone turning over in flight), at `opacity`: nearest samples, for
// stones in motion.
void lay_stone_moved(const StonePicture& picture, std::uint32_t* pixels, int width, int height, int stride, double x,
                     double y, double scale, double spin, double squash, double opacity, bool bgra, int clip_x0,
                     int clip_y0, int clip_x1, int clip_y1);

// Lays `picture` with its centre at (x, y) on BGRA (`bgra` true) or RGBA pixels.
void lay_stone(const StonePicture& picture, std::uint32_t* pixels, int width, int height, int stride, double x,
               double y, bool bgra, int clip_x0, int clip_y0, int clip_x1, int clip_y1);

}  // namespace stones
