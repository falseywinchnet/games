#pragma once
// A first-person software renderer in the spirit of 1995: perspective-correct
// texture mapping (walls rush past the camera, so affine mapping would warp),
// near-plane clipping, a z-buffer, nearest-neighbour texels, depth fog, and a
// camera that can pitch and roll right over onto the ceiling. In a blackout the
// only light is a lamp on the camera. Output is 0xAARRGGBB, which is the byte
// order a Canvas (BGRA in memory) already uses.
#include <cmath>
#include <cstdint>
#include <vector>

namespace mz {

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

struct Tex32 {
    int w = 0, h = 0, wm = 0, hm = 0;
    std::vector<std::uint32_t> px;  // 0xAARRGGBB, power-of-two sizes
    void make(int width, int height) { w = width; h = height; wm = w - 1; hm = h - 1; px.assign(static_cast<size_t>(w) * h, 0xFFFF00FFu); }
    std::uint32_t at(int x, int y) const { return px[static_cast<size_t>((y & hm) * w + (x & wm))]; }
};

struct Vert {
    V3 p;
    float u = 0, v = 0;          // texture coordinates, in texels/texture size (repeats wrap)
    float r = 1, g = 1, b = 1;   // tint / baked light
};

enum Flags : std::uint32_t {
    kAlphaTest = 1,     // texels with alpha < 128 are holes
    kAdditive = 2,      // add to the frame (glows)
    kNoZWrite = 4,
    kUnlit = 8,         // ignore fog and darkness
    kTwoSided = 16,
    kGlitch = 32,       // datamosh: shear rows, split the colour channels
    kTranslucent = 64,  // 50% blend
};

class Soft3D {
public:
    int W = 320, H = 200;
    std::vector<std::uint32_t> color;
    std::vector<float> zinv;   // 1/z per pixel (0 = far)

    // camera
    V3 eye{};
    double yaw = 0, pitch = 0, roll = 0;   // yaw 0 looks north (+y); turning right increases it
    double fov = 1.2;                      // horizontal field of view (radians)
    double near_z = .04;

    // light
    std::uint32_t fog_rgb = 0x000000;
    double fog_start = 6, fog_end = 16;
    bool blackout = false;
    double lamp_radius = 3.2;
    double time = 0;

    void resize(int w, int h);
    void begin(std::uint32_t clear_rgb);   // clears, and fixes the camera for this frame
    void tri(const Vert& a, const Vert& b, const Vert& c, const Tex32* tex, std::uint32_t flags = 0);
    void quad(const Vert& a, const Vert& b, const Vert& c, const Vert& d, const Tex32* tex, std::uint32_t flags = 0) {
        tri(a, b, c, tex, flags);
        tri(a, c, d, tex, flags);
    }
    // a camera-facing rectangle standing at `base` (its bottom centre), `w` by `h` world units; `spin` squeezes it
    // horizontally (cos of an angle) to fake a turntable; `up_world` false makes it face the camera's own up
    void sprite(V3 base, double w, double h, const Tex32* tex, std::uint32_t flags = kAlphaTest, float tint = 1, double spin = 1);
    bool project(V3 p, double& sx, double& sy, double& depth) const;
    V3 right() const { return R_; }
    V3 up() const { return U_; }
    V3 fwd() const { return F_; }
    long long tris = 0;

private:
    V3 R_{}, U_{}, F_{};
    double focal_ = 1;
    float light_lut_[512];
    double lut_scale_ = 1;
    struct CV { double x, y, z; float u, v, r, g, b; };
    void raster(const CV* v, int n, const Tex32* tex, std::uint32_t flags);
    void tri_screen(const CV& a, const CV& b, const CV& c, const Tex32* tex, std::uint32_t flags);
};

}  // namespace mz
