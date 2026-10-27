#pragma once
// Building meshes in place: boxes plain and rounded, rods, lathes, ellipsoids,
// bent tubes and quads, written straight into a world-space triangle list
// through a frame. Every triangle is wound to face out.
#include "platform/render.hpp"

#include <vector>

namespace zc {

// a triangle, wound so that it faces along `outward`; skipped if degenerate
void add_facing_triangle(std::vector<Vtx>& out, const Vtx& a, const Vtx& b, const Vtx& c, V3 outward);

// a box between two corners of `frame`
void add_box(std::vector<Vtx>& out, const M34& frame, V3 low, V3 high, Col colour);

// a box between two corners with its edges rounded to `radius`, `steps`
// facets to each eighth of a turn (1 is a chamfer that shades round)
void add_rounded_box(std::vector<Vtx>& out, const M34& frame, V3 low, V3 high, double radius, int steps, Col colour);

// a unit mesh copied through `frame`, tinted
void add_mesh(std::vector<Vtx>& out, const Mesh& mesh, const M34& frame, Col colour);

// an ellipsoid about `centre` of `frame`
void add_ellipsoid(std::vector<Vtx>& out, const M34& frame, V3 centre, V3 radii, Col colour, int slices, int stacks);

// a frame whose z axis points along `direction` from `origin`
M34 frame_along(V3 origin, V3 direction);

// a straight round rod from a to b (world space)
void add_rod(std::vector<Vtx>& out, V3 a, V3 b, double radius, Col colour, int slices);

// A surface of revolution about the frame's z axis. The profile runs through
// `count` points (radius, height), ordered so the surface's outside is on the
// right walking along it with radius to the right and height up (bottom to
// top up an outer wall). Neighbouring facets share normals unless they meet
// at more than about 40 degrees.
void add_lathe(std::vector<Vtx>& out, const M34& frame, const double* radius, const double* height, int count, int slices, Col colour);

// part of a ring: ring radius `ring` round the frame's z axis, a tube of
// radius `tube`, from angle a0 to a1 (radians, from +x toward +y)
void add_arc_tube(std::vector<Vtx>& out, const M34& frame, double ring, double tube, double a0, double a1, int pieces, int sides, Col colour);

// a flat quad a b c d (world space) with texture coordinates per corner
void add_quad(std::vector<Vtx>& out, V3 a, V3 b, V3 c, V3 d, const double st[4][2], V3 outward, Col colour);

// texture coordinates for vertices [first, end) by projection on two axes
void map_planar(std::vector<Vtx>& out, size_t first, V3 origin, V3 axis_s, V3 axis_t, double scale);

}  // namespace zc
