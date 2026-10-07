#include "mesh.hpp"

#include <map>

namespace eggy {

namespace {
void tri(Mesh& m, const Vtx& a, const Vtx& b, const Vtx& c) { m.push_back(a); m.push_back(b); m.push_back(c); }
Vtx sv(double th, double ph, double s, double t) {  // th: around z, ph: from -pi/2..pi/2
    const V3 p{std::cos(ph) * std::cos(th), std::cos(ph) * std::sin(th), std::sin(ph)};
    return {p, p, s, t, {1, 1, 1, 1}};
}
Mesh build_sphere(int sl, int st, double ph0) {
    Mesh m;
    for (int j = 0; j < st; ++j) {
        const double p0 = ph0 + (M_PI / 2 - ph0) * j / st, p1 = ph0 + (M_PI / 2 - ph0) * (j + 1) / st;
        for (int i = 0; i < sl; ++i) {
            const double t0 = 2 * M_PI * i / sl, t1 = 2 * M_PI * (i + 1) / sl;
            const double s0 = static_cast<double>(i) / sl, s1 = static_cast<double>(i + 1) / sl;
            const double v0 = 1 - static_cast<double>(j) / st, v1 = 1 - static_cast<double>(j + 1) / st;
            const Vtx a = sv(t0, p0, s0, v0), b = sv(t1, p0, s1, v0), c = sv(t1, p1, s1, v1), d = sv(t0, p1, s0, v1);
            tri(m, a, b, c);
            tri(m, a, c, d);
        }
    }
    return m;
}
}  // namespace

const Mesh& sphere_mesh(int sl, int st) {
    static std::map<std::pair<int, int>, Mesh> cache;
    auto& m = cache[{sl, st}];
    if (m.empty()) m = build_sphere(sl, st, -M_PI / 2);
    return m;
}
const Mesh& hemisphere_mesh(int sl, int st) {
    static std::map<std::pair<int, int>, Mesh> cache;
    auto& m = cache[{sl, st}];
    if (m.empty()) m = build_sphere(sl, st, 0);
    return m;
}
const Mesh& cylinder_mesh(int sl) {
    static std::map<int, Mesh> cache;
    auto& m = cache[sl];
    if (m.empty())
        for (int i = 0; i < sl; ++i) {
            const double t0 = 2 * M_PI * i / sl, t1 = 2 * M_PI * (i + 1) / sl;
            const V3 n0{std::cos(t0), std::sin(t0), 0}, n1{std::cos(t1), std::sin(t1), 0};
            const double s0 = static_cast<double>(i) / sl, s1 = static_cast<double>(i + 1) / sl;
            const Vtx a{n0, n0, s0, 1}, b{n1, n1, s1, 1}, c{n1 + V3{0, 0, 1}, n1, s1, 0}, d{n0 + V3{0, 0, 1}, n0, s0, 0};
            tri(m, a, b, c);
            tri(m, a, c, d);
        }
    return m;
}
const Mesh& disc_mesh(int sl) {
    static std::map<int, Mesh> cache;
    auto& m = cache[sl];
    if (m.empty())
        for (int i = 0; i < sl; ++i) {
            const double t0 = 2 * M_PI * i / sl, t1 = 2 * M_PI * (i + 1) / sl;
            const V3 n{0, 0, 1};
            tri(m, {{0, 0, 0}, n, .5, .5}, {{std::cos(t0), std::sin(t0), 0}, n, .5 + .5 * std::cos(t0), .5 + .5 * std::sin(t0)},
                {{std::cos(t1), std::sin(t1), 0}, n, .5 + .5 * std::cos(t1), .5 + .5 * std::sin(t1)});
        }
    return m;
}
const Mesh& cone_mesh(int sl) {
    static std::map<int, Mesh> cache;
    auto& m = cache[sl];
    if (m.empty())
        for (int i = 0; i < sl; ++i) {
            const double t0 = 2 * M_PI * i / sl, t1 = 2 * M_PI * (i + 1) / sl, tm = (t0 + t1) / 2;
            const V3 n0 = norm({std::cos(t0), std::sin(t0), .7}), n1 = norm({std::cos(t1), std::sin(t1), .7}), nm = norm({std::cos(tm), std::sin(tm), .7});
            tri(m, {{std::cos(t0), std::sin(t0), 0}, n0, static_cast<double>(i) / sl, 1},
                {{std::cos(t1), std::sin(t1), 0}, n1, static_cast<double>(i + 1) / sl, 1}, {{0, 0, 1}, nm, (i + .5) / sl, 0});
        }
    return m;
}
const Mesh& box_mesh() {
    static Mesh m;
    if (m.empty()) {
        auto face = [&](V3 n, V3 a, V3 b, V3 c, V3 d) {
            tri(m, {a, n, 0, 1}, {b, n, 1, 1}, {c, n, 1, 0});
            tri(m, {a, n, 0, 1}, {c, n, 1, 0}, {d, n, 0, 0});
        };
        face({0, 0, 1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1});
        face({0, -1, 0}, {-1, -1, 0}, {1, -1, 0}, {1, -1, 1}, {-1, -1, 1});
        face({1, 0, 0}, {1, -1, 0}, {1, 1, 0}, {1, 1, 1}, {1, -1, 1});
        face({0, 1, 0}, {1, 1, 0}, {-1, 1, 0}, {-1, 1, 1}, {1, 1, 1});
        face({-1, 0, 0}, {-1, 1, 0}, {-1, -1, 0}, {-1, -1, 1}, {-1, 1, 1});
    }
    return m;
}
Mesh rock_mesh(std::uint64_t seed, int sl, int st, double jitter) {
    Mesh m = build_sphere(sl, st, -M_PI / 2);
    auto bump = [&](V3 p) {
        const int ix = static_cast<int>(std::lround(p.x * 8)), iy = static_cast<int>(std::lround(p.y * 8)), iz = static_cast<int>(std::lround(p.z * 8));
        std::uint64_t h = seed ^ (static_cast<std::uint64_t>(ix + 99) * 73856093ULL) ^ (static_cast<std::uint64_t>(iy + 99) * 19349663ULL) ^ (static_cast<std::uint64_t>(iz + 99) * 83492791ULL);
        h ^= h >> 29; h *= 0xbf58476d1ce4e5b9ULL; h ^= h >> 32;
        return 1 + jitter * (static_cast<double>(h & 0xffff) / 65535.0 - .5);
    };
    for (Vtx& v : m) v.p = v.p * bump(v.p);
    for (size_t i = 0; i + 2 < m.size(); i += 3) {  // faceted: flat normals per triangle
        const V3 n = norm(cross(m[i + 1].p - m[i].p, m[i + 2].p - m[i].p));
        m[i].n = m[i + 1].n = m[i + 2].n = n;
    }
    return m;
}
void tint(Mesh& m, Col c) { for (Vtx& v : m) v.c = c; }

void draw_mesh(R3D& r, const Mesh& m, const M34& model, const Tex* tex, Col tc, std::uint8_t mat, double ts) {
    thread_local Mesh tmp;
    tmp.assign(m.begin(), m.end());
    for (Vtx& v : tmp) {
        v.c = {v.c.r * tc.r, v.c.g * tc.g, v.c.b * tc.b, v.c.a * tc.a};
        v.s *= ts; v.t *= ts;
    }
    r.draw(tmp.data(), tmp.size(), tex, mat, &model);
}

}  // namespace eggy
