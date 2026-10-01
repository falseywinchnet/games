#include "box.hpp"

#include <algorithm>
#include <bit>
#include <mutex>
#include <thread>

namespace ap {

Port port(int p) {
    if (p < 8) return {p, -1, 0, 1};
    if (p < 16) return {kN, p - 8, -1, 0};
    if (p < 24) return {23 - p, kN, 0, -1};
    return {-1, 31 - p, 1, 0};
}

int port_at(int x, int y) {
    if (y == -1 && x >= 0 && x < kN) return x;
    if (x == kN && y >= 0 && y < kN) return 8 + y;
    if (y == kN && x >= 0 && x < kN) return 23 - x;
    if (x == -1 && y >= 0 && y < kN) return 31 - y;
    return -1;
}

Trace trace(Atoms atoms, int p) {
    Trace t;
    const Port s = port(p);
    int x = s.x, y = s.y, dx = s.dx, dy = s.dy;
    t.path.push_back({static_cast<double>(x), static_cast<double>(y)});
    auto inside = [](int cx, int cy) { return cx >= 0 && cx < kN && cy >= 0 && cy < kN; };
    for (int step = 0; step < 400; ++step) {
        const int nx = x + dx, ny = y + dy;
        if (has(atoms, nx, ny)) {
            t.kind = Outcome::hit;
            t.path.push_back({static_cast<double>(nx), static_cast<double>(ny)});
            return t;
        }
        // the two squares diagonally ahead
        const bool a = has(atoms, nx - dy, ny + dx), b = has(atoms, nx + dy, ny - dx);
        if (a || b) {
            if (!inside(x, y)) {
                // an atom beside the entry square: turned back before it gets in
                t.kind = Outcome::reflect;
                t.path.push_back({x + dx * .5, y + dy * .5});
                t.path.push_back({static_cast<double>(x), static_cast<double>(y)});
                return t;
            }
            if (t.path.back().x != x || t.path.back().y != y) t.path.push_back({static_cast<double>(x), static_cast<double>(y)});
            if (a && b) { dx = -dx; dy = -dy; }
            else if (a) { const int ox = dx; dx = dy; dy = -ox; }   // turn away from a
            else { const int ox = dx; dx = -dy; dy = ox; }          // turn away from b
            continue;
        }
        x = nx;
        y = ny;
        if (!inside(x, y)) {
            t.path.push_back({static_cast<double>(x), static_cast<double>(y)});
            const int e = port_at(x, y);
            if (e == p) t.kind = Outcome::reflect;
            else { t.kind = Outcome::exit; t.exit = e; }
            return t;
        }
    }
    t.kind = Outcome::reflect;  // unreachable: paths are reversible, so every beam leaves
    return t;
}

namespace {
// trace() without the path: just the report (32 hit, 33 reflection, else the exit port)
int report(Atoms atoms, int p) {
    const Port s = port(p);
    int x = s.x, y = s.y, dx = s.dx, dy = s.dy;
    bool in = false;
    for (int step = 0; step < 400; ++step) {
        const int nx = x + dx, ny = y + dy;
        if (has(atoms, nx, ny)) return 32;
        const bool a = has(atoms, nx - dy, ny + dx), b = has(atoms, nx + dy, ny - dx);
        if (a || b) {
            if (!in) return 33;
            if (a && b) { dx = -dx; dy = -dy; }
            else if (a) { const int ox = dx; dx = dy; dy = -ox; }
            else { const int ox = dx; dx = -dy; dy = ox; }
            continue;
        }
        x = nx;
        y = ny;
        in = x >= 0 && x < kN && y >= 0 && y < kN;
        if (!in) { const int e = port_at(x, y); return e == p ? 33 : e; }
    }
    return 33;
}
}  // namespace

std::array<std::uint8_t, kPorts> signature(Atoms atoms) {
    std::array<std::uint8_t, kPorts> s{};
    for (int p = 0; p < kPorts; ++p) s[static_cast<size_t>(p)] = static_cast<std::uint8_t>(report(atoms, p));
    return s;
}

namespace {
std::uint64_t sig_hash(Atoms atoms) {
    const auto s = signature(atoms);
    std::uint64_t h = 1469598103934665603ULL;
    for (std::uint8_t v : s) { h ^= v; h *= 1099511628211ULL; }
    return h;
}

// Every arrangement's 32 reports, hashed and sorted. Two arrangements with the
// same reports share a hash, so a hash that appears once proves its box is
// unique. (Equal hashes are treated as ambiguous, which is only ever cautious.)
const std::vector<std::uint64_t>& all_hashes() {
    static std::vector<std::uint64_t> v;
    static std::once_flag once;
    std::call_once(once, [] {
        // split by the first atom across a few threads (about 0.1 s in all)
        std::vector<std::vector<std::uint64_t>> part(8);
        std::vector<std::thread> th;
        for (int k = 0; k < 8; ++k)
            th.emplace_back([k, &part] {
                for (int a = k; a < 64; a += 8)
                    for (int b = a + 1; b < 64; ++b)
                        for (int c = b + 1; c < 64; ++c)
                            for (int d = c + 1; d < 64; ++d)
                                part[static_cast<size_t>(k)].push_back(sig_hash((1ULL << a) | (1ULL << b) | (1ULL << c) | (1ULL << d)));
            });
        for (std::thread& t : th) t.join();
        v.reserve(635376);
        for (const auto& p : part) v.insert(v.end(), p.begin(), p.end());
        std::sort(v.begin(), v.end());
    });
    return v;
}
}  // namespace

bool deducible(Atoms atoms) {
    const auto& v = all_hashes();
    const std::uint64_t h = sig_hash(atoms);
    const auto r = std::equal_range(v.begin(), v.end(), h);
    return r.second - r.first == 1;
}

Box::Box(std::uint64_t seed) : rng_(seed ? seed : 0x9E3779B97F4A7C15ULL) {}

std::uint64_t Box::next() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 7;
    rng_ ^= rng_ << 17;
    return rng_;
}

void Box::set_atoms(Atoms a) {
    atoms_ = a;
    probes_.clear();
    fired_.clear();
    marks_ = empties_ = 0;
    opened_ = false;
}

void Box::new_box() {
    for (;;) {
        Atoms a = 0;
        while (std::popcount(a) < kAtoms) a |= 1ULL << (next() % 64);
        if (deducible(a)) { set_atoms(a); return; }
    }
}

int Box::known(int p) const {
    for (size_t i = 0; i < probes_.size(); ++i)
        if (probes_[i].port == p || (probes_[i].kind == Outcome::exit && probes_[i].exit == p)) return static_cast<int>(i);
    return -1;
}

const Probe& Box::fire(int p, bool& fresh) {
    p = std::clamp(p, 0, kPorts - 1);
    const int k = known(p);
    if (k >= 0) { fresh = false; return probes_[static_cast<size_t>(k)]; }
    fresh = true;
    const Trace t = trace(atoms_, p);
    Probe pr;
    pr.port = p;
    pr.kind = t.kind;
    pr.exit = t.exit;
    if (t.kind == Outcome::exit) {
        int n = 0;
        for (const Probe& q : probes_) n += q.kind == Outcome::exit;
        pr.pair = n + 1;
    }
    probes_.push_back(pr);
    fired_.push_back(p);
    return probes_.back();
}

bool Box::toggle_mark(int cell) {
    if (opened_ || cell < 0 || cell >= kN * kN) return false;
    if (!marked(cell) && marks() >= kAtoms) return false;
    marks_ ^= 1ULL << cell;
    empties_ &= ~(1ULL << cell);
    return true;
}

bool Box::toggle_empty(int cell) {
    if (opened_ || cell < 0 || cell >= kN * kN) return false;
    empties_ ^= 1ULL << cell;
    marks_ &= ~(1ULL << cell);
    return true;
}

int Box::marks() const { return std::popcount(marks_); }

void Box::open() {
    if (can_open()) opened_ = true;
}

int Box::points() const {
    int n = 0;
    for (const Probe& p : probes_) n += p.kind == Outcome::exit ? 2 : 1;
    return n;
}

int Box::found() const { return std::popcount(marks_ & atoms_); }

BoxState Box::state() const { return {atoms_, fired_, marks_, empties_, opened_}; }

bool Box::restore(const BoxState& s) {
    if (std::popcount(s.atoms) != kAtoms || std::popcount(s.marks) > kAtoms || (s.marks & s.empties)) return false;
    if (s.opened && std::popcount(s.marks) != kAtoms) return false;
    set_atoms(s.atoms);
    for (int p : s.fired) {
        if (p < 0 || p >= kPorts) return false;
        bool fresh = false;
        fire(p, fresh);
        if (!fresh) return false;
    }
    marks_ = s.marks;
    empties_ = s.empties;
    opened_ = s.opened;
    return true;
}

}  // namespace ap
