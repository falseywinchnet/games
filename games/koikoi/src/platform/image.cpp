#include "image.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <array>
namespace kk {
bool load_png(const std::string& path, Canvas& out) {
    std::filesystem::path pixels(path); pixels.replace_extension(".bgpix");
    std::ifstream input(pixels, std::ios::binary);
    std::array<unsigned char,16> header{};
    if (!input.read(reinterpret_cast<char*>(header.data()),16)) return false;
    if (header[0]!='B' || header[1]!='G' || header[2]!='P' || header[3]!='X') return false;
    std::array<std::uint32_t,3> fields{};
    for (int i=0;i<3;++i) for(int j=0;j<4;++j) fields[i] |= std::uint32_t(header[4+i*4+j]) << (j*8);
    if (fields[0]!=1 || fields[1]==0 || fields[2]==0 || fields[1]>4096 || fields[2]>4096) return false;
    Canvas decoded; decoded.resize(static_cast<int>(fields[1]),static_cast<int>(fields[2]));
    if (!input.read(reinterpret_cast<char*>(decoded.px.data()),static_cast<std::streamsize>(decoded.px.size()))) return false;
    if (input.peek()!=std::char_traits<char>::eof()) return false;
    out=std::move(decoded); return true;
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
