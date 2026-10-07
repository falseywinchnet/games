#include "terrain.hpp"
#include "run.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace zc {
double smoothstep(double a, double b, double x) {
    const double t = std::min(1.0, std::max(0.0, (x - a) / (b - a)));
    return t * t * (3 - 2 * t);
}

std::uint32_t hash2(int x, int y, std::uint32_t seed) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

double hash_unit(int x, int y, std::uint32_t seed) {
    return static_cast<double>(hash2(x, y, seed) & 0xffffu) / 65535.0;
}

// smooth value noise in [0, 1]
double value_noise(double x, double y, std::uint32_t seed) {
    const double fx = std::floor(x);
    const double fy = std::floor(y);
    const int ix = static_cast<int>(fx);
    const int iy = static_cast<int>(fy);
    const double tx = x - fx;
    const double ty = y - fy;
    const double u = tx * tx * (3 - 2 * tx);
    const double v = ty * ty * (3 - 2 * ty);
    const double a = hash_unit(ix, iy, seed);
    const double b = hash_unit(ix + 1, iy, seed);
    const double c = hash_unit(ix, iy + 1, seed);
    const double d = hash_unit(ix + 1, iy + 1, seed);
    const double top = a + (b - a) * u;
    const double bottom = c + (d - c) * u;
    return top + (bottom - top) * v;
}

// the bedrock ledge's outline: radius at angle a round the stack's spot
double ledge_radius(double a) {
    const double wobble = 0.16 * (value_noise(std::cos(a) * 1.8 + 5, std::sin(a) * 1.8 + 5, 401u) - 0.5) +
                          0.06 * (value_noise(std::cos(a) * 5 + 9, std::sin(a) * 5 + 9, 403u) - 0.5);
    return 0.44 * (1 + wobble);
}

bool on_ledge(double x, double y, double margin) {
    const SiteLayout& layout = site_layout();
    const double dx = x - layout.stack_centre.x;
    const double dy = (y - layout.stack_centre.y) / 0.74;
    const double a = std::atan2(dy, dx);
    return std::sqrt(dx * dx + dy * dy) < ledge_radius(a) + margin;
}

// What a terrain patch is.


// The bank: river gravel round the bedrock ledge, darker and dipping toward
// the brook, rising into stony slopes away from the work.
// The brook's banks wander. The near edge keeps its distance by the work
// (it may only swing out a little there); both wander freely beyond.
double brook_near_at(double x) {
    const SiteLayout& layout = site_layout();
    const double free = smoothstep(0.9, 2.0, std::abs(x));
    return layout.brook_near + 0.025 * std::sin(x * 2.1 + 0.4) + 0.07 * free * std::sin(x * 0.85 + 1.3) + 0.03 * free * std::sin(x * 2.9 + 2.2);
}

double brook_far_at(double x) {
    const SiteLayout& layout = site_layout();
    return layout.brook_far + 0.08 * std::sin(x * 0.9 + 1.7) + 0.04 * std::sin(x * 2.3 + 0.5) + 0.015 * std::sin(x * 6.1);
}

double terrain_height(Terrain kind, double x, double y) {
    const SiteLayout& layout = site_layout();
    if (kind == Terrain::bank) {
        const double fine = 0.010 * (value_noise(x * 7, y * 7, 3u) - 0.5) + 0.016 * (value_noise(x * 1.6, y * 1.6, 7u) - 0.5);
        const double edge = brook_near_at(x);
        // down to the waterline, then on under the water, below the bed
        const double to_water = -0.075 * smoothstep(edge - 0.22, edge + 0.02, y) - 0.07 * smoothstep(edge, edge + 0.14, y);
        const double dx = std::max(0.0, std::abs(x - 0.0) - 1.3);
        const double dy = std::max(0.0, -0.75 - y);
        const double away = std::sqrt(dx * dx + dy * dy);
        // the slopes round the work flatten out toward the water
        const double slopes = 0.30 * smoothstep(0.0, 1.6, away) * (0.7 + 0.6 * value_noise(x * 0.8, y * 0.8, 9u)) *
                              (1 - smoothstep(brook_near_at(x) - 0.45, brook_near_at(x) - 0.05, y));
        // under the bedrock ledge the gravel sits well below its top, so it never shows through
        const double under = on_ledge(x, y, 0.08) ? -0.03 : 0.0;
        return fine - 0.004 + to_water + slopes + under;
    }
    if (kind == Terrain::brook_bed) {
        return layout.water_level - 0.07 + 0.025 * (value_noise(x * 4, y * 4, 5u) - 0.5);
    }
    // far bank: up from the water into a grassy slope
    const double edge = brook_far_at(x);
    // out of the water in a short lip, then up the grassy slope
    const double lip = 0.05 * smoothstep(edge - 0.03, edge + 0.10, y);
    const double rise = 0.24 * smoothstep(edge, edge + 1.4, y);
    const double under = -0.09 * (1 - smoothstep(edge - 0.16, edge, y));
    return layout.water_level - 0.025 + lip + rise + under + 0.06 * (value_noise(x * 0.9, y * 0.9, 13u) - 0.5) * smoothstep(edge + 0.1, edge + 0.6, y);
}


phys::Vec3 ledge_rim(int index) {
    const double a = 6.28318530717958647692 * index / 48;
    const double radius = ledge_radius(a);
    const phys::Vec3 centre = site_layout().stack_centre;
    return phys::Vec3{centre.x + radius * std::cos(a), centre.y + radius * std::sin(a) * 0.74, 0};
}

phys::Vec3 ledge_foot(int index) {
    const double a = 6.28318530717958647692 * index / 48;
    const double radius = ledge_radius(a) + 0.035 + 0.02 * hash_unit(index, 1, 409u);
    const phys::Vec3 centre = site_layout().stack_centre;
    return phys::Vec3{centre.x + radius * std::cos(a), centre.y + radius * std::sin(a) * 0.74, -0.045};
}

namespace {
// The rendered triangles, indexed once into small XY cells. A contact query
// visits only its cell; no terrain bodies enter the all-pairs broad phase.
struct GroundTriangle {
    phys::Vec3 a, ab, ac, normal;
    double inverse_area;
    bool contains(double x, double y) const {
        const double dx = x - a.x, dy = y - a.y;
        const double u = (dx * ac.y - dy * ac.x) * inverse_area;
        const double v = (ab.x * dy - ab.y * dx) * inverse_area;
        return u >= -1e-9 && v >= -1e-9 && u + v <= 1 + 1e-9;
    }
    double height(double x, double y) const {
        return a.z - ((x - a.x) * normal.x + (y - a.y) * normal.y) / normal.z;
    }
};

class WorksiteGround final : public phys::GroundSurface {
public:
    WorksiteGround() : cells_(columns * rows) {
        const SiteLayout& layout = site_layout();
        const double near_end = layout.brook_near + 0.26;
        add_patch(Terrain::bank, -1.5, 1.5, -1.1, near_end, 0.05, false);
        add_patch(Terrain::bank, -4.5, 4.5, -3.4, near_end, 0.2, true);
        add_patch(Terrain::brook_bed, -4.5, 4.5, layout.brook_near - 0.12, layout.brook_far + 0.18, 0.1, false);
        add_patch(Terrain::far_bank, -4.5, 4.5, layout.brook_far - 0.3, 4.8, 0.12, false);
        for (int k = 0; k < 48; k += 1) {
            const int next = (k + 1) % 48;
            add_triangle(layout.stack_centre, ledge_rim(k), ledge_rim(next));
            add_triangle(ledge_rim(k), ledge_foot(k), ledge_foot(next));
            add_triangle(ledge_rim(k), ledge_foot(next), ledge_rim(next));
        }
    }

    phys::GroundSample sample(double x, double y) const override {
        // Outside the scenery is below the playable world, never an invisible
        // platform at pad height. Crane reach stays well inside these bounds.
        phys::GroundSample best{-1, phys::Vec3{0, 0, 1}};
        if (x < x0 || x > 4.5 || y < y0 || y > 4.8) return best;
        const std::vector<std::size_t>& cell = cells_[cell_y(y) * columns + cell_x(x)];
        for (std::size_t index : cell) {
            const GroundTriangle& triangle = triangles_[index];
            if (triangle.contains(x, y)) {
                const double height = triangle.height(x, y);
                if (height > best.height) best = phys::GroundSample{height, triangle.normal};
            }
        }
        return best;
    }

    double raycast(phys::Vec3 origin, phys::Vec3 direction, double max_t) const override {
        double best = max_t;
        // Picking happens on input, not in the simulation loop. Use the very
        // same triangles as contacts, including the ledge skirt and river bed.
        for (const GroundTriangle& triangle : triangles_) {
            const double rate = phys::dot(direction, triangle.normal);
            if (std::abs(rate) < 1e-12) continue;
            const double t = phys::dot(triangle.a - origin, triangle.normal) / rate;
            if (t < 0 || t >= best) continue;
            const phys::Vec3 point = origin + direction * t;
            if (triangle.contains(point.x, point.y)) best = t;
        }
        return best;
    }

private:
    static constexpr double x0 = -4.5, y0 = -3.4, cell_size = 0.05;
    static constexpr int columns = 180, rows = 164;
    std::vector<GroundTriangle> triangles_;
    std::vector<std::vector<std::size_t>> cells_;
    static int cell_x(double x) { return std::clamp(static_cast<int>(std::floor((x - x0) / cell_size)), 0, columns - 1); }
    static int cell_y(double y) { return std::clamp(static_cast<int>(std::floor((y - y0) / cell_size)), 0, rows - 1); }

    void add_triangle(phys::Vec3 a, phys::Vec3 b, phys::Vec3 c) {
        const phys::Vec3 ab = b - a, ac = c - a;
        const double area = ab.x * ac.y - ab.y * ac.x;
        if (std::abs(area) < 1e-12) return;
        phys::Vec3 normal = phys::normalized(phys::cross(ab, ac));
        if (normal.z < 0) normal = normal * -1;
        const std::size_t index = triangles_.size();
        triangles_.push_back(GroundTriangle{a, ab, ac, normal, 1 / area});
        const int first_x = cell_x(std::min({a.x, b.x, c.x}));
        const int last_x = cell_x(std::max({a.x, b.x, c.x}));
        const int first_y = cell_y(std::min({a.y, b.y, c.y}));
        const int last_y = cell_y(std::max({a.y, b.y, c.y}));
        for (int y = first_y; y <= last_y; y += 1) {
            for (int x = first_x; x <= last_x; x += 1) cells_[y * columns + x].push_back(index);
        }
    }

    void add_patch(Terrain kind, double xa, double xb, double ya, double yb, double step, bool hole) {
        const int nx = std::max(1, static_cast<int>(std::ceil((xb - xa) / step)));
        const int ny = std::max(1, static_cast<int>(std::ceil((yb - ya) / step)));
        const double sx = (xb - xa) / nx, sy = (yb - ya) / ny;
        for (int j = 0; j < ny; j += 1) {
            for (int i = 0; i < nx; i += 1) {
                const double x = xa + i * sx, y = ya + j * sy;
                if (hole && x + sx / 2 > -1.5 && x + sx / 2 < 1.5 && y + sy / 2 > -1.1 && y + sy / 2 < site_layout().brook_near + 0.26) continue;
                const phys::Vec3 a{x, y, terrain_height(kind, x, y)};
                const phys::Vec3 b{x + sx, y, terrain_height(kind, x + sx, y)};
                const phys::Vec3 c{x + sx, y + sy, terrain_height(kind, x + sx, y + sy)};
                const phys::Vec3 d{x, y + sy, terrain_height(kind, x, y + sy)};
                add_triangle(a, b, c);
                add_triangle(a, c, d);
            }
        }
    }
};
} // namespace

std::shared_ptr<const phys::GroundSurface> worksite_ground() {
    static const std::shared_ptr<const phys::GroundSurface> ground = std::make_shared<const WorksiteGround>();
    return ground;
}
} // namespace zc
