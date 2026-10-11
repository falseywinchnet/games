#pragma once
#include "gui_forms/basic_controls.hpp"
#include "puzzles.hpp"
#include <cstdint>
#include <vector>
namespace games {
namespace gf = gui_forms;

// The Untangle cat. It lives on the cushion while you work: it watches the pegs, wanders,
// grooms, sits on a peg (which then cannot be picked up until the cat is shooed), chases
// the peg you are dragging, and on Medium and Hard swats a peg out of place if you leave the
// puzzle alone too long. When the threads are clear it curls up asleep in the web.
// Positions are in board units (0..1), so the cat survives a resize.
class Kitten {
  public:
    struct Scene {
        const std::vector<Point2>* pegs = nullptr;
        std::vector<bool> swattable; // pegs the cat may knock (not frozen, not held)
        int dragged = -1;
        Point2 pointer{.5, .5};
        bool pointer_inside = false;
        bool solved = false;
        bool mischief = false; // Medium and Hard
        bool reduced = false;  // reduced motion: the cat sits and watches
        double idle = 0;       // seconds since the player last moved a peg
    };
    struct Swat {
        int peg = -1;
        Point2 to{};
    };
    void reset(std::uint32_t seed);
    // Advances the cat; a returned peg index means it has just knocked that peg to `to`.
    Swat step(double dt, const Scene& scene);
    void paint(gf::Painter& p, gf::Rect board) const;
    [[nodiscard]] gf::Rect paint_bounds(gf::Rect board) const;
    [[nodiscard]] bool hit(Point2 at) const;
    // A click on the cat sends it scampering off.
    void shoo();
    // Picked up: the cat dangles under the pointer until it is put down, where it sits.
    void pick_up();
    void carry(Point2 to);
    void put_down();
    [[nodiscard]] bool held() const {
        return mode_ == Mode::held;
    }
    // The peg the cat is sitting on, or -1.
    [[nodiscard]] int perch() const {
        return mode_ == Mode::perch && settled_ ? target_ : -1;
    }
    [[nodiscard]] bool asleep() const {
        return mode_ == Mode::nap && settled_;
    }

  private:
    enum class Mode { watch, wander, chase, perch, stalk, swat, nap, groom, flee, held };
    Mode mode_ = Mode::watch;
    Point2 at_{.88, .9}, goal_{.88, .9};
    double face_ = -1; // -1 faces left, 1 faces right
    double t_ = 0, clock_ = 0, stride_ = 0, speed_ = 0, linger_ = 3, cooldown_ = 12;
    double blink_ = 0;
    double lift_ = 0; // board units it is held above where it would stand
    Point2 look_{0, 0};
    int target_ = -1;
    bool settled_ = false, struck_ = false;
    std::uint32_t seed_ = 1;
    double random();
    void enter(Mode mode);
    bool walk(double dt, Point2 goal, double speed); // true once arrived
};
} // namespace games
