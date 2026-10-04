#include "thieves_view.hpp"

#include "gen.hpp"
#include "platform/audio.hpp"
#include "platform/text.hpp"
#include "help_route.hpp"

#include "gui_forms/surface_material.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ct {

namespace {
const Col kInk = hex(0x3A2A1A), kPaper = hex(0xF6EED8), kPaperDark = hex(0xE6D8B4), kLeafGreen = hex(0x5A8A34), kLeafDark = hex(0x2E5A1E);
const Col kOrange = hex(0xF08A24), kGold = hex(0xF4C430), kSilver = hex(0xC8D0D8), kBronze = hex(0xC8804A);
const char* kSeasonName[] = {"spring", "summer", "autumn", "winter", "night"};
struct Tier { const char* name; int w, h, boxes, min_pushes; long long budget; Season season; };
const Tier kTiers[] = {
    {"Spring", 6, 6, 2, 12, 200000, Season::spring},
    {"Summer", 7, 7, 3, 20, 300000, Season::summer},
    {"Autumn", 8, 8, 4, 26, 500000, Season::autumn},
    {"Winter", 9, 8, 4, 32, 900000, Season::winter},
    {"Night", 9, 9, 5, 34, 1200000, Season::night},
};
constexpr int kTiersN = 5;

bool script_index(std::string_view value, int limit, int& result) {
    if (value.empty() || value.size() > 7) return false;
    int number = 0;
    for (char digit : value) {
        if (digit < '0' || digit > '9') return false;
        number = number * 10 + digit - '0';
        if (number >= limit) return false;
    }
    result = number;
    return true;
}
std::uint64_t seed_now() { return static_cast<std::uint64_t>(wall_clock() * 1000) ^ 0x7417EF5ULL; }
std::string joined(const std::string& xsb) { std::string s = xsb; std::replace(s.begin(), s.end(), '\n', '|'); return s; }
std::string split(const std::string& s) { std::string x = s; std::replace(x.begin(), x.end(), '|', '\n'); return x; }

// a little pumpkin medal for the HUD and the map, in game pixels
void medal_icon(Canvas& c, double x, double y, double r, int medal) {
    const Col ring = medal == 3 ? kGold : medal == 2 ? kSilver : medal == 1 ? kBronze : hex(0xB8AC90);
    c.fill_circle(x, y, r + 1.2, hex(0x3A2A1A, .8f));
    c.fill_circle(x, y, r, medal ? kOrange : hex(0xD8CCB0));
    if (medal) {
        c.stroke_line(x, y - r + 1, x, y + r - 1, hex(0xC86010, .7f), .8);
        c.begin(); c.ellipse(x, y, r * .5, r - .8); c.stroke(hex(0xC86010, .6f), .8);
    }
    c.fill_rect(x - .6, y - r - 2, 1.4, 2.4, hex(0x5A7A22));
    c.begin(); c.circle(x, y, r + .4); c.stroke(ring, 1.4);
}
}  // namespace

ThievesView::ThievesView(gf::StableId id, Options opt) : Control(std::move(id)), opt_(opt), show_(seed_now()) {
    cab_front_ = !opt_.hosted;
    set_focusable(true);
    set_style(gf::ControlStyles::opaque, true);
    {
        gf::SurfaceMaterial none;
        none.fills = {gf::MaterialFillLayer::solid(gf::Color::rgba(0, 0, 0))};
        set_authored_surface_material(none);
    }
    set_accessible_name("Catching Thieves. Raccoon bandits are raiding the bear's garden from their burrows. Push a pumpkin onto every burrow to trap them. "
                        "Arrow keys or WASD walk; walking into a pumpkin pushes it. Click a square to walk there. Z undo, R restart, L gardens, H or F1 help; Hint is in the command capsule.");
    load_campaign();
    const bool have = load_save(save_path(opt_.dev), save_);
    if (have && save_.level == -1 && !save_.endless_xsb.empty()) {
        LevelEntry e;
        if (Level::parse(split(save_.endless_xsb), e.level)) {
            e.level.solution = save_.endless_solution;
            e.section = std::string("Endless ") + kTiers[std::clamp(save_.endless_tier, 0, kTiersN - 1)].name;
            enter_endless(e, save_.history);
        } else enter(0);
    } else {
        enter(std::clamp(save_.level, 0, std::max(0, static_cast<int>(levels_.size()) - 1)), save_.history);
    }
    if (const char* sc = std::getenv("CT_SCRIPT"); sc && opt_.dev) {
        std::string all = sc;
        for (size_t pos = 0; pos < all.size();) {
            size_t end = all.find(',', pos);
            if (end == std::string::npos) end = all.size();
            const std::string item = all.substr(pos, end - pos);
            const size_t colon = item.find(':');
            if (colon != std::string::npos) script_.push_back({std::atof(item.c_str()), item.substr(colon + 1)});
            pos = end + 1;
        }
    }
}

ThievesView::~ThievesView() {
    audio_stop();
    join(hint_);
    join(gen_);
}

void ThievesView::join(std::unique_ptr<Worker>& w) {
    if (w) { (*w).cancellation.request_stop(); if ((*w).th.joinable()) (*w).th.join(); }
    w.reset();
}

void ThievesView::on_attached_to_window() {
    audio_start(asset_dir());
    timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::milliseconds(33));
    subs_.push_back((*timer_).tick().subscribe(*this, gf::Delegate<>::bind<ThievesView, &ThievesView::tick>(*this)));
    last_ = std::chrono::steady_clock::now();
    if (cab_front_ && visible()) (*timer_).start();
}

void ThievesView::on_detaching_from_window(gf::Window&) noexcept {
    try { persist(); } catch (...) {}
    if (timer_) (*timer_).stop();
    timer_.reset();
    direct_ = false;
    surface_.reset();
    if (hint_) (*hint_).cancellation.request_stop();
    if (gen_) (*gen_).cancellation.request_stop();
    audio_stop();
}

void ThievesView::activate() {
    if (attached_window()) static_cast<void>((*attached_window()).request_focus(shared_from_this()));
}

void ThievesView::request_frame() {
    render_dirty_ = true;
    if (timer_ && cab_front_ && visible()) {
        if (!(*timer_).enabled()) last_ = std::chrono::steady_clock::now();
        (*timer_).start();
    }
}

void ThievesView::set_cabinet(bool foreground, bool music, bool sound, bool reduced) {
    const bool returning = foreground && !cab_front_;
    cab_front_ = foreground; cab_music_ = music; cab_sound_ = sound; cab_reduced_ = reduced;
    audio_cabinet(foreground, music, sound);
    if (!foreground) {
        if (timer_) (*timer_).stop();
        queue_.clear(); pressed_.clear(); hover_.clear(); mouse_in_ = false; hover_cell_ = -1;
        show_.state().hover_cell = -1; show_.state().path.clear();
        set_cursor(gf::CursorKind::arrow);
        if (hint_) (*hint_).cancellation.request_stop();
        if (gen_) (*gen_).cancellation.request_stop();
        persist();
        return;
    }
    if (returning) {
        if (hint_ && (*hint_).cancellation.stop_requested()) { join(hint_); show_.thinking(false); }
        if (gen_ && (*gen_).cancellation.stop_requested()) join(gen_);
        if (save_.level < 0 && !gen_) start_endless_gen(save_.endless_tier);
    }
    if (reduced) { show_.settle(board_); if (won_) won_t_ = 9; }
    audio_music(std::string("ct_music_") + kSeasonName[static_cast<int>(show_.state().season)],
                music && (opt_.hosted || save_.settings.music));
    request_frame();
}

std::vector<games::GameCommand> ThievesView::commands() const {
    return {{"new", "New garden", true, false, true},
            {"restart", "Start over", panel_ == Panel::none},
            {"undo", "Undo", board_.moves() > 0 && !won_ && panel_ == Panel::none},
            {"hint", "Hint", !won_ && panel_ == Panel::none},
            {"map", "Gardens", true, panel_ == Panel::map},
            {"help", "Help", true, panel_ == Panel::help}};
}

void ThievesView::run_command(std::string_view id) {
    if (!cab_front_) return;
    for (const games::GameCommand& command : commands()) {
        if (command.id != id || !command.enabled) continue;
        if (id == "new") { open(Panel::none); next(); }
        else if (id == "map") open(panel_ == Panel::map ? Panel::none : Panel::map);
        else if (id == "help") open(panel_ == Panel::help ? Panel::none : Panel::help);
        else action(std::string(id));
        request_frame();
        return;
    }
}

std::string ThievesView::hit_button() const {
    for (const Button& button : buttons_)
        if (button.enabled && mouse_x_ >= button.x && mouse_x_ < button.x + button.w &&
            mouse_y_ >= button.y && mouse_y_ < button.y + button.h) return button.id;
    return {};
}

bool ThievesView::controls_fit() const {
    for (const Button& button : buttons_)
        if (button.x < 0 || button.y < 0 || button.w < 1 || button.h < 1 ||
            button.x + button.w > pw_ || button.y + button.h > ph_) return false;
    return true;
}

void ThievesView::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    hud_w_ = bounds.width < 850 ? 88 : 128;
    pw_ = std::max(64, static_cast<int>(std::ceil(bounds.width / pixel_)));
    ph_ = std::max(64, static_cast<int>(std::ceil(bounds.height / pixel_)));
    frame_.resize(pw_, ph_);
    garden_.resize(pw_, ph_, hud_w_);
    bs_ = attached_window() ? (*attached_window()).scale() : 1.0;
    phys_w_ = std::max(1, static_cast<int>(std::lround(bounds.width * bs_)));
    phys_h_ = std::max(1, static_cast<int>(std::lround(bounds.height * bs_)));
    xmap_.clear();
    if (surface_) {
        gf::LiveSurfaceDescription d;
        d.width = static_cast<std::uint32_t>(phys_w_);
        d.height = static_cast<std::uint32_t>(phys_h_);
        static_cast<void>((*surface_).reconfigure(d));
    }
    layout_buttons();
    request_frame();
}

void ThievesView::on_paint(gf::Painter& p, gf::Rect) {
    const gf::Rect b = client_rectangle();
    if (surface_) p.draw_live_surface(surface_, b);
    else p.fill_rect(b, gf::Color::rgba(120, 180, 80));
}

// ------------------------------------------------------------------ the campaign
void ThievesView::load_campaign() {
    std::filesystem::path campaign = std::filesystem::path(asset_dir()) / "catchingthieves/levels/campaign.txt";
    if (!std::filesystem::is_regular_file(campaign)) campaign = std::filesystem::path(asset_dir()) / "levels/campaign.txt";
    std::ifstream f(campaign);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string err;
    if (!load_levels(ss.str(), levels_, &err) || levels_.empty()) {
        throw std::runtime_error("Catching Thieves campaign missing or invalid: " + campaign.string() + " " + err);
    }
}

int ThievesView::section_solved(const std::string& section) const {
    int n = 0;
    for (size_t i = 0; i < levels_.size(); ++i)
        if (levels_[i].section == section && save_.records.count(static_cast<int>(i))) ++n;
    return n;
}

// The first two sections are open from the start; each later one opens once half of the one before is cleared.
bool ThievesView::section_open(const std::string& section) const {
    std::vector<std::string> order;
    for (const LevelEntry& e : levels_)
        if (order.empty() || order.back() != e.section) order.push_back(e.section);
    const std::vector<std::string>::const_iterator it = std::find(order.begin(), order.end(), section);
    if (it == order.end()) return true;
    const size_t k = static_cast<size_t>(it - order.begin());
    if (k < 2) return true;
    int total = 0;
    for (const LevelEntry& e : levels_) total += e.section == order[k - 1];
    return section_solved(order[k - 1]) * 2 >= total;
}

int ThievesView::medal(int index) const {
    const std::map<int, LevelRecord>::const_iterator it = save_.records.find(index);
    if (it == save_.records.end()) return 0;
    const int par = levels_[static_cast<size_t>(index)].par;
    if (par <= 0) return 1;
    if ((*it).second.best_pushes <= par) return 3;
    if ((*it).second.best_pushes <= par + std::max(2, par / 5)) return 2;
    return 1;
}

void ThievesView::begin_level() {
    garden_.set_level(board_.level());
    show_.set_level(board_, garden_, season_for(current_.section));
    show_.state().fade = cab_reduced_ ? 0 : 1;
    queue_.clear();
    won_ = false;
    stuck_ = board_.stuck();
    show_.stuck(stuck_);
    join(hint_);
    show_.state().hint_from = -1;
    if (board_.solved()) { won_ = true; won_t_ = 9; show_.won(); }
    if (cab_reduced_) show_.settle(board_);
    dirty_ = true;
    layout_buttons();
    request_frame();
}

void ThievesView::enter(int index, const std::string& history) {
    if (levels_.empty()) return;
    waiting_endless_ = false;
    index = std::clamp(index, 0, static_cast<int>(levels_.size()) - 1);
    current_ = levels_[static_cast<size_t>(index)];
    save_.level = index;
    board_.load(current_.level);
    if (!history.empty() && !board_.replay(history)) board_.restart();
    save_.history = board_.history();
    begin_level();
    say(current_.level.title.empty() ? "A new garden" : current_.level.title, kPaper);
    persist();
}

void ThievesView::enter_endless(const LevelEntry& e, const std::string& history) {
    current_ = e;
    save_.level = -1;
    save_.endless_xsb = joined(e.level.xsb());
    save_.endless_solution = e.level.solution;
    board_.load(e.level);
    if (!history.empty() && !board_.replay(history)) board_.restart();
    save_.history = board_.history();
    begin_level();
    say("Endless garden #" + std::to_string(save_.endless_cleared + 1), kPaper);
    persist();
    // grow the next one in the background
    if (cab_front_) start_endless_gen(save_.endless_tier);
}

void ThievesView::start_endless_gen(int tier) {
    if (gen_ && !(*gen_).done) return;
    join(gen_);
    gen_ = std::make_unique<Worker>();
    (*gen_).th = std::thread(&ThievesView::generate_worker, gen_.get(), tier, seed_now());
    request_frame();
}

void ThievesView::generate_worker(Worker* worker, int tier, std::uint64_t seed) {
    const Tier tr = kTiers[std::clamp(tier, 0, kTiersN - 1)];
    GenParams parameters;
    parameters.w = tr.w; parameters.h = tr.h; parameters.boxes = tr.boxes;
    parameters.min_pushes = tr.min_pushes; parameters.reverse_budget = tr.budget;
    parameters.seed = seed;
    const std::stop_token stop = (*worker).cancellation.get_token();
    GenResult result = generate(parameters, stop);
    if (!result.ok && !stop.stop_requested()) {
        parameters.boxes = std::max(2, parameters.boxes - 1);
        parameters.min_pushes -= 6; parameters.seed ^= 0x55;
        result = generate(parameters, stop);
    }
    std::lock_guard<std::mutex> lock((*worker).m);
    if (result.ok && !stop.stop_requested()) {
        (*worker).level.level = result.level;
        (*worker).level.par = result.pushes;
        (*worker).level.switches = result.box_lines;
        (*worker).level.section = std::string("Endless ") + tr.name;
        (*worker).level.level.title = std::string(tr.name) + " garden";
    }
    (*worker).done = true;
}

bool ThievesView::same_position(const Board& x) const {
            std::vector<int> p = x.boxes(), q = board_.boxes();
            std::sort(p.begin(), p.end());
            std::sort(q.begin(), q.end());
            if (p != q) return false;
            // the bear must be able to walk from one spot to the other
            const Level& lv = x.level();
            std::vector<char> seen(static_cast<size_t>(lv.w * lv.h), 0);
            std::vector<int> st{x.player()};
            seen[static_cast<size_t>(x.player())] = 1;
            while (!st.empty()) {
                const int c = st.back();
                st.pop_back();
                if (c == board_.player()) return true;
                for (int d = 0; d < 4; ++d) {
                    const int n = lv.step(c, d);
                    if (lv.floor(n) && x.box_at(n) < 0 && !seen[static_cast<size_t>(n)]) { seen[static_cast<size_t>(n)] = 1; st.push_back(n); }
                }
            }
            return false;
}

void ThievesView::start_hint() {
    if (won_) return;
    if (hint_ && !(*hint_).done) { say("Thinking...", kPaper); return; }
    // instant when the garden still lies somewhere along its known best solution: the rest of it is still best
    if (!current_.level.solution.empty()) {
        Board b;
        b.load(current_.level);
        const std::string& sol = current_.level.solution;
        int pushes_left = 0;
        for (char c : sol) pushes_left += c >= 'A' && c <= 'Z';
        for (size_t k = 0; k <= sol.size(); ++k) {
            const bool at_push = k == sol.size() || (sol[k] >= 'A' && sol[k] <= 'Z');
            if (at_push && same_position(b) && k < sol.size()) {
                // the next push from here: its pumpkin is in front of where the bear will stand
                Board w = b;
                const int d = dir_of(sol[k]);
                show_.state().hint_from = w.level().step(w.player(), d);
                show_.state().hint_dir = d;
                show_.state().hint_t = 0;
                say("Push that one " + std::string(d == kUp ? "up" : d == kDown ? "down" : d == kLeft ? "left" : "right") + ". " + std::to_string(pushes_left) +
                        (pushes_left == 1 ? " push to go." : " pushes to go."),
                    hex(0xFFF2A0));
                play("ct_hint", .6f, 1.2f);
                return;
            }
            if (k == sol.size() || !b.move(dir_of(sol[k]))) break;
            if (sol[k] >= 'A' && sol[k] <= 'Z') --pushes_left;
        }
    }
    join(hint_);
    hint_ = std::make_unique<Worker>();
    hint_for_ = board_.history();
    (*hint_).th = std::thread(&ThievesView::hint_worker, hint_.get(), board_);
    request_frame();
    say("Let me think...", kPaper);
    play("ct_hint", .5f);
    show_.thinking(true);
}

void ThievesView::hint_worker(Worker* worker, Board board) {
    const SolveResult result = solve(board, 2500000, (*worker).cancellation.get_token());
    std::lock_guard<std::mutex> lock((*worker).m);
    (*worker).solve = result;
    (*worker).done = true;
}

void ThievesView::poll_workers() {
    if (hint_ && (*hint_).done) {
        SolveResult r;
        { std::lock_guard<std::mutex> lk((*hint_).m); r = (*hint_).solve; }
        join(hint_);
        show_.thinking(false);
        if (hint_for_ != board_.history()) return;  // the board moved on while it thought
        if (r.solved && !r.lurd.empty()) {
            // the first push of a best solution: which pumpkin, which way
            Board b = board_;
            for (char c : r.lurd) {
                const int d = dir_of(c);
                if (d < 0) break;
                if (c >= 'A' && c <= 'Z') {
                    show_.state().hint_from = b.level().step(b.player(), d);
                    show_.state().hint_dir = d;
                    show_.state().hint_t = 0;
                    break;
                }
                b.move(d);
            }
            say("Push that one " + std::string(show_.state().hint_dir == kUp ? "up" : show_.state().hint_dir == kDown ? "down" : show_.state().hint_dir == kLeft ? "left" : "right") +
                    ". " + std::to_string(r.pushes) + (r.pushes == 1 ? " push to go." : " pushes to go."),
                hex(0xFFF2A0));
            play("ct_hint", .6f, 1.2f);
        } else if (!r.exhausted) {
            say("No way out from here. Undo a few moves.", hex(0xFFB0A0));
            show_.stuck(true);
        } else {
            say("Too tangled to see from here. Try undoing.", hex(0xFFB0A0));
        }
    }
    if (gen_ && (*gen_).done && waiting_endless_ && panel_ == Panel::none) {
        waiting_endless_ = false;
        next();
    }
}

// ------------------------------------------------------------------ moving
void ThievesView::step(int dir) {
    if (won_ || panel_ != Panel::none) return;
    if (dir < kUp || dir > kLeft) return;
    if (queue_.size() < 3) queue_.push_back(dir);
    request_frame();
    if (cab_reduced_) commit_queued_moves();
}

std::vector<int> ThievesView::path_to(int cell) const {
    const Level& lv = board_.level();
    if (!lv.floor(cell) || board_.box_at(cell) >= 0) return {};
    std::vector<int> prev(static_cast<size_t>(lv.w * lv.h), -1), q{board_.player()};
    prev[static_cast<size_t>(board_.player())] = board_.player();
    for (size_t k = 0; k < q.size(); ++k) {
        const int c = q[k];
        if (c == cell) break;
        for (int d = 0; d < 4; ++d) {
            const int n = lv.step(c, d);
            if (lv.floor(n) && board_.box_at(n) < 0 && prev[static_cast<size_t>(n)] < 0) { prev[static_cast<size_t>(n)] = c; q.push_back(n); }
        }
    }
    if (prev[static_cast<size_t>(cell)] < 0) return {};
    std::vector<int> path;
    for (int c = cell; c != board_.player(); c = prev[static_cast<size_t>(c)]) path.push_back(c);
    std::reverse(path.begin(), path.end());
    return path;
}

void ThievesView::walk_to(int cell) {
    if (won_ || cell < 0) return;
    const Level& lv = board_.level();
    // a pumpkin right beside him: push it
    for (int d = 0; d < 4; ++d)
        if (lv.step(board_.player(), d) == cell && board_.box_at(cell) >= 0) { queue_.clear(); queue_.push_back(d); request_frame(); if (cab_reduced_) commit_queued_moves(); return; }
    const std::vector<int> path = path_to(cell);
    if (path.empty()) return;
    queue_.clear();
    int at = board_.player();
    for (int c : path) {
        for (int d = 0; d < 4; ++d)
            if (lv.step(at, d) == c) { queue_.push_back(d); break; }
        at = c;
    }
    request_frame();
    if (cab_reduced_) commit_queued_moves();
}

void ThievesView::after_move() {
    save_.history = board_.history();
    dirty_ = true;
    show_.state().hint_from = -1;
    const bool s = board_.stuck();
    if (s != stuck_) {
        stuck_ = s;
        show_.stuck(s);
        if (s) { queue_.clear(); say("That pumpkin is stuck for good. Z to undo.", hex(0xFFB0A0)); }
    }
    show_.state().stuck_cell = -1;
    if (stuck_)
        for (int b : board_.boxes())
            if (!board_.level().goal[static_cast<size_t>(b)] && board_.dead_square(b)) { show_.state().stuck_cell = b; break; }
    if (board_.solved()) finish();
    if (cab_reduced_) show_.settle(board_);
    persist();
    request_frame();
}

void ThievesView::undo() {
    if (won_ || panel_ != Panel::none) return;
    queue_.clear();
    Move m;
    if (!board_.undo(&m)) return;
    show_.moved(board_, m, true);
    after_move();
}

void ThievesView::restart() {
    if (panel_ != Panel::none) return;
    queue_.clear();
    won_ = false;
    board_.restart();
    show_.restart(board_);
    stuck_ = false;
    show_.stuck(false);
    show_.state().stuck_cell = -1;
    show_.state().hint_from = -1;
    save_.history.clear();
    dirty_ = true;
    play("ct_restart", .6f);
    if (cab_reduced_) show_.settle(board_);
    persist();
    request_frame();
    layout_buttons();
}

void ThievesView::finish() {
    won_ = true;
    won_t_ = cab_reduced_ ? 9 : 0;
    queue_.clear();
    show_.won();
    if (save_.level >= 0) {
        LevelRecord& r = save_.records[save_.level];
        if (r.best_moves == 0 || board_.moves() < r.best_moves) r.best_moves = board_.moves();
        if (r.best_pushes == 0 || board_.pushes() < r.best_pushes) r.best_pushes = board_.pushes();
    } else {
        ++save_.endless_cleared;
    }
    persist();
    layout_buttons();
}

void ThievesView::next() {
    if (save_.level < 0) {
        if (gen_ && (*gen_).done) {
            LevelEntry e;
            bool ok = false;
            { std::lock_guard<std::mutex> lk((*gen_).m); ok = (*gen_).level.level.player >= 0; e = (*gen_).level; }
            join(gen_);
            if (ok) { enter_endless(e); return; }
            start_endless_gen(save_.endless_tier);
        }
        say("Growing the next garden...", kPaper);
        waiting_endless_ = true;
        return;
    }
    const int n = save_.level + 1;
    if (n >= static_cast<int>(levels_.size())) {
        say("Every garden in the book is safe! On to the endless gardens.", kGold);
        play("ct_stinger_book", .9f);
        audio_duck_music(1);
        save_.endless_tier = kTiersN - 1;
        open(Panel::map);
        return;
    }
    if (!section_open(levels_[static_cast<size_t>(n)].section)) {
        say("Clear half of this season's gardens to open the next.", kPaper);
        open(Panel::map);
        return;
    }
    enter(n);
}

// ------------------------------------------------------------------ frame
void ThievesView::play(const std::string& name, float gain, float rate) {
    audio_sfx(name, gain, rate, (opt_.hosted || save_.settings.sound) && cab_sound_ && cab_front_ && visible());
}

void ThievesView::say(const std::string& s, Col c) {
    message_ = s;
    message_t_ = 0;
    static_cast<void>(c);
}

void ThievesView::persist() {
    if (save_.level >= 0) save_.history = board_.history();
    else save_.history = board_.history();
    write_save(save_path(opt_.dev), save_);
    dirty_ = false;
}

void ThievesView::run_script() {
    while (!script_.empty() && script_.front().first <= t_) {
        const std::string code = script_.front().second;
        script_.erase(script_.begin());
        static_cast<void>(scripted_action(code));
    }
}

bool ThievesView::scripted_action(std::string_view requested) {
    if (!opt_.dev || !cab_front_ || requested.size() > 128) return false;
    const std::string code(requested);
    int numeric = 0;
    if (requested.starts_with("lvl") && !script_index(requested.substr(3), static_cast<int>(levels_.size()), numeric)) return false;
    if (requested.starts_with("end") && !script_index(requested.substr(3), kTiersN, numeric)) return false;
    if (requested.starts_with("mv") && !script_index(requested.substr(2), board_.level().w * board_.level().h, numeric)) return false;
        if (code == "u") step(kUp);
        else if (code == "d") step(kDown);
        else if (code == "l") step(kLeft);
        else if (code == "r") step(kRight);
        else if (code == "z") undo();
        else if (code == "rs") restart();
        else if (code == "h") start_hint();
        else if (code == "n") next();
        else if (code == "map") open(Panel::map);
        else if (code == "help") open(Panel::help);
        else if (code == "close") open(Panel::none);
        else if (code == "sol") {
            // the recorded solution from the start of the level, a letter per step
            for (char c : current_.level.solution) if (dir_of(c) >= 0) queue_.push_back(dir_of(c));
        }
        else if (code.rfind("lvl", 0) == 0) {
            const int level = std::atoi(code.c_str() + 3);
            if (level < 0 || level >= static_cast<int>(levels_.size())) return false;
            enter(level);
        }
        else if (code == "stuckme") {
            // dev: the shortest walk that wedges a pumpkin for good (breadth-first over a few moves)
            struct N { std::string path; };
            std::vector<N> q{{""}};
            for (size_t k = 0; k < q.size() && k < 20000; ++k) {
                Board b = board_;
                if (!b.replay(q[k].path)) continue;
                if (b.stuck()) { for (char c : q[k].path) queue_.push_back(dir_of(c)); break; }
                if (q[k].path.size() >= 8) continue;
                for (int d = 0; d < 4; ++d) {
                    Board c = b;
                    Move m;
                    if (c.move(d, &m)) q.push_back({q[k].path + (m.push ? kPush[d] : kWalk[d])});
                }
            }
        }
        else if (code.rfind("end", 0) == 0) action("endless" + code.substr(3));
        else if (code.rfind("mv", 0) == 0) walk_to(std::atoi(code.c_str() + 2));
        else return false;
    if (cab_reduced_) commit_queued_moves();
    request_frame();
    return true;
}

void ThievesView::commit_queued_moves() {
    while ((cab_reduced_ || !show_.busy()) && !queue_.empty() && !won_) {
        const int direction = queue_.front(); queue_.pop_front();
        Move move;
        if (board_.move(direction, &move)) { show_.moved(board_, move, false); after_move(); }
        else { show_.blocked(direction); queue_.clear(); if (cab_reduced_) show_.settle(board_); }
    }
}

void ThievesView::tick() {
    ++timer_callbacks_;
    if (!cab_front_ || !visible()) { if (timer_) (*timer_).stop(); return; }
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const double dt = std::clamp(std::chrono::duration<double>(now - last_).count(), 0.0, .25);
    last_ = now; t_ += dt;
    run_script(); poll_workers(); commit_queued_moves();
    const bool active = !attached_window() || (*attached_window()).active();
    const bool animate = !cab_reduced_ && active;
    if (animate) show_.update(queue_.size() > 2 ? dt * 1.6 : dt, board_);
    for (const Cue& cue : show_.cues)
        if (cue.kind == Cue::sound) play(cue.text, cue.gain, cue.rate);
    show_.cues.clear();
    GardenState& state = show_.state();
    state.fade = cab_reduced_ ? 0 : std::max(0.0, state.fade - dt * 2.5);
    state.hover_cell = panel_ == Panel::none && !won_ ? hover_cell_ : -1;
    state.path.clear();
    if (state.hover_cell >= 0 && queue_.empty()) state.path = path_to(state.hover_cell);
    if (won_) won_t_ = cab_reduced_ ? 9 : won_t_ + dt;
    if (message_t_ < 4.5) { message_t_ += dt; render_dirty_ = true; }
    audio_music(std::string("ct_music_") + kSeasonName[static_cast<int>(state.season)],
                (opt_.hosted || save_.settings.music) && cab_music_);
    audio_tick(dt);
    const bool hidden = attached_window() && (*attached_window()).occluded();
    if (!hidden && !garden_.r.rgb.empty() && !frame_.px.empty() && (render_dirty_ || animate)) {
        garden_.render(state, cab_reduced_ ? 0 : t_); compose(); publish(); render_dirty_ = false;
    }
    const bool worker_pending = (hint_ && !(*hint_).done) || (gen_ && !(*gen_).done);
    const bool ongoing = animate || worker_pending || !queue_.empty() || !script_.empty() ||
                         message_t_ < 4.5 || audio_needs_tick();
    if (timer_) {
        (*timer_).set_interval(std::chrono::milliseconds(animate ? (show_.busy() ? 33 : 67) : 50));
        if (!ongoing) (*timer_).stop();
    }
}

void ThievesView::publish() {
    if (!surface_) {
        gf::LiveSurfaceDescription d;
        d.width = static_cast<std::uint32_t>(phys_w_);
        d.height = static_cast<std::uint32_t>(phys_h_);
        surface_ = gf::LiveSurface::create(d);
        if (surface_ && attached_window()) direct_ = (*attached_window()).queue_live_surface_presentation(shared_from_this(), surface_);
    }
    if (!surface_) return;
    gf::LiveSurfaceWriteLease lease = (*surface_).try_acquire_write();
    if (lease && static_cast<int>(lease.width()) == phys_w_ && static_cast<int>(lease.height()) == phys_h_) {
        std::span<std::byte> dst = lease.pixels();
        const size_t rb = lease.row_bytes();
        const double k = pixel_ * bs_;
        if (xmap_.size() != static_cast<size_t>(phys_w_)) {
            xmap_.resize(static_cast<size_t>(phys_w_));
            for (int x = 0; x < phys_w_; ++x) xmap_[static_cast<size_t>(x)] = std::min(frame_.w - 1, static_cast<int>(x / k));
        }
        const std::uint32_t* src = reinterpret_cast<const std::uint32_t*>(frame_.px.data());
        for (int y = 0; y < phys_h_; ++y) {
            const int sy = std::min(frame_.h - 1, static_cast<int>(y / k));
            std::uint32_t* o = reinterpret_cast<std::uint32_t*>(dst.data() + static_cast<size_t>(y) * rb);
            const std::uint32_t* sr = src + static_cast<size_t>(sy) * frame_.w;
            for (int x = 0; x < phys_w_; ++x) o[x] = sr[xmap_[static_cast<size_t>(x)]];
        }
        blit_texts(reinterpret_cast<std::uint32_t*>(dst.data()), rb / 4, k);
        static_cast<void>(lease.publish());
        ++published_frames_;
    }
    if (!direct_) invalidate(gf::Dirty::paint);
    text_cache_trim();
}

// ------------------------------------------------------------------ input
void ThievesView::on_pointer(gf::PointerEvent& e) {
    if (!cab_front_) return;
    request_frame();
    const gf::Point local = point_from_window(e.position);
    mouse_x_ = local.x / pixel_;
    mouse_y_ = local.y / pixel_;
    mouse_in_ = e.action != gf::PointerAction::leave;
    if (e.action == gf::PointerAction::move || e.action == gf::PointerAction::leave) {
        hover_ = hit_button();
        hover_cell_ = mouse_in_ && hover_.empty() && panel_ == Panel::none && mouse_x_ < pw_ - hud_w_ ? garden_.pick_cell(mouse_x_, mouse_y_) : -1;
        if (hover_cell_ >= 0 && !board_.level().floor(hover_cell_)) hover_cell_ = -1;
        set_cursor(!hover_.empty() || hover_cell_ >= 0 ? gf::CursorKind::hand : gf::CursorKind::arrow);
    }
    if (e.action == gf::PointerAction::down && e.button == gf::PointerButton::primary) {
        activate();
        const std::string h = hit_button();
        if (!h.empty()) { pressed_ = h; e.handled = true; return; }
        if (panel_ == Panel::none && !won_) walk_to(garden_.pick_cell(mouse_x_, mouse_y_));
        e.handled = true;
    } else if (e.action == gf::PointerAction::up) {
        if (!pressed_.empty() && hit_button() == pressed_) action(pressed_);
        pressed_.clear();
        e.handled = true;
    }
}

void ThievesView::on_key(gf::KeyEvent& e) {
    if (e.handled || !cab_front_) return;
    request_frame();
    if (opt_.hosted && e.physical_key == gf::PhysicalKey::m) return;
    using K = gf::PhysicalKey;
    if (e.action != gf::KeyAction::down) return;
    const std::uint32_t k = e.physical_key;
    if (panel_ != Panel::none) {
        if (k == K::escape || k == K::enter || (k == K::l && panel_ == Panel::map) || (k == K::f1 && panel_ == Panel::help)) open(Panel::none);
        e.handled = true;
        return;
    }
    int dir = -1;
    if (k == K::up || k == K::w) dir = kUp;
    else if (k == K::down || k == K::s) dir = kDown;
    else if (k == K::left || k == K::a) dir = kLeft;
    else if (k == K::right || k == K::d) dir = kRight;
    if (dir >= 0) {
        if (e.repeat && queue_.size() > 1) { e.handled = true; return; }
        if (!e.repeat) queue_.clear();
        step(dir);
        e.handled = true;
        return;
    }
    if (k == K::z || k == K::backspace || k == K::u) { undo(); e.handled = true; return; }
    if (k == K::r) { restart(); e.handled = true; return; }
    if (k == K::h) { open(Panel::help); e.handled = true; return; }
    if (k == K::enter || k == K::n) { if (won_) next(); e.handled = true; return; }
    if (k == K::l) { open(Panel::map); e.handled = true; return; }
    if (k == K::f1) { open(Panel::help); e.handled = true; return; }
    if (k == K::m) { action("music"); e.handled = true; return; }
    if (k == K::escape) { e.handled = true; return; }
}

void ThievesView::action(const std::string& id) {
    request_frame();
    play("ct_click", .4f);
    if (panel_ == Panel::menu && (id == "undo" || id == "restart" || id == "hint" || id == "new")) open(Panel::none);
    if (id == "menu") open(panel_ == Panel::menu ? Panel::none : Panel::menu);
    else if (id == "new") next();
    else if (id == "close") open(Panel::none);
    else if (id == "undo") undo();
    else if (id == "restart") restart();
    else if (id == "hint") start_hint();
    else if (id == "map") open(Panel::map);
    else if (id == "help") open(Panel::help);
    else if (id == "next") next();
    else if (id == "again") restart();
    else if (id == "map_previous") { map_section_ = std::max(0, map_section_ - 1); layout_buttons(); }
    else if (id == "map_next") { ++map_section_; layout_buttons(); }
    else if (id == "sound" && !opt_.hosted) { save_.settings.sound = !save_.settings.sound; dirty_ = true; persist(); layout_buttons(); }
    else if (id == "music" && !opt_.hosted) { save_.settings.music = !save_.settings.music; dirty_ = true; persist(); layout_buttons(); }
    else if (id.rfind("lv", 0) == 0) {
        const int i = std::atoi(id.c_str() + 2);
        if (i >= 0 && i < static_cast<int>(levels_.size()) && section_open(levels_[static_cast<size_t>(i)].section)) { open(Panel::none); enter(i); }
    } else if (id.rfind("endless", 0) == 0) {
        const int tier = std::atoi(id.c_str() + 7);
        if (tier < 0 || tier >= kTiersN) return;
        open(Panel::none);
        if (save_.endless_tier != tier || save_.endless_xsb.empty()) {
            save_.endless_tier = tier;
            join(gen_);
            start_endless_gen(tier);
            save_.level = -1;
            won_ = true;
            won_t_ = 9;
            current_.section = std::string("Endless ") + kTiers[tier].name;
            save_.endless_xsb.clear();
            waiting_endless_ = true;
            say("Growing a garden...", kPaper);
        } else {
            LevelEntry e;
            if (Level::parse(split(save_.endless_xsb), e.level)) {
                e.level.solution = save_.endless_solution;
                e.section = std::string("Endless ") + kTiers[tier].name;
                enter_endless(e);
            }
        }
    }
}

void ThievesView::open(Panel p) {
    if (p == Panel::help && opt_.hosted && games::route_help(*this)) return;
    panel_ = p;
    request_frame();
    pressed_.clear();
    layout_buttons();
}

// ------------------------------------------------------------------ drawing
const Mask& ThievesView::tmask(const std::string& s, int font, double size, int wrap_game) const {
    const Font f = font == 1 ? Font::speech_bold : font == 2 ? Font::title : Font::speech;
    return text_mask(s, f, size * bs_, wrap_game > 0 ? wrap_game * pixel_ * bs_ : 0);
}
int ThievesView::text(const std::string& s, int x, int y, Col c, double size, int font, int wrap) {
    texts_.push_back({s, font, size, wrap, x, y, c});
    return text_w(s, size, font);
}
int ThievesView::text_w(const std::string& s, double size, int font) const {
    return static_cast<int>(std::ceil(tmask(s, font, size, 0).w / (pixel_ * bs_)));
}
int ThievesView::text_h(const std::string& s, double size, int font, int wrap) const {
    return static_cast<int>(std::ceil(tmask(s, font, size, wrap).h / (pixel_ * bs_)));
}

void ThievesView::layout_buttons() {
    buttons_.clear();
    if (pw_ <= 0) return;
    if (panel_ == Panel::none) {
        if (opt_.hosted) return;
        buttons_.push_back({"menu", "Menu", pw_ - hud_w_ + 10, ph_ - 24, hud_w_ - 22, 16});
        if (won_) {
            // the win card's buttons, under the garden
            const int gw = pw_ - hud_w_;
            const int cy = ph_ - 40;
            buttons_.push_back({"next", "Next garden", gw / 2 + 4, cy, 86, 18, 1});
            buttons_.push_back({"again", "Play again", gw / 2 - 90, cy, 86, 18});
        }
        return;
    }
    if (panel_ == Panel::menu) {
        const char* ids[] = {"new", "restart", "undo", "hint", "map", "help", "music", "sound", "close"};
        const char* names[] = {"New garden", "Start over", "Undo", "Hint", "Gardens", "Help", "Music", "Sound", "Close"};
        const int width = (pw_ - 44) / 3;
        for (int index = 0; index < 9; ++index) {
            std::string label = names[index];
            if (index == 6) label += save_.settings.music ? " on" : " off";
            if (index == 7) label += save_.settings.sound ? " on" : " off";
            buttons_.push_back({ids[index], label, 16 + index % 3 * (width + 6),
                                42 + index / 3 * 25, width, 20});
        }
        return;
    }
    if (panel_ == Panel::help) {
        const int ww = std::min(pw_ - 30, 400), wh = std::min(ph_ - 30, 300);
        const int wx = (pw_ - ww) / 2, wy = (ph_ - wh) / 2;
        buttons_.push_back({"close", "Back to the garden", wx + ww - 120, wy + wh - 22, 110, 15, 1});
        return;
    }
    std::vector<std::string> sections;
    for (const LevelEntry& entry : levels_)
        if (sections.empty() || sections.back() != entry.section) sections.push_back(entry.section);
    if (sections.empty()) return;
    map_section_ = std::clamp(map_section_, 0, static_cast<int>(sections.size()) - 1);
    const std::string& section = sections[static_cast<std::size_t>(map_section_)];
    const int columns = std::max(1, (pw_ - 32) / 18);
    int position = 0;
    for (std::size_t index = 0; index < levels_.size(); ++index) {
        if (levels_[index].section != section) continue;
        buttons_.push_back({"lv" + std::to_string(index), std::to_string(position + 1),
                            16 + position % columns * 18, 46 + position / columns * 18,
                            16, 16, 2, section_open(section)});
        ++position;
    }
    const int endless_y = std::max(94, ph_ - 46), tier_width = (pw_ - 32) / kTiersN;
    for (int tier = 0; tier < kTiersN; ++tier)
        buttons_.push_back({"endless" + std::to_string(tier), kTiers[tier].name,
                            16 + tier * tier_width, endless_y, tier_width - 3, 15});
    buttons_.push_back({"map_previous", "Previous", 16, ph_ - 25, 65, 15, 0, map_section_ > 0});
    buttons_.push_back({"map_next", "Next season", 86, ph_ - 25, 72, 15, 0,
                        map_section_ + 1 < static_cast<int>(sections.size())});
    buttons_.push_back({"close", "Close", pw_ - 76, ph_ - 25, 60, 15, 1});
}

void ThievesView::draw_button(const Button& b) {
    const bool down = pressed_ == b.id, over = hover_ == b.id && b.enabled;
    if (b.style == 2) {
        // a map tile: a pumpkin medal, or a seed packet still to plant
        const int i = std::atoi(b.id.c_str() + 2);
        const int m = medal(i);
        const bool here = save_.level == i;
        frame_.fill_rect(b.x, b.y, b.w, b.h, !b.enabled ? hex(0x8A8070, .5f) : over ? hex(0xFFF6D8) : here ? hex(0xFFE9A8) : hex(0xEAD9B0));
        frame_.begin(); frame_.rect(b.x + .5, b.y + .5, b.w - 1, b.h - 1); frame_.stroke(here ? kOrange : hex(0x8A7450), 1);
        if (m) medal_icon(frame_, b.x + b.w / 2.0, b.y + b.h / 2.0 + .5, 3.6, m);
        return;
    }
    const Col face = b.style == 1 ? (over ? hex(0x7AAE48) : kLeafGreen) : (over ? hex(0xFFF8E4) : kPaperDark);
    frame_.fill_rect(b.x + 1, b.y + 2, b.w, b.h, hex(0x000000, .25f));
    frame_.begin(); frame_.rrect(b.x, b.y + (down ? 1 : 0), b.w, b.h, 4); frame_.fill(face);
    frame_.begin(); frame_.rrect(b.x + .5, b.y + .5 + (down ? 1 : 0), b.w - 1, b.h - 1, 4); frame_.stroke(b.style == 1 ? kLeafDark : hex(0x8A7450), 1);
    const double size = 10.5;
    const int tw = text_w(b.label, size, 1), th = text_h(b.label, size, 1);
    text(b.label, b.x + (b.w - tw) / 2, b.y + (b.h - th) / 2 + (down ? 1 : 0), b.style == 1 ? hex(0xFFFFFF) : kInk, size, 1);
}

void ThievesView::draw_hud() {
    const int x = pw_ - hud_w_, w = hud_w_;
    // a paper card pinned to the hedge
    frame_.fill_rect(x + 4, 4, w - 8, ph_ - 8, hex(0x000000, .2f));
    frame_.begin(); frame_.rrect(x + 3, 3, w - 8, ph_ - 9, 6); frame_.fill(kPaper);
    frame_.begin(); frame_.rrect(x + 3.5, 3.5, w - 9, ph_ - 10, 6); frame_.stroke(kLeafDark, 1);
    int y = 10;
    const std::string sec = current_.section;
    text(sec, x + 10, y, kLeafGreen, 10, 1);
    y += 13;
    const std::string title = current_.level.title.empty() ? "A garden" : current_.level.title;
    text(title, x + 10, y, kInk, 13, 2, w - 22);
    y += text_h(title, 13, 2, w - 22) + 2;
    if (save_.level >= 0) {
        text("Garden " + std::to_string(save_.level + 1) + " of " + std::to_string(levels_.size()), x + 10, y, hex(0x8A7450), 9.5, 0);
    } else {
        text("Endless: " + std::to_string(save_.endless_cleared) + " cleared", x + 10, y, hex(0x8A7450), 9.5, 0);
    }
    y += 16;
    frame_.fill_rect(x + 10, y, w - 26, 1, hex(0xC8B890));
    y += 6;
    if (current_.par > 0) {
        text("Best possible", x + 10, y, hex(0x6A5A40), 10, 0);
        y += 12;
        text(std::to_string(current_.par) + " pushes", x + 10, y, kInk, 12, 1);
        y += 15;
    }
    // thieves caught so far, a pumpkin each
    y += 2;
    const int n = static_cast<int>(board_.boxes().size()), caught = board_.on_goal();
    text("Caught " + std::to_string(caught) + "/" + std::to_string(n), x + 10, y, hex(0x6A5A40), 10, 1);
    y += 18;
    if (save_.level >= 0) {
        const int m = medal(save_.level);
        if (m) {
            medal_icon(frame_, x + 16, y + 6, 5, m);
            const LevelRecord& r = save_.records.at(save_.level);
            text("Best: " + std::to_string(r.best_pushes) + " pushes", x + 26, y, hex(0x6A5A40), 10, 0);
        }
    }
    for (const Button& b : buttons_)
        if (b.id != "next" && b.id != "again") draw_button(b);
}

void ThievesView::draw_bubbles() {
    for (const Bubble& b : show_.bubbles) {
        V3 head;
        if (b.who < 0) head = bear_head_center(show_.state().bear) + V3{0, 0, .55};
        else if (b.who < static_cast<int>(show_.state().coons.size())) {
            const CoonPose& p = show_.state().coons[static_cast<size_t>(b.who)];
            head = (p.rise > .3 ? coon_head_center(p) : p.pos) + V3{0, 0, .55};
        } else continue;
        double hx, hy;
        garden_.to_screen(head, hx, hy);
        const double size = 10.5;
        const int wrap = 90;
        const int tw = std::min(wrap, text_w(b.text, size, 1)), th = text_h(b.text, size, 1, wrap);
        const int bw = tw + 10, bh = th + 6;
        const double pop = std::min(1.0, b.age * 8);
        int bx = static_cast<int>(hx - bw / 2.0), by = static_cast<int>(hy - bh - 6);
        bx = std::clamp(bx, 4, pw_ - hud_w_ - bw - 4);
        by = std::clamp(by, 4, ph_ - bh - 4);
        const float a = static_cast<float>(std::min(1.0, (b.life - b.age) * 3) * pop);
        const Col fill = b.who < 0 ? hex(0xFFF8E4, a) : hex(0xFFFFFF, a);
        frame_.begin(); frame_.move(std::clamp(hx, bx + 6.0, bx + bw - 6.0) - 4, by + bh - 1); frame_.line(hx, hy - 1); frame_.line(std::clamp(hx, bx + 6.0, bx + bw - 6.0) + 4, by + bh - 1); frame_.close(); frame_.fill(fill);
        frame_.begin(); frame_.rrect(bx, by, bw, bh, 5); frame_.fill(fill);
        frame_.begin(); frame_.rrect(bx + .5, by + .5, bw - 1, bh - 1, 5); frame_.stroke(hex(0x3A2A1A, a * .8f), 1);
        text(b.text, bx + 5, by + 3, alpha(kInk, a), size, 1, wrap);
    }
}

void ThievesView::draw_win_card() {
    if (!won_ || won_t_ < 1.6) return;
    const int gw = pw_ - hud_w_;
    const int w = std::min(210, gw - 8), h = 70, x = (gw - w) / 2, y = ph_ - 92;
    const float a = static_cast<float>(std::min(1.0, (won_t_ - 1.6) * 3));
    frame_.fill_rect(x + 3, y + 3, w, h, hex(0x000000, .25f * a));
    frame_.begin(); frame_.rrect(x, y, w, h, 8); frame_.fill(alpha(kPaper, a));
    frame_.begin(); frame_.rrect(x + .5, y + .5, w - 1, h - 1, 8); frame_.stroke(alpha(kLeafDark, a), 1.2);
    const std::string head = "All the thieves are caught!";
    text(head, x + (w - text_w(head, 13, 2)) / 2, y + 6, alpha(kLeafGreen, a), 13, 2);
    std::string sub = std::to_string(board_.moves()) + " steps, " + std::to_string(board_.pushes()) + " pushes";
    if (current_.par > 0) sub += board_.pushes() <= current_.par ? "  (perfect!)" : "  (best possible " + std::to_string(current_.par) + ")";
    text(sub, x + 8, y + 23, alpha(kInk, a), 10, 0, w - 16);
    for (const Button& b : buttons_)
        if (b.id == "next" || b.id == "again") draw_button(b);
}

void ThievesView::draw_map() {
    frame_.fill_rect(0, 0, pw_, ph_, hex(0x2A3A1A, .55f));
    frame_.begin(); frame_.rrect(8, 8, pw_ - 16, ph_ - 16, 8); frame_.fill(kPaper);
    frame_.begin(); frame_.rrect(8.5, 8.5, pw_ - 17, ph_ - 17, 8); frame_.stroke(kLeafDark, 1.2);
    text("The Garden Book", 18, 13, kLeafGreen, 15, 2);
    std::vector<std::string> sections;
    for (const LevelEntry& entry : levels_)
        if (sections.empty() || sections.back() != entry.section) sections.push_back(entry.section);
    if (!sections.empty()) {
        const std::string& section = sections[static_cast<std::size_t>(map_section_)];
        text(section + (section_open(section) ? "" : " (locked)"), 18, 31, kInk, 11, 1);
    }
    text("Endless gardens", 18, std::max(82, ph_ - 58), kInk, 9, 1);
    for (const Button& b : buttons_) draw_button(b);
    // the hovered garden's name
    for (const Button& b : buttons_)
        if (b.id == hover_ && b.style == 2) {
            const int i = std::atoi(b.id.c_str() + 2);
            const LevelEntry& e = levels_[static_cast<size_t>(i)];
            std::string s = e.level.title + "  -  best possible " + std::to_string(e.par) + " pushes";
            if (save_.records.count(i)) s += ",  yours " + std::to_string(save_.records.at(i).best_pushes);
            text(s, 130, 14, kInk, 9, 0, pw_ - 150);
        }
}

void ThievesView::draw_panel() {
    if (panel_ == Panel::none) return;
    if (panel_ == Panel::map) { draw_map(); return; }
    if (panel_ == Panel::menu) {
        frame_.fill_rect(0, 0, pw_, ph_, hex(0x2A3A1A, .55f));
        frame_.begin(); frame_.rrect(8, 8, pw_ - 16, ph_ - 16, 8); frame_.fill(kPaper);
        text("Garden commands", 18, 16, kLeafGreen, 15, 2);
        for (const Button& button : buttons_) draw_button(button);
        return;
    }
    frame_.fill_rect(0, 0, pw_, ph_, hex(0x2A3A1A, .5f));
    const int ww = std::min(pw_ - 30, 400), wh = std::min(ph_ - 30, 300);
    const int wx = (pw_ - ww) / 2, wy = (ph_ - wh) / 2;
    frame_.begin(); frame_.rrect(wx, wy, ww, wh, 8); frame_.fill(kPaper);
    frame_.begin(); frame_.rrect(wx + .5, wy + .5, ww - 1, wh - 1, 8); frame_.stroke(kLeafDark, 1.2);
    int ly = wy + 34;
    text("Catching Thieves", wx + (ww - text_w("Catching Thieves", 16, 2)) / 2, wy + 10, kLeafGreen, 16, 2);
    const char* paragraphs[] = {
        "Push a pumpkin onto every raccoon burrow. The bear can push, never pull. A pumpkin in a corner may need Undo.",
        "Every garden has a verified solution. Match its fewest pushes for a gold pumpkin.",
        "Arrows or WASD walk; click a square to walk there. Z undo, R restart, L gardens. Use Hint in the menu."};
    for (int index = 0; index < 3; ++index) {
        const std::string paragraph = paragraphs[index];
        text(paragraph, wx + 14, ly, kInk, 10, 0, ww - 28);
        ly += text_h(paragraph, 10, 0, ww - 28) + 4;
    }
    for (const Button& b : buttons_) draw_button(b);
}

void ThievesView::compose() {
    texts_.clear();
    garden_.r.present(frame_, 1, 0, 0, true);
    if (panel_ != Panel::none) { draw_panel(); return; }  // text sits on its own layer above the frame: nothing from below may show through
    draw_bubbles();
    draw_hud();
    draw_win_card();
    if (!message_.empty() && message_t_ < 4.5) {
        const float a = static_cast<float>(std::clamp(4.5 - message_t_, 0.0, 1.0));
        const int gw = pw_ - hud_w_;
        const int tw = text_w(message_, 12, 1);
        frame_.begin(); frame_.rrect(gw / 2.0 - tw / 2.0 - 8, 6, tw + 16, 18, 8); frame_.fill(hex(0x2A2016, .55f * a));
        text(message_, gw / 2 - tw / 2, 8, alpha(kPaper, a), 12, 1);
    }
}

void ThievesView::blit_texts(std::uint32_t* dst, size_t stride_px, double k) {
    const int sc = 1;
    for (const HiText& h : texts_) {
        const Mask& m = tmask(h.s, h.font, h.size, h.wrap);
        const int ox = static_cast<int>(h.x * k), oy = static_cast<int>(h.y * k);
        const float pr = h.c.r * h.c.a, pg = h.c.g * h.c.a, pb = h.c.b * h.c.a;
        for (int my = 0; my < m.h * sc; ++my) {
            const int dy = oy + my;
            if (dy < 0 || dy >= phys_h_) continue;
            const std::uint8_t* srow = m.a.data() + static_cast<size_t>(my / sc) * m.w;
            std::uint32_t* drow = dst + static_cast<size_t>(dy) * stride_px;
            for (int mx = 0; mx < m.w * sc; ++mx) {
                const int dx = ox + mx;
                if (dx < 0 || dx >= phys_w_) continue;
                const float cov = srow[mx / sc] * (1.f / 255.f);
                if (cov <= 0) continue;
                const std::uint32_t d = drow[dx];
                const float a = h.c.a * cov, kk = 1 - a;
                const std::uint32_t blue = static_cast<std::uint32_t>(std::min(255.f, pb * cov * 255 + static_cast<float>(d & 255) * kk + .5f));
                const std::uint32_t green = static_cast<std::uint32_t>(std::min(255.f, pg * cov * 255 + static_cast<float>((d >> 8) & 255) * kk + .5f));
                const std::uint32_t red = static_cast<std::uint32_t>(std::min(255.f, pr * cov * 255 + static_cast<float>((d >> 16) & 255) * kk + .5f));
                drow[dx] = blue | (green << 8) | (red << 16) | (0xFFu << 24);
            }
        }
    }
}

}  // namespace ct
