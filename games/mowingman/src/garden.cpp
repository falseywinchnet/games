#include "garden.hpp"

#include <algorithm>
#include <cmath>

namespace mm {
namespace {

constexpr double pi = 3.14159265358979323846;

// A few incommensurate waves: smooth, seedable, no tables. About -1..1.
double smooth_noise(double x, double y, std::uint64_t seed) {
    const double a = static_cast<double>(seed % 1000U) * 0.013;
    const double b = static_cast<double>((seed / 1000U) % 1000U) * 0.017;
    const double result = 0.5 * std::sin(x * 0.61 + a) * std::sin(y * 0.83 + b) +
                          0.3 * std::sin(x * 1.37 - y * 0.91 + a * 2.1) + 0.2 * std::sin(y * 2.03 + x * 0.47 + b * 1.7);
    return result;
}

// What has been placed so far, as boxes: enough to keep things apart.
struct Claim {
    double x{};
    double y{};
    double rx{};
    double ry{};
};

bool room_for(const std::vector<Claim>& claims, double x, double y, double rx, double ry) {
    for (const Claim& claim : claims) {
        if (std::abs(claim.x - x) < claim.rx + rx + mower_room && std::abs(claim.y - y) < claim.ry + ry + mower_room)
            return false;
    }
    return true;
}

// A place for a footprint: clear of the edges by more than a mower's width, clear of
// the corner the mower starts in, and clear of everything already placed.
bool find_place(std::uint64_t& random, const std::vector<Claim>& claims, double rx, double ry, double& x, double& y) {
    constexpr double edge = 1.7;
    for (int attempt = 0; attempt < 80; ++attempt) {
        x = random_range(random, edge + rx, lawn_width - edge - rx);
        y = random_range(random, edge + ry, lawn_height - edge - ry);
        if (x - rx < 3.4 && y - ry < 3.4)
            continue;
        if (room_for(claims, x, y, rx, ry))
            return true;
    }
    return false;
}

int pick(std::uint64_t& random, const double* chances, int count) {
    double roll = random_unit(random);
    for (int index = 0; index < count; ++index) {
        if (roll < chances[index])
            return index;
        roll -= chances[index];
    }
    return count - 1;
}

void mark(std::vector<std::uint8_t>& cells, int cx, int cy) {
    cells[static_cast<std::size_t>(cy) * static_cast<std::size_t>(lawn_cells_x) + static_cast<std::size_t>(cx)] = 1;
}

// Plants a bed: flowers scattered a plant's width apart, the two kinds (if two)
// keeping to their own sides of a line across the bed.
void plant(Garden& garden, int bed_index, std::uint64_t& random) {
    const Bed& bed = garden.beds[static_cast<std::size_t>(bed_index)];
    const double split_angle = random_range(random, 0, pi);
    const double nx = std::cos(split_angle);
    const double ny = std::sin(split_angle);
    const std::size_t first = garden.plants.size();
    const int tries = static_cast<int>(bed.rx * bed.ry * 260) + 40;
    for (int attempt = 0; attempt < tries; ++attempt) {
        const double x = random_range(random, bed.x - bed.rx, bed.x + bed.rx);
        const double y = random_range(random, bed.y - bed.ry, bed.y + bed.ry);
        const double side = (x - bed.x) * nx + (y - bed.y) * ny + 0.18 * std::sin((x + y) * 3.1);
        const Flower kind = bed.flowers[bed.kinds > 1 && side > 0 ? 1 : 0];
        const double reach = flower_reach(kind);
        // the whole plant stays inside the edging
        Bed inner = bed;
        inner.rx = std::max(0.05, bed.rx - reach * 0.55 - 0.06);
        inner.ry = std::max(0.05, bed.ry - reach * 0.55 - 0.06);
        if (!inside_bed(inner, x, y))
            continue;
        bool clear = true;
        for (std::size_t index = first; index < garden.plants.size() && clear; ++index) {
            const Plant& other = garden.plants[index];
            const double apart = (reach + flower_reach(other.kind)) * 0.60;
            clear = std::hypot(other.x - x, other.y - y) >= apart;
        }
        if (clear)
            garden.plants.push_back(Plant{kind, x, y, static_cast<std::uint32_t>(next_random(random)), bed_index, false});
    }
}

} // namespace

std::uint64_t next_random(std::uint64_t& state) {
    state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = state;
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
}

double random_unit(std::uint64_t& state) {
    const double result = static_cast<double>(next_random(state) >> 11U) * (1.0 / 9007199254740992.0);
    return result;
}

double random_range(std::uint64_t& state, double low, double high) {
    const double result = low + (high - low) * random_unit(state);
    return result;
}

const char* flower_name(Flower flower) {
    switch (flower) {
    case Flower::crocus:
        return "crocus";
    case Flower::rose:
        return "rose";
    case Flower::sunflower:
        return "sunflower";
    case Flower::tulip:
        return "tulip";
    case Flower::lily:
        return "lily";
    case Flower::orchid:
        return "orchid";
    case Flower::peony:
        return "peony";
    case Flower::hydrangea:
        return "hydrangea";
    case Flower::daisy:
        return "daisy";
    }
    return "daisy";
}

double flower_reach(Flower flower) {
    const double reach[flower_count] = {0.17, 0.45, 0.48, 0.21, 0.33, 0.26, 0.42, 0.50, 0.23};
    return reach[std::clamp(static_cast<int>(flower), 0, flower_count - 1)];
}

int plant_tone(const Garden& garden, const Plant& plant) {
    const Bed& bed = garden.beds[static_cast<std::size_t>(plant.bed)];
    return static_cast<int>((plant.kind == bed.flowers[0] ? bed.seed : bed.seed >> 8U) % 3U);
}

std::uint32_t flower_tint(Flower flower, int tone) {
    const std::uint32_t tints[flower_count][3] = {{0x8E5FD0, 0xF2F1EC, 0xF2C230}, {0xD0202A, 0xE98AA6, 0xF3EBD2}, {0xF2C230, 0xF2C230, 0xF09A26},
                                                  {0xD0202A, 0xF2C230, 0xE98AA6}, {0xF2F1EC, 0xF09A26, 0xE98AA6}, {0xD052B0, 0xF2F1EC, 0xB49AE0},
                                                  {0xE98AA6, 0xF2F1EC, 0xB8203C}, {0x7F9BE6, 0xE98AA6, 0xB49AE0}, {0xF2F1EC, 0xF2F1EC, 0xF3EBD2}};
    return tints[std::clamp(static_cast<int>(flower), 0, flower_count - 1)][std::clamp(tone, 0, 2)];
}

bool inside_bed(const Bed& bed, double x, double y) {
    const double dx = (x - bed.x) / bed.rx;
    const double dy = (y - bed.y) / bed.ry;
    if (bed.shape == BedShape::rounded) {
        // A superellipse: a rectangle with generous corners.
        const double result = std::pow(std::abs(dx), 4.0) + std::pow(std::abs(dy), 4.0) < 1.0;
        return result;
    }
    return dx * dx + dy * dy < 1.0;
}

bool inside_prop(const Prop& prop, double x, double y) {
    const double dx = x - prop.x;
    const double dy = y - prop.y;
    if (prop.kind == PropKind::fountain || prop.kind == PropKind::birdbath || prop.kind == PropKind::pool)
        return dx * dx + dy * dy < prop.rx * prop.rx;
    return std::abs(dx) < prop.rx && std::abs(dy) < prop.ry;
}

bool open_lawn(const Garden& garden, double x, double y) {
    if (x < 0 || y < 0 || x >= lawn_width || y >= lawn_height)
        return false;
    const int cx = std::min(lawn_cells_x - 1, static_cast<int>(x / lawn_cell));
    const int cy = std::min(lawn_cells_y - 1, static_cast<int>(y / lawn_cell));
    return garden.obstacles[static_cast<std::size_t>(cy) * static_cast<std::size_t>(lawn_cells_x) + static_cast<std::size_t>(cx)] == 0;
}

Garden make_garden(std::uint64_t seed) {
    Garden garden{};
    garden.seed = seed;
    std::uint64_t random = seed * 0x2545F4914F6CDD1DULL + 1;
    std::vector<Claim> claims{};

    // A shed or a greenhouse, its back against the top or the right-hand edge.
    if (random_unit(random) < 0.35) {
        Prop building{};
        building.kind = random_unit(random) < 0.6 ? PropKind::shed : PropKind::greenhouse;
        // half its length and depth: sheds come in sizes, from a tool store to a workshop
        const double along = building.kind == PropKind::shed ? random_range(random, 0.85, 1.45) : 1.4;
        const double deep = building.kind == PropKind::shed ? random_range(random, 0.7, 1.0) : 0.95;
        building.seed = next_random(random);
        if (random_unit(random) < 0.6) {
            building.rx = along;
            building.ry = deep;
            building.angle = 0;
            building.x = random_range(random, 4.5 + along, lawn_width - 1.7 - along);
            building.y = deep;
        } else {
            building.rx = deep;
            building.ry = along;
            building.angle = pi / 2;
            building.x = lawn_width - deep;
            building.y = random_range(random, 1.7 + along, lawn_height - 1.7 - along);
        }
        garden.props.push_back(building);
        claims.push_back(Claim{building.x, building.y, building.rx, building.ry});
    }

    // Trees: often none, sometimes a few.
    const double tree_chances[5] = {0.38, 0.26, 0.20, 0.11, 0.05};
    // (the lawn is four times the area it was: about twice as many as these)
    const int trees = 2 * pick(random, tree_chances, 5) + (random_unit(random) < 0.4 ? 1 : 0);
    for (int index = 0; index < trees; ++index) {
        Tree tree{};
        tree.trunk = random_range(random, 0.11, 0.17);
        // a small tree: its mulch shows all round, beyond the canopy
        tree.crown = random_range(random, 0.62, 0.95);
        tree.ring = tree.crown + random_range(random, 0.22, 0.38);
        const double kind_chances[4] = {0.50, 0.20, 0.15, 0.15};
        tree.kind = pick(random, kind_chances, 4);
        tree.seed = next_random(random);
        if (!find_place(random, claims, tree.ring, tree.ring, tree.x, tree.y))
            continue;
        garden.trees.push_back(tree);
        claims.push_back(Claim{tree.x, tree.y, tree.ring, tree.ring});
    }

    // Flower beds: sometimes none. One kind of flower to a bed as a rule, now and then two.
    const double bed_chances[4] = {0.24, 0.30, 0.30, 0.16};
    const int beds = pick(random, bed_chances, 4);
    const Flower theme = static_cast<Flower>(next_random(random) % static_cast<std::uint64_t>(flower_count));
    for (int index = 0; index < beds; ++index) {
        Bed bed{};
        bed.shape = random_unit(random) < 0.45 ? BedShape::rounded : BedShape::oval;
        bed.rx = random_range(random, 0.8, 1.7);
        bed.ry = random_range(random, 0.6, 1.2);
        // a garden mostly keeps to one flower; a bed may take another, or a second beside it
        bed.flowers[0] = random_unit(random) < 0.6 ? theme : static_cast<Flower>(next_random(random) % static_cast<std::uint64_t>(flower_count));
        bed.flowers[1] = static_cast<Flower>(next_random(random) % static_cast<std::uint64_t>(flower_count));
        bed.kinds = random_unit(random) < 0.25 && bed.flowers[1] != bed.flowers[0] ? 2 : 1;
        bed.seed = next_random(random);
        if (!find_place(random, claims, bed.rx, bed.ry, bed.x, bed.y))
            continue;
        garden.beds.push_back(bed);
        claims.push_back(Claim{bed.x, bed.y, bed.rx, bed.ry});
    }

    // Things left out on the grass.
    struct Extra {
        PropKind kind;
        double chance;
        double rx;
        double ry;
    };
    const double water = random_unit(random);
    const Extra extras[6] = {{PropKind::fountain, water < 0.25 ? 1.0 : 0.0, 0.62, 0.62},
                             {PropKind::birdbath, water >= 0.25 && water < 0.48 ? 1.0 : 0.0, 0.38, 0.38},
                             {PropKind::grill, 0.25, 0.42, 0.34},
                             {PropKind::sandbox, 0.20, 0.80, 0.80},
                             {PropKind::pool, 0.20, 0.85, 0.85},
                             {PropKind::chair, 0.32, 0.36, 0.82}};
    for (int pass = 0; pass < 2; ++pass)
    for (const Extra& extra : extras) {
        // a second pass may bring another of the things left about, but never a second fountain or bath
        if (pass == 1 && (extra.kind == PropKind::fountain || extra.kind == PropKind::birdbath))
            continue;
        const double roll = random_unit(random) * (pass == 0 ? 1.0 : 1.6);
        const bool turned = random_unit(random) < 0.5;
        const bool occupied = random_unit(random) < 0.5;
        const std::uint64_t prop_seed = next_random(random);
        if (roll >= extra.chance)
            continue;
        Prop prop{};
        prop.kind = extra.kind;
        prop.rx = turned ? extra.ry : extra.rx;
        prop.ry = turned ? extra.rx : extra.ry;
        prop.angle = turned ? pi / 2 : 0;
        prop.occupied = extra.kind == PropKind::chair && occupied;
        prop.seed = prop_seed;
        if (!find_place(random, claims, prop.rx, prop.ry, prop.x, prop.y))
            continue;
        garden.props.push_back(prop);
        claims.push_back(Claim{prop.x, prop.y, prop.rx, prop.ry});
    }

    // The three maps the rest of the game reads.
    const std::size_t cells = static_cast<std::size_t>(lawn_cells_x) * static_cast<std::size_t>(lawn_cells_y);
    garden.obstacles.assign(cells, 0);
    garden.mulch.assign(cells, 0);
    garden.solid.assign(cells, 0);
    for (int cy = 0; cy < lawn_cells_y; ++cy) {
        for (int cx = 0; cx < lawn_cells_x; ++cx) {
            const double x = (cx + 0.5) * lawn_cell;
            const double y = (cy + 0.5) * lawn_cell;
            for (const Bed& bed : garden.beds) {
                if (inside_bed(bed, x, y))
                    mark(garden.mulch, cx, cy);
            }
            for (const Tree& tree : garden.trees) {
                const double away = std::hypot(x - tree.x, y - tree.y);
                if (away < tree.ring)
                    mark(garden.mulch, cx, cy);
                if (away < tree.trunk)
                    mark(garden.solid, cx, cy);
            }
            for (const Prop& prop : garden.props) {
                if (inside_prop(prop, x, y))
                    mark(garden.solid, cx, cy);
            }
            const std::size_t index = static_cast<std::size_t>(cy) * static_cast<std::size_t>(lawn_cells_x) + static_cast<std::size_t>(cx);
            if (garden.mulch[index] != 0 || garden.solid[index] != 0)
                garden.obstacles[index] = 1;
        }
    }
    for (int index = 0; index < static_cast<int>(garden.beds.size()); ++index)
        plant(garden, index, random);

    // What grows in the grass. Not every lawn has the same flowers, and none has many.
    std::uint64_t flora = seed * 0x9FB21C651E98DF25ULL + 11;
    const bool clover = random_unit(flora) < 0.55;
    const bool buttercups = random_unit(flora) < 0.45;
    const bool dandelions = random_unit(flora) < 0.40;
    const bool daisies = random_unit(flora) < 0.40;
    const bool blue = random_unit(flora) < 0.30;
    const bool mushrooms = random_unit(flora) < 0.25;
    const std::uint64_t drift_seed = next_random(flora);
    const double area = lawn_width * lawn_height;
    struct Sown {
        bool grows;
        int kind;
        double per_m2;
        bool drifts;
    };
    const Sown sown[3] = {{clover, 0, 0.8, true}, {buttercups, 1, 0.12, false}, {daisies, 3, 0.35, true}};
    for (const Sown& flower : sown) {
        const int tries = static_cast<int>(flower.per_m2 * area * random_range(flora, 0.5, 1.5));
        for (int k = 0; k < tries; ++k) {
            const double x = random_range(flora, 0.3, lawn_width - 0.3);
            const double y = random_range(flora, 0.3, lawn_height - 0.3);
            const double roll = random_unit(flora);
            if (!flower.grows || !open_lawn(garden, x, y) || !open_lawn(garden, x + 0.3, y) || !open_lawn(garden, x - 0.3, y) ||
                !open_lawn(garden, x, y + 0.3) || !open_lawn(garden, x, y - 0.3))
                continue;
            if (flower.drifts) {
                const double field = smooth_noise(x * 1.3 + flower.kind * 7.0, y * 1.3, drift_seed);
                if (roll > std::clamp((field - 0.12) * 3.0, 0.0, 1.0))
                    continue;
            }
            garden.blooms.push_back(Bloom{x, y, flower.kind, false});
        }
    }
    if (blue) {
        const int count = 1 + static_cast<int>(next_random(flora) % 3U);
        const double cx = random_range(flora, 1.0, lawn_width - 1.0);
        const double cy = random_range(flora, 1.0, lawn_height - 1.0);
        for (int k = 0; k < count; ++k) {
            const double x = cx + random_range(flora, -0.2, 0.2);
            const double y = cy + random_range(flora, -0.2, 0.2);
            if (open_lawn(garden, x, y) && open_lawn(garden, x + 0.3, y) && open_lawn(garden, x - 0.3, y))
                garden.blooms.push_back(Bloom{x, y, 4, false});
        }
    }
    if (dandelions) {
        const int count = 5 + static_cast<int>(next_random(flora) % 10U);
        for (int k = 0; k < count; ++k) {
            const Dandelion flower{random_range(flora, 0.3, lawn_width - 0.3), random_range(flora, 0.3, lawn_height - 0.3),
                                   random_range(flora, 0.03, 0.05), random_unit(flora) < 0.3, false};
            if (open_lawn(garden, flower.x, flower.y) && open_lawn(garden, flower.x + 0.3, flower.y) && open_lawn(garden, flower.x - 0.3, flower.y))
                garden.dandelions.push_back(flower);
        }
    }
    if (mushrooms) {
        // One fairy ring, or part of one.
        const double cx = random_range(flora, 2.0, lawn_width - 2.0);
        const double cy = random_range(flora, 2.0, lawn_height - 2.0);
        const double radius = random_range(flora, 0.7, 1.6);
        const double arc = random_range(flora, 0.45, 1.0) * 2 * pi;
        const double begin = random_range(flora, 0, 2 * pi);
        const int count = 5 + static_cast<int>(arc * radius / 0.25 * random_range(flora, 0.5, 1.0));
        const int kind = static_cast<int>(next_random(flora) % 3U);
        for (int k = 0; k < count; ++k) {
            const double angle = begin + arc * (k + random_range(flora, 0, 0.8)) / count;
            const double r = radius + random_range(flora, -0.05, 0.05);
            const Mushroom mushroom{cx + std::cos(angle) * r, cy + std::sin(angle) * r, random_range(flora, 0.025, 0.06), kind, false};
            if (random_unit(flora) > 0.2 && open_lawn(garden, mushroom.x, mushroom.y) && open_lawn(garden, mushroom.x + 0.25, mushroom.y) &&
                open_lawn(garden, mushroom.x - 0.25, mushroom.y))
                garden.mushrooms.push_back(mushroom);
        }
    }
    return garden;
}

} // namespace mm
