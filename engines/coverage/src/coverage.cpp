#include "coverage.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#ifdef COVERAGE_PARITY_MATH
// A test build can swap in its own trigonometry so that a run can be compared
// step for step with the reference model, which is given the same functions.
#include "parity_math.hpp"
#endif

namespace coverage {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double grid = 0.1;         // planner cell, metres
constexpr double fine = 0.05;        // cut cell, metres
constexpr double pose_gap = 0.62;    // how close a pose may sit to anything solid
constexpr double pass_gap = 0.60;    // ground a route may cross
constexpr double lap_gap = 0.64;     // where a lap runs: a little outside the closest pose, so curves and smoothing never rub
constexpr double verge = 0.5;        // grass this close to the lawn's edge is beyond what the mower has to cut
constexpr double track_step = 0.15;  // spacing of track points
constexpr double germ_far = 7;       // germ spacing in boring space
constexpr double germ_near = 1.8;    // and in structured space
constexpr double diagonal = pi / 4;
constexpr int tier_outer = 0;
constexpr int tier_outer2 = 1;
constexpr int tier_obstacle = 2;
constexpr int tier_stripe = 3;
constexpr int tier_spot = 4;
constexpr int tier_escape = 5;
constexpr double tier_cost[5] = {0, 2, 3, 6, 9};
constexpr double goal_reach = 0.35;  // the deck is over a goal this close to its centre
constexpr double far_off = 1e9;
constexpr int step_x[8] = {1, -1, 0, 0, 1, 1, -1, -1};
constexpr int step_y[8] = {0, 0, 1, -1, 1, -1, 1, -1};

#ifdef COVERAGE_PARITY_MATH
double sine(double a) {
    return parity::sine(a);
}
double cosine(double a) {
    return parity::cosine(a);
}
double angle_of(double y, double x) {
    return parity::angle_of(y, x);
}
double arcsine(double a) {
    return parity::arcsine(a);
}
double length(double x, double y) {
    return parity::length(x, y);
}
#else
double sine(double a) {
    return std::sin(a);
}
double cosine(double a) {
    return std::cos(a);
}
double angle_of(double y, double x) {
    return std::atan2(y, x);
}
double arcsine(double a) {
    return std::asin(a);
}
double length(double x, double y) {
    return std::hypot(x, y);
}
#endif

double wrap(double angle) {
    while (angle > pi)
        angle -= 2 * pi;
    while (angle < -pi)
        angle += 2 * pi;
    return angle;
}

double clamp(double value, double low, double high) {
    return value < low ? low : (value > high ? high : value);
}

// Larger and smaller of two, with equal zeros resolved as the reference model does.
double larger(double a, double b) {
    if (a > b)
        return a;
    if (b > a)
        return b;
    return a == 0 ? a + b : a;
}

double smaller(double a, double b) {
    if (a < b)
        return a;
    if (b < a)
        return b;
    return a == 0 ? -(-a - b) : a;
}

// Nearest whole number, halves upward; a small negative rounds to negative zero.
double nearest(double value) {
    const double result = std::floor(value + 0.5);
    if (result == 0 && value < 0)
        return -0.0;
    return result;
}

// 0 .. 2 pi
double turn_of(double angle) {
    const double two = 2 * pi;
    return std::fmod(std::fmod(angle, two) + two, two);
}

int axis_cell(double value, int count) {
    const double q = value / grid;
    if (!(q >= 1))
        return 0;
    if (q >= count)
        return count - 1;
    return static_cast<int>(q);
}

// Grids of distances are stored single but always read double, so that sums and
// comparisons come out the same wherever this is built.
double read(const std::vector<float>& field, int cell) {
    return static_cast<double>(field[static_cast<std::size_t>(cell)]);
}

void write(std::vector<float>& field, int cell, double value) {
    field[static_cast<std::size_t>(cell)] = static_cast<float>(value);
}

// A binary heap of (cost, cell), smallest cost first, for the wavefronts.
struct Heap {
    std::vector<float> keys{};
    std::vector<int> cells{};
    int count{};
    double top_key{};

    explicit Heap(int capacity) : keys(static_cast<std::size_t>(capacity) + 1), cells(static_cast<std::size_t>(capacity) + 1) {
    }
    void push(double key, int cell) {
        if (static_cast<std::size_t>(count) + 1 >= keys.size()) {
            keys.resize(keys.size() * 2);
            cells.resize(cells.size() * 2);
        }
        std::size_t i = static_cast<std::size_t>(++count);
        while (i > 1) {
            const std::size_t parent = i >> 1U;
            if (static_cast<double>(keys[parent]) <= key)
                break;
            keys[i] = keys[parent];
            cells[i] = cells[parent];
            i = parent;
        }
        keys[i] = static_cast<float>(key);
        cells[i] = cell;
    }
    int pop() {
        const int top = cells[1];
        top_key = static_cast<double>(keys[1]);
        const std::size_t size = static_cast<std::size_t>(count);
        const float key = keys[size];
        const int cell = cells[size];
        --count;
        const std::size_t left = static_cast<std::size_t>(count);
        std::size_t i = 1;
        for (;;) {
            std::size_t child = i * 2;
            if (child > left)
                break;
            if (child < left && keys[child + 1] < keys[child])
                ++child;
            if (keys[child] >= key)
                break;
            keys[i] = keys[child];
            cells[i] = cells[child];
            i = child;
        }
        keys[i] = key;
        cells[i] = cell;
        return top;
    }
};

// One pass of the distance transform along a line of `count` samples.
void transform_line(int count, const std::vector<double>& g, std::vector<int>& v, std::vector<double>& z, std::vector<double>& out) {
    const double inf = 1e12;
    int k = 0;
    v[0] = 0;
    z[0] = -inf;
    z[1] = inf;
    for (int q = 1; q < count; ++q) {
        double s = 0;
        for (;;) {
            const int p = v[static_cast<std::size_t>(k)];
            s = ((g[static_cast<std::size_t>(q)] + static_cast<double>(q) * q) - (g[static_cast<std::size_t>(p)] + static_cast<double>(p) * p)) /
                (2.0 * q - 2.0 * p);
            if (s <= z[static_cast<std::size_t>(k)])
                --k;
            else
                break;
        }
        ++k;
        v[static_cast<std::size_t>(k)] = q;
        z[static_cast<std::size_t>(k)] = s;
        z[static_cast<std::size_t>(k) + 1] = inf;
    }
    k = 0;
    for (int q = 0; q < count; ++q) {
        while (z[static_cast<std::size_t>(k) + 1] < q)
            ++k;
        const int p = v[static_cast<std::size_t>(k)];
        out[static_cast<std::size_t>(q)] = static_cast<double>(q - p) * (q - p) + g[static_cast<std::size_t>(p)];
    }
}

// Exact Euclidean distance (metres) from every cell to the nearest set cell.
std::vector<float> distance_to(const std::vector<std::uint8_t>& mask, int nx, int ny) {
    const double inf = 1e12;
    const std::size_t cells = static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny);
    std::vector<double> f(cells);
    std::vector<float> d(cells);
    const std::size_t n = static_cast<std::size_t>(std::max(nx, ny));
    std::vector<int> v(n);
    std::vector<double> z(n + 1);
    std::vector<double> g(n);
    std::vector<double> out(n);
    for (int y = 0; y < ny; ++y) {
        for (int x = 0; x < nx; ++x)
            g[static_cast<std::size_t>(x)] = mask[static_cast<std::size_t>(y * nx + x)] != 0 ? 0 : inf;
        transform_line(nx, g, v, z, out);
        for (int x = 0; x < nx; ++x)
            f[static_cast<std::size_t>(y * nx + x)] = out[static_cast<std::size_t>(x)];
    }
    for (int x = 0; x < nx; ++x) {
        for (int y = 0; y < ny; ++y)
            g[static_cast<std::size_t>(y)] = f[static_cast<std::size_t>(y * nx + x)];
        transform_line(ny, g, v, z, out);
        for (int y = 0; y < ny; ++y)
            d[static_cast<std::size_t>(y * nx + x)] = static_cast<float>(std::sqrt(out[static_cast<std::size_t>(y)]) * grid);
    }
    return d;
}

// Orders germ candidates: where the ground wants most attention first, and among
// equals a fixed scatter, so that germs do not line up along the scan.
struct ByDemand {
    const std::vector<float>* demand{};
    bool operator()(int p, int q) const {
        const double dp = read(*demand, p);
        const double dq = read(*demand, q);
        if (dq - dp != 0)
            return dq - dp < 0;
        const std::uint32_t hp = static_cast<std::uint32_t>(static_cast<std::uint64_t>(p) * 2654435761ULL);
        const std::uint32_t hq = static_cast<std::uint32_t>(static_cast<std::uint64_t>(q) * 2654435761ULL);
        return hp < hq;
    }
};

} // namespace

Mower::Mower(FieldSetup field, Machine machine, std::vector<std::uint8_t> obstacles, Pose start)
    : field_(field), machine_(machine), pose_(start) {
    width_ = field_.width;
    height_ = field_.height;
    nx_ = std::max(8, static_cast<int>(std::lround(width_ / grid)));
    ny_ = std::max(8, static_cast<int>(std::lround(height_ / grid)));
    cells_ = nx_ * ny_;
    cells_x_ = nx_ * 2;
    cells_y_ = ny_ * 2;
    const std::size_t cells = static_cast<std::size_t>(cells_);
    // A planner cell is solid if any of the ground handed in under it is.
    const int given_x = std::max(1, static_cast<int>(std::lround(width_ / field_.cell)));
    const int given_y = std::max(1, static_cast<int>(std::lround(height_ / field_.cell)));
    obstacles.resize(static_cast<std::size_t>(given_x) * static_cast<std::size_t>(given_y), 0);
    truth_.assign(cells, 0);
    for (int j = 0; j < cells_y_; ++j) {
        for (int i = 0; i < cells_x_; ++i) {
            const int gx = std::min(given_x - 1, static_cast<int>((i + 0.5) * fine / field_.cell));
            const int gy = std::min(given_y - 1, static_cast<int>((j + 0.5) * fine / field_.cell));
            if (obstacles[static_cast<std::size_t>(gy) * static_cast<std::size_t>(given_x) + static_cast<std::size_t>(gx)] != 0)
                truth_[static_cast<std::size_t>((j >> 1) * nx_ + (i >> 1))] = 1;
        }
    }
    // The fence. It is part of the world, not of what the mower is told.
    for (int j = 0; j < ny_; ++j) {
        for (int i = 0; i < nx_; ++i) {
            if (i < 2 || j < 2 || i >= nx_ - 2 || j >= ny_ - 2)
                truth_[static_cast<std::size_t>(j * nx_ + i)] = 1;
        }
    }
    belief_.assign(cells, 0);
    cut_.assign(static_cast<std::size_t>(cells_x_) * static_cast<std::size_t>(cells_y_), 0);
    const std::vector<float> true_clear = distance_to(truth_, nx_, ny_);
    std::vector<float> inside(cells);
    for (int j = 0; j < ny_; ++j) {
        for (int i = 0; i < nx_; ++i) {
            const double x = (i + 0.5) * grid;
            const double y = (j + 0.5) * grid;
            write(inside, j * nx_ + i, smaller(smaller(x, width_ - x), smaller(y, height_ - y)));
        }
    }
    cuttable_.assign(cells, 0);
    for (int k = 0; k < cells_; ++k) {
        if (truth_[static_cast<std::size_t>(k)] == 0 && read(true_clear, k) >= 0.2 && read(inside, k) >= verge + 0.25) {
            cuttable_[static_cast<std::size_t>(k)] = 1;
            cuttable_count_ += 4;
        }
    }
    owner_.assign(cells, -1);
    frozen_.assign(cells, -1);
    frames_.push_back(diagonal);
    clear_ = inside;
    demand_.assign(cells, 0);
    grad_x_.assign(cells, 0);
    grad_y_.assign(cells, 0);
    fence_dist_.assign(cells, 99);
    edge_dist_.assign(cells, 99);
    outer_.assign(cells, 0);
    near_seen_.assign(cells, 0);
    shun_.assign(cells, 0);
    travel_.assign(cells, static_cast<float>(far_off));
    travel_from_.assign(cells, -1);
    crowd_.assign(cells, 0);
    grow_.assign(cells, 0);
    wary_.assign(cells, 0);
    sense();
    learn();
    next();
    publish_path();
}

// ---------------------------------------------------------------- the machine

int Mower::cell_of(double x, double y) const {
    return axis_cell(y, ny_) * nx_ + axis_cell(x, nx_);
}

// The scanner sees furthest straight ahead, like headlights: the short range at
// the edges of the fan, the long reach in the middle.
double Mower::range_at(double off) const {
    const double lobe = larger(0, cosine(off * pi / machine_.lidar_field));
    return machine_.lidar_range + (machine_.lidar_reach - machine_.lidar_range) * (lobe * lobe);
}

bool Mower::blocked(double x, double y) const {
    const double r = machine_.body_radius;
    if (x < r || y < r || x > width_ - r || y > height_ - r)
        return true;
    const int i0 = std::max(0, static_cast<int>((x - r) / grid));
    const int i1 = std::min(nx_ - 1, static_cast<int>((x + r) / grid));
    const int j0 = std::max(0, static_cast<int>((y - r) / grid));
    const int j1 = std::min(ny_ - 1, static_cast<int>((y + r) / grid));
    for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
            const std::vector<std::uint8_t>& ground = held_ && !hard_.empty() ? hard_ : truth_;
            if (ground[static_cast<std::size_t>(j * nx_ + i)] == 0)
                continue;
            const double dx = (i + 0.5) * grid - x;
            const double dy = (j + 0.5) * grid - y;
            if (dx * dx + dy * dy < r * r)
                return true;
        }
    }
    return false;
}

void Mower::sense() {
    const int rays = std::max(2, machine_.lidar_rays);
    bool changed = false;
    for (int k = 0; k < rays; ++k) {
        const double off = (static_cast<double>(k) / (rays - 1) - 0.5) * machine_.lidar_field;
        const double a = pose_.heading + off;
        const double ca = cosine(a);
        const double sa = sine(a);
        const double far = range_at(off);
        for (double t = 0; t <= far; t += 0.04) {
            const double x = pose_.x + ca * t;
            const double y = pose_.y + sa * t;
            if (x < 0 || y < 0 || x >= width_ || y >= height_)
                break;
            const int c = static_cast<int>(y / grid) * nx_ + static_cast<int>(x / grid);
            if (c >= cells_)
                continue;
            std::uint8_t& known = belief_[static_cast<std::size_t>(c)];
            if (truth_[static_cast<std::size_t>(c)] != 0) {
                if (known != 2) {
                    known = 2;
                    changed = true;
                }
                break;
            }
            if (known == 0) {
                known = 1;
                changed = true;
            }
        }
    }
    std::uint8_t& here = belief_[static_cast<std::size_t>(cell_of(pose_.x, pose_.y))];
    if (here == 0) {
        here = 1;
        changed = true;
    }
    if (changed)
        belief_dirty_ = true;
}

// Touching something teaches what is there, a little beyond the body all round.
void Mower::bump(double x, double y) {
    const double r = machine_.clearance + 0.12;
    const int j1 = std::min(ny_ - 1, static_cast<int>((y + r) / grid));
    const int i1 = std::min(nx_ - 1, static_cast<int>((x + r) / grid));
    for (int j = std::max(0, static_cast<int>((y - r) / grid)); j <= j1; ++j) {
        for (int i = std::max(0, static_cast<int>((x - r) / grid)); i <= i1; ++i) {
            const std::size_t k = static_cast<std::size_t>(j * nx_ + i);
            if (truth_[k] != 0 && belief_[k] != 2) {
                belief_[k] = 2;
                belief_dirty_ = true;
            }
        }
    }
    touched_ = true;
}

// Cuts what is under the deck. Ground mown while a germ owns it keeps that owner.
int Mower::mow(double x, double y, double heading) {
    const double ch = cosine(heading);
    const double sh = sine(heading);
    const double reach = 0.62;
    const double half_l = machine_.deck_length / 2;
    const double half_w = machine_.deck_width / 2;
    const int i0 = std::max(0, static_cast<int>((x - reach) / fine));
    const int i1 = std::min(cells_x_ - 1, static_cast<int>((x + reach) / fine));
    const int j0 = std::max(0, static_cast<int>((y - reach) / fine));
    const int j1 = std::min(cells_y_ - 1, static_cast<int>((y + reach) / fine));
    int count = 0;
    for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
            const double dx = (i + 0.5) * fine - x;
            const double dy = (j + 0.5) * fine - y;
            const double along = dx * ch + dy * sh;
            const double across = -dx * sh + dy * ch;
            if (!(along > -half_l && along < half_l && across > -half_w && across < half_w))
                continue;
            const std::size_t k = static_cast<std::size_t>(j * cells_x_ + i);
            if (cut_[k] != 0)
                continue;
            const std::size_t coarse = static_cast<std::size_t>((j >> 1) * nx_ + (i >> 1));
            cut_[k] = 1;
            ++count;
            if (cuttable_[coarse] != 0)
                ++cut_count_;
            if (owner_[coarse] >= 0 && frozen_[coarse] < 0)
                frozen_[coarse] = owner_[coarse];
        }
    }
    return count;
}

int Mower::cut_under(const Pose& pose) {
    return mow(pose.x, pose.y, pose.heading);
}

// Under someone's hand the mower simply rolls toward the pointer, as tightly as
// its turning circle allows. It steers in arcs and cannot turn on the spot.
bool Mower::steer(double heading, double distance, double speed, double dt) {
    const double e = wrap(heading - pose_.heading);
    const double v = smaller(speed * clamp(1 - std::abs(e) / 2.5, 0.5, 1), larger(distance / dt, 0.2));
    const double limit = v / machine_.turning_radius;
    const double want = clamp(2.5 * e, -limit, limit);
    turn_ += clamp(want - turn_, -5 * dt, 5 * dt);
    turn_ = clamp(turn_, -limit, limit);
    pose_.heading = wrap(pose_.heading + turn_ * dt);
    const double x = pose_.x + cosine(pose_.heading) * v * dt;
    const double y = pose_.y + sine(pose_.heading) * v * dt;
    if (blocked(x, y)) {
        bump(x, y);
        return false;
    }
    moved_ = length(x - pose_.x, y - pose_.y);
    pose_.x = x;
    pose_.y = y;
    return true;
}

// ---------------------------------------------------------------- the manager

// The manager holds one tape (the way to a line, then the line), drives it to its
// end, then chooses again. The tape is a curve already checked against the turning
// circle and everything known, with its changes of gear marked. Driving is nothing
// but tracking it: the mower never works out a turn while it is making one.
void Mower::step(double dt) {
    moved_ = 0;
    if (finished_ && !held_)
        return;
    time_ += dt;
    sense();
    tick_ -= dt;
    if (held_) {
        const double d = length(hold_x_ - pose_.x, hold_y_ - pose_.y);
        // Close enough to the pointer is there: a tractor asked to sit on a point would circle it.
        if (d >= 0.08)
            static_cast<void>(steer(angle_of(hold_y_ - pose_.y, hold_x_ - pose_.x), d, machine_.speed, dt));
        else
            turn_ = 0;
        if (tick_ <= 0) {
            tick_ = 0.3;
            survey();
            Pick pick = elect();
            preview_.clear();
            if (pick.valid)
                preview_.swap(pick.tape);
        }
    } else {
        if (!cur_.valid || at_ >= static_cast<int>(tape_.size())) {
            next();
        } else if (tick_ <= 0) {
            tick_ = 0.25;
            watch();
            if (!cur_.valid)
                next();
        }
        if (cur_.valid && at_ < static_cast<int>(tape_.size()))
            track(dt);
    }
}

void Mower::track(double dt) {
    std::vector<TapePoint>& tape = tape_;
    const int last = static_cast<int>(tape.size()) - 1;
    int i = at_;
    const double mx = pose_.x;
    const double my = pose_.y;
    // the stretch in the present gear ends at the next change of gear, or at the end of the tape
    const int gear = tape[static_cast<std::size_t>(i)].dir;
    int stop = i;
    while (stop < last && tape[static_cast<std::size_t>(stop) + 1].dir == gear)
        ++stop;
    const double fx = cosine(pose_.heading) * gear;
    const double fy = sine(pose_.heading) * gear;
    while (i < stop) {
        const TapePoint& p = tape[static_cast<std::size_t>(i)];
        const bool reached = length(p.x - mx, p.y - my) < 0.1;
        const bool passed = (p.x - mx) * fx + (p.y - my) * fy < 0 && length(p.x - mx, p.y - my) < 0.6;
        if (!reached && !passed)
            break;
        ++i;
    }
    // the tape is only ever followed, never chased: if the mower is not on it, the tape is void and it chooses again from where it is
    if (length(tape[static_cast<std::size_t>(i)].x - mx, tape[static_cast<std::size_t>(i)].y - my) > 0.9) {
        cur_.valid = false;
        turn_ = 0;
        return;
    }
    const TapePoint& end = tape[static_cast<std::size_t>(stop)];
    const double to_stop = length(end.x - mx, end.y - my);
    if (i == stop && (to_stop < 0.07 || ((end.x - mx) * fx + (end.y - my) * fy < 0 && to_stop < 0.5))) {
        at_ = stop + 1;
        if (stop < last && tape[static_cast<std::size_t>(stop) + 1].dir != gear)
            ++reversals_;
        turn_ = 0;
        return;
    }
    at_ = i;
    const double reach = gear > 0 ? 0.45 : 0.35;
    int j = i;
    while (j < stop && length(tape[static_cast<std::size_t>(j)].x - mx, tape[static_cast<std::size_t>(j)].y - my) < reach)
        ++j;
    const double tx = tape[static_cast<std::size_t>(j)].x - mx;
    const double ty = tape[static_cast<std::size_t>(j)].y - my;
    const double ahead = larger(length(tx, ty), 0.15);
    const double e = wrap(angle_of(ty, tx) - (gear > 0 ? pose_.heading : pose_.heading + pi));
    // slow for the bend coming: the wheel takes time to come across, so a tight curve is only followed truly if it is entered gently
    const int k2 = std::min(stop, i + 5);
    const int k1 = std::min(stop, i + 2);
    double bend_ahead = 0;
    if (k2 > k1 && k1 > i) {
        const TapePoint& a = tape[static_cast<std::size_t>(i)];
        const TapePoint& b = tape[static_cast<std::size_t>(k1)];
        const TapePoint& c = tape[static_cast<std::size_t>(k2)];
        bend_ahead = std::abs(wrap(angle_of(c.y - b.y, c.x - b.x) - angle_of(b.y - a.y, b.x - a.x))) / larger(0.1, length(c.x - a.x, c.y - a.y) * 0.6);
    }
    const double radius = machine_.turning_radius;
    const double top = (gear < 0 ? 0.6 : machine_.speed) / (1 + 2.2 * larger(0, smaller(bend_ahead * radius, 1.2) - 0.35));
    const double v = smaller(top, 0.3 + 1.6 * to_stop) * clamp(1 - std::abs(e) / 1.5, 0.4, 1);
    // rolling forward too near something, it eases away from it
    double shy = 0;
    {
        const int k = cell_of(mx, my);
        const double room = read(clear_, k);
        if (gear > 0 && room < 0.6) {
            const double away = cosine(pose_.heading) * read(grad_y_, k) - sine(pose_.heading) * read(grad_x_, k);
            shy = (away >= 0 ? 1 : -1) * smaller(1, (0.6 - room) / 0.08) * 0.9;
        }
    }
    const double limit = v / radius;
    const double want = clamp(v * 2 * sine(e) / ahead + shy, -limit, limit);
    turn_ += clamp(want - turn_, -6 * dt, 6 * dt);
    turn_ = clamp(turn_, -limit, limit);
    pose_.heading = wrap(pose_.heading + turn_ * dt);
    const double x = mx + cosine(pose_.heading) * gear * v * dt;
    const double y = my + sine(pose_.heading) * gear * v * dt;
    if (blocked(x, y)) {
        // touched something it had not seen. It now knows it is there; it draws straight off it the way it came, then chooses afresh.
        bump(x, y);
        ++bumps_;
        avoid_.push_back({mx + fx * 0.6, my + fy * 0.6, time_ + 25});
        turn_ = 0;
        std::vector<Spot> out{};
        for (int k = 1; k <= 10; ++k) {
            const double ox = mx - fx * 0.1 * k;
            const double oy = my - fy * 0.1 * k;
            if (blocked(ox, oy) || (k > 4 && read(clear_, cell_of(ox, oy)) < read(clear_, cell_of(mx, my)) - 0.05))
                break;
            out.push_back({ox, oy});
        }
        if (out.size() >= 2 && cur_.kind != Kind::escape) {
            cur_ = Job{tier_escape, Kind::escape, 0, -1, true};
            job_ = cur_;
            tape_.clear();
            for (const Spot& spot : out) {
                TapePoint point{};
                point.x = spot.x;
                point.y = spot.y;
                point.dir = -gear;
                tape_.push_back(point);
            }
            at_ = 0;
        } else {
            cur_.valid = false;
        }
        return;
    }
    moved_ = length(x - mx, y - my);
    pose_.x = x;
    pose_.y = y;
}

int Mower::lean_count(double x, double y, double nx, double ny, double lo, double hi) const {
    int n = 0;
    for (double s = lo; s <= hi + 1e-6; s += 0.1) {
        const double qx = x + nx * s;
        const double qy = y + ny * s;
        if (qx < 0.1 || qy < 0.1 || qx >= width_ - 0.1 || qy >= height_ - 0.1)
            continue;
        const int fi = static_cast<int>(qx / fine);
        const int fj = static_cast<int>(qy / fine);
        const int k = (fj >> 1) * nx_ + (fi >> 1);
        if (belief_[static_cast<std::size_t>(k)] == 2 || read(clear_, k) < 0.2 || read(fence_dist_, k) < verge + 0.05)
            continue;
        if (cut_[static_cast<std::size_t>(fj * cells_x_ + fi)] == 0)
            ++n;
    }
    return n;
}

// Where the deck is already overlapping cut ground on one side and would just miss
// standing grass on the other, the line ahead leans over, by up to 30 cm, to take
// it. It never leans away from grass it was going to cut, and the lean is smoothed
// so the line stays easy to drive.
void Mower::lean() {
    std::vector<TapePoint>& tape = tape_;
    const int size = static_cast<int>(tape.size());
    const int from = at_ + 4;
    const int to = std::min(size - 1, at_ + 26);
    if (to - from < 6)
        return;
    const double shifts[6] = {0.1, -0.1, 0.2, -0.2, 0.3, -0.3};
    std::vector<double> want{};
    for (int i = from; i <= to; ++i) {
        TapePoint& p = tape[static_cast<std::size_t>(i)];
        if (!p.based) {
            p.base_x = p.x;
            p.base_y = p.y;
            p.based = true;
        }
        const TapePoint& q = tape[static_cast<std::size_t>(std::min(i + 1, size - 1))];
        const TapePoint& o = tape[static_cast<std::size_t>(i) - 1];
        const double ox = o.based ? o.base_x : o.x;
        const double oy = o.based ? o.base_y : o.y;
        const double qx = q.based ? q.base_x : q.x;
        const double qy = q.based ? q.base_y : q.y;
        double tx = qx - ox;
        double ty = qy - oy;
        double tl = length(tx, ty);
        if (tl == 0)
            tl = 1;
        tx /= tl;
        ty /= tl;
        const double nx = -ty;
        const double ny = tx;
        p.normal_x = nx;
        p.normal_y = ny;
        const int base = lean_count(p.base_x, p.base_y, nx, ny, -0.5, 0.5);
        double best = 0;
        double best_gain = 0;
        for (const double d : shifts) {
            const int cell = cell_of(p.base_x + nx * d, p.base_y + ny * d);
            if (!passable(cell) || read(clear_, cell) < lap_gap)
                continue;
            const int got = lean_count(p.base_x, p.base_y, nx, ny, -0.5 + d, 0.5 + d);
            const int dropped = d > 0 ? lean_count(p.base_x, p.base_y, nx, ny, -0.5, -0.5 + d - 0.05)
                                      : lean_count(p.base_x, p.base_y, nx, ny, 0.5 + d + 0.05, 0.5);
            const double gain = got - base - 3 * dropped - std::abs(d) * 2;
            if (gain > best_gain) {
                best_gain = gain;
                best = d;
            }
        }
        want.push_back(best);
    }
    const int wanted = static_cast<int>(want.size());
    for (int i = from; i <= to; ++i) {
        double sum = 0;
        int n = 0;
        for (int u = -4; u <= 4; ++u) {
            const int j = i - from + u;
            sum += j < 0 || j >= wanted ? 0 : want[static_cast<std::size_t>(j)];
            ++n;
        }
        const double taper = smaller(smaller(1, (i - from + 1) / 4.0), (to - i + 1) / 4.0);
        const double d = sum / n * taper;
        TapePoint& p = tape[static_cast<std::size_t>(i)];
        p.x = p.base_x + p.normal_x * d;
        p.y = p.base_y + p.normal_y * d;
    }
}

void Mower::take(Pick& pick) {
    cur_ = pick.job;
    cur_.valid = true;
    job_ = cur_;
    tape_.swap(pick.tape);
    at_ = 0;
    preview_.clear();
    tick_ = 0.25;
}

void Mower::escape_with(const std::vector<Spot>& pts) {
    cur_ = Job{tier_escape, Kind::escape, 0, -1, true};
    job_ = cur_;
    tape_.clear();
    for (const Spot& spot : pts) {
        TapePoint point{};
        point.x = spot.x;
        point.y = spot.y;
        point.dir = -1;
        tape_.push_back(point);
    }
    at_ = 0;
}

// A tape has ended (or there was none): ask the planner for its menu and take the
// best next piece of work.
void Mower::next() {
    if (has_goal_ && plan_goal())
        return;
    // a tape that ended at once, without the mower having moved, was a bad offer: leave that spot alone for a while
    if (last_elect_.valid && time_ - last_elect_.time < 0.8 && length(pose_.x - last_elect_.x, pose_.y - last_elect_.y) < 0.1)
        avoid_.push_back({last_elect_.end_x, last_elect_.end_y, time_ + 30});
    survey();
    Pick pick = elect();
    ++elections_;
    bool work_listed = false;
    for (const Run& run : runs_)
        work_listed = work_listed || run.kind != Kind::tuft;
    // nothing on the menu can be planned to: that is not the same as nothing left to do
    if (!pick.valid && work_listed) {
        std::vector<Run> left{};
        tufts(left);
        if (left.empty())
            scout(left);
        runs_.swap(left);
        pick = elect();
        work_listed = false;
        for (const Run& run : runs_)
            work_listed = work_listed || run.kind != Kind::tuft;
    }
    last_elect_.valid = false;
    if (pick.valid && !pick.tape.empty()) {
        const TapePoint& end = pick.tape.back();
        last_elect_ = Elected{time_, pose_.x, pose_.y, end.x, end.y, true};
    }
    if (!pick.valid) {
        // nothing on the menu can be reached from here by any planned move: back straight out a way, if there is room, and ask again
        if (work_listed && escapes_ < 8) {
            const Spot here{pose_.x, pose_.y};
            Arc out = back(here, pose_.heading, 0.8);
            if (!out.valid)
                out = back(here, pose_.heading, 0.4);
            if (out.valid) {
                ++escapes_;
                escape_with(out.pts);
                return;
            }
        }
        cur_.valid = false;
        job_.valid = false;
        tape_.clear();
        at_ = 0;
        finished_ = true;
        has_goal_ = false;
        return;
    }
    escapes_ = 0;
    if (pick.job.kind == Kind::tuft) {
        if (pick.tour) {
            for (const int key : pick.tour_keys)
                ++tuft_tries_[key];
        } else {
            ++tuft_tries_[pick.job.lane];
        }
    }
    if (pick.job.kind == Kind::stripe) {
        has_last_stripe_ = true;
        last_frame_ = pick.job.frame;
        last_lane_ = pick.job.lane;
        axis_locked_ = true;
    }
    last_kind_ = pick.job.kind;
    take(pick);
}

// The only things that change a tape once it is being driven: the line ahead has
// become impossible, there is nothing left on it to cut, or (for a lap round
// something still being discovered) the line ahead is redrawn.
void Mower::watch() {
    const double mx = pose_.x;
    const double my = pose_.y;
    if (belief_dirty_) {
        learn();
        const bool on_lap = cur_.kind == Kind::contour || cur_.kind == Kind::lap;
        if (on_lap && tape_[static_cast<std::size_t>(at_)].work && tape_[static_cast<std::size_t>(at_)].dir > 0) {
            survey();
            const double hx = cosine(pose_.heading);
            const double hy = sine(pose_.heading);
            int best_run = -1;
            int best_i = 0;
            double best_d = 0.9;
            bool best_back = false;
            for (std::size_t r = 0; r < runs_.size(); ++r) {
                const Run& run = runs_[r];
                if (run.kind != cur_.kind || run.tier != cur_.tier)
                    continue;
                const int n = static_cast<int>(run.pts.size());
                for (int i = 0; i < n - 1; ++i) {
                    const RunPoint& p = run.pts[static_cast<std::size_t>(i)];
                    const double d = length(p.x - mx, p.y - my);
                    if (d > best_d)
                        continue;
                    const RunPoint& q = run.pts[static_cast<std::size_t>(i) + 1];
                    const bool forward = (q.x - p.x) * hx + (q.y - p.y) * hy >= 0;
                    if (!forward && run.one_way)
                        continue;
                    best_run = static_cast<int>(r);
                    best_i = i;
                    best_d = d;
                    best_back = !forward;
                }
            }
            {
                // first choice: keep the next metre of the present line and carry on from there along the line as now known
                const int size = static_cast<int>(tape_.size());
                int a = at_;
                double kept = 0;
                while (a < size - 1 && kept < 1.0) {
                    ++a;
                    kept += length(tape_[static_cast<std::size_t>(a)].x - tape_[static_cast<std::size_t>(a) - 1].x,
                                   tape_[static_cast<std::size_t>(a)].y - tape_[static_cast<std::size_t>(a) - 1].y);
                }
                const TapePoint anchor = tape_[static_cast<std::size_t>(a)];
                const TapePoint before = tape_[static_cast<std::size_t>(std::max(0, a - 2))];
                const double ax = anchor.x - before.x;
                const double ay = anchor.y - before.y;
                double al = length(ax, ay);
                if (al == 0)
                    al = 1;
                int hit_run = -1;
                int hit_i = 0;
                double hit_d = 0.2;
                bool hit_back = false;
                for (std::size_t r = 0; r < runs_.size(); ++r) {
                    const Run& run = runs_[r];
                    if (run.kind != cur_.kind || run.tier != cur_.tier)
                        continue;
                    const int n = static_cast<int>(run.pts.size());
                    for (int i = 0; i < n - 1; ++i) {
                        const RunPoint& p = run.pts[static_cast<std::size_t>(i)];
                        const double d = length(p.x - anchor.x, p.y - anchor.y);
                        if (d > hit_d)
                            continue;
                        const RunPoint& q = run.pts[static_cast<std::size_t>(i) + 1];
                        const double along = ((q.x - p.x) * ax + (q.y - p.y) * ay) / (al * track_step);
                        if (std::abs(along) < 0.85)
                            continue;
                        hit_run = static_cast<int>(r);
                        hit_i = i;
                        hit_d = d;
                        hit_back = along < 0;
                    }
                }
                if (hit_run >= 0) {
                    const Run& run = runs_[static_cast<std::size_t>(hit_run)];
                    const int n = static_cast<int>(run.pts.size());
                    std::vector<RunPoint> rest{};
                    if (hit_back) {
                        if (run.closed) {
                            for (int k = 0; k < n - 1; ++k)
                                rest.push_back(run.pts[static_cast<std::size_t>(((hit_i - k) % n + n) % n)]);
                        } else {
                            for (int k = hit_i; k >= 0; --k)
                                rest.push_back(run.pts[static_cast<std::size_t>(k)]);
                        }
                    } else if (run.closed) {
                        for (int k = 1; k < n; ++k)
                            rest.push_back(run.pts[static_cast<std::size_t>((hit_i + k) % n)]);
                    } else {
                        for (int k = hit_i + 1; k < n; ++k)
                            rest.push_back(run.pts[static_cast<std::size_t>(k)]);
                    }
                    tape_.resize(static_cast<std::size_t>(a) + 1);
                    for (const RunPoint& p : rest) {
                        TapePoint point{};
                        point.x = p.x;
                        point.y = p.y;
                        point.work = true;
                        tape_.push_back(point);
                    }
                    best_run = -1;
                }
            }
            if (best_run >= 0) {
                // rejoin the redrawn line a little way ahead, by a curve from where the mower is now
                const Run& run = runs_[static_cast<std::size_t>(best_run)];
                const int n = static_cast<int>(run.pts.size());
                std::vector<RunPoint> rest{};
                if (best_back) {
                    if (run.closed) {
                        for (int k = 0; k < n; ++k)
                            rest.push_back(run.pts[static_cast<std::size_t>(((best_i - k) % n + n) % n)]);
                    } else {
                        for (int k = best_i; k >= 0; --k)
                            rest.push_back(run.pts[static_cast<std::size_t>(k)]);
                    }
                } else if (run.closed) {
                    for (int k = 1; k <= n; ++k)
                        rest.push_back(run.pts[static_cast<std::size_t>((best_i + k) % n)]);
                } else {
                    for (int k = best_i + 1; k < n; ++k)
                        rest.push_back(run.pts[static_cast<std::size_t>(k)]);
                }
                const int joins[8] = {5, 8, 11, 14, 18, 23, 29, 36};
                const int count = static_cast<int>(rest.size());
                for (const int jn : joins) {
                    if (jn > count - 2)
                        break;
                    const RunPoint& join = rest[static_cast<std::size_t>(jn)];
                    const RunPoint& onward = rest[static_cast<std::size_t>(jn) + 1];
                    const Curve c = curve({mx, my}, pose_.heading, {join.x, join.y}, angle_of(onward.y - join.y, onward.x - join.x));
                    if (!c.valid)
                        continue;
                    tape_.clear();
                    for (const Spot& spot : c.pts) {
                        TapePoint point{};
                        point.x = spot.x;
                        point.y = spot.y;
                        point.work = true;
                        tape_.push_back(point);
                    }
                    for (int k = jn + 1; k < count; ++k) {
                        TapePoint point{};
                        point.x = rest[static_cast<std::size_t>(k)].x;
                        point.y = rest[static_cast<std::size_t>(k)].y;
                        point.work = true;
                        tape_.push_back(point);
                    }
                    at_ = 0;
                    return;
                }
            }
        }
    }
    std::vector<TapePoint>& tape = tape_;
    double run = 0;
    for (int i = at_; i < static_cast<int>(tape.size()) && run < 4; ++i) {
        const TapePoint& p = tape[static_cast<std::size_t>(i)];
        const int k = cell_of(p.x, p.y);
        if (i > at_)
            run += length(p.x - tape[static_cast<std::size_t>(i) - 1].x, p.y - tape[static_cast<std::size_t>(i) - 1].y);
        if (belief_[static_cast<std::size_t>(k)] == 2 || read(clear_, k) < machine_.body_radius - 0.03) {
            // the line ahead runs into something now known. End it early enough to turn away in one sweep, not nose-up to it.
            int j = i;
            while (j > at_ && read(clear_, cell_of(tape[static_cast<std::size_t>(j)].x, tape[static_cast<std::size_t>(j)].y)) < 1.2)
                --j;
            if (j <= at_ + 1) {
                if (run < 0.6) {
                    cur_.valid = false;
                    return;
                }
                j = std::max(at_ + 1, i - 3);
            }
            tape.resize(static_cast<std::size_t>(j) + 1);
            break;
        }
    }
    if (tape[static_cast<std::size_t>(at_)].work) {
        const int size = static_cast<int>(tape.size());
        int last_todo = -1;
        for (int i = at_; i < size; ++i) {
            const TapePoint& p = tape[static_cast<std::size_t>(i)];
            const TapePoint& q = tape[static_cast<std::size_t>(std::min(i + 1, size - 1))];
            const TapePoint& o = tape[static_cast<std::size_t>(std::max(i - 1, 0))];
            const double tx = q.x - o.x;
            const double ty = q.y - o.y;
            double tl = length(tx, ty);
            if (tl == 0)
                tl = 1;
            if (cur_.kind == Kind::tuft || todo_at(p.x, p.y, -ty / tl, tx / tl))
                last_todo = i;
        }
        if (last_todo < 0) {
            cur_.valid = false;
            return;
        }
        if (last_todo + 3 < size)
            tape.resize(static_cast<std::size_t>(last_todo) + 3);
        if (cur_.kind == Kind::stripe || cur_.kind == Kind::lap)
            lean();
    }
}

// A found object: the way to it is planned like the way to a tuft, which has no
// proper direction either. Where the point itself is too near something to stand
// on, the nearest ground a pose may take is driven to instead.
bool Mower::can_stand(double x, double y) const {
    const double margin = machine_.body_radius + 0.08;
    if (!(x > margin && y > margin && x < width_ - margin && y < height_ - margin))
        return false;
    const int k = cell_of(x, y);
    return passable(k) && read(clear_, k) >= pose_gap;
}

bool Mower::plan_goal() {
    const double gap = length(goal_x_ - pose_.x, goal_y_ - pose_.y);
    // Each fresh attempt must be getting somewhere; a point that cannot be come at is given up.
    // While no way to it can be drawn at all, the lawn goes on and the goal waits its turn.
    if (gap < goal_best_ - 0.3) {
        goal_best_ = gap;
        goal_tries_ = 0;
    }
    if (goal_tries_ >= 6) {
        has_goal_ = false;
        return false;
    }
    survey();
    Spot target{goal_x_, goal_y_};
    if (!can_stand(target.x, target.y)) {
        const int ci = static_cast<int>(goal_x_ / grid);
        const int cj = static_cast<int>(goal_y_ / grid);
        double nearest_gap = 0.9;
        bool found = false;
        for (int v = -8; v <= 8; ++v) {
            for (int u = -8; u <= 8; ++u) {
                const double x = (ci + u + 0.5) * grid;
                const double y = (cj + v + 0.5) * grid;
                const double d = length(x - goal_x_, y - goal_y_);
                if (d >= nearest_gap || !can_stand(x, y))
                    continue;
                nearest_gap = d;
                target = Spot{x, y};
                found = true;
            }
        }
        if (!found)
            return false;
    }
    const Spot here{pose_.x, pose_.y};
    const double reach = length(target.x - here.x, target.y - here.y);
    const double bearing = angle_of(target.y - here.y, target.x - here.x);
    const double off = std::abs(wrap(bearing - pose_.heading));
    Move move{};
    if (reach > 0.05 && (off < 1.2 || reach > 2.2) && clear_line(here.x, here.y, target.x, target.y, true)) {
        const int n = std::max(1, static_cast<int>(std::ceil(reach / 0.15)));
        Piece line{};
        for (int k = 1; k <= n; ++k)
            line.pts.push_back({here.x + (target.x - here.x) * k / n, here.y + (target.y - here.y) * k / n});
        move.pieces.push_back(line);
        move.valid = true;
    }
    if (!move.valid) {
        const double headings[5] = {bearing, bearing + 0.7, bearing - 0.7, bearing + 1.4, bearing - 1.4};
        for (const double a : headings) {
            Curve c = curve(here, pose_.heading, target, a);
            if (!c.valid)
                continue;
            Piece piece{};
            piece.pts.swap(c.pts);
            move.pieces.push_back(piece);
            move.valid = true;
            break;
        }
    }
    if (!move.valid)
        move = transition(here, pose_.heading, target, bearing);
    if (!move.valid)
        return false;
    tape_.clear();
    for (const Piece& piece : move.pieces) {
        for (const Spot& spot : piece.pts) {
            TapePoint point{};
            point.x = spot.x;
            point.y = spot.y;
            point.dir = piece.dir;
            tape_.push_back(point);
        }
    }
    if (tape_.empty())
        return false;
    ++goal_tries_;
    cur_ = Job{tier_escape, Kind::goal, 0, -1, true};
    job_ = cur_;
    at_ = 0;
    preview_.clear();
    tick_ = 0.25;
    return true;
}

// Simulation advances in fixed 20 ms steps counted in whole microseconds, so the
// same total time gives the same steps however it is divided between frames.
int Mower::take_steps(double seconds) {
    if (!(seconds > 0))
        return 0;
    pending_us_ += std::llround(std::min(seconds, 0.25) * 1e6);
    const long long steps = pending_us_ / step_us;
    pending_us_ -= steps * step_us;
    return static_cast<int>(steps);
}

void Mower::publish_path() {
    path_.clear();
    if (held_) {
        for (const TapePoint& point : preview_)
            path_.push_back({point.x, point.y, 0});
        return;
    }
    for (std::size_t k = static_cast<std::size_t>(std::max(at_, 0)); k < tape_.size(); ++k)
        path_.push_back({tape_[k].x, tape_[k].y, 0});
}

Step Mower::update(double seconds) {
    Step total{};
    const int steps = take_steps(seconds);
    const double dt = static_cast<double>(step_us) * 1e-6;
    for (int k = 0; k < steps; ++k) {
        touched_ = false;
        const bool idle = finished_ && !held_;
        step(dt);
        total.speed = moved_ / dt;
        total.turning = idle ? 0 : turn_;
        total.bumped = total.bumped || touched_;
        if (!idle)
            total.newly_cut += mow(pose_.x, pose_.y, pose_.heading);
        if (has_goal_ && !held_ && length(goal_x_ - pose_.x, goal_y_ - pose_.y) < goal_reach) {
            // the deck is over it: carry on with the lawn
            total.goal_reached = true;
            has_goal_ = false;
            if (cur_.kind == Kind::goal)
                cur_.valid = false;
        }
    }
    publish_path();
    return total;
}

Step Mower::drive_toward(double x, double y, double seconds) {
    held_ = true;
    hold_x_ = x;
    hold_y_ = y;
    finished_ = false;
    return update(seconds);
}

void Mower::set_solid(const std::vector<std::uint8_t>& solid) {
    const int given_x = std::max(1, static_cast<int>(std::lround(width_ / field_.cell)));
    const int given_y = std::max(1, static_cast<int>(std::lround(height_ / field_.cell)));
    if (solid.size() != static_cast<std::size_t>(given_x) * static_cast<std::size_t>(given_y))
        return;
    hard_.assign(static_cast<std::size_t>(cells_), 0);
    for (int j = 0; j < ny_; ++j) {
        for (int i = 0; i < nx_; ++i) {
            bool stop = i < 2 || j < 2 || i >= nx_ - 2 || j >= ny_ - 2;
            const int x0 = std::min(given_x - 1, static_cast<int>(i * grid / field_.cell));
            const int x1 = std::min(given_x - 1, static_cast<int>(((i + 1) * grid - 1e-9) / field_.cell));
            const int y0 = std::min(given_y - 1, static_cast<int>(j * grid / field_.cell));
            const int y1 = std::min(given_y - 1, static_cast<int>(((j + 1) * grid - 1e-9) / field_.cell));
            for (int y = y0; y <= y1 && !stop; ++y) {
                for (int x = x0; x <= x1 && !stop; ++x)
                    stop = solid[static_cast<std::size_t>(y) * static_cast<std::size_t>(given_x) + static_cast<std::size_t>(x)] != 0;
            }
            hard_[static_cast<std::size_t>(j * nx_ + i)] = stop ? 1 : 0;
        }
    }
}

void Mower::resume() {
    held_ = false;
    tuft_tries_.clear();
    cur_.valid = false;
    job_.valid = false;
    has_last_stripe_ = false;
    tape_.clear();
    at_ = 0;
    escapes_ = 0;
    preview_.clear();
    publish_path();
}

void Mower::set_goal(double x, double y) {
    goal_x_ = clamp(x, 0, width_);
    goal_y_ = clamp(y, 0, height_);
    has_goal_ = true;
    goal_tries_ = 0;
    goal_best_ = std::numeric_limits<double>::infinity();
    finished_ = false;
    if (!held_) {
        // drop what it was doing: the next thing chosen is the way to the goal
        cur_.valid = false;
        last_elect_.valid = false;
    }
}

void Mower::clear_goal() {
    has_goal_ = false;
    if (cur_.valid && cur_.kind == Kind::goal)
        cur_.valid = false;
}

Phase Mower::phase() const {
    if (held_)
        return Phase::manual;
    if (finished_)
        return Phase::finished;
    if (has_goal_ && cur_.valid && cur_.kind == Kind::goal)
        return Phase::goal;
    if (job_.valid && (job_.kind == Kind::tuft || job_.kind == Kind::escape))
        return Phase::cleaning;
    return Phase::sweeping;
}

double Mower::progress() const {
    if (cuttable_count_ == 0)
        return 1;
    return static_cast<double>(cut_count_) / cuttable_count_;
}

Belief Mower::belief_at(double x, double y) const {
    if (!(x >= 0 && y >= 0 && x < width_ && y < height_))
        return Belief::occupied;
    const std::uint8_t known = belief_[static_cast<std::size_t>(cell_of(x, y))];
    return known == 2 ? Belief::occupied : (known == 1 ? Belief::free : Belief::unknown);
}

bool Mower::cut_at(double x, double y) const {
    const int cx = static_cast<int>(std::floor(x / fine));
    const int cy = static_cast<int>(std::floor(y / fine));
    if (cx < 0 || cy < 0 || cx >= cells_x_ || cy >= cells_y_)
        return false;
    return cut_[static_cast<std::size_t>(cy * cells_x_ + cx)] != 0;
}

bool Mower::sees(double x, double y) const {
    const double dx = x - pose_.x;
    const double dy = y - pose_.y;
    const double distance = length(dx, dy);
    if (distance < 1e-9)
        return true;
    const double off = wrap(angle_of(dy, dx) - pose_.heading);
    if (std::abs(off) > machine_.lidar_field * 0.5 || distance > range_at(off))
        return false;
    // the last planner cell is the point's own: something right against it does not hide it
    for (double t = 0; t < distance - grid; t += 0.04) {
        const double px = pose_.x + dx / distance * t;
        const double py = pose_.y + dy / distance * t;
        if (px < 0 || py < 0 || px >= width_ || py >= height_)
            return false;
        if (truth_[static_cast<std::size_t>(cell_of(px, py))] != 0)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------- the planner

// The planner learns the lawn and publishes the menu of lines; it never drives.
void Mower::learn() {
    belief_dirty_ = false;
    fields();
    nucleate();
    index_germs();
    own();
    if (self_test()) {
        index_germs();
        own();
    }
}

void Mower::survey() {
    if (belief_dirty_)
        learn();
    shunned();
    transit();
    build_runs();
}

void Mower::fields() {
    const std::vector<std::uint8_t>& b = belief_;
    const std::size_t cells = static_cast<std::size_t>(cells_);
    std::vector<std::uint8_t> solid(cells);
    bool any = false;
    for (std::size_t k = 0; k < cells; ++k) {
        solid[k] = b[k] == 2 ? 1 : 0;
        any = any || solid[k] != 0;
    }
    // Nothing is given. The fence is whatever seen solid runs along the limit of the ground; anything standing too near
    // it to pass behind counts with it, because the first lap has to go round both.
    if (any)
        clear_ = distance_to(solid, nx_, ny_);
    else
        clear_.assign(cells, 99);
    const std::vector<float>& c = clear_;
    outer_.assign(cells, 0);
    {
        std::vector<int> label(cells, 0);
        std::vector<std::uint8_t> fence(cells, 0);
        std::vector<int> members{};  // every solid cell, grouped by connected piece
        std::vector<int> starts{};   // where each piece begins in `members`
        std::vector<std::uint8_t> on_rim{};
        int id = 0;
        bool any_fence = false;
        for (int k0 = 0; k0 < cells_; ++k0) {
            if (solid[static_cast<std::size_t>(k0)] == 0 || label[static_cast<std::size_t>(k0)] != 0)
                continue;
            ++id;
            const std::size_t first = members.size();
            members.push_back(k0);
            label[static_cast<std::size_t>(k0)] = id;
            bool rim = false;
            for (std::size_t h = first; h < members.size(); ++h) {
                const int k = members[h];
                const int i = k % nx_;
                const int j = k / nx_;
                if (i < 2 || j < 2 || i >= nx_ - 2 || j >= ny_ - 2)
                    rim = true;
                for (int v = -1; v <= 1; ++v) {
                    for (int u = -1; u <= 1; ++u) {
                        const int x = i + u;
                        const int y = j + v;
                        if (x < 0 || y < 0 || x >= nx_ || y >= ny_)
                            continue;
                        const std::size_t q = static_cast<std::size_t>(y * nx_ + x);
                        if (solid[q] != 0 && label[q] == 0) {
                            label[q] = id;
                            members.push_back(static_cast<int>(q));
                        }
                    }
                }
            }
            starts.push_back(static_cast<int>(first));
            on_rim.push_back(rim ? 1 : 0);
            if (rim) {
                any_fence = true;
                for (std::size_t h = first; h < members.size(); ++h)
                    fence[static_cast<std::size_t>(members[h])] = 1;
            }
        }
        starts.push_back(static_cast<int>(members.size()));
        if (any_fence)
            fence_dist_ = distance_to(fence, nx_, ny_);
        else
            fence_dist_.assign(cells, 99);
        std::vector<std::uint8_t> attached = fence;
        for (std::size_t piece = 0; piece < on_rim.size(); ++piece) {
            if (on_rim[piece] != 0)
                continue;
            bool near_edge = false;
            for (int h = starts[piece]; h < starts[piece + 1] && !near_edge; ++h)
                near_edge = read(fence_dist_, members[static_cast<std::size_t>(h)]) < 2 * lap_gap + 0.15;
            if (near_edge) {
                for (int h = starts[piece]; h < starts[piece + 1]; ++h)
                    attached[static_cast<std::size_t>(members[static_cast<std::size_t>(h)])] = 1;
            }
        }
        if (any_fence) {
            edge_dist_ = distance_to(attached, nx_, ny_);
            for (int k = 0; k < cells_; ++k)
                outer_[static_cast<std::size_t>(k)] = read(edge_dist_, k) <= read(c, k) + 0.05 ? 1 : 0;
        } else {
            edge_dist_.assign(cells, 99);
        }
    }
    // ground within half a metre of something seen free
    std::vector<std::uint8_t> row(cells, 0);
    near_seen_.assign(cells, 0);
    for (int j = 0; j < ny_; ++j) {
        for (int i = 0; i < nx_; ++i) {
            std::uint8_t v = 0;
            const int u1 = std::min(nx_ - 1, i + 5);
            for (int u = std::max(0, i - 5); u <= u1 && v == 0; ++u)
                v = b[static_cast<std::size_t>(j * nx_ + u)] == 1 ? 1 : 0;
            row[static_cast<std::size_t>(j * nx_ + i)] = v;
        }
    }
    for (int j = 0; j < ny_; ++j) {
        for (int i = 0; i < nx_; ++i) {
            std::uint8_t v = 0;
            const int u1 = std::min(ny_ - 1, j + 5);
            for (int u = std::max(0, j - 5); u <= u1 && v == 0; ++u)
                v = row[static_cast<std::size_t>(u * nx_ + i)];
            near_seen_[static_cast<std::size_t>(j * nx_ + i)] = v;
        }
    }
    // geometric uncertainty: solid cells whose far side has not been seen
    std::vector<float> doubt(cells, 0);
    double weight[13][13];
    for (int v = -6; v <= 6; ++v) {
        for (int u = -6; u <= 6; ++u)
            weight[v + 6][u + 6] = 1 - length(u, v) / 7;
    }
    for (int j = 1; j < ny_ - 1; ++j) {
        for (int i = 1; i < nx_ - 1; ++i) {
            const std::size_t k = static_cast<std::size_t>(j * nx_ + i);
            if (b[k] != 2)
                continue;
            if (b[k - 1] != 0 && b[k + 1] != 0 && b[k - static_cast<std::size_t>(nx_)] != 0 && b[k + static_cast<std::size_t>(nx_)] != 0)
                continue;
            for (int v = -6; v <= 6; ++v) {
                for (int u = -6; u <= 6; ++u) {
                    const int x = i + u;
                    const int y = j + v;
                    if (x < 0 || y < 0 || x >= nx_ || y >= ny_)
                        continue;
                    const double w = weight[v + 6][u + 6];
                    if (w > read(doubt, y * nx_ + x))
                        write(doubt, y * nx_ + x, w);
                }
            }
        }
    }
    grad_x_.assign(cells, 0);
    grad_y_.assign(cells, 0);
    for (int j = 1; j < ny_ - 1; ++j) {
        for (int i = 1; i < nx_ - 1; ++i) {
            const int k = j * nx_ + i;
            const double here = read(c, k);
            const double gx = (read(c, k + 1) - read(c, k - 1)) / (2 * grid);
            const double gy = (read(c, k + nx_) - read(c, k - nx_)) / (2 * grid);
            const double gm = length(gx, gy);
            write(grad_x_, k, gm > 1e-6 ? gx / gm : 0);
            write(grad_y_, k, gm > 1e-6 ? gy / gm : 0);
            // ridges (as far from everything as it gets) and inside corners are where the ground is intricate
            const double ridge = clamp((1 - gm - 0.3) / 0.7, 0, 1);
            const double lap = (read(c, k + 1) + read(c, k - 1) + read(c, k + nx_) + read(c, k - nx_) - 4 * here) / (grid * grid);
            const double corner = clamp(lap * 0.8, 0, 1) * clamp(2.5 - here, 0, 1);
            write(demand_, k, b[static_cast<std::size_t>(k)] == 2 ? 0 : 1.2 * corner + 1.5 * ridge / (1 + here) + 0.9 * read(doubt, k));
        }
    }
    // what the wavefronts charge for each cell, worked out once here instead of at every edge they cross
    for (int k = 0; k < cells_; ++k) {
        const double room = read(c, k);
        crowd_[static_cast<std::size_t>(k)] = 0.25 / room;
        grow_[static_cast<std::size_t>(k)] = 1 + 1.5 / (0.3 + room);
        wary_[static_cast<std::size_t>(k)] = 1.2 * (clamp(1.5 - room, 0, 1.5) / 1.5);
    }
}

double Mower::spacing(int cell) const {
    return clamp(germ_far / (1 + 3 * read(demand_, cell)), germ_near, germ_far);
}

void Mower::nucleate() {
    const std::vector<std::uint8_t>& b = belief_;
    for (int g = static_cast<int>(germs_.size()) - 1; g >= 0; --g) {
        const int cell = germs_[static_cast<std::size_t>(g)].cell;
        if (b[static_cast<std::size_t>(cell)] == 2 || read(clear_, cell) < 0.3)
            germs_.erase(germs_.begin() + g);
    }
    std::vector<int> candidates{};
    for (int j = 2; j < ny_ - 2; j += 2) {
        for (int i = 2; i < nx_ - 2; i += 2) {
            const int k = j * nx_ + i;
            if (b[static_cast<std::size_t>(k)] == 1 && read(clear_, k) >= pose_gap && frozen_[static_cast<std::size_t>(k)] < 0)
                candidates.push_back(k);
        }
    }
    ByDemand order{};
    order.demand = &demand_;
    std::stable_sort(candidates.begin(), candidates.end(), order);
    int born = 0;
    for (const int k : candidates) {
        if (born >= 3)
            break;
        const double x = (k % nx_ + 0.5) * grid;
        const double y = (k / nx_ + 0.5) * grid;
        const double s = spacing(k);
        bool ok = true;
        for (const Germ& germ : germs_) {
            const double d = length(germ.x - x, germ.y - y);
            if (d < s * 0.8 || d < germ.spacing * 0.5) {
                ok = false;
                break;
            }
        }
        if (!ok)
            continue;
        Germ germ{};
        germ.id = next_germ_++;
        germ.x = x;
        germ.y = y;
        germ.cell = k;
        germ.spacing = s;
        germs_.push_back(germ);
        ++born;
    }
}

void Mower::index_germs() {
    germ_index_.assign(static_cast<std::size_t>(next_germ_), -1);
    for (std::size_t g = 0; g < germs_.size(); ++g)
        germ_index_[static_cast<std::size_t>(germs_[g].id)] = static_cast<int>(g);
}

// Each germ grows through known-free space; mown ground keeps the owner it had.
void Mower::own() {
    const std::vector<std::uint8_t>& b = belief_;
    const std::vector<float>& c = clear_;
    std::vector<float> dist(static_cast<std::size_t>(cells_), static_cast<float>(far_off));
    Heap heap(cells_);
    std::fill(owner_.begin(), owner_.end(), -1);
    for (int k = 0; k < cells_; ++k) {
        const int kept = frozen_[static_cast<std::size_t>(k)];
        if (kept >= 0 && germ_index_[static_cast<std::size_t>(kept)] >= 0 && b[static_cast<std::size_t>(k)] == 1) {
            owner_[static_cast<std::size_t>(k)] = kept;
            write(dist, k, 0);
            heap.push(0, k);
        }
    }
    for (const Germ& germ : germs_) {
        if (read(dist, germ.cell) > 0) {
            owner_[static_cast<std::size_t>(germ.cell)] = germ.id;
            write(dist, germ.cell, 0);
            heap.push(0, germ.cell);
        }
    }
    while (heap.count > 0) {
        const int k = heap.pop();
        const double d = heap.top_key;
        if (d > read(dist, k))
            continue;
        const int i = k % nx_;
        const int j = k / nx_;
        for (int n = 0; n < 8; ++n) {
            const int x = i + step_x[n];
            const int y = j + step_y[n];
            if (x < 0 || y < 0 || x >= nx_ || y >= ny_)
                continue;
            const int q = y * nx_ + x;
            const double room = read(c, q);
            if (b[static_cast<std::size_t>(q)] != 1 || room < 0.3 || (frozen_[static_cast<std::size_t>(q)] >= 0 && owner_[static_cast<std::size_t>(q)] >= 0))
                continue;
            // growth is dearer near things, and dearer still straight toward or away from them, so owners lie along edges
            const double len = n < 4 ? grid : grid * 1.4142;
            const double across = (step_x[n] * read(grad_x_, q) + step_y[n] * read(grad_y_, q)) / (n < 4 ? 1 : 1.4142);
            const double nd = d + len * grow_[static_cast<std::size_t>(q)] * (1 + wary_[static_cast<std::size_t>(q)] * across * across);
            if (nd < read(dist, q)) {
                write(dist, q, nd);
                owner_[static_cast<std::size_t>(q)] = owner_[static_cast<std::size_t>(k)];
                heap.push(nd, q);
            }
        }
    }
    // owner shape: area, centroid, principal axis
    struct Shape {
        double n{};
        double sx{};
        double sy{};
        double sxx{};
        double sxy{};
        double syy{};
        int far{-1};
        double far_distance{};
        double todo{};
    };
    std::vector<Shape> shapes(germs_.size());
    for (int k = 0; k < cells_; ++k) {
        const int id = owner_[static_cast<std::size_t>(k)];
        if (id < 0 || germ_index_[static_cast<std::size_t>(id)] < 0)
            continue;
        Shape& s = shapes[static_cast<std::size_t>(germ_index_[static_cast<std::size_t>(id)])];
        const double x = k % nx_;
        const double y = k / nx_;
        s.n += 1;
        s.sx += x;
        s.sy += y;
        s.sxx += x * x;
        s.sxy += x * y;
        s.syy += y * y;
        if (frozen_[static_cast<std::size_t>(k)] < 0) {
            s.todo += 1;
            if (read(dist, k) > s.far_distance && read(c, k) >= pose_gap) {
                s.far_distance = read(dist, k);
                s.far = k;
            }
        }
    }
    for (std::size_t g = 0; g < germs_.size(); ++g) {
        const Shape& s = shapes[g];
        Germ& germ = germs_[g];
        const double n = larger(s.n, 1);
        const double mx = s.sx / n;
        const double my = s.sy / n;
        const double a = s.sxx / n - mx * mx;
        const double bb = s.sxy / n - mx * my;
        const double d = s.syy / n - my * my;
        const double tr = a + d;
        const double det = a * d - bb * bb;
        const double l1 = tr / 2 + std::sqrt(larger(tr * tr / 4 - det, 0));
        const double l2 = tr - l1;
        germ.area = s.n * grid * grid;
        germ.todo = s.todo * grid * grid;
        germ.centre_x = (mx + 0.5) * grid;
        germ.centre_y = (my + 0.5) * grid;
        germ.axis = angle_of(l1 - a, bb != 0 ? bb : 1e-9);
        germ.longer = std::sqrt(larger(l1, 0)) * grid;
        germ.shorter = std::sqrt(larger(l2, 0)) * grid;
        germ.far = s.far;
        germ.far_distance = s.far_distance;
    }
}

int Mower::frame_of(int cell) const {
    const int id = owner_[static_cast<std::size_t>(cell)];
    if (id < 0 || germ_index_[static_cast<std::size_t>(id)] < 0)
        return 0;
    return germs_[static_cast<std::size_t>(germ_index_[static_cast<std::size_t>(id)])].frame;
}

// Would another sweep axis serve this owner better? Count the lane pieces each would need.
Mower::Count Mower::pieces(const Germ& germ, double axis) const {
    const double ux = cosine(axis);
    const double uy = sine(axis);
    const double nx = -uy;
    const double ny = ux;
    const double reach = germ.longer * 2.2 + 1;
    const double pitch = machine_.lane_pitch;
    Count result{};
    const double across = germ.centre_x * nx + germ.centre_y * ny;
    for (double k = std::ceil((across - reach) / pitch); k * pitch <= across + reach; k += 1) {
        bool on = false;
        const double t0 = germ.centre_x * ux + germ.centre_y * uy;
        for (double t = t0 - reach; t <= t0 + reach; t += track_step) {
            const double x = nx * k * pitch + ux * t;
            const double y = ny * k * pitch + uy * t;
            bool ok = x > 0 && y > 0 && x < width_ && y < height_;
            if (ok) {
                const int cell = cell_of(x, y);
                ok = owner_[static_cast<std::size_t>(cell)] == germ.id && read(clear_, cell) >= pose_gap;
            }
            if (ok) {
                ++result.len;
                if (!on)
                    ++result.pieces;
            }
            on = ok;
        }
    }
    return result;
}

// Each owner checks its own shape: a long thin one may take a sweep axis of its
// own, and one that would need far too many lane pieces is split.
bool Mower::self_test() {
    bool split = false;
    const std::size_t count = germs_.size();
    for (std::size_t index = 0; index < count; ++index) {
        Germ germ = germs_[index];
        if (germ.todo < 3 || std::abs(germ.area - germ.tested) < 0.25 * germ.area)
            continue;
        germ.tested = germ.area;
        const Count diag = pieces(germ, frames_[0]);
        if (germ.longer / larger(germ.shorter, 0.05) >= 2.5 && germ.shorter * 3.5 < 4.5 && germ.todo > 0.6 * germ.area) {
            const double a = nearest(germ.axis / (pi / 16)) * (pi / 16);
            const Count alt = pieces(germ, a);
            const double want = alt.pieces + 1 < 0.65 * diag.pieces && alt.len > 0.8 * diag.len ? a : frames_[0];
            int f = -1;
            for (std::size_t v = 0; v < frames_.size() && f < 0; ++v) {
                if (std::abs(wrap(2 * (frames_[v] - want))) < 0.02)
                    f = static_cast<int>(v);
            }
            if (f < 0) {
                frames_.push_back(want);
                f = static_cast<int>(frames_.size()) - 1;
            }
            germ.frame = f;
        }
        // far more pieces than a clean patch of this size would need: the support was under-nucleated
        const double ideal = std::sqrt(germ.area) * 1.45 / machine_.lane_pitch;
        if (!split && germ.frame == 0 && germ.splits < 2 && germ.area > 9 && diag.pieces > 1.7 * ideal + 2 && germ.far >= 0) {
            const int k = germ.far;
            Germ child{};
            child.id = next_germ_++;
            child.x = (k % nx_ + 0.5) * grid;
            child.y = (k / nx_ + 0.5) * grid;
            child.cell = k;
            child.spacing = germ.spacing;
            child.splits = 2;
            germs_.push_back(child);
            ++germ.splits;
            split = true;
        }
        germs_[index] = germ;
    }
    return split;
}

// ---------------------------------------------------------------- tracks

// Is there still grass under a deck centred here, lying across (nx, ny)?
bool Mower::todo_at(double x, double y, double nx, double ny) const {
    int n = 0;
    for (double s = -0.45; s <= 0.451; s += 0.1) {
        const double qx = x + nx * s;
        const double qy = y + ny * s;
        if (qx < 0.1 || qy < 0.1 || qx >= width_ - 0.1 || qy >= height_ - 0.1 || read(fence_dist_, cell_of(qx, qy)) < verge + 0.05)
            continue;
        const int fi = static_cast<int>(qx / fine);
        const int fj = static_cast<int>(qy / fine);
        const int coarse = (fj >> 1) * nx_ + (fi >> 1);
        if (belief_[static_cast<std::size_t>(coarse)] == 2)
            continue;
        if (cut_[static_cast<std::size_t>(fj * cells_x_ + fi)] == 0 && read(clear_, coarse) > 0.15)
            ++n;
    }
    return n >= 2;
}

// pts: a line in order; a run is a stretch of valid points that still has grass
// under it. `meta` carries what every run cut from this line has in common.
void Mower::add_runs(const std::vector<RunPoint>& pts, int tier, bool closed, std::vector<Run>& out, const Run& meta) const {
    const int n = static_cast<int>(pts.size());
    if (n < 2)
        return;
    std::vector<std::uint8_t> todo(static_cast<std::size_t>(n), 0);
    std::vector<int> tiers(static_cast<std::size_t>(n), 0);
    for (int i = 0; i < n; ++i) {
        const RunPoint& p = pts[static_cast<std::size_t>(i)];
        if (!p.ok) {
            tiers[static_cast<std::size_t>(i)] = -1;
            continue;
        }
        const RunPoint& q = pts[static_cast<std::size_t>((i + 1) % n)];
        const RunPoint& o = pts[static_cast<std::size_t>((i + n - 1) % n)];
        double tx = q.x - o.x;
        double ty = q.y - o.y;
        double tl = length(tx, ty);
        if (tl == 0)
            tl = 1;
        tx /= tl;
        ty /= tl;
        tiers[static_cast<std::size_t>(i)] = tier;
        todo[static_cast<std::size_t>(i)] = todo_at(p.x, p.y, -ty, tx) ? 1 : 0;
    }
    int start = 0;
    if (closed) {
        int s = -1;
        for (int i = 0; i < n && s < 0; ++i) {
            if (tiers[static_cast<std::size_t>(i)] < 0 || todo[static_cast<std::size_t>(i)] == 0)
                s = i;
        }
        if (s < 0) {
            Run whole = meta;
            whole.pts = pts;
            whole.tier = tiers[0];
            whole.closed = true;
            out.push_back(whole);
            return;
        }
        start = s;
    }
    // a run may carry a short gap of cut ground, but never ends on one
    std::vector<RunPoint> cur{};
    std::vector<std::uint8_t> flags{};
    bool open = false;
    int gap = 0;
    for (int s = 0; s <= n; ++s) {
        const int i = (start + s) % n;
        const bool last = s == n;
        const bool breaks = last || tiers[static_cast<std::size_t>(i)] < 0;
        bool flush = breaks;
        if (!breaks && todo[static_cast<std::size_t>(i)] == 0 && open && gap + 1 > 8)
            flush = true;
        if (flush) {
            if (open && cur.size() >= 3) {
                while (!cur.empty() && flags.back() == 0) {
                    cur.pop_back();
                    flags.pop_back();
                }
                if (cur.size() >= 3) {
                    Run run = meta;
                    run.pts = cur;
                    run.tier = tier;
                    run.closed = false;
                    out.push_back(run);
                }
            }
            cur.clear();
            flags.clear();
            open = false;
            gap = 0;
            continue;
        }
        if (todo[static_cast<std::size_t>(i)] != 0) {
            open = true;
            cur.push_back(pts[static_cast<std::size_t>(i)]);
            flags.push_back(1);
            gap = 0;
        } else if (open) {
            ++gap;
            cur.push_back(pts[static_cast<std::size_t>(i)]);
            flags.push_back(0);
        }
    }
}

namespace {

struct Segment {
    int a{};
    int b{};
    bool used{};
};

// The segments meeting at each cell edge, in the order the edges were first met.
struct SegmentMap {
    std::vector<Segment> segments{};
    std::vector<int> first{};
    std::vector<int> second{};
    std::vector<int> order{};

    void put(int key, int segment) {
        if (first[static_cast<std::size_t>(key)] < 0) {
            first[static_cast<std::size_t>(key)] = segment;
            order.push_back(key);
        } else {
            second[static_cast<std::size_t>(key)] = segment;
        }
    }
    // Follows unused segments from an edge until the line ends or closes.
    std::vector<int> walk(int key) {
        std::vector<int> keys{};
        int at = key;
        for (;;) {
            int next = first[static_cast<std::size_t>(at)];
            if (next < 0 || segments[static_cast<std::size_t>(next)].used) {
                next = second[static_cast<std::size_t>(at)];
                if (next < 0 || segments[static_cast<std::size_t>(next)].used)
                    break;
            }
            Segment& segment = segments[static_cast<std::size_t>(next)];
            segment.used = true;
            at = segment.a == at ? segment.b : segment.a;
            keys.push_back(at);
        }
        return keys;
    }
};

constexpr int march_count[16] = {0, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 0};
constexpr int march_table[16][2][2] = {
    {{0, 0}, {0, 0}}, {{3, 0}, {0, 0}}, {{0, 1}, {0, 0}}, {{3, 1}, {0, 0}}, {{1, 2}, {0, 0}}, {{3, 0}, {1, 2}},
    {{0, 2}, {0, 0}}, {{3, 2}, {0, 0}}, {{2, 3}, {0, 0}}, {{2, 0}, {0, 0}}, {{0, 1}, {2, 3}}, {{2, 1}, {0, 0}},
    {{1, 3}, {0, 0}}, {{1, 0}, {0, 0}}, {{0, 3}, {0, 0}}, {{0, 0}, {0, 0}}};

} // namespace

// The lines along which a field takes a given value, each resampled evenly.
std::vector<Mower::Contour> Mower::contours(double level, const std::vector<float>& field) const {
    std::vector<Contour> lines{};
    SegmentMap map{};
    map.first.assign(static_cast<std::size_t>(cells_) * 2, -1);
    map.second.assign(static_cast<std::size_t>(cells_) * 2, -1);
    for (int j = 0; j < ny_ - 1; ++j) {
        for (int i = 0; i < nx_ - 1; ++i) {
            const int k = j * nx_ + i;
            const int bits = (read(field, k) > level ? 1 : 0) | (read(field, k + 1) > level ? 2 : 0) | (read(field, k + nx_ + 1) > level ? 4 : 0) |
                             (read(field, k + nx_) > level ? 8 : 0);
            if (bits == 0 || bits == 15)
                continue;
            const int edge[4] = {2 * k, 2 * (k + 1) + 1, 2 * (k + nx_), 2 * k + 1};
            for (int s = 0; s < march_count[bits]; ++s) {
                const Segment segment{edge[march_table[bits][s][0]], edge[march_table[bits][s][1]], false};
                const int index = static_cast<int>(map.segments.size());
                map.segments.push_back(segment);
                map.put(segment.a, index);
                map.put(segment.b, index);
            }
        }
    }
    for (std::size_t o = 0; o < map.order.size(); ++o) {
        const int key = map.order[o];
        for (int slot = 0; slot < 2; ++slot) {
            const int index = slot == 0 ? map.first[static_cast<std::size_t>(key)] : map.second[static_cast<std::size_t>(key)];
            if (index < 0 || map.segments[static_cast<std::size_t>(index)].used)
                continue;
            map.segments[static_cast<std::size_t>(index)].used = true;
            const Segment first = map.segments[static_cast<std::size_t>(index)];
            const std::vector<int> forward = map.walk(first.b);
            const bool closed = !forward.empty() && forward.back() == first.a;
            std::vector<int> keys{};
            if (!closed) {
                keys = map.walk(first.a);
                std::reverse(keys.begin(), keys.end());
            }
            keys.push_back(first.a);
            keys.push_back(first.b);
            keys.insert(keys.end(), forward.begin(), forward.end());
            if (closed)
                keys.pop_back();
            if (keys.size() < 4)
                continue;
            std::vector<Spot> raw{};
            for (const int edge : keys) {
                const int e = edge >> 1;
                const int i = e % nx_;
                const int j = e / nx_;
                const double a = read(field, e);
                if ((edge & 1) != 0) {
                    const double t = (level - a) / (read(field, e + nx_) - a);
                    raw.push_back({(i + 0.5) * grid, (j + 0.5 + t) * grid});
                } else {
                    const double t = (level - a) / (read(field, e + 1) - a);
                    raw.push_back({(i + 0.5 + t) * grid, (j + 0.5) * grid});
                }
            }
            // resample evenly
            Contour line{};
            line.closed = closed;
            const std::size_t total = raw.size() + (closed ? 1 : 0);
            double carry = 0;
            for (std::size_t s = 1; s < total; ++s) {
                const Spot& a = raw[s - 1];
                const Spot& b = raw[s % raw.size()];
                const double len = length(b.x - a.x, b.y - a.y);
                double t = track_step - carry;
                while (t <= len) {
                    line.pts.push_back({a.x + (b.x - a.x) * t / len, a.y + (b.y - a.y) * t / len});
                    t += track_step;
                }
                carry = std::fmod(carry + len, track_step);
            }
            if (line.pts.size() >= 4)
                lines.push_back(line);
        }
    }
    return lines;
}

// Places the machine could not get through are left alone for a while.
void Mower::shunned() {
    std::fill(shun_.begin(), shun_.end(), 0);
    std::vector<Avoid> kept{};
    for (const Avoid& a : avoid_) {
        if (a.until > time_)
            kept.push_back(a);
    }
    if (kept.size() > 12)
        kept.erase(kept.begin(), kept.end() - 12);
    avoid_.swap(kept);
    for (const Avoid& a : avoid_) {
        for (int v = -3; v <= 3; ++v) {
            for (int u = -3; u <= 3; ++u) {
                const int x = static_cast<int>(a.x / grid) + u;
                const int y = static_cast<int>(a.y / grid) + v;
                if (x >= 0 && y >= 0 && x < nx_ && y < ny_ && u * u + v * v <= 9)
                    shun_[static_cast<std::size_t>(y * nx_ + x)] = 1;
            }
        }
    }
    shun_[static_cast<std::size_t>(cell_of(pose_.x, pose_.y))] = 0;
}

// Laps: the contour of the support field a safe distance out from everything
// solid, round the fence and round each bed. (`second` shadows the first lap one
// further out; it is kept for the lawns that want it.)
void Mower::laps(double level, bool second, std::vector<Run>& runs) {
    const std::vector<std::uint8_t>& b = belief_;
    const std::vector<Contour> lines = contours(level, second ? edge_dist_ : clear_);
    for (const Contour& line : lines) {
        std::vector<Spot> pts = line.pts;
        const int n = static_cast<int>(pts.size());
        {
            std::vector<Spot> smooth = pts;
            for (int i = 0; i < n; ++i) {
                if (!line.closed && (i < 2 || i > n - 3))
                    continue;
                double x = 0;
                double y = 0;
                for (int u = -2; u <= 2; ++u) {
                    const Spot& q = pts[static_cast<std::size_t>(((i + u) % n + n) % n)];
                    x += q.x;
                    y += q.y;
                }
                smooth[static_cast<std::size_t>(i)] = Spot{x / 5, y / 5};
            }
            pts.swap(smooth);
        }
        // no part of a lap may bend tighter than the mower can steer: sharp inside corners are eased until they can be driven
        const double limit = track_step / machine_.turning_radius * 0.8;
        for (int it = 0; it < 80; ++it) {
            bool moved = false;
            std::vector<Spot> eased = pts;
            for (int i = 0; i < n; ++i) {
                if (!line.closed && (i < 1 || i > n - 2))
                    continue;
                const Spot& p = pts[static_cast<std::size_t>(i)];
                const Spot& a = pts[static_cast<std::size_t>((i + n - 1) % n)];
                const Spot& b2 = pts[static_cast<std::size_t>((i + 1) % n)];
                const double bend = std::abs(wrap(angle_of(b2.y - p.y, b2.x - p.x) - angle_of(p.y - a.y, p.x - a.x)));
                if (bend <= limit)
                    continue;
                moved = true;
                eased[static_cast<std::size_t>(i)] = Spot{p.x * 0.5 + (a.x + b2.x) * 0.25, p.y * 0.5 + (a.y + b2.y) * 0.25};
            }
            pts.swap(eased);
            if (!moved)
                break;
        }
        // counter-clockwise round a bed as seen from above: whatever is solid stays on the mower's left
        double side = 0;
        double edge = 0;
        for (int i = 0; i < n; ++i) {
            const Spot& p = pts[static_cast<std::size_t>(i)];
            const Spot& q = pts[static_cast<std::size_t>((i + 1) % n)];
            const int k = cell_of(p.x, p.y);
            side += read(grad_x_, k) * (q.y - p.y) - read(grad_y_, k) * (q.x - p.x);
            edge += outer_[static_cast<std::size_t>(k)];
        }
        const bool lawn = second || edge >= 0.3 * n;
        // beds are rounded counter-clockwise (bed on the left); the lawn's own edge is followed with the fence on the right,
        // which carries the mower counter-clockwise round the lawn as well
        if ((side > 0) != lawn)
            std::reverse(pts.begin(), pts.end());
        // the second lap shadows the first; where a tree or bed stands in its way it breaks off and that thing's own lap takes over
        std::vector<RunPoint> marked{};
        for (const Spot& p : pts) {
            const int k = cell_of(p.x, p.y);
            const std::size_t at = static_cast<std::size_t>(k);
            marked.push_back({p.x, p.y, b[at] != 2 && shun_[at] == 0 && near_seen_[at] != 0 && (!second || read(clear_, k) >= lap_gap - 0.03)});
        }
        Run meta{};
        meta.kind = second ? Kind::lap : Kind::contour;
        meta.one_way = lawn;
        add_runs(marked, second ? tier_outer2 : (lawn ? tier_outer : tier_obstacle), line.closed, runs, meta);
    }
}

void Mower::build_runs() {
    const std::vector<std::uint8_t>& b = belief_;
    std::vector<Run> runs{};
    laps(lap_gap, false, runs);
    // stripes keep off the ground the laps will cut
    std::vector<std::uint8_t> band(static_cast<std::size_t>(cells_), 0);
    for (const Run& run : runs) {
        for (const RunPoint& p : run.pts) {
            const int ci = static_cast<int>(p.x / grid);
            const int cj = static_cast<int>(p.y / grid);
            for (int v = -4; v <= 4; ++v) {
                for (int u = -4; u <= 4; ++u) {
                    const int x = ci + u;
                    const int y = cj + v;
                    if (x >= 0 && y >= 0 && x < nx_ && y < ny_ && u * u + v * v <= 17)
                        band[static_cast<std::size_t>(y * nx_ + x)] = 1;
                }
            }
        }
    }
    if (!axis_locked_)
        choose_axis();
    const double pitch = machine_.lane_pitch;
    for (std::size_t f = 0; f < frames_.size(); ++f) {
        const double a = frames_[f];
        const double ux = cosine(a);
        const double uy = sine(a);
        const double nx = -uy;
        const double ny = ux;
        const double corner_x[4] = {0, width_, 0, width_};
        const double corner_y[4] = {0, 0, height_, height_};
        double pn_low = 0;
        double pn_high = 0;
        double pu_low = 0;
        double pu_high = 0;
        for (int k = 0; k < 4; ++k) {
            const double pn = corner_x[k] * nx + corner_y[k] * ny;
            const double pu = corner_x[k] * ux + corner_y[k] * uy;
            pn_low = k == 0 ? pn : smaller(pn_low, pn);
            pn_high = k == 0 ? pn : larger(pn_high, pn);
            pu_low = k == 0 ? pu : smaller(pu_low, pu);
            pu_high = k == 0 ? pu : larger(pu_high, pu);
        }
        for (double k = std::ceil(pn_low / pitch); k <= pn_high / pitch; k += 1) {
            std::vector<RunPoint> pts{};
            for (double t = pu_low; t <= pu_high; t += track_step) {
                const double x = nx * k * pitch + ux * t;
                const double y = ny * k * pitch + uy * t;
                if (x < 0 || y < 0 || x >= width_ || y >= height_)
                    continue;
                const int q = cell_of(x, y);
                const std::size_t at = static_cast<std::size_t>(q);
                pts.push_back({x, y,
                               b[at] != 2 && near_seen_[at] != 0 && shun_[at] == 0 && band[at] == 0 && read(clear_, q) >= pass_gap &&
                                   frame_of(q) == static_cast<int>(f)});
            }
            Run meta{};
            meta.kind = Kind::stripe;
            meta.frame = static_cast<int>(f);
            meta.lane = static_cast<int>(k);
            add_runs(pts, tier_stripe, false, runs, meta);
        }
    }
    // a track must actually go somewhere: scraps shorter than the deck is long are not offered;
    // nor is anything no route leads to
    std::vector<Run> kept{};
    for (Run& run : runs) {
        double len = 0;
        for (std::size_t i = 1; i < run.pts.size() && len < 0.7; ++i)
            len += length(run.pts[i].x - run.pts[i - 1].x, run.pts[i].y - run.pts[i - 1].y);
        if (!(len >= 0.7))
            continue;
        bool can = false;
        if (!run.closed && !run.one_way) {
            const RunPoint& head = run.pts.front();
            const RunPoint& tail = run.pts.back();
            can = read(travel_, cell_of(head.x, head.y)) < 1e8 || read(travel_, cell_of(tail.x, tail.y)) < 1e8;
        } else {
            for (std::size_t i = 0; i + 2 < run.pts.size() && !can; i += 2)
                can = read(travel_, cell_of(run.pts[i].x, run.pts[i].y)) < 1e8;
        }
        if (can)
            kept.push_back(std::move(run));
    }
    if (kept.empty())
        tufts(kept);
    if (kept.empty())
        scout(kept);
    runs_.swap(kept);
}

// Which way should the stripes run? Lay each candidate direction over the ground
// seen so far and count what it would cost: the length to drive, plus a charge
// for every separate piece, since each piece ends in a turn.
void Mower::choose_axis() {
    const double candidates[4] = {pi / 4, 0, pi / 2, -pi / 4};
    const double pitch = machine_.lane_pitch;
    bool found = false;
    double best_axis = 0;
    double best_cost = 0;
    for (const double a : candidates) {
        const double ux = cosine(a);
        const double uy = sine(a);
        const double nx = -uy;
        const double ny = ux;
        const double corner_x[4] = {0, width_, 0, width_};
        const double corner_y[4] = {0, 0, height_, height_};
        double pn_low = 0;
        double pn_high = 0;
        double pu_low = 0;
        double pu_high = 0;
        for (int k = 0; k < 4; ++k) {
            const double pn = corner_x[k] * nx + corner_y[k] * ny;
            const double pu = corner_x[k] * ux + corner_y[k] * uy;
            pn_low = k == 0 ? pn : smaller(pn_low, pn);
            pn_high = k == 0 ? pn : larger(pn_high, pn);
            pu_low = k == 0 ? pu : smaller(pu_low, pu);
            pu_high = k == 0 ? pu : larger(pu_high, pu);
        }
        int count = 0;
        double len = 0;
        for (double k = std::ceil(pn_low / pitch); k <= pn_high / pitch; k += 1) {
            bool on = false;
            for (double t = pu_low; t <= pu_high; t += 0.3) {
                const double x = nx * k * pitch + ux * t;
                const double y = ny * k * pitch + uy * t;
                bool ok = x > 0 && y > 0 && x < width_ && y < height_;
                if (ok) {
                    const int q = cell_of(x, y);
                    ok = belief_[static_cast<std::size_t>(q)] == 1 && read(clear_, q) >= pass_gap &&
                         cut_[static_cast<std::size_t>(static_cast<int>(y / fine) * cells_x_ + static_cast<int>(x / fine))] == 0;
                }
                if (ok) {
                    len += 0.3;
                    if (!on)
                        ++count;
                }
                on = ok;
            }
        }
        const double cost = len / machine_.speed + count * 4.5;
        if (!found || cost < best_cost - (a == pi / 4 ? 0 : 1e-9)) {
            found = true;
            best_axis = a;
            best_cost = cost;
        }
    }
    if (found)
        frames_[0] = best_axis;
}

// The nearest place it can stand that borders ground it has not seen.
void Mower::scout(std::vector<Run>& runs) const {
    int best = -1;
    double best_distance = 1e8;
    for (int j = 1; j < ny_ - 1; ++j) {
        for (int i = 1; i < nx_ - 1; ++i) {
            const int k = j * nx_ + i;
            const std::size_t at = static_cast<std::size_t>(k);
            if (belief_[at] != 1 || read(travel_, k) >= best_distance || !passable(k) || read(clear_, k) < 0.8)
                continue;
            if (belief_[at - 1] == 0 || belief_[at + 1] == 0 || belief_[at - static_cast<std::size_t>(nx_)] == 0 ||
                belief_[at + static_cast<std::size_t>(nx_)] == 0) {
                best_distance = read(travel_, k);
                best = k;
            }
        }
    }
    if (best < 0)
        return;
    const double x = (best % nx_ + 0.5) * grid;
    const double y = (best / nx_ + 0.5) * grid;
    double d = length(x - pose_.x, y - pose_.y);
    if (d == 0)
        d = 1;
    const double ux = (x - pose_.x) / d;
    const double uy = (y - pose_.y) / d;
    Run run{};
    run.pts.push_back({x - ux * 0.3, y - uy * 0.3, false});
    run.pts.push_back({x - ux * 0.15, y - uy * 0.15, false});
    run.pts.push_back({x, y, false});
    run.tier = tier_spot;
    run.kind = Kind::tuft;
    run.lane = -1 - best;
    runs.push_back(run);
}

// Does this planner cell still hold grass the deck could get over?
bool Mower::standing(const std::vector<std::uint8_t>& reach, int cell) const {
    const std::size_t at = static_cast<std::size_t>(cell);
    if (belief_[at] == 2 || reach[at] == 0 || read(clear_, cell) < 0.25 || read(fence_dist_, cell) < verge + 0.05)
        return false;
    const int i = cell % nx_;
    const int j = cell / nx_;
    const std::size_t f = static_cast<std::size_t>(j * 2 * cells_x_ + i * 2);
    const std::size_t row = static_cast<std::size_t>(cells_x_);
    return cut_[f] == 0 || cut_[f + 1] == 0 || cut_[f + row] == 0 || cut_[f + row + 1] == 0;
}

// What is left standing once the lines are done: each patch becomes a short line
// to drive over it.
void Mower::tufts(std::vector<Run>& runs) const {
    const std::vector<float>& c = clear_;
    const std::size_t cells = static_cast<std::size_t>(cells_);
    std::vector<std::uint8_t> seen(cells, 0);
    std::vector<std::uint8_t> reach(cells, 0);
    // only grass the deck can actually get over counts: within its half-width of ground a pose may stand on
    for (int k = 0; k < cells_; ++k) {
        if (!passable(k))
            continue;
        const int i = k % nx_;
        const int j = k / nx_;
        for (int v = -4; v <= 4; ++v) {
            for (int u = -4; u <= 4; ++u) {
                const int x = i + u;
                const int y = j + v;
                if (x >= 0 && y >= 0 && x < nx_ && y < ny_ && u * u + v * v <= 16)
                    reach[static_cast<std::size_t>(y * nx_ + x)] = 1;
            }
        }
    }
    std::vector<int> patch{};
    for (int k0 = 0; k0 < cells_; ++k0) {
        if (seen[static_cast<std::size_t>(k0)] != 0 || !standing(reach, k0))
            continue;
        patch.clear();
        patch.push_back(k0);
        seen[static_cast<std::size_t>(k0)] = 1;
        for (std::size_t h = 0; h < patch.size(); ++h) {
            const int k = patch[h];
            const int i = k % nx_;
            const int j = k / nx_;
            for (int v = -1; v <= 1; ++v) {
                for (int u = -1; u <= 1; ++u) {
                    const int x = i + u;
                    const int y = j + v;
                    if (x < 0 || y < 0 || x >= nx_ || y >= ny_)
                        continue;
                    const int q = y * nx_ + x;
                    if (seen[static_cast<std::size_t>(q)] == 0 && standing(reach, q)) {
                        seen[static_cast<std::size_t>(q)] = 1;
                        patch.push_back(q);
                    }
                }
            }
        }
        if (patch.size() < 4)
            continue;  // a speck is not worth a trip
        const double size = static_cast<double>(patch.size());
        double mx = 0;
        double my = 0;
        for (const int k : patch) {
            mx += k % nx_ + 0.5;
            my += k / nx_ + 0.5;
        }
        mx *= grid / size;
        my *= grid / size;
        double a = 0;
        double bb = 0;
        double d = 0;
        for (const int k : patch) {
            const double x = (k % nx_ + 0.5) * grid - mx;
            const double y = (k / nx_ + 0.5) * grid - my;
            a += x * x;
            bb += x * y;
            d += y * y;
        }
        const double ang = 0.5 * angle_of(2 * bb, a - d);
        const double ux = cosine(ang);
        const double uy = sine(ang);
        double ext = 0;
        for (const int k : patch)
            ext = larger(ext, std::abs(((k % nx_ + 0.5) * grid - mx) * ux + ((k / nx_ + 0.5) * grid - my) * uy));
        const int key = cell_of(mx, my);
        const std::map<int, int>::const_iterator tried = tuft_tries_.find(key);
        if (tried != tuft_tries_.end() && (*tried).second > 2)
            continue;
        // a pose that can stand over it: on the tuft's own line if possible, else the nearest allowed ground
        Run run{};
        for (double t = -ext - 0.15; t <= ext + 0.151; t += track_step) {
            const double x = mx + ux * t;
            const double y = my + uy * t;
            if (x > 0 && y > 0 && x < width_ && y < height_ && passable(cell_of(x, y)) && read(c, cell_of(x, y)) >= 0.8)
                run.pts.push_back({x, y, false});
        }
        if (run.pts.size() < 2) {
            int best = -1;
            double best_distance = 0.5;
            for (int v = -5; v <= 5; ++v) {
                for (int u = -5; u <= 5; ++u) {
                    const int x = static_cast<int>(mx / grid) + u;
                    const int y = static_cast<int>(my / grid) + v;
                    if (x < 0 || y < 0 || x >= nx_ || y >= ny_ || !passable(y * nx_ + x) || read(c, y * nx_ + x) < 0.8)
                        continue;
                    const double dd = length((x + 0.5) * grid - mx, (y + 0.5) * grid - my);
                    if (dd < best_distance) {
                        best_distance = dd;
                        best = y * nx_ + x;
                    }
                }
            }
            if (best < 0)
                continue;
            const double px = (best % nx_ + 0.5) * grid;
            const double py = (best / nx_ + 0.5) * grid;
            run.pts.clear();
            run.pts.push_back({px, py, false});
            run.pts.push_back({px + (mx - px) * 0.2 + 0.01, py + (my - py) * 0.2, false});
        }
        run.tier = tier_spot;
        run.kind = Kind::tuft;
        run.lane = key;
        runs.push_back(run);
    }
}

// ---------------------------------------------------------------- traversal

bool Mower::passable(int cell) const {
    const std::size_t at = static_cast<std::size_t>(cell);
    return belief_[at] != 2 && read(clear_, cell) >= pass_gap && shun_[at] == 0;
}

// The cost of driving from the mower to everywhere it can get to.
void Mower::transit() {
    std::fill(travel_.begin(), travel_.end(), static_cast<float>(far_off));
    std::fill(travel_from_.begin(), travel_from_.end(), -1);
    Heap heap(cells_);
    const int start = cell_of(pose_.x, pose_.y);
    const int sx = start % nx_;
    const int sy = start / nx_;
    write(travel_, start, 0);
    heap.push(0, start);
    const double tight = machine_.body_radius - 0.06;
    while (heap.count > 0) {
        const int k = heap.pop();
        const double d = heap.top_key;
        if (d > read(travel_, k))
            continue;
        const int i = k % nx_;
        const int j = k / nx_;
        for (int n = 0; n < 8; ++n) {
            const int x = i + step_x[n];
            const int y = j + step_y[n];
            if (x < 0 || y < 0 || x >= nx_ || y >= ny_)
                continue;
            const int q = y * nx_ + x;
            const std::uint8_t known = belief_[static_cast<std::size_t>(q)];
            const double room = read(clear_, q);
            const bool pass = passable(q);
            // left tight against something, it may back out through ground too close to plan on
            if (!pass && !(known != 2 && room >= tight && std::abs(x - sx) <= 9 && std::abs(y - sy) <= 9))
                continue;
            const double w =
                (n < 4 ? grid : grid * 1.4142) * (known == 1 ? 1 : 2.2) * (1 + crowd_[static_cast<std::size_t>(q)] + (room < 0.85 ? 0.8 : 0)) * (pass ? 1 : 25);
            if (d + w < read(travel_, q)) {
                write(travel_, q, d + w);
                travel_from_[static_cast<std::size_t>(q)] = k;
                heap.push(d + w, q);
            }
        }
    }
}

// Distances through the lawn from one cell, for ordering the clean-up.
// Only the distances to `wanted` cells are read afterwards, and a cell's distance
// is final once the front has passed it, so the front stops when it has them all.
std::vector<float> Mower::spread(int start, const std::vector<int>& wanted) const {
    std::vector<float> dist(static_cast<std::size_t>(cells_), static_cast<float>(far_off));
    std::vector<std::uint8_t> waiting(static_cast<std::size_t>(cells_), 0);
    int left = 0;
    for (const int cell : wanted) {
        if (waiting[static_cast<std::size_t>(cell)] == 0) {
            waiting[static_cast<std::size_t>(cell)] = 1;
            ++left;
        }
    }
    Heap heap(cells_);
    write(dist, start, 0);
    heap.push(0, start);
    while (heap.count > 0 && left > 0) {
        const int k = heap.pop();
        const double d = heap.top_key;
        if (d > read(dist, k))
            continue;
        if (waiting[static_cast<std::size_t>(k)] != 0) {
            waiting[static_cast<std::size_t>(k)] = 0;
            --left;
        }
        const int i = k % nx_;
        const int j = k / nx_;
        for (int n = 0; n < 8; ++n) {
            const int x = i + step_x[n];
            const int y = j + step_y[n];
            if (x < 0 || y < 0 || x >= nx_ || y >= ny_)
                continue;
            const int q = y * nx_ + x;
            if (!passable(q))
                continue;
            const double w = (n < 4 ? grid : grid * 1.4142) * (1 + crowd_[static_cast<std::size_t>(q)]);
            if (d + w < read(dist, q)) {
                write(dist, q, d + w);
                heap.push(d + w, q);
            }
        }
    }
    return dist;
}

bool Mower::clear_line(double ax, double ay, double bx, double by, bool wide) const {
    const int n = static_cast<int>(std::ceil(length(bx - ax, by - ay) / 0.05));
    for (int s = 0; s <= n; ++s) {
        // two points on one spot have no line between them; the reference model then looks at the lawn's corner
        const int k = n == 0 ? 0 : cell_of(ax + (bx - ax) * s / n, ay + (by - ay) * s / n);
        if (!passable(k) || (wide && read(clear_, k) < 0.85))
            return false;
    }
    return true;
}

// The cheapest way to a cell, pulled taut wherever a straight line is clear.
std::vector<Mower::Spot> Mower::route(int cell, bool wide) const {
    std::vector<Spot> pts{};
    for (int k = cell; k >= 0; k = travel_from_[static_cast<std::size_t>(k)])
        pts.push_back({(k % nx_ + 0.5) * grid, (k / nx_ + 0.5) * grid});
    std::reverse(pts.begin(), pts.end());
    if (!pts.empty())
        pts[0] = Spot{pose_.x, pose_.y};
    std::vector<Spot> out{};
    const int count = static_cast<int>(pts.size());
    int i = 0;
    while (i < count - 1) {
        int j = std::min(count - 1, i + 80);
        while (j > i + 1 && !clear_line(pts[static_cast<std::size_t>(i)].x, pts[static_cast<std::size_t>(i)].y, pts[static_cast<std::size_t>(j)].x,
                                        pts[static_cast<std::size_t>(j)].y, wide))
            --j;
        out.push_back(pts[static_cast<std::size_t>(j)]);
        i = j;
    }
    return out;
}

// ---------------------------------------------------------------- moves

// Every transition is drawn before it is driven. A move is a list of pieces; each
// piece is a run of points driven in one gear. Nothing here is steering: these are
// curves checked against the turning circle and against everything known.

// Moves keep a margin from what is solid, except where the mower already stands.
bool Mower::room_at(double x, double y) const {
    const bool near = length(x - pose_.x, y - pose_.y) < 0.7;
    const double g = near ? 0.005 : 0.08;
    const double body = machine_.body_radius;
    if (!(x > body + g && y > body + g && x < width_ - body - g && y < height_ - body - g))
        return false;
    const int k = cell_of(x, y);
    return belief_[static_cast<std::size_t>(k)] != 2 && read(clear_, k) >= body + (near ? -0.04 : 0.08);
}

// Cubic Hermite from one pose to another, with the stiffness of each end swept;
// the gentlest curve that fits wins.
Mower::Curve Mower::hermite(Spot p0, double h0, Spot p1, double h1) const {
    Curve best{};
    const double span = length(p1.x - p0.x, p1.y - p0.y);
    if (span < 0.05) {
        if (std::abs(wrap(h1 - h0)) < 0.3) {
            best.pts.push_back(p1);
            best.len = span;
            best.bend = 0;
            // no cost is given for standing still: it compares as neither better nor worse than anything
            best.cost = std::numeric_limits<double>::quiet_NaN();
            best.valid = true;
        }
        return best;
    }
    const double radius = machine_.turning_radius;
    const double kmax = 1 / radius * 1.03;
    const double stiff[5] = {0.7, 1.1, 1.6, 2.3, 3.2};
    const double c0 = cosine(h0);
    const double s0 = sine(h0);
    const double c1 = cosine(h1);
    const double s1 = sine(h1);
    std::vector<Spot> pts{};
    for (const double a : stiff) {
        for (const double b : stiff) {
            const double m0x = c0 * a * span;
            const double m0y = s0 * a * span;
            const double m1x = c1 * b * span;
            const double m1y = s1 * b * span;
            const int n = std::max(12, static_cast<int>(std::ceil(span * (a + b) / 0.16)));
            pts.clear();
            bool ok = true;
            double len = 0;
            double bend = 0;
            double ph = h0;
            double px = p0.x;
            double py = p0.y;
            for (int i = 1; i <= n && ok; ++i) {
                const double t = static_cast<double>(i) / n;
                const double t2 = t * t;
                const double t3 = t2 * t;
                const double x = (2 * t3 - 3 * t2 + 1) * p0.x + (t3 - 2 * t2 + t) * m0x + (-2 * t3 + 3 * t2) * p1.x + (t3 - t2) * m1x;
                const double y = (2 * t3 - 3 * t2 + 1) * p0.y + (t3 - 2 * t2 + t) * m0y + (-2 * t3 + 3 * t2) * p1.y + (t3 - t2) * m1y;
                const double ds = length(x - px, y - py);
                if (ds < 1e-6)
                    continue;
                const double h = angle_of(y - py, x - px);
                const double dh = std::abs(wrap(h - ph));
                if ((i > 1 && dh / ds > kmax) || (i == 1 && dh > 0.35) || !room_at(x, y)) {
                    ok = false;
                    break;
                }
                len += ds;
                bend += dh * dh / ds;
                ph = h;
                px = x;
                py = y;
                pts.push_back({x, y});
            }
            if (!ok || std::abs(wrap(h1 - ph)) > 0.35)
                continue;
            const double cost = len + 1.2 * bend * radius * radius;
            if (!best.valid || cost < best.cost) {
                best.pts = pts;
                best.len = len;
                best.bend = bend;
                best.cost = cost;
                best.valid = true;
            }
        }
    }
    return best;
}

// Arc, straight, arc: the shapes a tractor makes when one sweep will not do. Tried
// at several sizes of circle; with the Hermite above, the gentlest curve that fits
// is the one driven, so turns are loose wherever there is room.
Mower::Curve Mower::curve(Spot p0, double h0, Spot p1, double h1) const {
    Curve best = hermite(p0, h0, p1, h1);
    const double radius = machine_.turning_radius;
    const double circles[4] = {radius * 2.4, radius * 1.6, radius * 1.05, radius};
    const int locks[4][2] = {{1, 1}, {-1, -1}, {1, -1}, {-1, 1}};
    for (const double r : circles) {
        for (int w = 0; w < 4; ++w) {
            const int s0 = locks[w][0];
            const int s1 = locks[w][1];
            const double c0x = p0.x - s0 * r * sine(h0);
            const double c0y = p0.y + s0 * r * cosine(h0);
            const double c1x = p1.x - s1 * r * sine(h1);
            const double c1y = p1.y + s1 * r * cosine(h1);
            const double d = length(c1x - c0x, c1y - c0y);
            const double th = angle_of(c1y - c0y, c1x - c0x);
            double hs = th;
            if (s0 != s1) {
                if (d < 2 * r)
                    continue;
                hs = th + s0 * arcsine(2 * r / d);
            }
            const double a0 = turn_of(s0 * (hs - h0));
            const double a1 = turn_of(s1 * (h1 - hs));
            const double run = s0 == s1 ? d : std::sqrt(larger(d * d - 4 * r * r, 0));
            if (a0 > 5.2 || a1 > 5.2)
                continue;
            Arc first{};
            first.end = p0;
            first.heading = h0;
            first.valid = true;
            if (a0 > 1e-3)
                first = arc(p0, h0, s0, a0, 1, r);
            if (!first.valid)
                continue;
            std::vector<Spot> pts = first.pts;
            bool ok = true;
            const int n = static_cast<int>(std::ceil(run / 0.12));
            for (int i = 1; i <= n && ok; ++i) {
                const double x = first.end.x + cosine(hs) * run * i / n;
                const double y = first.end.y + sine(hs) * run * i / n;
                if (!room_at(x, y))
                    ok = false;
                else
                    pts.push_back({x, y});
            }
            if (!ok)
                continue;
            const Spot e = pts.empty() ? p0 : pts.back();
            Arc second{};
            second.end = e;
            second.heading = hs;
            second.valid = true;
            if (a1 > 1e-3)
                second = arc(e, hs, s1, a1, 1, r);
            if (!second.valid)
                continue;
            if (length(second.end.x - p1.x, second.end.y - p1.y) > 0.015)
                continue;
            pts.insert(pts.end(), second.pts.begin(), second.pts.end());
            if (pts.empty())
                continue;
            const double len = first.len + run + second.len;
            const double bend = (a0 + a1) / r;
            const double cost = len + 1.2 * bend * radius * radius;
            if (!best.valid || cost < best.cost) {
                best.pts.swap(pts);
                best.len = len;
                best.bend = bend;
                best.cost = cost;
                best.valid = true;
            }
        }
    }
    return best;
}

// An arc on a given lock, forward or in reverse, as far as it stays clear.
// turn > 0 is to the left.
Mower::Arc Mower::arc(Spot p, double heading, int turn, double angle, int dir, double radius) const {
    Arc result{};
    const int n = std::max(2, static_cast<int>(std::ceil(angle * radius / 0.1)));
    double x = p.x;
    double y = p.y;
    double a = heading;
    for (int i = 1; i <= n; ++i) {
        a = heading + turn * angle * i / n;
        x = p.x + (dir * turn) * radius * (sine(a) - sine(heading));
        y = p.y - (dir * turn) * radius * (cosine(a) - cosine(heading));
        if (!room_at(x, y))
            return Arc{};
        result.pts.push_back({x, y});
    }
    result.end = Spot{x, y};
    result.heading = wrap(a);
    result.len = angle * radius;
    result.valid = true;
    return result;
}

// Straight back.
Mower::Arc Mower::back(Spot p, double heading, double distance) const {
    Arc result{};
    const int n = static_cast<int>(std::ceil(distance / 0.1));
    for (int i = 1; i <= n; ++i) {
        const double x = p.x - cosine(heading) * distance * i / n;
        const double y = p.y - sine(heading) * distance * i / n;
        if (!room_at(x, y))
            return Arc{};
        result.pts.push_back({x, y});
    }
    result.end = result.pts.back();
    result.heading = heading;
    result.len = distance;
    result.valid = true;
    return result;
}

// One hop between two poses: a single forward curve if one fits; otherwise a
// planned turn with one or two changes of gear.
Mower::Move Mower::hop(Spot p0, double h0, Spot p1, double h1) const {
    Move best{};
    {
        Curve f = curve(p0, h0, p1, h1);
        if (f.valid) {
            Piece piece{};
            piece.pts.swap(f.pts);
            best.pieces.push_back(piece);
            best.cost = f.cost;
            best.valid = true;
            return best;
        }
    }
    const double lock = machine_.turning_radius * 1.05;
    // back off first (straight, or on either lock), then one forward curve
    std::vector<Arc> backs{};
    const double straight[3] = {0.6, 1.0, 1.5};
    for (const double d : straight) {
        const Arc b = back(p0, h0, d);
        if (b.valid)
            backs.push_back(b);
    }
    const int sides[2] = {1, -1};
    const double swings[4] = {0.5, 0.9, 1.3, 1.7};
    for (const int s : sides) {
        for (const double angle : swings) {
            const Arc b = arc(p0, h0, s, angle, -1, lock);
            if (b.valid)
                backs.push_back(b);
        }
    }
    for (const Arc& b : backs) {
        const Curve f2 = curve(b.end, b.heading, p1, h1);
        if (!f2.valid)
            continue;
        const double cost = b.len * 1.3 + f2.cost + 4;
        if (!best.valid || cost < best.cost) {
            best.pieces.clear();
            best.pieces.push_back(Piece{b.pts, -1});
            best.pieces.push_back(Piece{f2.pts, 1});
            best.cost = cost;
            best.cusps = 1;
            best.valid = true;
        }
    }
    // swing forward on one lock, back on the other, then forward: the three-point turn
    const double turns[3] = {0.5, 0.9, 1.3};
    for (const int s : sides) {
        for (const double a1 : turns) {
            const Arc f1 = arc(p0, h0, s, a1, 1, lock);
            if (!f1.valid)
                continue;
            for (const double a2 : turns) {
                const Arc b = arc(f1.end, f1.heading, s, a2, -1, lock);
                if (!b.valid)
                    continue;
                const Curve f2 = curve(b.end, b.heading, p1, h1);
                if (!f2.valid)
                    continue;
                const double cost = f1.len + b.len * 1.3 + f2.cost + 7;
                if (!best.valid || cost < best.cost) {
                    best.pieces.clear();
                    best.pieces.push_back(Piece{f1.pts, 1});
                    best.pieces.push_back(Piece{b.pts, -1});
                    best.pieces.push_back(Piece{f2.pts, 1});
                    best.cost = cost;
                    best.cusps = 2;
                    best.valid = true;
                }
            }
        }
    }
    return best;
}

// The whole way from where the mower is to the start of a line. Close by, one
// planned hop. Further off: a planned departure onto the route, the route itself
// driven loosely (it crosses ground already dealt with), and a planned arrival.
Mower::Move Mower::transition(Spot p0, double h0, Spot p1, double h1) const {
    const Move direct = hop(p0, h0, p1, h1);
    const double span = length(p1.x - p0.x, p1.y - p0.y);
    if (direct.valid && direct.cusps == 0 && span < 5)
        return direct;
    if (read(travel_, cell_of(p1.x, p1.y)) > 1e8)
        return direct;
    std::vector<Spot> way{};
    way.push_back(p0);
    {
        const std::vector<Spot> through = route(cell_of(p1.x, p1.y), true);
        way.insert(way.end(), through.begin(), through.end());
    }
    way.push_back(p1);
    std::vector<Spot> dense{};
    for (std::size_t i = 1; i < way.size(); ++i) {
        const Spot& a = way[i - 1];
        const Spot& b = way[i];
        const int n = std::max(1, static_cast<int>(std::ceil(length(b.x - a.x, b.y - a.y) / 0.15)));
        for (int k = 1; k <= n; ++k)
            dense.push_back({a.x + (b.x - a.x) * k / n, a.y + (b.y - a.y) * k / n});
    }
    // last resort, so that reachable work is never abandoned: follow the route as it is and settle onto the line on arrival
    Move rough{};
    if (!dense.empty()) {
        // from a tight spot the rough way begins by drawing straight back into the open, so the nose can come round
        if (read(clear_, cell_of(p0.x, p0.y)) < 0.85) {
            const double draws[3] = {1.0, 0.7, 0.4};
            for (const double d : draws) {
                const Arc b = back(p0, h0, d);
                if (b.valid) {
                    rough.pieces.push_back(Piece{b.pts, -1});
                    break;
                }
            }
        }
        const int pre = static_cast<int>(rough.pieces.size());
        rough.pieces.push_back(Piece{dense, 1});
        rough.cost = static_cast<double>(dense.size()) * 0.15 + 6 + pre * 3;
        rough.cusps = pre;
        rough.valid = true;
    }
    if (dense.size() < 30)
        return direct.valid ? direct : rough;
    // the way round: a chain of drawn curves through the route's corners, each one checked like any other move
    struct Via {
        Spot at{};
        double heading{};
    };
    std::vector<Via> vias{};
    for (std::size_t i = 1; i + 1 < way.size(); ++i) {
        const Spot& q = way[i];
        if (length(q.x - p0.x, q.y - p0.y) < 1.4 || length(q.x - p1.x, q.y - p1.y) < 1.4 ||
            (!vias.empty() && length(q.x - vias.back().at.x, q.y - vias.back().at.y) < 1.4))
            continue;
        const Spot& a = way[i - 1];
        const Spot& b = way[i + 1];
        vias.push_back(Via{q, angle_of(b.y - a.y, b.x - a.x)});
    }
    vias.push_back(Via{p1, h1});
    Move chain{};
    Spot p = p0;
    double h = h0;
    bool ok = true;
    const int count = static_cast<int>(vias.size());
    for (int i = 0; i < count && ok; ++i) {
        Move leg{};
        int j = i;
        // if a corner cannot be met cleanly, look past it to the next
        for (; j < count; ++j) {
            const Via& v = vias[static_cast<std::size_t>(j)];
            if (i == 0 && j == 0) {
                leg = hop(p, h, v.at, v.heading);
            } else {
                Curve c = curve(p, h, v.at, v.heading);
                if (c.valid) {
                    Piece piece{};
                    piece.pts.swap(c.pts);
                    leg.pieces.push_back(piece);
                    leg.cost = c.cost;
                    leg.cusps = 0;
                    leg.valid = true;
                } else if (j == count - 1) {
                    leg = hop(p, h, v.at, v.heading);
                }
            }
            if (leg.valid)
                break;
        }
        if (!leg.valid) {
            ok = false;
            break;
        }
        for (const Piece& piece : leg.pieces)
            chain.pieces.push_back(piece);
        chain.cost += leg.cost;
        chain.cusps += leg.cusps;
        p = vias[static_cast<std::size_t>(j)].at;
        h = vias[static_cast<std::size_t>(j)].heading;
        i = j;
    }
    if (ok && (!direct.valid || chain.cost < direct.cost)) {
        chain.valid = true;
        return chain;
    }
    return direct.valid ? direct : rough;
}

// ---------------------------------------------------------------- choosing

double Mower::distance_from(const Stance& stance, double x, double y) const {
    if (stance.crow)
        return 1.15 * length(x - stance.x, y - stance.y);
    return read(travel_, cell_of(x, y));
}

// A mower does not spin on the spot for free: every radian it must turn costs like ground driven.
double Mower::turn_for(const Stance& stance, double bearing, double gap, double angle) const {
    return std::abs(wrap(bearing - stance.heading)) * smaller(1, gap / 0.6) + std::abs(wrap(angle - bearing));
}

bool Mower::lane_open(double x, double y, double nx, double ny) const {
    return todo_at(x, y, nx, ny) && passable(cell_of(x, y));
}

// How good is this line as the next thing to do, from a given pose? Says where to
// join it and which way to run.
Mower::Offer Mower::offer(Run& run, const Stance& stance) {
    const int n = static_cast<int>(run.pts.size());
    const bool lap = run.closed || run.one_way;
    int ends[2] = {0, n - 1};
    int count = 2;
    if (lap) {
        // a lap is joined wherever is handiest, but joining late leaves its start for another trip, which costs
        count = 0;
        int best_i = -1;
        double best_d = 1e8;
        for (int i = 0; i < n - 2; i += 2) {
            const RunPoint& p = run.pts[static_cast<std::size_t>(i)];
            const double d = distance_from(stance, p.x, p.y) + (run.closed ? 0 : 0.25 * i * track_step);
            if (d < best_d) {
                best_d = d;
                best_i = i;
            }
        }
        if (best_i >= 0) {
            ends[0] = best_i;
            count = 1;
        }
    }
    Offer best{};
    for (int w = 0; w < count; ++w) {
        const int e = ends[w];
        const RunPoint& p = run.pts[static_cast<std::size_t>(e)];
        const double d = distance_from(stance, p.x, p.y);
        if (d > 1e7)
            continue;
        const RunPoint& q = lap ? run.pts[static_cast<std::size_t>((e + 1) % n)] : run.pts[static_cast<std::size_t>(e == 0 ? 1 : n - 2)];
        double dir = angle_of(q.y - p.y, q.x - p.x);
        const double gap = length(p.x - stance.x, p.y - stance.y);
        const double bearing = gap > 0.6 ? angle_of(p.y - stance.y, p.x - stance.x) : stance.heading;
        double turn = turn_for(stance, bearing, gap, dir);
        bool rev = false;
        if (run.closed && !run.one_way && turn_for(stance, bearing, gap, dir + pi) < turn) {
            dir += pi;
            turn = turn_for(stance, bearing, gap, dir);
            rev = true;
        }
        // the order of work is a preference; what is near and straight ahead can outweigh it
        double score = d + 2.0 * turn + (n < 8 ? 2 : 0) + tier_cost[run.tier] - (gap < 2.5 && std::abs(wrap(bearing - stance.heading)) < 0.5 ? 1.5 : 0) -
                       (stance.kind == Kind::contour && run.kind == Kind::contour && gap < 2.5 ? 4 : 0);
        if (run.tier == tier_stripe) {
            // pursue the edge just cut: the next lane over comes first, a lane with tall grass on both sides last
            if (run.sides < 0) {
                const double a = frames_[static_cast<std::size_t>(run.frame)];
                const double nx = -sine(a);
                const double ny = cosine(a);
                const RunPoint& mid = run.pts[static_cast<std::size_t>(n >> 1)];
                const double pitch = machine_.lane_pitch;
                run.sides = (lane_open(mid.x + nx * pitch, mid.y + ny * pitch, nx, ny) ? 1 : 0) +
                            (lane_open(mid.x + nx * -pitch, mid.y + ny * -pitch, nx, ny) ? 1 : 0);
            }
            score += run.sides == 2 ? 6 : (run.sides == 0 ? -1 : 0);
            if (stance.has_last && stance.last_frame == run.frame) {
                const int dl = std::abs(run.lane - stance.last_lane);
                score += dl == 1 || dl == 2 ? -3 : (dl == 0 ? -1.5 : std::min(dl - 2, 4));
            }
        }
        if (!best.valid || score < best.score) {
            best.score = score;
            best.end = e;
            best.rev = rev;
            best.dir = dir;
            best.travel = d + 2.0 * turn;
            best.valid = true;
        }
    }
    return best;
}

// The line's points in the order an offer would drive them.
std::vector<Mower::RunPoint> Mower::ordered(const Run& run, const Offer& taken) const {
    const int n = static_cast<int>(run.pts.size());
    std::vector<RunPoint> pts{};
    if (run.closed) {
        for (int s = 0; s < n; ++s)
            pts.push_back(run.pts[static_cast<std::size_t>(((taken.rev ? taken.end - s : taken.end + s) % n + n) % n)]);
        return pts;
    }
    if (run.one_way) {
        pts.assign(run.pts.begin() + taken.end, run.pts.end());
        return pts;
    }
    pts = run.pts;
    if (taken.end != 0)
        std::reverse(pts.begin(), pts.end());
    return pts;
}

// What would be best to do after a line is finished, standing at its end?
// Returns the run's place on the menu, or -1.
int Mower::after(const Run& run, const Offer& taken, const Stance& start, int skip_a, int skip_b, Offer& found) {
    const std::vector<RunPoint> pts = ordered(run, taken);
    const RunPoint& z = pts.back();
    const RunPoint& y = pts[pts.size() >= 3 ? pts.size() - 3 : 0];
    Stance stance{};
    stance.x = z.x;
    stance.y = z.y;
    stance.heading = angle_of(z.y - y.y, z.x - y.x);
    stance.crow = true;
    if (run.kind == Kind::stripe) {
        stance.has_last = true;
        stance.last_frame = run.frame;
        stance.last_lane = run.lane;
    } else {
        stance.has_last = start.has_last;
        stance.last_frame = start.last_frame;
        stance.last_lane = start.last_lane;
    }
    int best = -1;
    for (std::size_t r = 0; r < runs_.size(); ++r) {
        if (static_cast<int>(r) == skip_a || static_cast<int>(r) == skip_b)
            continue;
        const Offer o = offer(runs_[r], stance);
        if (o.valid && (best < 0 || o.score < found.score)) {
            best = static_cast<int>(r);
            found = o;
        }
    }
    return best;
}

// The clean-up as one job. By now the lawn has been seen and the tufts are a
// fixed, small set, so their order is worked out as a whole (nearest-first, then
// improved by reversing and moving sections until nothing gets shorter) and they
// are strung on one continuous curve. A tuft has no proper direction, so the curve
// passes through each at whatever angle keeps the line easy to drive.
Mower::Pick Mower::tour() {
    Pick none{};
    std::vector<int> tufted{};
    for (std::size_t r = 0; r < runs_.size(); ++r) {
        if (runs_[r].kind == Kind::tuft && runs_[r].lane >= 0)
            tufted.push_back(static_cast<int>(r));
    }
    if (tufted.size() < 2 || tufted.size() != runs_.size())
        return none;
    const int n = static_cast<int>(tufted.size());
    std::vector<Spot> pts{};
    for (const int r : tufted) {
        const Run& run = runs_[static_cast<std::size_t>(r)];
        const RunPoint& mid = run.pts[run.pts.size() >> 1U];
        pts.push_back({mid.x, mid.y});
    }
    std::vector<double> from_here{};
    std::vector<double> between(static_cast<std::size_t>(n) * static_cast<std::size_t>(n));
    std::vector<int> stops_at{};
    for (const Spot& spot : pts)
        stops_at.push_back(cell_of(spot.x, spot.y));
    for (int i = 0; i < n; ++i) {
        const int cell = stops_at[static_cast<std::size_t>(i)];
        from_here.push_back(read(travel_, cell));
        const std::vector<float> d = spread(cell, stops_at);
        for (int j = 0; j < n; ++j)
            between[static_cast<std::size_t>(i * n + j)] = read(d, stops_at[static_cast<std::size_t>(j)]);
    }
    // nearest-first to start with
    std::vector<int> order{};
    std::vector<std::uint8_t> used(static_cast<std::size_t>(n), 0);
    int at = -1;
    for (int s = 0; s < n; ++s) {
        int b = -1;
        double bd = 1e9;
        for (int i = 0; i < n; ++i) {
            if (used[static_cast<std::size_t>(i)] != 0)
                continue;
            const double d = at < 0 ? from_here[static_cast<std::size_t>(i)] : between[static_cast<std::size_t>(at * n + i)];
            if (d < bd) {
                bd = d;
                b = i;
            }
        }
        if (b < 0 || bd > 1e8)
            break;
        used[static_cast<std::size_t>(b)] = 1;
        order.push_back(b);
        at = b;
    }
    if (order.size() < 2)
        return none;
    struct Legs {
        const std::vector<double>* from_here{};
        const std::vector<double>* between{};
        int n{};
        double total(const std::vector<int>& o) const {
            double t = 0;
            int a = -1;
            for (const int b : o) {
                t += a < 0 ? (*from_here)[static_cast<std::size_t>(b)] : (*between)[static_cast<std::size_t>(a * n + b)];
                a = b;
            }
            return t;
        }
    };
    const Legs legs{&from_here, &between, n};
    double best = legs.total(order);
    bool better = true;
    int guard = 0;
    const int stops = static_cast<int>(order.size());
    while (better && guard++ < 60) {
        better = false;
        for (int i = 0; i < stops - 1 && !better; ++i) {
            for (int j = i + 1; j < stops && !better; ++j) {
                std::vector<int> o = order;
                std::reverse(o.begin() + i, o.begin() + j + 1);
                const double l = legs.total(o);
                if (l < best - 1e-6) {
                    best = l;
                    order.swap(o);
                    better = true;
                }
            }
        }
        for (int i = 0; i < stops && !better; ++i) {
            for (int j = 0; j < stops && !better; ++j) {
                if (i == j)
                    continue;
                std::vector<int> o = order;
                const int x = o[static_cast<std::size_t>(i)];
                o.erase(o.begin() + i);
                o.insert(o.begin() + j, x);
                const double l = legs.total(o);
                if (l < best - 1e-6) {
                    best = l;
                    order.swap(o);
                    better = true;
                }
            }
        }
    }
    // string them on one curve
    Pick pick{};
    Spot p{pose_.x, pose_.y};
    double h = pose_.heading;
    for (int s = 0; s < stops; ++s) {
        const Spot q = pts[static_cast<std::size_t>(order[static_cast<std::size_t>(s)])];
        const double into = angle_of(q.y - p.y, q.x - p.x);
        double out = into;
        if (s + 1 < stops) {
            const Spot& onward = pts[static_cast<std::size_t>(order[static_cast<std::size_t>(s) + 1])];
            out = angle_of(onward.y - q.y, onward.x - q.x);
        }
        const double mid = angle_of(sine(into) + sine(out), cosine(into) + cosine(out));
        const double tries[5] = {mid, into, out, into + 0.6, into - 0.6};
        Move piece{};
        double arrive = into;
        for (const double a : tries) {
            Curve c = curve(p, h, q, a);
            if (!c.valid)
                continue;
            Piece part{};
            part.pts.swap(c.pts);
            piece.pieces.push_back(part);
            piece.valid = true;
            arrive = a;
            break;
        }
        if (!piece.valid) {
            if (s > 0)
                break;
            piece = transition(p, h, q, into);
            if (!piece.valid)
                return none;
            arrive = into;
        }
        for (const Piece& part : piece.pieces) {
            for (const Spot& z : part.pts) {
                TapePoint point{};
                point.x = z.x;
                point.y = z.y;
                point.dir = part.dir;
                point.work = part.dir > 0;
                pick.tape.push_back(point);
            }
        }
        pick.tour_keys.push_back(runs_[static_cast<std::size_t>(tufted[static_cast<std::size_t>(order[static_cast<std::size_t>(s)])])].lane);
        p = q;
        h = arrive;
    }
    if (pick.tape.size() < 3)
        return none;
    // run on a deck's length past the last one so it is fully under the blades
    for (int k = 1; k <= 3; ++k) {
        const double x = p.x + cosine(h) * 0.15 * k;
        const double y = p.y + sine(h) * 0.15 * k;
        if (!room_at(x, y))
            break;
        TapePoint point{};
        point.x = x;
        point.y = y;
        point.work = true;
        pick.tape.push_back(point);
    }
    pick.job = Job{tier_spot, Kind::tuft, pick.tour_keys[0], -1, true};
    pick.tour = true;
    pick.valid = true;
    return pick;
}

// Choose the next line by looking past it: what each choice leaves the mower
// placed to do afterwards counts too.
Mower::Pick Mower::elect() {
    {
        Pick chain = tour();
        if (chain.valid)
            return chain;
    }
    const Spot here{pose_.x, pose_.y};
    const double heading = pose_.heading;
    Stance start{};
    start.x = here.x;
    start.y = here.y;
    start.heading = heading;
    start.has_last = has_last_stripe_;
    start.last_frame = last_frame_;
    start.last_lane = last_lane_;
    start.kind = last_kind_;
    struct Candidate {
        int run{};
        Offer offer{};
    };
    std::vector<Candidate> first{};
    bool any_big = false;
    bool any_outline = false;
    for (std::size_t r = 0; r < runs_.size(); ++r) {
        const Offer o = offer(runs_[r], start);
        if (!o.valid)
            continue;
        first.push_back(Candidate{static_cast<int>(r), o});
        const double len = static_cast<double>(runs_[r].pts.size()) * track_step;
        any_big = any_big || (runs_[r].tier == tier_outer && len >= 3);
        any_outline = any_outline || (runs_[r].tier <= tier_obstacle && len >= 1.5);
    }
    {
        // the lawn's edge is not negotiable: the first lap is finished before anything else.
        // outlines before fill: while any lap of something seen is waiting, stripes and tufts wait too
        std::vector<Candidate> kept{};
        for (const Candidate& c : first) {
            const Run& run = runs_[static_cast<std::size_t>(c.run)];
            const double len = static_cast<double>(run.pts.size()) * track_step;
            if (any_big ? (run.tier == tier_outer && len >= 3) : (!any_outline || run.tier <= tier_obstacle))
                kept.push_back(c);
        }
        first.swap(kept);
    }
    // best score first; equals keep the order of the menu
    for (std::size_t i = 1; i < first.size(); ++i) {
        const Candidate moving = first[i];
        std::size_t j = i;
        while (j > 0 && moving.offer.score - first[j - 1].offer.score < 0) {
            first[j] = first[j - 1];
            --j;
        }
        first[j] = moving;
    }
    bool picked = false;
    double pick_total = 0;
    int pick_run = -1;
    std::vector<RunPoint> pick_pts{};
    Move pick_move{};
    int tried = 0;
    for (const Candidate& c : first) {
        if ((tried >= 6 && picked) || tried >= 24)
            break;
        ++tried;
        const Run& run = runs_[static_cast<std::size_t>(c.run)];
        const bool lap = run.closed || run.one_way;
        std::vector<RunPoint> pts = ordered(run, c.offer);
        const RunPoint e0 = pts[0];
        const RunPoint e2 = pts[std::min<std::size_t>(2, pts.size() - 1)];
        const double hin = angle_of(e2.y - e0.y, e2.x - e0.x);
        const double gap0 = length(e0.x - here.x, e0.y - here.y);
        const double off0 = std::abs(wrap(angle_of(e0.y - here.y, e0.x - here.x) - heading));
        // The start of the line is just ahead and nearly in line: roll onto it. Nobody backs up to hit a mark half a metre off.
        const bool at_hand =
            (gap0 < 0.2 && std::abs(wrap(hin - heading)) < 0.25) || (gap0 < 1.0 && std::abs(wrap(hin - heading)) < 0.6 && (gap0 < 0.3 || off0 < 0.9));
        Move move{};
        if (at_hand) {
            move.cost = gap0;
            move.valid = true;
        } else if (run.kind == Kind::tuft && (off0 < 1.2 || gap0 > 2.2) && clear_line(here.x, here.y, e0.x, e0.y, true)) {
            // a tuft has no proper direction: drive straight at it and over it
            Piece line{};
            const int n = std::max(1, static_cast<int>(std::ceil(gap0 / 0.15)));
            for (int k = 1; k <= n; ++k)
                line.pts.push_back({here.x + (e0.x - here.x) * k / n, here.y + (e0.y - here.y) * k / n});
            move.pieces.push_back(line);
            move.cost = gap0 + 2 * off0;
            move.valid = true;
        } else if (run.kind == Kind::tuft) {
            const double b0 = angle_of(e0.y - here.y, e0.x - here.x);
            const double tries[7] = {b0, b0 + 0.7, b0 - 0.7, b0 + 1.4, b0 - 1.4, hin, hin + pi};
            for (const double a : tries) {
                Curve cv = curve(here, heading, {e0.x, e0.y}, a);
                if (!cv.valid)
                    continue;
                Piece piece{};
                piece.pts.swap(cv.pts);
                move.pieces.push_back(piece);
                move.cost = cv.cost;
                move.valid = true;
                pts.resize(1);
                break;
            }
            if (!move.valid)
                move = transition(here, heading, {e0.x, e0.y}, hin);
        } else {
            move = transition(here, heading, {e0.x, e0.y}, hin);
        }
        // a lap need not be met at its first point: join it ahead with a forward curve rather than turn round to reach its start
        if ((!move.valid || move.cusps != 0) && lap && gap0 < 3.5) {
            const int joins[6] = {3, 5, 9, 14, 20, 27};
            for (const int k : joins) {
                if (k > static_cast<int>(pts.size()) - 4)
                    break;
                const RunPoint& q = pts[static_cast<std::size_t>(k)];
                const RunPoint& q2 = pts[static_cast<std::size_t>(k) + 2];
                Curve alt = curve(here, heading, {q.x, q.y}, angle_of(q2.y - q.y, q2.x - q.x));
                const double skip = run.closed ? 0 : 0.4 * k * track_step;
                if (alt.valid && (!move.valid || alt.cost + skip < move.cost)) {
                    move = Move{};
                    Piece piece{};
                    piece.pts.swap(alt.pts);
                    move.pieces.push_back(piece);
                    move.cost = alt.cost + skip;
                    move.valid = true;
                    const std::vector<RunPoint> skipped(pts.begin(), pts.begin() + k + 1);
                    pts.erase(pts.begin(), pts.begin() + k + 1);
                    if (run.closed)
                        pts.insert(pts.end(), skipped.begin(), skipped.end());
                    break;
                }
            }
        }
        // a stripe can be taken from either end: if this end needs a change of gear, see whether the other does not
        if (move.valid && move.cusps != 0 && !lap && run.kind != Kind::tuft && pts.size() > 3) {
            std::vector<RunPoint> turned = pts;
            std::reverse(turned.begin(), turned.end());
            const RunPoint& f0 = turned[0];
            const RunPoint& f2 = turned[2];
            const Move alt = transition(here, heading, {f0.x, f0.y}, angle_of(f2.y - f0.y, f2.x - f0.x));
            if (alt.valid && alt.cusps == 0 && alt.cost < move.cost + 3) {
                move = alt;
                pts.swap(turned);
            }
        }
        if (!move.valid)
            continue;
        double total = c.offer.score - c.offer.travel + move.cost;
        Offer second{};
        const int two = after(run, c.offer, start, c.run, -1, second);
        if (two >= 0) {
            total += 0.6 * second.score;
            Offer third{};
            const int three = after(runs_[static_cast<std::size_t>(two)], second, start, c.run, two, third);
            if (three >= 0)
                total += 0.35 * third.score;
        }
        if (!picked || total < pick_total) {
            picked = true;
            pick_total = total;
            pick_run = c.run;
            pick_pts.swap(pts);
            pick_move = move;
        }
    }
    Pick pick{};
    if (!picked)
        return pick;
    for (const Piece& piece : pick_move.pieces) {
        for (const Spot& spot : piece.pts) {
            TapePoint point{};
            point.x = spot.x;
            point.y = spot.y;
            point.dir = piece.dir;
            pick.tape.push_back(point);
        }
    }
    for (const RunPoint& p : pick_pts) {
        TapePoint point{};
        point.x = p.x;
        point.y = p.y;
        point.work = true;
        pick.tape.push_back(point);
    }
    const Run& chosen = runs_[static_cast<std::size_t>(pick_run)];
    pick.job = Job{chosen.tier, chosen.kind, chosen.lane, chosen.frame, true};
    pick.valid = true;
    return pick;
}

} // namespace coverage
