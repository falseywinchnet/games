#include "cube_view.hpp"

#include "audio.hpp"
#include "portable_mask_cache.hpp"
#include "help_route.hpp"
#include "runtime_paths.hpp"
#include "r2d.hpp"
#include "scene.hpp"

#include "gui_forms/window.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace ps_cube {
namespace {


std::uint64_t seed_from_clock() {
    const std::chrono::system_clock::duration since = std::chrono::system_clock::now().time_since_epoch();
    const std::uint64_t ticks =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(since).count());
    return ticks;
}

std::vector<std::uint8_t> read_bytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    std::vector<std::uint8_t> bytes;
    if (!file) {
        return bytes;
    }
    const std::streamsize size = file.tellg();
    if (size <= 0 || size > 8 * 1024 * 1024) {
        return bytes;
    }
    file.seekg(0);
    bytes.resize(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!file) {
        bytes.clear();
    }
    return bytes;
}

// Runs on a worker: deals a board and nothing else.
Puzzle deal_board(int level, std::uint64_t seed, bool gentle, int portals, std::shared_ptr<std::atomic<bool>> cancel) {
    GenerateOptions options;
    options.gentle_portal = gentle;
    options.portals = portals;
    options.cancel = cancel.get();
    Puzzle puzzle = generate(level, seed, options);
    return puzzle;
}

constexpr std::uint32_t ink = 0xE7EEF7;
constexpr std::uint32_t accent = 0x77D8E5;
constexpr std::uint32_t dark_ink = 0x1E242C;

// A text mask from the shared GUI.Forms text service: 8-bit coverage, row-major.
struct TextMask {
    int w = 0;
    int h = 0;
    std::vector<std::uint8_t> a;
};
games::PortableMaskCache<TextMask> text_masks;

render::Color rgb(std::uint32_t color) {
    const render::Color out{static_cast<float>((color >> 16) & 255), static_cast<float>((color >> 8) & 255),
                            static_cast<float>(color & 255)};
    return out;
}

}  // namespace

CubeView::CubeView(gf::StableId id, Options options) : Control(std::move(id)), options_(options) {
    front_ = !options_.hosted;
    set_focusable(true);
    set_style(gf::ControlStyles::opaque, true);
    set_accessible_name("Nature Cube. Join each coloured pair with a line across three faces of a cube.");
    if (!restore()) {
        // A first game: deal it now if it is quick, otherwise in the background while the
        // lake shows; the cube arrives as soon as it is ready.
        session_.next_level = std::clamp(session_.next_level, 0, 2);
        if (session_.next_level < 2) {
            take_board(generate(session_.next_level, seed_from_clock(), GenerateOptions{}));
        } else {
            start_next();
            waiting_ = true;
        }
    } else {
        begin_arrival(motion_, session_.puzzle, false);
        note_lines(motion_, session_.puzzle, session_.play, true);
        // A board already won (an old save finished under the new rules) celebrates on show.
        finish_on_show_ = session_.play.won;
        start_next();
    }
}

CubeView::~CubeView() {
    abandon_next();
    for (Job& job : abandoned_) {
        if (job.board.valid()) {
            job.board.wait();
        }
    }
}

std::filesystem::path CubeView::save_path() const {
    const char* name = options_.dev ? "cube-dev-v3.txt" : "cube-v3.txt";
    const std::filesystem::path path = games::state_directory() / name;
    return path;
}

void CubeView::persist() {
    if (session_.puzzle.tiles.empty()) {
        return;
    }
    static_cast<void>(write_save(save_path(), encode_session(session_)));
}

bool CubeView::restore() {
    std::string body;
    if (read_save(save_path(), body) && decode_session(body, session_)) {
        return true;
    }
    if (options_.dev) {
        return false;
    }
    // Earlier versions: the game in progress from cube-v2.txt, or the names and scores
    // from cube-v1.txt. Both files are left as they are.
    std::string text;
    if (read_file(games::state_directory() / "cube-v2.txt", text) && import_legacy(text, true, session_)) {
        persist();
        return true;
    }
    if (read_file(games::state_directory() / "cube-v1.txt", text)) {
        static_cast<void>(import_legacy(text, false, session_));
    }
    return false;
}

// ---------------------------------------------------------------- boards

void CubeView::start_next() {
    const int level = std::clamp(session_.next_level, 0, 2);
    if (next_.board.valid() && next_.level == level) {
        return;
    }
    abandon_next();
    const bool gentle = !session_.portals_met && level >= 1;
    next_.cancel = std::make_shared<std::atomic<bool>>(false);
    next_.level = level;
    next_.board = std::async(std::launch::async, deal_board, level, seed_from_clock(), gentle, -1, next_.cancel);
}

void CubeView::abandon_next() {
    if (!next_.board.valid()) {
        return;
    }
    (*next_.cancel).store(true);
    abandoned_.push_back(std::move(next_));
    next_ = Job{};
    // Forget the ones that have finished.
    std::vector<Job> running;
    for (Job& job : abandoned_) {
        if (job.board.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            running.push_back(std::move(job));
        }
    }
    abandoned_ = std::move(running);
}

void CubeView::take_board(Puzzle puzzle) {
    if (!witness_valid(puzzle)) {
        // Never expected: the generator always returns a proven board. Deal Easy instead.
        puzzle = generate(0, seed_from_clock(), GenerateOptions{});
    }
    session_.portals_met = session_.portals_met || !puzzle.portals.empty();
    session_.puzzle = std::move(puzzle);
    session_.play = fresh_play(session_.puzzle);
    tracing_ = false;
    seeking_ = false;
    hover_ = -1;
    waiting_ = false;
    begin_arrival(motion_, session_.puzzle, reduced_);
    note_lines(motion_, session_.puzzle, session_.play, true);
    persist();
    games::sound_play("ui_new_game", sound_ && front_);
    start_next();
    request_frame();
}

// The next board is wanted now.
void CubeView::deal_next() {
    const int level = std::clamp(session_.next_level, 0, 2);
    if (next_.board.valid() && next_.level == level &&
        next_.board.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        Puzzle puzzle = next_.board.get();
        next_ = Job{};
        take_board(std::move(puzzle));
        return;
    }
    if (level < 2) {
        abandon_next();
        take_board(generate(level, seed_from_clock(), GenerateOptions{}));
        return;
    }
    start_next();
    waiting_ = true;
    request_frame();
}

void CubeView::new_board() {
    if (waiting_) {
        return;
    }
    tracing_ = false;
    set_pointer_capture(false);
    if (motion_.phase == Phase::departed || session_.puzzle.tiles.empty()) {
        deal_next();
        return;
    }
    begin_departure(motion_, reduced_);
    request_frame();
}

void CubeView::finished() {
    tracing_ = false;
    set_pointer_capture(false);
    const int strokes = session_.play.strokes;
    const bool best = !unranked_ && qualifies(session_.scores, strokes);
    session_.pending = best ? strokes : -1;
    persist();
    games::sound_play(std::string("stinger_") + (best ? "topscore_" : "win_") + "nature_cube", sound_ && front_);
    begin_completion(motion_, reduced_);
    request_frame();
}

void CubeView::after_step(std::size_t before, int pair) {
    note_lines(motion_, session_.puzzle, session_.play, reduced_);
    const std::vector<int>& path = session_.play.paths[static_cast<std::size_t>(pair)];
    // A hop: the line now continues from the far portal; wait for the pointer to get there.
    if (path.size() == before + 2) {
        seeking_ = true;
    }
    if (session_.play.won) {
        finished();
        return;
    }
    // Each change to a line has its own short sound (the live score answers these names).
    const char* effect = "nature_cube_trace";
    if (path.size() < before) {
        effect = "nature_cube_erase";
    } else if (path.size() == before + 2) {
        effect = "nature_cube_portal";
    }
    if (complete(session_.puzzle, path, pair)) {
        effect = "nature_cube_connect";
    }
    games::sound_play(effect, sound_ && front_);
    request_frame();
}

// ---------------------------------------------------------------- the shell

void CubeView::on_attached_to_window() {
    timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::milliseconds(16));
    interval_ = 16;
    subscriptions_.push_back(
        (*timer_).tick().subscribe(*this, gf::Delegate<>::bind<CubeView, &CubeView::tick>(*this)));
    if (!environment_loaded_) {
        // Prepared once in the window's byte order, darkened and lifted as the glass shows it.
        const std::vector<std::uint8_t> gpix = read_bytes(games::asset_directory() + "/nature-lake.gpix");
        const render::Order order = gf::native_live_surface_pixel_format() == gf::LiveSurfacePixelFormat::rgba32_premultiplied_srgb
                                        ? render::Order::rgba
                                        : render::Order::bgra;
        environment_loaded_ = panorama_.load_gpix(gpix, order, .82F, 35);
    }
    if (lake_.width == 0) {
        const std::vector<std::uint8_t> png = read_bytes(games::asset_directory() + "/nature-lake.png");
        static_cast<void>(decode_png(png, lake_));
    }
    backdrop_dirty_ = true;
    request_frame();
}

void CubeView::on_detaching_from_window(gf::Window&) noexcept {
    try {
        persist();
    } catch (...) {
    }
    if (timer_) {
        (*timer_).stop();
    }
    timer_.reset();
    surface_ = render::Surface{};
    surface_attached_ = false;
    shown_ = Shown{};
    backdrop_.clear();
    fade_.clear();
    backdrop_dirty_ = true;
}

void CubeView::activate() {
    gf::Window* window = attached_window();
    if (window != nullptr) {
        static_cast<void>((*window).request_focus(shared_from_this()));
    }
    games::music_play("nature_cube", music_);
    if (finish_on_show_) {
        finish_on_show_ = false;
        finished();
    }
    request_frame();
}

void CubeView::set_cabinet(bool foreground, bool music, bool sound, bool reduced) {
    const bool music_changed = music != music_;
    front_ = foreground;
    music_ = music;
    sound_ = sound;
    if (reduced && !reduced_ && motion_.phase == Phase::arriving) {
        begin_arrival(motion_, session_.puzzle, true);
    }
    reduced_ = reduced;
    if (!foreground) {
        hover_ = -1;
        if (tracing_) {
            tracing_ = false;
            release(session_.play);
            persist();
        }
        if (timer_) {
            (*timer_).stop();
        }
        return;
    }
    if (music_changed) {
        games::music_play("nature_cube", music_);
    }
    request_frame();
}

std::vector<games::GameCommand> CubeView::commands() const {
    std::vector<games::GameCommand> list;
    list.push_back({"new", "New game", !waiting_, false, true});
    list.push_back({"scores", "Top scores", true, panel_ == Panel::scores, false});
    list.push_back({"help", "Help", true, panel_ == Panel::help, false});
    return list;
}

void CubeView::run_command(std::string_view id) {
    if (id == "new") {
        close_panel();
        new_board();
    } else if (id == "scores") {
        if (panel_ == Panel::scores) {
            close_panel();
        } else if (panel_ == Panel::none) {
            panel_ = Panel::scores;
            request_frame();
        }
    } else if (id == "help") {
        set_help(panel_ != Panel::help);
    }
}

std::vector<games::GameSetting> CubeView::settings() const {
    games::GameSetting level;
    level.id = "level";
    level.label = "Level";
    level.kind = games::GameSetting::Kind::choice;
    level.value = static_cast<double>(std::clamp(session_.next_level, 0, 2));
    level.choices = {"Easy", "Medium", "Hard"};
    level.note = "An untouched board is dealt again; otherwise the next game uses it.";
    return {level};
}

void CubeView::change_setting(std::string_view id, double value) {
    const int level = static_cast<int>(std::lround(value));
    if (id != "level" || level < 0 || level > 2 || level == session_.next_level) {
        return;
    }
    session_.next_level = level;
    start_next();
    persist();
    // A board nobody has touched is simply dealt again at the new level.
    if (session_.play.strokes == 0 && !session_.play.won && !waiting_) {
        new_board();
    }
}

bool CubeView::scripted_action(std::string_view action) {
    if (!options_.dev || action.size() > 64) {
        return false;
    }
    std::istringstream in{std::string(action)};
    std::string verb;
    in >> verb;
    if (verb == "deal" || verb == "portal") {
        int level = 1;
        std::uint64_t seed = 1;
        in >> level >> seed;
        if (!in || level < 0 || level > 2) {
            return false;
        }
        // Dealt in the background, as in play; the cube arrives when the board is ready.
        close_panel();
        session_.pending = -1;
        abandon_next();
        next_.cancel = std::make_shared<std::atomic<bool>>(false);
        next_.level = level;
        next_.board = std::async(std::launch::async, deal_board, level, seed, false, verb == "portal" ? 1 : -1,
                                 next_.cancel);
        waiting_ = true;
        begin_departure(motion_, true);
        request_frame();
        return true;
    }
    if (verb == "unranked") {
        // Finished boards offer no name entry: for measuring the animation alone.
        unranked_ = true;
        return true;
    }
    if (verb == "lines" || verb == "finish" || verb == "through") {
        int count = static_cast<int>(session_.puzzle.witness.size());
        if (verb == "lines") {
            in >> count;
        }
        if (verb == "through") {
            count = 0;
        }
        if (!in && verb == "lines") {
            return false;
        }
        std::vector<std::vector<int>> lines(session_.puzzle.ends.size());
        for (int pair = 0; pair < count && pair < static_cast<int>(lines.size()); ++pair) {
            lines[static_cast<std::size_t>(pair)] = session_.puzzle.witness[static_cast<std::size_t>(pair)];
        }
        if (verb == "through") {
            // Only the lines that use a portal.
            for (std::size_t pair = 0; pair < lines.size(); ++pair) {
                const std::vector<int>& line = session_.puzzle.witness[pair];
                bool hops = false;
                for (std::size_t index = 1; index < line.size(); ++index) {
                    hops = hops || !adjacent(session_.puzzle.geometry, line[index - 1], line[index]);
                }
                lines[pair] = hops ? line : std::vector<int>{};
            }
        }
        Play play;
        if (!replay(session_.puzzle, lines, std::max(1, count), play)) {
            return false;
        }
        session_.play = play;
        note_lines(motion_, session_.puzzle, session_.play, reduced_);
        if (session_.play.won) {
            finished();
        } else {
            persist();
            request_frame();
        }
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- frames

void CubeView::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    gf::Window* window = attached_window();
    scale_ = window != nullptr ? (*window).scale() : 1.0;
    layout_ = compute_layout(bounds.width, bounds.height);
    device_width_ = std::max(1, static_cast<int>(std::lround(bounds.width * scale_)));
    device_height_ = std::max(1, static_cast<int>(std::lround(bounds.height * scale_)));
    // The surface takes its new size with the layout, before the next frame is drawn.
    if (surface_.live()) {
        static_cast<void>(surface_.configure(device_width_, device_height_));
    }
    backdrop_dirty_ = true;
    request_frame();
}

void CubeView::on_paint(gf::Painter& painter, gf::Rect) {
    const gf::Rect bounds = client_rectangle();
    if (surface_.live()) {
        painter.draw_live_surface(surface_.live(), bounds);
    } else {
        painter.fill_rect(bounds, gf::Color::rgba(40, 62, 60));
    }
}

void CubeView::request_frame() {
    render_dirty_ = true;
    if (!timer_ || !visible() || !front_) {
        return;
    }
    if (!(*timer_).enabled()) {
        last_tick_ = std::chrono::steady_clock::now();
    }
    pace(16);
    (*timer_).start();
}

// Changes the timer's interval only when it differs: setting it re-arms the timer.
void CubeView::pace(long long milliseconds) {
    if (!timer_ || interval_ == milliseconds) {
        return;
    }
    interval_ = milliseconds;
    (*timer_).set_interval(std::chrono::milliseconds(milliseconds));
}

bool CubeView::settled() const {
    const bool still = !render_dirty_ && !waiting_ && (motion_.phase == Phase::resting) &&
                       motion_.yaw == motion_.target_yaw && motion_.pitch == motion_.target_pitch;
    return still;
}

void CubeView::tick() {
    if (!visible() || !front_) {
        if (timer_) {
            (*timer_).stop();
        }
        return;
    }
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const double seconds = std::clamp(std::chrono::duration<double>(now - last_tick_).count(), 0.0, .1);
    last_tick_ = now;
    if (waiting_ && next_.board.valid() &&
        next_.board.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        Puzzle puzzle = next_.board.get();
        next_ = Job{};
        take_board(std::move(puzzle));
    }
    bool moving = advance(motion_, session_.puzzle, session_.play, seconds, reduced_);
    // A result worth keeping asks for a name once the next cube has arrived.
    if (panel_ == Panel::none && session_.pending >= 0 && motion_.phase == Phase::resting && !waiting_ &&
        !tracing_) {
        if (qualifies(session_.scores, session_.pending)) {
            panel_ = Panel::name;
            name_entry_ = session_.player;
        } else {
            session_.pending = -1;
        }
        render_dirty_ = true;
    }
    if (motion_.departed && !waiting_) {
        deal_next();
        moving = true;
    }
    // Under a panel the cube carries on but is not redrawn until the panel changes.
    if (render_dirty_ || (moving && panel_ == Panel::none)) {
        render_dirty_ = false;
        render_frame();
    }
    if (!timer_) {
        return;
    }
    if (moving || render_dirty_) {
        // Twenty-five frames a second for the cube's own motion; a step being traced asks
        // for its frame at once through request_frame.
        // The finale's light changes slowly; twenty frames a second carry it.
        const long long interval = render_dirty_ || tracing_ ? 16 : motion_.phase == Phase::lighting ? 50 : 40;
        pace(interval);
        return;
    }
    // Settled. While a board is being dealt, look in now and then; a finished board at
    // rest (reduced motion) wakes once when it is time to leave; otherwise stop.
    const double wait = seconds_until_event(motion_);
    if (waiting_) {
        pace(60);
    } else if (wait >= 0) {
        pace(static_cast<long long>(std::ceil(wait * 1000)) + 10);
    } else {
        (*timer_).stop();
        interval_ = 0;
    }
}

double CubeView::draw_text(const render::Target& target, const std::string& words, double x, double y, double size,
                           bool bold, std::uint32_t color, double wrap) {
    if (words.empty()) {
        return 0;
    }
    const TextMask& mask = text_masks.get(words, bold ? 1 : 0, size * scale_, wrap * scale_);
    render::r2d::mask(target, target.bounds(), static_cast<int>(std::lround(x * scale_)),
                      static_cast<int>(std::lround(y * scale_)), mask.w, mask.h, mask.a, rgb(color));
    return mask.w / scale_;
}

Box CubeView::device_board() const {
    const Box box{layout_.board.x * scale_, layout_.board.y * scale_, layout_.board.w * scale_, layout_.board.h * scale_};
    return box;
}

render::Rect CubeView::changes(const Box& board) {
    const Pose look = pose(motion_);
    const int hover = tracing_ ? -1 : hover_;
    const bool panel = panel_ != Panel::none;
    const std::size_t pairs = session_.play.paths.size();
    std::vector<std::uint8_t> live(pairs, 0);
    for (std::size_t pair = 0; pair < pairs; ++pair) {
        live[pair] = (pair < motion_.grow.size() && motion_.grow[pair] < 1) ||
                     (pair < motion_.ripple.size() && motion_.ripple[pair] >= 0);
    }
    const render::Rect cube = session_.puzzle.tiles.empty() ? render::Rect{} : cube_bounds(motion_, board);
    const bool same_pose = look.yaw == shown_.pose.yaw && look.pitch == shown_.pose.pitch &&
                           look.scale == shown_.pose.scale && look.lift == shown_.pose.lift &&
                           look.drift == shown_.pose.drift && look.opacity == shown_.pose.opacity;
    const bool same_board = board.x == shown_.board.x && board.y == shown_.board.y && board.w == shown_.board.w &&
                            board.h == shown_.board.h;
    render::Rect damage;
    if (!shown_.valid || panel || shown_.panel || !same_board) {
        // A panel covers everything; a new size or a first frame changes it all.
        damage = render::Rect{0, 0, device_width_, device_height_};
    } else if (!same_pose || motion_.phase != Phase::resting || shown_.paths.size() != pairs) {
        // The cube moved: where it is now and where it was.
        damage = cube.united(shown_.cube);
    } else {
        // At rest only the hover and the lines being drawn change.
        std::vector<int> cells;
        if (hover != shown_.hover) {
            cells.push_back(hover);
            cells.push_back(shown_.hover);
        }
        for (std::size_t pair = 0; pair < pairs; ++pair) {
            const std::vector<int>& now = session_.play.paths[pair];
            const std::vector<int>& before = shown_.paths[pair];
            if (live[pair] || shown_.live[pair] || now != before) {
                cells.insert(cells.end(), now.begin(), now.end());
                cells.insert(cells.end(), before.begin(), before.end());
            }
        }
        damage = cells.empty() ? render::Rect{} : cell_bounds(session_.puzzle, motion_, board, cells);
    }
    shown_.valid = true;
    shown_.panel = panel;
    shown_.pose = look;
    shown_.board = board;
    shown_.hover = hover;
    shown_.paths = session_.play.paths;
    shown_.live = std::move(live);
    shown_.cube = cube;
    return damage;
}

void CubeView::render_frame() {
    if (device_width_ <= 0 || device_height_ <= 0 || !surface_.configure(device_width_, device_height_)) {
        return;
    }
    gf::Window* window = attached_window();
    if (!surface_attached_ && window != nullptr) {
        static_cast<void>(surface_.attach(*window, shared_from_this()));
        surface_attached_ = true;
    }
    const render::Order order = surface_.order();
    const std::size_t count = static_cast<std::size_t>(device_width_) * static_cast<std::size_t>(device_height_);
    const auto layer = [&](std::vector<std::uint32_t>& pixels) {
        const render::Target target{pixels.data(), device_width_, device_width_, device_height_, order};
        return target;
    };
    if (backdrop_dirty_ || backdrop_.size() != count) {
        // The lake behind the cube, drawn once per size.
        backdrop_.assign(count, 0);
        cover(lake_, layer(backdrop_));
        backdrop_dirty_ = false;
        shown_.valid = false;
    }
    if (buffers_.width() != device_width_ || buffers_.height() != device_height_) {
        buffers_.resize(device_width_, device_height_, true);
        shown_.valid = false;
    }
    const Box board = device_board();
    const render::Rect damage = changes(board);
    if (damage.empty()) {
        return;
    }
    std::optional<render::Surface::Frame> frame = surface_.begin(damage);
    if (!frame) {
        // Every buffer is still being shown; the damage waits for the next tick.
        render_dirty_ = true;
        return;
    }
    const render::Target& target = (*frame).target;
    const render::Rect repair = (*frame).repair;
    // Only what this buffer lacks is put back from the lake and drawn again.
    render::r2d::restore(target, layer(backdrop_), repair);
    buffers_.clear(repair);
    if (!session_.puzzle.tiles.empty()) {
        // Moving frames are drawn exactly like still ones, so the cube looks the same in
        // motion. Only a computer too slow for that (a full frame over 28 ms) gets the light
        // draft while the cube moves.
        const bool moving = motion_.phase != Phase::resting || motion_.yaw != motion_.target_yaw ||
                            motion_.pitch != motion_.target_pitch;
        const bool light = moving && slow_;
        Drawing drawing{render::r3d::Pass{target, &buffers_, repair}, environment_loaded_ ? &panorama_ : nullptr,
                        board, light};
        const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        show_cube(drawing, fade_, session_.puzzle, session_.play, motion_, tracing_ ? -1 : hover_);
        if (moving && !light) {
            slow_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() > 28;
        }
    }
    if (panel_ != Panel::none) {
        paint_panel(target);
    }
    text_masks.trim();
    surface_.publish();
    if (!surface_.direct()) {
        invalidate(gf::Dirty::paint);
    }
}

void CubeView::paint_panel(const render::Target& target) {
    // Dim the scene, then a card of dark green glass, like the cube's body, with a pale rim.
    render::r2d::tint(target, target.bounds(), rgb(0x040C1A), .55F);
    const Box& box = layout_.panel;
    render::r2d::rounded(target, target.bounds(), box.x * scale_, box.y * scale_, box.w * scale_, box.h * scale_,
                         14 * scale_, rgb(0x0E282C), .95F, rgb(0xD6F0EC), 1.5 * scale_);
    const double left = box.x + 26;
    double y = box.y + 22;
    if (panel_ == Panel::help) {
        static_cast<void>(draw_text(target, "How to play", left, y, 22, true, ink));
        const std::string help =
            "Join each coloured pair of squares with a line across the cube's three faces. Lines may not "
            "cross, share a square or pass a stone. Press a coloured square and trace; trace back to "
            "shorten a line. A portal sends a line out of its partner. Every pair joined solves the board.";
        static_cast<void>(draw_text(target, help, left, y + 40, 14, false, ink, box.w - 52));
        return;
    }
    static_cast<void>(draw_text(target, "Top scores", left, y, 22, true, ink));
    y += 42;
    if (panel_ == Panel::name) {
        static_cast<void>(draw_text(target, "Solved in " + std::to_string(session_.pending) + " strokes. Your name:",
                                    left, y, 15, false, accent));
        y += 28;
        const double field_w = std::min(260.0, box.w - 52);
        render::r2d::rounded(target, target.bounds(), left * scale_, y * scale_, field_w * scale_, 32 * scale_,
                             6 * scale_, rgb(0xFAF8EC), 1, rgb(0x77D8E5), 2 * scale_);
        static_cast<void>(draw_text(target, name_entry_ + "|", left + 10, y + 6, 15, false, dark_ink));
        y += 40;
        static_cast<void>(draw_text(target, "Enter keeps it; Escape skips.", left, y, 13, false, ink));
        y += 30;
    } else {
        static_cast<void>(draw_text(target, "Fewest strokes", left, y, 15, false, accent));
        y += 30;
    }
    const double row = box.h < 280 ? 20 : 25;
    for (std::size_t index = 0; index < session_.scores.size(); ++index) {
        if (y + row > box.y + box.h - 8) {
            break;
        }
        const TopScore& score = session_.scores[index];
        static_cast<void>(draw_text(target, std::to_string(index + 1), left, y, 15, false, accent));
        static_cast<void>(draw_text(target, score.name, left + 36, y, 15, false, ink));
        static_cast<void>(draw_text(target, std::to_string(score.strokes), box.x + box.w - 70, y, 15, false, accent));
        y += row;
    }
    if (session_.scores.empty() && panel_ == Panel::scores) {
        static_cast<void>(draw_text(target, "Solve a board to enter the table.", left, y, 14, false, ink));
    }
}

void CubeView::set_help(bool open) {
    if (open && games::route_help(*this)) {
        return;
    }
    if (open && panel_ == Panel::none) {
        panel_ = Panel::help;
    } else if (!open && panel_ == Panel::help) {
        panel_ = Panel::none;
    }
    request_frame();
}

// ---------------------------------------------------------------- input

int CubeView::cell_under(gf::Point local) const {
    if (buffers_.width() <= 1 || layout_.board.w <= 0 || session_.puzzle.tiles.empty() ||
        motion_.phase != Phase::resting) {
        return -1;
    }
    const int cell = pick(buffers_, device_board(), local.x * scale_, local.y * scale_);
    return cell;
}

std::optional<gf::Point> CubeView::cell_point(int cell) const {
    double x = 0;
    double y = 0;
    if (scale_ <= 0 || !ps_cube::cell_point(session_.puzzle, motion_, device_board(), cell, x, y)) {
        return std::nullopt;
    }
    const gf::Point point{x / scale_, y / scale_};
    return point;
}

void CubeView::lean_toward(gf::Point local) {
    const gf::Rect bounds = client_rectangle();
    if (bounds.width <= 0 || bounds.height <= 0) {
        return;
    }
    const double yaw = std::clamp(rest_yaw + (local.x / bounds.width - .5) * .8, .40, 1.10);
    const double pitch = std::clamp(rest_pitch + (local.y / bounds.height - .5) * .55, -.78, -.34);
    // A sweep of the cube moves air across the glass.
    if (std::fabs(yaw - motion_.yaw) + std::fabs(pitch - motion_.pitch) > .06 && motion_.phase == Phase::resting) {
        games::sound_play("nature_cube_turn", sound_ && front_);
    }
    lean(motion_, yaw, pitch);
    request_frame();
}

void CubeView::on_pointer(gf::PointerEvent& event) {
    const gf::Point local = point_from_window(event.position);
    if (panel_ != Panel::none) {
        if (event.action == gf::PointerAction::down) {
            const Box& box = layout_.panel;
            if (!inside(box, local.x, local.y) && (panel_ == Panel::scores || panel_ == Panel::help)) {
                close_panel();
            }
            event.handled = true;
        }
        return;
    }
    const int cell = cell_under(local);
    if (event.action == gf::PointerAction::move) {
        if (!tracing_) {
            lean_toward(local);
        }
        if (tracing_ && cell >= 0) {
            const int pair = session_.play.active;
            const bool held = pair >= 0 && pair < static_cast<int>(session_.play.paths.size());
            const std::size_t before = held ? session_.play.paths[static_cast<std::size_t>(pair)].size() : 0;
            const int head = held && before > 0 ? session_.play.paths[static_cast<std::size_t>(pair)].back() : -1;
            // After a hop, steps count again once the pointer reaches the far side.
            if (seeking_ && (cell == head || adjacent(session_.puzzle.geometry, head, cell))) {
                seeking_ = false;
            }
            bool stepped = false;
            if (!seeking_ && held) {
                stepped = extend(session_.puzzle, session_.play, cell);
                // A quick diagonal flick skips a cell: route through the free corner between.
                if (!stepped && head >= 0) {
                    const std::array<int, 4>& around = session_.puzzle.geometry.neighbours[static_cast<std::size_t>(head)];
                    for (int middle : around) {
                        if (stepped || middle < 0 || !adjacent(session_.puzzle.geometry, middle, cell)) {
                            continue;
                        }
                        Play trial = session_.play;
                        if (extend(session_.puzzle, trial, middle) && trial.paths[static_cast<std::size_t>(pair)].size() == before + 1 &&
                            extend(session_.puzzle, trial, cell)) {
                            session_.play = trial;
                            stepped = true;
                        }
                    }
                }
            }
            if (stepped) {
                after_step(before, pair);
            } else if (!seeking_ && held && cell != hover_ && head >= 0 &&
                       adjacent(session_.puzzle.geometry, head, cell) &&
                       owner(session_.play, cell) != pair) {
                // The pointer reached a neighbour the line may not enter.
                games::sound_play("nature_cube_blocked", sound_ && front_);
            }
        }
        if (cell != hover_) {
            hover_ = cell;
            const bool live = cell >= 0 && (session_.puzzle.pair_of[static_cast<std::size_t>(cell)] >= 0 ||
                                            owner(session_.play, cell) >= 0);
            set_cursor(live || tracing_ ? gf::CursorKind::hand : gf::CursorKind::arrow);
            request_frame();
        }
        return;
    }
    if (event.action == gf::PointerAction::leave) {
        if (hover_ >= 0) {
            hover_ = -1;
            request_frame();
        }
        return;
    }
    if (event.action == gf::PointerAction::down) {
        activate();
        if (event.button != gf::PointerButton::primary || session_.play.won || waiting_ || cell < 0) {
            return;
        }
        if (press(session_.puzzle, session_.play, cell)) {
            tracing_ = true;
            seeking_ = false;
            set_pointer_capture(true);
            hold_tilt(motion_);
            note_lines(motion_, session_.puzzle, session_.play, reduced_);
            persist();
            games::sound_play("nature_cube_pick", sound_ && front_);
            request_frame();
        }
        event.handled = true;
        return;
    }
    if (event.action == gf::PointerAction::up) {
        if (tracing_) {
            tracing_ = false;
            seeking_ = false;
            release(session_.play);
            set_pointer_capture(false);
            persist();
            request_frame();
        }
        event.handled = true;
    }
}

void CubeView::close_panel() {
    if (panel_ == Panel::name) {
        session_.pending = -1;
        persist();
    }
    panel_ = Panel::none;
    request_frame();
}

void CubeView::confirm_name() {
    std::string name = name_entry_;
    while (!name.empty() && name.back() == ' ') {
        name.pop_back();
    }
    if (!valid_name(name) || !record(session_.scores, name, session_.pending)) {
        return;
    }
    session_.player = name;
    session_.pending = -1;
    persist();
    games::sound_play("ui_name_confirm", sound_ && front_);
    panel_ = Panel::scores;
    request_frame();
}

void CubeView::on_key(gf::KeyEvent& event) {
    if (event.handled || event.action != gf::KeyAction::down) {
        return;
    }
    const std::uint32_t key = event.physical_key;
    if (panel_ == Panel::name) {
        if (key == gf::PhysicalKey::enter) {
            confirm_name();
        } else if (key == gf::PhysicalKey::escape) {
            close_panel();
        } else if (key == gf::PhysicalKey::backspace && !name_entry_.empty()) {
            // Remove one UTF-8 character.
            std::size_t cut = name_entry_.size() - 1;
            while (cut > 0 && (static_cast<unsigned char>(name_entry_[cut]) & 0xC0) == 0x80) {
                --cut;
            }
            name_entry_.resize(cut);
            request_frame();
        } else {
            return;
        }
        event.handled = true;
        return;
    }
    if ((key == gf::PhysicalKey::f1 || key == gf::PhysicalKey::h) && !event.repeat) {
        set_help(panel_ != Panel::help);
    } else if (key == gf::PhysicalKey::escape && panel_ != Panel::none) {
        close_panel();
    } else if ((key == gf::PhysicalKey::enter || key == gf::PhysicalKey::space) && panel_ == Panel::scores) {
        close_panel();
    } else if (key == gf::PhysicalKey::n && !event.repeat && panel_ == Panel::none) {
        new_board();
    } else {
        return;
    }
    event.handled = true;
}

void CubeView::on_text_input(gf::TextInputEvent& event) {
    if (panel_ != Panel::name || event.composing || event.text_utf8.empty()) {
        return;
    }
    for (char c : event.text_utf8) {
        if (static_cast<unsigned char>(c) < 32 || c == 127) {
            return;
        }
    }
    if (name_entry_.size() + event.text_utf8.size() <= 24) {
        name_entry_ += event.text_utf8;
        request_frame();
    }
    event.handled = true;
}

}  // namespace ps_cube
