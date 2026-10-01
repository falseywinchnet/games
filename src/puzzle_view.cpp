#include "puzzle_view.hpp"
#include "audio.hpp"
#include "gui_forms/window.hpp"
#include "presentation.hpp"
#include "storage.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
namespace games {
static const gf::Color ink = gf::Color::rgba(231, 238, 247), muted = gf::Color::rgba(154, 177, 198),
                       accent = gf::Color::rgba(119, 216, 229);
static gf::Color peg_color(int n) {
    const gf::Color colors[] = {gf::Color::rgba(47, 60, 79),    gf::Color::rgba(228, 72, 102),
                                gf::Color::rgba(67, 175, 244),  gf::Color::rgba(246, 189, 70),
                                gf::Color::rgba(113, 211, 137), gf::Color::rgba(167, 121, 237),
                                gf::Color::rgba(71, 211, 197)};
    return colors[std::clamp(n, 0, 6)];
}
static bool in_rect(gf::Rect r, gf::Point p) {
    return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height;
}
static gf::ImageId load_art(gf::Window& window, const std::string& name) {
    std::ifstream file(asset_directory() + "/" + name, std::ios::binary | std::ios::ate);
    if (!file)
        return {};
    std::streamsize size = file.tellg();
    file.seekg(0);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    gf::ImageLoadResult result = window.load_png(bytes);
    return result ? result.image : gf::ImageId{};
}
PuzzleView::PuzzleView(gf::StableId id, PuzzleKind kind) : Control(std::move(id)), game(kind) {
    set_focusable(true);
    set_accessible_name(puzzle_title(kind));
    if (!game.load(path())) {
        if (game.kind == PuzzleKind::cube || game.kind == PuzzleKind::solve) {
            PuzzleGame previous(game.kind);
            if (previous.load(cabinet_path().parent_path() /
                              (std::string(puzzle_slug(game.kind)) + "-v1.txt"))) {
                game.scores = previous.scores;
                game.player_name = previous.player_name;
            }
        }
        game.deal(static_cast<std::uint32_t>(
            std::chrono::system_clock::now().time_since_epoch().count()));
    }
}
std::filesystem::path PuzzleView::path() const {
    return cabinet_path().parent_path() /
           (std::string(puzzle_slug(game.kind)) +
            ((game.kind == PuzzleKind::cube || game.kind == PuzzleKind::solve) ? "-v2.txt"
                                                                               : "-v1.txt"));
}
void PuzzleView::initialize_control_tree() {
    const char* labels[] = {"New game", "Help",  "Top scores", "Submit",  "Rotate",
                            "Flip",     "Close", "Save name",  "New game"};
    for (int i = 0; i < 9; ++i) {
        buttons_[i] = gf::make_control<GameButton>(
            gf::StableId(std::string(puzzle_slug(game.kind)) + ".action." + std::to_string(i)),
            labels[i]);
        add_child(buttons_[i]);
        subscriptions_.push_back(
            (*buttons_[i])
                .clicked()
                .subscribe(
                    *this,
                    gf::Delegate<gf::ButtonBase&>::bind<PuzzleView, &PuzzleView::action>(*this)));
        (*buttons_[i])
            .set_visible(
                i < 3 ||
                (i == 3 && (game.kind == PuzzleKind::pegs || game.kind == PuzzleKind::atom)) ||
                ((i == 4 || i == 5) && game.kind == PuzzleKind::solve));
    }
    name_ = gf::make_control<gf::TextBox>(
        gf::StableId(std::string(puzzle_slug(game.kind)) + ".name"), game.player_name);
    (*name_).set_maximum_length(24);
    (*name_).set_accessible_name("Your name for the top scores");
    (*name_).set_visible(false);
    add_child(name_);
}
void PuzzleView::on_attached_to_window() {
    timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::milliseconds(16));
    subscriptions_.push_back((*timer_).tick().subscribe(
        *this, gf::Delegate<>::bind<PuzzleView, &PuzzleView::tick>(*this)));
    if (game.kind == PuzzleKind::cube) {
        nature_ = load_art(*attached_window(), "nature-lake.png");
        raster_.load_environment(asset_directory() + "/nature-lake.png");
    }
    if (game.kind == PuzzleKind::pegs)
        curator_ = load_art(*attached_window(), "four-pegs-curator.png");
    for (int i = 0; i < 4; ++i)
        guess_[i] = game.state.marks[i];
    clock_start_ = gf::FrameClock::now();
}
void PuzzleView::on_detaching_from_window(gf::Window& window) noexcept {
    persist();
    if (timer_)
        (*timer_).stop();
    timer_.reset();
    for (gf::ImageId id : {image_, nature_, curator_})
        if (id.value)
            static_cast<void>(window.remove_image(id));
}
void PuzzleView::activate() {
    Cabinet preferences;
    if (load_cabinet(cabinet_path(), preferences)) {
        sound_ = preferences.sound;
        music_ = preferences.music;
        reduced_ = preferences.reduced;
    }
    music_play(game.kind == PuzzleKind::cube ? "nature_cube" : puzzle_slug(game.kind), music_);
    persist();
    if (game.state.over && !game.recorded)
        panel(2);
    if (attached_window())
        static_cast<void>((*attached_window()).request_focus(shared_from_this()));
    if (game.kind == PuzzleKind::gems && !reduced_ && timer_)
        (*timer_).start();
    render();
    invalidate(gf::Dirty::paint);
}
void PuzzleView::arrange(gf::Rect b) {
    arrange_self(b);
    set_child_layout(buttons_[0], {b.width - 367, 18, 108, 34});
    set_child_layout(buttons_[1], {b.width - 247, 18, 80, 34});
    set_child_layout(buttons_[2], {b.width - 155, 18, 128, 34});
    double reserve = game.kind == PuzzleKind::solve                                   ? 496.0
                     : game.kind == PuzzleKind::pegs || game.kind == PuzzleKind::atom ? 424.0
                                                                                      : 300.0;
    double side = std::min({548.0, b.height - 155, b.width - reserve});
    board_ = {std::max(248.0, (b.width - side) * .5), 86, side, side};
    set_child_layout(buttons_[3],
                     {board_.x + board_.width + 37, board_.y + board_.height - 45, 128, 35});
    set_child_layout(buttons_[4],
                     {board_.x + board_.width + 26, board_.y + board_.height - 88, 87, 34});
    set_child_layout(buttons_[5],
                     {board_.x + board_.width + 122, board_.y + board_.height - 88, 77, 34});
    if (game.kind == PuzzleKind::pegs) {
        board_ = {24, 82, b.width - 332, b.height - 139};
        set_child_layout(buttons_[3],
                         {board_.x + board_.width - 151, board_.y + board_.height - 57, 124, 36});
        (*buttons_[3]).set_text("Check code");
    }
    popup_ = {b.width * .5 - 320, 86, 640, std::min(530.0, b.height - 103)};
    set_child_layout(buttons_[6], {popup_.x + 530, popup_.y + 17, 85, 32});
    set_child_layout(name_, {popup_.x + 28, popup_.y + 110, 270, 34});
    set_child_layout(buttons_[7], {popup_.x + 310, popup_.y + 110, 110, 34});
    set_child_layout(buttons_[8], {popup_.x + 490, popup_.y + popup_.height - 45, 122, 32});
    if (game.kind == PuzzleKind::gems || game.kind == PuzzleKind::cube) {
        int w = static_cast<int>(board_.width * 1.5), h = static_cast<int>(board_.height * 1.5);
        if (raster_.width != w || raster_.height != h)
            raster_.resize(w, h);
        render();
    }
}
void PuzzleView::text(gf::Painter& p, double x, double y, const std::string& s, double size,
                      gf::Color c) {
    p.draw_text_utf8({x, y}, s,
                     {size >= 18 ? gf::FontRole::control : gf::FontRole::content, size,
                      static_cast<std::uint16_t>(size >= 18 ? 600 : 400), false},
                     c);
}
double PuzzleView::elapsed() const {
    return std::chrono::duration<double>(gf::FrameClock::now() - animation_start_).count();
}
void PuzzleView::render() {
    if (!attached_window() || raster_.width <= 1)
        return;
    if (game.kind == PuzzleKind::cube)
        raster_.cube(game, yaw_, pitch_, hover_);
    else if (game.kind == PuzzleKind::gems) {
        raster_.clear();
        double time = std::chrono::duration<double>(gf::FrameClock::now() - clock_start_).count();
        double cell = raster_.width / 8.0;
        double t = elapsed();
        bool moving = animation_duration_ > 0 && t < animation_duration_;
        const std::array<int, 96>* grid = &game.state.grid;
        const std::array<int, 96>* source = nullptr;
        bool falling = false;
        double progress = 1;
        std::array<int, 64> origin{};
        if (moving) {
            if (t < .23 || invalid_swap_)
                grid = &before_swap_;
            else if (game.cascade_frames.size() > 1) {
                double frame_time = (t - .23) / .24;
                int step = std::clamp(static_cast<int>(frame_time), 0,
                                      static_cast<int>(game.cascade_frames.size()) - 2);
                progress = std::clamp(frame_time - step, 0.0, 1.0);
                source = &game.cascade_frames[step];
                grid = &game.cascade_frames[step + 1];
                falling = (step % 2) == 1;
                if (falling)
                    for (int x = 0; x < 8; ++x) {
                        int dest = 7;
                        for (int row = 7; row >= 0; --row)
                            if ((*source)[row * 8 + x])
                                origin[dest-- * 8 + x] = row;
                        int missing = dest + 1;
                        while (dest >= 0) {
                            origin[dest * 8 + x] = dest - missing;
                            --dest;
                        }
                    }
            }
        }
        for (int i = 0; i < 64; ++i) {
            int value = (*grid)[i];
            double radius = cell * .38;
            double x = (i % 8 + .5) * cell, y = (i / 8 + .5) * cell;
            if (source && !falling && (*source)[i] != value) {
                if (progress < .5) {
                    value = (*source)[i];
                    radius *= 1 - progress * 1.7;
                } else
                    radius *= .15 + (progress - .5) * 1.7;
            }
            if (!value || radius < .5)
                continue;
            if (falling) {
                double ease = 1 - std::pow(1 - progress, 3);
                y = (origin[i] + (i / 8 - origin[i]) * ease + .5) * cell;
            }
            if (moving && (i == swap_a_ || i == swap_b_) && (t < .23 || invalid_swap_)) {
                double u = std::clamp(t / .23, 0.0, 1.0);
                if (invalid_swap_ && t > .23)
                    u = 1 - std::clamp((t - .23) / .23, 0.0, 1.0);
                u = u * u * (3 - 2 * u);
                int other = i == swap_a_ ? swap_b_ : swap_a_;
                x += (other % 8 - i % 8) * cell * u;
                y += (other / 8 - i / 8) * cell * u;
            }
            double spin = hover_ == i && !reduced_ ? time * 2.1 : 0,
                   glisten = reduced_ ? 0 : .5 + .5 * std::sin(time * 1.25 + i * 2.7);
            raster_.gem({x, y}, radius, value, spin, glisten, i);
        }
    } else
        return;
    gf::ImageLoadResult result =
        image_.value ? (*attached_window())
                           .update_bgra32_premultiplied(image_, raster_.width, raster_.height,
                                                        raster_.width * 4, raster_.pixels, *this)
                     : (*attached_window())
                           .load_bgra32_premultiplied(raster_.width, raster_.height,
                                                      raster_.width * 4, raster_.pixels);
    if (result)
        image_ = result.image;
}
void PuzzleView::paint_gems(gf::Painter& p) {
    p.draw_box_shadow(board_, 16, {0, 8}, 24, 0, gf::Color::rgba(0, 0, 0, 100));
    p.fill_rounded_rect(board_, 16, gf::Color::rgba(12, 22, 42));
    double cell = board_.width / 8;
    for (int i = 0; i < 64; ++i)
        if ((i / 8 + i % 8) % 2 == 0)
            p.fill_rounded_rect(
                {board_.x + (i % 8) * cell + 2, board_.y + (i / 8) * cell + 2, cell - 4, cell - 4},
                7, gf::Color::rgba(26, 41, 63));
    if (image_.value)
        p.draw_image(image_, board_);
    text(p, 28, 113, "Match & cascade", 19, accent);
    text(p, 28, 146, "Drag to swap neighbors.", 13, ink);
    text(p, 28, 172, "Match 3 or more.", 13, muted);
    text(p, 28, 222, "4 in a line → bomb", 13, ink);
    text(p, 28, 250, "5 in a line → hypercube", 13, ink);
    text(p, 28, 278, "T or L → star", 13, ink);
    text(p, 28, 328, "Match bombs and stars", 13, muted);
    text(p, 28, 351, "with their own color.", 13, muted);
    text(p, 28, 398, std::to_string(game.gem_colors()) + " colors in play", 14, accent);
}
void PuzzleView::paint_untangle(gf::Painter& p) {
    std::vector<Point2> positions = game.state.nodes;
    if (drag_ >= 0)
        positions[drag_] = drag_position_;
    for (Edge e : game.state.edges) {
        Point2 a = positions[e.a], b = positions[e.b];
        p.draw_line({board_.x + a.x * board_.width, board_.y + a.y * board_.height},
                    {board_.x + b.x * board_.width, board_.y + b.y * board_.height},
                    gf::Color::rgba(112, 193, 214, 190), 2.3);
    }
    for (int i = 0; i < 12; ++i) {
        Point2 q = positions[i];
        gf::Rect r{board_.x + q.x * board_.width - 14, board_.y + q.y * board_.height - 14, 28, 28};
        p.draw_box_shadow(r, 14, {0, 3}, 10, 0, gf::Color::rgba(0, 0, 0, 100));
        p.fill_rounded_rect(
            r, 14, i == drag_ ? gf::Color::rgba(248, 210, 133) : gf::Color::rgba(183, 231, 226));
        p.stroke_rounded_rect(r, 14, gf::Color::rgba(75, 137, 153), 2);
        text(p, r.x + (i < 9 ? 10 : 6), r.y + 19, std::to_string(i + 1), 11,
             gf::Color::rgba(32, 67, 80));
    }
    text(p, 28, 119, "Untie the lines", 18, accent);
    text(p, 28, 157, "Drag the pearl points.", 13, ink);
    text(p, 28, 184, "No lines may cross.", 13, ink);
    text(p, 28, 235, "Points need their", 13, muted);
    text(p, 28, 258, "own breathing space.", 13, muted);
}
static void draw_peg(gf::Painter& p, gf::Rect r, int value, bool selected = false) {
    p.fill_rounded_rect({r.x - 3, r.y - 1, r.width + 6, r.height + 7}, r.width / 2,
                        gf::Color::rgba(8, 17, 23));
    gf::Color c = peg_color(value),
              lit = gf::Color::rgba(std::min(255, c.red + 55), std::min(255, c.green + 55),
                                    std::min(255, c.blue + 55));
    const gf::GradientStop stops[] = {
        {0, lit}, {.55, c}, {1, gf::Color::rgba(c.red * .45, c.green * .45, c.blue * .45)}};
    p.save();
    p.clip_rounded_rect(r, r.width / 2);
    p.fill_radial_gradient(r, {r.x + r.width * .35, r.y + r.height * .28},
                           {r.width * .7, r.height * .8}, stops);
    p.restore();
    p.stroke_rounded_rect(r, r.width / 2, gf::Color::rgba(198, 186, 152, 150), 1);
    if (value)
        p.draw_text_utf8(
            {r.x + r.width * .34, r.y + r.height * .68}, std::string(1, 'A' + value - 1),
            {gf::FontRole::content, r.height * .45, 700, false}, gf::Color::rgba(20, 30, 38));
    if (selected)
        p.stroke_rounded_rect({r.x - 6, r.y - 6, r.width + 12, r.height + 12}, r.width / 2 + 6,
                              gf::Color::rgba(240, 206, 135), 2);
}
void PuzzleView::paint_pegs(gf::Painter& p) {
    cells_.clear();
    double x = board_.x, y = board_.y, w = board_.width, h = board_.height, console = y + h - 207;
    p.save();
    p.clip_rect(board_);
    if (curator_.value)
        p.draw_image(curator_, {x, y, w, w * 2 / 3});
    p.fill_rect({x, y, 264, 135}, gf::Color::rgba(14, 23, 28, 178));
    text(p, x + 18, y + 28, "THE CURATOR", 11, gf::Color::rgba(227, 194, 133));
    std::string line1 = "I have chosen four symbols.", line2 = "Let us see what you can deduce.";
    if (game.state.over) {
        line1 = game.state.won ? "An elegant deduction." : "The secret outlasted you.";
        line2 = game.state.won ? "Shall we try another?" : "A new code awaits.";
    } else if (game.state.stage) {
        int n = game.state.aux[(game.state.stage - 1) * 2];
        line1 = n >= 3   ? "You are very close."
                : n >= 1 ? "A promising observation."
                         : "An informative experiment.";
        line2 = std::to_string(10 - game.state.stage) + " attempts remain.";
    }
    text(p, x + 18, y + 63, line1, 15, ink);
    text(p, x + 18, y + 89, line2, 13, muted);
    paint_polygon(p,
                  {{x + 28, console},
                   {x + w - 28, console},
                   {x + w, console + 38},
                   {x + w, y + h},
                   {x, y + h},
                   {x, console + 38}},
                  gf::Color::rgba(114, 122, 123));
    paint_polygon(p,
                  {{x + 28, console},
                   {x + w - 28, console},
                   {x + w - 12, console + 35},
                   {x + 12, console + 35}},
                  gf::Color::rgba(165, 172, 166));
    p.fill_rect({x + 12, console + 36, w - 24, 160}, gf::Color::rgba(60, 73, 81));
    text(p, x + 40, console + 25, "FOUR PEGS  /  DEDUCTION CONSOLE", 11,
         gf::Color::rgba(37, 49, 57));
    double unit = std::min(68.0, (w - 120) / 6), left = x + (w - (4 * unit + 3 * 18)) / 2;
    for (int j = 0; j < 4; ++j) {
        gf::Rect r{left + j * (unit + 18), console + 52, unit, unit};
        cells_.push_back(r);
        draw_peg(p, r, game.state.over ? game.state.secret[j] : guess_[j],
                 j == selection_ && !game.state.over);
    }
    double palette = y + h - 53;
    for (int i = 1; i <= 6; ++i) {
        gf::Rect r{x + 33 + (i - 1) * 48, palette, 32, 32};
        cells_.push_back(r);
        draw_peg(p, r, i);
    }
    p.restore();
    p.stroke_rect(board_, gf::Color::rgba(138, 148, 150), 1);
    double hx = x + w + 23, hy = y;
    gf::Rect history{hx, hy, 260, h};
    p.fill_rect(history, gf::Color::rgba(234, 230, 216));
    text(p, hx + 17, hy + 27, "ATTEMPT RECORD", 12, gf::Color::rgba(58, 65, 68));
    text(p, hx + 17, hy + 51, "Four places · six symbols · repeats", 12,
         gf::Color::rgba(100, 107, 108));
    double step = std::min(43.0, (h - 152) / 10);
    for (int a = 0; a < 10; ++a) {
        double yy = hy + 69 + a * step;
        if (a == game.state.stage)
            p.fill_rect({hx + 7, yy - 3, 246, step}, gf::Color::rgba(209, 219, 218));
        text(p, hx + 14, yy + 20, std::to_string(a + 1), 12, gf::Color::rgba(100, 107, 108));
        for (int j = 0; j < 4; ++j) {
            int v = a < game.state.stage ? game.state.grid[a * 4 + j] : 0;
            gf::Rect r{hx + 40 + j * 33, yy + 2, 24, 24};
            if (v)
                draw_peg(p, r, v);
            else
                p.stroke_rounded_rect(r, 12, gf::Color::rgba(169, 178, 177), 1);
        }
        for (int j = 0; j < 4; ++j) {
            gf::Rect r{hx + 193 + (j % 2) * 16, yy + 3 + (j / 2) * 15, 9, 9};
            int exact = game.state.aux[a * 2], elsewhere = game.state.aux[a * 2 + 1];
            gf::Color c = a >= game.state.stage || j >= exact + elsewhere
                              ? gf::Color::rgba(195, 198, 187)
                          : j < exact ? gf::Color::rgba(35, 46, 52)
                                      : gf::Color::rgba(255, 255, 251);
            p.fill_rounded_rect(r, 4.5, c);
            p.stroke_rounded_rect(r, 4.5, gf::Color::rgba(143, 151, 145), .7);
        }
    }
    text(p, hx + 16, hy + h - 56, "●  Dark: exact place", 13, gf::Color::rgba(56, 65, 66));
    text(p, hx + 16, hy + h - 32, "○  Light: elsewhere in the code", 13,
         gf::Color::rgba(56, 65, 66));
    p.stroke_rect(history, gf::Color::rgba(139, 154, 157), 1);
}
void PuzzleView::paint_atoms(gf::Painter& p) {
    cells_.clear();
    double cell = board_.width / 6, ox = board_.x + cell, oy = board_.y + cell;
    for (int i = 0; i < 16; ++i) {
        gf::Rect r{ox + i % 4 * cell, oy + i / 4 * cell, cell, cell};
        cells_.push_back(r);
        p.fill_rounded_rect({r.x + 3, r.y + 3, cell - 6, cell - 6}, 9, gf::Color::rgba(29, 49, 66));
        if (game.state.grid[i] || (game.state.over && game.state.secret[i])) {
            gf::Color c = game.state.over
                              ? game.state.secret[i] ? accent : gf::Color::rgba(229, 112, 124)
                              : gf::Color::rgba(229, 183, 105);
            p.fill_rounded_rect({r.x + cell * .27, r.y + cell * .27, cell * .46, cell * .46},
                                cell * .23, c);
        }
    }
    for (int port = 0; port < 16; ++port) {
        double x, y;
        if (port < 4) {
            x = ox + (port + .5) * cell;
            y = oy - cell * .5;
        } else if (port < 8) {
            x = ox + 4.5 * cell;
            y = oy + (port - 4 + .5) * cell;
        } else if (port < 12) {
            x = ox + (11 - port + .5) * cell;
            y = oy + 4.5 * cell;
        } else {
            x = ox - cell * .5;
            y = oy + (15 - port + .5) * cell;
        }
        gf::Rect r{x - 23, y - 23, 46, 46};
        cells_.push_back(r);
        p.fill_rounded_rect(r, 12,
                            game.state.aux[port] == -99 ? gf::Color::rgba(49, 70, 86)
                                                        : gf::Color::rgba(65, 114, 122));
        std::string label = game.state.aux[port] == -99  ? std::to_string(port + 1)
                            : game.state.aux[port] == -1 ? "A"
                            : game.state.aux[port] == -2 ? "R"
                                                         : std::to_string(game.state.aux[port] + 1);
        text(p, r.x + (label.size() == 1 ? 18 : 12), r.y + 29, label, 18, ink);
    }
    text(p, 28, 120, "Find three atoms", 21, accent);
    text(p, 28, 158, "Click an edge port", 13, ink);
    text(p, 28, 181, "to send a probe.", 13, ink);
    text(p, 28, 224, "A  Absorbed", 13, ink);
    text(p, 28, 252, "R  Reflected", 13, ink);
    text(p, 28, 280, "Number  Exit port", 13, ink);
    text(p, 28, 326, "Click squares to mark", 13, muted);
    text(p, 28, 349, "your three-atom guess.", 13, muted);
    text(p, 28, 400, "Help explains deflection.", 13, accent);
}
// Triangle atoms share integer half-cell vertices. Only exterior edges are
// outlined, so the player sees continuous polyforms rather than a triangle grid.
static std::array<std::pair<int, int>, 3> atom_vertices(PieceCell c) {
    const int corners[4][2] = {{0, 0}, {2, 0}, {2, 2}, {0, 2}};
    int w = c.wedge;
    return {{{2 * c.x + 1, 2 * c.y + 1},
             {2 * c.x + corners[w][0], 2 * c.y + corners[w][1]},
             {2 * c.x + corners[(w + 1) % 4][0], 2 * c.y + corners[(w + 1) % 4][1]}}};
}
static void draw_polyform(gf::Painter& p, const std::vector<PieceCell>& shape, double x, double y,
                          double unit, bool used = false,
                          gf::Color edge = gf::Color::rgba(44, 65, 78), bool ghost = false) {
    using Vertex = std::pair<int, int>;
    std::map<std::pair<Vertex, Vertex>, int> edges;
    for (const auto& c : shape) {
        auto v = atom_vertices(c);
        std::vector<gf::Point> points;
        for (auto q : v)
            points.push_back({x + q.first * unit * .5, y + q.second * unit * .5});
        gf::Color color = used           ? gf::Color::rgba(115, 127, 136)
                          : c.color == 1 ? gf::Color::rgba(47, 146, 221)
                                         : gf::Color::rgba(250, 205, 75);
        if (ghost)
            color.alpha = 110;
        paint_polygon(p, points, color);
        for (int j = 0; j < 3; ++j) {
            auto a = v[j], b = v[(j + 1) % 3];
            if (b < a)
                std::swap(a, b);
            ++edges[{a, b}];
        }
    }
    for (auto [e, count] : edges)
        if (count == 1)
            p.draw_line({x + e.first.first * unit * .5, y + e.first.second * unit * .5},
                        {x + e.second.first * unit * .5, y + e.second.second * unit * .5}, edge,
                        ghost ? 2 : 1.4);
}
void PuzzleView::paint_solve(gf::Painter& p) {
    double unit = board_.width / 4;
    p.fill_rect(board_, gf::Color::rgba(231, 233, 225));
    // Subtle registration dots are guides; there are no artificial square seams.
    for (int y = 0; y <= 4; ++y)
        for (int x = 0; x <= 4; ++x)
            p.fill_rounded_rect({board_.x + x * unit - 1.5, board_.y + y * unit - 1.5, 3, 3}, 1.5,
                                gf::Color::rgba(149, 160, 159));
    for (int piece = 0; piece < 7; ++piece) {
        std::vector<PieceCell> shape;
        for (int i = 0; i < 64; ++i)
            if (game.state.grid[i] / 4 == piece + 1)
                shape.push_back({(i / 4) % 4, i / 16, game.state.grid[i] % 4, i % 4});
        draw_polyform(p, shape, board_.x, board_.y, unit, false,
                      piece == selection_ ? accent : gf::Color::rgba(38, 64, 85));
    }
    if (hover_ >= 0 && !game.state.over) {
        auto shape = game.piece_cells(selection_, rotation_, flip_);
        bool valid = true;
        for (auto& c : shape) {
            c.x += hover_ % 4;
            c.y += hover_ / 4;
            if (c.x >= 4 || c.y >= 4 ||
                (game.state.grid[(c.y * 4 + c.x) * 4 + c.wedge] &&
                 game.state.grid[(c.y * 4 + c.x) * 4 + c.wedge] / 4 != selection_ + 1))
                valid = false;
        }
        p.save();
        p.clip_rect(board_);
        draw_polyform(p, shape, board_.x, board_.y, unit, false,
                      valid ? gf::Color::rgba(49, 137, 99) : gf::Color::rgba(204, 68, 72), true);
        p.restore();
    }
    p.stroke_rect(board_, gf::Color::rgba(144, 163, 169), 2);
    text(p, 28, 121, "The target", 20, accent);
    std::vector<PieceCell> target;
    for (int i = 0; i < 64; ++i)
        target.push_back({(i / 4) % 4, i / 16, game.state.secret[i], i % 4});
    draw_polyform(p, target, 28, 143, 38);
    text(p, 28, 330, "Seven diagonal pieces", 14, ink);
    text(p, 28, 362, "Drag a piece into the frame.", 13, ink);
    text(p, 28, 389, "R rotates. F reflects.", 13, ink);
    text(p, 28, 430, "Match both shape and color.", 13, muted);
    text(p, 28, 457, "Right click to lift a piece.", 13, muted);
    cells_.clear();
    double tx = board_.x + board_.width + 26, step = (board_.height - 112) / 4;
    text(p, tx, board_.y - 12, "PIECES", 11, muted);
    for (int piece = 0; piece < 7; ++piece) {
        gf::Rect r{tx + (piece % 2) * 95, board_.y + (piece / 2) * step, 86, step - 8};
        cells_.push_back(r);
        bool used = false;
        for (int i = 0; i < 64; ++i)
            used = used || game.state.grid[i] / 4 == piece + 1;
        p.fill_rounded_rect(r, 3,
                            piece == selection_ ? gf::Color::rgba(76, 101, 117)
                                                : gf::Color::rgba(37, 53, 68));
        auto shape = game.piece_cells(piece, piece == selection_ ? rotation_ : 0,
                                      piece == selection_ && flip_);
        int mx = 0, my = 0;
        for (auto c : shape) {
            mx = std::max(mx, c.x + 1);
            my = std::max(my, c.y + 1);
        }
        double u = std::min((r.width - 16) / mx, (r.height - 23) / my);
        draw_polyform(p, shape, r.x + (r.width - mx * u) / 2, r.y + 6, u, used,
                      gf::Color::rgba(195, 211, 213));
        text(p, r.x + 6, r.y + r.height - 5, std::to_string(piece + 1) + (used ? "   placed" : ""),
             11, muted);
        if (piece == selection_)
            p.stroke_rounded_rect(r, 3, accent, 1.5);
    }
    text(p, tx, board_.y + board_.height - 29, "Piece " + std::to_string(selection_ + 1), 14,
         accent);
}
void PuzzleView::paint_sticks(gf::Painter& p) {
    std::vector<Point2> hex = PuzzleGame::hex_cells();
    double unit = board_.width / 14.5, cx = board_.x + board_.width * .5,
           cy = board_.y + board_.height * .5;
    cells_.clear();
    for (int i = 0; i < 37; ++i) {
        double x = cx + unit * 1.7320508 * (hex[i].x + hex[i].y * .5),
               y = cy + unit * 1.5 * hex[i].y;
        gf::Rect r{x - unit * .8, y - unit * .8, unit * 1.6, unit * 1.6};
        cells_.push_back(r);
        if (hover_ >= 100 && hover_ < 121) {
            int axis = (hover_ - 100) / 7, line = (hover_ - 100) % 7 - 3;
            int coordinate = static_cast<int>(axis == 0   ? hex[i].x
                                              : axis == 1 ? hex[i].y
                                                          : -hex[i].x - hex[i].y);
            if (coordinate == line)
                for (int row = -static_cast<int>(unit * .88); row <= static_cast<int>(unit * .88);
                     ++row) {
                    double half = std::min(unit * .762, (unit * .88 - std::abs(row)) * 1.732);
                    p.draw_line({x - half, y + row}, {x + half, y + row},
                                gf::Color::rgba(130, 161, 109, 35), 1);
                }
        }
        for (int k = 0; k < 6; ++k) {
            double a = (k * 60 + 30) * 3.14159265 / 180, b = ((k + 1) * 60 + 30) * 3.14159265 / 180;
            p.draw_line({x + unit * .91 * std::cos(a), y + unit * .91 * std::sin(a)},
                        {x + unit * .91 * std::cos(b), y + unit * .91 * std::sin(b)},
                        gf::Color::rgba(103, 143, 143), 1.4);
        }
        int v = game.state.grid[i];
        if (v == 1) {
            p.fill_rounded_rect({x - unit * .36, y - unit * .30, unit * .72, unit * .62},
                                unit * .27, gf::Color::rgba(182, 191, 177));
            p.draw_line({x - unit * .18, y - unit * .13}, {x + unit * .12, y - unit * .19},
                        gf::Color::rgba(224, 231, 213), 2);
        } else if (v) {
            double angle = (v - 2) * 3.14159265 / 3;
            gf::Point a{x - std::cos(angle) * unit * .53, y - std::sin(angle) * unit * .53},
                b{x + std::cos(angle) * unit * .53, y + std::sin(angle) * unit * .53};
            if (v >= 5) {
                gf::Point previous = a;
                for (int k = 1; k <= 10; ++k) {
                    double t = k / 10.0;
                    gf::Point next{a.x + (b.x - a.x) * t -
                                       std::sin(angle) * std::sin(t * 3.14159265) * unit * .3,
                                   a.y + (b.y - a.y) * t +
                                       std::cos(angle) * std::sin(t * 3.14159265) * unit * .3};
                    p.draw_line(previous, next, gf::Color::rgba(215, 160, 100), 6);
                    previous = next;
                }
            } else
                p.draw_line(a, b, gf::Color::rgba(215, 160, 100), 6);
        }
    }
    for (int axis = 0; axis < 3; ++axis)
        for (int line = -3; line <= 3; ++line) {
            Point2 extreme{};
            bool first = true;
            for (Point2 c : hex) {
                int n = static_cast<int>(axis == 0 ? c.x : axis == 1 ? c.y : -c.x - c.y);
                if (n != line)
                    continue;
                Point2 q{cx + unit * 1.7320508 * (c.x + c.y * .5), cy + unit * 1.5 * c.y};
                if (first || (axis == 0   ? q.y < extreme.y
                              : axis == 1 ? q.x > extreme.x
                                          : q.x < extreme.x)) {
                    extreme = q;
                    first = false;
                }
            }
            Point2 endpoint = extreme;
            if (axis == 0) {
                extreme.x -= unit * .736;
                extreme.y -= unit * 1.275;
            } else if (axis == 1)
                extreme.x += unit * 1.30;
            else {
                extreme.x -= unit * .736;
                extreme.y += unit * 1.275;
            }
            clues_[axis * 7 + line + 3] = {extreme.x - 17, extreme.y - 11, 34, 22};
            bool highlighted = hover_ == 100 + axis * 7 + line + 3;
            p.draw_line({endpoint.x + (extreme.x - endpoint.x) * .68,
                         endpoint.y + (extreme.y - endpoint.y) * .68},
                        {endpoint.x + (extreme.x - endpoint.x) * .86,
                         endpoint.y + (extreme.y - endpoint.y) * .86},
                        highlighted ? gf::Color::rgba(213, 217, 145)
                                    : gf::Color::rgba(119, 145, 122),
                        highlighted ? 2 : 1);
            std::array<int, 2> target = game.hex_count(axis, line, true),
                               current = game.hex_count(axis, line, false);
            gf::Color color = current == target ? gf::Color::rgba(135, 225, 161)
                              : current[0] > target[0] || current[1] > target[1]
                                  ? gf::Color::rgba(240, 135, 132)
                                  : gf::Color::rgba(223, 207, 172);
            text(p, extreme.x - 8, extreme.y + 5,
                 std::to_string(target[0]) + "/" + std::to_string(target[1]), 12, color);
        }
    text(p, 28, 119, "Balance every line", 19, accent);
    text(p, 28, 157, "Perimeter: sticks / stones", 13, ink);
    text(p, 28, 191, "Every straight line", 13, muted);
    text(p, 28, 214, "must match its clue.", 13, muted);
    text(p, 28, 233, "Hover a clue to follow its line.", 11, accent);
    for (int value = 1; value <= 7; ++value) {
        int i = value - 1;
        gf::Rect tile{27 + (i % 2) * 99.0, 245 + (i / 2) * 66.0, 88, 56};
        cells_.push_back(tile);
        p.fill_rounded_rect(
            tile, 6, value == palette_ ? gf::Color::rgba(70, 93, 72) : gf::Color::rgba(36, 56, 47));
        p.stroke_rounded_rect(
            tile, 6,
            value == palette_ ? gf::Color::rgba(219, 197, 134) : gf::Color::rgba(105, 129, 108), 1);
        text(p, tile.x + 7, tile.y + 45, std::to_string(value), 11, muted);
        double x = tile.x + 47, y = tile.y + 27;
        if (value == 1) {
            p.fill_rounded_rect({x - 15, y - 11, 30, 23}, 11, gf::Color::rgba(183, 194, 177));
            p.draw_line({x - 7, y - 4}, {x + 5, y - 6}, gf::Color::rgba(229, 233, 219), 2);
        } else {
            double angle = (value - 2) * 3.14159265 / 3;
            gf::Point previous{};
            for (int k = 0; k <= 16; ++k) {
                double t = k / 16.0, u = (t - .5) * 38,
                       v = value >= 5 ? std::sin(t * 3.14159265) * 12 : 0;
                gf::Point q{x + u * std::cos(angle) - v * std::sin(angle),
                            y + u * std::sin(angle) + v * std::cos(angle)};
                if (k)
                    p.draw_line(previous, q, gf::Color::rgba(221, 171, 109), 5);
                previous = q;
            }
        }
    }
    text(p, 28, 536, "Click to place. Right click to clear.", 12, ink);
    text(p, 28, 558, "Keys 1–7 choose a shape.", 12, muted);
}
void PuzzleView::paint_help(gf::Painter& p) {
    std::vector<std::string> lines;
    if (game.kind == PuzzleKind::gems)
        lines = {"Drag between neighboring gems to match three or more of one color.",
                 "An invalid swap finishes, then returns both gems to their places.",
                 "Four in a line makes a colored bomb. Matching it clears a 3 × 3 area.",
                 "A T or L match makes a star. Matching it clears its row and column.",
                 "Five in a line makes a hypercube: swap it with a color to clear that color.",
                 "Blasts trigger other specials. Cascades refill the board from above.",
                 "Additional colors arrive as you progress. No available swap ends the run."};
    else if (game.kind == PuzzleKind::cube)
        lines = {"Connect all six colored pairs on the three visible faces.",
                 "Click a colored endpoint square and trace through neighboring squares.",
                 "Paths cannot share a square. Empty squares are allowed.",
                 "Move the mouse to tilt all three faces. No rotation drag is needed.",
                 "The cube holds still while you trace a path.",
                 "Click an original endpoint to redraw that pair; trace backward to erase.",
                 "Every generated puzzle includes a replay-verified solution."};
    else if (game.kind == PuzzleKind::untangle)
        lines = {"Drag points until no two unrelated edges cross or touch.",
                 "Connected edges may meet at their shared point.",
                 "Keep points separated; piling them together does not solve the puzzle.",
                 "Each graph was built from a planar solution before it was scrambled.",
                 "You can move one point at a time, or pull a cluster outward."};
    else if (game.kind == PuzzleKind::pegs)
        lines = {"Find the four-symbol code in ten guesses. Six symbols A–F are available.",
                 "The secret may repeat a symbol. Feedback counts each occurrence once.",
                 "Filled feedback: right symbol in the right position.",
                 "Ring feedback: right symbol, but in a different position.",
                 "Choose a place, then click a colored letter, or type A–F / 1–6.",
                 "Left / right selects a place. Enter submits the completed row.",
                 "The code remains fixed throughout the game."};
    else if (game.kind == PuzzleKind::atom)
        lines = {"Three atoms are hidden in a 4 × 4 chamber. Click ports to launch rays.",
                 "An atom directly ahead absorbs the ray (A), before any deflection.",
                 "An atom diagonally ahead bends the ray 90° away from that atom.",
                 "Atoms on both forward diagonals reflect it (R). A boundary deflection",
                 "also reflects immediately. A number labels the port where a ray emerges.",
                 "Click squares to mark exactly three atoms, then Submit your deduction.",
                 "The generator rejects boards with identical complete probe signatures."};
    else if (game.kind == PuzzleKind::solve)
        lines = {"Reproduce the blue / yellow target using all seven geometric pieces.",
                 "Triangles, a square, and a parallelogram carry blue / yellow artwork.",
                 "Choose a piece on the right. Rotate / R turns it; Flip / F mirrors it.",
                 "Click the large frame to place the piece with its top-left bounds there.",
                 "Pieces cannot overlap. Right click a placed piece to lift it.",
                 "The target is generated from an actual tiling, so a solution always exists.",
                 "An alternative arrangement with the same colors is also a solution."};
    else
        lines = {"Fill all 37 hexagons with a stick or stone.",
                 "Each perimeter clue reads number of sticks / number of stones.",
                 "The three straight-line directions all impose constraints at once.",
                 "Choose 1 for stone; 2–4 for straight sticks; 5–7 for curved sticks.",
                 "Different stick shapes have the same count. Right click clears a hex.",
                 "Green clues are satisfied; red clues exceed a required count.",
                 "Any complete arrangement satisfying every clue wins."};
    for (std::size_t i = 0; i < lines.size(); ++i)
        text(p, popup_.x + 28, popup_.y + 94 + i * 42, lines[i], 14, ink);
}
void PuzzleView::on_paint(gf::Painter& p, gf::Rect) {
    gf::Rect b = client_rectangle();
    p.fill_rect(b, gf::Color::rgba(112, 126, 133));
    if (game.kind != PuzzleKind::cube && game.kind != PuzzleKind::pegs) {
        double heights[] = {350, 0, 225, 347, 319, 288, 390, 495};
        gf::Rect notes{14, 86, 224, heights[static_cast<int>(game.kind)]};
        p.fill_rounded_rect(notes, 8, gf::Color::rgba(8, 22, 33, 110));
        p.stroke_rounded_rect(notes, 8, gf::Color::rgba(126, 151, 158, 45), 1);
        p.draw_line({28, 133}, {222, 133}, gf::Color::rgba(145, 174, 174, 65), 1);
        if (game.kind == PuzzleKind::untangle || game.kind == PuzzleKind::solve) {
            gf::Rect mat{board_.x - 10, board_.y - 10, board_.width + 20, board_.height + 20};
            p.draw_box_shadow(mat, 13, {0, 5}, 20, 0, gf::Color::rgba(0, 8, 19, 110));
            p.fill_rounded_rect(mat, 13, gf::Color::rgba(13, 30, 43));
            p.stroke_rounded_rect(mat, 13, gf::Color::rgba(92, 121, 133, 90), 1);
            p.draw_inset_box_shadow(mat, 13, {0, 3}, 8, 0, gf::Color::rgba(0, 5, 16, 100));
        }
    }
    if (game.kind == PuzzleKind::cube && nature_.value) {
        p.draw_image(nature_, b);
        p.fill_rect(b, gf::Color::rgba(8, 24, 24, 56));
        p.fill_rounded_rect({15, 83, 235, 176}, 12, gf::Color::rgba(15, 38, 42, 190));
        text(p, 28, 114, "Let each color find home", 17, ink);
        text(p, 28, 145, "Trace from a colored square.", 13, ink);
        text(p, 28, 174, "Move the mouse to tilt the cube.", 13, ink);
        text(p, 28, 203, "Hold a colored square to trace.", 13, ink);
        text(p, 28, 232, "Help: continue across faces.", 13, ink);
        if (image_.value)
            p.draw_image(image_, board_);
    } else if (game.kind == PuzzleKind::gems)
        paint_gems(p);
    else if (game.kind == PuzzleKind::untangle)
        paint_untangle(p);
    else if (game.kind == PuzzleKind::pegs)
        paint_pegs(p);
    else if (game.kind == PuzzleKind::atom)
        paint_atoms(p);
    else if (game.kind == PuzzleKind::solve)
        paint_solve(p);
    else if (game.kind == PuzzleKind::sticks)
        paint_sticks(p);
    paint_heading(p, {0, 0, b.width, 68}, puzzle_title(game.kind), static_cast<int>(game.kind) + 2);
    p.fill_rect({0, b.height - 43, b.width, 43}, gf::Color::rgba(17, 31, 45, 225));
    text(p, 28, b.height - 16, game.message, 13, ink);
    if (!panel_)
        return;
    p.fill_rect(b, gf::Color::rgba(4, 12, 26, 175));
    paint_dialog(p, popup_, panel_ == 1 ? "How to play" : "TOP SCORES",
                 game_accent(static_cast<int>(game.kind) + 2));
    if (panel_ == 1) {
        paint_help(p);
        return;
    }
    text(p, popup_.x + 28, popup_.y + 82,
         std::string(puzzle_title(game.kind)) + " · " +
             (game.kind == PuzzleKind::gems ? "Highest points" : "Fewest moves") +
             (game.state.over ? " · Result: " + std::to_string(game.result()) : ""),
         15, accent);
    if (!game.qualifies())
        text(p, popup_.x + 28, popup_.y + 132,
             game.recorded                        ? "Your name is saved."
             : game.state.over && !game.state.won ? "A fresh puzzle is waiting when you are ready."
                                                  : "Complete a game to enter your name.",
             14, ink);
    for (std::size_t i = 0; i < game.scores.size(); ++i) {
        text(p, popup_.x + 28, popup_.y + 178 + i * 27, std::to_string(i + 1), 16, accent);
        text(p, popup_.x + 75, popup_.y + 178 + i * 27, game.scores[i].name, 16, ink);
        text(p, popup_.x + 544, popup_.y + 178 + i * 27, std::to_string(game.scores[i].value), 16,
             accent);
    }
}
void PuzzleView::panel(int kind) {
    panel_ = kind;
    for (int i = 0; i < 6; ++i)
        (*buttons_[i]).set_enabled(!kind);
    (*buttons_[6]).set_visible(kind != 0);
    (*buttons_[7]).set_visible(kind == 2 && game.qualifies());
    (*buttons_[8]).set_visible(kind == 2 && game.state.over);
    (*name_).set_visible(kind == 2 && game.qualifies());
    if (kind == 2)
        (*name_).set_text(game.player_name);
    if (attached_window())
        static_cast<void>(
            (*attached_window()).request_focus(kind ? buttons_[6] : shared_from_this()));
    invalidate(gf::Dirty::paint);
}
void PuzzleView::persist() {
    if (game.kind == PuzzleKind::pegs)
        for (int i = 0; i < 4; ++i)
            game.state.marks[i] = guess_[i];
    if (!game.save(path()))
        game.message = "Your game is in memory; the local save could not be written.";
}
void PuzzleView::changed(const std::string& effect) {
    persist();
    if (game.state.over) {
        if (game.state.won)
            sound_play("stinger_" + std::string(game.qualifies() ? "topscore_" : "win_") +
                           (game.kind == PuzzleKind::cube ? "nature_cube" : puzzle_slug(game.kind)),
                       sound_);
        else
            sound_play(game.kind == PuzzleKind::pegs ? "four_pegs_out_of_turns"
                                                     : "atom_probe_check_wrong",
                       sound_);
        if (animation_duration_ <= 0)
            panel(2);
    } else if (!effect.empty())
        sound_play(effect, sound_);
    render();
    invalidate(gf::Dirty::paint);
}
void PuzzleView::new_game() {
    game.deal(
        static_cast<std::uint32_t>(std::chrono::system_clock::now().time_since_epoch().count()));
    animation_duration_ = 0;
    drag_ = -1;
    tracing_ = false;
    guess_.fill(0);
    selection_ = 0;
    rotation_ = 0;
    flip_ = false;
    set_pointer_capture(false);
    panel(0);
    persist();
    music_play(game.kind == PuzzleKind::cube ? "nature_cube" : puzzle_slug(game.kind), music_);
    sound_play("ui_new_game", sound_);
    render();
}
void PuzzleView::action(gf::ButtonBase& button) {
    std::string id(button.stable_id().value());
    int index = std::stoi(id.substr(id.rfind('.') + 1));
    if (index == 0 || index == 8)
        new_game();
    if (index == 1)
        panel(1);
    if (index == 2)
        panel(2);
    if (index == 3) {
        if (game.kind == PuzzleKind::pegs && game.guess_pegs(guess_)) {
            guess_.fill(0);
            selection_ = 0;
            changed("four_pegs_submit");
        } else if (game.kind == PuzzleKind::atom && game.submit_atoms())
            changed(game.state.won ? "atom_probe_check_correct" : "atom_probe_check_wrong");
    }
    if (index == 4) {
        rotation_ = (rotation_ + 1) % 4;
        sound_play("puzzle_solve_rotate", sound_);
    }
    if (index == 5) {
        flip_ = !flip_;
        sound_play("puzzle_solve_flip", sound_);
    }
    if (index == 6)
        panel(0);
    if (index == 7 && game.record(std::string((*name_).text()))) {
        persist();
        sound_play("ui_name_confirm", sound_);
        panel(2);
    }
    if (!panel_ && attached_window())
        static_cast<void>((*attached_window()).request_focus(shared_from_this()));
    invalidate(gf::Dirty::paint);
}
int PuzzleView::hit(gf::Point p) const {
    if (game.kind == PuzzleKind::gems) {
        if (!in_rect(board_, p))
            return -1;
        int x = static_cast<int>((p.x - board_.x) * 8 / board_.width),
            y = static_cast<int>((p.y - board_.y) * 8 / board_.height);
        return y * 8 + x;
    }
    if (game.kind == PuzzleKind::cube) {
        if (!in_rect(board_, p) || raster_.ids.empty())
            return -1;
        int x = std::clamp(static_cast<int>((p.x - board_.x) / board_.width * raster_.width), 0,
                           raster_.width - 1),
            y = std::clamp(static_cast<int>((p.y - board_.y) / board_.height * raster_.height), 0,
                           raster_.height - 1);
        return raster_.ids[y * raster_.width + x];
    }
    if (game.kind == PuzzleKind::untangle) {
        for (int i = 0; i < 12; ++i)
            if (std::hypot(p.x - board_.x - game.state.nodes[i].x * board_.width,
                           p.y - board_.y - game.state.nodes[i].y * board_.height) < 22)
                return i;
        return -1;
    }
    for (std::size_t i = 0; i < cells_.size(); ++i)
        if (in_rect(cells_[i], p))
            return static_cast<int>(i);
    return -1;
}
void PuzzleView::tick() {
    if (!effectively_visible()) {
        if (timer_)
            (*timer_).stop();
        return;
    }
    bool moving = false;
    if (game.kind == PuzzleKind::gems && animation_duration_ > 0 && !invalid_swap_ &&
        elapsed() >= .23 && !game.state.over) {
        int step = static_cast<int>((elapsed() - .23) / .24);
        if (step != cascade_step_ && step % 2 == 0 &&
            step + 1 < static_cast<int>(game.cascade_frames.size())) {
            int depth = std::min(5, step / 2 + 1);
            std::string effect = "gems_match_0" + std::to_string(depth);
            const std::array<int, 96>& before = game.cascade_frames[step];
            const std::array<int, 96>& after = game.cascade_frames[step + 1];
            for (int i = 0; i < 64; ++i) {
                if (before[i] >= 16 && before[i] != after[i])
                    effect = before[i] == 48   ? "gems_hypercube"
                             : before[i] >= 32 ? "gems_star_line"
                                               : "gems_bomb";
            }
            sound_play(effect, sound_);
        }
        cascade_step_ = step;
    }
    if (animation_duration_ > 0) {
        if (elapsed() >= animation_duration_) {
            animation_duration_ = 0;
            if (game.state.over)
                panel(2);
        } else
            moving = true;
    }
    if (game.kind == PuzzleKind::cube) {
        double dx = target_yaw_ - yaw_, dy = target_pitch_ - pitch_;
        if (std::abs(dx) + std::abs(dy) > .0005) {
            yaw_ += dx * .23;
            pitch_ += dy * .23;
            moving = true;
        }
    }
    if (game.kind == PuzzleKind::gems && !reduced_ && !panel_)
        moving = true;
    render();
    invalidate(board_);
    if (!moving && timer_)
        (*timer_).stop();
}
void PuzzleView::on_pointer(gf::PointerEvent& e) {
    const gf::Point local_position = point_from_window(e.position);
    if (panel_)
        return;
    int cell = hit(local_position);
    if (e.action == gf::PointerAction::move) {
        if (game.kind == PuzzleKind::sticks) {
            int next = -1;
            for (int i = 0; i < 21; ++i)
                if (in_rect(clues_[i], local_position))
                    next = 100 + i;
            if (next != hover_) {
                hover_ = next;
                invalidate(gf::Dirty::paint);
            }
        }
        if (game.kind == PuzzleKind::solve) {
            int next = -1;
            if (in_rect(board_, local_position))
                next = static_cast<int>((local_position.y - board_.y) * 4 / board_.height) * 4 +
                       static_cast<int>((local_position.x - board_.x) * 4 / board_.width);
            if (next != hover_) {
                hover_ = next;
                invalidate(board_);
            }
        }

        if (game.kind == PuzzleKind::gems) {
            if (hover_ != cell) {
                hover_ = cell;
                if (reduced_) {
                    render();
                    invalidate(board_);
                }
            }
        }
        if (game.kind == PuzzleKind::untangle && drag_ >= 0) {
            drag_position_ = {
                std::clamp((local_position.x - board_.x) / board_.width, .035, .965),
                std::clamp((local_position.y - board_.y) / board_.height, .035, .965)};
            invalidate(board_);
        }
        if (game.kind == PuzzleKind::cube) {
            if (!tracing_) {
                gf::Rect bounds = client_rectangle();
                target_yaw_ =
                    std::clamp(.75 + (local_position.x / bounds.width - .5) * .8, .40, 1.10);
                target_pitch_ =
                    std::clamp(-.56 + (local_position.y / bounds.height - .5) * .55, -.78, -.34);
                if (timer_)
                    (*timer_).start();
            }
            if (tracing_ && cell >= 0 && game.cube_extend(cell))
                changed("nature_cube_trace");
            hover_ = cell;
            last_pointer_ = local_position;
            render();
            invalidate(board_);
        }
        return;
    }
    if (e.action == gf::PointerAction::down) {
        if (attached_window())
            static_cast<void>((*attached_window()).request_focus(shared_from_this()));
        if (game.state.over)
            return;
        if (game.kind == PuzzleKind::cube) {
            last_pointer_ = local_position;
            orbit_ = false;
            if (e.button != gf::PointerButton::primary)
                return;
            if (cell >= 0) {
                bool started = false;
                for (int pair = 0; pair < 6; ++pair)
                    if (!game.state.paths[pair].empty() && game.state.paths[pair].back() == cell &&
                        !game.cube_pair(cell)) {
                        game.state.stage = pair + 1;
                        started = true;
                    }
                if (!started)
                    started = game.cube_start(cell);
                if (started) {
                    tracing_ = true;
                    set_pointer_capture(true);
                    target_yaw_ = yaw_;
                    target_pitch_ = pitch_;
                    changed("nature_cube_connect");
                }
            }
            return;
        }
        if (animation_duration_ > 0)
            return;
        if (game.kind == PuzzleKind::gems && cell >= 0) {
            drag_ = cell;
            set_pointer_capture(true);
        } else if (game.kind == PuzzleKind::untangle && cell >= 0) {
            drag_ = cell;
            drag_position_ = game.state.nodes[cell];
            set_pointer_capture(true);
            sound_play("untangle_grab", sound_);
        } else if (game.kind == PuzzleKind::pegs) {
            if (cell >= 0 && cell < 4)
                selection_ = cell;
            if (cell >= 4 && cell < 10 && !game.state.over) {
                guess_[selection_] = cell - 3;
                selection_ = (selection_ + 1) % 4;
                sound_play("four_pegs_place", sound_);
                persist();
            }
        } else if (game.kind == PuzzleKind::atom && cell >= 0) {
            if (cell < 16 && game.mark_atom(cell))
                changed("atom_probe_mark");
            else if (cell >= 16 && game.probe(cell - 16))
                changed("atom_probe_fire");
        } else if (game.kind == PuzzleKind::solve) {
            if (cell >= 0) {
                selection_ = cell;
                rotation_ = 0;
                flip_ = false;
                sound_play("puzzle_solve_pickup", sound_);
                drag_ = cell;
                set_pointer_capture(true);
            } else if (in_rect(board_, local_position)) {
                int x = static_cast<int>((local_position.x - board_.x) * 4 / board_.width),
                    y = static_cast<int>((local_position.y - board_.y) * 4 / board_.height);
                if (e.button == gf::PointerButton::secondary) {
                    double fx = (local_position.x - board_.x) * 4 / board_.width - x,
                           fy = (local_position.y - board_.y) * 4 / board_.height - y;
                    int wedge = fy < fx ? (fy < 1 - fx ? 0 : 1) : (fy < 1 - fx ? 3 : 2);
                    int piece = game.state.grid[(y * 4 + x) * 4 + wedge] / 4 - 1;
                    if (game.remove_piece(piece)) {
                        selection_ = piece;
                        changed("puzzle_solve_pickup");
                    }
                } else if (game.place_piece(selection_, x, y, rotation_, flip_))
                    changed("puzzle_solve_place");
                else
                    sound_play("puzzle_solve_no_fit", sound_);
            }
        } else if (game.kind == PuzzleKind::sticks) {
            if (cell >= 0 &&
                game.set_stick(cell, e.button == gf::PointerButton::secondary ? 0 : palette_))
                changed(palette_ == 1 ? "sticks_stones_stone_place" : "sticks_stones_stick_place");
            if (cell >= 37 && cell < 44)
                palette_ = cell - 36;
        }
        invalidate(gf::Dirty::paint);
        e.handled = true;
    }
    if (e.action == gf::PointerAction::up) {
        set_pointer_capture(false);
        if (game.kind == PuzzleKind::solve && drag_ >= 0) {
            drag_ = -1;
            if (in_rect(board_, local_position)) {
                int x = static_cast<int>((local_position.x - board_.x) * 4 / board_.width),
                    y = static_cast<int>((local_position.y - board_.y) * 4 / board_.height);
                if (game.place_piece(selection_, x, y, rotation_, flip_))
                    changed("puzzle_solve_place");
                else
                    sound_play("puzzle_solve_no_fit", sound_);
            }
        }
        if (game.kind == PuzzleKind::cube) {
            tracing_ = false;
            orbit_ = false;
            persist();
        }
        if (game.kind == PuzzleKind::untangle && drag_ >= 0) {
            game.move_node(drag_, drag_position_);
            drag_ = -1;
            changed("untangle_release");
        }
        if (game.kind == PuzzleKind::gems && drag_ >= 0) {
            int from = drag_;
            drag_ = -1;
            if (cell >= 0 && std::abs(from / 8 - cell / 8) + std::abs(from % 8 - cell % 8) == 1) {
                before_swap_ = game.state.grid;
                cascade_step_ = -1;
                swap_a_ = from;
                swap_b_ = cell;
                invalid_swap_ = !game.gem_swap(from, cell);
                animation_start_ = gf::FrameClock::now();
                animation_duration_ = reduced_ ? 0
                                      : invalid_swap_
                                          ? .46
                                          : .23 + .24 * (game.cascade_frames.size() - 1);
                if (timer_)
                    (*timer_).start();
                changed(invalid_swap_ ? "gems_swap_return" : "gems_swap");
            }
        }
        e.handled = true;
    }
}
void PuzzleView::on_key_bubble(gf::KeyEvent& e) {
    on_key(e);
}
void PuzzleView::on_key(gf::KeyEvent& e) {
    if (e.handled || e.action != gf::KeyAction::down)
        return;
    if (e.physical_key == gf::PhysicalKey::escape) {
        panel(0);
        e.handled = true;
        return;
    }
    if (panel_)
        return;
    if (e.physical_key == gf::PhysicalKey::r && game.kind == PuzzleKind::solve)
        rotation_ = (rotation_ + 1) % 4;
    else if (e.physical_key == gf::PhysicalKey::f && game.kind == PuzzleKind::solve)
        flip_ = !flip_;
    else if (e.physical_key == gf::PhysicalKey::left && game.kind == PuzzleKind::pegs)
        selection_ = (selection_ + 3) % 4;
    else if (e.physical_key == gf::PhysicalKey::right && game.kind == PuzzleKind::pegs)
        selection_ = (selection_ + 1) % 4;
    else if (e.physical_key == gf::PhysicalKey::enter &&
             (game.kind == PuzzleKind::pegs || game.kind == PuzzleKind::atom))
        action(*buttons_[3]);
    else
        return;
    invalidate(gf::Dirty::paint);
    e.handled = true;
}
void PuzzleView::on_text_input(gf::TextInputEvent& e) {
    if (panel_ || game.state.over || animation_duration_ > 0 || e.text_utf8.size() != 1)
        return;
    char c = e.text_utf8[0];
    int value = c >= '1' && c <= '7'   ? c - '0'
                : c >= 'a' && c <= 'f' ? c - 'a' + 1
                : c >= 'A' && c <= 'F' ? c - 'A' + 1
                                       : 0;
    if (game.kind == PuzzleKind::pegs && value >= 1 && value <= 6) {
        guess_[selection_] = value;
        selection_ = (selection_ + 1) % 4;
        sound_play("four_pegs_place", sound_);
        persist();
    } else if (game.kind == PuzzleKind::sticks && value >= 1 && value <= 7)
        palette_ = value;
    else
        return;
    e.handled = true;
    invalidate(gf::Dirty::paint);
}
} // namespace games
