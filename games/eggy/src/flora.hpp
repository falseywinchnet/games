#pragma once
// The species library: 100 flowers, 50 trees, 30 logs, 20 bushes,
// 40 mushrooms, 40 ferns and 200 rocks, all generated parametrically from a
// fixed seed at start-up, with their own pixel textures and mip chains.
#include "mesh.hpp"
#include "r3d.hpp"
#include "world.hpp"

#include <array>
#include <vector>

namespace eggy {

constexpr int kFlowers = 100, kTrees = 50, kLogs = 30, kBushes = 20, kMushrooms = 40, kFerns = 40, kRocks = 200, kTufts = 12;

enum class Canopy : std::uint8_t { blobs, conifer, column, umbrella, weeping, birch, snag };

struct TreeSpecies {
    Canopy canopy;
    double height, trunk, spread;
    int blobs, tiers, bark, leaf;
    Col tint;
    unsigned biomes;  // bit per Biome
};
struct LogSpecies { double radius; int bark; Col tint; bool mossy, hollow; int brackets; };
struct BushSpecies { int blobs; double radius, height; int leaf; Col tint, berry; int berries; };
struct MushroomSpecies { int shape; double cap_r, cap_h, stem_h, stem_r; int cap_tex; Col cap, stem; int cluster; };
struct RockSpecies { int mesh; double sx, sy, sz; int ground; Col tint; bool moss; };

struct Flora {
    std::array<TreeSpecies, kTrees> trees;
    std::array<LogSpecies, kLogs> logs;
    std::array<BushSpecies, kBushes> bushes;
    std::array<MushroomSpecies, kMushrooms> mushrooms;
    std::array<RockSpecies, kRocks> rocks;
    std::array<Tex, kFlowers> flower;
    std::array<double, kFlowers> flower_size;
    std::array<Tex, kFerns> fern;
    std::array<double, kFerns> fern_size;
    std::array<Tex, kTufts> tuft;
    std::array<Tex, 8> bark;
    std::array<Tex, 12> leaf;
    std::array<Tex, 8> cap;
    std::vector<Mesh> rock_meshes;   // 24 base shapes reused at different proportions
    std::vector<Mesh> blob_meshes;   // canopy lumps
};

const Flora& flora();

// Species choice for a place in the world (deterministic).
int tree_for(Biome b, unsigned hash);
int rock_for(Biome b, unsigned hash);

extern M34 g_tree_pre;  // set around draw_tree to topple a tree; reset to identity after

// drawing (positions in render space: z already scaled)
void draw_tree(R3D& r, const TreeSpecies& t, double x, double y, double z, double sway, double scale, bool snowy, std::uint8_t mat, Col fade);
void draw_log(R3D& r, const LogSpecies& l, double x0, double x1, double y, double z, bool cap_left, bool cap_right);
void draw_bush(R3D& r, const BushSpecies& b, double x, double y, double z, double sway, unsigned seed, std::uint8_t mat, Col fade);
void draw_mushrooms(R3D& r, const MushroomSpecies& m, double x, double y, double z, unsigned seed);
void draw_rock(R3D& r, const RockSpecies& k, double x, double y, double z, double size, bool snowy, std::uint8_t mat, Col fade);

}  // namespace eggy
