// Mowing's core checks: gardens are sound, every garden gets mowed, the gnome
// freezes and shatters when found, someone can take the controls, the engine bogs
// and recovers, everything is deterministic, and the retained lawn always matches a
// fresh rebuild.
#include "garden.hpp"
#include "grass_art.hpp"
#include "sim.hpp"
#include "yard.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int checks = 0;
void require(bool condition, const char* what) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        std::exit(1);
    }
}

bool has_cue(const std::vector<mm::Cue>& cues, const std::string& name) {
    for (const mm::Cue& cue : cues) {
        if (cue.name == name)
            return true;
    }
    return false;
}

// True when every patch of lawn the mower can stand on is joined to where it starts:
// open cells more than the mower's half-width from anything, flooded from the start.
bool mower_gets_everywhere(const mm::Garden& garden) {
    const int w = mm::lawn_cells_x;
    const int h = mm::lawn_cells_y;
    const int reach = 13;
    std::vector<std::uint8_t> open(static_cast<std::size_t>(w) * h, 0);
    for (int y = reach; y < h - reach; ++y) {
        for (int x = reach; x < w - reach; ++x) {
            bool clear = true;
            for (int dy = -reach; dy <= reach && clear; ++dy) {
                for (int dx = -reach; dx <= reach && clear; ++dx)
                    clear = dx * dx + dy * dy > reach * reach || garden.obstacles[static_cast<std::size_t>(y + dy) * w + static_cast<std::size_t>(x + dx)] == 0;
            }
            open[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)] = clear ? 1 : 0;
        }
    }
    std::vector<int> stack{static_cast<int>(garden.start_y / mm::lawn_cell) * w + static_cast<int>(garden.start_x / mm::lawn_cell)};
    while (!stack.empty()) {
        const int at = stack.back();
        stack.pop_back();
        if (at < 0 || at >= w * h || open[static_cast<std::size_t>(at)] != 1)
            continue;
        open[static_cast<std::size_t>(at)] = 2;
        stack.push_back(at - 1);
        stack.push_back(at + 1);
        stack.push_back(at - w);
        stack.push_back(at + w);
    }
    for (const std::uint8_t cell : open) {
        if (cell == 1)
            return false;
    }
    return true;
}

void test_gardens() {
    int with_beds = 0;
    int without_beds = 0;
    int with_trees = 0;
    int with_props = 0;
    int two_kinds = 0;
    int with_lawn_flowers = 0;
    int without_mushrooms = 0;
    std::vector<int> kinds_seen(static_cast<std::size_t>(mm::flower_count), 0);
    for (std::uint64_t seed = 1; seed <= 120; ++seed) {
        const mm::Garden garden = mm::make_garden(seed);
        with_beds += garden.beds.empty() ? 0 : 1;
        without_beds += garden.beds.empty() ? 1 : 0;
        with_trees += garden.trees.empty() ? 0 : 1;
        with_props += garden.props.empty() ? 0 : 1;
        with_lawn_flowers += garden.blooms.empty() && garden.dandelions.empty() ? 0 : 1;
        without_mushrooms += garden.mushrooms.empty() ? 1 : 0;
        require(mm::open_lawn(garden, garden.start_x, garden.start_y) && mm::open_lawn(garden, garden.start_x + 1.2, garden.start_y + 1.2),
                "the mower starts in open grass");
        require(mower_gets_everywhere(garden), "the mower can get round everything that was placed");
        for (const mm::Bed& bed : garden.beds) {
            require(bed.kinds == 1 || bed.kinds == 2, "a bed has one kind of flower, or two");
            two_kinds += bed.kinds == 2 ? 1 : 0;
            ++kinds_seen[static_cast<std::size_t>(bed.flowers[0])];
        }
        for (const mm::Plant& plant : garden.plants) {
            const mm::Bed& bed = garden.beds[static_cast<std::size_t>(plant.bed)];
            require(mm::inside_bed(bed, plant.x, plant.y), "bed flowers are planted in their bed");
            require(plant.kind == bed.flowers[0] || (bed.kinds == 2 && plant.kind == bed.flowers[1]), "and are of the bed's kind");
        }
        for (const mm::Mushroom& mushroom : garden.mushrooms)
            require(mm::open_lawn(garden, mushroom.x, mushroom.y), "mushrooms grow in the lawn");
        for (const mm::Bloom& bloom : garden.blooms)
            require(mm::open_lawn(garden, bloom.x, bloom.y), "lawn flowers grow in the lawn");
        for (std::size_t index = 0; index < garden.mulch.size(); ++index)
            require(garden.obstacles[index] == (garden.mulch[index] != 0 || garden.solid[index] != 0 ? 1 : 0), "the mower keeps off mulch and props, and nothing else");
        const mm::Garden again = mm::make_garden(seed);
        require(again.beds.size() == garden.beds.size() && again.plants.size() == garden.plants.size() && again.trees.size() == garden.trees.size() &&
                    again.props.size() == garden.props.size() && again.blooms.size() == garden.blooms.size() && again.obstacles == garden.obstacles,
                "a seed always makes the same garden");
    }
    require(with_beds > 60 && without_beds > 10, "most gardens have flower beds, and some have none");
    require(with_trees > 40 && with_trees < 100, "some gardens have trees");
    require(with_props > 50, "things are left out on the grass");
    require(two_kinds > 5, "now and then a bed holds two kinds of flower");
    require(with_lawn_flowers > 60 && with_lawn_flowers < 120 && without_mushrooms > 70, "not every lawn has flowers or mushrooms");
    for (const int seen : kinds_seen)
        require(seen > 0, "every kind of flower turns up");
}

void test_mowing_completes() {
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
        mm::Mowing mowing(seed, mm::Livery::green_jd);
        double time = 0;
        while (!mowing.complete() && time < 1500) {
            mowing.advance(1.0 / 20, false);
            time += 1.0 / 20;
        }
        std::printf("garden %llu: mowed in %.0f s, %.1f%% of the lawn\n", static_cast<unsigned long long>(seed), time,
                    mowing.mower().progress() * 100);
        require(mowing.complete(), "every garden is mowed");
        require(mowing.mower().progress() > 0.95, "nearly all of the lawn is cut (the rest hugs the beds)");
        int standing = 0;
        for (const mm::Dandelion& flower : mowing.garden().dandelions)
            standing += flower.cut ? 0 : 1;
        for (const mm::Bloom& bloom : mowing.garden().blooms)
            standing += bloom.cut ? 0 : 1;
        const std::size_t flowers = mowing.garden().dandelions.size() + mowing.garden().blooms.size();
        require(standing <= 3 + static_cast<int>(flowers / 20), "the lawn's flowers go with the grass");
    }
}

void test_gnome() {
    mm::Mowing mowing(7, mm::Livery::orange_h);
    std::vector<mm::Cue> heard{};
    double time = 0;
    while (mowing.gnome().state != mm::GnomeState::looking && time < 400) {
        mowing.advance(1.0 / 30, false);
        for (const mm::Cue& cue : mowing.take_cues())
            heard.push_back(cue);
        time += 1.0 / 30;
    }
    require(mowing.gnome().state == mm::GnomeState::looking, "the gnome comes up for a look");
    require(has_cue(heard, "mm_gnome_hoo") || has_cue(heard, "mm_gnome_hoo2"), "and says hoo");
    require(!mowing.poke(mowing.gnome().x + 2, mowing.gnome().y), "a click elsewhere misses him");
    require(mowing.poke(mowing.gnome().x, mowing.gnome().y), "a click on him freezes him");
    const mm::Gnome frozen = mowing.gnome();
    for (int k = 0; k < 30; ++k)
        mowing.advance(1.0 / 30, false);
    require(mowing.gnome().state == mm::GnomeState::frozen && mowing.gnome().look == frozen.look &&
                mowing.gnome().height == frozen.height,
            "frozen means stuck mid-pose");
    heard.clear();
    time = 0;
    while (mowing.gnome().state == mm::GnomeState::frozen && time < 900) {
        mowing.advance(1.0 / 30, false);
        for (const mm::Cue& cue : mowing.take_cues())
            heard.push_back(cue);
        time += 1.0 / 30;
    }
    require(mowing.gnome().state == mm::GnomeState::shattered, "the mower finds him");
    for (int k = 0; k < 20; ++k) {
        mowing.advance(1.0 / 30, false);
        for (const mm::Cue& cue : mowing.take_cues())
            heard.push_back(cue);
    }
    require(has_cue(heard, "mm_shatter") && has_cue(heard, "mm_scream"), "china shatters, and a faint scream");
    int shards = 0;
    for (const mm::Particle& particle : mowing.particles())
        shards += particle.kind == mm::ParticleKind::shard ? 1 : 0;
    require(shards > 0, "and there are pieces");
}

void test_takeover() {
    mm::Mowing mowing(3, mm::Livery::red_t);
    for (int k = 0; k < 100; ++k)
        mowing.advance(1.0 / 30, false);
    const coverage::Pose start = mowing.mower().pose();
    require(!mowing.grab(start.x + 3, start.y + 3), "grabbing the grass does nothing");
    require(mowing.grab(start.x, start.y), "grabbing the mower takes the controls");
    const double tx = std::clamp(start.x, 2.0, 14.0);
    const double ty = std::clamp(start.y + 1.5, 1.0, 9.0);
    mowing.steer(tx, ty);
    for (int k = 0; k < 150; ++k)
        mowing.advance(1.0 / 30, false);
    require(mowing.mower().phase() == coverage::Phase::manual, "it stays under control while held");
    const coverage::Pose moved = mowing.mower().pose();
    require(std::hypot(moved.x - start.x, moved.y - start.y) > 0.5, "and drives where it is dragged");
    mowing.let_go();
    // (dropped on a tree's mulch, it first drives itself clear)
    for (int k = 0; k < 240; ++k)
        mowing.advance(1.0 / 30, false);
    require(mowing.mower().phase() != coverage::Phase::manual, "letting go hands it back");
}

void test_engine() {
    mm::Mowing mowing(5, mm::Livery::orange_h);
    double lowest = 2;
    for (int k = 0; k < 900; ++k) {
        mowing.advance(1.0 / 30, false);
        lowest = std::min(lowest, mowing.voice().rate);
    }
    require(lowest < 0.995 && lowest > 0.9, "tall grass bogs the engine a little");
    require(mowing.voice().throttle > 0 && mowing.voice().throttle <= 1, "the throttle stays in range");
}

void test_deterministic() {
    mm::Mowing a(9, mm::Livery::orange_h);
    mm::Mowing b(9, mm::Livery::orange_h);
    for (int k = 0; k < 1200; ++k) {
        a.advance(0.05, false);
        b.advance(0.03, false);
        b.advance(0.02, false);
    }
    require(a.stripes() == b.stripes() && a.mower().cut() == b.mower().cut(), "the same garden is mowed the same way");
}

void test_retained_lawn() {
    // With and without grass art: refreshing what changed equals shading everything.
    for (int pass = 0; pass < 2; ++pass) {
        mm::GrassArt art{};
        if (pass == 1)
            art = mm::make_grass_art(mm::make_garden(4), nullptr);
        mm::Mowing mowing(4, mm::Livery::orange_h);
        mm::Yard live{};
        live.build(420, 270, art, mowing);
        static_cast<void>(mowing.take_dirty());
        for (int k = 0; k < 600; ++k) {
            mowing.advance(1.0 / 30, false);
            live.refresh(art, mowing, mowing.take_dirty());
        }
        mm::Yard fresh{};
        fresh.build(420, 270, art, mowing);
        const mm::MowerPose pose{mowing.mower().pose().x, mowing.mower().pose().y, mowing.mower().pose().heading, 0, 0, false};
        const std::vector<std::uint8_t> a = live.compose(mowing, pose, 1.0);
        const std::vector<std::uint8_t> b = fresh.compose(mowing, pose, 1.0);
        require(a == b, "the retained lawn matches a full rebuild");
    }
}

} // namespace

void test_engine_switch() {
    mm::Mowing mowing(6, mm::Livery::orange_h);
    for (int k = 0; k < 300; ++k)
        mowing.advance(1.0 / 30, false);
    static_cast<void>(mowing.take_cues());
    const coverage::Pose parked = mowing.mower().pose();
    const double cut = mowing.mower().progress();
    mowing.set_engine(false);
    std::vector<mm::Cue> cues = mowing.take_cues();
    require(cues.size() == 1 && cues[0].name == "mm_mower_stop", "turning the key off plays the engine stopping");
    for (int k = 0; k < 300; ++k)
        mowing.advance(1.0 / 30, false);
    require(mowing.mower().pose().x == parked.x && mowing.mower().pose().y == parked.y && mowing.mower().progress() == cut,
            "with the engine off the mower stands still and cuts nothing");
    static_cast<void>(mowing.grab(parked.x, parked.y));
    mowing.steer(parked.x + 2, parked.y + 2);
    for (int k = 0; k < 60; ++k)
        mowing.advance(1.0 / 30, false);
    require(mowing.mower().pose().x == parked.x, "nor can it be dragged about with the engine off");
    mowing.let_go();
    mowing.set_engine(true);
    cues = mowing.take_cues();
    require(cues.size() == 1 && cues[0].name == "mm_mower_start", "turning it on plays the engine starting");
    for (int k = 0; k < 45; ++k)
        mowing.advance(1.0 / 30, false);
    require(mowing.mower().pose().x == parked.x && mowing.mower().pose().y == parked.y, "it does not move while it is starting");
    for (int k = 0; k < 300; ++k)
        mowing.advance(1.0 / 30, false);
    require(mowing.mower().progress() > cut, "and then it carries on mowing");
}

// A garden with a flower bed, and where it is.
bool bed_in(std::uint64_t seed, mm::Bed& bed) {
    const mm::Garden garden = mm::make_garden(seed);
    if (garden.beds.empty() || garden.plants.empty())
        return false;
    bed = garden.beds[0];
    return true;
}

// Nothing solid (a tree, a prop) on the straight line between two lawn points.
bool clear_line(const mm::Garden& garden, double x0, double y0, double x1, double y1) {
    const int steps = static_cast<int>(std::hypot(x1 - x0, y1 - y0) / 0.05) + 1;
    for (int k = 0; k <= steps; ++k) {
        const double x = x0 + (x1 - x0) * k / steps;
        const double y = y0 + (y1 - y0) * k / steps;
        for (double dx = -0.6; dx <= 0.6; dx += 0.3)
            for (double dy = -0.6; dy <= 0.6; dy += 0.3) {
                const int cx = std::clamp(static_cast<int>((x + dx) / mm::lawn_cell), 0, mm::lawn_cells_x - 1);
                const int cy = std::clamp(static_cast<int>((y + dy) / mm::lawn_cell), 0, mm::lawn_cells_y - 1);
                if (garden.solid[static_cast<std::size_t>(cy) * static_cast<std::size_t>(mm::lawn_cells_x) + static_cast<std::size_t>(cx)] != 0)
                    return false;
            }
    }
    return true;
}

void test_driving_over_a_bed() {
    // A garden with a bed the mower, once it has worked a while, can be driven straight at.
    std::uint64_t seed = 1;
    mm::Bed bed{};
    std::unique_ptr<mm::Mowing> found{};
    for (; seed < 80 && !found; ++seed) {
        if (!bed_in(seed, bed))
            continue;
        std::unique_ptr<mm::Mowing> trial = std::make_unique<mm::Mowing>(seed, mm::Livery::orange_h);
        for (int k = 0; k < 3060; ++k)
            (*trial).advance(1.0 / 30, false);
        const coverage::Pose& at = (*trial).mower().pose();
        if (clear_line((*trial).garden(), at.x, at.y, bed.x, bed.y)) {
            found = std::move(trial);
            break;
        }
    }
    require(found != nullptr, "some garden has a bed in clear sight of the mower");
    mm::Mowing& mowing = *found;
    // The mower on its own never sets a wheel on the mulch.
    bool trampled = false;
    for (const std::uint8_t cell : mowing.trampled())
        trampled = trampled || cell != 0;
    require(!trampled && mowing.granny().state == mm::GrannyState::away, "the mower's own mind keeps off the beds");
    // A hand on the wheel drives it straight across the bed.
    const coverage::Pose& pose = mowing.mower().pose();
    require(mowing.grab(pose.x, pose.y), "the mower can be grabbed");
    static_cast<void>(mowing.take_cues());
    double time = 0;
    bool chewed = false;
    bool heard = false;
    bool heard_granny = false;
    while (time < 150 && (mowing.granny().state == mm::GrannyState::away || !chewed)) {  // the lawn is 32 m across
        mowing.steer(bed.x, bed.y);
        mowing.advance(1.0 / 30, false);
        time += 1.0 / 30;
        chewed = chewed || mowing.wreck() > 0.3;
        const std::vector<mm::Cue> cues = mowing.take_cues();
        heard = heard || has_cue(cues, "mm_chipper");
        heard_granny = heard_granny || has_cue(cues, "mm_granny_shout");
    }
    int crushed = 0;
    for (const mm::Plant& plant : mowing.garden().plants)
        crushed += plant.crushed ? 1 : 0;
    require(crushed > 0, "driving over the bed destroys its flowers");
    require(chewed && heard, "the deck chews through them, loudly");
    trampled = false;
    for (const std::uint8_t cell : mowing.trampled())
        trampled = trampled || cell != 0;
    require(trampled, "the mulch it crossed is churned");
    require(mowing.granny().state != mm::GrannyState::away, "and the old lady comes out");
    bool shouted = heard_granny;
    require(shouted, "shouting as she does");
    // Sitting still in her way, she catches up.
    mowing.steer(pose.x, pose.y);
    for (int k = 0; k < 1500 && !mowing.caught(); ++k)
        mowing.advance(1.0 / 30, false);
    require(mowing.caught(), "she catches a mower that stands still");
    // A new go at the same garden: dodge her for half a minute instead.
    mm::Mowing again(seed, mm::Livery::orange_h);
    for (int k = 0; k < 60; ++k)
        again.advance(1.0 / 30, false);
    require(again.grab(again.mower().pose().x, again.mower().pose().y), "grabbed again");
    for (int k = 0; k < 900 && again.granny().state == mm::GrannyState::away; ++k) {
        again.steer(bed.x, bed.y);
        again.advance(1.0 / 30, false);
    }
    require(again.granny().state != mm::GrannyState::away, "she comes out again");
    // Dodge as a player would: always off towards open lawn away from her, keeping clear
    // of the walls so as not to be cornered.
    bool fled = false;
    for (int k = 0; k < 2400 && !again.caught() && !fled; ++k) {
        const coverage::Pose& at = again.mower().pose();
        double best_x = at.x;
        double best_y = at.y;
        double best = -1e9;
        for (int d = 0; d < 24; ++d) {
            const double angle = d * 2 * 3.14159265358979 / 24;
            const double x = at.x + std::cos(angle) * 2.5;
            const double y = at.y + std::sin(angle) * 2.5;
            if (x < 1.0 || y < 1.0 || x > mm::lawn_width - 1.0 || y > mm::lawn_height - 1.0)
                continue;
            const double wall = std::min(std::min(x, mm::lawn_width - x), std::min(y, mm::lawn_height - y));
            const double score = std::hypot(x - again.granny().x, y - again.granny().y) + 0.6 * std::min(wall, 4.0) +
                                 0.8 * std::cos(angle - at.heading);
            if (score > best) {
                best = score;
                best_x = x;
                best_y = y;
            }
        }
        again.steer(best_x, best_y);
        again.advance(1.0 / 30, false);
        fled = again.granny().state == mm::GrannyState::fleeing || again.granny().state == mm::GrannyState::startled;
    }
    if (!(fled && !again.caught()))
        std::fprintf(stderr, "caught %d state %d chase %.1f at %.1f,%.1f mower %.1f,%.1f\n", again.caught() ? 1 : 0, static_cast<int>(again.granny().state), again.granny().chase,
                     again.granny().x, again.granny().y, again.mower().pose().x, again.mower().pose().y);
    require(fled && !again.caught(), "dodged for long enough, she is seen off by a gnome");
    require(again.guard().state != mm::GnomeState::hidden, "who is up and looking");
    for (int k = 0; k < 900 && again.granny().state != mm::GrannyState::away; ++k)
        again.advance(1.0 / 30, false);
    require(again.granny().state == mm::GrannyState::away, "and she runs off the lawn");
    // Let go on the bed, the mower takes itself back to the grass and carries on.
    again.steer(bed.x, bed.y);
    for (int k = 0; k < 400; ++k)
        again.advance(1.0 / 30, false);
    again.let_go();
    for (int k = 0; k < 600; ++k)
        again.advance(1.0 / 30, false);
    require(mm::open_lawn(again.garden(), again.mower().pose().x, again.mower().pose().y), "let go on a bed, it finds its own way back to the grass");
}

void test_striking_a_tree() {
    std::uint64_t seed = 1;
    while (mm::make_garden(seed).trees.empty())
        ++seed;
    mm::Mowing mowing(seed, mm::Livery::orange_h);
    for (int k = 0; k < 30; ++k)
        mowing.advance(1.0 / 30, false);
    // Drive at the nearest tree; whichever tree the mower meets, it shakes.
    std::size_t nearest = 0;
    for (std::size_t t = 1; t < mowing.garden().trees.size(); ++t)
        if (std::hypot(mowing.garden().trees[t].x - mowing.mower().pose().x, mowing.garden().trees[t].y - mowing.mower().pose().y) <
            std::hypot(mowing.garden().trees[nearest].x - mowing.mower().pose().x, mowing.garden().trees[nearest].y - mowing.mower().pose().y))
            nearest = t;
    const mm::Tree& tree = mowing.garden().trees[nearest];
    require(mowing.grab(mowing.mower().pose().x, mowing.mower().pose().y), "grabbed");
    std::size_t leaves = 0;
    std::size_t struck = nearest;
    bool shaken = false;
    for (int k = 0; k < 1800 && !shaken; ++k) {
        mowing.steer(tree.x, tree.y);
        mowing.advance(1.0 / 30, false);
        for (const mm::Particle& p : mowing.particles())
            leaves += p.kind == mm::ParticleKind::leaf ? 1 : 0;
        for (std::size_t t = 0; t < mowing.garden().trees.size(); ++t)
            if (mowing.tree_shake(t) > 0) {
                shaken = true;
                struck = t;
            }
    }
    require(shaken && mowing.tree_shake(struck) > 0, "driving into a tree shakes it");
    for (int k = 0; k < 10; ++k)
        mowing.advance(1.0 / 30, false);
    for (const mm::Particle& p : mowing.particles())
        leaves += p.kind == mm::ParticleKind::leaf ? 1 : 0;
    require(leaves > 0, "and leaves come down");
}

void test_bees_and_birds() {
    std::uint64_t seed = 1;
    while (mm::make_garden(seed).trees.empty() || mm::make_garden(seed).plants.empty())
        ++seed;
    mm::Mowing mowing(seed, mm::Livery::orange_h);
    mowing.set_engine(false);
    bool bee_visited = false;
    bool bird_sang = false;
    for (int k = 0; k < 30 * 240 && !(bee_visited && bird_sang); ++k) {
        mowing.advance(1.0 / 30, false);
        for (const mm::Bee& bee : mowing.bees())
            bee_visited = bee_visited || bee.state == mm::BeeState::visiting;
        for (const mm::Bird& bird : mowing.birds())
            bird_sang = bird_sang || bird.singing;
    }
    require(bee_visited, "bees come to the flowers");
    require(bird_sang, "birds come to the trees and sing");
    // The mower started up and driven at them sends them off.
    mowing.set_engine(true);
    static_cast<void>(mowing.grab(mowing.mower().pose().x, mowing.mower().pose().y));
    int scattered = 0;
    for (int k = 0; k < 30 * 120; ++k) {
        double tx = -1;
        double ty = -1;
        for (const mm::Bee& bee : mowing.bees()) {
            if (bee.state == mm::BeeState::visiting) {
                tx = bee.x;
                ty = bee.y;
            }
        }
        for (const mm::Bird& bird : mowing.birds()) {
            if (bird.state == mm::BirdState::perched) {
                tx = bird.x;
                ty = bird.y;
            }
        }
        if (tx >= 0)
            mowing.steer(std::clamp(tx, 0.8, mm::lawn_width - 0.8), std::clamp(ty, 0.8, mm::lawn_height - 0.8));
        mowing.advance(1.0 / 30, false);
        for (const mm::Bee& bee : mowing.bees())
            scattered += bee.state == mm::BeeState::leaving && std::hypot(bee.x - mowing.mower().pose().x, bee.y - mowing.mower().pose().y) < 2.5 ? 1 : 0;
        for (const mm::Bird& bird : mowing.birds())
            scattered += bird.state == mm::BirdState::leaving && std::hypot(bird.x - mowing.mower().pose().x, bird.y - mowing.mower().pose().y) < 3.5 ? 1 : 0;
    }
    require(scattered > 0, "and they leave when the mower comes near");
}

int main() {
    test_gardens();
    test_engine();
    test_engine_switch();
    test_driving_over_a_bed();
    test_striking_a_tree();
    test_bees_and_birds();
    test_takeover();
    test_deterministic();
    test_gnome();
    test_retained_lawn();
    test_mowing_completes();
    std::printf("mowing man: %d checks passed\n", checks);
    return 0;
}
