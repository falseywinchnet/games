#include "picture.hpp"

#include "r2d.hpp"

#include "inflate.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ps_cube {
namespace {

std::uint32_t big_endian(std::span<const std::uint8_t> bytes, std::size_t at) {
    const std::uint32_t value = (static_cast<std::uint32_t>(bytes[at]) << 24) |
                                (static_cast<std::uint32_t>(bytes[at + 1]) << 16) |
                                (static_cast<std::uint32_t>(bytes[at + 2]) << 8) | bytes[at + 3];
    return value;
}

int paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) {
        return a;
    }
    return pb <= pc ? b : c;
}

// Reverses the PNG row filters in place. Rows are `stride` bytes after a filter byte.
bool unfilter(std::vector<std::uint8_t>& rows, std::size_t stride, int height, int pixel_bytes) {
    std::vector<std::uint8_t> previous(stride, 0);
    for (int y = 0; y < height; ++y) {
        std::uint8_t* row = rows.data() + static_cast<std::size_t>(y) * (stride + 1);
        const int filter = row[0];
        std::uint8_t* line = row + 1;
        for (std::size_t x = 0; x < stride; ++x) {
            const int left = x >= static_cast<std::size_t>(pixel_bytes) ? line[x - static_cast<std::size_t>(pixel_bytes)] : 0;
            const int up = previous[x];
            const int corner = x >= static_cast<std::size_t>(pixel_bytes) ? previous[x - static_cast<std::size_t>(pixel_bytes)] : 0;
            int add = 0;
            if (filter == 1) {
                add = left;
            } else if (filter == 2) {
                add = up;
            } else if (filter == 3) {
                add = (left + up) / 2;
            } else if (filter == 4) {
                add = paeth(left, up, corner);
            } else if (filter != 0) {
                return false;
            }
            line[x] = static_cast<std::uint8_t>((line[x] + add) & 255);
        }
        std::copy(line, line + stride, previous.begin());
    }
    return true;
}

void sample(const Picture& picture, double x, double y, double out[3]) {
    const double fx = std::clamp(x - .5, 0.0, static_cast<double>(picture.width - 1));
    const double fy = std::clamp(y - .5, 0.0, static_cast<double>(picture.height - 1));
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    const int x1 = std::min(x0 + 1, picture.width - 1);
    const int y1 = std::min(y0 + 1, picture.height - 1);
    const double tx = fx - x0;
    const double ty = fy - y0;
    const std::size_t w = static_cast<std::size_t>(picture.width);
    for (int c = 0; c < 3; ++c) {
        const std::size_t k = static_cast<std::size_t>(c);
        const double top = picture.rgb[(static_cast<std::size_t>(y0) * w + static_cast<std::size_t>(x0)) * 3 + k] * (1 - tx) +
                           picture.rgb[(static_cast<std::size_t>(y0) * w + static_cast<std::size_t>(x1)) * 3 + k] * tx;
        const double bottom = picture.rgb[(static_cast<std::size_t>(y1) * w + static_cast<std::size_t>(x0)) * 3 + k] * (1 - tx) +
                              picture.rgb[(static_cast<std::size_t>(y1) * w + static_cast<std::size_t>(x1)) * 3 + k] * tx;
        out[c] = top * (1 - ty) + bottom * ty;
    }
}

}  // namespace

bool decode_png(std::span<const std::uint8_t> file, Picture& out) {
    out = Picture{};
    const std::uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (file.size() < 33 || !std::equal(signature, signature + 8, file.begin())) {
        return false;
    }
    std::size_t at = 8;
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<std::uint8_t> compressed;
    while (at + 12 <= file.size()) {
        const std::size_t length = big_endian(file, at);
        if (at + 12 + length > file.size()) {
            return false;
        }
        const std::span<const std::uint8_t> type = file.subspan(at + 4, 4);
        const std::span<const std::uint8_t> data = file.subspan(at + 8, length);
        const bool header = type[0] == 'I' && type[1] == 'H' && type[2] == 'D' && type[3] == 'R';
        const bool image = type[0] == 'I' && type[1] == 'D' && type[2] == 'A' && type[3] == 'T';
        const bool end = type[0] == 'I' && type[1] == 'E' && type[2] == 'N' && type[3] == 'D';
        if (header) {
            if (length != 13) {
                return false;
            }
            width = static_cast<int>(big_endian(data, 0));
            height = static_cast<int>(big_endian(data, 4));
            const int depth = data[8];
            const int colour = data[9];
            const int interlace = data[12];
            channels = colour == 2 ? 3 : colour == 6 ? 4 : 0;
            if (depth != 8 || channels == 0 || interlace != 0 || width <= 0 || height <= 0 ||
                width > 8192 || height > 8192) {
                return false;
            }
        } else if (image) {
            compressed.insert(compressed.end(), data.begin(), data.end());
        } else if (end) {
            break;
        }
        at += 12 + length;
    }
    if (channels == 0 || compressed.empty()) {
        return false;
    }
    const std::size_t stride = static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
    std::vector<std::uint8_t> rows;
    if (!ambient::inflate_zlib(compressed, (stride + 1) * static_cast<std::size_t>(height), rows) ||
        !unfilter(rows, stride, height, channels)) {
        return false;
    }
    out.width = width;
    out.height = height;
    out.rgb.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* line = rows.data() + static_cast<std::size_t>(y) * (stride + 1) + 1;
        std::uint8_t* target = out.rgb.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 3;
        for (int x = 0; x < width; ++x) {
            const std::size_t from = static_cast<std::size_t>(x) * static_cast<std::size_t>(channels);
            const std::size_t to = static_cast<std::size_t>(x) * 3;
            target[to] = line[from];
            target[to + 1] = line[from + 1];
            target[to + 2] = line[from + 2];
        }
    }
    return true;
}

void cover(const Picture& picture, const render::Target& target) {
    const int width = target.width;
    const int height = target.height;
    if (picture.width <= 0 || width <= 0 || height <= 0) {
        render::r2d::fill(target, target.bounds(), render::Color{40, 62, 60});
        return;
    }
    const double fit = std::max(static_cast<double>(width) / picture.width, static_cast<double>(height) / picture.height);
    const double offset_x = (picture.width * fit - width) * .5;
    const double offset_y = (picture.height * fit - height) * .5;
    // Reducing, four samples a pixel keep the trees from shimmering into noise.
    const int taps = fit < .75 ? 2 : 1;
    const double darken = 1 - 56.0 / 255;
    const double tint[3] = {8 * (1 - darken), 24 * (1 - darken), 24 * (1 - darken)};
    for (int y = 0; y < height; ++y) {
        std::uint32_t* row = target.row(y);
        for (int x = 0; x < width; ++x) {
            double sum[3] = {0, 0, 0};
            for (int j = 0; j < taps; ++j) {
                for (int i = 0; i < taps; ++i) {
                    double colour[3] = {0, 0, 0};
                    const double sx = (x + (i + .5) / taps + offset_x) / fit;
                    const double sy = (y + (j + .5) / taps + offset_y) / fit;
                    sample(picture, sx, sy, colour);
                    sum[0] += colour[0];
                    sum[1] += colour[1];
                    sum[2] += colour[2];
                }
            }
            const double n = taps * taps;
            row[x] = render::pack(target.order, static_cast<float>(sum[0] / n * darken + tint[0]),
                                  static_cast<float>(sum[1] / n * darken + tint[1]),
                                  static_cast<float>(sum[2] / n * darken + tint[2]));
        }
    }
}

}  // namespace ps_cube
