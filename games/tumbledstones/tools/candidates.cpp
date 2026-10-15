// Candidate sheets of the stones and the tray, for the owner to choose from before the
// look goes into the game:
//
//   families.png  each style of family in its eight orientations, every stone in a
//                 different mineral, as the player meets them;
//   minerals.png  every mineral, three stones of each;
//   board-wood.png, board-felt.png  a whole board at 1100 x 760 points, scale 2, with
//                 sockets of bare wood or lined with felt.
//
//   ts_candidates <out dir> [room label]
#include "minerals.hpp"
#include "noise.hpp"
#include "room.hpp"
#include "shapes.hpp"
#include "stone_render.hpp"
#include "tray.hpp"

#include "headless_text.hpp"
#include "png_writer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Sheet {
    int width = 0, height = 0;
    std::vector<std::uint32_t> pixels;  // BGRA words
};

void label(Sheet& sheet, const std::string& text, int x, int y, double size, std::uint32_t rgb, bool bold) {
    const kit::TextMask mask = kit::render_text(text, bold ? kit::Face::serif_bold : kit::Face::serif, size, 0, false);
    const std::uint32_t r = (rgb >> 16) & 0xffU;
    const std::uint32_t g = (rgb >> 8) & 0xffU;
    const std::uint32_t b = rgb & 0xffU;
    for (int my = 0; my < mask.h; ++my)
        for (int mx = 0; mx < mask.w; ++mx) {
            const int px = x + mx;
            const int py = y + my;
            if (px < 0 || py < 0 || px >= sheet.width || py >= sheet.height)
                continue;
            const std::uint32_t a = mask.a[static_cast<std::size_t>(my * mask.w + mx)];
            if (a == 0)
                continue;
            std::uint32_t& d = sheet.pixels[static_cast<std::size_t>(py * sheet.width + px)];
            const std::uint32_t db = d & 0xffU;
            const std::uint32_t dg = (d >> 8) & 0xffU;
            const std::uint32_t dr = (d >> 16) & 0xffU;
            const std::uint32_t nb = (db * (255 - a) + b * a) / 255;
            const std::uint32_t ng = (dg * (255 - a) + g * a) / 255;
            const std::uint32_t nr = (dr * (255 - a) + r * a) / 255;
            d = nb | (ng << 8) | (nr << 16) | 0xff000000U;
        }
}

bool save(const Sheet& sheet, const std::string& path) {
    std::vector<std::uint8_t> bytes(sheet.pixels.size() * 4);
    for (std::size_t i = 0; i < sheet.pixels.size(); ++i) {
        const std::uint32_t p = sheet.pixels[i];
        bytes[i * 4] = static_cast<std::uint8_t>(p & 0xffU);
        bytes[i * 4 + 1] = static_cast<std::uint8_t>((p >> 8) & 0xffU);
        bytes[i * 4 + 2] = static_cast<std::uint8_t>((p >> 16) & 0xffU);
        bytes[i * 4 + 3] = 255;
    }
    const bool ok = kit::write_png(path, sheet.width, sheet.height, bytes);
    std::printf("%s %s\n", ok ? "wrote" : "FAILED", path.c_str());
    return ok;
}

struct Stone {
    ts::Shape shape;
    ts::Finish finish;
    ts::MineralPicture picture;
};

void place(Sheet& sheet, ts::StoneWork& work, const Stone& stone, int orientation, double cx, double cy, double radius,
           int fineness) {
    const ts::StoneLook look{&stone.shape, &stone.finish, &stone.picture};
    ts::DrawStone how;
    how.radius = radius;
    how.fineness = fineness;
    ts::StonePicture picture;
    draw_stone(look, ts::orientation_pose(orientation), how, work, picture);
    ts::lay_stone(picture, sheet.pixels.data(), sheet.width, sheet.height, sheet.width, cx, cy, true, 0, 0,
                  sheet.width, sheet.height);
}

double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "ts_candidates <out dir> [room label]\n");
        return 2;
    }
    const std::string out = argv[1];
    const std::string room = argc > 2 ? argv[2] : "room";
    ts::StoneWork work;
    const std::uint32_t cream = 0xeee2c8;

    // Close up: petrified wood and scalloped chunks, both faces, in several minerals.
    if (argc > 3 && std::string(argv[3]) == "detail") {
        const double cell = 210;
        const int left = 30;
        const int top = 90;
        const int rows = 6;
        Sheet sheet;
        sheet.width = left + static_cast<int>(cell * 6) + 20;
        sheet.height = top + static_cast<int>(cell * rows) + 20;
        sheet.pixels.assign(static_cast<std::size_t>(sheet.width * sheet.height), 0U);
        ts::TrayPlan plan;
        plan.width = sheet.width;
        plan.height = sheet.height;
        plan.scale = 3;
        plan.grid_x = left;
        plan.grid_y = top;
        plan.cell = cell;
        plan.columns = 6;
        plan.rows = rows;
        plan.wall = 14;
        ts::draw_tray(plan, sheet.pixels.data(), sheet.width, true, 0, sheet.height);
        label(sheet, "Petrified wood (rows 1-3) and agate nodules (rows 4-6)", 24, 14, 22, cream, true);
        const char* columns[6] = {"face up", "face up", "face down", "face down", "turned", "turned over"};
        const ts::Mineral minerals[6] = {ts::Mineral::petrified, ts::Mineral::walnut, ts::Mineral::petrified,
                                         ts::Mineral::agate, ts::Mineral::obsidian, ts::Mineral::jasper};
        const ts::Mineral flake_minerals[6] = {ts::Mineral::agate, ts::Mineral::obsidian, ts::Mineral::agate,
                                               ts::Mineral::amethyst, ts::Mineral::smoky, ts::Mineral::lapis};
        const int orientations[6] = {0, 0, 4, 4, 1, 6};
        for (int c = 0; c < 6; ++c)
            label(sheet, c < 4 ? columns[c] : (c == 4 ? "a quarter turn" : "over, two turns"),
                  left + static_cast<int>(cell * c) + 16, 58, 16, cream, false);
        for (int r = 0; r < rows; ++r) {
            const bool wood = r < 3;
            Stone stone;
            stone.shape = ts::Shape::make(70000U + static_cast<std::uint32_t>(r) * 977U, wood ? ts::Style::wood : ts::Style::nodule);
            for (int c = 0; c < 6; ++c) {
                const ts::Mineral mineral = wood ? minerals[c] : flake_minerals[c];
                stone.finish = ts::make_finish(mineral, 300U + static_cast<std::uint32_t>(r * 6 + c) * 17U);
                ts::print_mineral(stone.finish, stone.shape, 192, stone.picture);
                place(sheet, work, stone, orientations[c], left + cell * (c + .5), top + cell * (r + .5), cell * .42, 2);
            }
        }
        save(sheet, out + "/detail-" + room + ".png");
        return 0;
    }
    if (argc > 3 && std::string(argv[3]) == "styles") {
        const double cell = 190;
        const int left = 30;
        const int top = 60;
        const ts::Style styles[7] = {ts::Style::shell, ts::Style::spiral, ts::Style::pebble, ts::Style::candy,
                                     ts::Style::bean, ts::Style::pillow, ts::Style::facet};
        Sheet sheet;
        sheet.width = left + static_cast<int>(cell * 6) + 20;
        sheet.height = top + static_cast<int>(cell * 7) + 20;
        sheet.pixels.assign(static_cast<std::size_t>(sheet.width * sheet.height), 0U);
        ts::TrayPlan plan;
        plan.width = sheet.width;
        plan.height = sheet.height;
        plan.scale = 3;
        plan.grid_x = left;
        plan.grid_y = top;
        plan.cell = cell;
        plan.columns = 6;
        plan.rows = 7;
        plan.wall = 14;
        ts::draw_tray(plan, sheet.pixels.data(), sheet.width, true, 0, sheet.height);
        label(sheet, "The other styles, close up: face up and face down in several minerals", 24, 14, 22, cream, true);
        const ts::Mineral minerals[6] = {ts::Mineral::bone, ts::Mineral::obsidian, ts::Mineral::amber,
                                         ts::Mineral::jade, ts::Mineral::labradorite, ts::Mineral::tigereye};
        const int orientations[6] = {0, 4, 0, 4, 1, 6};
        for (int r = 0; r < 7; ++r) {
            Stone stone;
            stone.shape = ts::Shape::make(81000U + static_cast<std::uint32_t>(r) * 577U, styles[r]);
            for (int c = 0; c < 6; ++c) {
                stone.finish = ts::make_finish(minerals[(c + r) % 6], 500U + static_cast<std::uint32_t>(r * 6 + c) * 19U);
                ts::print_mineral(stone.finish, stone.shape, 192, stone.picture);
                place(sheet, work, stone, orientations[c], left + cell * (c + .5), top + cell * (r + .5), cell * .42, 2);
            }
        }
        save(sheet, out + "/styles-" + room + ".png");
        return 0;
    }

    // Families: a row for each style, eight orientations across.
    {
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        const double cell = 104;
        const int left = 220;
        Sheet sheet;
        sheet.width = left + static_cast<int>(cell * 8) + 30;
        sheet.height = 70 + static_cast<int>(cell * ts::style_count) + 20;
        sheet.pixels.assign(static_cast<std::size_t>(sheet.width * sheet.height), 0U);
        ts::TrayPlan plan;
        plan.width = sheet.width;
        plan.height = sheet.height;
        plan.scale = 1.6;
        plan.grid_x = left;
        plan.grid_y = 70;
        plan.cell = cell;
        plan.columns = 8;
        plan.rows = ts::style_count;
        plan.wall = 12;
        ts::draw_tray(plan, sheet.pixels.data(), sheet.width, true, 0, sheet.height);
        const char* turns[8] = {"up", "up, 1 turn", "up, 2 turns", "up, 3 turns", "down", "down, 1 turn",
                                "down, 2 turns", "down, 3 turns"};
        for (int o = 0; o < 8; ++o)
            label(sheet, turns[o], left + static_cast<int>(cell * o) + 14, 40, 15, cream, false);
        label(sheet, "Families: one shape each row, every stone a different mineral", 24, 12, 18, cream, true);
        double least = 1e9;
        for (int s = 0; s < ts::style_count; ++s) {
            const ts::Style style = static_cast<ts::Style>(s);
            Stone stone;
            stone.shape = ts::Shape::make(1000U + static_cast<std::uint32_t>(s) * 77U, style);
            const double self = ts::self_distance(stone.shape);
            least = std::min(least, self);
            char text[96];
            std::snprintf(text, sizeof(text), "%s", ts::style_name(style));
            label(sheet, text, 24, 70 + static_cast<int>(cell * s) + 34, 18, cream, true);
            std::snprintf(text, sizeof(text), "turns apart %.3f", self);
            label(sheet, text, 24, 70 + static_cast<int>(cell * s) + 58, 13, 0xb8a888, false);
            for (int o = 0; o < 8; ++o) {
                stone.finish = ts::draw_finish(5000U + static_cast<std::uint32_t>(s * 8 + o) * 13U);
                ts::print_mineral(stone.finish, stone.shape, 112, stone.picture);
                place(sheet, work, stone, o, left + cell * (o + .5), 70 + cell * (s + .5), cell * .40, 2);
            }
        }
        save(sheet, out + "/families-" + room + ".png");
        std::printf("families: %.2f s, least self distance %.3f\n", seconds_since(start), least);
    }

    // Minerals: three of each.
    {
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        const double cell = 104;
        const int columns = 9;
        const int rows = (ts::mineral_count * 3 + columns - 1) / columns;
        Sheet sheet;
        sheet.width = static_cast<int>(cell * columns) + 60;
        sheet.height = 60 + static_cast<int>(cell * rows) + 30;
        sheet.pixels.assign(static_cast<std::size_t>(sheet.width * sheet.height), 0U);
        ts::TrayPlan plan;
        plan.width = sheet.width;
        plan.height = sheet.height;
        plan.scale = 1.6;
        plan.grid_x = 30;
        plan.grid_y = 60;
        plan.cell = cell;
        plan.columns = columns;
        plan.rows = rows;
        plan.wall = 12;
        ts::draw_tray(plan, sheet.pixels.data(), sheet.width, true, 0, sheet.height);
        label(sheet, "Minerals: any shape may come in any of them", 24, 16, 18, cream, true);
        for (int i = 0; i < ts::mineral_count * 3; ++i) {
            const ts::Mineral mineral = static_cast<ts::Mineral>(i / 3);
            Stone stone;
            stone.shape = ts::Shape::make(300U + static_cast<std::uint32_t>(i) * 31U,
                                          static_cast<ts::Style>((i * 5 + 1) % ts::style_count));
            stone.finish = ts::make_finish(mineral, 9000U + static_cast<std::uint32_t>(i) * 7U);
            ts::print_mineral(stone.finish, stone.shape, 112, stone.picture);
            const int column = i % columns;
            const int row = i / columns;
            const double cx = 30 + cell * (column + .5);
            const double cy = 60 + cell * (row + .5);
            place(sheet, work, stone, i % 8, cx, cy - 6, cell * .37, 2);
            if (i % 3 == 0)
                label(sheet, ts::mineral_name(mineral), static_cast<int>(cx - cell * .45), static_cast<int>(cy + cell * .36),
                      12, cream, false);
        }
        save(sheet, out + "/minerals-" + room + ".png");
        std::printf("minerals: %.2f s\n", seconds_since(start));
    }

    // Whole boards: 1100 x 760 points at scale 2, five families.
    for (int lining = 0; lining < 2; ++lining) {
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        const double scale = 2;
        Sheet sheet;
        sheet.width = static_cast<int>(1100 * scale);
        sheet.height = static_cast<int>(760 * scale);
        sheet.pixels.assign(static_cast<std::size_t>(sheet.width * sheet.height), 0U);
        const double cell = 66 * scale;
        ts::TrayPlan plan;
        plan.width = sheet.width;
        plan.height = sheet.height;
        plan.scale = scale;
        plan.cell = cell;
        plan.grid_x = (sheet.width - cell * 10) * .5 + 120 * scale;
        plan.grid_y = (sheet.height - cell * 10) * .5 + 8 * scale;
        plan.lining = lining == 0 ? ts::Lining::wood : ts::Lining::felt;
        plan.wall = 14 * scale;
        ts::draw_tray(plan, sheet.pixels.data(), sheet.width, true, 0, sheet.height);
        const double tray_seconds = seconds_since(start);
        std::vector<ts::Shape> families;
        const ts::Style styles[5] = {ts::Style::shell, ts::Style::wood, ts::Style::candy, ts::Style::spiral,
                                     ts::Style::pebble};
        for (int f = 0; f < 5; ++f)
            families.push_back(ts::Shape::make(4242U + static_cast<std::uint32_t>(f) * 101U, styles[f]));
        std::uint64_t random = 77;
        for (int row = 0; row < 10; ++row)
            for (int column = 0; column < 10; ++column) {
                Stone stone;
                stone.shape = families[static_cast<std::size_t>(ts::next_below(random, 5))];
                stone.finish = ts::draw_finish(static_cast<std::uint32_t>(ts::next_random(random)));
                ts::print_mineral(stone.finish, stone.shape, 112, stone.picture);
                place(sheet, work, stone, ts::next_below(random, 8), plan.grid_x + cell * (column + .5),
                      plan.grid_y + cell * (row + .5), cell * .40, 2);
            }
        label(sheet, "Tumbled Stones", static_cast<int>(40 * scale), static_cast<int>(90 * scale), 30 * scale, cream, true);
        label(sheet, lining == 0 ? "sockets of bare wood" : "sockets lined with felt", static_cast<int>(40 * scale),
              static_cast<int>(130 * scale), 15 * scale, 0xc8b898, false);
        save(sheet, out + "/board-" + std::string(lining == 0 ? "wood" : "felt") + "-" + room + ".png");
        std::printf("board: tray %.2f s, all %.2f s\n", tray_seconds, seconds_since(start));
    }
    return 0;
}
