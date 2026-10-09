#include "r3d_mesh.hpp"

#include <map>
#include <tuple>

namespace render::r3d {

namespace {
void tri(Mesh& m, const Vtx& a, const Vtx& b, const Vtx& c) { m.push_back(a); m.push_back(b); m.push_back(c); }
Vtx sv(double th, double ph, double s, double t) {  // th: around z, ph: from -pi/2..pi/2
    const V3 p{std::cos(ph) * std::cos(th), std::cos(ph) * std::sin(th), std::sin(ph)};
    return {p, p, s, t, {1, 1, 1, 1}};
}
// One side of the box: two triangles.
void face(Mesh& m, V3 n, V3 a, V3 b, V3 c, V3 d) {
    tri(m, {a, n, 0, 1}, {b, n, 1, 1}, {c, n, 1, 0});
    tri(m, {a, n, 0, 1}, {c, n, 1, 0}, {d, n, 0, 0});
}
// A rock's radius at a direction: hashed from the seed, so the lumps are fixed.
double bump(std::uint64_t seed, double jitter, V3 p) {
    const int ix = static_cast<int>(std::lround(p.x * 8)), iy = static_cast<int>(std::lround(p.y * 8)), iz = static_cast<int>(std::lround(p.z * 8));
    std::uint64_t h = seed ^ (static_cast<std::uint64_t>(ix + 99) * 73856093ULL) ^ (static_cast<std::uint64_t>(iy + 99) * 19349663ULL) ^ (static_cast<std::uint64_t>(iz + 99) * 83492791ULL);
    h ^= h >> 29; h *= 0xbf58476d1ce4e5b9ULL; h ^= h >> 32;
    return 1 + jitter * (static_cast<double>(h & 0xffff) / 65535.0 - .5);
}
// A point on the torus: i of `ma` round the ring, j of `mi` round the tube.
Vtx torus_point(int ma, int mi, double rr, int i, int j) {
    const double u = 2 * M_PI * i / ma, v = 2 * M_PI * j / mi;
    const V3 c{std::cos(u), std::sin(u), 0};
    const V3 n = norm(c * std::cos(v) + V3{0, 0, 1} * std::sin(v));
    return Vtx{c + n * rr, n, static_cast<double>(i) / ma, static_cast<double>(j) / mi};
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
    Mesh& m = cache[{sl, st}];
    if (m.empty()) m = build_sphere(sl, st, -M_PI / 2);
    return m;
}
const Mesh& hemisphere_mesh(int sl, int st) {
    static std::map<std::pair<int, int>, Mesh> cache;
    Mesh& m = cache[{sl, st}];
    if (m.empty()) m = build_sphere(sl, st, 0);
    return m;
}
const Mesh& cylinder_mesh(int sl) {
    static std::map<int, Mesh> cache;
    Mesh& m = cache[sl];
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
    Mesh& m = cache[sl];
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
    Mesh& m = cache[sl];
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
        face(m, {0, 0, 1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1});
        face(m, {0, -1, 0}, {-1, -1, 0}, {1, -1, 0}, {1, -1, 1}, {-1, -1, 1});
        face(m, {1, 0, 0}, {1, -1, 0}, {1, 1, 0}, {1, 1, 1}, {1, -1, 1});
        face(m, {0, 1, 0}, {1, 1, 0}, {-1, 1, 0}, {-1, 1, 1}, {1, 1, 1});
        face(m, {-1, 0, 0}, {-1, 1, 0}, {-1, -1, 0}, {-1, -1, 1}, {-1, 1, 1});
    }
    return m;
}
Mesh rock_mesh(std::uint64_t seed, int sl, int st, double jitter) {
    Mesh m = build_sphere(sl, st, -M_PI / 2);
    for (Vtx& v : m) v.p = v.p * bump(seed, jitter, v.p);
    for (size_t i = 0; i + 2 < m.size(); i += 3) {  // faceted: flat normals per triangle
        const V3 n = norm(cross(m[i + 1].p - m[i].p, m[i + 2].p - m[i].p));
        m[i].n = m[i + 1].n = m[i + 2].n = n;
    }
    return m;
}
const Mesh& torus_mesh(int ma, int mi, double rr) {
    static std::map<std::tuple<int, int, int>, Mesh> cache;
    Mesh& m = cache[{ma, mi, static_cast<int>(rr * 1000)}];
    if (m.empty()) {
        for (int i = 0; i < ma; ++i)
            for (int j = 0; j < mi; ++j) {
                const Vtx a = torus_point(ma, mi, rr, i, j), b = torus_point(ma, mi, rr, i + 1, j);
                const Vtx c = torus_point(ma, mi, rr, i + 1, j + 1), d = torus_point(ma, mi, rr, i, j + 1);
                tri(m, a, b, c);
                tri(m, a, c, d);
            }
    }
    return m;
}
const Mesh& star_mesh(double inner, double depth) {
    static std::map<std::pair<int, int>, Mesh> cache;
    Mesh& m = cache[{static_cast<int>(inner * 1000), static_cast<int>(depth * 1000)}];
    if (m.empty()) {
        V3 rim[10];
        for (int i = 0; i < 10; ++i) {
            const double a = M_PI / 2 + i * M_PI / 5, r = i % 2 ? inner : 1;
            rim[i] = {std::cos(a) * r, 0, std::sin(a) * r};
        }
        const V3 f{0, -depth, 0}, b{0, depth * .4, 0};
        for (int i = 0; i < 10; ++i) {
            const V3 p = rim[i], q = rim[(i + 1) % 10];
            // front: a shallow pyramid so the facets catch light
            const V3 nf = norm(cross(q - f, p - f));
            tri(m, {f, nf, .5, .5}, {q, nf, .5 + q.x * .5, .5 - q.z * .5}, {p, nf, .5 + p.x * .5, .5 - p.z * .5});
            const V3 nb = norm(cross(p - b, q - b));
            tri(m, {b, nb, .5, .5}, {p, nb, .5, .5}, {q, nb, .5, .5});
        }
    }
    return m;
}

void draw_outline(Renderer& r, const Mesh& m, const M34& model, double width, r2d::Col c) {
    thread_local Mesh tmp;
    tmp.resize(m.size());
    const M34 nm = model.normal_matrix();
    for (size_t i = 0; i < m.size(); ++i) {
        const V3 n = norm(nm.dir(m[i].n));
        tmp[i].p = model.apply(m[i].p) + n * width;
        tmp[i].n = n;
        tmp[i].c = c;
        tmp[i].s = tmp[i].t = 0;
    }
    r.draw(tmp.data(), tmp.size(), nullptr, static_cast<std::uint16_t>(inverted | unlit | no_fog));
}

void tint(Mesh& m, r2d::Col c) { for (Vtx& v : m) v.c = c; }

void draw_mesh(Renderer& r, const Mesh& m, const M34& model, const Tex* tex, r2d::Col tc, std::uint16_t mat, double ts) {
    thread_local Mesh tmp;
    tmp.assign(m.begin(), m.end());
    for (Vtx& v : tmp) {
        v.c = {v.c.r * tc.r, v.c.g * tc.g, v.c.b * tc.b, v.c.a * tc.a};
        v.s *= ts; v.t *= ts;
    }
    r.draw(tmp.data(), tmp.size(), tex, mat, &model);
}

}  // namespace render::r3d
