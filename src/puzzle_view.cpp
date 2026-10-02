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
        // New game, Help and Top scores run from the PlaySuite capsule.
        (*buttons_[i])
            .set_visible(
                (i == 3 && (game.kind == PuzzleKind::pegs || game.kind == PuzzleKind::atom)) ||
                false); // Rotate and Flip live in the capsule; R, F and right click work while
                        // dragging.
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
        raster_.load_environment(asset_directory() + "/nature-lake.gpix");
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
    double reserve = game.kind == PuzzleKind::solve                                   ? 496.0
                     : game.kind == PuzzleKind::pegs || game.kind == PuzzleKind::atom ? 424.0
                                                                                      : 300.0;
    double side = std::max(140.0, std::min({548.0, b.height - 116, b.width - reserve}));
    board_ = {std::max(248.0, (b.width - side) * .5), 86, side, side};
    set_child_layout(buttons_[3],
                     {board_.x + board_.width + 37, board_.y + board_.height - 45, 128, 35});
    set_child_layout(buttons_[4],
                     {board_.x + board_.width + 26, board_.y + board_.height - 88, 87, 34});
    set_child_layout(buttons_[5],
                     {board_.x + board_.width + 122, board_.y + board_.height - 88, 77, 34});
    if (game.kind == PuzzleKind::solve)
        layout_solve(b);
    if (game.kind == PuzzleKind::gems || game.kind == PuzzleKind::untangle) {
        // Centered when narrow; with room, a legend card sits to the left.
        const bool wide = b.width >= 900;
        const double gside =
            std::max(160.0, std::min({600.0, b.height - 104, b.width - (wide ? 560.0 : 48.0)}));
        board_ = {wide ? std::max(264.0, (b.width - gside) * .5) : (b.width - gside) * .5,
                  std::max(66.0, 62 + (b.height - 62 - 30 - gside) * .5), gside, gside};
    }
    if (game.kind == PuzzleKind::pegs) {
        board_ = {24, 82, b.width - 332, b.height - 139};
        set_child_layout(buttons_[3],
                         {board_.x + board_.width - 151, board_.y + board_.height - 57, 124, 36});
        (*buttons_[3]).set_text("Check code");
    }
    popup_ = {std::max(8.0, b.width * .5 - 320), 60, std::min(640.0, b.width - 16),
              std::min(530.0, b.height - 70)};
    set_child_layout(buttons_[6], {popup_.x + popup_.width - 110, popup_.y + 17, 85, 32});
    set_child_layout(name_, {popup_.x + 28, popup_.y + 110, 270, 34});
    set_child_layout(buttons_[7], {popup_.x + 310, popup_.y + 110, 110, 34});
    set_child_layout(buttons_[8],
                     {popup_.x + popup_.width - 150, popup_.y + popup_.height - 45, 122, 32});
    if (game.kind == PuzzleKind::gems || game.kind == PuzzleKind::cube) {
        fit_raster();
        render();
    }
}
// Renders at device resolution (at least 1.5x) so the image stays crisp on high-DPI screens.
// While the cube is tilting it renders lighter, then sharpens once it settles.
void PuzzleView::fit_raster() {
    const double device = attached_window() ? (*attached_window()).scale() : 1.0;
    double density = std::min(std::max(1.5, device), 1400.0 / std::max(1.0, board_.width));
    if (coarse_)
        density = std::min(density, 1.1);
    const int w = std::max(2, static_cast<int>(board_.width * density)),
              h = std::max(2, static_cast<int>(board_.height * density));
    if (raster_.width != w || raster_.height != h)
        raster_.resize(w, h);
}
void PuzzleView::text(gf::Painter& p, double x, double y, const std::string& s, double size,
                      gf::Color c) {
    p.draw_text_utf8(
        {x, y}, s,
        {gf::FontRole::content, size, static_cast<std::uint16_t>(size >= 18 ? 600 : 400), false},
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
                // Ease out with a little overshoot, so gems settle like they have weight.
                const double c1 = 1.4, c3 = c1 + 1, u = progress - 1;
                double ease = 1 + c3 * u * u * u + c1 * u * u;
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
    // In-place updates must keep the registered dimensions; a resized raster replaces the image.
    gf::Window& window = *attached_window();
    gf::ImageLoadResult result =
        !image_.value ? window.load_bgra32_premultiplied(raster_.width, raster_.height,
                                                         raster_.width * 4, raster_.pixels)
        : image_size_ ==
                gf::Size{static_cast<double>(raster_.width), static_cast<double>(raster_.height)}
            ? window.update_bgra32_premultiplied(image_, raster_.width, raster_.height,
                                                 raster_.width * 4, raster_.pixels, *this)
            : window.replace_bgra32_premultiplied(image_, raster_.width, raster_.height,
                                                  raster_.width * 4, raster_.pixels, *this);
    if (result) {
        image_ = result.image;
        image_size_ = {static_cast<double>(raster_.width), static_cast<double>(raster_.height)};
    }
}
namespace {
gf::Color gem_color(int value) {
    static const gf::Color colors[] = {
        gf::Color::rgba(220, 230, 240), gf::Color::rgba(228, 49, 86),
        gf::Color::rgba(59, 181, 255),  gf::Color::rgba(255, 182, 48),
        gf::Color::rgba(126, 224, 111), gf::Color::rgba(168, 101, 249),
        gf::Color::rgba(53, 220, 208),  gf::Color::rgba(244, 119, 44),
        gf::Color::rgba(233, 127, 203)};
    return colors[std::clamp(value % 16, 0, 8)];
}
double unit_random(std::uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return (state >> 8) / 16777216.0;
}
} // namespace
// A cleared gem bursts into shards; a bomb sends out a shockwave, a star fires beams along
// its row and column, a hypercube throws lightning to everything it clears. Deep cascades
// earn a call-out. Effects live in cell units so they survive a resize.
void PuzzleView::spawn_effects(const std::array<int, 96>& before, const std::array<int, 96>& after,
                               int depth) {
    if (reduced_)
        return;
    std::vector<int> cleared;
    for (int i = 0; i < 64; ++i)
        if (before[i] && before[i] != after[i])
            cleared.push_back(i);
    for (int i : cleared) {
        const double cx = i % 8 + .5, cy = i / 8 + .5;
        const gf::Color c = gem_color(before[i]);
        effects_.push_back({GemEffect::ring, cx, cy, 0, 0, 0, .45, .55, c});
        for (int k = 0; k < 9; ++k) {
            const double a = unit_random(fx_seed_) * 6.2831853,
                         speed = 2.2 + unit_random(fx_seed_) * 3.2;
            effects_.push_back({GemEffect::shard, cx, cy, std::cos(a) * speed,
                                std::sin(a) * speed - 1.6, unit_random(fx_seed_) * 6.28,
                                .55 + unit_random(fx_seed_) * .35, .1 + unit_random(fx_seed_) * .08,
                                c});
        }
        if (before[i] >= 16 && before[i] < 32) {
            effects_.push_back({GemEffect::shock, cx, cy, 0, 0, 0, .6, 1.8, c});
            shake_ = std::max(shake_, .5);
        } else if (before[i] >= 32 && before[i] < 48) {
            effects_.push_back({GemEffect::beam_row, cx, cy, 0, 0, 0, .55, .5, c});
            effects_.push_back({GemEffect::beam_column, cx, cy, 0, 0, 0, .55, .5, c});
        } else if (before[i] == 48) {
            shake_ = std::max(shake_, .8);
            for (int j : cleared)
                if (j != i)
                    effects_.push_back({GemEffect::bolt, cx, cy, j % 8 + .5 - cx, j / 8 + .5 - cy,
                                        unit_random(fx_seed_) * 100, .5, 0, gem_color(before[j])});
        }
    }
    if (depth >= 2 && !cleared.empty()) {
        static const char* words[] = {"", "", "Nice!", "Great!", "Superb!", "Dazzling!"};
        callout_ = words[std::min(depth, 5)];
        callout_life_ = 1.1;
    }
}
void PuzzleView::step_effects() {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const double dt = std::clamp(std::chrono::duration<double>(now - fx_clock_).count(), 0.0, .05);
    fx_clock_ = now;
    for (GemEffect& e : effects_) {
        e.age += dt;
        if (e.kind == GemEffect::shard) {
            e.vy += 9.5 * dt;
            e.x += e.vx * dt;
            e.y += e.vy * dt;
            e.spin += dt * 9;
        }
    }
    struct ExpiredEffect {
        bool operator()(const GemEffect& e) const {
            return e.age >= e.life;
        }
    };
    std::erase_if(effects_, ExpiredEffect{});
    shake_ = std::max(0.0, shake_ - dt * 2.6);
    callout_life_ = std::max(0.0, callout_life_ - dt);
}
void PuzzleView::paint_gems(gf::Painter& p) {
    const gf::Rect b = client_rectangle();
    // Velvet workspace under a jeweller's lamp.
    p.fill_rect(b, gf::Color::rgba(26, 16, 44));
    const gf::GradientStop lamp[] = {{0, gf::Color::rgba(86, 52, 128)},
                                     {1, gf::Color::rgba(22, 12, 38)}};
    p.fill_radial_gradient(b, {board_.x + board_.width * .5, board_.y + board_.height * .45},
                           {b.width * .75, b.height * .85}, lamp);
    const double cell = board_.width / 8;
    const double t = std::chrono::duration<double>(gf::FrameClock::now() - clock_start_).count();
    const gf::Point shake{shake_ > 0 ? std::sin(t * 61) * shake_ * cell * .08 : 0,
                          shake_ > 0 ? std::cos(t * 47) * shake_ * cell * .08 : 0};
    p.save();
    p.translate(shake);
    const gf::Rect tray{board_.x - 10, board_.y - 10, board_.width + 20, board_.height + 20};
    p.draw_box_shadow(tray, 18, {0, 10}, 28, 0, gf::Color::rgba(0, 0, 0, 150));
    const gf::GradientStop rim[] = {{0, gf::Color::rgba(250, 214, 140)},
                                    {.5, gf::Color::rgba(178, 126, 52)},
                                    {1, gf::Color::rgba(120, 78, 26)}};
    p.save();
    p.clip_rounded_rect(tray, 18);
    p.fill_linear_gradient(tray, {tray.x, tray.y}, {tray.x + tray.width, tray.y + tray.height},
                           rim);
    p.restore();
    p.fill_rounded_rect(board_, 12, gf::Color::rgba(14, 10, 30));
    p.draw_inset_box_shadow(board_, 12, {0, 4}, 14, 0, gf::Color::rgba(0, 0, 0, 160));
    for (int i = 0; i < 64; ++i)
        if ((i / 8 + i % 8) % 2 == 0)
            p.fill_rounded_rect(
                {board_.x + (i % 8) * cell + 2, board_.y + (i / 8) * cell + 2, cell - 4, cell - 4},
                cell * .16, gf::Color::rgba(44, 30, 74, 150));
    if (hover_ >= 0 && hover_ < 64 && animation_duration_ <= 0)
        p.fill_rounded_rect({board_.x + (hover_ % 8) * cell + 2, board_.y + (hover_ / 8) * cell + 2,
                             cell - 4, cell - 4},
                            cell * .16, gf::Color::rgba(255, 220, 150, 40));
    if (image_.value)
        p.draw_image(image_, board_);
    // Effects over the gems.
    struct GemPosition {
        gf::Rect board_;
        double cell;
        gf::Point operator()(double x, double y) const {
            return gf::Point{board_.x + x * cell, board_.y + y * cell};
        }
    };
    GemPosition at{board_, cell};
    for (const GemEffect& e : effects_) {
        const double k = std::clamp(e.age / e.life, 0.0, 1.0), fade = 1 - k;
        const gf::Color c = e.color;
        switch (e.kind) {
        case GemEffect::shard: {
            const gf::Point q = at(e.x, e.y);
            const double r = e.size * cell * (1 - k * .5);
            std::vector<gf::Point> tri;
            for (int v = 0; v < 3; ++v)
                tri.push_back(
                    {q.x + std::cos(e.spin + v * 2.1) * r, q.y + std::sin(e.spin + v * 2.1) * r});
            paint_polygon(
                p, tri,
                gf::Color::rgba(c.red, c.green, c.blue, static_cast<unsigned char>(240 * fade)));
            break;
        }
        case GemEffect::ring: {
            const gf::Point q = at(e.x, e.y);
            const double r = cell * (.25 + e.size * k);
            p.stroke_rounded_rect(
                {q.x - r, q.y - r, 2 * r, 2 * r}, r,
                gf::Color::rgba(255, 255, 255, static_cast<unsigned char>(200 * fade)),
                2.5 * fade + .5);
            break;
        }
        case GemEffect::shock: {
            const gf::Point q = at(e.x, e.y);
            const double r = cell * (.3 + e.size * k);
            p.stroke_rounded_rect(
                {q.x - r, q.y - r, 2 * r, 2 * r}, r,
                gf::Color::rgba(255, 236, 190, static_cast<unsigned char>(230 * fade)),
                cell * .12 * fade + 1);
            const gf::GradientStop glow[] = {
                {0, gf::Color::rgba(255, 240, 200, static_cast<unsigned char>(160 * fade))},
                {1, gf::Color::rgba(255, 200, 120, 0)}};
            p.fill_radial_gradient({q.x - r, q.y - r, 2 * r, 2 * r}, q, {r, r}, glow);
            break;
        }
        case GemEffect::beam_row:
        case GemEffect::beam_column: {
            const bool row = e.kind == GemEffect::beam_row;
            const gf::Point q = at(e.x, e.y);
            const double half = cell * (.12 + .3 * fade);
            const gf::Rect beam = row ? gf::Rect{board_.x, q.y - half, board_.width, half * 2}
                                      : gf::Rect{q.x - half, board_.y, half * 2, board_.height};
            const gf::GradientStop stops[] = {
                {0, gf::Color::rgba(255, 255, 255, 0)},
                {.5, gf::Color::rgba(255, 252, 230, static_cast<unsigned char>(230 * fade))},
                {1, gf::Color::rgba(255, 255, 255, 0)}};
            p.fill_linear_gradient(beam,
                                   row ? gf::Point{beam.x, beam.y} : gf::Point{beam.x, beam.y},
                                   row ? gf::Point{beam.x, beam.y + beam.height}
                                       : gf::Point{beam.x + beam.width, beam.y},
                                   stops);
            break;
        }
        case GemEffect::bolt: {
            // A jagged bolt from the hypercube to a cleared gem, re-jittered each frame.
            std::uint32_t seed =
                static_cast<std::uint32_t>(e.spin) + static_cast<std::uint32_t>(t * 20);
            gf::Point prev = at(e.x, e.y);
            for (int seg = 1; seg <= 6; ++seg) {
                const double f = seg / 6.0;
                gf::Point next = at(e.x + e.vx * f, e.y + e.vy * f);
                if (seg < 6) {
                    next.x += (unit_random(seed) - .5) * cell * .45;
                    next.y += (unit_random(seed) - .5) * cell * .45;
                }
                p.draw_line(
                    prev, next,
                    gf::Color::rgba(c.red, c.green, c.blue, static_cast<unsigned char>(200 * fade)),
                    4);
                p.draw_line(prev, next,
                            gf::Color::rgba(255, 255, 255, static_cast<unsigned char>(240 * fade)),
                            1.6);
                prev = next;
            }
            break;
        }
        }
    }
    p.restore();
    if (callout_life_ > 0 && !callout_.empty()) {
        const double k = 1 - callout_life_ / 1.1;
        const gf::FontSpec f{gf::FontRole::content,
                             std::min(44.0, cell * .8) *
                                 (1 + .15 * std::sin(std::min(k * 5, 3.14159))),
                             800, false, 1};
        const gf::Size m = p.measure_text_utf8(callout_, f);
        const double x = board_.x + (board_.width - m.width) * .5,
                     y = board_.y + board_.height * (.48 - k * .12);
        const unsigned char alpha =
            static_cast<unsigned char>(255 * std::clamp(callout_life_ * 2.5, 0.0, 1.0));
        for (const gf::Point o : {gf::Point{-2, 0}, gf::Point{2, 0}, gf::Point{0, -2},
                                  gf::Point{0, 2}, gf::Point{2, 3}})
            p.draw_text_utf8({x + o.x, y + o.y}, callout_, f, gf::Color::rgba(60, 20, 70, alpha));
        p.draw_text_utf8({x, y}, callout_, f, gf::Color::rgba(255, 226, 140, alpha));
    }
    // Legend card (left), shown when there is room.
    if (board_.x > 230) {
        const gf::Rect card{std::max(14.0, board_.x - 250), board_.y, 214, 300};
        p.draw_box_shadow(card, 8, {0, 4}, 12, 0, gf::Color::rgba(0, 0, 0, 110));
        fill_vertical(p, card, gf::Color::rgba(58, 36, 92, 235), gf::Color::rgba(36, 22, 60, 235));
        p.stroke_rounded_rect(card, 8, gf::Color::rgba(255, 210, 122, 90), 1);
        const gf::FontSpec caps{gf::FontRole::content, 11, 700, false, 1.2};
        p.draw_text_utf8({card.x + 14, card.y + 24}, "HOW GEMS WORK", caps,
                         gf::Color::rgba(255, 222, 150));
        const gf::FontSpec body{gf::FontRole::content, 13, 400, false};
        const gf::Color ink2 = gf::Color::rgba(236, 226, 248),
                        soft = gf::Color::rgba(190, 176, 214);
        p.draw_text_utf8({card.x + 14, card.y + 52}, "Drag a gem onto a neighbor.", body, ink2);
        p.draw_text_utf8({card.x + 14, card.y + 72}, "Line up three or more.", body, ink2);
        const char* rows[][2] = {{"Four in a row", "makes a bomb"},
                                 {"T or L shape", "makes a star"},
                                 {"Five in a row", "makes a hypercube"}};
        for (int i = 0; i < 3; ++i) {
            p.draw_text_utf8({card.x + 14, card.y + 110 + i * 42}, rows[i][0],
                             {gf::FontRole::content, 13, 700, false}, ink2);
            p.draw_text_utf8({card.x + 14, card.y + 128 + i * 42}, rows[i][1], body, soft);
        }
        p.draw_text_utf8({card.x + 14, card.y + 250}, "Bombs and stars go off when", body, soft);
        p.draw_text_utf8({card.x + 14, card.y + 268}, "matched with their own color.", body, soft);
        p.draw_text_utf8({card.x + 14, card.y + 290},
                         std::to_string(game.gem_colors()) + " colors in play",
                         {gf::FontRole::content, 13, 700, false}, gf::Color::rgba(255, 222, 150));
    }
}
double PuzzleView::untangle_radius() const {
    return std::clamp(board_.width * .026, 11.0, 17.0);
}
namespace {
double turn(Point2 a, Point2 b, Point2 c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
bool segments_cross(Point2 p, Point2 q, Point2 r, Point2 s) {
    return turn(p, q, r) * turn(p, q, s) < 0 && turn(r, s, p) * turn(r, s, q) < 0;
}
} // namespace
// Untangle: a night-sky slate where crossing threads glow coral and clear ones aqua.
// When the last crossing clears, the threads turn gold and a ripple runs outward.
void PuzzleView::paint_untangle(gf::Painter& p) {
    const gf::Rect b = client_rectangle();
    p.fill_rect(b, gf::Color::rgba(10, 26, 36));
    const gf::GradientStop sky[] = {{0, gf::Color::rgba(28, 74, 92)},
                                    {1, gf::Color::rgba(8, 20, 30)}};
    p.fill_radial_gradient(b, {board_.x + board_.width * .5, board_.y + board_.height * .4},
                           {b.width * .7, b.height * .85}, sky);
    const gf::Rect mat{board_.x - 14, board_.y - 14, board_.width + 28, board_.height + 28};
    p.draw_box_shadow(mat, 16, {0, 8}, 26, 0, gf::Color::rgba(0, 0, 0, 140));
    fill_vertical(p, mat, gf::Color::rgba(22, 46, 60), gf::Color::rgba(12, 28, 40));
    p.stroke_rounded_rect(mat, 16, gf::Color::rgba(120, 220, 230, 60), 1);
    for (int y = 1; y < 12; ++y)
        for (int x = 1; x < 12; ++x)
            p.fill_rect({board_.x + board_.width * x / 12 - .75,
                         board_.y + board_.height * y / 12 - .75, 1.5, 1.5},
                        gf::Color::rgba(140, 210, 220, 50));
    std::vector<Point2> positions = game.state.nodes;
    if (drag_ >= 0)
        positions[drag_] = drag_position_;
    struct BoardPosition {
        gf::Rect board_;
        gf::Point operator()(Point2 q) const {
            return gf::Point{board_.x + q.x * board_.width, board_.y + q.y * board_.height};
        }
    };
    BoardPosition at{board_};
    std::vector<bool> crossed(game.state.edges.size(), false);
    int crossings = 0;
    for (std::size_t i = 0; i < game.state.edges.size(); ++i)
        for (std::size_t j = i + 1; j < game.state.edges.size(); ++j) {
            const Edge e = game.state.edges[i], f = game.state.edges[j];
            if (e.a == f.a || e.a == f.b || e.b == f.a || e.b == f.b)
                continue;
            if (segments_cross(positions[e.a], positions[e.b], positions[f.a], positions[f.b])) {
                crossed[i] = crossed[j] = true;
                ++crossings;
            }
        }
    const double celebrate = game.state.won && animation_duration_ > 0
                                 ? std::clamp(elapsed() / animation_duration_, 0.0, 1.0)
                                 : -1;
    const gf::Point center = at({.5, .5});
    for (std::size_t i = 0; i < game.state.edges.size(); ++i) {
        const Edge e = game.state.edges[i];
        const gf::Point a = at(positions[e.a]), c = at(positions[e.b]);
        gf::Color color =
            crossed[i] ? gf::Color::rgba(240, 132, 120, 205) : gf::Color::rgba(110, 222, 236, 225);
        if (game.state.won) {
            // Gold spreads outward from the middle as the ripple passes.
            const double d =
                std::hypot((a.x + c.x) * .5 - center.x, (a.y + c.y) * .5 - center.y) / board_.width;
            const double wave = celebrate < 0 ? 1 : std::clamp((celebrate * 1.6 - d) * 4, 0.0, 1.0);
            color = mix_color(gf::Color::rgba(110, 222, 236, 210),
                              gf::Color::rgba(255, 214, 110, 255), wave);
        }
        if (crossed[i])
            p.draw_line(a, c, gf::Color::rgba(255, 90, 80, 28), 6);
        else
            p.draw_line(a, c, gf::Color::rgba(110, 222, 236, 40), 6);
        p.draw_line(a, c, color, crossed[i] ? 1.8 : 2.4);
    }
    if (celebrate >= 0) {
        const double r = board_.width * (.1 + celebrate * .8);
        p.stroke_rounded_rect(
            {center.x - r, center.y - r, 2 * r, 2 * r}, r,
            gf::Color::rgba(255, 220, 140, static_cast<unsigned char>(200 * (1 - celebrate))), 3);
    }
    const double radius = untangle_radius();
    for (int i = 0; i < static_cast<int>(positions.size()); ++i) {
        const gf::Point q = at(positions[i]);
        const bool held = i == drag_, hot = i == hover_;
        const double r = radius * (held ? 1.18 : hot ? 1.08 : 1);
        const gf::Rect pearl{q.x - r, q.y - r, 2 * r, 2 * r};
        p.draw_box_shadow(pearl, r, {0, held ? 6.0 : 3.0}, held ? 14 : 8, 0,
                          gf::Color::rgba(0, 0, 0, held ? 150 : 110));
        const gf::GradientStop shine[] = {
            {0, gf::Color::rgba(255, 255, 255)},
            {.55, game.state.won ? gf::Color::rgba(255, 226, 150)
                  : held         ? gf::Color::rgba(255, 220, 150)
                                 : gf::Color::rgba(196, 238, 236)},
            {1, game.state.won ? gf::Color::rgba(196, 140, 50) : gf::Color::rgba(88, 150, 162)}};
        p.save();
        p.clip_rounded_rect(pearl, r);
        p.fill_radial_gradient(pearl, {q.x - r * .35, q.y - r * .4}, {r * 1.6, r * 1.6}, shine);
        p.restore();
        p.stroke_rounded_rect(pearl, r, gf::Color::rgba(30, 70, 84, 200), 1.2);
        const std::string label = std::to_string(i + 1);
        const gf::FontSpec f{gf::FontRole::content, std::max(9.0, r * .78), 700, false};
        const gf::Size m = p.measure_text_utf8(label, f);
        p.draw_text_utf8({q.x - m.width * .5, q.y + f.size * .36}, label, f,
                         gf::Color::rgba(24, 56, 66));
    }
    // Status plaque under the board.
    const std::string status = game.state.won
                                   ? "Untangled in " + std::to_string(game.state.moves) + " moves"
                               : crossings == 1 ? "1 crossing left"
                                                : std::to_string(crossings) + " crossings left";
    const gf::FontSpec sf{gf::FontRole::content, 14, 700, false};
    const gf::Size sm = p.measure_text_utf8(status, sf);
    p.draw_text_utf8({board_.x + (board_.width - sm.width) * .5, mat.y + mat.height + 20}, status,
                     sf,
                     game.state.won ? gf::Color::rgba(255, 220, 140)
                     : crossings    ? gf::Color::rgba(255, 160, 150)
                                    : gf::Color::rgba(150, 230, 236));
    if (board_.x > 230) {
        const gf::Rect card{std::max(14.0, board_.x - 250), board_.y, 214, 196};
        p.draw_box_shadow(card, 8, {0, 4}, 12, 0, gf::Color::rgba(0, 0, 0, 110));
        fill_vertical(p, card, gf::Color::rgba(26, 58, 72, 235), gf::Color::rgba(14, 34, 46, 235));
        p.stroke_rounded_rect(card, 8, gf::Color::rgba(120, 220, 230, 70), 1);
        const gf::FontSpec caps{gf::FontRole::content, 11, 700, false, 1.2};
        p.draw_text_utf8({card.x + 14, card.y + 24}, "UNTIE THE THREADS", caps,
                         gf::Color::rgba(150, 230, 236));
        const gf::FontSpec body{gf::FontRole::content, 13, 400, false};
        const gf::Color ink2 = gf::Color::rgba(222, 240, 244),
                        soft = gf::Color::rgba(160, 196, 204);
        p.draw_text_utf8({card.x + 14, card.y + 52}, "Drag the pearls so that", body, ink2);
        p.draw_text_utf8({card.x + 14, card.y + 70}, "no two threads cross.", body, ink2);
        p.draw_text_utf8({card.x + 14, card.y + 104}, "Coral threads are crossing.", body,
                         gf::Color::rgba(255, 160, 150));
        p.draw_text_utf8({card.x + 14, card.y + 122}, "Aqua threads are clear.", body,
                         gf::Color::rgba(150, 230, 236));
        p.draw_text_utf8({card.x + 14, card.y + 156}, "Give each pearl its own", body, soft);
        p.draw_text_utf8({card.x + 14, card.y + 174}, "space; they may not touch.", body, soft);
    }
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
namespace {
const gf::Color solve_blue = gf::Color::rgba(46, 132, 222),
                solve_gold = gf::Color::rgba(248, 199, 66);
enum class PieceLook { placed, lifted, tray, used, ghost_ok, ghost_bad };
// A piece is a union of quarter-square triangles. Its outer edges get a bevel: a light
// line on edges facing up and left, a shaded line on edges facing down and right.
void draw_piece(gf::Painter& p, const std::vector<PieceCell>& shape, double x, double y,
                double unit, PieceLook look) {
    using Vertex = std::pair<int, int>;
    struct Edge {
        Vertex a, b, inside;
        int count = 0;
    };
    std::map<std::pair<Vertex, Vertex>, Edge> edges;
    struct PiecePosition {
        double x, y, unit;
        gf::Point operator()(Vertex v) const {
            return gf::Point{x + v.first * unit * .5, y + v.second * unit * .5};
        }
    };
    PiecePosition at{x, y, unit};
    const bool ghost = look == PieceLook::ghost_ok || look == PieceLook::ghost_bad;
    if (look == PieceLook::placed || look == PieceLook::lifted) {
        const double drop = look == PieceLook::lifted ? unit * .09 : unit * .03;
        for (const PieceCell& c : shape) {
            std::vector<gf::Point> pts;
            for (Vertex v : atom_vertices(c))
                pts.push_back({at(v).x + drop * .6, at(v).y + drop});
            paint_polygon(p, pts, gf::Color::rgba(0, 0, 0, look == PieceLook::lifted ? 55 : 40),
                          false);
        }
    }
    for (const PieceCell& c : shape) {
        std::array<std::pair<int, int>, 3> v = atom_vertices(c);
        std::vector<gf::Point> pts;
        for (Vertex q : v)
            pts.push_back(at(q));
        gf::Color fill = c.color == 1 ? solve_blue : solve_gold;
        if (look == PieceLook::used)
            fill = gf::Color::rgba(fill.red, fill.green, fill.blue, 50);
        if (look == PieceLook::ghost_ok)
            fill = gf::Color::rgba(fill.red, fill.green, fill.blue, 120);
        if (look == PieceLook::ghost_bad)
            fill = gf::Color::rgba(214, 70, 74, 90);
        paint_polygon(p, pts, fill, false);
        for (int j = 0; j < 3; ++j) {
            Vertex a = v[j], b = v[(j + 1) % 3], inside = v[(j + 2) % 3];
            std::pair<Vertex, Vertex> key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
            Edge& e = edges[key];
            e.a = a;
            e.b = b;
            e.inside = inside;
            ++e.count;
        }
    }
    for (const std::pair<const std::pair<Vertex, Vertex>, Edge>& entry : edges) {
        const Edge& e = entry.second;
        if (e.count != 1)
            continue;
        const gf::Point a = at(e.a), b = at(e.b), in = at(e.inside);
        if (ghost) {
            p.draw_line(a, b,
                        look == PieceLook::ghost_ok ? gf::Color::rgba(255, 255, 255, 230)
                                                    : gf::Color::rgba(255, 210, 210, 230),
                        2);
            continue;
        }
        p.draw_line(a, b,
                    look == PieceLook::used ? gf::Color::rgba(160, 180, 205, 90)
                                            : gf::Color::rgba(26, 44, 72, 170),
                    1.1);
        if (look == PieceLook::used)
            continue;
        // Outward normal decides whether this edge catches the light.
        double nx = b.y - a.y, ny = -(b.x - a.x);
        const double length = std::hypot(nx, ny);
        nx /= length;
        ny /= length;
        if ((in.x - a.x) * nx + (in.y - a.y) * ny > 0) {
            nx = -nx;
            ny = -ny;
        }
        const double inset = std::max(1.2, unit * .035);
        const gf::Point ia{a.x - nx * inset, a.y - ny * inset},
            ib{b.x - nx * inset, b.y - ny * inset};
        const bool lit = nx * -.7 + ny * -.7 > .1;
        if (lit)
            p.draw_line(ia, ib, gf::Color::rgba(255, 255, 255, 105), 1.0);
    }
}
void sparkle(gf::Painter& p, gf::Point c, double r, gf::Color color) {
    paint_polygon(p,
                  {{c.x, c.y - r},
                   {c.x + r * .22, c.y - r * .22},
                   {c.x + r, c.y},
                   {c.x + r * .22, c.y + r * .22},
                   {c.x, c.y + r},
                   {c.x - r * .22, c.y + r * .22},
                   {c.x - r, c.y},
                   {c.x - r * .22, c.y - r * .22}},
                  color);
}
} // namespace
// Puzzle Solve: target picture on the left (or above the tray when narrow), the frame,
// and a tray of the seven pieces. Everything scales with the window.
void PuzzleView::layout_solve(gf::Rect b) {
    const double top = 62, bottom = b.height - 26, gap = std::clamp(b.width * .02, 10.0, 22.0);
    const double avail = std::max(120.0, bottom - top);
    const bool wide = b.width >= 820;
    const double column =
        wide ? std::clamp(b.width * .19, 150.0, 230.0) : std::clamp(b.width * .34, 150.0, 240.0);
    const double frame = 12;
    double side = wide ? std::min(avail - 2 * frame, b.width - 2 * column - 4 * gap - 2 * frame)
                       : std::min(avail - 2 * frame, b.width - column - 3 * gap - 2 * frame);
    side = std::max(120.0, std::min(side, 620.0));
    if (wide) {
        const double total = column * 2 + side + 2 * frame + gap * 2;
        const double x0 = (b.width - total) * .5;
        target_ = {x0, top, column, std::min(avail, column * 1.32)};
        board_ = {x0 + column + gap + frame, top + (avail - side) * .5, side, side};
        tray_ = {board_.x + side + frame + gap, top, column, avail};
    } else {
        board_ = {gap + frame, top + (avail - side) * .5, side, side};
        const double x = board_.x + side + frame + gap, w = b.width - x - gap;
        target_ = {x, top, w, std::min(avail * .42, w * 1.05)};
        tray_ = {x, target_.y + target_.height + 8, w, bottom - target_.y - target_.height - 8};
    }
    // Tray slots: two or three columns, sized to fit seven pieces.
    cells_.clear();
    const double inner_top = tray_.y + 26, inner_h = tray_.height - 26 - 22;
    const int cols = tray_.width >= 210 && inner_h < tray_.width * 1.6 ? 3 : 2;
    const int rows = (7 + cols - 1) / cols;
    const double slot = std::min((tray_.width - 16) / cols, inner_h / rows);
    const double ox = tray_.x + (tray_.width - slot * cols) * .5;
    for (int piece = 0; piece < 7; ++piece)
        cells_.push_back({ox + (piece % cols) * slot + 3, inner_top + (piece / cols) * slot + 3,
                          slot - 6, slot - 6});
}
void PuzzleView::paint_solve(gf::Painter& p) {
    const gf::Rect b = client_rectangle();
    const double unit = board_.width / 4;
    // Workspace: deep navy blotter under a soft lamp.
    p.fill_rect(b, gf::Color::rgba(18, 26, 44));
    const gf::GradientStop lamp[] = {{0, gf::Color::rgba(64, 92, 140, 255)},
                                     {1, gf::Color::rgba(18, 26, 44, 255)}};
    p.fill_radial_gradient(b, {board_.x + board_.width * .5, board_.y + board_.height * .4},
                           {b.width * .7, b.height * .8}, lamp);
    // Frame: gilded wood around an ivory card with a faint construction grid.
    const double celebrate = game.state.won && animation_duration_ > 0
                                 ? std::clamp(elapsed() / animation_duration_, 0.0, 1.0)
                                 : -1;
    const gf::Rect outer{board_.x - 12, board_.y - 12, board_.width + 24, board_.height + 24};
    p.draw_box_shadow(outer, 6, {0, 8}, 22, 0, gf::Color::rgba(0, 0, 0, 140));
    if (celebrate >= 0 || game.state.won)
        p.draw_box_shadow(outer, 8, {0, 0}, 26, 4,
                          gf::Color::rgba(255, 214, 120,
                                          static_cast<unsigned char>(
                                              game.state.won && celebrate < 0
                                                  ? 90
                                                  : 90 + 120 * std::sin(celebrate * 3.14159))));
    const gf::GradientStop wood[] = {{0, gf::Color::rgba(222, 182, 104)},
                                     {.5, gf::Color::rgba(176, 128, 58)},
                                     {1, gf::Color::rgba(132, 88, 36)}};
    p.fill_linear_gradient(outer, {outer.x, outer.y},
                           {outer.x + outer.width, outer.y + outer.height}, wood);
    p.stroke_rect(outer, gf::Color::rgba(90, 58, 22), 1);
    p.stroke_rect({outer.x + 4, outer.y + 4, outer.width - 8, outer.height - 8},
                  gf::Color::rgba(255, 232, 170, 160), 1);
    p.fill_rect({board_.x - 2, board_.y - 2, board_.width + 4, board_.height + 4},
                gf::Color::rgba(98, 66, 30));
    fill_vertical(p, board_, gf::Color::rgba(250, 245, 232), gf::Color::rgba(236, 228, 208));
    for (int i = 1; i < 4; ++i) {
        p.draw_line({board_.x + i * unit, board_.y},
                    {board_.x + i * unit, board_.y + board_.height},
                    gf::Color::rgba(196, 184, 156, 110), 1);
        p.draw_line({board_.x, board_.y + i * unit}, {board_.x + board_.width, board_.y + i * unit},
                    gf::Color::rgba(196, 184, 156, 110), 1);
    }
    for (int k = 0; k <= 8; k += 2) {
        p.draw_line({board_.x + k * unit * .5, board_.y}, {board_.x, board_.y + k * unit * .5},
                    gf::Color::rgba(196, 184, 156, 45), 1);
        p.draw_line({board_.x + board_.width - k * unit * .5, board_.y},
                    {board_.x + board_.width, board_.y + k * unit * .5},
                    gf::Color::rgba(196, 184, 156, 45), 1);
    }
    for (int piece = 0; piece < 7; ++piece) {
        if (piece == drag_)
            continue;
        std::vector<PieceCell> shape;
        for (int i = 0; i < 64; ++i)
            if (game.state.grid[i] / 4 == piece + 1)
                shape.push_back({(i / 4) % 4, i / 16, game.state.grid[i] % 4, i % 4});
        if (!shape.empty())
            draw_piece(p, shape, board_.x, board_.y, unit, PieceLook::placed);
    }
    if (drag_ >= 0 && !game.state.over && in_rect(board_, solve_pointer_)) {
        int sx = 0, sy = 0;
        const bool valid = solve_snap(sx, sy);
        std::vector<PieceCell> shape = game.piece_cells(selection_, rotation_, flip_);
        for (PieceCell& c : shape) {
            c.x += sx;
            c.y += sy;
        }
        p.save();
        p.clip_rect(board_);
        draw_piece(p, shape, board_.x, board_.y, unit,
                   valid ? PieceLook::ghost_ok : PieceLook::ghost_bad);
        p.restore();
    }
    // Target card.
    const gf::Color label = gf::Color::rgba(255, 222, 150),
                    muted_ink = gf::Color::rgba(176, 192, 220);
    const gf::FontSpec caps{gf::FontRole::content, 11, 700, false, 1.2};
    p.draw_box_shadow(target_, 6, {0, 4}, 12, 0, gf::Color::rgba(0, 0, 0, 110));
    fill_vertical(p, target_, gf::Color::rgba(38, 52, 82), gf::Color::rgba(26, 36, 60));
    p.stroke_rounded_rect(target_, 6, gf::Color::rgba(255, 210, 122, 90), 1);
    p.draw_text_utf8({target_.x + 12, target_.y + 20}, "THE PICTURE", caps, label);
    const double picture = std::min(target_.width - 24, target_.height - 62);
    if (picture > 30) {
        const gf::Rect pic{target_.x + (target_.width - picture) * .5, target_.y + 30, picture,
                           picture};
        p.fill_rect({pic.x - 3, pic.y - 3, pic.width + 6, pic.height + 6},
                    gf::Color::rgba(250, 245, 232));
        std::vector<PieceCell> target;
        for (int i = 0; i < 64; ++i)
            target.push_back({(i / 4) % 4, i / 16, game.state.secret[i], i % 4});
        for (const PieceCell& c : target) {
            std::vector<gf::Point> pts;
            for (std::pair<int, int> v : atom_vertices(c))
                pts.push_back({pic.x + v.first * picture / 8, pic.y + v.second * picture / 8});
            paint_polygon(p, pts, c.color == 1 ? solve_blue : solve_gold, false);
        }
        int placed = 0;
        for (int piece = 0; piece < 7; ++piece)
            for (int i = 0; i < 64; ++i)
                if (game.state.grid[i] / 4 == piece + 1) {
                    ++placed;
                    break;
                }
        const std::string progress =
            game.state.won ? "Solved in " + std::to_string(game.state.moves) + " moves"
                           : std::to_string(placed) + " of 7 pieces placed";
        p.draw_text_utf8({target_.x + 12, pic.y + pic.height + 22}, progress,
                         {gf::FontRole::content, 13, 600, false},
                         game.state.won ? label : muted_ink);
    }
    // Tray.
    p.draw_box_shadow(tray_, 6, {0, 4}, 12, 0, gf::Color::rgba(0, 0, 0, 110));
    fill_vertical(p, tray_, gf::Color::rgba(38, 52, 82), gf::Color::rgba(26, 36, 60));
    p.stroke_rounded_rect(tray_, 6, gf::Color::rgba(255, 210, 122, 90), 1);
    p.draw_text_utf8({tray_.x + 12, tray_.y + 18}, "PIECES", caps, label);
    for (int piece = 0; piece < 7 && piece < static_cast<int>(cells_.size()); ++piece) {
        const gf::Rect r = cells_[static_cast<std::size_t>(piece)];
        bool used = false;
        for (int i = 0; i < 64; ++i)
            used = used || game.state.grid[i] / 4 == piece + 1;
        const bool chosen = piece == selection_;
        p.fill_rounded_rect(
            r, 5, chosen ? gf::Color::rgba(62, 84, 126) : gf::Color::rgba(16, 24, 42, 170));
        p.draw_inset_box_shadow(r, 5, {0, 2}, 5, 0, gf::Color::rgba(0, 0, 0, 120));
        if (chosen)
            p.stroke_rounded_rect(r, 5, gf::Color::rgba(255, 210, 122), 1.5);
        if (piece == drag_)
            continue;
        std::vector<PieceCell> shape =
            game.piece_cells(piece, chosen ? rotation_ : piece_rotation_[piece],
                             chosen ? flip_ : piece_flip_[piece]);
        int mx = 0, my = 0;
        for (const PieceCell& c : shape) {
            mx = std::max(mx, c.x + 1);
            my = std::max(my, c.y + 1);
        }
        const double u = std::min((r.width - 14) / mx, (r.height - 14) / my);
        draw_piece(p, shape, r.x + (r.width - mx * u) * .5, r.y + (r.height - my * u) * .5, u,
                   used ? PieceLook::used : PieceLook::tray);
    }
    p.draw_text_utf8({tray_.x + 12, tray_.y + tray_.height - 8},
                     tray_.width > 200 ? "Drag to the frame · R turns · F flips"
                                       : "R turns · F flips",
                     {gf::FontRole::content, 11, 400, false}, muted_ink);
    if (drag_ >= 0) {
        // The lifted piece rides under the pointer at frame scale, above everything else.
        std::vector<PieceCell> shape = game.piece_cells(selection_, rotation_, flip_);
        draw_piece(p, shape, solve_pointer_.x - grab_.x * unit, solve_pointer_.y - grab_.y * unit,
                   unit, PieceLook::lifted);
    }
    if (celebrate >= 0) {
        // Sparkles drift around the frame while the solved picture glows.
        for (int i = 0; i < 14; ++i) {
            const double a = i * 2.39996 + celebrate * 2.2,
                         rr = outer.width * (.52 + .06 * std::sin(i * 1.7 + celebrate * 6));
            const gf::Point c{outer.x + outer.width * .5 + std::cos(a) * rr,
                              outer.y + outer.height * .5 + std::sin(a) * rr};
            const double life = std::sin(std::clamp(celebrate * 1.3 - i * .03, 0.0, 1.0) * 3.14159);
            sparkle(p, c, 4 + 9 * life,
                    gf::Color::rgba(255, 236, 170, static_cast<unsigned char>(220 * life)));
        }
        const gf::Rect banner{board_.x + board_.width * .5 - 110,
                              board_.y + board_.height * .5 - 30, 220, 60};
        p.draw_box_shadow(banner, 10, {0, 6}, 18, 0, gf::Color::rgba(0, 0, 0, 120));
        paint_gloss(p, banner, 10, GlossTone::gold, {});
        const gf::FontSpec big{gf::FontRole::content, 28, 700, false, .5};
        const gf::Size m = p.measure_text_utf8("Solved!", big);
        p.draw_text_utf8({banner.x + (banner.width - m.width) * .5, banner.y + 41}, "Solved!", big,
                         gloss_ink(GlossTone::gold));
    }
}
void PuzzleView::solve_select(int piece) {
    if (piece == selection_)
        return;
    piece_rotation_[selection_] = rotation_;
    piece_flip_[selection_] = flip_;
    selection_ = piece;
    rotation_ = piece_rotation_[piece];
    flip_ = piece_flip_[piece];
}
void PuzzleView::solve_recenter() {
    int mx = 0, my = 0;
    for (const PieceCell& c : game.piece_cells(selection_, rotation_, flip_)) {
        mx = std::max(mx, c.x + 1);
        my = std::max(my, c.y + 1);
    }
    grab_ = {mx * .5, my * .5};
}
bool PuzzleView::solve_snap(int& x, int& y) const {
    double unit = board_.width / 4;
    x = static_cast<int>(std::lround((solve_pointer_.x - board_.x) / unit - grab_.x));
    y = static_cast<int>(std::lround((solve_pointer_.y - board_.y) / unit - grab_.y));
    for (const PieceCell& c : game.piece_cells(selection_, rotation_, flip_)) {
        int xx = x + c.x, yy = y + c.y;
        if (xx < 0 || yy < 0 || xx >= 4 || yy >= 4)
            return false;
        int value = game.state.grid[(yy * 4 + xx) * 4 + c.wedge];
        if (value && value / 4 != selection_ + 1)
            return false;
    }
    return true;
}
// Recovers the orientation and origin of a placed piece, then removes it so it can be carried.
bool PuzzleView::solve_lift(int piece) {
    std::vector<std::pair<int, int>> placed;
    int minx = 10, miny = 10;
    for (int i = 0; i < 64; ++i)
        if (game.state.grid[i] / 4 == piece + 1) {
            int x = (i / 4) % 4, y = i / 16;
            placed.push_back({(y * 4 + x) * 4 + i % 4, 0});
            minx = std::min(minx, x);
            miny = std::min(miny, y);
        }
    if (placed.empty())
        return false;
    for (int orientation = 0; orientation < 8; ++orientation) {
        std::vector<PieceCell> cells = game.piece_cells(piece, orientation % 4, orientation >= 4);
        if (cells.size() != placed.size())
            continue;
        bool match = true;
        for (const PieceCell& c : cells) {
            int i = ((miny + c.y) * 4 + minx + c.x) * 4 + c.wedge;
            if (minx + c.x >= 4 || miny + c.y >= 4 ||
                game.state.grid[i] != (piece + 1) * 4 + c.color)
                match = false;
        }
        if (!match)
            continue;
        if (!game.remove_piece(piece))
            return false;
        solve_select(piece);
        rotation_ = orientation % 4;
        flip_ = orientation >= 4;
        lifted_ = true;
        lift_x_ = minx;
        lift_y_ = miny;
        lift_rotation_ = rotation_;
        lift_flip_ = flip_;
        double unit = board_.width / 4;
        grab_ = {(solve_pointer_.x - board_.x) / unit - minx,
                 (solve_pointer_.y - board_.y) / unit - miny};
        return true;
    }
    return false;
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
        lines = {"Rebuild the blue and yellow picture using all seven pieces.",
                 "Drag a piece from the tray into the frame; it snaps into place.",
                 "While dragging, R or a right click turns it and F mirrors it.",
                 "Click the chosen piece in the tray to turn it before you drag.",
                 "Drag placed pieces to move them. Right click one to return it.",
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
    if (game.kind == PuzzleKind::atom || game.kind == PuzzleKind::sticks) {
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
    text(p, 17, b.height - 11, game.message, 13, gf::Color::rgba(0, 0, 0, 120));
    text(p, 16, b.height - 12, game.message, 13, ink);
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
        text(p, popup_.x + popup_.width - 96, popup_.y + 178 + i * 27,
             std::to_string(game.scores[i].value), 16, accent);
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
    if (game.state.over && game.state.won && game.kind != PuzzleKind::gems &&
        animation_duration_ <= 0) {
        // A short celebration plays before the Top scores card opens.
        animation_start_ = gf::FrameClock::now();
        animation_duration_ = reduced_ ? .6 : 2.2;
        if (timer_)
            (*timer_).start();
    }
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
    piece_rotation_.fill(0);
    piece_flip_.fill(false);
    lifted_ = false;
    set_pointer_capture(false);
    panel(0);
    persist();
    music_play(game.kind == PuzzleKind::cube ? "nature_cube" : puzzle_slug(game.kind), music_);
    sound_play("ui_new_game", sound_);
    render();
}
std::vector<GameCommand> PuzzleView::commands() const {
    std::vector<GameCommand> list{{"new", "New game", true, false, true}};
    if (game.kind == PuzzleKind::solve) {
        list.push_back({"rotate", "Rotate", panel_ == 0 && !game.state.over});
        list.push_back({"flip", "Flip", panel_ == 0 && !game.state.over, flip_});
    }
    list.push_back({"help", "Help", true, panel_ == 1});
    list.push_back({"scores", "Top scores", true, panel_ == 2});
    return list;
}
void PuzzleView::run_command(std::string_view id) {
    if (id == "new")
        action(*buttons_[0]);
    else if (id == "help")
        panel_ == 1 ? panel(0) : action(*buttons_[1]);
    else if (id == "scores")
        panel_ == 2 ? panel(0) : action(*buttons_[2]);
    else if (id == "rotate")
        action(*buttons_[4]);
    else if (id == "flip")
        action(*buttons_[5]);
    invalidate(gf::Dirty::paint);
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
std::optional<gf::Point> PuzzleView::cube_cell_point(int cell) const {
    double sx = 0, sy = 0;
    int count = 0;
    for (int y = 0; y < raster_.height; ++y)
        for (int x = 0; x < raster_.width; ++x)
            if (raster_.ids[static_cast<std::size_t>(y * raster_.width + x)] == cell) {
                sx += x + .5;
                sy += y + .5;
                ++count;
            }
    if (!count || raster_.width <= 1)
        return std::nullopt;
    return gf::Point{board_.x + sx / count / raster_.width * board_.width,
                     board_.y + sy / count / raster_.height * board_.height};
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
        if (raster_.ids[y * raster_.width + x] >= 0)
            return raster_.ids[y * raster_.width + x];
        // The grout between tiles belongs to the nearest tile, so presses never fall through.
        const int reach = std::max(2, raster_.width / 90);
        int best = -1, best_distance = reach * reach + 1;
        for (int dy = -reach; dy <= reach; ++dy)
            for (int dx = -reach; dx <= reach; ++dx) {
                const int xx = x + dx, yy = y + dy;
                if (xx < 0 || yy < 0 || xx >= raster_.width || yy >= raster_.height)
                    continue;
                const int id = raster_.ids[yy * raster_.width + xx];
                if (id >= 0 && dx * dx + dy * dy < best_distance) {
                    best = id;
                    best_distance = dx * dx + dy * dy;
                }
            }
        return best;
    }
    if (game.kind == PuzzleKind::untangle) {
        for (int i = 0; i < 12; ++i)
            if (std::hypot(p.x - board_.x - game.state.nodes[i].x * board_.width,
                           p.y - board_.y - game.state.nodes[i].y * board_.height) <
                untangle_radius() + 8)
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
    if (game.kind == PuzzleKind::gems)
        step_effects();
    if (game.kind == PuzzleKind::gems && animation_duration_ > 0 && !invalid_swap_ &&
        elapsed() >= .23) {
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
            spawn_effects(before, after, depth);
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
        if (coarse_ != moving) {
            coarse_ = moving;
            fit_raster();
        }
    }
    if (game.kind == PuzzleKind::gems && ((!reduced_ && !panel_) || !effects_.empty()))
        moving = true;
    render();
    // Celebrations draw beyond the board, so the whole view repaints while they run.
    if (animation_duration_ > 0 && game.kind != PuzzleKind::gems)
        invalidate(gf::Dirty::paint);
    else
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
        if (game.kind == PuzzleKind::solve && drag_ >= 0) {
            solve_pointer_ = local_position;
            invalidate(gf::Dirty::paint);
            e.handled = true;
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
        if (game.kind == PuzzleKind::untangle && drag_ < 0 && hover_ != cell) {
            hover_ = cell;
            invalidate(board_);
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
            if (tracing_ && cell >= 0) {
                bool extended = game.cube_extend(cell);
                // A quick diagonal flick skips a cell; route through the free corner cell.
                const int pair = game.state.stage - 1;
                if (!extended && pair >= 0 && pair < 6 && !game.state.paths[pair].empty()) {
                    const int back = game.state.paths[pair].back();
                    for (int m = 0; m < 96 && !extended; ++m)
                        if (PuzzleGame::cube_adjacent(back, m) &&
                            PuzzleGame::cube_adjacent(m, cell)) {
                            PuzzleGame trial = game;
                            if (trial.cube_extend(m) && trial.cube_extend(cell)) {
                                game = trial;
                                extended = true;
                            }
                        }
                }
                if (extended)
                    changed("nature_cube_trace");
            }
            hover_ = cell;
            last_pointer_ = local_position;
            // Rendering happens once per frame in tick(), however many moves arrive.
            if (timer_)
                (*timer_).start();
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
            solve_pointer_ = local_position;
            const bool secondary = e.button == gf::PointerButton::secondary;
            if (drag_ >= 0 && secondary) {
                // Right click while carrying a piece turns it about its center.
                rotation_ = (rotation_ + 1) % 4;
                solve_recenter();
                sound_play("puzzle_solve_rotate", sound_);
            } else if (cell >= 0 && !secondary && !game.state.over) {
                // Pressing the piece that is already chosen, without dragging, turns it.
                tray_turn_ = cell == selection_;
                press_point_ = local_position;
                solve_select(cell);
                for (int i = 0; i < 64; ++i)
                    if (game.state.grid[i] / 4 == cell + 1) {
                        game.remove_piece(cell);
                        break;
                    }
                lifted_ = false;
                solve_recenter();
                sound_play("puzzle_solve_pickup", sound_);
                drag_ = cell;
                set_pointer_capture(true);
            } else if (in_rect(board_, local_position) && !game.state.over) {
                int x = std::clamp(
                        static_cast<int>((local_position.x - board_.x) * 4 / board_.width), 0, 3),
                    y = std::clamp(
                        static_cast<int>((local_position.y - board_.y) * 4 / board_.height), 0, 3);
                double fx = (local_position.x - board_.x) * 4 / board_.width - x,
                       fy = (local_position.y - board_.y) * 4 / board_.height - y;
                int wedge = fy < fx ? (fy < 1 - fx ? 0 : 1) : (fy < 1 - fx ? 3 : 2);
                int piece = game.state.grid[(y * 4 + x) * 4 + wedge] / 4 - 1;
                if (piece >= 0 && secondary) {
                    // Right click returns a placed piece to the tray.
                    if (game.remove_piece(piece)) {
                        solve_select(piece);
                        changed("puzzle_solve_pickup");
                    }
                } else if (piece >= 0 && solve_lift(piece)) {
                    drag_ = piece;
                    set_pointer_capture(true);
                    sound_play("puzzle_solve_pickup", sound_);
                } else if (piece < 0 && !secondary) {
                    // Clicking empty frame space drops the selected piece there, kept inside the
                    // frame.
                    int mx = 0, my = 0;
                    for (const PieceCell& c : game.piece_cells(selection_, rotation_, flip_)) {
                        mx = std::max(mx, c.x + 1);
                        my = std::max(my, c.y + 1);
                    }
                    double unit = board_.width / 4;
                    int px = std::clamp(static_cast<int>(std::lround(
                                            (local_position.x - board_.x) / unit - mx * .5)),
                                        0, 4 - mx),
                        py = std::clamp(static_cast<int>(std::lround(
                                            (local_position.y - board_.y) / unit - my * .5)),
                                        0, 4 - my);
                    if (game.place_piece(selection_, px, py, rotation_, flip_))
                        changed("puzzle_solve_place");
                    else
                        sound_play("puzzle_solve_no_fit", sound_);
                }
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
        if (!(game.kind == PuzzleKind::solve && drag_ >= 0 &&
              e.button == gf::PointerButton::secondary))
            set_pointer_capture(false);
        if (game.kind == PuzzleKind::solve && drag_ >= 0 &&
            e.button != gf::PointerButton::secondary) {
            const int carried = drag_;
            drag_ = -1;
            solve_pointer_ = local_position;
            int x = 0, y = 0;
            const bool clicked = std::hypot(local_position.x - press_point_.x,
                                            local_position.y - press_point_.y) < 5;
            if (!lifted_ && clicked && carried >= 0 && carried < static_cast<int>(cells_.size()) &&
                in_rect(cells_[static_cast<std::size_t>(carried)], local_position)) {
                if (tray_turn_) {
                    rotation_ = (rotation_ + 1) % 4;
                    sound_play("puzzle_solve_rotate", sound_);
                }
                tray_turn_ = false;
                invalidate(gf::Dirty::paint);
            } else if (in_rect(board_, local_position) && solve_snap(x, y) &&
                       game.place_piece(selection_, x, y, rotation_, flip_))
                changed("puzzle_solve_place");
            else {
                // A piece carried from the frame and dropped nowhere useful goes back where it was.
                if (lifted_ && in_rect(board_, local_position)) {
                    sound_play("puzzle_solve_no_fit", sound_);
                    game.place_piece(selection_, lift_x_, lift_y_, lift_rotation_, lift_flip_);
                }
                persist();
            }
            lifted_ = false;
            invalidate(gf::Dirty::paint);
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
    if (e.physical_key == gf::PhysicalKey::r && game.kind == PuzzleKind::solve) {
        rotation_ = (rotation_ + 1) % 4;
        if (drag_ >= 0)
            solve_recenter();
    } else if (e.physical_key == gf::PhysicalKey::f && game.kind == PuzzleKind::solve) {
        flip_ = !flip_;
        if (drag_ >= 0)
            solve_recenter();
    } else if (e.physical_key == gf::PhysicalKey::left && game.kind == PuzzleKind::pegs)
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
