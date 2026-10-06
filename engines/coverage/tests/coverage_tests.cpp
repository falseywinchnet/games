// The coverage engine's acceptance tests: random gardens are mowed completely,
// safely, deterministically, without standing about, and with only what the
// scanner could have seen; someone can take the controls; a goal is driven to.
#include "coverage.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

constexpr double pi = 3.14159265358979323846;

int checks = 0;
void require(bool condition, const char* what) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        std::exit(1);
    }
}

struct Random {
    std::uint64_t state{};
    double unit() {
        state += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state;
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        z ^= z >> 31U;
        return static_cast<double>(z >> 11U) * (1.0 / 9007199254740992.0);
    }
    double range(double low, double high) {
        return low + (high - low) * unit();
    }
};

struct Garden {
    coverage::FieldSetup field{};
    std::vector<std::uint8_t> obstacles{};
    int cells_x{};
    int cells_y{};
    bool blocked(double x, double y, double radius) const {
        for (int cy = std::max(0, static_cast<int>((y - radius) / field.cell)); cy <= std::min(cells_y - 1, static_cast<int>((y + radius) / field.cell)); ++cy)
            for (int cx = std::max(0, static_cast<int>((x - radius) / field.cell)); cx <= std::min(cells_x - 1, static_cast<int>((x + radius) / field.cell)); ++cx) {
                if (obstacles[static_cast<std::size_t>(cy * cells_x + cx)] == 0)
                    continue;
                const double nx = std::clamp(x, cx * field.cell, (cx + 1) * field.cell);
                const double ny = std::clamp(y, cy * field.cell, (cy + 1) * field.cell);
                if (std::hypot(nx - x, ny - y) < radius)
                    return true;
            }
        return false;
    }
};

Garden empty_garden(double width, double height, double cell) {
    Garden garden{};
    garden.field.width = width;
    garden.field.height = height;
    garden.field.cell = cell;
    garden.cells_x = static_cast<int>(std::lround(width / cell));
    garden.cells_y = static_cast<int>(std::lround(height / cell));
    garden.obstacles.assign(static_cast<std::size_t>(garden.cells_x * garden.cells_y), 0);
    return garden;
}

// Flower beds (ellipses) and a few boxes, kept off a clear start corner.
Garden make_garden(std::uint64_t seed, int beds) {
    Garden garden = empty_garden(16, 10, 0.05);
    Random random{seed};
    for (int b = 0; b < beds; ++b) {
        const double cx = random.range(3, 14.5);
        const double cy = random.range(2, 9);
        const double rx = random.range(0.4, 1.4);
        const double ry = random.range(0.4, 1.1);
        const bool box = random.unit() < 0.3;
        for (int y = 0; y < garden.cells_y; ++y)
            for (int x = 0; x < garden.cells_x; ++x) {
                const double px = (x + 0.5) * garden.field.cell - cx;
                const double py = (y + 0.5) * garden.field.cell - cy;
                const bool inside = box ? std::abs(px) < rx && std::abs(py) < ry : (px * px) / (rx * rx) + (py * py) / (ry * ry) < 1;
                if (inside)
                    garden.obstacles[static_cast<std::size_t>(y * garden.cells_x + x)] = 1;
            }
    }
    return garden;
}

// What one whole mowing looked like from outside.
struct Outcome {
    double time{};
    double longest_stop{};
    bool finished{};
    bool safe{true};
    bool inside{true};
    bool honest{true};
    double progress{};
    int gear_changes{};
    int bumps{};
};

// The body is a 0.52 m circle tested against 0.1 m planner cells; against the
// 0.05 m obstacles handed in, that guarantees this much.
constexpr double sure_clearance = 0.45;

Outcome mow_all(const Garden& garden, coverage::Mower& mower, const coverage::Machine& machine, double limit) {
    Outcome outcome{};
    double stopped = 0;
    while (!mower.finished() && outcome.time < limit) {
        const coverage::Step step = mower.update(0.05);
        outcome.time += 0.05;
        const coverage::Pose& pose = mower.pose();
        outcome.safe = outcome.safe && !garden.blocked(pose.x, pose.y, sure_clearance);
        outcome.inside = outcome.inside && pose.x >= machine.body_radius && pose.y >= machine.body_radius &&
                         pose.x <= garden.field.width - machine.body_radius && pose.y <= garden.field.height - machine.body_radius;
        stopped = step.speed < 0.02 && !mower.finished() ? stopped + 0.05 : 0;
        outcome.longest_stop = std::max(outcome.longest_stop, stopped);
    }
    // Everything the machine believes solid really is: a bed, or the fence round the edge.
    const int nx = static_cast<int>(std::lround(garden.field.width / 0.1));
    const int ny = static_cast<int>(std::lround(garden.field.height / 0.1));
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            if (mower.belief_at((i + 0.5) * 0.1, (j + 0.5) * 0.1) != coverage::Belief::occupied)
                continue;
            const bool fence = i < 2 || j < 2 || i >= nx - 2 || j >= ny - 2;
            outcome.honest = outcome.honest && (fence || garden.blocked((i + 0.5) * 0.1, (j + 0.5) * 0.1, 0.08));
        }
    outcome.finished = mower.finished();
    outcome.progress = mower.progress();
    outcome.gear_changes = mower.gear_changes();
    outcome.bumps = mower.bumps();
    return outcome;
}

void test_empty_field() {
    const Garden garden = empty_garden(16, 10, 0.05);
    coverage::Machine machine{};
    coverage::Mower mower(garden.field, machine, garden.obstacles, {1.2, 1.2, pi / 2});
    require(mower.cells_x() == 320 && mower.cells_y() == 200, "the cut grid is 5 cm cells");
    require(mower.cut().size() == static_cast<std::size_t>(320 * 200), "and covers the field");
    const Outcome outcome = mow_all(garden, mower, machine, 1500);
    std::printf("empty field: %.0f s, %.2f%% cut, %d changes of gear, %d bumps, longest stop %.2f s\n", outcome.time, outcome.progress * 100,
                outcome.gear_changes, outcome.bumps, outcome.longest_stop);
    require(outcome.finished, "an empty field is finished");
    require(outcome.progress > 0.97, "an empty field is mown");
    require(outcome.inside, "the machine stays inside the fence");
    require(outcome.bumps == 0, "the fence is seen, not felt");
    require(mower.phase() == coverage::Phase::finished && mower.path().empty(), "and then it rests");
    const coverage::Step rest = mower.update(0.05);
    require(rest.speed == 0 && rest.newly_cut == 0, "a finished mower stands still");
}

void test_gardens() {
    for (std::uint64_t seed = 1; seed <= 8; ++seed) {
        const Garden garden = make_garden(seed, 2 + static_cast<int>(seed % 5));
        coverage::Machine machine{};
        coverage::Mower mower(garden.field, machine, garden.obstacles, {0.8, 0.8, 0});
        const Outcome outcome = mow_all(garden, mower, machine, 1500);
        std::printf("garden %llu: %.0f s, %.2f%% cut, %d changes of gear, %d bumps, longest stop %.2f s\n", static_cast<unsigned long long>(seed),
                    outcome.time, outcome.progress * 100, outcome.gear_changes, outcome.bumps, outcome.longest_stop);
        require(outcome.finished, "every garden is finished");
        require(outcome.progress > 0.95, "nearly all the grass it is expected to cut is cut");
        require(outcome.safe, "the machine never overlaps an obstacle");
        require(outcome.inside, "the machine stays inside the fence");
        require(outcome.honest, "the belief map holds no invented obstacles");
        require(outcome.longest_stop < 3.0, "it never stands about while there is work");
    }
}

// Other sizes of field, and obstacles handed in on a coarser grid.
void test_other_fields() {
    Garden garden = empty_garden(12, 8, 0.1);
    for (int y = 0; y < garden.cells_y; ++y)
        for (int x = 0; x < garden.cells_x; ++x)
            if (std::hypot((x + 0.5) * 0.1 - 7.0, (y + 0.5) * 0.1 - 4.2) < 0.9)
                garden.obstacles[static_cast<std::size_t>(y * garden.cells_x + x)] = 1;
    coverage::Machine machine{};
    coverage::Mower mower(garden.field, machine, garden.obstacles, {1.2, 1.2, pi / 2});
    require(mower.cells_x() == 240 && mower.cells_y() == 160, "the cut grid stays at 5 cm");
    const Outcome outcome = mow_all(garden, mower, machine, 1500);
    std::printf("12 x 8 field: %.0f s, %.2f%% cut\n", outcome.time, outcome.progress * 100);
    require(outcome.finished && outcome.progress > 0.95, "a smaller field is mown");
    require(outcome.safe && outcome.inside && outcome.honest, "safely and honestly");
    require(!mower.cut_at(7.0, 4.2), "the middle of a bed is never cut");
}

void test_scanner() {
    Garden garden = empty_garden(16, 10, 0.05);
    // a wall across the view, 2.5 m ahead
    for (int y = 0; y < garden.cells_y; ++y)
        for (int x = 0; x < garden.cells_x; ++x) {
            const double px = (x + 0.5) * 0.05;
            const double py = (y + 0.5) * 0.05;
            if (px > 10.5 && px < 10.8 && py > 4.4 && py < 5.6)
                garden.obstacles[static_cast<std::size_t>(y * garden.cells_x + x)] = 1;
        }
    const coverage::Machine machine{};
    coverage::Mower mower(garden.field, machine, garden.obstacles, {8, 5, 0});
    require(mower.belief_at(9.0, 5.0) == coverage::Belief::free, "the scanner sees ahead");
    require(mower.belief_at(7.0, 5.0) == coverage::Belief::unknown, "and nothing behind");
    require(mower.belief_at(10.55, 5.0) == coverage::Belief::occupied, "it sees the face of the wall");
    require(mower.belief_at(11.5, 5.0) == coverage::Belief::unknown, "and nothing through it");
    // along one of its rays (the 22nd of 61), past the wall's end and 3.6 m out
    const double ray = (21.0 / 60.0 - 0.5) * machine.lidar_field;
    require(mower.belief_at(8.0 + 3.6 * std::cos(ray), 5.0 + 3.6 * std::sin(ray)) == coverage::Belief::free, "it reaches furthest near straight ahead");
    require(mower.belief_at(8.0 + 3.0 * std::cos(0.95), 5.0 + 3.0 * std::sin(0.95)) == coverage::Belief::unknown, "and less far to the side");
    require(mower.belief_at(-1, 5) == coverage::Belief::occupied, "outside the field counts as solid");
    require(mower.sees(9.2, 5.3) && !mower.sees(6.5, 5.0), "sees() agrees with the fan");
    require(mower.sees(11.5, 3.9) && !mower.sees(11.5, 5.0), "and with what stands in the way");
    require(!mower.sees(8.0 + 3.0 * std::cos(0.95), 5.0 + 3.0 * std::sin(0.95)), "and with the fan's shorter sides");
    require(!mower.sees(8.0 + 6.0, 5.0 - 2.0), "and with its reach");
}

void test_manual_and_goal() {
    const Garden garden = make_garden(5, 3);
    const coverage::Machine machine{};
    coverage::Mower mower(garden.field, machine, garden.obstacles, {0.8, 0.8, 0});
    for (int k = 0; k < 200; ++k)
        static_cast<void>(mower.update(0.05));
    require(mower.phase() != coverage::Phase::manual, "left alone, it drives itself");
    // someone drags it to open lawn
    const double before = std::hypot(mower.pose().x - 3.0, mower.pose().y - 8.6);
    bool clear = true;
    double nearest = before;
    for (int k = 0; k < 400; ++k) {
        static_cast<void>(mower.drive_toward(3.0, 8.6, 0.05));
        clear = clear && !garden.blocked(mower.pose().x, mower.pose().y, sure_clearance);
        nearest = std::min(nearest, std::hypot(mower.pose().x - 3.0, mower.pose().y - 8.6));
    }
    require(mower.phase() == coverage::Phase::manual && !mower.finished(), "someone else is driving");
    require(nearest < 0.2 && nearest < before, "it rolls to where it is dragged");
    require(clear, "manual driving stays clear");
    // dragged at a bed, it stops against it rather than going through
    bool touched = false;
    for (int k = 0; k < 600; ++k) {
        const coverage::Step step = mower.drive_toward(13.5, 2.0, 0.05);
        touched = touched || step.bumped;
        clear = clear && !garden.blocked(mower.pose().x, mower.pose().y, sure_clearance);
    }
    require(clear, "nor can it be dragged through anything");
    static_cast<void>(touched);
    mower.resume();
    static_cast<void>(mower.update(0.05));
    require(mower.phase() != coverage::Phase::manual, "letting go hands it back");
    // a goal comes next, whatever it was doing
    mower.set_goal(2.0, 8.5);
    static_cast<void>(mower.update(0.05));
    require(mower.phase() == coverage::Phase::goal, "a goal comes first");
    bool reached = false;
    double time = 0;
    for (int k = 0; k < 4000 && !reached; ++k) {
        reached = mower.update(0.05).goal_reached;
        time += 0.05;
        clear = clear && !garden.blocked(mower.pose().x, mower.pose().y, sure_clearance);
    }
    std::printf("goal reached in %.1f s, %.2f m from the deck's centre\n", time, std::hypot(mower.pose().x - 2.0, mower.pose().y - 8.5));
    require(reached, "a goal is reached");
    require(std::hypot(mower.pose().x - 2.0, mower.pose().y - 8.5) < 0.36, "the deck ends over the goal");
    require(clear, "and the way there was clear");
    static_cast<void>(mower.update(0.05));
    require(mower.phase() != coverage::Phase::goal, "then it carries on");
    // a goal can be called off
    mower.set_goal(14.0, 8.5);
    static_cast<void>(mower.update(0.05));
    require(mower.phase() == coverage::Phase::goal, "another goal");
    mower.clear_goal();
    bool again = false;
    for (int k = 0; k < 40; ++k)
        again = mower.update(0.05).goal_reached || again;
    require(mower.phase() != coverage::Phase::goal && !again, "called off");
    // and the lawn still gets finished
    const Outcome outcome = mow_all(garden, mower, machine, 1500);
    require(outcome.finished && outcome.progress > 0.95, "interruptions do not stop the lawn being mown");
    // a goal wakes a finished mower
    mower.set_goal(8.0, 1.5);
    require(!mower.finished(), "a goal wakes it");
    reached = false;
    for (int k = 0; k < 4000 && !reached; ++k)
        reached = mower.update(0.05).goal_reached;
    require(reached, "a finished mower still goes to a goal");
    for (int k = 0; k < 2000 && !mower.finished(); ++k)
        static_cast<void>(mower.update(0.05));
    require(mower.finished(), "and settles again");
}

void test_deterministic() {
    const Garden garden = make_garden(7, 4);
    coverage::Mower a(garden.field, coverage::Machine{}, garden.obstacles, {0.8, 0.8, 0});
    coverage::Mower b(garden.field, coverage::Machine{}, garden.obstacles, {0.8, 0.8, 0});
    coverage::Mower c(garden.field, coverage::Machine{}, garden.obstacles, {0.8, 0.8, 0});
    for (int k = 0; k < 3000; ++k) {
        static_cast<void>(a.update(0.05));
        static_cast<void>(b.update(0.03));
        static_cast<void>(b.update(0.02));
        static_cast<void>(c.update(0.05));
    }
    require(a.pose().x == c.pose().x && a.pose().y == c.pose().y && a.pose().heading == c.pose().heading && a.progress() == c.progress(),
            "two runs of the same garden are identical");
    require(a.cut() == b.cut() && a.pose().x == b.pose().x && a.pose().y == b.pose().y,
            "the same garden is mowed the same way, whatever the frame rate");
}

void test_cutting() {
    const Garden garden = empty_garden(16, 10, 0.05);
    coverage::Mower mower(garden.field, coverage::Machine{}, garden.obstacles, {1.2, 1.2, pi / 2});
    require(!mower.cut_at(8.0, 5.0), "grass starts uncut");
    const int first = mower.cut_under({8.0, 5.0, 0});
    require(first > 250 && first <= 280, "a deck's worth of cells is cut");
    require(mower.cut_at(8.0, 5.0) && mower.cut_at(8.3, 5.45) && !mower.cut_at(8.45, 5.0), "under the deck and nowhere else");
    require(mower.cut_under({8.0, 5.0, 0}) == 0, "cut grass is not cut again");
    int reported = 0;
    for (int k = 0; k < 100; ++k)
        reported += mower.update(0.05).newly_cut;
    int counted = 0;
    for (const std::uint8_t cell : mower.cut())
        counted += cell;
    require(counted == first + reported, "every newly cut cell is reported once");
}

} // namespace

int main() {
    test_scanner();
    test_cutting();
    test_manual_and_goal();
    test_deterministic();
    test_empty_field();
    test_other_fields();
    test_gardens();
    std::printf("coverage engine: %d checks passed\n", checks);
    return 0;
}
