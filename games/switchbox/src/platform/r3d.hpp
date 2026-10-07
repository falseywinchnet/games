#pragma once
// A small 1990s-style software 3D renderer: orthographic (isometric) camera,
// z-buffered texture-mapped triangles with nearest-neighbour sampling,
// Gouraud vertex lighting, distance haze, cut-out and translucent materials,
// rendered at low resolution and presented with ordered dithering and chunky
// integer upscaling.
#include "raster.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace sbx {

struct V3 {
    double x = 0, y = 0, z = 0;
    V3 operator+(V3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(V3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(double s) const { return {x * s, y * s, z * s}; }
};
inline double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double len(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 norm(V3 a) { const double l = len(a); return l > 1e-12 ? a * (1 / l) : V3{0, 0, 1}; }

// 3x4 affine transform (column vectors): p' = R p + t
struct M34 {
    double m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    V3 apply(V3 p) const {
        return {m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
                m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]};
    }
    V3 dir(V3 p) const {
        return {m[0] * p.x + m[1] * p.y + m[2] * p.z, m[4] * p.x + m[5] * p.y + m[6] * p.z, m[8] * p.x + m[9] * p.y + m[10] * p.z};
    }
    M34 operator*(const M34& b) const;
    static M34 translate(double x, double y, double z) { M34 r; r.m[3] = x; r.m[7] = y; r.m[11] = z; return r; }
    static M34 scale(double x, double y, double z) { M34 r; r.m[0] = x; r.m[5] = y; r.m[10] = z; return r; }
    static M34 rot_x(double a);
    static M34 rot_y(double a);
    static M34 rot_z(double a);
    M34 normal_matrix() const;  // inverse-transpose of the linear part
};

struct Tex {
    int w = 0, h = 0, wm = 0, hm = 0;  // power-of-two sizes; masks
    std::vector<std::uint32_t> px;     // 0xAARRGGBB
    std::vector<Tex> mips;             // successively halved levels (built by build_mips)
    void make(int width, int height) { w = width; h = height; wm = w - 1; hm = h - 1; px.assign(static_cast<size_t>(w) * h, 0xFFFFFFFF); mips.clear(); }
    std::uint32_t& at(int x, int y) { return px[static_cast<size_t>((y & hm) * w + (x & wm))]; }
    void build_mips();                 // box-filtered chain down to 4x4
    const Tex& level(int l) const { return l <= 0 || mips.empty() ? *this : mips[static_cast<size_t>(std::min<int>(l, static_cast<int>(mips.size())) - 1)]; }
};

enum Material : std::uint16_t {
    opaque = 0, cutout = 1, translucent = 2, additive = 4, double_sided = 8, unlit = 16, no_fog = 32, no_depth_write = 64,
    inverted = 128,  // draw back faces only (inverted-hull outlines)
    toon = 256       // per-pixel two-tone light ramp instead of Gouraud
};

struct Vtx {
    V3 p;            // world position
    V3 n{0, 0, 1};   // world normal
    double s = 0, t = 0;  // texture coordinates (in texture repeats)
    Col c{1, 1, 1, 1};    // vertex colour / tint
    float w = 0;          // splat weight of the second texture (terrain blending)
};

struct Lighting {
    V3 sun = norm({-.45, .25, .85});
    Col sun_col{1, .97f, .9f, 1};
    Col amb_col{.42f, .47f, .58f, 1};
    Col fog_col{.75f, .85f, .95f, 1};
    double fog_near = 18, fog_far = 40;  // distance from the focus point (world units)
    V3 focus{};
    float toon_edge = .18f, toon_soft = .10f;  // light ramp threshold and softness
};

class R3D {
public:
    int W = 320, H = 200;
    std::vector<float> rgb;   // W*H*3, linear 0..1
    std::vector<float> depth;
    Lighting light;
    // camera
    V3 target{};        // world point at screen anchor
    double yaw = .785398, pitch = .5236, scale = 22;  // pixels per world unit
    double ax = .5, ay = .55;  // screen anchor as fraction of W,H
    double height_scale = 1;   // world z multiplier
    double time = 0;

    void resize(int w, int h);
    void clear_depth();
    void set_camera();
    void project(V3 p, double& sx, double& sy, double& sz) const;
    bool unproject_ground(double sx, double sy, double ground_z, double& wx, double& wy) const;
    V3 right() const { return R_; }
    V3 up() const { return U_; }
    V3 fwd() const { return F_; }

    // draw a triangle list (3 vertices each)
    void draw(const Vtx* v, size_t count, const Tex* tex, std::uint16_t mat, const M34* model = nullptr,
              const Tex* tex2 = nullptr, const Tex* splat_noise = nullptr);
    // camera-facing quad (billboard) centred at bottom-centre `p`
    void billboard(V3 p, double w, double h, const Tex* tex, Col tint, std::uint16_t mat, double s0 = 0, double s1 = 1);
    // 2D helpers on the low-res buffer (sky, overlays)
    void fill_rect2(int x0, int y0, int x1, int y1, Col c, float a = 1);
    void present(Canvas& out, int scale, int ox, int oy, bool dither) const;

    long long tris_drawn = 0;

private:
    V3 R_{}, U_{}, F_{};
    Col shade_vertex(const Vtx& v, std::uint16_t mat) const;
};

}  // namespace sbx
