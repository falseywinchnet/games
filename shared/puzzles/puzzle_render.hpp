#pragma once
#include "puzzles.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>
namespace games {
struct PixelColor {
    double r = 0, g = 0, b = 0, a = 255;
};
struct RasterVertex {
    double x = 0, y = 0, z = 0;
    PixelColor color;
    double u = -1, v = -1;
    double env = 1; // how much of the environment reflection replaces the color
};
class PuzzleRaster {
  public:
    int width = 1, height = 1;
    std::vector<std::byte> pixels;
    std::vector<int> ids;
    std::vector<double> depth;
    double z_bias = 0; // added to ordinary depths; a lifted gem draws over its neighbors
    void resize(int w, int h);
    void clear();
    void triangle(RasterVertex a, RasterVertex b, RasterVertex c, int id);
    void line(Point2 a, Point2 b, double width, PixelColor color, int id, double z = -10);
    void line(Point2 a, Point2 b, double width, PixelColor color, int id, double za, double zb);
    void disc(Point2 center, double radius, PixelColor color, int id, double z = -10);
    void gem(Point2 center, double radius, int value, double angle, double glisten, int id);
    void cube(const PuzzleGame& game, double yaw, double pitch, int hover);
    void load_environment(const std::string& file);

  private:
    std::vector<unsigned char> environment_;
    int env_width_ = 0, env_height_ = 0;
    PixelColor reflection(Point3 normal, Point3 position) const;
};
// Puzzle Solve's glass, steel and plaster. Each is shaded per device pixel from exact
// outlines, so edges are sharp at every display scale, and drawn once into an image the
// view caches: painting a frame only places images.
struct GlassImage {
    int width = 0, height = 0;     // device pixels
    std::vector<std::byte> pixels; // width * height premultiplied BGRA, row-major
};
enum class GlassLook { placed, tray, lifted, used };
// One piece. Cells are normalized (smallest x and y are 0). Its cell origin lands at
// (margin, margin) device pixels inside the image. The light comes from the upper left
// and stays there: a piece's sheen runs along its longest edge, on the side nearer the
// light, so turning a piece moves the highlight across it as on a real glass bar.
struct GlassPiece {
    std::vector<PieceCell> cells;
    double unit = 40;  // device pixels per cell
    double scale = 1;  // device pixels per point, for line widths
    double margin = 0; // device pixels around the piece, room for its shadow
    GlassLook look = GlassLook::placed;
};
[[nodiscard]] double glass_margin(GlassLook look, double unit, double scale);
void render_glass(const GlassPiece& piece, GlassImage& out);
// The raised steel tray around the frame and the plaster well inside it. The frame's cell
// origin lands at (margin + border, margin + border).
struct SolveTrayArt {
    std::vector<Point2> outline; // the frame, cell units
    int columns = 4, rows = 4;
    double unit = 40, scale = 1;
    double border = 16; // device pixels of steel
    double margin = 8;  // device pixels beyond the steel, room for its shadow
};
void render_solve_tray(const SolveTrayArt& art, GlassImage& out);
// The design: the picture in flat enamel inside a thin steel line, cell origin at
// (margin, margin).
struct SolveDesignArt {
    std::array<int, 96> picture{}; // secret: 1 blue, 2 yellow, 0 outside
    std::vector<Point2> outline;
    int columns = 4, rows = 4;
    double unit = 20, scale = 1, margin = 3;
};
void render_solve_design(const SolveDesignArt& art, GlassImage& out);
// A seamless tile of lime plaster, side device pixels square.
void render_plaster(int side, double scale, GlassImage& out);
} // namespace games
