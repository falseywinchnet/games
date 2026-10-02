#include "kitten.hpp"
#include "presentation.hpp"
#include <algorithm>
#include <cmath>
namespace games {
namespace {
constexpr double tau = 6.283185307179586;
const gf::Color fur = gf::Color::rgba(236, 150, 72), fur_dark = gf::Color::rgba(184, 98, 44),
                cream = gf::Color::rgba(252, 236, 210), pink = gf::Color::rgba(240, 150, 160),
                eye = gf::Color::rgba(132, 206, 96), ink = gf::Color::rgba(46, 26, 20);
gf::Color alpha(gf::Color c, double a) {
    return gf::Color::rgba(c.red, c.green, c.blue,
                           static_cast<unsigned char>(std::clamp(a, 0.0, 1.0) * c.alpha));
}
// Draws in cat units: x grows toward the way the cat faces, y grows downward, one unit is
// about a fifth of the cat's length.
struct Pen {
    gf::Painter& p;
    gf::Point origin;
    double unit, face;
    [[nodiscard]] gf::Point at(double x, double y) const {
        return {origin.x + x * face * unit, origin.y + y * unit};
    }
    void ellipse(double x, double y, double rx, double ry, gf::Color c, double tilt = 0) const {
        std::vector<gf::Point> points;
        for (int k = 0; k < 28; ++k) {
            const double a = k * tau / 28;
            const double ex = std::cos(a) * rx, ey = std::sin(a) * ry;
            points.push_back(at(x + ex * std::cos(tilt) - ey * std::sin(tilt),
                                y + ex * std::sin(tilt) + ey * std::cos(tilt)));
        }
        paint_polygon(p, points, c);
    }
    void triangle(double ax, double ay, double bx, double by, double cx, double cy,
                  gf::Color c) const {
        paint_polygon(p, {at(ax, ay), at(bx, by), at(cx, cy)}, c);
    }
    void dot(double x, double y, double r, gf::Color c) const {
        const gf::Point q = at(x, y);
        p.fill_rounded_rect({q.x - r * unit, q.y - r * unit, 2 * r * unit, 2 * r * unit}, r * unit,
                            c);
    }
    void line(double ax, double ay, double bx, double by, double width, gf::Color c) const {
        p.draw_line(at(ax, ay), at(bx, by), c, std::max(.8, width * unit));
    }
    // A tapering tail along a gentle curve, from the base outward.
    void tail(const std::vector<Point2>& spine, double width, gf::Color c, gf::Color tip) const {
        for (std::size_t i = 1; i < spine.size(); ++i) {
            const double k = double(i) / spine.size();
            const double w = width * (1 - .45 * k);
            const gf::Color shade = i + 2 >= spine.size() ? tip : c;
            line(spine[i - 1].x, spine[i - 1].y, spine[i].x, spine[i].y, w, shade);
            dot(spine[i].x, spine[i].y, w * .5, shade);
        }
    }
};
void paint_head(const Pen& pen, double x, double y, double r, Point2 look, bool closed,
                double blink) {
    // Ears first, so the head overlaps their bases.
    pen.triangle(x - r * .78, y - r * .35, x - r * .55, y - r * 1.35, x - r * .05, y - r * .8, fur);
    pen.triangle(x + r * .2, y - r * .82, x + r * .68, y - r * 1.32, x + r * .85, y - r * .3, fur);
    pen.triangle(x - r * .6, y - r * .5, x - r * .5, y - r * 1.08, x - r * .2, y - r * .78, pink);
    pen.triangle(x + r * .32, y - r * .78, x + r * .62, y - r * 1.06, x + r * .7, y - r * .45,
                 pink);
    pen.ellipse(x, y, r, r * .9, fur);
    // Forehead stripes.
    for (int k = -1; k <= 1; ++k)
        pen.line(x + k * r * .22, y - r * .85, x + k * r * .15, y - r * .52, r * .1, fur_dark);
    // Muzzle, nose, mouth.
    pen.ellipse(x + r * .22, y + r * .32, r * .42, r * .3, cream);
    pen.triangle(x + r * .12, y + r * .14, x + r * .38, y + r * .14, x + r * .25, y + r * .3, pink);
    pen.line(x + r * .25, y + r * .3, x + r * .12, y + r * .44, r * .06, ink);
    pen.line(x + r * .25, y + r * .3, x + r * .38, y + r * .44, r * .06, ink);
    // Whiskers.
    for (int k = -1; k <= 1; ++k) {
        pen.line(x + r * .55, y + r * .32, x + r * 1.35, y + r * (.22 + k * .16), r * .035,
                 alpha(cream, .9));
        pen.line(x - r * .05, y + r * .32, x - r * .8, y + r * (.22 + k * .16), r * .035,
                 alpha(cream, .9));
    }
    // Eyes: green with slit pupils that follow `look`, or closed arcs.
    for (const double ex : {-.2, .5}) {
        const double cx = x + r * ex, cy = y - r * .12;
        if (closed) {
            pen.line(cx - r * .17, cy, cx, cy + r * .09, r * .07, ink);
            pen.line(cx, cy + r * .09, cx + r * .17, cy, r * .07, ink);
            continue;
        }
        const double open = std::max(.12, 1 - blink);
        pen.ellipse(cx, cy, r * .17, r * .2 * open, eye);
        pen.ellipse(cx + look.x * r * .07, cy + look.y * r * .06, r * .05, r * .17 * open, ink);
        pen.dot(cx - r * .05, cy - r * .07 * open, r * .035, gf::Color::rgba(255, 255, 255));
    }
}
} // namespace
double Kitten::random() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return (seed_ >> 8) / 16777216.0;
}
void Kitten::reset(std::uint32_t seed) {
    seed_ = seed ? seed : 1;
    at_ = goal_ = {.9, .92};
    face_ = -1;
    target_ = -1;
    settled_ = struck_ = false;
    cooldown_ = 14;
    enter(Mode::watch);
}
void Kitten::enter(Mode mode) {
    mode_ = mode;
    t_ = 0;
    settled_ = false;
    struck_ = false;
    linger_ = 2.5 + random() * 3.5;
}
void Kitten::shoo() {
    goal_ = {at_.x < .5 ? .9 : .1, at_.y < .5 ? .9 : .12};
    enter(Mode::flee);
}
bool Kitten::hit(Point2 at) const {
    return std::hypot(at.x - at_.x, (at.y - at_.y) * 1.2) < .055;
}
bool Kitten::walk(double dt, Point2 goal, double speed) {
    const double dx = goal.x - at_.x, dy = goal.y - at_.y, d = std::hypot(dx, dy);
    if (d < .008) {
        speed_ = 0;
        return true;
    }
    const double step = std::min(d, speed * dt);
    at_.x += dx / d * step;
    at_.y += dy / d * step;
    if (std::abs(dx) > .004)
        face_ = dx > 0 ? 1 : -1;
    speed_ = step / std::max(dt, 1e-6);
    stride_ += step * 38;
    return false;
}
Kitten::Swat Kitten::step(double dt, const Scene& scene) {
    Swat swat;
    clock_ += dt;
    t_ += dt;
    cooldown_ -= dt;
    // An occasional slow blink.
    const double cycle = std::fmod(clock_, 4.7);
    blink_ = cycle < .16 ? std::sin(cycle / .16 * 3.14159) : 0;
    const std::vector<Point2>& pegs = *scene.pegs;
    const int count = static_cast<int>(pegs.size());
    // Eyes follow the action.
    Point2 focus = scene.pointer_inside ? scene.pointer : Point2{.5, .5};
    if (scene.dragged >= 0 && scene.dragged < count)
        focus = scene.pointer;
    const double fx = focus.x - at_.x, fy = focus.y - (at_.y - .05),
                 fd = std::max(.001, std::hypot(fx, fy));
    look_ = {std::clamp(fx / fd * face_, -1.0, 1.0), std::clamp(fy / fd, -1.0, 1.0)};
    if (scene.reduced) {
        speed_ = 0;
        if (scene.solved && mode_ != Mode::nap)
            enter(Mode::nap);
        if (!scene.solved && mode_ != Mode::watch)
            enter(Mode::watch);
        settled_ = true;
        return swat;
    }
    if (scene.solved) {
        if (mode_ != Mode::nap) {
            Point2 middle{0, 0};
            for (Point2 q : pegs) {
                middle.x += q.x / std::max(1, count);
                middle.y += q.y / std::max(1, count);
            }
            goal_ = {std::clamp(middle.x, .1, .9), std::clamp(middle.y + .02, .1, .92)};
            enter(Mode::nap);
        }
        if (!settled_)
            settled_ = walk(dt, goal_, .3);
        return swat;
    }
    if (mode_ == Mode::nap)
        enter(Mode::watch);
    if (scene.dragged >= 0 && mode_ != Mode::chase && mode_ != Mode::flee) {
        enter(Mode::chase);
        target_ = scene.dragged;
    }
    switch (mode_) {
    case Mode::chase: {
        if (scene.dragged < 0) {
            enter(Mode::watch);
            break;
        }
        // Stay a pounce behind the peg, on the near side.
        const Point2 prey = scene.pointer;
        const double side = at_.x < prey.x ? -1 : 1;
        goal_ = {std::clamp(prey.x + side * .07, .05, .95), std::clamp(prey.y + .03, .06, .95)};
        if (walk(dt, goal_, .62))
            face_ = prey.x > at_.x ? 1 : -1;
        break;
    }
    case Mode::watch:
        speed_ = 0;
        if (t_ < linger_)
            break;
        if (scene.mischief && scene.idle > 16 && cooldown_ <= 0) {
            std::vector<int> choices;
            for (int i = 0; i < count; ++i)
                if (i < static_cast<int>(scene.swattable.size()) && scene.swattable[i])
                    choices.push_back(i);
            if (!choices.empty()) {
                target_ =
                    choices[static_cast<std::size_t>(random() * choices.size()) % choices.size()];
                enter(Mode::stalk);
                break;
            }
        }
        {
            const double roll = random();
            if (roll < .38) {
                goal_ = {.08 + random() * .84, .1 + random() * .82};
                enter(Mode::wander);
            } else if (roll < .62 && count > 0) {
                target_ = static_cast<int>(random() * count) % count;
                enter(Mode::perch);
            } else if (roll < .82)
                enter(Mode::groom);
            else
                enter(Mode::watch);
        }
        break;
    case Mode::wander:
        if (walk(dt, goal_, .2))
            enter(Mode::watch);
        break;
    case Mode::perch:
        if (target_ < 0 || target_ >= count) {
            enter(Mode::watch);
            break;
        }
        if (!settled_)
            settled_ = walk(dt, {pegs[target_].x, pegs[target_].y + .015}, .22);
        else {
            at_ = {pegs[target_].x, pegs[target_].y + .015};
            if (t_ > 7 + linger_)
                enter(Mode::watch);
        }
        break;
    case Mode::groom:
        speed_ = 0;
        if (t_ > 2.6)
            enter(Mode::watch);
        break;
    case Mode::stalk: {
        if (target_ < 0 || target_ >= count || scene.idle < 1) {
            enter(Mode::watch);
            break;
        }
        const Point2 prey = pegs[target_];
        const double side = at_.x < prey.x ? -1 : 1;
        if (walk(dt, {prey.x + side * .07, prey.y + .02}, .14)) {
            face_ = -side;
            enter(Mode::swat);
        }
        break;
    }
    case Mode::swat:
        speed_ = 0;
        if (!struck_ && t_ > .32 && target_ >= 0 && target_ < count) {
            struck_ = true;
            const Point2 prey = pegs[target_];
            swat.peg = target_;
            swat.to = {std::clamp(prey.x + face_ * (.07 + random() * .05), .05, .95),
                       std::clamp(prey.y + (random() - .5) * .08, .05, .95)};
            cooldown_ = 22 + random() * 10;
        }
        if (t_ > .9)
            enter(Mode::watch);
        break;
    case Mode::flee:
        if (walk(dt, goal_, .75))
            enter(Mode::watch);
        break;
    case Mode::nap:
        break;
    }
    return swat;
}
void Kitten::paint(gf::Painter& p, gf::Rect board) const {
    const double unit = std::max(2.2, board.width * .0145);
    const gf::Point ground{board.x + at_.x * board.width, board.y + at_.y * board.height};
    const Pen pen{p, ground, unit, face_};
    const bool moving = speed_ > .01;
    // Soft shadow on the cushion.
    {
        const double rx = (mode_ == Mode::nap ? 5.2 : moving ? 6.4 : 4.6) * unit;
        const gf::Rect shadow{ground.x - rx, ground.y - unit * .9, rx * 2, unit * 2.6};
        const gf::GradientStop stops[] = {{0, gf::Color::rgba(20, 6, 18, 90)},
                                          {1, gf::Color::rgba(20, 6, 18, 0)}};
        p.fill_radial_gradient(shadow, {ground.x, ground.y + unit * .4}, {rx, unit * 1.3}, stops);
    }
    const double sway = std::sin(clock_ * 2.2);
    if (mode_ == Mode::nap && settled_) {
        // Curled up: a round loaf, head tucked on the paws, tail wrapped round the front.
        pen.tail({{-3.4, -.4}, {-3.6, 1.0}, {-2.4, 2.0}, {-.6, 2.4}, {1.4, 2.1}, {2.8, 1.4}}, 1.3,
                 fur, fur_dark);
        const double breathe = 1 + .03 * std::sin(clock_ * 1.8);
        pen.ellipse(-.4, -.9, 3.6 * breathe, 2.6 * breathe, fur);
        for (int k = 0; k < 3; ++k)
            pen.line(-2.2 + k * 1.2, -3.1, -1.8 + k * 1.2, -1.6, .45, fur_dark);
        pen.ellipse(2.2, .4, 1.1, .6, cream);
        paint_head(pen, 2.1, -1.0, 1.75, {0, 0}, true, 0);
        // Drifting Zs.
        for (int k = 0; k < 3; ++k) {
            const double life = std::fmod(clock_ * .45 + k / 3.0, 1.0);
            const gf::FontSpec f{gf::FontRole::content, unit * (1.6 + life * 1.6), 700, false};
            const gf::Point q = pen.at(3.2 + life * 2.4, -3.6 - life * 4.5);
            p.draw_text_utf8(
                {q.x, q.y}, "z", f,
                gf::Color::rgba(255, 244, 220,
                                static_cast<unsigned char>(200 * std::sin(life * 3.14))));
        }
        return;
    }
    if (moving) {
        // Walking or running: long body, legs in a diagonal gait, tail up.
        const double crouch = mode_ == Mode::stalk ? 1.0 : 0;
        const double gait = stride_;
        const double body_y = -2.9 + crouch * 1.1 + std::abs(std::sin(gait)) * -.25;
        pen.tail({{-3.6, body_y - .2},
                  {-4.6, body_y - 1.4 - crouch * -.8},
                  {-5.0 + sway * .3, body_y - 2.8 + crouch * 1.6},
                  {-4.6 + sway * .6, body_y - 3.9 + crouch * 2.2}},
                 1.1, fur, fur_dark);
        const double legs[4][2] = {{2.2, 0}, {1.4, 3.14}, {-2.4, 3.14}, {-3.0, 0}};
        for (int k = 0; k < 4; ++k) {
            const double swing = std::sin(gait + legs[k][1]) * .9;
            const double hip_x = legs[k][0], hip_y = body_y + 1.2;
            const double foot_x = hip_x + swing,
                         foot_y = -.25 - std::max(0.0, -std::cos(gait + legs[k][1])) * .5;
            const gf::Color shade = k == 1 || k == 2 ? fur_dark : fur;
            pen.line(hip_x, hip_y, foot_x, foot_y, 1.05, shade);
            pen.ellipse(foot_x + .25, foot_y, .6, .35, cream);
        }
        pen.ellipse(-.5, body_y, 4.2, 1.95 - crouch * .3, fur);
        pen.ellipse(.4, body_y + .8, 2.4, .9, cream);
        for (int k = 0; k < 3; ++k)
            pen.line(-2.8 + k * 1.3, body_y - 1.8, -2.4 + k * 1.3, body_y - .4, .45, fur_dark);
        paint_head(pen, 3.6, body_y - 1.5 + crouch * .5, 1.75, look_, false, blink_);
        return;
    }
    // Sitting: watching, grooming, perched on a peg, or swatting.
    const bool grooming = mode_ == Mode::groom;
    const double reach =
        mode_ == Mode::swat ? std::sin(std::clamp(t_ / .9, 0.0, 1.0) * 3.14159) : 0;
    pen.tail({{-1.6, -.6},
              {-3.0, -.2},
              {-3.4 + sway * .4, .6},
              {-2.4 + sway * .5, 1.3},
              {-.6 + sway * .4, 1.3}},
             1.15, fur, fur_dark);
    pen.ellipse(-.4, -2.6, 2.7, 2.9, fur);
    for (int k = 0; k < 3; ++k)
        pen.line(-2.6 + k * .6, -4.4 + k * .9, -1.4 + k * .6, -4.0 + k * .9, .45, fur_dark);
    pen.ellipse(.5, -2.2, 1.35, 2.0, cream);
    // Front paws; when swatting, the near one strikes out toward the peg.
    pen.ellipse(-.4, -.3, .75, .45, cream);
    if (reach > 0) {
        pen.line(1.0, -2.0, 1.4 + reach * 3.6, -1.5 - reach * .8, 1.0, fur);
        pen.ellipse(1.6 + reach * 3.8, -1.5 - reach * .8, .7, .5, cream);
    } else if (grooming) {
        const double lick = std::sin(t_ * 9) * .3;
        pen.line(1.0, -1.6, 2.1, -5.2 + lick, 1.0, fur);
        pen.ellipse(2.2, -5.4 + lick, .7, .55, cream);
    } else
        pen.ellipse(1.2, -.3, .75, .45, cream);
    paint_head(pen, .9, -6.2, 1.9, look_, grooming && std::fmod(t_, 1.2) < .9, blink_);
}
} // namespace games
