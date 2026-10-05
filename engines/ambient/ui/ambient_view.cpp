#include "ambient_view.hpp"

#include "help_route.hpp"
#include "portable_mask_cache.hpp"
#include "runtime_paths.hpp"

#include "gui_forms/surface_material.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <cmath>
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

template <typename Result> bool finished(const std::future<Result>& future) {
    const bool result = future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    return result;
}

// Blends a straight-alpha colour over an opaque BGRA pixel.
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

} // namespace

// ---------------------------------------------------------------- construction

AmbientView::AmbientView(gf::StableId id, SceneSetup setup, ViewOptions options)
    : Control(std::move(id)), setup_(std::move(setup)), options_(options) {
    front_ = !options_.hosted;
    set_focusable(true);
    set_style(gf::ControlStyles::opaque, true);
    gf::SurfaceMaterial plain;
    plain.fills = {gf::MaterialFillLayer::solid(gf::Color::rgba(static_cast<std::uint8_t>(setup_.backdrop >> 16U),
                                                                static_cast<std::uint8_t>(setup_.backdrop >> 8U),
                                                                static_cast<std::uint8_t>(setup_.backdrop)))};
    set_authored_surface_material(plain);
    set_accessible_name(setup_.accessible_name);
    restore();
    update_limits();
    const std::filesystem::path archive = std::filesystem::path(games::asset_directory()) / setup_.id / setup_.archive;
    loading_ = std::async(std::launch::async, &AmbientView::load_bundle, archive, setup_.make_look);
}

AmbientView::~AmbientView() {
    // Workers read the bundle; wait for them before anything they use goes away.
    if (building_.valid())
        building_.wait();
    if (loading_.valid())
        loading_.wait();
}

std::unique_ptr<AmbientView::Bundle> AmbientView::load_bundle(std::filesystem::path archive, LookFactory make_look) {
    std::unique_ptr<Bundle> bundle = std::make_unique<Bundle>();
    if (!load_scene(archive.string(), (*bundle).scene, (*bundle).error))
        return bundle;
    if (make_look == nullptr) {
        (*bundle).error = "The scene has no look";
        return bundle;
    }
    (*bundle).look = make_look((*bundle).scene);
    (*bundle).shadows = build_shadow_map((*bundle).scene, *(*bundle).look, 1024);
    (*bundle).foliage = prepare_foliage((*bundle).scene, *(*bundle).look, (*bundle).shadows);
    return bundle;
}

void AmbientView::on_attached_to_window() {
    gf::Window* window = attached_window();
    timer_ = std::make_unique<gf::Timer>(*window, std::chrono::milliseconds(33));
    subscriptions_.push_back(
        (*timer_).tick().subscribe(*this, gf::Delegate<>::bind<AmbientView, &AmbientView::tick>(*this)));
    subscriptions_.push_back((*window).active_changed().subscribe(
        *this, gf::Delegate<bool>::bind<AmbientView, &AmbientView::on_window_active>(*this)));
    window_active_ = (*window).active();
    update_limits();
    request_frame();
}

void AmbientView::on_detaching_from_window(gf::Window&) noexcept {
    try {
        persist();
    } catch (...) {
    }
    subscriptions_.clear();
    if (timer_)
        (*timer_).stop();
    timer_.reset();
    audio_.stop();
}

void AmbientView::activate() {
    gf::Window* window = attached_window();
    if (window != nullptr)
        static_cast<void>((*window).request_focus(shared_from_this()));
    request_frame();
}

void AmbientView::set_cabinet(bool foreground, bool music, bool sound, bool reduced) {
    front_ = foreground;
    music_ = music;
    sound_ = sound;
    reduced_ = reduced;
    audio_.cabinet(foreground, music, sound);
    if (!foreground) {
        // Behind the shelf: no timer, no frames, no sound, no workers started.
        if (timer_)
            (*timer_).stop();
        return;
    }
    sync_sound();
    request_frame();
}

void AmbientView::on_window_active(bool active) {
    window_active_ = active;
    update_limits();
    request_frame();
}

// A full budget while the window is in use; half of it, and half the rates, while
// another window is in front, so a tank left open beside other work stays cheap.
void AmbientView::update_limits() {
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

Rates AmbientView::rates() const {
    const Rates result = governor_.rates();
    return result;
}

bool AmbientView::scene_ready() const {
    const bool result = stage_ && (*stage_).ready();
    return result;
}

// ---------------------------------------------------------------- layout and paint

void AmbientView::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    gf::Window* window = attached_window();
    scale_ = window != nullptr ? (*window).scale() : 1.0;
    device_width_ = std::max(1, static_cast<int>(std::lround(bounds.width * scale_)));
    device_height_ = std::max(1, static_cast<int>(std::lround(bounds.height * scale_)));
    wanted_ = scene_size(device_width_, device_height_, scale_, settings_.detail);
    last_arrange_ = Clock::now();
    if (surface_) {
        gf::LiveSurfaceDescription description;
        description.width = static_cast<std::uint32_t>(device_width_);
        description.height = static_cast<std::uint32_t>(device_height_);
        static_cast<void>((*surface_).reconfigure(description));
    }
    request_frame();
}

void AmbientView::on_paint(gf::Painter& painter, gf::Rect) {
    const gf::Rect bounds = client_rectangle();
    if (surface_) {
        painter.draw_live_surface(surface_, bounds);
    } else {
        painter.fill_rect(bounds, gf::Color::rgba(static_cast<std::uint8_t>(setup_.backdrop >> 16U),
                                                  static_cast<std::uint8_t>(setup_.backdrop >> 8U),
                                                  static_cast<std::uint8_t>(setup_.backdrop)));
    }
}

// ---------------------------------------------------------------- the clock

void AmbientView::request_frame() {
    render_dirty_ = true;
    if (!timer_ || !visible() || !front_)
        return;
    if (!(*timer_).enabled())
        last_tick_ = Clock::now();
    (*timer_).set_interval(std::chrono::milliseconds(16));
    (*timer_).start();
}

bool AmbientView::animating() const {
    const bool result = scene_ready() && !settings_.paused && front_ && visible();
    return result;
}

void AmbientView::tick() {
    if (!visible() || !front_) {
        if (timer_)
            (*timer_).stop();
        return;
    }
    const Clock::time_point now = Clock::now();
    const double elapsed = std::clamp(std::chrono::duration<double>(now - last_tick_).count(), 0.0, 0.25);
    last_tick_ = now;
    poll_workers();
    audio_.tick(elapsed);
    gf::Window* window = attached_window();
    const bool occluded = window != nullptr && (*window).occluded();
    const bool moving = animating();
    if (moving && !occluded) {
        scene_time_ += elapsed;
        if (!reduced_)
            light_time_ = scene_time_;
    }
    if (!occluded && (render_dirty_ || moving)) {
        // The governor is charged for the whole frame: composing, enlarging and
        // publishing (foliage updates are charged separately inside render()).
        render_dirty_ = false;
        const Clock::time_point start = Clock::now();
        const double sway_seconds = render();
        publish();
        if (scene_ready())
            governor_.record_frame(std::max(0.0, seconds_since(start) - sway_seconds));
    }
    if (!timer_)
        return;
    const bool workers = loading_.valid() || building_.valid();
    if (workers) {
        (*timer_).set_interval(std::chrono::milliseconds(30));
    } else if (moving && occluded) {
        // Not on screen: look again four times a second, draw nothing.
        (*timer_).set_interval(std::chrono::milliseconds(250));
    } else if (moving) {
        const double frames = std::max(1.0, rates().frames);
        (*timer_).set_interval(std::chrono::milliseconds(static_cast<long long>(std::lround(1000.0 / frames))));
    } else if (render_dirty_) {
        (*timer_).set_interval(std::chrono::milliseconds(16));
    } else if (audio_.needs_tick()) {
        (*timer_).set_interval(std::chrono::milliseconds(50));
    } else {
        // Paused or settled: nothing runs until input, a command or a resize.
        (*timer_).stop();
    }
}

void AmbientView::poll_workers() {
    if (finished(loading_)) {
        bundle_ = loading_.get();
        if (bundle_ && (*bundle_).error.empty()) {
            stage_ = std::make_unique<Stage>((*bundle_).scene, *(*bundle_).look, (*bundle_).foliage);
            creatures_ = creatures_from((*bundle_).scene.actors);
        }
        render_dirty_ = true;
        sync_sound();
    }
    if (finished(building_)) {
        FixedLayer layer = building_.get();
        if (stage_ && layer.width == building_size_.width && layer.height == building_size_.height) {
            (*stage_).adopt(std::move(layer));
            last_sway_ = -1;
            governor_.reset();
            columns_.clear();
        }
        render_dirty_ = true;
    }
    if (!stage_ || building_.valid() || wanted_.width <= 0)
        return;
    const bool stale = (*stage_).width() != wanted_.width || (*stage_).height() != wanted_.height;
    if (!stale)
        return;
    // The first layer is built at once; later ones wait for a resize to pause, and the
    // previous picture is enlarged to the new size in the meantime.
    if (!(*stage_).ready() || seconds_since(last_arrange_) > 0.15)
        start_build();
    else
        render_dirty_ = true;
}

void AmbientView::start_build() {
    building_size_ = wanted_;
    building_ = std::async(std::launch::async, &build_fixed_layer, std::cref((*bundle_).scene),
                           std::cref(*(*bundle_).look), std::cref((*bundle_).shadows), std::cref((*bundle_).foliage),
                           building_size_.width, building_size_.height, 2, 0.5F);
}

// ---------------------------------------------------------------- drawing

double AmbientView::render() {
    if (!scene_ready())
        return 0;
    const Rates current = rates();
    const bool first = last_sway_ < 0;
    const bool due = scene_time_ - last_sway_ >= 1.0 / std::max(current.sway, 0.5) || scene_time_ < last_sway_;
    double sway_seconds = 0;
    if (first || (!reduced_ && !settings_.paused && due)) {
        const Clock::time_point start = Clock::now();
        (*stage_).update_sway(scene_time_);
        sway_seconds = seconds_since(start);
        if (!first)
            governor_.record_sway(sway_seconds);
        last_sway_ = scene_time_;
    }
    (*stage_).compose(scene_time_, light_time_, creatures_);
    return sway_seconds;
}

void AmbientView::publish() {
    if (!surface_) {
        gf::LiveSurfaceDescription description;
        description.width = static_cast<std::uint32_t>(device_width_);
        description.height = static_cast<std::uint32_t>(device_height_);
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
        return;
    }
    const std::span<std::byte> destination = lease.pixels();
    const std::size_t row_bytes = lease.row_bytes();
    if (scene_ready()) {
        const int width = (*stage_).width();
        if (columns_.size() != static_cast<std::size_t>(device_width_) || columns_width_ != width) {
            build_column_map(width, device_width_, columns_);
            columns_width_ = width;
        }
        present_nearest((*stage_).frame().data(), width, (*stage_).height(), columns_, destination.data(), row_bytes,
                        device_width_, device_height_);
    } else {
        const std::uint32_t backdrop = 0xFF000000U | setup_.backdrop;
        for (int y = 0; y < device_height_; ++y) {
            std::uint32_t* row = reinterpret_cast<std::uint32_t*>(destination.data() + static_cast<std::size_t>(y) * row_bytes);
            std::fill(row, row + device_width_, backdrop);
        }
    }
    const bool failed = bundle_ && !(*bundle_).error.empty();
    if (help_open_ || failed)
        draw_help(destination.data(), row_bytes);
    static_cast<void>(lease.publish());
    if (!direct_)
        invalidate(gf::Dirty::paint);
    text_masks.trim();
}

// The help card, drawn by the view itself when no shell shows help (and to report a
// scene that could not be loaded). Lines are wrapped here from words, so only short
// runs of text are rasterized.
void AmbientView::draw_help(std::byte* pixels, std::size_t row_bytes) {
    const double s = scale_;
    const int margin = static_cast<int>(std::lround(18 * s));
    const int card_width = std::min(device_width_ - margin * 2, static_cast<int>(std::lround(520 * s)));
    if (card_width < 80)
        return;
    const int inner = card_width - margin * 2;
    const double body_size = 14 * s;
    const double title_size = 20 * s;
    const bool failed = bundle_ && !(*bundle_).error.empty();
    std::vector<std::string> paragraphs = setup_.help_paragraphs;
    std::string title = setup_.help_title;
    if (failed) {
        title = "The scene could not be opened";
        paragraphs = {(*bundle_).error + ".", "Reinstalling PlaySuite restores its scene files."};
    }
    if (!failed)
        paragraphs.push_back("Esc or a click closes this card.");
    // Lay out: one mask per line, words added while they fit.
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
            blend_pixel(row[x], 0x07130F, 0.86F);
    }
    int y = top + margin;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const Line& line = lines[index];
        y += line.gap;
        if (y + (*line.mask).h > top + card_height)
            break;
        const std::uint32_t ink = index == 0 ? 0xE9F3DCU : 0xC9D8C4U;
        draw_mask(*line.mask, left + margin, y, ink, pixels, row_bytes, device_width_, device_height_);
        y += (*line.mask).h;
    }
}

// ---------------------------------------------------------------- actions

void AmbientView::tap(double x, double y) {
    if (!scene_ready() || settings_.paused)
        return;
    const Projection& projection = (*stage_).projection();
    const ScreenTap at{x, y, static_cast<double>(projection.width) / std::max(1, projection.height), 0.2};
    const std::size_t startled = startle(creatures_, projection, at, scene_time_, setup_.bounds);
    if (!setup_.tap_sound.empty()) {
        const double gain = startled > 0 ? 0.85 : 0.6;
        audio_.effects(setup_.tap_sound, gain, 1.0, sound_ && front_ && visible(), std::clamp(x * 2 - 1, -1.0, 1.0));
    }
    request_frame();
}

void AmbientView::set_paused(bool paused) {
    settings_.paused = paused;
    persist();
    sync_sound();
    last_tick_ = Clock::now();
    request_frame();
}

void AmbientView::set_detail(Detail detail) {
    settings_.detail = detail;
    persist();
    update_limits();
    wanted_ = scene_size(device_width_, device_height_, scale_, settings_.detail);
    last_arrange_ = Clock::now() - std::chrono::seconds(1);  // rebuild without waiting
    request_frame();
}

void AmbientView::set_help(bool open) {
    if (open && games::route_help(*this))
        return;
    help_open_ = open;
    request_frame();
}

void AmbientView::sync_sound() {
    // The water's sound plays while the tank runs and stops with it.
    const bool on = !setup_.ambience.empty() && front_ && !settings_.paused && bundle_ != nullptr;
    audio_.music(on ? setup_.ambience : std::string(), on);
}

void AmbientView::on_pointer(gf::PointerEvent& event) {
    if (event.action != gf::PointerAction::down || event.button != gf::PointerButton::primary)
        return;
    activate();
    if (help_open_) {
        set_help(false);
    } else {
        const gf::Point local = point_from_window(event.position);
        const gf::Rect bounds = client_rectangle();
        if (bounds.width > 0 && bounds.height > 0)
            tap(local.x / bounds.width, local.y / bounds.height);
    }
    event.handled = true;
}

void AmbientView::on_key(gf::KeyEvent& event) {
    if (event.handled || event.action != gf::KeyAction::down)
        return;
    using Key = gf::PhysicalKey;
    const std::uint32_t key = event.physical_key;
    event.handled = true;
    if (help_open_ && (key == Key::escape || key == Key::f1 || key == Key::enter || key == Key::space)) {
        set_help(false);
    } else if (key == Key::f1 || key == Key::h) {
        set_help(true);
    } else if (key == Key::space || key == Key::p) {
        set_paused(!settings_.paused);
    } else if (key == Key::d) {
        const Detail next = settings_.detail == Detail::light
                                ? Detail::balanced
                                : (settings_.detail == Detail::balanced ? Detail::fine : Detail::light);
        set_detail(next);
    } else {
        event.handled = false;
    }
}

std::vector<games::GameCommand> AmbientView::commands() const {
    std::vector<games::GameCommand> list{};
    std::string detail = "Detail: ";
    detail += settings_.detail == Detail::light ? "Light" : (settings_.detail == Detail::fine ? "Fine" : "Balanced");
    list.push_back({"pause", settings_.paused ? "Resume" : "Pause", true, settings_.paused, true});
    list.push_back({"detail", detail, true, false, false});
    list.push_back({"help", "Help", true, help_open_, false});
    return list;
}

void AmbientView::run_command(std::string_view id) {
    if (id == "pause") {
        set_paused(!settings_.paused);
    } else if (id == "detail") {
        const Detail next = settings_.detail == Detail::light
                                ? Detail::balanced
                                : (settings_.detail == Detail::balanced ? Detail::fine : Detail::light);
        set_detail(next);
    } else if (id == "help") {
        set_help(!help_open_);
    }
}

bool AmbientView::scripted_action(std::string_view action) {
    if (!options_.dev || !front_ || !visible())
        return false;
    std::istringstream input{std::string(action)};
    std::string verb{};
    input >> verb;
    if (verb == "tap") {
        double x = -1;
        double y = -1;
        input >> x >> y;
        if (!input || x < 0 || x > 1 || y < 0 || y > 1)
            return false;
        tap(x, y);
        return true;
    }
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
    return false;
}

// ---------------------------------------------------------------- settings

std::filesystem::path AmbientView::settings_path() const {
    // A development run keeps separate settings: my_scene-v1.txt becomes my_scene-v1-dev.txt.
    std::filesystem::path name = setup_.settings_file.empty() ? setup_.id + "-v1.txt" : setup_.settings_file;
    if (options_.dev)
        name = name.stem().string() + "-dev" + name.extension().string();
    const std::filesystem::path path = games::state_directory() / name;
    return path;
}

void AmbientView::persist() const {
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

void AmbientView::restore() {
    std::ifstream input(settings_path(), std::ios::binary);
    if (!input)
        return;
    std::string text(4096, '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(std::max<std::streamsize>(input.gcount(), 0)));
    static_cast<void>(decode_settings(text, settings_));
}

} // namespace ambient
