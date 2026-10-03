#include "r3d.hpp"

#include <algorithm>

namespace kk {

M34 M34::operator*(const M34& b) const {
    M34 r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j)
            r.m[i * 4 + j] = m[i * 4] * b.m[j] + m[i * 4 + 1] * b.m[4 + j] + m[i * 4 + 2] * b.m[8 + j];
        r.m[i * 4 + 3] = m[i * 4] * b.m[3] + m[i * 4 + 1] * b.m[7] + m[i * 4 + 2] * b.m[11] + m[i * 4 + 3];
    }
    return r;
}
M34 M34::rot_x(double a) { M34 r; const double c = std::cos(a), s = std::sin(a); r.m[5] = c; r.m[6] = -s; r.m[9] = s; r.m[10] = c; return r; }
M34 M34::rot_y(double a) { M34 r; const double c = std::cos(a), s = std::sin(a); r.m[0] = c; r.m[2] = s; r.m[8] = -s; r.m[10] = c; return r; }
M34 M34::rot_z(double a) { M34 r; const double c = std::cos(a), s = std::sin(a); r.m[0] = c; r.m[1] = -s; r.m[4] = s; r.m[5] = c; return r; }
M34 M34::normal_matrix() const {
    const double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const double A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const double D = -(b * i - c * h), E = a * i - c * g, F = -(a * h - b * g);
    const double G = b * f - c * e, H = -(a * f - c * d), I = a * e - b * d;
    double det = a * A + b * B + c * C;
    if (std::fabs(det) < 1e-12) det = 1e-12;
    // inverse = adj / det; inverse-transpose = cofactor / det
    M34 r;
    r.m[0] = A / det; r.m[1] = B / det; r.m[2] = C / det;
    r.m[4] = D / det; r.m[5] = E / det; r.m[6] = F / det;
    r.m[8] = G / det; r.m[9] = H / det; r.m[10] = I / det;
    r.m[3] = r.m[7] = r.m[11] = 0;
    return r;
}

void Tex::build_mips() {
    mips.clear();
    const Tex* src = this;
    while (src->w > 4 && src->h > 4) {
        Tex m;
        m.make(src->w / 2, src->h / 2);
        for (int y = 0; y < m.h; ++y)
            for (int x = 0; x < m.w; ++x) {
                unsigned acc[4] = {0, 0, 0, 0};
                for (int k = 0; k < 4; ++k) {
                    const std::uint32_t c = src->px[static_cast<size_t>((y * 2 + k / 2) * src->w + x * 2 + k % 2)];
                    for (int ch = 0; ch < 4; ++ch) acc[ch] += (c >> (ch * 8)) & 255;
                }
                m.px[static_cast<size_t>(y * m.w + x)] = (acc[0] / 4) | ((acc[1] / 4) << 8) | ((acc[2] / 4) << 16) | ((acc[3] / 4) << 24);
            }
        m.mips.clear();
        mips.push_back(std::move(m));
        src = &mips.back();
    }
}

void R3D::resize(int w, int h) {
    W = std::max(16, w);
    H = std::max(16, h);
    rgb.assign(static_cast<size_t>(W) * H * 3, 0.f);
    depth.assign(static_cast<size_t>(W) * H, 1e30f);
}

void R3D::clear_depth() { std::fill(depth.begin(), depth.end(), 1e30f); }

void R3D::set_camera() {
    R_ = {std::cos(yaw), std::sin(yaw), 0};
    const V3 h{-std::sin(yaw), std::cos(yaw), 0};
    F_ = norm(h * std::cos(pitch) + V3{0, 0, -1} * std::sin(pitch));
    U_ = norm(cross(R_, F_));
}

void R3D::project(V3 p, double& sx, double& sy, double& sz) const {
    const V3 d{p.x - target.x, p.y - target.y, p.z * height_scale - target.z * height_scale};
    sz = dot(d, F_);
    // perspective divides by distance from the eye; the textures stay affine, a 1990s look
    const double k = persp > 0 ? persp / std::max(.05, persp + sz) : 1.0;
    sx = W * ax + dot(d, R_) * scale * k;
    sy = H * ay - dot(d, U_) * scale * k;
}

bool R3D::unproject_plane(double sx, double sy, double gz, double& wx, double& wy) const {
    if (!unproject_ground(sx, sy, gz, wx, wy)) return false;
    if (persp <= 0) return true;
    // Newton steps on the projection, starting from the orthographic answer
    for (int it = 0; it < 8; ++it) {
        double px, py, pz, ax_, ay_, bx, by;
        project({wx, wy, gz}, px, py, pz);
        project({wx + .01, wy, gz}, ax_, ay_, pz);
        project({wx, wy + .01, gz}, bx, by, pz);
        const double j11 = (ax_ - px) / .01, j21 = (ay_ - py) / .01, j12 = (bx - px) / .01, j22 = (by - py) / .01;
        const double det = j11 * j22 - j12 * j21;
        if (std::fabs(det) < 1e-9) return false;
        const double ex = sx - px, ey = sy - py;
        wx += (j22 * ex - j12 * ey) / det;
        wy += (-j21 * ex + j11 * ey) / det;
        if (ex * ex + ey * ey < 1e-4) break;
    }
    return true;
}

bool R3D::unproject_ground(double sx, double sy, double gz, double& wx, double& wy) const {
    const double a = (sx - W * ax) / scale, b = -(sy - H * ay) / scale;
    const double dz = (gz - target.z) * height_scale;
    if (std::fabs(F_.z) < 1e-9) return false;
    const double c = (dz - a * R_.z - b * U_.z) / F_.z;
    const V3 d = R_ * a + U_ * b + F_ * c;
    wx = target.x + d.x;
    wy = target.y + d.y;
    return true;
}

Col R3D::shade_vertex(const Vtx& v, std::uint16_t mat) const {
    if (mat & unlit) return v.c;
    const double nd = std::max(0.0, dot(norm(v.n), light.sun));
    const float k = static_cast<float>(nd);
    return {v.c.r * (light.amb_col.r + light.sun_col.r * k), v.c.g * (light.amb_col.g + light.sun_col.g * k),
            v.c.b * (light.amb_col.b + light.sun_col.b * k), v.c.a};
}

namespace {
struct SV {
    double x, y, z, s, t, r, g, b, a, f, w, l;
};
}  // namespace

void R3D::draw(const Vtx* verts, size_t count, const Tex* tex0, std::uint16_t mat, const M34* model, const Tex* tex2_0, const Tex* splat) {
    M34 nm;
    if (model) nm = model->normal_matrix();
    for (size_t i = 0; i + 2 < count; i += 3) {
        SV sv[3];
        for (int k = 0; k < 3; ++k) {
            Vtx v = verts[i + static_cast<size_t>(k)];
            if (model) {
                // model matrices are already in render space (their z is pre-scaled)
                v.p = model->apply(v.p); v.n = nm.dir(v.n);
                project({v.p.x, v.p.y, v.p.z / height_scale}, sv[k].x, sv[k].y, sv[k].z);
            } else {
                project(v.p, sv[k].x, sv[k].y, sv[k].z);
            }
            const Col c = (mat & toon) ? v.c : shade_vertex(v, mat);
            sv[k].l = (mat & toon) ? dot(norm(v.n), light.sun) : 0;
            sv[k].s = v.s; sv[k].t = v.t; sv[k].w = v.w;
            sv[k].r = c.r; sv[k].g = c.g; sv[k].b = c.b; sv[k].a = c.a;
            double f = 0;
            if (!(mat & no_fog)) {
                const double dist = std::hypot(v.p.x - light.focus.x, v.p.y - light.focus.y);
                f = std::clamp((dist - light.fog_near) / (light.fog_far - light.fog_near), 0.0, 1.0) * .85;
            }
            sv[k].f = f;
        }
        const double area = (sv[1].x - sv[0].x) * (sv[2].y - sv[0].y) - (sv[2].x - sv[0].x) * (sv[1].y - sv[0].y);
        if (std::fabs(area) < 1e-9) continue;
        if (mat & inverted) { if (area < 0) continue; }
        else if (!(mat & double_sided) && area > 0) continue;
        int y0 = static_cast<int>(std::ceil(std::min({sv[0].y, sv[1].y, sv[2].y}) - .5));
        int y1 = static_cast<int>(std::floor(std::max({sv[0].y, sv[1].y, sv[2].y}) - .5));
        y0 = std::max(y0, 0); y1 = std::min(y1, H - 1);
        if (y0 > y1) continue;
        const double minx = std::min({sv[0].x, sv[1].x, sv[2].x}), maxx = std::max({sv[0].x, sv[1].x, sv[2].x});
        if (maxx < 0 || minx > W) continue;
        ++tris_drawn;
        // plane equations for every attribute: a(x,y) = a0 + ax*x + ay*y (affine is exact for an orthographic camera)
        const double inv = 1.0 / area;
        auto plane = [&](double SV::*m, float& ax, float& ay, float& a0) {
            const double d1 = sv[1].*m - sv[0].*m, d2 = sv[2].*m - sv[0].*m;
            const double gx = (d1 * (sv[2].y - sv[0].y) - d2 * (sv[1].y - sv[0].y)) * inv;
            const double gy = (d2 * (sv[1].x - sv[0].x) - d1 * (sv[2].x - sv[0].x)) * inv;
            ax = static_cast<float>(gx); ay = static_cast<float>(gy);
            a0 = static_cast<float>(sv[0].*m - gx * sv[0].x - gy * sv[0].y);
        };
        float zx, zy, z0, sx_, sy_, s0, tx_, ty_, t0, rx, ry, r0, gx_, gy_, g0, bx, by, b0, axx, ayy, a0v, fx, fy, f0;
        plane(&SV::z, zx, zy, z0); plane(&SV::s, sx_, sy_, s0); plane(&SV::t, tx_, ty_, t0);
        plane(&SV::r, rx, ry, r0); plane(&SV::g, gx_, gy_, g0); plane(&SV::b, bx, by, b0);
        plane(&SV::a, axx, ayy, a0v); plane(&SV::f, fx, fy, f0);
        float wx = 0, wy = 0, w0v = 0;
        if (tex2_0) plane(&SV::w, wx, wy, w0v);
        const bool toon_px = mat & toon;
        float lx = 0, ly = 0, l0 = 0;
        if (toon_px) plane(&SV::l, lx, ly, l0);
        const float te0 = light.toon_edge - light.toon_soft * .5f, tinv = 1.f / std::max(1e-4f, light.toon_soft);
        // mip level from the texel footprint of one screen pixel (per triangle)
        const Tex* tex = tex0;
        const Tex* tex2 = tex2_0;
        if (tex0 && !tex0->mips.empty()) {
            const float fx2 = std::max(std::fabs(sx_), std::fabs(sy_)) * tex0->w, fy2 = std::max(std::fabs(tx_), std::fabs(ty_)) * tex0->h;
            const float rho = std::max(fx2, fy2);
            int lvl = 0;
            for (float q = rho; q > 1.5f && lvl < 6; q *= .5f) ++lvl;
            tex = &tex0->level(lvl);
            if (tex2_0) tex2 = &tex2_0->level(lvl);
        }
        const bool cut = mat & cutout, trans = mat & (translucent | additive), add = mat & additive;
        const bool zwrite = !(mat & no_depth_write) && !trans;
        const bool fog = f0 != 0 || fx != 0 || fy != 0;
        for (int y = y0; y <= y1; ++y) {
            const double py = y + .5;
            // span of the triangle on this scanline
            double xl = 1e30, xr = -1e30;
            for (int e = 0; e < 3; ++e) {
                const SV& A = sv[e];
                const SV& B = sv[(e + 1) % 3];
                if ((A.y <= py && B.y > py) || (B.y <= py && A.y > py)) {
                    const double x = A.x + (py - A.y) * (B.x - A.x) / (B.y - A.y);
                    xl = std::min(xl, x); xr = std::max(xr, x);
                }
            }
            if (xl > xr) continue;
            int xa = std::max(0, static_cast<int>(std::ceil(xl - .5)));
            int xb = std::min(W - 1, static_cast<int>(std::floor(xr - .5)));
            if (xa > xb) continue;
            const float fpy = static_cast<float>(py);
            float px0 = static_cast<float>(xa) + .5f;
            float z = z0 + zx * px0 + zy * fpy, s = s0 + sx_ * px0 + sy_ * fpy, t = t0 + tx_ * px0 + ty_ * fpy;
            float cr0 = r0 + rx * px0 + ry * fpy, cg0 = g0 + gx_ * px0 + gy_ * fpy, cb0 = b0 + bx * px0 + by * fpy;
            float ca0 = a0v + axx * px0 + ayy * fpy, f = f0 + fx * px0 + fy * fpy;
            float wv = w0v + wx * px0 + wy * fpy;
            float lv = l0 + lx * px0 + ly * fpy;
            size_t pi = static_cast<size_t>(y) * W + xa;
            for (int x = xa; x <= xb; ++x, ++pi, z += zx, s += sx_, t += tx_, cr0 += rx, cg0 += gx_, cb0 += bx, ca0 += axx, f += fx, wv += wx, lv += lx) {
                if (z >= depth[pi]) continue;
                float cr = cr0, cg = cg0, cb = cb0, ca = ca0;
                if (toon_px) {
                    // two-tone anime light: shadow side takes the ambient tone, lit side the sun
                    float k = (lv - te0) * tinv;
                    k = k < 0 ? 0 : (k > 1 ? 1 : k);
                    k = k * k * (3 - 2 * k);
                    cr *= light.amb_col.r + light.sun_col.r * k;
                    cg *= light.amb_col.g + light.sun_col.g * k;
                    cb *= light.amb_col.b + light.sun_col.b * k;
                }
                if (tex) {
                    // organic splat: a world-locked noise threshold picks the second ground texture
                    const Tex* tt = tex;
                    if (tex2 && wv > .02f) {
                        const int nx = static_cast<int>(s * 64.f + 1048576.f) & splat->wm, ny = static_cast<int>(t * 64.f + 1048576.f) & splat->hm;
                        const float nz = static_cast<float>(splat->px[static_cast<size_t>(ny * splat->w + nx)] & 255) * (1.f / 255.f);
                        if (wv > nz) tt = tex2;
                    }
                    // texel coordinates via a positive bias instead of floor()
                    const int tx = static_cast<int>(s * static_cast<float>(tt->w) + 1048576.f) & tt->wm;
                    const int ty = static_cast<int>(t * static_cast<float>(tt->h) + 1048576.f) & tt->hm;
                    const std::uint32_t c = tt->px[static_cast<size_t>(ty * tt->w + tx)];
                    const float ta = static_cast<float>(c >> 24) * (1.f / 255.f);
                    if (cut && ta < .5f) continue;
                    cr *= static_cast<float>((c >> 16) & 255) * (1.f / 255.f);
                    cg *= static_cast<float>((c >> 8) & 255) * (1.f / 255.f);
                    cb *= static_cast<float>(c & 255) * (1.f / 255.f);
                    ca *= ta;
                }
                if (fog && f > 0) {
                    cr += (light.fog_col.r - cr) * f; cg += (light.fog_col.g - cg) * f; cb += (light.fog_col.b - cb) * f;
                }
                float* o = rgb.data() + pi * 3;
                if (add) {
                    o[0] += cr * ca; o[1] += cg * ca; o[2] += cb * ca;
                } else if (trans) {
                    o[0] += (cr - o[0]) * ca; o[1] += (cg - o[1]) * ca; o[2] += (cb - o[2]) * ca;
                } else {
                    o[0] = cr; o[1] = cg; o[2] = cb;
                }
                if (zwrite) depth[pi] = z;
            }
        }
    }
}

void R3D::billboard(V3 p, double w, double h, const Tex* tex, Col tint, std::uint16_t mat, double s0, double s1) {
    const V3 r = R_ * (w * .5);
    const V3 up{0, 0, h / height_scale};
    const V3 n = F_ * -1.0;
    Vtx q[6];
    const V3 a = p - r, b = p + r, c = p + r + up, d = p - r + up;
    q[0] = {a, n, s0, 1, tint}; q[1] = {b, n, s1, 1, tint}; q[2] = {c, n, s1, 0, tint};
    q[3] = {a, n, s0, 1, tint}; q[4] = {c, n, s1, 0, tint}; q[5] = {d, n, s0, 0, tint};
    draw(q, 6, tex, static_cast<std::uint16_t>(mat | double_sided));
}

void R3D::fill_rect2(int x0, int y0, int x1, int y1, Col c, float a) {
    x0 = std::max(0, x0); y0 = std::max(0, y0); x1 = std::min(W, x1); y1 = std::min(H, y1);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            float* o = rgb.data() + (static_cast<size_t>(y) * W + x) * 3;
            o[0] += (c.r - o[0]) * a; o[1] += (c.g - o[1]) * a; o[2] += (c.b - o[2]) * a;
        }
}

void R3D::present(Canvas& out, int s, int ox, int oy, bool dither) const {
    // 15-bit colour with a 4x4 ordered dither, done with integer lookups
    static const int bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    static std::uint8_t level8[32];
    static bool init = false;
    if (!init) { for (int i = 0; i < 32; ++i) level8[i] = static_cast<std::uint8_t>(i * 255 / 31); init = true; }
    auto q = [&](float v, int th) -> std::uint8_t {
        int iv = static_cast<int>(v * (31.f * 16.f));
        iv = iv < 0 ? 0 : (iv > 31 * 16 ? 31 * 16 : iv);
        int l = (iv + th) >> 4;
        return level8[l > 31 ? 31 : l];
    };
    for (int y = 0; y < H; ++y) {
        const float* c = rgb.data() + static_cast<size_t>(y) * W * 3;
        for (int x = 0; x < W; ++x, c += 3) {
            std::uint8_t r8, g8, b8;
            if (dither) {
                const int th = bayer[(y & 3) * 4 + (x & 3)];
                r8 = q(c[0], th); g8 = q(c[1], th); b8 = q(c[2], th);
            } else {
                r8 = static_cast<std::uint8_t>(std::clamp(c[0], 0.f, 1.f) * 255);
                g8 = static_cast<std::uint8_t>(std::clamp(c[1], 0.f, 1.f) * 255);
                b8 = static_cast<std::uint8_t>(std::clamp(c[2], 0.f, 1.f) * 255);
            }
            for (int yy = 0; yy < s; ++yy) {
                const int dy = oy + y * s + yy;
                if (dy < 0 || dy >= out.h) continue;
                std::uint8_t* row = out.px.data() + static_cast<size_t>(dy) * out.w * 4;
                for (int xx = 0; xx < s; ++xx) {
                    const int dx = ox + x * s + xx;
                    if (dx < 0 || dx >= out.w) continue;
                    std::uint8_t* o = row + dx * 4;
                    o[0] = b8; o[1] = g8; o[2] = r8; o[3] = 255;
                }
            }
        }
    }
}

}  // namespace kk
