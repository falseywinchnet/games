#include "still.hpp"

#include "particles.hpp"

#include <array>
#include <chrono>
#include <span>

namespace cf {
namespace {

// ---- The place ----------------------------------------------------------------------

constexpr double wall_z = 85;           // the cliff face, metres ahead
constexpr double deck_height = 12;      // the walkway's deck above the pool
// The walkway runs straight across the view high on the forest's edge, the pool and
// its clearing below and beyond it. The eye stands on it toward its back. Its own frame:
// u along it, v across it (toward the falls), the eye at u = 0, v = eye_v.
constexpr double walkway_heading = 0;
constexpr double walkway_half = 1.5;
constexpr double eye_v = -.9;
// The pool's near shore, metres ahead, and its half-width at the cliff.
constexpr double near_shore = 26;
double ground_height(double x, double z);
Vec3 to_walkway(Vec3 p);
double outside_pool(double x, double z);
constexpr double gravity = 9.81;
constexpr double brink_speed = .9;      // m/s downward as the water leaves the brink

// The river leaves the cliff in several channels between wooded outcrops, as at
// Iguazu: a broad main fall split by a spur, lacy curtains, a thread.
struct Channel {
    double left = 0, right = 0;  // metres across the wall
    double level = 0;            // the river's surface at its brink
    double depth = 0;            // water over the brink
    double ledge = 0;            // a shelf the curtain lands on and pours on from, or 0
};
// One broad, continuous curtain across the wall, thick in its middle and thinning to
// lace at its ends, its brink rising and falling a little along its length.
constexpr int channel_count = 1;
constexpr Channel channels[channel_count] = {
    {-46, 42, 40.2, .5, 0},
};

// Where the river is at `x`: the rock under it at the brink, the water's surface, and
// how much of a channel this is (0 on the outcrops between).
struct Brink {
    double rock = 0, level = 0, inside = 0;
    int channel = -1;
};
Brink brink_at(double x) {
    const double outside = 41.6 + 1.6 * fbm(x / 11, 1.3, 21, 3) + .9 * fbm(x / 2.7, 9.1, 22, 3) + .35 * fbm(x / .8, 2.2, 23, 2);
    Brink out{outside, 0, 0};
    for (int i = 0; i < channel_count; ++i) {
        const Channel& ch = channels[i];
        const double w = smoothstep(ch.left - 1.4, ch.left + .5, x) * (1 - smoothstep(ch.right - .5, ch.right + 1.4, x));
        if (w <= out.inside)
            continue;
        const double across = (x - (ch.left + ch.right) / 2) / ((ch.right - ch.left) / 2);
        const double fullness = std::max(.18, 1 - std::pow(std::fabs(across), 3)) *
                                (.7 + .3 * (.5 + .5 * fbm(x / 3.1, i * 3.1, 11, 3)));
        const double level = ch.level + 1.1 * fbm(x / 17, 4.4, 13, 2) + .35 * fbm(x / 4.5, 1.9, 14, 2) +
                             .1 * fbm(x / 1.2, 6.1, 15, 2);
        const double floor = level - ch.depth * fullness - .04 * fbm(x / .7, 3.7, 12, 2);
        out.inside = w;
        out.channel = i;
        out.level = level;
        out.rock = lerp(outside, floor, w);
    }
    if (out.inside <= 0)
        out.level = out.rock - 1;
    return out;
}
double rock_top(double x) {
    return brink_at(x).rock;
}
// Metres from `x` to the nearest channel's edge, 0 inside one.
double from_channel(double x) {
    double best = 1e9;
    for (int i = 0; i < channel_count; ++i) {
        const double d = std::max(channels[i].left - x, x - channels[i].right);
        best = std::min(best, std::max(0.0, d));
    }
    return best;
}

// Seconds after leaving the brink at which water has fallen `drop` metres.
double flight_time(double drop) {
    return (-brink_speed + std::sqrt(brink_speed * brink_speed + 2 * gravity * drop)) / gravity;
}
double fall_speed(double drop) {
    return std::sqrt(brink_speed * brink_speed + 2 * gravity * drop);
}

// How far the rock stands out toward the eye at (x, y), metres: basalt laid down in
// flows a few metres thick, each a face over a recessed ledge, broken into columns.
double relief(double x, double y) {
    // Flows of uneven thickness that wander, pinch out and split along the wall.
    const double warp = 4.5 * fbm(x / 28, y / 16, 31, 3) + 1.4 * fbm(x / 7, y / 6, 32, 3);
    const double thickness = 5.5 + 2.5 * fbm(x / 22, 7.7, 30, 2);
    const double layer = (y + warp) / thickness;
    const double index = std::floor(layer);
    const double f = layer - index;
    // Within a flow the face leans out a little toward its top, then steps back by an
    // amount that differs from flow to flow and along each; some flows show no step.
    const double recess = 1.6 * std::max(0.0, fbm(x / 12, index * 7.3, 33, 3) + .15);
    const double lip = .74 + .16 * fbm(x / 3, index * 3.1, 34, 2);
    const double step = recess * (smoothstep(0, lip, f) - smoothstep(lip, 1, f));
    // Columnar joints: tall blocks of differing set-back with deep cracks between, and
    // larger blocks broken out of the face.
    const Cells joints = cells(x / 1.9, (y + warp) / 6.5, 37);
    const double crack = joints.second - joints.nearest;
    const double column = .5 * smoothstep(0, .3, crack) + .45 * (unit(joints.id) - .5);
    const Cells blocks = cells(x / 6.5, (y + warp) / 5, 38);
    const double broken = .7 * (unit(blocks.id) - .5) * smoothstep(0, .15, blocks.second - blocks.nearest);
    const double rough = .3 * fbm(x / 1.1, y / 1.1, 41, 4) + .09 * noise2(x / .2, y / .2, 42);
    return step + column + broken + rough;
}

// ---- Sampling ------------------------------------------------------------------------

struct Ray {
    Vec3 origin, direction;
};

// One crown of a tree above the rim, or a palm.
struct Crown {
    double x = 0, y = 0, z = 0;  // its centre, metres
    double radius = 0;
    int lobes = 0;
    std::array<double, 7> lobe_x{}, lobe_y{}, lobe_r{};  // in units of radius
    bool palm = false;
    double trunk_base = 0;       // y where the trunk meets the rim
    std::uint32_t seed = 0;
    double az = 0, el = 0, distance = 0;  // as seen from the eye
    double reach = 0;            // angular radius of its bounds
};

// A leaf of the near canopy: a hanging frond (a curved rachis with leaflets) or a
// broad heart-shaped leaf, in metres on a plane `distance` ahead.
struct NearLeaf {
    bool frond = false;
    bool slender = false;         // a narrow pointed leaf on a branch
    double next_x = 0, next_y = 0; // where the branch goes on to the next leaf
    bool ground = false;          // a plant of the forest floor, lit from above
    double x = 0, y = 0;          // its base, on the plane
    double angle = 0;             // the direction it grows, radians from straight down
    double size = 0;              // metres
    double bend = 0;
    double distance = 4;
    std::uint32_t seed = 0;
};

// A lily pad on the pool, perhaps with a flower.
struct Lily {
    double x = 0, z = 0, radius = 0, angle = 0;
    bool flower = false;
};

struct Context {
    const Look* look = nullptr;
    const Camera* camera = nullptr;
    Sky sky{SkyParameters{}};
    Vec3 sun{};
    Rgb sunlight{};
    Rgb fill_front{};   // sky light on a face turned toward the eye
    Rgb fill_up{};      // on a face turned up
    Rgb water_light{};  // the pool's light on a face turned down toward it
    double time = 0;
    std::vector<Crown> crowns;
    std::vector<Crown> jungle;   // the trees and shrubs either side of the clearing, nearest first
    std::vector<NearLeaf> leaves;
    std::vector<Lily> lilies;
    // The sky precomputed over azimuth and elevation, `sky_step` radians apart,
    // bilinear between: azimuth -100..100 degrees, elevation -10..90.
    double sky_step = .4 * pi / 180;
    int sky_columns = 0, sky_rows = 0;
    std::vector<Rgb> sky_grid;
    int width = 0, height = 0;
};

// Henyey and Greenstein: how much of light is thrown `mu` (cosine) off its path.
double phase_hg(double mu, double g) {
    const double g2 = g * g;
    return (1 - g2) / (4 * pi * std::pow(1 + g2 - 2 * g * mu, 1.5));
}

struct Basis {
    Vec3 forward, right, up;
};
Basis camera_basis(const Camera& camera) {
    const double cp = std::cos(camera.pitch), sp = std::sin(camera.pitch);
    const double cy = std::cos(camera.yaw), sy = std::sin(camera.yaw);
    const Vec3 forward{sy * cp, sp, cy * cp};
    const Vec3 right{cy, 0, -sy};
    return {forward, right, cross(forward, right)};
}

// The lens's focal length in pixels for a picture `height` tall (see particles::Lens).
double focal_pixels(const Camera& camera, int height) {
    const double half = camera.fov / 2;
    const double b = camera.bend;
    return height / 2.0 * (b + std::cos(half)) / ((b + 1) * std::sin(half));
}

Vec3 direction_at(const Camera& camera, int width, int height, double px, double py) {
    const Basis basis = camera_basis(camera);
    const double f = focal_pixels(camera, height);
    const double dx = px - width / 2.0;
    const double dy = height / 2.0 - py;
    const double r = std::hypot(dx, dy);
    if (r < 1e-9)
        return basis.forward;
    // Solve r = (b + 1) f sin(a) / (b + cos(a)) for the angle a.
    const double b = camera.bend;
    const double k = r / ((b + 1) * f);
    const double angle = std::atan(k) + std::asin(std::clamp(k * b / std::sqrt(1 + k * k), -1.0, 1.0));
    const Vec3 sideways = normalized(basis.right * (dx / r) + basis.up * (dy / r));
    return normalized(basis.forward * std::cos(angle) + sideways * std::sin(angle));
}

// Where `direction` falls in the picture; false if the lens cannot see it.
bool pixel_of(const Camera& camera, int width, int height, Vec3 direction, double& px, double& py) {
    const Basis basis = camera_basis(camera);
    const double f = focal_pixels(camera, height);
    const double ahead = dot(direction, basis.forward);
    const double across = dot(direction, basis.right);
    const double upward = dot(direction, basis.up);
    const double side = std::hypot(across, upward);
    const double angle = std::atan2(side, ahead);
    const double b = camera.bend;
    if (b + std::cos(angle) <= .05)
        return false;
    const double r = (b + 1) * f * std::sin(angle) / (b + std::cos(angle));
    px = width / 2.0 + (side > 1e-9 ? r * across / side : 0);
    py = height / 2.0 - (side > 1e-9 ? r * upward / side : 0);
    return true;
}

Vec3 view_direction(const Context& c, double px, double py) {
    return direction_at(*c.camera, c.width, c.height, px, py);
}

constexpr double sky_azimuth_from = -100 * pi / 180;
constexpr double sky_elevation_from = -10 * pi / 180;

Rgb sky_at(const Context& c, Vec3 direction) {
    const double az = std::atan2(direction.x, direction.z);
    const double el = std::asin(std::clamp(direction.y, -1.0, 1.0));
    const double gx = std::clamp((az - sky_azimuth_from) / c.sky_step, 0.0, c.sky_columns - 1.001);
    const double gy = std::clamp((el - sky_elevation_from) / c.sky_step, 0.0, c.sky_rows - 1.001);
    const int ix = static_cast<int>(gx);
    const int iy = static_cast<int>(gy);
    const double fx = gx - ix;
    const double fy = gy - iy;
    const std::size_t row = static_cast<std::size_t>(iy) * static_cast<std::size_t>(c.sky_columns);
    const std::size_t ixs = static_cast<std::size_t>(ix);
    const std::size_t stride = static_cast<std::size_t>(c.sky_columns);
    const Rgb a = c.sky_grid[row + ixs];
    const Rgb b = c.sky_grid[row + ixs + 1];
    const Rgb d = c.sky_grid[row + stride + ixs];
    const Rgb e = c.sky_grid[row + stride + ixs + 1];
    return mix(mix(a, b, fx), mix(d, e, fx), fy);
}

void plant_forest(Context& c) {
    const double eye = (*c.camera).eye_height;
    std::uint32_t seed = (*c.look).seed * 7919U + 3;
    // Broadleaf crowns along both banks above the cliff, nearer ones larger; the
    // river's channel over the brink stays open to the sky.
    int tries = 0;
    for (int i = 0; i < 70 && tries < 4000; ++i, ++tries) {
        seed = hash32(seed + 1);
        Crown crown;
        crown.z = wall_z + 3 + 40 * unit(hash32(seed + 5)) * unit(hash32(seed + 6));
        crown.x = -75 + 150 * unit(seed);
        // The river is open to the sky over its channels; trees stand on the
        // outcrops between them and on the banks.
        if (brink_at(crown.x).inside > .15 || from_channel(crown.x) < 1.5) {
            --i;
            continue;
        }
        const double room = std::min(from_channel(crown.x), 8.0);
        crown.radius = std::min(2.4 + 4.5 * unit(hash32(seed + 7)), 1.2 + room);
        const double rim = rock_top(crown.x);
        const double understory = 1.2 + 5.5 * std::sqrt(std::min(room, 6.0) / 6);
        const double tall = understory + crown.radius * .45 + 12 * unit(hash32(seed + 8)) * (.2 + .8 * room / 8);
        crown.y = rim + tall;
        crown.seed = hash32(seed + 9);
        crown.lobes = 5 + static_cast<int>(hash32(seed + 10) % 3);
        for (int l = 0; l < crown.lobes; ++l) {
            const std::uint32_t h = hash3(i, l, 1, crown.seed);
            const double a = unit(h) * 2 * pi;
            const double r = .55 * unit(hash32(h));
            crown.lobe_x[l] = std::cos(a) * r;
            crown.lobe_y[l] = std::sin(a) * r * .6 + .1;
            crown.lobe_r[l] = .45 + .35 * unit(hash32(h + 1));
        }
        c.crowns.push_back(crown);
    }
    // Tall slender palms (palmito) at the channel's edges, their fronds against the sky.
    const double palm_x[4] = {-51.5, -60.0, 47.5, 57.0};
    const double palm_z[4] = {wall_z + 4, wall_z + 12, wall_z + 7, wall_z + 16};
    const double palm_h[4] = {17, 21, 15, 23};
    for (int p = 0; p < 4; ++p) {
        Crown palm;
        palm.palm = true;
        palm.x = palm_x[p];
        palm.z = palm_z[p];
        palm.trunk_base = rock_top(palm.x);
        palm.y = palm.trunk_base + palm_h[p];
        palm.radius = 3.4;
        palm.seed = hash32(seed + 100 + static_cast<std::uint32_t>(p));
        c.crowns.push_back(palm);
    }
    for (Crown& crown : c.crowns) {
        const double ground = std::hypot(crown.x, crown.z);
        crown.distance = std::hypot(ground, crown.y - eye);
        crown.az = std::atan2(crown.x, crown.z);
        crown.el = std::atan2(crown.y - eye, ground);
        const double extent = crown.palm ? crown.y - crown.trunk_base + 4 : crown.radius * 1.9;
        crown.reach = extent / crown.distance;
    }
    // Farther first is not needed: the nearest covering crown wins below.
}

// The plants of the forest floor on both banks: ferns arching from their crowns,
// clumps of broad aroid leaves, and tall strap leaves, in drifts.
void plant_ground(Context& c) {
    int planted = 0;
    for (int i = 0; i < 20000 && planted < 1500; ++i) {
        const std::uint32_t h = hash32(7000 + static_cast<std::uint32_t>(i));
        const double z = 2.8 + 70 * std::pow(unit(h), 1.8);
        const double x = -75 + 150 * unit(hash32(h + 1));
        const double base = ground_height(x, z);
        if (base < .15)
            continue;
        // Keep the walkway's deck clear.
        const Vec3 w = to_walkway({x, 0, z});
        if (std::fabs(w.z + eye_v) < walkway_half + .3)
            continue;
        ++planted;
        const int kind = static_cast<int>(hash32(h + 2) % 3);
        // Nearer the eye the plants are the big ones of the bank's edge.
        const double scale = (.6 + .6 * unit(hash32(h + 3))) * (1 + .35 * std::exp(-z / 5));
        const int count = kind == 0 ? 7 : kind == 1 ? 4 : 6;
        for (int k = 0; k < count; ++k) {
            const std::uint32_t g = hash3(i, k, 9, 31);
            NearLeaf leaf;
            leaf.ground = true;
            leaf.distance = z + .05 * k;
            leaf.x = x + (unit(g) - .5) * .15;
            leaf.y = base;
            leaf.seed = hash32(g + 1);
            const double spread = (k + .5) / count - .5;
            if (kind == 0) {
                leaf.frond = true;
                leaf.angle = pi / 2 + spread * 2.4 + .2 * (unit(hash32(g + 2)) - .5);
                leaf.size = (.6 + .6 * unit(hash32(g + 3))) * scale;
                leaf.bend = (spread < 0 ? -1.0 : 1.0) * (.5 + .4 * unit(hash32(g + 4)));
            } else if (kind == 1) {
                leaf.angle = pi + spread * 1.6;
                leaf.y = base + (.25 + .4 * unit(hash32(g + 2))) * scale;
                leaf.size = (.32 + .3 * unit(hash32(g + 3))) * scale;
            } else {
                leaf.slender = true;
                leaf.angle = pi + spread * 1.3;
                leaf.size = (.6 + .7 * unit(hash32(g + 3))) * scale;
                leaf.bend = (unit(hash32(g + 4)) - .5) * 1.2;
                leaf.next_x = leaf.x;
                leaf.next_y = leaf.y;
            }
            c.leaves.push_back(leaf);
        }
    }
}

// Nearer crowns first, so a ray can stop once it is covered.
struct NearerFirst {
    bool operator()(const Crown& a, const Crown& b) const {
        return a.distance < b.distance;
    }
};

// The jungle either side of the clearing: tall trees standing from the foreground back
// to the cliff, and the shrubs under them.
void plant_jungle(Context& c) {
    const double eye = (*c.camera).eye_height;
    int planted = 0;
    for (int i = 0; i < 6000 && planted < 260; ++i) {
        const std::uint32_t h = hash32(9100 + static_cast<std::uint32_t>(i));
        const double side = (i % 2 == 0) ? -1.0 : 1.0;
        const double x = side * (14 + 60 * unit(h));
        const double z = 3 + 80 * std::pow(unit(hash32(h + 1)), 1.1);
        if (outside_pool(x, z) < 2.5 || z > wall_z - 2)
            continue;
        // Keep the middle of the view open to the falls and the pool.
        const double az = std::atan2(x, z);
        if (std::fabs(az) < .36)
            continue;
        const double base = ground_height(x, z);
        const bool tree = unit(hash32(h + 2)) < .55;
        Crown crown;
        crown.x = x;
        crown.z = z;
        crown.radius = tree ? 3.5 + 5 * unit(hash32(h + 3)) : 1.4 + 2.2 * unit(hash32(h + 3));
        crown.y = base + (tree ? 9 + 20 * unit(hash32(h + 4)) : crown.radius * .8);
        crown.trunk_base = base;
        crown.palm = false;
        crown.seed = hash32(h + 5);
        crown.lobes = 5 + static_cast<int>(hash32(h + 6) % 3);
        for (int l = 0; l < crown.lobes; ++l) {
            const std::uint32_t g = hash3(i, l, 3, crown.seed);
            const double a = unit(g) * 2 * pi;
            const double r = .6 * unit(hash32(g));
            crown.lobe_x[l] = std::cos(a) * r;
            crown.lobe_y[l] = std::sin(a) * r * .7;
            crown.lobe_r[l] = .45 + .35 * unit(hash32(g + 1));
        }
        const double ground = std::hypot(crown.x, crown.z);
        crown.distance = std::hypot(ground, crown.y - eye);
        crown.az = std::atan2(crown.x, crown.z);
        crown.el = std::atan2(crown.y - eye, ground);
        crown.reach = (crown.radius * 1.9 + (crown.y - base)) / crown.distance;
        c.jungle.push_back(crown);
        ++planted;
    }
    std::sort(c.jungle.begin(), c.jungle.end(), NearerFirst{});
}

// Farther leaves first, so nearer ones are laid over them.
struct FartherFirst {
    bool operator()(const NearLeaf& a, const NearLeaf& b) const {
        return a.distance > b.distance;
    }
};

// The point on the plane `distance` ahead that the scene's 1100 x 760 picture shows at
// pixel (px, py), which may lie just outside it.
void anchor_outside(const Camera& camera, double px, double py, double distance, double& x, double& y) {
    const Vec3 d = direction_at(camera, 1100, 760, px, py);
    const double t = distance / std::max(.05, d.z);
    x = d.x * t;
    y = camera.eye_height + d.y * t;
}

void plant_near_canopy(Context& c) {
    // Placed for the scene's own view, whatever view draws them.
    const Camera scene_camera{};
    plant_ground(c);
    // Fronds hanging into the view from the upper left, a leafy branch from the upper
    // right, on planes a few metres ahead (metres measured on the plane), each rooted
    // just outside the picture's corner as the scene's lens sees it.
    for (int i = 0; i < 5; ++i) {
        NearLeaf leaf;
        leaf.frond = true;
        leaf.distance = 3.2 + .6 * i;
        anchor_outside(scene_camera, 70.0 - 12 * i, -10.0 + 40 * i, leaf.distance, leaf.x, leaf.y);
        leaf.angle = -1.05 + .17 * i;
        leaf.size = 3.4 + .4 * i;
        leaf.bend = .55 + .1 * i;
        leaf.seed = hash32(51 + static_cast<std::uint32_t>(i));
        c.leaves.push_back(leaf);
    }
    // Three branches reaching in from the upper right, each hung with narrow pointed
    // leaves on alternate sides, drooping.
    for (int branch = 0; branch < 3; ++branch) {
        const double distance = 2.6 + .9 * branch;
        double x = 0, y = 0;
        anchor_outside(scene_camera, 1050.0 + 10 * branch, -5.0 + 50 * branch, distance, x, y);
        double heading = pi + .25 + .18 * branch;  // leftward, a little down
        const int count = 18 + 4 * branch;
        for (int k = 0; k < count; ++k) {
            const std::uint32_t h = hash3(branch, k, 2, 77);
            NearLeaf leaf;
            leaf.slender = true;
            leaf.distance = distance;
            leaf.x = x;
            leaf.y = y;
            const double side = (k % 2 == 0) ? 1.0 : -1.0;
            // Leaves hang from the branch: mostly downward, swung to their side.
            leaf.angle = side * (.35 + .5 * unit(h)) + .15 * (unit(hash32(h)) - .5);
            leaf.size = (.22 + .16 * unit(hash32(h + 1))) * (1 - .35 * k / count);
            leaf.bend = .5 * (unit(hash32(h + 2)) - .5);
            leaf.seed = hash32(h + 3);
            c.leaves.push_back(leaf);
            const double step = .075 + .02 * unit(hash32(h + 4));
            heading += .05 * (unit(hash32(h + 5)) - .45);
            x += std::cos(heading) * step;
            y += std::sin(heading) * step - .012 * k * step;
            c.leaves.back().next_x = x;
            c.leaves.back().next_y = y;
        }
    }
}

void prepare(Context& c, const Look& look, const Camera& camera, int width, int height, double time) {
    c.look = &look;
    c.camera = &camera;
    c.sky = Sky(look.sky);
    c.sun = c.sky.sun_direction();
    c.sunlight = c.sky.sunlight();
    c.time = time;
    c.width = width;
    c.height = height;
    // The sky's light on a face toward the eye (the sky behind the viewer and above)
    // and on one turned up.
    const Vec3 front_dirs[5] = {{0, .5, -.86}, {.6, .4, -.7}, {-.6, .4, -.7}, {0, .9, -.43}, {0, .25, -.97}};
    const Vec3 up_dirs[5] = {{0, 1, 0}, {.5, .8, .3}, {-.5, .8, .3}, {0, .7, .7}, {0, .7, -.7}};
    Rgb front{}, up{};
    for (int i = 0; i < 5; ++i) {
        front = front + c.sky.radiance(normalized(front_dirs[i]));
        up = up + c.sky.radiance(normalized(up_dirs[i]));
    }
    // Irradiance over the radiance of the visible half: pi, and half the half for a
    // vertical face.
    // Light cloud behind the viewer, lit by the high sun, sends a soft white light onto
    // everything facing it; a little of it reaches what faces up.
    const double cloud = look.cloud;
    c.fill_front = front * (pi / 5 * .5) + c.sunlight * (.055 * cloud);
    c.fill_up = up * (pi / 5) + c.sunlight * (.02 * cloud);
    // The pool's own brightness, thrown back up onto what faces it.
    c.water_light = (c.fill_up * .08 + c.sunlight * .05) * Rgb{.75, .95, .9};
    c.sky_columns = static_cast<int>(200 * pi / 180 / c.sky_step) + 2;
    c.sky_rows = static_cast<int>(100 * pi / 180 / c.sky_step) + 2;
    c.sky_grid.assign(static_cast<std::size_t>(c.sky_columns) * static_cast<std::size_t>(c.sky_rows), Rgb{});
    for (int gy = 0; gy < c.sky_rows; ++gy)
        for (int gx = 0; gx < c.sky_columns; ++gx) {
            const double az = sky_azimuth_from + gx * c.sky_step;
            const double el = std::max(sky_elevation_from + gy * c.sky_step, .002);
            const Vec3 d{std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az)};
            c.sky_grid[static_cast<std::size_t>(gy) * static_cast<std::size_t>(c.sky_columns) + static_cast<std::size_t>(gx)] =
                c.sky.radiance(d);
        }
    plant_forest(c);
    plant_jungle(c);
    plant_near_canopy(c);
    std::stable_sort(c.leaves.begin(), c.leaves.end(), FartherFirst{});
    // Lily pads in the calm water beside the walkway, in drifts.
    for (int i = 0; i < 46; ++i) {
        const std::uint32_t h = hash32(4000 + static_cast<std::uint32_t>(i));
        const int drift = i % 4;
        const double drift_x[4] = {-14, 9, -3, 22};
        const double drift_z[4] = {31, 30, 37, 36};
        Lily lily;
        lily.x = drift_x[drift] + 6 * (unit(h) - .5) * 2;
        lily.z = drift_z[drift] + 4 * (unit(hash32(h + 1)) - .5) * 2;
        const Vec3 on_walkway = to_walkway({lily.x, 0, lily.z});
        if (std::fabs(on_walkway.z + eye_v) < walkway_half + .6 || ground_height(lily.x, lily.z) > -.3)
            continue;
        lily.radius = .3 + .4 * unit(hash32(h + 2));
        lily.angle = unit(hash32(h + 3)) * 2 * pi;
        lily.flower = unit(hash32(h + 4)) < .12;
        c.lilies.push_back(lily);
    }
}

// ---- Layers --------------------------------------------------------------------------

// A covering layer at a sample: its colour and how much it covers.
struct Cover {
    Rgb color{};
    double alpha = 0;
};

// Leaves seen against the light: their own dim face, and the sky's light coming
// through them where they are thin, glowing when the sun is just behind.
Rgb backlit_leaf(const Context& c, Vec3 direction, Rgb behind, double thin, double shade, Rgb tint) {
    const double mu = dot(direction, c.sun);
    const double forward = phase_hg(mu, .82) * 4 * pi;
    const Rgb face = Rgb{.022, .034, .016} * (luminance(c.fill_front) * shade);
    const Rgb through = (behind * .3 + c.sunlight * (.003 * forward)) * tint * thin;
    return face + through;
}

double crown_coverage(const Context& c, const Crown& crown, double az, double el, double& depth) {
    static_cast<void>(c);
    depth = 0;
    const double dx = (az - crown.az) * crown.distance;
    const double dy = (el - crown.el) * crown.distance;
    const double r = crown.radius;
    if (std::fabs(dx) > crown.reach * crown.distance || dy > r * 1.6 || dy < -(crown.reach * crown.distance))
        return 0;
    double inside = -1e9;  // positive inside, metres
    for (int l = 0; l < crown.lobes; ++l) {
        const double lx = dx - crown.lobe_x[l] * r;
        const double ly = (dy - crown.lobe_y[l] * r) * 1.25;
        const double d = crown.lobe_r[l] * r - std::hypot(lx, ly);
        inside = std::max(inside, d);
    }
    // A leafy edge: clusters of leaves and the sky between them.
    const double leafy = .5 * fbm(dx / 1.4, dy / 1.4, crown.seed, 3) + .35 * fbm(dx / .45, dy / .45, crown.seed + 3, 3) +
                         .2 * noise2(dx / .15, dy / .15, crown.seed + 4);
    const double edge = inside + leafy * std::min(r * .55, 2.2);
    depth = std::max(0.0, edge);
    const double pixel = crown.distance * .0008;  // a sample's footprint, metres
    return std::clamp(edge / pixel + .5, 0.0, 1.0);
}

// Distance from (px, py) to the segment from (ax, ay) to (bx, by).
double segment_distance(double px, double py, double ax, double ay, double bx, double by) {
    const double vx = bx - ax, vy = by - ay;
    const double wx = px - ax, wy = py - ay;
    const double length2 = vx * vx + vy * vy;
    const double t = length2 > 0 ? std::clamp((wx * vx + wy * vy) / length2, 0.0, 1.0) : 0.0;
    return std::hypot(wx - vx * t, wy - vy * t);
}

double palm_coverage(const Context& c, const Crown& palm, double az, double el, double& depth) {
    depth = 0;
    const double dx = (az - palm.az) * palm.distance;
    const double dy = (el - palm.el) * palm.distance;  // 0 at the crown
    const double reach = palm.reach * palm.distance;
    if (std::fabs(dx) > 5 || dy > 2.5 || dy < -reach)
        return 0;
    const double pixel = palm.distance * .0008;
    static_cast<void>(c);
    double cover = 0;
    // The trunk: slender, a gentle lean.
    const double height = palm.y - palm.trunk_base;
    if (dy < 0 && dy > -height) {
        const double along = -dy / height;
        const double centre = .9 * along * along * (unit(palm.seed) - .5);
        const double half = .13 + .05 * along;
        cover = std::max(cover, std::clamp((half - std::fabs(dx - centre)) / pixel + .5, 0.0, 1.0));
    }
    // Fronds: arching out and down from the crown, their leaflets hanging from them in
    // long strands (the palmito's drooping crown).
    for (int f = 0; f < 15; ++f) {
        const std::uint32_t h = hash3(f, 0, 0, palm.seed);
        const double around = (f / 15.0) * 2 * pi + .35 * unit(h);
        const double side = std::cos(around);
        const double length_f = 3.0 + 1.0 * unit(hash32(h));
        const double rise = .45 + .35 * unit(hash32(h + 1));
        double previous_x = 0, previous_y = 0;
        for (int k = 1; k <= 30; ++k) {
            const double s = k / 30.0;
            const double fx = side * length_f * s;
            const double fy = length_f * (rise * s - (rise + .75) * s * s);
            // The rachis, segment by segment.
            const double rachis = .04 * (1 - .5 * s) - segment_distance(dx, dy, previous_x, previous_y, fx, fy);
            cover = std::max(cover, std::clamp(rachis / pixel + .5, 0.0, 1.0));
            previous_x = fx;
            previous_y = fy;
            // A leaflet from each side, hanging and swinging out a little.
            const double hang = (.25 + .75 * std::sin(std::min(1.0, s * 1.1) * pi)) *
                                (.8 + .4 * unit(hash3(f, k, 1, palm.seed)));
            for (int side_k = 0; side_k < 2; ++side_k) {
                const double out = (side_k == 0 ? -.18 : .18) + side * .2;
                const double tip_x = fx + out * hang;
                const double tip_y = fy - hang;
                const double leaflet = .032 - segment_distance(dx, dy, fx, fy, tip_x, tip_y);
                cover = std::max(cover, std::clamp(leaflet / pixel + .5, 0.0, 1.0) * .9);
            }
        }
    }
    depth = cover * .2;
    return cover;
}

// The jungle either side, seen along `direction` short of `limit` metres: its trees
// lit from behind and above, deep green in their own shade, their edges glowing
// where the sky and sun come through, their trunks dark. `nearest` is the distance
// of the first crown that covers.
Cover jungle_layer(const Context& c, Vec3 direction, Rgb behind, double limit, double& nearest) {
    nearest = 1e9;
    Cover out;
    if (direction.z <= 0)
        return out;
    const double az = std::atan2(direction.x, direction.z);
    const double el = std::asin(std::clamp(direction.y, -1.0, 1.0));
    double passing = 1;
    Rgb gathered{};
    const double mu = dot(direction, c.sun);
    const double forward = phase_hg(mu, .55) * 4 * pi;
    for (const Crown& crown : c.jungle) {
        if (crown.distance > limit + crown.radius || passing < .02)
            break;
        if (std::fabs(az - crown.az) > crown.reach + .01)
            continue;
        double depth = 0;
        double a = crown_coverage(c, crown, az, el, depth);
        // The trunk below a tree's crown.
        const double dx = (az - crown.az) * crown.distance;
        const double dy = (el - crown.el) * crown.distance;
        const double height = crown.y - crown.trunk_base;
        double trunk = 0;
        if (height > 6 && dy < 0 && dy > -height) {
            const double lean = .6 * (unit(crown.seed) - .5) * (-dy / height);
            const double half = .25 + .2 * (-dy / height);
            trunk = std::clamp((half - std::fabs(dx - lean)) / (crown.distance * .0009) + .5, 0.0, 1.0);
        }
        if (a <= 0 && trunk <= 0)
            continue;
        if (nearest > 1e8)
            nearest = crown.distance;
        const double tone = unit(crown.seed);
        const Rgb albedo = mix(Rgb{.035, .07, .02}, Rgb{.075, .11, .03}, tone);
        const double clusters = fbm(dx / .8, dy / .8, crown.seed + 9, 3);
        const double hollow = std::clamp(.55 + .6 * clusters, .12, 1.2);
        const double thin = std::exp(-depth / .35);
        const double top = smoothstep(-crown.radius * .2, crown.radius * .9, dy);
        const Rgb leaf = albedo * ((c.fill_front * .9 + c.fill_up * (.5 * top)) * hollow) +
                         Rgb{.25, .42, .08} * (luminance(behind) * .25 * thin) +
                         c.sunlight * Rgb{.5, .75, .2} * (.006 * forward * thin + .04 * top * top * smoothstep(.2, .8, clusters));
        const double moss = smoothstep(-.2, .5, fbm(dx / .3, dy / 1.5, crown.seed + 4, 3));
        const Rgb bark = mix(Rgb{.035, .028, .022}, Rgb{.03, .05, .018}, moss) * (luminance(c.fill_front) * 1.2);
        const double cover = std::max(a, trunk);
        const Rgb colour = a >= trunk ? leaf : bark;
        gathered = gathered + colour * (cover * passing);
        passing *= 1 - cover;
    }
    out.color = passing < 1 ? gathered * (1 / (1 - passing)) : Rgb{};
    out.alpha = 1 - passing;
    return out;
}

Cover forest_layer(const Context& c, Vec3 direction, Rgb behind) {
    const double az = std::atan2(direction.x, direction.z);
    const double el = std::asin(std::clamp(direction.y, -1.0, 1.0));
    Cover out;
    double best = 1e9;
    for (const Crown& crown : c.crowns) {
        if (crown.distance > best)
            continue;
        if (std::fabs(az - crown.az) > crown.reach + .01)
            continue;
        double depth = 0;
        const double a = crown.palm ? palm_coverage(c, crown, az, el, depth) : crown_coverage(c, crown, az, el, depth);
        if (a <= 0)
            continue;
        const double thin = std::exp(-depth / .2) * .55 + .03;
        const double shade = .7 + .6 * noise2(az * 900, el * 900, crown.seed);
        const Rgb tint{.42, .62, .16};
        const Rgb color = backlit_leaf(c, direction, behind, thin, shade, tint);
        // Nearer crowns hide farther ones: blend this one over what was found.
        if (crown.distance < best) {
            out.color = mix(out.color, color, a);
            out.alpha = std::max(out.alpha, a);
            if (a > .99)
                best = crown.distance;
        }
    }
    return out;
}

// The vegetation along the cliff's edge: ferns and shrubs overhanging the rim.
double rim_fringe(double x, double y, double rim, double& depth) {
    depth = 0;
    // A dense understory on the outcrops, lower toward the channels' edges.
    const double room = std::min(from_channel(x), 6.0) / 6;
    const double height = room <= 0 ? 0 : (2.6 + 5 * std::sqrt(room)) * (.7 + .45 * fbm(x / 2.2, 5.5, 61, 3)) * smoothstep(0, .12, room);
    const double above = y - rim;
    if (above < 0 || height <= 0)
        return 0;
    const double tuft = height * (.6 + .3 * fbm(x / 1.1, above / 1.1, 63, 3) + .25 * fbm(x / .3, above / .3, 62, 3)) - above;
    depth = std::max(0.0, tuft);
    return std::clamp(tuft / .06 + .5, 0.0, 1.0);
}

Rgb rock_color(const Context& c, double x, double y, double rim, double falls_glow) {
    const double e = .06;
    const double h = relief(x, y);
    const double hx = (relief(x + e, y) - h) / e;
    const double hy = (relief(x, y + e) - h) / e;
    const Vec3 n = normalized({-hx, -hy, -1});
    // Basalt mottled at every scale: its own colour wandering from dusky brown to
    // blue-grey, iron weeping rust down it from the joints, pale crusts of lichen with
    // speckled edges and a few orange spots, black seep streaks and white mineral runs
    // from the ledges, a grain of crystals, and a green film where the spray keeps it wet.
    const double wx = x + 2.2 * fbm(x / 9, y / 9, 101, 3);
    const double wy = y + 2.2 * fbm(x / 9 + 5.2, y / 9, 102, 3);
    const double hue = fbm(wx / 14, wy / 10, 103, 4);
    Rgb albedo = mix(Rgb{.050, .040, .032}, Rgb{.040, .045, .052}, smoothstep(-.4, .4, hue));
    albedo = albedo * (.75 + .5 * (.5 + .5 * fbm(wx / 3.5, wy / 3.5, 104, 4)));
    const double rust = smoothstep(.15, .65, fbm(x / 2.2, y / 11 + .3 * fbm(x, y / 5, 105, 2), 106, 4)) *
                        smoothstep(-.2, .5, fbm(x / 7, y / 7, 107, 2));
    albedo = mix(albedo, Rgb{.12, .060, .028}, rust * .7);
    const Cells crust = cells(wx / 1.3, wy / 1.1, 108);
    const double lichen_field = fbm(wx / 4.5, wy / 3.5, 109, 4) + .25 * (unit(crust.id) - .5);
    const double lichen = smoothstep(.22, .42, lichen_field) * (.65 + .35 * noise2(x / .07, y / .07, 110));
    albedo = mix(albedo, Rgb{.20, .21, .18}, lichen * .75);
    const double orange = smoothstep(.55, .7, fbm(x / .9, y / .9, 111, 3)) * smoothstep(.2, .5, lichen_field + .3);
    albedo = mix(albedo, Rgb{.30, .13, .03}, orange * .6);
    const double seep = smoothstep(.25, .7, fbm(x / .45, y / 9, 112, 3)) * smoothstep(-.1, .4, fbm(x / 6, 2.7, 113, 2));
    albedo = albedo * (1 - .6 * seep);
    const double calcite = smoothstep(.62, .78, fbm(x / .35, y / 7, 114, 3)) * (1 - seep);
    albedo = mix(albedo, Rgb{.16, .16, .15}, calcite * .5);
    albedo = albedo * ((.8 + .4 * (.5 + .5 * noise2(x / .04, y / .04, 115))) * 1.9);
    const double moss_bed = smoothstep(.15, .5, n.y) * smoothstep(.1, .55, fbm(x / 2.2, y / 2.2, 73, 4)) *
                            smoothstep(-.3, .2, fbm(x / 11, y / 9, 74, 2));
    albedo = mix(albedo, Rgb{.045, .085, .018}, moss_bed);
    // Ferns, vines and shrubs draped from the rim and over the outcrops between the
    // curtains, thinning down the wall; leafy in texture, not flat.
    const double between = 1 - std::exp(-from_channel(x) / 1.2);
    const double drape = std::exp(-std::max(0.0, rim - y) / (5 + 6 * between)) * (.5 + .5 * between);
    const double clumps = fbm(x / 1.4, y / 3.2, 76, 4) + .35 * fbm(x / .35, y / .5, 77, 3);
    const double greenery = std::clamp(smoothstep(.55 - drape - .25 * moss_bed, .8 - drape, clumps + .3), 0.0, 1.0);
    const double near_falls = std::exp(-from_channel(x) / 3);
    const double wet = std::clamp(near_falls + .5 * smoothstep(.3, .8, fbm(x / .7, y / 9, 75, 3)) + .6 * seep, 0.0, 1.0);
    albedo = albedo * (1 - .45 * wet);
    const double film = near_falls * smoothstep(-.2, .4, fbm(x / 2, y / 2, 116, 3)) * smoothstep(30, 5, y);
    albedo = mix(albedo, Rgb{.018, .045, .02}, film * .7);
    // Cavities and the undersides of the flows are darker.
    const double cavity = std::clamp(.65 + 1.1 * (h - .35), .25, 1.0);
    // Sky light by facing, the sun on what faces it (ledges and the rim's top), the
    // falls' white light thrown sideways onto the rock beside them.
    const double sky_up = std::clamp(n.y, 0.0, 1.0);
    const Rgb sky_light = mix(c.fill_front, c.fill_up, sky_up);
    const double sun_dot = std::max(0.0, dot(n, c.sun));
    const double lit_ledge = smoothstep(.25, .6, n.y);
    const Rgb sun_light = c.sunlight * (sun_dot * lit_ledge);
    const Rgb glow = Rgb{.9, .88, .82} * falls_glow;
    Rgb color = albedo * (sky_light * cavity + sun_light + glow * cavity);
    // A wet face mirrors a little of the bright sky above it.
    const double sheen = wet * .03 * std::pow(std::max(0.0, -n.z * .5 + n.y * .5 + .3), 3);
    color = color + c.fill_up * sheen;
    if (greenery > 0) {
        const double clusters = fbm(x / .7, y / .7, 79, 3);
        const double leaves = std::max(.08, .45 + .45 * noise2(x / .12, y / .12, 78) + .7 * clusters);
        const Rgb leaf_albedo = mix(Rgb{.03, .065, .016}, Rgb{.075, .11, .028}, smoothstep(-.3, .5, fbm(x / 4, y / 4, 80, 2)));
        const double sun_on = std::max(0.0, c.sun.y) * smoothstep(-1.5, 0, y - rim + 1.5) * .45 * smoothstep(-.1, .4, clusters);
        const Rgb lit = c.fill_front * .8 + c.fill_up * .3 + glow + c.sunlight * sun_on;
        color = mix(color, leaf_albedo * lit * std::max(.2, leaves), greenery);
    }
    return color;
}

Rgb water_tint(const Context& c, double path) {
    const double amber = (*c.look).water_amber;
    return {std::exp(-1.1 * amber * path), std::exp(-2.0 * amber * path), std::exp(-4.2 * amber * path)};
}

// The falling water at (x, y) on the wall's plane.
Cover falls_layer(const Context& c, Vec3 direction, double x, double y, Rgb rock_behind) {
    Cover out;
    // A curtain spreads a little as it falls: trace the water back to where it left.
    const Brink here = brink_at(x);
    const double approx_drop = std::max(0.0, here.level - y);
    double centre = x;
    for (int i = 0; i < channel_count; ++i)
        if (x > channels[i].left - 3 && x < channels[i].right + 3)
            centre = (channels[i].left + channels[i].right) / 2;
    const double x0 = centre + (x - centre) / (1 + .009 * approx_drop);
    const Brink brink = brink_at(x0);
    const double depth = std::max(0.0, brink.level - brink.rock);
    const double drop = brink.level - y;
    // Seen from below, the river shows nothing above its brink but the brink itself:
    // a thin rim where the water turning over is thinnest and the sun comes straight
    // through it, sparkling where ripples arriving at the edge catch it.
    const double rim_width = .07;
    if (depth <= 0 || drop < -rim_width)
        return out;
    const double mu = dot(direction, c.sun);
    const double t = c.time;
    if (drop < 0) {
        const double glow = phase_hg(mu, .9) * 4 * pi;
        const double ripple = noise2(x0 * 5.5, t * 1.7, 87) + .5 * noise2(x0 * 17, t * 3.1, 88);
        const double sparkle = std::pow(std::clamp(.5 + .6 * ripple, 0.0, 1.0), 6);
        out.color = c.sunlight * water_tint(c, .15) * (glow * (.06 + .5 * sparkle));
        out.alpha = smoothstep(-rim_width, -rim_width * .3, drop) * smoothstep(0, .08, depth);
        return out;
    }
    // A curtain that lands on a shelf pours on from it as a broad white cascade.
    const bool has_shelf = brink.channel >= 0 && channels[brink.channel].ledge > 0;
    const Channel& shelf = channels[std::max(0, brink.channel)];
    const bool below_shelf = has_shelf && y < shelf.ledge;
    double fallen = drop;     // metres since the water last left rock
    double offset = 0;        // its flight time before that
    double xs = x0;
    if (below_shelf) {
        fallen = shelf.ledge - y;
        offset = flight_time(shelf.level - shelf.ledge);
        xs = centre + (x - centre) / (1 + .02 * fallen);
    }
    const double tau = offset + flight_time(fallen);
    const double s = tau - t;  // the water's own time: it moves with the water
    const double speed = fall_speed(fallen);
    // Coherent near the brink, aerated and breaking into streaks and clumps below;
    // thin water breaks up sooner.
    const double breakup = 4 + 22 * depth;
    const double coherent = below_shelf ? 0 : std::exp(-drop / breakup);
    const double aerated = below_shelf ? 1 : 1 - std::exp(-std::max(0.0, drop - .5) / (2.2 + 8 * depth));
    const double streak = fbm(xs * 1.7, s * 2.6, 81, 4);
    const double fine = noise2(xs * 7.5, s * 9, 82);
    const double clump = fbm(xs * 1.1, s * 2.2, 83, 3);
    // Waves of water arriving in pulses, carried down and stretched as they fall.
    const double pulse = fbm(xs * .09, s * .9, 84, 3);
    const double white = std::clamp(aerated * (.45 + .7 * streak + .25 * fine + .7 * pulse), 0.0, 1.0);
    // Where the sheet has torn, the rock behind shows between the falling ropes.
    const double gaps = below_shelf ? .8 : smoothstep(.1, .9, drop / breakup);
    const double hold = .35 + .5 * coherent + .45 * clump + .2 * streak + .5 * pulse;
    double alpha = std::clamp(lerp(1.0, smoothstep(.05, .45, hold), gaps), 0.0, 1.0);
    // The sheet's own edges are ragged where the rock rises out of the river, and
    // fray as it falls.
    alpha *= smoothstep(0, .08, depth + .04 * fine);
    // Its ends fray outward as it falls, torn into ropes and spray.
    const Channel& own = channels[std::max(0, brink.channel)];
    const double from_end = std::min(x - own.left, own.right - x) + .035 * drop;
    alpha *= smoothstep(-.5, 2.5, from_end + 1.6 * noise2(s * 1.6, xs * .7, 95) + .8 * streak);
    if (below_shelf) {
        const double half = (shelf.right - shelf.left) / 2 * (1 + .02 * fallen) + .8 * fbm(s * 2, y / 3, 85, 2);
        alpha *= smoothstep(0, 1.2, half - std::fabs(x - centre));
    }
    // Near the pool the curtain is lost in its own spray.
    alpha *= smoothstep(-1, 5, y);
    // Glassy water: the rock behind, darkened and tinted, and the sun's light coming
    // straight through, gathered into bright ropes where the sheet's surface rolls;
    // gold where the river is thin, brightest just under the brink.
    const double thickness = depth * (1 + .3 * fine);
    const double glass_glow = phase_hg(mu, .9) * 4 * pi;
    const double brink_line = std::exp(-drop / .25);
    const double ridge_a = 1 - std::fabs(noise2(xs * 3.2, s * .9, 89));
    const double ridge_b = 1 - std::fabs(noise2(xs * 9, s * 1.6, 90));
    const double swell = .5 + .5 * noise2(xs * .8, s * .6, 94);
    const double rope = std::pow(ridge_a, 12) * (.4 + swell) + .3 * std::pow(ridge_b, 16);
    const Rgb glass = rock_behind * water_tint(c, thickness * .5) * .3 +
                      c.sunlight * water_tint(c, thickness * (.8 - .5 * brink_line)) *
                          (glass_glow * (.0025 + .004 * swell + .05 * rope + .045 * brink_line)) +
                      c.fill_up * .008;
    // White water: light scattered many times in a froth of bubbles and drops, so
    // nearly white; lit by the sky, and by the sun through it, strongly forward. Its
    // texture is torn and streaked, finer than the sheet's.
    const double sunlit = 1 - .55 * smoothstep(8, 26, drop + 4 * clump);
    const double froth_glow = phase_hg(mu, .6) * 4 * pi;
    const double torn = fbm(xs * 3.2, s * 5.5, 91, 5);
    const double strands = 1 - std::fabs(noise2(xs * 11, s * 3.5, 92));
    const double texture = std::clamp(.45 + .6 * torn + .35 * std::pow(strands, 4) + .2 * pulse, 0.0, 1.4);
    const Rgb froth = (c.fill_front * .75 + c.fill_up * .15) * (.45 + .55 * texture) +
                      c.sunlight * (.02 * froth_glow * sunlit * texture * texture);
    // The white begins as fingers reaching down out of the glass, then takes it over.
    const double onset = below_shelf ? 1.0 : smoothstep(.3, 3.5 + 6 * depth, drop);
    const double fingers = 1 - std::fabs(noise2(xs * 2.4, s * 1.1, 93));
    const double whiten = std::clamp(smoothstep(.85 - onset, 1.05 - onset * .8, fingers * .6 + white * .6), 0.0, 1.0);
    // Below the crest the white is the particles' work; the sheet behind them is
    // thinner and greyer, the water seen through its own spray.
    const double veil = smoothstep(3, 12, drop);
    const double white_now = std::max(whiten, white * smoothstep(.4, 1, onset)) * (1 - .25 * veil);
    alpha *= 1 - .15 * veil * (1 - clump);
    out.color = mix(glass, froth, white_now);
    out.alpha = alpha;
    // Where a curtain strikes its shelf the water bursts out white across it.
    if (has_shelf) {
        const double band = std::exp(-std::pow((y - shelf.ledge - .6) / 1.6, 2)) * (y > shelf.ledge ? 1.0 : .7);
        const double burst = band * smoothstep(-.2, .5, fbm(x * .8, (y + t * 2.5) * .9, 86, 3) + .3);
        const double inside = smoothstep(0, 1.5, (shelf.right - shelf.left) / 2 + 1.2 - std::fabs(x - centre));
        out.color = mix(out.color, froth * 1.1, burst * inside);
        out.alpha = std::max(out.alpha, burst * inside);
    }
    // The sun's glints: drops and the edges of ropes throwing its light at the eye.
    // Each lives in a cell of the water's own space, so it falls with the water. Most
    // are faint; a few are brilliant; they crowd toward the crest.
    const double density = .11 * (*c.look).glints * (.35 + .65 * white) * sunlit * (.25 + .75 * std::exp(-drop / 7));
    const double cell_x = .2;
    const double cell_s = .045;
    const int ix = static_cast<int>(std::floor(xs / cell_x));
    const int is = static_cast<int>(std::floor(s / cell_s));
    double sparkle = 0;
    for (int ds = -1; ds <= 1; ++ds)
        for (int dx = -1; dx <= 1; ++dx) {
            const std::uint32_t h = hash3(ix + dx, is + ds, 5, (*c.look).seed);
            if (unit(h) > density)
                continue;
            const double gx = (ix + dx + unit(hash32(h))) * cell_x;
            const double gs = (is + ds + unit(hash32(h + 1))) * cell_s;
            const double ex = (xs - gx) / .022;
            const double ey = (s - gs) * speed / (.022 + speed * .003);
            const double r2 = ex * ex + ey * ey;
            if (r2 > 1)
                continue;
            const double strength = std::pow(unit(hash32(h + 2)), 6);
            sparkle += (1 - r2) * strength;
        }
    if (sparkle > 0) {
        const double glint_phase = phase_hg(mu, .75) * 4 * pi;
        out.color = out.color + c.sunlight * (sparkle * 22 * glint_phase / std::max(out.alpha, .2));
    }
    return out;
}

// The sunlit spray hanging round the brink: added light, no cover.
Rgb crest_haze(const Context& c, Vec3 direction, double x, double y) {
    const Brink brink = brink_at(x);
    const double inside = std::exp(-from_channel(x) / 2.5) * (.4 + .6 * std::min(1.0, (brink.level - brink.rock) / .3));
    if (inside <= .01)
        return {};
    const double above = y - brink.level;
    const double density = std::exp(-std::fabs(above + .8) / 1.6) * (.6 + .4 * fbm(x / 2.2, (y - c.time * .6) / 1.5, 91, 3));
    const double mu = dot(direction, c.sun);
    const double glow = phase_hg(mu, .76) * 4 * pi;
    return c.sunlight * (.0065 * (*c.look).crest_haze * density * inside * glow);
}

// The spray boiling up where each curtain meets the pool: lit through by the sun
// above it, whiter where denser. Added light, as the crest's haze.
Rgb plunge_mist(const Context& c, Vec3 direction, double x, double y) {
    const double near = std::exp(-from_channel(x) / 3.5);
    if (near < .01 || y > 16)
        return {};
    const double billow = .55 + .45 * fbm(x / 3.2, (y - c.time * 1.1) / 2.6, 95, 4);
    const double density = near * std::exp(-std::max(0.0, y) / 4.5) * billow;
    const double mu = dot(direction, c.sun);
    const Rgb light = c.fill_front * 1.2 + c.sunlight * (phase_hg(mu, .7) * 4 * pi * .05);
    return light * (.3 * density);
}

// Mist in the air between the eye and the wall, thicker low down.
// How much of what lies `distance` away passes the air, and the light the air adds.
void air_parts(const Context& c, Vec3 direction, double distance, double y, double& through, Rgb& added) {
    const double density = .0006 + .0035 * std::exp(-std::max(0.0, y) / 4);
    through = std::exp(-density * distance);
    const double mu = dot(direction, c.sun);
    const Rgb light = c.fill_front * .6 + c.sunlight * (phase_hg(mu, .7) * .08);
    added = light * (1 - through);
}

Rgb air_between(const Context& c, Vec3 direction, Rgb seen, double distance, double y) {
    double through = 1;
    Rgb added{};
    air_parts(c, direction, distance, y, through, added);
    return seen * through + added;
}

Cover near_canopy(const Context& c, Vec3 direction, Rgb behind, double limit) {
    Cover out;
    const double eye = (*c.camera).eye_height;
    for (const NearLeaf& leaf : c.leaves) {
        if (direction.z <= 0)
            continue;
        const double t = leaf.distance / direction.z;
        if (t > limit)
            continue;
        const double px = direction.x * t;
        const double py = eye + direction.y * t;
        const double pixel = leaf.distance * .0009;
        double a = 0;
        double vein = 0;
        if (leaf.frond) {
            // A rachis curving from its base, leaflets hanging from both sides.
            const double ca = std::cos(leaf.angle);
            const double sa = std::sin(leaf.angle);
            const double lx = (px - leaf.x) * ca + (py - leaf.y) * sa;
            const double ly = -(px - leaf.x) * sa + (py - leaf.y) * ca;
            // lx runs along the frond (+ away from the base); ly across it.
            if (lx < -.2 || lx > leaf.size + .3)
                continue;
            const double s = std::clamp(lx / leaf.size, 0.0, 1.0);
            const double centre = -leaf.bend * s * s * leaf.size * .5;
            const double across = ly - centre;
            const double rachis = .018 * (1 - .6 * s) - std::fabs(across);
            a = std::max(a, std::clamp(rachis / pixel + .5, 0.0, 1.0));
            // Leaflets: narrow, drooping back along the frond on both sides.
            const double reach = .42 * std::sin(std::min(1.0, s * 1.15) * pi) + .05;
            const double side = std::fabs(across);
            if (side < reach && s > .02) {
                const double slant = lx + side * 1.1;
                const double spacing = .055;
                const double phase = slant / spacing - std::floor(slant / spacing);
                const double strip = .5 - std::fabs(phase - .5);
                const double width = .17 * (1 - side / reach) + .06;
                const double leaflet = (width - (.5 - strip)) * spacing;
                const double edge = std::clamp(leaflet / pixel + .5, 0.0, 1.0);
                a = std::max(a, edge * (1 - smoothstep(.85, 1, side / reach)));
                vein = .3;
            }
        } else if (leaf.slender) {
            // A narrow pointed leaf hanging from its stalk, its midrib curved.
            const double ca = std::cos(leaf.angle);
            const double sa = std::sin(leaf.angle);
            const double lx = (px - leaf.x) * ca + (py - leaf.y) * sa;
            const double ly = -(px - leaf.x) * sa + (py - leaf.y) * ca;
            const double u = -ly / leaf.size;  // 0 at the stalk, 1 at the tip
            // The branch from this leaf to the next, thinning toward its tip.
            const double twig = .007 - segment_distance(px, py, leaf.x, leaf.y, leaf.next_x, leaf.next_y);
            a = std::clamp(twig / pixel + .5, 0.0, 1.0);
            if (u < -.05 || u > 1.05) {
                if (a <= 0)
                    continue;
                vein = 1;
            }
            const double curve = leaf.bend * u * u * leaf.size * .3;
            const double v = (lx - curve) / leaf.size;
            const double half = .17 * std::pow(std::max(0.0, std::sin(std::clamp(u, 0.0, 1.0) * pi)), .75) *
                                (1 - .3 * u);
            if (std::fabs(v) > .25 && a <= 0)
                continue;
            const double inside = (half - std::fabs(v)) * leaf.size;
            if (u >= -.05 && u <= 1.05)
                a = std::max(a, std::clamp(inside / pixel + .5, 0.0, 1.0));
            // The stalk joining it to the branch.
            if (u < .08)
                a = std::max(a, std::clamp((.004 - std::fabs(lx)) / pixel + .5, 0.0, 1.0));
            const double midrib = std::exp(-std::pow(v * leaf.size / .004, 2));
            vein = midrib * .8 + .15 * std::pow(std::fabs(std::cos((u * 14 + std::fabs(v) * 10) * pi)), 4);
        } else {
            // A broad heart-shaped leaf hanging from its stalk.
            const double ca = std::cos(leaf.angle);
            const double sa = std::sin(leaf.angle);
            const double lx = (px - leaf.x) * ca + (py - leaf.y) * sa;
            const double ly = -(px - leaf.x) * sa + (py - leaf.y) * ca;
            const double u = -ly / leaf.size;  // 0 at the stalk, 1 at the tip
            const double v = lx / leaf.size;
            if (u < -.15 || u > 1.05 || std::fabs(v) > .6)
                continue;
            const double half = .48 * std::pow(std::max(0.0, std::sin(std::clamp(u, 0.0, 1.0) * pi * .92 + .25)), .8) *
                                (1 - .25 * u);
            const double lobe = u < .12 ? .05 * (1 - u / .12) : 0;
            const double inside = (half + lobe - std::fabs(v)) * leaf.size;
            const double notch = u < .05 ? std::fabs(v) * leaf.size - .01 : 1.0;
            a = std::clamp(std::min(inside, notch) / pixel + .5, 0.0, 1.0);
            // Veins: the midrib and the arching side veins, darker against the light.
            const double midrib = std::exp(-std::pow(v * leaf.size / .006, 2));
            const double side_phase = (u * 9 + std::fabs(v) * 6);
            const double sides = std::pow(std::fabs(std::cos(side_phase * pi)), 6) * smoothstep(.02, .1, std::fabs(v));
            vein = std::max(midrib, sides * .35);
        }
        if (a <= 0)
            continue;
        const double thin = (leaf.ground ? .1 : .16) * (1 - .6 * vein);
        const double shade = leaf.ground ? 1.0 + 1.6 * unit(leaf.seed) : .45;
        const Rgb tint{.38, .6, .12};
        Rgb color = backlit_leaf(c, direction, behind, thin, shade, tint);
        if (leaf.ground) {
            const double fleck = std::pow(smoothstep(.4, .65, fbm(leaf.x / .8, leaf.distance / .8, 208, 3)), 2);
            color = color + Rgb{.05, .09, .02} * (luminance(c.sunlight) * fleck * .5) +
                    Rgb{.03, .05, .015} * (luminance(c.fill_up) * .6 * (1 - vein));
        }
        out.color = mix(out.color, color, a);
        out.alpha = std::max(out.alpha, a);
    }
    return out;
}

// For the live scene: where a ray meets the falling sheet, what lies behind it, and what
// lies between it and the eye, so the sheet can be drawn anew each frame and the rest
// kept: what the eye sees is through * (sheet over rock + haze) + fog, then the jungle.
struct SheetCapture {
    bool hit = false;
    double x = 0, y = 0;        // on the wall
    Rgb rock{};
    Rgb haze{};
    double through = 1;
    Rgb fog{};
    double jungle = 0;          // the jungle's cover in front of it
    Rgb jungle_colour{};
    // The pool, where the ray meets it: its point, and how its sun glitter reaches the eye.
    bool pool = false;
    Vec3 pool_point{};
    double pool_reach = 0;      // the air's and the jungle's passing, times the Fresnel term
};

// Whether the falling sheet (or its brink) lies at (x, y) on the wall.
bool on_sheet(double x, double y) {
    const Brink here = brink_at(x);
    const double approx_drop = std::max(0.0, here.level - y);
    double centre = x;
    for (int i = 0; i < channel_count; ++i)
        if (x > channels[i].left - 3 && x < channels[i].right + 3)
            centre = (channels[i].left + channels[i].right) / 2;
    const double x0 = centre + (x - centre) / (1 + .009 * approx_drop);
    const Brink brink = brink_at(x0);
    const double depth = std::max(0.0, brink.level - brink.rock);
    return depth > 0 && brink.level - y >= -.07;
}

// What lies beyond the walkway along a ray from `origin`: the sky, the forest over
// the rim, the rock wall and the falls in front of it, the spray, the air. `sky_seen`
// is how much of the sky shows through (for the sun's visibility).
Rgb far_scene(const Context& c, Vec3 origin, Vec3 direction, double& sky_seen, bool live, SheetCapture& capture) {
    sky_seen = 0;
    if (direction.z <= .01)
        return sky_at(c, direction) * .2;
    const double t = (wall_z - origin.z) / direction.z;
    const double x = origin.x + direction.x * t;
    const double y = origin.y + direction.y * t;
    const Brink brink = brink_at(x);
    const double rim = brink.rock;
    const Rgb sky = sky_at(c, direction);
    Rgb base{};
    double open = 0;
    if (y > std::max(rim, brink.level - .02)) {
        double fringe_depth = 0;
        const double fringe = rim_fringe(x, y, rim, fringe_depth);
        const Cover forest = forest_layer(c, direction, sky);
        base = mix(sky, forest.color, forest.alpha);
        open = 1 - forest.alpha;
        if (fringe > 0) {
            const Rgb leaf = backlit_leaf(c, direction, sky, std::exp(-fringe_depth / .25) * .5 + .04, .8, Rgb{.4, .6, .15});
            base = mix(base, leaf, fringe);
            open *= 1 - fringe;
        }
    } else {
        // The falls' white light on the rock beside it.
        const double beside = from_channel(x);
        const double behind = brink.inside > .5 ? 1.0 : 0.0;
        const double drop = std::max(0.0, 40 - y);
        const double glow = .5 * std::exp(-beside / 2.5) * (1 - behind) * (.5 + .5 * std::exp(-drop / 25)) * luminance(c.sunlight);
        base = rock_color(c, x, y, rim, glow);
        if (behind > 0)
            base = base * .35;  // the hollow behind the sheet
        const double edge = smoothstep(-.25, 0, y - rim);
        base = base + c.sunlight * (.02 * edge);
    }
    const Rgb haze = crest_haze(c, direction, x, y) + plunge_mist(c, direction, x, y);
    if (live && on_sheet(x, y)) {
        // The live scene draws the sheet itself, each frame.
        capture.hit = true;
        capture.x = x;
        capture.y = y;
        capture.rock = base;
        capture.haze = haze;
        air_parts(c, direction, t, y, capture.through, capture.fog);
        sky_seen = open * .5;
        return (base + haze) * capture.through + capture.fog;
    }
    const Cover falls = falls_layer(c, direction, x, y, base);
    Rgb seen = mix(base, falls.color, falls.alpha);
    open *= 1 - falls.alpha;
    seen = seen + haze;
    seen = air_between(c, direction, seen, t, y);
    sky_seen = open;
    return seen;
}

Rgb far_scene(const Context& c, Vec3 origin, Vec3 direction, double& sky_seen) {
    SheetCapture unused;
    return far_scene(c, origin, direction, sky_seen, false, unused);
}

// ---- The walkway ---------------------------------------------------------------------

struct Box {
    Vec3 low, high;
    int kind = 0;  // 0 deck, 1 rail, 2 post
};

// The walkway runs across the view through the canopy; the eye stands on it, back
// from its far rail, looking out over the rail at the falls. Its deck of boards is laid
// across it, its rail has posts, a hand rail and two cables, and piles carry it down
// into the pool.
// The walkway in its own frame (x along it, z across it): a deck of boards laid
// across, on each side posts, a hand rail and two cables, and piles down into the pool.
std::vector<Box> walkway_boxes() {
    std::vector<Box> boxes;
    const double deck = deck_height;
    const double h = walkway_half;
    boxes.push_back({{-80, deck - .16, -h - eye_v}, {80, deck, h - eye_v}, 0});
    const double sides[2] = {-h - eye_v, h - eye_v};
    for (int k = 0; k < 2; ++k) {
        const double v = sides[k];
        boxes.push_back({{-80, deck + .98, v - .06}, {80, deck + 1.08, v + .06}, 1});
        boxes.push_back({{-80, deck + .64, v - .008}, {80, deck + .655, v + .008}, 3});
        boxes.push_back({{-80, deck + .32, v - .008}, {80, deck + .335, v + .008}, 3});
        for (double u = -78.8; u < 80; u += 2.4)
            boxes.push_back({{u - .055, deck - .2, v - .055}, {u + .055, deck + .98, v + .055}, 2});
        for (double u = -78; u < 80; u += 4.8)
            boxes.push_back({{u - .1, -.5, v - .1}, {u + .1, deck - .16, v + .1}, 2});
    }
    return boxes;
}

Vec3 to_walkway(Vec3 p) {
    const double cs = std::cos(walkway_heading), sn = std::sin(walkway_heading);
    return {p.x * cs + p.z * sn, p.y, -p.x * sn + p.z * cs};
}
Vec3 from_walkway(Vec3 p) {
    const double cs = std::cos(walkway_heading), sn = std::sin(walkway_heading);
    return {p.x * cs - p.z * sn, p.y, p.x * sn + p.z * cs};
}

// The nearest box a ray enters, or false: where (in the walkway's frame, for its
// grain), and the face it enters through (in the world, for its light).
bool hit_box(const std::vector<Box>& boxes, Vec3 world_origin, Vec3 world_direction, double& nearest, Vec3& point,
             Vec3& normal, int& kind) {
    const Vec3 origin = to_walkway(world_origin);
    const Vec3 direction = to_walkway(world_direction);
    bool found = false;
    nearest = 1e9;
    for (const Box& box : boxes) {
        double near_t = -1e9, far_t = 1e9;
        int axis = 0;
        bool miss = false;
        const double o[3] = {origin.x, origin.y, origin.z};
        const double d[3] = {direction.x, direction.y, direction.z};
        const double lo[3] = {box.low.x, box.low.y, box.low.z};
        const double hi[3] = {box.high.x, box.high.y, box.high.z};
        for (int i = 0; i < 3; ++i) {
            if (std::fabs(d[i]) < 1e-12) {
                if (o[i] < lo[i] || o[i] > hi[i])
                    miss = true;
                continue;
            }
            double t0 = (lo[i] - o[i]) / d[i];
            double t1 = (hi[i] - o[i]) / d[i];
            if (t0 > t1)
                std::swap(t0, t1);
            if (t0 > near_t) {
                near_t = t0;
                axis = i;
            }
            far_t = std::min(far_t, t1);
        }
        if (miss || near_t > far_t || near_t <= 0 || near_t >= nearest)
            continue;
        nearest = near_t;
        found = true;
        kind = box.kind;
        point = origin + direction * near_t;
        const double sign = d[axis] > 0 ? -1.0 : 1.0;
        const Vec3 local = axis == 0 ? Vec3{sign, 0, 0} : axis == 1 ? Vec3{0, sign, 0} : Vec3{0, 0, sign};
        normal = from_walkway(local);
    }
    return found;
}

// ---- The banks -----------------------------------------------------------------------

// The forest floor on either side of the pool, falling to the water's edge: the left
// bank the walkway leaves behind the eye, the right bank it reaches far off. Below the
// water (or where there is no bank) it is under the pool.
// The pool's shore: how far `x`, `z` lies outside the water, metres (negative in it).
double outside_pool(double x, double z) {
    const double front = near_shore + 3 * fbm(x / 9, 1.1, 201, 3) + .0035 * x * x;
    const double half = 36 + 7 * fbm(z / 11, 2.3, 202, 3) + .12 * std::max(0.0, z - near_shore);
    const double from_front = front - z;
    const double from_side = std::fabs(x) - half;
    return std::max(from_front, from_side);
}

// The forest floor around the clearing: below the walkway a slope falling to the near
// shore; to either side the jungle's floor, rising gently away from the water.
double ground_height(double x, double z) {
    const double out = outside_pool(x, z);
    if (out < -3)
        return -5;
    const double toward = std::clamp((near_shore - z) / (near_shore - 2), 0.0, 1.0);
    const double slope = (deck_height - 2) * std::pow(toward, 1.4);
    const double side = 3.5 * smoothstep(0, 18, std::fabs(x) - 30);
    const double rise = std::max(slope, side) * smoothstep(-1.5, 3, out) - .5 * (1 - smoothstep(-1.5, 2, out));
    const double lumps = .9 * fbm(x / 3.5, z / 3.5, 203, 3) * smoothstep(-1, 3, out);
    const Cells stones = cells(x / 1.6, z / 1.6, 204);
    const double boulder = 1.8 * std::max(0.0, .45 - stones.nearest) * (unit(stones.id) > .75 ? 1.0 : 0.0) *
                           smoothstep(-2, 1, out);
    return rise + lumps + boulder;
}

bool hit_ground(Vec3 origin, Vec3 direction, double limit, double& at) {
    double t = .3;
    double previous = t;
    // The banks are never higher than this, and end short of the cliff.
    const double top = 13;
    while (t < limit) {
        const Vec3 p = origin + direction * t;
        if (p.z > wall_z - 2 || (p.y > top && direction.y >= 0))
            return false;
        if (p.y < top && p.y < ground_height(p.x, p.z)) {
            // Narrow down to the surface.
            double low = previous, high = t;
            for (int i = 0; i < 8; ++i) {
                const double mid = (low + high) / 2;
                const Vec3 q = origin + direction * mid;
                if (q.y < ground_height(q.x, q.z))
                    high = mid;
                else
                    low = mid;
            }
            at = high;
            return true;
        }
        previous = t;
        // High above the banks the ray may stride to where it comes down to them.
        if (p.y > top && direction.y < 0)
            t = std::max(t + .04, (top - origin.y) / direction.y);
        else
            t += std::max(.04, t * .015);
    }
    return false;
}

// Leaf litter and dark soil under the canopy, moss on the rises, stones; lit by the
// sky through the leaves above and a few flecks of sun.
Rgb ground_color(const Context& c, Vec3 p) {
    const double e = .05;
    const double h = ground_height(p.x, p.z);
    const double hx = (ground_height(p.x + e, p.z) - h) / e;
    const double hz = (ground_height(p.x, p.z + e) - h) / e;
    const Vec3 n = normalized({-hx, 1, -hz});
    const double litter = fbm(p.x / .3, p.z / .3, 205, 3);
    Rgb albedo = mix(Rgb{.045, .03, .018}, Rgb{.09, .06, .03}, smoothstep(-.4, .5, litter));
    albedo = mix(albedo, Rgb{.035, .07, .02}, smoothstep(.1, .5, fbm(p.x / 2, p.z / 2, 206, 3)) * .8);
    const Cells stones = cells(p.x / 1.6, p.z / 1.6, 204);
    if (unit(stones.id) > .7 && stones.nearest < .45)
        albedo = mix(Rgb{.07, .068, .062}, Rgb{.04, .07, .03}, smoothstep(.1, .6, n.y) * .7);
    const double wet = smoothstep(1.2, 0, h);
    albedo = albedo * (1 - .4 * wet);
    const double canopy = .35 + .65 * smoothstep(-.1, .5, fbm(p.x / 2.5, p.z / 2.5, 207, 3));
    const double flecks = std::pow(smoothstep(.35, .6, fbm(p.x / .8, p.z / .8, 208, 3)), 2);
    const Rgb light = mix(c.fill_front, c.fill_up, std::clamp(n.y, 0.0, 1.0)) * (.55 * canopy) +
                      c.sunlight * (std::max(0.0, dot(n, c.sun)) * flecks * .6);
    return albedo * light;
}

// Weathered hardwood, wet from the spray: boards across the deck with gaps, grain
// along each piece, sunlight broken by the canopy, and the glare of the wet top toward
// the sun.
Rgb wood(const Context& c, Vec3 point, Vec3 normal, Vec3 direction, int kind) {
    double along = point.x, across = point.z;
    if (kind == 0) {
        along = point.z;
        across = point.x;
    }
    if (kind == 3) {
        const double up = std::clamp(normal.y, 0.0, 1.0);
        return mix(c.fill_front, c.fill_up, up) * .05 + c.sunlight * (.1 * std::pow(std::max(0.0, dot(direction, c.sun)), 30));
    }
    // Hardwood weathered to a warm grey-brown: each board its own tone, the grain in
    // long streaks with darker figure, damp in the gaps and along the edges.
    const double board = across / .16;
    const double index = std::floor(board);
    const double within = board - index;
    const bool deck_top = kind == 0 && normal.y > .5;
    const double gap = deck_top ? smoothstep(.0, .06, within) * (1 - smoothstep(.94, 1, within)) : 1.0;
    const std::uint32_t own = hash2(static_cast<int>(index), kind, 211);
    const double tone = .65 + .7 * unit(own);
    const double streaks = fbm(along * 1.1 + index * 17, across * 45, 212, 4);
    const double figure = std::pow(std::fabs(std::sin((along * 2.3 + 3 * fbm(along * .6, across * 9 + index, 217, 3)) * pi)), 6);
    const double grain = .72 + .45 * streaks - .18 * figure;
    const Rgb warm{.165, .118, .078};
    const Rgb grey{.13, .118, .102};
    Rgb albedo = mix(warm, grey, .35 + .4 * unit(hash32(own))) * (tone * grain);
    albedo = mix(albedo, Rgb{.06, .075, .035}, .2 * smoothstep(.25, .7, fbm(point.x * 2, point.z * .7, 213, 3)));
    const double edge_damp = deck_top ? 1 - .25 * (1 - smoothstep(.06, .2, std::min(within, 1 - within))) : 1.0;
    albedo = albedo * (.8 * edge_damp);
    const double sun_dot = std::max(0.0, dot(normal, c.sun));
    const double up = std::clamp(normal.y, 0.0, 1.0);
    // In the open at midday, the light softened by the thin cloud behind: the sky's
    // light, the sun on it but for the odd shadow of a leaf overhead.
    const double flecks = 1 - .55 * smoothstep(.15, .45, fbm(point.x / 1.4 + 3.3, point.z / 1.4, 216, 3));
    double corner = 1;
    if (deck_top) {
        const double to_rail = std::min(point.z - (-walkway_half - eye_v), (walkway_half - eye_v) - point.z);
        corner = .7 + .3 * smoothstep(0, .6, to_rail);
    }
    const Rgb sky_light = mix(c.fill_front, c.fill_up, up);
    const Rgb from_water = c.water_light * std::clamp(-normal.y * .8 + .4, 0.0, 1.0);
    Rgb color = albedo * (sky_light * corner + from_water + c.sunlight * (sun_dot * flecks * .5)) * gap;
    // A wet surface mirrors what it faces: on the deck, faintly, the sky; on the rail,
    // beaded with spray, the bright falls and sky glisten in a run of small highlights.
    Vec3 mirror = direction - normal * (2 * dot(direction, normal));
    mirror = normalized(mirror);
    const double cos_view = std::max(0.0, -dot(direction, normal));
    const double fresnel = .03 + .97 * std::pow(1 - cos_view, 5);
    Rgb seen = sky_at(c, mirror);
    // The falls fill the mirror's view straight ahead and a little up.
    const double az = std::atan2(mirror.x, mirror.z);
    const double el = std::asin(std::clamp(mirror.y, -1.0, 1.0));
    if (std::fabs(az) < .5 && el > -.15 && el < .32)
        seen = mix(seen, (c.fill_front * .9 + c.fill_up * .3 + c.sunlight * .05) * 1.3, smoothstep(.5, .35, std::fabs(az)));
    if (kind == 1) {
        // Drops scattered along it, each a tiny bright point where it mirrors the
        // falls, over a soft wet sheen.
        const double cell = .035;
        const int ia = static_cast<int>(std::floor(along / cell));
        const int ib = static_cast<int>(std::floor((across + normal.y * .37 + normal.x * .91) / cell));
        double beads = 0;
        for (int da = -1; da <= 1; ++da)
            for (int db = -1; db <= 1; ++db) {
                const std::uint32_t h = hash3(ia + da, ib + db, 4, 218);
                if (unit(h) > .22)
                    continue;
                const double ca = (ia + da + unit(hash32(h))) * cell;
                const double cb = (ib + db + unit(hash32(h + 1))) * cell;
                const double r = std::hypot(along - ca, (across + normal.y * .37 + normal.x * .91) - cb) / (.004 + .006 * unit(hash32(h + 2)));
                if (r < 1)
                    beads += (1 - r * r) * (4 + 20 * unit(hash32(h + 3)));
            }
        const double streak = .35 + .4 * (.5 + .5 * noise2(along * .8, 1.7, 219));
        const double wet = streak + beads;
        color = color + seen * (fresnel * wet * .7);
    } else if (deck_top) {
        color = color + seen * (fresnel * .25 * gap);
    }
    return color;
}


// ---- The pool ------------------------------------------------------------------------

double pool_height(const Context& c, double x, double z) {
    // Swell spreading from the plunges, and a fine chop.
    const double t = c.time;
    const double from_falls = std::max(0.0, wall_z - z);
    const double rings = .025 * std::sin(from_falls * 2.1 - t * 3.2 + .7 * fbm(x / 6, z / 6, 301, 2)) * std::exp(-from_falls / 25);
    const double chop = .012 * fbm(x / .9 + t * .15, z / .9 - t * .25, 302, 4);
    return rings + chop;
}

Rgb pool(const Context& c, Vec3 point, Vec3 direction, const std::vector<Box>& boxes, bool glitter_too) {
    const double e = .03;
    const double h = pool_height(c, point.x, point.z);
    const double hx = (pool_height(c, point.x + e, point.z) - h) / e;
    const double hz = (pool_height(c, point.x, point.z + e) - h) / e;
    const Vec3 n = normalized({-hx, 1, -hz});
    const double cos_in = std::max(0.02, -dot(direction, n));
    const double fresnel = .02 + .98 * std::pow(1 - cos_in, 5);
    Vec3 mirror = direction - n * (2 * dot(direction, n));
    mirror.y = std::max(mirror.y, .01);
    mirror = normalized(mirror);
    // The reflection: the walkway's underside and rails if the mirror ray meets them,
    // else the falls, the cliff, the forest and the sky.
    double t = 0;
    Vec3 hit{}, normal{};
    int kind = 0;
    Rgb reflected{};
    if (hit_box(boxes, point, mirror, t, hit, normal, kind)) {
        reflected = wood(c, hit, normal, mirror, kind) * .6;
    } else {
        double seen = 0;
        reflected = far_scene(c, point, mirror, seen);
    }
    // Under the surface: the ray bends into the water and runs down to the bottom,
    // stones and sand and sunken leaves shallow near the shore, deepening toward the
    // falls; the water takes the red first, then the blue, and glows its own green with
    // the light scattered in it, milkier toward the plunge.
    const double eta = 1 / 1.333;
    const double cos_i = -dot(direction, n);
    const double k = 1 - eta * eta * (1 - cos_i * cos_i);
    const Vec3 bent = normalized(direction * eta + n * (eta * cos_i - std::sqrt(std::max(0.0, k))));
    const double toward_falls = smoothstep(near_shore, wall_z - 6, point.z);
    const double bottom_depth = std::max(.3, .7 + 7 * toward_falls + 1.1 * fbm(point.x / 6, point.z / 6, 311, 3));
    const double path = bottom_depth / std::max(.05, -bent.y);
    const Vec3 floor_at = point + bent * path;
    const Cells pebbles = cells(floor_at.x / .35, floor_at.z / .35, 312);
    const Cells rocks = cells(floor_at.x / 2.2, floor_at.z / 2.2, 313);
    Rgb ground = mix(Rgb{.13, .11, .075}, Rgb{.07, .065, .055}, smoothstep(.2, .45, pebbles.nearest));
    ground = mix(ground, Rgb{.045, .045, .04}, unit(rocks.id) > .6 && rocks.nearest < .5 ? 1.0 : 0.0);
    ground = mix(ground, Rgb{.035, .06, .02}, smoothstep(.1, .5, fbm(floor_at.x / 2, floor_at.z / 2, 314, 3)) * .6);
    const double ripple_light = std::pow(1 - std::fabs(noise2(floor_at.x * 1.3 + c.time * .4, floor_at.z * 1.3, 315)), 7) +
                                .6 * std::pow(1 - std::fabs(noise2(floor_at.x * 2.9, floor_at.z * 2.9 - c.time * .5, 316)), 9);
    const double down = bottom_depth / std::max(.2, c.sun.y);
    const Rgb into{std::exp(-.5 * down), std::exp(-.2 * down), std::exp(-.32 * down)};
    const Rgb lit_floor = ground * (c.fill_up * .5 * Rgb{std::exp(-.5 * bottom_depth), std::exp(-.2 * bottom_depth), std::exp(-.32 * bottom_depth)} +
                                    c.sunlight * into * (std::max(0.0, c.sun.y) * (.35 + 1.4 * ripple_light)));
    const double murk = 1 + 2.5 * toward_falls;
    const Rgb through{std::exp(-.55 * murk * path), std::exp(-.24 * murk * path), std::exp(-.36 * murk * path)};
    const Rgb glow = Rgb{.012, .028, .016} * (luminance(c.fill_up) * 1.6) + Rgb{.02, .03, .03} * (toward_falls * toward_falls);
    const Rgb under = lit_floor * through + glow * Rgb{1 - through.r, 1 - through.g, 1 - through.b};
    Rgb color = mix(under, reflected, fresnel);
    // The sun glittering on the ripples: a sharp highlight on each facet turned just
    // so, and a broad sheen about the sun's path across the water.
    const Vec3 half_way = normalized(c.sun - direction);
    const double facing = std::max(0.0, dot(n, half_way));
    const double glitter = std::pow(facing, 900) * 60 + std::pow(facing, 60) * .5;
    if (glitter_too)
        color = color + c.sunlight * (glitter * (.3 + fresnel));
    // Foam drifting out from the plunges.
    const double from_wall = wall_z - point.z;
    const double plunge = std::exp(-from_channel(point.x) / 2) * std::exp(-from_wall / 6);
    const double foam = plunge * smoothstep(-.1, .5, fbm(point.x / 1.3, (from_wall - c.time * .8) / 1.3, 303, 4));
    color = mix(color, c.fill_front * 1.1 + c.fill_up * .3, std::clamp(foam, 0.0, 1.0) * .85);
    // Lily pads: round, with the notch to the centre, waxy, rimmed; a flower on a few.
    const double pixel = std::max(.004, length(point - Vec3{0, (*c.camera).eye_height, 0}) * .0009);
    for (const Lily& lily : c.lilies) {
        const double dx = point.x - lily.x;
        const double dz = point.z - lily.z;
        const double r = std::hypot(dx, dz);
        if (r > lily.radius + pixel)
            continue;
        double a = std::clamp((lily.radius - r) / pixel + .5, 0.0, 1.0);
        double angle = std::atan2(dz, dx) - lily.angle;
        angle = angle - 2 * pi * std::floor(angle / (2 * pi));
        const double notch_half = .2;
        const double from_notch = (std::min(angle, 2 * pi - angle) - notch_half) * r;
        a *= std::clamp(from_notch / pixel + .5, 0.0, 1.0);
        const double veins = .9 + .1 * std::cos(angle * 22);
        const Rgb albedo = Rgb{.025, .06, .012} * (veins * (.85 + .3 * unit(hash2(static_cast<int>(lily.x * 10), static_cast<int>(lily.z * 10), 7))));
        Rgb pad = albedo * (c.fill_up * .9 + c.sunlight * (std::max(0.0, c.sun.y) * .8));
        const Vec3 up{0, 1, 0};
        const Vec3 m = direction - up * (2 * dot(direction, up));
        pad = pad + sky_at(c, m) * .02 + c.sunlight * (std::pow(std::max(0.0, dot(m, c.sun)), 40) * .3);
        const double rim = smoothstep(lily.radius - pixel * 2, lily.radius, r);
        pad = mix(pad, pad * 1.6 + Rgb{.02, .0, .01}, rim * .5);
        if (lily.flower && r < lily.radius * .45) {
            const double petals = .5 + .5 * std::cos(std::atan2(dz, dx) * 8);
            const double bloom = smoothstep(lily.radius * .45, lily.radius * .2, r) * (.6 + .4 * petals);
            const Rgb petal = Rgb{.75, .55, .62} * (luminance(c.fill_up) * 1.5) + c.sunlight * .25;
            const Rgb heart = Rgb{.7, .5, .08} * (luminance(c.fill_up) * 1.5) + c.sunlight * Rgb{.3, .22, .03};
            pad = mix(pad, r < lily.radius * .1 ? heart : petal, std::clamp(bloom, 0.0, 1.0));
        }
        color = mix(color, pad, a);
    }
    return color;
}

// ---- One sample ----------------------------------------------------------------------

// What the eye sees along one ray, in two parts so particles can go between them:
// what lies behind the falling water (the pool, the wall, the sky), and what is in
// front of it (the walkway, the near leaves) with its cover.
struct Sample {
    Rgb behind{};
    double depth = 1e9;     // the behind part's distance along the view
    Rgb front{};            // premultiplied by its cover
    double cover = 0;
    double far_open = 0;    // how much of the sky shows through the behind part
};

Sample trace(const Context& c, const std::vector<Box>& boxes, Vec3 direction, bool live, SheetCapture& capture) {
    const Vec3 eye{0, (*c.camera).eye_height, 0};
    // Depths as the particles measure them: along the view for a pinhole lens, along
    // the ray for a stereographic one.
    const double along = (*c.camera).bend > 0 ? 1.0 : dot(direction, camera_basis(*c.camera).forward);
    Sample out;
    double t = 0;
    Vec3 hit{}, normal{};
    int kind = 0;
    const bool on_walkway = hit_box(boxes, eye, direction, t, hit, normal, kind);
    const double pool_t = direction.y < 0 ? -eye.y / direction.y : 1e9;
    const Vec3 pool_point = eye + direction * pool_t;
    const bool on_pool = pool_t < 1e8 && pool_point.z < wall_z;
    double ground_t = 1e9;
    const double reach = std::min(on_walkway ? t : 1e9, on_pool ? pool_t : 120.0);
    const bool on_ground = hit_ground(eye, direction, reach, ground_t);
    double limit = 1e9;  // the nearest opaque thing, which hides leaves beyond it
    if (on_walkway && (!on_pool || t < pool_t) && (!on_ground || t < ground_t)) {
        out.front = air_between(c, direction, wood(c, hit, normal, direction, kind), t, hit.y);
        out.cover = 1;
        out.depth = t * along;
        limit = t;
    } else if (on_ground) {
        const Vec3 p = eye + direction * ground_t;
        out.behind = air_between(c, direction, ground_color(c, p), ground_t, p.y);
        out.depth = ground_t * along;
        limit = ground_t;
    } else if (on_pool) {
        limit = pool_t;
        Rgb seen = pool(c, pool_point, direction, boxes, !live);
        seen = seen + plunge_mist(c, direction, pool_point.x, 1.5) * std::min(1.0, pool_t / wall_z);
        out.behind = air_between(c, direction, seen, pool_t, 0);
        out.depth = pool_t * along;
        if (live) {
            double through = 1;
            Rgb unused{};
            air_parts(c, direction, pool_t, 0, through, unused);
            const double cos_in = std::max(.02, -direction.y);
            capture.pool = true;
            capture.pool_point = pool_point;
            capture.pool_reach = through * (.3 + .02 + .98 * std::pow(1 - cos_in, 5));
        }
    } else {
        out.behind = far_scene(c, eye, direction, out.far_open, live, capture);
        out.depth = direction.z > .01 ? (wall_z / direction.z) * along : 1e9;
        limit = direction.z > .01 ? wall_z / direction.z : 1e9;
        if (out.far_open > .5)
            out.depth = 1e9;
    }
    // The jungle either side, in front of whatever lies behind it.
    if (out.cover < 1) {
        double jungle_at = 1e9;
        const Cover trees = jungle_layer(c, direction, out.behind, limit, jungle_at);
        if (trees.alpha > 0) {
            capture.jungle = trees.alpha;
            capture.jungle_colour = air_between(c, direction, trees.color, jungle_at, 10);
            out.behind = mix(out.behind, capture.jungle_colour, trees.alpha);
            out.far_open *= 1 - trees.alpha;
            if (trees.alpha > .5) {
                out.depth = jungle_at * along;
                limit = std::min(limit, jungle_at);
            }
        }
    }
    const Rgb seen = out.cover >= 1 ? out.front : out.behind;
    const Cover near = near_canopy(c, direction, seen, limit);
    out.front = out.front * (1 - near.alpha) + near.color * near.alpha;
    out.cover = out.cover + near.alpha * (1 - out.cover);
    return out;
}

Sample trace(const Context& c, const std::vector<Box>& boxes, Vec3 direction) {
    SheetCapture unused;
    return trace(c, boxes, direction, false, unused);
}

// ---- The falling water's particles ---------------------------------------------------

struct FallsParticles {
    particles::System system{40000};
    int clumps_from = 0, clumps_to = 0;   // emitters of white water breaking from the sheet
    int drops_from = 0, drops_to = 0;     // drops thrown off its face
    int glints_from = 0, glints_to = 0;   // drops catching the sun
    int mist_from = 0, mist_to = 0;       // spray boiling up from the plunge
    std::vector<double> clump_x;          // each clump emitter's place across the wall
    std::vector<float> clump_rate;        // and its steady rate
    std::vector<bool> wide;               // and whether it makes broad faint sheets
    std::vector<float> base_rates;        // every emitter's rate as planted
};

particles::Vec vec(double x, double y, double z) {
    return {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
}

// How much water a part of the curtain carries just now: surges and lulls that travel
// along the brink, so the falls come down in heavier and lighter bands.
double surge(double x, double t) {
    const double slow = noise2(x / 12 + 3.1, t * .32, 120);
    const double quick = noise2(x / 3.5 + 8.7, t * 1.1, 121);
    return .12 + 2.2 * smoothstep(-.3, .6, slow + .4 * quick);
}

void plant_falls(FallsParticles& f, std::uint32_t seed) {
    particles::System& system = f.system;
    const Channel& ch = channels[0];
    // White water: clumps leaving the brink in metre-wide strips.
    f.clumps_from = system.emitter_count();
    for (double x = ch.left - .5; x < ch.right + .5; x += 1) {
        const Brink b = brink_at(x + .5);
        const double depth = std::max(0.0, b.level - b.rock);
        if (depth < .03)
            continue;
        for (int wide = 0; wide < 2; ++wide) {
            particles::Emitter e;
            e.low = vec(x, b.level - .25, wall_z - 1.1);
            e.high = vec(x + 1, b.level, wall_z - .6);
            e.velocity = vec(0, -.9, -1.5);
            e.velocity_spread = vec(.25, .4, .6);
            e.life = {9, 9};
            e.kind = particles::Kind::streak;
            e.seed = seed * 131U + static_cast<std::uint32_t>(x * 7 + 1000) + static_cast<std::uint32_t>(wide) * 7919U;
            e.forces.drag = .05f;
            e.forces.floor = 0;
            if (wide == 0) {
                // Clumps of white water and the torn runs behind them.
                e.rate = static_cast<float>(20 + 70 * depth);
                e.size = {static_cast<float>(.08 + .2 * depth), static_cast<float>(.3 + .8 * depth)};
                e.tail = {.04f, .24f};
            } else {
                // Broad, faint sheets of spray between them.
                e.rate = static_cast<float>(3 + 9 * depth);
                e.size = {.9f, 2.4f};
                e.tail = {.15f, .4f};
            }
            static_cast<void>(system.add_emitter(e));
            f.clump_x.push_back(x + .5);
            f.clump_rate.push_back(e.rate);
            f.wide.push_back(wide == 1);
        }
    }
    f.clumps_to = system.emitter_count();
    // Drops flung off the face as it falls, and the glints among them.
    const double heights[5] = {34, 28, 21, 14, 8};
    for (int kind = 0; kind < 2; ++kind) {
        if (kind == 0)
            f.drops_from = system.emitter_count();
        else
            f.glints_from = system.emitter_count();
        for (int k = 0; k < 5; ++k) {
            const double h = heights[k];
            const double fallen = ch.level - h;
            const double out = 1.5 * flight_time(fallen);
            particles::Emitter e;
            e.low = vec(ch.left + 1, h - 3, wall_z - out - .8);
            e.high = vec(ch.right - 1, h + 3, wall_z - out - .2);
            e.rate = kind == 0 ? 420 : 300;
            e.velocity = vec(0, -.85 * fall_speed(fallen), -2.6);
            e.velocity_spread = vec(1.4, 2.5, 2.2);
            e.life = kind == 0 ? particles::Range{1.2f, 2.4f} : particles::Range{.5f, 1.4f};
            e.size = {.012f, .035f};
            e.tail = {.015f, .05f};
            e.kind = kind == 0 ? particles::Kind::streak : particles::Kind::glint;
            e.seed = seed * 977U + static_cast<std::uint32_t>(kind * 10 + k);
            e.forces.drag = .35f;
            e.forces.swirl = 1.2f;
            e.forces.swirl_scale = 5;
            e.forces.floor = 0;
            static_cast<void>(system.add_emitter(e));
        }
        if (kind == 0)
            f.drops_to = system.emitter_count();
        else
            f.glints_to = system.emitter_count();
    }
    // Spray boiling up and rolling out from the plunge, slow and swirling.
    f.mist_from = system.emitter_count();
    for (int k = 0; k < 2; ++k) {
        particles::Emitter e;
        e.low = vec(ch.left - 2, 0, wall_z - 9 - 6 * k);
        e.high = vec(ch.right + 2, 3 - 1.5 * k, wall_z - 2 - 6 * k);
        e.rate = k == 0 ? 34 : 10;
        e.velocity = vec(0, 2.0 - 1.4 * k, -1.2);
        e.velocity_spread = vec(1.5, 1.8, 1.0);
        e.life = {8, 15};
        e.size = {2.0f + k, 5.0f + k};
        e.kind = particles::Kind::mist;
        e.seed = seed * 5U + 77U + static_cast<std::uint32_t>(k);
        e.forces.gravity = 0;
        e.forces.lift = .12f;
        e.forces.drag = .6f;
        e.forces.swirl = 1.6f;
        e.forces.swirl_scale = 7;
        e.forces.swirl_period = 5;
        e.forces.growth = .45f;
        static_cast<void>(system.add_emitter(e));
    }
    f.mist_to = system.emitter_count();
}

// Runs the falls from still water to `time`, the surges driving the clumps' rates.
void run_falls(FallsParticles& f, double time) {
    const double warm = 10;  // long enough for the first water to reach the pool
    const double step = 1.0 / 30;
    const int steps = static_cast<int>(std::ceil((warm + time) / step));
    for (int n = 0; n < steps; ++n) {
        const double t = n * step - warm;
        for (int e = f.clumps_from; e < f.clumps_to; ++e) {
            const std::size_t k = static_cast<std::size_t>(e - f.clumps_from);
            f.system.set_rate(e, static_cast<float>(f.clump_rate[k] * surge(f.clump_x[k], t)));
        }
        f.system.advance(step);
    }
}

particles::Lens lens_for(const Camera& camera, int width, int height) {
    const Basis basis = camera_basis(camera);
    particles::Lens lens;
    lens.eye = vec(0, camera.eye_height, 0);
    lens.forward = vec(basis.forward.x, basis.forward.y, basis.forward.z);
    lens.right = vec(basis.right.x, basis.right.y, basis.right.z);
    lens.up = vec(basis.up.x, basis.up.y, basis.up.z);
    lens.focal = static_cast<float>(focal_pixels(camera, height));
    lens.bend = static_cast<float>(camera.bend);
    lens.width = width;
    lens.height = height;
    return lens;
}

// Each particle lit as the scene lights it: white water by the sky and by the sun
// through it, drops brighter, glints the sun itself twinkling, mist glowing.
void shade_particles(const Context& c, const FallsParticles& f, const particles::Lens& lens, std::vector<particles::Shade>& shades) {
    const particles::System& system = f.system;
    shades.assign(system.count(), particles::Shade{});
    const Vec3 eye{0, (*c.camera).eye_height, 0};
    const double level = channels[0].level;
    for (std::size_t i = 0; i < system.count(); ++i) {
        const particles::Vec p = system.positions()[i];
        const int origin = system.origins()[i];
        const double age = system.ages()[i];
        const double life = system.lives()[i];
        const std::uint32_t seed = system.seeds()[i];
        const Vec3 at{p.x, p.y, p.z};
        const Vec3 direction = normalized(at - eye);
        const double mu = dot(direction, c.sun);
        const double drop = level - p.y;
        const double sunlit = 1 - .55 * smoothstep(8, 26, drop);
        const double vary = .8 + .4 * unit(seed);
        particles::Shade shade;
        Rgb colour{};
        double alpha = 0;
        if (origin >= f.clumps_from && origin < f.clumps_to) {
            const double froth_glow = phase_hg(mu, .6) * 4 * pi;
            const bool wide = f.wide[static_cast<std::size_t>(origin - f.clumps_from)];
            colour = (c.fill_front * .8 + c.fill_up * .2) * vary + c.sunlight * (.026 * froth_glow * sunlit * vary);
            const double strength = wide ? .22 : .3 + .55 * unit(hash32(seed + 3));
            alpha = strength * smoothstep(1.5, 7, drop) * smoothstep(0, 3, p.y);
        } else if (origin >= f.drops_from && origin < f.drops_to) {
            const double drop_glow = phase_hg(mu, .75) * 4 * pi;
            colour = (c.fill_front * .9 + c.fill_up * .2) * vary + c.sunlight * (.05 * drop_glow * sunlit);
            alpha = .6 * (1 - smoothstep(.7, 1, age / life));
        } else if (origin >= f.glints_from && origin < f.glints_to) {
            // A drop flashes as it turns through the angle that sends the sun to the eye.
            const double phase = unit(hash32(seed)) * 2 * pi + age * (8 + 10 * unit(seed));
            const double twinkle = std::pow(std::max(0.0, std::sin(phase)), 14);
            const double glint_glow = phase_hg(mu, .8) * 4 * pi;
            colour = c.sunlight * (glint_glow * twinkle * sunlit * (*c.look).glints * 1.6);
            alpha = 1;  // pixels' worth of the colour
        } else {
            // Mist: a puff's light spread over its disc; it thickens as it forms and
            // thins away at the end of its life.
            const particles::Projected seen = particles::project(lens, p);
            if (!seen.in_front)
                continue;
            const double radius = system.sizes()[i] * seen.scale;
            const double fade = smoothstep(0, .25, age / life) * (1 - smoothstep(.55, 1, age / life));
            const double mist_glow = phase_hg(mu, .7) * 4 * pi;
            colour = c.fill_front * 1.05 + c.fill_up * .3 + c.sunlight * (.045 * mist_glow * smoothstep(-2, 6, p.y));
            alpha = .03 * fade * pi * radius * radius * .5 * vary * vary;
        }
        shade.r = static_cast<float>(colour.r);
        shade.g = static_cast<float>(colour.g);
        shade.b = static_cast<float>(colour.b);
        shade.alpha = static_cast<float>(alpha);
        shades[i] = shade;
    }
}

std::vector<particles::Shade> shade_falls(const Context& c, const FallsParticles& f, const particles::Lens& lens) {
    std::vector<particles::Shade> shades;
    shade_particles(c, f, lens, shades);
    return shades;
}

} // namespace

void render_still(const Look& look, const Camera& camera, int width, int height, int first_row, int last_row,
                  double time, int samples, LinearImage& out) {
    Context c;
    prepare(c, look, camera, width, height, time);
    const std::vector<Box> boxes = walkway_boxes();
    out.width = width;
    out.height = last_row - first_row;
    const std::size_t pixels = static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height);
    out.pixels.assign(pixels, Rgb{});
    const Vec3 sun = c.sun;
    // Every pixel's behind and front parts, its behind depth from its middle sample.
    std::vector<Rgb> behind(pixels), front(pixels);
    std::vector<double> cover(pixels, 0.0), open(pixels, 0.0);
    std::vector<float> depth(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 1e9f);
    const double per = 1.0 / (samples * samples);
    for (int row = first_row; row < last_row; ++row)
        for (int column = 0; column < width; ++column) {
            const std::size_t i = static_cast<std::size_t>(row - first_row) * static_cast<std::size_t>(width) +
                                  static_cast<std::size_t>(column);
            for (int sy = 0; sy < samples; ++sy)
                for (int sx = 0; sx < samples; ++sx) {
                    const double px = column + (sx + .5) / samples;
                    const double py = row + (sy + .5) / samples;
                    const Vec3 d = view_direction(c, px, py);
                    Sample s = trace(c, boxes, d);
                    const double angle = std::acos(std::clamp(dot(d, sun), -1.0, 1.0));
                    if (s.far_open > 0 && angle < Sky::disc_radius())
                        s.behind = s.behind + c.sky.disc(angle) * s.far_open;
                    behind[i] = behind[i] + s.behind * per;
                    front[i] = front[i] + s.front * per;
                    cover[i] += s.cover * per;
                    open[i] += s.far_open * (1 - s.cover) * per;
                    if (sx == samples / 2 && sy == samples / 2)
                        depth[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(column)] =
                            static_cast<float>(s.depth);
                }
        }
    // The falling water's particles between the two parts.
    FallsParticles falls;
    plant_falls(falls, look.seed);
    run_falls(falls, time);
    const particles::Lens lens = lens_for(camera, width, height);
    const std::vector<particles::Shade> shades = shade_falls(c, falls, lens);
    particles::Layer layer;
    particles::Splat splat;
    splat.softness = .85f;
    splat.taper = .3f;
    splat.fray = .6f;
    splat.fray_length = .4f;
    particles::Splatter::draw(falls.system, shades, lens, depth, splat, layer);
    std::vector<float> picture(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3, 0.0f);
    for (std::size_t i = 0; i < pixels; ++i) {
        const std::size_t at = (static_cast<std::size_t>(first_row) * static_cast<std::size_t>(width) + i) * 3;
        picture[at] = static_cast<float>(behind[i].r);
        picture[at + 1] = static_cast<float>(behind[i].g);
        picture[at + 2] = static_cast<float>(behind[i].b);
    }
    particles::Splatter::compose(layer, picture);
    // How much of the sun shows, for its glare in the eye, which lies over everything.
    double sun_visible = 0;
    {
        const Basis basis = camera_basis(camera);
        const double ahead = dot(sun, basis.forward);
        double sx = -1, sy = -1;
        static_cast<void>(pixel_of(camera, width, height, sun, sx, sy));
        const int ix = static_cast<int>(sx);
        const int iy = static_cast<int>(sy);
        if (ahead > 0 && ix >= 0 && ix < width && iy >= first_row && iy < last_row) {
            sun_visible = open[static_cast<std::size_t>(iy - first_row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(ix)];
        } else if (ahead > 0) {
            // Out of the picture: trace toward it.
            const Sample s = trace(c, boxes, sun);
            sun_visible = s.far_open * (1 - s.cover);
        }
    }
    for (int row = first_row; row < last_row; ++row)
        for (int column = 0; column < width; ++column) {
            const std::size_t i = static_cast<std::size_t>(row - first_row) * static_cast<std::size_t>(width) +
                                  static_cast<std::size_t>(column);
            const std::size_t at = (static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(column)) * 3;
            const Rgb seen{picture[at], picture[at + 1], picture[at + 2]};
            Rgb pixel = seen * (1 - cover[i]) + front[i];
            const Vec3 d = view_direction(c, column + .5, row + .5);
            const double angle = std::acos(std::clamp(dot(d, sun), -1.0, 1.0));
            pixel = pixel + c.sky.glare(angle) * sun_visible;
            out.pixels[i] = pixel;
        }
}

void render_sky(const Look& look, const Camera& camera, int width, int height, LinearImage& out) {
    Context c;
    c.look = &look;
    c.camera = &camera;
    c.sky = Sky(look.sky);
    c.width = width;
    c.height = height;
    out.width = width;
    out.height = height;
    out.pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), Rgb{});
    const Vec3 sun = c.sky.sun_direction();
    for (int row = 0; row < height; ++row)
        for (int column = 0; column < width; ++column) {
            // The disc sampled finely across the pixel; the sky is smooth.
            Rgb disc{};
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx) {
                    const Vec3 d = view_direction(c, column + (sx + .5) / 4, row + (sy + .5) / 4);
                    disc = disc + c.sky.disc(std::acos(std::clamp(dot(d, sun), -1.0, 1.0)));
                }
            const Vec3 d = view_direction(c, column + .5, row + .5);
            const double angle = std::acos(std::clamp(dot(d, sun), -1.0, 1.0));
            out.pixels[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(column)] =
                c.sky.radiance(d) + disc * (1.0 / 16) + c.sky.glare(angle);
        }
}

namespace {
void blur(std::vector<Rgb>& image, int width, int height, double sigma) {
    const int radius = static_cast<int>(std::ceil(sigma * 3));
    std::vector<double> kernel(static_cast<std::size_t>(2 * radius + 1));
    double total = 0;
    for (int i = -radius; i <= radius; ++i) {
        kernel[static_cast<std::size_t>(i + radius)] = std::exp(-i * i / (2 * sigma * sigma));
        total += kernel[static_cast<std::size_t>(i + radius)];
    }
    for (double& k : kernel)
        k /= total;
    std::vector<Rgb> line(static_cast<std::size_t>(std::max(width, height)));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Rgb s{};
            for (int i = -radius; i <= radius; ++i) {
                const int xx = std::clamp(x + i, 0, width - 1);
                s = s + image[static_cast<std::size_t>(y) * width + xx] * kernel[static_cast<std::size_t>(i + radius)];
            }
            line[static_cast<std::size_t>(x)] = s;
        }
        for (int x = 0; x < width; ++x)
            image[static_cast<std::size_t>(y) * width + x] = line[static_cast<std::size_t>(x)];
    }
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            Rgb s{};
            for (int i = -radius; i <= radius; ++i) {
                const int yy = std::clamp(y + i, 0, height - 1);
                s = s + image[static_cast<std::size_t>(yy) * width + x] * kernel[static_cast<std::size_t>(i + radius)];
            }
            line[static_cast<std::size_t>(y)] = s;
        }
        for (int y = 0; y < height; ++y)
            image[static_cast<std::size_t>(y) * width + x] = line[static_cast<std::size_t>(y)];
    }
}
} // namespace

void add_bloom(LinearImage& image, double threshold) {
    std::vector<Rgb> bright(image.pixels.size());
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        const Rgb p = image.pixels[i];
        const double l = luminance(p);
        if (l > threshold)
            bright[i] = p * ((l - threshold) / l);
    }
    std::vector<Rgb> wide = bright;
    blur(bright, image.width, image.height, 1.3);
    blur(wide, image.width, image.height, 6);
    for (std::size_t i = 0; i < image.pixels.size(); ++i)
        image.pixels[i] = image.pixels[i] + bright[i] * .1 + wide[i] * .006;
}

void tone_map(const LinearImage& image, double exposure, std::vector<std::uint8_t>& bgra) {
    bgra.assign(image.pixels.size() * 4, 255);
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        const Rgb p = image.pixels[i] * exposure;
        const double channels[3] = {p.b, p.g, p.r};
        for (int k = 0; k < 3; ++k) {
            const double x = std::max(0.0, channels[k]);
            // The fitted ACES curve: a toe, a long straight run, a soft shoulder.
            const double mapped = std::clamp(x * (2.51 * x + .03) / (x * (2.43 * x + .59) + .14), 0.0, 1.0);
            const double encoded = mapped <= .0031308 ? mapped * 12.92 : 1.055 * std::pow(mapped, 1 / 2.4) - .055;
            bgra[i * 4 + static_cast<std::size_t>(k)] = static_cast<std::uint8_t>(std::lround(encoded * 255));
        }
    }
}


// ---- The live scene --------------------------------------------------------------------

namespace {

// The noise the falling sheet is drawn from, made once: for each column of water across
// the brink and each moment of a loop of the water's own time, the values the sheet's
// look is made of, so a frame reads them instead of computing noise. The loop is joined
// by crossfading its two ends.
constexpr int noise_channels = 9;  // streak, fine, clump, pulse, fray, rope, swell, texture, fingers
constexpr double loop_seconds = 8;
constexpr int loop_rows = 384;     // 48 a second

struct SheetNoise {
    int columns = 0;
    double x_from = 0, dx = .12;
    std::vector<float> values;     // columns * loop_rows * noise_channels
};

void sheet_noise_at(double xs, double s, float out[noise_channels]) {
    const double streak = fbm(xs * 1.7, s * 2.6, 81, 4);
    const double fine = noise2(xs * 7.5, s * 9, 82);
    const double clump = fbm(xs * 1.1, s * 2.2, 83, 3);
    const double pulse = fbm(xs * .09, s * .9, 84, 3);
    const double fray = noise2(s * 1.6, xs * .7, 95);
    const double ridge_a = 1 - std::fabs(noise2(xs * 3.2, s * .9, 89));
    const double ridge_b = 1 - std::fabs(noise2(xs * 9, s * 1.6, 90));
    const double swell = .5 + .5 * noise2(xs * .8, s * .6, 94);
    const double rope = std::pow(ridge_a, 12) * (.4 + swell) + .3 * std::pow(ridge_b, 16);
    const double torn = fbm(xs * 3.2, s * 5.5, 91, 5);
    const double strands = 1 - std::fabs(noise2(xs * 11, s * 3.5, 92));
    const double texture = std::clamp(.45 + .6 * torn + .35 * std::pow(strands, 4) + .2 * pulse, 0.0, 1.4);
    const double fingers = 1 - std::fabs(noise2(xs * 2.4, s * 1.1, 93));
    out[0] = static_cast<float>(streak);
    out[1] = static_cast<float>(fine);
    out[2] = static_cast<float>(clump);
    out[3] = static_cast<float>(pulse);
    out[4] = static_cast<float>(fray);
    out[5] = static_cast<float>(rope);
    out[6] = static_cast<float>(swell);
    out[7] = static_cast<float>(texture);
    out[8] = static_cast<float>(fingers);
}

void make_sheet_noise(SheetNoise& n) {
    const Channel& ch = channels[0];
    n.x_from = ch.left - 3;
    n.columns = static_cast<int>((ch.right - ch.left + 6) / n.dx) + 1;
    n.values.assign(static_cast<std::size_t>(n.columns) * loop_rows * noise_channels, 0.0f);
    for (int col = 0; col < n.columns; ++col) {
        const double xs = n.x_from + col * n.dx;
        for (int row = 0; row < loop_rows; ++row) {
            const double s = loop_seconds * row / loop_rows;
            const double w = static_cast<double>(row) / loop_rows;
            float a[noise_channels] = {}, b[noise_channels] = {};
            sheet_noise_at(xs, s, a);
            sheet_noise_at(xs, s - loop_seconds, b);
            float* out = &n.values[(static_cast<std::size_t>(col) * loop_rows + static_cast<std::size_t>(row)) * noise_channels];
            for (int k = 0; k < noise_channels; ++k)
                out[k] = static_cast<float>(a[k] * (1 - w) + b[k] * w);
        }
    }
}

// What a frame needs of one pixel of the sheet, worked out once per size: everything
// that does not move.
struct SheetPixel {
    std::uint32_t index = 0;
    std::uint32_t column = 0;
    float tau = 0;              // the water's flight time to here
    bool rim = false;           // the brink itself
    float x0 = 0;
    float coherent = 0, aerated = 0, gaps = 0, onset = 0, veil = 0, end = 0, low = 0, depth_edge = 0;
    float glass0[3]{}, glass1[3]{}, froth0[3]{}, froth1[3]{};
    float rock[3]{};
    float a = 1, b[3]{};        // what the eye sees: a * (the sheet over the rock) + b
};

// One pixel of the pool where the sun's glitter can reach the eye.
struct GlitterPixel {
    std::uint32_t index = 0;
    float x = 0, z = 0;
    float half[3]{};            // between the sun and the eye
    float reach = 0;
};

// Everything that does not move, for one picture size.
struct Baked {
    int width = 0, height = 0;
    std::vector<float> behind;   // 3 per pixel, linear light
    std::vector<float> over;     // 3 per pixel: the walkway, the near leaves and the sun's glare
    std::vector<float> cover;    // per pixel: how much the walkway and leaves hide
    std::vector<float> depth;    // per pixel: the distance particles must be nearer than
    std::vector<SheetPixel> sheet;
    std::vector<GlitterPixel> glitter;
    Context context;
    std::vector<Box> boxes;
    int samples = 2;
    int band_rows = 16;
    // Each band's sheet and glitter pixels, joined in order when the size is finished;
    // a band writes only its own, so bands can be baked at once.
    std::vector<std::vector<SheetPixel>> band_sheet;
    std::vector<std::vector<GlitterPixel>> band_glitter;
};

SheetPixel sheet_pixel(const Context& c, Vec3 direction, const SheetCapture& cap, const SheetNoise& noise) {
    SheetPixel p;
    const double x = cap.x, y = cap.y;
    const Brink here = brink_at(x);
    const double approx_drop = std::max(0.0, here.level - y);
    double centre = x;
    for (int i = 0; i < channel_count; ++i)
        if (x > channels[i].left - 3 && x < channels[i].right + 3)
            centre = (channels[i].left + channels[i].right) / 2;
    const double x0 = centre + (x - centre) / (1 + .009 * approx_drop);
    const Brink brink = brink_at(x0);
    const double depth = std::max(0.0, brink.level - brink.rock);
    const double drop = brink.level - y;
    const double mu = dot(direction, c.sun);
    p.x0 = static_cast<float>(x0);
    p.column = static_cast<std::uint32_t>(std::clamp(static_cast<int>(std::lround((x0 - noise.x_from) / noise.dx)), 0, noise.columns - 1));
    p.rock[0] = static_cast<float>(cap.rock.r);
    p.rock[1] = static_cast<float>(cap.rock.g);
    p.rock[2] = static_cast<float>(cap.rock.b);
    // What lies in front: the haze added, the air, then the jungle.
    const double keep = cap.through * (1 - cap.jungle);
    const Rgb added = (cap.haze * cap.through + cap.fog) * (1 - cap.jungle) + cap.jungle_colour * cap.jungle;
    p.a = static_cast<float>(keep);
    p.b[0] = static_cast<float>(added.r);
    p.b[1] = static_cast<float>(added.g);
    p.b[2] = static_cast<float>(added.b);
    if (drop < 0) {
        // The brink's thin bright rim.
        p.rim = true;
        const double glow = phase_hg(mu, .9) * 4 * pi;
        const Rgb rim = c.sunlight * water_tint(c, .15) * glow;
        p.glass0[0] = static_cast<float>(rim.r);
        p.glass0[1] = static_cast<float>(rim.g);
        p.glass0[2] = static_cast<float>(rim.b);
        p.gaps = static_cast<float>(smoothstep(-.07, -.07 * .3, drop) * smoothstep(0, .08, depth));
        return p;
    }
    p.tau = static_cast<float>(flight_time(drop));
    const double breakup = 4 + 22 * depth;
    p.coherent = static_cast<float>(std::exp(-drop / breakup));
    p.aerated = static_cast<float>(1 - std::exp(-std::max(0.0, drop - .5) / (2.2 + 8 * depth)));
    p.gaps = static_cast<float>(smoothstep(.1, .9, drop / breakup));
    p.onset = static_cast<float>(smoothstep(.3, 3.5 + 6 * depth, drop));
    p.veil = static_cast<float>(smoothstep(3, 12, drop));
    const Channel& own = channels[std::max(0, brink.channel)];
    p.end = static_cast<float>(std::min(x - own.left, own.right - x) + .035 * drop);
    p.low = static_cast<float>(smoothstep(-1, 5, y));
    p.depth_edge = static_cast<float>(depth);
    const double glass_glow = phase_hg(mu, .9) * 4 * pi;
    const double brink_line = std::exp(-drop / .25);
    const Rgb through_glass = c.sunlight * water_tint(c, depth * (.8 - .5 * brink_line)) * glass_glow;
    const Rgb glass0 = cap.rock * water_tint(c, depth * .5) * .3 + through_glass * (.0025 + .045 * brink_line) + c.fill_up * .008;
    const double sunlit = 1 - .55 * smoothstep(8, 26, drop + 2);
    const double froth_glow = phase_hg(mu, .6) * 4 * pi;
    const Rgb froth0 = c.fill_front * .75 + c.fill_up * .15;
    const Rgb froth1 = c.sunlight * (.02 * froth_glow * sunlit);
    const Rgb parts[4] = {glass0, through_glass, froth0, froth1};
    float* targets[4] = {p.glass0, p.glass1, p.froth0, p.froth1};
    for (int k = 0; k < 4; ++k) {
        targets[k][0] = static_cast<float>(parts[k].r);
        targets[k][1] = static_cast<float>(parts[k].g);
        targets[k][2] = static_cast<float>(parts[k].b);
    }
    return p;
}

float smooth_f(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

// The sheet at one pixel now: its colour over the rock, as the eye sees it.
void shade_sheet(const SheetPixel& p, const SheetNoise& noise, double time, float out[3]) {
    if (p.rim) {
        const double ripple = noise2(p.x0 * 5.5, time * 1.7, 87) + .5 * noise2(p.x0 * 17, time * 3.1, 88);
        const double sparkle = std::pow(std::clamp(.5 + .6 * ripple, 0.0, 1.0), 6);
        const float glow = static_cast<float>(.06 + .5 * sparkle);
        const float alpha = p.gaps;
        for (int k = 0; k < 3; ++k)
            out[k] = p.a * (p.rock[k] + (p.glass0[k] * glow - p.rock[k]) * alpha) + p.b[k];
        return;
    }
    // Where this water is in the loop of its own time.
    double s = (p.tau - time) / loop_seconds;
    s -= std::floor(s);
    const double row_f = s * loop_rows;
    const int row = static_cast<int>(row_f);
    const float w = static_cast<float>(row_f - row);
    const int next = (row + 1) % loop_rows;
    const float* r0 = &noise.values[(static_cast<std::size_t>(p.column) * loop_rows + static_cast<std::size_t>(row)) * noise_channels];
    const float* r1 = &noise.values[(static_cast<std::size_t>(p.column) * loop_rows + static_cast<std::size_t>(next)) * noise_channels];
    float n[noise_channels];
    for (int k = 0; k < noise_channels; ++k)
        n[k] = r0[k] + (r1[k] - r0[k]) * w;
    const float streak = n[0], fine = n[1], clump = n[2], pulse = n[3], fray = n[4];
    const float rope = n[5], swell = n[6], texture = n[7], fingers = n[8];
    const float white = std::clamp(p.aerated * (.45f + .7f * streak + .25f * fine + .7f * pulse), 0.0f, 1.0f);
    const float hold = .35f + .5f * p.coherent + .45f * clump + .2f * streak + .5f * pulse;
    float alpha = std::clamp(1 + (smooth_f(.05f, .45f, hold) - 1) * p.gaps, 0.0f, 1.0f);
    alpha *= smooth_f(0, .08f, p.depth_edge + .04f * fine);
    alpha *= smooth_f(-.5f, 2.5f, p.end + 1.6f * fray + .8f * streak);
    alpha *= p.low;
    const float whiten = std::clamp(smooth_f(.85f - p.onset, 1.05f - p.onset * .8f, fingers * .6f + white * .6f), 0.0f, 1.0f);
    const float white_now = std::max(whiten, white * smooth_f(.4f, 1, p.onset)) * (1 - .25f * p.veil);
    alpha *= 1 - .15f * p.veil * (1 - clump);
    const float shine = .004f * swell + .05f * rope;
    const float froth_a = .45f + .55f * texture;
    const float froth_b = texture * texture;
    for (int k = 0; k < 3; ++k) {
        const float glass = p.glass0[k] + p.glass1[k] * shine;
        const float froth = p.froth0[k] * froth_a + p.froth1[k] * froth_b;
        const float sheet = glass + (froth - glass) * white_now;
        out[k] = p.a * (p.rock[k] + (sheet - p.rock[k]) * alpha) + p.b[k];
    }
}

// A height field for the pool's ripples that tiles: a few travelling waves with whole
// numbers of cycles across its 8 m square, so two layers can drift over each other.
double ripple_height(double x, double z, double t) {
    const double k = 2 * pi / 8;
    double h = 0;
    const int waves[6][2] = {{3, 1}, {-2, 3}, {5, -2}, {1, -6}, {-7, 2}, {4, 5}};
    const double speeds[6] = {.9, -.7, 1.3, .6, -1.1, .8};
    for (int i = 0; i < 6; ++i) {
        const double phase = k * (waves[i][0] * x + waves[i][1] * z) - speeds[i] * t * 2;
        h += std::sin(phase) / (1 + .25 * i);
    }
    return h * .0045;
}

} // namespace

struct LiveFalls::State {
    Look look{};
    Camera camera{};
    Context base{};
    SheetNoise noise{};
    bool noise_made = false;
    std::unique_ptr<Baked> shown;
    std::unique_ptr<Baked> pending;
    FallsParticles particles{};
    bool warmed = false;
    double density = 1;          // what the water was planted at
    double wanted_density = 1;   // and what it should be: applied on the window's thread
    double time = 0;
    std::vector<particles::Shade> shades;
    particles::Layer layer{};
    std::vector<float> picture;
    std::array<std::uint8_t, 4096> curve{};   // the tone curve, by the square root of light
    std::array<double, 6> spent{};             // seconds in each stage of drawing, summed
    long frames = 0;
};

namespace {

// The tone curve as a table: exposure, the fitted ACES curve, sRGB, by sqrt(light / 16).
void make_curve(std::array<std::uint8_t, 4096>& curve, double exposure) {
    for (std::size_t i = 0; i < curve.size(); ++i) {
        const double root = static_cast<double>(i) / (curve.size() - 1);
        const double x = root * root * 16 * exposure;
        const double mapped = std::clamp(x * (2.51 * x + .03) / (x * (2.43 * x + .59) + .14), 0.0, 1.0);
        const double encoded = mapped <= .0031308 ? mapped * 12.92 : 1.055 * std::pow(mapped, 1 / 2.4) - .055;
        curve[i] = static_cast<std::uint8_t>(std::lround(std::clamp(encoded, 0.0, 1.0) * 255));
    }
}

std::uint8_t tone(const std::array<std::uint8_t, 4096>& curve, float linear) {
    const float v = std::max(0.0f, linear) * (1.0f / 16);
    const float root = std::sqrt(std::min(1.0f, v));
    return curve[static_cast<std::size_t>(root * 4095 + .5f)];
}

void scale_rates(FallsParticles& f, double density) {
    for (int e = 0; e < f.system.emitter_count(); ++e) {
        if (e >= f.clumps_from && e < f.clumps_to)
            continue;  // the clumps follow the surges, scaled where they are set
        f.system.set_rate(e, static_cast<float>(f.base_rates[static_cast<std::size_t>(e)] * density));
    }
}

void step_falls(FallsParticles& f, double time, double seconds, double density) {
    for (int e = f.clumps_from; e < f.clumps_to; ++e) {
        const std::size_t k = static_cast<std::size_t>(e - f.clumps_from);
        f.system.set_rate(e, static_cast<float>(f.clump_rate[k] * surge(f.clump_x[k], time) * density));
    }
    f.system.advance(seconds);
}

} // namespace

LiveFalls::LiveFalls(const Look& look, const Camera& camera) : state_(std::make_unique<State>()) {
    State& s = *state_;
    s.look = look;
    s.camera = camera;
    make_curve(s.curve, look.exposure);
}

LiveFalls::~LiveFalls() = default;

void LiveFalls::begin_size(int width, int height, int samples) {
    State& s = *state_;
    s.pending = std::make_unique<Baked>();
    Baked& b = *s.pending;
    b.width = std::max(8, width);
    b.height = std::max(8, height);
    b.samples = std::clamp(samples, 1, 3);
    const std::size_t pixels = static_cast<std::size_t>(b.width) * static_cast<std::size_t>(b.height);
    b.behind.assign(pixels * 3, 0.0f);
    b.over.assign(pixels * 3, 0.0f);
    b.cover.assign(pixels, 0.0f);
    b.depth.assign(pixels, 1e9f);
    b.boxes = walkway_boxes();
    const std::size_t bands = static_cast<std::size_t>((b.height + b.band_rows - 1) / b.band_rows);
    b.band_sheet.assign(bands, std::vector<SheetPixel>{});
    b.band_glitter.assign(bands, std::vector<GlitterPixel>{});
}

int LiveFalls::bands() const {
    const State& s = *state_;
    if (!s.pending)
        return 0;
    return ((*s.pending).height + (*s.pending).band_rows - 1) / (*s.pending).band_rows;
}

void LiveFalls::prepare_once() {
    State& s = *state_;
    // The context every size shares: the sky, the light, the planting.
    prepare(s.base, s.look, s.camera, 1100, 760, 0);
    if (!s.noise_made) {
        make_sheet_noise(s.noise);
        s.noise_made = true;
    }
    if (!s.warmed) {
        plant_falls(s.particles, s.look.seed);
        s.particles.base_rates.clear();
        for (int e = 0; e < s.particles.system.emitter_count(); ++e)
            s.particles.base_rates.push_back(s.particles.system.emitter(e).rate);
        scale_rates(s.particles, s.density);
        // Run the water until it has reached the pool and the spray has risen.
        for (int n = 0; n < 300; ++n)
            step_falls(s.particles, n / 30.0 - 10, 1.0 / 30, s.density);
        s.warmed = true;
    }
}

namespace {
// One ray's worth of a pixel for the bake: what lies behind, what is in front, how
// much it covers.
void bake_sample(const Context& c, const std::vector<Box>& boxes, Vec3 d, Rgb& behind, Rgb& front, double& cover,
                 SheetCapture& capture, double& depth) {
    Sample sample = trace(c, boxes, d, true, capture);
    const double angle = std::acos(std::clamp(dot(d, c.sun), -1.0, 1.0));
    if (sample.far_open > 0 && angle < Sky::disc_radius())
        sample.behind = sample.behind + c.sky.disc(angle) * sample.far_open;
    behind = sample.behind;
    front = sample.front;
    cover = sample.cover;
    depth = sample.depth;
}
} // namespace

void LiveFalls::bake_band(int band) {
    State& s = *state_;
    Baked& b = *s.pending;
    Context c = s.base;
    c.width = b.width;
    c.height = b.height;
    const int first = band * b.band_rows;
    const int last = std::min(b.height, first + b.band_rows);
    const Vec3 sun = c.sun;
    std::vector<SheetPixel> sheet;
    std::vector<GlitterPixel> glitter;
    // First a ray through each pixel's centre.
    for (int row = first; row < last; ++row)
        for (int column = 0; column < b.width; ++column) {
            const std::size_t i = static_cast<std::size_t>(row) * static_cast<std::size_t>(b.width) + static_cast<std::size_t>(column);
            const Vec3 d = view_direction(c, column + .5, row + .5);
            Rgb behind{}, front{};
            double cover = 0, depth = 1e9;
            SheetCapture centre;
            bake_sample(c, b.boxes, d, behind, front, cover, centre, depth);
            b.depth[i] = static_cast<float>(depth);
            b.behind[i * 3] = static_cast<float>(behind.r);
            b.behind[i * 3 + 1] = static_cast<float>(behind.g);
            b.behind[i * 3 + 2] = static_cast<float>(behind.b);
            b.over[i * 3] = static_cast<float>(front.r);
            b.over[i * 3 + 1] = static_cast<float>(front.g);
            b.over[i * 3 + 2] = static_cast<float>(front.b);
            b.cover[i] = static_cast<float>(cover);
            if (cover > .995)
                continue;
            if (centre.hit) {
                SheetPixel p = sheet_pixel(c, d, centre, s.noise);
                p.index = static_cast<std::uint32_t>(i);
                sheet.push_back(p);
            } else if (centre.pool) {
                const Vec3 half = normalized(sun - d);
                if (half.y > .96) {
                    GlitterPixel g;
                    g.index = static_cast<std::uint32_t>(i);
                    g.x = static_cast<float>(centre.pool_point.x);
                    g.z = static_cast<float>(centre.pool_point.z);
                    g.half[0] = static_cast<float>(half.x);
                    g.half[1] = static_cast<float>(half.y);
                    g.half[2] = static_cast<float>(half.z);
                    g.reach = static_cast<float>(centre.pool_reach * (1 - centre.jungle));
                    glitter.push_back(g);
                }
            }
        }
    // Then, where a pixel differs from its neighbours (an edge: a leaf, the rail, the
    // brink), four more rays across it, and their average.
    if (b.samples > 1) {
        std::vector<float> seen(static_cast<std::size_t>(last - first) * static_cast<std::size_t>(b.width));
        for (int row = first; row < last; ++row)
            for (int column = 0; column < b.width; ++column) {
                const std::size_t i = static_cast<std::size_t>(row) * static_cast<std::size_t>(b.width) + static_cast<std::size_t>(column);
                const float keep = 1 - b.cover[i];
                seen[static_cast<std::size_t>(row - first) * static_cast<std::size_t>(b.width) + static_cast<std::size_t>(column)] =
                    std::sqrt(std::max(0.0f, (b.behind[i * 3 + 1] * keep + b.over[i * 3 + 1]) * .7f +
                                                 (b.behind[i * 3] * keep + b.over[i * 3]) * .2f +
                                                 (b.behind[i * 3 + 2] * keep + b.over[i * 3 + 2]) * .1f));
            }
        for (int row = first; row < last; ++row)
            for (int column = 0; column < b.width; ++column) {
                const std::size_t local = static_cast<std::size_t>(row - first) * static_cast<std::size_t>(b.width) + static_cast<std::size_t>(column);
                const float here = seen[local];
                float most = 0;
                if (column > 0)
                    most = std::max(most, std::fabs(seen[local - 1] - here));
                if (column + 1 < b.width)
                    most = std::max(most, std::fabs(seen[local + 1] - here));
                if (row > first)
                    most = std::max(most, std::fabs(seen[local - static_cast<std::size_t>(b.width)] - here));
                if (row + 1 < last)
                    most = std::max(most, std::fabs(seen[local + static_cast<std::size_t>(b.width)] - here));
                if (most < .025f)
                    continue;
                const std::size_t i = static_cast<std::size_t>(row) * static_cast<std::size_t>(b.width) + static_cast<std::size_t>(column);
                Rgb behind_sum{}, front_sum{};
                double cover_sum = 0;
                for (int sy = 0; sy < 2; ++sy)
                    for (int sx = 0; sx < 2; ++sx) {
                        const Vec3 d = view_direction(c, column + .25 + .5 * sx, row + .25 + .5 * sy);
                        Rgb behind{}, front{};
                        double cover = 0, depth = 0;
                        SheetCapture unused;
                        bake_sample(c, b.boxes, d, behind, front, cover, unused, depth);
                        behind_sum = behind_sum + behind * .25;
                        front_sum = front_sum + front * .25;
                        cover_sum += cover * .25;
                    }
                b.behind[i * 3] = static_cast<float>(behind_sum.r);
                b.behind[i * 3 + 1] = static_cast<float>(behind_sum.g);
                b.behind[i * 3 + 2] = static_cast<float>(behind_sum.b);
                b.over[i * 3] = static_cast<float>(front_sum.r);
                b.over[i * 3 + 1] = static_cast<float>(front_sum.g);
                b.over[i * 3 + 2] = static_cast<float>(front_sum.b);
                b.cover[i] = static_cast<float>(cover_sum);
            }
    }
    b.band_sheet[static_cast<std::size_t>(band)] = std::move(sheet);
    b.band_glitter[static_cast<std::size_t>(band)] = std::move(glitter);
}

void LiveFalls::finish_size() {
    State& s = *state_;
    Baked& b = *s.pending;
    b.context = s.base;
    b.context.width = b.width;
    b.context.height = b.height;
    // The bands' sheet and glitter pixels, in order.
    for (const std::vector<SheetPixel>& part : b.band_sheet)
        b.sheet.insert(b.sheet.end(), part.begin(), part.end());
    for (const std::vector<GlitterPixel>& part : b.band_glitter)
        b.glitter.insert(b.glitter.end(), part.begin(), part.end());
    b.band_sheet.clear();
    b.band_glitter.clear();
    // The sun's glare in the eye, over everything, by how much of its disc shows.
    const Context& c = b.context;
    SheetCapture unused;
    const Sample toward = trace(c, b.boxes, c.sun, true, unused);
    const double visible = toward.far_open * (1 - toward.cover);
    if (visible > 0)
        for (int row = 0; row < b.height; ++row)
            for (int column = 0; column < b.width; ++column) {
                const std::size_t i = static_cast<std::size_t>(row) * static_cast<std::size_t>(b.width) + static_cast<std::size_t>(column);
                const Vec3 d = view_direction(c, column + .5, row + .5);
                const Rgb g = c.sky.glare(std::acos(std::clamp(dot(d, c.sun), -1.0, 1.0))) * visible;
                b.over[i * 3] += static_cast<float>(g.r);
                b.over[i * 3 + 1] += static_cast<float>(g.g);
                b.over[i * 3 + 2] += static_cast<float>(g.b);
            }
}

void LiveFalls::adopt() {
    State& s = *state_;
    if (s.pending)
        s.shown = std::move(s.pending);
}

bool LiveFalls::ready() const {
    return (*state_).shown != nullptr;
}

void LiveFalls::size(int& width, int& height) const {
    const State& s = *state_;
    width = s.shown ? (*s.shown).width : 0;
    height = s.shown ? (*s.shown).height : 0;
}

void LiveFalls::set_density(double density) {
    State& s = *state_;
    s.wanted_density = std::clamp(density, .2, 2.0);
    // Before the water is first planted, it is planted at this density.
    if (!s.shown && !s.warmed)
        s.density = s.wanted_density;
}

void LiveFalls::advance(double seconds) {
    State& s = *state_;
    // Nothing moves until the first size has been adopted: until then the water may
    // still be being planted on another thread.
    if (!s.shown || seconds <= 0)
        return;
    if (s.wanted_density != s.density) {
        s.density = s.wanted_density;
        scale_rates(s.particles, s.density);
    }
    seconds = std::min(seconds, .25);
    step_falls(s.particles, s.time, seconds, s.density);
    s.time += seconds;
}

std::size_t LiveFalls::particle_count() const {
    return (*state_).particles.system.count();
}

double LiveFalls::time() const {
    return (*state_).time;
}

// Seconds since `start`, added to `sum`, and the clock restarted.
void lap(std::chrono::steady_clock::time_point& start, double& sum) {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    sum += std::chrono::duration<double>(now - start).count();
    start = now;
}

void LiveFalls::stage_costs(double out[6]) const {
    const State& s = *state_;
    for (std::size_t k = 0; k < 6; ++k)
        out[k] = s.frames > 0 ? s.spent[k] / static_cast<double>(s.frames) : 0.0;
}

void LiveFalls::draw(std::vector<std::uint32_t>& out) {
    State& s = *state_;
    if (!s.shown)
        return;
    ++s.frames;
    std::chrono::steady_clock::time_point clock = std::chrono::steady_clock::now();
    const Baked& b = *s.shown;
    const std::size_t pixels = static_cast<std::size_t>(b.width) * static_cast<std::size_t>(b.height);
    s.picture.assign(b.behind.begin(), b.behind.end());
    lap(clock, s.spent[0]);
    // The falling sheet, now.
    for (const SheetPixel& p : b.sheet) {
        float colour[3];
        shade_sheet(p, s.noise, s.time, colour);
        float* at = &s.picture[static_cast<std::size_t>(p.index) * 3];
        at[0] = colour[0];
        at[1] = colour[1];
        at[2] = colour[2];
    }
    lap(clock, s.spent[1]);
    // The sun glittering on the pool's ripples.
    const Context& c = b.context;
    const float sun_r = static_cast<float>(c.sunlight.r), sun_g = static_cast<float>(c.sunlight.g), sun_b = static_cast<float>(c.sunlight.b);
    for (const GlitterPixel& g : b.glitter) {
        const double e = .03;
        const double h = ripple_height(g.x, g.z, s.time);
        const double hx = (ripple_height(g.x + e, g.z, s.time) - h) / e;
        const double hz = (ripple_height(g.x, g.z + e, s.time) - h) / e;
        const double fine = .006 * noise2(g.x / .5 + s.time * .3, g.z / .5, 302);
        const Vec3 n = normalized({-hx - fine, 1, -hz + fine});
        const double facing = std::max(0.0, n.x * g.half[0] + n.y * g.half[1] + n.z * g.half[2]);
        const double f60 = std::pow(facing, 60);
        const double glitter = std::pow(f60, 15) * 60 + f60 * .5;
        const float add = static_cast<float>(glitter * g.reach);
        float* at = &s.picture[static_cast<std::size_t>(g.index) * 3];
        at[0] += sun_r * add;
        at[1] += sun_g * add;
        at[2] += sun_b * add;
    }
    lap(clock, s.spent[2]);
    // The particles, between what lies behind and what is in front.
    const particles::Lens lens = lens_for(s.camera, b.width, b.height);
    shade_particles(c, s.particles, lens, s.shades);
    s.layer.reset(b.width, b.height);
    lap(clock, s.spent[3]);
    particles::Splat splat;
    splat.softness = .85f;
    splat.taper = .3f;
    splat.fray = .6f;
    splat.fray_length = .4f;
    particles::Splatter::draw(s.particles.system, s.shades, lens, b.depth, splat, s.layer);
    particles::Splatter::compose(s.layer, s.picture);
    lap(clock, s.spent[4]);
    // What is in front, then the tone curve.
    out.resize(pixels);
    for (std::size_t i = 0; i < pixels; ++i) {
        const float keep = 1 - b.cover[i];
        const float r = s.picture[i * 3] * keep + b.over[i * 3];
        const float g = s.picture[i * 3 + 1] * keep + b.over[i * 3 + 1];
        const float bl = s.picture[i * 3 + 2] * keep + b.over[i * 3 + 2];
        out[i] = 0xFF000000U | (static_cast<std::uint32_t>(tone(s.curve, r)) << 16) |
                 (static_cast<std::uint32_t>(tone(s.curve, g)) << 8) | tone(s.curve, bl);
    }
    lap(clock, s.spent[5]);
}

} // namespace cf
