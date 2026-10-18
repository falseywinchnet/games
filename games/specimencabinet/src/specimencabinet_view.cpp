#include "help_route.hpp"
#include "specimencabinet_view.hpp"

#include "cards/table_art.hpp"
#include "platform/audio.hpp"
#include "platform/text.hpp"
#include "save.hpp"

#include "gui_forms/surface_material.hpp"
#include "gui_forms/threading.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <thread>

namespace sc {
namespace {

constexpr double turn_time = .36;   // the drum turns before a stone spills
constexpr double fall_time = .3;    // and the stone falls to the pile
constexpr double turn_speed = 4.5;  // radians a second while it turns

std::uint64_t seed_from_clock() {
    const std::chrono::system_clock::duration since = std::chrono::system_clock::now().time_since_epoch();
    const std::uint64_t ticks =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(since).count());
    return ticks ^ 0x5bec1e5ULL;
}

// A few heavy jobs at a time (the stones' pictures, the table's bands), shared by GUI.Forms
// Workers and the caller, all taking jobs from one count: the card table's crew
// (shared/cards/table_art.cpp), borrowed.
struct Crew {
    int threads = 1;
};
struct Round {
    void (*job)(void*, int) noexcept;
    void* context;
    int count;
    std::atomic<int> next{0};
};
void take_jobs(Round& round) noexcept {
    for (int i = round.next.fetch_add(1); i < round.count; i = round.next.fetch_add(1))
        round.job(round.context, i);
}
void crew_member(const gf::CancellationFlag&, void* context) {
    take_jobs(*static_cast<Round*>(context));
}
void crew_run(void* runner, int count, void (*job)(void*, int) noexcept, void* context) {
    const Crew& crew = *static_cast<const Crew*>(runner);
    Round round{job, context, count};
    std::vector<std::unique_ptr<gf::Worker>> helpers;
    const int wanted = std::min(crew.threads, count) - 1;
    try {
        for (int i = 0; i < wanted; ++i)
            helpers.push_back(std::make_unique<gf::Worker>(crew_member, &round));
    } catch (...) {
        // fewer helpers than hoped: the caller and those started do the work
    }
    take_jobs(round);
    for (std::unique_ptr<gf::Worker>& helper : helpers)
        (*helper).join();
}
Crew& crew() {
    static Crew shared{static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1U, 8U))};
    return shared;
}

int size_index(int n) {
    for (std::size_t k = 0; k < sizes.size(); ++k)
        if (sizes[k] == n)
            return static_cast<int>(k);
    return 1;
}

}  // namespace

SpecimenCabinetView::SpecimenCabinetView(gf::StableId id, Options options) : Control(std::move(id)), options_(options) {
    front_ = !options_.hosted;
    set_focusable(true);
    set_style(gf::ControlStyles::opaque, true);
    gf::SurfaceMaterial plain;
    plain.fills = {gf::MaterialFillLayer::solid(gf::Color::rgba(36, 23, 14))};
    set_authored_surface_material(plain);
    set_accessible_name(
        "Specimen Cabinet. Set every stone in the cabinet so that no row and no column holds two stones of one "
        "shape or of one material.");
    scene_.set_runner(Runner{crew_run, &crew()});
    if (!restore()) {
        session_ = Session{};
        new_game(session_, seed_from_clock(), 5);
        session_.playing = false;
    }
    choosing_ = size_index(session_.next_n);
    if (!session_.playing)
        panel_ = Panel::choose;
    else if (session_.won)
        panel_ = Panel::won;
}

SpecimenCabinetView::~SpecimenCabinetView() {
    stop_studio();
}

void SpecimenCabinetView::run_studio(const gf::CancellationFlag&, void* context) {
    SpecimenCabinetView& view = *static_cast<SpecimenCabinetView*>(context);
    view.scene_.run_batch();
    view.studio_done_.store(true);
}

void SpecimenCabinetView::stop_studio() noexcept {
    if (studio_) {
        (*studio_).join();
        studio_.reset();
        scene_.end_batch();
    }
}

// Collects what the frame lacks and sets it making on the workshop's worker while play
// goes on.
void SpecimenCabinetView::keep_studio_busy() {
    if (studio_ && studio_done_.load()) {
        (*studio_).join();
        studio_.reset();
        scene_.end_batch();
        render_dirty_ = true;
    }
    if (!laid_out_)
        return;
    scene_.want(frame());
    if (studio_ || !scene_.begin_batch())
        return;
    studio_done_.store(false);
    try {
        studio_ = std::make_unique<gf::Worker>(run_studio, this);
    } catch (...) {
        scene_.run_batch();
        scene_.end_batch();
        render_dirty_ = true;
    }
}

void SpecimenCabinetView::on_attached_to_window() {
    gf::Window& window = *attached_window();
    timer_ = std::make_unique<gf::Timer>(window, std::chrono::milliseconds(16));
    subscriptions_.push_back(
        (*timer_).tick().subscribe(*this, gf::Delegate<>::bind<SpecimenCabinetView, &SpecimenCabinetView::tick>(*this)));
    subscriptions_.push_back(window.active_changed().subscribe(
        *this, gf::Delegate<bool>::bind<SpecimenCabinetView, &SpecimenCabinetView::window_activation>(*this)));
    request_frame();
}

void SpecimenCabinetView::on_detaching_from_window(gf::Window&) noexcept {
    try {
        persist();
    } catch (...) {
    }
    if (timer_)
        (*timer_).stop();
    timer_.reset();
    subscriptions_.clear();
    stop_studio();
    surface_.detach();
    surface_attached_ = false;
    audio_stop();
}

void SpecimenCabinetView::window_activation(bool active) {
    if (!active)
        cancel_press();
}

void SpecimenCabinetView::activate() {
    gf::Window* window = attached_window();
    if (window != nullptr)
        static_cast<void>((*window).request_focus(shared_from_this()));
    request_frame();
}

void SpecimenCabinetView::set_cabinet(bool foreground, bool music, bool sound, bool reduced) {
    front_ = foreground;
    music_ = music;
    sound_ = sound;
    reduced_ = reduced;
    audio_cabinet(foreground, music, sound);
    if (!foreground) {
        cancel_press();
        if (timer_)
            (*timer_).stop();
        persist();
        return;
    }
    sync_music();
    request_frame();
}

void SpecimenCabinetView::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    gf::Window* window = attached_window();
    scale_ = window != nullptr ? (*window).scale() : 1.0;
    width_ = bounds.width;
    height_ = bounds.height;
    const Layout next = compute_layout(bounds.width, bounds.height, session_.puzzle.n);
    const int width = std::max(1, static_cast<int>(std::lround(bounds.width * scale_)));
    const int height = std::max(1, static_cast<int>(std::lround(bounds.height * scale_)));
    const bool changed = !laid_out_ || width != device_width_ || height != device_height_ || next.cell != layout_.cell ||
                         next.cabinet.x != layout_.cabinet.x || next.cabinet.y != layout_.cabinet.y;
    layout_ = next;
    device_width_ = width;
    device_height_ = height;
    if (changed) {
        laid_out_ = false;
        static_cast<void>(surface_.configure(device_width_, device_height_));
    }
    request_frame();
}

void SpecimenCabinetView::on_paint(gf::Painter& painter, gf::Rect) {
    const gf::Rect bounds = client_rectangle();
    if (surface_.live() && surface_attached_)
        painter.draw_live_surface(surface_.live(), bounds);
    else
        painter.fill_rect(bounds, gf::Color::rgba(36, 23, 14));
}

void SpecimenCabinetView::request_frame() {
    render_dirty_ = true;
    if (!timer_ || !visible() || !front_)
        return;
    if (!(*timer_).enabled())
        last_tick_ = std::chrono::steady_clock::now();
    (*timer_).set_interval(std::chrono::milliseconds(16));
    (*timer_).start();
}

bool SpecimenCabinetView::clock_running() const {
    return session_.playing && !session_.won && panel_ == Panel::none && !help_ && front_ && visible();
}

bool SpecimenCabinetView::dropping() const {
    return !waiting_.empty();
}

// The tumbler turns, a stone spills over the lip and falls onto the pile; then the next.
void SpecimenCabinetView::step_drops(double seconds) {
    if (waiting_.empty())
        return;
    if (drop_wait_ >= 0) {
        // A new cabinet's stones are shaped and drawn in the background: the drops wait
        // for them (a few seconds at most), or the stones would fall unseen.
        drop_wait_ += seconds;
        if (scene_.pictures_ready(session_) || drop_wait_ > 4)
            drop_wait_ = -1;
        else
            return;
    }
    if (drop_time_ == 0)
        play("sc_tumble", .3F, 1.15F);
    drop_time_ += seconds;
    turn_ += seconds * turn_speed * (drop_time_ < turn_time ? 1.0 : .4);
    if (drop_time_ >= turn_time + fall_time) {
        const int stone = waiting_.front();
        waiting_.erase(waiting_.begin());
        drop_time_ = 0;
        play("sc_land", .4F, static_cast<float>(.9 + .05 * (stone % 5)));
    }
    render_dirty_ = true;
}

void SpecimenCabinetView::tick() {
    if (!visible() || !front_) {
        if (timer_)
            (*timer_).stop();
        return;
    }
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - last_tick_).count();
    const double seconds = std::clamp(elapsed, 0.0, .1);
    last_tick_ = now;
    if (clock_running()) {
        session_.seconds += std::max(0.0, std::min(elapsed, 1.5));
        if (static_cast<int>(session_.seconds) != shown_second_)
            render_dirty_ = true;
    }
    step_drops(seconds);
    keep_studio_busy();
    audio_tick(seconds);
    if (render_dirty_)
        publish();
    if (render_dirty_ || dropping() || !timer_)
        return;
    if (studio_) {
        // The workshop is still making pictures: look in again shortly.
        (*timer_).set_interval(std::chrono::milliseconds(30));
        return;
    }
    if (audio_needs_tick()) {
        (*timer_).set_interval(std::chrono::milliseconds(50));
    } else if (clock_running()) {
        // Wake for the clock's next second.
        const double into = session_.seconds - std::floor(session_.seconds);
        (*timer_).set_interval(std::chrono::milliseconds(static_cast<int>((1 - into) * 1000) + 5));
    } else {
        (*timer_).stop();
    }
}

Frame SpecimenCabinetView::frame() const {
    Frame f;
    f.session = &session_;
    f.stones = session_.playing;
    f.waiting = &waiting_;
    if (!waiting_.empty() && drop_wait_ < 0 && drop_time_ >= turn_time) {
        f.falling = waiting_.front();
        f.fall = (drop_time_ - turn_time) / fall_time;
    }
    f.turn = turn_;
    f.cursor = keyboard_ && panel_ == Panel::none ? cursor_ : -1;
    if (held_ >= 0) {
        f.held = held_;
        f.held_x = held_x_;
        f.held_y = held_y_;
        f.target = target_;
        if (target_ >= 0) {
            const int there = stone_in(session_, target_);
            f.target_ok = there < 0 || !session_.puzzle.clue[static_cast<std::size_t>(target_)];
        }
    }
    f.clashes = session_.playing && !session_.won && cabinet_full(session_);
    f.panel = panel_;
    f.choosing = choosing_;
    f.can_cancel = session_.playing && !session_.won;
    f.new_best = new_best_;
    f.pressed = pressed_button_;
    f.help = help_;
    return f;
}

void SpecimenCabinetView::publish() {
    render_dirty_ = false;
    if (!surface_.configure(device_width_, device_height_)) {
        render_dirty_ = true;
        return;
    }
    gf::Window* window = attached_window();
    if (!surface_attached_ && window != nullptr) {
        static_cast<void>(surface_.attach(*window, shared_from_this()));
        surface_attached_ = true;
        invalidate(gf::Dirty::paint);
    }
    if (!felt_given_) {
        // The card tables' felt, made once for the suite.
        felt_given_ = true;
        scene_.set_felt(games::felt_tile(), games::felt_tile_side);
    }
    if (!laid_out_) {
        scene_.resize(layout_, scale_, surface_.order());
        laid_out_ = true;
        keep_studio_busy();
    }
    const std::optional<render::Surface::Frame> drawn = surface_.begin(render::Rect{0, 0, device_width_, device_height_});
    if (!drawn) {
        render_dirty_ = true;
        return;
    }
    scene_.draw((*drawn).target, frame());
    text_cache_trim();
    shown_second_ = static_cast<int>(session_.seconds);
    surface_.publish();
    if (!surface_.direct())
        invalidate(gf::Dirty::paint);
}

void SpecimenCabinetView::play(const std::string& name, float gain, float rate) {
    audio_sfx(name, gain, rate, sound_ && front_ && visible());
}

void SpecimenCabinetView::sync_music() {
    audio_music("sc_music", music_ && front_);
}

// ---------------------------------------------------------------- the stone in hand

void SpecimenCabinetView::take(int stone, double x, double y) {
    if (stone < 0 || !movable(session_, stone) || session_.won || !session_.playing)
        return;
    held_ = stone;
    double sx = 0;
    double sy = 0;
    stone_point(layout_, session_, stone, sx, sy);
    grip_x_ = x - sx;
    grip_y_ = y - sy;
    held_x_ = sx;
    held_y_ = sy;
    target_ = pick_cell(layout_, held_x_, held_y_);
    play("sc_lift", .35F, static_cast<float>(.95 + .02 * (stone % 6)));
    keep_studio_busy();
    request_frame();
}

// Where a stone in hand goes when it is let go at the point (x, y): a socket, the felt, or
// back where it came from.
void SpecimenCabinetView::let_go(double x, double y) {
    if (held_ < 0)
        return;
    const double cx = x - grip_x_;
    const double cy = y - grip_y_;
    const int cell = pick_cell(layout_, cx, cy);
    if (cell >= 0)
        set_held_in(cell);
    else if (inside(layout_.felt, cx, cy))
        lay_held_on_felt(cx, cy);
    else
        put_back();
}

void SpecimenCabinetView::put_back() {
    if (held_ < 0)
        return;
    held_ = -1;
    carrying_ = false;
    target_ = -1;
    play("sc_set", .2F, .9F);
    request_frame();
}

void SpecimenCabinetView::set_held_in(int cell) {
    const int stone = held_;
    if (stone < 0)
        return;
    const Outcome outcome = set_in_cabinet(session_, stone, cell);
    if (!outcome.accepted) {
        // A pinned stone's socket, or the socket it came from: it goes back.
        if (stone_in(session_, cell) != stone)
            play("sc_deny", .4F, 1);
        put_back();
        return;
    }
    held_ = -1;
    carrying_ = false;
    target_ = -1;
    play("sc_set", .45F, static_cast<float>(.92 + .16 * (cell % layout_.n) / layout_.n));
    show_outcome(outcome);
}

void SpecimenCabinetView::lay_held_on_felt(double x, double y) {
    const int stone = held_;
    if (stone < 0)
        return;
    double fx = 0;
    double fy = 0;
    felt_fraction(layout_, x, y, fx, fy);
    const Outcome outcome = set_on_felt(session_, stone, fx, fy);
    held_ = -1;
    carrying_ = false;
    target_ = -1;
    play("sc_set", .3F, 1.05F);
    show_outcome(outcome);
}

void SpecimenCabinetView::show_outcome(const Outcome& outcome) {
    if (reduced_) {
        if (!outcome.dropped.empty())
            play("sc_land", .35F, 1);
    } else {
        waiting_.insert(waiting_.end(), outcome.dropped.begin(), outcome.dropped.end());
    }
    if (outcome.won) {
        new_best_ = outcome.new_best;
        panel_ = Panel::won;
        play("sc_win", .7F, 1);
    }
    persist();
    request_frame();
}

void SpecimenCabinetView::take_from_pile() {
    // The next stone on the pile after the one in hand, round and round.
    std::vector<int> choices;
    for (int stone : session_.pile)
        if (std::find(waiting_.begin(), waiting_.end(), stone) == waiting_.end())
            choices.push_back(stone);
    if (choices.empty())
        return;
    std::size_t next = 0;
    if (held_ >= 0) {
        std::vector<int>::iterator found = std::find(choices.begin(), choices.end(), held_);
        if (found != choices.end())
            next = (static_cast<std::size_t>(found - choices.begin()) + 1) % choices.size();
    }
    held_ = -1;
    take(choices[next], 0, 0);
    grip_x_ = 0;
    grip_y_ = 0;
    carrying_ = true;
    aim_at_cursor();
}

// A stone carried by the keys lies over the cursor's socket.
void SpecimenCabinetView::aim_at_cursor() {
    if (cursor_ < 0)
        cursor_ = (layout_.n / 2) * layout_.n + layout_.n / 2;
    if (held_ < 0)
        return;
    const Box b = cell_box(layout_, cursor_);
    held_x_ = b.x + b.w * .5;
    held_y_ = b.y + b.h * .5;
    target_ = cursor_;
    render_dirty_ = true;
}

void SpecimenCabinetView::move_cursor(int rows, int columns) {
    keyboard_ = true;
    const int n = layout_.n;
    if (cursor_ < 0 || cursor_ >= n * n) {
        cursor_ = (n / 2) * n + n / 2;
    } else {
        const int row = std::clamp(cursor_ / n + rows, 0, n - 1);
        const int column = std::clamp(cursor_ % n + columns, 0, n - 1);
        cursor_ = row * n + column;
    }
    aim_at_cursor();
    request_frame();
}

void SpecimenCabinetView::open_chooser() {
    put_back();
    cancel_press();
    choosing_ = size_index(session_.next_n);
    panel_ = Panel::choose;
    play("sc_button", .3F, 1);
    request_frame();
}

void SpecimenCabinetView::set_help(bool open) {
    // Inside PlaySuite the shell's help document opens instead.
    if (open && games::route_help(*this))
        return;
    help_ = open;
    request_frame();
}

void SpecimenCabinetView::begin_game(int n) {
    new_game(session_, seed_from_clock(), n);
    panel_ = Panel::none;
    new_best_ = false;
    held_ = -1;
    carrying_ = false;
    target_ = -1;
    cursor_ = -1;
    // The tumbler fills the pile, a stone at a time, once the stones are drawn.
    waiting_ = reduced_ ? std::vector<int>{} : session_.pile;
    drop_time_ = 0;
    drop_wait_ = reduced_ ? -1.0 : 0.0;
    play("sc_button", .35F, 1.1F);
    persist();
    layout_ = compute_layout(width_, height_, session_.puzzle.n);
    laid_out_ = false;
    request_frame();
}

// ---------------------------------------------------------------- the panels

int SpecimenCabinetView::panel_button(double x, double y) const {
    if (panel_ == Panel::choose) {
        for (std::size_t k = 0; k < layout_.choose.size(); ++k)
            if (inside(layout_.choose[k], x, y))
                return static_cast<int>(k);
        if (session_.playing && !session_.won && inside(layout_.cancel, x, y))
            return 3;
    } else if (panel_ == Panel::won) {
        if (inside(layout_.again, x, y))
            return 4;
    }
    return -1;
}

void SpecimenCabinetView::press_panel_button(int button) {
    if (panel_ == Panel::choose) {
        if (button >= 0 && button < static_cast<int>(sizes.size())) {
            choosing_ = button;
            begin_game(sizes[static_cast<std::size_t>(button)]);
        } else if (button == 3) {
            panel_ = Panel::none;
            play("sc_button", .25F, .9F);
        }
    } else if (panel_ == Panel::won && button == 4) {
        open_chooser();
    }
    request_frame();
}

void SpecimenCabinetView::cancel_press() {
    if (pressing_ || dragging_ || pressed_button_ >= 0)
        set_pointer_capture(false);
    if (dragging_ || pressing_) {
        // A stone dragged when the window loses the pointer goes back.
        held_ = -1;
        carrying_ = false;
        target_ = -1;
    }
    pressing_ = false;
    dragging_ = false;
    pressed_button_ = -1;
}

// ---------------------------------------------------------------- input

void SpecimenCabinetView::on_pointer(gf::PointerEvent& event) {
    const gf::Point local = point_from_window(event.position);
    const double x = local.x;
    const double y = local.y;
    if (panel_ != Panel::none) {
        const int button = panel_button(x, y);
        if (event.action == gf::PointerAction::move) {
            set_cursor(button >= 0 ? gf::CursorKind::hand : gf::CursorKind::arrow);
        } else if (event.action == gf::PointerAction::down && event.button == gf::PointerButton::primary) {
            activate();
            pressed_button_ = button;
            if (button >= 0)
                set_pointer_capture(true);
            request_frame();
            event.handled = true;
        } else if (event.action == gf::PointerAction::up && event.button == gf::PointerButton::primary) {
            const int pressed = pressed_button_;
            cancel_press();
            if (pressed >= 0 && pressed == button)
                press_panel_button(pressed);
            request_frame();
            event.handled = true;
        }
        return;
    }
    if (help_) {
        if (event.action == gf::PointerAction::down)
            set_help(false);
        event.handled = event.action == gf::PointerAction::down;
        return;
    }
    if (event.action == gf::PointerAction::move || event.action == gf::PointerAction::leave) {
        if (pressing_ && !dragging_ && std::hypot(x - press_x_, y - press_y_) > layout_.stone * .12)
            dragging_ = true;
        if (held_ >= 0 && (dragging_ || carrying_)) {
            held_x_ = x - grip_x_;
            held_y_ = y - grip_y_;
            target_ = pick_cell(layout_, held_x_, held_y_);
            keyboard_ = false;
            render_dirty_ = true;
            set_cursor(gf::CursorKind::hand);
        } else if (event.action == gf::PointerAction::move) {
            const int under = stone_under(layout_, session_, x, y, waiting_);
            set_cursor(under >= 0 ? gf::CursorKind::hand : gf::CursorKind::arrow);
        }
        if (render_dirty_)
            request_frame();
        return;
    }
    if (event.action == gf::PointerAction::down) {
        activate();
        event.handled = true;
        if (!session_.playing || session_.won)
            return;
        if (event.button == gf::PointerButton::secondary) {
            put_back();
            return;
        }
        if (event.button != gf::PointerButton::primary)
            return;
        keyboard_ = false;
        if (held_ >= 0 && carrying_) {
            // The second click of a carry sets the stone down here.
            let_go(x, y);
            return;
        }
        const int stone = stone_under(layout_, session_, x, y, waiting_);
        if (stone < 0)
            return;
        take(stone, x, y);
        pressing_ = true;
        dragging_ = false;
        press_x_ = x;
        press_y_ = y;
        set_pointer_capture(true);
        return;
    }
    if (event.action == gf::PointerAction::up && event.button == gf::PointerButton::primary) {
        event.handled = true;
        if (!pressing_)
            return;
        const bool dragged = dragging_;
        set_pointer_capture(false);
        pressing_ = false;
        dragging_ = false;
        if (held_ < 0)
            return;
        if (dragged) {
            let_go(x, y);
        } else {
            // A click: the stone is carried until the next.
            carrying_ = true;
            held_x_ = x - grip_x_;
            held_y_ = y - grip_y_;
            request_frame();
        }
    }
}

void SpecimenCabinetView::on_key(gf::KeyEvent& event) {
    if (event.handled || event.action != gf::KeyAction::down)
        return;
    using Key = gf::PhysicalKey;
    const std::uint32_t key = event.physical_key;
    const bool enter = key == Key::enter;
    event.handled = true;
    if (help_) {
        if (key == Key::escape || key == Key::f1 || key == Key::h || enter || key == Key::space)
            set_help(false);
        return;
    }
    if (key == Key::f1 || key == Key::h) {
        set_help(true);
        return;
    }
    if (panel_ == Panel::choose) {
        if (key == Key::left || key == Key::up) {
            choosing_ = std::max(0, choosing_ - 1);
            play("sc_turn", .3F, .9F);
            request_frame();
        } else if (key == Key::right || key == Key::down) {
            choosing_ = std::min(static_cast<int>(sizes.size()) - 1, choosing_ + 1);
            play("sc_turn", .3F, 1.1F);
            request_frame();
        } else if ((enter || key == Key::space) && !event.repeat) {
            press_panel_button(choosing_);
        } else if (key == Key::escape && session_.playing && !session_.won) {
            press_panel_button(3);
        } else {
            event.handled = false;
        }
        return;
    }
    if (panel_ == Panel::won) {
        if ((enter || key == Key::space || key == Key::n) && !event.repeat)
            press_panel_button(4);
        else
            event.handled = false;
        return;
    }
    if (key == Key::up) {
        move_cursor(-1, 0);
    } else if (key == Key::down) {
        move_cursor(1, 0);
    } else if (key == Key::left) {
        move_cursor(0, -1);
    } else if (key == Key::right) {
        move_cursor(0, 1);
    } else if (event.repeat) {
        event.handled = true;
    } else if (key == Key::space) {
        keyboard_ = true;
        take_from_pile();
    } else if (enter) {
        keyboard_ = true;
        if (cursor_ < 0) {
            move_cursor(0, 0);
        } else if (held_ >= 0) {
            set_held_in(cursor_);
        } else {
            const int stone = stone_in(session_, cursor_);
            if (stone >= 0 && movable(session_, stone)) {
                take(stone, 0, 0);
                grip_x_ = 0;
                grip_y_ = 0;
                carrying_ = true;
                aim_at_cursor();
            } else if (stone >= 0) {
                play("sc_deny", .3F, 1);
            }
        }
    } else if (key == Key::escape) {
        if (held_ >= 0)
            put_back();
        else
            event.handled = false;
    } else if (key == Key::n) {
        open_chooser();
    } else {
        event.handled = false;
    }
}

// ---------------------------------------------------------------- the capsule

std::vector<games::GameCommand> SpecimenCabinetView::commands() const {
    std::vector<games::GameCommand> list;
    list.push_back({"new", "New cabinet", true, panel_ == Panel::choose, true});
    list.push_back({"help", "Help", true, help_, false});
    return list;
}

void SpecimenCabinetView::run_command(std::string_view id) {
    if (id == "new") {
        if (panel_ == Panel::choose && session_.playing && !session_.won)
            press_panel_button(3);
        else
            open_chooser();
    } else if (id == "help") {
        set_help(!help_);
    }
}

std::vector<games::GameSetting> SpecimenCabinetView::settings() const {
    return {};
}

void SpecimenCabinetView::change_setting(std::string_view, double) {}

bool SpecimenCabinetView::scripted_action(std::string_view action) {
    if (!options_.dev)
        return false;
    const std::string text(action);
    if (text == "take") {
        take_from_pile();
    } else if (text.rfind("set ", 0) == 0) {
        const int cell = std::atoi(text.c_str() + 4);
        if (cell < 0 || cell >= session_.puzzle.n * session_.puzzle.n)
            return false;
        if (held_ < 0)
            take_from_pile();
        set_held_in(cell);
    } else if (text.rfind("felt ", 0) == 0) {
        char* end = nullptr;
        const double fx = std::strtod(text.c_str() + 5, &end);
        const double fy = std::strtod(end, nullptr);
        if (held_ < 0)
            take_from_pile();
        double x = 0;
        double y = 0;
        felt_point(layout_, fx, fy, x, y);
        lay_held_on_felt(x, y);
    } else if (text.rfind("solve", 0) == 0) {
        // Sets stones where the answer has them, any stone out of place, as the tumbler
        // gives them up.
        int count = text.size() > 6 ? std::atoi(text.c_str() + 6) : 0;
        if (count <= 0)
            count = most_stones;
        waiting_.clear();
        held_ = -1;
        carrying_ = false;
        target_ = -1;
        const int cells = session_.puzzle.n * session_.puzzle.n;
        for (int step = 0; step < cells * 4 && count > 0 && !session_.won; ++step) {
            int cell = 0;
            while (cell < cells && !(stone_in(session_, cell) != solution_stone(session_.puzzle, cell) &&
                                     movable(session_, solution_stone(session_.puzzle, cell))))
                ++cell;
            if (cell == cells)
                break;
            const Outcome outcome = set_in_cabinet(session_, solution_stone(session_.puzzle, cell), cell);
            if (outcome.won)
                new_best_ = outcome.new_best;
            --count;
        }
        if (session_.won)
            panel_ = Panel::won;
        persist();
    } else if (text.rfind("size ", 0) == 0) {
        const int n = std::atoi(text.c_str() + 5);
        if (std::find(sizes.begin(), sizes.end(), n) == sizes.end())
            return false;
        begin_game(n);
    } else {
        return false;
    }
    request_frame();
    return true;
}

// ---------------------------------------------------------------- the save

std::string_view SpecimenCabinetView::save_name() const {
    return options_.dev ? "specimen_cabinet-dev-v1.txt" : "specimen_cabinet-v1.txt";
}

void SpecimenCabinetView::persist() {
    static_cast<void>(write_save(save_name(), encode_session(session_)));
}

bool SpecimenCabinetView::restore() {
    std::string body;
    if (!read_save(save_name(), body))
        return false;
    return decode_session(body, session_);
}

}  // namespace sc
