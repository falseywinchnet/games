// Petrified wood, grown and then broken: a log is modelled as a solid with structure (a
// wandering pith, growth rings of uneven widths, rays running out through them, fibres
// along the grain, a fissured bark), and a chunk of it is freed by breaking it as wood
// breaks, four to seven times, each break its own kind at its own angle:
//   across the grain, in a staircase: each bundle of fibres snaps at its own length, and
//     some tear out long as splinters;
//   along a ray, the split stepping ring by ring;
//   along a ring, the split stepping ray by ray;
//   a rough fracture at any angle;
// and, the wood being silica now, chipped at its edges in broad glassy conchoidal scoops.
// The chunk lies as it would settle, its thinnest way up. It is sampled on a fine grid and
// given a surface (surface nets) whose normals come from the solid, so its facets stay sharp
// and catch the light.
#include "shapes.hpp"

#include "noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace ts {
namespace {

constexpr double pi = 3.14159265358979323846;

double unit_hash(int a, int b, std::uint32_t key) {
    return (hash_signed(a, b, 17, key) + 1) * .5;
}

// Where a point lies in the log's structure: along the grain (s = x), how far out (r, as a
// share of the bark's radius: grown), which ring and which ray.
struct Place {
    double r = 0, angle = 0, grown = 0;
    int ring = 0, ray = 0;
};

struct Break {
    int kind = 0;              // 0 across the grain, 1 along a ray, 2 along a ring, 3 rough
    double n[3] = {1, 0, 0};   // the side of the plane that is broken away
    double offset = 0;         // from the chunk's middle
    double ring_at = 0;        // a ring split's radius
    bool outer = true;         // the ring split keeps what is inside it
    double size = .3;          // a chip's sphere
    double c[3] = {0, 0, 0};   // a chip's centre
};

struct Log final : Solid {
    std::uint32_t key = 1;
    double radius = .9;
    double rings = 16;
    double rays = 24;
    double wander = .08;
    double step = .09;
    double splinter = .1;
    double fissure = .03;
    double middle[3] = {0, 0, 0};  // the chunk's middle, in the log's frame
    std::vector<Break> breaks;
    std::vector<Break> chips;

    Place place(double x, double y, double z) const {
        const double py = wander * noise2(x * 1.3, .5, key + 1);
        const double pz = wander * noise2(x * 1.3, 3.5, key + 2);
        const double du = y - py;
        const double dv = z - pz;
        Place p;
        p.r = std::sqrt(du * du + dv * dv);
        p.angle = std::atan2(dv, du);
        p.grown = p.r / radius + .025 * noise2(p.angle * 2, x * .8, key + 3);
        const double ring = p.grown * rings + 1.5 * noise2(p.grown * 3, p.angle * .5, key + 4);
        p.ring = static_cast<int>(std::floor(ring));
        p.ray = static_cast<int>(std::floor((p.angle + pi) / (2 * pi) * rays));
        return p;
    }

    // Negative inside the chunk, positive outside; roughly the distance. In the log's frame.
    double density(double x, double y, double z) const override {
        const Place p = place(x, y, z);
        // The bark: lumpy, fissured into plates along the grain.
        const double plates = std::abs(std::sin(p.angle * 7 + .6 * noise2(x * 1.4, p.angle, key + 5)));
        const double bark = radius * (1 + .05 * noise2(p.angle * 1.5, x * .7, key + 6)) - fissure * std::pow(1 - plates, 6);
        double d = p.r - bark;
        const double mx = x - middle[0];
        const double my = y - middle[1];
        const double mz = z - middle[2];
        for (const Break& b : breaks) {
            const double plane = mx * b.n[0] + my * b.n[1] + mz * b.n[2] - b.offset;
            double face = plane;
            if (b.kind == 0) {
                // Bundles (wedges between rays, stepping ring by ring) snap at their own
                // lengths along the grain; some tear out long.
                const int bundle_ray = p.ray / 2;
                const int bundle_ring = p.ring / 3;
                const double stagger = step * ((unit_hash(bundle_ray, 1, key + 7 + static_cast<std::uint32_t>(b.offset * 1000)) - .5) * 2 +
                                               .35 * (unit_hash(bundle_ring, bundle_ray, key + 14) - .5));
                const double torn = unit_hash(bundle_ray, bundle_ring, key + 8) < splinter
                                        ? .3 * unit_hash(bundle_ring, bundle_ray, key + 9) * (1 - std::min(1.0, p.grown))
                                        : 0.0;
                face = plane - (stagger + torn) * std::abs(b.n[0]);
            } else if (b.kind == 1) {
                // Along a ray, stepping where the split jumped a ring.
                face = plane + .03 * (unit_hash(p.ring, 2, key + 15) - .5);
            } else if (b.kind == 2) {
                // Along a ring, stepping where it jumped a ray.
                const double jag = .025 * (unit_hash(p.ray / 2, 3, key + 16) - .5);
                face = b.outer ? (p.r - b.ring_at + jag) : (b.ring_at - p.r + jag);
            } else {
                face = plane + .03 * fbm3(x * 4, y * 4, z * 4, key + 17, 3);
            }
            d = std::max(d, face);
        }
        for (const Break& chip : chips) {
            const double cx = x - chip.c[0];
            const double cy = y - chip.c[1];
            const double cz = z - chip.c[2];
            d = std::max(d, chip.size - std::sqrt(cx * cx + cy * cy + cz * cz));
        }
        return d;
    }
};

Log make_log(const Shape::Measures& m) {
    const std::array<double, 16>& f = m.feature;
    Log log;
    log.key = static_cast<std::uint32_t>(f[4]);
    log.radius = f[1];
    log.rings = f[2];
    log.rays = f[3];
    log.step = f[5];
    log.splinter = f[6];
    log.wander = f[8];
    log.fissure = f[9];
    // The chunk's middle: somewhere between pith and bark.
    const double out = f[10] * log.radius;
    log.middle[0] = 0;
    log.middle[1] = out * std::cos(f[11]);
    log.middle[2] = out * std::sin(f[11]);
    const double reach = f[12];
    // Its breaks: two across the grain, one each side, then the rest of any kind.
    const int count = static_cast<int>(f[0]);
    for (int k = 0; k < count; ++k) {
        Break b;
        const double h0 = unit_hash(k, 31, log.key);
        const double h1 = unit_hash(k, 32, log.key);
        const double h2 = unit_hash(k, 33, log.key);
        b.kind = k < 2 ? 0 : static_cast<int>(h0 * 4) % 4;
        b.offset = reach * (.75 + .5 * h1);
        if (b.kind == 0) {
            // Across the grain, tilted up to forty degrees.
            const double tilt = (h2 - .5) * 1.4;
            const double round = 2 * pi * unit_hash(k, 34, log.key);
            const double side = k % 2 == 0 ? 1.0 : -1.0;
            b.n[0] = side * std::cos(tilt);
            b.n[1] = std::sin(tilt) * std::cos(round);
            b.n[2] = std::sin(tilt) * std::sin(round);
            b.offset = reach * (1.1 + .7 * h1);
        } else if (b.kind == 1) {
            // Along a ray: a plane holding the grain's direction.
            const double angle = 2 * pi * h2;
            b.n[0] = 0;
            b.n[1] = std::cos(angle);
            b.n[2] = std::sin(angle);
        } else if (b.kind == 2) {
            // Along a ring, inside or outside the chunk's middle.
            const double here = std::sqrt(log.middle[1] * log.middle[1] + log.middle[2] * log.middle[2]);
            b.outer = h2 > .4;
            b.ring_at = b.outer ? here + reach * (.6 + .5 * h1) : std::max(.05, here - reach * (.6 + .5 * h1));
        } else {
            // A rough fracture at any angle.
            const double zz = h2 * 2 - 1;
            const double a = 2 * pi * unit_hash(k, 35, log.key);
            const double rr = std::sqrt(std::max(0.0, 1 - zz * zz));
            b.n[0] = rr * std::cos(a) * .6;
            b.n[1] = rr * std::sin(a);
            b.n[2] = zz;
            const double l = std::sqrt(b.n[0] * b.n[0] + b.n[1] * b.n[1] + b.n[2] * b.n[2]);
            for (double& v : b.n)
                v /= l;
        }
        log.breaks.push_back(b);
    }
    // Glassy chips: a big sphere grazing the chunk's surface, found by walking out from the
    // middle until the solid ends.
    const int chips = static_cast<int>(f[7]);
    for (int k = 0; k < chips; ++k) {
        const double zz = unit_hash(k, 41, log.key) * 2 - 1;
        const double a = 2 * pi * unit_hash(k, 42, log.key);
        const double rr = std::sqrt(std::max(0.0, 1 - zz * zz));
        const double dir[3] = {rr * std::cos(a), rr * std::sin(a), zz};
        double at = 0;
        for (int step = 1; step < 200; ++step) {
            at = step * .01;
            if (log.density(log.middle[0] + dir[0] * at, log.middle[1] + dir[1] * at, log.middle[2] + dir[2] * at) > 0)
                break;
        }
        Break chip;
        chip.size = .4 + .4 * unit_hash(k, 43, log.key);
        const double cut = .04 + .06 * unit_hash(k, 44, log.key);
        const double reach_out = at + chip.size - cut;
        chip.c[0] = log.middle[0] + dir[0] * reach_out;
        chip.c[1] = log.middle[1] + dir[1] * reach_out;
        chip.c[2] = log.middle[2] + dir[2] * reach_out;
        log.chips.push_back(chip);
    }
    return log;
}

// The eigenvectors of a symmetric 3 x 3 matrix by Jacobi rotations, as the columns of v,
// with their values in w.
void eigen(double a[3][3], double v[3][3], double w[3]) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            v[i][j] = i == j ? 1 : 0;
    for (int sweep = 0; sweep < 24; ++sweep) {
        for (int p = 0; p < 3; ++p)
            for (int q = p + 1; q < 3; ++q) {
                if (std::abs(a[p][q]) < 1e-12)
                    continue;
                const double theta = (a[q][q] - a[p][p]) / (2 * a[p][q]);
                const double t = (theta >= 0 ? 1 : -1) / (std::abs(theta) + std::sqrt(theta * theta + 1));
                const double c = 1 / std::sqrt(t * t + 1);
                const double s = t * c;
                for (int k = 0; k < 3; ++k) {
                    const double akp = a[k][p];
                    const double akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < 3; ++k) {
                    const double apk = a[p][k];
                    const double aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < 3; ++k) {
                    const double vkp = v[k][p];
                    const double vkq = v[k][q];
                    v[k][p] = c * vkp - s * vkq;
                    v[k][q] = s * vkp + c * vkq;
                }
            }
    }
    for (int i = 0; i < 3; ++i)
        w[i] = a[i][i];
}

// The chunk as it lies: the log's frame turned so the chunk's thinnest way is up.
struct Lying {
    const Solid* solid = nullptr;
    double centre[3] = {0, 0, 0};  // the chunk's centre, log frame
    double axes[3][3] = {};        // rows: the lying frame's x, y, z in the log frame
    double density(double x, double y, double z) const {
        const double lx = centre[0] + axes[0][0] * x + axes[1][0] * y + axes[2][0] * z;
        const double ly = centre[1] + axes[0][1] * x + axes[1][1] * y + axes[2][1] * z;
        const double lz = centre[2] + axes[0][2] * x + axes[1][2] * y + axes[2][2] * z;
        return (*solid).density(lx, ly, lz);
    }
};

}  // namespace

void Shape::grow_wood() {
    const Log log = make_log(measures_);
    const double grain[3] = {1, 0, 0};
    const double pith[3] = {0, 0, 0};
    grow_from(log, log.middle, 1.6, grain, pith);
}

void Shape::grow_from(const Solid& solid, const double middle[3], double box_half, const double grain[3], const double pith[3]) {
    // Find the chunk: sample a box round its middle, gather what is inside, and take its
    // centre and its principal axes.
    double sum[3] = {0, 0, 0};
    double cov[3][3] = {};
    std::vector<std::array<double, 3>> inside;
    constexpr int probe = 26;
    const double box = box_half;
    for (int k = 0; k < probe; ++k)
        for (int j = 0; j < probe; ++j)
            for (int i = 0; i < probe; ++i) {
                const double x = middle[0] + (-1 + 2 * (i + .5) / probe) * box;
                const double y = middle[1] + (-1 + 2 * (j + .5) / probe) * box;
                const double z = middle[2] + (-1 + 2 * (k + .5) / probe) * box;
                if (solid.density(x, y, z) < 0)
                    inside.push_back({x, y, z});
            }
    if (inside.empty())
        inside.push_back({middle[0], middle[1], middle[2]});
    for (const std::array<double, 3>& p : inside)
        for (int a = 0; a < 3; ++a)
            sum[a] += p[static_cast<std::size_t>(a)];
    const double samples = static_cast<double>(inside.size());
    const double centre[3] = {sum[0] / samples, sum[1] / samples, sum[2] / samples};
    for (const std::array<double, 3>& p : inside)
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b)
                cov[a][b] += (p[static_cast<std::size_t>(a)] - centre[a]) * (p[static_cast<std::size_t>(b)] - centre[b]) / samples;
    double vectors[3][3];
    double values3[3];
    eigen(cov, vectors, values3);
    // Order the axes longest first: x the chunk's length, z its thinnest, which lies up.
    int order[3] = {0, 1, 2};
    for (int a = 0; a < 3; ++a)
        for (int b = a + 1; b < 3; ++b)
            if (values3[order[b]] > values3[order[a]])
                std::swap(order[a], order[b]);
    Lying lying;
    lying.solid = &solid;
    for (int a = 0; a < 3; ++a)
        lying.centre[a] = centre[a];
    for (int row = 0; row < 3; ++row)
        for (int a = 0; a < 3; ++a)
            lying.axes[row][a] = vectors[a][order[row]];
    // A right-handed frame.
    const double cross_x = lying.axes[0][1] * lying.axes[1][2] - lying.axes[0][2] * lying.axes[1][1];
    const double cross_y = lying.axes[0][2] * lying.axes[1][0] - lying.axes[0][0] * lying.axes[1][2];
    const double cross_z = lying.axes[0][0] * lying.axes[1][1] - lying.axes[0][1] * lying.axes[1][0];
    lying.axes[2][0] = cross_x;
    lying.axes[2][1] = cross_y;
    lying.axes[2][2] = cross_z;
    // Its extent along each axis, for the grid.
    double extent[3] = {.2, .2, .2};
    for (const std::array<double, 3>& p : inside)
        for (int row = 0; row < 3; ++row) {
            const double along = (p[0] - centre[0]) * lying.axes[row][0] + (p[1] - centre[1]) * lying.axes[row][1] +
                                 (p[2] - centre[2]) * lying.axes[row][2];
            extent[row] = std::max(extent[row], std::abs(along));
        }
    // The grain's direction and the pith, as the chunk lies (before the family's turn and
    // scale, applied below with the rest).
    double grain_lying[3] = {0, 0, 0};
    double pith_lying[3] = {0, 0, 0};
    for (int row = 0; row < 3; ++row) {
        grain_lying[row] = grain[0] * lying.axes[row][0] + grain[1] * lying.axes[row][1] + grain[2] * lying.axes[row][2];
        pith_lying[row] = (pith[0] - centre[0]) * lying.axes[row][0] + (pith[1] - centre[1]) * lying.axes[row][1] +
                          (pith[2] - centre[2]) * lying.axes[row][2];
    }

    // The grid over the lying chunk.
    constexpr int n = 84;
    const double half_x = extent[0] + .12;
    const double half_y = extent[1] + .12;
    const double half_z = extent[2] + .12;
    const double hx = 2 * half_x / n;
    const double hy = 2 * half_y / n;
    const double hz = 2 * half_z / n;
    const std::size_t side = n + 1;
    std::vector<float> values(side * side * side);
    for (int k = 0; k <= n; ++k)
        for (int j = 0; j <= n; ++j)
            for (int i = 0; i <= n; ++i) {
                const double x = -half_x + i * hx;
                const double y = -half_y + j * hy;
                const double z = -half_z + k * hz;
                const bool edge = i == 0 || j == 0 || k == 0 || i == n || j == n || k == n;
                values[(static_cast<std::size_t>(k) * side + static_cast<std::size_t>(j)) * side + static_cast<std::size_t>(i)] =
                    edge ? 1.0F : static_cast<float>(lying.density(x, y, z));
            }
    // Surface nets: a vertex in every cell the surface crosses, at the mean of its edges'
    // crossings; a quad across every grid edge that changes sign.
    std::vector<int> cell_vertex(static_cast<std::size_t>(n) * n * n, -1);
    std::vector<double> points;
    const int corners[8][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}};
    const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (int k = 0; k < n; ++k)
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                double v[8];
                int inside = 0;
                for (int c = 0; c < 8; ++c) {
                    v[c] = values[(static_cast<std::size_t>(k + corners[c][2]) * side + static_cast<std::size_t>(j + corners[c][1])) * side +
                                  static_cast<std::size_t>(i + corners[c][0])];
                    if (v[c] < 0)
                        ++inside;
                }
                if (inside == 0 || inside == 8)
                    continue;
                double sx = 0;
                double sy = 0;
                double sz = 0;
                int crossings = 0;
                for (const int* e : edges) {
                    const double a = v[e[0]];
                    const double b = v[e[1]];
                    if ((a < 0) == (b < 0))
                        continue;
                    const double t = a / (a - b);
                    sx += corners[e[0]][0] + (corners[e[1]][0] - corners[e[0]][0]) * t;
                    sy += corners[e[0]][1] + (corners[e[1]][1] - corners[e[0]][1]) * t;
                    sz += corners[e[0]][2] + (corners[e[1]][2] - corners[e[0]][2]) * t;
                    ++crossings;
                }
                cell_vertex[(static_cast<std::size_t>(k) * n + static_cast<std::size_t>(j)) * n + static_cast<std::size_t>(i)] =
                    static_cast<int>(points.size() / 3);
                points.push_back(-half_x + (i + sx / crossings) * hx);
                points.push_back(-half_y + (j + sy / crossings) * hy);
                points.push_back(-half_z + (k + sz / crossings) * hz);
            }
    std::vector<std::uint32_t> faces;
    for (int k = 1; k < n; ++k)
        for (int j = 1; j < n; ++j)
            for (int i = 1; i < n; ++i) {
                const float here = values[(static_cast<std::size_t>(k) * side + static_cast<std::size_t>(j)) * side + static_cast<std::size_t>(i)];
                for (int axis = 0; axis < 3; ++axis) {
                    const int ni = i + (axis == 0 ? 1 : 0);
                    const int nj = j + (axis == 1 ? 1 : 0);
                    const int nk = k + (axis == 2 ? 1 : 0);
                    if (ni > n || nj > n || nk > n)
                        continue;
                    const float there = values[(static_cast<std::size_t>(nk) * side + static_cast<std::size_t>(nj)) * side + static_cast<std::size_t>(ni)];
                    if ((here < 0) == (there < 0))
                        continue;
                    // The four cells round this edge.
                    int around[4][3];
                    if (axis == 0) {
                        const int cells[4][3] = {{i, j - 1, k - 1}, {i, j, k - 1}, {i, j, k}, {i, j - 1, k}};
                        std::copy(&cells[0][0], &cells[0][0] + 12, &around[0][0]);
                    } else if (axis == 1) {
                        const int cells[4][3] = {{i - 1, j, k - 1}, {i - 1, j, k}, {i, j, k}, {i, j, k - 1}};
                        std::copy(&cells[0][0], &cells[0][0] + 12, &around[0][0]);
                    } else {
                        const int cells[4][3] = {{i - 1, j - 1, k}, {i, j - 1, k}, {i, j, k}, {i - 1, j, k}};
                        std::copy(&cells[0][0], &cells[0][0] + 12, &around[0][0]);
                    }
                    int quad[4];
                    bool whole = true;
                    for (int q = 0; q < 4; ++q) {
                        const int ci = around[q][0];
                        const int cj = around[q][1];
                        const int ck = around[q][2];
                        quad[q] = cell_vertex[(static_cast<std::size_t>(ck) * n + static_cast<std::size_t>(cj)) * n + static_cast<std::size_t>(ci)];
                        whole = whole && quad[q] >= 0;
                    }
                    if (!whole)
                        continue;
                    // Wound so the face looks out of the solid.
                    if (here < 0) {
                        faces.insert(faces.end(), {static_cast<std::uint32_t>(quad[0]), static_cast<std::uint32_t>(quad[1]),
                                                   static_cast<std::uint32_t>(quad[2]), static_cast<std::uint32_t>(quad[0]),
                                                   static_cast<std::uint32_t>(quad[2]), static_cast<std::uint32_t>(quad[3])});
                    } else {
                        faces.insert(faces.end(), {static_cast<std::uint32_t>(quad[0]), static_cast<std::uint32_t>(quad[2]),
                                                   static_cast<std::uint32_t>(quad[1]), static_cast<std::uint32_t>(quad[0]),
                                                   static_cast<std::uint32_t>(quad[3]), static_cast<std::uint32_t>(quad[2])});
                    }
                }
            }

    // The columns: for a point in the tray's plane, the solid's top and bottom, read from
    // the grid. The piece is turned in the tray by the family's turn.
    const double turn_c = std::cos(measures_.turn);
    const double turn_s = std::sin(measures_.turn);
    // Grid value at a column (fractional i, j) and level k, bilinear across the column.
    struct Column {
        const std::vector<float>* values;
        std::size_t side;
        int n;
        double fi, fj;
        double at(int k) const {
            const int i0 = std::clamp(static_cast<int>(fi), 0, n - 1);
            const int j0 = std::clamp(static_cast<int>(fj), 0, n - 1);
            const double tx = std::clamp(fi - i0, 0.0, 1.0);
            const double ty = std::clamp(fj - j0, 0.0, 1.0);
            const std::size_t base = (static_cast<std::size_t>(k) * side + static_cast<std::size_t>(j0)) * side + static_cast<std::size_t>(i0);
            const double a = (*values)[base] + ((*values)[base + 1] - (*values)[base]) * tx;
            const double b = (*values)[base + side] + ((*values)[base + side + 1] - (*values)[base + side]) * tx;
            return a + (b - a) * ty;
        }
    };
    double z_low = 1e9;
    double z_high = -1e9;
    for (std::size_t p = 0; p + 2 < points.size(); p += 3) {
        z_low = std::min(z_low, points[p + 2]);
        z_high = std::max(z_high, points[p + 2]);
    }
    const double level = (z_low + z_high) * .5;
    // How far the solid reaches round the tray's plane: object points are turned back into
    // the grid's frame and their columns read.
    const int levels = n;
    std::vector<double> reach(sectors, 0.0);
    for (int jj = 0; jj < sectors; ++jj) {
        const double angle = 2 * pi * jj / sectors;
        const double ca = std::cos(angle);
        const double sa = std::sin(angle);
        double found = 0;
        for (int step = 400; step > 0; --step) {
            const double r = step * .005;
            const double ox = r * ca;
            const double oy = r * sa;
            const double gx = ox * turn_c + oy * turn_s;
            const double gy = -ox * turn_s + oy * turn_c;
            const Column column{&values, side, n, (gx + half_x) / hx, (gy + half_y) / hy};
            if (column.fi < 0 || column.fj < 0 || column.fi > n || column.fj > n)
                continue;
            bool solid = false;
            for (int k = 1; k < levels && !solid; ++k)
                solid = column.at(k) < 0;
            if (solid) {
                found = r;
                break;
            }
        }
        reach[static_cast<std::size_t>(jj)] = std::max(found, .05);
    }
    // Scaled to fill the socket, as every shape is.
    double fit = 1e9;
    for (int jj = 0; jj < sectors; ++jj)
        fit = std::min(fit, socket_reach(2 * pi * jj / sectors) / reach[static_cast<std::size_t>(jj)]);
    const double scale = fit * .97;
    scale_ = scale;
    outline_.assign(sectors, 0.0F);
    for (int jj = 0; jj < sectors; ++jj)
        outline_[static_cast<std::size_t>(jj)] = static_cast<float>(reach[static_cast<std::size_t>(jj)] * scale);
    ring_u_.assign(rings + 1, 0.0F);
    for (int i = 0; i <= rings; ++i)
        ring_u_[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(pi * .5 * i / rings));
    const std::size_t count = static_cast<std::size_t>((rings + 1) * sectors);
    top_.assign(count, 0.0F);
    bottom_.assign(count, 0.0F);
    top_peak_ = 0;
    bottom_peak_ = 0;
    for (int i = 0; i <= rings; ++i) {
        const double u = ring_u_[static_cast<std::size_t>(i)];
        for (int jj = 0; jj < sectors; ++jj) {
            const double angle = 2 * pi * jj / sectors;
            const double r = u * reach[static_cast<std::size_t>(jj)] * .995;
            const double ox = r * std::cos(angle);
            const double oy = r * std::sin(angle);
            const double gx = ox * turn_c + oy * turn_s;
            const double gy = -ox * turn_s + oy * turn_c;
            const Column column{&values, side, n, (gx + half_x) / hx, (gy + half_y) / hy};
            double top = 0;
            double bottom = 0;
            bool seen = false;
            for (int k = levels - 1; k >= 1; --k) {
                const double a = column.at(k);
                const double b = column.at(k - 1);
                if (a >= 0 && b < 0) {
                    top = -half_z + (k - a / (a - b)) * hz - level;
                    seen = true;
                    break;
                }
            }
            for (int k = 1; k < levels && seen; ++k) {
                const double a = column.at(k - 1);
                const double b = column.at(k);
                if (a >= 0 && b < 0) {
                    bottom = -(-half_z + (k - 1 + a / (a - b)) * hz - level);
                    break;
                }
            }
            if (i == rings || !seen) {
                top = 0;
                bottom = 0;
            }
            const double least = .04;
            if (top + bottom < least && i < rings)
                bottom = least - top;
            const std::size_t at = static_cast<std::size_t>(i * sectors + jj);
            top_[at] = static_cast<float>(top * scale);
            bottom_[at] = static_cast<float>(bottom * scale);
            top_peak_ = std::max(top_peak_, top * scale);
            bottom_peak_ = std::max(bottom_peak_, bottom * scale);
        }
    }

    // The surface, turned and scaled into the tray, its normals from the solid itself.
    vertices_.clear();
    triangles_ = faces;
    const double e = std::min({hx, hy, hz}) * .5;
    for (std::size_t p = 0; p + 2 < points.size(); p += 3) {
        const double gx = points[p];
        const double gy = points[p + 1];
        const double gz = points[p + 2];
        double nx = lying.density(gx + e, gy, gz) - lying.density(gx - e, gy, gz);
        double ny = lying.density(gx, gy + e, gz) - lying.density(gx, gy - e, gz);
        double nz = lying.density(gx, gy, gz + e) - lying.density(gx, gy, gz - e);
        const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (length > 0) {
            nx /= length;
            ny /= length;
            nz /= length;
        } else {
            nz = 1;
        }
        ShapeVertex vertex;
        vertex.x = static_cast<float>((gx * turn_c - gy * turn_s) * scale);
        vertex.y = static_cast<float>((gx * turn_s + gy * turn_c) * scale);
        vertex.z = static_cast<float>((gz - level) * scale);
        vertex.nx = static_cast<float>(nx * turn_c - ny * turn_s);
        vertex.ny = static_cast<float>(nx * turn_s + ny * turn_c);
        vertex.nz = static_cast<float>(nz);
        vertices_.push_back(vertex);
    }
    grown_ = true;
    grain_[0] = grain_lying[0] * turn_c - grain_lying[1] * turn_s;
    grain_[1] = grain_lying[0] * turn_s + grain_lying[1] * turn_c;
    grain_[2] = grain_lying[2];
    pith_[0] = (pith_lying[0] * turn_c - pith_lying[1] * turn_s) * scale;
    pith_[1] = (pith_lying[0] * turn_s + pith_lying[1] * turn_c) * scale;
    pith_[2] = (pith_lying[2] - level) * scale;

    // The solid's own fine relief where each face is seen, at bake_detail's texels: the
    // point on the face taken back through the turn, the scale and the lying frame.
    constexpr int texels = detail_side + 4;
    const double texel = 2 * ts::reach / detail_side;
    for (int face = 0; face < 2; ++face) {
        std::vector<float>& relief = relief_[face];
        relief.assign(static_cast<std::size_t>(texels * texels), 0.0F);
        bool any = false;
        for (int row = 0; row < texels; ++row)
            for (int column = 0; column < texels; ++column) {
                const double x = (column - 2 + .5) * texel - ts::reach;
                const double y = ts::reach - (row - 2 + .5) * texel;
                double h = 0;
                if (!height(face, x, y, h))
                    continue;
                const double z = face == 0 ? h : -h;
                const double gx = (x * turn_c + y * turn_s) / scale;
                const double gy = (-x * turn_s + y * turn_c) / scale;
                const double gz = z / scale + level;
                const double sx = lying.centre[0] + lying.axes[0][0] * gx + lying.axes[1][0] * gy + lying.axes[2][0] * gz;
                const double sy = lying.centre[1] + lying.axes[0][1] * gx + lying.axes[1][1] * gy + lying.axes[2][1] * gz;
                const double sz = lying.centre[2] + lying.axes[0][2] * gx + lying.axes[1][2] * gy + lying.axes[2][2] * gz;
                const double r = solid.relief(sx, sy, sz) * scale;
                relief[static_cast<std::size_t>(row * texels + column)] = static_cast<float>(r);
                any = any || r != 0;
            }
        if (!any)
            relief.clear();
    }
}

}  // namespace ts
