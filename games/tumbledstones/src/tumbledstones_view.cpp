#include "help_route.hpp"
#include "tumbledstones_view.hpp"

#include "platform/audio.hpp"
#include "save.hpp"

#include "gui_forms/surface_material.hpp"
#include "gui_forms/threading.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <thread>

namespace ts {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double turn_over_time = .26;
constexpr double turn_round_time = .17;

std::uint64_t seed_from_clock() {
    const std::chrono::system_clock::duration since = std::chrono::system_clock::now().time_since_epoch();
    const std::uint64_t ticks =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(since).count());
    return ticks ^ 0x5745ed5ULL;
}

double smoothstep(double x) {
    const double t = std::clamp(x, 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

// A few heavy jobs at a time (the stones' pictures, the tray's bands), shared by GUI.Forms
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

bool neighbours(int a, int b) {
    const int dr = a / side - b / side;
    const int dc = a % side - b % side;
    return (dr == 0 && (dc == 1 || dc == -1)) || (dc == 0 && (dr == 1 || dr == -1));
}

}  // namespace

TumbledStonesView::TumbledStonesView(gf::StableId id, Options options) : Control(std::move(id)), options_(options) {
    front_ = !options_.hosted;
    set_focusable(true);
    set_style(gf::ControlStyles::opaque, true);
    gf::SurfaceMaterial plain;
    plain.fills = {gf::MaterialFillLayer::solid(gf::Color::rgba(30, 19, 12))};
    set_authored_surface_material(plain);
    set_accessible_name(
        "Tumbled Stones. Flip and turn polished stones until neighbours of one shape lie the same way, and pull "
        "the size of group you chose.");
    scene_.set_runner(Runner{crew_run, &crew()});
    if (!restore()) {
        session_ = Session{};
        session_.board = new_game(seed_from_clock(), session_.next_target);
        session_.playing = false;
    }
    choosing_ = session_.next_target;
    if (!session_.playing)
        panel_ = Panel::choose;
    else if (session_.board.won)
        panel_ = Panel::won;
    motion_.settle(session_.board);
}

TumbledStonesView::~TumbledStonesView() {
    stop_studio();
}

void TumbledStonesView::run_studio(const gf::CancellationFlag&, void* context) {
    TumbledStonesView& view = *static_cast<TumbledStonesView*>(context);
    view.scene_.run_batch();
    view.studio_done_.store(true);
}

void TumbledStonesView::stop_studio() noexcept {
    if (studio_) {
        (*studio_).join();
        studio_.reset();
        scene_.end_batch();
    }
}

// Collects what the frame lacks and sets it making: a little at once, here (a stone set
// down, a few new ones), anything more on the workshop's worker while play goes on.
void TumbledStonesView::keep_studio_busy() {
    if (studio_ && studio_done_.load()) {
        (*studio_).join();
        studio_.reset();
        scene_.end_batch();
        render_dirty_ = true;
    }
    scene_.want(frame());
    if (studio_)
        return;
    const bool small = !scene_.shapes_waiting() && scene_.pictures_waiting() <= 6;
    if (!scene_.begin_batch())
        return;
    if (small) {
        scene_.run_batch();
        scene_.end_batch();
        render_dirty_ = true;
        return;
    }
    studio_done_.store(false);
    try {
        studio_ = std::make_unique<gf::Worker>(run_studio, this);
    } catch (...) {
        scene_.run_batch();
        scene_.end_batch();
        render_dirty_ = true;
    }
}

void TumbledStonesView::on_attached_to_window() {
    gf::Window& window = *attached_window();
    timer_ = std::make_unique<gf::Timer>(window, std::chrono::milliseconds(16));
    subscriptions_.push_back(
        (*timer_).tick().subscribe(*this, gf::Delegate<>::bind<TumbledStonesView, &TumbledStonesView::tick>(*this)));
    subscriptions_.push_back(window.active_changed().subscribe(
        *this, gf::Delegate<bool>::bind<TumbledStonesView, &TumbledStonesView::window_activation>(*this)));
    request_frame();
}

void TumbledStonesView::on_detaching_from_window(gf::Window&) noexcept {
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

void TumbledStonesView::window_activation(bool active) {
    if (!active)
        cancel_press();
}

void TumbledStonesView::activate() {
    gf::Window* window = attached_window();
    if (window != nullptr)
        static_cast<void>((*window).request_focus(shared_from_this()));
    request_frame();
}

void TumbledStonesView::set_cabinet(bool foreground, bool music, bool sound, bool reduced) {
    front_ = foreground;
    music_ = music;
    sound_ = sound;
    reduced_ = reduced;
    audio_cabinet(foreground, music, sound);
    if (!foreground) {
        cancel_press();
        hover_ = -1;
        if (timer_)
            (*timer_).stop();
        return;
    }
    sync_music();
    request_frame();
}

void TumbledStonesView::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    gf::Window* window = attached_window();
    scale_ = window != nullptr ? (*window).scale() : 1.0;
    const Layout next = compute_layout(bounds.width, bounds.height);
    const int width = std::max(1, static_cast<int>(std::lround(bounds.width * scale_)));
    const int height = std::max(1, static_cast<int>(std::lround(bounds.height * scale_)));
    const bool changed = !laid_out_ || width != device_width_ || height != device_height_ ||
                         next.cell != layout_.cell || next.grid_x != layout_.grid_x || next.grid_y != layout_.grid_y;
    layout_ = next;
    device_width_ = width;
    device_height_ = height;
    if (changed) {
        laid_out_ = false;
        static_cast<void>(surface_.configure(device_width_, device_height_));
    }
    request_frame();
}

void TumbledStonesView::on_paint(gf::Painter& painter, gf::Rect) {
    const gf::Rect bounds = client_rectangle();
    if (surface_.live() && surface_attached_)
        painter.draw_live_surface(surface_.live(), bounds);
    else
        painter.fill_rect(bounds, gf::Color::rgba(30, 19, 12));
}

void TumbledStonesView::request_frame() {
    render_dirty_ = true;
    if (!timer_ || !visible() || !front_)
        return;
    if (!(*timer_).enabled())
        last_tick_ = std::chrono::steady_clock::now();
    (*timer_).set_interval(std::chrono::milliseconds(16));
    (*timer_).start();
}

bool TumbledStonesView::busy() const {
    return motion_.moving();
}

void TumbledStonesView::tick() {
    if (!visible() || !front_) {
        if (timer_)
            (*timer_).stop();
        return;
    }
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - last_tick_).count();
    const double seconds = std::clamp(elapsed, 0.0, .1);
    last_tick_ = now;
    const bool turning = !hand_.turns.empty();
    step_hand(seconds);
    const bool settling = hover_ >= 0 && hover_time_ < 1.4 && !reduced_;
    hover_time_ = std::min(10.0, hover_time_ + seconds);
    const bool was_moving = motion_.moving();
    const bool moving = motion_.advance(seconds);
    land_sounds_ = 0;
    for (const Motion::Cue& cue : motion_.take_cues()) {
        if (cue.kind == 1) {
            play("ts_pull", .55F, static_cast<float>(.86 + .035 * std::min(cue.size, 14)));
        } else if (cue.kind == 2 && land_sounds_ < 3) {
            ++land_sounds_;
            play("ts_land", .22F, static_cast<float>(.88 + .25 * cue.x));
        } else if (cue.kind == 3) {
            play("ts_tumble", .55F, 1);
        } else if (cue.kind == 4) {
            play("ts_settle", .5F, 1);
        }
    }
    keep_studio_busy();
    if (was_moving && !moving) {
        scene_.forget_unused(session_);
        render_dirty_ = true;
        if (win_waiting_) {
            win_waiting_ = false;
            panel_ = Panel::won;
            play("ts_win", .7F, 1);
        }
    }
    audio_tick(seconds);
    if (render_dirty_ || moving || was_moving || turning || settling)
        publish();
    if (moving || render_dirty_ || !hand_.turns.empty() || settling || !timer_)
        return;
    if (studio_) {
        // The workshop is still making pictures: look in again shortly.
        (*timer_).set_interval(std::chrono::milliseconds(30));
        return;
    }
    if (audio_needs_tick())
        (*timer_).set_interval(std::chrono::milliseconds(50));
    else
        (*timer_).stop();
}

Frame TumbledStonesView::frame() const {
    Frame f;
    f.session = &session_;
    f.motion = &motion_;
    f.hover = panel_ == Panel::none && !busy() ? hover_ : -1;
    f.hover_x = pointer_x_;
    f.hover_y = pointer_y_;
    // The stone under the pointer lifts a touch and rocks, the rocking dying away.
    const double t = reduced_ ? 10.0 : hover_time_;
    f.hover_lift = smoothstep(t / .22);
    f.hover_spin = reduced_ ? 0.0 : .04 * std::exp(-t / .3) * std::sin(t * 18);
    f.cursor = keyboard_ && panel_ == Panel::none ? cursor_ : -1;
    if (hand_.serial != 0) {
        f.held = hand_.serial;
        f.held_cell = hand_.cell;
        f.held_orientation = hand_.orientation;
        f.held_turning = !hand_.turns.empty();
        f.held_pose = hand_pose();
    }
    if (dragging_ && press_cell_ >= 0) {
        f.drag_cell = press_cell_;
        f.drag_x = drag_x_;
        f.drag_y = drag_y_;
        f.drag_to = drag_to_;
        f.drag_allowed = drag_to_ >= 0 && can_swap(session_.board, press_cell_, drag_to_);
    }
    f.panel = panel_;
    f.choosing = choosing_;
    f.can_cancel = session_.playing && !session_.board.won;
    f.new_best = new_best_;
    f.pressed_button = pressed_button_;
    f.help = help_;
    return f;
}

void TumbledStonesView::publish() {
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
    surface_.publish();
    if (!surface_.direct())
        invalidate(gf::Dirty::paint);
}

void TumbledStonesView::play(const std::string& name, float gain, float rate) {
    audio_sfx(name, gain, rate, sound_ && front_ && visible());
}

void TumbledStonesView::sync_music() {
    audio_music("ts_music", music_ && front_);
}

// ---------------------------------------------------------------- the stone in hand

void TumbledStonesView::pick_up(int cell) {
    if (cell < 0 || cell >= cell_count || busy() || session_.board.won || !session_.playing)
        return;
    const Stone& stone = session_.board.cells[static_cast<std::size_t>(cell)];
    if (stone.family < 0 || hand_.serial == stone.serial)
        return;
    if (hand_.serial != 0) {
        set_down();
        if (busy())
            return;
    }
    hand_ = Hand{};
    hand_.serial = session_.board.cells[static_cast<std::size_t>(cell)].serial;
    hand_.cell = cell;
    hand_.start = session_.board.cells[static_cast<std::size_t>(cell)].orientation;
    hand_.orientation = hand_.start;
    play("ts_lift", .35F, static_cast<float>(.95 + .1 * (cell % side) / side));
    request_frame();
}

void TumbledStonesView::turn_over() {
    if (hand_.serial == 0)
        return;
    if (!reduced_) {
        if (hand_.turns.size() >= 3)
            hand_.turns.erase(hand_.turns.begin());
        hand_.turns.push_back(Hand::Turn{1, pi, hand_.orientation});
    }
    hand_.orientation = flipped(hand_.orientation);
    play("ts_flip", .45F, 1);
    request_frame();
}

void TumbledStonesView::turn_round() {
    if (hand_.serial == 0)
        return;
    if (!reduced_) {
        if (hand_.turns.size() >= 3)
            hand_.turns.erase(hand_.turns.begin());
        hand_.turns.push_back(Hand::Turn{2, -pi / 2, hand_.orientation});
    }
    hand_.orientation = turned(hand_.orientation);
    play("ts_turn", .4F, 1);
    request_frame();
}

void TumbledStonesView::step_hand(double seconds) {
    double left = seconds;
    while (!hand_.turns.empty() && left > 0) {
        const double length = hand_.turns.front().axis == 1 ? turn_over_time : turn_round_time;
        const double need = (1 - hand_.progress) * length;
        if (left < need) {
            hand_.progress += left / length;
            left = 0;
        } else {
            left -= need;
            hand_.turns.erase(hand_.turns.begin());
            hand_.progress = 0;
        }
    }
}

Pose TumbledStonesView::hand_pose() const {
    if (hand_.turns.empty())
        return orientation_pose(hand_.orientation);
    const Hand::Turn& turn = hand_.turns.front();
    return turned(orientation_pose(turn.from), turn.axis, turn.angle * smoothstep(hand_.progress));
}

void TumbledStonesView::set_down() {
    if (hand_.serial == 0)
        return;
    const int cell = hand_.cell;
    const int orientation = hand_.orientation;
    hand_ = Hand{};
    Board before = session_.board;
    before.cells[static_cast<std::size_t>(cell)].orientation = orientation;
    const Outcome outcome = ts::set_down(session_.board, cell, orientation);
    if (!outcome.accepted) {
        request_frame();
        return;
    }
    play("ts_set", outcome.counted ? .45F : .3F, static_cast<float>(.92 + .16 * (cell % side) / side));
    show_outcome(before, outcome);
}

void TumbledStonesView::put_back() {
    if (hand_.serial == 0)
        return;
    hand_ = Hand{};
    play("ts_set", .25F, .9F);
    request_frame();
}

void TumbledStonesView::show_outcome(const Board& before, const Outcome& outcome) {
    motion_.plan(before, outcome, session_.board, reduced_);
    if (outcome.won) {
        new_best_ = record_win(session_);
        win_waiting_ = true;
        if (reduced_) {
            win_waiting_ = false;
            panel_ = Panel::won;
            play("ts_win", .7F, 1);
        }
    }
    persist();
    request_frame();
}

void TumbledStonesView::swap_with(int from, int to) {
    if (busy() || !session_.playing || session_.board.won)
        return;
    if (hand_.serial != 0) {
        set_down();
        if (busy())
            return;
    }
    if (!can_swap(session_.board, from, to)) {
        play("ts_deny", .4F, 1);
        request_frame();
        return;
    }
    // Shown as made: the moved stone where it was dropped, then whatever it pulls.
    Board before = session_.board;
    std::swap(before.cells[static_cast<std::size_t>(from)], before.cells[static_cast<std::size_t>(to)]);
    before.cells[static_cast<std::size_t>(from)].swapped = true;
    before.cells[static_cast<std::size_t>(to)].swapped = true;
    const Outcome outcome = swap_stones(session_.board, from, to);
    play("ts_set", .45F, 1.05F);
    show_outcome(before, outcome);
}

void TumbledStonesView::juggle() {
    if (busy() || !session_.playing || session_.board.won)
        return;
    put_back();
    const Board before = session_.board;
    const Outcome outcome = juggle_tray(session_.board);
    if (outcome.accepted)
        show_outcome(before, outcome);
}

void TumbledStonesView::open_chooser() {
    put_back();
    cancel_press();
    choosing_ = session_.next_target;
    panel_ = Panel::choose;
    play("ts_button", .3F, 1);
    request_frame();
}

void TumbledStonesView::set_help(bool open) {
    // Inside PlaySuite the shell's help document opens instead.
    if (open && games::route_help(*this))
        return;
    help_ = open;
    request_frame();
}

void TumbledStonesView::begin_game() {
    session_.next_target = choosing_;
    session_.board = new_game(seed_from_clock(), choosing_);
    session_.playing = true;
    panel_ = Panel::none;
    new_best_ = false;
    win_waiting_ = false;
    hand_ = Hand{};
    // The stones pour in from above.
    Board empty = session_.board;
    for (Stone& stone : empty.cells)
        stone = Stone{};
    Outcome pour;
    pour.accepted = true;
    motion_.plan(empty, pour, session_.board, reduced_);
    play("ts_button", .35F, 1.1F);
    persist();
    request_frame();
}

void TumbledStonesView::move_cursor(int rows, int columns, bool swap) {
    keyboard_ = true;
    if (cursor_ < 0) {
        cursor_ = hover_ >= 0 ? hover_ : 44;
        request_frame();
        return;
    }
    const int row = std::clamp(cursor_ / side + rows, 0, side - 1);
    const int column = std::clamp(cursor_ % side + columns, 0, side - 1);
    const int next = row * side + column;
    if (next == cursor_)
        return;
    if (swap) {
        swap_with(cursor_, next);
        cursor_ = next;
        return;
    }
    if (hand_.serial != 0 && hand_.cell != next)
        set_down();
    cursor_ = next;
    request_frame();
}

// ---------------------------------------------------------------- the panels

int TumbledStonesView::panel_button(double x, double y) const {
    if (panel_ == Panel::choose) {
        if (layout_.fewer.contains(x, y))
            return 1;
        if (layout_.more.contains(x, y))
            return 2;
        if (layout_.begin.contains(x, y))
            return 3;
        if (session_.playing && !session_.board.won && layout_.cancel.contains(x, y))
            return 4;
    } else if (panel_ == Panel::won) {
        if (layout_.begin.contains(x, y))
            return 3;
    }
    return 0;
}

void TumbledStonesView::press_panel_button(int button) {
    if (panel_ == Panel::choose) {
        if (button == 1) {
            choosing_ = std::max(smallest_target, choosing_ - 1);
            play("ts_turn", .3F, .9F);
        } else if (button == 2) {
            choosing_ = std::min(largest_target, choosing_ + 1);
            play("ts_turn", .3F, 1.1F);
        } else if (button == 3) {
            begin_game();
        } else if (button == 4) {
            panel_ = Panel::none;
            play("ts_button", .25F, .9F);
        }
    } else if (panel_ == Panel::won && button == 3) {
        open_chooser();
    }
    request_frame();
}

void TumbledStonesView::cancel_press() {
    if (press_cell_ >= 0 || dragging_ || pressed_button_ != 0)
        set_pointer_capture(false);
    press_cell_ = -1;
    dragging_ = false;
    drag_to_ = -1;
    pressed_button_ = 0;
}

// ---------------------------------------------------------------- input

void TumbledStonesView::on_pointer(gf::PointerEvent& event) {
    const gf::Point local = point_from_window(event.position);
    const double x = local.x;
    const double y = local.y;
    if (panel_ != Panel::none) {
        const int button = panel_button(x, y);
        if (event.action == gf::PointerAction::move) {
            set_cursor(button != 0 ? gf::CursorKind::hand : gf::CursorKind::arrow);
        } else if (event.action == gf::PointerAction::down && event.button == gf::PointerButton::primary) {
            activate();
            pressed_button_ = button;
            if (button != 0)
                set_pointer_capture(true);
            request_frame();
            event.handled = true;
        } else if (event.action == gf::PointerAction::up && event.button == gf::PointerButton::primary) {
            const int pressed = pressed_button_;
            cancel_press();
            if (pressed != 0 && pressed == button)
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
    const int under = pick_cell(layout_, x, y);
    if (event.action == gf::PointerAction::move || event.action == gf::PointerAction::leave) {
        const int hover = event.action == gf::PointerAction::leave ? -1 : under;
        const bool moved = std::abs(x - pointer_x_) + std::abs(y - pointer_y_) > .5;
        pointer_x_ = x;
        pointer_y_ = y;
        if (hover >= 0 && moved)
            render_dirty_ = true;  // the glint follows the pointer
        if (hover != hover_) {
            hover_ = hover;
            hover_time_ = 0;
            keyboard_ = false;
            set_cursor(hover >= 0 ? gf::CursorKind::hand : gf::CursorKind::arrow);
            render_dirty_ = true;
        }
        // Moving off the stone in hand sets it down.
        if (hand_.serial != 0 && hover != hand_.cell && !dragging_)
            set_down();
        if (press_cell_ >= 0 && !busy()) {
            const double moved = std::hypot(x - press_x_, y - press_y_);
            if (!dragging_ && moved > layout_.cell * .2) {
                dragging_ = true;
                if (hand_.serial != 0) {
                    const bool same = hand_.cell == press_cell_;
                    set_down();
                    if (busy() || (same && session_.board.cells[static_cast<std::size_t>(press_cell_)].family < 0)) {
                        cancel_press();
                        request_frame();
                        return;
                    }
                }
                play("ts_lift", .3F, 1.05F);
            }
            if (dragging_) {
                drag_x_ = x;
                drag_y_ = y;
                drag_to_ = under >= 0 && neighbours(under, press_cell_) ? under : -1;
                render_dirty_ = true;
            }
        }
        if (render_dirty_)
            request_frame();
        return;
    }
    if (event.action == gf::PointerAction::down) {
        activate();
        event.handled = true;
        if (busy() || under < 0 || !session_.playing || session_.board.won)
            return;
        if (event.button == gf::PointerButton::primary) {
            press_cell_ = under;
            press_x_ = x;
            press_y_ = y;
            dragging_ = false;
            set_pointer_capture(true);
        } else if (event.button == gf::PointerButton::secondary) {
            pick_up(under);
            turn_round();
        }
        return;
    }
    if (event.action == gf::PointerAction::up && event.button == gf::PointerButton::primary) {
        event.handled = true;
        const int pressed = press_cell_;
        const bool dragged = dragging_;
        const int target = drag_to_;
        cancel_press();
        if (pressed < 0) {
            request_frame();
            return;
        }
        if (dragged) {
            if (target >= 0)
                swap_with(pressed, target);
            else
                play("ts_set", .25F, .95F);
        } else if (under == pressed) {
            pick_up(under);
            turn_over();
        }
        request_frame();
    }
}

void TumbledStonesView::on_key(gf::KeyEvent& event) {
    if (event.handled || event.action != gf::KeyAction::down)
        return;
    using Key = gf::PhysicalKey;
    const std::uint32_t key = event.physical_key;
    const bool confirm = key == Key::enter || key == Key::space;
    const bool shift = gf::has_modifier(event.modifiers, gf::Modifier::shift);
    event.handled = true;
    if (help_) {
        if (key == Key::escape || key == Key::f1 || key == Key::h || confirm)
            set_help(false);
        return;
    }
    if (key == Key::f1 || key == Key::h) {
        set_help(true);
        return;
    }
    if (panel_ == Panel::choose) {
        if (key == Key::left || key == Key::down)
            press_panel_button(1);
        else if (key == Key::right || key == Key::up)
            press_panel_button(2);
        else if (confirm && !event.repeat)
            press_panel_button(3);
        else if (key == Key::escape && session_.playing && !session_.board.won)
            press_panel_button(4);
        else
            event.handled = false;
        return;
    }
    if (panel_ == Panel::won) {
        if (confirm && !event.repeat)
            press_panel_button(3);
        else if (key == Key::n && !event.repeat)
            open_chooser();
        else
            event.handled = false;
        return;
    }
    if (key == Key::up) {
        move_cursor(-1, 0, shift);
    } else if (key == Key::down) {
        move_cursor(1, 0, shift);
    } else if (key == Key::left) {
        move_cursor(0, -1, shift);
    } else if (key == Key::right) {
        move_cursor(0, 1, shift);
    } else if (event.repeat) {
        event.handled = true;
    } else if (key == Key::f || key == Key::r) {
        keyboard_ = true;
        if (cursor_ < 0)
            cursor_ = hover_ >= 0 ? hover_ : 44;
        pick_up(cursor_);
        if (key == Key::f)
            turn_over();
        else
            turn_round();
    } else if (confirm) {
        if (hand_.serial != 0) {
            set_down();
        } else if (cursor_ >= 0) {
            pick_up(cursor_);
            set_down();
        }
    } else if (key == Key::escape) {
        if (hand_.serial != 0)
            put_back();
        else
            event.handled = false;
    } else if (key == Key::j) {
        juggle();
    } else if (key == Key::n) {
        open_chooser();
    } else {
        event.handled = false;
    }
}

// ---------------------------------------------------------------- the capsule

std::vector<games::GameCommand> TumbledStonesView::commands() const {
    const bool playing = session_.playing && !session_.board.won && panel_ == Panel::none;
    std::vector<games::GameCommand> list;
    list.push_back({"new", "New game", true, panel_ == Panel::choose, true});
    list.push_back({"juggle", "Juggle", playing && !busy(), false, true});
    list.push_back({"help", "Help", true, help_, false});
    return list;
}

void TumbledStonesView::run_command(std::string_view id) {
    if (id == "new") {
        if (panel_ == Panel::choose && session_.playing && !session_.board.won)
            press_panel_button(4);
        else
            open_chooser();
    } else if (id == "juggle") {
        juggle();
    } else if (id == "help") {
        set_help(!help_);
    }
}

std::vector<games::GameSetting> TumbledStonesView::settings() const {
    return {};
}

void TumbledStonesView::change_setting(std::string_view, double) {}

bool TumbledStonesView::scripted_action(std::string_view action) {
    if (!options_.dev)
        return false;
    if (action == "flip") {
        pick_up(cursor_ >= 0 ? cursor_ : 44);
        turn_over();
    } else if (action == "turn") {
        pick_up(cursor_ >= 0 ? cursor_ : 44);
        turn_round();
    } else if (action == "set") {
        set_down();
    } else if (action == "juggle") {
        juggle();
    } else if (action.rfind("pick ", 0) == 0) {
        const std::string number(action.substr(5));
        const int cell = std::atoi(number.c_str());
        if (cell < 0 || cell >= cell_count)
            return false;
        cursor_ = cell;
        pick_up(cell);
    } else {
        return false;
    }
    request_frame();
    return true;
}

// ---------------------------------------------------------------- the save

std::string_view TumbledStonesView::save_name() const {
    return options_.dev ? "tumbled_stones-dev-v1.txt" : "tumbled_stones-v1.txt";
}

void TumbledStonesView::persist() {
    static_cast<void>(write_save(save_name(), encode_session(session_)));
}

bool TumbledStonesView::restore() {
    std::string body;
    if (!read_save(save_name(), body))
        return false;
    const bool ok = decode_session(body, session_);
    return ok;
}

}  // namespace ts
