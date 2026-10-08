#pragma once
// A garden to mow: a 16 x 10 m lawn and whatever stands in it. Placement comes first
// and is all here; the pictures are generated from it afterwards, and shown last.
//
// Each garden is different. It may have flower beds in mulch (usually one kind of
// flower, sometimes two), a few small trees each in a ring of mulch, a shed or a
// greenhouse against an edge, and the odd thing left out on the grass: a fountain or
// a birdbath, a grill, a sandbox, a paddling pool, a lawn chair. The lawn itself may
// carry clover, daisies, buttercups, dandelions, a ring of mushrooms, or none of them.
// Whatever is placed, the mower can always get round it: things keep a mower's
// width and more from each other and from the edge, unless they stand right against it.
//
// Generated from a seed; pure and deterministic.
#include <cstdint>
#include <vector>

namespace mm {

constexpr double lawn_width = 32.0;   // metres
constexpr double lawn_height = 20.0;  // metres
constexpr double lawn_cell = 0.05;    // metres per fine cell
constexpr int lawn_cells_x = 640;
constexpr int lawn_cells_y = 400;
constexpr double mower_room = 1.5;    // clear lawn kept between any two things, metres

enum class Flower : std::uint8_t { crocus, rose, sunflower, tulip, lily, orchid, peony, hydrangea, daisy };
constexpr int flower_count = 9;
const char* flower_name(Flower flower);
// The colour a plant flowers in (0xRRGGBB): one of its kind's three, chosen by its bed.
struct Garden;
struct Plant;
int plant_tone(const Garden& garden, const Plant& plant);
std::uint32_t flower_tint(Flower flower, int tone);
// How far one plant of this flower spreads from its root, metres as drawn.
double flower_reach(Flower flower);

enum class BedShape : std::uint8_t { oval, rounded };

// A flower bed: mulch, edged with stones, planted. The mower's own mind keeps off
// it; a hand on the wheel need not.
struct Bed {
    double x{};  // centre, metres
    double y{};
    double rx{};  // half-sizes, metres
    double ry{};
    BedShape shape{BedShape::oval};
    int kinds{1};  // one kind of flower, or two
    Flower flowers[2]{Flower::daisy, Flower::daisy};
    std::uint64_t seed{};
};

// One plant in a bed.
struct Plant {
    Flower kind{};
    double x{};
    double y{};
    std::uint32_t seed{};
    int bed{};  // which bed it grows in
    bool crushed{};
};

// A small tree standing in a ring of mulch.
struct Tree {
    double x{};
    double y{};
    double trunk{};  // radius, metres
    double ring{};   // radius of its mulch, wider than the canopy
    double crown{};  // radius of its canopy
    int kind{};      // 0 green, 1 pink blossom, 2 white blossom, 3 copper leaf
    std::uint64_t seed{};
};

enum class PropKind : std::uint8_t { shed, greenhouse, fountain, birdbath, grill, sandbox, pool, chair };

// Something standing on the grass that nothing drives through.
struct Prop {
    PropKind kind{};
    double x{};
    double y{};
    double rx{};  // half-sizes of its footprint, metres (round things: rx == ry)
    double ry{};
    double angle{};  // radians; buildings and loungers are turned in quarter turns
    bool occupied{};  // someone is on the lawn chair
    std::uint64_t seed{};
};

// Which kind of a thing it is, from its own seed: 0, 1 or 2. A grill is a kettle or (2) a
// barrel on a cart; a sandbox is planks (0) or a turtle; a chair is a lounger, a deckchair
// (1) or a lounger under a parasol (2).
inline int prop_style(const Prop& prop) {
    return static_cast<int>((prop.seed >> 33U) % 3U);
}

struct Mushroom {
    double x{};
    double y{};
    double size{};  // cap radius, metres
    int kind{};
    bool cut{};
};

struct Dandelion {
    double x{};
    double y{};
    double size{};
    bool clock{};  // a white seed head rather than a yellow flower
    bool cut{};
};

// A small flower growing in the lawn: 0 clover, 1 buttercup, 3 daisy, 4 a little blue one
// (the numbers are the lawn art's flower kinds).
struct Bloom {
    double x{};
    double y{};
    int kind{};
    bool cut{};
};

struct Garden {
    std::uint64_t seed{};
    std::vector<Bed> beds{};
    std::vector<Plant> plants{};
    std::vector<Tree> trees{};
    std::vector<Prop> props{};
    std::vector<Mushroom> mushrooms{};
    std::vector<Dandelion> dandelions{};
    std::vector<Bloom> blooms{};
    // lawn_cells_x * lawn_cells_y each:
    std::vector<std::uint8_t> obstacles{};  // 1 where the mower's own mind will not go: mulch and props
    std::vector<std::uint8_t> mulch{};      // 1 on mulch: beds and tree rings
    std::vector<std::uint8_t> solid{};      // 1 where nothing can be driven at all: trunks and props
    // Where the mower begins: a body's length in from the corner, facing up the lawn, so
    // its first move is along the edge and not into it.
    double start_x{1.2};
    double start_y{1.2};
    double start_heading{1.5707963267948966};
};

[[nodiscard]] Garden make_garden(std::uint64_t seed);
[[nodiscard]] bool inside_bed(const Bed& bed, double x, double y);
[[nodiscard]] bool inside_prop(const Prop& prop, double x, double y);
// True where the lawn is open grass (inside the lawn and not an obstacle cell).
[[nodiscard]] bool open_lawn(const Garden& garden, double x, double y);

// splitmix64: integer-only, identical everywhere.
std::uint64_t next_random(std::uint64_t& state);
double random_unit(std::uint64_t& state);
double random_range(std::uint64_t& state, double low, double high);

} // namespace mm
