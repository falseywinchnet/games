#include "stage.hpp"

#include <algorithm>
#include <cmath>

namespace ps_cube {
namespace {

double clamp01(double value) {
    const double clamped = std::clamp(value, 0.0, 1.0);
    return clamped;
}

double ease_out_cubic(double t) {
    const double u = 1 - clamp01(t);
    return 1 - u * u * u;
}

double ease_in_cubic(double t) {
    const double u = clamp01(t);
    return u * u * u;
}

// Overshoots a little and settles: things that land with a bit of weight.
double ease_out_back(double t) {
    const double u = clamp01(t) - 1;
    const double c1 = 1.5;
    const double c3 = c1 + 1;
    return 1 + c3 * u * u * u + c1 * u * u;
}

// How far a cell lies from the corner the three faces share, 0 to 1. Stones rise in a
// wave that starts at that corner.
double corner_distance(const Puzzle& puzzle, int cell) {
    const Point3 center = cell_center(puzzle.side, cell);
    const double dx = center.x + 1;
    const double dy = center.y + 1;
    const double dz = center.z - 1;
    const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    return clamp01(distance / 2.83);
}

constexpr double spin_seconds = 1.0;
constexpr double stones_begin = .55;
constexpr double stones_spread = .45;
constexpr double stone_seconds = .38;
constexpr double bloom_begin = 1.05;
constexpr double bloom_seconds = .32;

bool ease_toward(double& value, double target, double seconds, bool reduced) {
    const double gap = target - value;
    if (gap == 0) {
        return false;
    }
    if (reduced || std::fabs(gap) < .0005) {
        value = target;
        return true;
    }
    const double k = 1 - std::exp(-seconds * 14);
    value += gap * k;
    return true;
}

}  // namespace

bool inside(const Box& box, double x, double y) {
    const bool within = x >= box.x && y >= box.y && x < box.x + box.w && y < box.y + box.h;
    return within;
}

bool overlap(const Box& a, const Box& b) {
    const bool apart = a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y;
    return !apart;
}

Layout compute_layout(double width, double height) {
    Layout layout;
    layout.width = width;
    layout.height = height;
    // The command capsule floats over the top of the scene.
    // The shell's rail holds the command capsule above this surface.
    layout.top_clear = 4;
    const double bottom = 6;
    const double room_h = std::max(40.0, height - layout.top_clear - bottom);
    const double side = std::max(40.0, std::min(width - 24, room_h));
    layout.board = {(width - side) * .5, layout.top_clear + (room_h - side) * .5, side, side};
    const double panel_w = std::min(440.0, width - 32);
    const double panel_h = std::min(330.0, room_h - 8);
    layout.panel = {(width - panel_w) * .5, layout.top_clear + (room_h - panel_h) * .5, panel_w, panel_h};
    return layout;
}

void begin_arrival(Motion& motion, const Puzzle& puzzle, bool reduced) {
    motion.departed = false;
    motion.clock = 0;
    motion.phase = reduced ? Phase::resting : Phase::arriving;
    const std::size_t pairs = puzzle.ends.size();
    motion.drawn.assign(pairs, 0);
    motion.grow.assign(pairs, 1.0);
    motion.ripple.assign(pairs, -1.0);
    motion.joined.assign(pairs, 0);
}

void begin_completion(Motion& motion, bool reduced) {
    motion.clock = 0;
    motion.phase = reduced ? Phase::holding : Phase::lighting;
}

void begin_departure(Motion& motion, bool reduced) {
    motion.clock = 0;
    motion.phase = reduced ? Phase::departed : Phase::departing;
    motion.departed = reduced;
}

void note_lines(Motion& motion, const Puzzle& puzzle, const Play& play, bool reduced) {
    const std::size_t pairs = puzzle.ends.size();
    if (motion.drawn.size() != pairs) {
        motion.drawn.assign(pairs, 0);
        motion.grow.assign(pairs, 1.0);
        motion.ripple.assign(pairs, -1.0);
        motion.joined.assign(pairs, 0);
    }
    for (std::size_t pair = 0; pair < pairs && pair < play.paths.size(); ++pair) {
        const int length = static_cast<int>(play.paths[pair].size());
        if (length > motion.drawn[pair] && motion.drawn[pair] >= 1 && !reduced) {
            motion.grow[pair] = 0;
        } else if (length <= motion.drawn[pair] || reduced) {
            motion.grow[pair] = 1;
        }
        motion.drawn[pair] = length;
        const bool joined = complete(puzzle, play.paths[pair], static_cast<int>(pair));
        if (joined && !motion.joined[pair]) {
            motion.ripple[pair] = reduced ? -1.0 : 0.0;
        }
        if (!joined) {
            motion.ripple[pair] = -1;
        }
        motion.joined[pair] = joined ? 1 : 0;
    }
}

void lean(Motion& motion, double yaw, double pitch) {
    motion.target_yaw = yaw;
    motion.target_pitch = pitch;
}

void hold_tilt(Motion& motion) {
    motion.target_yaw = motion.yaw;
    motion.target_pitch = motion.pitch;
}

bool advance(Motion& motion, const Puzzle& puzzle, const Play& play, double seconds, bool reduced) {
    static_cast<void>(puzzle);
    static_cast<void>(play);
    bool moving = false;
    moving = ease_toward(motion.yaw, motion.target_yaw, seconds, reduced) || moving;
    moving = ease_toward(motion.pitch, motion.target_pitch, seconds, reduced) || moving;
    for (std::size_t pair = 0; pair < motion.grow.size(); ++pair) {
        if (motion.grow[pair] < 1) {
            motion.grow[pair] = reduced ? 1 : std::min(1.0, motion.grow[pair] + seconds / step_seconds);
            moving = true;
        }
        if (motion.ripple[pair] >= 0) {
            motion.ripple[pair] += seconds;
            if (motion.ripple[pair] >= ripple_seconds || reduced) {
                motion.ripple[pair] = -1;
            }
            moving = true;
        }
    }
    switch (motion.phase) {
    case Phase::arriving:
        motion.clock += seconds;
        if (reduced || motion.clock >= arrival_seconds) {
            motion.phase = Phase::resting;
            motion.clock = 0;
        }
        moving = true;
        break;
    case Phase::lighting:
        motion.clock += seconds;
        if (reduced || motion.clock >= lighting_seconds) {
            begin_departure(motion, reduced);
        }
        moving = true;
        break;
    case Phase::departing:
        motion.clock += seconds;
        if (reduced || motion.clock >= departure_seconds) {
            motion.phase = Phase::departed;
            motion.departed = true;
        }
        moving = true;
        break;
    case Phase::holding:
        // Resting lit, with nothing moving: the view wakes once when it is time.
        motion.clock += seconds;
        if (motion.clock >= hold_seconds) {
            motion.phase = Phase::departed;
            motion.departed = true;
            moving = true;
        }
        break;
    case Phase::resting:
    case Phase::departed:
        break;
    }
    return moving;
}

double seconds_until_event(const Motion& motion) {
    if (motion.phase == Phase::holding) {
        return std::max(0.0, hold_seconds - motion.clock);
    }
    return -1;
}

Pose pose(const Motion& motion) {
    Pose result;
    result.yaw = motion.yaw;
    result.pitch = motion.pitch;
    if (motion.phase == Phase::arriving) {
        const double t = motion.clock / spin_seconds;
        const double settle = ease_out_cubic(t);
        result.yaw = motion.yaw - 2.3 * (1 - settle);
        result.pitch = motion.pitch + .45 * (1 - settle);
        result.scale = .3 + .7 * ease_out_back(t);
        result.lift = -.1 * (1 - settle);
        result.opacity = clamp01(t * 3);
    } else if (motion.phase == Phase::departing) {
        const double t = motion.clock / departure_seconds;
        const double away = ease_in_cubic(t);
        result.yaw = motion.yaw + 2.9 * away;
        result.pitch = motion.pitch - .3 * away;
        result.scale = 1 - .6 * away;
        result.lift = -.16 * away;
        result.drift = .05 * away;
        result.opacity = 1 - clamp01((t - .45) / .55);
    } else if (motion.phase == Phase::departed) {
        result.opacity = 0;
    }
    return result;
}

double stone_rise(const Motion& motion, const Puzzle& puzzle, int cell) {
    if (motion.phase != Phase::arriving) {
        return 1;
    }
    const double start = stones_begin + stones_spread * corner_distance(puzzle, cell);
    return ease_out_back((motion.clock - start) / stone_seconds);
}

double tile_bloom(const Motion& motion, const Puzzle& puzzle, int cell) {
    if (motion.phase != Phase::arriving || cell < 0 || cell >= puzzle.geometry.cells) {
        return 1;
    }
    const int pair = puzzle.pair_of[static_cast<std::size_t>(cell)];
    const int portal = puzzle.portal_of[static_cast<std::size_t>(cell)];
    double start = bloom_begin;
    if (pair >= 0) {
        start += .045 * pair;
    } else if (portal >= 0) {
        start += .4 + .1 * portal;
    }
    return ease_out_back((motion.clock - start) / bloom_seconds);
}

double growth(const Motion& motion, int pair) {
    if (pair < 0 || pair >= static_cast<int>(motion.grow.size())) {
        return 1;
    }
    return motion.grow[static_cast<std::size_t>(pair)];
}

double line_glow(const Motion& motion, int pair, double along) {
    double glow = 0;
    if (pair >= 0 && pair < static_cast<int>(motion.ripple.size()) &&
        motion.ripple[static_cast<std::size_t>(pair)] >= 0) {
        const double t = motion.ripple[static_cast<std::size_t>(pair)] / ripple_seconds;
        const double front = t * 1.4 - .2;
        const double fade = 1 - t;
        glow = std::max(glow, (1 - std::min(1.0, std::fabs(along - front) / .2)) * fade);
    }
    if (motion.phase == Phase::lighting) {
        const double start = .1 * pair;
        const double lit = clamp01((motion.clock - start) / .45);
        const double front = (motion.clock - start) / .45 * 1.3 - .15;
        const double sparkle = std::max(0.0, 1 - std::fabs(along - front) / .15);
        glow = std::max(glow, .55 * lit + .45 * sparkle * (front < 1.2 ? 1 : 0));
    } else if (motion.phase == Phase::departing || motion.phase == Phase::holding) {
        glow = std::max(glow, .55);
    }
    return clamp01(glow);
}

}  // namespace ps_cube
