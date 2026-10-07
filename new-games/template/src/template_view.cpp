#include "help_route.hpp"
#include "template_view.hpp"

#include "platform/audio.hpp"
#include "runtime_paths.hpp"
#include "save.hpp"
#include "scene.hpp"

#include "gui_forms/surface_material.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>

namespace tg {
namespace {

std::uint64_t seed_from_clock() {
    const std::chrono::system_clock::duration since = std::chrono::system_clock::now().time_since_epoch();
    const std::uint64_t ticks = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(since).count());
    return ticks;
}

std::string size_label(int side) {
    const std::string number = std::to_string(side);
    return number + " \xC3\x97 " + number;
}

}  // namespace

TemplateView::TemplateView(gf::StableId id, Options options)
    : Control(std::move(id)), options_(options) {
    front_ = !options_.hosted;
    set_focusable(true);
    set_style(gf::ControlStyles::opaque, true);
    gf::SurfaceMaterial plain;
    plain.fills = {gf::MaterialFillLayer::solid(gf::Color::rgba(0, 0, 0))};
    set_authored_surface_material(plain);
    set_accessible_name("Template Game. Press lamps to light the whole board.");
    if (!restore()) {
        session_.board = new_game(session_.next_side, seed_from_clock());
    }
    snap(visual_, session_.board);
    last_input_ = std::chrono::steady_clock::now();
}

TemplateView::~TemplateView() = default;

void TemplateView::on_attached_to_window() {
    timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::milliseconds(16));
    subscriptions_.push_back((*timer_).tick().subscribe(
        *this, gf::Delegate<>::bind<TemplateView, &TemplateView::tick>(*this)));
    request_frame();
}

void TemplateView::on_detaching_from_window(gf::Window&) noexcept {
    try {
        persist();
    } catch (...) {
    }
    if (timer_) {
        (*timer_).stop();
    }
    timer_.reset();
    audio_stop();
}

void TemplateView::activate() {
    gf::Window* window = attached_window();
    if (window != nullptr) {
        static_cast<void>((*window).request_focus(shared_from_this()));
    }
    request_frame();
}

void TemplateView::set_cabinet(bool foreground, bool music, bool sound, bool reduced) {
    front_ = foreground;
    music_ = music;
    sound_ = sound;
    reduced_ = reduced;
    audio_cabinet(foreground, music, sound);
    if (!foreground) {
        // Leaving the front: nothing may keep running. The position is already saved.
        visual_.hover = -1;
        if (timer_) {
            (*timer_).stop();
        }
        return;
    }
    // Time spent behind the shelf is not time spent waiting on the player.
    last_input_ = std::chrono::steady_clock::now();
    sync_music();
    request_frame();
}

void TemplateView::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    gf::Window* window = attached_window();
    scale_ = window != nullptr ? (*window).scale() : 1.0;
    layout_ = compute_layout(bounds.width, bounds.height);
    device_width_ = std::max(1, static_cast<int>(std::lround(bounds.width * scale_)));
    device_height_ = std::max(1, static_cast<int>(std::lround(bounds.height * scale_)));
    frame_.resize(device_width_, device_height_);
    if (surface_) {
        gf::LiveSurfaceDescription description;
        description.width = static_cast<std::uint32_t>(device_width_);
        description.height = static_cast<std::uint32_t>(device_height_);
        static_cast<void>((*surface_).reconfigure(description));
    }
    request_frame();
}

void TemplateView::on_paint(gf::Painter& painter, gf::Rect) {
    const gf::Rect bounds = client_rectangle();
    if (surface_) {
        // A retained repaint (an expose) shows the last published frame.
        painter.draw_live_surface(surface_, bounds);
    } else {
        painter.fill_rect(bounds, gf::Color::rgba(43, 32, 23));
    }
}

// Asks for one more frame. Safe to call often; it only restarts a stopped timer.
void TemplateView::request_frame() {
    render_dirty_ = true;
    if (!timer_ || !visible() || !front_) {
        return;
    }
    if (!(*timer_).enabled()) {
        last_tick_ = std::chrono::steady_clock::now();
    }
    (*timer_).set_interval(std::chrono::milliseconds(16));
    (*timer_).start();
}

// The player did something: restart the wait for the timed remark.
void TemplateView::note_player() {
    last_input_ = std::chrono::steady_clock::now();
    if (note_input(visual_)) {
        request_frame();
    }
}

void TemplateView::tick() {
    if (!visible() || !front_) {
        if (timer_) {
            (*timer_).stop();
        }
        return;
    }
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - last_tick_).count();
    const double seconds = std::clamp(elapsed, 0.0, .1);
    last_tick_ = now;
    const double idle = std::chrono::duration<double>(now - last_input_).count();
    if (update_remark(visual_, session_.board, idle)) {
        render_dirty_ = true;
    }
    const bool moving = advance(visual_, session_.board, seconds, reduced_);
    audio_tick(seconds);
    if (render_dirty_ || moving) {
        render_dirty_ = false;
        draw_scene(frame_, scale_, layout_, session_.board, visual_);
        publish();
    }
    if (moving || render_dirty_ || !timer_) {
        return;
    }
    // Settled. Keep a slow tick only while a sound is still loading or fading. If a
    // timed remark is pending, sleep until exactly then: one wake, no frames between.
    // Otherwise stop, and the game costs nothing until the next input.
    const double wait = seconds_until_remark(visual_, session_.board, idle);
    if (audio_needs_tick()) {
        (*timer_).set_interval(std::chrono::milliseconds(50));
    } else if (wait >= 0) {
        const long long milliseconds = static_cast<long long>(std::ceil(wait * 1000)) + 20;
        (*timer_).set_interval(std::chrono::milliseconds(milliseconds));
    } else {
        (*timer_).stop();
    }
}

void TemplateView::publish() {
    if (!surface_) {
        gf::LiveSurfaceDescription description;
        description.width = static_cast<std::uint32_t>(device_width_);
        description.height = static_cast<std::uint32_t>(device_height_);
        surface_ = gf::LiveSurface::create(description);
        gf::Window* window = attached_window();
        if (surface_ && window != nullptr) {
            direct_ = (*window).queue_live_surface_presentation(shared_from_this(), surface_);
        }
    }
    if (!surface_) {
        return;
    }
    gf::LiveSurfaceWriteLease lease = (*surface_).try_acquire_write();
    const bool matches = lease && static_cast<int>(lease.width()) == device_width_ &&
                         static_cast<int>(lease.height()) == device_height_ &&
                         frame_.w == device_width_ && frame_.h == device_height_;
    if (matches) {
        // The canvas and the surface are both BGRA premultiplied; copy row by row
        // because the surface's rows may be wider than the picture.
        const std::span<std::byte> destination = lease.pixels();
        const std::size_t row_bytes = lease.row_bytes();
        const std::size_t picture_bytes = static_cast<std::size_t>(device_width_) * 4;
        for (int y = 0; y < device_height_; ++y) {
            const std::size_t row = static_cast<std::size_t>(y);
            std::memcpy(destination.data() + row * row_bytes, frame_.px.data() + row * picture_bytes,
                        picture_bytes);
        }
        static_cast<void>(lease.publish());
    } else {
        // The surface was busy or is being resized: try again on the next tick.
        render_dirty_ = true;
    }
    if (!direct_) {
        invalidate(gf::Dirty::paint);
    }
}

void TemplateView::play(const std::string& name, float gain, float rate) {
    audio_sfx(name, gain, rate, sound_ && front_ && visible());
}

void TemplateView::sync_music() {
    // The template ships no music. Name your loop here, for example "tg_music".
    audio_music("", music_ && front_);
}

// ---------------------------------------------------------------- the game

void TemplateView::press_cell(int cell) {
    note_player();
    if (solved(session_.board)) {
        new_board();
        return;
    }
    if (!press(session_.board, cell)) {
        return;
    }
    // A lower note for a lamp going dark, a higher one for a lamp coming on.
    const bool now_lit = session_.board.lit[static_cast<std::size_t>(cell)] != 0;
    play("tg_press", .5f, now_lit ? 1.12f : .9f);
    if (solved(session_.board)) {
        play("tg_solved", .7f, 1);
    }
    persist();
    request_frame();
}

void TemplateView::undo_press() {
    note_player();
    if (solved(session_.board) || !undo(session_.board)) {
        return;
    }
    play("tg_press", .35f, .8f);
    persist();
    request_frame();
}

void TemplateView::new_board() {
    note_player();
    const int best = session_.next_side == session_.board.side ? session_.board.best : 0;
    session_.board = new_game(session_.next_side, seed_from_clock());
    session_.board.best = best;
    visual_.cursor = -1;
    play("tg_deal", .5f, 1);
    persist();
    request_frame();
}

void TemplateView::cycle_size() {
    set_next_size(session_.next_side >= maximum_side ? minimum_side : session_.next_side + 1);
}

void TemplateView::set_next_size(int side) {
    session_.next_side = side;
    // The size applies to the next board; a board nobody has touched is replaced at once.
    if (session_.board.moves.empty()) {
        new_board();
        return;
    }
    persist();
    request_frame();
}

void TemplateView::set_help(bool open) {
    if (open && games::route_help(*this)) {
        return;
    }
    note_player();
    visual_.help = open;
    request_frame();
}

void TemplateView::move_cursor(int rows, int columns) {
    const int side = session_.board.side;
    int row = visual_.cursor >= 0 ? visual_.cursor / side : 0;
    int column = visual_.cursor >= 0 ? visual_.cursor % side : 0;
    if (visual_.cursor >= 0) {
        row = std::clamp(row + rows, 0, side - 1);
        column = std::clamp(column + columns, 0, side - 1);
    }
    visual_.cursor = row * side + column;
    note_player();
    request_frame();
}

// ---------------------------------------------------------------- input

void TemplateView::on_pointer(gf::PointerEvent& event) {
    const gf::Point local = point_from_window(event.position);
    const int under = visual_.help ? -1 : pick_cell(layout_, session_.board.side, local.x, local.y);
    if (event.action == gf::PointerAction::move || event.action == gf::PointerAction::leave) {
        const int hover = event.action == gf::PointerAction::leave ? -1 : under;
        if (hover != visual_.hover) {
            visual_.hover = hover;
            set_cursor(hover >= 0 ? gf::CursorKind::hand : gf::CursorKind::arrow);
            request_frame();
        }
        return;
    }
    if (event.action == gf::PointerAction::down && event.button == gf::PointerButton::primary) {
        activate();
        if (visual_.help) {
            set_help(false);
        } else if (solved(session_.board)) {
            new_board();
        } else if (under >= 0) {
            visual_.cursor = -1;
            press_cell(under);
        }
        event.handled = true;
    }
}

void TemplateView::on_key(gf::KeyEvent& event) {
    if (event.handled || event.action != gf::KeyAction::down) {
        return;
    }
    using Key = gf::PhysicalKey;
    const std::uint32_t key = event.physical_key;
    const bool confirm = key == Key::enter || key == Key::space;
    event.handled = true;
    if (visual_.help) {
        if (key == Key::escape || key == Key::f1 || confirm) {
            set_help(false);
        }
        return;
    }
    if (key == Key::f1 || key == Key::h) {
        set_help(true);
    } else if (key == Key::z || key == Key::u || key == Key::backspace) {
        undo_press();
    } else if (key == Key::n) {
        new_board();
    } else if (key == Key::up) {
        move_cursor(-1, 0);
    } else if (key == Key::down) {
        move_cursor(1, 0);
    } else if (key == Key::left) {
        move_cursor(0, -1);
    } else if (key == Key::right) {
        move_cursor(0, 1);
    } else if (confirm && solved(session_.board)) {
        new_board();
    } else if (confirm && visual_.cursor >= 0) {
        press_cell(visual_.cursor);
    } else {
        event.handled = false;
    }
}

// ---------------------------------------------------------------- the capsule

std::vector<games::GameCommand> TemplateView::commands() const {
    const Board& board = session_.board;
    std::vector<games::GameCommand> list;
    // id, label, enabled, checked, primary. Primary commands show while the capsule is folded.
    list.push_back({"new", "New board", true, false, true});
    list.push_back({"undo", "Undo", !board.moves.empty() && !solved(board), false, false});
    list.push_back({"help", "Help", true, visual_.help, false});
    return list;
}

// ---------------------------------------------------------------- the Settings screen

std::vector<games::GameSetting> TemplateView::settings() const {
    // id, label, kind, value, choices, minimum, maximum, step, note. The shell draws them
    // under its own Music, Sound, volume and Motion controls. The game saves the value.
    games::GameSetting size{"size", "Board", games::GameSetting::Kind::choice,
                            static_cast<double>(session_.next_side - minimum_side)};
    for (int side = minimum_side; side <= maximum_side; ++side)
        size.choices.push_back(size_label(side));
    size.note = "An untouched board changes at once; otherwise the next board does.";
    return {size};
}

void TemplateView::change_setting(std::string_view id, double value) {
    const int side = minimum_side + static_cast<int>(std::lround(value));
    if (id == "size" && side >= minimum_side && side <= maximum_side && side != session_.next_side)
        set_next_size(side);
}

void TemplateView::run_command(std::string_view id) {
    if (id == "new") {
        set_help(false);
        new_board();
    } else if (id == "undo") {
        undo_press();
    } else if (id == "size") {
        cycle_size();
    } else if (id == "help") {
        set_help(!visual_.help);
    }
}

// ---------------------------------------------------------------- the save

std::filesystem::path TemplateView::save_path() const {
    const char* name = options_.dev ? "template_game-dev-v1.txt" : "template_game-v1.txt";
    // state_directory() honours GAMES_STATE_DIR, which tests use to isolate saves.
    const std::filesystem::path path = games::state_directory() / name;
    return path;
}

void TemplateView::persist() {
    static_cast<void>(write_save(save_path(), encode_session(session_)));
}

bool TemplateView::restore() {
    std::string body;
    if (!read_save(save_path(), body)) {
        return false;
    }
    const bool ok = decode_session(body, session_);
    return ok;
}

}  // namespace tg
