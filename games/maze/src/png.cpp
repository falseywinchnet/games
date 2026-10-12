#include "png.hpp"

#include "inflate.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

namespace mz {
namespace {

std::uint32_t big_endian(const std::uint8_t* p) {
    return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | std::uint32_t(p[3]);
}
std::uint8_t paeth(int left, int up, int corner) {
    const int estimate = left + up - corner;
    const int a = std::abs(estimate - left), b = std::abs(estimate - up), c = std::abs(estimate - corner);
    return static_cast<std::uint8_t>(a <= b && a <= c ? left : (b <= c ? up : corner));
}
// Reverses PNG's per-row filters in place; `rows` holds a filter byte before each row.
bool unfilter(std::vector<std::uint8_t>& rows, std::size_t stride, std::size_t bpp, std::uint32_t height) {
    const std::vector<std::uint8_t> zero(stride, 0);
    for (std::uint32_t y = 0; y < height; ++y) {
        std::uint8_t* row = rows.data() + static_cast<std::size_t>(y) * (stride + 1) + 1;
        const std::uint8_t* up = y ? row - (stride + 1) : zero.data();
        switch (row[-1]) {
            case 0: break;
            case 1: for (std::size_t i = bpp; i < stride; ++i) row[i] = static_cast<std::uint8_t>(row[i] + row[i - bpp]); break;
            case 2: for (std::size_t i = 0; i < stride; ++i) row[i] = static_cast<std::uint8_t>(row[i] + up[i]); break;
            case 3:
                for (std::size_t i = 0; i < stride; ++i) row[i] = static_cast<std::uint8_t>(row[i] + (((i >= bpp ? row[i - bpp] : 0) + up[i]) >> 1));
                break;
            case 4:
                for (std::size_t i = 0; i < stride; ++i)
                    row[i] = static_cast<std::uint8_t>(row[i] + paeth(i >= bpp ? row[i - bpp] : 0, up[i], i >= bpp ? up[i - bpp] : 0));
                break;
            default: return false;
        }
    }
    return true;
}

}  // namespace

bool decode_png(std::span<const std::uint8_t> file, Tex32& out) {
    static const std::array<std::uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (file.size() < signature.size() || !std::equal(signature.begin(), signature.end(), file.begin())) return false;
    std::uint32_t width = 0, height = 0;
    std::size_t bpp = 0;
    std::vector<std::uint8_t> compressed;
    bool header = false, ended = false;
    std::size_t at = signature.size();
    while (!ended) {
        if (file.size() - at < 12) return false;
        const std::uint32_t length = big_endian(&file[at]);
        if (length > file.size() - at - 12) return false;
        const std::uint8_t* type = &file[at + 4];
        const std::uint8_t* data = &file[at + 8];
        const bool image_data = std::memcmp(type, "IDAT", 4) == 0;  // covered by the zlib stream's own checksum
        if (!image_data && ambient::crc32(file.subspan(at + 4, length + 4)) != big_endian(&file[at + 8 + length])) return false;
        if (!header) {
            if (std::memcmp(type, "IHDR", 4) != 0 || length != 13) return false;
            width = big_endian(data);
            height = big_endian(data + 4);
            if (width == 0 || height == 0 || width > 4096 || height > 4096) return false;
            if (data[8] != 8 || (data[9] != 2 && data[9] != 6) || data[10] != 0 || data[11] != 0 || data[12] != 0) return false;
            bpp = data[9] == 2 ? 3 : 4;
            header = true;
        } else if (image_data) {
            compressed.insert(compressed.end(), data, data + length);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            ended = true;
        } else if ((type[0] & 32) == 0) {
            return false;  // an unknown critical chunk changes what the pixels mean
        }
        at += 12 + static_cast<std::size_t>(length);
    }
    // a texel map wants power-of-two sides
    if ((width & (width - 1)) != 0 || (height & (height - 1)) != 0) return false;
    const std::size_t stride = static_cast<std::size_t>(width) * bpp;
    std::vector<std::uint8_t> rows;
    if (!ambient::inflate_zlib(compressed, (stride + 1) * height, rows) || !unfilter(rows, stride, bpp, height)) return false;
    Tex32 t;
    t.make(static_cast<int>(width), static_cast<int>(height));
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* s = rows.data() + static_cast<std::size_t>(y) * (stride + 1) + 1;
        for (std::uint32_t x = 0; x < width; ++x, s += bpp) {
            const std::uint32_t a = bpp == 4 ? s[3] : 255u;
            t.px[static_cast<std::size_t>(y) * width + x] = a << 24 | std::uint32_t(s[0]) << 16 | std::uint32_t(s[1]) << 8 | s[2];
        }
    }
    out = std::move(t);
    return true;
}

bool load_png(const std::string& path, Tex32& out) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const std::streamoff size = input ? static_cast<std::streamoff>(input.tellg()) : -1;
    if (size <= 0 || size > (16 << 20)) return false;
    std::vector<std::uint8_t> file(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(file.data()), static_cast<std::streamsize>(size))) return false;
    return decode_png(file, out);
}

}  // namespace mz
