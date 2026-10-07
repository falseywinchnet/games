#include "sim.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <utility>

namespace mm {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double grab_radius = 1.1;   // metres from the mower's centre
constexpr double walk_cell = 0.25;
constexpr int walk_cells_x = static_cast<int>(lawn_width / walk_cell);   // the whole lawn
constexpr int walk_cells_y = static_cast<int>(lawn_height / walk_cell);
constexpr double granny_speed = 0.98;       // the mower does 1.15 with a hand on the wheel
constexpr double granny_reach = 0.80;       // how near she must get, metres
constexpr double engine_start_seconds = 2.4;  // the starter, up to speed, blades in
constexpr double poke_radius = 0.38;  // metres from the gnome
constexpr double gnome_reach = 0.6;   // the deck's reach: any pass of it over a frozen gnome breaks him

coverage::FieldSetup field_setup() {
    coverage::FieldSetup field{};
    field.width = lawn_width;
    field.height = lawn_height;
    field.cell = lawn_cell;
    field.pose_cell = 0.15;
    return field;
}

coverage::Machine machine_setup() {
    coverage::Machine machine{};
    machine.clearance = 0.58;
    machine.deck_width = 1.0;
    machine.deck_length = 0.7;
    machine.lane_pitch = 0.9;
    machine.speed = 1.15;
    machine.travel_speed = 1.5;
    machine.turn_rate = 1.9;
    return machine;
}

} // namespace

const char* livery_name(Livery livery) {
    if (livery == Livery::red_t)
        return "Red T";
    if (livery == Livery::green_jd)
        return "Green JD";
    return "Orange H";
}

const char* livery_key(Livery livery) {
    if (livery == Livery::red_t)
        return "t";
    if (livery == Livery::green_jd)
        return "jd";
    return "h";
}

EngineSpec engine_spec(Livery livery) {
    // Orange: a big single; red: a parallel twin; green: a 90-degree V-twin.
    if (livery == Livery::red_t)
        return {3400, 0.05, 0.30};
    if (livery == Livery::green_jd)
        return {3600, 0.04, 0.26};
    return {3300, 0.065, 0.36};
}

std::uint8_t heading_code(double heading) {
    double turns = heading / (2 * pi);
    turns -= std::floor(turns);
    // 1..255 so that 0 can mean "not cut".
    const std::uint8_t result = static_cast<std::uint8_t>(1 + std::min(254.0, std::floor(turns * 255.0)));
    return result;
}

void Dirty::include(double x, double y, double radius) {
    x0 = std::min(x0, x - radius);
    y0 = std::min(y0, y - radius);
    x1 = std::max(x1, x + radius);
    y1 = std::max(y1, y + radius);
}

Mowing::Mowing(std::uint64_t seed, Livery livery)
    : random_(seed * 0x9E3779B97F4A7C15ULL + 7), garden_(make_garden(seed)), livery_(livery),
      mower_(field_setup(), machine_setup(), garden_.obstacles, {garden_.start_x, garden_.start_y, garden_.start_heading}) {
    stripes_.assign(static_cast<std::size_t>(lawn_cells_x) * static_cast<std::size_t>(lawn_cells_y), 0);
    trampled_.assign(stripes_.size(), 0);
    tree_shake_.assign(garden_.trees.size(), 0.0);
    mower_.set_solid(garden_.solid);
    // Where the old lady can stand: a quarter-metre grid, clear of mulch and of everything solid.
    walkable_.assign(static_cast<std::size_t>(walk_cells_x) * static_cast<std::size_t>(walk_cells_y), 0);
    for (int j = 0; j < walk_cells_y; ++j) {
        for (int i = 0; i < walk_cells_x; ++i)
            walkable_[static_cast<std::size_t>(j) * static_cast<std::size_t>(walk_cells_x) + static_cast<std::size_t>(i)] =
                clear_around((i + 0.5) * walk_cell, (j + 0.5) * walk_cell, 0.32) ? 1 : 0;
    }
    // How many cells each standing place is from the nearest thing in her way: she keeps
    // to open grass rather than brushing every bed and trunk.
    clearance_.assign(walkable_.size(), 0);
    std::vector<int> ring{};
    for (int cell = 0; cell < walk_cells_x * walk_cells_y; ++cell) {
        if (walkable_[static_cast<std::size_t>(cell)] == 0)
            ring.push_back(cell);
        else
            clearance_[static_cast<std::size_t>(cell)] = 255;
    }
    for (std::size_t head = 0; head < ring.size(); ++head) {
        const int at = ring[head];
        const int ax = at % walk_cells_x;
        const int ay = at / walk_cells_x;
        const int near[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const int* d : near) {
            const int bx = ax + d[0];
            const int by = ay + d[1];
            if (bx < 0 || by < 0 || bx >= walk_cells_x || by >= walk_cells_y)
                continue;
            const int next = by * walk_cells_x + bx;
            const int value = clearance_[static_cast<std::size_t>(at)] + 1;
            if (clearance_[static_cast<std::size_t>(next)] > value) {
                clearance_[static_cast<std::size_t>(next)] = static_cast<std::uint8_t>(std::min(value, 250));
                ring.push_back(next);
            }
        }
    }
    gnome_.next = random_range(random_, 72.0, 160.0);  // he comes up a quarter as often as he once did
    const EngineSpec spec = engine_spec(livery_);
    voice_.rpm = spec.rated_rpm;
    voice_.rate = 1;
    record_cut(mower_.pose(), mower_.pose(), false);
}

void Mowing::set_livery(Livery livery) {
    livery_ = livery;
    voice_.rpm = engine_spec(livery_).rated_rpm;
}

bool Mowing::complete() const {
    return mower_.finished() && idle_ > 6.0 && gnome_.state != GnomeState::frozen &&
           gnome_.state != GnomeState::shattered;
}

std::vector<Cue> Mowing::take_cues() {
    std::vector<Cue> result{};
    result.swap(cues_);
    return result;
}

Dirty Mowing::take_dirty() {
    const Dirty result = dirty_;
    dirty_ = Dirty{};
    return result;
}

void Mowing::cue(const std::string& name, double gain, double rate, double x) {
    if (cues_.size() < 32)
        cues_.push_back({name, gain, rate, std::clamp(x / lawn_width * 2 - 1, -1.0, 1.0) * 0.7});
}

// There is only one gnome voice: a new line waits until the last has finished, or is dropped.
void Mowing::gnome_say(const char* name, double gain, double rate, double x) {
    if (gnome_voice_ > 0)
        return;
    const std::string clip = name;
    const double length = clip == "mm_gnome_giggle" ? 2.75 : clip == "mm_gnome_nyah" ? 2.25 : clip == "mm_gnome_giggle2" ? 1.5 : clip == "mm_gnome_hoo2" ? 0.75 : 0.6;
    gnome_voice_ = length / rate + 0.1;
    cue(clip, gain, rate, x);
}

// ---------------------------------------------------------------- input

bool Mowing::near_mower(double x, double y) const {
    const coverage::Pose& pose = mower_.pose();
    return std::hypot(x - pose.x, y - pose.y) <= grab_radius;
}

bool Mowing::grab(double x, double y) {
    if (!near_mower(x, y))
        return false;
    held_ = true;
    rescuing_ = false;
    hold_x_ = mower_.pose().x;
    hold_y_ = mower_.pose().y;
    return true;
}

void Mowing::steer(double x, double y) {
    if (!held_)
        return;
    hold_x_ = std::clamp(x, 0.0, lawn_width);
    hold_y_ = std::clamp(y, 0.0, lawn_height);
}

void Mowing::let_go() {
    if (!held_)
        return;
    held_ = false;
    const coverage::Pose& pose = mower_.pose();
    if (!clear_around(pose.x, pose.y, 0.50)) {
        // Left on a bed: it takes itself back to the nearest open grass before it thinks again.
        for (double ring = 0.2; ring < 5.0 && !rescuing_; ring += 0.2) {
            for (int k = 0; k < 16 && !rescuing_; ++k) {
                const double x = pose.x + std::cos(k * pi / 8) * ring;
                const double y = pose.y + std::sin(k * pi / 8) * ring;
                if (x > 1.0 && y > 1.0 && x < lawn_width - 1.0 && y < lawn_height - 1.0 && clear_around(x, y, 0.72)) {
                    rescuing_ = true;
                    rescue_x_ = x;
                    rescue_y_ = y;
                }
            }
        }
        if (rescuing_)
            return;
    }
    mower_.resume();
    if (gnome_.state == GnomeState::frozen && gnome_.sought)
        mower_.set_goal(gnome_.x, gnome_.y);
}

void Mowing::set_engine(bool on) {
    if (on == engine_on_)
        return;
    engine_on_ = on;
    engine_clock_ = 0;
    cue(on ? "mm_mower_start" : "mm_mower_stop", 0.85, 1.0, mower_.pose().x);
}

bool Mowing::poke(double x, double y) {
    const bool visible = gnome_.state == GnomeState::rising || gnome_.state == GnomeState::looking ||
                         gnome_.state == GnomeState::sinking;
    if (!visible || gnome_.height < 0.25 || std::hypot(x - gnome_.x, y - gnome_.y) > poke_radius)
        return false;
    gnome_.state = GnomeState::frozen;
    gnome_.clock = 0;
    cue("mm_freeze", 0.7, 1.0, gnome_.x);
    return true;
}

// ---------------------------------------------------------------- the clock

// The garden advances in fixed 20 ms steps counted in whole microseconds, so the
// same total time plays out identically however it is divided between frames.
void Mowing::advance(double seconds, bool reduced_motion) {
    if (!(seconds > 0))
        return;
    reduced_ = reduced_motion;
    constexpr long long step_us = 20000;
    pending_us_ += std::llround(std::min(seconds, 0.25) * 1e6);
    while (pending_us_ >= step_us) {
        pending_us_ -= step_us;
        step(static_cast<double>(step_us) * 1e-6);
    }
}

void Mowing::step(double dt) {
    time_ += dt;
    engine_clock_ += dt;
    const coverage::Pose before = mower_.pose();
    coverage::Step step{};
    const bool running = engine_on_ && engine_clock_ >= engine_start_seconds;
    if (!running)
        step = coverage::Step{};
    else if (held_)
        step = mower_.drive_toward(hold_x_, hold_y_, dt);
    else if (rescuing_) {
        step = mower_.drive_toward(rescue_x_, rescue_y_, dt);
        if (clear_around(mower_.pose().x, mower_.pose().y, 0.50) || std::hypot(rescue_x_ - mower_.pose().x, rescue_y_ - mower_.pose().y) < 0.1) {
            rescuing_ = false;
            mower_.resume();
        }
    } else
        step = mower_.update(dt);
    const bool driven = running && (held_ || rescuing_);
    wreck_beds(dt, driven);
    if (held_ && step.bumped)
        strike_trees();
    for (double& shake : tree_shake_)
        shake = std::max(0.0, shake - dt / 1.2);
    update_granny(dt);
    update_life(dt);
    record_cut(before, mower_.pose(), step.speed > 0.25);
    if (step.bumped && bump_quiet_ <= 0) {
        cue("mm_bump", 0.5, 1.0, mower_.pose().x);
        bump_quiet_ = 0.8;
    }
    bump_quiet_ -= dt;
    run_over_things();
    update_gnome(dt);
    update_engine(dt, step.newly_cut);
    spray(dt);
    update_particles(dt);
    if (mower_.finished() && !held_)
        idle_ += dt;
    else
        idle_ = 0;
}

// Marks the mowing direction of every newly cut cell between two poses, and the
// region whose look changed. Stripes come from driving: grass cut while the mower
// pivots in place is left without a direction (neither light nor dark).
void Mowing::record_cut(const coverage::Pose& from, const coverage::Pose& to, bool driving) {
    const double reach = 0.65;
    const double x0 = std::min(from.x, to.x) - reach;
    const double x1 = std::max(from.x, to.x) + reach;
    const double y0 = std::min(from.y, to.y) - reach;
    const double y1 = std::max(from.y, to.y) + reach;
    const std::uint8_t code = heading_code(to.heading);
    const std::vector<std::uint8_t>& cut = mower_.cut();
    const int cx0 = std::max(0, static_cast<int>(x0 / lawn_cell));
    const int cx1 = std::min(lawn_cells_x - 1, static_cast<int>(x1 / lawn_cell));
    const int cy0 = std::max(0, static_cast<int>(y0 / lawn_cell));
    const int cy1 = std::min(lawn_cells_y - 1, static_cast<int>(y1 / lawn_cell));
    bool changed = false;
    for (int cy = cy0; cy <= cy1; ++cy) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            const std::size_t index = static_cast<std::size_t>(cy) * static_cast<std::size_t>(lawn_cells_x) + static_cast<std::size_t>(cx);
            if (cut[index] != 0 && stripes_[index] == 0) {
                stripes_[index] = driving ? code : 255;
                changed = true;
            }
        }
    }
    if (changed) {
        dirty_.include(from.x, from.y, reach + 0.15);
        dirty_.include(to.x, to.y, reach + 0.15);
    }
}

void Mowing::run_over_things() {
    for (Dandelion& flower : garden_.dandelions) {
        if (flower.cut || !mower_.cut_at(flower.x, flower.y))
            continue;
        flower.cut = true;
        dirty_.include(flower.x, flower.y, 0.2);
        if (reduced_)
            continue;
        const int count = flower.clock ? 22 : 6;
        for (int k = 0; k < count; ++k) {
            Particle seed{};
            seed.kind = flower.clock ? ParticleKind::seed : ParticleKind::petal;
            seed.x = flower.x;
            seed.y = flower.y;
            seed.z = 0.05;
            const double angle = random_range(random_, 0, 2 * pi);
            const double speed = flower.clock ? random_range(random_, 0.15, 0.6) : random_range(random_, 0.3, 1.2);
            seed.vx = std::cos(angle) * speed;
            seed.vy = std::sin(angle) * speed;
            seed.vz = flower.clock ? random_range(random_, 0.2, 0.6) : random_range(random_, 0.5, 1.2);
            seed.life = flower.clock ? random_range(random_, 2.5, 5.0) : random_range(random_, 0.5, 1.0);
            seed.size = flower.clock ? 0.018 : 0.012;
            seed.spin = random_range(random_, -6, 6);
            seed.tint = flower.clock ? 0xF2F0E6 : 0xF5C518;
            particles_.push_back(seed);
        }
        if (flower.clock)
            cue("mm_puff", 0.5, random_range(random_, 0.9, 1.1), flower.x);
    }
    for (Bloom& bloom : garden_.blooms) {
        if (bloom.cut || !mower_.cut_at(bloom.x, bloom.y))
            continue;
        bloom.cut = true;
        if (reduced_)
            continue;
        const std::uint32_t tints[5] = {0xF4F2E6, 0xF5C518, 0xF5C518, 0xFAFAF4, 0x5B7FE0};
        for (int k = 0; k < 4; ++k) {
            Particle petal{};
            petal.kind = ParticleKind::petal;
            petal.x = bloom.x;
            petal.y = bloom.y;
            petal.z = 0.05;
            const double angle = random_range(random_, 0, 2 * pi);
            const double speed = random_range(random_, 0.3, 1.1);
            petal.vx = std::cos(angle) * speed;
            petal.vy = std::sin(angle) * speed;
            petal.vz = random_range(random_, 0.5, 1.1);
            petal.life = random_range(random_, 0.5, 0.9);
            petal.size = 0.010;
            petal.spin = random_range(random_, -6, 6);
            petal.tint = tints[std::clamp(bloom.kind, 0, 4)];
            particles_.push_back(petal);
        }
    }
    for (Mushroom& mushroom : garden_.mushrooms) {
        if (mushroom.cut || !mower_.cut_at(mushroom.x, mushroom.y))
            continue;
        mushroom.cut = true;
        dirty_.include(mushroom.x, mushroom.y, 0.2);
        cue("mm_mushroom", 0.45, random_range(random_, 0.85, 1.2), mushroom.x);
        if (reduced_)
            continue;
        for (int k = 0; k < 7; ++k) {
            Particle chunk{};
            chunk.kind = ParticleKind::mushroom;
            chunk.x = mushroom.x;
            chunk.y = mushroom.y;
            chunk.z = 0.04;
            const double angle = random_range(random_, 0, 2 * pi);
            chunk.vx = std::cos(angle) * random_range(random_, 0.4, 1.4);
            chunk.vy = std::sin(angle) * random_range(random_, 0.4, 1.4);
            chunk.vz = random_range(random_, 0.6, 1.6);
            chunk.life = random_range(random_, 0.6, 1.1);
            chunk.size = mushroom.size * random_range(random_, 0.3, 0.6);
            chunk.spin = random_range(random_, -9, 9);
            chunk.tint = mushroom.kind == 2 ? 0xC9372C : (mushroom.kind == 1 ? 0x9A7651 : 0xEDE6D6);
            particles_.push_back(chunk);
        }
    }
}

// ---------------------------------------------------------------- the gnome

// ---------------------------------------------------------------- beds, trees and the old lady

bool Mowing::clear_around(double x, double y, double radius) const {
    if (x < radius || y < radius || x > lawn_width - radius || y > lawn_height - radius)
        return false;
    const int x0 = std::max(0, static_cast<int>((x - radius) / lawn_cell));
    const int x1 = std::min(lawn_cells_x - 1, static_cast<int>((x + radius) / lawn_cell));
    const int y0 = std::max(0, static_cast<int>((y - radius) / lawn_cell));
    const int y1 = std::min(lawn_cells_y - 1, static_cast<int>((y + radius) / lawn_cell));
    for (int cy = y0; cy <= y1; ++cy) {
        for (int cx = x0; cx <= x1; ++cx) {
            if (garden_.obstacles[static_cast<std::size_t>(cy) * static_cast<std::size_t>(lawn_cells_x) + static_cast<std::size_t>(cx)] == 0)
                continue;
            const double dx = (cx + 0.5) * lawn_cell - x;
            const double dy = (cy + 0.5) * lawn_cell - y;
            if (dx * dx + dy * dy < radius * radius)
                return false;
        }
    }
    return true;
}

// The deck on a flower bed: the mulch under it is churned, every plant it reaches is
// shredded and thrown out of the chute, and the engine chews instead of cutting.
void Mowing::wreck_beds(double seconds, bool driven) {
    wreck_ = std::max(0.0, wreck_ - seconds / 0.45);
    wreck_cue_ -= seconds;
    if (!driven)
        return;
    const coverage::Pose& pose = mower_.pose();
    const double co = std::cos(pose.heading);
    const double si = std::sin(pose.heading);
    const double half_length = 0.35;
    const double half_width = 0.5;
    int churned = 0;
    const int x0 = std::max(0, static_cast<int>((pose.x - 0.62) / lawn_cell));
    const int x1 = std::min(lawn_cells_x - 1, static_cast<int>((pose.x + 0.62) / lawn_cell));
    const int y0 = std::max(0, static_cast<int>((pose.y - 0.62) / lawn_cell));
    const int y1 = std::min(lawn_cells_y - 1, static_cast<int>((pose.y + 0.62) / lawn_cell));
    for (int cy = y0; cy <= y1; ++cy) {
        for (int cx = x0; cx <= x1; ++cx) {
            const std::size_t index = static_cast<std::size_t>(cy) * static_cast<std::size_t>(lawn_cells_x) + static_cast<std::size_t>(cx);
            if (garden_.mulch[index] == 0 || trampled_[index] != 0)
                continue;
            const double dx = (cx + 0.5) * lawn_cell - pose.x;
            const double dy = (cy + 0.5) * lawn_cell - pose.y;
            if (std::abs(dx * co + dy * si) > half_length || std::abs(-dx * si + dy * co) > half_width)
                continue;
            trampled_[index] = 1;
            ++churned;
        }
    }
    if (churned > 0)
        dirty_.include(pose.x, pose.y, 0.8);
    const double right = pose.heading - pi / 2;
    int shredded = 0;
    for (Plant& plant : garden_.plants) {
        if (plant.crushed)
            continue;
        const double dx = plant.x - pose.x;
        const double dy = plant.y - pose.y;
        if (std::abs(dx * co + dy * si) > half_length + 0.08 || std::abs(-dx * si + dy * co) > half_width + 0.05)
            continue;
        plant.crushed = true;
        ++shredded;
        dirty_.include(plant.x, plant.y, 0.7);
        if (reduced_)
            continue;
        const std::uint32_t tint = flower_tint(plant.kind, plant_tone(garden_, plant));
        for (int k = 0; k < 16 && particles_.size() < 900; ++k) {
            // petals and torn leaves, most of them out of the chute
            Particle bit{};
            bit.kind = ParticleKind::petal;
            const bool chute = k % 4 != 0;
            bit.x = chute ? pose.x + std::cos(right) * 0.55 : plant.x;
            bit.y = chute ? pose.y + std::sin(right) * 0.55 : plant.y;
            bit.z = 0.08;
            const double angle = chute ? right + random_range(random_, -0.7, 0.7) : random_range(random_, 0, 2 * pi);
            const double speed = random_range(random_, 1.0, 3.4);
            bit.vx = std::cos(angle) * speed;
            bit.vy = std::sin(angle) * speed;
            bit.vz = random_range(random_, 0.8, 2.6);
            bit.life = random_range(random_, 0.7, 1.5);
            bit.size = random_range(random_, 0.016, 0.03);
            bit.spin = random_range(random_, -9, 9);
            bit.tint = k % 3 == 2 ? 0x3F7A2E : tint;
            particles_.push_back(bit);
        }
    }
    if (churned > 0 || shredded > 0) {
        wreck_ = std::min(1.0, wreck_ + 0.25 + 0.5 * shredded + 0.01 * churned);
        if (wreck_cue_ <= 0) {
            cue("mm_chipper", 0.8, random_range(random_, 0.92, 1.08), pose.x);
            wreck_cue_ = 0.55;
        }
        mulch_carry_ += churned * 0.35;
        while (mulch_carry_ >= 1 && !reduced_ && particles_.size() < 900) {
            mulch_carry_ -= 1;
            Particle chip{};
            chip.kind = ParticleKind::mulch;
            chip.x = pose.x + std::cos(right) * 0.55 + random_range(random_, -0.1, 0.1);
            chip.y = pose.y + std::sin(right) * 0.55 + random_range(random_, -0.1, 0.1);
            chip.z = 0.06;
            const double angle = right + random_range(random_, -0.8, 0.8);
            const double speed = random_range(random_, 1.2, 3.8);
            chip.vx = std::cos(angle) * speed;
            chip.vy = std::sin(angle) * speed;
            chip.vz = random_range(random_, 0.6, 2.4);
            chip.life = random_range(random_, 0.6, 1.3);
            chip.size = random_range(random_, 0.018, 0.04);
            chip.spin = random_range(random_, -14, 14);
            const double shade = random_unit(random_);
            chip.tint = shade < 0.5 ? 0x5A2A1C : (shade < 0.85 ? 0x74382A : 0x3C1A12);
            particles_.push_back(chip);
        }
        mulch_carry_ = std::min(mulch_carry_, 4.0);
    }
    if (shredded > 0)
        summon_granny();
}

// Driven into a trunk: the tree shakes and lets go of some leaves.
void Mowing::strike_trees() {
    const coverage::Pose& pose = mower_.pose();
    for (std::size_t index = 0; index < garden_.trees.size(); ++index) {
        const Tree& tree = garden_.trees[index];
        if (std::hypot(tree.x - pose.x, tree.y - pose.y) > tree.trunk + 0.95 || tree_shake_[index] > 0.35)
            continue;
        tree_shake_[index] = 1.0;
        cue("mm_bump", 0.7, 0.8, tree.x);
        cue("mm_puff", 0.6, 0.55, tree.x);
        if (reduced_)
            continue;
        const std::uint32_t leaves[4][2] = {{0x3E7A2A, 0x6FA544}, {0xF0A9C0, 0x4E8A36}, {0xF4F2EA, 0x4E8A36}, {0x9A3A1E, 0xC4622E}};
        const int count = 26 + static_cast<int>(tree.crown * 18);
        for (int k = 0; k < count && particles_.size() < 900; ++k) {
            Particle leaf{};
            leaf.kind = ParticleKind::leaf;
            const double angle = random_range(random_, 0, 2 * pi);
            const double away = tree.crown * std::sqrt(random_unit(random_));
            leaf.x = tree.x + std::cos(angle) * away;
            leaf.y = tree.y + std::sin(angle) * away;
            leaf.z = random_range(random_, 1.2, 2.2);
            leaf.vx = std::cos(angle) * random_range(random_, 0.1, 0.7);
            leaf.vy = std::sin(angle) * random_range(random_, 0.1, 0.7);
            leaf.vz = random_range(random_, -0.3, 0.2);
            leaf.life = random_range(random_, 2.2, 4.5);
            leaf.size = random_range(random_, 0.022, 0.04);
            leaf.spin = random_range(random_, -5, 5);
            leaf.tint = leaves[std::clamp(tree.kind, 0, 3)][next_random(random_) % 2U];
            particles_.push_back(leaf);
        }
    }
}

bool Mowing::granny_can_stand(double x, double y) const {
    const int i = static_cast<int>(std::floor(x / walk_cell));
    const int j = static_cast<int>(std::floor(y / walk_cell));
    if (i < 0 || j < 0 || i >= walk_cells_x || j >= walk_cells_y)
        return false;
    return walkable_[static_cast<std::size_t>(j) * static_cast<std::size_t>(walk_cells_x) + static_cast<std::size_t>(i)] != 0;
}

bool Mowing::granny_can_walk(double x0, double y0, double x1, double y1) const {
    const double length = std::hypot(x1 - x0, y1 - y0);
    const int steps = std::max(1, static_cast<int>(length / 0.1));
    for (int k = 1; k <= steps; ++k) {
        const double t = static_cast<double>(k) / steps;
        if (!granny_can_stand(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t))
            return false;
    }
    return true;
}

// She comes in from the edge of the lawn: not on top of the mower, and not from the far end of the garden.
void Mowing::summon_granny() {
    if (granny_.state != GrannyState::away || granny_.rest > 0)
        return;
    const coverage::Pose& pose = mower_.pose();
    double best = 1e9;
    for (int k = 0; k < 48; ++k) {
        const double along = (k % 12 + 0.5) / 12.0;
        const int side = k / 12;
        const double x = side == 0 ? along * lawn_width : (side == 1 ? lawn_width - 0.4 : (side == 2 ? along * lawn_width : 0.4));
        const double y = side == 0 ? 0.4 : (side == 1 ? along * lawn_height : (side == 2 ? lawn_height - 0.4 : along * lawn_height));
        if (!granny_can_stand(x, y))
            continue;
        const double score = std::abs(std::hypot(x - pose.x, y - pose.y) - 5.5);
        if (score < best) {
            best = score;
            granny_.x = x;
            granny_.y = y;
        }
    }
    if (best > 1e8)
        return;
    granny_.state = GrannyState::turning;
    granny_.clock = 0;
    granny_.chase = 0;
    granny_.stride = 0;
    granny_.facing = std::atan2(pose.y - granny_.y, pose.x - granny_.x);
    granny_.target_x = granny_.x;
    granny_.target_y = granny_.y;
    granny_.next_line = 2.8;
    cue("mm_granny_shout", 0.9, 1.0, granny_.x);
}

// Chooses her next straight line: the farthest point she can see along the shortest
// comfortable way round to (gx, gy), the way kept off the edges of beds and trunks.
void Mowing::aim_granny_at(double gx, double gy) {
    const int cells = walk_cells_x * walk_cells_y;
    const int from = std::clamp(static_cast<int>(granny_.y / walk_cell), 0, walk_cells_y - 1) * walk_cells_x +
                     std::clamp(static_cast<int>(granny_.x / walk_cell), 0, walk_cells_x - 1);
    std::vector<float> cost(static_cast<std::size_t>(cells), 1e9F);
    std::vector<int> parent(static_cast<std::size_t>(cells), -1);
    typedef std::pair<float, int> Entry;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open{};
    cost[static_cast<std::size_t>(from)] = 0;
    open.push({0.0F, from});
    while (!open.empty()) {
        const Entry top = open.top();
        open.pop();
        const int at = top.second;
        if (top.first > cost[static_cast<std::size_t>(at)])
            continue;
        const int ax = at % walk_cells_x;
        const int ay = at / walk_cells_x;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int bx = ax + dx;
                const int by = ay + dy;
                if ((dx == 0 && dy == 0) || bx < 0 || by < 0 || bx >= walk_cells_x || by >= walk_cells_y)
                    continue;
                const int next = by * walk_cells_x + bx;
                if (walkable_[static_cast<std::size_t>(next)] == 0)
                    continue;
                // No cutting corners between two blocked cells.
                if (dx != 0 && dy != 0 &&
                    (walkable_[static_cast<std::size_t>(ay * walk_cells_x + bx)] == 0 || walkable_[static_cast<std::size_t>(by * walk_cells_x + ax)] == 0))
                    continue;
                const float near_edge = 1.2F / static_cast<float>(clearance_[static_cast<std::size_t>(next)]);
                const float step = (dx != 0 && dy != 0 ? 1.4142F : 1.0F) * (1.0F + near_edge);
                const float through = cost[static_cast<std::size_t>(at)] + step;
                if (through < cost[static_cast<std::size_t>(next)]) {
                    cost[static_cast<std::size_t>(next)] = through;
                    parent[static_cast<std::size_t>(next)] = at;
                    open.push({through, next});
                }
            }
        }
    }
    // The cell she can reach that is nearest the goal (and, among near ones, cheapest to reach).
    int goal = from;
    double nearest = 1e9;
    for (int cell = 0; cell < cells; ++cell) {
        if (cost[static_cast<std::size_t>(cell)] > 1e8F)
            continue;
        const double away = std::hypot((cell % walk_cells_x + 0.5) * walk_cell - gx, (cell / walk_cells_x + 0.5) * walk_cell - gy) +
                            cost[static_cast<std::size_t>(cell)] * walk_cell * 0.02;
        if (away < nearest) {
            nearest = away;
            goal = cell;
        }
    }
    // Pull the string: walk back from the goal; the first cell she can see is where she heads.
    granny_.target_x = granny_.x;
    granny_.target_y = granny_.y;
    for (int cell = goal, guard = 0; cell >= 0 && cell != from && guard < cells; cell = parent[static_cast<std::size_t>(cell)], ++guard) {
        const double x = (cell % walk_cells_x + 0.5) * walk_cell;
        const double y = (cell / walk_cells_x + 0.5) * walk_cell;
        if (granny_can_walk(granny_.x, granny_.y, x, y)) {
            granny_.target_x = x;
            granny_.target_y = y;
            break;
        }
    }
    if (granny_can_walk(granny_.x, granny_.y, gx, gy)) {
        granny_.target_x = gx;
        granny_.target_y = gy;
    }
}

// After the mower: where it will be in a moment, not where it was.
void Mowing::aim_granny() {
    const coverage::Pose& pose = mower_.pose();
    const double lead = std::min(0.4, std::hypot(pose.x - granny_.x, pose.y - granny_.y) * 0.08);
    const double gx = std::clamp(pose.x + std::cos(pose.heading) * lead, 0.2, lawn_width - 0.2);
    const double gy = std::clamp(pose.y + std::sin(pose.heading) * lead, 0.2, lawn_height - 0.2);
    aim_granny_at(gx, gy);
}

void Mowing::update_granny(double seconds) {
    granny_.rest = std::max(0.0, granny_.rest - seconds);
    // The gnome who sees her off keeps his own little clock.
    if (guard_.state != GnomeState::hidden) {
        guard_.clock += seconds;
        if (guard_.state == GnomeState::rising) {
            guard_.height = std::min(1.15, guard_.clock / 0.22 * 1.15);
            if (guard_.clock > 0.22) {
                guard_.state = GnomeState::looking;
                guard_.clock = 0;
            }
        } else if (guard_.state == GnomeState::looking) {
            guard_.height = 1.0 + 0.15 * std::exp(-guard_.clock * 6);
            guard_.look = 0.5 * std::sin(guard_.clock * 3);
            if (granny_.state == GrannyState::away && guard_.clock > 1.5) {
                guard_.state = GnomeState::sinking;
                guard_.clock = 0;
            }
        } else {
            guard_.height = std::max(0.0, 1.0 - guard_.clock / 0.4);
            if (guard_.height <= 0)
                guard_ = Gnome{};
        }
    }
    if (granny_.state == GrannyState::away)
        return;
    granny_.clock += seconds;
    const coverage::Pose& pose = mower_.pose();
    if (granny_.state == GrannyState::walking || granny_.state == GrannyState::turning) {
        granny_.chase += seconds;
        granny_.next_line -= seconds;
        if (granny_.next_line <= 0) {
            granny_.next_line = random_range(random_, 3.0, 5.5);
            cue("mm_granny_scold", 0.85, 1.0, granny_.x);
        }
        if (std::hypot(pose.x - granny_.x, pose.y - granny_.y) < granny_reach) {
            // Got them. The garden starts again.
            caught_ = true;
            cue("mm_granny_bonk", 1.0, 1.0, granny_.x);
            return;
        }
        if (granny_.chase >= granny_patience) {
            // Dodged for long enough: a gnome pops up in her way, and that is too much for her.
            guard_ = Gnome{};
            guard_.state = GnomeState::rising;
            guard_.x = std::clamp(granny_.x + std::cos(granny_.facing) * 0.7, 0.3, lawn_width - 0.3);
            guard_.y = std::clamp(granny_.y + std::sin(granny_.facing) * 0.7, 0.3, lawn_height - 0.3);
            guard_.facing = granny_.facing + pi;
            gnome_say("mm_gnome_hoo2", 0.9, 1.0, guard_.x);
            cue("mm_granny_shriek", 0.95, 1.0, granny_.x);
            granny_.state = GrannyState::startled;
            granny_.clock = 0;
            return;
        }
    }
    if (granny_.state == GrannyState::turning) {
        // A moment to see where it has got to, turning to face the new line.
        if (granny_.clock < 1e-9 + seconds)
            aim_granny();
        const double want = std::hypot(granny_.target_x - granny_.x, granny_.target_y - granny_.y) > 0.05
                                ? std::atan2(granny_.target_y - granny_.y, granny_.target_x - granny_.x)
                                : std::atan2(pose.y - granny_.y, pose.x - granny_.x);
        double turn = want - granny_.facing;
        turn = std::atan2(std::sin(turn), std::cos(turn));
        granny_.facing += std::clamp(turn, -7.0 * seconds, 7.0 * seconds);
        if (granny_.clock >= 0.38 && std::abs(turn) < 0.35) {
            granny_.state = GrannyState::walking;
            granny_.clock = 0;
            granny_.replan = 0.3;
        }
    } else if (granny_.state == GrannyState::walking) {
        // She looks again every few steps without stopping, and only stops to turn round
        // when the mower has got behind her.
        granny_.replan -= seconds;
        if (granny_.replan <= 0) {
            granny_.replan = 0.3;
            aim_granny();
        }
        const double left = std::hypot(granny_.target_x - granny_.x, granny_.target_y - granny_.y);
        if (left > 0.05) {
            double turn = std::atan2(granny_.target_y - granny_.y, granny_.target_x - granny_.x) - granny_.facing;
            turn = std::remainder(turn, 2.0 * pi);
            if (std::abs(turn) > 1.9) {
                granny_.state = GrannyState::turning;
                granny_.clock = 0;
                return;
            }
            granny_.facing += std::clamp(turn, -4.5 * seconds, 4.5 * seconds);
            // Slower through a sharp bend, full speed when she is facing her way.
            const double go = std::min(left, granny_speed * seconds * (0.55 + 0.45 * std::cos(std::min(std::abs(turn), pi * 0.5))));
            granny_.x += (granny_.target_x - granny_.x) / left * go;
            granny_.y += (granny_.target_y - granny_.y) / left * go;
            granny_.stride += go;
        } else {
            granny_.replan = 0;
        }
    } else if (granny_.state == GrannyState::startled) {
        if (granny_.clock > 0.7) {
            // Off the lawn by the nearest way out, as fast as her legs will take her: out to
            // either side or off the near edge, never over the back wall.
            const double exits[3][2] = {{lawn_width + 1.5, granny_.y}, {granny_.x, lawn_height + 1.5}, {-1.5, granny_.y}};
            double best = 1e9;
            for (const double* exit : exits) {
                const double away = std::hypot(exit[0] - granny_.x, exit[1] - granny_.y) + (std::hypot(exit[0] - guard_.x, exit[1] - guard_.y) < std::hypot(exit[0] - granny_.x, exit[1] - granny_.y) ? 6.0 : 0.0);
                if (away < best) {
                    best = away;
                    granny_.target_x = exit[0];
                    granny_.target_y = exit[1];
                }
            }
            granny_.exit_x = granny_.target_x;
            granny_.exit_y = granny_.target_y;
            granny_.replan = 0;
            granny_.state = GrannyState::fleeing;
            granny_.clock = 0;
            cue("mm_granny_wail", 0.9, 1.0, granny_.x);
        }
    } else if (granny_.state == GrannyState::fleeing) {
        // Round the beds and trees to the edge of the lawn, then straight off it.
        granny_.replan -= seconds;
        if (granny_.replan <= 0) {
            granny_.replan = 0.25;
            const double ex = std::clamp(granny_.exit_x, 0.2, lawn_width - 0.2);
            const double ey = std::clamp(granny_.exit_y, 0.2, lawn_height - 0.2);
            const bool on_lawn = granny_.x > 0 && granny_.y > 0 && granny_.x < lawn_width && granny_.y < lawn_height;
            if (!on_lawn || std::hypot(ex - granny_.x, ey - granny_.y) < 0.3 || granny_can_walk(granny_.x, granny_.y, ex, ey)) {
                granny_.target_x = granny_.exit_x;
                granny_.target_y = granny_.exit_y;
            } else {
                aim_granny_at(ex, ey);
            }
        }
        const double left = std::hypot(granny_.target_x - granny_.x, granny_.target_y - granny_.y);
        const double go = std::min(left, 3.0 * seconds);
        if (left > 1e-6) {
            granny_.facing = std::atan2(granny_.target_y - granny_.y, granny_.target_x - granny_.x);
            granny_.x += (granny_.target_x - granny_.x) / left * go;
            granny_.y += (granny_.target_y - granny_.y) / left * go;
            granny_.stride += go;
        }
        const bool gone = granny_.x < -1.0 || granny_.y < -1.0 || granny_.x > lawn_width + 1.0 || granny_.y > lawn_height + 1.0;
        if (gone || (left - go < 0.05 && std::hypot(granny_.exit_x - granny_.x, granny_.exit_y - granny_.y) < 0.1)) {
            granny_ = Granny{};
            granny_.rest = 12.0;
        }
    }
}

// ---------------------------------------------------------------- bees and birds

// A flower still standing: a lawn bloom or dandelion in uncut grass, or a bed plant.
bool Mowing::flower_for_bee(double& x, double& y) {
    int standing = 0;
    for (const Bloom& bloom : garden_.blooms)
        standing += bloom.cut ? 0 : 1;
    for (const Dandelion& flower : garden_.dandelions)
        standing += flower.cut || flower.clock ? 0 : 1;
    for (const Plant& plant : garden_.plants)
        standing += plant.crushed ? 0 : 1;
    if (standing == 0)
        return false;
    int pick = static_cast<int>(next_random(random_) % static_cast<std::uint64_t>(standing));
    for (const Bloom& bloom : garden_.blooms) {
        if (bloom.cut)
            continue;
        if (pick-- == 0) {
            x = bloom.x;
            y = bloom.y;
            return true;
        }
    }
    for (const Dandelion& flower : garden_.dandelions) {
        if (flower.cut || flower.clock)
            continue;
        if (pick-- == 0) {
            x = flower.x;
            y = flower.y;
            return true;
        }
    }
    for (const Plant& plant : garden_.plants) {
        if (plant.crushed)
            continue;
        if (pick-- == 0) {
            x = plant.x;
            y = plant.y;
            return true;
        }
    }
    return false;
}

// Somewhere to land: on a tree's canopy, or the rim of the fountain or birdbath.
bool Mowing::perch_for_bird(double& x, double& y, double& z) {
    std::vector<int> choices{};
    for (std::size_t index = 0; index < garden_.trees.size(); ++index)
        choices.push_back(static_cast<int>(index));
    for (std::size_t index = 0; index < garden_.props.size(); ++index) {
        if (garden_.props[index].kind == PropKind::fountain || garden_.props[index].kind == PropKind::birdbath)
            choices.push_back(-1 - static_cast<int>(index));
    }
    if (choices.empty())
        return false;
    const int chosen = choices[static_cast<std::size_t>(next_random(random_) % choices.size())];
    const double angle = random_range(random_, 0, 2 * pi);
    if (chosen >= 0) {
        const Tree& tree = garden_.trees[static_cast<std::size_t>(chosen)];
        const double out = tree.crown * random_range(random_, 0.35, 0.8);
        x = tree.x + std::cos(angle) * out;
        y = tree.y + std::sin(angle) * out;
        z = 2.2 + 0.6 * (1 - out / tree.crown);
        return true;
    }
    const Prop& prop = garden_.props[static_cast<std::size_t>(-1 - chosen)];
    x = prop.x + std::cos(angle) * prop.rx * 0.9;
    y = prop.y + std::sin(angle) * prop.rx * 0.9;
    z = prop.kind == PropKind::fountain ? 0.8 : 0.9;
    return true;
}

void Mowing::update_life(double seconds) {
    const coverage::Pose& pose = mower_.pose();
    const bool running = engine_on_ && engine_clock_ >= engine_start_seconds;
    // Bees arrive one at a time while there are flowers, more often when the garden is quiet.
    bee_wait_ -= seconds * (running ? 0.6 : 1.0);
    if (bee_wait_ <= 0) {
        bee_wait_ = random_range(random_, 8.0, 20.0);
        double fx = 0;
        double fy = 0;
        if (bees_.size() < 4 && flower_for_bee(fx, fy)) {
            Bee bee{};
            const double side = random_unit(random_);
            bee.x = side < 0.5 ? (side < 0.25 ? -0.5 : lawn_width + 0.5) : random_range(random_, 0, lawn_width);
            bee.y = side < 0.5 ? random_range(random_, 0, lawn_height) : (side < 0.75 ? -0.5 : lawn_height + 0.5);
            bee.z = 0.6;
            bee.target_x = fx;
            bee.target_y = fy;
            bee.visits = 1 + static_cast<int>(next_random(random_) % 3U);
            bee.wobble = random_range(random_, 0, 2 * pi);
            bee.id = next_bee_++;
            bees_.push_back(bee);
        }
    }
    std::size_t kept = 0;
    for (std::size_t index = 0; index < bees_.size(); ++index) {
        Bee bee = bees_[index];
        bee.clock += seconds;
        bee.wobble += seconds * 9;
        const double near_mower = std::hypot(bee.x - pose.x, bee.y - pose.y);
        if (bee.state != BeeState::leaving && near_mower < 2.2 && running) {
            // Off, away from the mower.
            bee.state = BeeState::leaving;
            const double away = std::atan2(bee.y - pose.y, bee.x - pose.x);
            bee.target_x = bee.x + std::cos(away) * 30;
            bee.target_y = bee.y + std::sin(away) * 30;
        }
        if (bee.state == BeeState::visiting) {
            // Hovering about the flower, dipping to it.
            bee.z = 0.08 + 0.05 * std::sin(bee.clock * 5) + 0.02 * std::sin(bee.wobble);
            bee.x = bee.target_x + 0.06 * std::sin(bee.clock * 2.1) + 0.02 * std::sin(bee.wobble * 0.7);
            bee.y = bee.target_y + 0.06 * std::cos(bee.clock * 1.7);
            if (bee.clock >= bee.stay) {
                bee.visits -= 1;
                double fx = 0;
                double fy = 0;
                if (bee.visits > 0 && flower_for_bee(fx, fy)) {
                    bee.state = BeeState::arriving;
                    bee.target_x = fx;
                    bee.target_y = fy;
                } else {
                    bee.state = BeeState::leaving;
                    const double away = random_range(random_, 0, 2 * pi);
                    bee.target_x = bee.x + std::cos(away) * 30;
                    bee.target_y = bee.y + std::sin(away) * 30;
                }
                bee.clock = 0;
            }
        } else {
            // In flight: a wavering line toward the target, a little above the grass.
            const double dx = bee.target_x - bee.x;
            const double dy = bee.target_y - bee.y;
            const double away = std::hypot(dx, dy);
            const double speed = bee.state == BeeState::leaving ? 2.4 : 1.4;
            const double heading = std::atan2(dy, dx) + 0.6 * std::sin(bee.wobble * 0.5);
            bee.vx += (std::cos(heading) * speed - bee.vx) * (1 - std::exp(-seconds / 0.2));
            bee.vy += (std::sin(heading) * speed - bee.vy) * (1 - std::exp(-seconds / 0.2));
            bee.x += bee.vx * seconds;
            bee.y += bee.vy * seconds;
            bee.z += ((bee.state == BeeState::leaving ? 1.2 : 0.35) + 0.08 * std::sin(bee.wobble) - bee.z) * (1 - std::exp(-seconds / 0.5));
            if (bee.state == BeeState::arriving && away < 0.12) {
                bee.state = BeeState::visiting;
                bee.clock = 0;
                bee.stay = random_range(random_, 1.5, 4.5);
                bee.vx = 0;
                bee.vy = 0;
            }
            if (bee.state == BeeState::leaving && (bee.x < -1 || bee.y < -1 || bee.x > lawn_width + 1 || bee.y > lawn_height + 1))
                continue;
        }
        bees_[kept] = bee;
        ++kept;
    }
    bees_.resize(kept);
    // Birds come to the trees and the water, and sit a while.
    bird_wait_ -= seconds * (running ? 0.5 : 1.0);
    if (bird_wait_ <= 0) {
        bird_wait_ = random_range(random_, 12.0, 30.0);
        double px = 0;
        double py = 0;
        double pz = 0;
        if (birds_.size() < 3 && perch_for_bird(px, py, pz)) {
            Bird bird{};
            bird.kind = static_cast<int>(next_random(random_) % 4U);
            const double side = random_unit(random_);
            bird.x = side < 0.5 ? (side < 0.25 ? -1.5 : lawn_width + 1.5) : random_range(random_, 0, lawn_width);
            bird.y = side < 0.5 ? random_range(random_, 0, lawn_height) : (side < 0.75 ? -1.5 : lawn_height + 1.5);
            bird.z = 4.0;
            bird.target_x = px;
            bird.target_y = py;
            bird.target_z = pz;
            bird.hops = 1 + static_cast<int>(next_random(random_) % 3U);
            bird.heading = std::atan2(py - bird.y, px - bird.x);
            birds_.push_back(bird);
        }
    }
    kept = 0;
    for (std::size_t index = 0; index < birds_.size(); ++index) {
        Bird bird = birds_[index];
        bird.clock += seconds;
        const double near_mower = std::hypot(bird.x - pose.x, bird.y - pose.y);
        if (bird.state != BirdState::leaving && near_mower < 3.2 && running) {
            bird.state = BirdState::leaving;
            bird.singing = false;
            const double away = std::atan2(bird.y - pose.y, bird.x - pose.x);
            bird.target_x = bird.x + std::cos(away) * 40;
            bird.target_y = bird.y + std::sin(away) * 40;
            bird.target_z = 6;
            bird.clock = 0;
        }
        if (bird.state == BirdState::perched) {
            bird.flap = 0;
            // The odd hop and turn; it sings for a spell when it has settled.
            if (std::fmod(bird.clock, 2.5) < seconds)
                bird.heading += random_range(random_, -1.2, 1.2);
            bird.singing = bird.clock > 1.0 && bird.clock < bird.stay - 0.5;
            if (bird.clock >= bird.stay) {
                bird.hops -= 1;
                bird.singing = false;
                double px = 0;
                double py = 0;
                double pz = 0;
                if (bird.hops > 0 && perch_for_bird(px, py, pz)) {
                    bird.state = BirdState::arriving;
                    bird.target_x = px;
                    bird.target_y = py;
                    bird.target_z = pz;
                } else {
                    bird.state = BirdState::leaving;
                    const double away = random_range(random_, 0, 2 * pi);
                    bird.target_x = bird.x + std::cos(away) * 40;
                    bird.target_y = bird.y + std::sin(away) * 40;
                    bird.target_z = 6;
                }
                bird.clock = 0;
            }
        } else {
            const double dx = bird.target_x - bird.x;
            const double dy = bird.target_y - bird.y;
            const double away = std::hypot(dx, dy);
            const double speed = bird.state == BirdState::leaving ? 6.0 : std::min(5.0, 1.0 + away * 1.5);
            const double want = std::atan2(dy, dx);
            double turn = want - bird.heading;
            turn = std::atan2(std::sin(turn), std::cos(turn));
            bird.heading += std::clamp(turn, -4.0 * seconds, 4.0 * seconds);
            bird.x += std::cos(bird.heading) * speed * seconds;
            bird.y += std::sin(bird.heading) * speed * seconds;
            // Glides in on the last stretch, flaps the rest.
            bird.flap += seconds * (away < 1.0 && bird.state == BirdState::arriving ? 4 : 14);
            const double climb = bird.state == BirdState::leaving ? 6.0 : bird.target_z + std::min(2.0, away * 0.5);
            bird.z += (climb - bird.z) * (1 - std::exp(-seconds / 0.6));
            if (bird.state == BirdState::arriving && away < 0.15) {
                bird.state = BirdState::perched;
                bird.z = bird.target_z;
                bird.clock = 0;
                bird.stay = random_range(random_, 5.0, 14.0);
            }
            if (bird.state == BirdState::leaving && (bird.x < -2 || bird.y < -2 || bird.x > lawn_width + 2 || bird.y > lawn_height + 2))
                continue;
        }
        birds_[kept] = bird;
        ++kept;
    }
    birds_.resize(kept);
}

namespace {

// How long each of his moves lasts, seconds.
double move_length(int dance) {
    const double lengths[6] = {1.68, 1.68, 1.2, 1.7, 2.1, 1.68};
    return lengths[std::clamp(dance, 0, 5)];
}

} // namespace

void Mowing::place_gnome() {
    const coverage::Pose& pose = mower_.pose();
    // Up he comes anywhere on the lawn, at random: never right on top of the mower, best
    // in tall grass he can hide in.
    double best_x = -1;
    double best_y = -1;
    double best = -1e9;
    for (int attempt = 0; attempt < 120; ++attempt) {
        const double x = random_range(random_, 0.8, lawn_width - 0.8);
        const double y = random_range(random_, 0.8, lawn_height - 0.8);
        if (std::hypot(x - pose.x, y - pose.y) < 3.0)
            continue;
        const bool clear = open_lawn(garden_, x, y) && open_lawn(garden_, x + 0.4, y) && open_lawn(garden_, x - 0.4, y) &&
                           open_lawn(garden_, x, y + 0.4) && open_lawn(garden_, x, y - 0.4);
        if (!clear)
            continue;
        int tall = mower_.cut_at(x, y) ? 0 : 1;
        for (int k = 0; k < 8; ++k)
            tall += mower_.cut_at(x + 0.3 * std::cos(k * pi / 4), y + 0.3 * std::sin(k * pi / 4)) ? 0 : 1;
        const double score = (tall == 9 ? 1.0 : 0.08 * tall) + random_range(random_, 0, 1.0);
        if (score > best) {
            best = score;
            best_x = x;
            best_y = y;
        }
    }
    if (best_x < 0) {
        gnome_.next = random_range(random_, 2.0, 5.0);
        return;
    }
    const double x = best_x;
    const double y = best_y;
    if (!gnome_.encore)
        gnome_.pops = 2 + static_cast<int>(next_random(random_) % 3U);
    gnome_.encore = false;
    gnome_.x = x;
    gnome_.y = y;
    gnome_.home_x = x;
    gnome_.home_y = y;
    gnome_.state = GnomeState::rising;
    gnome_.clock = 0;
    gnome_.length = random_range(random_, 6.0, 10.0);
    gnome_.facing = std::atan2(pose.y - y, pose.x - x);
    gnome_.look = 0;
    gnome_.dance = static_cast<int>(next_random(random_) % 6U);
    gnome_.beat = 0;
    gnome_.sought = false;
    gnome_say(random_unit(random_) < 0.5 ? "mm_gnome_hoo" : "mm_gnome_hoo2", 0.8, random_range(random_, 0.96, 1.04), x);
}

void Mowing::update_gnome(double seconds) {
    gnome_voice_ = std::max(0.0, gnome_voice_ - seconds);
    Gnome& g = gnome_;
    g.clock += seconds;
    const coverage::Pose& pose = mower_.pose();
    const double mower_distance = std::hypot(g.x - pose.x, g.y - pose.y);
    switch (g.state) {
    case GnomeState::hidden:
        g.height = 0;
        g.next -= seconds;
        if (g.next <= 0 && !mower_.finished())
            place_gnome();
        break;
    case GnomeState::rising: {
        // A jump out of the grass: up past standing height, then settle.
        const double t = std::min(1.0, g.clock / 0.45);
        g.height = t < 0.7 ? 1.25 * std::sin(t / 0.7 * pi * 0.5) : 1.25 - 0.25 * (t - 0.7) / 0.3;
        if (t >= 1) {
            g.state = GnomeState::looking;
            g.clock = 0;
            g.height = 1;
        }
        break;
    }
    case GnomeState::looking: {
        // He dances, and always turns to face the mower wherever it goes.
        const double want = std::atan2(pose.y - g.y, pose.x - g.x);
        double turn = std::remainder(want - g.facing, 2 * pi);
        turn = std::clamp(turn, -5.0 * seconds, 5.0 * seconds);
        g.facing += turn;
        g.look = 0.12 * std::sin(g.clock * 5.3) + 0.08 * std::sin(g.clock * 13.1);
        g.height = 1;
        g.beat += seconds;
        if (g.beat >= move_length(g.dance)) {
            // On to another move, with a giggle or a jeer.
            const int last = g.dance;
            g.dance = static_cast<int>(next_random(random_) % 5U);
            if (g.dance >= last)
                ++g.dance;
            g.beat = 0;
            const char* laugh = g.dance == 3 ? "mm_gnome_nyah" : g.dance == 4 ? "mm_gnome_giggle2" : random_unit(random_) < 0.5 ? "mm_gnome_giggle" : "mm_gnome_hoo2";
            gnome_say(laugh, 0.75, random_range(random_, 0.95, 1.06), g.x);
        }
        // He wanders about where he came up, in a lazy figure of eight, where the lawn is open.
        const double wx = g.home_x + 0.38 * std::sin(g.clock * 0.9);
        const double wy = g.home_y + 0.22 * std::sin(g.clock * 1.8);
        if (open_lawn(garden_, wx, wy)) {
            dirty_.include(g.x, g.y, 0.01);
            g.x = wx;
            g.y = wy;
        }
        // Too close, or bored: down he dives, laughing, to pop up again further on.
        if (g.clock >= g.length || mower_distance < 1.4) {
            g.state = GnomeState::sinking;
            g.clock = 0;
            gnome_say(mower_distance < 1.4 ? "mm_gnome_hoo2" : "mm_gnome_giggle", 0.8, random_range(random_, 1.0, 1.1), g.x);
        }
        break;
    }
    case GnomeState::sinking:
        g.height = std::max(0.0, 1 - g.clock / 0.3);
        if (g.clock >= 0.3) {
            g.state = GnomeState::hidden;
            g.height = 0;
            if (g.pops > 0) {
                --g.pops;
                g.encore = true;
                g.next = random_range(random_, 0.6, 1.4);
            } else {
                g.encore = false;
                g.next = random_range(random_, 120.0, 300.0);
            }
        }
        break;
    case GnomeState::frozen:
        // Stuck exactly as he was, mid-step. The mower comes for him once it has seen him.
        if (!g.sought && (mower_.sees(g.x, g.y) || mower_.finished())) {
            g.sought = true;
            if (!held_)
                mower_.set_goal(g.x, g.y);
        }
        if (mower_distance < gnome_reach)
            shatter();
        break;
    case GnomeState::shattered:
        if (scream_delay_ >= 0) {
            scream_delay_ -= seconds;
            if (scream_delay_ < 0)
                cue("mm_scream", 0.32, random_range(random_, 0.95, 1.08), g.x);
        }
        if (g.clock > 2.5) {
            g.state = GnomeState::hidden;
            g.pops = 0;
            g.encore = false;
            g.next = random_range(random_, 140.0, 320.0);
        }
        break;
    }
    dirty_.include(g.x, g.y, 0.01);
}

void Mowing::shatter() {
    Gnome& g = gnome_;
    g.state = GnomeState::shattered;
    g.clock = 0;
    mower_.clear_goal();
    cue("mm_shatter", 0.9, 1.0, g.x);
    scream_delay_ = 0.12;
    const int shards = reduced_ ? 0 : 26;
    for (int k = 0; k < shards; ++k) {
        Particle shard{};
        shard.kind = ParticleKind::shard;
        shard.x = g.x;
        shard.y = g.y;
        shard.z = 0.15 * g.height;
        const double angle = random_range(random_, 0, 2 * pi);
        const double speed = random_range(random_, 0.6, 2.4);
        shard.vx = std::cos(angle) * speed;
        shard.vy = std::sin(angle) * speed;
        shard.vz = random_range(random_, 0.8, 2.6);
        shard.life = random_range(random_, 0.8, 1.6);
        shard.size = random_range(random_, 0.012, 0.035);
        shard.spin = random_range(random_, -14, 14);
        const double which = random_unit(random_);
        shard.tint = which < 0.35 ? 0xC8322B : (which < 0.55 ? 0x2F6FB0 : (which < 0.75 ? 0xF2EDE4 : 0xE8B48F));
        particles_.push_back(shard);
    }
    if (!reduced_) {
        Particle hat{};
        hat.kind = ParticleKind::hat;
        hat.x = g.x;
        hat.y = g.y;
        hat.z = 0.25;
        hat.vx = random_range(random_, -0.8, 0.8);
        hat.vy = random_range(random_, -0.8, 0.8);
        hat.vz = 3.0;
        hat.life = 1.6;
        hat.size = 0.07;
        hat.spin = random_range(random_, -6, 6);
        hat.tint = 0xC8322B;
        particles_.push_back(hat);
    }
}

// ---------------------------------------------------------------- the engine

void Mowing::update_engine(double seconds, int newly_cut) {
    // Cutting load: tall grass taken per second against a full deck's worth.
    const coverage::Machine machine = machine_setup();
    const double full = machine.deck_width * machine.speed / (lawn_cell * lawn_cell);
    const double load = std::clamp(static_cast<double>(newly_cut) / std::max(seconds, 1e-6) / full, 0.0, 1.0);
    load_smooth_ += (std::max(load, wreck_) - load_smooth_) * (1 - std::exp(-seconds / 0.25));
    const EngineSpec spec = engine_spec(livery_);
    // The governor: speed sags with load, then the throttle opens to pull it back.
    const double target = spec.rated_rpm * (1 - spec.droop * load_smooth_);
    const double hunt = 1 + 0.004 * std::sin(time_ * 2.3) * load_smooth_;
    voice_.rpm += (target * hunt - voice_.rpm) * (1 - std::exp(-seconds / spec.response));
    voice_.rate = voice_.rpm / spec.rated_rpm;
    voice_.throttle = std::clamp(0.25 + 0.75 * load_smooth_ + (spec.rated_rpm - voice_.rpm) / spec.rated_rpm * 4, 0.0, 1.0);
    voice_.load = load_smooth_;
}

// ---------------------------------------------------------------- particles

void Mowing::spray(double seconds) {
    if (reduced_ || voice_.load < 0.05)
        return;
    // Clippings leave the discharge chute on the mower's right.
    spray_carry_ += seconds * voice_.load * 70;
    const coverage::Pose& pose = mower_.pose();
    const double right = pose.heading - pi / 2;
    while (spray_carry_ >= 1 && particles_.size() < 600) {
        spray_carry_ -= 1;
        Particle bit{};
        bit.kind = ParticleKind::clipping;
        bit.x = pose.x + std::cos(right) * 0.55 + random_range(random_, -0.08, 0.08);
        bit.y = pose.y + std::sin(right) * 0.55 + random_range(random_, -0.08, 0.08);
        bit.z = 0.05;
        const double angle = right + random_range(random_, -0.6, 0.5);
        const double speed = random_range(random_, 0.8, 2.2);
        bit.vx = std::cos(angle) * speed;
        bit.vy = std::sin(angle) * speed;
        bit.vz = random_range(random_, 0.2, 0.9);
        bit.life = random_range(random_, 0.35, 0.8);
        bit.size = random_range(random_, 0.012, 0.025);
        bit.spin = random_range(random_, -12, 12);
        const double shade = random_unit(random_);
        bit.tint = shade < 0.5 ? 0x5F8F33 : (shade < 0.85 ? 0x7DA944 : 0xA9BE62);
        particles_.push_back(bit);
    }
    spray_carry_ = std::min(spray_carry_, 1.0);
}

void Mowing::update_particles(double seconds) {
    std::size_t kept = 0;
    for (std::size_t index = 0; index < particles_.size(); ++index) {
        Particle p = particles_[index];
        p.age += seconds;
        if (p.age >= p.life)
            continue;
        const bool floats = p.kind == ParticleKind::seed || p.kind == ParticleKind::leaf;
        const double drag = floats ? 1.6 : 2.2;
        p.vx *= std::exp(-drag * seconds);
        p.vy *= std::exp(-drag * seconds);
        if (floats) {
            // Seeds drift on the breeze and sink slowly.
            p.vx += 0.25 * seconds;
            p.vz = std::max(p.vz - 0.4 * seconds, -0.05);
        } else {
            p.vz -= 9.8 * seconds;
        }
        p.x += p.vx * seconds;
        p.y += p.vy * seconds;
        p.z += p.vz * seconds;
        if (p.z < 0) {
            p.z = 0;
            p.vz = 0;
            p.vx *= 0.5;
            p.vy *= 0.5;
        }
        particles_[kept] = p;
        ++kept;
    }
    particles_.resize(kept);
}

} // namespace mm
