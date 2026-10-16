#include "stage.hpp"

#include "noise.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace ts {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double gravity = 46;  // cells per second squared, for falling stones

double smoothstep(double edge0, double edge1, double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

double fall_time(double distance) {
    return std::sqrt(2 * std::max(distance, .05) / gravity);
}

void cell_point(int cell, double& x, double& y) {
    x = cell % side + .5;
    y = cell / side + .5;
}

}  // namespace

Layout compute_layout(double width, double height) {
    Layout layout;
    layout.width = width;
    layout.height = height;
    const double margin = 8;
    const double help_corner = 48;   // Help's question mark in the top-right corner
    const double capsule_bottom = 58;
    const double counts_least = 170;
    const double counts_wide = std::clamp(width * .26, counts_least, 300.0);
    const double by_height = (height - 2 * margin) / side;
    const double by_width = (width - help_corner - margin - counts_least) / side;
    layout.cell = std::max(8.0, std::min(by_height, by_width));
    const double grid = layout.cell * side;
    // The grid in the room right of the counts, kept clear of Help's corner.
    const double room_left = std::min(counts_wide, width - help_corner - grid);
    const double room_right = width - help_corner;
    layout.grid_x = room_left + std::max(0.0, (room_right - room_left - grid) * .5);
    layout.grid_y = std::max(margin, (height - grid) * .5);
    layout.counts = Box{margin + 8, capsule_bottom, std::max(40.0, layout.grid_x - 2 * margin - 16),
                        std::max(40.0, height - capsule_bottom - margin)};
    layout.text = std::clamp(layout.cell * .22, 12.0, 17.0);
    // The panels sit over the middle of the grid.
    const double panel_w = std::min(grid * .86, 430.0);
    const double panel_h = std::min(grid * .74, 300.0);
    layout.panel = Box{layout.grid_x + (grid - panel_w) * .5, layout.grid_y + (grid - panel_h) * .5, panel_w, panel_h};
    const double button = std::clamp(panel_h * .2, 36.0, 52.0);
    const Box& p = layout.panel;
    layout.fewer = Box{p.x + p.w * .5 - button * 2.4, p.y + p.h * .5 - button * .5, button, button};
    layout.more = Box{p.x + p.w * .5 + button * 1.4, p.y + p.h * .5 - button * .5, button, button};
    const double begin_w = std::min(p.w * .6, 200.0);
    layout.begin = Box{p.x + (p.w - begin_w) * .5, p.y + p.h - button - 14, begin_w, button};
    layout.cancel = Box{p.x + p.w - 40, p.y + 4, 36, 36};
    return layout;
}

int pick_cell(const Layout& layout, double x, double y) {
    const double gx = (x - layout.grid_x) / layout.cell;
    const double gy = (y - layout.grid_y) / layout.cell;
    if (gx < 0 || gy < 0 || gx >= side || gy >= side)
        return -1;
    const int column = static_cast<int>(gx);
    const int row = static_cast<int>(gy);
    return row * side + column;
}

void cell_centre(const Layout& layout, int cell, double& x, double& y) {
    x = layout.grid_x + (cell % side + .5) * layout.cell;
    y = layout.grid_y + (cell / side + .5) * layout.cell;
}

void Motion::settle(const Board& board) {
    pieces_.clear();
    cues_.clear();
    pull_starts_.clear();
    pull_sizes_.clear();
    pulls_cued_ = 0;
    clock_ = 0;
    finish_ = 0;
    juggle_start_ = -1;
    juggle_end_ = -1;
    juggle_cued_ = false;
    juggle_landed_ = false;
    for (int cell = 0; cell < cell_count; ++cell) {
        const Stone& stone = board.cells[static_cast<std::size_t>(cell)];
        if (stone.family < 0)
            continue;
        Piece piece;
        piece.serial = stone.serial;
        piece.look = stone;
        piece.landing_look = stone;
        piece.family = board.families[static_cast<std::size_t>(stone.family)];
        piece.landing_family = piece.family;
        cell_point(cell, piece.x, piece.y);
        pieces_.push_back(piece);
    }
}

void Motion::plan(const Board& before_board, const Outcome& outcome, const Board& after, bool reduced) {
    const std::array<Stone, cell_count>& before = before_board.cells;
    if (reduced) {
        settle(after);
        return;
    }
    settle(after);
    std::map<std::uint32_t, int> was;
    for (int cell = 0; cell < cell_count; ++cell)
        if (before[static_cast<std::size_t>(cell)].family >= 0)
            was[before[static_cast<std::size_t>(cell)].serial] = cell;
    std::map<std::uint32_t, std::size_t> index;
    for (std::size_t i = 0; i < pieces_.size(); ++i)
        index[pieces_[i].serial] = i;

    // Pulls, one after another.
    double time = 0;
    std::size_t first = 0;
    for (std::size_t p = 0; p < outcome.pull_sizes.size(); ++p) {
        const double start = p * .14;
        pull_starts_.push_back(start);
        pull_sizes_.push_back(outcome.pull_sizes[p]);
        for (int k = 0; k < outcome.pull_sizes[p]; ++k) {
            const int cell = outcome.pulled[first + static_cast<std::size_t>(k)];
            const Stone& stone = before[static_cast<std::size_t>(cell)];
            Piece piece;
            piece.serial = stone.serial;
            piece.look = stone;
            piece.landing_look = stone;
            piece.family = before_board.families[static_cast<std::size_t>(stone.family)];
            piece.landing_family = piece.family;
            cell_point(cell, piece.x, piece.y);
            Piece::Leg& leg = piece.legs[piece.leg_count++];
            leg.kind = 1;
            leg.start = start + k * .015;
            leg.end = leg.start + pull_time;
            leg.x0 = leg.x1 = piece.x;
            leg.y0 = leg.y1 = piece.y;
            piece.landed = false;
            time = std::max(time, leg.end);
            pieces_.push_back(piece);
        }
        first += static_cast<std::size_t>(outcome.pull_sizes[p]);
    }
    const double fall_start = outcome.pulled.empty() ? 0.0 : time - pull_time * .45;

    // Falling and filling, to the tray before any juggle.
    const std::array<Stone, cell_count>& filled = outcome.juggled ? outcome.before_juggle : after.cells;
    std::map<std::uint32_t, int> fell_to;
    double fall_end = fall_start;
    int arriving[side] = {};
    for (int cell = 0; cell < cell_count; ++cell) {
        const Stone& stone = filled[static_cast<std::size_t>(cell)];
        if (stone.family >= 0 && was.count(stone.serial) == 0)
            ++arriving[cell % side];
    }
    for (int cell = 0; cell < cell_count; ++cell) {
        const Stone& stone = filled[static_cast<std::size_t>(cell)];
        if (stone.family < 0)
            continue;
        fell_to[stone.serial] = cell;
        std::map<std::uint32_t, std::size_t>::const_iterator found = index.find(stone.serial);
        if (found == index.end())
            continue;
        Piece& piece = pieces_[(*found).second];
        double x0 = 0;
        double y0 = 0;
        double x1 = 0;
        double y1 = 0;
        cell_point(cell, x1, y1);
        std::map<std::uint32_t, int>::const_iterator from = was.find(stone.serial);
        if (from != was.end()) {
            if ((*from).second == cell)
                continue;
            cell_point((*from).second, x0, y0);
            piece.look = before[static_cast<std::size_t>((*from).second)];
            piece.family = before_board.families[static_cast<std::size_t>(piece.look.family)];
        } else {
            // A new stone, dropping in from above the tray in its column's order.
            x0 = x1;
            y0 = y1 - arriving[cell % side] - .7;
            piece.look = stone;
        }
        Piece::Leg& leg = piece.legs[piece.leg_count++];
        leg.kind = 2;
        leg.start = fall_start + (cell % side) * .012;
        leg.end = leg.start + fall_time(y1 - y0);
        leg.x0 = x0;
        leg.y0 = y0;
        leg.x1 = x1;
        leg.y1 = y1;
        piece.landed = false;
        fall_end = std::max(fall_end, leg.end);
    }

    // The juggle: every stone lifted, thrown across the tray turning over, set down anew.
    if (outcome.juggled) {
        juggle_start_ = fall_end + .1;
        double land = juggle_start_;
        for (int cell = 0; cell < cell_count; ++cell) {
            const Stone& stone = after.cells[static_cast<std::size_t>(cell)];
            if (stone.family < 0)
                continue;
            std::map<std::uint32_t, std::size_t>::const_iterator found = index.find(stone.serial);
            if (found == index.end())
                continue;
            Piece& piece = pieces_[(*found).second];
            double x1 = 0;
            double y1 = 0;
            cell_point(cell, x1, y1);
            std::map<std::uint32_t, int>::const_iterator from = fell_to.find(stone.serial);
            Piece::Leg& leg = piece.legs[piece.leg_count++];
            const std::uint32_t h = hash_bits(stone.serial * 2654435761U);
            const double stagger = static_cast<double>(h & 0xffU) / 255.0 * .12;
            if (from == fell_to.end()) {
                // Filled after the juggle: drops in once the others are down.
                leg.kind = 2;
                leg.start = juggle_start_ + juggle_rise + juggle_fly + juggle_fall;
                leg.x0 = x1;
                leg.y0 = y1 - 3;
                leg.end = leg.start + fall_time(3);
                piece.look = stone;
            } else {
                double x0 = 0;
                double y0 = 0;
                cell_point((*from).second, x0, y0);
                leg.kind = 3;
                leg.start = juggle_start_ + stagger;
                leg.end = leg.start + juggle_rise + juggle_fly + juggle_fall;
                leg.x0 = x0;
                leg.y0 = y0;
                leg.spin = (h >> 8) % 2 == 0 ? 1.0 : -1.0;
                if (piece.leg_count == 1) {
                    piece.look = outcome.before_juggle[static_cast<std::size_t>((*from).second)];
                    // A stone turned into the incoming family keeps its old shape until it turns over.
                    std::map<std::uint32_t, int>::const_iterator old = was.find(stone.serial);
                    if (old != was.end())
                        piece.family = before_board.families[static_cast<std::size_t>(before[static_cast<std::size_t>((*old).second)].family)];
                }
            }
            leg.x1 = x1;
            leg.y1 = y1;
            piece.landing_look = stone;
            piece.landing_family = after.families[static_cast<std::size_t>(stone.family)];
            piece.landed = false;
            land = std::max(land, leg.end);
        }
        juggle_end_ = juggle_start_ + juggle_rise + juggle_fly + juggle_fall;
        fall_end = std::max(fall_end, land);
    }
    finish_ = std::max(time, fall_end);
    for (Piece& piece : pieces_)
        place(piece);
}

void Motion::pour(const Board& board, bool reduced) {
    settle(board);
    if (reduced)
        return;
    double end = 0;
    // settle laid the pieces out in the cells' order.
    std::size_t next = 0;
    for (int cell = 0; cell < cell_count; ++cell) {
        if (board.cells[static_cast<std::size_t>(cell)].family < 0)
            continue;
        Piece& piece = pieces_[next++];
        const int column = cell % side;
        const int row = cell / side;
        double x1 = 0;
        double y1 = 0;
        cell_point(cell, x1, y1);
        // The column's stones in a train above the tray, the lowest first, a gap between each.
        const std::uint32_t h = hash_bits(static_cast<std::uint32_t>(column) * 2654435761U + board.cells[0].serial);
        const double jitter = static_cast<double>(h & 0xffU) / 255.0 * .05;
        Piece::Leg& leg = piece.legs[piece.leg_count++];
        leg.kind = 2;
        leg.start = column * .06 + jitter;
        leg.x0 = x1;
        leg.y0 = -.6 - (side - 1 - row) * 1.15;
        leg.x1 = x1;
        leg.y1 = y1;
        leg.end = leg.start + fall_time(y1 - leg.y0);
        piece.landed = false;
        end = std::max(end, leg.end);
    }
    finish_ = end;
    for (Piece& piece : pieces_)
        place(piece);
}

void Motion::place(Piece& piece) const {
    if (piece.leg_count == 0) {
        piece.moving = false;
        piece.dropping = false;
        return;
    }
    const Piece::Leg& last = piece.legs[piece.leg_count - 1];
    piece.moving = clock_ < last.end;
    piece.dropping = false;
    piece.lift = 0;
    piece.scale = 1;
    piece.opacity = 1;
    piece.spin = 0;
    piece.squash = 1;
    if (clock_ >= last.end) {
        piece.x = last.x1;
        piece.y = last.y1;
        if (last.kind == 1) {
            piece.gone = true;
        } else {
            piece.look = piece.landing_look;
            piece.family = piece.landing_family;
        }
        return;
    }
    // The leg under way, or the first one not yet begun (the stone waits where it was).
    int at = 0;
    while (at + 1 < piece.leg_count && clock_ >= piece.legs[at].end)
        ++at;
    const Piece::Leg& leg = piece.legs[at];
    piece.dropping = leg.kind == 2;
    if (clock_ < leg.start) {
        piece.x = leg.x0;
        piece.y = leg.y0;
        return;
    }
    const double u = std::clamp((clock_ - leg.start) / (leg.end - leg.start), 0.0, 1.0);
    if (leg.kind == 1) {
        piece.x = leg.x0;
        piece.y = leg.y0 - .18 * u;
        piece.lift = smoothstep(0, .35, u);
        piece.scale = 1 + .22 * u;
        piece.opacity = 1 - smoothstep(.3, 1, u);
    } else if (leg.kind == 2) {
        const double fallen = u * u;
        piece.x = leg.x0 + (leg.x1 - leg.x0) * u;
        piece.y = leg.y0 + (leg.y1 - leg.y0) * fallen;
        piece.lift = .15 * (1 - u);
    } else {
        const double t = clock_ - leg.start;
        if (t < juggle_rise) {
            const double v = smoothstep(0, 1, t / juggle_rise);
            piece.x = leg.x0;
            piece.y = leg.y0 - .1 * v;
            piece.lift = v;
            piece.scale = 1 + .12 * v;
        } else if (t < juggle_rise + juggle_fly) {
            const double v = smoothstep(0, 1, (t - juggle_rise) / juggle_fly);
            piece.x = leg.x0 + (leg.x1 - leg.x0) * v;
            piece.y = leg.y0 - .1 + (leg.y1 - leg.y0) * v - .7 * std::sin(pi * v);
            piece.lift = 1;
            piece.scale = 1.12 + .08 * std::sin(pi * v);
            piece.spin = leg.spin * 2 * pi * v;
            piece.squash = std::abs(std::cos(pi * v));
            if (v >= .5) {
                piece.look = piece.landing_look;
                piece.family = piece.landing_family;
            }
        } else {
            const double v = smoothstep(0, 1, (t - juggle_rise - juggle_fly) / juggle_fall);
            piece.x = leg.x1;
            piece.y = leg.y1 - .1 * (1 - v);
            piece.lift = 1 - v;
            piece.scale = 1.12 - .12 * v;
            piece.look = piece.landing_look;
            piece.family = piece.landing_family;
        }
    }
}

bool Motion::advance(double seconds) {
    const double before = clock_;
    clock_ = std::min(clock_ + std::max(0.0, seconds), finish_);
    for (Piece& piece : pieces_) {
        const bool was_landed = piece.landed;
        place(piece);
        if (!was_landed && piece.leg_count > 0) {
            const Piece::Leg& last = piece.legs[piece.leg_count - 1];
            if (clock_ >= last.end) {
                piece.landed = true;
                if (last.kind == 2)
                    cues_.push_back(Cue{2, 0, piece.x / side});
            }
        }
    }
    while (pulls_cued_ < pull_starts_.size() && clock_ >= pull_starts_[pulls_cued_]) {
        cues_.push_back(Cue{1, pull_sizes_[pulls_cued_], .5});
        ++pulls_cued_;
    }
    if (juggle_start_ >= 0 && !juggle_cued_ && clock_ >= juggle_start_) {
        juggle_cued_ = true;
        cues_.push_back(Cue{3, 0, .5});
    }
    if (juggle_end_ >= 0 && !juggle_landed_ && clock_ >= juggle_end_) {
        juggle_landed_ = true;
        cues_.push_back(Cue{4, 0, .5});
    }
    if (clock_ >= finish_ && before < finish_) {
        // Pulled stones are gone for good.
        std::vector<Piece> kept;
        for (const Piece& piece : pieces_)
            if (!piece.gone)
                kept.push_back(piece);
        pieces_ = kept;
    }
    return clock_ < finish_;
}

std::vector<Motion::Cue> Motion::take_cues() {
    std::vector<Cue> out;
    out.swap(cues_);
    return out;
}

}  // namespace ts
