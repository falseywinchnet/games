#include "model3d.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace mm {
namespace {

constexpr double pi = 3.14159265358979323846;

// The garden's sun: shadows fall 0.22 m right and 0.30 m down the lawn per metre of height.
const V3 sun_dir = normalized({-0.22, -0.30, 1.0});

double clamp01(double x) {
    return std::clamp(x, 0.0, 1.0);
}
double smooth(double a, double b, double x) {
    const double t = clamp01((x - a) / (b - a));
    return t * t * (3 - 2 * t);
}
double fract(double x) {
    return x - std::floor(x);
}

struct Lin {
    double r{};
    double g{};
    double b{};
};
Lin operator+(Lin a, Lin b) {
    return {a.r + b.r, a.g + b.g, a.b + b.b};
}
Lin operator*(Lin a, double k) {
    return {a.r * k, a.g * k, a.b * k};
}
Lin operator*(Lin a, Lin b) {
    return {a.r * b.r, a.g * b.g, a.b * b.b};
}
Lin mixed(Lin a, Lin b, double t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
double to_linear(float c) {
    return std::pow(std::max(0.0F, c), 2.2);
}
Lin linear(Col c) {
    return {to_linear(c.r), to_linear(c.g), to_linear(c.b)};
}

// Linear light to an sRGB byte, through a filmic shoulder like the lawn's.
struct ToneTable {
    std::array<std::uint8_t, 4097> byte{};
    ToneTable() {
        for (int k = 0; k <= 4096; ++k) {
            const double v = k / 4096.0 * 4.0 * 0.62;
            const double aces = clamp01((v * (2.51 * v + 0.03)) / (v * (2.43 * v + 0.59) + 0.14));
            byte[static_cast<std::size_t>(k)] = static_cast<std::uint8_t>(std::lround(255 * std::pow(aces, 1 / 2.2)));
        }
    }
};
const ToneTable& tone_table() {
    static const ToneTable table{};
    return table;
}
double tone(double v) {
    const int k = static_cast<int>(std::clamp(v / 4.0, 0.0, 1.0) * 4096 + 0.5);
    return tone_table().byte[static_cast<std::size_t>(k)] / 255.0;
}

std::uint32_t lattice(int x, int y, int z) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393U + static_cast<std::uint32_t>(y) * 668265263U + static_cast<std::uint32_t>(z) * 2147483647U;
    h = (h ^ (h >> 13U)) * 1274126177U;
    return h ^ (h >> 16U);
}
double lattice01(int x, int y, int z) {
    return lattice(x, y, z) / 4294967295.0;
}

} // namespace

double hash01(std::uint64_t& state) {
    state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = state;
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return static_cast<double>((z ^ (z >> 31U)) >> 11U) / 9007199254740992.0;
}

double noise3(V3 p) {
    const double fx = std::floor(p.x);
    const double fy = std::floor(p.y);
    const double fz = std::floor(p.z);
    const int ix = static_cast<int>(fx);
    const int iy = static_cast<int>(fy);
    const int iz = static_cast<int>(fz);
    double u = p.x - fx;
    double v = p.y - fy;
    double w = p.z - fz;
    u = u * u * (3 - 2 * u);
    v = v * v * (3 - 2 * v);
    w = w * w * (3 - 2 * w);
    double c[2][2];
    for (int dz = 0; dz < 2; ++dz)
        for (int dy = 0; dy < 2; ++dy) {
            const double a = lattice01(ix, iy + dy, iz + dz);
            const double b = lattice01(ix + 1, iy + dy, iz + dz);
            c[dz][dy] = a + (b - a) * u;
        }
    const double near = c[0][0] + (c[0][1] - c[0][0]) * v;
    const double far = c[1][0] + (c[1][1] - c[1][0]) * v;
    return near + (far - near) * w;
}

double fbm3(V3 p, int octaves) {
    double sum = 0;
    double weight = 0.5;
    double total = 0;
    for (int k = 0; k < octaves; ++k) {
        sum += noise3(p) * weight;
        total += weight;
        p = V3{p.x * 2.03 + 17.1, p.y * 2.03 - 3.7, p.z * 2.03 + 9.2};
        weight *= 0.5;
    }
    return sum / total;
}

V3 Xform::normal(V3 n) const {
    // cofactor matrix of the 3x3 part
    const double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const double c00 = e * i - f * h, c01 = -(d * i - f * g), c02 = d * h - e * g;
    const double c10 = -(b * i - c * h), c11 = a * i - c * g, c12 = -(a * h - b * g);
    const double c20 = b * f - c * e, c21 = -(a * f - c * d), c22 = a * e - b * d;
    const double det = a * c00 + b * c01 + c * c02;
    const double s = det < 0 ? -1.0 : 1.0;
    return normalized(V3{c00 * n.x + c01 * n.y + c02 * n.z, c10 * n.x + c11 * n.y + c12 * n.z, c20 * n.x + c21 * n.y + c22 * n.z} * s);
}

Xform Xform::operator*(const Xform& o) const {
    Xform r{};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col)
            r.m[row * 4 + col] = m[row * 4] * o.m[col] + m[row * 4 + 1] * o.m[4 + col] + m[row * 4 + 2] * o.m[8 + col];
        r.m[row * 4 + 3] = m[row * 4] * o.m[3] + m[row * 4 + 1] * o.m[7] + m[row * 4 + 2] * o.m[11] + m[row * 4 + 3];
    }
    return r;
}

Xform translation(V3 t) {
    Xform r{};
    r.m[3] = t.x;
    r.m[7] = t.y;
    r.m[11] = t.z;
    return r;
}
Xform scaling(double sx, double sy, double sz) {
    Xform r{};
    r.m[0] = sx;
    r.m[5] = sy;
    r.m[10] = sz;
    return r;
}
Xform rotation_x(double a) {
    Xform r{};
    r.m[5] = std::cos(a);
    r.m[6] = -std::sin(a);
    r.m[9] = std::sin(a);
    r.m[10] = std::cos(a);
    return r;
}
Xform rotation_y(double a) {
    Xform r{};
    r.m[0] = std::cos(a);
    r.m[2] = std::sin(a);
    r.m[8] = -std::sin(a);
    r.m[10] = std::cos(a);
    return r;
}
Xform rotation_z(double a) {
    Xform r{};
    r.m[0] = std::cos(a);
    r.m[1] = -std::sin(a);
    r.m[4] = std::sin(a);
    r.m[5] = std::cos(a);
    return r;
}
Xform aimed(V3 origin, V3 axis) {
    const V3 x = normalized(axis);
    V3 up{0, 0, 1};
    if (std::abs(dot(up, x)) > 0.98)
        up = V3{0, 1, 0};
    const V3 y = normalized(cross(up, x));
    const V3 z = cross(x, y);
    Xform r{};
    r.m[0] = x.x;
    r.m[1] = y.x;
    r.m[2] = z.x;
    r.m[3] = origin.x;
    r.m[4] = x.y;
    r.m[5] = y.y;
    r.m[6] = z.y;
    r.m[7] = origin.y;
    r.m[8] = x.z;
    r.m[9] = y.z;
    r.m[10] = z.z;
    r.m[11] = origin.z;
    return r;
}

void Mesh::append(const Mesh& other, const Xform& place) {
    const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
    for (const Vertex& v : other.vertices) {
        Vertex w = v;
        w.p = place.point(v.p);
        w.o = place.point(v.o);
        w.n = place.normal(v.n);
        vertices.push_back(w);
    }
    for (std::uint32_t index : other.indices)
        indices.push_back(base + index);
    materials.insert(materials.end(), other.materials.begin(), other.materials.end());
}

// ---------------------------------------------------------------------------------------------
// Building

Builder::Builder(Mesh& mesh, double ppm) : mesh_(mesh), ppm_(ppm) {}

int Builder::sides(double radius, int least, int most) const {
    const double scale = at.vector(V3{1, 0, 0}).x * at.vector(V3{1, 0, 0}).x + at.vector(V3{1, 0, 0}).y * at.vector(V3{1, 0, 0}).y +
                         at.vector(V3{1, 0, 0}).z * at.vector(V3{1, 0, 0}).z;
    const double pixels = 2 * pi * radius * std::sqrt(scale) * ppm_;
    // About four pixels a facet; small things may drop below their usual least sides.
    const int n = static_cast<int>(std::ceil(pixels / 4.0));
    const int floor_sides = std::min(least, std::max(4, n));
    return std::clamp((n + 1) / 2 * 2, floor_sides, most);
}

void Builder::emit(const std::vector<Vertex>& local, int rows, int cols, bool ring, bool flip) {
    const std::uint32_t base = static_cast<std::uint32_t>(mesh_.vertices.size());
    for (const Vertex& v : local) {
        Vertex w = v;
        w.p = at.point(v.p);
        w.o = w.p;
        w.n = at.normal(flip ? v.n * -1 : v.n);
        w.blend = blend;
        mesh_.vertices.push_back(w);
    }
    const int span = ring ? cols : cols - 1;
    for (int r = 0; r + 1 < rows; ++r)
        for (int c = 0; c < span; ++c) {
            const int c1 = (c + 1) % cols;
            const std::uint32_t a = base + static_cast<std::uint32_t>(r * cols + c);
            const std::uint32_t b = base + static_cast<std::uint32_t>(r * cols + c1);
            const std::uint32_t d = base + static_cast<std::uint32_t>((r + 1) * cols + c);
            const std::uint32_t e = base + static_cast<std::uint32_t>((r + 1) * cols + c1);
            mesh_.indices.insert(mesh_.indices.end(), {a, b, e, a, e, d});
            mesh_.materials.push_back(material);
            mesh_.materials.push_back(material);
        }
}

void Builder::grid(const std::vector<V3>& points, int rows, int cols, bool ring, bool flip) {
    std::vector<Vertex> local(points.size());
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            const std::size_t at_index = static_cast<std::size_t>(r * cols + c);
            const int cl = ring ? (c + cols - 1) % cols : std::max(0, c - 1);
            const int cr = ring ? (c + 1) % cols : std::min(cols - 1, c + 1);
            const int rd = std::max(0, r - 1);
            const int ru = std::min(rows - 1, r + 1);
            const V3 du = points[static_cast<std::size_t>(r * cols + cr)] - points[static_cast<std::size_t>(r * cols + cl)];
            const V3 dv = points[static_cast<std::size_t>(ru * cols + c)] - points[static_cast<std::size_t>(rd * cols + c)];
            V3 n = cross(du, dv);
            Vertex& v = local[at_index];
            v.p = points[at_index];
            v.n = n;
            v.u = static_cast<float>(c) / static_cast<float>(ring ? cols : std::max(1, cols - 1));
            v.v = static_cast<float>(r) / static_cast<float>(std::max(1, rows - 1));
        }
    // Where a row has shrunk to a point (a pole), borrow the normal of the row beside it.
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            Vertex& v = local[static_cast<std::size_t>(r * cols + c)];
            if (length(v.n) > 1e-12) {
                v.n = normalized(v.n);
                continue;
            }
            const int other = r == 0 ? std::min(rows - 1, 1) : r - 1;
            V3 sum{};
            for (int k = 0; k < cols; ++k) {
                const Vertex& w = local[static_cast<std::size_t>(other * cols + k)];
                sum = sum + w.n;
            }
            V3 pole{0, 0, r == 0 ? -1.0 : 1.0};
            const V3 axis = points[static_cast<std::size_t>(other * cols)] - points[static_cast<std::size_t>(r * cols)];
            if (length(sum) > 1e-9)
                pole = normalized(sum);
            else if (length(axis) > 1e-12)
                pole = normalized(axis * -1);
            v.n = pole;
        }
    emit(local, rows, cols, ring, flip);
}

void Builder::lathe(const std::vector<double>& radius, const std::vector<double>& height, int segments, double a0, double a1) {
    double widest = 0;
    for (double r : radius)
        widest = std::max(widest, r);
    const bool ring = std::abs(a1 - a0 - 2 * pi) < 1e-6;
    const int around = segments > 0 ? segments : sides(widest, 8, 96);
    const int cols = ring ? around : around + 1;
    const int rows = static_cast<int>(radius.size());
    std::vector<V3> points{};
    points.reserve(static_cast<std::size_t>(rows * cols));
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            const double a = a0 + (a1 - a0) * c / around;
            points.push_back(V3{std::cos(a) * radius[static_cast<std::size_t>(r)], std::sin(a) * radius[static_cast<std::size_t>(r)], height[static_cast<std::size_t>(r)]});
        }
    grid(points, rows, cols, ring);
}

void Builder::ellipsoid(V3 centre, V3 radii, int segments) {
    const int around = segments > 0 ? segments : sides(std::max(radii.x, radii.y), 8, 72);
    const int rows = std::max(5, around / 2 + 1);
    std::vector<Vertex> local{};
    local.reserve(static_cast<std::size_t>(rows * around));
    for (int r = 0; r < rows; ++r) {
        const double lat = -pi / 2 + pi * r / (rows - 1);
        for (int c = 0; c < around; ++c) {
            const double a = 2 * pi * c / around;
            const V3 unit{std::cos(lat) * std::cos(a), std::cos(lat) * std::sin(a), std::sin(lat)};
            Vertex v{};
            v.p = centre + V3{unit.x * radii.x, unit.y * radii.y, unit.z * radii.z};
            v.n = normalized(V3{unit.x / radii.x, unit.y / radii.y, unit.z / radii.z});
            v.u = static_cast<float>(c) / static_cast<float>(around);
            v.v = static_cast<float>(r) / static_cast<float>(rows - 1);
            local.push_back(v);
        }
    }
    emit(local, rows, around, true, false);
}

void Builder::cylinder(V3 a, V3 b, double ra, double rb, bool caps, int segments) {
    const Xform keep = at;
    at = at * aimed(a, b - a);
    const double len = length(b - a);
    const int around = segments > 0 ? segments : sides(std::max(ra, rb), 6, 64);
    // Along x: rings at both ends, with a narrow chamfer ring so the caps' edges catch the light.
    std::vector<Vertex> local{};
    const double slope = (ra - rb) / std::max(1e-9, len);
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < around; ++c) {
            const double t = 2 * pi * c / around;
            const double rad = r == 0 ? ra : rb;
            Vertex v{};
            v.p = V3{r == 0 ? 0.0 : len, std::cos(t) * rad, std::sin(t) * rad};
            v.n = normalized(V3{slope, std::cos(t), std::sin(t)});
            v.u = static_cast<float>(c) / static_cast<float>(around);
            v.v = static_cast<float>(r);
            local.push_back(v);
        }
    emit(local, 2, around, true, false);
    if (caps) {
        for (int end = 0; end < 2; ++end) {
            const double rad = end == 0 ? ra : rb;
            if (rad <= 0)
                continue;
            std::vector<Vertex> cap{};
            for (int r = 0; r < 2; ++r)
                for (int c = 0; c < around; ++c) {
                    const double t = 2 * pi * c / around;
                    Vertex v{};
                    v.p = V3{end == 0 ? 0.0 : len, r == 0 ? 0.0 : std::cos(t) * rad, r == 0 ? 0.0 : std::sin(t) * rad};
                    v.n = V3{end == 0 ? -1.0 : 1.0, 0, 0};
                    v.u = static_cast<float>(0.5 + 0.5 * std::cos(t) * r);
                    v.v = static_cast<float>(0.5 + 0.5 * std::sin(t) * r);
                    cap.push_back(v);
                }
            emit(cap, 2, around, true, false);
        }
    }
    at = keep;
}

void Builder::tube(const std::vector<V3>& points, const std::vector<double>& radii, bool caps, int segments) {
    const int rows = static_cast<int>(points.size());
    if (rows < 2)
        return;
    double widest = 0;
    for (double r : radii)
        widest = std::max(widest, r);
    const int around = segments > 0 ? segments : sides(widest, 6, 48);
    // Parallel-transported frames along the path.
    std::vector<V3> tangents(points.size());
    for (int k = 0; k < rows; ++k) {
        const V3 ahead = points[static_cast<std::size_t>(std::min(rows - 1, k + 1))];
        const V3 behind = points[static_cast<std::size_t>(std::max(0, k - 1))];
        tangents[static_cast<std::size_t>(k)] = normalized(ahead - behind);
    }
    V3 normal = std::abs(tangents[0].z) < 0.9 ? normalized(cross(tangents[0], V3{0, 0, 1})) : normalized(cross(tangents[0], V3{1, 0, 0}));
    std::vector<Vertex> local{};
    double travelled = 0;
    for (int k = 0; k < rows; ++k) {
        const V3 t = tangents[static_cast<std::size_t>(k)];
        normal = normalized(normal - t * dot(normal, t));
        const V3 side = cross(t, normal);
        if (k > 0)
            travelled += length(points[static_cast<std::size_t>(k)] - points[static_cast<std::size_t>(k - 1)]);
        for (int c = 0; c < around; ++c) {
            const double a = 2 * pi * c / around;
            const V3 out = normal * std::cos(a) + side * std::sin(a);
            Vertex v{};
            v.p = points[static_cast<std::size_t>(k)] + out * radii[static_cast<std::size_t>(k)];
            v.n = out;
            v.u = static_cast<float>(c) / static_cast<float>(around);
            v.v = static_cast<float>(travelled);
            local.push_back(v);
        }
    }
    emit(local, rows, around, true, false);
    if (!caps)
        return;
    for (int end = 0; end < 2; ++end) {
        const int k = end == 0 ? 0 : rows - 1;
        const double rad = radii[static_cast<std::size_t>(k)];
        if (rad <= 0)
            continue;
        // A rounded end: a small dome so tube ends read as solid rods.
        const V3 t = tangents[static_cast<std::size_t>(k)] * (end == 0 ? -1.0 : 1.0);
        const V3 n0 = normalized(cross(t, std::abs(t.z) < 0.9 ? V3{0, 0, 1} : V3{1, 0, 0}));
        const V3 s0 = cross(t, n0);
        std::vector<Vertex> dome{};
        const int rings = 3;
        for (int r = 0; r <= rings; ++r) {
            const double lat = (pi / 2) * r / rings;
            for (int c = 0; c < around; ++c) {
                const double a = 2 * pi * c / around;
                const V3 out = (n0 * std::cos(a) + s0 * std::sin(a)) * std::cos(lat) + t * std::sin(lat);
                Vertex v{};
                v.p = points[static_cast<std::size_t>(k)] + out * rad;
                v.n = out;
                dome.push_back(v);
            }
        }
        emit(dome, rings + 1, around, true, false);
    }
}

void Builder::torus(V3 centre, double major, double minor, int segments, int rings) {
    const int around = segments > 0 ? segments : sides(major + minor, 8, 96);
    const int tube_sides = rings > 0 ? rings : sides(minor, 6, 32);
    std::vector<Vertex> local{};
    for (int r = 0; r < tube_sides; ++r) {
        const double b = 2 * pi * r / tube_sides;
        for (int c = 0; c < around; ++c) {
            const double a = 2 * pi * c / around;
            const V3 out{std::cos(a) * std::cos(b), std::sin(a) * std::cos(b), std::sin(b)};
            Vertex v{};
            v.p = centre + V3{std::cos(a) * (major + minor * std::cos(b)), std::sin(a) * (major + minor * std::cos(b)), minor * std::sin(b)};
            v.n = out;
            v.u = static_cast<float>(c) / static_cast<float>(around);
            v.v = static_cast<float>(r) / static_cast<float>(tube_sides);
            local.push_back(v);
        }
    }
    // close the tube's own ring by repeating the first row
    for (int c = 0; c < around; ++c) {
        Vertex v = local[static_cast<std::size_t>(c)];
        v.v = 1;
        local.push_back(v);
    }
    emit(local, tube_sides + 1, around, true, false);
}

void Builder::rounded_box(V3 centre, V3 half, double r) {
    r = std::min({r, half.x, half.y, half.z});
    const int bend = std::clamp(sides(r, 4, 16) / 4, 1, 4);
    const double inner[3] = {half.x - r, half.y - r, half.z - r};
    // Coordinates along each axis: the bend at each end and the flat between.
    std::vector<double> axis[3];
    for (int k = 0; k < 3; ++k) {
        for (int j = 0; j <= bend; ++j)
            axis[k].push_back(-inner[k] - r * std::cos(pi / 2 * j / bend));
        const int flats = std::max(1, static_cast<int>(std::ceil(inner[k] * 2 * ppm_ / 70)));
        for (int j = 1; j < flats; ++j)
            axis[k].push_back(-inner[k] + 2 * inner[k] * j / flats);
        for (int j = 0; j <= bend; ++j)
            axis[k].push_back(inner[k] + r * std::sin(pi / 2 * j / bend));
    }
    for (int face = 0; face < 6; ++face) {
        const int n_axis = face / 2;
        const double sign = face % 2 == 0 ? 1.0 : -1.0;
        const int ua = (n_axis + 1) % 3;
        const int va = (n_axis + 2) % 3;
        const int cols = static_cast<int>(axis[ua].size());
        const int rows = static_cast<int>(axis[va].size());
        std::vector<Vertex> local{};
        for (int rr = 0; rr < rows; ++rr)
            for (int cc = 0; cc < cols; ++cc) {
                double q[3];
                q[n_axis] = sign * (inner[n_axis] + r);
                q[ua] = axis[ua][static_cast<std::size_t>(cc)];
                q[va] = axis[va][static_cast<std::size_t>(rr)];
                double c[3];
                for (int k = 0; k < 3; ++k)
                    c[k] = std::clamp(q[k], -inner[k], inner[k]);
                const V3 d = normalized(V3{q[0] - c[0], q[1] - c[1], q[2] - c[2]});
                Vertex v{};
                v.p = centre + V3{c[0], c[1], c[2]} + d * r;
                v.n = d;
                v.u = static_cast<float>(cc) / static_cast<float>(cols - 1);
                v.v = static_cast<float>(rr) / static_cast<float>(rows - 1);
                local.push_back(v);
            }
        emit(local, rows, cols, false, false);
    }
}

void Builder::loft(const std::vector<Section>& sections, int around, bool caps) {
    double widest = 0;
    for (const Section& s : sections)
        widest = std::max({widest, s.w, s.h});
    const int cols = around > 0 ? around : sides(widest, 12, 96);
    const int rows = static_cast<int>(sections.size());
    std::vector<V3> points{};
    for (const Section& s : sections)
        for (int c = 0; c < cols; ++c) {
            const double t = 2 * pi * c / cols;
            const double ct = std::cos(t);
            const double st = std::sin(t);
            const double e = 2.0 / s.power;
            const double y = s.w * std::copysign(std::pow(std::abs(ct), e), ct);
            const double z = s.h * std::copysign(std::pow(std::abs(st), e), st);
            points.push_back(V3{s.x, s.cy + y, s.cz + z});
        }
    grid(points, rows, cols, true);
    if (!caps)
        return;
    for (int end = 0; end < 2; ++end) {
        const Section& s = sections[end == 0 ? 0 : sections.size() - 1];
        if (s.w <= 1e-6 || s.h <= 1e-6)
            continue;
        std::vector<Vertex> cap{};
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < cols; ++c) {
                Vertex v{};
                v.p = r == 0 ? V3{s.x, s.cy, s.cz} : points[static_cast<std::size_t>((end == 0 ? 0 : rows - 1) * cols + c)];
                v.n = V3{end == 0 ? -1.0 : 1.0, 0, 0};
                cap.push_back(v);
            }
        emit(cap, 2, cols, true, false);
    }
}

void Builder::sweep(const std::vector<V3>& points, const std::vector<double>& w, const std::vector<double>& h, double power, V3 side, bool caps, int around) {
    const int rows = static_cast<int>(points.size());
    if (rows < 2)
        return;
    double widest = 0;
    for (std::size_t k = 0; k < w.size(); ++k)
        widest = std::max({widest, w[k], h[k]});
    const int cols = around > 0 ? around : sides(widest, 12, 64);
    std::vector<V3> grid_points{};
    std::vector<V3> centres{};
    std::vector<V3> tangents{};
    for (int k = 0; k < rows; ++k) {
        const V3 ahead = points[static_cast<std::size_t>(std::min(rows - 1, k + 1))];
        const V3 behind = points[static_cast<std::size_t>(std::max(0, k - 1))];
        const V3 t = normalized(ahead - behind);
        const V3 a = normalized(side - t * dot(side, t));
        const V3 b = cross(t, a);
        const double e = 2.0 / power;
        for (int c = 0; c < cols; ++c) {
            const double q = 2 * pi * c / cols;
            const double cq = std::cos(q);
            const double sq = std::sin(q);
            const double x = w[static_cast<std::size_t>(k)] * std::copysign(std::pow(std::abs(cq), e), cq);
            const double y = h[static_cast<std::size_t>(k)] * std::copysign(std::pow(std::abs(sq), e), sq);
            grid_points.push_back(points[static_cast<std::size_t>(k)] + a * x + b * y);
        }
        tangents.push_back(t);
    }
    grid(grid_points, rows, cols, true);
    if (!caps)
        return;
    for (int end = 0; end < 2; ++end) {
        const int k = end == 0 ? 0 : rows - 1;
        if (w[static_cast<std::size_t>(k)] <= 1e-6 || h[static_cast<std::size_t>(k)] <= 1e-6)
            continue;
        std::vector<Vertex> cap{};
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < cols; ++c) {
                Vertex v{};
                v.p = r == 0 ? points[static_cast<std::size_t>(k)] : grid_points[static_cast<std::size_t>(k * cols + c)];
                v.n = tangents[static_cast<std::size_t>(k)] * (end == 0 ? -1.0 : 1.0);
                cap.push_back(v);
            }
        emit(cap, 2, cols, true, false);
    }
}

void Builder::prism(const std::vector<double>& xs, const std::vector<double>& ys, double z0, double z1) {
    const int n = static_cast<int>(xs.size());
    double cx = 0;
    double cy = 0;
    for (int k = 0; k < n; ++k) {
        cx += xs[static_cast<std::size_t>(k)] / n;
        cy += ys[static_cast<std::size_t>(k)] / n;
    }
    for (int end = 0; end < 2; ++end) {
        std::vector<Vertex> cap{};
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < n; ++c) {
                Vertex v{};
                v.p = r == 0 ? V3{cx, cy, end == 0 ? z0 : z1} : V3{xs[static_cast<std::size_t>(c)], ys[static_cast<std::size_t>(c)], end == 0 ? z0 : z1};
                v.n = V3{0, 0, end == 0 ? -1.0 : 1.0};
                v.u = static_cast<float>(v.p.x);
                v.v = static_cast<float>(v.p.y);
                cap.push_back(v);
            }
        emit(cap, 2, n, true, false);
    }
    for (int k = 0; k < n; ++k) {
        const int j = (k + 1) % n;
        const V3 a{xs[static_cast<std::size_t>(k)], ys[static_cast<std::size_t>(k)], z0};
        const V3 b{xs[static_cast<std::size_t>(j)], ys[static_cast<std::size_t>(j)], z0};
        const V3 side = normalized(cross(b - a, V3{0, 0, 1}));
        std::vector<Vertex> wall(4);
        wall[0].p = a;
        wall[1].p = b;
        wall[2].p = V3{a.x, a.y, z1};
        wall[3].p = V3{b.x, b.y, z1};
        for (Vertex& v : wall)
            v.n = side;
        wall[1].u = 1;
        wall[3].u = 1;
        wall[2].v = 1;
        wall[3].v = 1;
        emit(wall, 2, 2, false, false);
    }
}

void Builder::quad(V3 a, V3 b, V3 c, V3 d) {
    const V3 n = normalized(cross(b - a, d - a));
    std::vector<Vertex> sheet(4);
    sheet[0].p = a;
    sheet[1].p = b;
    sheet[2].p = d;
    sheet[3].p = c;
    sheet[1].u = 1;
    sheet[3].u = 1;
    sheet[2].v = 1;
    sheet[3].v = 1;
    for (Vertex& v : sheet)
        v.n = n;
    emit(sheet, 2, 2, false, false);
}

// ---------------------------------------------------------------------------------------------
// Occlusion: the model seen from many directions; a vertex sees the sky that way if nothing
// of the model stands between it and the sky there.

namespace {

struct DepthGrid {
    V3 r{};
    V3 u{};
    V3 d{};
    double x0{};
    double y0{};
    double cell{};
    int w{};
    int h{};
    std::vector<float> depth{};
};

void raster_depth(DepthGrid& grid, const std::vector<V3>& points, const std::vector<std::uint32_t>& indices, const std::vector<char>* skip) {
    std::vector<double> gx(points.size());
    std::vector<double> gy(points.size());
    std::vector<double> gz(points.size());
    for (std::size_t k = 0; k < points.size(); ++k) {
        gx[k] = (dot(points[k], grid.r) - grid.x0) / grid.cell;
        gy[k] = (dot(points[k], grid.u) - grid.y0) / grid.cell;
        gz[k] = dot(points[k], grid.d);
    }
    for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
        if (skip != nullptr && (*skip)[t / 3] != 0)
            continue;
        const std::uint32_t i0 = indices[t];
        const std::uint32_t i1 = indices[t + 1];
        const std::uint32_t i2 = indices[t + 2];
        const double ax = gx[i0], ay = gy[i0], bx = gx[i1], by = gy[i1], cx = gx[i2], cy = gy[i2];
        const double area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (std::abs(area) < 1e-12)
            continue;
        const int xmin = std::max(0, static_cast<int>(std::floor(std::min({ax, bx, cx}))));
        const int xmax = std::min(grid.w - 1, static_cast<int>(std::ceil(std::max({ax, bx, cx}))));
        const int ymin = std::max(0, static_cast<int>(std::floor(std::min({ay, by, cy}))));
        const int ymax = std::min(grid.h - 1, static_cast<int>(std::ceil(std::max({ay, by, cy}))));
        const double inv = 1 / area;
        const double e0a = (by - cy) * inv, e0b = (cx - bx) * inv, e0c = (bx * cy - by * cx) * inv;
        const double e1a = (cy - ay) * inv, e1b = (ax - cx) * inv, e1c = (cx * ay - cy * ax) * inv;
        // Conservative: a cell the triangle touches counts, so thin parts still cast.
        const double slack = 0.7 * std::abs(inv) * std::max({std::abs(bx - cx) + std::abs(by - cy), std::abs(cx - ax) + std::abs(cy - ay), std::abs(ax - bx) + std::abs(ay - by)});
        const double z0 = gz[i0], z1 = gz[i1], z2 = gz[i2];
        for (int y = ymin; y <= ymax; ++y) {
            float* row = grid.depth.data() + static_cast<std::size_t>(y * grid.w);
            const double py = y + 0.5;
            for (int x = xmin; x <= xmax; ++x) {
                const double px = x + 0.5;
                double w0 = e0a * px + e0b * py + e0c;
                double w1 = e1a * px + e1b * py + e1c;
                double w2 = 1 - w0 - w1;
                if (w0 < -slack || w1 < -slack || w2 < -slack)
                    continue;
                w0 = std::max(0.0, w0);
                w1 = std::max(0.0, w1);
                w2 = std::max(0.0, w2);
                const float z = static_cast<float>((z0 * w0 + z1 * w1 + z2 * w2) / (w0 + w1 + w2));
                row[x] = std::max(row[x], z);
            }
        }
    }
}

void frame_grid(DepthGrid& grid, const std::vector<V3>& points, double cell_hint, int most) {
    double x0 = 1e30, x1 = -1e30, y0 = 1e30, y1 = -1e30;
    for (const V3& p : points) {
        x0 = std::min(x0, dot(p, grid.r));
        x1 = std::max(x1, dot(p, grid.r));
        y0 = std::min(y0, dot(p, grid.u));
        y1 = std::max(y1, dot(p, grid.u));
    }
    grid.cell = std::max({cell_hint, (x1 - x0) / most, (y1 - y0) / most, 1e-4});
    grid.x0 = x0 - grid.cell * 2;
    grid.y0 = y0 - grid.cell * 2;
    grid.w = static_cast<int>(std::ceil((x1 - x0) / grid.cell)) + 5;
    grid.h = static_cast<int>(std::ceil((y1 - y0) / grid.cell)) + 5;
    grid.depth.assign(static_cast<std::size_t>(grid.w * grid.h), -1e30F);
}

void basis_for(V3 d, V3& r, V3& u) {
    r = normalized(cross(d, std::abs(d.z) < 0.95 ? V3{0, 0, 1} : V3{1, 0, 0}));
    u = cross(d, r);
}

} // namespace

void bake_occlusion(Mesh& mesh, int directions) {
    if (mesh.vertices.empty())
        return;
    std::vector<V3> points(mesh.vertices.size());
    double extent = 0;
    V3 lo{1e30, 1e30, 1e30};
    V3 hi{-1e30, -1e30, -1e30};
    for (std::size_t k = 0; k < points.size(); ++k) {
        points[k] = mesh.vertices[k].p;
        lo = V3{std::min(lo.x, points[k].x), std::min(lo.y, points[k].y), std::min(lo.z, points[k].z)};
        hi = V3{std::max(hi.x, points[k].x), std::max(hi.y, points[k].y), std::max(hi.z, points[k].z)};
    }
    extent = length(hi - lo);
    std::vector<double> seen(points.size(), 0.0);
    std::vector<double> total(points.size(), 0.0);
    const double golden = pi * (3 - std::sqrt(5.0));
    for (int k = 0; k < directions; ++k) {
        // Fibonacci points over the sphere; directions under the ground see only the ground.
        const double z = 1 - 2 * (k + 0.5) / directions;
        const double ring = std::sqrt(std::max(0.0, 1 - z * z));
        const V3 d{std::cos(golden * k) * ring, std::sin(golden * k) * ring, z};
        DepthGrid grid{};
        grid.d = d;
        basis_for(d, grid.r, grid.u);
        frame_grid(grid, points, extent / 220, 220);
        raster_depth(grid, points, mesh.indices, nullptr);
        for (std::size_t v = 0; v < points.size(); ++v) {
            const double facing = dot(mesh.vertices[v].n, d);
            if (facing <= 0)
                continue;
            const V3 p = points[v] + mesh.vertices[v].n * (grid.cell * 1.5);
            bool open = true;
            if (d.z < 0) {
                // Towards the ground: the ground itself is lit; count it open unless the model
                // stands between (handled below like any other direction).
                open = true;
            }
            const int gx = static_cast<int>((dot(p, grid.r) - grid.x0) / grid.cell);
            const int gy = static_cast<int>((dot(p, grid.u) - grid.y0) / grid.cell);
            if (gx >= 0 && gy >= 0 && gx < grid.w && gy < grid.h) {
                const float there = grid.depth[static_cast<std::size_t>(gy * grid.w + gx)];
                if (there > dot(p, d) + grid.cell * 2.0)
                    open = false;
            }
            total[v] += facing;
            if (open)
                seen[v] += facing;
        }
    }
    for (std::size_t v = 0; v < points.size(); ++v)
        mesh.vertices[v].ao = total[v] > 0 ? static_cast<float>(std::max(0.12, seen[v] / total[v])) : 1.0F;
}

// ---------------------------------------------------------------------------------------------
// Shadow map

float ShadowMap::light(V3 p, double bias) const {
    if (w <= 0)
        return 1;
    const double gx = (dot(p, r) - x0) / cell - 0.5;
    const double gy = (dot(p, u) - y0) / cell - 0.5;
    const double here = dot(p, l) + bias;
    const int ix = static_cast<int>(std::floor(gx));
    const int iy = static_cast<int>(std::floor(gy));
    const double fx = gx - ix;
    const double fy = gy - iy;
    // 3x3 taps with bilinear weights over a 2x2 footprint: soft-edged shade.
    double lit = 0;
    double weight = 0;
    for (int dy = 0; dy <= 2; ++dy)
        for (int dx = 0; dx <= 2; ++dx) {
            const int x = ix + dx - (fx < 0.5 ? 1 : 0);
            const int y = iy + dy - (fy < 0.5 ? 1 : 0);
            const double gx_w = fx < 0.5 ? fx + 0.5 : fx - 0.5;
            const double gy_w = fy < 0.5 ? fy + 0.5 : fy - 0.5;
            const double wx = dx == 0 ? 1 - gx_w : dx == 2 ? gx_w : 1.0;
            const double wy = dy == 0 ? 1 - gy_w : dy == 2 ? gy_w : 1.0;
            const double k = wx * wy;
            weight += k;
            if (x < 0 || y < 0 || x >= w || y >= h) {
                lit += k;
                continue;
            }
            if (depth[static_cast<std::size_t>(y * w + x)] <= here)
                lit += k;
        }
    return static_cast<float>(lit / weight);
}

// ---------------------------------------------------------------------------------------------
// Shooting

namespace {

struct Placed {
    V3 colour{1, 1, 1};
    V3 p{};
    V3 n{};
    V3 o{};
    float u{};
    float v{};
    float ao{};
    float blend{};
    double sx{};
    double sy{};
    double depth{};
};

struct Tri {
    std::uint32_t a{};
    std::uint32_t b{};
    std::uint32_t c{};
    const Material* material{};
    const Xform* place{};
};

struct Fragment {
    V3 colour{1, 1, 1};
    V3 p{};
    V3 n{};
    V3 o{};
    double u{};
    double v{};
    double ao{};
    double blend{};
};

// The sky and the lawn as seen in a direction: what glossy things reflect.
Lin environment(V3 d) {
    const Lin zenith{0.30, 0.48, 0.85};
    const Lin horizon{0.78, 0.86, 0.94};
    const Lin lawn{0.055, 0.11, 0.025};
    if (d.z >= 0)
        return mixed(horizon, zenith, std::sqrt(d.z)) * 1.25;
    return mixed(Lin{0.25, 0.32, 0.18}, lawn, smooth(0.0, 0.25, -d.z));
}

// Light from the sky and the lawn on a surface facing `n`.
Lin hemisphere(V3 n) {
    const Lin sky{0.40, 0.52, 0.78};
    const Lin ground{0.13, 0.20, 0.06};
    const double t = 0.5 + 0.5 * n.z;
    return mixed(ground, sky, t) * 0.95;
}

// SDF strokes for the hood badges.
double segment_distance(double px, double py, double ax, double ay, double bx, double by) {
    const double vx = bx - ax, vy = by - ay;
    const double t = clamp01(((px - ax) * vx + (py - ay) * vy) / std::max(1e-12, vx * vx + vy * vy));
    return std::hypot(px - ax - vx * t, py - ay - vy * t);
}
double glyph_distance(int glyph, double x, double y) {
    double d = 1e9;
    if (glyph == 'H') {
        d = std::min({segment_distance(x, y, -0.45, -0.7, -0.45, 0.7), segment_distance(x, y, 0.45, -0.7, 0.45, 0.7), segment_distance(x, y, -0.45, 0, 0.45, 0)});
    } else if (glyph == 'T') {
        d = std::min(segment_distance(x, y, -0.55, -0.65, 0.55, -0.65), segment_distance(x, y, 0, -0.65, 0, 0.7));
    } else if (glyph == 'J') {
        d = std::min(segment_distance(x, y, 0.3, -0.7, 0.3, 0.3), segment_distance(x, y, 0.4, -0.7, -0.05, -0.7));
        // the hook
        const double r = std::hypot(x - 0.0, y - 0.3);
        if (y > 0.3)
            d = std::min(d, std::abs(r - 0.3));
    } else if (glyph == 'D') {
        d = segment_distance(x, y, -0.4, -0.7, -0.4, 0.7);
        d = std::min({d, segment_distance(x, y, -0.4, -0.7, 0.0, -0.7), segment_distance(x, y, -0.4, 0.7, 0.0, 0.7)});
        if (x > 0.0) {
            const double r = std::hypot((x - 0.0) / 0.5, y / 0.7);
            d = std::min(d, std::abs(r - 1) * 0.5);
        }
    }
    return d;
}

// What the surface looks like here: colour, a bent normal, how glossy and how opaque.
struct Look {
    Lin albedo{};
    V3 n{};
    double specular{};
    double gloss{};
    double cavity{1};  // extra darkening of crevices
    double alpha{1};
    double emit{};
};

V3 bumped(V3 n, V3 gradient_world, double strength) {
    const V3 tangential = gradient_world - n * dot(gradient_world, n);
    return normalized(n - tangential * strength);
}

V3 noise_gradient(V3 p, double eps, int octaves) {
    const double c = fbm3(p, octaves);
    return V3{fbm3(p + V3{eps, 0, 0}, octaves) - c, fbm3(p + V3{0, eps, 0}, octaves) - c, fbm3(p + V3{0, 0, eps}, octaves) - c} * (1 / eps);
}

Look surface(const Material& m, const Fragment& f, const Xform& place, double time) {
    Look look{};
    const Lin base = linear(m.base);
    const Lin tint = linear(m.tint);
    look.albedo = mixed(base, tint, f.blend) * Lin{f.colour.x, f.colour.y, f.colour.z};
    look.n = f.n;
    look.specular = m.specular;
    look.gloss = m.gloss;
    look.alpha = m.opacity;
    look.emit = m.emit;
    const double s = m.scale;
    switch (m.pattern) {
    case Pattern::none:
        break;
    case Pattern::tread: {
        // u round the tyre (0..1), v across it (0..1); chevron lugs on the tread face only
        const double across = f.v - 0.5;
        const double on_face = 1 - smooth(0.36, 0.44, std::abs(across));
        const double phase = fract(f.u * s + std::abs(across) * 1.6);
        const double groove = smooth(0.0, 0.06, phase) * (1 - smooth(0.36, 0.42, phase));
        const double lug = on_face * groove;
        look.albedo = look.albedo * (0.45 + 0.75 * lug);
        look.cavity = 0.55 + 0.45 * (lug + (1 - on_face) * 0.9);
        look.gloss = 0.25 + 0.1 * lug;
        // sidewall lettering ring and the bead
        if (std::abs(across) > 0.44 && std::abs(across) < 0.47)
            look.albedo = look.albedo * 1.35;
        break;
    }
    case Pattern::stone:
    case Pattern::wet_stone: {
        const V3 q = f.o * (s * 6);
        const double mottle = fbm3(q, 4);
        const double fine = noise3(q * 7.3);
        look.albedo = look.albedo * (0.78 + 0.42 * mottle) * (0.92 + 0.12 * fine);
        // lichen: pale and dark-grey specks
        const double lichen = smooth(0.66, 0.72, noise3(q * 2.1 + V3{4, 1, 7}));
        look.albedo = mixed(look.albedo, Lin{0.36, 0.38, 0.30}, lichen * 0.35);
        look.n = bumped(look.n, place.vector(noise_gradient(q * 3.0, 0.05, 3)), 0.035 / s);
        look.cavity = 0.85 + 0.15 * mottle;
        if (m.pattern == Pattern::wet_stone) {
            const double below = 1 - smooth(m.level - 0.03, m.level + 0.05, f.o.z);
            const double band = std::exp(-std::pow((f.o.z - m.level) / 0.06, 2));
            const double moss = band * smooth(0.45, 0.62, fbm3(q * 1.7 + V3{9, 2, 5}, 3));
            look.albedo = look.albedo * (1 - 0.45 * below);
            look.albedo = mixed(look.albedo, Lin{0.045, 0.075, 0.018}, moss * 0.85);
            look.gloss = look.gloss + (0.85 - look.gloss) * below;
            look.specular = look.specular + 0.02 * below;
        }
        break;
    }
    case Pattern::bark: {
        const V3 q{f.o.x * s * 14, f.o.y * s * 14, f.o.z * s * 2.2};
        const double ridges = std::abs(fbm3(q, 4) * 2 - 1);
        const double furrow = smooth(0.0, 0.35, ridges);
        look.albedo = look.albedo * (0.55 + 0.5 * furrow) * (0.9 + 0.2 * noise3(q * 3.1));
        const double lichen = smooth(0.7, 0.78, noise3(V3{f.o.x * 30, f.o.y * 30, f.o.z * 18}));
        look.albedo = mixed(look.albedo, Lin{0.2, 0.24, 0.15}, lichen * 0.35);
        look.n = bumped(look.n, place.vector(noise_gradient(q, 0.04, 3)), 0.5 / (s * 14));
        look.cavity = 0.6 + 0.4 * furrow;
        break;
    }
    case Pattern::floral: {
        const double cu = f.u * s;
        const double cv = f.v * s * 1.4;
        const int iu = static_cast<int>(std::floor(cu));
        const int iv = static_cast<int>(std::floor(cv));
        const double jx = lattice01(iu, iv, 3) * 0.4 + 0.3;
        const double jy = lattice01(iu, iv, 5) * 0.4 + 0.3;
        const double dx = fract(cu) - jx;
        const double dy = fract(cv) - jy;
        const double r = std::hypot(dx, dy);
        const double a = std::atan2(dy, dx);
        const double petal = 0.21 * (0.7 + 0.3 * std::cos(5 * a + lattice01(iu, iv, 9) * 6));
        if (r < petal)
            look.albedo = mixed(tint, Lin{0.9, 0.9, 0.85}, smooth(petal * 0.7, petal, r) * 0.3);
        if (r < 0.055)
            look.albedo = Lin{0.80, 0.55, 0.05};
        // little leaves
        const double lx = fract(cu + 0.5) - 0.5;
        const double ly = fract(cv + 0.5) - 0.5;
        if (std::abs(lx * 0.8 + ly * 0.6) < 0.035 && std::abs(-lx * 0.6 + ly * 0.8) < 0.11)
            look.albedo = Lin{0.08, 0.20, 0.05};
        break;
    }
    case Pattern::knit: {
        const double rib = 0.5 + 0.5 * std::cos(f.u * s * 2 * pi);
        const double stitch = 0.5 + 0.5 * std::cos((f.v * s * 1.6 + std::abs(fract(f.u * s) - 0.5)) * 2 * pi);
        look.albedo = look.albedo * (0.72 + 0.18 * rib + 0.14 * stitch);
        look.cavity = 0.8 + 0.2 * rib;
        break;
    }
    case Pattern::stripes: {
        const double stripe = smooth(0.46, 0.5, fract(f.v * s)) * (1 - smooth(0.96, 1.0, fract(f.v * s)));
        look.albedo = mixed(base, tint, stripe);
        break;
    }
    case Pattern::tread_plate: {
        const double gx = f.o.x * s * 30;
        const double gy = f.o.y * s * 30;
        const int ix = static_cast<int>(std::floor(gx));
        const int iy = static_cast<int>(std::floor(gy));
        const double lx = fract(gx) - 0.5;
        const double ly = fract(gy) - 0.5;
        const bool turn = ((ix + iy) & 1) != 0;
        const double a = turn ? (lx + ly) : (lx - ly);
        const double b = turn ? (lx - ly) : (lx + ly);
        const double lozenge = 1 - smooth(0.08, 0.12, std::abs(b) + std::abs(a) * 0.18);
        look.albedo = look.albedo * (0.85 + 0.35 * lozenge);
        look.gloss = look.gloss + 0.2 * lozenge;
        break;
    }
    case Pattern::grille: {
        const double slot = smooth(0.38, 0.45, fract(f.u * s)) * (1 - smooth(0.88, 0.95, fract(f.u * s)));
        const double inset = smooth(0.06, 0.12, f.v) * (1 - smooth(0.88, 0.94, f.v));
        const double hole = slot * inset;
        look.albedo = mixed(look.albedo, Lin{0.012, 0.012, 0.012}, hole);
        look.cavity = 1 - 0.8 * hole;
        look.specular = look.specular * (1 - hole);
        break;
    }
    case Pattern::glyph: {
        const double x = (f.u * 2 - 1) * 1.15;
        const double y = (f.v * 2 - 1) * 1.15;
        double d = 0;
        if (static_cast<int>(m.level) == 1)
            d = x < 0 ? glyph_distance('J', (x + 0.5) * 1.8, y * 1.15) / 1.8 : glyph_distance('D', (x - 0.5) * 1.8, y * 1.15) / 1.8;
        else
            d = glyph_distance(static_cast<int>(m.level), x, y);
        const double ink = 1 - smooth(0.13, 0.17, d);
        look.albedo = mixed(look.albedo, tint, ink);
        break;
    }
    case Pattern::scales: {
        const double row = f.v * s;
        const int ir = static_cast<int>(std::floor(row));
        const double col = f.u * s * 2.4 + (ir % 2 == 0 ? 0.0 : 0.5);
        const double dx = fract(col) - 0.5;
        const double dy = fract(row);
        const double edge = smooth(0.25, 0.5, std::hypot(dx * 1.2, dy - 0.15));
        look.albedo = look.albedo * (1.1 - 0.55 * edge);
        look.cavity = 1 - 0.4 * edge;
        break;
    }
    case Pattern::hair: {
        const double strand = noise3(V3{f.u * s * 60, f.v * 4, 0.5}) * 0.6 + noise3(V3{f.u * s * 170, f.v * 9, 3.5}) * 0.4;
        look.albedo = look.albedo * (0.7 + 0.55 * strand);
        look.gloss = 0.55;
        look.specular = 0.05;
        look.cavity = 0.8 + 0.2 * strand;
        break;
    }
    case Pattern::wood: {
        const double grain = fract(f.u * s * 3 + fbm3(V3{f.u * 2, f.v * 30, 1}, 2) * 2.5);
        look.albedo = look.albedo * (0.82 + 0.25 * smooth(0.0, 0.7, grain));
        break;
    }
    case Pattern::down: {
        const double fluff = fbm3(f.o * (s * 90), 3);
        look.albedo = look.albedo * (0.82 + 0.3 * fluff);
        look.n = bumped(look.n, place.vector(noise_gradient(f.o * (s * 60), 0.05, 2)), 0.6 / (s * 60));
        break;
    }
    case Pattern::weave: {
        const double wx = 0.5 + 0.5 * std::cos(f.u * s * 2 * pi * 6);
        const double wy = 0.5 + 0.5 * std::cos(f.v * s * 2 * pi * 6);
        const bool over = (static_cast<int>(std::floor(f.u * s * 6)) + static_cast<int>(std::floor(f.v * s * 6))) % 2 == 0;
        const Lin colour = fract(f.v * m.level) < 0.5 ? tint : base;
        look.albedo = colour * (0.78 + 0.27 * (over ? wx : wy));
        look.cavity = 0.85 + 0.15 * (over ? wx : wy);
        break;
    }
    case Pattern::water: {
        // Rings spreading from where the falling water lands, and a fine breeze chop.
        const double rr = std::hypot(f.o.x, f.o.y);
        const double ring = rr - m.level;
        const double k = 70.0;
        const double wave = std::sin(k * ring - time * 9.0) * std::exp(-std::abs(ring) * 9);
        const double slope_r = std::cos(k * ring - time * 9.0) * k * std::exp(-std::abs(ring) * 9) * 0.0016;
        const V3 radial = rr > 1e-6 ? V3{f.o.x / rr, f.o.y / rr, 0} : V3{0, 0, 0};
        // a breeze chop: a few small waves crossing, cheap to slope analytically
        const double w1 = std::cos(f.o.x * 31 + f.o.y * 17 - time * 3.1);
        const double w2 = std::cos(f.o.x * -13 + f.o.y * 37 - time * 2.6);
        const double w3 = std::cos(f.o.x * 47 - f.o.y * 23 - time * 4.3);
        const V3 chop{(31 * w1 - 13 * w2 + 47 * w3) * 0.00022, (17 * w1 + 37 * w2 - 23 * w3) * 0.00022, 0};
        const V3 g = radial * slope_r + chop;
        look.n = normalized(V3{-g.x, -g.y, 1});
        look.albedo = look.albedo * (0.9 + 0.1 * wave);
        break;
    }
    case Pattern::curtain: {
        const double streak = noise3(V3{f.u * s * 40, f.v * 6 - time * 5.5, 0.3}) * 0.55 + noise3(V3{f.u * s * 110, f.v * 11 - time * 7.5, 4.1}) * 0.45;
        look.alpha = m.opacity * (0.25 + 0.95 * smooth(0.3, 0.8, streak));
        look.albedo = mixed(look.albedo, Lin{0.9, 0.95, 0.97}, smooth(0.6, 0.9, streak) * 0.6 + smooth(0.75, 1.0, f.v) * 0.4);
        break;
    }
    case Pattern::blossom: {
        const double cu = f.u * s;
        const double cv = f.v * s * 1.2;
        const int iu = static_cast<int>(std::floor(cu));
        const int iv = static_cast<int>(std::floor(cv));
        const double dx = fract(cu) - 0.5 - (lattice01(iu, iv, 1) - 0.5) * 0.3;
        const double dy = fract(cv) - 0.5 - (lattice01(iu, iv, 2) - 0.5) * 0.3;
        const double r = std::hypot(dx, dy);
        const double a = std::atan2(dy, dx) + lattice01(iu, iv, 4) * 6;
        const double petal = 0.33 * (0.62 + 0.38 * std::pow(std::abs(std::cos(2.5 * a)), 0.6));
        const double leaf = std::abs(std::sin(a * 1.0 + r * 9)) < 0.18 && r > 0.3 && r < 0.48 ? 1.0 : 0.0;
        if (leaf > 0)
            look.albedo = Lin{0.03, 0.17, 0.10};
        if (r < petal)
            look.albedo = mixed(tint, Lin{0.95, 0.85, 0.7}, smooth(petal * 0.2, 0.0, r) * 0.6);
        if (r < 0.04)
            look.albedo = Lin{0.9, 0.7, 0.1};
        break;
    }
    case Pattern::skin: {
        const double mottle = fbm3(f.o * 40.0, 2);
        look.albedo = look.albedo * (0.94 + 0.1 * mottle);
        break;
    }
    case Pattern::rubber: {
        const double pebble = noise3(f.o * (s * 400));
        look.albedo = look.albedo * (0.85 + 0.3 * pebble);
        look.cavity = 0.9 + 0.1 * pebble;
        break;
    }
    case Pattern::metal_brushed: {
        const double brush = noise3(V3{f.u * 3, f.v * 300, 0.5});
        look.gloss = look.gloss * (0.85 + 0.15 * brush);
        break;
    }
    case Pattern::cap_print: {
        const double brush = fbm3(f.o * (s * 30), 3);
        look.albedo = look.albedo * (0.88 + 0.22 * brush);
        look.gloss = look.gloss * (0.8 + 0.4 * brush);
        break;
    }
    case Pattern::sand: {
        // Grains: a fine speckle of light quartz and darker bits; dug hollows and heaps;
        // where it was wetted, darker and smoother.
        const V3 q = f.o * s;
        const double grain = noise3(q * 900.0), speck = noise3(q * 420.0 + V3{3, 7, 1});
        const double heaps = fbm3(q * 4.0, 3);
        const double damp = smooth(0.58, 0.7, fbm3(q * 2.2 + V3{11, 4, 2}, 3));
        look.albedo = look.albedo * (0.86 + 0.24 * grain) * (0.9 + 0.16 * heaps);
        if (speck > 0.8)
            look.albedo = look.albedo * 1.25;
        else if (speck < 0.16)
            look.albedo = look.albedo * 0.7;
        look.albedo = look.albedo * (1 - 0.32 * damp);
        look.n = bumped(look.n, place.vector(noise_gradient(q * 4.0, 0.02, 3)), 0.18);
        look.n = bumped(look.n, place.vector(noise_gradient(q * 300.0, 0.002, 1)), 0.0016);
        look.cavity = 0.82 + 0.18 * heaps;
        look.gloss = 0.08 + 0.12 * damp;
        break;
    }
    case Pattern::shell: {
        // Hexagonal plates, each a little domed and lighter in the middle, with deep seams.
        const double k = s * 7.0;
        const double x = f.o.x * k, y = f.o.y * k * 1.1547;
        const double row = std::floor(y);
        const double col = std::floor(x - (static_cast<int>(row) & 1) * 0.5);
        double best = 1e9, second = 1e9;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const double r = row + dy, c = col + dx;
                const double cx = c + 0.5 + (static_cast<int>(r) & 1) * 0.5, cy = r + 0.5;
                const double d = std::hypot((x - cx), (y - cy) * 0.866);
                if (d < best) {
                    second = best;
                    best = d;
                } else if (d < second)
                    second = d;
            }
        const double seam = 1 - smooth(0.0, 0.06, second - best);
        const double centre = 1 - smooth(0.0, 0.45, best);
        look.albedo = mixed(look.albedo, tint, 0.35 * centre) * (1 - 0.55 * seam);
        look.cavity = 1 - 0.4 * seam;
        break;
    }
    }
    return look;
}

Lin lit(const Look& look, const Material& m, const Fragment& f, V3 view, float shadow) {
    V3 n = look.n;
    if (dot(n, view) < 0 && m.two_sided)
        n = n * -1;
    const double nl = dot(n, sun_dir);
    const double nv = std::max(1e-3, dot(n, view));
    const Lin sun{1.0, 0.93, 0.80};
    const double sun_power = 2.35;
    const double wrap = m.wrap;
    const double diffuse_sun = std::max(0.0, (nl + wrap) / (1 + wrap)) * shadow * sun_power;
    const double ao = f.ao * look.cavity;
    const Lin ambient = hemisphere(n) * ao;
    const double metal = m.metal;
    Lin colour = look.albedo * (1 - metal) * (1 - look.specular) * 1.0;
    colour = colour * (sun * diffuse_sun + ambient);
    // Specular: Blinn-Phong highlight of the sun, and the sky and lawn in reflection.
    const V3 h = normalized(sun_dir + view);
    const double exponent = std::pow(2.0, 1 + look.gloss * 11);
    const double fresnel = std::pow(1 - nv, 5);
    const Lin f0 = mixed(Lin{look.specular, look.specular, look.specular}, look.albedo, metal);
    const Lin reflectance = f0 + (Lin{1, 1, 1} + f0 * -1.0) * fresnel;
    const double highlight = (exponent + 8) / (8 * pi) * std::pow(std::max(0.0, dot(n, h)), exponent) * std::max(0.0, nl) * shadow * sun_power;
    const V3 reflected = n * (2 * dot(n, view)) - view;
    const Lin mirror = mixed(hemisphere(reflected), environment(reflected), look.gloss * look.gloss);
    colour = colour + reflectance * (sun * highlight) + reflectance * mirror * (ao * (0.35 + 0.65 * look.gloss));
    if (m.sheen > 0)
        colour = colour + look.albedo * hemisphere(n) * (m.sheen * std::pow(1 - nv, 3) * ao);
    if (look.emit > 0)
        colour = colour + look.albedo * look.emit;
    return colour;
}

// Four samples a pixel on a rotated grid.
const double sample_x[4] = {0.375, 0.875, 0.125, 0.625};
const double sample_y[4] = {0.125, 0.375, 0.625, 0.875};

struct Scratch {
    std::vector<Placed> placed{};
    std::vector<Tri> solid{};
    std::vector<Tri> clear{};
    std::vector<float> depth{};
    std::vector<std::int32_t> id{};
    std::vector<float> colour{};  // premultiplied, display, 4 per pixel
    std::vector<float> zclip{};
    std::vector<std::uint16_t> cover{};
};

Scratch& scratch() {
    thread_local Scratch s{};
    return s;
}

} // namespace

double view_depth(const Frame& frame, V3 p) {
    return p.y * frame.rise + p.z * frame.tilt;
}

void shoot(const Frame& frame, const std::vector<Part>& parts, const ShotOptions& options, Shot& shot) {
    Scratch& s = scratch();
    std::chrono::steady_clock::time_point mark = std::chrono::steady_clock::now();
    const bool profile = std::getenv("MM_PROFILE") != nullptr;
    s.placed.clear();
    s.solid.clear();
    s.clear.clear();
    const V3 view = normalized(V3{0, frame.rise, frame.tilt});
    std::vector<V3> casters{};
    std::vector<std::uint32_t> caster_indices{};
    double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
    double gx0 = 1e30, gy0 = 1e30, gx1 = -1e30, gy1 = -1e30;
    for (const Part& part : parts) {
        const Mesh& mesh = *part.mesh;
        const std::uint32_t base = static_cast<std::uint32_t>(s.placed.size());
        for (const Vertex& v : mesh.vertices) {
            Placed p{};
            p.p = part.place.point(v.p);
            p.n = part.place.normal(v.n);
            p.o = v.o;
            p.u = v.u;
            p.v = v.v;
            p.ao = v.ao;
            p.blend = v.blend;
            p.colour = v.colour;
            p.sx = frame.px(p.p.x);
            p.sy = frame.py(p.p.y) - frame.up(p.p.z);
            p.depth = view_depth(frame, p.p);
            s.placed.push_back(p);
        }
        // What throws the shade: the coarse mesh if there is one, else the drawn one.
        const Mesh& caster = part.shadow != nullptr ? *part.shadow : mesh;
        const std::uint32_t cbase = static_cast<std::uint32_t>(casters.size());
        if (part.shadow != nullptr) {
            for (const Vertex& v : caster.vertices)
                casters.push_back(part.place.point(v.p));
        } else {
            for (std::size_t k = base; k < s.placed.size(); ++k)
                casters.push_back(s.placed[k].p);
        }
        for (std::size_t t = 0; t < caster.materials.size(); ++t)
            if ((*part.materials)[caster.materials[t]].opacity >= 1)
                caster_indices.insert(caster_indices.end(), {cbase + caster.indices[t * 3], cbase + caster.indices[t * 3 + 1], cbase + caster.indices[t * 3 + 2]});
        for (std::size_t t = 0; t < mesh.materials.size(); ++t) {
            const Material& material = (*part.materials)[mesh.materials[t]];
            const Tri tri{base + mesh.indices[t * 3], base + mesh.indices[t * 3 + 1], base + mesh.indices[t * 3 + 2], &material, &part.place};
            if (part.casts_only)
                continue;
            // Facing away on every corner: the back of a closed solid, hidden anyway.
            if (!material.two_sided && material.opacity >= 1) {
                const Placed& a = s.placed[tri.a];
                const Placed& b = s.placed[tri.b];
                const Placed& c = s.placed[tri.c];
                if (dot(a.n, view) < -0.1 && dot(b.n, view) < -0.1 && dot(c.n, view) < -0.1)
                    continue;
            }
            for (std::uint32_t k : {tri.a, tri.b, tri.c}) {
                x0 = std::min(x0, s.placed[k].sx);
                x1 = std::max(x1, s.placed[k].sx);
                y0 = std::min(y0, s.placed[k].sy);
                y1 = std::max(y1, s.placed[k].sy);
            }
            if (material.opacity >= 1)
                s.solid.push_back(tri);
            else
                s.clear.push_back(tri);
        }
    }

    if (profile) { std::fprintf(stderr, "%s %.3f ms\n", "A", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mark).count()); mark = std::chrono::steady_clock::now(); }
    // The shade map, from the sun.
    if (!caster_indices.empty()) {
        ShadowMap& map = shot.map;
        map.l = sun_dir;
        basis_for(sun_dir, map.r, map.u);
        DepthGrid grid{};
        grid.d = map.l;
        grid.r = map.r;
        grid.u = map.u;
        frame_grid(grid, casters, 1 / (frame.ppm * 1.15), 900);
        raster_depth(grid, casters, caster_indices, nullptr);
        map.x0 = grid.x0;
        map.y0 = grid.y0;
        map.cell = grid.cell;
        map.w = grid.w;
        map.h = grid.h;
        map.depth = std::move(grid.depth);
    } else if (options.occluder != nullptr) {
        shot.map = (*options.occluder).map;
    } else {
        shot.map = ShadowMap{};
    }
    const ShadowMap& map = shot.map;

    if (profile) { std::fprintf(stderr, "%s %.3f ms\n", "B", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mark).count()); mark = std::chrono::steady_clock::now(); }
    // The shadow on the grass: every solid triangle pressed flat along the sun.
    shot.shadow.clear();
    shot.sw = shot.sh = 0;
    if (options.ground_shadow && !caster_indices.empty()) {
        std::vector<double> gx(casters.size());
        std::vector<double> gy(casters.size());
        for (std::size_t k = 0; k < casters.size(); ++k) {
            const V3 p = casters[k];
            const double z = std::max(0.0, p.z);
            const V3 g{p.x - sun_dir.x / sun_dir.z * z, p.y - sun_dir.y / sun_dir.z * z, 0};
            gx[k] = frame.px(g.x);
            gy[k] = frame.py(g.y);
            gx0 = std::min(gx0, gx[k]);
            gx1 = std::max(gx1, gx[k]);
            gy0 = std::min(gy0, gy[k]);
            gy1 = std::max(gy1, gy[k]);
        }
        const int soft = std::max(1, static_cast<int>(std::lround(frame.ppm * 0.012)));
        shot.sx0 = static_cast<int>(std::floor(gx0)) - soft - 1;
        shot.sy0 = static_cast<int>(std::floor(gy0)) - soft - 1;
        shot.sw = static_cast<int>(std::ceil(gx1)) - shot.sx0 + soft + 2;
        shot.sh = static_cast<int>(std::ceil(gy1)) - shot.sy0 + soft + 2;
        if (shot.sw > 0 && shot.sh > 0 && shot.sw < 8000 && shot.sh < 8000) {
            std::vector<std::uint8_t> bits(static_cast<std::size_t>(shot.sw * shot.sh), 0);
            for (std::size_t t = 0; t + 2 < caster_indices.size(); t += 3) {
                const std::uint32_t i0 = caster_indices[t], i1 = caster_indices[t + 1], i2 = caster_indices[t + 2];
                if (casters[i0].z < 0 && casters[i1].z < 0 && casters[i2].z < 0)
                    continue;
                const double ax = gx[i0] - shot.sx0, ay = gy[i0] - shot.sy0;
                const double bx = gx[i1] - shot.sx0, by = gy[i1] - shot.sy0;
                const double cx = gx[i2] - shot.sx0, cy = gy[i2] - shot.sy0;
                const double area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
                if (std::abs(area) < 1e-9)
                    continue;
                const double inv = 1 / area;
                const int xmin = std::max(0, static_cast<int>(std::floor(std::min({ax, bx, cx}))));
                const int xmax = std::min(shot.sw - 1, static_cast<int>(std::ceil(std::max({ax, bx, cx}))));
                const int ymin = std::max(0, static_cast<int>(std::floor(std::min({ay, by, cy}))));
                const int ymax = std::min(shot.sh - 1, static_cast<int>(std::ceil(std::max({ay, by, cy}))));
                const double e0a = (by - cy) * inv, e0b = (cx - bx) * inv, e0c = (bx * cy - by * cx) * inv;
                const double e1a = (cy - ay) * inv, e1b = (ax - cx) * inv, e1c = (cx * ay - cy * ax) * inv;
                for (int y = ymin; y <= ymax; ++y)
                    for (int x = xmin; x <= xmax; ++x) {
                        std::uint8_t& cell = bits[static_cast<std::size_t>(y * shot.sw + x)];
                        if (cell != 0)
                            continue;
                        const double px = x + 0.5;
                        const double py = y + 0.5;
                        const double w0 = e0a * px + e0b * py + e0c;
                        const double w1 = e1a * px + e1b * py + e1c;
                        if (w0 >= 0 && w1 >= 0 && w0 + w1 <= 1)
                            cell = 15;
                    }
            }
            // Coverage, then a small blur for the penumbra.
            std::vector<float> a(bits.size());
            for (std::size_t k = 0; k < bits.size(); ++k) {
                const unsigned b = bits[k];
                a[k] = static_cast<float>(((b & 1U) + ((b >> 1U) & 1U) + ((b >> 2U) & 1U) + ((b >> 3U) & 1U)) / 4.0);
            }
            std::vector<float> tmp(a.size());
            for (int pass = 0; pass < 2; ++pass) {
                const int w = shot.sw, h = shot.sh;
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x) {
                        double sum = 0;
                        int count = 0;
                        for (int k = -soft; k <= soft; ++k) {
                            const int xx = pass == 0 ? x + k : x;
                            const int yy = pass == 0 ? y : y + k;
                            if (xx < 0 || yy < 0 || xx >= w || yy >= h)
                                continue;
                            sum += a[static_cast<std::size_t>(yy * w + xx)];
                            ++count;
                        }
                        tmp[static_cast<std::size_t>(y * w + x)] = static_cast<float>(sum / std::max(1, count));
                    }
                a.swap(tmp);
            }
            shot.shadow.resize(a.size());
            for (std::size_t k = 0; k < a.size(); ++k)
                shot.shadow[k] = static_cast<std::uint8_t>(std::lround(255 * std::clamp(a[k] * options.shadow_strength, 0.0F, 1.0F)));
        } else {
            shot.sw = shot.sh = 0;
        }
    }

    if (profile) { std::fprintf(stderr, "%s %.3f ms\n", "C", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mark).count()); mark = std::chrono::steady_clock::now(); }
    if (s.solid.empty() && s.clear.empty()) {
        shot.w = shot.h = 0;
        shot.px.clear();
        shot.depth.clear();
        return;
    }
    shot.x0 = static_cast<int>(std::floor(x0)) - 1;
    shot.y0 = static_cast<int>(std::floor(y0)) - 1;
    shot.w = static_cast<int>(std::ceil(x1)) - shot.x0 + 2;
    shot.h = static_cast<int>(std::ceil(y1)) - shot.y0 + 2;
    if (shot.w <= 0 || shot.h <= 0 || shot.w > 8000 || shot.h > 8000) {
        shot.w = shot.h = 0;
        return;
    }
    const int w = shot.w;
    const int h = shot.h;
    const std::size_t samples = static_cast<std::size_t>(w * h * 4);
    s.depth.assign(samples, -1e30F);
    s.id.assign(samples, -1);
    if (options.occluder != nullptr) {
        const Shot& o = *options.occluder;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const int ox = x + shot.x0 - o.x0;
                const int oy = y + shot.y0 - o.y0;
                if (ox < 0 || oy < 0 || ox >= o.w || oy >= o.h)
                    continue;
                const float d = o.depth[static_cast<std::size_t>(oy * o.w + ox)];
                for (int k = 0; k < 4; ++k)
                    s.depth[static_cast<std::size_t>((y * w + x) * 4 + k)] = d;
            }
    }

    // Solids into the sample buffer. Edge functions are planes in the picture, stepped per sample.
    for (std::size_t t = 0; t < s.solid.size(); ++t) {
        const Tri& tri = s.solid[t];
        const Placed& A = s.placed[tri.a];
        const Placed& B = s.placed[tri.b];
        const Placed& C = s.placed[tri.c];
        const double ax = A.sx - shot.x0, ay = A.sy - shot.y0;
        const double bx = B.sx - shot.x0, by = B.sy - shot.y0;
        const double cx = C.sx - shot.x0, cy = C.sy - shot.y0;
        const double area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (std::abs(area) < 1e-10)
            continue;
        const double inv = 1 / area;
        // w0 = (e0a x + e0b y + e0c), w1 likewise; w2 = 1 - w0 - w1.
        const double e0a = (by - cy) * inv, e0b = (cx - bx) * inv, e0c = (bx * cy - by * cx) * inv;
        const double e1a = (cy - ay) * inv, e1b = (ax - cx) * inv, e1c = (cx * ay - cy * ax) * inv;
        const double da = A.depth - C.depth, db = B.depth - C.depth;
        const double za = A.p.z - C.p.z, zb = B.p.z - C.p.z;
        const int xmin = std::max(0, static_cast<int>(std::floor(std::min({ax, bx, cx}))));
        const int xmax = std::min(w - 1, static_cast<int>(std::floor(std::max({ax, bx, cx}))));
        const int ymin = std::max(0, static_cast<int>(std::floor(std::min({ay, by, cy}))));
        const int ymax = std::min(h - 1, static_cast<int>(std::floor(std::max({ay, by, cy}))));
        const bool clip = options.clip_ground;
        for (int y = ymin; y <= ymax; ++y) {
            float* depth_row = s.depth.data() + static_cast<std::size_t>(y * w) * 4U;
            std::int32_t* id_row = s.id.data() + static_cast<std::size_t>(y * w) * 4U;
            for (int x = xmin; x <= xmax; ++x)
                for (int k = 0; k < 4; ++k) {
                    const double px = x + sample_x[k];
                    const double py = y + sample_y[k];
                    const double w0 = e0a * px + e0b * py + e0c;
                    if (w0 < 0)
                        continue;
                    const double w1 = e1a * px + e1b * py + e1c;
                    if (w1 < 0 || w0 + w1 > 1)
                        continue;
                    if (clip && C.p.z + za * w0 + zb * w1 < 0)
                        continue;
                    const float z = static_cast<float>(C.depth + da * w0 + db * w1);
                    const std::size_t at = static_cast<std::size_t>(x * 4 + k);
                    if (z > depth_row[at]) {
                        depth_row[at] = z;
                        id_row[at] = static_cast<std::int32_t>(t);
                    }
                }
        }
    }

    if (profile) { std::fprintf(stderr, "%s %.3f ms\n", "D", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mark).count()); mark = std::chrono::steady_clock::now(); }
    // Resolve: shade each distinct triangle in a pixel once, at the middle of its samples.
    s.colour.assign(static_cast<std::size_t>(w * h * 4), 0.0F);
    shot.depth.assign(static_cast<std::size_t>(w * h), -1e30F);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t at = static_cast<std::size_t>((y * w + x) * 4);
            float* out = &s.colour[at];
            float nearest = -1e30F;
            bool done[4] = {false, false, false, false};
            for (int k = 0; k < 4; ++k) {
                const std::int32_t id = s.id[at + static_cast<std::size_t>(k)];
                if (id < 0 || done[k])
                    continue;
                double mx = 0, my = 0;
                int count = 0;
                for (int j = k; j < 4; ++j)
                    if (s.id[at + static_cast<std::size_t>(j)] == id) {
                        done[j] = true;
                        mx += x + sample_x[j];
                        my += y + sample_y[j];
                        ++count;
                    }
                nearest = std::max(nearest, s.depth[at + static_cast<std::size_t>(k)]);
                mx /= count;
                my /= count;
                const Tri& tri = s.solid[static_cast<std::size_t>(id)];
                const Placed& A = s.placed[tri.a];
                const Placed& B = s.placed[tri.b];
                const Placed& C = s.placed[tri.c];
                const double ax = A.sx - shot.x0, ay = A.sy - shot.y0;
                const double bx = B.sx - shot.x0, by = B.sy - shot.y0;
                const double cx = C.sx - shot.x0, cy = C.sy - shot.y0;
                const double inv = 1 / ((bx - ax) * (cy - ay) - (by - ay) * (cx - ax));
                double w0 = ((bx - mx) * (cy - my) - (by - my) * (cx - mx)) * inv;
                double w1 = ((cx - mx) * (ay - my) - (cy - my) * (ax - mx)) * inv;
                w0 = clamp01(w0);
                w1 = std::clamp(w1, 0.0, 1 - w0);
                const double w2 = 1 - w0 - w1;
                Fragment f{};
                f.p = A.p * w0 + B.p * w1 + C.p * w2;
                f.n = normalized(A.n * w0 + B.n * w1 + C.n * w2);
                f.o = A.o * w0 + B.o * w1 + C.o * w2;
                f.u = A.u * w0 + B.u * w1 + C.u * w2;
                f.v = A.v * w0 + B.v * w1 + C.v * w2;
                f.ao = A.ao * w0 + B.ao * w1 + C.ao * w2;
                f.blend = A.blend * w0 + B.blend * w1 + C.blend * w2;
                f.colour = A.colour * w0 + B.colour * w1 + C.colour * w2;
                const Material& m = *tri.material;
                const Look look = surface(m, f, *tri.place, options.time);
                const double bias = map.cell * (2.0 + 3.0 * (1 - std::abs(dot(f.n, sun_dir))));
                const float sun = map.light(f.p + f.n * (map.cell * 2.4), bias);
                const Lin c = lit(look, m, f, view, sun);
                const double k_cover = count / 4.0;
                out[0] += static_cast<float>(tone(c.r) * k_cover);
                out[1] += static_cast<float>(tone(c.g) * k_cover);
                out[2] += static_cast<float>(tone(c.b) * k_cover);
                out[3] += static_cast<float>(k_cover);
            }
            shot.depth[static_cast<std::size_t>(y * w + x)] = nearest;
        }

    if (profile) { std::fprintf(stderr, "%s %.3f ms\n", "E", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mark).count()); mark = std::chrono::steady_clock::now(); }
    // See-through things, far first, each blended over what is already there. Blending is
    // per sample, so neighbouring triangles of one sheet do not double up along their edges.
    if (!s.clear.empty()) {
        std::vector<float>& under = s.zclip;
        under.assign(static_cast<std::size_t>(w * h * 16), 0.0F);
        for (std::size_t k = 0; k < static_cast<std::size_t>(w * h); ++k)
            for (int j = 0; j < 4; ++j)
                for (int c = 0; c < 4; ++c)
                    under[k * 16 + static_cast<std::size_t>(j * 4 + c)] = s.colour[k * 4 + static_cast<std::size_t>(c)];
        std::vector<std::pair<double, std::size_t>> order{};
        for (std::size_t t = 0; t < s.clear.size(); ++t) {
            const Tri& tri = s.clear[t];
            order.emplace_back(std::max({s.placed[tri.a].depth, s.placed[tri.b].depth, s.placed[tri.c].depth}), t);
        }
        std::sort(order.begin(), order.end());
        for (const std::pair<double, std::size_t>& entry : order) {
            const Tri& tri = s.clear[entry.second];
            const Placed& A = s.placed[tri.a];
            const Placed& B = s.placed[tri.b];
            const Placed& C = s.placed[tri.c];
            const double ax = A.sx - shot.x0, ay = A.sy - shot.y0;
            const double bx = B.sx - shot.x0, by = B.sy - shot.y0;
            const double cx = C.sx - shot.x0, cy = C.sy - shot.y0;
            const double area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
            if (std::abs(area) < 1e-10)
                continue;
            const double inv = 1 / area;
            const double e0a = (by - cy) * inv, e0b = (cx - bx) * inv, e0c = (bx * cy - by * cx) * inv;
            const double e1a = (cy - ay) * inv, e1b = (ax - cx) * inv, e1c = (cx * ay - cy * ax) * inv;
            // A shared edge belongs to one side only: samples exactly on it go to the triangle
            // whose interior lies to its top or left.
            const bool own0 = e0a > 0 || (e0a == 0 && e0b > 0);
            const bool own1 = e1a > 0 || (e1a == 0 && e1b > 0);
            const double e2a = -e0a - e1a, e2b = -e0b - e1b;
            const bool own2 = e2a > 0 || (e2a == 0 && e2b > 0);
            const int xmin = std::max(0, static_cast<int>(std::floor(std::min({ax, bx, cx}))));
            const int xmax = std::min(w - 1, static_cast<int>(std::floor(std::max({ax, bx, cx}))));
            const int ymin = std::max(0, static_cast<int>(std::floor(std::min({ay, by, cy}))));
            const int ymax = std::min(h - 1, static_cast<int>(std::floor(std::max({ay, by, cy}))));
            for (int y = ymin; y <= ymax; ++y)
                for (int x = xmin; x <= xmax; ++x) {
                    const std::size_t at = static_cast<std::size_t>((y * w + x) * 4);
                    bool hit[4] = {false, false, false, false};
                    int count = 0;
                    double mx = 0, my = 0;
                    for (int k = 0; k < 4; ++k) {
                        const double px = x + sample_x[k];
                        const double py = y + sample_y[k];
                        const double w0 = e0a * px + e0b * py + e0c;
                        const double w1 = e1a * px + e1b * py + e1c;
                        const double w2 = 1 - w0 - w1;
                        if (w0 < 0 || w1 < 0 || w2 < 0)
                            continue;
                        if ((w0 == 0 && !own0) || (w1 == 0 && !own1) || (w2 == 0 && !own2))
                            continue;
                        if (options.clip_ground && C.p.z + (A.p.z - C.p.z) * w0 + (B.p.z - C.p.z) * w1 < 0)
                            continue;
                        const double z = C.depth + (A.depth - C.depth) * w0 + (B.depth - C.depth) * w1;
                        if (z <= s.depth[at + static_cast<std::size_t>(k)])
                            continue;
                        hit[k] = true;
                        mx += px;
                        my += py;
                        ++count;
                    }
                    if (count == 0)
                        continue;
                    mx /= count;
                    my /= count;
                    double w0 = clamp01(e0a * mx + e0b * my + e0c);
                    double w1 = std::clamp(e1a * mx + e1b * my + e1c, 0.0, 1 - w0);
                    const double w2 = 1 - w0 - w1;
                    Fragment f{};
                    f.p = A.p * w0 + B.p * w1 + C.p * w2;
                    f.n = normalized(A.n * w0 + B.n * w1 + C.n * w2);
                    f.o = A.o * w0 + B.o * w1 + C.o * w2;
                    f.u = A.u * w0 + B.u * w1 + C.u * w2;
                    f.v = A.v * w0 + B.v * w1 + C.v * w2;
                    f.ao = A.ao * w0 + B.ao * w1 + C.ao * w2;
                    f.blend = A.blend * w0 + B.blend * w1 + C.blend * w2;
                    f.colour = A.colour * w0 + B.colour * w1 + C.colour * w2;
                f.colour = A.colour * w0 + B.colour * w1 + C.colour * w2;
                    const Material& m = *tri.material;
                    const Look look = surface(m, f, *tri.place, options.time);
                    const float sun = map.light(f.p + f.n * (map.cell * 2.4), map.cell * 2.0);
                    const Lin c = lit(look, m, f, view, sun);
                    const double a = clamp01(look.alpha);
                    const double keep = 1 - a;
                    const double tone_c[3] = {tone(c.r) * a, tone(c.g) * a, tone(c.b) * a};
                    for (int k = 0; k < 4; ++k) {
                        if (!hit[k])
                            continue;
                        float* sample = &under[at * 4 + static_cast<std::size_t>(k * 4)];
                        for (int ch = 0; ch < 3; ++ch)
                            sample[ch] = static_cast<float>(sample[ch] * keep + tone_c[ch]);
                        sample[3] = static_cast<float>(sample[3] * keep + a);
                    }
                }
        }
        for (std::size_t k = 0; k < static_cast<std::size_t>(w * h); ++k)
            for (int c = 0; c < 4; ++c) {
                double sum = 0;
                for (int j = 0; j < 4; ++j)
                    sum += under[k * 16 + static_cast<std::size_t>(j * 4 + c)];
                s.colour[k * 4 + static_cast<std::size_t>(c)] = static_cast<float>(sum / 4);
            }
    }

    if (profile) { std::fprintf(stderr, "F %.3f ms tris %zu solid %zu w %d h %d\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mark).count(), s.placed.size(), s.solid.size(), w, h); }
    shot.px.assign(static_cast<std::size_t>(w * h * 4), 0);
    for (std::size_t k = 0; k < static_cast<std::size_t>(w * h); ++k) {
        const float* c = &s.colour[k * 4];
        shot.px[k * 4] = static_cast<std::uint8_t>(std::lround(255 * std::clamp(c[2], 0.0F, 1.0F)));
        shot.px[k * 4 + 1] = static_cast<std::uint8_t>(std::lround(255 * std::clamp(c[1], 0.0F, 1.0F)));
        shot.px[k * 4 + 2] = static_cast<std::uint8_t>(std::lround(255 * std::clamp(c[0], 0.0F, 1.0F)));
        shot.px[k * 4 + 3] = static_cast<std::uint8_t>(std::lround(255 * std::clamp(c[3], 0.0F, 1.0F)));
    }
}

void paint_shadow(Canvas& canvas, const Shot& shot, int dx, int dy) {
    if (shot.sw <= 0 || shot.shadow.empty())
        return;
    // The shade is a cool dark green, as the garden's other shadows are.
    const int sx0 = shot.sx0 + dx;
    const int sy0 = shot.sy0 + dy;
    const int x_from = std::max(0, -sx0);
    const int x_to = std::min(shot.sw, canvas.w - sx0);
    for (int y = std::max(0, -sy0); y < std::min(shot.sh, canvas.h - sy0); ++y) {
        std::uint8_t* row = canvas.px.data() + (static_cast<std::size_t>(y + sy0) * static_cast<std::size_t>(canvas.w)) * 4U;
        const std::uint8_t* a = shot.shadow.data() + static_cast<std::size_t>(y * shot.sw);
        for (int x = x_from; x < x_to; ++x) {
            const unsigned k = a[x];
            if (k == 0)
                continue;
            std::uint8_t* p = row + static_cast<std::size_t>(x + sx0) * 4U;
            const unsigned keep = 255U - k;
            p[0] = static_cast<std::uint8_t>((p[0] * keep + 4U * k + 127U) / 255U);
            p[1] = static_cast<std::uint8_t>((p[1] * keep + 14U * k + 127U) / 255U);
            p[2] = static_cast<std::uint8_t>((p[2] * keep + 6U * k + 127U) / 255U);
        }
    }
}

void paint(Canvas& canvas, const Shot& shot, int dx, int dy) {
    if (shot.empty())
        return;
    const int x0 = shot.x0 + dx;
    const int y0 = shot.y0 + dy;
    const int x_from = std::max(0, -x0);
    const int x_to = std::min(shot.w, canvas.w - x0);
    for (int y = std::max(0, -y0); y < std::min(shot.h, canvas.h - y0); ++y) {
        std::uint8_t* row = canvas.px.data() + (static_cast<std::size_t>(y + y0) * static_cast<std::size_t>(canvas.w)) * 4U;
        const std::uint8_t* from = shot.px.data() + static_cast<std::size_t>(y * shot.w) * 4U;
        for (int x = x_from; x < x_to; ++x) {
            const std::uint8_t* s = from + static_cast<std::size_t>(x) * 4U;
            const unsigned a = s[3];
            if (a == 0)
                continue;
            std::uint8_t* p = row + static_cast<std::size_t>(x + x0) * 4U;
            const unsigned keep = 255U - a;
            p[0] = static_cast<std::uint8_t>(std::min(255U, s[0] + (p[0] * keep + 127U) / 255U));
            p[1] = static_cast<std::uint8_t>(std::min(255U, s[1] + (p[1] * keep + 127U) / 255U));
            p[2] = static_cast<std::uint8_t>(std::min(255U, s[2] + (p[2] * keep + 127U) / 255U));
            p[3] = static_cast<std::uint8_t>(std::min(255U, a + (p[3] * keep + 127U) / 255U));
        }
    }
}

} // namespace mm
