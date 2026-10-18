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

#include "carpet.hpp"
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

// Specimen Cabinet layout mock-ups: a box in device pixels.
struct Area {
    double x = 0, y = 0, w = 0, h = 0;
};

// A felt strip: deep green baize, a fine nap, sunk a little into the wood.
void felt(Sheet& sheet, const Area& a, double scale) {
    const double radius = 10 * scale;
    for (int y = static_cast<int>(a.y); y < static_cast<int>(a.y + a.h); ++y)
        for (int x = static_cast<int>(a.x); x < static_cast<int>(a.x + a.w); ++x) {
            if (x < 0 || y < 0 || x >= sheet.width || y >= sheet.height)
                continue;
            const double dx = std::max({a.x + radius - x, 0.0, x - (a.x + a.w - radius)});
            const double dy = std::max({a.y + radius - y, 0.0, y - (a.y + a.h - radius)});
            if (dx * dx + dy * dy > radius * radius)
                continue;
            const double nap = .06 * ts::fbm2(x * .9 / scale, y * .9 / scale, 7, 3) + .03 * ts::noise2(x * 3.0, y * 3.0, 9);
            // Shade where the wood's edge overhangs it, top and left.
            const double inset = std::min({(x - a.x) / (14 * scale), (y - a.y) / (14 * scale), 1.0});
            const double light = (.82 + nap) * (.55 + .45 * inset);
            const double r = 34 * light, g = 86 * light, b = 62 * light;
            sheet.pixels[static_cast<std::size_t>(y * sheet.width + x)] =
                static_cast<std::uint32_t>(std::clamp(b, 0.0, 255.0)) | (static_cast<std::uint32_t>(std::clamp(g, 0.0, 255.0)) << 8) |
                (static_cast<std::uint32_t>(std::clamp(r, 0.0, 255.0)) << 16) | 0xff000000U;
        }
}

// The tumbler, seen from above: a turned walnut bowl, a brass band round its rim, dark
// inside where the stones lie jumbled.
void bowl(Sheet& sheet, double cx, double cy, double radius, double scale) {
    for (int y = static_cast<int>(cy - radius - 2); y <= static_cast<int>(cy + radius + 2); ++y)
        for (int x = static_cast<int>(cx - radius - 2); x <= static_cast<int>(cx + radius + 2); ++x) {
            if (x < 0 || y < 0 || x >= sheet.width || y >= sheet.height)
                continue;
            const double d = std::hypot(x - cx, y - cy) / radius;
            if (d > 1.0)
                continue;
            double r = 0, g = 0, b = 0;
            const double band = 8 * scale / radius;
            if (d > 1 - band) {
                // Brass, lit from the window above and left.
                const double lit = .6 + .4 * (-(x - cx) - (y - cy)) / (radius * 1.4);
                r = 200 * lit;
                g = 156 * lit;
                b = 78 * lit;
            } else {
                // The bowl's inside: walnut, darker towards the middle and the far wall.
                const double depth = 1 - d / (1 - band);
                const double grain = .08 * ts::noise2(x * .05 / scale, y * .5 / scale, 11);
                const double wall = .35 + .65 * std::pow(d / (1 - band), 3) * (.6 + .4 * ((x - cx) + (y - cy)) / radius);
                const double shade = (.28 + .3 * wall + grain) * (1 - .35 * depth);
                r = 120 * shade;
                g = 74 * shade;
                b = 44 * shade;
            }
            sheet.pixels[static_cast<std::size_t>(y * sheet.width + x)] =
                static_cast<std::uint32_t>(std::clamp(b, 0.0, 255.0)) | (static_cast<std::uint32_t>(std::clamp(g, 0.0, 255.0)) << 8) |
                (static_cast<std::uint32_t>(std::clamp(r, 0.0, 255.0)) << 16) | 0xff000000U;
        }
}

// Felt from the suite's fibre renderer, a tile laid over the area, a shade where the wood's
// edge overhangs it.
void felt_cloth(Sheet& sheet, const Area& a, const paint::Image& tile, double scale) {
    const double radius = 10 * scale;
    for (int y = static_cast<int>(a.y); y < static_cast<int>(a.y + a.h); ++y)
        for (int x = static_cast<int>(a.x); x < static_cast<int>(a.x + a.w); ++x) {
            if (x < 0 || y < 0 || x >= sheet.width || y >= sheet.height)
                continue;
            const double dx = std::max({a.x + radius - x, 0.0, x - (a.x + a.w - radius)});
            const double dy = std::max({a.y + radius - y, 0.0, y - (a.y + a.h - radius)});
            if (dx * dx + dy * dy > radius * radius)
                continue;
            const paint::Color& c = tile.pixels[static_cast<std::size_t>((y % tile.height) * tile.width + (x % tile.width))];
            const double inset = std::min({(x - a.x) / (12 * scale), (y - a.y) / (12 * scale), 1.0});
            const double light = .5 + .5 * inset;
            sheet.pixels[static_cast<std::size_t>(y * sheet.width + x)] = static_cast<std::uint32_t>(c.b * light) |
                (static_cast<std::uint32_t>(c.g * light) << 8) | (static_cast<std::uint32_t>(c.r * light) << 16) | 0xff000000U;
        }
}

// The tumbler: a drum tipped with its open mouth raised towards the felt, as a mixer's is,
// its body running off beyond the window's top edge. Fins inside lift the stones as it
// turns and spill them over the lip onto the felt. Ray cast from straight above (the
// tray's own view), lit by the room's bake as the stones are, and casting its shadow.
//   style 0: black rubber, ribbed; style 1: walnut staves, a brass lip; style 2: copper,
//   a riveted band.
struct Drum {
    double c[3];      // the mouth's centre: screen x, screen y (down), height above the table
    double a[3];      // the axis, from the mouth into the drum
    double u[3];      // two directions round it
    double v[3];
    double radius;
    double wall;
    double turn;
    int style;
};

// Where a vertical ray at (x, y) first meets the cylinder of radius `r` about the drum's
// axis, from above (first) or from below (!first): its height, or false.
bool cylinder_hit(const Drum& d, double x, double y, double r, bool first, double& z) {
    // The ray: (x, y, h) for all h. w(h) = p - c; radial part's length = r.
    const double wx = x - d.c[0];
    const double wy = y - d.c[1];
    // w = (wx, wy, h - cz); radial = w - (w.a) a. Quadratic in h.
    const double ax = d.a[0], ay = d.a[1], az = d.a[2];
    // radial(h) = e + (h - cz) * g, with e from (wx, wy, 0) and g from (0, 0, 1).
    const double ea = wx * ax + wy * ay;
    const double e[3] = {wx - ea * ax, wy - ea * ay, -ea * az};
    const double g[3] = {-az * ax, -az * ay, 1 - az * az};
    const double qa = g[0] * g[0] + g[1] * g[1] + g[2] * g[2];
    const double qb = 2 * (e[0] * g[0] + e[1] * g[1] + e[2] * g[2]);
    const double qc = e[0] * e[0] + e[1] * e[1] + e[2] * e[2] - r * r;
    const double disc = qb * qb - 4 * qa * qc;
    if (disc < 0 || qa < 1e-12)
        return false;
    const double root = std::sqrt(disc);
    const double k = first ? (-qb + root) / (2 * qa) : (-qb - root) / (2 * qa);
    z = d.c[2] + k;
    return true;
}

double axial(const Drum& d, double x, double y, double z) {
    return (x - d.c[0]) * d.a[0] + (y - d.c[1]) * d.a[1] + (z - d.c[2]) * d.a[2];
}

double around(const Drum& d, double x, double y, double z) {
    const double w[3] = {x - d.c[0], y - d.c[1], z - d.c[2]};
    return std::atan2(w[0] * d.v[0] + w[1] * d.v[1] + w[2] * d.v[2], w[0] * d.u[0] + w[1] * d.u[1] + w[2] * d.u[2]);
}

std::uint32_t screen_byte(double light) {
    return static_cast<std::uint32_t>(std::lround(ts::screen_value(light) * 255));
}

// `rim_only` draws the mouth's rim alone: over stones laid inside the drum.
void tumbler_end(Sheet& sheet, double cx, double mouth_y, double radius, int style, double turn, double scale, bool rim_only) {
    constexpr double pi = 3.14159265358979;
    Drum d{};
    const double tilt = .3;  // radians the axis falls from level, the mouth the higher end
    d.c[0] = cx;
    d.c[1] = mouth_y;
    d.c[2] = radius * 1.25;
    // Into the drum: up the screen (negative y) and down towards the table.
    d.a[0] = 0;
    d.a[1] = -std::cos(tilt);
    d.a[2] = -std::sin(tilt);
    d.u[0] = 1; d.u[1] = 0; d.u[2] = 0;
    d.v[0] = 0; d.v[1] = d.a[2]; d.v[2] = -d.a[1];  // a x u, up away from the table at the mouth
    d.radius = radius;
    d.wall = radius * .1;
    d.turn = turn;
    d.style = style;
    const double expose = ts::exposure();
    // Its shadow: the drum's outline as seen from above, moved away from the window's light
    // and softened, laid over the table.
    if (!rim_only) {
        const int x0 = std::max(0, static_cast<int>(cx - radius * 2));
        const int x1 = std::min(sheet.width, static_cast<int>(cx + radius * 2.4));
        const int y1 = std::min(sheet.height, static_cast<int>(mouth_y + radius * 1.6));
        const int w = x1 - x0;
        std::vector<float> cover(static_cast<std::size_t>(w * y1), 0.0F);
        const double shift_x = radius * .18;
        const double shift_y = radius * .3;
        for (int y = 0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                double z = 0;
                const double px = x - shift_x;
                const double py = y - shift_y;
                bool in = cylinder_hit(d, px, py, radius, true, z) && axial(d, px, py, z) >= 0;
                if (!in && std::abs(d.a[2]) > 1e-6) {
                    const double zm = d.c[2] + ((px - d.c[0]) * d.a[0] + (py - d.c[1]) * d.a[1]) / -d.a[2];
                    const double dx = px - d.c[0], dy = py - d.c[1], dz = zm - d.c[2];
                    in = dx * dx + dy * dy + dz * dz < radius * radius;
                }
                cover[static_cast<std::size_t>(y * w + (x - x0))] = in ? 1.0F : 0.0F;
            }
        // Softened: two passes of a box blur, across then down.
        const int reach = std::max(2, static_cast<int>(radius * .22));
        std::vector<float> across(cover.size(), 0.0F);
        for (int y = 0; y < y1; ++y) {
            double sum = 0;
            for (int x = -reach; x < w + reach; ++x) {
                const int in_x = x + reach;
                const int out_x = x - reach - 1;
                if (in_x >= 0 && in_x < w)
                    sum += cover[static_cast<std::size_t>(y * w + in_x)];
                if (out_x >= 0 && out_x < w)
                    sum -= cover[static_cast<std::size_t>(y * w + out_x)];
                if (x >= 0 && x < w)
                    across[static_cast<std::size_t>(y * w + x)] = static_cast<float>(sum / (2 * reach + 1));
            }
        }
        for (int x = 0; x < w; ++x) {
            double sum = 0;
            for (int y = -reach; y < y1 + reach; ++y) {
                const int in_y = y + reach;
                const int out_y = y - reach - 1;
                if (in_y >= 0 && in_y < y1)
                    sum += across[static_cast<std::size_t>(in_y * w + x)];
                if (out_y >= 0 && out_y < y1)
                    sum -= across[static_cast<std::size_t>(out_y * w + x)];
                if (y >= 0 && y < y1) {
                    const double k = 1 - .5 * sum / (2 * reach + 1);
                    std::uint32_t& p = sheet.pixels[static_cast<std::size_t>(y * sheet.width + x + x0)];
                    p = static_cast<std::uint32_t>((p & 0xffU) * k) | (static_cast<std::uint32_t>(((p >> 8) & 0xffU) * k) << 8) |
                        (static_cast<std::uint32_t>(((p >> 16) & 0xffU) * k) << 16) | 0xff000000U;
                }
            }
        }
    }
    // Then the drum.
    for (int y = 0; y < static_cast<int>(mouth_y + radius * 1.6); ++y)
        for (int x = static_cast<int>(cx - radius - 2); x <= static_cast<int>(cx + radius + 2); ++x) {
            if (x < 0 || y < 0 || x >= sheet.width || y >= sheet.height)
                continue;
            const double px = x + .5;
            const double py = y + .5;
            double n[3] = {0, 0, 1};
            double albedo[3] = {0, 0, 0};
            double f0[3] = {.04, .04, .04};
            double rough = .4;
            double dim = 1;
            bool seen = false;
            double z = 0;
            // The outside, from above.
            if (cylinder_hit(d, px, py, radius, true, z) && axial(d, px, py, z) >= 0) {
                seen = true;
                const double t = axial(d, px, py, z);
                for (int k = 0; k < 3; ++k)
                    n[k] = ((k == 0 ? px : k == 1 ? py : z) - d.c[k] - t * d.a[k]) / radius;
                const double phi = around(d, px, py, z) + turn;
                if (style == 0) {
                    albedo[0] = albedo[1] = albedo[2] = .03;
                    f0[0] = f0[1] = f0[2] = .035;
                    rough = .55;
                    // Ribs round the barrel, and a raised lip at the mouth.
                    const double rib = std::fmod(t / scale, 26.0);
                    if (rib < 4 || t < 7 * scale)
                        rough = .4;
                } else if (style == 1) {
                    if (t < 9 * scale) {
                        albedo[0] = albedo[1] = albedo[2] = 0;
                        f0[0] = .91; f0[1] = .72; f0[2] = .38;
                        rough = .2;
                    } else {
                        const double stave = phi / (2 * pi) * 16;
                        const double seam = std::abs(stave - std::round(stave)) < .05 ? .45 : 1.0;
                        const double grain = .82 + .18 * ts::noise2(stave * 3, t / scale * .05, 23);
                        albedo[0] = .25 * grain * seam; albedo[1] = .125 * grain * seam; albedo[2] = .065 * grain * seam;
                        rough = .3;
                    }
                } else {
                    albedo[0] = albedo[1] = albedo[2] = 0;
                    f0[0] = .95; f0[1] = .62; f0[2] = .48;
                    rough = .26;
                    if (t < 14 * scale) {
                        f0[0] = .82; f0[1] = .5; f0[2] = .38;
                        rough = .18;
                        const double rivet = std::fmod(phi + 20 * pi, pi / 8) - pi / 16;
                        const double along = t / scale - 7;
                        if (rivet * rivet * radius * radius / (scale * scale) + along * along < 5) {
                            rough = .1;
                            n[2] += .5;
                        }
                    }
                }
            } else {
                // The mouth: a ring of the wall's end, and inside it the dark of the drum.
                // The plane t = 0 met by the vertical ray: z = (w.a = 0).
                if (std::abs(d.a[2]) > 1e-6) {
                    const double zm = d.c[2] + ((px - d.c[0]) * d.a[0] + (py - d.c[1]) * d.a[1]) / -d.a[2];
                    const double w[3] = {px - d.c[0], py - d.c[1], zm - d.c[2]};
                    const double r = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
                    if (r < radius) {
                        seen = true;
                        if (r > radius - d.wall) {
                            // The rim, facing out of the mouth.
                            n[0] = -d.a[0]; n[1] = -d.a[1]; n[2] = -d.a[2];
                            if (style == 1) {
                                f0[0] = .91; f0[1] = .72; f0[2] = .38;
                                rough = .25;
                            } else if (style == 2) {
                                f0[0] = .82; f0[1] = .5; f0[2] = .38;
                                rough = .22;
                            } else {
                                albedo[0] = albedo[1] = albedo[2] = .04;
                                rough = .45;
                            }
                        } else if (rim_only) {
                            // Over the stones laid inside: the drum's shade on them.
                            std::uint32_t& p = sheet.pixels[static_cast<std::size_t>(y * sheet.width + x)];
                            const double k = .45 + .25 * (r / (radius - d.wall));
                            p = static_cast<std::uint32_t>((p & 0xffU) * k) | (static_cast<std::uint32_t>(((p >> 8) & 0xffU) * k) << 8) |
                                (static_cast<std::uint32_t>(((p >> 16) & 0xffU) * k) << 16) | 0xff000000U;
                            seen = false;
                        } else {
                            // Inside: the far wall, seen through the mouth, darker the deeper.
                            double zi = 0;
                            if (cylinder_hit(d, px, py, radius - d.wall, false, zi)) {
                                const double t = std::max(0.0, axial(d, px, py, zi));
                                for (int k = 0; k < 3; ++k)
                                    n[k] = -((k == 0 ? px : k == 1 ? py : zi) - d.c[k] - t * d.a[k]) / (radius - d.wall);
                                dim = std::exp(-t / (radius * .9)) * .6;
                                const double phi = around(d, px, py, zi) + turn;
                                // The lifting fins, coming round as it turns.
                                const double fin = std::fmod(phi + 20 * pi, 2 * pi / 3);
                                const bool on_fin = fin < .16;
                                albedo[0] = style == 1 ? .2 : style == 2 ? .3 : .05;
                                albedo[1] = style == 1 ? .1 : style == 2 ? .18 : .05;
                                albedo[2] = style == 1 ? .05 : style == 2 ? .12 : .05;
                                if (on_fin)
                                    dim *= 1.6;
                                rough = .6;
                            }
                        }
                    }
                }
            }
            if (!seen)
                continue;
            const double nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            for (double& c : n)
                c /= nl;
            // The bake's directions are the tray's: y up the screen.
            const double tx = n[0];
            const double ty = -n[1];
            const double tz = n[2];
            const ts::Light3 mirrored = ts::room_reflection(2 * tz * tx, 2 * tz * ty, 2 * tz * tz - 1, rough);
            const ts::Light3 diffuse = ts::room_diffuse(tx, ty, tz);
            const double fres = std::pow(1 - std::clamp(tz, 0.0, 1.0), 5);
            const double spec[3] = {mirrored.r, mirrored.g, mirrored.b};
            const double diff[3] = {diffuse.r, diffuse.g, diffuse.b};
            double out[3];
            for (int k = 0; k < 3; ++k) {
                const double f = f0[k] + (1 - f0[k]) * fres;
                out[k] = (f * spec[k] + (1 - f) * albedo[k] * diff[k]) * expose * dim;
            }
            sheet.pixels[static_cast<std::size_t>(y * sheet.width + x)] =
                screen_byte(out[2]) | (screen_byte(out[1]) << 8) | (screen_byte(out[0]) << 16) | 0xff000000U;
        }
}

// A brass pin's head: a clue, fixed in its socket.
void pin(Sheet& sheet, double cx, double cy, double radius) {
    for (int y = static_cast<int>(cy - radius); y <= static_cast<int>(cy + radius); ++y)
        for (int x = static_cast<int>(cx - radius); x <= static_cast<int>(cx + radius); ++x) {
            if (x < 0 || y < 0 || x >= sheet.width || y >= sheet.height)
                continue;
            const double d = std::hypot(x - cx, y - cy) / radius;
            if (d > 1)
                continue;
            const double lit = .55 + .45 * std::max(0.0, 1 - std::hypot(x - cx + radius * .35, y - cy + radius * .35) / radius);
            sheet.pixels[static_cast<std::size_t>(y * sheet.width + x)] = static_cast<std::uint32_t>(70 * lit) |
                (static_cast<std::uint32_t>(170 * lit) << 8) | (static_cast<std::uint32_t>(226 * lit) << 16) | 0xff000000U;
        }
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

    // Specimen Cabinet: a layout mock-up at a window size.
    //   ts_candidates <out> <room> cabinet <width> <height> <A|B|C>
    if (argc > 6 && std::string(argv[3]) == "cabinet") {
        const double points_w = std::atof(argv[4]);
        const double points_h = std::atof(argv[5]);
        const std::string layout = argv[6];
        const double scale = 2;
        Sheet sheet;
        sheet.width = static_cast<int>(points_w * scale);
        sheet.height = static_cast<int>(points_h * scale);
        sheet.pixels.assign(static_cast<std::size_t>(sheet.width * sheet.height), 0U);
        constexpr int n = 5;
        // Where things go, in points: clear of the capsule (top left) and Help (top right).
        const double m = 12;
        const double top = 54;
        const double avail_h = points_h - top - m;
        Area cabinet;
        Area strip;
        double tumbler_x = 0, tumbler_y = 0, tumbler_r = 0;
        if (layout == "A") {
            // Tumbler | cabinet | felt strip, side by side.
            const double size = std::min(avail_h, (points_w - 4 * m) * .5);
            const double rest = points_w - size - 4 * m;
            const double bowl_d = std::min({rest * .5, avail_h, size * .8});
            cabinet = {m + bowl_d + m, top + (avail_h - size) * .5, size, size};
            tumbler_r = bowl_d * .5;
            tumbler_x = m + tumbler_r;
            tumbler_y = top + avail_h * .5;
            strip = {cabinet.x + size + m, top, points_w - (cabinet.x + size + m) - m, avail_h};
        } else if (layout == "B") {
            // Cabinet and tumbler above, a felt strip the window's width below.
            const double size = std::min(avail_h * .7, (points_w - 3 * m) * .62);
            cabinet = {m, top, size, size};
            const double room = points_w - size - 3 * m;
            tumbler_r = std::min(room, size) * .5;
            tumbler_x = cabinet.x + size + m + room * .5;
            tumbler_y = top + size * .5;
            strip = {m, top + size + m, points_w - 2 * m, points_h - (top + size + m) - m};
        } else if (layout == "D") {
            // The cabinet at the left; at the right the closed tumbler over a felt strip,
            // its stones spilling into a pile at the strip's head.
            const double size = std::min(avail_h, (points_w - 3 * m) * .6);
            cabinet = {m, top + (avail_h - size) * .5, size, size};
            const double column = points_w - size - 3 * m;
            strip = {cabinet.x + size + m, top + 24, column, avail_h - 24};
            tumbler_r = 0;
            tumbler_x = strip.x;
            tumbler_y = top;
        } else {
            // A column at the left, tumbler over felt; the cabinet beside it.
            const double size = std::min(avail_h, (points_w - 3 * m) * .62);
            const double column = points_w - size - 3 * m;
            cabinet = {m + column + m, top + (avail_h - size) * .5, size, size};
            tumbler_r = std::min(column, avail_h * .45) * .5;
            tumbler_x = m + column * .5;
            tumbler_y = top + tumbler_r;
            strip = {m, top + 2 * tumbler_r + m, column, avail_h - 2 * tumbler_r - m};
        }
        const double cell = cabinet.w / n;
        ts::TrayPlan plan;
        plan.width = sheet.width;
        plan.height = sheet.height;
        plan.scale = scale;
        plan.grid_x = cabinet.x * scale;
        plan.grid_y = cabinet.y * scale;
        plan.cell = cell * scale;
        plan.columns = n;
        plan.rows = n;
        plan.wall = 10 * scale;
        plan.wood = argc > 9 ? std::atoi(argv[9]) : 0;
        ts::draw_tray(plan, sheet.pixels.data(), sheet.width, true, 0, sheet.height);
        const bool closed = layout == "D";
        if (closed) {
            const int preset = argc > 7 ? std::atoi(argv[7]) : 9;
            paint::CarpetParameters cloth = paint::carpet_preset(preset);
            const paint::Image tile = paint::render_carpet_tile(cloth, 256);
            felt_cloth(sheet, {strip.x * scale, strip.y * scale, strip.w * scale, strip.h * scale}, tile, scale);
            const int style = argc > 8 ? std::atoi(argv[8]) : 0;
            const double drum_r = strip.w * .25;
            const double mouth_y = strip.y + 6;
            tumbler_end(sheet, (strip.x + strip.w * .5) * scale, mouth_y * scale, drum_r * scale, style, .35, scale, false);
            // A few stones inside, in the drum's shade, then the rim over them.
            const ts::Style inner_styles[3] = {ts::Style::candy, ts::Style::flake, ts::Style::shell};
            const ts::Mineral inner_kinds[3] = {ts::Mineral::amber, ts::Mineral::jade, ts::Mineral::obsidian};
            const double inner_spots[3][2] = {{-.35, -.05}, {.15, .02}, {.42, -.12}};
            for (int k = 0; k < 3; ++k) {
                Stone stone;
                stone.shape = ts::Shape::make(7700U + static_cast<std::uint32_t>(k) * 97U, inner_styles[k]);
                stone.finish = ts::make_finish(inner_kinds[k], 51U + static_cast<std::uint32_t>(k));
                ts::print_mineral(stone.finish, stone.shape, 128, stone.picture);
                const ts::StoneLook look{&stone.shape, &stone.finish, &stone.picture};
                ts::DrawStone how;
                how.radius = drum_r * scale * .3;
                how.fineness = 2;
                ts::StonePicture picture;
                draw_stone(look, ts::orientation_pose(k * 3), how, work, picture);
                ts::lay_stone_moved(picture, sheet.pixels.data(), sheet.width, sheet.height, sheet.width,
                                    (strip.x + strip.w * .5 + inner_spots[k][0] * drum_r) * scale, (mouth_y + inner_spots[k][1] * drum_r) * scale, 1,
                                    k * 1.3, 1, 1, true, 0, 0, sheet.width, sheet.height);
            }
            tumbler_end(sheet, (strip.x + strip.w * .5) * scale, mouth_y * scale, drum_r * scale, style, .35, scale, true);
        } else {
            felt(sheet, {strip.x * scale, strip.y * scale, strip.w * scale, strip.h * scale}, scale);
            bowl(sheet, tumbler_x * scale, tumbler_y * scale, tumbler_r * scale, scale);
        }
        // Five shapes and five materials; the solution pairs them by two orthogonal squares.
        const ts::Style styles[n] = {ts::Style::shell, ts::Style::wood, ts::Style::candy, ts::Style::flake, ts::Style::nodule};
        const ts::Mineral kinds[n] = {ts::Mineral::lapis, ts::Mineral::amber, ts::Mineral::jade, ts::Mineral::obsidian,
                                      ts::Mineral::nacre};
        std::vector<ts::Shape> shapes;
        std::vector<ts::Finish> finishes;
        for (int k = 0; k < n; ++k) {
            shapes.push_back(ts::Shape::make(83000U + static_cast<std::uint32_t>(k) * 211U, styles[k]));
            finishes.push_back(ts::make_finish(kinds[k], 1300U + static_cast<std::uint32_t>(k) * 17U));
        }
        std::vector<Stone> stones;
        for (int k = 0; k < n * n; ++k) {
            Stone stone;
            stone.shape = shapes[static_cast<std::size_t>(k / n)];
            stone.finish = finishes[static_cast<std::size_t>(k % n)];
            ts::print_mineral(stone.finish, stone.shape, 160, stone.picture);
            stones.push_back(std::move(stone));
        }
        // The clues in the cabinet, pinned; their shape is (row + column), their material
        // (2 row + column), both mod 5.
        const int clues[7][2] = {{0, 1}, {1, 3}, {2, 0}, {2, 4}, {3, 2}, {4, 1}, {4, 4}};
        std::vector<int> used(static_cast<std::size_t>(n * n), 0);
        for (const int* rc : clues) {
            const int shape = (rc[0] + rc[1]) % n;
            const int material = (2 * rc[0] + rc[1]) % n;
            used[static_cast<std::size_t>(shape * n + material)] = 1;
            const double cx = (cabinet.x + (rc[1] + .5) * cell) * scale;
            const double cy = (cabinet.y + (rc[0] + .5) * cell) * scale;
            place(sheet, work, stones[static_cast<std::size_t>(shape * n + material)], (rc[0] * 3 + rc[1]) % 8, cx, cy,
                  cell * scale * .42, 2);
            pin(sheet, cx + cell * scale * .3, cy - cell * scale * .3, 3.5 * scale);
        }
        // On the felt: a few sorted by shape, in rows, as a player lays them out.
        std::vector<int> left;
        for (int k = 0; k < n * n; ++k)
            if (used[static_cast<std::size_t>(k)] == 0)
                left.push_back(k);
        const bool wide = strip.w > strip.h;
        const double small = std::min(cell * .8, wide ? strip.h * .8 : strip.w / 3.3);
        const double pitch = small * 1.06;
        for (int shape = 0; shape < 3; ++shape) {
            int along = 0;
            for (int k : left) {
                if (k / n != shape || along >= 3)
                    continue;
                // Wide: groups of three side by side; tall: a row for each shape.
                const double cx = wide ? strip.x + 10 + (shape * 3.4 + along + .5) * pitch : strip.x + 8 + (along + .5) * pitch;
                const double cy = wide ? strip.y + strip.h * .5 : strip.y + 8 + (shape + .5 + (closed ? 1.6 : 0)) * pitch;
                if (cx + small * .45 > strip.x + strip.w || cy + small * .45 > strip.y + strip.h)
                    continue;
                place(sheet, work, stones[static_cast<std::size_t>(k)], (k * 5) % 8, cx * scale, cy * scale, small * scale * .42, 2);
                used[static_cast<std::size_t>(k)] = 2;
                ++along;
            }
        }
        // The rest jumbled in the tumbler, overlapping, the lowest darker.
        std::uint64_t random = 991;
        std::vector<int> heap;
        for (int k : left)
            if (used[static_cast<std::size_t>(k)] == 0)
                heap.push_back(k);
        if (closed) {
            // A small pile under the chute: six stones heaped, the rest still in the drum.
            const double piece = std::min(cell * .7, strip.w * .2);
            const double px = strip.x + strip.w * .5;
            const double py = strip.y + 6 + strip.w * .25 * .35 + piece * .65;
            const double spots[6][2] = {{-.55, .25}, {.5, .3}, {0, .45}, {-.25, -.05}, {.3, -.02}, {.02, .12}};
            for (std::size_t i = 0; i < heap.size() && i < 6; ++i) {
                const Stone& stone = stones[static_cast<std::size_t>(heap[i])];
                const ts::StoneLook look{&stone.shape, &stone.finish, &stone.picture};
                ts::DrawStone how;
                how.radius = piece * scale * .42;
                how.fineness = 2;
                ts::StonePicture picture;
                draw_stone(look, ts::orientation_pose(static_cast<int>(i % 8)), how, work, picture);
                ts::lay_stone_moved(picture, sheet.pixels.data(), sheet.width, sheet.height, sheet.width,
                                    (px + spots[i][0] * piece) * scale, (py + spots[i][1] * piece) * scale, 1, ts::next_between(random, -3.14, 3.14), 1,
                                    1, true, 0, 0, sheet.width, sheet.height);
            }
            heap.clear();
        }
        const double piece = tumbler_r * .44;
        for (std::size_t i = 0; i < heap.size(); ++i) {
            const double a = ts::next_between(random, -3.14159, 3.14159);
            const double r = std::sqrt(ts::next_unit(random)) * (tumbler_r * .78 - piece * .5);
            const Stone& stone = stones[static_cast<std::size_t>(heap[i])];
            const ts::StoneLook look{&stone.shape, &stone.finish, &stone.picture};
            ts::DrawStone how;
            how.radius = piece * scale * .5;
            how.fineness = 2;
            ts::StonePicture picture;
            draw_stone(look, ts::orientation_pose(static_cast<int>(i % 8)), how, work, picture);
            ts::lay_stone_moved(picture, sheet.pixels.data(), sheet.width, sheet.height, sheet.width,
                                (tumbler_x + r * std::cos(a)) * scale, (tumbler_y + r * std::sin(a)) * scale, 1,
                                ts::next_between(random, -3.14159, 3.14159), 1, .55 + .45 * static_cast<double>(i) / heap.size(), true,
                                0, 0, sheet.width, sheet.height);
        }
        const std::string title = std::string("Layout ") + layout + ", " + std::to_string(static_cast<int>(points_w)) + " x " +
                                  std::to_string(static_cast<int>(points_h));
        label(sheet, title, static_cast<int>(16 * scale), static_cast<int>(14 * scale), 15 * scale, cream, true);
        save(sheet, out + "/cabinet-" + layout + "-" + std::to_string(static_cast<int>(points_w)) + ".png");
        return 0;
    }
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
    if (argc > 4 && std::string(argv[3]) == "review") {
        // One shape generator alone, for review: twelve families, each face up then face down,
        // then six of them tilted towards the eye, all in plain bone so only the shape speaks.
        const ts::Style style = static_cast<ts::Style>(std::clamp(std::atoi(argv[4]), 0, ts::style_count - 1));
        const double cell = 170;
        const int left = 30;
        const int top = 60;
        const int rows = 5;
        const int columns = 6;
        Sheet sheet;
        sheet.width = left + static_cast<int>(cell * columns) + 20;
        sheet.height = top + static_cast<int>(cell * rows) + 20;
        sheet.pixels.assign(static_cast<std::size_t>(sheet.width * sheet.height), 0U);
        ts::TrayPlan plan;
        plan.width = sheet.width;
        plan.height = sheet.height;
        plan.scale = 2.6;
        plan.grid_x = left;
        plan.grid_y = top;
        plan.cell = cell;
        plan.columns = columns;
        plan.rows = rows;
        plan.wall = 12;
        ts::draw_tray(plan, sheet.pixels.data(), sheet.width, true, 0, sheet.height);
        const std::string title = std::string("Shape review: ") + ts::style_name(style) +
                                  ". Twelve families face up then down; last row six of them tilted";
        label(sheet, title, 24, 16, 19, cream, true);
        std::vector<Stone> stones(12);
        for (int k = 0; k < 12; ++k) {
            Stone& stone = stones[static_cast<std::size_t>(k)];
            stone.shape = ts::Shape::make(61000U + static_cast<std::uint32_t>(k) * 7919U, style);
            stone.finish = ts::make_finish(ts::Mineral::bone, 4242U + static_cast<std::uint32_t>(k) * 31U);
            ts::print_mineral(stone.finish, stone.shape, 160, stone.picture);
            const int row = k / 3;
            const int column = (k % 3) * 2;
            place(sheet, work, stone, 0, left + cell * (column + .5), top + cell * (row + .5), cell * .42, 2);
            place(sheet, work, stone, 4, left + cell * (column + 1.5), top + cell * (row + .5), cell * .42, 2);
        }
        for (int k = 0; k < 6; ++k) {
            const Stone& stone = stones[static_cast<std::size_t>(k * 2)];
            const ts::StoneLook look{&stone.shape, &stone.finish, &stone.picture};
            ts::DrawStone how;
            how.radius = cell * .4;
            how.fineness = 2;
            ts::StonePicture picture;
            draw_stone(look, ts::turned(ts::orientation_pose(0), 0, -0.85), how, work, picture);
            ts::lay_stone(picture, sheet.pixels.data(), sheet.width, sheet.height, sheet.width, left + cell * (k + .5),
                          top + cell * 4.5, true, 0, 0, sheet.width, sheet.height);
        }
        save(sheet, out + "/review-" + std::to_string(static_cast<int>(style)) + ".png");
        return 0;
    }
    if (argc > 3 && std::string(argv[3]) == "tilted") {
        // The same fossil-log pieces from straight above and from a three-quarter view.
        const double cell = 200;
        const int left = 30;
        const int top = 60;
        Sheet sheet;
        sheet.width = left + static_cast<int>(cell * 6) + 20;
        sheet.height = top + static_cast<int>(cell * 2) + 20;
        sheet.pixels.assign(static_cast<std::size_t>(sheet.width * sheet.height), 0U);
        ts::TrayPlan plan;
        plan.width = sheet.width;
        plan.height = sheet.height;
        plan.scale = 3;
        plan.grid_x = left;
        plan.grid_y = top;
        plan.cell = cell;
        plan.columns = 6;
        plan.rows = 2;
        plan.wall = 12;
        ts::draw_tray(plan, sheet.pixels.data(), sheet.width, true, 0, sheet.height);
        label(sheet, "Top row: from straight above, as the game is now.  Bottom row: the same pieces tilted towards you", 24, 16, 18, cream, true);
        const std::uint32_t seeds[6] = {52000U, 52000U + 7919U * 4, 52000U + 7919U * 2, 52000U + 7919U * 9, 52000U + 7919U * 6, 52000U + 7919U * 7};
        for (int k = 0; k < 6; ++k) {
            Stone stone;
            stone.shape = ts::Shape::make(seeds[k], ts::Style::wood);
            stone.finish = ts::make_finish(ts::Mineral::petrified, 4242U + static_cast<std::uint32_t>(k) * 31U);
            ts::print_mineral(stone.finish, stone.shape, 160, stone.picture);
            place(sheet, work, stone, 0, left + cell * (k + .5), top + cell * .5, cell * .42, 2);
            const ts::StoneLook look{&stone.shape, &stone.finish, &stone.picture};
            ts::DrawStone how;
            how.radius = cell * .4;
            how.fineness = 2;
            ts::StonePicture picture;
            draw_stone(look, ts::turned(ts::orientation_pose(0), 0, -0.85), how, work, picture);
            ts::lay_stone(picture, sheet.pixels.data(), sheet.width, sheet.height, sheet.width, left + cell * (k + .5),
                          top + cell * 1.5, true, 0, 0, sheet.width, sheet.height);
        }
        save(sheet, out + "/tilted-" + room + ".png");
        return 0;
    }
    if (argc > 3 && std::string(argv[3]) == "woodshapes") {
        // The petrified wood shape generator alone: twelve families, each face up and face
        // down, in one plain material so only the shape speaks.
        const double cell = 170;
        const int left = 30;
        const int top = 60;
        const int rows = 4;
        const int columns = 6;
        Sheet sheet;
        sheet.width = left + static_cast<int>(cell * columns) + 20;
        sheet.height = top + static_cast<int>(cell * rows) + 20;
        sheet.pixels.assign(static_cast<std::size_t>(sheet.width * sheet.height), 0U);
        ts::TrayPlan plan;
        plan.width = sheet.width;
        plan.height = sheet.height;
        plan.scale = 2.6;
        plan.grid_x = left;
        plan.grid_y = top;
        plan.cell = cell;
        plan.columns = columns;
        plan.rows = rows;
        plan.wall = 12;
        ts::draw_tray(plan, sheet.pixels.data(), sheet.width, true, 0, sheet.height);
        const ts::Mineral material = argc > 4 && std::string(argv[4]) == "petrified" ? ts::Mineral::petrified : ts::Mineral::bone;
        label(sheet, "The petrified wood shape: twelve families, each face up then face down", 24, 16, 20, cream, true);
        for (int k = 0; k < 12; ++k) {
            Stone stone;
            stone.shape = ts::Shape::make(52000U + static_cast<std::uint32_t>(k) * 7919U, ts::Style::wood);
            stone.finish = ts::make_finish(material, 4242U + static_cast<std::uint32_t>(k) * 31U);
            ts::print_mineral(stone.finish, stone.shape, 160, stone.picture);
            const int row = k / 3;
            const int column = (k % 3) * 2;
            place(sheet, work, stone, 0, left + cell * (column + .5), top + cell * (row + .5), cell * .42, 2);
            place(sheet, work, stone, 4, left + cell * (column + 1.5), top + cell * (row + .5), cell * .42, 2);
        }
        save(sheet, out + "/woodshapes-" + room + (material == ts::Mineral::bone ? "" : "-petrified") + ".png");
        return 0;
    }
    if (argc > 3 && std::string(argv[3]) == "kinds") {
        // Every material, a third of them a sheet: each on all six kinds of shape, every
        // stone its own draw of the material, so its variants show.
        const int group = argc > 4 ? std::clamp(std::atoi(argv[4]), 0, 2) : 0;
        const ts::Mineral groups[3][8] = {
            {ts::Mineral::obsidian, ts::Mineral::labradorite, ts::Mineral::lava, ts::Mineral::walnut, ts::Mineral::petrified,
             ts::Mineral::bone, ts::Mineral::jasper, ts::Mineral::malachite},
            {ts::Mineral::candy, ts::Mineral::nacre, ts::Mineral::cone, ts::Mineral::seaglass, ts::Mineral::quartz,
             ts::Mineral::smoky, ts::Mineral::amber, ts::Mineral::dripstone},
            {ts::Mineral::rose, ts::Mineral::amethyst, ts::Mineral::lapis, ts::Mineral::sodalite, ts::Mineral::blue_lace,
             ts::Mineral::chrysocolla, ts::Mineral::jade, ts::Mineral::agate}};
        const char* titles[3] = {"Materials 1 of 3: dark stone, wood, bone", "Materials 2 of 3: candy, shell, glass, clear stone",
                                 "Materials 3 of 3: the pretty stones"};
        const double cell = 150;
        const int left = 190;
        const int top = 56;
        const int rows = 8;
        const int columns = 6;
        Sheet sheet;
        sheet.width = left + static_cast<int>(cell * columns) + 20;
        sheet.height = top + static_cast<int>(cell * rows) + 20;
        sheet.pixels.assign(static_cast<std::size_t>(sheet.width * sheet.height), 0U);
        ts::TrayPlan plan;
        plan.width = sheet.width;
        plan.height = sheet.height;
        plan.scale = 2.2;
        plan.grid_x = left;
        plan.grid_y = top;
        plan.cell = cell;
        plan.columns = columns;
        plan.rows = rows;
        plan.wall = 12;
        ts::draw_tray(plan, sheet.pixels.data(), sheet.width, true, 0, sheet.height);
        label(sheet, titles[group], 24, 14, 22, cream, true);
        std::vector<ts::Shape> shapes;
        for (int c = 0; c < columns; ++c)
            shapes.push_back(ts::Shape::make(91000U + static_cast<std::uint32_t>(c) * 313U, static_cast<ts::Style>(c)));
        for (int r = 0; r < rows; ++r) {
            const ts::Mineral kind = groups[group][r];
            const bool redone = kind == ts::Mineral::walnut || kind == ts::Mineral::nacre;
            const std::string name = std::string(ts::mineral_name(kind)) + (redone ? " (redone)" : "");
            label(sheet, name, 20, top + static_cast<int>(cell * (r + .5)) - 10, 17, cream, true);
            for (int c = 0; c < columns; ++c) {
                Stone stone;
                stone.shape = shapes[static_cast<std::size_t>(c)];
                stone.finish = ts::make_finish(kind, 700U + static_cast<std::uint32_t>(r * 7 + c) * 23U);
                ts::print_mineral(stone.finish, stone.shape, 160, stone.picture);
                place(sheet, work, stone, (r + c) % 8, left + cell * (c + .5), top + cell * (r + .5), cell * .42, 2);
            }
        }
        save(sheet, out + "/kinds-" + std::to_string(group) + "-" + room + ".png");
        return 0;
    }
    if (argc > 3 && std::string(argv[3]) == "styles") {
        const double cell = 190;
        const int left = 30;
        const int top = 60;
        const ts::Style styles[7] = {ts::Style::shell, ts::Style::nodule, ts::Style::flake, ts::Style::candy,
                                     ts::Style::wood, ts::Style::shell, ts::Style::facet};
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
                                         ts::Mineral::jade, ts::Mineral::labradorite, ts::Mineral::malachite};
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
        const ts::Style styles[5] = {ts::Style::shell, ts::Style::wood, ts::Style::candy, ts::Style::nodule,
                                     ts::Style::flake};
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
