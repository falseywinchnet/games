#pragma once
// The aviary's perches and where the birds settle on them. A table's perches are set out
// through a jungle grown inside a great glasshouse dome, in depth and at every height: a
// bare perch tree's branches, bamboo poles, an iron lamp standard, rope swings and a
// brass hoop hung from the ribs, the fountain's rim, a bird bath, a driftwood log. About twenty places on a dozen
// bars; birds on one bar are neighbours. Palms stand among them, and their canopies
// throw the shade. Each bird has a few likings (high or low, wood or
// rope, shade or sun, company or a bar to itself, no red bird beside it), and the birds
// are settled so that every one of them has a place it likes. The likings are dealt
// from a seating found first, so they can always be met: no table asks the impossible.
// Pure and deterministic: no drawing, no clock.
#include "platform/render.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pt {

enum class Stuff : std::uint8_t { wood, rope, brass, iron, stone };
const char* stuff_name(Stuff s);  // "wood", "rope", "brass", "iron", "stone"

enum class StandKind : std::uint8_t { tree, bamboo, lamp, swing, hoop, fountain, log, bath };

struct Stand {
    StandKind kind = StandKind::tree;
    V3 base{};          // where it stands on the floor, or for a swing or hoop, where it hangs from
    double reach = 1;   // its size: a tree's height, a pole's, a swing's drop
};

// A tree's crown, a disc of shade at its height: a palm's fronds or a broadleaf's leaves.
struct Canopy {
    V3 crown{};         // the top of the trunk
    V3 foot{};          // where the trunk stands
    double radius = 3;
    bool palm = true;
};

// The house: the floor's circle, the wall, the gallery round it, and the dome over all.
constexpr double kDomeR = 12.5, kWallH = 5.6, kDomeRise = 8.6, kGalleryZ = 5.0;
// The birds' size in the dome, and the room each takes on a bar.
constexpr double kBirdSize = 1.7, kSeatGap = .46 * kBirdSize;
// Where the player stands: low near the front, looking up into the dome.
inline V3 eye_at() { return {0, -9.6, 1.4}; }
// Toward the sun, high and a little to the left and in front.
inline V3 sun_dir() { return norm(V3{-.22, -.25, .94}); }

struct Bar {
    V3 a{}, b{};        // a straight bar from a to b (a hoop's bar is its lowest arc)
    Stuff stuff = Stuff::wood;
    int stand = 0;
};

struct Place {
    V3 pos{};           // where a bird's feet go
    int bar = 0;
    Stuff stuff = Stuff::wood;
    bool shade = false; // under the palm's canopy
    int rank = 0;       // height order: 0 the lowest place
};

struct Layout {
    std::vector<Stand> stands;
    std::vector<Bar> bars;
    std::vector<Place> places;
    std::vector<Canopy> canopies;
    [[nodiscard]] bool neighbours(int p, int q) const { return p != q && places[static_cast<size_t>(p)].bar == places[static_cast<size_t>(q)].bar; }
    [[nodiscard]] bool high(int p) const;   // in the upper third of the places
    [[nodiscard]] bool low(int p) const;    // in the lower third
};

// About twenty places, laid out afresh from `seed`.
Layout make_layout(std::uint64_t seed);

// What a bird likes in a place. Each bird has one to three.
enum class Liking : std::uint8_t {
    high,          // likes to sit high
    low,           // keeps near the floor
    wood,          // prefers wood
    rope,          // prefers rope
    brass,         // likes the brass
    iron,          // likes the ironwork
    stone,         // likes stone
    shade,         // wants shade
    sun,           // wants the sun
    company,       // wants at least one neighbour
    one_neighbour, // wants exactly one neighbour
    alone,         // wants a bar to itself
    no_red_beside, // refuses to sit beside a red bird
};
const char* liking_name(Liking l);

struct Bird {
    bool red = false;              // a scarlet bird (other birds may mind)
    std::vector<Liking> likes;
};

// Is bird i content at place `at[i]`, with everyone else where `at` says?
bool content(const Layout& L, const std::vector<Bird>& birds, const std::vector<int>& at, int i);

// Settles `birds.size()` birds: chooses a seating that keeps the flock together where it
// can (most birds share a bar with someone), then deals each bird one to three likings
// that seating meets, so the result always satisfies everyone. `red` says which birds
// are red. Returns the place of each bird; fills in each bird's likings.
std::vector<int> settle(const Layout& L, std::vector<Bird>& birds, std::uint64_t seed);

// The order the birds land in, and for some the place they try first and leave (the
// search the player sees): per bird, an optional first try (or -1), then the final place.
struct Landing {
    int bird = 0;
    int first_try = -1;
    int place = 0;
};
std::vector<Landing> landing_order(const Layout& L, const std::vector<Bird>& birds, const std::vector<int>& at, std::uint64_t seed);

}  // namespace pt
