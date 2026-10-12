#include "table_light.hpp"

#include <algorithm>
#include <cmath>

namespace games {
namespace {

// The bake's grid, as authoring/table_light/export.py writes it.
constexpr int columns = 49, rows = 41;
constexpr double across = 1.2, deep = 1.0;  // metres
constexpr double card_metres = .063;

double at(const float* grid, int channels, int channel, int row, int column) {
    return static_cast<double>(grid[(row * columns + column) * channels + channel]);
}

// A grid value at fractional column and row, bilinear, clamped to the grid.
double sample(const float* grid, int channels, int channel, double column, double row) {
    column = std::clamp(column, 0.0, columns - 1.0);
    row = std::clamp(row, 0.0, rows - 1.0);
    const int c = std::min(static_cast<int>(column), columns - 2);
    const int r = std::min(static_cast<int>(row), rows - 2);
    const double fc = column - c, fr = row - r;
    return (at(grid, channels, channel, r, c) * (1 - fc) +
            at(grid, channels, channel, r, c + 1) * fc) * (1 - fr) +
           (at(grid, channels, channel, r + 1, c) * (1 - fc) +
            at(grid, channels, channel, r + 1, c + 1) * fc) * fr;
}

std::byte unit_byte(double v) {
    return static_cast<std::byte>(std::lround(std::clamp(v, 0.0, 1.0) * 255));
}

}  // namespace

TableLight light_table(double width, double height, double card_width, double cell) {
    TableLight light;
    light.width = std::max(2, static_cast<int>(std::ceil(width / cell)));
    light.height = std::max(2, static_cast<int>(std::ceil(height / cell)));
    light.face.resize(static_cast<std::size_t>(light.width) * light.height * 4);
    const double metres = card_metres / std::max(1.0, card_width);  // per point
    for (int y = 0; y < light.height; ++y)
        for (int x = 0; x < light.width; ++x) {
            // The cell's middle on the grid: across from the eyes, and in from its far side
            // (the table's near edge lies on the grid's).
            const double px = (x + .5) * width / light.width;
            const double py = (y + .5) * height / light.height;
            const double column = ((px - width * .5) * metres / across + .5) * (columns - 1);
            const double row = (deep - (height - py) * metres) / deep * (rows - 1);
            const double level = sample(detail::light_level, 1, 0, column, row);
            std::byte* out =
                light.face.data() + (static_cast<std::size_t>(y) * light.width + x) * 4;
            // BGRA. The gloss may exceed the alpha: drawn over the face it adds to it.
            for (int c = 0; c < 3; ++c)
                out[c] = unit_byte(sample(detail::light_gloss, 3, 2 - c, column, row));
            out[3] = unit_byte(1 - level);
        }
    return light;
}

}  // namespace games
