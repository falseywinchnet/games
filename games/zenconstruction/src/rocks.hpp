#pragma once
// Rocks for Zen Construction, ported from the physics prototype's rockgen.mjs.
//
// A rock is a radius field over directions: an ellipsoid, cut by a few
// fracture planes, times one plus a sum of noise octaves whose amplitude falls
// with frequency. The octaves are split at a cutoff wavelength set by the
// collision hull's vertex budget: the bands below it are the convex hull the
// physics collides; the bands above it are drawn (and raise the friction by
// Patton's law, mu = tan(basic angle + roughness angle)) but never reach the
// physics as shape. Five classes after photographs of balanced stones: river
// discs, cobbles, blocks, slabs and shards.
//
// Coordinates: metres, z up, every rock in its own body frame (origin at its
// centre of mass, as the physics has it).
#include "physics.hpp"
#include "platform/render.hpp"
#include "stones.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace zc {

enum class RockClass { river_disc, cobble, block, slab, shard };

// How a rock is made: everything the radius field needs, derived from its seed.
struct RockRecipe {
    std::uint32_t seed = 0;
    RockClass kind = RockClass::cobble;
    double axes[3] = {0, 0, 0};          // semi-axes, metres
    double radius = 0;                   // geometric mean of the semi-axes
    std::vector<phys::Vec3> fracture_normals;
    std::vector<double> fracture_offsets;
    double amplitude = 0.12, hurst = 0.9;
    double squareness = 2;               // the base's superquadric exponent: 2 an ellipsoid, higher boxier
    bool ridged = false;                 // creased noise (sharp ridges) instead of rolling
    int octaves = 7;
    int cutoff_octave = 7;               // octaves [0, cutoff) are geometry, the rest texture
    double noise_offset[3] = {0, 0, 0};
    double basic_friction_angle = 31;    // degrees
    // appearance: the stone it's made of (its texture), tinted
    Stone stone = Stone::grey_granite;
    Col colour{1, 1, 1, 1};
    bool vein = false;
    phys::Vec3 vein_normal;
    double vein_offset = 0, vein_width = 0;
    double lichen = 0;                   // 0 none; else the share of the surface it may cover
    Col lichen_colour{.62f, .66f, .5f, 1};
};

// A render mesh: an unindexed triangle list in the rock's body frame.
struct RockMesh {
    std::vector<Vtx> triangles;          // three vertices per triangle
    int level = 0;                       // icosphere subdivision level
};

struct Rock {
    int index = 0;                       // its number in the run's bowl, 0..
    RockRecipe recipe;
    phys::Shape shape;
    double diameter = 0;                 // metres
    double friction = 0;
    RockMesh far_mesh;                   // level 2 (320 triangles): the bowl and distant views
    RockMesh near_mesh;                  // level 3 (1,280 triangles): built when first needed
};

const char* rock_class_name(RockClass kind);

// One rock of a run: the class, size and look follow from (run seed, index).
Rock make_rock(std::uint32_t run_seed, int index);

// Builds the rock's near mesh if it doesn't have it yet.
void ensure_near_mesh(Rock& rock);

// The orientation that lays a rock flat: its thinnest axis (largest moment of
// inertia) vertical.
phys::Quat flat_side_down(const Rock& rock);

// The texture a rock is drawn with: the stone it's made of.
const Tex& rock_texture(const Rock& rock);

// A plain fine-grain stone texture (64 x 64, values around 1) for scenery.
const Tex& stone_grain();

}  // namespace zc
