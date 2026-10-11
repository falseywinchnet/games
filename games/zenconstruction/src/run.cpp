#include "run.hpp"
#include "terrain.hpp"

#include <algorithm>
#include <thread>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace zc {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFrame = 1.0 / 60.0;
constexpr int kFirstRocks = 50;
constexpr int kMoreRocks = 10;
constexpr double kFlySeconds = 0.9;
constexpr double kTravelSpeed = 0.38;   // m/s: the hook going to fetch, or home
constexpr double kLiftSpeed = 0.10;     // m/s: a rock just picked up, rising clear

struct Random {
    std::uint32_t state = 0;
};

double random_unit(Random& random) {
    random.state = random.state + 0x6d2b79f5u;
    std::uint32_t t = random.state;
    t = (t ^ (t >> 15)) * (t | 1u);
    t = t ^ (t + (t ^ (t >> 7)) * (t | 61u));
    const std::uint32_t mixed = t ^ (t >> 14);
    return static_cast<double>(mixed) / 4294967296.0;
}

double horizontal_distance(phys::Vec3 a, phys::Vec3 b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

phys::Quat normalized_quat(phys::Quat q) {
    const double n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (n < 1e-12) {
        return phys::Quat{};
    }
    return phys::Quat{q.w / n, q.x / n, q.y / n, q.z / n};
}

// normalised linear interpolation of orientations, along the shorter way
phys::Quat nlerp(phys::Quat a, phys::Quat b, double t) {
    double sign = 1;
    if (a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z < 0) {
        sign = -1;
    }
    const phys::Quat q{a.w + (sign * b.w - a.w) * t, a.x + (sign * b.x - a.x) * t, a.y + (sign * b.y - a.y) * t,
                       a.z + (sign * b.z - a.z) * t};
    return normalized_quat(q);
}

phys::Quat random_orientation(Random& random) {
    const double z = 2 * random_unit(random) - 1;
    const double angle = 2 * kPi * random_unit(random);
    const double ring = std::sqrt(std::max(0.0, 1 - z * z));
    const phys::Vec3 axis{ring * std::cos(angle), ring * std::sin(angle), z};
    return phys::from_axis_angle(axis, 2 * kPi * random_unit(random));
}

const char* place_name(Place place) {
    if (place == Place::stack) {
        return "stack";
    }
    if (place == Place::loose) {
        return "loose";
    }
    return "bowl";
}

Place place_from(const std::string& name) {
    if (name == "stack") {
        return Place::stack;
    }
    if (name == "loose") {
        return Place::loose;
    }
    return Place::bowl;
}

}  // namespace

const SiteLayout& site_layout() {
    static const SiteLayout layout;
    return layout;
}

phys::Vec3 crane_pivot() {
    const SiteLayout& layout = site_layout();
    return layout.crane_base - phys::Vec3{0.06 * std::cos(layout.crane_heading), 0.06 * std::sin(layout.crane_heading), 0};
}

double crane_angle(phys::Vec3 point) {
    const phys::Vec3 d = point - crane_pivot();
    return std::remainder(std::atan2(d.y, d.x) - site_layout().crane_heading, 2 * kPi);
}

phys::Vec3 crane_reachable(phys::Vec3 point) {
    const SiteLayout& layout = site_layout();
    const phys::Vec3 pivot = crane_pivot();
    const double radius = std::clamp(horizontal_distance(point, pivot), layout.crane_min_reach, layout.crane_reach);
    const double angle = layout.crane_heading + std::clamp(crane_angle(point), -layout.crane_slew_limit, layout.crane_slew_limit);
    return phys::Vec3{pivot.x + radius * std::cos(angle), pivot.y + radius * std::sin(angle), point.z};
}

double rock_top(const Rock& rock, const phys::Pose& pose) {
    double high = -1e9;
    const std::vector<phys::Vec3>& vertices = rock.shape.hulls[0].vertices;
    for (size_t k = 0; k < vertices.size(); k += 1) {
        const phys::Vec3 w = phys::rotate(pose.q, vertices[k]);
        high = std::max(high, w.z);
    }
    return pose.p.z + high;
}

double rock_bottom(const Rock& rock, const phys::Pose& pose) {
    double low = 1e9;
    const std::vector<phys::Vec3>& vertices = rock.shape.hulls[0].vertices;
    for (size_t k = 0; k < vertices.size(); k += 1) {
        const phys::Vec3 w = phys::rotate(pose.q, vertices[k]);
        low = std::min(low, w.z);
    }
    return pose.p.z + low;
}

Run::Run() {
    new_world();
}

void Run::new_world() {
    phys::WorldParams params;
    params.ground_friction = 0.9;   // sand
    params.ground_surface = worksite_ground();
    world_ = std::make_unique<phys::World>(params);
    add_bowl(*world_);
}

// The bowl: a floor and a ring of wall segments that flare outward, each a
// static convex hull.
void Run::add_bowl(phys::World& world) const {
    const SiteLayout& layout = site_layout();
    const int segments = 24;
    for (int k = 0; k < segments; k += 1) {
        const double a0 = 2 * kPi * k / segments;
        const double a1 = 2 * kPi * (k + 1) / segments;
        const double angles[2] = {a0, a1};
        phys::HullDesc hull;
        for (int side = 0; side < 2; side += 1) {
            const double c = std::cos(angles[side]);
            const double s = std::sin(angles[side]);
            const double r_floor = layout.bowl_floor_radius;
            const double r_rim = layout.bowl_rim_radius;
            const double w = layout.bowl_wall;
            hull.points.push_back(phys::Vec3{r_floor * c, r_floor * s, layout.bowl_floor});
            hull.points.push_back(phys::Vec3{r_rim * c, r_rim * s, layout.bowl_height});
            hull.points.push_back(phys::Vec3{(r_floor + w) * c, (r_floor + w) * s, 0.0});
            hull.points.push_back(phys::Vec3{(r_rim + w) * c, (r_rim + w) * s, layout.bowl_height});
        }
        phys::ShapeDesc desc;
        desc.hulls.push_back(hull);
        desc.friction = 0.6;
        const phys::Shape shape = phys::cook(desc);
        phys::Pose pose;
        pose.p = layout.bowl_centre + shape.com_offset;
        world.add_static_body(shape, pose);
    }
    // the boulder the crane stands on: a broad, low, rounded slab
    {
        phys::HullDesc rock;
        const int around = 16;
        for (int k = 0; k < around; k += 1) {
            const double a = 2 * kPi * k / around;
            const double c = std::cos(a);
            const double s = std::sin(a);
            rock.points.push_back(phys::Vec3{0.30 * c, 0.20 * s, 0.0});
            rock.points.push_back(phys::Vec3{0.27 * c, 0.18 * s, layout.crane_boulder_top * 0.75});
            rock.points.push_back(phys::Vec3{0.21 * c, 0.13 * s, layout.crane_boulder_top});
        }
        phys::ShapeDesc desc;
        desc.hulls.push_back(rock);
        desc.friction = 0.8;
        const phys::Shape shape = phys::cook(desc);
        phys::Pose pose;
        pose.p = layout.crane_base + shape.com_offset;
        world.add_static_body(shape, pose);
    }
    phys::HullDesc floor;
    for (int k = 0; k < segments; k += 1) {
        const double a = 2 * kPi * k / segments;
        const double r = layout.bowl_floor_radius + layout.bowl_wall;
        floor.points.push_back(phys::Vec3{r * std::cos(a), r * std::sin(a), 0.0});
        floor.points.push_back(phys::Vec3{r * std::cos(a), r * std::sin(a), layout.bowl_floor});
    }
    phys::ShapeDesc desc;
    desc.hulls.push_back(floor);
    desc.friction = 0.6;
    const phys::Shape shape = phys::cook(desc);
    phys::Pose pose;
    pose.p = layout.bowl_centre + shape.com_offset;
    world.add_static_body(shape, pose);
}

phys::BodyId Run::add_rock_body(int index, const phys::Pose& pose, bool asleep) {
    RockState& state = rocks[static_cast<size_t>(index)];
    const phys::Vec3 still{0, 0, 0};
    state.body = world_->add_body(state.rock.shape, pose, still, still, asleep);
    return state.body;
}

// The top of the heap in the bowl: the highest point of any rock lying in it,
// or the bowl's floor.
double Run::heap_top() const {
    const SiteLayout& layout = site_layout();
    double top = layout.bowl_floor;
    for (size_t i = 0; i < rocks.size(); i += 1) {
        if (rocks[i].body >= 0 && rocks[i].place == Place::bowl) {
            const phys::Pose pose = world_->state(rocks[i].body).pose;
            if (horizontal_distance(pose.p, layout.bowl_centre) < layout.bowl_rim_radius) {
                top = std::max(top, rock_top(rocks[i].rock, pose));
            }
        }
    }
    return top;
}

// Settles the world (at most `limit` frames).
void Run::settle(int limit) {
    for (int f = 0; f < limit; f += 1) {
        if (f > 20 && world_->rest_report().quiet) {
            return;
        }
        world_->step();
    }
}

// A height map over the bowl: the floor and the flaring walls, then the top
// of every rock laid in so far. Cells are 1 cm.
struct BowlHeights {
    double x0 = 0, y0 = 0, cell = 0.01;
    int n = 0;
    std::vector<double> h;
};

void init_heights(BowlHeights& map, const SiteLayout& layout) {
    const double reach = layout.bowl_rim_radius + layout.bowl_wall;
    map.n = static_cast<int>(std::ceil(2 * reach / map.cell));
    map.x0 = layout.bowl_centre.x - reach;
    map.y0 = layout.bowl_centre.y - reach;
    map.h.assign(static_cast<size_t>(map.n) * static_cast<size_t>(map.n), 10.0);
    for (int j = 0; j < map.n; j += 1) {
        for (int i = 0; i < map.n; i += 1) {
            const double x = map.x0 + (i + 0.5) * map.cell - layout.bowl_centre.x;
            const double y = map.y0 + (j + 0.5) * map.cell - layout.bowl_centre.y;
            const double r = std::sqrt(x * x + y * y);
            double floor = 10.0;
            if (r <= layout.bowl_floor_radius) {
                floor = layout.bowl_floor;
            } else if (r <= layout.bowl_rim_radius - 0.01) {
                const double t = (r - layout.bowl_floor_radius) / (layout.bowl_rim_radius - layout.bowl_floor_radius);
                floor = layout.bowl_floor + t * (layout.bowl_height - layout.bowl_floor) + 0.01;
            }
            map.h[static_cast<size_t>(j) * static_cast<size_t>(map.n) + static_cast<size_t>(i)] = floor;
        }
    }
}

// the cell under a world point, or -1 outside the map
int heights_cell(const BowlHeights& map, double x, double y) {
    const int i = static_cast<int>(std::floor((x - map.x0) / map.cell));
    const int j = static_cast<int>(std::floor((y - map.y0) / map.cell));
    if (i < 0 || j < 0 || i >= map.n || j >= map.n) {
        return -1;
    }
    return j * map.n + i;
}

// Points over a rock's surface (body frame, rotated): each far-mesh triangle's
// corners, edge midpoints and centre, dense enough for 1 cm cells.
void surface_samples(const Rock& rock, phys::Quat q, std::vector<phys::Vec3>& out) {
    out.clear();
    const std::vector<Vtx>& tris = rock.far_mesh.triangles;
    for (size_t k = 0; k + 2 < tris.size(); k += 3) {
        const phys::Vec3 a{tris[k].p.x, tris[k].p.y, tris[k].p.z};
        const phys::Vec3 b{tris[k + 1].p.x, tris[k + 1].p.y, tris[k + 1].p.z};
        const phys::Vec3 c{tris[k + 2].p.x, tris[k + 2].p.y, tris[k + 2].p.z};
        const phys::Vec3 points[7] = {a, b, c, (a + b) * 0.5, (b + c) * 0.5, (c + a) * 0.5, (a + b + c) * (1.0 / 3.0)};
        for (int p = 0; p < 7; p += 1) {
            out.push_back(phys::rotate(q, points[p]));
        }
    }
}

// Makes rocks [first, first + count) of this run on several threads (each
// rock depends only on the seed and its number) and appends them.
void make_rock_range(std::uint32_t seed, int first, int stride, std::vector<RockState>* out) {
    for (size_t k = static_cast<size_t>(first); k < (*out).size(); k += static_cast<size_t>(stride)) {
        (*out)[k].rock = make_rock(seed, (*out)[k].rock.index);
    }
}

// The scratch world's bodies, by the rock each one is (-1 for the bowl).
void set_owner(std::vector<int>& owner, phys::BodyId id, int index) {
    if (static_cast<size_t>(id) >= owner.size()) {
        owner.resize(static_cast<size_t>(id) + 1, -1);
    }
    owner[static_cast<size_t>(id)] = index;
}

// What rests on what, from contacts between rocks: covers at a normal 0.2 from
// level or steeper, supports at 0.5.
void note_rests(const std::vector<phys::Contact>& contacts, const std::vector<int>& owner, std::vector<std::pair<int, int>>& covers,
                std::vector<std::pair<int, int>>& supports) {
    for (size_t c = 0; c < contacts.size(); c += 1) {
        const phys::Contact& contact = contacts[c];
        int ra = -1;
        int rb = -1;
        if (contact.a >= 0 && static_cast<size_t>(contact.a) < owner.size()) {
            ra = owner[static_cast<size_t>(contact.a)];
        }
        if (contact.b >= 0 && static_cast<size_t>(contact.b) < owner.size()) {
            rb = owner[static_cast<size_t>(contact.b)];
        }
        if (ra < 0 || rb < 0 || contact.normal_impulse <= 0) {
            continue;
        }
        // the normal is the direction b pushes a: up means a sits on b
        std::pair<int, int> cover(-1, -1);
        if (contact.normal.z >= 0.2) {
            cover = std::pair<int, int>(ra, rb);
        } else if (contact.normal.z <= -0.2) {
            cover = std::pair<int, int>(rb, ra);
        }
        if (cover.first < 0) {
            continue;
        }
        if (std::find(covers.begin(), covers.end(), cover) == covers.end()) {
            covers.push_back(cover);
        }
        if (std::abs(contact.normal.z) >= 0.5 && std::find(supports.begin(), supports.end(), cover) == supports.end()) {
            supports.push_back(cover);
        }
    }
}

// Lays rocks [first, first + count) into the bowl. Each is set flat at a
// random turn at the lowest of a dozen spots on the height map, then settled
// on its own in a scratch world where the bowl and every rock before it are
// fixed and the air is thick, so each settle is one light body coming to rest
// in a few frames. The top layer then settles once more together. The rocks
// go into the real world asleep where they came to rest, with what rests on
// what taken from the scratch contacts. (Fifty rocks settled as one heap
// would cost a heavy solve a frame for two seconds; this takes a fraction of
// that.)
void Run::pour(int first, int count) {
    const SiteLayout& layout = site_layout();
    Random random;
    random.state = seed_ * 2654435761u + static_cast<std::uint32_t>(first) * 97u + 13u;
    phys::WorldParams params;
    params.ground_friction = 0.9;
    params.ground_surface = worksite_ground();
    // Thick enough to soak up a bounce, thin enough that a rock on a slope it
    // can't hold still slides (more drag and it creeps slower than `still_speed`
    // and would pass for resting).
    params.linear_damping = 1.5;
    params.angular_damping = 1.5;
    const int min_frames = 8;
    const int kTopLayer = 16;
    const double still_speed = 0.001;   // m/s at the rim of the rock
    phys::World scratch(params);
    add_bowl(scratch);
    std::vector<int> owner;             // scratch body -> rock index
    BowlHeights map;
    init_heights(map, layout);
    std::vector<phys::Vec3> samples;
    // the rocks already in the bowl are fixed in place
    for (size_t i = 0; i < rocks.size(); i += 1) {
        if (rocks[i].body < 0 || static_cast<int>(i) >= first) {
            continue;
        }
        const phys::Pose pose = world_->state(rocks[i].body).pose;
        if (!in_bowl(pose)) {
            continue;
        }
        set_owner(owner, scratch.add_static_body(rocks[i].rock.shape, pose), static_cast<int>(i));
        surface_samples(rocks[i].rock, pose.q, samples);
        for (size_t k = 0; k < samples.size(); k += 1) {
            const int cell = heights_cell(map, pose.p.x + samples[k].x, pose.p.y + samples[k].y);
            if (cell >= 0) {
                map.h[static_cast<size_t>(cell)] = std::max(map.h[static_cast<size_t>(cell)], pose.p.z + samples[k].z);
            }
        }
    }
    std::vector<phys::Pose> rest(static_cast<size_t>(count));
    std::vector<phys::BodyId> fixed_body(static_cast<size_t>(count), -1);
    for (int k = 0; k < count; k += 1) {
        const int index = first + k;
        const Rock& rock = rocks[static_cast<size_t>(index)].rock;
        // flat, at a random turn, tipped a little
        const phys::Quat turn = phys::from_axis_angle(phys::Vec3{0, 0, 1}, 2 * kPi * random_unit(random));
        const phys::Vec3 tilt_axis{random_unit(random) - 0.5, random_unit(random) - 0.5, 0};
        const phys::Quat tilt = phys::from_axis_angle(phys::normalized(tilt_axis + phys::Vec3{1e-9, 0, 0}), 0.25 * (random_unit(random) - 0.5));
        const phys::Quat q = normalized_quat(phys::multiply(tilt, phys::multiply(turn, flat_side_down(rock))));
        surface_samples(rock, q, samples);
        phys::Pose pose;
        pose.q = q;
        phys::BodyId id = -1;
        for (int attempt = 0; attempt < 4; attempt += 1) {
            // the lowest of a dozen spots, with a little preference for the middle
            double best_score = 1e9;
            pose.p = phys::Vec3{layout.bowl_centre.x, layout.bowl_centre.y, heap_top() + 0.05};
            for (int spot = 0; spot < 12; spot += 1) {
                const double angle = 2 * kPi * random_unit(random);
                const double reach = layout.bowl_floor_radius * 0.85 * std::sqrt(random_unit(random));
                const double x = layout.bowl_centre.x + reach * std::cos(angle);
                const double y = layout.bowl_centre.y + reach * std::sin(angle);
                double lowest = -1e9;
                bool inside = true;
                for (size_t s = 0; s < samples.size(); s += 1) {
                    const int cell = heights_cell(map, x + samples[s].x, y + samples[s].y);
                    if (cell < 0) {
                        inside = false;
                        break;
                    }
                    lowest = std::max(lowest, map.h[static_cast<size_t>(cell)] - samples[s].z);
                }
                if (!inside || lowest > 9) {
                    continue;
                }
                const double score = lowest + 0.02 * reach / layout.bowl_floor_radius;
                if (score < best_score) {
                    best_score = score;
                    pose.p = phys::Vec3{x, y, lowest + 0.002};
                }
            }
            if (id < 0) {
                id = scratch.add_body(rock.shape, pose);
            } else {
                scratch.set_pose(id, pose);
            }
            int calm = 0;
            for (int f = 0; f < 180; f += 1) {
                scratch.step();
                const phys::BodyState now = scratch.state(id);
                const double speed = phys::length(now.v) + phys::length(now.w) * 0.5 * rock.diameter;
                calm = speed < still_speed ? calm + 1 : 0;
                if (f >= min_frames && (calm >= 10 || now.asleep)) {
                    break;
                }
            }
            if (in_bowl(scratch.state(id).pose)) {
                break;
            }
        }
        pose = scratch.state(id).pose;
        rest[static_cast<size_t>(k)] = pose;
        // what it came to rest on
        set_owner(owner, id, index);
        note_rests(scratch.contacts(), owner, covers_, supports_);
        // fixed from now on, for the rocks that land on it
        scratch.remove_body(id);
        const phys::BodyId fixed = scratch.add_static_body(rock.shape, pose);
        set_owner(owner, fixed, index);
        fixed_body[static_cast<size_t>(k)] = fixed;
        surface_samples(rock, pose.q, samples);
        for (size_t s = 0; s < samples.size(); s += 1) {
            const int cell = heights_cell(map, pose.p.x + samples[s].x, pose.p.y + samples[s].y);
            if (cell >= 0) {
                map.h[static_cast<size_t>(cell)] = std::max(map.h[static_cast<size_t>(cell)], pose.p.z + samples[s].z);
            }
        }
    }
    // The top layer settles again, together: rocks laid one at a time on fixed
    // ones can perch where a heap that gives a little won't hold them.
    std::vector<int> order;
    for (int k = 0; k < count; k += 1) {
        order.push_back(k);
    }
    for (size_t i = 1; i < order.size(); i += 1) {
        for (size_t j = i; j > 0 && rest[static_cast<size_t>(order[j])].p.z > rest[static_cast<size_t>(order[j - 1])].p.z; j -= 1) {
            std::swap(order[j], order[j - 1]);
        }
    }
    const int top = std::min(count, kTopLayer);
    std::vector<phys::BodyId> loose(static_cast<size_t>(top), -1);
    for (int n = 0; n < top; n += 1) {
        const int k = order[static_cast<size_t>(n)];
        scratch.remove_body(fixed_body[static_cast<size_t>(k)]);
        loose[static_cast<size_t>(n)] = scratch.add_body(rocks[static_cast<size_t>(first + k)].rock.shape, rest[static_cast<size_t>(k)]);
        set_owner(owner, loose[static_cast<size_t>(n)], first + k);
    }
    int calm = 0;
    for (int f = 0; f < 150 && calm < 10; f += 1) {
        scratch.step();
        bool still = true;
        for (int n = 0; n < top; n += 1) {
            const phys::BodyState now = scratch.state(loose[static_cast<size_t>(n)]);
            const double reach = 0.5 * rocks[static_cast<size_t>(first + order[static_cast<size_t>(n)])].rock.diameter;
            if (!now.asleep && phys::length(now.v) + phys::length(now.w) * reach >= still_speed) {
                still = false;
            }
        }
        calm = still ? calm + 1 : 0;
    }
    std::vector<char> moved(rocks.size(), 0);
    for (int n = 0; n < top; n += 1) {
        const int k = order[static_cast<size_t>(n)];
        rest[static_cast<size_t>(k)] = scratch.state(loose[static_cast<size_t>(n)]).pose;
        moved[static_cast<size_t>(first + k)] = 1;
    }
    std::vector<std::pair<int, int>> kept;
    for (size_t e = 0; e < covers_.size(); e += 1) {
        if (!moved[static_cast<size_t>(covers_[e].first)] && !moved[static_cast<size_t>(covers_[e].second)]) {
            kept.push_back(covers_[e]);
        }
    }
    covers_ = kept;
    kept.clear();
    for (size_t e = 0; e < supports_.size(); e += 1) {
        if (!moved[static_cast<size_t>(supports_[e].first)] && !moved[static_cast<size_t>(supports_[e].second)]) {
            kept.push_back(supports_[e]);
        }
    }
    supports_ = kept;
    note_rests(scratch.contacts(), owner, covers_, supports_);
    for (int k = 0; k < count; k += 1) {
        add_rock_body(first + k, rest[static_cast<size_t>(k)], true);
        rocks[static_cast<size_t>(first + k)].place = Place::bowl;
    }
}

void Run::make_rocks(int first, int count) {
    std::vector<RockState> made(static_cast<size_t>(count));
    for (int k = 0; k < count; k += 1) {
        made[static_cast<size_t>(k)].rock.index = first + k;
    }
    const int workers = std::max(1, std::min(4, static_cast<int>(std::thread::hardware_concurrency())));
    std::vector<std::thread> threads;
    for (int w = 1; w < workers; w += 1) {
        threads.push_back(std::thread(make_rock_range, seed_, w, workers, &made));
    }
    make_rock_range(seed_, 0, workers, &made);
    for (size_t t = 0; t < threads.size(); t += 1) {
        threads[t].join();
    }
    for (size_t k = 0; k < made.size(); k += 1) {
        rocks.push_back(made[k]);
    }
}

void Run::begin(std::uint32_t seed, const std::string& company) {
    seed_ = seed;
    company_ = company;
    best_ = 0;
    start_over();
}

void Run::start_over() {
    rocks.clear();
    supports_.clear();
    covers_.clear();
    base_ = -1;
    height_ = 0;
    stack_count_ = 0;
    last_stack_count_ = 0;
    crane = Crane();
    crane.hook = site_layout().hook_rest;
    new_world();
    make_rocks(0, kFirstRocks);
    generated_ = kFirstRocks;
    pour(0, kFirstRocks);
    sort_rocks();
    remember_quiet();
    sorted_since_quiet_ = true;
}

// Ten more rocks, once every rock has left the bowl.
void Run::generate_more() {
    const int first = generated_;
    make_rocks(first, kMoreRocks);
    generated_ += kMoreRocks;
    pour(first, kMoreRocks);
}

// ---------------------------------------------------------------- queries

bool Run::in_bowl(const phys::Pose& pose) const {
    const SiteLayout& layout = site_layout();
    const double reach = horizontal_distance(pose.p, layout.bowl_centre);
    return reach < layout.bowl_rim_radius + 0.02 && pose.p.z < layout.bowl_height + 0.3;
}

phys::Pose Run::rock_pose(int index) const {
    const RockState& state = rocks[static_cast<size_t>(index)];
    if (state.body >= 0) {
        return world_->state(state.body).pose;
    }
    // flying back to the bowl: an arc
    const double t = std::min(1.0, std::max(0.0, state.fly_t));
    const double ease = t * t * (3 - 2 * t);
    phys::Pose pose;
    pose.p = state.fly_from.p + (state.fly_to.p - state.fly_from.p) * ease;
    pose.p.z = pose.p.z + 0.22 * std::sin(kPi * t);
    pose.q = nlerp(state.fly_from.q, state.fly_to.q, ease);
    return pose;
}

double Run::stack_top() const {
    double top = 0;
    for (size_t i = 0; i < rocks.size(); i += 1) {
        if (rocks[i].place == Place::stack && rocks[i].body >= 0) {
            top = std::max(top, rock_top(rocks[i].rock, world_->state(rocks[i].body).pose));
        }
    }
    return top;
}

bool Run::quiet() const {
    if (crane.attached) {
        return false;
    }
    for (size_t i = 0; i < rocks.size(); i += 1) {
        if (rocks[i].place == Place::flying) {
            return false;
        }
    }
    // everything asleep: the engine's rest report can call a still-hanging rock
    // quiet, and the moment it's let go it isn't
    return world_->all_asleep();
}

bool Run::busy() const {
    return crane.mode != CraneMode::parked;
}

int Run::rock_at_ray(phys::Vec3 origin, phys::Vec3 direction) const {
    const std::optional<std::pair<phys::BodyId, double>> hit = world_->raycast(origin, phys::normalized(direction), 20.0);
    if (!hit.has_value()) {
        return -1;
    }
    const phys::BodyId body = (*hit).first;
    for (size_t i = 0; i < rocks.size(); i += 1) {
        if (rocks[i].body == body) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// `upper` rests on `lower`, by the support graph of the last sort.
bool Run::resting_on(int upper, int lower) const {
    for (size_t k = 0; k < supports_.size(); k += 1) {
        if (supports_[k].first == upper && supports_[k].second == lower) {
            return true;
        }
    }
    return false;
}

void Run::forget_supports(int rock) {
    std::vector<std::pair<int, int>> kept_covers;
    for (size_t k = 0; k < covers_.size(); k += 1) {
        if (covers_[k].first != rock && covers_[k].second != rock) {
            kept_covers.push_back(covers_[k]);
        }
    }
    covers_ = kept_covers;
    std::vector<std::pair<int, int>> kept;
    for (size_t k = 0; k < supports_.size(); k += 1) {
        if (supports_[k].first != rock && supports_[k].second != rock) {
            kept.push_back(supports_[k]);
        }
    }
    supports_ = kept;
}

// Rebuilds the support graph from the world's retained contacts (a loaded
// contact whose normal points up from the lower rock to the upper). A rock
// with no retained contact at all hasn't been touched since it was loaded
// asleep: its saved edges still hold.
void Run::update_supports() {
    const std::vector<phys::Contact> contacts = world_->retained_contacts();
    std::vector<int> owner;
    for (size_t i = 0; i < rocks.size(); i += 1) {
        const phys::BodyId body = rocks[i].body;
        if (body >= 0) {
            if (static_cast<size_t>(body) >= owner.size()) {
                owner.resize(static_cast<size_t>(body) + 1, -1);
            }
            owner[static_cast<size_t>(body)] = static_cast<int>(i);
        }
    }
    std::vector<char> seen(rocks.size(), 0);
    std::vector<char> grounded(rocks.size(), 0);
    std::vector<std::pair<int, int>> fresh;
    std::vector<std::pair<int, int>> fresh_covers;
    for (size_t k = 0; k < contacts.size(); k += 1) {
        const phys::Contact& c = contacts[k];
        int ra = -1;
        int rb = -1;
        if (c.a >= 0 && static_cast<size_t>(c.a) < owner.size()) {
            ra = owner[static_cast<size_t>(c.a)];
        }
        if (c.b >= 0 && static_cast<size_t>(c.b) < owner.size()) {
            rb = owner[static_cast<size_t>(c.b)];
        }
        if (ra >= 0) {
            seen[static_cast<size_t>(ra)] = 1;
        }
        if (rb >= 0) {
            seen[static_cast<size_t>(rb)] = 1;
        }
        // Only a contact that touches and bears weight: the engine also reports near
        // misses a little apart, and a rock hovering over the grass has not fallen.
        if (ra >= 0 && c.b == phys::kGround && c.separation <= 0.001 && c.normal_impulse > 1e-6) {
            grounded[static_cast<size_t>(ra)] = 1;
        }
        if (ra < 0 || rb < 0) {
            continue;
        }
        // the normal is the direction b pushes a: up means a sits on b
        std::pair<int, int> cover(-1, -1);
        if (c.normal.z >= 0.2) {
            cover = std::pair<int, int>(ra, rb);
        } else if (c.normal.z <= -0.2) {
            cover = std::pair<int, int>(rb, ra);
        }
        if (cover.first >= 0) {
            bool known_cover = false;
            for (size_t e = 0; e < fresh_covers.size(); e += 1) {
                if (fresh_covers[e] == cover) {
                    known_cover = true;
                }
            }
            if (!known_cover) {
                fresh_covers.push_back(cover);
            }
        }
        std::pair<int, int> edge(-1, -1);
        if (c.normal.z >= 0.5) {
            edge = std::pair<int, int>(ra, rb);
        } else if (c.normal.z <= -0.5) {
            edge = std::pair<int, int>(rb, ra);
        }
        if (edge.first < 0) {
            continue;
        }
        bool known = false;
        for (size_t e = 0; e < fresh.size(); e += 1) {
            if (fresh[e] == edge) {
                known = true;
            }
        }
        if (!known) {
            fresh.push_back(edge);
        }
    }
    for (size_t k = 0; k < supports_.size(); k += 1) {
        const std::pair<int, int>& edge = supports_[k];
        const bool untouched = !seen[static_cast<size_t>(edge.first)] && !seen[static_cast<size_t>(edge.second)];
        const bool present = rocks[static_cast<size_t>(edge.first)].body >= 0 && rocks[static_cast<size_t>(edge.second)].body >= 0;
        if (untouched && present) {
            fresh.push_back(edge);
        }
    }
    for (size_t k = 0; k < covers_.size(); k += 1) {
        const std::pair<int, int>& edge = covers_[k];
        const bool untouched = !seen[static_cast<size_t>(edge.first)] && !seen[static_cast<size_t>(edge.second)];
        const bool present = rocks[static_cast<size_t>(edge.first)].body >= 0 && rocks[static_cast<size_t>(edge.second)].body >= 0;
        if (untouched && present) {
            fresh_covers.push_back(edge);
        }
    }
    // a rock untouched since it was loaded asleep keeps what it was touching
    for (size_t i = 0; i < rocks.size() && i < grounded_.size(); i += 1) {
        if (!seen[i]) {
            grounded[i] = grounded_[i];
        }
    }
    supports_ = fresh;
    covers_ = fresh_covers;
    grounded_ = grounded;
}

bool Run::can_fetch(int index) const {
    if (index < 0 || index >= static_cast<int>(rocks.size())) {
        return false;
    }
    if (crane.mode != CraneMode::parked && crane.mode != CraneMode::returning) {
        return false;
    }
    const RockState& state = rocks[static_cast<size_t>(index)];
    if (state.body < 0) {
        return false;
    }
    const phys::Vec3 position = (*world_).state(state.body).pose.p;
    if (horizontal_distance(position, crane_reachable(position)) > 0.002) {
        return false; // a fallen or restored rock beyond a mechanical stop
    }
    // only a rock with nothing resting on it, in the bowl or on the stack: the
    // crane's wires lift at most one and a half times the rock's own weight
    for (size_t k = 0; k < covers_.size(); k += 1) {
        if (covers_[k].second == index && rocks[static_cast<size_t>(covers_[k].first)].body >= 0) {
            return false;
        }
    }
    for (size_t k = 0; k < wedged_.size(); k += 1) {
        if (wedged_[k] == index) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------- sorting

// When the site is quiet: which rocks are in the stack, which in the bowl,
// which lying loose; the height; collapses and records.
void Run::sort_rocks() {
    update_supports();
    wedged_.clear();
    const size_t count = rocks.size();
    std::vector<Place> before(count);
    for (size_t i = 0; i < count; i += 1) {
        before[i] = rocks[i].place;
    }
    for (size_t i = 0; i < count; i += 1) {
        RockState& state = rocks[i];
        if (state.body < 0 || state.place == Place::held) {
            continue;
        }
        const phys::Pose pose = world_->state(state.body).pose;
        state.place = in_bowl(pose) ? Place::bowl : Place::loose;
    }
    if (base_ >= 0 && rocks[static_cast<size_t>(base_)].body >= 0 && rocks[static_cast<size_t>(base_)].place != Place::held) {
        rocks[static_cast<size_t>(base_)].place = Place::stack;
        // grow the stack upward through vertical, unmoving contact
        bool grew = true;
        while (grew) {
            grew = false;
            for (size_t k = 0; k < supports_.size(); k += 1) {
                RockState& upper = rocks[static_cast<size_t>(supports_[k].first)];
                const RockState& lower = rocks[static_cast<size_t>(supports_[k].second)];
                const bool on_ground = grounded_[static_cast<size_t>(supports_[k].first)] != 0;
                if (lower.place == Place::stack && upper.place == Place::loose && !on_ground && world_->resting(upper.body)) {
                    upper.place = Place::stack;
                    grew = true;
                }
            }
        }
    }
    height_ = 0;
    stack_count_ = 0;
    bool fell = false;
    for (size_t i = 0; i < count; i += 1) {
        if (rocks[i].place == Place::stack) {
            stack_count_ += 1;
            height_ = std::max(height_, rock_top(rocks[i].rock, world_->state(rocks[i].body).pose));
        }
        if (before[i] == Place::stack && rocks[i].place == Place::loose) {
            fell = true;
        }
    }
    // A rock resting on the stack with a foot on the ground has toppled: the stack fell.
    for (size_t k = 0; k < supports_.size(); k += 1) {
        const int upper = supports_[k].first;
        if (upper != base_ && grounded_[static_cast<size_t>(upper)] != 0 &&
            rocks[static_cast<size_t>(supports_[k].second)].place == Place::stack &&
            rocks[static_cast<size_t>(upper)].place == Place::loose && before[static_cast<size_t>(upper)] != Place::loose) {
            fell = true;
        }
    }
    if (fell) {
        out().collapsed = true;
    }
    if (stack_count_ > 0 && height_ > best_ + 0.0005) {
        best_ = height_;
        out().new_best = true;
    }
    last_stack_count_ = stack_count_;
    out().settled = true;
}

// Rocks lying loose fly back into the bowl, one arc each.
void Run::start_tidy() {
    const SiteLayout& layout = site_layout();
    Random random;
    random.state = static_cast<std::uint32_t>(seed_ * 31u + static_cast<std::uint32_t>(generated_) * 7u + 99u);
    for (size_t i = 0; i < rocks.size(); i += 1) {
        RockState& state = rocks[i];
        if (state.place != Place::loose || state.body < 0) {
            continue;
        }
        state.fly_from = world_->state(state.body).pose;
        world_->remove_body(state.body);
        state.body = -1;
        state.place = Place::flying;
        state.fly_t = 0;
        const double angle = 2 * kPi * random_unit(random);
        const double reach = layout.bowl_floor_radius * 0.5 * random_unit(random);
        state.fly_to.q = random_orientation(random);
        state.fly_to.p = phys::Vec3{layout.bowl_centre.x + reach * std::cos(angle), layout.bowl_centre.y + reach * std::sin(angle), 0};
        state.fly_to.p.z = layout.bowl_height + 0.06 - rock_bottom(state.rock, state.fly_to) + state.fly_to.p.z;
        out().tidied = true;
    }
}

void Run::step_flying(double dt) {
    for (size_t i = 0; i < rocks.size(); i += 1) {
        RockState& state = rocks[i];
        if (state.place != Place::flying) {
            continue;
        }
        state.fly_t += dt / kFlySeconds;
        if (state.fly_t >= 1) {
            add_rock_body(static_cast<int>(i), state.fly_to, false);
            state.place = Place::bowl;
            out().landed = true;
        }
    }
}

void Run::remember_quiet() {
    quiet_poses_.resize(rocks.size());
    quiet_places_.resize(rocks.size());
    for (size_t i = 0; i < rocks.size(); i += 1) {
        quiet_poses_[i] = rock_pose(static_cast<int>(i));
        quiet_places_[i] = rocks[i].place;
    }
}

// Contacts that appear with a real impulse: rocks knocking together, for sound.
void Run::note_impacts() {
    const std::vector<phys::Contact> contacts = world_->contacts();
    std::vector<double> now(rocks.size(), 0.0);
    std::vector<int> owner;
    for (size_t i = 0; i < rocks.size(); i += 1) {
        const phys::BodyId body = rocks[i].body;
        if (body >= 0) {
            if (static_cast<size_t>(body) >= owner.size()) {
                owner.resize(static_cast<size_t>(body) + 1, -1);
            }
            owner[static_cast<size_t>(body)] = static_cast<int>(i);
        }
    }
    for (size_t k = 0; k < contacts.size(); k += 1) {
        const phys::Contact& c = contacts[k];
        if (c.a < 0 || static_cast<size_t>(c.a) >= owner.size()) {
            continue;
        }
        const int rock = owner[static_cast<size_t>(c.a)];
        if (rock >= 0) {
            now[static_cast<size_t>(rock)] = std::max(now[static_cast<size_t>(rock)], c.normal_impulse);
        }
    }
    last_impulse_.resize(rocks.size(), 0.0);
    was_awake_.resize(rocks.size(), 0);
    for (size_t i = 0; i < rocks.size(); i += 1) {
        const phys::BodyId body = rocks[i].body;
        const bool awake = body >= 0 && !world_->state(body).asleep;
        const double rise = now[i] - last_impulse_[i];
        const double weight_per_frame = rocks[i].rock.shape.mass * 9.81 * kFrame;
        // A knock: a jump well beyond the rock's own weight that at least
        // doubles what it was carrying. A rock just woken (its contacts were
        // asleep, so it seemed to carry nothing) or merely taking more of the
        // heap's load doesn't count.
        if (awake && was_awake_[i] && rise > 0.004 && rise > 1.6 * weight_per_frame && rise > last_impulse_[i]) {
            out().knocks += 1;
            out().hardest_impact = std::max(out().hardest_impact, rise);
        }
        last_impulse_[i] = now[i];
        was_awake_[i] = awake ? 1 : 0;
    }
}

// ---------------------------------------------------------------- the crane

double Run::travel_height() const {
    const SiteLayout& layout = site_layout();
    double height = std::max(0.34, layout.bowl_height + 0.26);
    height = std::max(height, stack_top() + 0.24);
    return std::min(height, 1.5);
}

void Run::plan_path_to(phys::Vec3 goal) {
    crane.path.clear();
    crane.path_speed = kTravelSpeed;
    goal = crane_reachable(goal);
    const double high = std::max(travel_height(), goal.z);
    const phys::Vec3 from = crane.attached ? crane.target.p : crane.hook;
    if (from.z < high - 0.005) {
        crane.path.push_back(phys::Vec3{from.x, from.y, high});
    }
    // Travel through the permitted arc, never take a straight chord through
    // the cab or wrap around the rear mechanical stop.
    const phys::Vec3 pivot = crane_pivot();
    const double a0 = crane_angle(from), a1 = crane_angle(goal);
    const double r0 = horizontal_distance(from, pivot), r1 = horizontal_distance(goal, pivot);
    const int segments = std::max(1, static_cast<int>(std::ceil(std::abs(a1 - a0) / 0.12)));
    for (int k = 1; k <= segments; k += 1) {
        const double t = static_cast<double>(k) / segments;
        const double angle = site_layout().crane_heading + a0 + (a1 - a0) * t;
        const double radius = r0 + (r1 - r0) * t;
        crane.path.push_back(phys::Vec3{pivot.x + radius * std::cos(angle), pivot.y + radius * std::sin(angle), high});
    }
    crane.path.push_back(goal);
}

// Moves the hook (or, when attached, the hold target) along the path, easing
// into each stop.
void Run::follow_path(double dt) {
    if (crane.path.empty()) {
        return;
    }
    phys::Vec3& at = crane.attached ? crane.target.p : crane.hook;
    const phys::Vec3 next = crane.path.front();
    const phys::Vec3 gap = next - at;
    const double distance = phys::length(gap);
    const double speed = crane.path_speed * std::min(1.0, 0.25 + distance / 0.12);
    const double stride = speed * dt;
    if (distance <= stride || distance < 0.0005) {
        at = next;
        crane.path.erase(crane.path.begin());
        return;
    }
    at = crane_reachable(at + gap * (stride / distance));
}

void Run::attach() {
    RockState& state = rocks[static_cast<size_t>(crane.rock)];
    const phys::Pose pose = world_->state(state.body).pose;
    crane.target = pose;
    crane.velocity = phys::Vec3{};
    crane.angular = phys::Vec3{};
    // a firm grip to pull the rock clear of whatever it lies among; the gentle
    // one (the physics defaults) takes over once the player has it
    // the crane's motor pulls the same whatever the rock weighs: at least 25 N
    // (a small stone among big ones comes free, nudging them), or 3 x its weight
    phys::HoldParams firm;
    const double weight = state.rock.shape.mass * 9.81;
    firm.max_lift = std::max(3.0, 25.0 / weight);
    firm.max_lateral = std::max(1.0, 10.0 / weight);
    firm.max_torque = std::max(1.0, 10.0 / weight);
    world_->hold(state.body, crane.target, firm);
    forget_supports(crane.rock);
    crane.attached = true;
    if (crane.rock == base_) {
        base_ = -1;   // unwinding the last rock of the stack
    }
    state.place = Place::held;
    out().grabbed = true;
}

void Run::detach() {
    world_->release();
    crane.attached = false;
    RockState& state = rocks[static_cast<size_t>(crane.rock)];
    state.place = Place::loose;
    if (base_ < 0) {
        const phys::Pose pose = world_->state(state.body).pose;
        if (!in_bowl(pose)) {
            base_ = crane.rock;   // the first rock placed is the stack's foot
        }
    }
    crane.mode = CraneMode::releasing;
    crane.timer = 0;
    out().released = true;
    sorted_since_quiet_ = false;
}

bool Run::fetch(int index) {
    if (!can_fetch(index)) {
        return false;
    }
    crane.rock = index;
    crane.mode = CraneMode::fetching;
    crane.wires = 0;
    const phys::Pose pose = world_->state(rocks[static_cast<size_t>(index)].body).pose;
    const double top = rock_top(rocks[static_cast<size_t>(index)].rock, pose);
    plan_path_to(phys::Vec3{pose.p.x, pose.p.y, top + crane.hang});
    return true;
}

void Run::release() {
    if (crane.attached && (crane.mode == CraneMode::steering || crane.mode == CraneMode::lifting)) {
        detach();
    }
}

// Shaking the bowl: every rock lying in it hops a little, outward this way or that and
// back toward the middle so none leaves the bowl, turning as it goes, and settles anew.
bool Run::jiggle() {
    if (!world_)
        return false;
    const SiteLayout& layout = site_layout();
    Random random;
    random.state = static_cast<std::uint32_t>(seed_ * 131u + jiggles_ * 977u + 5u);
    jiggles_ += 1;
    int moved = 0;
    for (size_t i = 0; i < rocks.size(); i += 1) {
        RockState& state = rocks[i];
        if (state.place != Place::bowl || state.body < 0 || (crane.attached && crane.rock == static_cast<int>(i))) {
            continue;
        }
        const phys::BodyState now = world_->state(state.body);
        phys::Vec3 inward{layout.bowl_centre.x - now.pose.p.x, layout.bowl_centre.y - now.pose.p.y, 0};
        const double reach = phys::length(inward);
        inward = reach > 1e-6 ? inward * (1 / reach) : phys::Vec3{};
        const double angle = 2 * kPi * random_unit(random);
        const double push = 0.05 + 0.08 * random_unit(random);
        const phys::Vec3 v{std::cos(angle) * push + inward.x * 0.14 * reach / layout.bowl_floor_radius,
                           std::sin(angle) * push + inward.y * 0.14 * reach / layout.bowl_floor_radius,
                           0.32 + 0.22 * random_unit(random)};
        const phys::Vec3 w{(random_unit(random) - 0.5) * 6, (random_unit(random) - 0.5) * 6, (random_unit(random) - 0.5) * 3};
        phys::Pose pose = now.pose;
        pose.p.z += 0.003;  // just clear of what it lay on
        world_->remove_body(state.body);
        state.body = world_->add_body(state.rock.shape, pose, v, w);
        moved += 1;
    }
    return moved > 0;
}

void Run::throw_back() {
    if (!crane.attached || (crane.mode != CraneMode::steering && crane.mode != CraneMode::lifting)) {
        return;
    }
    const SiteLayout& layout = site_layout();
    const RockState& state = rocks[static_cast<size_t>(crane.rock)];
    const phys::Pose pose = world_->state(state.body).pose;
    const double below = pose.p.z - rock_bottom(state.rock, pose);
    crane.mode = CraneMode::carrying_back;
    crane.release_over_bowl = true;
    plan_path_to(phys::Vec3{layout.bowl_centre.x, layout.bowl_centre.y, layout.bowl_height + 0.12 + below});
}

void Run::cancel() {
    if (crane.mode == CraneMode::fetching || crane.mode == CraneMode::attaching) {
        crane.mode = CraneMode::returning;
        crane.rock = -1;
        crane.wires = 0;
        plan_path_to(site_layout().hook_rest);
    }
}

// keeps the hold target where the crane can reach and the wires can do something
void Run::clamp_target() {
    phys::Vec3& p = crane.target.p;
    p = crane_reachable(p);
    p.z = std::min(p.z, 1.45);
    // lowering past the point where the wires go slack does nothing more
    const RockState& state = rocks[static_cast<size_t>(crane.rock)];
    const double actual = world_->state(state.body).pose.p.z;
    p.z = std::max(p.z, actual - 0.035);
}

void Run::step_crane(const CraneInput& input, double camera_yaw, double camera_pitch, double dt) {
    (void)camera_yaw;
    (void)camera_pitch;
    const SiteLayout& layout = site_layout();
    if (crane.mode == CraneMode::parked) {
        crane.hook = crane.hook + (layout.hook_rest - crane.hook) * std::min(1.0, dt * 2);
    } else if (crane.mode == CraneMode::fetching) {
        // the rock may have shifted (the bowl settling): keep the goal over it
        if (!crane.path.empty() && crane.rock >= 0 && rocks[static_cast<size_t>(crane.rock)].body >= 0) {
            const RockState& state = rocks[static_cast<size_t>(crane.rock)];
            const phys::Pose pose = world_->state(state.body).pose;
            phys::Vec3& goal = crane.path.back();
            goal = crane_reachable(phys::Vec3{pose.p.x, pose.p.y, rock_top(state.rock, pose) + crane.hang});
        }
        follow_path(dt);
        if (crane.path.empty()) {
            crane.mode = CraneMode::attaching;
            crane.timer = 0;
        }
    } else if (crane.mode == CraneMode::attaching) {
        crane.wires = std::min(1.0, crane.wires + dt / 0.45);
        if (crane.wires >= 1) {
            attach();
            crane.mode = CraneMode::lifting;
            crane.timer = 0;
            // Straight up, slowly, until it's clear of everything; then it's the
            // player's to swing over and set down.
            const RockState& state = rocks[static_cast<size_t>(crane.rock)];
            const double below = crane.target.p.z - rock_bottom(state.rock, crane.target);
            const double clear = std::max(travel_height(), stack_top() + below + 0.10);
            crane.path.clear();
            crane.path.push_back(phys::Vec3{crane.target.p.x, crane.target.p.y, std::max(clear, crane.target.p.z)});
            crane.path_speed = kLiftSpeed;
        }
    } else if (crane.mode == CraneMode::lifting || crane.mode == CraneMode::carrying_back) {
        // a rock that won't come up (caught under another) is let go rather than strained at
        const RockState& held = rocks[static_cast<size_t>(crane.rock)];
        const phys::Pose actual = world_->state(held.body).pose;
        if (crane.target.p.z - actual.p.z > 0.04) {
            crane.timer += dt;
        } else {
            crane.timer = 0;
        }
        if (crane.timer > 1.5) {
            out().stuck = true;
            wedged_.push_back(crane.rock);   // not fetchable again until the site next settles
            detach();
            return;
        }
        follow_path(dt);
        if (crane.path.empty()) {
            if (crane.mode == CraneMode::carrying_back) {
                detach();
            } else {
                crane.mode = CraneMode::steering;
                phys::HoldParams gentle;
                gentle.sling_length = rock_top(held.rock, crane.target) - crane.target.p.z + crane.hang;
                world_->hold(held.body, crane.target, gentle);
            }
        }
    } else if (crane.mode == CraneMode::steering) {
        // Fine work at a fifth of the pace, a hurry at over twice it; and a big rock comes
        // round more slowly than a small one (by a gentle power of its volume).
        const double pace = input.fine ? 0.2 : input.fast ? 2.2 : 1.0;
        double heavy = 1;
        if (crane.rock >= 0 && crane.rock < static_cast<int>(rocks.size())) {
            const double volume = rocks[static_cast<size_t>(crane.rock)].rock.shape.volume;
            if (volume > 0)
                heavy = std::clamp(std::pow(0.0006 / volume, 0.35), 0.55, 1.25);
        }
        const double speed = 0.11 * pace * heavy;
        const double lift = 0.07 * pace * heavy;
        const double turn = 1.1 * pace * std::sqrt(heavy);
        // the arrows work the crane as its operator does: up and down telescope
        // the boom out and in along its line, left and right swing it round
        const phys::Vec3 pivot = crane_pivot();
        const phys::Vec3 out_from_crane{crane.target.p.x - pivot.x, crane.target.p.y - pivot.y, 0};
        const phys::Vec3 boom_line = phys::length(out_from_crane) > 1e-6 ? phys::normalized(out_from_crane) : phys::Vec3{1, 0, 0};
        const phys::Vec3 swing_right{boom_line.y, -boom_line.x, 0};
        const phys::Vec3 wanted = swing_right * (input.right * speed) + boom_line * (input.forward * speed) + phys::Vec3{0, 0, input.up * lift};
        const double ease = std::min(1.0, dt * 9);
        crane.velocity = crane.velocity + (wanted - crane.velocity) * ease;
        crane.target.p = crane.target.p + crane.velocity * dt;
        // rotations by the crane's own directions, never the camera's: turn
        // about the vertical, tip toward or away from the crane (about the
        // swing's axis), roll about the boom's line
        const phys::Vec3 wanted_turn{input.pitch * turn, input.roll * turn, input.yaw * turn};
        crane.angular = crane.angular + (wanted_turn - crane.angular) * ease;
        const phys::Quat pitch = phys::from_axis_angle(swing_right, crane.angular.x * dt);
        const phys::Quat roll = phys::from_axis_angle(boom_line, crane.angular.y * dt);
        const phys::Quat yaw = phys::from_axis_angle(phys::Vec3{0, 0, 1}, crane.angular.z * dt);
        phys::Quat q = phys::multiply(pitch, crane.target.q);
        q = phys::multiply(roll, q);
        q = phys::multiply(yaw, q);
        crane.target.q = normalized_quat(q);
        clamp_target();
    } else if (crane.mode == CraneMode::releasing) {
        crane.wires = std::max(0.0, crane.wires - dt / 0.4);
        crane.hook.z = crane.hook.z + 0.05 * dt;
        if (crane.wires <= 0) {
            crane.mode = CraneMode::returning;
            crane.rock = -1;
            plan_path_to(layout.hook_rest);
        }
    } else if (crane.mode == CraneMode::returning) {
        follow_path(dt);
        if (crane.path.empty()) {
            crane.mode = CraneMode::parked;
        }
    }
    if (crane.attached) {
        world_->set_hold_target(crane.target);
        // the hook hangs above the target by the rock's height over its centre, plus the slings
        const RockState& state = rocks[static_cast<size_t>(crane.rock)];
        const double above = rock_top(state.rock, crane.target) - crane.target.p.z;
        crane.hook = crane.target.p + phys::Vec3{0, 0, above + crane.hang};
    }
}

void Run::step(const CraneInput& input, double camera_yaw, double camera_pitch) {
    // what happened since the last step (a release by hand), then this step's own
    events_ = queued_;
    queued_ = RunEvents();
    in_step_ = true;
    step_crane(input, camera_yaw, camera_pitch, kFrame);
    world_->step();
    step_flying(kFrame);
    note_impacts();
    if (quiet()) {
        quiet_for_ += kFrame;
        if (!sorted_since_quiet_) {
            sort_rocks();
            sorted_since_quiet_ = true;
            start_tidy();
            if (!out().tidied) {
                remember_quiet();
            }
        }
    } else {
        quiet_for_ = 0;
    }
    // rocks that land in the bowl after a tidy settle; sort again then
    bool any_flying = false;
    for (size_t i = 0; i < rocks.size(); i += 1) {
        if (rocks[i].place == Place::flying) {
            any_flying = true;
        }
    }
    if (out().tidied || any_flying) {
        sorted_since_quiet_ = false;
    }
    // the bowl never runs dry: new rocks once every rock has been used
    if (!busy() && quiet()) {
        bool any_in_bowl = false;
        for (size_t i = 0; i < rocks.size(); i += 1) {
            if (rocks[i].place == Place::bowl) {
                any_in_bowl = true;
            }
        }
        if (!any_in_bowl) {
            generate_more();
            sorted_since_quiet_ = false;
        }
    }
    in_step_ = false;
}

// ---------------------------------------------------------------- saving

std::string Run::save() const {
    std::ostringstream out;
    out << "seed=" << seed_ << "\n";
    out << "company=" << company_ << "\n";
    char buffer[512];
    std::snprintf(buffer, sizeof buffer, "best=%.17g\n", best_);
    out << buffer;
    out << "base=" << base_ << "\n";
    out << "generated=" << generated_ << "\n";
    for (size_t k = 0; k < supports_.size(); k += 1) {
        out << "rests=" << supports_[k].first << "," << supports_[k].second << "\n";
    }
    for (size_t k = 0; k < covers_.size(); k += 1) {
        out << "covers=" << covers_[k].first << "," << covers_[k].second << "\n";
    }
    const size_t count = std::min(quiet_poses_.size(), rocks.size());
    for (size_t i = 0; i < count; i += 1) {
        const phys::Pose& p = quiet_poses_[i];
        std::snprintf(buffer, sizeof buffer, "rock=%zu,%s,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g\n", i, place_name(quiet_places_[i]), p.p.x,
                      p.p.y, p.p.z, p.q.w, p.q.x, p.q.y, p.q.z);
        out << buffer;
    }
    return out.str();
}

bool Run::load(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    std::uint32_t seed = 0;
    std::string company;
    double best = 0;
    int base = -1;
    int generated = 0;
    std::vector<Place> places;
    std::vector<phys::Pose> poses;
    std::vector<std::pair<int, int>> rests;
    std::vector<std::pair<int, int>> covers;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "seed") {
            seed = static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
        } else if (key == "company") {
            company = value;
        } else if (key == "best") {
            best = std::strtod(value.c_str(), nullptr);
        } else if (key == "base") {
            base = std::atoi(value.c_str());
        } else if (key == "generated") {
            generated = std::atoi(value.c_str());
        } else if (key == "covers") {
            const size_t comma = value.find(',');
            if (comma == std::string::npos) {
                return false;
            }
            covers.push_back(std::pair<int, int>(std::atoi(value.substr(0, comma).c_str()), std::atoi(value.substr(comma + 1).c_str())));
        } else if (key == "rests") {
            const size_t comma = value.find(',');
            if (comma == std::string::npos) {
                return false;
            }
            rests.push_back(std::pair<int, int>(std::atoi(value.substr(0, comma).c_str()), std::atoi(value.substr(comma + 1).c_str())));
        } else if (key == "rock") {
            std::vector<std::string> parts;
            std::string part;
            std::istringstream fields(value);
            while (std::getline(fields, part, ',')) {
                parts.push_back(part);
            }
            if (parts.size() != 9) {
                return false;
            }
            const size_t index = static_cast<size_t>(std::atoi(parts[0].c_str()));
            if (index != places.size()) {
                return false;
            }
            places.push_back(place_from(parts[1]));
            phys::Pose pose;
            pose.p = phys::Vec3{std::strtod(parts[2].c_str(), nullptr), std::strtod(parts[3].c_str(), nullptr), std::strtod(parts[4].c_str(), nullptr)};
            pose.q = phys::Quat{std::strtod(parts[5].c_str(), nullptr), std::strtod(parts[6].c_str(), nullptr),
                                std::strtod(parts[7].c_str(), nullptr), std::strtod(parts[8].c_str(), nullptr)};
            const double norm = pose.q.w * pose.q.w + pose.q.x * pose.q.x + pose.q.y * pose.q.y + pose.q.z * pose.q.z;
            if (std::abs(norm - 1) > 1e-9) {
                pose.q = normalized_quat(pose.q);   // only a hand-edited file needs this; saved ones load bit for bit
            }
            poses.push_back(pose);
        }
    }
    if (generated < kFirstRocks || static_cast<int>(places.size()) != generated || base >= generated) {
        return false;
    }
    for (size_t k = 0; k < rests.size(); k += 1) {
        if (rests[k].first < 0 || rests[k].second < 0 || rests[k].first >= generated || rests[k].second >= generated) {
            return false;
        }
    }
    for (size_t k = 0; k < covers.size(); k += 1) {
        if (covers[k].first < 0 || covers[k].second < 0 || covers[k].first >= generated || covers[k].second >= generated) {
            return false;
        }
    }
    supports_ = rests;
    covers_ = covers;
    seed_ = seed;
    company_ = company;
    best_ = best;
    base_ = base;
    generated_ = generated;
    crane = Crane();
    crane.hook = site_layout().hook_rest;
    new_world();
    rocks.clear();
    make_rocks(0, generated);
    for (int i = 0; i < generated; i += 1) {
        rocks[static_cast<size_t>(i)].place = places[static_cast<size_t>(i)];
    }
    for (int i = 0; i < generated; i += 1) {
        add_rock_body(i, poses[static_cast<size_t>(i)], true);
    }
    // heights and the stack, from the saved arrangement
    height_ = 0;
    stack_count_ = 0;
    for (int i = 0; i < generated; i += 1) {
        if (rocks[static_cast<size_t>(i)].place == Place::stack) {
            stack_count_ += 1;
            height_ = std::max(height_, rock_top(rocks[static_cast<size_t>(i)].rock, poses[static_cast<size_t>(i)]));
        }
    }
    last_stack_count_ = stack_count_;
    remember_quiet();
    sorted_since_quiet_ = true;
    return true;
}

}  // namespace zc
