#include "soft3d.hpp"

#include "r3d_scan.hpp"

#include <algorithm>

namespace mz {
namespace {

// A channel (0..1) to a byte, rounded.
std::uint32_t byte_of(float value) {
    const std::uint32_t result = static_cast<std::uint32_t>(std::clamp(value, 0.f, 1.f) * 255.f + .5f);
    return result;
}

// What happens at a pixel a maze triangle covers: the texel under the perspective-
// correct u, v (sheared and split for a glitching paint), the corners' tint, the light
// by depth (fog, or the lamp in a blackout), the blend the flags ask for, the depth
// write. The frame's buffers and the frame's light are read into it once per draw.
struct Pixel {
    static constexpr bool perspective = true;
    int width = 0;
    std::uint32_t* color = nullptr;
    float* zinv = nullptr;
    // the light, fixed for the frame
    const float* light_lut = nullptr;
    float lut_scale = 1;
    bool blackout = false;
    float fog_r = 0, fog_g = 0, fog_b = 0;
    double time = 0;
    // the triangle
    const Tex32* tex = nullptr;
    float u[3] = {0, 0, 0};
    float v[3] = {0, 0, 0};
    float r[3] = {1, 1, 1};
    float g[3] = {1, 1, 1};
    float b[3] = {1, 1, 1};
    bool alpha_test = false, additive = false, z_write = true, unlit = false, glitch = false, translucent = false;
    bool rain = false, slide = false;
    // the glitch's shear, solved once per row
    int shear_row = -1;
    int shear = 0;

    bool test(int x, int y, float inverse_depth) const {
        const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
        const bool nearer = inverse_depth > zinv[index];
        return nearer;
    }
    void cover(const render::r3d::Coverage& at, bool) {
        const std::size_t index = static_cast<std::size_t>(at.y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(at.x);
        const float z = 1.f / at.inverse_depth;
        const float tu = u[0] * at.weight0 + u[1] * at.weight1 + u[2] * at.weight2;
        const float tv = v[0] * at.weight0 + v[1] * at.weight1 + v[2] * at.weight2;
        std::uint32_t texel = 0xFFFFFFFFu;
        if (tex != nullptr) {
            if (glitch && at.y != shear_row) {
                // datamosh: every ninth row or so slides sideways, drifting with time
                shear_row = at.y;
                shear = (at.y + static_cast<int>(time * 9)) % 9 < 2 ? static_cast<int>(6 * std::sin(at.y * .7 + time * 17)) : 0;
            }
            const int tx = static_cast<int>(std::floor(tu * static_cast<float>((*tex).w))) + (glitch ? shear : 0);
            int ty = static_cast<int>(std::floor(tv * static_cast<float>((*tex).h)));
            if (rain) {
                // every 8-texel column falls at its own pace
                ty -= static_cast<int>(time * (10 + ((tx >> 3) & 15) * 2.5));
            }
            if (slide) {
                // the whole texture creeps up the wall
                ty += static_cast<int>(time * 5);
            }
            texel = (*tex).at(tx, ty);
            if (glitch) {
                // split the red channel off sideways
                const std::uint32_t beside = (*tex).at(tx + 3, ty);
                texel = (texel & 0xFF00FFFFu) | (beside & 0x00FF0000u);
            }
            if (alpha_test && (texel >> 24) < 128) {
                return;
            }
        }
        const float lr = r[0] * at.weight0 + r[1] * at.weight1 + r[2] * at.weight2;
        const float lg = g[0] * at.weight0 + g[1] * at.weight1 + g[2] * at.weight2;
        const float lb = b[0] * at.weight0 + b[1] * at.weight1 + b[2] * at.weight2;
        float cr = static_cast<float>((texel >> 16) & 255) / 255.f * lr;
        float cg = static_cast<float>((texel >> 8) & 255) / 255.f * lg;
        float cb = static_cast<float>(texel & 255) / 255.f * lb;
        if (!unlit) {
            const int li = std::min(511, static_cast<int>(z * lut_scale));
            const float k = light_lut[li];
            if (blackout) {
                cr *= k;
                cg *= k;
                cb *= k;
            } else {
                cr = cr * k + fog_r * (1 - k);
                cg = cg * k + fog_g * (1 - k);
                cb = cb * k + fog_b * (1 - k);
            }
        }
        if (additive || translucent) {
            const std::uint32_t under = color[index];
            const float dr = static_cast<float>((under >> 16) & 255) / 255.f;
            const float dg = static_cast<float>((under >> 8) & 255) / 255.f;
            const float db = static_cast<float>(under & 255) / 255.f;
            if (additive) {
                cr += dr;
                cg += dg;
                cb += db;
            } else {
                cr = (cr + dr) * .5f;
                cg = (cg + dg) * .5f;
                cb = (cb + db) * .5f;
            }
        }
        color[index] = 0xFF000000u | (byte_of(cr) << 16) | (byte_of(cg) << 8) | byte_of(cb);
        if (z_write) {
            zinv[index] = at.inverse_depth;
        }
    }
};

}  // namespace

void Soft3D::resize(int w, int h) {
    W = std::max(16, w);
    H = std::max(16, h);
    color.assign(static_cast<size_t>(W) * H, 0xFF000000u);
    zinv.assign(static_cast<size_t>(W) * H, 0.f);
}

void Soft3D::begin(std::uint32_t clear_rgb) {
    std::fill(color.begin(), color.end(), 0xFF000000u | clear_rgb);
    std::fill(zinv.begin(), zinv.end(), 0.f);
    const double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    F_ = {sy * cp, cy * cp, sp};
    const V3 r0{cy, -sy, 0};
    const V3 u0 = cross(r0, F_);
    const double cr = std::cos(roll), sr = std::sin(roll);
    R_ = r0 * cr + u0 * sr;
    U_ = u0 * cr - r0 * sr;
    focal_ = (W * .5) / std::tan(fov * .5);
    // light against depth: fog in the distance, or the camera's own lamp in a blackout
    lut_scale_ = 512 / 40.0;
    for (int i = 0; i < 512; ++i) {
        const double d = i / lut_scale_;
        double f = 1;
        if (blackout) {
            const double k = std::clamp(1 - d / lamp_radius, 0.0, 1.0);
            f = std::pow(k, 1.4) * 1.15;
        } else {
            f = d < fog_start ? 1.0 : std::max(0.0, 1 - (d - fog_start) / (fog_end - fog_start));
        }
        light_lut_[i] = static_cast<float>(std::min(1.15, f));
    }
    tris = 0;
}

bool Soft3D::project(V3 p, double& sx, double& sy, double& depth) const {
    const V3 d = p - eye;
    const double z = dot(d, F_);
    if (z < near_z) return false;
    sx = W * .5 + dot(d, R_) / z * focal_;
    sy = H * .5 - dot(d, U_) / z * focal_;
    depth = z;
    return true;
}

void Soft3D::tri(const Vert& a, const Vert& b, const Vert& c, const Tex32* tex, std::uint32_t flags) {
    // facing, decided in the world: every surface winds so its normal points away from the side it is seen from
    if (!(flags & kTwoSided) && dot(cross(b.p - a.p, c.p - a.p), eye - a.p) >= 0) return;
    const Vert* in[3] = {&a, &b, &c};
    render::r3d::ViewPoint view[3];
    for (int k = 0; k < 3; ++k) {
        const V3 d = (*in[k]).p - eye;
        view[k] = {static_cast<float>(dot(d, R_)), static_cast<float>(dot(d, U_)), static_cast<float>(dot(d, F_))};
    }
    // cheap reject: wholly behind the camera
    const float near_plane = static_cast<float>(near_z);
    if (view[0].depth < near_plane && view[1].depth < near_plane && view[2].depth < near_plane) return;
    Pixel pixel;
    pixel.width = W;
    pixel.color = color.data();
    pixel.zinv = zinv.data();
    pixel.light_lut = light_lut_;
    pixel.lut_scale = static_cast<float>(lut_scale_);
    pixel.blackout = blackout;
    pixel.fog_r = static_cast<float>((fog_rgb >> 16) & 255) / 255.f;
    pixel.fog_g = static_cast<float>((fog_rgb >> 8) & 255) / 255.f;
    pixel.fog_b = static_cast<float>(fog_rgb & 255) / 255.f;
    pixel.time = time;
    pixel.tex = tex;
    for (int k = 0; k < 3; ++k) {
        pixel.u[k] = (*in[k]).u;
        pixel.v[k] = (*in[k]).v;
        pixel.r[k] = (*in[k]).r;
        pixel.g[k] = (*in[k]).g;
        pixel.b[k] = (*in[k]).b;
    }
    pixel.alpha_test = (flags & kAlphaTest) != 0;
    pixel.additive = (flags & kAdditive) != 0;
    pixel.z_write = (flags & kNoZWrite) == 0;
    pixel.unlit = (flags & kUnlit) != 0;
    pixel.glitch = (flags & kGlitch) != 0;
    pixel.translucent = (flags & kTranslucent) != 0;
    pixel.rain = (flags & kRain) != 0;
    pixel.slide = (flags & kSlide) != 0;
    ++tris;
    const render::r3d::Lens lens{W, H, static_cast<float>(focal_), near_plane};
    render::r3d::rasterize(pixel, lens, view[0], view[1], view[2], render::r3d::Cull::none);
}

void Soft3D::sprite(V3 base, double w, double h, const Tex32* tex, std::uint32_t flags, float tint, double spin) {
    // stands upright in the world, turned to face the camera
    const V3 rh = norm(V3{std::cos(yaw), -std::sin(yaw), 0}) * (w * .5 * spin);
    const V3 up{0, 0, h};
    Vert q[4] = {{base - rh, 0, 1, tint, tint, tint}, {base + rh, 1, 1, tint, tint, tint}, {base + rh + up, 1, 0, tint, tint, tint}, {base - rh + up, 0, 0, tint, tint, tint}};
    quad(q[0], q[1], q[2], q[3], tex, flags | kTwoSided);
}

}  // namespace mz
