#pragma once
// The mountain: a seeded, random-access, endless-feeling corridor of isometric
// tiles that climbs for hundreds of millions of rows. Nothing here depends on
// UI, audio, files or the wall clock; the same seed always yields the same
// mountain, and any row can be generated without generating the rows below it.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace eggy {

constexpr int kWidth = 10;            // walkable lateral tiles, u in [0, kWidth)
constexpr int kSegment = 256;         // rows per biome segment
constexpr double kSlope = 0.25;       // average rise per row (height units)
constexpr double kMaxHop = 0.80;      // tallest riser/obstacle a hop clears
constexpr double kCampEnd = 34;       // base camp: level ground before the climb
constexpr double kRampRows = 70;      // the slope eases in over these rows

enum class Biome : std::uint8_t { meadow, forest, autumn, pond, ravine, alpine, snow, ice, ridge, summit, count };
enum class Surface : std::uint8_t { grass, forest_floor, path, rock, gravel, snow, ice, water, sand, moss };
enum class Feature : std::uint8_t {
    none, flower, tuft, fern, mushroom, lichen, pebble, clover,
    tree, pine, boulder, stump, cairn, signpost, flags, bush,    // blocking
    log, rock, snowdrift,                                         // hop over
    lily, stone, puddle, ledge, crystal,                          // walkable specials
    campfire, crate                                               // base camp (blocking)
};

const char* biome_name(Biome b);

struct Tile {
    double z[4]{};  // corner heights: (u,v) (u+1,v) (u+1,v+1) (u,v+1)
    Surface surface = Surface::grass;
    Feature feature = Feature::none;
    Biome biome = Biome::meadow;
    std::uint8_t variant = 0;      // art variation
    std::uint8_t look = 0;         // ground texture id (see ground.hpp)
    float fx = .5f, fy = .5f;      // feature position inside the tile
    float flow = 0;                // brook flow (lateral) for water animation
    bool lane = false;             // on the guaranteed climbable lane
};

struct Row {
    std::int64_t v = -1;
    std::array<Tile, kWidth> t{};
    double riser = 0;              // height of the step up from row v-1 (front face)
    Biome biome = Biome::meadow;   // dominant biome
};

struct SegmentInfo {
    Biome biome = Biome::meadow;
    bool terraces = false;
    int terrace_rows = 2;
    bool brook = false;
    double brook_row = 0, brook_phase = 0, brook_amp = 2;
    bool autumn = false;
    float wind = 0;    // 0..1 gustiness
    float leaves = 0;  // 0..1 leaf fall
    float heat = 0;    // 0..1 sun baking
    float cold = 0;    // 0..1 freezing
    float rain = 0;    // 0..1 drizzle/mist
};

struct Star {
    std::int64_t v = 0;
    float u = 0;
};

class World {
public:
    explicit World(std::uint64_t seed);
    std::uint64_t seed() const { return seed_; }
    std::int64_t length() const { return length_; }      // summit row
    const Row& row(std::int64_t v) const;                // cached generation
    const Tile& tile(int u, std::int64_t v) const { return row(v).t[static_cast<size_t>(u)]; }
    SegmentInfo segment(std::int64_t k) const;
    SegmentInfo segment_at(double v) const { return segment(static_cast<std::int64_t>(std::floor(v / kSegment))); }
    double lane(double v) const;                         // lateral centre of the guaranteed lane
    double ground(double u, double v) const;             // surface height
    double base_near(std::int64_t v) const;              // row height at its near edge
    double base_far(std::int64_t v) const;               // row height at its far edge
    double base_at(double v) const;                      // smooth base height along the climb
    double climb_distance(double v) const;
    double camp_flatness(double v) const;
    int camp_fire_u() const;
    double altitude_m(double v) const { return ground(lane(v), v) * 4.0; }
    const std::vector<Star>& stars() const { return stars_; }
    int star_at(int u, std::int64_t v) const;            // index or -1
    static bool blocking(Feature f);
    static double hop_height(Feature f);                 // 0 when not a hop obstacle
    bool slippery(const Tile& t) const { return t.surface == Surface::ice && t.feature != Feature::ledge; }
    // exposed for tests and art
    std::uint8_t look_for(Surface s, Biome b, double u, double v) const;  // regional ground choice
    std::uint8_t flank_look(double u, std::int64_t v, bool steep) const;
    double hash01(std::int64_t a, std::int64_t b, std::int64_t c) const;
    double noise2(double x, double y, std::int64_t salt) const;
    double noise1(double x, std::int64_t salt) const;

private:
    std::uint64_t seed_;
    std::int64_t length_;
    std::vector<Star> stars_;
    mutable std::vector<Row> cache_;
    struct SegCache { std::int64_t k = -999; SegmentInfo info; };
    mutable SegCache seg_cache_[64];
    SegmentInfo compute_segment(std::int64_t k) const;
    void generate(std::int64_t v, Row& out) const;
    Biome pick_biome(std::int64_t k) const;
    double relief(double u, double v) const;
};

std::uint64_t mix64(std::uint64_t x);

}  // namespace eggy
