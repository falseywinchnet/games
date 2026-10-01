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
};
class PuzzleRaster {
  public:
    int width = 1, height = 1;
    std::vector<std::byte> pixels;
    std::vector<int> ids;
    std::vector<double> depth;
    void resize(int w, int h);
    void clear();
    void triangle(RasterVertex a, RasterVertex b, RasterVertex c, int id);
    void line(Point2 a, Point2 b, double width, PixelColor color, int id, double z = -10);
    void disc(Point2 center, double radius, PixelColor color, int id, double z = -10);
    void gem(Point2 center, double radius, int value, double angle, double glisten, int id);
    void cube(const PuzzleGame& game, double yaw, double pitch, int hover);
    void load_environment(const std::string& file);

  private:
    std::vector<unsigned char> environment_;
    int env_width_ = 0, env_height_ = 0;
    PixelColor reflection(Point3 normal, Point3 position) const;
};
} // namespace games
