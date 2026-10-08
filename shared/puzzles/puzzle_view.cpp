#include "puzzle_view.hpp"
#include "audio.hpp"
#include "gui_forms/window.hpp"
#include "help_route.hpp"
#include "presentation.hpp"
#include "storage.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
namespace games {
namespace {
// Nature Cube's sound for a path that just changed: it grew, shrank or reached its pair.
std::string cube_step_sound(const PuzzleGame& game, std::size_t before) {
    const int pair = game.state.stage - 1;
    if (pair < 0 || pair >= game.cube_pairs())
        return "nature_cube_trace";
    const std::vector<int>& path = game.state.paths[pair];
    if (path.size() < before)
        return "nature_cube_erase";
    if (path.size() > 1 && game.cube_pair(path.back()) == pair + 1)
        return "nature_cube_connect";
    return "nature_cube_trace";
}
// True when the pointer reached a neighbour of an unfinished path's end that the path
// may not enter.
bool cube_refused(const PuzzleGame& game, int cell) {
    const std::vector<int>& path = game.state.paths[game.state.stage - 1];
    if (path.empty() || !PuzzleGame::cube_adjacent(path.back(), cell))
        return false;
    if (path.size() > 1 && game.cube_pair(path.back()) == game.state.stage)
        return false;
    return std::find(path.begin(), path.end(), cell) == path.end();
}
} // namespace
// Retain scenery, animation and foreground text independently, in authored order.
// These controls own no input; all game interaction stays on PuzzleView.
class PuzzleScenePart final : public gf::Control {
  public:
    PuzzleScenePart(gf::StableId id, PuzzleView& owner, int part)
        : Control(std::move(id)), owner_(owner), part_(part) {
        set_hit_test_transparent(true);
    }
    void on_paint(gf::Painter& painter, gf::Rect) override {
        if (owner_.surface_) {
            if (part_ == 0)
                painter.draw_live_surface(owner_.surface_, client_rectangle());
            return;
        }
        if (part_ == 0)
            painter.fill_rect(client_rectangle(), gf::Color::rgba(112, 126, 133));
        if (owner_.game.kind == PuzzleKind::gems)
            owner_.paint_gems(painter, part_);
        else
            owner_.paint_untangle(painter, part_);
    }

  private:
    PuzzleView& owner_;
    int part_;
};
void PuzzleView::invalidate_scene() {
    solve_prepare();
    refresh_scene();
    if (!surface_ || panel_)
        invalidate(gf::Dirty::paint);
}
void PuzzleView::refresh_scene() {
    untangle_geometry_dirty_ = true;
    static_pixels_dirty_ = true;
    if (!panel_)
        static_cast<void>(prepare_framebuffer());
    for (int index = 0; index < 3; ++index)
        if (scene_parts_[index]) {
            (*scene_parts_[index]).set_visible(panel_ == 0 && (!surface_ || index == 0));
            if (!surface_)
                (*scene_parts_[index]).invalidate(gf::Dirty::paint);
        }
    if (surface_ && !panel_)
        present_framebuffer(client_rectangle());
}
void PuzzleView::invalidate_static_scene() {
    static_pixels_dirty_ = true;
    if (!surface_) {
        (*scene_parts_[0]).invalidate(untangle_board_damage());
        (*scene_parts_[2]).invalidate(gf::Dirty::paint);
    }
}
void PuzzleView::invalidate_animation(gf::Rect damage) {
    if (!panel_ && prepare_framebuffer()) {
        present_framebuffer(damage);
    } else if (scene_parts_[1] && !panel_) {
        (*scene_parts_[1]).invalidate(damage);
    } else {
        invalidate(damage);
    }
}
bool PuzzleView::prepare_framebuffer() {
    if (!scene_parts_[0] || !attached_window())
        return false;
    gf::Window& window = *attached_window();
    const gf::Rect bounds = client_rectangle();
    const gf::Size size{bounds.width, bounds.height};
    if (framebuffer_ && size == framebuffer_size_ && window.scale() == framebuffer_scale_)
        return true;
    if (size.width <= 0 || size.height <= 0)
        return false;
    std::unique_ptr<gf::PaintFramebuffer> replacement =
        window.create_framebuffer(size, window.scale());
    if (!replacement)
        return false;
    gf::LiveSurfaceDescription description;
    description.width = (*replacement).width();
    description.height = (*replacement).height();
    // The framebuffer's own byte order, and opaque: presenting it is a straight copy.
    description.pixel_format = (*replacement).channel_order() == gf::FramebufferChannelOrder::rgba
                                   ? gf::LiveSurfacePixelFormat::rgba32_premultiplied_srgb
                                   : gf::LiveSurfacePixelFormat::bgra32_premultiplied_srgb;
    description.opaque = true;
    if (surface_) {
        if (!(*surface_).reconfigure(description))
            return false;
    } else {
        surface_ = gf::LiveSurface::create(description);
        if (!surface_)
            return false;
    }
    framebuffer_ = std::move(replacement);
    framebuffer_size_ = size;
    framebuffer_scale_ = window.scale();
    static_pixels_dirty_ = true;
    static_pixels_.clear();
    (*scene_parts_[1]).set_visible(false);
    (*scene_parts_[2]).set_visible(false);
    direct_ = window.queue_live_surface_presentation(scene_parts_[0], surface_);
    if (game.kind == PuzzleKind::gems) {
        render();
        if (image_.value) {
            static_cast<void>(window.remove_image(image_));
            image_ = {};
        }
    }
    return true;
}
void PuzzleView::present_framebuffer(gf::Rect damage) {
    if (framebuffer_painting_ || panel_ || !prepare_framebuffer())
        return;
    gf::PaintFramebuffer& target = *framebuffer_;
    const gf::Rect bounds = client_rectangle();
    const bool rebuild = static_pixels_dirty_ || static_pixels_.empty();
    if (rebuild)
        damage = bounds;
    damage = gf::Rect::intersection(damage, bounds);
    if (damage.empty())
        return;
    // Restore and paint exactly the same physical pixels at fractional DPI.
    const int left = std::max(0, static_cast<int>(std::floor(damage.x * framebuffer_scale_)));
    const int top = std::max(0, static_cast<int>(std::floor(damage.y * framebuffer_scale_)));
    const int right =
        std::min(static_cast<int>(target.width()),
                 static_cast<int>(std::ceil((damage.x + damage.width) * framebuffer_scale_)));
    const int bottom =
        std::min(static_cast<int>(target.height()),
                 static_cast<int>(std::ceil((damage.y + damage.height) * framebuffer_scale_)));
    damage = {left / framebuffer_scale_, top / framebuffer_scale_,
              (right - left) / framebuffer_scale_, (bottom - top) / framebuffer_scale_};
    if (!target.begin(frame_images_, damage))
        return;
    framebuffer_painting_ = true;
    gf::Painter& painter = target.painter();
    std::span<std::byte> pixels = target.pixels();
    if (rebuild) {
        painter.fill_rect(bounds, gf::Color::rgba(112, 126, 133));
        if (game.kind == PuzzleKind::gems)
            paint_gems(painter, 0);
        else
            paint_untangle(painter, 0);
        target.end();
        static_pixels_.assign(pixels.begin(), pixels.end());
        static_pixels_dirty_ = false;
        if (!target.begin(frame_images_, damage)) {
            framebuffer_painting_ = false;
            return;
        }
    } else {
        for (int y = top; y < bottom; ++y) {
            const std::size_t offset = static_cast<std::size_t>(y) * target.row_bytes() + left * 4U;
            std::memcpy(pixels.data() + offset, static_pixels_.data() + offset,
                        static_cast<std::size_t>(right - left) * 4U);
        }
    }
    if (game.kind == PuzzleKind::gems) {
        paint_gems(painter, 1);
        paint_gems(painter, 2);
    } else {
        paint_untangle(painter, 1);
        paint_untangle(painter, 2);
    }
    target.end();
    gf::LiveSurfaceWriteLease lease = (*surface_).try_acquire_write();
    if (lease) {
        // Pool rotation and skipped generations require a complete candidate. The
        // surface shares the framebuffer's byte order; alpha is forced opaque as promised.
        for (std::uint32_t y = 0; y < target.height(); ++y) {
            const std::uint32_t* source =
                reinterpret_cast<const std::uint32_t*>(pixels.data() + y * target.row_bytes());
            std::uint32_t* destination =
                reinterpret_cast<std::uint32_t*>(lease.pixels().data() + y * lease.row_bytes());
            for (std::uint32_t x = 0; x < target.width(); ++x)
                destination[x] = 0xff000000U | source[x];
        }
        static_cast<void>(lease.publish(damage));
        if (!direct_)
            (*scene_parts_[0]).invalidate(damage);
    }
    framebuffer_painting_ = false;
}
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
    // The level of the game on the table is also the level of the next one.
    if (game.kind == PuzzleKind::cube || game.kind == PuzzleKind::untangle)
        game.level = std::clamp(game.state.aux[94], 0, 2);
    if (game.kind == PuzzleKind::solve)
        game.level = game.solve_level();
}
std::filesystem::path PuzzleView::path() const {
    return cabinet_path().parent_path() /
           (std::string(puzzle_slug(game.kind)) +
            ((game.kind == PuzzleKind::cube || game.kind == PuzzleKind::solve) ? "-v2.txt"
                                                                               : "-v1.txt"));
}
void PuzzleView::initialize_control_tree() {
    if (game.kind == PuzzleKind::gems || game.kind == PuzzleKind::untangle)
        for (int i = 0; i < 3; ++i) {
            scene_parts_[i] = gf::make_control<PuzzleScenePart>(
                gf::StableId(std::string(puzzle_slug(game.kind)) + ".scene." + std::to_string(i)),
                *this, i);
            add_child(scene_parts_[i]);
        }
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
    framebuffer_.reset();
    surface_.reset();
    static_pixels_.clear();
    frame_images_.clear();
    frame_image_ = {};
    if (timer_)
        (*timer_).stop();
    timer_.reset();
    for (gf::ImageId id : {image_, nature_, curator_})
        if (id.value)
            static_cast<void>(window.remove_image(id));
    solve_release_images(window);
}
void PuzzleView::activate() {
    refresh_scene();
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
    if ((game.kind == PuzzleKind::gems || game.kind == PuzzleKind::untangle) && timer_)
        (*timer_).start();
    render();
    invalidate_scene();
}
void PuzzleView::arrange(gf::Rect b) {
    arrange_self(b);
    for (const std::shared_ptr<gf::Control>& part : scene_parts_)
        if (part)
            set_child_layout(part, {0, 0, b.width, b.height});
    untangle_geometry_dirty_ = true;
    static_pixels_dirty_ = true;
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
        // Centered when narrow; with room, Untangle's legend card sits to the left. Gems
        // explains itself in the help document and is always centered.
        const bool wide = b.width >= 900 && game.kind == PuzzleKind::untangle;
        // Untangle's cushion and status line need room below the board.
        const double below = game.kind == PuzzleKind::untangle ? 64 : 0;
        const double gside = std::max(
            160.0, std::min({600.0, b.height - 104 - below, b.width - (wide ? 560.0 : 48.0)}));
        board_ = {wide ? std::max(264.0, (b.width - gside) * .5) : (b.width - gside) * .5,
                  std::max(66.0, 62 + (b.height - 62 - 30 - below - gside) * .5), gside, gside};
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
        const int old_width = raster_.width, old_height = raster_.height;
        fit_raster();
        if (old_width != raster_.width || old_height != raster_.height ||
            (!image_.value && !frame_image_.value))
            render();
    }
    if (surface_ && !panel_)
        present_framebuffer(client_rectangle());
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
void PuzzleView::request_render() {
    raster_dirty_ = true;
    if (timer_)
        (*timer_).start();
}
void PuzzleView::render() {
    if (!attached_window() || raster_.width <= 1)
        return;
    raster_dirty_ = false;
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
        const int lifted = !moving ? (drag_ >= 0 && gem_dragging_ ? drag_ : gem_spring_) : -1;
        int partner = -1;
        if (lifted >= 0) {
            const int sx = gem_offset_.x > 0   ? 1
                           : gem_offset_.x < 0 ? -1
                                               : 0,
                      sy = gem_offset_.y > 0   ? 1
                           : gem_offset_.y < 0 ? -1
                                               : 0;
            const int px = lifted % 8 + sx, py = lifted / 8 + sy;
            if ((sx || sy) && px >= 0 && px < 8 && py >= 0 && py < 8)
                partner = py * 8 + px;
        }
        const double intro_t =
            intro_ ? std::chrono::duration<double>(gf::FrameClock::now() - intro_start_).count()
                   : 9;
        if (intro_ && intro_t > 1.4)
            intro_ = false;
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i < 64; ++i) {
                // The lifted gem draws last, above everything it passes over.
                if ((pass == 1) != (i == lifted))
                    continue;
                int value = (*grid)[i];
                double radius = cell * .38;
                double x = (i % 8 + .5) * cell, y = (i / 8 + .5) * cell;
                if (!moving && !reduced_)
                    radius *= 1 + .022 * std::sin(time * 2.3 + i * 1.7); // the stones breathe
                if (i == lifted) {
                    x += gem_offset_.x * cell;
                    y += gem_offset_.y * cell;
                    radius *= 1.12;
                } else if (i == partner) {
                    x -= gem_offset_.x * cell;
                    y -= gem_offset_.y * cell;
                }
                if (!moving && i == gem_pick_)
                    radius *= 1.08 + .04 * std::sin(time * 7);
                if (!moving && gem_hint_ >= 0 && (i == gem_hint_ || i == gem_hint_to_) &&
                    !reduced_) {
                    // After a long pause, one pair that would match nudges toward each other.
                    const int other = i == gem_hint_ ? gem_hint_to_ : gem_hint_;
                    const double nudge =
                        std::fmod(time, 1.6) < .7 ? std::max(0.0, std::sin(time * 9)) * .09 : 0;
                    x += (other % 8 - i % 8) * cell * nudge;
                    y += (other / 8 - i / 8) * cell * nudge;
                }
                if (intro_t < 1.4) {
                    // A fresh board pours in from above, bottom rows first, with a small bounce.
                    const double delay = (7 - i / 8) * .07 + (i % 8) * .012;
                    const double u = std::clamp((intro_t - delay) / .42, 0.0, 1.0);
                    const double c1 = 1.5, c3 = c1 + 1, w = u - 1;
                    const double settle = 1 + c3 * w * w * w + c1 * w * w;
                    y -= (1 - settle) * (i / 8 + 1.6) * cell;
                }
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
                    u = u * u * (3 - 2 * u);
                    // A swap released from a drag starts where the gems already are.
                    u = swap_from_ + (1 - swap_from_) * u;
                    if (invalid_swap_ && t > .23) {
                        u = 1 - std::clamp((t - .23) / .23, 0.0, 1.0);
                        u = u * u * (3 - 2 * u);
                    }
                    int other = i == swap_a_ ? swap_b_ : swap_a_;
                    x += (other % 8 - i % 8) * cell * u;
                    y += (other / 8 - i / 8) * cell * u;
                }
                const bool turning = hover_ == i || i == gem_pick_ || i == lifted;
                double spin = turning && !reduced_ ? time * 2.1 : 0,
                       glisten = reduced_ ? 0 : .5 + .5 * std::sin(time * 1.25 + i * 2.7);
                raster_.z_bias = i == lifted ? -20 : 0;
                raster_.gem({x, y}, radius, value, spin, glisten, i);
                raster_.z_bias = 0;
            }
    } else
        return;
    // Board pixels do not change the surrounding legend or command capsule.
    // Patching keeps damage local; a resized raster still replaces the image.
    if (framebuffer_ && game.kind == PuzzleKind::gems) {
        gf::ImageLoadResult frame =
            !frame_image_.value
                ? frame_images_.load_bgra32_premultiplied(raster_.width, raster_.height,
                                                          raster_.width * 4, raster_.pixels)
                : frame_images_.update_bgra32_premultiplied(frame_image_, raster_.width,
                                                            raster_.height, raster_.width * 4,
                                                            raster_.pixels);
        if (!frame && frame_image_.value)
            frame = frame_images_.replace_bgra32_premultiplied(
                frame_image_, raster_.width, raster_.height, raster_.width * 4, raster_.pixels);
        if (frame)
            frame_image_ = frame.image;
        return;
    }
    gf::Window& window = *attached_window();
    gf::Control& consumer = scene_parts_[1] ? *scene_parts_[1] : *this;
    gf::ImageLoadResult result =
        !image_.value ? window.load_bgra32_premultiplied(raster_.width, raster_.height,
                                                         raster_.width * 4, raster_.pixels)
        : image_size_ ==
                gf::Size{static_cast<double>(raster_.width), static_cast<double>(raster_.height)}
            ? window.patch_bgra32_premultiplied(image_, 0, 0, raster_.width, raster_.height,
                                                raster_.width * 4, raster_.pixels, consumer, board_)
            : window.replace_bgra32_premultiplied(image_, raster_.width, raster_.height,
                                                  raster_.width * 4, raster_.pixels, consumer);
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
    if (!cleared.empty()) {
        double sx = 0, sy = 0;
        for (int i : cleared) {
            sx += i % 8 + .5;
            sy += i / 8 + .5;
        }
        const double n = static_cast<double>(cleared.size());
        effects_.push_back({GemEffect::points, sx / n, sy / n, 0, -1.1, 0, .9,
                            static_cast<double>(cleared.size() * 10),
                            gem_color(before[cleared.front()])});
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
    fx_dt_ = dt;
    for (GemEffect& e : effects_) {
        e.age += dt;
        if (e.kind == GemEffect::shard) {
            e.vy += 9.5 * dt;
            e.x += e.vx * dt;
            e.y += e.vy * dt;
            e.spin += dt * 9;
        }
        if (e.kind == GemEffect::points)
            e.y += e.vy * dt * (1 - e.age / e.life);
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
void PuzzleView::paint_gems(gf::Painter& p, int part) {
    const gf::Rect b = client_rectangle();
    const double cell = board_.width / 8;
    const double t = std::chrono::duration<double>(gf::FrameClock::now() - clock_start_).count();
    const gf::Point shake{shake_ > 0 ? std::sin(t * 61) * shake_ * cell * .08 : 0,
                          shake_ > 0 ? std::cos(t * 47) * shake_ * cell * .08 : 0};
    if (part < 0 || part == 0) {
        // Velvet workspace under a jeweller's lamp.
        p.fill_rect(b, gf::Color::rgba(26, 16, 44));
        const gf::GradientStop lamp[] = {{0, gf::Color::rgba(86, 52, 128)},
                                         {1, gf::Color::rgba(22, 12, 38)}};
        p.fill_radial_gradient(b, {board_.x + board_.width * .5, board_.y + board_.height * .45},
                               {b.width * .75, b.height * .85}, lamp);
    }
    p.save();
    p.translate(shake);
    if (part < 0 || part == 0) {
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
                p.fill_rounded_rect({board_.x + (i % 8) * cell + 2, board_.y + (i / 8) * cell + 2,
                                     cell - 4, cell - 4},
                                    cell * .16, gf::Color::rgba(44, 30, 74, 150));
    }
    if (part < 0 || part == 1) {
        if (hover_ >= 0 && hover_ < 64 && animation_duration_ <= 0)
            p.fill_rounded_rect({board_.x + (hover_ % 8) * cell + 2,
                                 board_.y + (hover_ / 8) * cell + 2, cell - 4, cell - 4},
                                cell * .16, gf::Color::rgba(255, 220, 150, 40));
        if (gem_pick_ >= 0 && animation_duration_ <= 0) {
            const double pulse = .5 + .5 * std::sin(t * 7);
            const gf::Rect pick{board_.x + (gem_pick_ % 8) * cell + 1,
                                board_.y + (gem_pick_ / 8) * cell + 1, cell - 2, cell - 2};
            p.fill_rounded_rect(
                pick, cell * .18,
                gf::Color::rgba(255, 214, 120, static_cast<unsigned char>(50 + 40 * pulse)));
            // The four neighbors it can trade places with.
            for (const int d : {-8, 8, -1, 1}) {
                const int n = gem_pick_ + d;
                if (n < 0 || n >= 64 || ((d == 1 || d == -1) && n / 8 != gem_pick_ / 8))
                    continue;
                p.fill_rounded_rect({board_.x + (n % 8) * cell + 4, board_.y + (n / 8) * cell + 4,
                                     cell - 8, cell - 8},
                                    cell * .16, gf::Color::rgba(255, 230, 170, 26));
            }
        }
        const gf::ImageId gem_image = framebuffer_painting_ ? frame_image_ : image_;
        if (gem_image.value)
            p.draw_image(gem_image, board_);
        if (gem_pick_ >= 0 && animation_duration_ <= 0) {
            const double pulse = .5 + .5 * std::sin(t * 7);
            const gf::Rect pick{board_.x + (gem_pick_ % 8) * cell + 2,
                                board_.y + (gem_pick_ / 8) * cell + 2, cell - 4, cell - 4};
            p.stroke_rounded_rect(
                pick, cell * .16,
                gf::Color::rgba(255, 222, 140, static_cast<unsigned char>(150 + 100 * pulse)), 2.5);
        }
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
                    tri.push_back({q.x + std::cos(e.spin + v * 2.1) * r,
                                   q.y + std::sin(e.spin + v * 2.1) * r});
                paint_polygon(p, tri,
                              gf::Color::rgba(c.red, c.green, c.blue,
                                              static_cast<unsigned char>(240 * fade)));
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
            case GemEffect::points: {
                const gf::Point q = at(e.x, e.y);
                const std::string label = "+" + std::to_string(static_cast<int>(e.size));
                const gf::FontSpec f{gf::FontRole::content, std::clamp(cell * .42, 14.0, 26.0), 800,
                                     false};
                const gf::Size m = p.measure_text_utf8(label, f);
                const unsigned char alpha =
                    static_cast<unsigned char>(255 * std::min(1.0, fade * 2));
                p.draw_text_utf8({q.x - m.width * .5 + 1.5, q.y + 2}, label, f,
                                 gf::Color::rgba(30, 10, 40, alpha));
                p.draw_text_utf8({q.x - m.width * .5, q.y}, label, f,
                                 gf::Color::rgba(255, 244, 214, alpha));
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
                    p.draw_line(prev, next,
                                gf::Color::rgba(c.red, c.green, c.blue,
                                                static_cast<unsigned char>(200 * fade)),
                                4);
                    p.draw_line(
                        prev, next,
                        gf::Color::rgba(255, 255, 255, static_cast<unsigned char>(240 * fade)),
                        1.6);
                    prev = next;
                }
                break;
            }
            }
        }
    }
    p.restore();
    if ((part < 0 || part == 1) && callout_life_ > 0 && !callout_.empty()) {
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
}
double PuzzleView::untangle_radius() const {
    return std::clamp(board_.width * .026, 11.0, 17.0);
}
// Untangle: yarn tied to wooden pegs on a tufted cushion, watched by a cat. Each color of
// yarn is its own layer; snags mark where a thread crosses its own color. When the last snag
// clears, the yarn warms to gold, a ripple runs outward and the cat curls up in the web.
bool PuzzleView::update_untangle_geometry() {
    std::vector<Point2> positions = game.state.nodes;
    if (swat_peg_ >= 0 && swat_peg_ < static_cast<int>(positions.size()) && swat_t_ < 1) {
        const double k = 1 - (1 - swat_t_) * (1 - swat_t_);
        positions[swat_peg_] = {swat_from_.x + (positions[swat_peg_].x - swat_from_.x) * k,
                                swat_from_.y + (positions[swat_peg_].y - swat_from_.y) * k};
    }
    if (drag_ >= 0)
        positions[drag_] = drag_position_;
    bool changed = untangle_geometry_dirty_ || positions.size() != untangle_positions_.size();
    if (!changed)
        for (std::size_t i = 0; i < positions.size(); ++i)
            if (positions[i].x != untangle_positions_[i].x ||
                positions[i].y != untangle_positions_[i].y) {
                changed = true;
                break;
            }
    if (!changed)
        return false;
    untangle_geometry_dirty_ = false;
    untangle_positions_ = positions;
    struct BoardPosition {
        gf::Rect board_;
        gf::Point operator()(Point2 q) const {
            return gf::Point{board_.x + q.x * board_.width, board_.y + q.y * board_.height};
        }
    };
    BoardPosition at{board_};
    const int edge_count = static_cast<int>(game.state.edges.size());
    std::vector<bool> crossed(game.state.edges.size(), false);
    std::vector<gf::Point> snags;
    for (int i = 0; i < edge_count; ++i)
        for (int j = i + 1; j < edge_count; ++j)
            if (game.threads_cross(i, j, positions)) {
                crossed[i] = crossed[j] = true;
                const Edge e = game.state.edges[i], f = game.state.edges[j];
                const Point2 p0 = positions[e.a], p1 = positions[e.b], q0 = positions[f.a],
                             q1 = positions[f.b];
                const double den = (p1.x - p0.x) * (q1.y - q0.y) - (p1.y - p0.y) * (q1.x - q0.x);
                if (std::abs(den) > 1e-9) {
                    const double s =
                        ((q0.x - p0.x) * (q1.y - q0.y) - (q0.y - p0.y) * (q1.x - q0.x)) / den;
                    snags.push_back(at({p0.x + s * (p1.x - p0.x), p0.y + s * (p1.y - p0.y)}));
                }
            }
    untangle_crossed_ = std::move(crossed);
    untangle_snags_ = std::move(snags);
    untangle_crossings_ = game.crossings();
    return true;
}
void PuzzleView::paint_untangle(gf::Painter& p, int part) {
    const gf::Rect b = client_rectangle();
    const double t = std::chrono::duration<double>(gf::FrameClock::now() - clock_start_).count();
    const double pad = std::max(14.0, board_.width * .045);
    const gf::Rect cushion{board_.x - pad, board_.y - pad, board_.width + 2 * pad,
                           board_.height + 2 * pad};
    const int layers = game.untangle_layers();
    const gf::Color yarn[] = {gf::Color::rgba(228, 76, 70), gf::Color::rgba(80, 136, 236),
                              gf::Color::rgba(240, 190, 58)};
    if (part < 0 || part == 0) {
        // A warm wooden floor under a tufted velvet cushion.
        fill_vertical(p, b, gf::Color::rgba(94, 60, 38), gf::Color::rgba(60, 36, 22));
        for (double x = -40; x < b.width; x += 74) {
            p.draw_line({x, 0}, {x, b.height}, gf::Color::rgba(40, 22, 12, 120), 2);
            p.draw_line({x + 2, 0}, {x + 2, b.height}, gf::Color::rgba(160, 110, 70, 40), 1);
        }
        const gf::GradientStop lamp[] = {{0, gf::Color::rgba(255, 210, 150, 60)},
                                         {1, gf::Color::rgba(0, 0, 0, 0)}};
        p.fill_radial_gradient(b, {board_.x + board_.width * .5, board_.y + board_.height * .3},
                               {b.width * .6, b.height * .7}, lamp);
        p.draw_box_shadow(cushion, pad * 1.4, {0, 12}, 30, 0, gf::Color::rgba(20, 6, 10, 170));
        p.save();
        p.clip_rounded_rect(cushion, pad * 1.4);
        const gf::GradientStop velvet[] = {{0, gf::Color::rgba(150, 62, 96)},
                                           {.7, gf::Color::rgba(104, 36, 66)},
                                           {1, gf::Color::rgba(70, 20, 44)}};
        p.fill_radial_gradient(cushion,
                               {cushion.x + cushion.width * .45, cushion.y + cushion.height * .4},
                               {cushion.width * .75, cushion.height * .75}, velvet);
        // Tufting: buttons on a diamond grid with soft creases between them.
        const double pitch = board_.width / 4;
        std::vector<gf::Point> tufts;
        for (int row = 0; row <= 4; ++row)
            for (int col = 0; col <= 4; ++col) {
                const double x = board_.x + col * pitch + (row % 2 ? pitch * .5 : 0);
                if (x > board_.x + board_.width + 1)
                    continue;
                tufts.push_back({x, board_.y + row * pitch});
            }
        for (const gf::Point a : tufts)
            for (const gf::Point c : tufts) {
                const double dx = c.x - a.x, dy = c.y - a.y;
                if (dy > 0 && std::abs(std::abs(dx) - pitch * .5) < 1 && std::abs(dy - pitch) < 1) {
                    p.draw_line(a, c, gf::Color::rgba(50, 10, 30, 70), 3);
                    p.draw_line({a.x + 1.5, a.y}, {c.x + 1.5, c.y},
                                gf::Color::rgba(220, 120, 160, 26), 1.5);
                }
            }
        for (const gf::Point a : tufts) {
            const double r = std::max(3.0, board_.width * .011);
            p.fill_rounded_rect({a.x - r * 1.8, a.y - r * 1.8, r * 3.6, r * 3.6}, r * 1.8,
                                gf::Color::rgba(40, 6, 24, 60));
            const gf::Rect button{a.x - r, a.y - r, 2 * r, 2 * r};
            const gf::GradientStop shine[] = {{0, gf::Color::rgba(236, 150, 186)},
                                              {1, gf::Color::rgba(92, 24, 54)}};
            p.save();
            p.clip_rounded_rect(button, r);
            p.fill_radial_gradient(button, {a.x - r * .35, a.y - r * .4}, {r * 1.6, r * 1.6},
                                   shine);
            p.restore();
        }
        p.restore();
        // Gold piping round the edge.
        p.stroke_rounded_rect(cushion, pad * 1.4, gf::Color::rgba(120, 82, 30), 5);
        p.stroke_rounded_rect(cushion, pad * 1.4, gf::Color::rgba(232, 190, 104), 2.4);
        // A ball of the first yarn in the corner, the end of its thread trailing off.
        if (cushion.x > 110) {
            const double r = std::min(46.0, cushion.x * .2);
            const gf::Point ball{cushion.x - r - 16, cushion.y + cushion.height - r};
            const gf::GradientStop shade[] = {{0, gf::Color::rgba(20, 6, 10, 130)},
                                              {1, gf::Color::rgba(20, 6, 10, 0)}};
            p.fill_radial_gradient({ball.x - r * 1.3, ball.y + r * .5, r * 2.6, r * .9},
                                   {ball.x, ball.y + r * .92}, {r * 1.2, r * .4}, shade);
            p.fill_rounded_rect({ball.x - r, ball.y - r, 2 * r, 2 * r}, r, yarn[0]);
            for (int k = 0; k < 7; ++k) {
                const double a = k * .45 - .6;
                std::vector<gf::Point> arc;
                for (int s = 0; s <= 12; ++s) {
                    const double u = -1.3 + s * 2.6 / 12;
                    arc.push_back(
                        {ball.x + std::cos(a) * r * .9 * std::sin(u) - std::sin(a) * r * .3,
                         ball.y + std::sin(a) * r * .9 * std::sin(u) +
                             std::cos(a) * r * .9 * std::cos(u) * .35});
                }
                for (std::size_t s = 1; s < arc.size(); ++s)
                    p.draw_line(arc[s - 1], arc[s],
                                mix_color(yarn[0], gf::Color::rgba(0, 0, 0), .28), 1.6);
            }
            const gf::GradientStop gloss[] = {{0, gf::Color::rgba(255, 255, 255, 70)},
                                              {1, gf::Color::rgba(255, 255, 255, 0)}};
            p.fill_radial_gradient({ball.x - r, ball.y - r, 2 * r, 2 * r},
                                   {ball.x - r * .4, ball.y - r * .45}, {r, r}, gloss);
        }
    }
    if (part <= 0)
        static_cast<void>(update_untangle_geometry());
    const std::vector<Point2>& positions = untangle_positions_;
    const std::vector<bool>& crossed = untangle_crossed_;
    const std::vector<gf::Point>& snags = untangle_snags_;
    const int crossings = untangle_crossings_;
    struct BoardPosition {
        gf::Rect board_;
        gf::Point operator()(Point2 q) const {
            return {board_.x + q.x * board_.width, board_.y + q.y * board_.height};
        }
    };
    const BoardPosition at{board_};
    const int edge_count = static_cast<int>(game.state.edges.size());
    const double celebrate = game.state.won && animation_duration_ > 0
                                 ? std::clamp(elapsed() / animation_duration_, 0.0, 1.0)
                                 : -1;
    const gf::Point center = at({.5, .5});
    const double thread = std::clamp(board_.width * .0105, 3.0, 7.0);
    if (part < 0 || part == 0) {
        // Threads, lowest color layer first, each a twisted strand of yarn.
        for (int layer = 0; layer < layers; ++layer)
            for (int i = 0; i < edge_count; ++i) {
                if (game.untangle_layer(i) != layer)
                    continue;
                const Edge e = game.state.edges[i];
                const gf::Point a = at(positions[e.a]), c = at(positions[e.b]);
                gf::Color color = yarn[layer];
                if (game.state.won) {
                    // Gold spreads outward from the middle as the ripple passes.
                    const double d =
                        std::hypot((a.x + c.x) * .5 - center.x, (a.y + c.y) * .5 - center.y) /
                        board_.width;
                    const double wave =
                        celebrate < 0 ? .35 : std::clamp((celebrate * 1.6 - d) * 4, 0.0, 1.0) * .7;
                    color = mix_color(color, gf::Color::rgba(255, 220, 120), wave);
                }
                const double w = crossed[i] ? thread * .85 : thread;
                p.draw_line({a.x + 1, a.y + 2.5}, {c.x + 1, c.y + 2.5},
                            gf::Color::rgba(30, 4, 16, 70), w + 2);
                p.draw_line(a, c, mix_color(color, gf::Color::rgba(0, 0, 0), .35), w + 1.6);
                p.draw_line(a, c, color, w);
                // The ply: short bright twists along the strand.
                const double length = std::hypot(c.x - a.x, c.y - a.y);
                if (length > 1) {
                    const double ux = (c.x - a.x) / length, uy = (c.y - a.y) / length;
                    const gf::Color ply = mix_color(color, gf::Color::rgba(255, 255, 255), .38);
                    for (double s = w; s < length - w * .5; s += w * 1.5) {
                        const gf::Point q{a.x + ux * s, a.y + uy * s};
                        p.draw_line(
                            {q.x - ux * w * .35 - uy * w * .4, q.y - uy * w * .35 + ux * w * .4},
                            {q.x + ux * w * .35 + uy * w * .4, q.y + uy * w * .35 - ux * w * .4},
                            ply, std::max(1.0, w * .3));
                    }
                }
            }
    }
    if (part < 0 || part == 1) {
        // Snags where same-colored threads cross, pulsing gently.
        for (const gf::Point q : snags) {
            // A small knot with a soft pulsing halo.
            const double r = thread * (.9 + .15 * std::sin(t * 5));
            p.fill_rounded_rect({q.x - r * 2, q.y - r * 2, r * 4, r * 4}, r * 2,
                                gf::Color::rgba(255, 236, 200, 46));
            p.fill_rounded_rect({q.x - r, q.y - r, 2 * r, 2 * r}, r,
                                gf::Color::rgba(70, 14, 24, 230));
            p.stroke_rounded_rect({q.x - r, q.y - r, 2 * r, 2 * r}, r,
                                  gf::Color::rgba(255, 226, 190, 220), 1.2);
        }
        if (celebrate >= 0) {
            const double r = board_.width * (.1 + celebrate * .8);
            p.stroke_rounded_rect(
                {center.x - r, center.y - r, 2 * r, 2 * r}, r,
                gf::Color::rgba(255, 220, 140, static_cast<unsigned char>(200 * (1 - celebrate))),
                3);
        }
        const double radius = untangle_radius();
        struct PegPainter {
            gf::Painter& p;
            double radius, t;
            void operator()(const PuzzleGame& game, int i, gf::Point q, bool held, bool hot,
                            double jiggle) const {
                q.x += jiggle;
                const double r = radius * (held ? 1.2 : hot ? 1.1 : 1);
                const gf::Rect head{q.x - r, q.y - r, 2 * r, 2 * r};
                p.draw_box_shadow(head, r, {0, held ? 7.0 : 3.0}, held ? 14 : 7, 0,
                                  gf::Color::rgba(30, 4, 16, held ? 150 : 110));
                const bool frozen = game.untangle_frozen(i);
                const gf::GradientStop wood[] = {{0, gf::Color::rgba(252, 226, 180)},
                                                 {.6, gf::Color::rgba(206, 150, 92)},
                                                 {1, gf::Color::rgba(122, 76, 40)}};
                const gf::GradientStop ice[] = {{0, gf::Color::rgba(250, 254, 255)},
                                                {.6, gf::Color::rgba(176, 218, 246)},
                                                {1, gf::Color::rgba(92, 146, 204)}};
                p.save();
                p.clip_rounded_rect(head, r);
                p.fill_radial_gradient(head, {q.x - r * .35, q.y - r * .4}, {r * 1.6, r * 1.6},
                                       frozen ? ice : wood);
                p.restore();
                p.stroke_rounded_rect(
                    head, r, frozen ? gf::Color::rgba(60, 110, 170) : gf::Color::rgba(90, 52, 26),
                    1.3);
                if (frozen) {
                    // A frost star, slowly glinting.
                    for (int k = 0; k < 3; ++k) {
                        const double a = k * 1.0472 + t * .2;
                        p.draw_line({q.x - std::cos(a) * r * .6, q.y - std::sin(a) * r * .6},
                                    {q.x + std::cos(a) * r * .6, q.y + std::sin(a) * r * .6},
                                    gf::Color::rgba(255, 255, 255, 230), 1.4);
                    }
                    const double glint = .5 + .5 * std::sin(t * 3 + i);
                    p.stroke_rounded_rect(
                        {q.x - r - 3, q.y - r - 3, 2 * r + 6, 2 * r + 6}, r + 3,
                        gf::Color::rgba(200, 236, 255, static_cast<unsigned char>(60 + 90 * glint)),
                        1.5);
                } else {
                    // The brass pin the yarn is tied to.
                    const double pin = r * .32;
                    p.fill_rounded_rect({q.x - pin, q.y - pin, 2 * pin, 2 * pin}, pin,
                                        gf::Color::rgba(118, 84, 30));
                    p.fill_rounded_rect({q.x - pin * .55, q.y - pin * .7, pin, pin}, pin * .5,
                                        gf::Color::rgba(255, 228, 150));
                }
                if (hot && !held)
                    p.stroke_rounded_rect({q.x - r - 4, q.y - r - 4, 2 * r + 8, 2 * r + 8}, r + 4,
                                          gf::Color::rgba(255, 232, 170, 150), 2);
            }
        };
        const PegPainter peg{p, radius, t};
        for (int i = 0; i < static_cast<int>(positions.size()); ++i)
            if (i != drag_) {
                const double jiggle =
                    i == frozen_nudge_ && frozen_nudge_t_ < .45
                        ? std::sin(frozen_nudge_t_ * 50) * 3 * (1 - frozen_nudge_t_ / .45)
                        : 0;
                peg(game, i, at(positions[i]), false, i == hover_, jiggle);
            }
        // Thaw puffs: little bursts of frost melting away.
        for (const ThawPuff& puff : puffs_) {
            const double k = std::clamp(puff.age / .8, 0.0, 1.0);
            const gf::Point q = at(puff.at);
            const double r = radius * (1.2 + k * 2.2);
            p.stroke_rounded_rect(
                {q.x - r, q.y - r, 2 * r, 2 * r}, r,
                gf::Color::rgba(210, 240, 255, static_cast<unsigned char>(220 * (1 - k))),
                2.5 * (1 - k) + .5);
            for (int s = 0; s < 6; ++s) {
                const double a = s * 1.0472 + .3;
                p.fill_rounded_rect(
                    {q.x + std::cos(a) * r - 2, q.y + std::sin(a) * r - 2, 4, 4}, 2,
                    gf::Color::rgba(230, 248, 255, static_cast<unsigned char>(240 * (1 - k))));
            }
        }
        kitten_.paint(p, board_);
        if (drag_ >= 0)
            peg(game, drag_, at(positions[drag_]), true, true, 0);
    }
    if (part < 0 || part == 2) {
        // Status line under the cushion.
        const char* levels[] = {"Easy", "Medium", "Hard"};
        const std::string status =
            game.state.won   ? "Untangled in " + std::to_string(game.state.moves) + " moves"
            : crossings == 1 ? "1 tangle left"
                             : std::to_string(crossings) + " tangles left";
        const gf::FontSpec sf{gf::FontRole::content, 14, 700, false};
        const std::string line = status + "   ·   " + levels[std::clamp(game.state.aux[94], 0, 2)];
        const gf::Size sm = p.measure_text_utf8(line, sf);
        p.draw_text_utf8(
            {board_.x + (board_.width - sm.width) * .5, cushion.y + cushion.height + 24}, line, sf,
            game.state.won ? gf::Color::rgba(255, 224, 150) : gf::Color::rgba(255, 236, 214));
        if (board_.x > 236) {
            const gf::Rect card{std::max(14.0, board_.x - pad - 240), board_.y, 214, 250};
            p.draw_box_shadow(card, 8, {0, 4}, 12, 0, gf::Color::rgba(0, 0, 0, 110));
            fill_vertical(p, card, gf::Color::rgba(252, 242, 222, 245),
                          gf::Color::rgba(236, 220, 192, 245));
            p.stroke_rounded_rect(card, 8, gf::Color::rgba(150, 100, 60, 120), 1);
            const gf::FontSpec caps{gf::FontRole::content, 11, 700, false, 1.2};
            p.draw_text_utf8({card.x + 14, card.y + 24}, "TIDY THE YARN", caps,
                             gf::Color::rgba(150, 60, 70));
            const gf::FontSpec body{gf::FontRole::content, 13, 400, false};
            const gf::Color ink2 = gf::Color::rgba(70, 44, 30), soft = gf::Color::rgba(124, 92, 66);
            p.draw_text_utf8({card.x + 14, card.y + 50}, "Drag the pegs until no", body, ink2);
            p.draw_text_utf8({card.x + 14, card.y + 68}, "thread crosses its own color.", body,
                             ink2);
            double y = card.y + 98;
            if (layers > 1) {
                p.draw_text_utf8({card.x + 14, y}, "Different colors may cross.", body, soft);
                for (int k = 0; k < layers; ++k)
                    p.draw_line({card.x + 14 + k * 30.0, y + 14}, {card.x + 38 + k * 30.0, y + 14},
                                yarn[k], 5);
                y += 40;
            }
            if (game.state.aux[94] > 0) {
                p.draw_text_utf8({card.x + 14, y}, "Frosted pegs stay put until", body, soft);
                p.draw_text_utf8({card.x + 14, y + 18}, "all their threads are clear.", body, soft);
                y += 46;
            }
            p.draw_text_utf8({card.x + 14, y}, "Click the cat to shoo it.", body, soft);
            if (game.state.aux[94] > 0)
                p.draw_text_utf8({card.x + 14, y + 18}, "Leave it idle and it swats.", body, soft);
        }
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
// Puzzle Solve: a glazier's bench. The design is pinned to the plaster at the left (above
// the pieces when narrow), the steel tray holds the frame, and the pieces lie loose at
// the right, all at one scale so their sizes compare truly. Cells are a whole number of
// device pixels, so every edge lands on a pixel at any display scale.
void PuzzleView::layout_solve(gf::Rect b) {
    const double scale = attached_window() ? (*attached_window()).scale() : 1.0;
    solve_scale_ = scale > 0 ? scale : 1.0;
    const int columns = game.solve_columns(), rows = game.solve_rows();
    const double top = 62, bottom = b.height - 30, gap = std::clamp(b.width * .02, 10.0, 22.0);
    const double avail = std::max(120.0, bottom - top);
    const bool wide = b.width >= 820;
    const double left_column = wide ? std::clamp(b.width * .17, 150.0, 210.0) : 0;
    const double right_column =
        wide ? std::clamp(b.width * .25, 200.0, 320.0) : std::clamp(b.width * .4, 190.0, 300.0);
    const double area_x = wide ? gap + left_column + gap : gap;
    const double area_w = b.width - area_x - right_column - 2 * gap;
    solve_border_ = std::clamp(std::min(area_w, avail) * .04, 11.0, 22.0);
    double unit = std::min((area_w - 2 * solve_border_) / columns,
                           (avail - 2 * solve_border_) / rows);
    unit = std::floor(std::clamp(unit, 16.0, 150.0) * solve_scale_) / solve_scale_;
    const double snap = 1 / solve_scale_;
    const double board_x = area_x + (area_w - unit * columns) * .5,
                 board_y = top + (avail - unit * rows) * .5;
    board_ = {std::round(board_x / snap) * snap, std::round(board_y / snap) * snap,
              unit * columns, unit * rows};
    const double right_x = board_.x + board_.width + solve_border_ + gap;
    const double right_w = b.width - right_x - gap;
    if (wide) {
        target_ = {gap, top, left_column, std::min(avail, left_column * 1.45)};
        tray_ = {right_x, top, right_w, avail};
    } else {
        const double design = std::min(avail * .24, right_w * .5);
        target_ = {right_x, top, right_w, design + 48};
        tray_ = {right_x, target_.y + target_.height + 8, right_w,
                 bottom - target_.y - target_.height - 8};
    }
    // The design keeps the frame's proportions inside its card.
    const double picture_w = target_.width - 24,
                 picture_h = target_.height - 24 - (wide ? 58 : 46);
    const double picture_unit =
        std::floor(std::max(4.0, std::min(picture_w / columns, picture_h / rows)) * solve_scale_) /
        solve_scale_;
    solve_design_ = {std::round((target_.x + (target_.width - picture_unit * columns) * .5) / snap) * snap,
                     std::round((target_.y + 28) / snap) * snap, picture_unit * columns,
                     picture_unit * rows};
    // Pieces lie in rows, each in a square as wide as its longest side so turning it never
    // moves another piece. The largest common scale that fits wins, never larger than the
    // frame's own.
    const int count = game.piece_count();
    std::vector<int> spans;
    for (int piece = 0; piece < count; ++piece) {
        int span = 1;
        for (const PieceCell& c : game.piece_cells(piece, 0, false))
            span = std::max({span, c.x + 1, c.y + 1});
        spans.push_back(span);
    }
    const double inner_x = tray_.x + 8, inner_w = tray_.width - 16, inner_y = tray_.y + 26,
                 inner_h = tray_.height - 26 - 22;
    double tray_unit = std::min(unit, 80.0);
    for (; tray_unit > 6; tray_unit -= .5) {
        const double pad = std::clamp(tray_unit * .3, 6.0, 14.0);
        double x = 0, y = 0, row = 0;
        bool fits = true;
        for (int span : spans) {
            const double side = span * tray_unit + pad;
            if (x > 0 && x + side > inner_w) {
                x = 0;
                y += row;
                row = 0;
            }
            fits = fits && side <= inner_w;
            x += side;
            row = std::max(row, side);
        }
        if (fits && y + row <= inner_h)
            break;
    }
    solve_tray_unit_ = std::floor(tray_unit * solve_scale_) / solve_scale_;
    const double pad = std::clamp(solve_tray_unit_ * .3, 6.0, 14.0);
    cells_.clear();
    std::vector<int> row_of;
    std::vector<double> row_width;
    double x = 0, y = 0, row = 0;
    for (int span : spans) {
        const double side = span * solve_tray_unit_ + pad;
        if (x > 0 && x + side > inner_w) {
            x = 0;
            y += row;
            row = 0;
            row_width.push_back(0);
        }
        if (row_width.empty())
            row_width.push_back(0);
        cells_.push_back({inner_x + x, inner_y + y, side, side});
        row_of.push_back(static_cast<int>(row_width.size()) - 1);
        x += side;
        row = std::max(row, side);
        row_width.back() = x;
    }
    // Center each row.
    for (std::size_t i = 0; i < cells_.size(); ++i)
        cells_[i].x += (inner_w - row_width[static_cast<std::size_t>(row_of[i])]) * .5;
    solve_prepare();
}
double PuzzleView::solve_unit() const {
    return board_.width / std::max(1, game.solve_columns());
}
// The atom of the frame under a point, or -1.
int PuzzleView::solve_atom_at(gf::Point local) const {
    const int columns = game.solve_columns(), rows = game.solve_rows();
    const double unit = solve_unit();
    const double fx = (local.x - board_.x) / unit, fy = (local.y - board_.y) / unit;
    if (fx < 0 || fy < 0 || fx >= columns || fy >= rows)
        return -1;
    const int x = static_cast<int>(fx), y = static_cast<int>(fy);
    const double u = fx - x, v = fy - y;
    const int wedge = v < u ? (v < 1 - u ? 0 : 1) : (v < 1 - u ? 3 : 2);
    return (y * columns + x) * 4 + wedge;
}
// A placed piece's atoms, normalized, and the cell where its corner sits.
std::vector<PieceCell> PuzzleView::solve_placed(int piece, int& x, int& y) const {
    const int columns = game.solve_columns();
    std::vector<PieceCell> cells;
    x = y = 1000;
    for (int i = 0; i < game.solve_atoms(); ++i)
        if (game.state.grid[i] / 4 == piece + 1) {
            const PieceCell c{(i / 4) % columns, i / 4 / columns, game.state.grid[i] % 4, i % 4};
            cells.push_back(c);
            x = std::min(x, c.x);
            y = std::min(y, c.y);
        }
    for (PieceCell& c : cells) {
        c.x -= x;
        c.y -= y;
    }
    return cells;
}
void PuzzleView::solve_upload(SolveSprite& sprite, const std::vector<int>& key,
                              const GlassImage& image, double margin) {
    gf::Window& window = *attached_window();
    if (image.width <= 0 || image.height <= 0)
        return;
    const std::uint32_t width = static_cast<std::uint32_t>(image.width),
                        height = static_cast<std::uint32_t>(image.height);
    gf::ImageLoadResult result =
        sprite.image.value
            ? window.replace_bgra32_premultiplied(sprite.image, width, height, width * 4U,
                                                  image.pixels, *this)
            : window.load_bgra32_premultiplied(width, height, width * 4U, image.pixels);
    if (!result)
        return;
    sprite.image = result.image;
    sprite.key = key;
    sprite.margin = margin;
    sprite.width = image.width;
    sprite.height = image.height;
}
void PuzzleView::solve_glass(SolveSprite& sprite, const std::vector<PieceCell>& cells,
                             double unit, GlassLook look) {
    const int device_unit = static_cast<int>(std::lround(unit * solve_scale_));
    std::vector<int> key{static_cast<int>(look), device_unit,
                         static_cast<int>(std::lround(solve_scale_ * 100))};
    for (const PieceCell& c : cells)
        key.push_back(((c.y * 8 + c.x) * 4 + c.wedge) * 4 + c.color);
    if (sprite.image.value && sprite.key == key)
        return;
    GlassPiece piece;
    piece.cells = cells;
    piece.unit = device_unit;
    piece.scale = solve_scale_;
    piece.look = look;
    piece.margin = glass_margin(look, piece.unit, solve_scale_);
    GlassImage image;
    render_glass(piece, image);
    solve_upload(sprite, key, image, piece.margin);
}
void PuzzleView::solve_prepare() {
    if (game.kind != PuzzleKind::solve || !attached_window() || board_.width <= 0)
        return;
    const double scale = (*attached_window()).scale() > 0 ? (*attached_window()).scale() : 1.0;
    if (scale != solve_scale_) {
        // A new display scale changes every cell's pixel size: lay out again.
        solve_scale_ = scale;
        layout_solve(client_rectangle());
        return;
    }
    const int columns = game.solve_columns(), rows = game.solve_rows();
    const int scale_key = static_cast<int>(std::lround(solve_scale_ * 100));
    const double unit = solve_unit();
    // Plaster wall: one seamless tile at the display's own pixels.
    if (!solve_plaster_.image.value || solve_plaster_.key != std::vector<int>{scale_key}) {
        GlassImage tile;
        render_plaster(static_cast<int>(std::lround(192 * solve_scale_)), solve_scale_, tile);
        solve_upload(solve_plaster_, {scale_key}, tile, 0);
    }
    // The steel tray and its well.
    const std::vector<Point2> frame = game.solve_frame();
    const int device_unit = static_cast<int>(std::lround(unit * solve_scale_));
    const int border = static_cast<int>(std::lround(solve_border_ * solve_scale_));
    const int margin = static_cast<int>(std::lround(10 * solve_scale_));
    std::vector<int> frame_key{columns, rows, device_unit, border, margin, scale_key};
    for (Point2 q : frame) {
        frame_key.push_back(static_cast<int>(std::lround(q.x * 2)));
        frame_key.push_back(static_cast<int>(std::lround(q.y * 2)));
    }
    if (!solve_frame_sprite_.image.value || solve_frame_sprite_.key != frame_key) {
        SolveTrayArt art;
        art.outline = frame;
        art.columns = columns;
        art.rows = rows;
        art.unit = device_unit;
        art.scale = solve_scale_;
        art.border = border;
        art.margin = margin;
        GlassImage image;
        render_solve_tray(art, image);
        solve_upload(solve_frame_sprite_, frame_key, image, margin + border);
    }
    // The design.
    const int design_unit =
        static_cast<int>(std::lround(solve_design_.width / columns * solve_scale_));
    const int design_margin = static_cast<int>(std::lround(3 * solve_scale_));
    std::vector<int> design_key{columns, rows, design_unit, design_margin, scale_key};
    for (int i = 0; i < game.solve_atoms(); ++i)
        design_key.push_back(game.state.secret[i]);
    if (design_unit > 0 &&
        (!solve_design_sprite_.image.value || solve_design_sprite_.key != design_key)) {
        SolveDesignArt art;
        art.picture = game.state.secret;
        art.outline = frame;
        art.columns = columns;
        art.rows = rows;
        art.unit = design_unit;
        art.scale = solve_scale_;
        art.margin = design_margin;
        GlassImage image;
        render_solve_design(art, image);
        solve_upload(solve_design_sprite_, design_key, image, design_margin);
    }
    // Pieces in the frame, in the tray, and in hand.
    const int count = game.piece_count();
    for (int piece = 0; piece < count && piece < 12; ++piece) {
        int x = 0, y = 0;
        const std::vector<PieceCell> placed = solve_placed(piece, x, y);
        solve_board_origin_[static_cast<std::size_t>(piece)] = {x * unit, y * unit};
        if (!placed.empty())
            solve_glass(solve_board_sprites_[static_cast<std::size_t>(piece)], placed, unit,
                        GlassLook::placed);
        const bool chosen = piece == selection_;
        const std::vector<PieceCell> shape = game.piece_cells(
            piece, chosen ? rotation_ : piece_rotation_[static_cast<std::size_t>(piece)],
            chosen ? flip_ : piece_flip_[static_cast<std::size_t>(piece)]);
        solve_glass(solve_tray_sprites_[static_cast<std::size_t>(piece)], shape, solve_tray_unit_,
                    placed.empty() ? GlassLook::tray : GlassLook::used);
    }
    if (drag_ >= 0) {
        const std::vector<PieceCell> shape = game.piece_cells(selection_, rotation_, flip_);
        solve_glass(solve_lift_sprite_, shape, unit, GlassLook::lifted);
        solve_glass(solve_ghost_sprite_, shape, unit, GlassLook::placed);
    }
}
void PuzzleView::solve_draw(gf::Painter& p, const SolveSprite& sprite, double x, double y,
                            double opacity) const {
    if (!sprite.image.value)
        return;
    const double left = std::round(x * solve_scale_) - sprite.margin,
                 top = std::round(y * solve_scale_) - sprite.margin;
    p.draw_image(sprite.image,
                 {left / solve_scale_, top / solve_scale_, sprite.width / solve_scale_,
                  sprite.height / solve_scale_},
                 opacity);
}
void PuzzleView::solve_release_images(gf::Window& window) {
    for (SolveSprite& sprite : solve_board_sprites_)
        if (sprite.image.value)
            static_cast<void>(window.remove_image(sprite.image));
    for (SolveSprite& sprite : solve_tray_sprites_)
        if (sprite.image.value)
            static_cast<void>(window.remove_image(sprite.image));
    for (SolveSprite* sprite : {&solve_lift_sprite_, &solve_ghost_sprite_, &solve_frame_sprite_,
                                &solve_design_sprite_, &solve_plaster_})
        if ((*sprite).image.value)
            static_cast<void>(window.remove_image((*sprite).image));
    solve_board_sprites_ = {};
    solve_tray_sprites_ = {};
    solve_lift_sprite_ = solve_ghost_sprite_ = solve_frame_sprite_ = solve_design_sprite_ =
        solve_plaster_ = SolveSprite{};
}
void PuzzleView::paint_solve(gf::Painter& p) {
    const gf::Rect b = client_rectangle();
    const double unit = solve_unit();
    const int columns = game.solve_columns(), count = game.piece_count();
    // The wall: lime plaster, warmer where the lamp falls on the bench.
    p.fill_rect(b, gf::Color::rgba(214, 211, 204));
    if (solve_plaster_.image.value)
        p.fill_image_pattern(solve_plaster_.image, {solve_plaster_.width, solve_plaster_.height}, b,
                             {solve_plaster_.width / solve_scale_,
                              solve_plaster_.height / solve_scale_});
    const gf::GradientStop lamp[] = {{0, gf::Color::rgba(255, 246, 226, 60)},
                                     {.6, gf::Color::rgba(255, 246, 226, 0)},
                                     {1, gf::Color::rgba(40, 34, 26, 70)}};
    p.fill_radial_gradient(b, {board_.x + board_.width * .5, board_.y + board_.height * .45},
                           {b.width * .75, b.height * .9}, lamp);
    const double celebrate = game.state.won && animation_duration_ > 0
                                 ? std::clamp(elapsed() / animation_duration_, 0.0, 1.0)
                                 : -1;
    const gf::Rect outer{board_.x - solve_border_, board_.y - solve_border_,
                         board_.width + 2 * solve_border_, board_.height + 2 * solve_border_};
    if (celebrate >= 0 || game.state.won)
        p.draw_box_shadow(outer, 8, {0, 0}, 26, 4,
                          gf::Color::rgba(255, 214, 120,
                                          static_cast<unsigned char>(
                                              game.state.won && celebrate < 0
                                                  ? 110
                                                  : 110 + 120 * std::sin(celebrate * 3.14159))));
    if (solve_frame_sprite_.image.value)
        solve_draw(p, solve_frame_sprite_, board_.x, board_.y);
    else {
        p.fill_rect(outer, gf::Color::rgba(170, 176, 186));
        p.fill_rect(board_, gf::Color::rgba(196, 198, 200));
    }
    for (int piece = 0; piece < count && piece < 12; ++piece) {
        if (piece == drag_)
            continue;
        int x = 0, y = 0;
        const std::vector<PieceCell> shape = solve_placed(piece, x, y);
        if (shape.empty())
            continue;
        const SolveSprite& sprite = solve_board_sprites_[static_cast<std::size_t>(piece)];
        if (sprite.image.value)
            solve_draw(p, sprite, board_.x + x * unit, board_.y + y * unit);
        else
            draw_piece(p, shape, board_.x + x * unit, board_.y + y * unit, unit, PieceLook::placed);
    }
    if (drag_ >= 0 && !game.state.over && in_rect(board_, solve_pointer_)) {
        int sx = 0, sy = 0;
        const bool valid = solve_snap(sx, sy);
        if (valid && solve_ghost_sprite_.image.value)
            solve_draw(p, solve_ghost_sprite_, board_.x + sx * unit, board_.y + sy * unit, .5);
        else {
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
    }
    // The design, pinned to the wall on a card.
    const gf::Color ink = gf::Color::rgba(46, 52, 62), soft_ink = gf::Color::rgba(88, 94, 104);
    const gf::FontSpec caps{gf::FontRole::content, 11, 700, false, 1.2};
    const double card_w = std::max(solve_design_.width + 20, std::min(target_.width, 220.0));
    const gf::Rect card{solve_design_.x + solve_design_.width * .5 - card_w * .5, target_.y, card_w,
                        solve_design_.height + 28 + 54.0};
    p.draw_box_shadow(card, 4, {1, 3}, 8, 0, gf::Color::rgba(40, 32, 20, 70));
    fill_vertical(p, card, gf::Color::rgba(250, 248, 242), gf::Color::rgba(236, 232, 222));
    p.stroke_rounded_rect(card, 4, gf::Color::rgba(160, 150, 130, 140), 1);
    p.draw_text_utf8({card.x + 10, card.y + 18}, "THE DESIGN", caps, soft_ink);
    if (solve_design_sprite_.image.value)
        solve_draw(p, solve_design_sprite_, solve_design_.x, solve_design_.y);
    int placed = 0;
    for (int piece = 0; piece < count; ++piece)
        for (int i = 0; i < game.solve_atoms(); ++i)
            if (game.state.grid[i] / 4 == piece + 1) {
                ++placed;
                break;
            }
    const std::string progress =
        game.state.won ? "Solved in " + std::to_string(game.state.moves) + " moves"
                       : std::to_string(placed) + " of " + std::to_string(count) + " placed";
    const double text_y = solve_design_.y + solve_design_.height + 20;
    p.draw_text_utf8({card.x + 10, text_y}, progress, {gf::FontRole::content, 13, 700, false},
                     game.state.won ? gf::Color::rgba(168, 98, 10) : ink);
    const char* levels[] = {"Easy", "Medium", "Hard"};
    const char* lessons[] = {"turning", "mirroring", "slanted cuts"};
    const int lesson = game.solve_lesson();
    const std::string level = std::string(levels[std::clamp(game.solve_level(), 0, 2)]) +
                              (lesson >= 0 ? std::string(" · ") + lessons[lesson]
                               : game.solve_family() == 2 ? " · tangram"
                                                          : "");
    p.draw_text_utf8({card.x + 10, text_y + 18}, level, {gf::FontRole::content, 12, 400, false},
                     soft_ink);
    // The pieces.
    p.draw_text_utf8({tray_.x + 10, tray_.y + 16}, "PIECES", caps, soft_ink);
    for (int piece = 0; piece < count && piece < static_cast<int>(cells_.size()); ++piece) {
        const gf::Rect r = cells_[static_cast<std::size_t>(piece)];
        const bool chosen = piece == selection_;
        if (chosen) {
            const gf::Rect plate{r.x + 2, r.y + 2, r.width - 4, r.height - 4};
            p.fill_rounded_rect(plate, 6, gf::Color::rgba(255, 252, 240, 120));
            p.stroke_rounded_rect(plate, 6, gf::Color::rgba(196, 128, 24, 220), 1.5);
        }
        if (piece == drag_)
            continue;
        const std::vector<PieceCell> shape = game.piece_cells(
            piece, chosen ? rotation_ : piece_rotation_[static_cast<std::size_t>(piece)],
            chosen ? flip_ : piece_flip_[static_cast<std::size_t>(piece)]);
        int mx = 0, my = 0;
        for (const PieceCell& c : shape) {
            mx = std::max(mx, c.x + 1);
            my = std::max(my, c.y + 1);
        }
        const double u = solve_tray_unit_;
        const double px = r.x + (r.width - mx * u) * .5, py = r.y + (r.height - my * u) * .5;
        const SolveSprite& sprite = solve_tray_sprites_[static_cast<std::size_t>(piece)];
        bool used = false;
        for (int i = 0; i < game.solve_atoms() && !used; ++i)
            used = game.state.grid[i] / 4 == piece + 1;
        if (sprite.image.value)
            solve_draw(p, sprite, px, py);
        else
            draw_piece(p, shape, px, py, u, used ? PieceLook::used : PieceLook::tray);
    }
    p.draw_text_utf8({tray_.x + 10, tray_.y + tray_.height - 6},
                     tray_.width > 230 ? "Drag to the frame · R turns · F flips"
                                       : "R turns · F flips",
                     {gf::FontRole::content, 11, 400, false}, soft_ink);
    if (drag_ >= 0) {
        // The lifted piece rides under the pointer at frame scale, above everything else.
        const double x = solve_pointer_.x - grab_.x * unit, y = solve_pointer_.y - grab_.y * unit;
        if (solve_lift_sprite_.image.value)
            solve_draw(p, solve_lift_sprite_, x, y);
        else
            draw_piece(p, game.piece_cells(selection_, rotation_, flip_), x, y, unit,
                       PieceLook::lifted);
    }
    if (celebrate >= 0) {
        // Sparkles drift around the frame while the solved picture glows.
        for (int i = 0; i < 14; ++i) {
            const double a = i * 2.39996 + celebrate * 2.2,
                         rr = std::max(outer.width, outer.height) *
                              (.52 + .06 * std::sin(i * 1.7 + celebrate * 6));
            const gf::Point c{outer.x + outer.width * .5 + std::cos(a) * rr * outer.width /
                                                             std::max(outer.width, outer.height),
                              outer.y + outer.height * .5 + std::sin(a) * rr * outer.height /
                                                              std::max(outer.width, outer.height)};
            const double life = std::sin(std::clamp(celebrate * 1.3 - i * .03, 0.0, 1.0) * 3.14159);
            sparkle(p, c, 4 + 9 * life,
                    gf::Color::rgba(255, 228, 140, static_cast<unsigned char>(230 * life)));
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
    static_cast<void>(columns);
}
bool PuzzleView::scripted_action(std::string_view requested) {
    if (!development || game.kind != PuzzleKind::solve || requested.size() > 64)
        return false;
    std::istringstream in{std::string(requested)};
    std::string verb;
    in >> verb;
    if (verb == "deal") {
        int level = 1;
        std::uint32_t seed = 1;
        in >> level >> seed;
        if (!in || level < 0 || level > 2)
            return false;
        game.level = level;
        new_game();
        game.deal(seed);
        layout_solve(client_rectangle());
        persist();
    } else if (verb == "witness") {
        int pieces = 0;
        in >> pieces;
        if (!in || pieces < 0 || pieces > game.piece_count())
            return false;
        for (int k = 0; k < pieces; ++k) {
            const std::vector<int>& w = game.state.solution_paths[static_cast<std::size_t>(k)];
            static_cast<void>(game.place_piece(w[0], w[1], w[2], w[3], w[4]));
        }
        changed("puzzle_solve_place");
    } else if (verb == "drag") {
        int piece = 0;
        double x = 0, y = 0;
        in >> piece >> x >> y;
        if (!in || piece < 0 || piece >= game.piece_count() || game.state.over)
            return false;
        solve_select(piece);
        static_cast<void>(game.remove_piece(piece));
        lifted_ = false;
        drag_ = piece;
        solve_recenter();
        solve_pointer_ = {board_.x + x * solve_unit(), board_.y + y * solve_unit()};
    } else
        return false;
    invalidate_scene();
    return true;
}
void PuzzleView::solve_select(int piece) {
    if (piece == selection_)
        return;
    piece_rotation_[static_cast<std::size_t>(selection_)] = rotation_;
    piece_flip_[static_cast<std::size_t>(selection_)] = flip_;
    selection_ = piece;
    rotation_ = piece_rotation_[static_cast<std::size_t>(piece)];
    flip_ = piece_flip_[static_cast<std::size_t>(piece)];
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
    const double unit = solve_unit();
    const int columns = game.solve_columns(), rows = game.solve_rows();
    x = static_cast<int>(std::lround((solve_pointer_.x - board_.x) / unit - grab_.x));
    y = static_cast<int>(std::lround((solve_pointer_.y - board_.y) / unit - grab_.y));
    for (const PieceCell& c : game.piece_cells(selection_, rotation_, flip_)) {
        const int xx = x + c.x, yy = y + c.y;
        if (xx < 0 || yy < 0 || xx >= columns || yy >= rows)
            return false;
        const int atom = (yy * columns + xx) * 4 + c.wedge;
        if (!game.solve_inside(atom))
            return false;
        const int value = game.state.grid[atom];
        if (value && value / 4 != selection_ + 1)
            return false;
    }
    return true;
}
// Recovers the orientation and origin of a placed piece, then removes it so it can be carried.
bool PuzzleView::solve_lift(int piece) {
    const int columns = game.solve_columns(), rows = game.solve_rows();
    int minx = 0, miny = 0;
    const std::vector<PieceCell> placed = solve_placed(piece, minx, miny);
    if (placed.empty())
        return false;
    for (int orientation = 0; orientation < 8; ++orientation) {
        std::vector<PieceCell> cells = game.piece_cells(piece, orientation % 4, orientation >= 4);
        if (cells.size() != placed.size())
            continue;
        bool match = true;
        for (const PieceCell& c : cells) {
            const int i = ((miny + c.y) * columns + minx + c.x) * 4 + c.wedge;
            if (minx + c.x >= columns || miny + c.y >= rows ||
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
        const double unit = solve_unit();
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
        lines = {"Click a gem, then a neighbor, to swap them; or drag the gem itself.",
                 "A swap that makes no match slides back. Wait and a pair will nudge.",
                 "Four in a line makes a colored bomb. Matching it clears a 3 × 3 area.",
                 "A T or L match makes a star. Matching it clears its row and column.",
                 "Five in a line makes a hypercube: swap it with a color to clear that color.",
                 "Blasts trigger other specials. Cascades refill the board from above.",
                 "Additional colors arrive as you progress. No available swap ends the run."};
    else if (game.kind == PuzzleKind::cube)
        lines = {"Connect every colored pair on the three visible faces.",
                 "Easy has five pairs; Medium seven and mossy stones; Hard nine pairs.",
                 "Paths cannot share a square or cross a stone.",
                 "Hard also asks you to fill every open square.",
                 "Move the mouse to tilt the cube; it holds still while you trace.",
                 "Click an endpoint to redraw that pair; trace backward to erase.",
                 "Next: in the bar sets the level. Every board has a full solution."};
    else if (game.kind == PuzzleKind::untangle)
        lines = {"Drag the pegs until no thread crosses another of its own color.",
                 "Threads of different colors may cross. Snags mark the tangles left.",
                 "Frosted pegs cannot move until every thread tied to them is clear.",
                 "Keep pegs apart; a peg sitting on a thread counts as a tangle.",
                 "The cat sits on pegs and chases what you drag. Click it to shoo it.",
                 "On Medium and Hard, leave the yarn alone and the cat swats a peg.",
                 "Next: in the bar sets the level. Every puzzle was tied from a solution."};
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
        lines = {"Rebuild the blue and yellow design using every piece.",
                 "Drag a piece into the frame; it snaps into place.",
                 "While dragging, R or a right click turns it and F mirrors it.",
                 "Click the chosen piece to turn it before you drag.",
                 "Drag placed pieces to move them. Right click one to return it.",
                 "Each design is cut from a real tiling, so a solution always exists;",
                 "any arrangement with the same colors also counts."};
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
    if (scene_parts_[0] && !panel_)
        return;
    gf::Rect b = client_rectangle();
    if (surface_) {
        p.draw_live_surface(surface_, b);
    } else {
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
            const char* levels[] = {"Easy", "Medium", "Hard"};
            const int level = std::clamp(game.state.aux[94], 0, 2);
            text(p, 28, 114, "Let each color find home", 17, ink);
            text(p, 28, 145,
                 std::string(levels[level]) + "  ·  " + std::to_string(game.cube_pairs()) +
                     " pairs",
                 13, ink);
            text(p, 28, 174, "Trace from a colored square;", 13, ink);
            text(p, 28, 203, level ? "paths go around the stones." : "paths may cross faces.", 13,
                 ink);
            if (game.cube_fill()) {
                const int open = game.cube_open_tiles();
                text(p, 28, 232,
                     open ? "Fill every tile: " + std::to_string(open) + " left"
                          : "Every tile filled",
                     13, open ? gf::Color::rgba(255, 222, 150) : ink);
            } else
                text(p, 28, 232, "Move the mouse to tilt it.", 13, ink);
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
        // Gems, Untangle and Puzzle Solve explain themselves in help: no commentary line.
        if (game.kind != PuzzleKind::solve && game.kind != PuzzleKind::gems &&
            game.kind != PuzzleKind::untangle) {
            text(p, 17, b.height - 11, game.message, 13, gf::Color::rgba(0, 0, 0, 120));
            text(p, 16, b.height - 12, game.message, 13, ink);
        }
    }
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
    if (kind == 1 && games::route_help(*this))
        return;
    panel_ = kind;
    refresh_scene();
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
    invalidate_scene();
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
    refresh_scene();
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
    if (game.kind == PuzzleKind::cube)
        request_render();
    else
        render();
    invalidate_scene();
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
    if (game.kind == PuzzleKind::solve)
        layout_solve(client_rectangle()); // a new frame and new pieces
    gem_pick_ = gem_spring_ = gem_hint_ = gem_hint_to_ = -1;
    gem_dragging_ = false;
    gem_offset_ = {};
    gem_idle_ = 0;
    effects_.clear();
    untangle_idle_ = 0;
    swat_peg_ = frozen_nudge_ = -1;
    puffs_.clear();
    kitten_.reset(game.state.seed);
    if (game.kind == PuzzleKind::gems && !reduced_) {
        intro_ = true;
        intro_start_ = gf::FrameClock::now();
        if (timer_)
            (*timer_).start();
    }
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
std::vector<GameSetting> PuzzleView::settings() const {
    if (game.kind != PuzzleKind::cube && game.kind != PuzzleKind::untangle &&
        game.kind != PuzzleKind::solve)
        return {};
    return {{"level", "Level", GameSetting::Kind::choice,
             static_cast<double>(std::clamp(game.level, 0, 2)), {"Easy", "Medium", "Hard"}, 0, 2,
             1, "An untouched board is dealt again; otherwise the next game uses it."}};
}
void PuzzleView::change_setting(std::string_view id, double value) {
    const int level = static_cast<int>(std::lround(value));
    if (id != "level" || level < 0 || level > 2 || level == game.level || settings().empty())
        return;
    // One step short, then the command's own step: it re-deals an untouched board once.
    game.level = (level + 2) % 3;
    run_command("level");
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
    else if (id == "level") {
        game.level = (game.level + 1) % 3;
        // A board nobody has touched yet is simply re-dealt at the new level.
        if (game.state.moves == 0 && !game.state.over)
            new_game();
        else
            game.message = "The next game will be " +
                           std::string(game.level == 0   ? "Easy"
                                       : game.level == 1 ? "Medium"
                                                         : "Hard") +
                           ".";
    }
    invalidate_scene();
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
    invalidate_scene();
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
        for (int i = 0; i < static_cast<int>(game.state.nodes.size()); ++i)
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
void PuzzleView::gem_swap(int a, int b, double from) {
    before_swap_ = game.state.grid;
    cascade_step_ = -1;
    swap_a_ = a;
    swap_b_ = b;
    swap_from_ = std::clamp(from, 0.0, .95);
    gem_pick_ = gem_spring_ = gem_hint_ = gem_hint_to_ = -1;
    gem_offset_ = {};
    gem_dragging_ = false;
    gem_idle_ = 0;
    invalid_swap_ = !game.gem_swap(a, b);
    animation_start_ = gf::FrameClock::now();
    animation_duration_ = reduced_        ? 0
                          : invalid_swap_ ? .46
                                          : .23 + .24 * (game.cascade_frames.size() - 1);
    if (timer_)
        (*timer_).start();
    changed(invalid_swap_ ? "gems_swap_return" : "gems_swap");
}
void PuzzleView::gem_drag(gf::Point local) {
    const double cell = board_.width / 8;
    const double dx = (local.x - gem_press_.x) / cell, dy = (local.y - gem_press_.y) / cell;
    if (!gem_dragging_ && std::hypot(dx, dy) * cell < 6)
        return;
    gem_dragging_ = true;
    gem_pick_ = -1;
    // The gem slides along the stronger axis only, at most one cell, and stops at the rim.
    const int col = drag_ % 8, row = drag_ / 8;
    Point2 o = std::abs(dx) >= std::abs(dy) ? Point2{dx, 0} : Point2{0, dy};
    o.x = std::clamp(o.x, col == 0 ? -.12 : -1.0, col == 7 ? .12 : 1.0);
    o.y = std::clamp(o.y, row == 0 ? -.12 : -1.0, row == 7 ? .12 : 1.0);
    gem_offset_ = o;
    // Pulled most of the way, the swap commits without waiting for release.
    if (std::abs(o.x) + std::abs(o.y) >= .72) {
        const int from = drag_;
        drag_ = -1;
        set_pointer_capture(false);
        gem_swap(from,
                 from +
                     (o.x > 0   ? 1
                      : o.x < 0 ? -1
                                : 0) +
                     (o.y > 0   ? 8
                      : o.y < 0 ? -8
                                : 0),
                 std::abs(o.x) + std::abs(o.y));
        return;
    }
    if (timer_)
        (*timer_).start();
}
void PuzzleView::gem_find_hint() {
    for (int i = 0; i < 64; ++i)
        for (const int d : {1, 8}) {
            const int j = i + d;
            if (j >= 64 || (d == 1 && j / 8 != i / 8))
                continue;
            PuzzleGame trial = game;
            if (trial.gem_swap(i, j)) {
                gem_hint_ = i;
                gem_hint_to_ = j;
                return;
            }
        }
    gem_idle_ = 0; // nothing found; look again later
}
void PuzzleView::untangle_step() {
    step_effects(); // advances the shared frame clock
    const double dt = fx_dt_;
    if (drag_ < 0 && !game.state.over && !panel_)
        untangle_idle_ += dt;
    swat_t_ = std::min(1.0, swat_t_ + dt / .28);
    frozen_nudge_t_ = std::min(1.0, frozen_nudge_t_ + dt);
    for (ThawPuff& puff : puffs_)
        puff.age += dt;
    struct Faded {
        bool operator()(const ThawPuff& puff) const {
            return puff.age > .8;
        }
    };
    std::erase_if(puffs_, Faded{});
    if (panel_)
        return;
    Kitten::Scene scene;
    scene.pegs = &game.state.nodes;
    for (int i = 0; i < static_cast<int>(game.state.nodes.size()); ++i)
        scene.swattable.push_back(!game.untangle_frozen(i) && i != drag_);
    scene.dragged = drag_;
    scene.pointer = drag_ >= 0 ? drag_position_
                               : Point2{(pointer_seen_.x - board_.x) / board_.width,
                                        (pointer_seen_.y - board_.y) / board_.height};
    scene.pointer_inside =
        drag_ >= 0 || (pointer_seen_.x >= board_.x && pointer_seen_.x <= board_.x + board_.width &&
                       pointer_seen_.y >= board_.y && pointer_seen_.y <= board_.y + board_.height);
    scene.solved = game.state.won;
    scene.mischief = game.state.aux[94] > 0;
    scene.reduced = reduced_;
    scene.idle = untangle_idle_;
    const Kitten::Swat swat = kitten_.step(dt, scene);
    if (swat.peg >= 0 && !game.state.over) {
        const std::array<int, 96> marks = game.state.marks;
        swat_from_ = game.state.nodes[swat.peg];
        if (game.move_node(swat.peg, swat.to, false)) {
            swat_peg_ = swat.peg;
            swat_t_ = 0;
            untangle_released(marks);
            changed("untangle_grab");
        }
    }
}
void PuzzleView::untangle_released(const std::array<int, 96>& marks_before) {
    for (int i = 0; i < static_cast<int>(game.state.nodes.size()); ++i)
        if (marks_before[i] == 1 && game.state.marks[i] == 2) {
            puffs_.push_back({game.state.nodes[i], 0});
            sound_play("untangle_edges_clear", sound_);
        }
}
gf::Rect PuzzleView::untangle_board_damage() const {
    // Held heads grow by 20%; the shadow reaches three blur radii plus its offset.
    const double margin = untangle_radius() * 1.2 + 14 * 3 + 7 + 2;
    return {board_.x - margin, board_.y - margin, board_.width + margin * 2,
            board_.height + margin * 2};
}
gf::Rect PuzzleView::untangle_animation_bounds() const {
    gf::Rect damage = kitten_.paint_bounds(board_);
    const double knot = std::clamp(board_.width * .0105, 3.0, 7.0) * 2.1 + 2;
    for (const gf::Point point : untangle_snags_)
        damage = gf::Rect::united(damage, {point.x - knot, point.y - knot, knot * 2, knot * 2});
    const double peg = untangle_radius() + 28;
    for (std::size_t i = 0; i < untangle_positions_.size(); ++i)
        if (game.untangle_frozen(static_cast<int>(i)) ||
            (static_cast<int>(i) == frozen_nudge_ && frozen_nudge_t_ < .45)) {
            const Point2 at = untangle_positions_[i];
            const gf::Point point{board_.x + at.x * board_.width, board_.y + at.y * board_.height};
            damage = gf::Rect::united(damage, {point.x - peg, point.y - peg, peg * 2, peg * 2});
        }
    const double puff_radius = untangle_radius() * 3.4 + 4;
    for (const ThawPuff& puff : puffs_) {
        const gf::Point point{board_.x + puff.at.x * board_.width,
                              board_.y + puff.at.y * board_.height};
        damage = gf::Rect::united(damage, {point.x - puff_radius, point.y - puff_radius,
                                           puff_radius * 2, puff_radius * 2});
    }
    return damage;
}
void PuzzleView::tick() {
    if (!effectively_visible()) {
        if (timer_)
            (*timer_).stop();
        return;
    }
    bool moving = false;
    const gf::Rect old_untangle_damage =
        game.kind == PuzzleKind::untangle ? untangle_animation_bounds() : gf::Rect{};
    const bool was_celebrating =
        game.kind == PuzzleKind::untangle && game.state.won && animation_duration_ > 0;
    const bool was_shaking = shake_ > 0;
    const bool had_outer_effects = !effects_.empty() || shake_ > 0 || callout_life_ > 0;
    if (game.kind == PuzzleKind::gems) {
        step_effects();
        if (gem_spring_ >= 0) {
            const double k = std::exp(-fx_dt_ * 22);
            gem_offset_ = {gem_offset_.x * k, gem_offset_.y * k};
            if (std::abs(gem_offset_.x) + std::abs(gem_offset_.y) < .01) {
                gem_offset_ = {};
                gem_spring_ = -1;
            }
        }
        if (animation_duration_ <= 0 && drag_ < 0 && !game.state.over && !panel_) {
            gem_idle_ += fx_dt_;
            if (gem_idle_ > 8 && gem_hint_ < 0)
                gem_find_hint();
        }
        if (intro_ || gem_spring_ >= 0 || gem_pick_ >= 0)
            moving = true;
    }
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
            raster_dirty_ = true;
        }
        if (coarse_ != moving) {
            coarse_ = moving;
            fit_raster();
            raster_dirty_ = true;
        }
    }
    if (game.kind == PuzzleKind::gems && ((!reduced_ && !panel_) || !effects_.empty()))
        moving = true;
    if (game.kind == PuzzleKind::untangle) {
        untangle_step();
        moving = !panel_ || !puffs_.empty();
        const bool geometry_changed = update_untangle_geometry();
        if (geometry_changed || animation_duration_ > 0 || was_celebrating) {
            // A moved peg changes its incident threads and their crossing appearance.
            // The still scene keeps its retained yarn commands on ordinary cat frames.
            invalidate_static_scene();
            invalidate_animation(was_celebrating ? client_rectangle() : untangle_board_damage());
        }
        invalidate_animation(gf::Rect::united(old_untangle_damage, untangle_animation_bounds()));
    }
    if (game.kind != PuzzleKind::cube || raster_dirty_)
        render();
    // Celebrations draw beyond the board, so the whole view repaints while they run.
    if (game.kind == PuzzleKind::gems) {
        if (was_shaking || shake_ > 0)
            invalidate_static_scene();
        invalidate_animation(had_outer_effects || !effects_.empty() || shake_ > 0 ||
                                     callout_life_ > 0
                                 ? client_rectangle()
                                 : board_);
    } else if (game.kind != PuzzleKind::untangle) {
        if (animation_duration_ > 0)
            invalidate_scene();
        else
            invalidate_animation(board_);
    }
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
                invalidate_scene();
            }
        }
        if (game.kind == PuzzleKind::solve && drag_ >= 0) {
            solve_pointer_ = local_position;
            invalidate_scene();
            e.handled = true;
        }

        if (game.kind == PuzzleKind::gems && drag_ >= 0)
            gem_drag(local_position);
        if (game.kind == PuzzleKind::gems) {
            if (hover_ != cell) {
                hover_ = cell;
                if (reduced_) {
                    request_render();
                    invalidate_animation(board_);
                }
            }
        }
        if (game.kind == PuzzleKind::untangle)
            pointer_seen_ = local_position;
        if (game.kind == PuzzleKind::untangle && drag_ < 0 && hover_ != cell) {
            hover_ = cell;
            invalidate_animation(untangle_board_damage());
        }
        if (game.kind == PuzzleKind::untangle && drag_ >= 0) {
            drag_position_ = {
                std::clamp((local_position.x - board_.x) / board_.width, .035, .965),
                std::clamp((local_position.y - board_.y) / board_.height, .035, .965)};
            // Keep yarn and heads in the same input frame, even before the next timer tick.
            if (update_untangle_geometry()) {
                invalidate_static_scene();
            }
            invalidate_animation(untangle_board_damage());
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
                const int pair = game.state.stage - 1;
                const bool held = pair >= 0 && pair < game.cube_pairs();
                const std::size_t before = held ? game.state.paths[pair].size() : 0;
                bool extended = game.cube_extend(cell);
                // A quick diagonal flick skips a cell; route through the free corner cell.
                if (!extended && pair >= 0 && pair < game.cube_pairs() &&
                    !game.state.paths[pair].empty()) {
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
                    changed(cube_step_sound(game, before));
                else if (held && cell != hover_ && cube_refused(game, cell))
                    sound_play("nature_cube_blocked", sound_);
            }
            if (hover_ != cell)
                raster_dirty_ = true;
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
                for (int pair = 0; pair < game.cube_pairs(); ++pair)
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
                    changed("nature_cube_pick");
                }
            }
            return;
        }
        if (animation_duration_ > 0)
            return;
        if (game.kind == PuzzleKind::gems && cell >= 0) {
            gem_idle_ = 0;
            gem_hint_ = gem_hint_to_ = -1;
            if (gem_pick_ >= 0 && cell != gem_pick_ &&
                std::abs(gem_pick_ / 8 - cell / 8) + std::abs(gem_pick_ % 8 - cell % 8) == 1) {
                gem_swap(gem_pick_, cell, 0);
                e.handled = true;
                return;
            }
            drag_ = cell;
            gem_press_ = local_position;
            gem_dragging_ = false;
            gem_spring_ = -1;
            gem_offset_ = {};
            set_pointer_capture(true);
            if (timer_)
                (*timer_).start();
        } else if (game.kind == PuzzleKind::untangle) {
            const Point2 spot{(local_position.x - board_.x) / board_.width,
                              (local_position.y - board_.y) / board_.height};
            if (kitten_.hit(spot) || (cell >= 0 && kitten_.perch() == cell)) {
                // The cat is in the way; it scampers off.
                kitten_.shoo();
                game.message = cell >= 0 ? "The cat was sitting on that peg." : "Shoo!";
                sound_play("untangle_release", sound_);
            } else if (cell >= 0 && game.untangle_frozen(cell)) {
                frozen_nudge_ = cell;
                frozen_nudge_t_ = 0;
                game.message = "That peg is frozen until its threads are clear.";
            } else if (cell >= 0) {
                drag_ = cell;
                drag_position_ = game.state.nodes[cell];
                untangle_idle_ = 0;
                set_pointer_capture(true);
                sound_play("untangle_grab", sound_);
            }
            if (timer_)
                (*timer_).start();
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
                for (int i = 0; i < game.solve_atoms(); ++i)
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
                const int atom = solve_atom_at(local_position);
                const int piece = atom < 0 ? -1 : game.state.grid[atom] / 4 - 1;
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
                    const double unit = solve_unit();
                    const int px = std::clamp(static_cast<int>(std::lround(
                                                  (local_position.x - board_.x) / unit - mx * .5)),
                                              0, std::max(0, game.solve_columns() - mx)),
                              py = std::clamp(static_cast<int>(std::lround(
                                                  (local_position.y - board_.y) / unit - my * .5)),
                                              0, std::max(0, game.solve_rows() - my));
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
        invalidate_scene();
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
                invalidate_scene();
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
            invalidate_scene();
        }
        if (game.kind == PuzzleKind::cube) {
            tracing_ = false;
            orbit_ = false;
            persist();
        }
        if (game.kind == PuzzleKind::untangle && drag_ >= 0) {
            const std::array<int, 96> marks = game.state.marks;
            game.move_node(drag_, drag_position_);
            drag_ = -1;
            untangle_idle_ = 0;
            untangle_released(marks);
            changed("untangle_release");
        }
        if (game.kind == PuzzleKind::gems && drag_ >= 0) {
            const int from = drag_;
            drag_ = -1;
            const double reach = std::abs(gem_offset_.x) + std::abs(gem_offset_.y);
            if (gem_dragging_) {
                gem_dragging_ = false;
                const int sx = gem_offset_.x > 0   ? 1
                               : gem_offset_.x < 0 ? -1
                                                   : 0,
                          sy = gem_offset_.y > 0   ? 1
                               : gem_offset_.y < 0 ? -1
                                                   : 0;
                const int col = from % 8 + sx, row = from / 8 + sy;
                if (reach >= .3 && (sx || sy) && col >= 0 && col < 8 && row >= 0 && row < 8)
                    gem_swap(from, row * 8 + col, reach);
                else
                    gem_spring_ = from; // not far enough: it settles back into its socket
            } else if (cell >= 0 && cell != from &&
                       std::abs(from / 8 - cell / 8) + std::abs(from % 8 - cell % 8) == 1)
                gem_swap(from, cell, 0);
            else if (cell == from) {
                gem_pick_ = gem_pick_ == from ? -1 : from;
                if (gem_pick_ >= 0)
                    sound_play("gems_hover", sound_);
                invalidate_scene();
            }
            if (timer_)
                (*timer_).start();
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
    invalidate_scene();
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
    invalidate_scene();
}
} // namespace games
