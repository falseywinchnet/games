#include "shapes.hpp"

#include <algorithm>
#include <cmath>

namespace zc {

namespace {

constexpr double kPi = 3.14159265358979323846;

double clamp_to(double v, double limit) {
    return std::max(-limit, std::min(limit, v));
}

double axis_of(V3 v, int axis) {
    return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
}

void set_axis(V3& v, int axis, double value) {
    if (axis == 0) {
        v.x = value;
    } else if (axis == 1) {
        v.y = value;
    } else {
        v.z = value;
    }
}

}  // namespace

void add_facing_triangle(std::vector<Vtx>& out, const Vtx& a, const Vtx& b, const Vtx& c, V3 outward) {
    const V3 face = cross(b.p - a.p, c.p - a.p);
    if (dot(face, face) < 1e-24) {
        return;
    }
    out.push_back(a);
    if (dot(face, outward) >= 0) {
        out.push_back(b);
        out.push_back(c);
    } else {
        out.push_back(c);
        out.push_back(b);
    }
}

void add_box(std::vector<Vtx>& out, const M34& frame, V3 low, V3 high, Col colour) {
    const Mesh& unit = box_mesh();   // [-1,1]^2 x [0,1]
    const M34 place = frame * M34::translate(0.5 * (low.x + high.x), 0.5 * (low.y + high.y), low.z) *
                      M34::scale(0.5 * (high.x - low.x), 0.5 * (high.y - low.y), high.z - low.z);
    const M34 normals = place.normal_matrix();
    for (size_t k = 0; k < unit.size(); k += 1) {
        Vtx v = unit[k];
        v.p = place.apply(v.p);
        v.n = norm(normals.dir(v.n));
        v.c = colour;
        v.s = v.p.x * 12;
        v.t = (v.p.y + v.p.z) * 12;
        out.push_back(v);
    }
}

void add_rounded_box(std::vector<Vtx>& out, const M34& frame, V3 low, V3 high, double radius, int steps, Col colour) {
    const V3 centre = (low + high) * 0.5;
    const V3 half = (high - low) * 0.5;
    const double r = std::max(1e-5, std::min(radius, 0.95 * std::min(half.x, std::min(half.y, half.z))));
    const V3 core{half.x - r, half.y - r, half.z - r};
    // the cuts along each axis: the flat middle, then the round in `steps`
    // pieces, spaced so the facets turn by equal angles
    std::vector<double> ticks[3];
    for (int axis = 0; axis < 3; axis += 1) {
        const double c = axis_of(core, axis);
        for (int k = steps; k >= 1; k -= 1) {
            ticks[axis].push_back(-(c + r * std::tan(0.25 * kPi * k / steps)));
        }
        ticks[axis].push_back(-c);
        ticks[axis].push_back(c);
        for (int k = 1; k <= steps; k += 1) {
            ticks[axis].push_back(c + r * std::tan(0.25 * kPi * k / steps));
        }
    }
    const M34 normals = frame.normal_matrix();
    for (int face = 0; face < 3; face += 1) {
        const int u = (face + 1) % 3;
        const int w = (face + 2) % 3;
        for (int side = -1; side <= 1; side += 2) {
            const size_t nu = ticks[u].size();
            const size_t nw = ticks[w].size();
            std::vector<Vtx> grid(nu * nw);
            for (size_t i = 0; i < nu; i += 1) {
                for (size_t j = 0; j < nw; j += 1) {
                    V3 q;
                    set_axis(q, face, side * axis_of(half, face));
                    set_axis(q, u, ticks[u][i]);
                    set_axis(q, w, ticks[w][j]);
                    const V3 inner{clamp_to(q.x, core.x), clamp_to(q.y, core.y), clamp_to(q.z, core.z)};
                    const V3 n = norm(q - inner);
                    Vtx v;
                    v.p = frame.apply(centre + inner + n * r);
                    v.n = norm(normals.dir(n));
                    v.c = colour;
                    v.s = (q.x + q.z) * 20;
                    v.t = (q.y - q.z) * 20;
                    grid[i * nw + j] = v;
                }
            }
            for (size_t i = 0; i + 1 < nu; i += 1) {
                for (size_t j = 0; j + 1 < nw; j += 1) {
                    const Vtx& a = grid[i * nw + j];
                    const Vtx& b = grid[(i + 1) * nw + j];
                    const Vtx& c = grid[(i + 1) * nw + j + 1];
                    const Vtx& d = grid[i * nw + j + 1];
                    const V3 outward = a.n + b.n + c.n + d.n;
                    add_facing_triangle(out, a, b, c, outward);
                    add_facing_triangle(out, a, c, d, outward);
                }
            }
        }
    }
}

void add_mesh(std::vector<Vtx>& out, const Mesh& mesh, const M34& frame, Col colour) {
    const M34 normals = frame.normal_matrix();
    for (size_t k = 0; k < mesh.size(); k += 1) {
        Vtx v = mesh[k];
        v.p = frame.apply(v.p);
        v.n = norm(normals.dir(v.n));
        v.c = Col{v.c.r * colour.r, v.c.g * colour.g, v.c.b * colour.b, v.c.a * colour.a};
        v.s = (v.p.x + v.p.z) * 22;
        v.t = (v.p.y - v.p.z) * 22;
        out.push_back(v);
    }
}

void add_ellipsoid(std::vector<Vtx>& out, const M34& frame, V3 centre, V3 radii, Col colour, int slices, int stacks) {
    add_mesh(out, sphere_mesh(slices, stacks), frame * M34::translate(centre.x, centre.y, centre.z) * M34::scale(radii.x, radii.y, radii.z), colour);
}

M34 frame_along(V3 origin, V3 direction) {
    const V3 z = norm(direction);
    const V3 helper = std::abs(z.z) < 0.95 ? V3{0, 0, 1} : V3{1, 0, 0};
    const V3 x = norm(cross(helper, z));
    const V3 y = cross(z, x);
    M34 m;
    m.m[0] = x.x;
    m.m[1] = y.x;
    m.m[2] = z.x;
    m.m[3] = origin.x;
    m.m[4] = x.y;
    m.m[5] = y.y;
    m.m[6] = z.y;
    m.m[7] = origin.y;
    m.m[8] = x.z;
    m.m[9] = y.z;
    m.m[10] = z.z;
    m.m[11] = origin.z;
    return m;
}

void add_rod(std::vector<Vtx>& out, V3 a, V3 b, double radius, Col colour, int slices) {
    const V3 d = b - a;
    const double length = len(d);
    if (length < 1e-6) {
        return;
    }
    const M34 frame = frame_along(a, d) * M34::scale(radius, radius, length);
    add_mesh(out, cylinder_mesh(slices), frame, colour);
}

void add_lathe(std::vector<Vtx>& out, const M34& frame, const double* radius, const double* height, int count, int slices, Col colour) {
    if (count < 2) {
        return;
    }
    // each piece's normal in the (radius, height) plane: the outside is to the right
    std::vector<double> nr(static_cast<size_t>(count - 1)), nz(static_cast<size_t>(count - 1));
    std::vector<char> live(static_cast<size_t>(count - 1), 0);
    for (int k = 0; k + 1 < count; k += 1) {
        const double dr = radius[k + 1] - radius[k];
        const double dz = height[k + 1] - height[k];
        const double l = std::sqrt(dr * dr + dz * dz);
        if (l > 1e-9) {
            nr[static_cast<size_t>(k)] = dz / l;
            nz[static_cast<size_t>(k)] = -dr / l;
            live[static_cast<size_t>(k)] = 1;
        }
    }
    const double smooth_limit = std::cos(40.0 * kPi / 180.0);
    const M34 normals = frame.normal_matrix();
    for (int k = 0; k + 1 < count; k += 1) {
        const size_t piece = static_cast<size_t>(k);
        if (!live[piece]) {
            continue;
        }
        // the normals at its two ends, shared with a neighbour that turns gently
        double end_r[2] = {nr[piece], nr[piece]};
        double end_z[2] = {nz[piece], nz[piece]};
        for (int end = 0; end < 2; end += 1) {
            const int other = end == 0 ? k - 1 : k + 1;
            if (other < 0 || other + 1 >= count || !live[static_cast<size_t>(other)]) {
                continue;
            }
            const double cosine = nr[piece] * nr[static_cast<size_t>(other)] + nz[piece] * nz[static_cast<size_t>(other)];
            if (cosine > smooth_limit) {
                const double sr = nr[piece] + nr[static_cast<size_t>(other)];
                const double sz = nz[piece] + nz[static_cast<size_t>(other)];
                const double l = std::sqrt(sr * sr + sz * sz);
                end_r[end] = sr / l;
                end_z[end] = sz / l;
            }
        }
        for (int s = 0; s < slices; s += 1) {
            const double a0 = 2 * kPi * s / slices;
            const double a1 = 2 * kPi * (s + 1) / slices;
            const double cosines[2] = {std::cos(a0), std::cos(a1)};
            const double sines[2] = {std::sin(a0), std::sin(a1)};
            Vtx corner[4];
            for (int c = 0; c < 4; c += 1) {
                const int around = (c == 1 || c == 2) ? 1 : 0;
                const int end = (c >= 2) ? 1 : 0;
                const double r = radius[k + end];
                const V3 local{r * cosines[around], r * sines[around], height[k + end]};
                const V3 n{end_r[end] * cosines[around], end_r[end] * sines[around], end_z[end]};
                corner[c].p = frame.apply(local);
                corner[c].n = norm(normals.dir(n));
                corner[c].c = colour;
                corner[c].s = static_cast<double>(s) / slices;
                corner[c].t = height[k + end] * 20;
            }
            const V3 outward = corner[0].n + corner[1].n + corner[2].n + corner[3].n;
            add_facing_triangle(out, corner[0], corner[1], corner[2], outward);
            add_facing_triangle(out, corner[0], corner[2], corner[3], outward);
        }
    }
}

void add_arc_tube(std::vector<Vtx>& out, const M34& frame, double ring, double tube, double a0, double a1, int pieces, int sides, Col colour) {
    const M34 normals = frame.normal_matrix();
    for (int i = 0; i < pieces; i += 1) {
        for (int j = 0; j < sides; j += 1) {
            Vtx corner[4];
            for (int c = 0; c < 4; c += 1) {
                const int di = (c == 1 || c == 2) ? 1 : 0;
                const int dj = (c >= 2) ? 1 : 0;
                const double u = a0 + (a1 - a0) * (i + di) / pieces;
                const double v = 2 * kPi * (j + dj) / sides;
                const V3 radial{std::cos(u), std::sin(u), 0};
                const V3 n = radial * std::cos(v) + V3{0, 0, std::sin(v)};
                corner[c].p = frame.apply(radial * ring + n * tube);
                corner[c].n = norm(normals.dir(n));
                corner[c].c = colour;
            }
            const V3 outward = corner[0].n + corner[1].n + corner[2].n + corner[3].n;
            add_facing_triangle(out, corner[0], corner[1], corner[2], outward);
            add_facing_triangle(out, corner[0], corner[2], corner[3], outward);
        }
    }
}

void add_quad(std::vector<Vtx>& out, V3 a, V3 b, V3 c, V3 d, const double st[4][2], V3 outward, Col colour) {
    const V3 corners[4] = {a, b, c, d};
    const V3 n = norm(outward);
    Vtx v[4];
    for (int k = 0; k < 4; k += 1) {
        v[k].p = corners[k];
        v[k].n = n;
        v[k].c = colour;
        v[k].s = st[k][0];
        v[k].t = st[k][1];
    }
    add_facing_triangle(out, v[0], v[1], v[2], n);
    add_facing_triangle(out, v[0], v[2], v[3], n);
}

void map_planar(std::vector<Vtx>& out, size_t first, V3 origin, V3 axis_s, V3 axis_t, double scale) {
    for (size_t k = first; k < out.size(); k += 1) {
        out[k].s = dot(out[k].p - origin, axis_s) * scale;
        out[k].t = dot(out[k].p - origin, axis_t) * scale;
    }
}

}  // namespace zc
