// conv_tool IN.f64 W H TW TH OUT.f64 [CHANNELS]: doubles through Plan Paint's CONV area resize.
// One channel (replicated, alpha 1) or four (premultiplied RGBA, as CONV takes them).
#include <cstdio>
#include <cstdlib>
#include <vector>
namespace paint { void conv_area_doubles(const std::vector<double>&, int, int, int, int, std::vector<double>&); }
int main(int argc, char** argv) {
    if (argc < 7) return 1;
    const int w = std::atoi(argv[2]), h = std::atoi(argv[3]), tw = std::atoi(argv[4]), th = std::atoi(argv[5]);
    const int channels = argc > 7 ? std::atoi(argv[7]) : 1;
    std::vector<double> in(static_cast<size_t>(w) * h * channels);
    FILE* f = std::fopen(argv[1], "rb");
    if (!f || std::fread(in.data(), sizeof(double), in.size(), f) != in.size()) return 2;
    std::fclose(f);
    std::vector<double> rgba(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        if (channels == 4) { for (int c = 0; c < 4; ++c) rgba[i * 4 + c] = in[i * 4 + c]; }
        else { rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = in[i]; rgba[i * 4 + 3] = 1.0; }
    }
    std::vector<double> out;
    paint::conv_area_doubles(rgba, w, h, tw, th, out);
    std::vector<double> res(static_cast<size_t>(tw) * th * channels);
    for (size_t i = 0; i < static_cast<size_t>(tw) * th; ++i) {
        if (channels == 4) { for (int c = 0; c < 4; ++c) res[i * 4 + c] = out[i * 4 + c]; }
        else res[i] = out[i * 4];
    }
    FILE* o = std::fopen(argv[6], "wb");
    std::fwrite(res.data(), sizeof(double), res.size(), o);
    std::fclose(o);
    return 0;
}
