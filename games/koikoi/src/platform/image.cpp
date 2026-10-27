#include "image.hpp"
#include "inflate.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>
namespace kk {
namespace {
std::uint32_t big_endian(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}
std::uint8_t paeth(int left, int up, int corner) {
    const int estimate = left + up - corner;
    const int a = std::abs(estimate - left), b = std::abs(estimate - up), c = std::abs(estimate - corner);
    return static_cast<std::uint8_t>(a <= b && a <= c ? left : (b <= c ? up : corner));
}
// Reverses PNG's per-row filters in place (four bytes per pixel). rows holds a filter byte before each row.
bool unfilter(std::vector<std::uint8_t>& rows, std::size_t stride, std::uint32_t height) {
    const std::vector<std::uint8_t> zero(stride, 0);
    for (std::uint32_t y = 0; y < height; ++y) {
        std::uint8_t* row = rows.data() + static_cast<std::size_t>(y) * (stride + 1) + 1;
        const std::uint8_t* up = y ? row - (stride + 1) : zero.data();
        const std::uint8_t filter = row[-1];
        if (filter == 1) {
            for (std::size_t i = 4; i < stride; ++i) row[i] = static_cast<std::uint8_t>(row[i] + row[i - 4]);
        } else if (filter == 2) {
            for (std::size_t i = 0; i < stride; ++i) row[i] = static_cast<std::uint8_t>(row[i] + up[i]);
        } else if (filter == 3) {
            for (std::size_t i = 0; i < 4; ++i) row[i] = static_cast<std::uint8_t>(row[i] + (up[i] >> 1));
            for (std::size_t i = 4; i < stride; ++i) row[i] = static_cast<std::uint8_t>(row[i] + ((row[i - 4] + up[i]) >> 1));
        } else if (filter == 4) {
            for (std::size_t i = 0; i < 4; ++i) row[i] = static_cast<std::uint8_t>(row[i] + up[i]);
            for (std::size_t i = 4; i < stride; ++i) row[i] = static_cast<std::uint8_t>(row[i] + paeth(row[i - 4], up[i], up[i - 4]));
        } else if (filter != 0) {
            return false;
        }
    }
    return true;
}
struct LoadQueue {
    const std::vector<std::string>* paths = nullptr;
    const std::vector<Canvas*>* out = nullptr;
    std::atomic<std::size_t> next{0};
    std::atomic<int> loaded{0};
};
void load_worker(LoadQueue* queue) {
    LoadQueue& work = *queue;
    for (std::size_t i = work.next++; i < (*work.paths).size(); i = work.next++)
        if (load_png((*work.paths)[i], *(*work.out)[i])) ++work.loaded;
}
}  // namespace

bool decode_png(std::span<const std::uint8_t> file, Canvas& out) {
    static const std::array<std::uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (file.size() < signature.size() || !std::equal(signature.begin(), signature.end(), file.begin())) return false;
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> compressed;
    bool header = false, ended = false;
    std::size_t at = signature.size();
    while (!ended) {
        if (file.size() - at < 12) return false;
        const std::uint32_t length = big_endian(&file[at]);
        if (length > file.size() - at - 12) return false;
        const std::uint8_t* type = &file[at + 4];
        const std::uint8_t* data = &file[at + 8];
        // The image data is covered by the zlib stream's own Adler-32, which inflate checks; a second checksum
        // over it would cost a fifth of the decode.
        const bool image_data = std::memcmp(type, "IDAT", 4) == 0;
        if (!image_data && ambient::crc32(file.subspan(at + 4, length + 4)) != big_endian(&file[at + 8 + length])) return false;
        if (!header) {
            // Only the layout the preparation writes: 8-bit RGBA, deflate, standard filters, no interlace.
            if (std::memcmp(type, "IHDR", 4) != 0 || length != 13) return false;
            width = big_endian(data);
            height = big_endian(data + 4);
            if (width == 0 || height == 0 || width > 4096 || height > 4096) return false;
            if (data[8] != 8 || data[9] != 6 || data[10] != 0 || data[11] != 0 || data[12] != 0) return false;
            header = true;
        } else if (image_data) {
            compressed.insert(compressed.end(), data, data + length);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            ended = true;
        } else if ((type[0] & 32) == 0) {
            return false;  // an unknown critical chunk (a palette, for one) changes the meaning of the pixels
        }
        at += 12 + static_cast<std::size_t>(length);
    }
    if (at != file.size()) return false;
    const std::size_t stride = static_cast<std::size_t>(width) * 4;
    std::vector<std::uint8_t> rows;
    if (!ambient::inflate_zlib(compressed, (stride + 1) * height, rows) || !unfilter(rows, stride, height)) return false;
    Canvas decoded;
    decoded.resize(static_cast<int>(width), static_cast<int>(height));
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* source = rows.data() + static_cast<std::size_t>(y) * (stride + 1) + 1;
        std::uint8_t* target = decoded.px.data() + static_cast<std::size_t>(y) * stride;
        for (std::size_t i = 0; i < stride; i += 4) {
            const unsigned alpha = source[i + 3];
            target[i] = static_cast<std::uint8_t>((source[i + 2] * alpha + 127) / 255);
            target[i + 1] = static_cast<std::uint8_t>((source[i + 1] * alpha + 127) / 255);
            target[i + 2] = static_cast<std::uint8_t>((source[i] * alpha + 127) / 255);
            target[i + 3] = static_cast<std::uint8_t>(alpha);
        }
    }
    out = std::move(decoded);
    return true;
}
bool load_png(const std::string& path, Canvas& out) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const std::streamoff size = input ? static_cast<std::streamoff>(input.tellg()) : -1;
    if (size <= 0 || size > (64 << 20)) return false;
    std::vector<std::uint8_t> file(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(file.data()), static_cast<std::streamsize>(size))) return false;
    return decode_png(file, out);
}
int load_pngs(const std::vector<std::string>& paths, const std::vector<Canvas*>& out) {
    if (paths.empty() || paths.size() != out.size()) return 0;
    LoadQueue queue;
    queue.paths = &paths;
    queue.out = &out;
    const std::size_t helpers = std::min<std::size_t>({paths.size(), std::max(1U, std::thread::hardware_concurrency()), 8}) - 1;
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < helpers; ++i) threads.emplace_back(load_worker, &queue);
    load_worker(&queue);
    for (std::thread& thread : threads) thread.join();
    return queue.loaded;
}
void reduce(const Canvas& src, int w, int h, Canvas& out) {
    w = std::max(1, w);
    h = std::max(1, h);
    out.resize(w, h);
    const double sx = static_cast<double>(src.w) / w, sy = static_cast<double>(src.h) / h;
    for (int y = 0; y < h; ++y) {
        const double y0 = y * sy, y1 = (y + 1) * sy;
        for (int x = 0; x < w; ++x) {
            const double x0 = x * sx, x1 = (x + 1) * sx;
            double acc[4] = {0, 0, 0, 0}, wt = 0;
            for (int yy = static_cast<int>(y0); yy < std::min(src.h, static_cast<int>(std::ceil(y1))); ++yy) {
                const double fy = std::min(y1, yy + 1.0) - std::max(y0, static_cast<double>(yy));
                for (int xx = static_cast<int>(x0); xx < std::min(src.w, static_cast<int>(std::ceil(x1))); ++xx) {
                    const double f = fy * (std::min(x1, xx + 1.0) - std::max(x0, static_cast<double>(xx)));
                    const std::uint8_t* p = &src.px[(static_cast<size_t>(yy) * src.w + xx) * 4];
                    for (int c = 0; c < 4; ++c) acc[c] += p[c] * f;
                    wt += f;
                }
            }
            std::uint8_t* o = &out.px[(static_cast<size_t>(y) * w + x) * 4];
            for (int c = 0; c < 4; ++c) o[c] = static_cast<std::uint8_t>(std::clamp(acc[c] / std::max(wt, 1e-9) + .5, 0.0, 255.0));
        }
    }
}

}  // namespace kk
