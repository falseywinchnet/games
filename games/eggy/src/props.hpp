#pragma once
// Isometric projection, lighting, and the hand-built vector art for every
// prop on the mountain (trees, rocks, logs, flowers, flags, stars …).
#include "raster.hpp"
#include "world.hpp"

namespace eggy {

struct Proj {
    static constexpr double TW = 96, TH = 48, ZS = 40;
    double cx0 = 0, cy0 = 0, zoom = 1, cu = 0, cv = 0, cz = 0;
    void p(double u, double v, double z, double& x, double& y) const {
        const double du = u - cu, dv = v - cv;
        x = cx0 + (du + dv) * TW * .5 * zoom;
        y = cy0 + (du - dv) * TH * .5 * zoom - (z - cz) * ZS * zoom;
    }
    double k() const { return zoom; }  // px per design pixel
};

struct Light {
    Col ambient{1, 1, 1, 1};  // multiplies world colours
    Col haze{.8f, .9f, 1, 1};
    double sun = 1, night = 0, dusk = 0;
    double wind = 0, time = 0;
    bool cold_biome = false;
    Col lit(Col c) const { return {c.r * ambient.r, c.g * ambient.g, c.b * ambient.b, c.a}; }
    Col lit(Col c, float shade_k) const { return lit(shade(c, shade_k)); }
};

void draw_feature(Canvas& c, const Proj& P, const Light& L, const World& w, const Tile& t, int u, std::int64_t v,
                  double gx, double gy, double t_now);
void draw_star(Canvas& c, double x, double y, double size, double t, float opacity);
void draw_tree(Canvas& c, const Light& L, double x, double y, double k, int variant, bool autumn, double sway);
void draw_pine(Canvas& c, const Light& L, double x, double y, double k, int variant, bool snowy, double sway);
void draw_boulder(Canvas& c, const Light& L, double x, double y, double k, int variant, double size, bool snowy, bool mossy);
void draw_tent(Canvas& c, const Light& L, double x, double y, double k, double flap_open, double t);

}  // namespace eggy
