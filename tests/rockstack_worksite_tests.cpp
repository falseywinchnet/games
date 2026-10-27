#include "run.hpp"
#include "terrain.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
int failures = 0;
void check(bool okay, const char* message) {
    std::printf("%s %s\n", okay ? "PASS" : "FAIL", message);
    if (!okay) failures += 1;
}
zc::phys::Shape box() {
    zc::phys::HullDesc hull;
    for (int x = -1; x <= 1; x += 2) {
        for (int y = -1; y <= 1; y += 2) {
            for (int z = -1; z <= 1; z += 2) hull.points.push_back(zc::phys::Vec3{x * .025, y * .025, z * .025});
        }
    }
    zc::phys::ShapeDesc desc;
    desc.hulls.push_back(hull);
    desc.friction = .9;
    return zc::phys::cook(desc);
}
void step(zc::phys::World& world, int count) {
    for (int k = 0; k < count; k += 1) world.step();
}
void ground_and_sling() {
    const std::shared_ptr<const zc::phys::GroundSurface> ground = zc::worksite_ground();
    check((*ground).sample(.36, .14).height == 0, "pad stays at its rendered height");
    // A fine-grid vertex outside the ledge, and a sloping foreground vertex.
    const double y = -1.1 + 12 * (2.16 / 44);
    check(std::abs((*ground).sample(.9, y).height - zc::terrain_height(zc::Terrain::bank, .9, y)) < 1e-10,
          "bank collision uses the rendered terrain vertices");
    check((*ground).sample(.8, -.95).normal.z < .9999, "bank contacts carry the slope normal");
    zc::phys::WorldParams params;
    params.ground_surface = ground;
    zc::phys::World world(params);
    const zc::phys::Shape shape = box();
    zc::phys::Pose target;
    target.p = zc::phys::Vec3{.86, -.28, .22};
    const zc::phys::BodyId id = world.add_body(shape, target);
    zc::phys::HoldParams hold;
    hold.sling_length = .12;
    world.hold(id, target, hold);
    step(world, 60);
    for (int f = 0; f < 900 && world.hold_state().tension > .03; f += 1) {
        target.p.z -= .0005;
        world.set_hold_target(target);
        world.step();
    }
    step(world, 90);
    const zc::phys::Vec3 start = world.state(id).pose.p;
    check(world.hold_state().tension < .05, "lowering outside the pad rests the load and slackens its sling");
    check(start.z < .025 && start.z > -.01, "off-pad rock rests on the bank, below the old invisible plane");
    double worst_penetration = 0, max_side_force = 0;
    int contact_frames = 0;
    for (int f = 0; f < 360; f += 1) {
        target.p.x += .00065;
        world.set_hold_target(target);
        world.step();
        const zc::phys::Pose pose = world.state(id).pose;
        for (const zc::phys::Vec3& vertex : shape.hulls[0].vertices) {
            const zc::phys::Vec3 at = pose.p + zc::phys::rotate(pose.q, vertex);
            worst_penetration = std::max(worst_penetration, (*ground).sample(at.x, at.y).height - at.z);
        }
        bool touching = false;
        for (const zc::phys::Contact& contact : world.contacts()) {
            if (contact.a == id && contact.b == zc::phys::kGround && contact.normal_impulse > 1e-6) touching = true;
        }
        if (touching) contact_frames += 1;
        max_side_force = std::max(max_side_force, world.hold_state().lateral_force);
    }
    const double travel = world.state(id).pose.p.x - start.x;
    std::printf("drag %.1f mm, contact %d/360 frames, penetration %.3f mm, lateral %.4f mg\n", travel * 1000, contact_frames, worst_penetration * 1000, max_side_force);
    check(travel > .10 && contact_frames > 240, "sideways sling motion drags a grounded load through contact and friction");
    check(worst_penetration < .003 && max_side_force <= .351, "ground drag keeps collision and the gentle lateral-force cap");
    for (int f = 0; f < 240; f += 1) {
        target.p.z += .001;
        world.set_hold_target(target);
        world.step();
    }
    check(world.state(id).pose.p.z > .15, "the same attached rock lifts cleanly after dragging");
    const double hit = (*ground).raycast(zc::phys::Vec3{.86, -.28, 1}, zc::phys::Vec3{0, 0, -1}, 5);
    check(std::abs(1 - hit - (*ground).sample(.86, -.28).height) < 1e-10, "picking and contact agree on the ground height");
}
void slew() {
    const zc::SiteLayout& layout = zc::site_layout();
    const zc::phys::Vec3 pivot = zc::crane_pivot();
    bool inside = true;
    for (int i = 0; i < 720; i += 1) {
        const double angle = i * 6.283185307179586 / 720;
        const zc::phys::Vec3 p = zc::crane_reachable(pivot + zc::phys::Vec3{2 * std::cos(angle), 2 * std::sin(angle), .4});
        inside = inside && std::abs(zc::crane_angle(p)) <= layout.crane_slew_limit + 1e-12;
        const zc::phys::Vec3 bowl = layout.bowl_centre + zc::phys::Vec3{layout.bowl_rim_radius * std::cos(angle), layout.bowl_rim_radius * std::sin(angle), 0};
        inside = inside && zc::phys::length(zc::crane_reachable(bowl) - bowl) < 1e-10;
    }
    check(inside, "slew stops constrain the hook while preserving reach over the whole bowl");
    // Counterweight extrema in chassis coordinates, including the stripe panel.
    double front = -10;
    for (int i = -1050; i <= 1050; i += 1) {
        const double angle = i * 3.141592653589793 / 1800;
        for (int side = -1; side <= 1; side += 2) front = std::max(front, -.06 - .196 * std::cos(angle) - side * .073 * std::sin(angle));
    }
    check(front < .082, "counterweight clears the cab and its air cleaner throughout the safe arc");
    zc::Run run;
    run.begin(7, "Slew test");
    int rock = -1;
    double high = -1;
    for (std::size_t i = 0; i < run.rocks.size(); i += 1) {
        if (run.can_fetch(static_cast<int>(i)) && run.rock_pose(static_cast<int>(i)).p.z > high) {
            rock = static_cast<int>(i); high = run.rock_pose(rock).p.z;
        }
    }
    bool path_safe = rock >= 0 && run.fetch(rock);
    const zc::CraneInput idle;
    for (int f = 0; f < 1500 && run.crane.mode != zc::CraneMode::steering; f += 1) {
        run.step(idle, 0, .6);
        path_safe = path_safe && std::abs(zc::crane_angle(run.crane.hook)) <= layout.crane_slew_limit + 1e-9;
    }
    check(path_safe && run.crane.mode == zc::CraneMode::steering, "automatic fetching stays within the safe arc");
    if (run.crane.mode != zc::CraneMode::steering) return;
    zc::CraneInput swing;
    swing.right = -1;
    run.crane.target.p = pivot + zc::phys::Vec3{.4 * std::cos(layout.crane_heading + layout.crane_slew_limit - .03), .4 * std::sin(layout.crane_heading + layout.crane_slew_limit - .03), .7};
    for (int f = 0; f < 300; f += 1) run.step(swing, 0, .6);
    check(std::abs(zc::crane_angle(run.crane.target.p) - layout.crane_slew_limit) < 1e-6, "holding swing at the stop cannot wrap the turret into the cab");
    run.throw_back();
    bool returned = false;
    for (int f = 0; f < 1800; f += 1) {
        run.step(idle, 0, .6);
        path_safe = path_safe && std::abs(zc::crane_angle(run.crane.hook)) <= layout.crane_slew_limit + 1e-9;
        if (run.crane.mode == zc::CraneMode::parked) { returned = true; break; }
    }
    check(path_safe && returned, "automatic return from a stop stays inside the permitted sector");
}
}
int main() {
    ground_and_sling();
    slew();
    return failures == 0 ? 0 : 1;
}
