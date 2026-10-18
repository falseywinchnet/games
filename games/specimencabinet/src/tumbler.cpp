#include "tumbler.hpp"

#include "room.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sc {
namespace {

constexpr double pi = 3.14159265358979323846;

struct Drum {
    double c[3];      // the mouth's centre: screen x, screen y (down), height above the table
    double a[3];      // the axis, from the mouth into the drum
    double u[3];      // two directions round it
    double v[3];
    double radius;
    double wall;
};

Drum drum_for(const TumblerLook& look) {
    Drum d{};
    const double tilt = .3;  // radians the axis falls from level, the mouth the higher end
    d.c[0] = look.x;
    d.c[1] = look.y;
    d.c[2] = look.radius * 1.25;
    // Into the drum: up the screen and down towards the table.
    d.a[0] = 0;
    d.a[1] = -std::cos(tilt);
    d.a[2] = -std::sin(tilt);
    d.u[0] = 1;
    d.u[1] = 0;
    d.u[2] = 0;
    d.v[0] = 0;
    d.v[1] = d.a[2];
    d.v[2] = -d.a[1];
    d.radius = look.radius;
    d.wall = look.radius * .1;
    return d;
}

// Where a vertical ray at (x, y) meets the cylinder of radius `r` about the axis, from
// above (`first`) or from below: its height, or false.
bool cylinder_hit(const Drum& d, double x, double y, double r, bool first, double& z) {
    const double wx = x - d.c[0];
    const double wy = y - d.c[1];
    const double ax = d.a[0], ay = d.a[1], az = d.a[2];
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

// The mouth's plane met by the vertical ray at (x, y): how far from the axis.
bool mouth_hit(const Drum& d, double x, double y, double& r) {
    if (std::abs(d.a[2]) < 1e-6)
        return false;
    const double zm = d.c[2] + ((x - d.c[0]) * d.a[0] + (y - d.c[1]) * d.a[1]) / -d.a[2];
    const double w[3] = {x - d.c[0], y - d.c[1], zm - d.c[2]};
    r = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    return true;
}

bool covers(const Drum& d, double x, double y) {
    double z = 0;
    if (cylinder_hit(d, x, y, d.radius, true, z) && axial(d, x, y, z) >= 0)
        return true;
    double r = 0;
    return mouth_hit(d, x, y, r) && r < d.radius;
}

std::uint32_t screen_byte(double light) {
    return static_cast<std::uint32_t>(std::lround(stones::screen_value(light) * 255));
}

void darken(std::uint32_t& p, double k) {
    p = static_cast<std::uint32_t>((p & 0xffU) * k) | (static_cast<std::uint32_t>(((p >> 8) & 0xffU) * k) << 8) |
        (static_cast<std::uint32_t>(((p >> 16) & 0xffU) * k) << 16) | 0xff000000U;
}

// The drum's shadow: its outline seen from above, moved away from the window's light and
// softened by two passes of a box blur.
void shadow(const render::Target& target, const Drum& d, const TumblerLook& look) {
    const int x0 = std::max(0, static_cast<int>(look.x - look.radius * 2));
    const int x1 = std::min(target.width, static_cast<int>(look.x + look.radius * 2.4));
    const int y1 = std::min(target.height, static_cast<int>(look.y + look.radius * 1.6));
    const int w = x1 - x0;
    if (w <= 0 || y1 <= 0)
        return;
    std::vector<float> cover(static_cast<std::size_t>(w * y1), 0.0F);
    const double shift_x = look.radius * .18;
    const double shift_y = look.radius * .3;
    for (int y = 0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            cover[static_cast<std::size_t>(y * w + (x - x0))] = covers(d, x - shift_x, y - shift_y) ? 1.0F : 0.0F;
    const int reach = std::max(2, static_cast<int>(look.radius * .22));
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
                const double amount = sum / (2 * reach + 1);
                if (amount > 0)
                    darken(target.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(target.stride) +
                                         static_cast<std::size_t>(x + x0)],
                           1 - .5 * amount);
            }
        }
    }
}

}  // namespace

int tumbler_bottom(const TumblerLook& look) {
    return static_cast<int>(look.y + look.radius * 1.6) + 1;
}

void draw_tumbler(const render::Target& target, const TumblerLook& look, TumblerPass pass) {
    const Drum d = drum_for(look);
    const bool over = pass == TumblerPass::over;
    if (!over)
        shadow(target, d, look);
    const double expose = stones::exposure();
    const int red_shift = target.order == render::Order::bgra ? 16 : 0;
    const int blue_shift = target.order == render::Order::bgra ? 0 : 16;
    const int y_end = std::min(target.height, static_cast<int>(look.y + look.radius * 1.6));
    for (int y = 0; y < y_end; ++y)
        for (int x = std::max(0, static_cast<int>(look.x - look.radius - 2));
             x <= std::min(target.width - 1, static_cast<int>(look.x + look.radius + 2)); ++x) {
            const double px = x + .5;
            const double py = y + .5;
            double n[3] = {0, 0, 1};
            double albedo = .03;
            double f0 = .035;
            double rough = .55;
            double dim = 1;
            bool seen = false;
            double z = 0;
            std::uint32_t& out = target.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(target.stride) +
                                               static_cast<std::size_t>(x)];
            if (cylinder_hit(d, px, py, look.radius, true, z) && axial(d, px, py, z) >= 0) {
                // The outside: ribs along it, and the mould's seams round it, which come and
                // go as it turns.
                seen = true;
                const double t = axial(d, px, py, z);
                for (int k = 0; k < 3; ++k)
                    n[k] = ((k == 0 ? px : k == 1 ? py : z) - d.c[k] - t * d.a[k]) / look.radius;
                const double rib = std::fmod(t / look.scale, 26.0);
                if (rib < 4 || t < 7 * look.scale)
                    rough = .4;
                const double seam = std::fmod(around(d, px, py, z) + look.turn + 20 * pi, pi / 3);
                if (seam < .045) {
                    rough = .3;
                    n[2] += .25;
                }
            } else {
                double r = 0;
                if (mouth_hit(d, px, py, r) && r < look.radius) {
                    seen = true;
                    if (r > look.radius - d.wall) {
                        // The rim, facing out of the mouth.
                        n[0] = -d.a[0];
                        n[1] = -d.a[1];
                        n[2] = -d.a[2];
                        albedo = .04;
                        rough = .45;
                    } else if (over) {
                        // The stones laid inside lie in the drum's dark.
                        darken(out, .45 + .25 * (r / (look.radius - d.wall)));
                        seen = false;
                    } else {
                        // Inside: the far wall through the mouth, darker the deeper, and the
                        // lifting fins coming round as it turns.
                        double zi = 0;
                        if (cylinder_hit(d, px, py, look.radius - d.wall, false, zi)) {
                            const double t = std::max(0.0, axial(d, px, py, zi));
                            for (int k = 0; k < 3; ++k)
                                n[k] = -((k == 0 ? px : k == 1 ? py : zi) - d.c[k] - t * d.a[k]) / (look.radius - d.wall);
                            dim = std::exp(-t / (look.radius * .9)) * .6;
                            const double fin = std::fmod(around(d, px, py, zi) + look.turn + 20 * pi, 2 * pi / 3);
                            if (fin < .16)
                                dim *= 1.6;
                            albedo = .05;
                            rough = .6;
                        }
                    }
                }
            }
            if (!seen)
                continue;
            const double nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            for (double& c : n)
                c /= nl;
            // The bake's directions are the table's: y up the screen.
            const double tx = n[0];
            const double ty = -n[1];
            const double tz = n[2];
            const stones::Light3 mirrored = stones::room_reflection(2 * tz * tx, 2 * tz * ty, 2 * tz * tz - 1, rough);
            const stones::Light3 diffuse = stones::room_diffuse(tx, ty, tz);
            const double fres = std::pow(1 - std::clamp(tz, 0.0, 1.0), 5);
            const double f = f0 + (1 - f0) * fres;
            const double r = (f * mirrored.r + (1 - f) * albedo * diffuse.r) * expose * dim;
            const double g = (f * mirrored.g + (1 - f) * albedo * diffuse.g) * expose * dim;
            const double b = (f * mirrored.b + (1 - f) * albedo * diffuse.b) * expose * dim;
            out = (screen_byte(r) << red_shift) | (screen_byte(g) << 8) | (screen_byte(b) << blue_shift) | 0xff000000U;
        }
}

}  // namespace sc
