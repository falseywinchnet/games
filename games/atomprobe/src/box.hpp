#pragma once
// The rules of the black box: an 8x8 chamber hides four atoms. A beam fired
// from one of the 32 ports around the edge is absorbed when it runs into an
// atom, turned 90 degrees away from an atom it would pass diagonally, sent
// back by two such atoms at once, and reflected outright when an atom sits
// beside its entry square. Pure and deterministic: no UI, no clock.
#include <array>
#include <cstdint>
#include <vector>

namespace ap {

constexpr int kN = 8, kAtoms = 4, kPorts = 32;
constexpr int kMissPenalty = 5;  // classic scoring: each atom not found costs five

// Ports run clockwise: 0-7 along the top (left to right), 8-15 down the right,
// 16-23 along the bottom (right to left), 24-31 up the left.
// Cells are row * 8 + col, row 0 at the top (far side of the chamber).
struct Port {
    int x, y;    // the square just outside the box, in cell coordinates (-1..8)
    int dx, dy;  // the direction a beam fired from here travels
};
Port port(int p);
int port_at(int x, int y);  // the port for an outside square, or -1

enum class Outcome : std::uint8_t { hit, reflect, exit };

struct Pt {
    double x, y;  // cell coordinates; cell centres are integers
};
struct Trace {
    Outcome kind = Outcome::reflect;
    int exit = -1;          // the port it leaves by (exit only)
    std::vector<Pt> path;   // the true path: from outside the port, through each turn, to where it ends
};

using Atoms = std::uint64_t;  // one bit per cell
inline bool has(Atoms a, int x, int y) { return x >= 0 && x < kN && y >= 0 && y < kN && (a >> (y * kN + x) & 1); }

Trace trace(Atoms atoms, int port);
// What all 32 ports report: 32 = hit, 33 = reflection, otherwise the exit port.
std::array<std::uint8_t, kPorts> signature(Atoms atoms);

// True when no other arrangement of four atoms gives the same 32 reports, so
// the box can be deduced exactly. Built once from all 635,376 arrangements.
bool deducible(Atoms atoms);

struct Probe {
    int port = 0;
    Outcome kind = Outcome::reflect;
    int exit = -1;
    int pair = 0;   // detours are numbered 1, 2, 3... in the order found
};

struct BoxState {
    Atoms atoms = 0;
    std::vector<int> fired;  // ports fired, in order
    std::uint64_t marks = 0, empties = 0;
    bool opened = false;
};

class Box {
public:
    explicit Box(std::uint64_t seed);
    void new_box();
    void set_atoms(Atoms a);  // tests and replays
    bool restore(const BoxState& s);
    BoxState state() const;

    // Fire a port. A port whose answer is already on the rim (fired, or the far
    // end of a detour) costs nothing more; `fresh` says whether it was new.
    const Probe& fire(int port, bool& fresh);
    int known(int port) const;  // index into probes(), or -1
    const std::vector<Probe>& probes() const { return probes_; }

    bool toggle_mark(int cell);   // at most four
    bool toggle_empty(int cell);
    bool marked(int cell) const { return marks_ >> cell & 1; }
    bool empty_mark(int cell) const { return empties_ >> cell & 1; }
    int marks() const;
    bool can_open() const { return !opened_ && marks() == kAtoms; }
    void open();

    bool opened() const { return opened_; }
    Atoms atoms() const { return atoms_; }
    bool atom(int cell) const { return atoms_ >> cell & 1; }
    int points() const;   // ports lit: a hit or reflection lights one, a detour two
    int found() const;    // atoms correctly marked
    int missed() const { return kAtoms - found(); }
    int total() const { return points() + kMissPenalty * missed(); }

private:
    std::uint64_t rng_;
    Atoms atoms_ = 0;
    std::vector<Probe> probes_;
    std::vector<int> fired_;
    std::uint64_t marks_ = 0, empties_ = 0;
    bool opened_ = false;
    std::uint64_t next();
};

}  // namespace ap
