#pragma once
// One worksite: its rocks, the physics world, the crane, and the rules.
//
// Every rock is a physics body from the moment it's poured into the bowl.
// The crane fetches a rock (from the bowl, or the top of the stack), lifts it
// on the physics hold, lets the player steer it, and lets go on Space. When
// the site falls quiet the rocks are sorted: the stack is the first rock
// placed (the base) and every rock in vertical, unmoving contact with a rock
// of the stack below it; rocks in the bowl stay there; any others (fallen,
// knocked off) are tidied back into the bowl. The score is the stack's height.
//
// Portable: no UI. Coordinates are metres, z up; the sand is z = 0.
#include "physics.hpp"
#include "rocks.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace zc {

// The worksite's layout, seen from the default camera (looking north, along
// +y): the bowl on the left, the crane perched on a boulder left of centre in
// the foreground, the stack's bedrock ledge on the right, the brook behind.
struct SiteLayout {
    phys::Vec3 stack_centre{0.36, 0.14, 0.0};
    phys::Vec3 bowl_centre{-0.58, 0.22, 0.0};
    double bowl_floor_radius = 0.30;       // inside, at the floor
    double bowl_rim_radius = 0.44;         // inside, at the rim
    double bowl_height = 0.15;             // rim height above the ground
    double bowl_wall = 0.025;              // wall thickness
    double bowl_floor = 0.02;              // floor thickness
    phys::Vec3 crane_base{-0.12, -0.36, 0.0};
    double crane_boulder_top = 0.075;      // the boulder the crane stands on
    double crane_pivot_height = 0.235;     // the boom's pivot above the ground
    double crane_reach = 1.30;             // horizontal reach of the hook from the base
    double crane_min_reach = 0.18;
    double crane_heading = 1.05;          // face the bowl and pad within the safe slew arc
    double crane_slew_limit = 1.8325957145940461; // 105 degrees either side; cab clearance
    phys::Vec3 hook_rest{0.02, -0.18, 0.46};
    // the brook: the water's near edge, far edge and surface
    double brook_near = 0.80, brook_far = 1.48, water_level = -0.06;
};

const SiteLayout& site_layout();
phys::Vec3 crane_pivot();
double crane_angle(phys::Vec3 point);     // relative to the chassis, in [-pi, pi]
phys::Vec3 crane_reachable(phys::Vec3 point);

enum class Place { bowl, stack, loose, held, flying };

struct RockState {
    Rock rock;
    phys::BodyId body = -1;                // -1 while flying back to the bowl
    Place place = Place::bowl;
    // flying back to the bowl after a collapse: a kinematic arc
    phys::Pose fly_from, fly_to;
    double fly_t = 0;                      // 0..1
};

enum class CraneMode { parked, fetching, attaching, lifting, steering, releasing, returning, carrying_back };

// The player's steering for one frame. Each axis is -1..1, relative to the
// camera: right and forward on the ground plane, up; yaw about the vertical,
// pitch about the camera's right, roll about its forward.
struct CraneInput {
    double right = 0, forward = 0, up = 0;
    double yaw = 0, pitch = 0, roll = 0;
    bool fine = false;
};

struct Crane {
    CraneMode mode = CraneMode::parked;
    phys::Vec3 hook;                       // the hook block: where the four wires meet
    std::vector<phys::Vec3> path;          // waypoints still to travel
    double path_speed = 0.45;              // m/s along the path
    int rock = -1;                         // the rock it's after or holding (index into rocks)
    bool attached = false;
    double hang = 0.06;                    // hook height above the held rock's top
    phys::Pose target;                     // the hold target (the rock's centre and orientation)
    phys::Vec3 velocity;                   // smoothed steering velocity of the target
    phys::Vec3 angular;                    // smoothed steering rate, rad/s
    double wires = 0;                      // 0 reeled in .. 1 let down to the rock
    double timer = 0;
    bool release_over_bowl = false;        // carrying back: let go when the path ends
};

// What happened this frame, for sound, the operator and the HUD.
struct RunEvents {
    bool grabbed = false, released = false, settled = false;
    bool collapsed = false, new_best = false, tidied = false;
    bool stuck = false;                    // the crane couldn't lift its rock and let go
    bool landed = false;                   // a rock flying back came down in the bowl
    double hardest_impact = 0;             // the largest contact impulse that appeared this frame, N s
    int knocks = 0;                        // contacts that appeared this frame with a real impulse
};

class Run {
public:
    Run();
    Run(const Run&) = delete;
    Run& operator=(const Run&) = delete;

    // A fresh worksite: generates the first fifty rocks and pours them into
    // the bowl (simulated; a second or two).
    void begin(std::uint32_t seed, const std::string& company);
    // the same seed's rocks, all back in the bowl (the best height stays)
    void start_over();

    // One physics frame (1/60 s). `camera_yaw` turns the steering into world directions.
    void step(const CraneInput& input, double camera_yaw, double camera_pitch);

    // commands
    [[nodiscard]] bool can_fetch(int rock) const;
    bool fetch(int rock);                  // the crane goes for this rock
    void release();                        // let go of the held rock where it is
    void throw_back();                     // carry the held rock to the bowl and drop it in
    void cancel();                         // abandon a fetch before the wires attach

    // queries
    int rock_at_ray(phys::Vec3 origin, phys::Vec3 direction) const;   // the rock a ray hits first, or -1
    double height() const { return height_; }
    double best_height() const { return best_; }
    int stack_count() const { return stack_count_; }
    bool quiet() const;
    bool busy() const;                     // the crane is doing something
    const RunEvents& events() const { return events_; }
    phys::Pose rock_pose(int rock) const;  // current pose (or the flying arc's)
    double stack_top() const;              // highest point of the stack, or 0
    std::uint32_t seed() const { return seed_; }
    const std::string& company() const { return company_; }
    void set_company(const std::string& name) { company_ = name; }
    void set_best(double best) { best_ = best; }
    int base_rock() const { return base_; }

    // saving: the last quiet arrangement (never a rock in mid-air)
    std::string save() const;
    [[nodiscard]] bool load(const std::string& text);

    phys::World& world() { return *world_; }
    const phys::World& world() const { return *world_; }
    std::vector<RockState> rocks;
    Crane crane;

private:
    std::unique_ptr<phys::World> world_;
    std::uint32_t seed_ = 1;
    std::string company_;
    int base_ = -1;
    int generated_ = 0;
    double height_ = 0, best_ = 0;
    int stack_count_ = 0;
    int last_stack_count_ = 0;
    bool sorted_since_quiet_ = false;
    double quiet_for_ = 0;
    RunEvents events_;
    RunEvents queued_;                     // raised between steps, handed on with the next
    bool in_step_ = false;
    RunEvents& out() { return in_step_ ? events_ : queued_; }
    std::vector<phys::Pose> quiet_poses_;  // the last quiet arrangement
    std::vector<Place> quiet_places_;
    std::vector<double> last_impulse_;     // per rock: the largest contact impulse last frame, for knocks
    std::vector<char> was_awake_;          // per rock: awake last frame (a sleeper's contacts aren't reported)
    // which rock rests on which (upper, lower), as of the last sort: sleeping
    // rocks keep their edges until something wakes them
    std::vector<std::pair<int, int>> supports_;
    // which rock bears down on which at all (a looser test than resting on):
    // a rock with anything bearing on it is too pinned for the crane
    std::vector<std::pair<int, int>> covers_;
    std::vector<int> wedged_;              // rocks the crane failed to pull free since the last settle
    void update_supports();
    void forget_supports(int rock);

    void new_world();
    void add_bowl(phys::World& world) const;
    void make_rocks(int first, int count);   // appends them, made in parallel
    void pour(int first, int count);
    void settle(int limit);
    double heap_top() const;
    void generate_more();
    phys::BodyId add_rock_body(int index, const phys::Pose& pose, bool asleep);
    void sort_rocks();
    void start_tidy();
    void step_flying(double dt);
    void step_crane(const CraneInput& input, double camera_yaw, double camera_pitch, double dt);
    void follow_path(double dt);
    void plan_path_to(phys::Vec3 goal);
    double travel_height() const;
    void attach();
    void detach();
    void clamp_target();
    void remember_quiet();
    void note_impacts();
    bool in_bowl(const phys::Pose& pose) const;
    bool resting_on(int upper, int lower) const;
};

double rock_top(const Rock& rock, const phys::Pose& pose);
double rock_bottom(const Rock& rock, const phys::Pose& pose);

}  // namespace zc
