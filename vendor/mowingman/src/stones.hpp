#pragma once
// Stones for the edges of the flower beds, from Rock Stack's rock generator
// (vendor/zenconstruction/src/rocks.cpp): the same radius field over directions (a
// superquadric cut by fracture planes, times one plus octaves of gradient noise), the
// same five classes after photographs of real stones, and the same stones they are made
// of, coloured and mottled, veined and lichened the same way. Only the shape is taken:
// these stones are scenery, with no physics.
#include "model3d.hpp"

#include <cstdint>

namespace mm {

enum class StoneClass : std::uint8_t { river_disc, cobble, block, slab, shard };

// A stone's mesh, about its centre (z up, its thin axis vertical) with colours in its
// vertices; `size` scales its mean diameter.
void build_stone(Mesh& mesh, std::uint32_t seed, double size, double ppm, const Xform& place);

} // namespace mm
