#include "stone_render.hpp"

#include "noise.hpp"
#include "room.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace ts {
namespace {

constexpr double pi = 3.14159265358979323846;

struct V {
    double x = 0, y = 0, z = 0;
};

V times(const Pose& p, double x, double y, double z) {
    return {p.m[0] * x + p.m[1] * y + p.m[2] * z, p.m[3] * x + p.m[4] * y + p.m[5] * z,
            p.m[6] * x + p.m[7] * y + p.m[8] * z};
}

Pose product(const Pose& a, const Pose& b) {
    Pose out;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            out.m[r * 3 + c] = a.m[r * 3] * b.m[c] + a.m[r * 3 + 1] * b.m[3 + c] + a.m[r * 3 + 2] * b.m[6 + c];
    return out;
}

// sRGB-coded byte to linear, once.
struct Decode {
    std::array<float, 256> value{};
    Decode() {
        for (int i = 0; i < 256; ++i)
            value[static_cast<std::size_t>(i)] = linear_from_srgb(static_cast<float>(i) / 255.0F);
    }
};
const Decode& decode() {
    static const Decode table;
    return table;
}

// The mineral picture at object (x, y) on face `face`, bilinear.
struct Texel {
    float r = 0, g = 0, b = 0, clarity = 0, fleck = 0, sheen = 0;
};
Texel read_picture(const MineralPicture& picture, int face, double x, double y) {
    const int side = picture.side;
    const double fx = std::clamp((x / reach + 1) * .5 * side - .5, 0.0, side - 1.001);
    const double fy = std::clamp((1 - y / reach) * .5 * side - .5, 0.0, side - 1.001);
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    const float tx = static_cast<float>(fx - x0);
    const float ty = static_cast<float>(fy - y0);
    const std::size_t base = static_cast<std::size_t>(face) * static_cast<std::size_t>(side * side);
    const std::size_t i00 = base + static_cast<std::size_t>(y0 * side + x0);
    const std::size_t i01 = i00 + 1;
    const std::size_t i10 = i00 + static_cast<std::size_t>(side);
    const std::size_t i11 = i10 + 1;
    const float w00 = (1 - tx) * (1 - ty);
    const float w01 = tx * (1 - ty);
    const float w10 = (1 - tx) * ty;
    const float w11 = tx * ty;
    const std::array<float, 256>& lin = decode().value;
    Texel t;
    const std::uint32_t c[4] = {picture.colour[i00], picture.colour[i01], picture.colour[i10], picture.colour[i11]};
    const std::uint16_t e[4] = {picture.extra[i00], picture.extra[i01], picture.extra[i10], picture.extra[i11]};
    const float w[4] = {w00, w01, w10, w11};
    for (int k = 0; k < 4; ++k) {
        t.r += w[k] * lin[c[k] & 0xffU];
        t.g += w[k] * lin[(c[k] >> 8) & 0xffU];
        t.b += w[k] * lin[(c[k] >> 16) & 0xffU];
        t.clarity += w[k] * static_cast<float>(c[k] >> 24) / 255.0F;
        t.fleck += w[k] * static_cast<float>(e[k] & 0xffU) / 255.0F;
        t.sheen += w[k] * static_cast<float>(e[k] >> 8) / 255.0F;
    }
    return t;
}

// GGX's spread of microfacets.
double ggx(double nh, double alpha) {
    const double a2 = alpha * alpha;
    const double k = nh * nh * (a2 - 1) + 1;
    return a2 / (pi * k * k);
}

double fresnel(double c, double f0) {
    const double m = 1 - std::clamp(c, 0.0, 1.0);
    const double m2 = m * m;
    return f0 + (1 - f0) * m2 * m2 * m;
}

// Bilinear read of a coarse grid at fine-sample position (sx, sy).
float coarse_at(const std::vector<float>& grid, int side_x, int side_y, double fx, double fy) {
    const double gx = std::clamp(fx, 0.0, side_x - 1.001);
    const double gy = std::clamp(fy, 0.0, side_y - 1.001);
    const int x0 = static_cast<int>(gx);
    const int y0 = static_cast<int>(gy);
    const float tx = static_cast<float>(gx - x0);
    const float ty = static_cast<float>(gy - y0);
    const std::size_t at = static_cast<std::size_t>(y0 * side_x + x0);
    const float a = grid[at] + (grid[at + 1] - grid[at]) * tx;
    const float b = grid[at + static_cast<std::size_t>(side_x)] + (grid[at + static_cast<std::size_t>(side_x) + 1] - grid[at + static_cast<std::size_t>(side_x)]) * tx;
    return a + (b - a) * ty;
}

struct Step {
    double dx = 0, dy = 0, rise = 0, weight = 0;  // per coarse step: across, down, up
    double x = 0, y = 0, z = 0;                    // the direction
};

// Whether the heights stop a ray leaving (sx, sy) at height h0 along `step` before it
// climbs above the highest point.
bool stopped(const std::vector<float>& height, int side, int sx, int sy, double h0, const Step& step, double peak) {
    if (step.rise <= 0)
        return false;
    double px = sx;
    double py = sy;
    double ray = h0;
    while (ray < peak) {
        px += step.dx;
        py += step.dy;
        ray += step.rise;
        const int ix = static_cast<int>(px);
        const int iy = static_cast<int>(py);
        if (ix < 0 || iy < 0 || ix >= side || iy >= side)
            return false;
        if (height[static_cast<std::size_t>(iy * side + ix)] > ray)
            return true;
    }
    return false;
}

// Trims the rows and columns that change nothing beneath.
void crop(StonePicture& picture) {
    int x0 = picture.width;
    int y0 = picture.height;
    int x1 = -1;
    int y1 = -1;
    for (int y = 0; y < picture.height; ++y)
        for (int x = 0; x < picture.width; ++x) {
            const std::size_t at = static_cast<std::size_t>(y * picture.width + x);
            if (picture.keep[at] == 0xffffffU && (picture.add[at] & 0xffffffU) == 0)
                continue;
            x0 = std::min(x0, x);
            y0 = std::min(y0, y);
            x1 = std::max(x1, x);
            y1 = std::max(y1, y);
        }
    if (x1 < 0) {
        picture.width = 0;
        picture.height = 0;
        picture.keep.clear();
        picture.add.clear();
        return;
    }
    const int w = x1 - x0 + 1;
    const int h = y1 - y0 + 1;
    std::vector<std::uint32_t> keep(static_cast<std::size_t>(w * h));
    std::vector<std::uint32_t> add(static_cast<std::size_t>(w * h));
    std::vector<std::uint32_t> shine(picture.shine.empty() ? 0 : static_cast<std::size_t>(w * h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t from = static_cast<std::size_t>((y + y0) * picture.width + x + x0);
            keep[static_cast<std::size_t>(y * w + x)] = picture.keep[from];
            add[static_cast<std::size_t>(y * w + x)] = picture.add[from];
            if (!shine.empty())
                shine[static_cast<std::size_t>(y * w + x)] = picture.shine[from];
        }
    picture.keep.swap(keep);
    picture.add.swap(add);
    picture.shine.swap(shine);
    picture.width = w;
    picture.height = h;
    picture.origin_x -= x0;
    picture.origin_y -= y0;
}

}  // namespace

Pose orientation_pose(int orientation) {
    Pose pose;
    if (orientation / 4 == 1) {
        pose.m[0] = -1;
        pose.m[8] = -1;
    }
    const int turns = orientation % 4;
    for (int k = 0; k < turns; ++k)
        pose = turned(pose, 2, -pi / 2);
    return pose;
}

Pose turned(const Pose& pose, int axis, double angle) {
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    Pose r;
    // Rows of the turn's matrix: the two axes it moves, by the right hand.
    const int a = axis == 0 ? 1 : 0;
    const int b = axis == 2 ? 1 : 2;
    const int sign = axis == 1 ? -1 : 1;
    r.m[a * 3 + a] = c;
    r.m[a * 3 + b] = -s * sign;
    r.m[b * 3 + a] = s * sign;
    r.m[b * 3 + b] = c;
    // Snap what rounding leaves of a quarter turn.
    for (double& v : r.m)
        if (std::abs(v) < 1e-12)
            v = 0;
    return product(r, pose);
}

void draw_stone(const StoneLook& look, const Pose& pose, const DrawStone& how, StoneWork& work, StonePicture& out) {
    const Shape& shape = *look.shape;
    const Finish& finish = *look.finish;
    const MineralPicture& picture = *look.picture;
    const int fine = std::clamp(how.fineness, 1, 3);
    const double grow = 1 + .08 * how.lift;
    const double radius = how.radius * grow;
    // The picture holds the stone and the shade it throws.
    const int half = static_cast<int>(std::ceil(radius * 1.9)) + 2;
    const int size = half * 2;
    out.width = size;
    out.height = size;
    out.origin_x = half;
    out.origin_y = half;
    const int side = size * fine;  // fine samples across
    const double unit = radius * fine;  // fine samples per object unit
    const std::size_t samples = static_cast<std::size_t>(side) * static_cast<std::size_t>(side);

    // Pose the solid. Heights are above the wood, in fine samples.
    const std::vector<ShapeVertex>& vertices = shape.vertices();
    work.corners.resize(vertices.size() * 3);
    work.normals.resize(vertices.size() * 3);
    double lowest = 1e9;
    double highest = -1e9;
    for (const ShapeVertex& v : vertices) {
        const V p = times(pose, v.x, v.y, v.z);
        lowest = std::min(lowest, p.z);
        highest = std::max(highest, p.z);
    }
    const double raised = how.lift * .55;
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const ShapeVertex& v = vertices[i];
        const V p = times(pose, v.x, v.y, v.z);
        const V n = times(pose, v.nx, v.ny, v.nz);
        work.corners[i * 3] = static_cast<float>(half * fine + p.x * unit);
        work.corners[i * 3 + 1] = static_cast<float>(half * fine - p.y * unit);
        work.corners[i * 3 + 2] = static_cast<float>((p.z - lowest + raised) * unit);
        work.normals[i * 3] = static_cast<float>(n.x);
        work.normals[i * 3 + 1] = static_cast<float>(n.y);
        work.normals[i * 3 + 2] = static_cast<float>(n.z);
    }
    const double peak = (highest - lowest + raised) * unit;

    // Rasterise: the nearest triangle's id at every fine sample.
    work.colour.assign(samples, 0U);
    if (work.buffers.width() != side || work.buffers.height() != side)
        work.buffers.resize(side, side, true);
    work.buffers.clear(render::Rect{0, 0, side, side});
    render::r3d::Pass pass;
    pass.target = render::Target{work.colour.data(), side, side, side, render::Order::rgba};
    pass.buffers = &work.buffers;
    pass.clip = render::Rect{0, 0, side, side};
    const render::r3d::Flat flat{0xffffffffU};
    const std::vector<std::uint32_t>& triangles = shape.triangles();
    for (std::size_t t = 0; t + 2 < triangles.size(); t += 3) {
        const std::size_t a = triangles[t];
        const std::size_t b = triangles[t + 1];
        const std::size_t c = triangles[t + 2];
        const float ax = work.corners[a * 3], ay = work.corners[a * 3 + 1];
        const float bx = work.corners[b * 3], by = work.corners[b * 3 + 1];
        const float cx = work.corners[c * 3], cy = work.corners[c * 3 + 1];
        // Facing away from the eye (screen y runs down): skip.
        const float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (area >= 0)
            continue;
        const render::r3d::Corner ca{ax, ay, -work.corners[a * 3 + 2]};
        const render::r3d::Corner cb{bx, by, -work.corners[b * 3 + 2]};
        const render::r3d::Corner cc{cx, cy, -work.corners[c * 3 + 2]};
        render::r3d::triangle(pass, ca, cb, cc, flat, static_cast<int>(t / 3));
    }

    // Heights over the fine samples, -1 where the wood shows.
    work.height.assign(samples, -1.0F);
    for (int y = 0; y < side; ++y) {
        const float* depth = work.buffers.depth_row(y);
        for (int x = 0; x < side; ++x) {
            if (work.buffers.id_at(x, y) >= 0)
                work.height[static_cast<std::size_t>(y * side + x)] = -depth[x];
        }
    }

    // The room's light reaching each coarse point, and the sun's: marched over the
    // heights towards each light direction. Coarse points lie every `coarse` fine samples.
    const std::vector<LightDirection>& lights = room_lights();
    const Sun& sun = room_sun();
    const bool has_sun = sun.light.r + sun.light.g + sun.light.b > 0;
    const int coarse = 2 * fine;
    const int grid = side / coarse + 1;
    work.shade.assign(static_cast<std::size_t>(grid * grid), 1.0F);
    work.sun.assign(static_cast<std::size_t>(grid * grid), 1.0F);
    std::vector<Step> steps;
    Step sun_step;
    for (std::size_t k = 0; k < lights.size(); ++k) {
        const LightDirection& l = lights[k];
        const double flat_length = std::sqrt(l.x * l.x + l.y * l.y);
        Step step;
        step.x = l.x;
        step.y = l.y;
        step.z = l.z;
        step.weight = l.weight;
        const double stride = coarse * .75;
        if (flat_length > 1e-6) {
            step.dx = l.x / flat_length * stride;
            step.dy = -l.y / flat_length * stride;
            step.rise = l.z / flat_length * stride;
        }
        const bool is_sun = has_sun && k + 1 == lights.size();
        if (is_sun)
            sun_step = step;
        else
            steps.push_back(step);
    }
    const double bias = 1.5 * fine;
    for (int gy = 0; gy < grid; ++gy) {
        for (int gx = 0; gx < grid; ++gx) {
            const int sx = std::min(gx * coarse, side - 1);
            const int sy = std::min(gy * coarse, side - 1);
            const std::size_t at = static_cast<std::size_t>(sy * side + sx);
            const float h0 = std::max(0.0F, work.height[at]);
            double nx = 0, ny = 0, nz = 1;
            const int id = work.buffers.id_at(sx, sy);
            if (id >= 0) {
                const std::size_t v = triangles[static_cast<std::size_t>(id) * 3];
                nx = work.normals[v * 3];
                ny = work.normals[v * 3 + 1];
                nz = work.normals[v * 3 + 2];
            }
            double lit = 0;
            double total = 0;
            for (const Step& step : steps) {
                const double facing = std::max(0.0, nx * step.x + ny * step.y + nz * step.z);
                if (facing <= 0)
                    continue;
                const double seen = stopped(work.height, side, sx, sy, h0 + bias, step, peak) ? 0.0 : 1.0;
                lit += step.weight * facing * seen;
                total += step.weight * facing;
            }
            work.shade[static_cast<std::size_t>(gy * grid + gx)] = total > 0 ? static_cast<float>(lit / total) : 1.0F;
            if (has_sun) {
                const double seen = stopped(work.height, side, sx, sy, h0 + bias, sun_step, peak) ? 0.0 : 1.0;
                work.sun[static_cast<std::size_t>(gy * grid + gx)] = static_cast<float>(seen);
            }
        }
    }

    // What a clear stone lets through, on average, for the colour of its shadow.
    const double through_r = finish.clarity * std::exp(-finish.absorb.r * .45);
    const double through_g = finish.clarity * std::exp(-finish.absorb.g * .45);
    const double through_b = finish.clarity * std::exp(-finish.absorb.b * .45);
    // The share of the light on the wood that is the sun's.
    const Light3 up = room_diffuse(0, 0, 1);
    const double sun_on_wood = has_sun ? (sun.light.r + sun.light.g + sun.light.b) * std::max(0.0, sun.z) / pi : 0.0;
    const double room_on_wood = up.r + up.g + up.b;
    const double sun_share = sun_on_wood / (sun_on_wood + room_on_wood);
    const double expose = exposure();
    const double base_rough = finish.roughness;
    const double sheen_x = finish.axis[0];
    const double sheen_y = finish.axis[1];
    const double sheen_z = std::abs(finish.axis[2]) + 1.2;
    const double sheen_length = std::sqrt(sheen_x * sheen_x + sheen_y * sheen_y + sheen_z * sheen_z);

    work.keep.assign(samples * 3, 1.0F);
    work.add.assign(samples * 3, 0.0F);
    if (how.shine)
        work.shine.assign(samples * 3, 0.0F);
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            const std::size_t at = static_cast<std::size_t>(y * side + x);
            const double gxf = static_cast<double>(x) / coarse;
            const double gyf = static_cast<double>(y) / coarse;
            const double room_open = coarse_at(work.shade, grid, grid, gxf, gyf);
            const double sun_seen = has_sun ? coarse_at(work.sun, grid, grid, gxf, gyf) : 0.0;
            const int id = work.buffers.id_at(x, y);
            if (id < 0) {
                const double room_seen = room_open;
                if (!how.shadow)
                    continue;
                // The wood: the room's light, and the sun's, less what the stone stops.
                const double lit = (1 - sun_share) * room_seen + sun_share * sun_seen;
                const double stopped = 1 - lit;
                work.keep[at * 3] = static_cast<float>(1 - stopped * (1 - through_r));
                work.keep[at * 3 + 1] = static_cast<float>(1 - stopped * (1 - through_g));
                work.keep[at * 3 + 2] = static_cast<float>(1 - stopped * (1 - through_b));
                // A clear stone gathers the light passing through it onto the wood: its
                // shade glows in its own colour.
                if (finish.clarity > .3) {
                    const double gathered = stopped * stopped * stopped * (finish.clarity - .3) * .9 * expose;
                    work.add[at * 3] = static_cast<float>(gathered * through_r * up.r);
                    work.add[at * 3 + 1] = static_cast<float>(gathered * through_g * up.g);
                    work.add[at * 3 + 2] = static_cast<float>(gathered * through_b * up.b);
                }
                continue;
            }
            // Where on the triangle: its corners' weights at this sample's centre.
            const std::size_t t = static_cast<std::size_t>(id) * 3;
            const std::size_t ia = triangles[t];
            const std::size_t ib = triangles[t + 1];
            const std::size_t ic = triangles[t + 2];
            const double ax = work.corners[ia * 3], ay = work.corners[ia * 3 + 1];
            const double bx = work.corners[ib * 3], by = work.corners[ib * 3 + 1];
            const double cx = work.corners[ic * 3], cy = work.corners[ic * 3 + 1];
            const double px = x + .5;
            const double py = y + .5;
            const double det = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
            double wa = 1.0 / 3;
            double wb = 1.0 / 3;
            if (std::abs(det) > 1e-12) {
                wa = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / det;
                wb = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / det;
            }
            wa = std::clamp(wa, 0.0, 1.0);
            wb = std::clamp(wb, 0.0, 1.0 - wa);
            const double wc = 1 - wa - wb;
            const ShapeVertex& va = vertices[ia];
            const ShapeVertex& vb = vertices[ib];
            const ShapeVertex& vc = vertices[ic];
            const double ox = va.x * wa + vb.x * wb + vc.x * wc;
            const double oy = va.y * wa + vb.y * wb + vc.y * wc;
            const double oz = va.z * wa + vb.z * wb + vc.z * wc;
            double nx = work.normals[ia * 3] * wa + work.normals[ib * 3] * wb + work.normals[ic * 3] * wc;
            double ny = work.normals[ia * 3 + 1] * wa + work.normals[ib * 3 + 1] * wb + work.normals[ic * 3 + 1] * wc;
            double nz = work.normals[ia * 3 + 2] * wa + work.normals[ib * 3 + 2] * wb + work.normals[ic * 3 + 2] * wc;
            const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= length;
            ny /= length;
            nz /= length;
            const int face = oz >= 0 ? 0 : 1;
            // The fine relief: cracks, fibres, pits and band ridges tilt the normal and
            // keep some of the room's light out of their hollows.
            float slope_x = 0;
            float slope_y = 0;
            float hollow = 0;
            if (shape.fine(face, ox, oy, slope_x, slope_y, hollow)) {
                const V tilt = times(pose, -slope_x, -slope_y, 0);
                nx += tilt.x;
                ny += tilt.y;
                nz += tilt.z;
                const double again = std::sqrt(nx * nx + ny * ny + nz * nz);
                nx /= again;
                ny /= again;
                nz /= again;
            }
            const double room_seen = room_open * (1 - .6 * hollow);
            const Texel tex = read_picture(picture, face, ox, oy);
            // Thickness through the stone here, for what a clear one lets through.
            double up_height = 0;
            double down_height = 0;
            const bool in_top = shape.height(0, ox, oy, up_height);
            const bool in_bottom = shape.height(1, ox, oy, down_height);
            const double thickness = in_top && in_bottom ? std::max(0.0, up_height + down_height) : 0.0;

            const double clarity = std::clamp(static_cast<double>(tex.clarity), 0.0, 1.0);
            const bool metal = tex.fleck > .5F;
            if (metal) {
                // Each fleck lies at its own tilt: they glint one by one as the stone turns.
                const std::uint32_t bits = hash_cell(static_cast<int>(std::floor(ox * 90)), static_cast<int>(std::floor(oy * 90)), face, 77U);
                nx += (static_cast<double>(bits & 0xffU) / 255.0 - .5) * 1.2;
                ny += (static_cast<double>((bits >> 8) & 0xffU) / 255.0 - .5) * 1.2;
                const double tilted = std::sqrt(nx * nx + ny * ny + nz * nz);
                nx /= tilted;
                ny /= tilted;
                nz /= tilted;
            }
            const double nv = std::max(nz, .02);
            const double rough = metal ? .12 : std::clamp(base_rough + .04 * (1 - nv), 0.0, 1.0);
            const double f0 = finish.shine;
            const double f = fresnel(nv, f0);
            // The room mirrored, the reflection's direction about the normal.
            const double rx = 2 * nv * nx;
            const double ry = 2 * nv * ny;
            const double rz = 2 * nv * nz - 1;
            const Light3 mirrored = room_reflection(rx, ry, rz, rough);
            const double mirror_seen = .45 + .55 * room_seen;
            double spec_r = f * mirrored.r * mirror_seen;
            double spec_g = f * mirrored.g * mirror_seen;
            double spec_b = f * mirrored.b * mirror_seen;
            if (metal) {
                const double gold = tex.fleck;
                spec_r = mirrored.r * finish.glitter.r * gold + spec_r * (1 - gold);
                spec_g = mirrored.g * finish.glitter.g * gold + spec_g * (1 - gold);
                spec_b = mirrored.b * finish.glitter.b * gold + spec_b * (1 - gold);
            }
            // The sun's glint.
            double sun_dot = 0;
            if (has_sun) {
                sun_dot = std::max(0.0, nx * sun.x + ny * sun.y + nz * sun.z);
                if (sun_dot > 0 && sun_seen > 0) {
                    const double hx = sun.x;
                    const double hy = sun.y;
                    const double hz = sun.z + 1;
                    const double hl = std::sqrt(hx * hx + hy * hy + hz * hz);
                    const double nh = std::max(0.0, (nx * hx + ny * hy + nz * hz) / hl);
                    const double alpha = std::max(rough * rough, .012);
                    const double glint = ggx(nh, alpha) * fresnel((sun.z + 1) / hl, f0) / (4 * nv) * sun_seen;
                    const double cap = std::min(glint, 60.0);
                    spec_r += sun.light.r * cap;
                    spec_g += sun.light.g * cap;
                    spec_b += sun.light.b * cap;
                }
            }
            // The body: the room's light on it, the sun's, through the polish.
            const Light3 room = room_diffuse(nx, ny, nz);
            const double body = (1 - f) * (1 - clarity);
            const double light_r = room.r * room_seen + sun.light.r * sun_dot / pi * sun_seen;
            const double light_g = room.g * room_seen + sun.light.g * sun_dot / pi * sun_seen;
            const double light_b = room.b * room_seen + sun.light.b * sun_dot / pi * sun_seen;
            double out_r = spec_r + body * tex.r * light_r;
            double out_g = spec_g + body * tex.g * light_g;
            double out_b = spec_b + body * tex.b * light_b;
            // A clear stone glows a little with the light scattered inside it.
            const double glow = (1 - f) * clarity * finish.scatter;
            out_r += glow * tex.r * light_r;
            out_g += glow * tex.g * light_g;
            out_b += glow * tex.b * light_b;
            // A sheen that comes and goes with the angle.
            if (finish.rainbow && tex.sheen > 0) {
                // Nacre: thin layers send back a colour that runs through the rainbow as the
                // surface turns.
                const double hue = 6 * (.15 + .55 * nz + .35 * nx - .25 * ny + .5 * tex.sheen);
                const double h = hue - 6 * std::floor(hue / 6);
                // Pearl's colours are pale, a silvery white; abalone's (a stronger sheen) run deep.
                const double vivid = std::clamp((finish.sheen - 1) / .8, .55, 1.0);
                const double r = 1 - vivid + vivid * std::clamp(std::abs(h - 3) - 1, 0.0, 1.0);
                const double g = 1 - vivid + vivid * std::clamp(2 - std::abs(h - 2), 0.0, 1.0);
                const double b = 1 - vivid + vivid * std::clamp(2 - std::abs(h - 4), 0.0, 1.0);
                const double lit = (light_r + light_g + light_b) / 3;
                const double flash = finish.sheen * (.4 + .6 * tex.sheen) * .32 * lit * (.5 + .5 * room_seen);
                out_r += flash * r;
                out_g += flash * g;
                out_b += flash * b;
            } else if (finish.sheen > 0 && tex.sheen > 0) {
                const double toward = std::max(0.0, (nx * sheen_x + ny * sheen_y + nz * sheen_z) / sheen_length);
                const double flash = finish.sheen * tex.sheen * std::pow(toward, 6) * (.5 + .5 * room_seen);
                const double lit = (light_r + light_g + light_b) / 3;
                out_r += flash * finish.sheen_colour.r * lit * 1.6;
                out_g += flash * finish.sheen_colour.g * lit * 1.6;
                out_b += flash * finish.sheen_colour.b * lit * 1.6;
            }
            const double through = (1 - f) * clarity;
            work.keep[at * 3] = static_cast<float>(through * std::exp(-finish.absorb.r * thickness));
            work.keep[at * 3 + 1] = static_cast<float>(through * std::exp(-finish.absorb.g * thickness));
            work.keep[at * 3 + 2] = static_cast<float>(through * std::exp(-finish.absorb.b * thickness));
            work.add[at * 3] = static_cast<float>(out_r * expose);
            work.add[at * 3 + 1] = static_cast<float>(out_g * expose);
            work.add[at * 3 + 2] = static_cast<float>(out_b * expose);
            if (how.shine) {
                work.shine[at * 3] = static_cast<float>(nx);
                work.shine[at * 3 + 1] = static_cast<float>(ny);
                work.shine[at * 3 + 2] = static_cast<float>(metal ? 1.0 : std::clamp(1 - rough * 1.7, 0.0, 1.0));
            }
        }
    }

    // Down to whole pixels: the mean of each pixel's fine samples.
    const std::size_t pixels = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    out.keep.assign(pixels, 0xffffffU);
    out.add.assign(pixels, 0U);
    if (how.shine)
        out.shine.assign(pixels, 0U);
    else
        out.shine.clear();
    const float share = 1.0F / static_cast<float>(fine * fine);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float keep[3] = {0, 0, 0};
            float add[3] = {0, 0, 0};
            int covered = 0;
            for (int sy = 0; sy < fine; ++sy)
                for (int sx = 0; sx < fine; ++sx) {
                    const std::size_t at = static_cast<std::size_t>((y * fine + sy) * side + x * fine + sx);
                    for (int c = 0; c < 3; ++c) {
                        keep[c] += work.keep[at * 3 + static_cast<std::size_t>(c)];
                        add[c] += work.add[at * 3 + static_cast<std::size_t>(c)];
                    }
                    if (work.height[at] >= 0)
                        ++covered;
                }
            std::uint32_t k = 0;
            std::uint32_t a = 0;
            for (int c = 0; c < 3; ++c) {
                const float kept = std::clamp(keep[c] * share, 0.0F, 1.0F);
                // The added light is coded for the screen as the stone's own colour is.
                const float added = add[c] * share;
                const float coded = added > 0 ? screen_value(added) : 0.0F;
                k |= static_cast<std::uint32_t>(std::lround(kept * 255)) << (8 * c);
                a |= static_cast<std::uint32_t>(std::lround(std::clamp(coded, 0.0F, 1.0F) * 255)) << (8 * c);
            }
            const std::uint32_t coverage = static_cast<std::uint32_t>(covered * 255 / (fine * fine));
            const std::size_t at = static_cast<std::size_t>(y * size + x);
            out.keep[at] = k;
            out.add[at] = a | (coverage << 24);
            if (how.shine && covered > 0) {
                float sx = 0;
                float sy = 0;
                float polish = 0;
                for (int sy2 = 0; sy2 < fine; ++sy2)
                    for (int sx2 = 0; sx2 < fine; ++sx2) {
                        const std::size_t f = static_cast<std::size_t>((y * fine + sy2) * side + x * fine + sx2);
                        if (work.height[f] < 0)
                            continue;
                        sx += work.shine[f * 3];
                        sy += work.shine[f * 3 + 1];
                        polish += work.shine[f * 3 + 2];
                    }
                const float n = static_cast<float>(covered);
                const std::uint32_t qx = static_cast<std::uint32_t>(std::lround(std::clamp(128 + 127 * sx / n, 0.0F, 255.0F)));
                const std::uint32_t qy = static_cast<std::uint32_t>(std::lround(std::clamp(128 + 127 * sy / n, 0.0F, 255.0F)));
                const std::uint32_t qp = static_cast<std::uint32_t>(std::lround(std::clamp(255 * polish / n, 0.0F, 255.0F)));
                out.shine[at] = qx | (qy << 8) | (qp << 16) | (coverage << 24);
            }
        }
    }
    crop(out);
}

void lay_stone_moved(const StonePicture& picture, std::uint32_t* pixels, int width, int height, int stride, double x,
                     double y, double scale, double spin, double squash, double opacity, bool bgra, int clip_x0,
                     int clip_y0, int clip_x1, int clip_y1) {
    if (picture.width == 0 || scale <= 0 || opacity <= 0)
        return;
    const double across = std::max(.04, std::abs(squash)) * scale;
    // The picture's corners, turned and scaled, bound what it can touch.
    const double c = std::cos(spin);
    const double s = std::sin(spin);
    double reach = 0;
    const double corners[4][2] = {{-picture.origin_x, -picture.origin_y},
                                  {picture.width - picture.origin_x, -picture.origin_y},
                                  {-picture.origin_x, picture.height - picture.origin_y},
                                  {picture.width - picture.origin_x, picture.height - picture.origin_y}};
    for (const double* corner : corners)
        reach = std::max(reach, std::hypot(corner[0] * across, corner[1] * scale));
    const int x0 = std::max({static_cast<int>(std::floor(x - reach)), clip_x0, 0});
    const int y0 = std::max({static_cast<int>(std::floor(y - reach)), clip_y0, 0});
    const int x1 = std::min({static_cast<int>(std::ceil(x + reach)), clip_x1, width});
    const int y1 = std::min({static_cast<int>(std::ceil(y + reach)), clip_y1, height});
    if (x0 >= x1 || y0 >= y1)
        return;
    const int red_shift = bgra ? 16 : 0;
    const int blue_shift = bgra ? 0 : 16;
    const std::uint32_t solid = static_cast<std::uint32_t>(std::lround(std::clamp(opacity, 0.0, 1.0) * 256));
    for (int py = y0; py < y1; ++py) {
        std::uint32_t* row = pixels + static_cast<std::ptrdiff_t>(py) * stride;
        const double dy = py + .5 - y;
        for (int px = x0; px < x1; ++px) {
            const double dx = px + .5 - x;
            // Back through the turn, the squash and the scale to the picture.
            const double ux = (dx * c + dy * s) / across;
            const double uy = (-dx * s + dy * c) / scale;
            const int sx = static_cast<int>(std::floor(ux + picture.origin_x));
            const int sy = static_cast<int>(std::floor(uy + picture.origin_y));
            if (sx < 0 || sy < 0 || sx >= picture.width || sy >= picture.height)
                continue;
            const std::size_t at = static_cast<std::size_t>(sy * picture.width + sx);
            std::uint32_t keep = picture.keep[at];
            std::uint32_t add = picture.add[at];
            if (keep == 0xffffffU && (add & 0xffffffU) == 0)
                continue;
            if (solid < 256) {
                // Fading: keep moves towards all, the added light towards none.
                std::uint32_t faded_keep = 0;
                std::uint32_t faded_add = 0;
                for (int k = 0; k < 3; ++k) {
                    const std::uint32_t kept = (keep >> (8 * k)) & 0xffU;
                    const std::uint32_t added = (add >> (8 * k)) & 0xffU;
                    faded_keep |= (255 - (((255 - kept) * solid) >> 8)) << (8 * k);
                    faded_add |= ((added * solid) >> 8) << (8 * k);
                }
                keep = faded_keep;
                add = faded_add;
            }
            const std::uint32_t d = row[px];
            const std::uint32_t dr = (d >> red_shift) & 0xffU;
            const std::uint32_t dg = (d >> 8) & 0xffU;
            const std::uint32_t db = (d >> blue_shift) & 0xffU;
            const std::uint32_t r = std::min(255U, (dr * (keep & 0xffU) + 127) / 255 + (add & 0xffU));
            const std::uint32_t g = std::min(255U, (dg * ((keep >> 8) & 0xffU) + 127) / 255 + ((add >> 8) & 0xffU));
            const std::uint32_t b = std::min(255U, (db * ((keep >> 16) & 0xffU) + 127) / 255 + ((add >> 16) & 0xffU));
            row[px] = (r << red_shift) | (g << 8) | (b << blue_shift) | 0xff000000U;
        }
    }
}

void lay_glint(const StonePicture& picture, std::uint32_t* pixels, int width, int height, int stride, double x,
               double y, double light_x, double light_y, double light_height, double strength, bool bgra) {
    if (picture.shine.empty() || strength <= 0)
        return;
    const int left = static_cast<int>(std::lround(x - picture.origin_x));
    const int top = static_cast<int>(std::lround(y - picture.origin_y));
    const int x0 = std::max(left, 0);
    const int y0 = std::max(top, 0);
    const int x1 = std::min(left + picture.width, width);
    const int y1 = std::min(top + picture.height, height);
    const int red_shift = bgra ? 16 : 0;
    const int blue_shift = bgra ? 0 : 16;
    for (int py = y0; py < y1; ++py) {
        std::uint32_t* row = pixels + static_cast<std::ptrdiff_t>(py) * stride;
        const std::size_t source_row = static_cast<std::size_t>((py - top) * picture.width);
        for (int px = x0; px < x1; ++px) {
            const std::uint32_t s = picture.shine[source_row + static_cast<std::size_t>(px - left)];
            const std::uint32_t covered = s >> 24;
            const std::uint32_t polish = (s >> 16) & 0xffU;
            if (covered == 0 || polish == 0)
                continue;
            const double nx = (static_cast<double>(s & 0xffU) - 128) / 127;
            const double ny = (static_cast<double>((s >> 8) & 0xffU) - 128) / 127;
            const double nz = std::sqrt(std::max(0.0, 1 - nx * nx - ny * ny));
            // The light above the pointer, the eye straight above: the half way between.
            double lx = light_x - (px + .5);
            double ly = -(light_y - (py + .5));
            double lz = light_height;
            const double ll = std::sqrt(lx * lx + ly * ly + lz * lz);
            lx /= ll;
            ly /= ll;
            lz /= ll;
            const double hz = lz + 1;
            const double hl = std::sqrt(lx * lx + ly * ly + hz * hz);
            const double nh = std::max(0.0, (nx * lx + ny * ly + nz * hz) / hl);
            const double p = polish / 255.0;
            // A crisp highlight with a softer bloom round it.
            const double sharp = 25 + 160 * p * p;
            const double glint = strength * p * (std::pow(nh, sharp) + .3 * std::pow(nh, sharp * .15)) * (covered / 255.0) * 1.5;
            if (glint < .004)
                continue;
            const std::uint32_t lift = static_cast<std::uint32_t>(std::lround(std::min(1.0, glint) * 255));
            const std::uint32_t d = row[px];
            const std::uint32_t r = std::min(255U, ((d >> red_shift) & 0xffU) + lift);
            const std::uint32_t g = std::min(255U, ((d >> 8) & 0xffU) + lift * 247 / 255);
            const std::uint32_t b = std::min(255U, ((d >> blue_shift) & 0xffU) + lift * 228 / 255);
            row[px] = (r << red_shift) | (g << 8) | (b << blue_shift) | 0xff000000U;
        }
    }
}

void lay_stone(const StonePicture& picture, std::uint32_t* pixels, int width, int height, int stride, double x,
               double y, bool bgra, int clip_x0, int clip_y0, int clip_x1, int clip_y1) {
    const int left = static_cast<int>(std::lround(x - picture.origin_x));
    const int top = static_cast<int>(std::lround(y - picture.origin_y));
    const int x0 = std::max({left, clip_x0, 0});
    const int y0 = std::max({top, clip_y0, 0});
    const int x1 = std::min({left + picture.width, clip_x1, width});
    const int y1 = std::min({top + picture.height, clip_y1, height});
    if (x0 >= x1 || y0 >= y1)
        return;
    const int red_shift = bgra ? 16 : 0;
    const int blue_shift = bgra ? 0 : 16;
    for (int py = y0; py < y1; ++py) {
        std::uint32_t* row = pixels + static_cast<std::ptrdiff_t>(py) * stride;
        const std::size_t source_row = static_cast<std::size_t>((py - top) * picture.width);
        for (int px = x0; px < x1; ++px) {
            const std::size_t at = source_row + static_cast<std::size_t>(px - left);
            const std::uint32_t keep = picture.keep[at];
            const std::uint32_t add = picture.add[at];
            if (keep == 0xffffffU && (add & 0xffffffU) == 0)
                continue;
            const std::uint32_t d = row[px];
            const std::uint32_t dr = (d >> red_shift) & 0xffU;
            const std::uint32_t dg = (d >> 8) & 0xffU;
            const std::uint32_t db = (d >> blue_shift) & 0xffU;
            const std::uint32_t r = std::min(255U, (dr * (keep & 0xffU) + 127) / 255 + (add & 0xffU));
            const std::uint32_t g = std::min(255U, (dg * ((keep >> 8) & 0xffU) + 127) / 255 + ((add >> 8) & 0xffU));
            const std::uint32_t b = std::min(255U, (db * ((keep >> 16) & 0xffU) + 127) / 255 + ((add >> 16) & 0xffU));
            row[px] = (r << red_shift) | (g << 8) | (b << blue_shift) | 0xff000000U;
        }
    }
}

}  // namespace ts
