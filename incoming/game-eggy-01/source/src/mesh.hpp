#pragma once
// Low-poly primitive meshes (unit sized, outward CCW winding) for props and Eggy.
#include "r3d.hpp"

#include <vector>

namespace eggy {

using Mesh = std::vector<Vtx>;

const Mesh& sphere_mesh(int slices, int stacks);           // radius 1, centre origin
const Mesh& hemisphere_mesh(int slices, int stacks);       // z >= 0
const Mesh& cylinder_mesh(int slices);                     // radius 1, z in [0,1], side only
const Mesh& disc_mesh(int slices);                         // radius 1 at z=0 facing +z
const Mesh& cone_mesh(int slices);                         // base radius 1 at z=0, apex z=1
const Mesh& box_mesh();                                    // [-1,1]^2 x [0,1]
Mesh rock_mesh(std::uint64_t seed, int slices, int stacks, double jitter);  // lumpy sphere

void tint(Mesh& m, Col c);
void draw_mesh(R3D& r, const Mesh& m, const M34& model, const Tex* tex, Col tint, std::uint8_t mat, double tex_scale = 1);

}  // namespace eggy
