#include "r3d.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <algorithm>

namespace zc {

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

// the ambient light a surface facing `n` receives
static Col ambient_for(const Lighting& light, V3 n) {
    if (!light.hemisphere) return light.amb_col;
    const float up = static_cast<float>(std::clamp((norm(n).z + 1) * .5, 0.0, 1.0));
    return mix(light.amb_ground, light.amb_col, up);
}

Col R3D::shade_vertex(const Vtx& v, std::uint16_t mat) const {
    if (mat & unlit) return v.c;
    const double nd = std::max(0.0, dot(norm(v.n), light.sun));
    const float k = static_cast<float>(nd);
    const Col amb = ambient_for(light, v.n);
    return {v.c.r * (amb.r + light.sun_col.r * k), v.c.g * (amb.g + light.sun_col.g * k),
            v.c.b * (amb.b + light.sun_col.b * k), v.c.a};
}

void R3D::shadow_coords(V3 p, double& u, double& v, double& h) const {
    const V3 d = p - shadow_centre_;
    u = dot(d, shadow_r_) * shadow_scale_ + shadow_size_ * .5;
    v = dot(d, shadow_u_) * shadow_scale_ + shadow_size_ * .5;
    h = dot(d, shadow_sun_);
}

void R3D::shadow_begin(V3 centre, double radius, int size) {
    shadow_size_ = std::max(16, size);
    shadow_map_.assign(static_cast<size_t>(shadow_size_) * static_cast<size_t>(shadow_size_), -1e30f);
    shadow_centre_ = centre;
    shadow_sun_ = norm(light.sun);
    const V3 helper = std::fabs(shadow_sun_.z) < .95 ? V3{0, 0, 1} : V3{1, 0, 0};
    shadow_r_ = norm(cross(helper, shadow_sun_));
    shadow_u_ = cross(shadow_sun_, shadow_r_);
    shadow_scale_ = shadow_size_ / (2 * radius);
    shadow_saved_.clear();
    shadow_dirty_x1_ = shadow_dirty_y1_ = -1;
    shadows_on_ = false;
}

void R3D::shadow_save() {
    shadow_saved_ = shadow_map_;
    shadow_dirty_x1_ = shadow_dirty_y1_ = -1;
}

void R3D::shadow_restore() {
    if (!shadow_saved()) return;
    // Moving casters touch only a small part of the map. Restore those spans
    // before the next frame, including the old location of a departing caster.
    for (int y = shadow_dirty_y0_; y <= shadow_dirty_y1_; y += 1) {
        const std::size_t row = static_cast<std::size_t>(y) * shadow_size_;
        std::copy(shadow_saved_.begin() + row + shadow_dirty_x0_,
                  shadow_saved_.begin() + row + shadow_dirty_x1_ + 1,
                  shadow_map_.begin() + row + shadow_dirty_x0_);
    }
    shadow_dirty_x1_ = shadow_dirty_y1_ = -1;
}

// Rasterises casters into the map, keeping each texel's height toward the sun
// of the nearest surface; both faces cast.
void R3D::shadow_cast(const Vtx* verts, size_t count, const M34* model) {
    if (shadow_size_ <= 0) return;
    const int n = shadow_size_;
    for (size_t i = 0; i + 2 < count; i += 3) {
        double u[3], v[3], h[3];
        for (int k = 0; k < 3; ++k) {
            V3 p = verts[i + static_cast<size_t>(k)].p;
            if (model) p = model->apply(p);
            shadow_coords(p, u[k], v[k], h[k]);
        }
        const double area = (u[1] - u[0]) * (v[2] - v[0]) - (u[2] - u[0]) * (v[1] - v[0]);
        if (std::fabs(area) < 1e-12) continue;
        const int y0 = std::max(0, static_cast<int>(std::ceil(std::min({v[0], v[1], v[2]}) - .5)));
        const int y1 = std::min(n - 1, static_cast<int>(std::floor(std::max({v[0], v[1], v[2]}) - .5)));
        if (y0 > y1) continue;
        if (std::max({u[0], u[1], u[2]}) < 0 || std::min({u[0], u[1], u[2]}) > n) continue;
        const int x0 = std::max(0, static_cast<int>(std::ceil(std::min({u[0], u[1], u[2]}) - .5)));
        const int x1 = std::min(n - 1, static_cast<int>(std::floor(std::max({u[0], u[1], u[2]}) - .5)));
        if (x0 > x1) continue;
        if (shadow_dirty_x1_ < 0) {
            shadow_dirty_x0_ = x0; shadow_dirty_x1_ = x1;
            shadow_dirty_y0_ = y0; shadow_dirty_y1_ = y1;
        } else {
            shadow_dirty_x0_ = std::min(shadow_dirty_x0_, x0);
            shadow_dirty_x1_ = std::max(shadow_dirty_x1_, x1);
            shadow_dirty_y0_ = std::min(shadow_dirty_y0_, y0);
            shadow_dirty_y1_ = std::max(shadow_dirty_y1_, y1);
        }
        // height as a plane over the map, filled a scanline span at a time
        const double inv = 1.0 / area;
        const double d1 = h[1] - h[0], d2 = h[2] - h[0];
        const double gx = (d1 * (v[2] - v[0]) - d2 * (v[1] - v[0])) * inv;
        const double gy = (d2 * (u[1] - u[0]) - d1 * (u[2] - u[0])) * inv;
        const double g0 = h[0] - gx * u[0] - gy * v[0];
        for (int y = y0; y <= y1; ++y) {
            const double py = y + .5;
            double xl = 1e30, xr = -1e30;
            for (int e = 0; e < 3; ++e) {
                const int f = (e + 1) % 3;
                if ((v[e] <= py && v[f] > py) || (v[f] <= py && v[e] > py)) {
                    const double x = u[e] + (py - v[e]) * (u[f] - u[e]) / (v[f] - v[e]);
                    xl = std::min(xl, x); xr = std::max(xr, x);
                }
            }
            if (xl > xr) continue;
            const int xa = std::max(0, static_cast<int>(std::ceil(xl - .5)));
            const int xb = std::min(n - 1, static_cast<int>(std::floor(xr - .5)));
            float hh = static_cast<float>(g0 + gx * (xa + .5) + gy * py);
            const float step = static_cast<float>(gx);
            float* cell = shadow_map_.data() + static_cast<size_t>(y) * static_cast<size_t>(n);
            for (int x = xa; x <= xb; ++x, hh += step) {
                if (hh > cell[x]) cell[x] = hh;
            }
        }
    }
}

void R3D::shadow_screen_pass(float sun_share) {
    if (shadow_size_ <= 0) return;
    BandJob job;
    job.self = this;
    job.kind = 1;
    job.sun_share = sun_share;
    run_bands(&job, 0);
}

void R3D::shadow_band(float sun_share, int band0, int band1) {
    // Compose camera -> world -> light once per band. The depth-dependent
    // perspective factor is still evaluated per pixel, with the same 2x2 PCF.
    const V3 world_r{R_.x, R_.y, R_.z / height_scale};
    const V3 world_u{U_.x, U_.y, U_.z / height_scale};
    const V3 world_f{F_.x, F_.y, F_.z / height_scale};
    const V3 right{dot(world_r, shadow_r_) * shadow_scale_, dot(world_r, shadow_u_) * shadow_scale_, dot(world_r, shadow_sun_)};
    const V3 up{dot(world_u, shadow_r_) * shadow_scale_, dot(world_u, shadow_u_) * shadow_scale_, dot(world_u, shadow_sun_)};
    const V3 forward{dot(world_f, shadow_r_) * shadow_scale_, dot(world_f, shadow_u_) * shadow_scale_, dot(world_f, shadow_sun_)};
    V3 origin;
    shadow_coords(target, origin.x, origin.y, origin.z);
    for (int y = band0; y <= band1; ++y) {
        const V3 row = up * (H * ay - (y + .5));
        for (int x = 0; x < W; ++x) {
            const size_t pi = static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x);
            const float z = depth[pi];
            if (z >= 1e29f) continue;
            const double factor = (persp > 0 ? std::max(.05, persp + z) / persp : 1.0) / scale;
            const V3 light_point = origin + (right * (x + .5 - W * ax) + row) * factor + forward * static_cast<double>(z);
            const float lit = shadow_lit(static_cast<float>(light_point.x), static_cast<float>(light_point.y), static_cast<float>(light_point.z));
            if (lit >= 1.f) continue;
            const float keep = 1.f - sun_share * shadow_darkness * (1.f - lit);
            float* o = rgb.data() + pi * 3;
            o[0] *= keep; o[1] *= keep; o[2] *= keep;
        }
    }
}

// Share of the sun reaching a point (0 shadowed, 1 lit), from a 2x2 filtered lookup.
float R3D::shadow_lit(float u, float v, float h) const {
    const int n = shadow_size_;
    if (u <= -.5f || v <= -.5f || u >= n + .5f || v >= n + .5f) return 1.f;
    const float bias = static_cast<float>(1.6 / shadow_scale_);   // about 1.6 texels of height
    const float fu = u - .5f, fv = v - .5f;
    const int x0 = static_cast<int>(std::floor(fu)), y0 = static_cast<int>(std::floor(fv));
    const float ax = fu - static_cast<float>(x0), ay = fv - static_cast<float>(y0);
    float taps[4];
    if (x0 >= 0 && y0 >= 0 && x0 + 1 < n && y0 + 1 < n) {
        const float* first = shadow_map_.data() + static_cast<std::size_t>(y0) * n + x0;
        const float compare = h + bias;
        taps[0] = compare >= first[0] ? 1.f : 0.f;
        taps[1] = compare >= first[1] ? 1.f : 0.f;
        taps[2] = compare >= first[n] ? 1.f : 0.f;
        taps[3] = compare >= first[n + 1] ? 1.f : 0.f;
    } else {
        for (int k = 0; k < 4; ++k) {
            const int x = x0 + (k & 1), y = y0 + (k >> 1);
            if (x < 0 || y < 0 || x >= n || y >= n) { taps[k] = 1; continue; }
            const float stored = shadow_map_[static_cast<size_t>(y) * static_cast<size_t>(n) + static_cast<size_t>(x)];
            taps[k] = h + bias >= stored ? 1.f : 0.f;
        }
    }
    const float top = taps[0] + (taps[1] - taps[0]) * ax;
    const float bottom = taps[2] + (taps[3] - taps[2]) * ax;
    return top + (bottom - top) * ay;
}



// ---------------------------------------------------------------- bands across the cores

// Worker threads that fill bands of rows. The screen is cut into many more
// bands than threads; each thread takes the next band free, so a band of sky
// costs nothing and nobody waits long on a busy one.
struct R3D::Pool {
    std::vector<std::thread> threads;
    std::mutex lock;
    std::condition_variable wake, finished;
    const BandJob* job = nullptr;
    int generation = 0;
    int busy = 0;
    int bands = 0;
    int rows = 0;
    std::atomic<int> next{0};
    bool quit = false;
};

namespace {

void work_bands(R3D::Pool* pool);

}  // namespace

static void take_bands(R3D::Pool& pool, const R3D::BandJob& job);

void R3D::run_bands(const BandJob* job, int serial) {
    if (serial != 0) {
        if ((*job).kind == 0) {
            raster_band(*job, 0, H - 1);
        } else {
            shadow_band((*job).sun_share, 0, H - 1);
        }
        return;
    }
    if (pool_ == nullptr) {
        pool_ = new Pool();
        const unsigned cores = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
        for (unsigned k = 1; k < cores; k += 1) {
            (*pool_).threads.push_back(std::thread(work_bands, pool_));
        }
    }
    Pool& pool = *pool_;
    {
        std::lock_guard<std::mutex> guard(pool.lock);
        pool.job = job;
        pool.rows = H;
        pool.bands = std::max(1, std::min(H, static_cast<int>(pool.threads.size() + 1) * 6));
        pool.next.store(0);
        pool.busy = static_cast<int>(pool.threads.size());
        pool.generation += 1;
    }
    pool.wake.notify_all();
    take_bands(pool, *job);
    std::unique_lock<std::mutex> wait(pool.lock);
    while (pool.busy > 0) {
        pool.finished.wait(wait);
    }
}

static void take_bands(R3D::Pool& pool, const R3D::BandJob& job) {
    while (true) {
        const int band = pool.next.fetch_add(1);
        if (band >= pool.bands) {
            return;
        }
        const int y0 = band * pool.rows / pool.bands;
        const int y1 = (band + 1) * pool.rows / pool.bands - 1;
        if (job.kind == 0) {
            job.self->raster_band_public(job, y0, y1);
        } else {
            job.self->shadow_band_public(job.sun_share, y0, y1);
        }
    }
}

namespace {

void work_bands(R3D::Pool* pool) {
    int seen = 0;
    while (true) {
        const R3D::BandJob* job = nullptr;
        {
            std::unique_lock<std::mutex> wait((*pool).lock);
            while (!(*pool).quit && (*pool).generation == seen) {
                (*pool).wake.wait(wait);
            }
            if ((*pool).quit) {
                return;
            }
            seen = (*pool).generation;
            job = (*pool).job;
        }
        take_bands(*pool, *job);
        {
            std::lock_guard<std::mutex> guard((*pool).lock);
            (*pool).busy -= 1;
        }
        (*pool).finished.notify_one();
    }
}

}  // namespace

R3D::~R3D() {
    if (pool_ != nullptr) {
        {
            std::lock_guard<std::mutex> guard((*pool_).lock);
            (*pool_).quit = true;
        }
        (*pool_).wake.notify_all();
        for (size_t k = 0; k < (*pool_).threads.size(); k += 1) {
            (*pool_).threads[k].join();
        }
        delete pool_;
    }
}

void R3D::draw(const Vtx* verts, size_t count, const Tex* tex0, std::uint16_t mat, const M34* model, const Tex* tex2_0, const Tex* splat) {
    // Two stages: every triangle is transformed, lit, culled and queued here;
    // then the queue is filled in horizontal bands, one per core, which never
    // share a pixel, so the picture is the same as drawing them one by one.
    std::vector<Prepared>& queue = prepared_;
    queue.clear();
    M34 nm;
    if (model) nm = model->normal_matrix();
    const bool shadow_px = shadows_on_ && !(mat & unlit) && !(mat & toon);
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
            Col c = (mat & toon) ? v.c : shade_vertex(v, mat);
            // the highlight: Blinn's half vector between the sun and the eye
            float spec = 0;
            if ((mat & gloss) && !(mat & unlit)) {
                const V3 n = norm(v.n);
                const V3 half = norm(light.sun - F_);
                const double nh = std::max(0.0, dot(n, half));
                const double lit = dot(n, light.sun) > 0 ? 1.0 : 0.0;
                spec = static_cast<float>(lit * light.gloss_strength * std::pow(nh, light.gloss_power));
            }
            sv[k].sr = sv[k].sg = sv[k].sb = 0; sv[k].u = sv[k].v = sv[k].h = 0;
            if (shadow_px) {
                // ambient here, the sun apart, so a shadow can take the sun alone
                const Col amb = ambient_for(light, v.n);
                const float kk = static_cast<float>(std::max(0.0, dot(norm(v.n), light.sun)));
                sv[k].sr = (v.c.r * kk + spec) * light.sun_col.r; sv[k].sg = (v.c.g * kk + spec) * light.sun_col.g; sv[k].sb = (v.c.b * kk + spec) * light.sun_col.b;
                c = {v.c.r * amb.r, v.c.g * amb.g, v.c.b * amb.b, v.c.a};
                shadow_coords(v.p, sv[k].u, sv[k].v, sv[k].h);
            } else if (spec > 0) {
                c = {c.r + spec * light.sun_col.r, c.g + spec * light.sun_col.g, c.b + spec * light.sun_col.b, c.a};
            }
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
        // a triangle reaching behind the eye would project inside out across the
        // whole screen; such near things are dropped rather than clipped
        if (persp > 0 && (persp + sv[0].z < .01 || persp + sv[1].z < .01 || persp + sv[2].z < .01)) continue;
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
        Prepared job;
        job.sv[0] = sv[0];
        job.sv[1] = sv[1];
        job.sv[2] = sv[2];
        job.y0 = y0;
        job.y1 = y1;
        queue.push_back(job);
    }
    if (queue.empty()) return;
    BandJob band;
    band.self = this;
    band.tex0 = tex0;
    band.tex2_0 = tex2_0;
    band.splat = splat;
    band.mat = mat;
    band.shadow_px = shadow_px;
    // a few small triangles aren't worth waking the other cores for
    size_t rows = 0;
    for (size_t i = 0; i < queue.size(); i += 1) {
        rows += static_cast<size_t>(queue[i].y1 - queue[i].y0 + 1);
    }
    run_bands(&band, rows > 600 ? 0 : 1);
}

void R3D::raster_band(const BandJob& job, int band0, int band1) {
    const Tex* tex0 = job.tex0;
    const Tex* tex2_0 = job.tex2_0;
    const Tex* splat = job.splat;
    const std::uint16_t mat = job.mat;
    const bool shadow_px = job.shadow_px;
    for (size_t q = 0; q < prepared_.size(); q += 1) {
        const Prepared& item = prepared_[q];
        if (item.y1 < band0 || item.y0 > band1) continue;
        const SV* sv = item.sv;
        const int y0 = std::max(item.y0, band0);
        const int y1 = std::min(item.y1, band1);
        const double area = (sv[1].x - sv[0].x) * (sv[2].y - sv[0].y) - (sv[2].x - sv[0].x) * (sv[1].y - sv[0].y);
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
        float srx = 0, sry = 0, sr0 = 0, sgx = 0, sgy = 0, sg0 = 0, sbx = 0, sby = 0, sb0 = 0;
        float ux = 0, uy = 0, u0 = 0, vx = 0, vy = 0, v0 = 0, hx = 0, hy = 0, h0 = 0;
        if (shadow_px) {
            plane(&SV::sr, srx, sry, sr0); plane(&SV::sg, sgx, sgy, sg0); plane(&SV::sb, sbx, sby, sb0);
            plane(&SV::u, ux, uy, u0); plane(&SV::v, vx, vy, v0); plane(&SV::h, hx, hy, h0);
        }
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
            float sr = sr0 + srx * px0 + sry * fpy, sg = sg0 + sgx * px0 + sgy * fpy, sb = sb0 + sbx * px0 + sby * fpy;
            float su = u0 + ux * px0 + uy * fpy, svv = v0 + vx * px0 + vy * fpy, sh = h0 + hx * px0 + hy * fpy;
            size_t pi = static_cast<size_t>(y) * W + xa;
            for (int x = xa; x <= xb; ++x, ++pi, z += zx, s += sx_, t += tx_, cr0 += rx, cg0 += gx_, cb0 += bx, ca0 += axx, f += fx, wv += wx, lv += lx,
                     sr += srx, sg += sgx, sb += sbx, su += ux, svv += vx, sh += hx) {
                if (z >= depth[pi]) continue;
                float cr = cr0, cg = cg0, cb = cb0, ca = ca0;
                if (shadow_px) {
                    const float lit = 1.f - shadow_darkness * (1.f - shadow_lit(su, svv, sh));
                    cr += sr * lit; cg += sg * lit; cb += sb * lit;
                }
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

}  // namespace zc
