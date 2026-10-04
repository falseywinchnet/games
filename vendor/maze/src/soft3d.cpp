#include "soft3d.hpp"

#include <algorithm>

namespace mz {

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
        double f;
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
    CV v[3];
    const Vert* in[3] = {&a, &b, &c};
    for (int k = 0; k < 3; ++k) {
        const V3 d = in[k]->p - eye;
        v[k] = {dot(d, R_), dot(d, U_), dot(d, F_), in[k]->u, in[k]->v, in[k]->r, in[k]->g, in[k]->b};
    }
    // cheap reject: wholly behind the camera
    if (v[0].z < near_z && v[1].z < near_z && v[2].z < near_z) return;
    // clip against the near plane
    CV out[4];
    int n = 0;
    for (int k = 0; k < 3; ++k) {
        const CV& p = v[k];
        const CV& q = v[(k + 1) % 3];
        const bool pin = p.z >= near_z, qin = q.z >= near_z;
        if (pin) out[n++] = p;
        if (pin != qin) {
            const double t = (near_z - p.z) / (q.z - p.z);
            auto L = [t](double x0, double x1) { return x0 + (x1 - x0) * t; };
            auto Lf = [t](float x0, float x1) { return static_cast<float>(x0 + (x1 - x0) * t); };
            out[n++] = {L(p.x, q.x), L(p.y, q.y), near_z, Lf(p.u, q.u), Lf(p.v, q.v), Lf(p.r, q.r), Lf(p.g, q.g), Lf(p.b, q.b)};
        }
    }
    if (n < 3) return;
    raster(out, n, tex, flags);
}

void Soft3D::raster(const CV* v, int n, const Tex32* tex, std::uint32_t flags) {
    // to the screen; attributes divided by z so they interpolate correctly in perspective
    CV s[4];
    for (int k = 0; k < n; ++k) {
        const double iz = 1 / v[k].z;
        s[k] = {W * .5 + v[k].x * iz * focal_, H * .5 - v[k].y * iz * focal_, iz, static_cast<float>(v[k].u * iz), static_cast<float>(v[k].v * iz),
                static_cast<float>(v[k].r * iz), static_cast<float>(v[k].g * iz), static_cast<float>(v[k].b * iz)};
    }
    for (int k = 1; k + 1 < n; ++k) tri_screen(s[0], s[k], s[k + 1], tex, flags);
}

void Soft3D::tri_screen(const CV& a, const CV& b, const CV& c, const Tex32* tex, std::uint32_t flags) {
    const double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::fabs(area) < 1e-9) return;
    const double inv = 1 / area;
    const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x})))), x1 = std::min(W - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y})))), y1 = std::min(H - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
    if (x0 > x1 || y0 > y1) return;
    ++tris;
    const bool alpha = flags & kAlphaTest, add = flags & kAdditive, zw = !(flags & kNoZWrite), unlit = flags & kUnlit, glitch = flags & kGlitch,
               half = flags & kTranslucent;
    const float fr = ((fog_rgb >> 16) & 255) / 255.f, fg = ((fog_rgb >> 8) & 255) / 255.f, fb = (fog_rgb & 255) / 255.f;
    for (int y = y0; y <= y1; ++y) {
        const double py = y + .5;
        const int shear = glitch && ((y + static_cast<int>(time * 9)) % 9 < 2) ? static_cast<int>(6 * std::sin(y * .7 + time * 17)) : 0;
        bool was_in = false;
        for (int x = x0; x <= x1; ++x) {
            const double px = x + .5;
            const double w1 = ((c.x - a.x) * (py - a.y) - (c.y - a.y) * (px - a.x)) * -inv;
            const double w2 = ((b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x)) * inv;
            const double w0 = 1 - w1 - w2;
            if (w0 < 0 || w1 < 0 || w2 < 0) { if (was_in) break; continue; }  // a triangle's row is one unbroken span
            was_in = true;
            const double iz = w0 * a.z + w1 * b.z + w2 * c.z;
            const size_t i = static_cast<size_t>(y) * W + x;
            if (iz <= zinv[i]) continue;
            const double z = 1 / iz;
            const float u = static_cast<float>((w0 * a.u + w1 * b.u + w2 * c.u) * z), vv = static_cast<float>((w0 * a.v + w1 * b.v + w2 * c.v) * z);
            float r = static_cast<float>((w0 * a.r + w1 * b.r + w2 * c.r) * z), g = static_cast<float>((w0 * a.g + w1 * b.g + w2 * c.g) * z),
                  bl = static_cast<float>((w0 * a.b + w1 * b.b + w2 * c.b) * z);
            std::uint32_t t = 0xFFFFFFFFu;
            if (tex) {
                const int tx = static_cast<int>(std::floor(u * tex->w)) + shear, ty = static_cast<int>(std::floor(vv * tex->h));
                t = tex->at(tx, ty);
                if (glitch) {
                    // split the red channel off sideways
                    const std::uint32_t t2 = tex->at(tx + 3, ty);
                    t = (t & 0xFF00FFFFu) | (t2 & 0x00FF0000u);
                }
                if (alpha && (t >> 24) < 128) continue;
            }
            float lr = r, lg = g, lb = bl;
            float k = 1;
            if (!unlit) {
                const int li = std::min(511, static_cast<int>(z * lut_scale_));
                k = light_lut_[li];
            }
            float cr = ((t >> 16) & 255) / 255.f * lr, cg = ((t >> 8) & 255) / 255.f * lg, cb = (t & 255) / 255.f * lb;
            if (!unlit) {
                if (blackout) { cr *= k; cg *= k; cb *= k; }
                else { cr = cr * k + fr * (1 - k); cg = cg * k + fg * (1 - k); cb = cb * k + fb * (1 - k); }
            }
            std::uint32_t& d = color[i];
            if (add || half) {
                const float dr = ((d >> 16) & 255) / 255.f, dg = ((d >> 8) & 255) / 255.f, db = (d & 255) / 255.f;
                if (add) { cr += dr; cg += dg; cb += db; }
                else { cr = (cr + dr) * .5f; cg = (cg + dg) * .5f; cb = (cb + db) * .5f; }
            }
            const auto q8 = [](float f) { return static_cast<std::uint32_t>(std::clamp(f, 0.f, 1.f) * 255.f + .5f); };
            d = 0xFF000000u | q8(cr) << 16 | q8(cg) << 8 | q8(cb);
            if (zw) zinv[i] = static_cast<float>(iz);
        }
    }
}

void Soft3D::sprite(V3 base, double w, double h, const Tex32* tex, std::uint32_t flags, float tint, double spin) {
    // stands upright in the world, turned to face the camera
    const V3 rh = norm(V3{std::cos(yaw), -std::sin(yaw), 0}) * (w * .5 * spin);
    const V3 up{0, 0, h};
    Vert q[4] = {{base - rh, 0, 1, tint, tint, tint}, {base + rh, 1, 1, tint, tint, tint}, {base + rh + up, 1, 0, tint, tint, tint}, {base - rh + up, 0, 0, tint, tint, tint}};
    quad(q[0], q[1], q[2], q[3], tex, flags | kTwoSided);
}

}  // namespace mz
