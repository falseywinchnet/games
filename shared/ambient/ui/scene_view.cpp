#include "scene_view.hpp"

#include "help_route.hpp"
#include "portable_mask_cache.hpp"
#include "runtime_paths.hpp"

#include "gui_forms/surface_material.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <span>
#include <sstream>

namespace ambient {
namespace {

using Clock = std::chrono::steady_clock;

// Text masks for the help card, from the shared GUI.Forms text service.
struct TextMask {
    int w = 0;
    int h = 0;
    std::vector<std::uint8_t> a{};
};
games::PortableMaskCache<TextMask> text_masks{};
constexpr int font_body = 0;   // Libre Baskerville
constexpr int font_title = 4;  // Libre Baskerville Bold

double seconds_since(Clock::time_point start) {
    const double result = std::chrono::duration<double>(Clock::now() - start).count();
    return result;
}

gf::Color backdrop_color(std::uint32_t rgb) {
    const gf::Color result = gf::Color::rgba(static_cast<std::uint8_t>(rgb >> 16U), static_cast<std::uint8_t>(rgb >> 8U),
                                             static_cast<std::uint8_t>(rgb));
    return result;
}

// Blends a straight-alpha colour over an opaque pixel (the colour already in the
// pixel's byte order, so each channel blends with its own).
void blend_pixel(std::uint32_t& pixel, std::uint32_t rgb, float alpha) {
    const float keep = 1.0F - alpha;
    std::uint32_t out = 0xFF000000U;
    for (std::uint32_t shift = 0; shift <= 16; shift += 8) {
        const float under = static_cast<float>((pixel >> shift) & 255U);
        const float over = static_cast<float>((rgb >> shift) & 255U);
        const std::uint32_t value = static_cast<std::uint32_t>(std::min(255.0F, under * keep + over * alpha + 0.5F));
        out |= value << shift;
    }
    pixel = out;
}

void draw_mask(const TextMask& mask, int left, int top, std::uint32_t rgb, std::byte* pixels, std::size_t row_bytes,
               int width, int height) {
    for (int y = 0; y < mask.h; ++y) {
        const int device_y = top + y;
        if (device_y < 0 || device_y >= height)
            continue;
        std::uint32_t* row = reinterpret_cast<std::uint32_t*>(pixels + static_cast<std::size_t>(device_y) * row_bytes);
        for (int x = 0; x < mask.w; ++x) {
            const int device_x = left + x;
            if (device_x < 0 || device_x >= width)
                continue;
            const std::uint8_t coverage =
                mask.a[static_cast<std::size_t>(y) * static_cast<std::size_t>(mask.w) + static_cast<std::size_t>(x)];
            if (coverage == 0)
                continue;
            blend_pixel(row[device_x], rgb, static_cast<float>(coverage) / 255.0F);
        }
    }
}

std::vector<std::string> split_words(std::string_view text) {
    std::vector<std::string> words{};
    std::istringstream input{std::string(text)};
    std::string word{};
    while (input >> word)
        words.push_back(word);
    return words;
}

Detail next_detail(Detail detail) {
    if (detail == Detail::light)
        return Detail::balanced;
    if (detail == Detail::balanced)
        return Detail::fine;
    return Detail::light;
}

} // namespace

// ---------------------------------------------------------------- construction

SceneView::SceneView(gf::StableId id, HostSetup setup, std::unique_ptr<Scenery> scenery, ViewOptions options)
    : Control(std::move(id)), setup_(std::move(setup)), scenery_(std::move(scenery)), options_(options) {
    front_ = !options_.hosted;
    set_focusable(true);
    set_style(gf::ControlStyles::opaque, true);
    gf::SurfaceMaterial plain;
    plain.fills = {gf::MaterialFillLayer::solid(backdrop_color(setup_.backdrop))};
    set_authored_surface_material(plain);
    set_accessible_name(setup_.accessible_name);
    trace_.on = std::getenv("GAMES_SCENE_TRACE") != nullptr;
    restore();
    update_limits();
}

SceneView::~SceneView() = default;

void SceneView::on_attached_to_window() {
    gf::Window* window = attached_window();
    timer_ = std::make_unique<gf::Timer>(*window, std::chrono::milliseconds(33));
    subscriptions_.push_back(
        (*timer_).tick().subscribe(*this, gf::Delegate<>::bind<SceneView, &SceneView::tick>(*this)));
    subscriptions_.push_back((*window).active_changed().subscribe(
        *this, gf::Delegate<bool>::bind<SceneView, &SceneView::on_window_active>(*this)));
    window_active_ = (*window).active();
    update_limits();
    request_frame();
}

void SceneView::on_detaching_from_window(gf::Window&) noexcept {
    try {
        persist();
        report_trace();
    } catch (...) {
    }
    subscriptions_.clear();
    if (timer_)
        (*timer_).stop();
    timer_.reset();
    sound_.stop();
}

void SceneView::activate() {
    gf::Window* window = attached_window();
    if (window != nullptr)
        static_cast<void>((*window).request_focus(shared_from_this()));
    request_frame();
}

void SceneView::set_cabinet(bool foreground, bool music, bool sound, bool reduced) {
    front_ = foreground;
    music_ = music;
    sound_on_ = sound;
    reduced_ = reduced;
    sound_.cabinet(foreground, music && !music_muted_, sound && !sound_muted_);
    if (!foreground) {
        // Behind the shelf: no timer, no frames, no sound.
        dragging_ = false;
        if (timer_)
            (*timer_).stop();
        return;
    }
    request_frame();
}

void SceneView::on_window_active(bool active) {
    window_active_ = active;
    update_limits();
    request_frame();
}

// A full budget while the window is in use; half of it, and half the rates, while
// another window is in front, so a scene left open beside other work stays cheap.
void SceneView::update_limits() {
    CadenceLimits limits = setup_.limits;
    if (settings_.detail == Detail::light)
        limits.budget *= 0.5;
    if (settings_.detail == Detail::fine)
        limits.budget *= 2.0;
    if (!window_active_) {
        limits.budget *= 0.5;
        limits.frames_preferred = std::max(limits.frames_lowest * 0.75, limits.frames_preferred * 0.5);
        limits.frames_lowest *= 0.67;
        limits.sway_preferred = std::max(limits.sway_lowest, limits.sway_preferred * 0.5);
    }
    governor_.set_limits(limits);
}

Rates SceneView::rates() const {
    const Rates result = governor_.rates();
    return result;
}

bool SceneView::scene_ready() const {
    const bool result = (*scenery_).ready();
    return result;
}

SceneContext SceneView::context() {
    SceneContext result{governor_, sound_, settings_, scene_time_, reduced_, settings_.paused,
                        sound_on_ && front_ && visible(), 0, false};
    return result;
}

void SceneView::finish(const SceneContext& context) {
    if (context.persist)
        persist();
}

// ---------------------------------------------------------------- layout and paint

void SceneView::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    gf::Window* window = attached_window();
    scale_ = window != nullptr ? (*window).scale() : 1.0;
    device_width_ = std::max(1, static_cast<int>(std::lround(bounds.width * scale_)));
    device_height_ = std::max(1, static_cast<int>(std::lround(bounds.height * scale_)));
    wanted_ = (*scenery_).size_for(device_width_, device_height_, scale_, settings_.detail);
    last_arrange_ = Clock::now();
    (*scenery_).resize(wanted_, false);
    if (surface_) {
        gf::LiveSurfaceDescription description;
        description.width = static_cast<std::uint32_t>(device_width_);
        description.height = static_cast<std::uint32_t>(device_height_);
        description.opaque = true;  // every pixel is written opaque: the host may copy, not blend
        description.pixel_format = gf::native_live_surface_pixel_format();  // the host's own byte order: a straight copy
        rgba_ = description.pixel_format == gf::LiveSurfacePixelFormat::rgba32_premultiplied_srgb;
        static_cast<void>((*surface_).reconfigure(description));
    }
    request_frame();
}

void SceneView::on_paint(gf::Painter& painter, gf::Rect) {
    const gf::Rect bounds = client_rectangle();
    if (surface_)
        painter.draw_live_surface(surface_, bounds);
    else
        painter.fill_rect(bounds, backdrop_color(setup_.backdrop));
}

// ---------------------------------------------------------------- the clock

void SceneView::request_frame() {
    render_dirty_ = true;
    if (!timer_ || !visible() || !front_)
        return;
    if (!(*timer_).enabled())
        last_tick_ = Clock::now();
    (*timer_).set_interval(std::chrono::milliseconds(16));
    (*timer_).start();
}

bool SceneView::animating() const {
    const bool result = scene_ready() && !settings_.paused && front_ && visible();
    return result;
}

void SceneView::tick() {
    if (!visible() || !front_) {
        if (timer_)
            (*timer_).stop();
        return;
    }
    const Clock::time_point now = Clock::now();
    const double elapsed = std::clamp(std::chrono::duration<double>(now - last_tick_).count(), 0.0, 0.25);
    last_tick_ = now;
    SceneContext work = context();
    const bool was_ready = scene_ready();
    const bool pending = (*scenery_).poll(work);
    if (scene_ready() != was_ready || work.redraw)
        render_dirty_ = true;
    // Expensive rebuilds wait until a resize has stopped for a moment.
    if (seconds_since(last_arrange_) > 0.15)
        (*scenery_).resize(wanted_, true);
    gf::Window* window = attached_window();
    const bool occluded = window != nullptr && (*window).occluded();
    const bool moving = animating();
    if (moving && !occluded) {
        scene_time_ += elapsed;
        work.time = scene_time_;
        (*scenery_).advance(elapsed, work);
    }
    (*scenery_).sound(moving, work);
    sound_.tick(elapsed);
    if (!occluded && (render_dirty_ || moving)) {
        // The governor is charged for the whole frame: drawing, enlarging and
        // publishing, less what the scenery charged separately (foliage, say).
        render_dirty_ = false;
        const Clock::time_point start = Clock::now();
        if (scene_ready()) {
            int width = 0;
            int height = 0;
            const std::vector<std::uint32_t>& picture = (*scenery_).draw(work, width, height);
            const Clock::time_point drawn = Clock::now();
            publish(&picture, width, height);
            governor_.record_frame(std::max(0.0, seconds_since(start) - work.charged));
            if (trace_.on && moving) {
                const double draw = std::chrono::duration<double>(drawn - start).count();
                const double present = seconds_since(drawn);
                const double interval = 1.0 / std::max(1.0, rates().frames);
                if (trace_.frames > 0) {
                    const double gap = std::chrono::duration<double>(start - trace_.last_frame).count();
                    trace_.gap_sum += gap;
                    trace_.gap_square += gap * gap;
                    trace_.gap_max = std::max(trace_.gap_max, gap);
                    if (gap > interval * 1.5)
                        ++trace_.late_frames;
                }
                trace_.last_frame = start;
                ++trace_.frames;
                trace_.draw_sum += draw;
                trace_.draw_max = std::max(trace_.draw_max, draw);
                trace_.present_sum += present;
                trace_.present_max = std::max(trace_.present_max, present);
            }
        } else {
            publish(nullptr, 0, 0);
        }
    }
    finish(work);
    if (!timer_)
        return;
    if (pending) {
        (*timer_).set_interval(std::chrono::milliseconds(30));
    } else if (moving && occluded) {
        // Not on screen: look again four times a second, draw nothing.
        (*timer_).set_interval(std::chrono::milliseconds(250));
    } else if (moving) {
        // Frames keep their phase: the timer's deadlines advance by whole intervals, so
        // it is only re-armed when the rate really changes, and then from this tick's
        // start rather than from now, so the work just done does not lengthen the frame.
        const double frames = std::max(1.0, rates().frames);
        const std::chrono::milliseconds wanted(static_cast<long long>(std::lround(1000.0 / frames)));
        const long long change = std::abs(wanted.count() - (*timer_).interval().count());
        if (change >= 2 || !(*timer_).enabled()) {
            (*timer_).set_interval(wanted);
            (*timer_).start_at(now + wanted);
        }
    } else if (render_dirty_) {
        (*timer_).set_interval(std::chrono::milliseconds(16));
    } else if (sound_.needs_tick()) {
        (*timer_).set_interval(std::chrono::milliseconds(50));
    } else {
        // Paused or settled: nothing runs until input, a command or a resize.
        (*timer_).stop();
    }
}

// ---------------------------------------------------------------- drawing

// A packed 0xRRGGBB colour as the surface stores it (red and blue exchanged for RGBA).
std::uint32_t SceneView::in_order(std::uint32_t rgb) const {
    return rgba_ ? ((rgb & 0xFF00U) | ((rgb >> 16) & 0xFFU) | ((rgb & 0xFFU) << 16)) : rgb;
}

void SceneView::publish(const std::vector<std::uint32_t>* picture, int width, int height) {
    if (!surface_) {
        gf::LiveSurfaceDescription description;
        description.width = static_cast<std::uint32_t>(device_width_);
        description.height = static_cast<std::uint32_t>(device_height_);
        description.opaque = true;  // every pixel is written opaque: the host may copy, not blend
        description.pixel_format = gf::native_live_surface_pixel_format();  // the host's own byte order: a straight copy
        rgba_ = description.pixel_format == gf::LiveSurfacePixelFormat::rgba32_premultiplied_srgb;
        surface_ = gf::LiveSurface::create(description);
        gf::Window* window = attached_window();
        if (surface_ && window != nullptr)
            direct_ = (*window).queue_live_surface_presentation(shared_from_this(), surface_);
    }
    if (!surface_)
        return;
    gf::LiveSurfaceWriteLease lease = (*surface_).try_acquire_write();
    const bool matches = lease && static_cast<int>(lease.width()) == device_width_ &&
                         static_cast<int>(lease.height()) == device_height_;
    if (!matches) {
        render_dirty_ = true;
        ++trace_.lease_misses;
        return;
    }
    const std::span<std::byte> destination = lease.pixels();
    const std::size_t row_bytes = lease.row_bytes();
    const bool drawable = picture != nullptr && width > 0 && height > 0 &&
                          (*picture).size() >= static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (drawable) {
        if (columns_.size() != static_cast<std::size_t>(device_width_) || columns_width_ != width) {
            build_column_map(width, device_width_, columns_);
            columns_width_ = width;
        }
        present_nearest((*picture).data(), width, height, columns_, destination.data(), row_bytes, device_width_,
                        device_height_, rgba_);
    } else {
        const std::uint32_t backdrop = 0xFF000000U | in_order(setup_.backdrop);
        for (int y = 0; y < device_height_; ++y) {
            std::uint32_t* row = reinterpret_cast<std::uint32_t*>(destination.data() + static_cast<std::size_t>(y) * row_bytes);
            std::fill(row, row + device_width_, backdrop);
        }
    }
    if (help_open_ || !(*scenery_).failure().empty())
        draw_help(destination.data(), row_bytes);
    static_cast<void>(lease.publish());
    if (!direct_)
        invalidate(gf::Dirty::paint);
    text_masks.trim();
}

void SceneView::report_trace() const {
    if (!trace_.on || trace_.frames < 2)
        return;
    const double gaps = static_cast<double>(trace_.frames - 1);
    const double mean = trace_.gap_sum / gaps;
    const double spread = std::sqrt(std::max(0.0, trace_.gap_square / gaps - mean * mean));
    const double frames = static_cast<double>(trace_.frames);
    std::fprintf(stderr,
                 "SCENE_TRACE %s frames %llu interval %.2f ms (sd %.2f, max %.1f, late %llu) draw %.2f ms (max %.1f) "
                 "present %.2f ms (max %.1f) lease misses %llu; scene %d x %d, window %d x %d, rates %.1f frames %.1f sway\n",
                 setup_.id.c_str(), static_cast<unsigned long long>(trace_.frames), mean * 1e3, spread * 1e3,
                 trace_.gap_max * 1e3, static_cast<unsigned long long>(trace_.late_frames),
                 trace_.draw_sum / frames * 1e3, trace_.draw_max * 1e3, trace_.present_sum / frames * 1e3,
                 trace_.present_max * 1e3, static_cast<unsigned long long>(trace_.lease_misses), wanted_.width,
                 wanted_.height, device_width_, device_height_, rates().frames, rates().sway);
}

// The help card, drawn by the view itself when no shell shows help (and to report a
// scene that could not be opened). Lines are wrapped here from words, so only short
// runs of text are rasterized.
void SceneView::draw_help(std::byte* pixels, std::size_t row_bytes) {
    const double s = scale_;
    const int margin = static_cast<int>(std::lround(18 * s));
    const int card_width = std::min(device_width_ - margin * 2, static_cast<int>(std::lround(520 * s)));
    if (card_width < 80)
        return;
    const int inner = card_width - margin * 2;
    const double body_size = 14 * s;
    const double title_size = 20 * s;
    const std::string failure = (*scenery_).failure();
    std::vector<std::string> paragraphs = setup_.help_paragraphs;
    std::string title = setup_.help_title;
    if (!failure.empty()) {
        title = "The scene could not be opened";
        paragraphs = {failure + ".", "Reinstalling PlaySuite restores its scene files."};
    } else {
        paragraphs.push_back("Esc or a click closes this card.");
    }
    struct Line {
        const TextMask* mask{};
        int gap{};
    };
    std::vector<Line> lines{};
    const TextMask& heading = text_masks.get(title, font_title, title_size, 0);
    lines.push_back({&heading, 0});
    for (const std::string& paragraph : paragraphs) {
        const std::vector<std::string> words = split_words(paragraph);
        std::string current{};
        int gap = static_cast<int>(std::lround(10 * s));
        for (std::size_t index = 0; index < words.size(); ++index) {
            const std::string candidate = current.empty() ? words[index] : current + " " + words[index];
            const TextMask& trial = text_masks.get(candidate, font_body, body_size, 0);
            if (trial.w > inner && !current.empty()) {
                const TextMask& line = text_masks.get(current, font_body, body_size, 0);
                lines.push_back({&line, gap});
                gap = 0;
                current = words[index];
            } else {
                current = candidate;
            }
        }
        if (!current.empty()) {
            const TextMask& line = text_masks.get(current, font_body, body_size, 0);
            lines.push_back({&line, gap});
        }
    }
    int text_height = 0;
    for (const Line& line : lines)
        text_height += (*line.mask).h + line.gap;
    const int card_height = std::min(device_height_ - margin * 2, text_height + margin * 2);
    const int left = (device_width_ - card_width) / 2;
    const int top = std::max(margin, (device_height_ - card_height) / 2);
    for (int y = top; y < top + card_height && y < device_height_; ++y) {
        std::uint32_t* row = reinterpret_cast<std::uint32_t*>(pixels + static_cast<std::size_t>(y) * row_bytes);
        for (int x = left; x < left + card_width; ++x)
            blend_pixel(row[x], in_order(0x07130F), 0.86F);
    }
    int y = top + margin;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const Line& line = lines[index];
        y += line.gap;
        if (y + (*line.mask).h > top + card_height)
            break;
        const std::uint32_t ink = index == 0 ? 0xE9F3DCU : 0xC9D8C4U;
        draw_mask(*line.mask, left + margin, y, in_order(ink), pixels, row_bytes, device_width_, device_height_);
        y += (*line.mask).h;
    }
}

// ---------------------------------------------------------------- actions

void SceneView::set_paused(bool paused) {
    settings_.paused = paused;
    dragging_ = false;
    persist();
    last_tick_ = Clock::now();
    request_frame();
}

void SceneView::set_detail(Detail detail) {
    settings_.detail = detail;
    persist();
    update_limits();
    wanted_ = (*scenery_).size_for(device_width_, device_height_, scale_, settings_.detail);
    (*scenery_).resize(wanted_, true);
    request_frame();
}

void SceneView::set_help(bool open) {
    if (open && games::route_help(*this))
        return;
    help_open_ = open;
    request_frame();
}

void SceneView::on_pointer(gf::PointerEvent& event) {
    const gf::Point local = point_from_window(event.position);
    const gf::Rect bounds = client_rectangle();
    if (bounds.width <= 0 || bounds.height <= 0)
        return;
    const double x = std::clamp(local.x / bounds.width, 0.0, 1.0);
    const double y = std::clamp(local.y / bounds.height, 0.0, 1.0);
    const bool live = scene_ready() && !settings_.paused && !help_open_;
    SceneContext work = context();
    if (event.action == gf::PointerAction::move) {
        if (dragging_ && live) {
            (*scenery_).drag(x, y, work);
            request_frame();
            event.handled = true;
        } else if (!dragging_) {
            set_cursor(live && (*scenery_).grabbable(x, y) ? gf::CursorKind::hand : gf::CursorKind::arrow);
        }
    } else if (event.action == gf::PointerAction::down && event.button == gf::PointerButton::primary) {
        activate();
        if (help_open_) {
            set_help(false);
        } else if (live) {
            dragging_ = true;
            (*scenery_).press(x, y, work);
            request_frame();
        }
        event.handled = true;
    } else if (event.action == gf::PointerAction::up && dragging_) {
        dragging_ = false;
        if (live)
            (*scenery_).release(x, y, work);
        request_frame();
        event.handled = true;
    } else if (event.action == gf::PointerAction::leave && !dragging_) {
        set_cursor(gf::CursorKind::arrow);
    }
    finish(work);
}

void SceneView::on_key(gf::KeyEvent& event) {
    if (event.handled)
        return;
    using Key = gf::PhysicalKey;
    const std::uint32_t key = event.physical_key;
    if (!help_open_ && !event.repeat) {
        SceneContext work = context();
        const bool taken = (*scenery_).hold_key(key, event.action == gf::KeyAction::down, work);
        finish(work);
        if (taken) {
            event.handled = true;
            request_frame();
            return;
        }
    }
    if (event.action != gf::KeyAction::down)
        return;
    event.handled = true;
    if (event.repeat)
        return;
    if (help_open_ && (key == Key::escape || key == Key::f1 || key == Key::enter || key == Key::space)) {
        set_help(false);
    } else if (key == Key::f1 || key == Key::h) {
        set_help(true);
    } else if (key == Key::space || key == Key::p) {
        set_paused(!settings_.paused);
    } else if (key == Key::q) {
        set_detail(next_detail(settings_.detail));
    } else if (key == Key::m) {
        run_command("music");
    } else {
        const std::string command = (*scenery_).key_command(key);
        if (command.empty())
            event.handled = false;
        else
            run_command(command);
    }
}

std::vector<games::GameCommand> SceneView::commands() const {
    std::vector<games::GameCommand> list{};
    list.push_back({"pause", settings_.paused ? "Resume" : "Pause", true, settings_.paused, true});
    (*scenery_).add_commands(list, settings_);
    list.push_back({"help", "Help", true, help_open_, false});
    return list;
}

std::vector<games::GameSetting> SceneView::settings() const {
    std::vector<games::GameSetting> list{};
    list.push_back({"detail", "Detail", games::GameSetting::Kind::choice,
                    static_cast<double>(static_cast<int>(settings_.detail)), {"Light", "Balanced", "Fine"},
                    0, 2, 1, "Light is gentlest on an older or busy computer. Q changes it too."});
    (*scenery_).add_settings(list, settings_);
    return list;
}

void SceneView::change_setting(std::string_view id, double value) {
    const int choice = static_cast<int>(std::lround(value));
    if (id == "detail") {
        set_detail(choice <= 0 ? Detail::light : (choice == 1 ? Detail::balanced : Detail::fine));
        return;
    }
    SceneContext work = context();
    if ((*scenery_).change_setting(id, value, work))
        request_frame();
    finish(work);
}

void SceneView::run_command(std::string_view id) {
    if (id == "pause") {
        set_paused(!settings_.paused);
    } else if (id == "detail") {
        set_detail(next_detail(settings_.detail));
    } else if (id == "help") {
        set_help(!help_open_);
    } else if ((id == "sound" || id == "music") && !options_.hosted) {
        // Standalone only: without the PlaySuite shell the scene keeps its own switches.
        // Hosted, the shell's masters are the only ones (the capsule, M and Settings).
        if (id == "sound")
            sound_muted_ = !sound_muted_;
        else
            music_muted_ = !music_muted_;
        sound_.cabinet(front_, music_ && !music_muted_, sound_on_ && !sound_muted_);
    } else {
        SceneContext work = context();
        if ((*scenery_).run_command(id, work))
            request_frame();
        finish(work);
    }
}

bool SceneView::scripted_action(std::string_view action) {
    if (!options_.dev || !front_ || !visible())
        return false;
    std::istringstream input{std::string(action)};
    std::string verb{};
    input >> verb;
    if (verb == "pause" || verb == "resume") {
        set_paused(verb == "pause");
        return true;
    }
    if (verb == "detail") {
        std::string value{};
        input >> value;
        if (value == "light")
            set_detail(Detail::light);
        else if (value == "balanced")
            set_detail(Detail::balanced);
        else if (value == "fine")
            set_detail(Detail::fine);
        else
            return false;
        return true;
    }
    SceneContext work = context();
    const bool accepted = (*scenery_).scripted_action(action, work);
    finish(work);
    if (accepted)
        request_frame();
    return accepted;
}

// ---------------------------------------------------------------- settings

std::filesystem::path SceneView::settings_path() const {
    // A development run keeps separate settings: my_scene-v1.txt becomes my_scene-v1-dev.txt.
    std::filesystem::path name = setup_.settings_file.empty() ? setup_.id + "-v1.txt" : setup_.settings_file;
    if (options_.dev)
        name = name.stem().string() + "-dev" + name.extension().string();
    const std::filesystem::path path = games::state_directory() / name;
    return path;
}

void SceneView::persist() const {
    const std::filesystem::path path = settings_path();
    std::error_code error{};
    std::filesystem::create_directories(path.parent_path(), error);
    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            return;
        output << encode_settings(settings_);
        if (!output)
            return;
    }
    std::filesystem::rename(temporary, path, error);
}

void SceneView::restore() {
    std::ifstream input(settings_path(), std::ios::binary);
    if (!input)
        return;
    std::string text(4096, '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(std::max<std::streamsize>(input.gcount(), 0)));
    static_cast<void>(decode_settings(text, settings_));
}

} // namespace ambient
