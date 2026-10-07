// The soil swatch sheet: every preset, each season and a few spoil heaps, written
// as one PPM so a person can judge the ground by eye. Timing goes to stdout.
//
//   soil_swatches sheet.ppm [texels_per_metre] [side] [zoom]
//
// The defaults show each swatch a metre across at 256 texels per metre. A small
// texels_per_metre with a zoom shows the ground as a game draws it, pixel by pixel.
#include "soil.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct Sheet {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb;  // width * height * 3
};

void place(Sheet& sheet, const soil::Image& image, int left, int top, int zoom, const std::uint8_t backdrop[3]) {
    for (int y = 0; y < image.height * zoom; ++y) {
        for (int x = 0; x < image.width * zoom; ++x) {
            const int sx = left + x;
            const int sy = top + y;
            if (sx < 0 || sy < 0 || sx >= sheet.width || sy >= sheet.height) continue;
            const std::size_t from = (static_cast<std::size_t>(y / zoom) * image.width + x / zoom) * 4;
            const std::size_t to = (static_cast<std::size_t>(sy) * sheet.width + sx) * 3;
            const int alpha = image.rgba[from + 3];
            for (int c = 0; c < 3; ++c) {
                const int over = image.rgba[from + c];
                sheet.rgb[to + c] = static_cast<std::uint8_t>((over * alpha + backdrop[c] * (255 - alpha)) / 255);
            }
        }
    }
}

bool write_ppm(const std::string& path, const Sheet& sheet) {
    FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) return false;
    std::fprintf(file, "P6 %d %d 255\n", sheet.width, sheet.height);
    const std::size_t written = std::fwrite(sheet.rgb.data(), 1, sheet.rgb.size(), file);
    std::fclose(file);
    return written == sheet.rgb.size();
}

double milliseconds_since(std::chrono::steady_clock::time_point start) {
    const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
    return elapsed.count();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: soil_swatches sheet.ppm [texels_per_metre] [side] [zoom]\n");
        return 1;
    }
    const double texels_per_metre = argc > 2 ? std::atof(argv[2]) : 256.0;
    const int side = argc > 3 ? std::atoi(argv[3]) : 256;
    const int zoom = argc > 4 ? std::max(1, std::atoi(argv[4])) : 1;
    const int gap = 6;
    const int cell = side * zoom + gap;
    const int columns = 5;
    // Rows: the ten presets; one ground through the four seasons and a darker
    // loam; spoil heaps.
    const int rows = 4;
    Sheet sheet;
    sheet.width = columns * cell + gap;
    sheet.height = rows * cell + gap;
    sheet.rgb.assign(static_cast<std::size_t>(sheet.width) * sheet.height * 3, 40);
    const std::uint8_t lawn[3] = {96, 132, 64};

    double total = 0.0;
    int made = 0;
    for (int index = 0; index < soil::preset_count; ++index) {
        soil::Parameters parameters = soil::preset(static_cast<soil::Preset>(index));
        parameters.texels_per_metre = texels_per_metre;
        parameters.seed = 7u + static_cast<std::uint32_t>(index);
        soil::Image image;
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        if (!soil::generate_surface(parameters, side, side, image)) return 2;
        const double spent = milliseconds_since(start);
        total += spent;
        ++made;
        std::printf("%-16s %4dx%-4d %7.1f ms\n", soil::preset_name(static_cast<soil::Preset>(index)), side, side, spent);
        place(sheet, image, gap + (index % columns) * cell, gap + (index / columns) * cell, zoom, lawn);
    }
    const soil::Season seasons[4] = {soil::Season::spring, soil::Season::summer, soil::Season::autumn, soil::Season::winter};
    const char* season_names[4] = {"spring", "summer", "autumn", "winter"};
    for (int index = 0; index < 5; ++index) {
        soil::Parameters parameters = soil::preset(soil::Preset::furrowed_rows);
        parameters.texels_per_metre = texels_per_metre;
        parameters.furrow_angle = 0.35;
        parameters.seed = 101u;
        if (index < 4) soil::apply_season(parameters, seasons[index]);
        else parameters.colour = soil::Colour{0.36, 0.28, 0.22};
        soil::Image image;
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        if (!soil::generate_surface(parameters, side, side, image)) return 2;
        const double spent = milliseconds_since(start);
        total += spent;
        ++made;
        std::printf("rows, %-10s %4dx%-4d %7.1f ms\n", index < 4 ? season_names[index] : "dark loam", side, side, spent);
        place(sheet, image, gap + index * cell, gap + 2 * cell, zoom, lawn);
    }
    for (int index = 0; index < 5; ++index) {
        soil::Parameters parameters = soil::preset(soil::Preset::fresh_spoil);
        parameters.texels_per_metre = texels_per_metre * 1.6;
        parameters.seed = 300u + static_cast<std::uint32_t>(index);
        if (index == 3) soil::apply_season(parameters, soil::Season::winter);
        if (index == 4) soil::apply_season(parameters, soil::Season::autumn);
        soil::Spoil spoil;
        spoil.fan_direction = 0.6 + index * 1.3;
        spoil.fan_spread = 0.8 + 0.15 * index;
        soil::Image image;
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        if (!soil::generate_spoil(parameters, spoil, side, side, image)) return 2;
        const double spent = milliseconds_since(start);
        total += spent;
        ++made;
        std::printf("spoil %d          %4dx%-4d %7.1f ms\n", index, side, side, spent);
        place(sheet, image, gap + index * cell, gap + 3 * cell, zoom, lawn);
    }
    std::printf("%d pictures, %.1f ms in all, %.1f ms each\n", made, total, total / made);
    return write_ppm(argv[1], sheet) ? 0 : 3;
}
