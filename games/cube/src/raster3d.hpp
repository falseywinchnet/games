#pragma once
// A small depth-buffered triangle rasteriser for the cube: per-vertex colour, an
// optional mirror reflection of the lake panorama, and a per-pixel cell id for picking.
// Output is BGRA premultiplied, transparent where nothing was drawn, so the view can lay
// it over the backdrop. Adapted from the shared puzzle engine's PuzzleRaster, which drew
// the four-by-four cube before this game had its own folder.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ps_cube {

struct Rgba {
    double r = 0;
    double g = 0;
    double b = 0;
    double a = 255;  // straight alpha, 0..255
};

struct Vertex {
    double x = 0;    // device pixels
    double y = 0;
    double z = 0;    // depth: smaller is nearer
    Rgba color;
    double u = -1;   // panorama coordinates when env > 0
    double v = -1;
    double env = 0;  // how much of the reflection replaces the colour
};

// Strokes painted over the surface in submission order use this depth.
inline constexpr double overlay_depth = -1e9;

class Raster3D {
public:
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;  // width * height * 4, BGRA premultiplied
    std::vector<int> ids;              // width * height, -1 where nothing was drawn
    std::vector<float> depth;
    // Draft quality while the cube moves: nearest reflection samples, coarser meshes.
    bool draft = false;

    void resize(int w, int h);
    void clear();
    // Overlay triangles (z <= overlay_depth) blend over what is there; others are opaque
    // and depth tested.
    void triangle(const Vertex& a, const Vertex& b, const Vertex& c, int id);
    // Strokes at a depth (za at the start, zb at the end); overlay_depth paints on top.
    void line(double ax, double ay, double bx, double by, double thickness, Rgba color, int id,
              double za = overlay_depth, double zb = overlay_depth);
    void disc(double x, double y, double radius, Rgba color, int id, double z = overlay_depth,
              int segments = 20);

    // The lake panorama: GPIX version 1, 1024 x 512 RGBA. False leaves a plain sky.
    bool load_environment(const std::vector<std::uint8_t>& file);
    [[nodiscard]] bool has_environment() const {
        return !environment_.empty();
    }

private:
    std::vector<std::uint8_t> environment_;
    int env_width_ = 0;
    int env_height_ = 0;
    void sample(double u, double v, double out[3]) const;
};

// Lays a raster over a BGRA (or RGBA, when `rgba`) premultiplied frame, scaled to the box
// (x, y, w, h) in frame pixels with bilinear filtering, at the given opacity.
void composite(const Raster3D& raster, std::uint8_t* frame, int frame_width, int frame_height,
               std::size_t row_bytes, bool rgba, double x, double y, double w, double h, double opacity);

}  // namespace ps_cube
