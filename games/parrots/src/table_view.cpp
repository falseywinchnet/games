#include "help_route.hpp"
#include "runtime_paths.hpp"
#include "table_view.hpp"

#include "platform/audio.hpp"
#include "platform/text.hpp"

#include "gui_forms/surface_material.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>

namespace pt {

namespace {
const Col kInk = hex(0x22301E), kPaper = hex(0xF6F0DE), kPaperDark = hex(0xE4DCC0), kGilt = hex(0xC8A048), kGreen = hex(0x2E8A48), kRed = hex(0xC03038),
          kGrey = hex(0x6A7480), kBand = hex(0x14261A);
const char* kNames[] = {"Ada", "Bram", "Cleo", "Dot", "Ezra", "Fig", "Gus", "Hattie", "Iggy", "Juno", "Kip", "Lulu", "Mango", "Nell", "Otto", "Pip", "Queenie", "Rollo", "Sal", "Tiko"};
constexpr int kNameCount = 20;
constexpr int kLook = 0;  // the house's look: A, white iron on a summer afternoon

std::uint64_t seed_now() { return static_cast<std::uint64_t>(wall_clock() * 1000) ^ 0x9A770CULL; }

std::filesystem::path save_path(bool dev, int version) {
    const std::string name = std::string("parrots_table") + (dev ? "-dev" : "") + "-v" + std::to_string(version) + ".txt";
    if (const char* dir = std::getenv("GAMES_STATE_DIR")) return std::filesystem::path(dir) / name;
    return games::state_directory() / name;
}
std::uint64_t fnv(const std::string& s) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}
// a saved file's body, its header and checksum checked; empty if it doesn't hold
std::string read_checked(const std::filesystem::path& p, const std::string& header) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string all = ss.str();
    if (all.size() > 16384 || all.rfind(header, 0) != 0) return {};
    const size_t ck = all.rfind("check=");
    if (ck == std::string::npos || ck < header.size()) return {};
    const std::string body = all.substr(header.size(), ck - header.size());
    if (std::to_string(fnv(body)) + "\n" != all.substr(ck + 6)) return {};
    return body;
}
V3 bezier(V3 a, V3 c, V3 b, double t) { return a * ((1 - t) * (1 - t)) + c * (2 * t * (1 - t)) + b * (t * t); }
}  // namespace

TableView::TableView(gf::StableId id, Options opt) : Control(std::move(id)), opt_(opt), rng_(seed_now()) {
    cab_front_ = !opt_.hosted;
    set_focusable(true);
    set_style(gf::ControlStyles::opaque, true);
    {
        gf::SurfaceMaterial none;
        none.fills = {gf::MaterialFillLayer::solid(gf::Color::rgba(0, 0, 0))};
        set_authored_surface_material(none);
    }
    set_accessible_name("The Paradoxiary. Parrots in a glasshouse aviary: one or two lie, two know the truth, and the rest only chatter. "
                        "Click a bird to hear it, ask the question bird one question, mark the birds, and name the culprit.");
    if (!load()) {
        load_old_records();
        new_table(1, seed_now(), true);
    }
    if (const char* sc = std::getenv("PT_SCRIPT"); sc && opt_.dev) {
        std::string all = sc;
        for (size_t pos = 0; pos < all.size();) {
            size_t end = all.find(',', pos);
            if (end == std::string::npos) end = all.size();
            const std::string item = all.substr(pos, end - pos);
            const size_t colon = item.find(':');
            if (colon != std::string::npos) script_cmds_.push_back({std::atof(item.c_str()), item.substr(colon + 1)});
            pos = end + 1;
        }
    }
}

double TableView::rand01() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 7;
    rng_ ^= rng_ << 17;
    return (rng_ >> 11) * (1.0 / 9007199254740992.0);
}

void TableView::on_attached_to_window() {
    audio_start(asset_dir());
    timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::milliseconds(33));
    subs_.push_back((*timer_).tick().subscribe(*this, gf::Delegate<>::bind<TableView, &TableView::tick>(*this)));
    last_ = std::chrono::steady_clock::now();
    (*timer_).start();
}

void TableView::on_detaching_from_window(gf::Window&) noexcept {
    try { persist(); } catch (...) {}
    if (timer_) (*timer_).stop();
    timer_.reset();
    audio_stop();
}

void TableView::activate() {
    if (attached_window()) static_cast<void>((*attached_window()).request_focus(shared_from_this()));
}

void TableView::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    pw_ = std::max(64, static_cast<int>(std::ceil(bounds.width / pixel_)));
    ph_ = std::max(64, static_cast<int>(std::ceil(bounds.height / pixel_)));
    frame_.resize(pw_, ph_);
    compact_ = pw_ < 460 || ph_ < 300;
    top0_ = opt_.hosted ? 26 : 0;
    top_h_ = top0_ + (compact_ ? 34 : 22);
    aviary_.resize(pw_, ph_, top_h_, 0);
    bs_ = attached_window() ? (*attached_window()).scale() : 1.0;
    phys_w_ = std::max(1, static_cast<int>(std::lround(bounds.width * bs_)));
    phys_h_ = std::max(1, static_cast<int>(std::lround(bounds.height * bs_)));
    // The surface takes its new size with the layout, before the next frame is drawn.
    if (pixels_.live()) static_cast<void>(pixels_.configure(phys_w_, phys_h_));
    layout_buttons();
}

void TableView::on_paint(gf::Painter& p, gf::Rect) {
    const gf::Rect b = client_rectangle();
    if (pixels_.live()) p.draw_live_surface(pixels_.live(), b);
    else p.fill_rect(b, gf::Color::rgba(20, 38, 26));
}

// ------------------------------------------------------------------ the table
void TableView::new_table(int level, std::uint64_t seed, bool fresh) {
    level_ = std::max(1, level);
    seed_ = seed;
    std::uint64_t h = seed ^ 0xB1BDULL;
    auto nxt = [&](int n) { h ^= h << 13; h ^= h >> 7; h ^= h << 17; return static_cast<int>(h % static_cast<std::uint64_t>(n)); };
    // five to ten birds, one or two liars, at any table; later tables say subtler things
    const int birds = 5 + nxt(6);
    const int liars = 1 + nxt(2);
    // a cast: names, manners, species (a scarlet macaw is the red bird)
    std::vector<int> nm(kNameCount), sp(kSpecies), mn(kManners);
    for (int i = 0; i < kNameCount; ++i) nm[static_cast<size_t>(i)] = i;
    for (int i = 0; i < kSpecies; ++i) sp[static_cast<size_t>(i)] = i;
    for (int i = 0; i < kManners; ++i) mn[static_cast<size_t>(i)] = i;
    for (int i = kNameCount - 1; i > 0; --i) std::swap(nm[static_cast<size_t>(i)], nm[static_cast<size_t>(nxt(i + 1))]);
    for (int i = kSpecies - 1; i > 0; --i) std::swap(sp[static_cast<size_t>(i)], sp[static_cast<size_t>(nxt(i + 1))]);
    for (int i = kManners - 1; i > 0; --i) std::swap(mn[static_cast<size_t>(i)], mn[static_cast<size_t>(nxt(i + 1))]);
    names_.clear();
    manners_.clear();
    species_.clear();
    flock_.assign(static_cast<size_t>(birds), Bird{});
    std::vector<bool> red(static_cast<size_t>(birds));
    for (int i = 0; i < birds; ++i) {
        names_.push_back(kNames[nm[static_cast<size_t>(i)]]);
        species_.push_back(sp[static_cast<size_t>(i % kSpecies)]);
        manners_.push_back(static_cast<Manner>(mn[static_cast<size_t>(i % kManners)]));
        red[static_cast<size_t>(i)] = flock_[static_cast<size_t>(i)].red = species_.back() == 0;
    }
    crime_ = nxt(crime_count());
    // the birds settle, and the case is set on where they sit
    layout_ = make_layout(seed);
    at_ = settle(layout_, flock_, seed);
    const Scene scene = scene_of(layout_, at_, red);
    GenParams g;
    g.birds = birds;
    g.liars = liars;
    g.tier = std::min(3, (level_ - 1) / 2);
    g.question = level_ >= 3 && nxt(2) == 0;
    g.choices = level_ >= 8 ? 4 : 3;
    g.seed = seed * 31 + 7;
    puz_ = generate(g, scene);
    if (puz_.birds != birds) {  // never seen; a plainer table rather than none
        g.question = false;
        g.tier = 0;
        puz_ = generate(g, scene);
    }
    script_ = Script(seed ^ 0x5C21ULL);
    script_.cast(names_, manners_, species_, flock_, scene, crime_);
    std::vector<bool> lies(static_cast<size_t>(birds));
    for (int i = 0; i < birds && i < static_cast<int>(puz_.roles.size()); ++i) lies[static_cast<size_t>(i)] = puz_.roles[static_cast<size_t>(i)] == Role::liar;
    script_.set_liars(lies);
    opener_ = script_.opener(puz_.liar_count, birds);
    offer_text_.clear();
    for (const Formula& f : puz_.offers) offer_text_.push_back(script_.question(puz_.asker, f));
    asked_ = -1;
    answer_ = false;
    marks_.assign(static_cast<size_t>(birds), unmarked);
    heard_.assign(static_cast<size_t>(birds), 0);
    next_claim_.assign(static_cast<size_t>(birds), 0);
    talk_.assign(static_cast<size_t>(birds), 0);
    shake_.assign(static_cast<size_t>(birds), 0);
    target_ = -1;
    splatted_ = false;
    shot_t_ = 0;
    table_points_ = liars_right_ = 0;
    won_ = false;
    popup_ = Popup{};
    // the scene: the birds, and how each flies in
    st_ = AviaryState{};
    st_.birds.resize(static_cast<size_t>(birds));
    st_.shaded.resize(static_cast<size_t>(birds));
    for (int i = 0; i < birds; ++i) {
        BirdPose& b = st_.birds[static_cast<size_t>(i)];
        b.species = species_[static_cast<size_t>(i)];
        b.manner = manners_[static_cast<size_t>(i)];
        b.size = kBirdSize;
        st_.shaded[static_cast<size_t>(i)] = scene.seats[static_cast<size_t>(i)].shade;
    }
    flights_.assign(static_cast<size_t>(birds), Flight{});
    const std::vector<Landing> order = landing_order(layout_, flock_, at_, seed);
    double start = 0;
    for (const Landing& l : order) {
        Flight& f = flights_[static_cast<size_t>(l.bird)];
        const double a = 2 * 3.14159265 * nxt(1000) / 1000.0;
        f.from = {std::cos(a) * 9, 2 + std::sin(a) * 7, 13.0 + nxt(3)};
        f.to = layout_.places[static_cast<size_t>(l.place)].pos;
        f.first_try = l.first_try;
        f.via = f.first_try >= 0 ? layout_.places[static_cast<size_t>(f.first_try)].pos : f.to;
        f.start = start;
        f.leg1 = 1.3 + len(f.via - f.from) / 12;
        f.rest = f.first_try >= 0 ? .5 : 0;
        f.leg2 = f.first_try >= 0 ? .7 + len(f.to - f.via) / 9 : 0;
        start += .35;
    }
    aviary_.set(layout_, kLook, seed);
    phase_t_ = 0;
    if (fresh && !cab_reduced_) {
        phase_ = Phase::arrive;
        play("pt_flap", .7f);
    } else {
        phase_ = Phase::play;
        settle_now();
    }
    if (fresh) play("pt_bell", .6f);
    panel_ = Panel::none;
    dirty_ = true;
    layout_buttons();
}

void TableView::settle_now() {
    if (phase_ == Phase::arrive) phase_ = Phase::play;
    for (Flight& f : flights_) f.start = -100;
    pose_birds(0);
    layout_buttons();
}

int TableView::weekday() const {
    const double days = std::floor(wall_clock() / 86400.0);
    return static_cast<int>(std::fmod(days + 4, 7.0));  // 1 January 1970 was a Thursday
}

void TableView::speak(int who, const std::string& text) {
    popup_ = Popup{};
    popup_.who = who;
    popup_.text = text;
    if (who >= 0 && who < static_cast<int>(talk_.size())) talk_[static_cast<size_t>(who)] = text.size() / 34.0 + .3;
    layout_buttons();
}

void TableView::click_bird(int bird) {
    if (bird < 0 || bird >= puz_.birds) return;
    if (phase_ == Phase::arrive) settle_now();
    if (phase_ == Phase::aim) {
        fire(bird);
        return;
    }
    if (phase_ == Phase::shot) return;
    const size_t b = static_cast<size_t>(bird);
    if (phase_ == Phase::verdict) {
        speak(bird, bird == puz_.culprit ? (won_ ? script_.guilty(bird) : script_.gloat(bird)) : script_.chatter(bird, weekday()));
        return;
    }
    if (popup_.who == bird && popup_.questions) return;
    std::string line;
    const std::vector<Formula>& claims = puz_.claims[b];
    if (bird == puz_.asker) {
        if (asked_ < 0) {
            popup_ = Popup{};
            popup_.who = bird;
            popup_.text = script_.ask_me(bird);
            popup_.questions = true;
            talk_[b] = popup_.text.size() / 34.0 + .3;
            ++heard_[b];
            play("pt_ask", .4f);
            layout_buttons();
            return;
        }
        line = rand01() < .5 ? "You asked: \"" + offer_text_[static_cast<size_t>(asked_)] + "\" " + script_.reply(bird, answer_) : script_.chatter(bird, weekday());
    } else if (!claims.empty() && (heard_[b] == 0 || rand01() < .65)) {
        // a playing bird's claims come round in turn, worded afresh each time
        line = script_.claim(bird, claims[static_cast<size_t>(next_claim_[b] % static_cast<int>(claims.size()))]);
        ++next_claim_[b];
    } else {
        line = script_.chatter(bird, weekday());
    }
    ++heard_[b];
    speak(bird, line);
    dirty_ = true;
}

void TableView::ask(int option) {
    if (phase_ != Phase::play || asked_ >= 0 || puz_.asker < 0 || option < 0 || option >= static_cast<int>(puz_.offers.size())) return;
    asked_ = option;
    answer_ = answer(puz_, option, puz_.truth());
    const int bird = puz_.asker;
    speak(bird, "You asked: \"" + offer_text_[static_cast<size_t>(option)] + "\" " + script_.reply(bird, answer_));
    play("pt_ask", .6f);
    dirty_ = true;
}

void TableView::cycle_mark(int bird) {
    // a right click: unmarked, truthful (green), liar (red), unmarked
    if (bird < 0 || bird >= puz_.birds || (phase_ != Phase::play && phase_ != Phase::aim)) return;
    int& m = marks_[static_cast<size_t>(bird)];
    m = (m + 1) % 3;
    play(m == mark_truthful ? "pt_mark_honest" : m == mark_liar ? "pt_mark_liar" : "pt_unmark", .7f, .95f + .1f * static_cast<float>(rand01()));
    dirty_ = true;
}

void TableView::fire(int bird) {
    // the paintball leaves the JE ACCUSE! and flies at the bird
    if (phase_ != Phase::aim || bird < 0 || bird >= puz_.birds) return;
    phase_ = Phase::shot;
    shot_t_ = 0;
    target_ = bird;
    splatted_ = false;
    splat_ = {(rand01() - .5) * .12, -.17, .32 + rand01() * .12};
    popup_ = Popup{};
    play("pt_click", .9f, .6f);
    layout_buttons();
}

void TableView::accuse(int bird) {
    if ((phase_ != Phase::play && phase_ != Phase::aim && phase_ != Phase::shot) || bird < 0 || bird >= puz_.birds) return;
    phase_ = Phase::verdict;
    phase_t_ = 0;
    won_ = bird == puz_.culprit;
    ++played_;
    // ten for the culprit; five more for each liar marked red
    liars_right_ = 0;
    for (int i = 0; i < puz_.birds; ++i)
        if (puz_.roles[static_cast<size_t>(i)] == Role::liar && marks_[static_cast<size_t>(i)] == mark_liar) ++liars_right_;
    table_points_ = (won_ ? 10 : 0) + 5 * liars_right_;
    points_ += table_points_;
    best_points_ = std::max(best_points_, table_points_);
    if (won_) {
        ++solved_;
        ++streak_;
        best_streak_ = std::max(best_streak_, streak_);
        speak(bird, script_.guilty(bird));
        play("pt_gavel", .9f);
        play("pt_stinger_win", .85f);
    } else {
        streak_ = 0;
        speak(bird, script_.wrongly(bird));
        play("pt_gavel", .9f);
        play("pt_stinger_lose", .85f);
    }
    open(Panel::none);
    persist();
}

// ------------------------------------------------------------------ saving
void TableView::persist() {
    std::ostringstream o;
    o << "level=" << level_ << "\nseed=" << seed_ << "\nphase=" << (phase_ == Phase::verdict ? 2 : 1) << "\nwon=" << won_ << "\nmarks=";
    for (int m : marks_) o << m;
    o << "\nasked=" << asked_ << "\nheard=";
    for (int h : heard_) o << std::min(h, 9);
    o << "\ntarget=" << target_ << "\ntablepoints=" << table_points_ << "\nliarsright=" << liars_right_;
    o << "\npoints=" << points_ << "\nbestpoints=" << best_points_;
    o << "\nsolved=" << solved_ << "\nplayed=" << played_ << "\nstreak=" << streak_ << "\nbest=" << best_streak_ << "\nsound=" << sound_ << "\nmusic=" << music_ << "\n";
    std::string body = o.str();
    body = "PARROTS2\n" + body + "check=" + std::to_string(fnv(body)) + "\n";
    const std::filesystem::path p = save_path(opt_.dev, 2);
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    {
        std::ofstream f(p.string() + ".tmp", std::ios::binary | std::ios::trunc);
        if (!f) return;
        f << body;
    }
    std::filesystem::rename(p.string() + ".tmp", p, ec);
    dirty_ = false;
}

bool TableView::load() {
    const std::string body = read_checked(save_path(opt_.dev, 2), "PARROTS2\n");
    if (body.empty()) return false;
    int level = 1, phase = 1, won = 0, asked = -1, target = -1, tp = 0, lr = 0;
    std::uint64_t seed = 1;
    std::string marks, heard;
    try {
        std::istringstream in(body);
        std::string line;
        while (std::getline(in, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "level") level = std::clamp(std::stoi(v), 1, 100000);
            else if (k == "seed") seed = std::stoull(v);
            else if (k == "phase") phase = std::stoi(v);
            else if (k == "won") won = std::stoi(v);
            else if (k == "marks") marks = v;
            else if (k == "asked") asked = std::stoi(v);
            else if (k == "heard") heard = v;
            else if (k == "target") target = std::stoi(v);
            else if (k == "tablepoints") tp = std::stoi(v);
            else if (k == "liarsright") lr = std::stoi(v);
            else if (k == "points") points_ = std::stoi(v);
            else if (k == "bestpoints") best_points_ = std::stoi(v);
            else if (k == "solved") solved_ = std::stoi(v);
            else if (k == "played") played_ = std::stoi(v);
            else if (k == "streak") streak_ = std::stoi(v);
            else if (k == "best") best_streak_ = std::stoi(v);
            else if (k == "sound") sound_ = v == "1";
            else if (k == "music") music_ = v == "1";
        }
    } catch (...) {
        return false;
    }
    new_table(level, seed, false);
    if (asked >= 0 && asked < static_cast<int>(puz_.offers.size())) {
        asked_ = asked;
        answer_ = answer(puz_, asked, puz_.truth());
    }
    for (size_t i = 0; i < marks.size() && i < marks_.size(); ++i) marks_[i] = std::clamp(marks[i] - '0', 0, 2);
    for (size_t i = 0; i < heard.size() && i < heard_.size(); ++i) heard_[i] = std::clamp(heard[i] - '0', 0, 9);
    if (phase == 2) {
        phase_ = Phase::verdict;
        won_ = won != 0;
        phase_t_ = 10;
        target_ = std::clamp(target, -1, puz_.birds - 1);
        splatted_ = target_ >= 0;
        splat_ = {0, -.17, .38};
        table_points_ = tp;
        liars_right_ = lr;
    }
    popup_ = Popup{};
    layout_buttons();
    return true;
}

void TableView::load_old_records() {
    // The Parrot's Table's records carry over to the aviary; its own save is left as it was.
    const std::string body = read_checked(save_path(opt_.dev, 1), "PARROTS1\n");
    if (body.empty()) return;
    try {
        std::istringstream in(body);
        std::string line;
        while (std::getline(in, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "solved") solved_ = std::stoi(v);
            else if (k == "played") played_ = std::stoi(v);
            else if (k == "streak") streak_ = std::stoi(v);
            else if (k == "best") best_streak_ = std::stoi(v);
            else if (k == "sound") sound_ = v == "1";
            else if (k == "music") music_ = v == "1";
        }
    } catch (...) {
        solved_ = played_ = streak_ = best_streak_ = 0;
    }
}

// ------------------------------------------------------------------ the frame
void TableView::play(const std::string& name, float gain, float rate) {
    audio_sfx(name, gain, rate, (opt_.hosted || sound_) && cab_sound_ && cab_front_ && visible());
}

void TableView::run_script() {
    while (!script_cmds_.empty() && script_cmds_.front().first <= t_) {
        const std::string c = script_cmds_.front().second;
        script_cmds_.erase(script_cmds_.begin());
        if (c.rfind("size", 0) == 0) {  // dev: size600x370 resizes the window
            int ww = 0, hh = 0;
            if (std::sscanf(c.c_str() + 4, "%dx%d", &ww, &hh) == 2) dev_resize_window(ww, hh);
            continue;
        }
        if (c == "skip") settle_now();
        else if (c.rfind("click", 0) == 0) click_bird(std::atoi(c.c_str() + 5));
        else if (c == "asker") click_bird(puz_.asker);
        else if (c == "talker") { for (int i = 0; i < puz_.birds; ++i) if (!puz_.claims[static_cast<size_t>(i)].empty()) { click_bird(i); break; } }
        else if (c == "askgood") { if (puz_.asker >= 0) { click_bird(puz_.asker); ask(puz_.good); } }
        else if (c == "truth") { for (int i = 0; i < puz_.birds; ++i) marks_[static_cast<size_t>(i)] = puz_.roles[static_cast<size_t>(i)] == Role::liar ? mark_liar : puz_.roles[static_cast<size_t>(i)] == Role::truthful ? mark_truthful : unmarked; }
        else if (c == "aim") action("jaccuse");
        else if (c == "right" || c == "wrong") {
            if (phase_ == Phase::play) action("jaccuse");
            fire(c == "right" ? puz_.culprit : (puz_.culprit + 1) % puz_.birds);
        }
        else if (c.rfind("mark", 0) == 0) cycle_mark(std::atoi(c.c_str() + 4));
        else if (c == "next") action("next");
        else if (c.rfind("lvl", 0) == 0) new_table(std::atoi(c.c_str() + 3), seed_now(), true);
        else if (c.rfind("seed", 0) == 0) new_table(level_, std::strtoull(c.c_str() + 4, nullptr, 10), true);
        else if (c == "help") open(Panel::help);
        else if (c == "records") open(Panel::records);
        else if (c == "close") action("close");
    }
}

void TableView::pose_birds(double dt) {
    const V3 eye = Aviary::eye();
    for (int i = 0; i < puz_.birds && i < static_cast<int>(st_.birds.size()); ++i) {
        BirdPose& b = st_.birds[static_cast<size_t>(i)];
        const Flight& f = flights_[static_cast<size_t>(i)];
        const V3 home = layout_.places[static_cast<size_t>(at_[static_cast<size_t>(i)])].pos;
        double& tk = talk_[static_cast<size_t>(i)];
        tk = std::max(0.0, tk - dt);
        // where it is: on the way in, or home
        const double tt = phase_t_ - f.start;
        bool flying = false;
        V3 pos = home, heading{0, 0, 0};
        if (phase_ == Phase::arrive && tt < f.leg1 + f.rest + f.leg2) {
            if (tt < 0) pos = f.from + V3{0, 0, 30};  // not yet in
            else if (tt < f.leg1) {
                const double u = tt / f.leg1;
                const V3 c = (f.from + f.via) * .5 + V3{0, 0, 3};
                pos = bezier(f.from, c, f.via, u);
                heading = bezier(f.from, c, f.via, std::min(1.0, u + .02)) - pos;
                flying = true;
            } else if (tt < f.leg1 + f.rest) {
                pos = f.via;  // tried this one: not right
                b.shake = std::sin((tt - f.leg1) / f.rest * 3.14159) * .8;
            } else {
                const double u = (tt - f.leg1 - f.rest) / f.leg2;
                const V3 c = (f.via + f.to) * .5 + V3{0, 0, 1.2};
                pos = bezier(f.via, c, f.to, u);
                heading = bezier(f.via, c, f.to, std::min(1.0, u + .02)) - pos;
                flying = true;
            }
        }
        b.pos = pos;
        const double face = std::atan2(eye.x - pos.x, -(eye.y - pos.y)) * .8;
        b.yaw = flying && std::hypot(heading.x, heading.y) > 1e-4 ? std::atan2(heading.x, -heading.y) : face;
        b.wing_l = b.wing_r = flying ? .55 + .45 * std::sin(t_ * 28 + i) : 0;
        b.beak = tk > 0 ? .5 + .5 * std::sin(t_ * 22 + i) : 0;
        if (tk > 0 && !flying) (i % 2 ? b.wing_r : b.wing_l) = .3 + .2 * std::sin(t_ * 5);
        // idle: heads turn to whoever talks, otherwise a glance about now and then (held
        // still between glances, so a quiet aviary needs no frames)
        const int talking = popup_.who;
        const double period = 5.5 + (i % 5) * 1.7;
        const auto slot = static_cast<std::uint32_t>(std::floor((t_ + i * 1.3) / period));
        auto h01 = [](std::uint32_t x) { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return (x & 0xFFFF) / 65535.0; };
        double toward = (h01(static_cast<std::uint32_t>(i) * 131u + slot * 7919u) - .5) * .9;
        const double tilt = (h01(static_cast<std::uint32_t>(i) * 71u + slot * 104729u) - .5) * .3;
        if (talking >= 0 && talking != i && talking < puz_.birds) {
            const V3 tp = st_.birds[static_cast<size_t>(talking)].pos;
            toward = std::clamp(std::atan2(tp.x - pos.x, -(tp.y - pos.y)) - b.yaw, -.9, .9);
        }
        b.head_turn += (toward - b.head_turn) * std::min(1.0, dt * 12);
        b.head_tilt += (tilt - b.head_tilt) * std::min(1.0, dt * 12);
        if (std::fabs(toward - b.head_turn) < .02) b.head_turn = toward;
        if (std::fabs(tilt - b.head_tilt) < .02) b.head_tilt = tilt;
        b.blink = std::fmod(t_ + i * 2.31, 6.5 + (i % 3) * 1.9) < .12 ? 1 : 0;
        double& sh = shake_[static_cast<size_t>(i)];
        sh = std::max(0.0, sh - dt * 1.5);
        if (!(phase_ == Phase::arrive && tt >= f.leg1 && tt < f.leg1 + f.rest)) b.shake = sh > 0 ? 1 - sh : 0;
        b.mark = marks_[static_cast<size_t>(i)] == mark_truthful ? 1 : marks_[static_cast<size_t>(i)] == mark_liar ? 2 : 0;
        b.hover = hover_bird_ == i && panel_ == Panel::none;
        b.fly = 0;
        b.bob = 0;
        b.ruffle = 0;
        b.head_nod = 0;
        if (phase_ == Phase::verdict) {
            if (i == puz_.culprit) {
                if (won_) { b.head_nod = .4; b.ruffle = .6; }
                else {
                    b.fly = std::clamp((phase_t_ - 2.6) / 2.0, 0.0, 1.0);
                    if (b.fly > 0) b.wing_l = b.wing_r = .5 + .5 * std::sin(t_ * 30);
                }
            } else if (won_) {
                const double u = std::fmod(t_ * 2 + i * .3, 1.0);
                b.bob = u < .5 ? u * 2 : 0;
            }
        }
        if (phase_ == Phase::shot && i == target_ && splatted_) {
            const double u = shot_t_ - .35;
            b.wing_l = b.wing_r = .6 + .4 * std::sin(u * 34);
            b.ruffle = .8;
            b.beak = 1;
            b.bob = std::fabs(std::sin(u * 9)) * .5;
        }
        if (cab_reduced_) {
            b.beak = b.bob = b.shake = b.blink = b.head_tilt = b.head_turn = 0;
            if (!flying) b.wing_l = b.wing_r = 0;
            if (b.fly > 0) b.fly = 1;
        }
    }
    if (phase_ == Phase::arrive) {
        bool all = true;
        for (const Flight& f : flights_) all = all && phase_t_ - f.start >= f.leg1 + f.rest + f.leg2;
        if (all) settle_now();
    }
}

void TableView::tick() {
    if (!visible() || !cab_front_) { last_ = std::chrono::steady_clock::now(); return; }
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::clamp(std::chrono::duration<double>(now - last_).count(), 0.0, .25);
    last_ = now;
    ++ticks_;
    t_ += dt;
    phase_t_ += dt;
    run_script();
    // the popup types out with a squawky babble
    if (popup_.who >= 0) {
        const int before = popup_.shown;
        popup_.age += dt;
        popup_.shown = std::min(static_cast<int>(popup_.text.size()), static_cast<int>(popup_.age * 40));
        if (popup_.shown / 3 != before / 3 && popup_.shown < static_cast<int>(popup_.text.size()) && popup_.text[static_cast<size_t>(popup_.shown)] != ' ') {
            const int sp = species_[static_cast<size_t>(popup_.who)];
            const float pitch = (sp == 6 || sp == 7) ? 1.35f : (sp <= 2 ? .85f : 1.05f);
            play("pt_voice_0" + std::to_string(1 + static_cast<int>(rand01() * 6)), .3f, pitch * (.94f + .12f * static_cast<float>(rand01())));
        }
    }
    if (phase_ == Phase::shot) {
        shot_t_ += dt;
        if (!splatted_ && shot_t_ >= .35) {
            splatted_ = true;  // splat: the bird flaps and squawks
            shake_[static_cast<size_t>(target_)] = 1;
            play("pt_flap", .9f);
            play("pt_contradiction", .7f, 1.4f);
        }
        if (shot_t_ >= 1.5) accuse(target_);
    }
    pose_birds(dt);
    if (phase_ == Phase::verdict && !won_ && phase_t_ > 2.4 && phase_t_ - dt <= 2.4) {
        speak(puz_.culprit, script_.gloat(puz_.culprit));
        play("pt_flap", .8f);
    }
    if (phase_ == Phase::verdict && won_ && phase_t_ > 2.2 && phase_t_ - dt <= 2.2) play("pt_cheer", .7f);
    st_.confetti = phase_ == Phase::verdict && won_ && !cab_reduced_ ? std::min(1.0, phase_t_ / .5) : 0;
    {
        const bool on = (opt_.hosted || music_) && cab_music_ && cab_front_;
        audio_music(visible() && cab_front_ ? "pt_music" : "", on);
    }
    audio_tick(dt);
    save_t_ += dt;
    if (dirty_ && save_t_ > .5) { save_t_ = 0; persist(); }
    gf::Window* win = attached_window();
    static const bool capturing = opt_.dev && std::getenv("PT_PRINT_WID");
    const bool hidden = win && (*win).occluded() && !capturing;
    const bool front = !win || (*win).active();
    const bool shown = visible();
    // busy (birds flying, a bird talking, the verdict): thirty a second; a quiet aviary breathes at fifteen
    bool busy = phase_ == Phase::arrive || phase_ == Phase::aim || phase_ == Phase::shot || (phase_ == Phase::verdict && phase_t_ < 6);
    for (double tk : talk_) busy = busy || tk > 0;
    busy = busy || (popup_.who >= 0 && popup_.shown < static_cast<int>(popup_.text.size()));
    const auto pace = std::chrono::milliseconds(hidden || !shown || !cab_front_ ? 500 : !front ? 200 : busy ? 33 : 66);
    if (timer_ && (*timer_).interval() != pace) (*timer_).set_interval(pace);
    if (hidden || !shown || frame_.px.empty() || aviary_.r.rgb.empty()) return;
    // nothing moved and nothing changed on screen: no frame
    {
        std::uint64_t sig = 1469598103934665603ULL;
        auto mixin = [&](double v) { sig = (sig ^ static_cast<std::uint64_t>(std::llround(v * 512))) * 1099511628211ULL; };
        for (const BirdPose& b : st_.birds) {
            for (double v : {b.pos.x, b.pos.y, b.pos.z, b.yaw, b.bob, b.head_tilt, b.head_turn, b.head_nod, b.beak, b.wing_l, b.wing_r, b.blink, b.ruffle, b.shake, b.fly})
                mixin(v);
            mixin(b.mark);
            mixin(b.hover);
        }
        mixin(static_cast<int>(phase_));
        mixin(static_cast<int>(panel_));
        mixin(popup_.who);
        mixin(popup_.shown);
        mixin(asked_);
        mixin(hover_bird_);
        mixin(static_cast<double>(std::hash<std::string>{}(hover_ + "|" + pressed_) & 0xFFFFFF));
        for (int m : marks_) mixin(m);
        mixin(pw_);
        mixin(ph_);
        mixin(bs_);
        if (phase_ == Phase::aim || phase_ == Phase::shot || phase_ == Phase::verdict) { mixin(mouse_x_); mixin(mouse_y_); mixin(shot_t_); mixin(phase_t_ < 9 ? phase_t_ : 9); }
        if (phase_ == Phase::arrive || (phase_ == Phase::play && phase_t_ < 9)) mixin(phase_t_ < 9 ? std::floor(phase_t_) : 9);
        if (sig == drawn_sig_ && pixels_.live()) return;
        drawn_sig_ = sig;
    }
    aviary_.render(st_, cab_reduced_ ? 0 : t_);
    compose();
    publish();
}

void TableView::publish() {
    if (!pixels_.configure(phys_w_, phys_h_)) return;
    if (!pixels_attached_ && attached_window()) {
        static_cast<void>(pixels_.attach(*attached_window(), shared_from_this()));
        pixels_attached_ = true;
        invalidate(gf::Dirty::paint);
    }
    const double k = pixel_ * bs_;
    const std::optional<render::Surface::Frame> frame = pixels_.begin(frame_, k, text_marks(k));
    if (frame) {
        blit_texts((*frame).target, (*frame).repair, k);
        pixels_.publish();
        if (!pixels_.direct()) invalidate(gf::Dirty::paint);
    }
    text_cache_trim();
}

// ------------------------------------------------------------------ input
void TableView::on_pointer(gf::PointerEvent& e) {
    const gf::Point local = point_from_window(e.position);
    mouse_x_ = local.x / pixel_;
    mouse_y_ = local.y / pixel_;
    auto hit = [&]() -> std::string {
        for (const Button& b : buttons_)
            if (b.enabled && mouse_x_ >= b.x && mouse_x_ < b.x + b.w && mouse_y_ >= b.y && mouse_y_ < b.y + b.h) return b.id;
        return {};
    };
    auto in_popup = [&]() {
        if (popup_.who < 0 || panel_ != Panel::none) return false;
        int x, y, w, h, th, oy;
        popup_box(x, y, w, h, th, oy);
        return mouse_x_ >= x && mouse_x_ < x + w && mouse_y_ >= y && mouse_y_ < y + h;
    };
    if (e.action == gf::PointerAction::wheel) return;
    if (e.action == gf::PointerAction::move) {
        hover_ = hit();
        hover_bird_ = hover_.empty() && panel_ == Panel::none && !in_popup() && mouse_y_ > top_h_ ? aviary_.pick_bird(mouse_x_, mouse_y_, st_) : -1;
        set_cursor(phase_ == Phase::aim && hover_.empty() ? gf::CursorKind::crosshair : !hover_.empty() || hover_bird_ >= 0 ? gf::CursorKind::hand : gf::CursorKind::arrow);
    }
    if (e.action == gf::PointerAction::down && e.button == gf::PointerButton::secondary) {
        activate();
        const int b = panel_ == Panel::none && mouse_y_ > top_h_ ? aviary_.pick_bird(mouse_x_, mouse_y_, st_) : -1;
        if (b >= 0) cycle_mark(b);
        e.handled = true;
        return;
    }
    if (e.action == gf::PointerAction::down && e.button == gf::PointerButton::primary) {
        activate();
        const std::string h = hit();
        if (!h.empty()) { pressed_ = h; e.handled = true; return; }
        if (panel_ != Panel::none) { e.handled = true; return; }
        if (in_popup()) { e.handled = true; return; }
        const int b = mouse_y_ > top_h_ ? aviary_.pick_bird(mouse_x_, mouse_y_, st_) : -1;
        if (b >= 0) click_bird(b);
        else if (phase_ == Phase::aim) {}  // a shot at the leaves is no shot: aim again
        else if (phase_ == Phase::arrive) settle_now();
        else if (popup_.who >= 0) { popup_ = Popup{}; layout_buttons(); }  // a click on the leaves: the popup closes
        e.handled = true;
    } else if (e.action == gf::PointerAction::up) {
        if (!pressed_.empty() && hit() == pressed_) action(pressed_);
        pressed_.clear();
        e.handled = true;
    }
}

void TableView::on_key(gf::KeyEvent& e) {
    if (e.handled || e.action != gf::KeyAction::down) return;
    using K = gf::PhysicalKey;
    const std::uint32_t k = e.physical_key;
    if (k == K::escape) {
        if (panel_ != Panel::none) open(Panel::none);
        else if (phase_ == Phase::aim) action("cancel");
        else if (popup_.who >= 0) { popup_ = Popup{}; layout_buttons(); }
        e.handled = true;
        return;
    }
    if (k == K::f1) { open(panel_ == Panel::help ? Panel::none : Panel::help); e.handled = true; return; }
    if (k == K::enter || k == K::space) {
        if (phase_ == Phase::arrive) settle_now();
        else if (phase_ == Phase::verdict) action("next");
        else if (panel_ != Panel::none) open(Panel::none);
        e.handled = true;
        return;
    }
    if (!opt_.hosted && k == K::m) { action("music"); e.handled = true; return; }
}

void TableView::action(const std::string& id) {
    play("pt_click", .4f);
    if (id == "close") open(Panel::none);
    else if (id == "help") open(Panel::help);
    else if (id == "records") open(Panel::records);
    else if (id == "music") { music_ = !music_; dirty_ = true; layout_buttons(); }
    else if (id == "jaccuse") {
        // pressed in: the word brightens and a crosshair takes the pointer; pressed again, it lets go
        if (phase_ == Phase::play) { phase_ = Phase::aim; popup_ = Popup{}; play("pt_gavel", .5f, 1.3f); layout_buttons(); }
        else if (phase_ == Phase::aim) action("cancel");
    }
    else if (id == "cancel") { if (phase_ == Phase::aim) { phase_ = Phase::play; layout_buttons(); } }
    else if (id == "next") new_table(won_ ? level_ + 1 : level_, seed_now(), true);
    else if (id == "newtable") new_table(level_, seed_now(), true);  // another table at the level reached
    else if (id.rfind("q", 0) == 0) ask(std::atoi(id.c_str() + 1));
}

void TableView::open(Panel p) {
    if (p == Panel::help && games::route_help(*this)) return;
    panel_ = p;
    pressed_.clear();
    layout_buttons();
}

// ------------------------------------------------------------------ layout
const Mask& TableView::tmask(const std::string& s, int font, double size, int wrap_game) const {
    const Font f = font == 1 ? Font::speech_bold : font == 2 ? Font::title : font == 3 ? Font::ui : Font::speech;
    return text_mask(s, f, size, wrap_game > 0 ? wrap_game * pixel_ : 0);
}
int TableView::text(const std::string& s, int x, int y, Col c, double size, int font, int wrap) {
    texts_.push_back({s, font, size, wrap, x, y, c});
    return text_w(s, size, font);
}
int TableView::text_w(const std::string& s, double size, int font) const { return static_cast<int>(std::ceil(tmask(s, font, size, 0).w / static_cast<double>(pixel_))); }
int TableView::text_h(const std::string& s, double size, int font, int wrap) const { return static_cast<int>(std::ceil(tmask(s, font, size, wrap).h / static_cast<double>(pixel_))); }

// The popup beside its bird: above the head when there is room, else below; the text, then
// the questions (a question bird not yet asked), then the marks.
void TableView::popup_box(int& x, int& y, int& w, int& h, int& th, int& options_y) const {
    w = std::min(compact_ ? 190 : 220, pw_ - 12);
    const int wrap = w - 16;
    th = text_h(popup_.text, 11, 0, wrap);
    int oh = 0;
    if (popup_.questions && asked_ < 0)
        for (const std::string& q : offer_text_) oh += std::max(14, text_h(q, 10.5, 0, wrap - 8) + 6) + 3;
    h = 6 + 12 + th + 6 + oh + 2;
    // where it may go: above the head, below the feet, or either side; the place that
    // covers fewest other birds and stays clear of the banners wins
    const int ceiling = top_h_ + 4 + (phase_ == Phase::aim || phase_ == Phase::verdict ? 22 : 0);
    const int floor_y = ph_ - 26;
    double hx = pw_ / 2.0, hy = ph_ / 2.0, fx = hx, fy = hy;
    if (popup_.who >= 0 && popup_.who < static_cast<int>(st_.birds.size())) {
        const BirdPose& b = st_.birds[static_cast<size_t>(popup_.who)];
        aviary_.to_screen(bird_head(b) + V3{0, 0, .35}, hx, hy);
        aviary_.to_screen(b.pos - V3{0, 0, .1}, fx, fy);
    }
    const double half = std::max(10.0, (fy - hy) / 2), mid = (hy + fy) / 2;
    const int cx = static_cast<int>(hx) - w / 2;
    const int cand[4][2] = {{cx, static_cast<int>(hy) - h - 6}, {cx, static_cast<int>(fy) + 14},
                            {static_cast<int>(hx - half) - w - 8, static_cast<int>(mid) - h / 2}, {static_cast<int>(hx + half) + 8, static_cast<int>(mid) - h / 2}};
    double best = 1e18;
    x = cand[0][0];
    y = cand[0][1];
    for (const auto& c : cand) {
        const int cx0 = std::clamp(c[0], 4, std::max(4, pw_ - w - 4)), cy0 = std::clamp(c[1], ceiling, std::max(ceiling, floor_y - h));
        double cost = std::abs(cx0 - c[0]) * 2.0 + std::abs(cy0 - c[1]) * 2.0;  // pushed off its place by the edges
        for (int i = 0; i < puz_.birds && i < static_cast<int>(st_.birds.size()); ++i) {
            double bx, by, tx2, ty2;
            aviary_.to_screen(st_.birds[static_cast<size_t>(i)].pos, bx, by);
            aviary_.to_screen(bird_head(st_.birds[static_cast<size_t>(i)]), tx2, ty2);
            const double bw = std::max(8.0, (by - ty2) * .7);
            const double ox = std::max(0.0, std::min<double>(cx0 + w, bx + bw) - std::max<double>(cx0, bx - bw));
            const double oy = std::max(0.0, std::min<double>(cy0 + h, by + 12) - std::max<double>(cy0, ty2 - 6));
            cost += ox * oy * (i == popup_.who ? 4 : 1);
        }
        if (cost < best) { best = cost; x = cx0; y = cy0; }
    }
    options_y = y + 6 + 12 + th + 6;
}

void TableView::layout_game_buttons() {
    buttons_.clear();
    if (pw_ <= 0) return;
    const int bw = 96, bh = 16;
    const int x = pw_ - bw - 8, y = ph_ - bh - 8;
    if (panel_ == Panel::help || panel_ == Panel::records) {
        int wx, wy, ww, wh;
        panel_box(wx, wy, ww, wh);
        buttons_.push_back({"close", "Close", pw_ / 2 - 40, wy + wh + 6, 80, 16});
        return;
    }
    if (phase_ == Phase::verdict) buttons_.push_back({"next", won_ ? "Next table" : "Try another", x, y, bw, bh});
    else {
        int jx, jy, jw, jh;
        jaccuse_box(jx, jy, jw, jh);
        buttons_.push_back({"jaccuse", "JE ACCUSE!", jx, jy, jw, jh, phase_ == Phase::play || phase_ == Phase::aim});
    }
    if (!opt_.hosted) {
        buttons_.push_back({"help", "Help", 8, y, 44, bh});
        buttons_.push_back({"music", music_ ? "Music" : "No music", 56, y, 56, bh});
    }
    // a question bird's questions, as lines in its popup
    if (popup_.who >= 0 && popup_.questions && asked_ < 0) {
        int px, py, pw, ph, th, oy;
        popup_box(px, py, pw, ph, th, oy);
        int qy = oy;
        for (size_t k = 0; k < offer_text_.size(); ++k) {
            const int qh = std::max(14, text_h(offer_text_[k], 10.5, 0, pw - 24) + 6);
            buttons_.push_back({"q" + std::to_string(k), offer_text_[k], px + 8, qy, pw - 16, qh, phase_ == Phase::play});
            qy += qh + 3;
        }
    }
}

void TableView::panel_box(int& wx, int& wy, int& ww, int& wh) const {
    ww = std::min(pw_ - 16, 400);
    const int room = ph_ - 16 - 22;  // Close sits beneath
    for (double size : {11.5, 10.5, 9.5, 9.0}) {
        ov_size_ = size;
        wh = 30 + 8;
        for (const std::string& l : panel_lines()) wh += text_h(l, size, 0, ww - 28) + 4;
        if (wh <= room) break;
    }
    wh = std::min(wh, room);
    wx = (pw_ - ww) / 2;
    wy = std::max(6, (ph_ - 22 - wh) / 2);
}

std::vector<std::string> TableView::panel_lines() const {
    if (panel_ == Panel::help)
        return {"Something has happened in the aviary, and one of the parrots did it. One or two of the birds are liars: everything they say about it is false. Two know the truth and tell it. The rest know nothing and only chatter.",
                "Click a bird to hear what it has to say. Ask again and it may say something else. The birds who are playing come back to the same few facts, in new words; the chatterers talk about the weather, the day, their perches and their jokes. Finding out who is playing is part of the case.",
                "Some claims you can check by looking: who sits on rope or brass, in the shade or the sun, high or low, beside whom. A liar caught out by eye is a liar for certain.",
                "On later tables one of the playing birds won't volunteer anything. Ask it one question: only one of the questions on offer settles the case.",
                "Right-click a bird to mark it truthful (green), then liar (red), then unmarked. When you're sure, press JE ACCUSE!, aim the crosshair at the culprit and click: the paintball tells. Ten points for the culprit, and five more for each liar you marked red.",
                "Keys: Esc closes a popup or lowers the crosshair.  Enter continues.  F1 help."};
    if (panel_ == Panel::records)
        return {"Cases solved: " + std::to_string(solved_) + " of " + std::to_string(played_), "Points: " + std::to_string(points_) + "   Best table: " + std::to_string(best_points_),
                "Current streak: " + std::to_string(streak_), "Best streak: " + std::to_string(best_streak_), "Now at table " + std::to_string(level_) + "."};
    return {};
}

void TableView::layout_buttons() {
    layout_game_buttons();
    if (!opt_.hosted) return;
    for (std::size_t i = buttons_.size(); i > 0; --i) {
        const std::string& id = buttons_[i - 1].id;
        if (id == "help" || id == "records" || id == "music") buttons_.erase(buttons_.begin() + static_cast<std::ptrdiff_t>(i - 1));
    }
}

// ------------------------------------------------------------------ drawing
void TableView::draw_button(const Button& b) {
    if (b.id == "jaccuse") { draw_jaccuse(); return; }
    if (b.id.rfind("q", 0) == 0) {
        // a question on offer: a dialogue line, lit under the pointer
        const bool over = hover_ == b.id && b.enabled;
        if (over) { frame_.begin(); frame_.rrect(b.x - 2, b.y, b.w + 4, b.h, 3); frame_.fill(hex(0xE8F0D8)); }
        text("\u203A", b.x, b.y + 2, kGreen, 11, 1);
        text(b.label, b.x + 9, b.y + 2, over ? kGreen : kInk, 10.5, 0, b.w - 12);
        return;
    }
    const bool down = pressed_ == b.id, over = hover_ == b.id && b.enabled;
    const bool tinted = b.tint.a > 0;
    const Col face = !b.enabled ? hex(0xB8B09A) : tinted ? b.tint : over ? hex(0xFFF8E6) : kPaperDark;
    frame_.fill_rect(b.x + 1, b.y + 2, b.w, b.h, hex(0x000000, .3f));
    frame_.begin(); frame_.rrect(b.x, b.y + (down ? 1 : 0), b.w, b.h, 3); frame_.fill(face);
    frame_.begin(); frame_.rrect(b.x + .5, b.y + .5 + (down ? 1 : 0), b.w - 1, b.h - 1, 3); frame_.stroke(over ? kInk : kGilt, 1);
    const double size = b.id.rfind("q", 0) == 0 ? 10.5 : b.id.rfind("mark", 0) == 0 ? 9.5 : 11;
    const int wrap = b.id.rfind("q", 0) == 0 ? b.w - 8 : 0;
    const int tw = std::min(b.w - 8, text_w(b.label, size, 0)), th = text_h(b.label, size, 0, wrap);
    text(b.label, b.x + std::max(4, (b.w - tw) / 2), b.y + (b.h - th) / 2 + (down ? 1 : 0), !b.enabled ? hex(0x7A6E5A) : tinted ? kPaper : kInk, size, 0, wrap);
}

void TableView::draw_top() {
    // the keeper's band: the table, the case, and the counts
    const int h = top_h_, t0 = top0_;
    frame_.fill_rect(0, t0, pw_, h - t0, alpha(kBand, .82f));
    frame_.fill_rect(0, h, pw_, 1, kGilt);
    text("Table " + std::to_string(level_), 8, t0 + 5, kGilt, 11, 1);
    const std::string counts = std::to_string(puz_.liar_count) + (puz_.liar_count == 1 ? " liar" : " liars") + ", 2 truthful, " + std::to_string(puz_.birds) + " birds";
    const std::string s = crime(crime_).what + ".";
    if (compact_) {
        text(counts, pw_ - 8 - text_w(counts, 10.5, 1), t0 + 5, kPaper, 10.5, 1);
        text(s, 8, t0 + 19, kPaper, 10.5, 0, pw_ - 16);
    } else {
        const int cw = text_w(counts, 11, 1);
        text(s, 70, t0 + 5, kPaper, 11, 0);
        text(counts, pw_ - 10 - cw, t0 + 5, kGilt, 11, 1);
    }
}

void TableView::draw_labels() {
    // each bird's name under its perch, coloured by the player's mark (by the truth, after the
    // verdict); labels that would overlap step down below one another
    struct Label { int who; std::string text; int x, y, w; int mark; };
    std::vector<Label> labels;
    const double size = compact_ ? 9 : 10;
    for (int i = 0; i < puz_.birds && i < static_cast<int>(st_.birds.size()); ++i) {
        const BirdPose& b = st_.birds[static_cast<size_t>(i)];
        if (phase_ == Phase::arrive && len(b.pos - layout_.places[static_cast<size_t>(at_[static_cast<size_t>(i)])].pos) > .05) continue;
        if (b.fly > .3) continue;
        double x, y;
        aviary_.to_screen(b.pos - V3{0, 0, .12}, x, y);
        int m = marks_[static_cast<size_t>(i)];
        std::string tag;
        if (phase_ == Phase::verdict) {
            const Role r = puz_.roles[static_cast<size_t>(i)];
            m = r == Role::liar ? mark_liar : r == Role::truthful ? mark_truthful : unmarked;
            tag = r == Role::liar ? " (liar)" : r == Role::truthful ? " (truthful)" : "";
        }
        const std::string text_s = names_[static_cast<size_t>(i)] + tag;
        const int w = text_w(text_s, size, 1) + 8;
        labels.push_back({i, text_s, static_cast<int>(x) - w / 2, static_cast<int>(y) + 1, w, m});
    }
    std::sort(labels.begin(), labels.end(), [](const Label& a, const Label& b) { return a.y < b.y || (a.y == b.y && a.x < b.x); });
    for (size_t k = 0; k < labels.size(); ++k)
        for (size_t j = 0; j < k; ++j) {
            Label& a = labels[k];
            const Label& o = labels[j];
            if (a.x < o.x + o.w + 2 && o.x < a.x + a.w + 2 && a.y < o.y + 12 && o.y < a.y + 12) a.y = o.y + 12;
        }
    for (const Label& l : labels) {
        const Col fill = l.mark == mark_truthful ? hex(0x1E8A3E, .92f) : l.mark == mark_liar ? hex(0xB01E26, .92f) : hex(0x10180E, .7f);
        const bool lit = hover_bird_ == l.who || popup_.who == l.who || target_ == l.who;
        frame_.begin(); frame_.rrect(l.x, l.y, l.w, 11, 4); frame_.fill(fill);
        if (lit) { frame_.begin(); frame_.rrect(l.x + .5, l.y + .5, l.w - 1, 10, 4); frame_.stroke(kGilt, 1); }
        text(l.text, l.x + 4, l.y, kPaper, size, 1);
    }
}

void TableView::draw_popup() {
    if (popup_.who < 0 || popup_.who >= puz_.birds) return;
    int x, y, w, h, th, oy;
    popup_box(x, y, w, h, th, oy);
    double hx, hy;
    aviary_.to_screen(bird_head(st_.birds[static_cast<size_t>(popup_.who)]) + V3{0, 0, .2}, hx, hy);
    const Col fill = hex(0xFFFDF4, .97f);
    // the tail toward the bird, from whichever side faces it
    frame_.begin();
    if (hx > x + w) { const double ty = std::clamp(hy, y + 10.0, y + h - 10.0); frame_.move(x + w - 1, ty - 5); frame_.line(hx - 4, hy); frame_.line(x + w - 1, ty + 5); }
    else if (hx < x) { const double ty = std::clamp(hy, y + 10.0, y + h - 10.0); frame_.move(x + 1, ty - 5); frame_.line(hx + 4, hy); frame_.line(x + 1, ty + 5); }
    else if (hy < y) { const double tx = std::clamp(hx, x + 10.0, x + w - 10.0); frame_.move(tx - 5, y + 1); frame_.line(hx, hy + 4); frame_.line(tx + 5, y + 1); }
    else { const double tx = std::clamp(hx, x + 10.0, x + w - 10.0); frame_.move(tx - 5, y + h - 1); frame_.line(hx, std::min(hy - 2, y + h + 14.0)); frame_.line(tx + 5, y + h - 1); }
    frame_.close();
    frame_.fill(fill);
    frame_.fill_rect(x + 2, y + 3, w, h, hex(0x000000, .25f));
    frame_.begin(); frame_.rrect(x, y, w, h, 6); frame_.fill(fill);
    frame_.begin(); frame_.rrect(x + .5, y + .5, w - 1, h - 1, 6); frame_.stroke(alpha(kInk, .7f), 1);
    const std::string head = names_[static_cast<size_t>(popup_.who)] + ", the " + species_name(species_[static_cast<size_t>(popup_.who)]);
    text(head, x + 8, y + 5, kGreen, 9.5, 1);
    const std::string s = popup_.text.substr(0, static_cast<size_t>(std::max(1, popup_.shown)));
    text(s, x + 8, y + 18, kInk, 11, 0, w - 16);
    static_cast<void>(th);
    static_cast<void>(oy);
}

void TableView::draw_panel() {
    if (panel_ == Panel::none) return;
    frame_.fill_rect(0, 0, pw_, ph_, hex(0x000000, .45f));
    int wx, wy, ww, wh;
    panel_box(wx, wy, ww, wh);
    frame_.fill_rect(wx, wy, ww, wh, kPaper);
    frame_.begin(); frame_.rect(wx + .5, wy + .5, ww - 1, wh - 1); frame_.stroke(kGilt, 1.5);
    int ly = wy + 30;
    const std::string title = panel_ == Panel::help ? "The Paradoxiary" : "Records";
    text(title, wx + (ww - text_w(title, 15, 2)) / 2, wy + 8, kInk, 15, 2);
    const std::vector<std::string> lines = panel_lines();
    for (size_t i = 0; i < lines.size(); ++i) {
        const bool keys = panel_ == Panel::help && i + 1 == lines.size();
        const double size = keys ? ov_size_ - 1 : ov_size_;
        text(lines[i], wx + 14, ly, keys ? hex(0x7A6A50) : kInk, size, keys ? 1 : 0, ww - 28);
        ly += text_h(lines[i], size, keys ? 1 : 0, ww - 28) + 4;
    }
}

// The JE ACCUSE! word: big comic letters along an arch on a jagged white burst, bottom right.
void TableView::jaccuse_box(int& x, int& y, int& w, int& h) const {
    w = std::min(compact_ ? 150 : 190, pw_ / 2);
    h = w * 32 / 100;
    x = pw_ - w - 6;
    y = ph_ - h - 6;
}

void TableView::draw_jaccuse() {
    if (phase_ != Phase::play && phase_ != Phase::aim) return;
    int x, y, w, h;
    jaccuse_box(x, y, w, h);
    const bool armed = phase_ == Phase::aim, over = hover_ == "jaccuse", down = pressed_ == "jaccuse" || armed;
    const int dy = down ? 2 : 0;
    // the burst: spikes round an ellipse, a black edge and a drop shadow
    const double cx = x + w / 2.0, cy = y + h / 2.0 + dy, rx = w / 2.0, ry = h / 2.0;
    auto burst = [&](double ox, double oy, double grow) {
        frame_.begin();
        const int spikes = 18;
        for (int k = 0; k <= spikes * 2; ++k) {
            const double a = 3.14159265 * k / spikes, r = k % 2 ? .78 : 1.0;
            const double jitter = 1 + .06 * std::sin(k * 2.7);
            const double px = cx + ox + std::cos(a) * rx * r * jitter * grow, py = cy + oy + std::sin(a) * ry * r * jitter * grow;
            if (k == 0) frame_.move(px, py);
            else frame_.line(px, py);
        }
        frame_.close();
    };
    burst(3, 4, 1);
    frame_.fill(hex(0x000000, .35f));
    burst(0, 0, 1.06);
    frame_.fill(hex(0x101010));
    burst(0, 0, 1);
    frame_.fill(armed ? hex(0xFFF2A0) : over ? hex(0xFFFDF0) : hex(0xF4F0E4));
    // the letters, along an arch, each outlined in black
    const std::string word = "JE ACCUSE!";
    const double size = h * .62;
    std::vector<int> widths;
    int total = 0;
    for (char c : word) {
        const int cw = c == ' ' ? static_cast<int>(size * .3) : text_w(std::string(1, c), size, 2);
        widths.push_back(cw);
        total += cw;
    }
    const int lh = text_h("J", size, 2);
    int lx = static_cast<int>(cx) - total / 2;
    const Col fill = armed ? hex(0xFF3A2A) : over ? hex(0xF0302A) : hex(0xD8201C);
    for (size_t k = 0; k < word.size(); ++k) {
        const double u = (lx + widths[k] / 2.0 - cx) / (total / 2.0);  // -1 .. 1 across the word
        const int ly = static_cast<int>(cy - lh / 2.0 + u * u * h * .16 - h * .04);
        if (word[k] != ' ') {
            const std::string ch(1, word[k]);
            for (int ox = -1; ox <= 1; ++ox)
                for (int oy = -1; oy <= 2; ++oy)
                    if (ox || oy) text(ch, lx + ox, ly + oy, hex(0x101010), size, 2);
            text(ch, lx, ly, fill, size, 2);
        }
        lx += widths[k];
    }
}

// The paintball on its way, the splat on the bird, and the crosshair while aiming.
void TableView::draw_shot() {
    if (phase_ == Phase::aim && hover_.empty()) {
        const double mx = mouse_x_, my = mouse_y_;
        const Col red = hex(0xE82020);
        frame_.begin(); frame_.circle(mx, my, 7); frame_.stroke(red, 1.4);
        frame_.begin(); frame_.circle(mx, my, 1.2); frame_.fill(red);
        for (int s = -1; s <= 1; s += 2) {
            frame_.fill_rect(static_cast<int>(mx) + s * 5 + (s < 0 ? -6 : 0), static_cast<int>(my), 6, 1, red);
            frame_.fill_rect(static_cast<int>(mx), static_cast<int>(my) + s * 5 + (s < 0 ? -6 : 0), 1, 6, red);
        }
    }
    if (target_ < 0 || target_ >= static_cast<int>(st_.birds.size())) return;
    const BirdPose& b = st_.birds[static_cast<size_t>(target_)];
    if (b.fly > .5) return;
    double tx, ty;
    const double c = std::cos(b.yaw), sn = std::sin(b.yaw);
    const V3 local{splat_.x * b.size, splat_.y * b.size, splat_.z * b.size};
    aviary_.to_screen(b.pos + V3{local.x * c - local.y * sn, local.x * sn + local.y * c, local.z}, tx, ty);
    if (phase_ == Phase::shot && !splatted_) {
        // from the word, in an arc, getting smaller as it goes
        int jx, jy, jw, jh;
        jaccuse_box(jx, jy, jw, jh);
        const double u = std::clamp(shot_t_ / .35, 0.0, 1.0);
        const double sx = jx + jw * .5, sy = jy + jh * .3;
        const double bx = sx + (tx - sx) * u, by = sy + (ty - sy) * u - std::sin(u * 3.14159) * 30;
        const double r = 6 - 3.5 * u;
        frame_.begin(); frame_.circle(bx + 1, by + 2, r); frame_.fill(hex(0x000000, .3f));
        frame_.begin(); frame_.circle(bx, by, r); frame_.fill(hex(0xD81818));
        frame_.begin(); frame_.circle(bx - r * .35, by - r * .35, r * .35); frame_.fill(hex(0xFF9090));
        return;
    }
    if (!splatted_) return;
    // the splat: a blot with drips and spatter
    double hx, hy;
    aviary_.to_screen(bird_head(b), hx, hy);
    const double size = std::max(3.0, std::hypot(hx - tx, hy - ty) * .45);
    const Col paint = hex(0xD01818, .95f);
    frame_.begin(); frame_.circle(tx, ty, size); frame_.fill(paint);
    for (int k = 0; k < 9; ++k) {
        const double a = k * 2.399 + target_, d = size * (1.1 + .5 * std::fmod(k * .37, 1.0));
        frame_.begin(); frame_.circle(tx + std::cos(a) * d, ty + std::sin(a) * d * .8, size * (.18 + .12 * std::fmod(k * .61, 1.0))); frame_.fill(paint);
    }
    for (int k = 0; k < 3; ++k) frame_.fill_rect(static_cast<int>(tx - size * .5 + k * size * .45), static_cast<int>(ty), std::max(1, static_cast<int>(size * .18)), static_cast<int>(size * (1.0 + .5 * k)), paint);
    frame_.begin(); frame_.circle(tx - size * .3, ty - size * .3, size * .25); frame_.fill(hex(0xFF7070, .8f));
}

void TableView::compose() {
    texts_.clear();
    aviary_.r.present(frame_, 1, 0, 0, true);
    if (panel_ != Panel::none) {
        draw_panel();
        for (const Button& b : buttons_) draw_button(b);
        return;
    }
    draw_labels();
    draw_top();
    // the keeper's notice, while the birds come in and a moment after
    if (phase_ == Phase::arrive || (phase_ == Phase::play && phase_t_ < 8 && popup_.who < 0 && std::all_of(heard_.begin(), heard_.end(), [](int h) { return h == 0; }))) {
        const int w = std::min(pw_ - 16, 360), th = text_h(opener_, 11.5, 0, w - 20);
        const int x = (pw_ - w) / 2, y = ph_ - 30 - th - 12;
        frame_.begin(); frame_.rrect(x, y, w, th + 12, 6); frame_.fill(alpha(kBand, .9f));
        frame_.begin(); frame_.rrect(x + .5, y + .5, w - 1, th + 11, 6); frame_.stroke(kGilt, 1);
        text(opener_, x + 10, y + 6, kPaper, 11.5, 0, w - 20);
    }
    if (phase_ == Phase::aim || phase_ == Phase::verdict) {
        std::string s = "Aim at the bird that did it, and click.";
        if (phase_ == Phase::verdict) {
            const std::string who = names_[static_cast<size_t>(puz_.culprit)];
            s = (won_ ? "Case closed! " + who + " " + crime(crime_).did + ". +10" : "Wrong bird! It was " + who + " all along.") + "   Liars spotted: " +
                std::to_string(liars_right_) + " of " + std::to_string(puz_.liar_count) + (liars_right_ > 0 ? " (+" + std::to_string(5 * liars_right_) + ")" : "") +
                "   " + std::to_string(table_points_) + (table_points_ == 1 ? " point" : " points");
        }
        const double size = phase_ == Phase::verdict ? 12.5 : 11.5;
        const int w = std::min(pw_ - 8, text_w(s, size, 1) + 20), th = text_h(s, size, 1, w - 20) + 4;
        const Col bg = phase_ == Phase::aim ? hex(0x6A1E22, .9f) : won_ ? hex(0x1E5A30, .92f) : hex(0x6A1E22, .92f);
        frame_.fill_rect((pw_ - w) / 2, top_h_ + 6, w, th, bg);
        text(s, (pw_ - w) / 2 + 10, top_h_ + 8, kPaper, size, 1, w - 20);
    }
    draw_shot();
    if (phase_ == Phase::play && popup_.who < 0) {
        const std::string hint = "Click a bird to listen.  Right-click: truthful, liar, unmarked.";
        const int w = text_w(hint, 9.5, 0) + 12, y = ph_ - 18 - (opt_.hosted ? 0 : 22);
        frame_.begin(); frame_.rrect(6, y, w, 13, 5); frame_.fill(alpha(kBand, .75f));
        text(hint, 12, y + 1, kPaper, 9.5, 0);
    }
    draw_popup();
    for (const Button& b : buttons_) draw_button(b);
}

std::vector<render::PixelSurface::Text> TableView::text_marks(double k) const {
    const int sc = std::max(1, static_cast<int>(std::lround(bs_)));
    std::vector<render::PixelSurface::Text> marks;
    marks.reserve(texts_.size());
    for (const HiText& h : texts_) {
        const Mask& m = tmask(h.s, h.font, h.size, h.wrap);
        const int ox = static_cast<int>(h.x * k), oy = static_cast<int>(h.y * k);
        std::uint64_t key = std::hash<std::string>{}(h.s);
        const float parts[] = {static_cast<float>(h.font), static_cast<float>(h.size), static_cast<float>(h.wrap), h.c.r, h.c.g, h.c.b, h.c.a};
        for (float part : parts) key = (key ^ std::hash<float>{}(part)) * 1099511628211ULL;
        marks.push_back({key, render::Rect{ox, oy, ox + m.w * sc, oy + m.h * sc}});
    }
    return marks;
}

void TableView::blit_texts(const render::Target& target, render::Rect clip, double k) {
    const int sc = std::max(1, static_cast<int>(std::lround(bs_)));
    for (const HiText& h : texts_)
        render::r2d::blit_text(target, clip, tmask(h.s, h.font, h.size, h.wrap), static_cast<int>(h.x * k), static_cast<int>(h.y * k), sc, h.c);
}

void TableView::set_cabinet(bool foreground, bool music, bool sound, bool reduced) {
    if (timer_) {
        if (foreground) {
            last_ = std::chrono::steady_clock::now();
            (*timer_).start();
        } else {
            (*timer_).stop();
        }
    }
    cab_front_ = foreground;
    cab_music_ = music;
    cab_sound_ = sound;
    cab_reduced_ = reduced;
    audio_cabinet(foreground, music, sound);
    if (!foreground) pressed_.clear();
    if (reduced && phase_ == Phase::arrive) settle_now();
}

std::vector<games::GameCommand> TableView::commands() const {
    return {{"newtable", "New table", true, false}, {"help", "Help", true, panel_ == Panel::help}, {"records", "Records", true, panel_ == Panel::records}};
}
void TableView::run_command(std::string_view id) {
    for (const games::GameCommand& cmd : commands())
        if (cmd.id == id && cmd.enabled) { action(cmd.checked ? "close" : std::string(id)); return; }
}

}  // namespace pt
