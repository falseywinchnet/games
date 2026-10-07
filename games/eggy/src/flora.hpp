#pragma once
// The species library: 100 flowers, 14 tree species in 8 grown forms each,
// 30 logs, 20 bushes, 40 mushrooms, 40 ferns and 200 rocks, all generated
// parametrically from fixed seeds at start-up, with their own pixel textures
// and mip chains.
#include "mesh.hpp"
#include "r3d.hpp"
#include "world.hpp"

#include <array>
#include <vector>

namespace eggy {

constexpr int kFlowers = 100, kLogs = 30, kBushes = 20, kMushrooms = 40, kFerns = 40, kRocks = 200, kTufts = 12;

// Trees are built like real ones: a trunk and limbs carry leaf clumps or
// needle tiers. Each species has kTreeForms grown forms (a different skeleton),
// and every tree in the world also gets its own age, lean, turn, girth and
// colour, so no two neighbours match.
enum class TreeKind : std::uint8_t {
    oak, beech, birch, poplar, willow, maple, rowan,             // broadleaves
    spruce, fir, pine, larch, stone_pine, juniper, snag, count  // conifers, wind-shaped and dead
};
constexpr int kTreeKinds = static_cast<int>(TreeKind::count), kTreeForms = 8, kFoliage = 20, kBarks = 10;

struct TreeModel {
    Mesh wood, leaf, snow, fruit;  // tree-local: base at the origin, z up (render units)
    double crown = .4;             // crown radius, for the ground shadow
    double height = 1;
};

// Where a tree stands decides which species may grow there.
enum class TreeSite : std::uint8_t { broadleaf, conifer, flank };

struct TreeLook {
    TreeKind kind = TreeKind::oak;
    int form = 0, bark = 0, foliage = 0;
    double yaw = 0, size = 1, girth = 1, lean = 0, lean_dir = 0;
    Col tint{1, 1, 1, 1};
};

struct LogSpecies { double radius; int bark; Col tint; bool mossy, hollow; int brackets; };
struct BushSpecies { int blobs; double radius, height; int leaf; Col tint, berry; int berries; };
struct MushroomSpecies { int shape; double cap_r, cap_h, stem_h, stem_r; int cap_tex; Col cap, stem; int cluster; };
struct RockSpecies { int mesh; double sx, sy, sz; int ground; Col tint; bool moss; };

struct Flora {
    std::vector<TreeModel> tree_models;  // kTreeKinds x kTreeForms
    std::array<LogSpecies, kLogs> logs;
    std::array<BushSpecies, kBushes> bushes;
    std::array<MushroomSpecies, kMushrooms> mushrooms;
    std::array<RockSpecies, kRocks> rocks;
    std::array<Tex, kFlowers> flower;
    std::array<double, kFlowers> flower_size;
    std::array<Tex, kFerns> fern;
    std::array<double, kFerns> fern_size;
    std::array<Tex, kTufts> tuft;
    std::array<Tex, kBarks> bark;
    std::array<Tex, 12> leaf;          // bushes
    std::array<Tex, kFoliage> foliage; // tree leaves and needles, summer and autumn
    std::array<Tex, 8> cap;
    std::vector<Mesh> rock_meshes;   // 24 base shapes reused at different proportions
    std::vector<Mesh> blob_meshes;   // canopy lumps
};

const Flora& flora();

// Species choice for a place in the world (deterministic). Trees of one
// species gather in stands; each tree still differs from its neighbours.
TreeLook tree_look(Biome b, TreeSite site, double u, double v, unsigned hash);
double tree_crown(const TreeLook& t, double scale);  // crown radius, for its shadow
int rock_for(Biome b, unsigned hash);

extern M34 g_tree_pre;  // set around draw_tree to topple a tree; reset to identity after

// drawing (positions in render space: z already scaled)
void draw_tree(R3D& r, const TreeLook& t, double x, double y, double z, double sway, double scale, bool snowy, std::uint8_t mat, Col fade);
void draw_log(R3D& r, const LogSpecies& l, double x0, double x1, double y, double z, bool cap_left, bool cap_right);
void draw_bush(R3D& r, const BushSpecies& b, double x, double y, double z, double sway, unsigned seed, std::uint8_t mat, Col fade);
void draw_mushrooms(R3D& r, const MushroomSpecies& m, double x, double y, double z, unsigned seed);
void draw_rock(R3D& r, const RockSpecies& k, double x, double y, double z, double size, bool snowy, std::uint8_t mat, Col fade);

}  // namespace eggy
