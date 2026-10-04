#include "help_route.hpp"
#include "runtime_paths.hpp"
#include "table_view.hpp"

#include "platform/audio.hpp"
#include "platform/text.hpp"

#include "gui_forms/surface_material.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace pt {

namespace {
const Col kInk = hex(0x2A1E16), kPaper = hex(0xF6EEDC), kPaperDark = hex(0xE6D8BA), kGilt = hex(0xC8A048), kGreen = hex(0x2E8A48), kRed = hex(0xC03038);
const char* kNames[] = {"Ada", "Bram", "Cleo", "Dot", "Ezra", "Fig", "Gus", "Hattie", "Iggy", "Juno", "Kip", "Lulu", "Mango", "Nell", "Otto", "Pip", "Queenie", "Rollo", "Sal", "Tiko"};
constexpr int kNameCount = 20;

std::uint64_t seed_now() { return static_cast<std::uint64_t>(wall_clock() * 1000) ^ 0x9A770CULL; }

std::filesystem::path save_path(bool dev) {
    const char* name = dev ? "parrots_table-dev-v1.txt" : "parrots_table-v1.txt";
    if (const char* dir = std::getenv("GAMES_STATE_DIR")) return std::filesystem::path(dir) / name;
    return games::state_directory() / name;
}
std::uint64_t fnv(const std::string& s) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}
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
    set_accessible_name("The Parrot's Table. Each parrot is honest or a liar; you are told how many lie. Read what they say, mark beaks honest or lying, ask the silent ones a question, and name the culprit.");
    if (!load()) new_table(1, seed_now(), true);
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
    // a narrow window: the host's words take two lines, and the notebook gets more of the height
    compact_ = pw_ < 460 || ph_ < 300;
    top_h_ = compact_ ? 34 : 24;
    panel_h_ = compact_ ? std::max(84, (ph_ - top_h_) * 45 / 100) : std::max(96, ph_ / 4 + 8);
    parlor_.resize(pw_, ph_, panel_h_, top_h_);
    parlor_.seat(puz_.parrots, st_);
    for (int i = 0; i < puz_.parrots && i < static_cast<int>(species_.size()); ++i) {
        st_.birds[static_cast<size_t>(i)].species = species_[static_cast<size_t>(i)];
        st_.birds[static_cast<size_t>(i)].manner = manners_[static_cast<size_t>(i)];
    }
    bs_ = attached_window() ? (*attached_window()).scale() : 1.0;
    phys_w_ = std::max(1, static_cast<int>(std::lround(bounds.width * bs_)));
    phys_h_ = std::max(1, static_cast<int>(std::lround(bounds.height * bs_)));
    xmap_.clear();
    if (surface_) {
        gf::LiveSurfaceDescription d;
        d.width = static_cast<std::uint32_t>(phys_w_);
        d.height = static_cast<std::uint32_t>(phys_h_);
        static_cast<void>(surface_->reconfigure(d));
    }
    layout_buttons();
}

void TableView::on_paint(gf::Painter& p, gf::Rect) {
    const gf::Rect b = client_rectangle();
    if (surface_) p.draw_live_surface(surface_, b);
    else p.fill_rect(b, gf::Color::rgba(42, 30, 22));
}

// ------------------------------------------------------------------ the table
void TableView::new_table(int level, std::uint64_t seed, bool fresh) {
    level_ = std::max(1, level);
    seed_ = seed;
    // the table grows, the statements get subtler, and later some birds keep quiet
    GenParams g;
    g.parrots = 4 + std::min(3, (level_ - 1) / 3);
    g.tier = std::min(3, (level_ - 1) / 2);
    g.silent = level_ < 4 ? 0 : level_ < 7 ? (level_ % 2 == 0) : level_ % 4 == 3 ? 0 : (level_ >= 10 && level_ % 3 == 0 ? 2 : 1);
    g.asks = g.silent >= 2 ? 2 : g.silent;
    g.twins = level_ >= 3 && level_ % 2 == 1;
    // a few candidates; keep the one whose difficulty suits the level
    const int target = 28 + level_ * 4;
    Puzzle best;
    int bd = 1 << 30;
    for (int k = 0; k < 6; ++k) {
        g.seed = seed * 31 + static_cast<std::uint64_t>(k);
        const Puzzle p = generate(g);
        if (std::abs(p.difficulty - target) < bd) { bd = std::abs(p.difficulty - target); best = p; }
    }
    puz_ = best;
    // a cast for it: names, manners, species, a crime
    std::uint64_t h = seed ^ 0xB1BDULL;
    auto nxt = [&](int n) { h ^= h << 13; h ^= h >> 7; h ^= h << 17; return static_cast<int>(h % static_cast<std::uint64_t>(n)); };
    names_.clear();
    manners_.clear();
    species_.clear();
    std::vector<int> nm(kNameCount), sp(kSpecies), mn(kManners);
    for (int i = 0; i < kNameCount; ++i) nm[static_cast<size_t>(i)] = i;
    for (int i = 0; i < kSpecies; ++i) sp[static_cast<size_t>(i)] = i;
    for (int i = 0; i < kManners; ++i) mn[static_cast<size_t>(i)] = i;
    for (int i = kNameCount - 1; i > 0; --i) std::swap(nm[static_cast<size_t>(i)], nm[static_cast<size_t>(nxt(i + 1))]);
    for (int i = kSpecies - 1; i > 0; --i) std::swap(sp[static_cast<size_t>(i)], sp[static_cast<size_t>(nxt(i + 1))]);
    for (int i = kManners - 1; i > 0; --i) std::swap(mn[static_cast<size_t>(i)], mn[static_cast<size_t>(nxt(i + 1))]);
    std::vector<std::string> sorted;
    for (int i = 0; i < puz_.parrots; ++i) sorted.push_back(kNames[nm[static_cast<size_t>(i)]]);
    std::sort(sorted.begin(), sorted.end());  // seated in alphabetical order, left to right: easy to find
    for (int i = 0; i < puz_.parrots; ++i) {
        names_.push_back(sorted[static_cast<size_t>(i)]);
        species_.push_back(sp[static_cast<size_t>(i % kSpecies)]);
        manners_.push_back(static_cast<Manner>(mn[static_cast<size_t>(i % kManners)]));
    }
    if (puz_.twins_a >= 0) species_[static_cast<size_t>(puz_.twins_b)] = species_[static_cast<size_t>(puz_.twins_a)];  // the twins look alike
    crime_ = nxt(crime_count());
    script_ = Script(seed ^ 0x5C21ULL);
    script_.cast(names_, manners_, puz_.twins_a, puz_.twins_b, crime_);
    opener_ = script_.opener(puz_.liar_count, puz_.parrots);
    offer_text_.clear();
    for (const auto& offer : puz_.offers) {
        offer_text_.emplace_back();
        for (const Question& q : offer) offer_text_.back().push_back(script_.question(q));
    }
    notebook_.clear();
    nb_first_ = 0;
    nb_follow_ = true;
    for (size_t i = 0; i < puz_.said.size(); ++i) notebook_.push_back({puz_.said[i].speaker, script_.statement(puz_.said[i]), false, static_cast<int>(i)});
    asked_.clear();
    marks_.assign(static_cast<size_t>(puz_.parrots), 0);
    talk_.assign(static_cast<size_t>(puz_.parrots), 0);
    shake_.assign(static_cast<size_t>(puz_.parrots), 0);
    glow_.assign(static_cast<size_t>(puz_.parrots), 0);
    suspect_ = -1;
    won_ = false;
    bubbles_.clear();
    st_ = ParlorState{};
    st_.prop = crime(crime_).prop;
    if (pw_ > 0) parlor_.seat(puz_.parrots, st_);
    else st_.birds.resize(static_cast<size_t>(puz_.parrots));
    for (int i = 0; i < puz_.parrots; ++i) {
        BirdPose& b = st_.birds[static_cast<size_t>(i)];
        b.species = species_[static_cast<size_t>(i)];
        b.manner = manners_[static_cast<size_t>(i)];
        b.tight_beaked = std::find(puz_.silent.begin(), puz_.silent.end(), i) != puz_.silent.end();
    }
    phase_ = Phase::intro;
    phase_t_ = 0;
    intro_next_ = -1;
    if (fresh) { play("pt_bell", .7f); say(-1, opener_, 4.5); }
    panel_ = Panel::none;
    dirty_ = true;
    layout_buttons();
}

void TableView::say(int who, const std::string& text, double life) {
    if (who >= 0 && who < static_cast<int>(talk_.size())) talk_[static_cast<size_t>(who)] = text.size() / 32.0 + .3;
    bubbles_.push_back({who, text, 0, life > 0 ? life : 1.7 + text.size() / 34.0, 0});
    while (bubbles_.size() > 3) bubbles_.pop_front();
}

void TableView::mark(int bird) {
    if (phase_ != Phase::play || bird < 0 || bird >= puz_.parrots) return;
    const std::vector<bool> before = contradictions();
    int& m = marks_[static_cast<size_t>(bird)];
    m = (m + 1) % 3;
    glow_[static_cast<size_t>(bird)] = 1;
    play(m == 1 ? "pt_mark_honest" : m == 2 ? "pt_mark_liar" : "pt_unmark", .7f, .95f + .1f * static_cast<float>(rand01()));
    // a contradiction that has just appeared: its speaker shakes their head
    const std::vector<bool> after = contradictions();
    for (size_t i = 0; i < after.size() && i < before.size(); ++i)
        if (after[i] && !before[i]) {
            shake_[static_cast<size_t>(notebook_[i].who)] = 1;
            play("pt_contradiction", .6f);
        }
    dirty_ = true;
}

void TableView::ask(int bird, int option) {
    if (phase_ != Phase::play || static_cast<int>(asked_.size()) >= puz_.asks) return;
    const auto it = std::find(puz_.silent.begin(), puz_.silent.end(), bird);
    if (it == puz_.silent.end()) return;
    const auto& offer = puz_.offers[static_cast<size_t>(it - puz_.silent.begin())];
    if (option < 0 || option >= static_cast<int>(offer.size())) return;
    const Question& q = offer[static_cast<size_t>(option)];
    for (const Asked& a : asked_)
        if (a.q.to == q.to && a.q.f.kind == q.f.kind && a.q.f.a == q.f.a && a.q.f.b == q.f.b && a.q.f.n == q.f.n) return;
    const bool yes = answer(q, puz_.truth, puz_.parrots);
    asked_.push_back({q, yes});
    const std::string& qtext = offer_text_[static_cast<size_t>(it - puz_.silent.begin())][static_cast<size_t>(option)];
    const std::string rtext = script_.reply(bird, yes);
    nb_follow_ = true;
    notebook_.push_back({bird, "You asked: \"" + qtext + "\"  " + (yes ? "YES" : "NO") + ": \"" + rtext + "\"", true, static_cast<int>(asked_.size()) - 1});
    st_.birds[static_cast<size_t>(bird)].tight_beaked = false;
    say(bird, rtext);
    play("pt_ask", .6f);
    open(Panel::none);
    dirty_ = true;
}

void TableView::accuse(int bird) {
    if (phase_ != Phase::play || bird < 0 || bird >= puz_.parrots) return;
    phase_ = Phase::verdict;
    phase_t_ = 0;
    won_ = bird == puz_.truth.culprit;
    ++played_;
    if (won_) {
        ++solved_;
        ++streak_;
        best_streak_ = std::max(best_streak_, streak_);
        say(bird, script_.guilty(bird));
        play("pt_gavel", .9f);
        play("pt_stinger_win", .85f);
    } else {
        streak_ = 0;
        say(bird, script_.wrongly(bird));
        play("pt_gavel", .9f);
        play("pt_stinger_lose", .85f);
    }
    // the truth, shown on every beak
    for (int i = 0; i < puz_.parrots; ++i) marks_[static_cast<size_t>(i)] = puz_.truth.honest(i) ? 1 : 2;
    suspect_ = -1;
    open(Panel::none);
    persist();
}

std::vector<bool> TableView::contradictions() const {
    // The worlds the player still allows: the right number of liars, agreeing with every mark (and the suspect, if
    // one is picked). A line is in contradiction when none of them makes it hold, or when it clashes with another
    // line: no allowed world makes both hold. Cached; it only changes with the marks, the suspect and the answers.
    std::string key = std::to_string(suspect_) + "/" + std::to_string(asked_.size()) + "/" + std::to_string(notebook_.size()) + "/";
    for (int m : marks_) key += static_cast<char>('0' + m);
    if (key == contra_key_) return contra_;
    const int n = puz_.parrots;
    std::vector<World> worlds;
    for (unsigned m = 0; m < (1u << n); ++m) {
        if (std::popcount(m) != puz_.liar_count) continue;
        bool fits = true;
        for (int i = 0; i < n && fits; ++i) {
            const bool liar = m >> i & 1;
            if ((marks_[static_cast<size_t>(i)] == 1 && liar) || (marks_[static_cast<size_t>(i)] == 2 && !liar)) fits = false;
        }
        if (!fits) continue;
        for (int c = 0; c < n; ++c)
            if (suspect_ < 0 || c == suspect_) worlds.push_back({static_cast<std::uint8_t>(m), c});
    }
    const size_t L = notebook_.size(), W = worlds.size();
    std::vector<std::vector<bool>> holds(L, std::vector<bool>(W));
    for (size_t li = 0; li < L; ++li)
        for (size_t k = 0; k < W; ++k) {
            const World& w = worlds[k];
            const Line& ln = notebook_[li];
            if (ln.answer) {
                const Asked& a = asked_[static_cast<size_t>(ln.index)];
                holds[li][k] = answer(a.q, w, n) == a.yes;
            } else {
                const Statement& st = puz_.said[static_cast<size_t>(ln.index)];
                holds[li][k] = st.f.eval(w, n) == w.honest(st.speaker);
            }
        }
    std::vector<bool> out(L, false);
    for (size_t i = 0; i < L; ++i) {
        if (std::find(holds[i].begin(), holds[i].end(), true) == holds[i].end()) { out[i] = true; continue; }
        for (size_t j = i + 1; j < L; ++j) {
            bool both = false;
            for (size_t k = 0; k < W && !both; ++k) both = holds[i][k] && holds[j][k];
            if (!both) out[i] = out[j] = true;
        }
    }
    contra_key_ = key;
    contra_ = out;
    return out;
}

bool TableView::count_wrong() const {
    int liars = 0, honest = 0;
    for (int m : marks_) { liars += m == 2; honest += m == 1; }
    return liars > puz_.liar_count || honest > puz_.parrots - puz_.liar_count;
}

// ------------------------------------------------------------------ saving
void TableView::persist() {
    std::ostringstream o;
    o << "level=" << level_ << "\nseed=" << seed_ << "\nphase=" << (phase_ == Phase::verdict ? 2 : 1) << "\nwon=" << won_ << "\nmarks=";
    for (int m : marks_) o << m;
    o << "\nasked=";
    for (const Asked& a : asked_) {
        const auto it = std::find(puz_.silent.begin(), puz_.silent.end(), a.q.to);
        const size_t si = static_cast<size_t>(it - puz_.silent.begin());
        int opt = 0;
        for (size_t k = 0; k < puz_.offers[si].size(); ++k)
            if (puz_.offers[si][k].f.kind == a.q.f.kind && puz_.offers[si][k].f.a == a.q.f.a && puz_.offers[si][k].f.b == a.q.f.b && puz_.offers[si][k].f.n == a.q.f.n) opt = static_cast<int>(k);
        o << a.q.to << ":" << opt << ";";
    }
    o << "\nsolved=" << solved_ << "\nplayed=" << played_ << "\nstreak=" << streak_ << "\nbest=" << best_streak_ << "\nsound=" << sound_ << "\nmusic=" << music_ << "\n";
    std::string body = o.str();
    body = "PARROTS1\n" + body + "check=" + std::to_string(fnv(body)) + "\n";
    const std::filesystem::path p = save_path(opt_.dev);
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
    std::ifstream f(save_path(opt_.dev), std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string all = ss.str();
    if (all.size() > 16384 || all.rfind("PARROTS1\n", 0) != 0) return false;
    const size_t ck = all.rfind("check=");
    if (ck == std::string::npos || ck < 9) return false;
    const std::string body = all.substr(9, ck - 9);
    if (std::to_string(fnv(body)) + "\n" != all.substr(ck + 6)) return false;
    int level = 1, phase = 1, won = 0;
    std::uint64_t seed = 1;
    std::string marks, asked;
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
            else if (k == "asked") asked = v;
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
    // the questions asked, in order, then the marks
    std::istringstream as(asked);
    std::string item;
    phase_ = Phase::play;
    while (std::getline(as, item, ';')) {
        const size_t c = item.find(':');
        if (c == std::string::npos) continue;
        ask(std::atoi(item.c_str()), std::atoi(item.c_str() + c + 1));
    }
    bubbles_.clear();
    for (size_t i = 0; i < marks.size() && i < marks_.size(); ++i) marks_[i] = std::clamp(marks[i] - '0', 0, 2);
    if (phase == 2) { phase_ = Phase::verdict; won_ = won != 0; phase_t_ = 10; for (int i = 0; i < puz_.parrots; ++i) marks_[static_cast<size_t>(i)] = puz_.truth.honest(i) ? 1 : 2; }
    else { phase_ = Phase::play; say(-1, "Welcome back. The table is as you left it.", 3); }
    layout_buttons();
    return true;
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
        if (c == "skip") { if (phase_ == Phase::intro) { phase_ = Phase::play; bubbles_.clear(); layout_buttons(); } }
        else if (c[0] == 'm') mark(std::atoi(c.c_str() + 1));
        else if (c == "truth") { for (int i = 0; i < puz_.parrots; ++i) marks_[static_cast<size_t>(i)] = puz_.truth.honest(i) ? 1 : 2; }
        else if (c == "askgood") {
            for (size_t si = 0; si < puz_.silent.size(); ++si)
                for (size_t k = 0; k < puz_.offers[si].size(); ++k)
                    if (settles(puz_, asked_, puz_.offers[si][k]) && static_cast<int>(asked_.size()) < puz_.asks) { ask(puz_.silent[si], static_cast<int>(k)); return; }
        }
        else if (c == "askpanel") open(Panel::ask);
        else if (c == "accusepanel") open(Panel::accuse);
        else if (c[0] == 's') { suspect_ = std::atoi(c.c_str() + 1); }
        else if (c == "right") accuse(puz_.truth.culprit);
        else if (c == "wrong") accuse((puz_.truth.culprit + 1) % puz_.parrots);
        else if (c == "next") action("next");
        else if (c.rfind("lvl", 0) == 0) new_table(std::atoi(c.c_str() + 3), seed_now(), true);
        else if (c == "help") open(Panel::help);
        else if (c == "records") open(Panel::records);
        else if (c == "close") action("close");
    }
}

void TableView::tick() {
    if (!visible() || !cab_front_) { last_ = std::chrono::steady_clock::now(); return; }
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::clamp(std::chrono::duration<double>(now - last_).count(), 0.0, .25);
    last_ = now;
    t_ += dt;
    phase_t_ += dt;
    run_script();
    // the introduction: each bird that speaks says its piece in turn
    if (phase_ == Phase::intro) {
        const bool idle = bubbles_.empty() || bubbles_.back().age > bubbles_.back().life - .2;
        if (idle) {
            ++intro_next_;
            if (intro_next_ < static_cast<int>(puz_.said.size())) say(notebook_[static_cast<size_t>(intro_next_)].who, notebook_[static_cast<size_t>(intro_next_)].text);
            else {
                // the quiet ones, if any, make it plain they won't volunteer
                for (int s : puz_.silent) say(s, script_.refuse(s), 3);
                phase_ = Phase::play;
                layout_buttons();
            }
        }
    }
    // bubbles type out with a squawky babble
    for (Bubble& b : bubbles_) {
        const int before = b.shown;
        b.age += dt;
        b.shown = std::min(static_cast<int>(b.text.size()), static_cast<int>(b.age * 34));
        if (b.who >= 0 && b.shown / 3 != before / 3 && b.shown < static_cast<int>(b.text.size()) && b.text[static_cast<size_t>(b.shown)] != ' ') {
            const int sp = species_[static_cast<size_t>(b.who)];
            const float pitch = (sp == 6 || sp == 7) ? 1.35f : (sp <= 2 ? .85f : 1.05f);
            play("pt_voice_0" + std::to_string(1 + static_cast<int>(rand01() * 6)), .35f, pitch * (.94f + .12f * static_cast<float>(rand01())));
        }
    }
    while (!bubbles_.empty() && bubbles_.front().age > bubbles_.front().life) bubbles_.pop_front();
    // the birds' performance
    const int speaking = !bubbles_.empty() ? bubbles_.back().who : -1;
    for (int i = 0; i < puz_.parrots && i < static_cast<int>(st_.birds.size()); ++i) {
        BirdPose& b = st_.birds[static_cast<size_t>(i)];
        double& tk = talk_[static_cast<size_t>(i)];
        tk = std::max(0.0, tk - dt);
        b.beak = tk > 0 ? .5 + .5 * std::sin(t_ * 22 + i) : 0;
        b.wing_r = tk > 0 && i % 2 ? .35 + .2 * std::sin(t_ * 5) : 0;
        b.wing_l = tk > 0 && !(i % 2) ? .35 + .2 * std::sin(t_ * 5) : 0;
        // heads turn to whoever is talking, otherwise a gentle look about
        const double toward = speaking >= 0 && speaking != i ? (speaking < i ? .7 : -.7) : (i < puz_.parrots / 2 ? .5 : -.5) + .15 * std::sin(t_ * .5 + i);
        b.head_turn += (toward - b.head_turn) * std::min(1.0, dt * 6);
        b.head_tilt = .12 * std::sin(t_ * .8 + i * 1.7);
        b.blink = std::fmod(t_ + i * 1.31, 3.7) < .12 ? 1 : 0;
        double& sh = shake_[static_cast<size_t>(i)];
        sh = std::max(0.0, sh - dt * 1.5);
        b.shake = sh > 0 ? 1 - sh : 0;
        if (sh <= 0) b.shake = 0;
        double& gl = glow_[static_cast<size_t>(i)];
        gl = std::max(0.0, gl - dt * 2);
        b.glow = gl;
        b.mark = marks_[static_cast<size_t>(i)];
        b.hover = phase_ == Phase::play && hover_bird_ == i && panel_ == Panel::none;
        b.ruffle = phase_ == Phase::verdict && !won_ && suspect_ < 0 && i == puz_.truth.culprit ? 0 : 0;
        b.fly = 0;
        // the verdict: the culprit caught (a guilty slump), or getting away (off out of the window)
        if (phase_ == Phase::verdict) {
            if (i == puz_.truth.culprit) {
                if (won_) { b.head_nod = .4; b.ruffle = .6; }
                else { b.fly = std::clamp((phase_t_ - 2.6) / 2.0, 0.0, 1.0); b.wing_l = b.wing_r = b.fly > 0 ? .5 + .5 * std::sin(t_ * 30) : 0; }
            } else if (won_) {
                b.bob = std::fmod(t_ * 2 + i * .3, 1.0) < .5 ? std::fmod(t_ * 2 + i * .3, 1.0) * 2 : 0;
                b.head_nod = 0;
            }
        } else {
            b.head_nod = 0;
            b.bob = 0;
        }
    }
    if (phase_ == Phase::verdict && !won_ && phase_t_ > 2.4 && phase_t_ - dt <= 2.4) { say(puz_.truth.culprit, script_.gloat(puz_.truth.culprit)); play("pt_flap", .8f); }
    if (phase_ == Phase::verdict && won_ && phase_t_ > 1.8 && phase_t_ - dt <= 1.8) {
        int k = (puz_.truth.culprit + 1) % puz_.parrots;
        say(k, script_.cheer(k));
        play("pt_cheer", .7f);
    }
    st_.confetti = phase_ == Phase::verdict && won_ ? std::min(1.0, phase_t_ / .5) : 0;
    if (cab_reduced_) {
        st_.confetti = 0;
        for (BirdPose& b : st_.birds) {
            b.beak = b.wing_l = b.wing_r = b.bob = b.shake = b.blink = b.head_tilt = 0;
            b.head_turn = 0;
            if (b.fly > 0) b.fly = 1;
        }
    }
    st_.spot = panel_ == Panel::accuse && suspect_ >= 0 ? suspect_ : -1;
    {
        const bool on = (opt_.hosted || music_) && cab_music_ && cab_front_;
        audio_music(visible() && cab_front_ ? "pt_music" : "", on);
    }
    audio_tick(dt);
    save_t_ += dt;
    if (dirty_ && save_t_ > .5) { save_t_ = 0; persist(); }
    gf::Window* win = attached_window();
    // dev captures (--dev with PT_PRINT_WID) keep drawing even when the window is covered
    static const bool capturing = opt_.dev && std::getenv("PT_PRINT_WID");
    const bool hidden = win && (*win).occluded() && !capturing;
    const bool front = !win || (*win).active();
    const bool shown = visible();
    const auto pace = std::chrono::milliseconds(hidden || !shown || !cab_front_ ? 500 : front ? 33 : 100);
    if (timer_ && (*timer_).interval() != pace) (*timer_).set_interval(pace);

    if (hidden || !shown || frame_.px.empty() || parlor_.r.rgb.empty()) return;
    parlor_.render(st_, cab_reduced_ ? 0 : t_);
    compose();
    publish();
}

void TableView::publish() {
    if (!surface_) {
        gf::LiveSurfaceDescription d;
        d.width = static_cast<std::uint32_t>(phys_w_);
        d.height = static_cast<std::uint32_t>(phys_h_);
        surface_ = gf::LiveSurface::create(d);
        if (surface_ && attached_window()) direct_ = (*attached_window()).queue_live_surface_presentation(shared_from_this(), surface_);
    }
    if (!surface_) return;
    gf::LiveSurfaceWriteLease lease = surface_->try_acquire_write();
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
    }
    if (!direct_) invalidate(gf::Dirty::paint);
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
    if (e.action == gf::PointerAction::wheel) {
        if (panel_ == Panel::none && mouse_y_ >= ph_ - panel_h_ && mouse_x_ < pw_ - 112 + 4) {
            if (e.wheel_delta.y != 0) { scroll_notebook(e.wheel_delta.y > 0 ? -1 : 1); nb_follow_ = false; }
            e.handled = true;
        }
        return;
    }
    if (e.action == gf::PointerAction::move) {
        hover_ = hit();
        hover_bird_ = hover_.empty() && mouse_y_ < ph_ - panel_h_ ? parlor_.pick_bird(mouse_x_, mouse_y_, st_) : -1;
        set_cursor(!hover_.empty() || hover_bird_ >= 0 ? gf::CursorKind::hand : gf::CursorKind::arrow);
    }
    if (e.action == gf::PointerAction::down && e.button == gf::PointerButton::primary) {
        activate();
        const std::string h = hit();
        if (!h.empty()) { pressed_ = h; e.handled = true; return; }
        if (phase_ == Phase::intro) { phase_ = Phase::play; bubbles_.clear(); layout_buttons(); e.handled = true; return; }  // impatient? skip ahead
        const int b = mouse_y_ < ph_ - panel_h_ ? parlor_.pick_bird(mouse_x_, mouse_y_, st_) : -1;
        if (b >= 0) {
            if (panel_ == Panel::accuse) { suspect_ = b; play("pt_tick", .5f); layout_buttons(); }
            else if (panel_ == Panel::none) mark(b);
        }
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
    if (k == K::escape) { if (panel_ != Panel::none) { suspect_ = -1; open(Panel::none); } e.handled = true; return; }
    if (k == K::f1) { open(panel_ == Panel::help ? Panel::none : Panel::help); e.handled = true; return; }
    if (k == K::enter || k == K::space) {
        if (phase_ == Phase::intro) { phase_ = Phase::play; bubbles_.clear(); layout_buttons(); }
        else if (phase_ == Phase::verdict) action("next");
        else if (panel_ != Panel::none) open(Panel::none);
        e.handled = true;
        return;
    }
    if (!opt_.hosted && k == K::m) { action("music"); e.handled = true; return; }
    // number keys mark the birds in order
    if (k >= 0x1E && k <= 0x24 && phase_ == Phase::play && panel_ == Panel::none) { mark(static_cast<int>(k - 0x1E)); e.handled = true; }
}

void TableView::action(const std::string& id) {
    play("pt_click", .4f);
    if (id == "close") { suspect_ = -1; open(Panel::none); }
    else if (id == "ask") open(Panel::ask);
    else if (id == "accuse") { suspect_ = -1; open(Panel::accuse); }
    else if (id == "help") open(Panel::help);
    else if (id == "records") open(Panel::records);
    else if (id == "clear") { std::fill(marks_.begin(), marks_.end(), 0); dirty_ = true; }
    else if (id == "music") { music_ = !music_; dirty_ = true; layout_buttons(); }
    else if (id == "sound") { sound_ = !sound_; dirty_ = true; layout_buttons(); }
    else if (id == "confirm") { if (suspect_ >= 0) accuse(suspect_); }
    else if (id == "nb_up") { scroll_notebook(-1); nb_follow_ = false; }
    else if (id == "nb_down") { scroll_notebook(1); nb_follow_ = false; }
    else if (id == "next") new_table(won_ ? level_ + 1 : level_, seed_now(), true);
    else if (id.rfind("q", 0) == 0) {
        const int bird = std::atoi(id.c_str() + 1);
        const size_t colon = id.find('_');
        if (colon != std::string::npos) ask(bird, std::atoi(id.c_str() + colon + 1));
    } else if (id.rfind("pick", 0) == 0) { suspect_ = std::atoi(id.c_str() + 4); layout_buttons(); }
}

void TableView::open(Panel p) {
    if (p == Panel::help && games::route_help(*this))
        return;
    panel_ = p;
    pressed_.clear();
    layout_buttons();
}

// ------------------------------------------------------------------ drawing
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

void TableView::layout_game_buttons() {
    buttons_.clear();
    if (pw_ <= 0) return;
    const int bw = 92, bh = 16, x = pw_ - bw - 8;
    int y = ph_ - panel_h_ + 8;
    auto add = [&](const std::string& id, const std::string& label, bool en = true) { buttons_.push_back({id, label, x, y, bw, bh, en}); y += bh + 4; };
    if (phase_ == Phase::verdict && panel_ == Panel::none) {
        add("next", won_ ? "Next table" : "Try another");
        add("records", "Records");
        add("help", "Help");
        return;
    }
    if (panel_ == Panel::none) {
        if (!puz_.silent.empty()) add("ask", "Ask (" + std::to_string(std::max(0, puz_.asks - static_cast<int>(asked_.size()))) + " left)", static_cast<int>(asked_.size()) < puz_.asks && phase_ == Phase::play);
        add("accuse", "Name the culprit", phase_ == Phase::play);
        add("clear", "Clear marks", phase_ == Phase::play);
        buttons_.push_back({"help", "Help", x, y, bw / 2 - 2, bh});
        buttons_.push_back({"music", music_ ? "Music" : "No music", x + bw / 2 + 2, y, bw / 2 - 2, bh});
        return;
    }
    if (panel_ == Panel::ask) {
        // the questions on offer, a button each, wrapped to the width
        const std::string head = "Ask a question (" + std::to_string(puz_.asks - static_cast<int>(asked_.size())) + " left). Choose well: some questions settle it, some don't.";
        int qy = 22 + text_h(head, 11.5, 1, pw_ - 48) + 8;
        const int qw = pw_ - 48;
        for (size_t si = 0; si < puz_.silent.size(); ++si)
            for (size_t k = 0; k < puz_.offers[si].size(); ++k) {
                const int bird = puz_.silent[si];
                const std::string& label = offer_text_[si][k];
                const int h = std::max(16, text_h(label, 11, 0, qw - 12) + 6);
                buttons_.push_back({"q" + std::to_string(bird) + "_" + std::to_string(k), label, 24, qy, qw, h});
                qy += h + 4;
            }
        buttons_.push_back({"close", "Never mind", pw_ / 2 - 46, std::min(qy + 4, ph_ - 36), 92, 16});
        return;
    }
    if (panel_ == Panel::accuse) {
        add("confirm", suspect_ >= 0 ? "Accuse " + names_[static_cast<size_t>(suspect_)] : "Pick a bird", suspect_ >= 0);
        add("close", "Not yet");
        return;
    }
    int wx, wy, ww, wh;
    panel_box(wx, wy, ww, wh);
    buttons_.push_back({"close", "Close", pw_ / 2 - 40, wy + wh + 6, 80, 16});
}

void TableView::panel_box(int& wx, int& wy, int& ww, int& wh) const {
    ww = std::min(pw_ - 16, 380);
    const int room = ph_ - 16 - 22;  // Close sits beneath
    // the words wrap to the width and step down a size until they fit
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
        return {"Every parrot is either honest, and everything they say is true, or a liar, and everything they say is false. You are told exactly how many are lying. One of them did the deed.",
                "Click a beak to mark that bird honest (green), a liar (red), or unmarked again. A statement that can't possibly hold with your marks turns red, as soon as it happens.",
                "Some birds won't volunteer anything: ask them a question. You only get one or two, and some questions settle the matter while others leave you guessing.",
                "When you're sure, name the culprit. Every table can be solved by reasoning alone; not every guess will be right.",
                "Keys: 1-7 mark a bird.  Enter continues.  Use the capsule for music and sound.  F1 help."};
    if (panel_ == Panel::records)
        return {"Tables solved: " + std::to_string(solved_) + " of " + std::to_string(played_), "Current streak: " + std::to_string(streak_),
                "Best streak: " + std::to_string(best_streak_), "Now at table " + std::to_string(level_) + "."};
    return {};
}

void TableView::scroll_notebook(int lines) {
    nb_first_ = std::clamp(nb_first_ + lines, 0, std::max(0, static_cast<int>(notebook_.size()) - 1));
}

void TableView::draw_button(const Button& b) {
    const bool down = pressed_ == b.id, over = hover_ == b.id && b.enabled;
    const Col face = !b.enabled ? hex(0xB8AA90) : over ? hex(0xFFF6E0) : kPaperDark;
    frame_.fill_rect(b.x + 1, b.y + 2, b.w, b.h, hex(0x000000, .3f));
    frame_.begin(); frame_.rrect(b.x, b.y + (down ? 1 : 0), b.w, b.h, 3); frame_.fill(face);
    frame_.begin(); frame_.rrect(b.x + .5, b.y + .5 + (down ? 1 : 0), b.w - 1, b.h - 1, 3); frame_.stroke(kGilt, 1);
    const double size = 11;
    const int tw = text_w(b.label, size, 0), th = text_h(b.label, size, 0);
    text(b.label, b.x + std::max(4, (b.w - tw) / 2), b.y + (b.h - th) / 2 + (down ? 1 : 0), b.enabled ? kInk : hex(0x7A6E5A), size, 0);
}

void TableView::draw_top() {
    // the host's card: what happened and how many lie (two lines in a narrow window)
    const int h = top_h_;
    frame_.fill_rect(0, 0, pw_, h, hex(0x1A120C, .78f));
    frame_.fill_rect(0, h, pw_, 1, kGilt);
    text("Table " + std::to_string(level_), 8, 6, kGilt, 11, 1);
    std::string s = crime(crime_).what + ".  Exactly " + std::to_string(puz_.liar_count) + (puz_.liar_count == 1 ? " liar" : " liars") + " at the table.";
    int lm = 0;
    for (int m : marks_) lm += m == 2;
    const std::string tally = (compact_ ? "Liars marked: " : "Marked liars: ") + std::to_string(lm) + " / " + std::to_string(puz_.liar_count);
    if (compact_) text(s, 8, 19, kPaper, 10.5, 0, pw_ - 16);
    else text(s, 70, 6, kPaper, 11.5, 0, pw_ - 70 - text_w(tally, 11, 1) - 20);
    text(tally, pw_ - 10 - text_w(tally, 11, 1), 6, count_wrong() ? hex(0xF07070) : kPaper, 11, 1);
    // a small table squeezes the place cards: smaller words, and "twin" goes below the card
    double gap = 1e9;
    for (int i = 0; i + 1 < puz_.parrots && i + 1 < static_cast<int>(st_.birds.size()); ++i) {
        double ax, ay, bx, by;
        parlor_.to_screen(st_.birds[static_cast<size_t>(i)].pos, ax, ay);
        parlor_.to_screen(st_.birds[static_cast<size_t>(i + 1)].pos, bx, by);
        gap = std::min(gap, std::abs(bx - ax));
    }
    int widest = 0;
    for (int i = 0; i < puz_.parrots && i < static_cast<int>(names_.size()); ++i) widest = std::max(widest, text_w(names_[static_cast<size_t>(i)] + " (twin)", 10.5, 1) + 10);
    const bool tight = widest > gap;
    const double ns = tight ? 9.5 : 10.5;
    // names under the birds, on little place cards
    for (int i = 0; i < puz_.parrots && i < static_cast<int>(st_.birds.size()); ++i) {
        double x, y;
        parlor_.to_screen(st_.birds[static_cast<size_t>(i)].pos + V3{0, -.62, .1}, x, y);
        std::string nm = names_[static_cast<size_t>(i)];
        const bool twin = i == puz_.twins_a || i == puz_.twins_b;
        if (twin && !tight) nm += " (twin)";
        const int w = text_w(nm, ns, 1) + 8;
        // tags hang below the card, or above it when the notebook leaves no room
        const int ntags = (twin && tight) + (std::find(puz_.silent.begin(), puz_.silent.end(), i) != puz_.silent.end() && phase_ != Phase::verdict);
        int tag_y = static_cast<int>(y) + 13;
        if (tag_y + ntags * 10 > ph_ - panel_h_) tag_y = static_cast<int>(y) - 1 - ntags * 10;
        if (twin && tight) {
            const int tw = text_w("twin", 8.5, 1) + 6;
            frame_.fill_rect(static_cast<int>(x) - tw / 2, tag_y, tw, 9, hex(0x2A1E16, .85f));
            text("twin", static_cast<int>(x) - tw / 2 + 3, tag_y, kPaper, 8.5, 1);
            tag_y += 10;
        }
        const int m = marks_[static_cast<size_t>(i)];
        const Col card = m == 1 ? hex(0xD8F0D8) : m == 2 ? hex(0xF8D4D4) : kPaper;
        frame_.fill_rect(static_cast<int>(x) - w / 2 + 1, static_cast<int>(y) + 1, w, 12, hex(0x000000, .3f));
        frame_.fill_rect(static_cast<int>(x) - w / 2, static_cast<int>(y), w, 12, card);
        frame_.begin(); frame_.rect(x - w / 2.0 + .5, y + .5, w - 1, 11); frame_.stroke(m == 1 ? kGreen : m == 2 ? kRed : kGilt, 1);
        text(nm, static_cast<int>(x) - w / 2 + 4, static_cast<int>(y) + 1, kInk, ns, 1);
        if (std::find(puz_.silent.begin(), puz_.silent.end(), i) != puz_.silent.end() && phase_ != Phase::verdict) {
            const bool told = std::any_of(asked_.begin(), asked_.end(), [&](const Asked& a) { return a.q.to == i; });
            if (!told) {
                const int ww = text_w("won't say", 9, 1) + 6;
                frame_.fill_rect(static_cast<int>(x) - ww / 2, tag_y, ww, 10, hex(0x2A1E16, .85f));
                text("won't say", static_cast<int>(x) - ww / 2 + 3, tag_y, kGilt, 9, 1);
            }
        }
    }
}

void TableView::draw_notebook() {
    const int y0 = ph_ - panel_h_, w = pw_ - 112;
    frame_.fill_rect(0, y0, pw_, panel_h_, hex(0x2A1E16));
    frame_.fill_rect(4, y0 + 4, w, panel_h_ - 8, kPaper);
    frame_.fill_rect(4, y0 + 4, w, 1, kGilt);
    const std::vector<bool> bad = contradictions();
    const double size = 10.5;
    // the speakers' column fits the names at this table
    nb_name_w_ = 0;
    for (const std::string& n : names_) nb_name_w_ = std::max(nb_name_w_, text_w(n + ":", size, 1));
    nb_name_w_ = std::min(nb_name_w_ + 6, 60);
    const int tx = 10 + nb_name_w_, tw = w - nb_name_w_ - 22;
    const int shown = phase_ == Phase::intro ? std::max(0, intro_next_ + 1) : static_cast<int>(notebook_.size());
    const int n = std::min(shown, static_cast<int>(notebook_.size()));
    auto quote = [&](const Line& L) { return (L.answer ? "" : "\"") + L.text + (L.answer ? "" : "\""); };
    auto height = [&](int i) { return text_h(quote(notebook_[static_cast<size_t>(i)]), size, 0, tw) + 2; };
    const int top = y0 + 7, bottom = ph_ - 6;
    // keep the newest line in view while lines are arriving; otherwise where the reader left it
    if (nb_follow_ && n > 0) {
        int first = n - 1, used = height(n - 1);
        while (first > 0 && used + height(first - 1) <= bottom - top) used += height(--first);
        nb_first_ = first;
    }
    nb_first_ = std::clamp(nb_first_, 0, std::max(0, n - 1));
    int y = top, last = nb_first_ - 1;
    for (int i = nb_first_; i < n; ++i) {
        const Line& L = notebook_[static_cast<size_t>(i)];
        const int lh = height(i);
        if (y + lh - 2 > bottom && i > nb_first_) break;
        const std::string who = names_[static_cast<size_t>(L.who)] + ":";
        const bool contra = bad[static_cast<size_t>(i)] && phase_ == Phase::play;
        if (contra) frame_.fill_rect(5, y - 1, w - 2, lh - 1, hex(0xF8C8C0));
        if (hover_bird_ == L.who && !contra) frame_.fill_rect(5, y - 1, w - 2, lh - 1, hex(0xF0E4C4));
        text(who, 10, y, contra ? kRed : kInk, size, 1);
        text(quote(L), tx, y, contra ? kRed : kInk, size, 0, tw);
        if (contra) text("!", w - 6, y, kRed, 12, 1);
        y += lh;
        last = i;
    }
    // more above or below: small arrows at the notebook's edge
    const bool up = nb_first_ > 0, down = last < n - 1;
    for (const Button& b : buttons_) if (b.id == "nb_up" || b.id == "nb_down") goto have;
    if (up || down) {
        buttons_.push_back({"nb_up", "^", w - 14, y0 + 6, 14, 12, up});
        buttons_.push_back({"nb_down", "v", w - 14, ph_ - 20, 14, 12, down});
    }
    return;
have:
    for (Button& b : buttons_) {
        if (b.id == "nb_up") b.enabled = up;
        if (b.id == "nb_down") b.enabled = down;
    }
    if (!up && !down) buttons_.erase(std::remove_if(buttons_.begin(), buttons_.end(), [](const Button& b) { return b.id == "nb_up" || b.id == "nb_down"; }), buttons_.end());
}

void TableView::draw_bubbles() {
    for (const Bubble& b : bubbles_) {
        const int wrap = std::min(150, pw_ - 30);
        const double size = 11;
        const std::string s = b.text.substr(0, static_cast<size_t>(std::max(1, b.shown)));
        const int tw = std::min(wrap, text_w(b.text, size, 0)), th = text_h(b.text, size, 0, wrap);
        const int bw = tw + 12, bh = th + 8;
        int bx, by;
        double hx = pw_ / 2.0, hy = 40;
        if (b.who >= 0) {
            parlor_.to_screen(bird_head(st_.birds[static_cast<size_t>(b.who)]) + V3{0, 0, .25}, hx, hy);
            bx = static_cast<int>(hx - bw / 2.0);
            by = static_cast<int>(hy - bh - 8);
        } else {
            bx = (pw_ - bw) / 2;
            by = top_h_ + 8;
        }
        bx = std::clamp(bx, 4, pw_ - bw - 4);
        // below the host's band, and below the verdict or the accusation prompt when one is showing
        int ceiling = top_h_ + 4;
        if (phase_ == Phase::verdict || panel_ == Panel::accuse) {
            const std::string bt = phase_ == Phase::verdict ? (won_ ? "Case closed! " + names_[static_cast<size_t>(puz_.truth.culprit)] + " " + crime(crime_).did + "."
                                                                     : "Wrong bird! It was " + names_[static_cast<size_t>(puz_.truth.culprit)] + " all along.")
                                                            : std::string("Click the bird you think did it. Lines that can't hold turn red.");
            const double bsz = phase_ == Phase::verdict ? 13 : 11.5;
            const int bw2 = std::min(pw_ - 8, text_w(bt, bsz, 1) + 20);
            ceiling = top_h_ + 6 + text_h(bt, bsz, 1, bw2 - 20) + 4 + 3;
        }
        by = std::clamp(by, ceiling, std::max(ceiling, ph_ - panel_h_ - bh - 4));
        const float a = static_cast<float>(std::clamp((b.life - b.age) * 3, 0.0, 1.0));
        const Col fill = b.who < 0 ? hex(0x2A1E16, .92f * a) : hex(0xFFFDF6, a);
        if (b.who >= 0) { frame_.begin(); frame_.move(std::clamp(hx, bx + 8.0, bx + bw - 8.0) - 5, by + bh - 1); frame_.line(hx, hy - 2); frame_.line(std::clamp(hx, bx + 8.0, bx + bw - 8.0) + 5, by + bh - 1); frame_.close(); frame_.fill(fill); }
        frame_.begin(); frame_.rrect(bx, by, bw, bh, 6); frame_.fill(fill);
        frame_.begin(); frame_.rrect(bx + .5, by + .5, bw - 1, bh - 1, 6); frame_.stroke(alpha(b.who < 0 ? kGilt : kInk, a * .8f), 1);
        text(s, bx + 6, by + 4, alpha(b.who < 0 ? kPaper : kInk, a), size, b.who < 0 ? 1 : 0, wrap);
    }
}

void TableView::draw_panel() {
    if (panel_ == Panel::none || panel_ == Panel::accuse) {
        if (panel_ == Panel::accuse) {
            const std::string s = suspect_ >= 0 ? "Accuse " + names_[static_cast<size_t>(suspect_)] + "? Lines that can't hold if so turn red." : "Click the bird you think did it.";
            const int w = std::min(pw_ - 8, text_w(s, 11.5, 1) + 16), th = text_h(s, 11.5, 1, w - 16) + 4;
            frame_.fill_rect((pw_ - w) / 2, top_h_ + 6, w, th, hex(0x2A1E16, .9f));
            text(s, (pw_ - w) / 2 + 8, top_h_ + 8, kGilt, 11.5, 1, w - 16);
        }
        return;
    }
    frame_.fill_rect(0, 0, pw_, ph_, hex(0x000000, .45f));
    if (panel_ == Panel::ask) {
        frame_.fill_rect(14, 14, pw_ - 28, ph_ - 28, kPaper);
        frame_.begin(); frame_.rect(14.5, 14.5, pw_ - 29, ph_ - 29); frame_.stroke(kGilt, 1.5);
        text("Ask a question (" + std::to_string(puz_.asks - static_cast<int>(asked_.size())) + " left). Choose well: some questions settle it, some don't.", 24, 22, kInk, 11.5, 1, pw_ - 48);
        return;
    }
    int wx, wy, ww, wh;
    panel_box(wx, wy, ww, wh);
    frame_.fill_rect(wx, wy, ww, wh, kPaper);
    frame_.begin(); frame_.rect(wx + .5, wy + .5, ww - 1, wh - 1); frame_.stroke(kGilt, 1.5);
    int ly = wy + 30;
    text(panel_ == Panel::help ? "The Parrot's Table" : "Records", wx + (ww - text_w(panel_ == Panel::help ? "The Parrot's Table" : "Records", 15, 2)) / 2, wy + 8, kInk, 15, 2);
    const std::vector<std::string> lines = panel_lines();
    for (size_t i = 0; i < lines.size(); ++i) {
        const bool keys = panel_ == Panel::help && i + 1 == lines.size();
        const double size = keys ? ov_size_ - 1 : ov_size_;
        text(lines[i], wx + 14, ly, keys ? hex(0x7A6A50) : kInk, size, keys ? 1 : 0, ww - 28);
        ly += text_h(lines[i], size, keys ? 1 : 0, ww - 28) + 4;
    }
}

void TableView::compose() {
    texts_.clear();
    parlor_.r.present(frame_, 1, 0, 0, true);
    if (panel_ == Panel::help || panel_ == Panel::records || panel_ == Panel::ask) {
        draw_panel();
        for (const Button& b : buttons_) draw_button(b);
        return;
    }
    draw_top();
    draw_bubbles();
    draw_notebook();
    draw_panel();
    for (const Button& b : buttons_) draw_button(b);
    if (phase_ == Phase::verdict) {
        const std::string s = won_ ? "Case closed! " + names_[static_cast<size_t>(puz_.truth.culprit)] + " " + crime(crime_).did + "."
                                   : "Wrong bird! It was " + names_[static_cast<size_t>(puz_.truth.culprit)] + " all along.";
        const int w = std::min(pw_ - 8, text_w(s, 13, 1) + 20), th = text_h(s, 13, 1, w - 20) + 4;
        frame_.fill_rect((pw_ - w) / 2, top_h_ + 6, w, th, won_ ? hex(0x1E5A30, .92f) : hex(0x6A1E22, .92f));
        text(s, (pw_ - w) / 2 + 10, top_h_ + 8, kPaper, 13, 1, w - 20);
    }
}

void TableView::blit_texts(std::uint32_t* dst, size_t stride_px, double k) {
    const int sc = std::max(1, static_cast<int>(std::lround(bs_)));
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
                const auto ch = [&](int sh, float src) { return static_cast<std::uint32_t>(std::min(255.f, src * cov * 255 + static_cast<float>((d >> sh) & 255) * kk + .5f)) << sh; };
                drow[dx] = ch(0, pb) | ch(8, pg) | ch(16, pr) | (0xFFu << 24);
            }
        }
    }
}

}  // namespace pt

namespace pt {
void TableView::set_cabinet(bool foreground, bool music, bool sound, bool reduced) {
    if (timer_) {
        if (foreground) {
            last_ = std::chrono::steady_clock::now();
            (*timer_).start();
        } else {
            (*timer_).stop();
        }
    }

    cab_front_ = foreground; cab_music_ = music; cab_sound_ = sound; cab_reduced_ = reduced;
    audio_cabinet(foreground, music, sound);
    if (!foreground) { pressed_.clear(); }
}
}

namespace pt {
void TableView::layout_buttons() {
    layout_game_buttons();
    if (!opt_.hosted) return;
    for (std::size_t i = buttons_.size(); i > 0; --i) {
        const std::string& id = buttons_[i-1].id;
        if (id == "help" || id == "records" || id == "music") buttons_.erase(buttons_.begin() + static_cast<std::ptrdiff_t>(i-1));
    }
}
std::vector<games::GameCommand> TableView::commands() const { return {{"help", "Help", true, panel_ == Panel::help}, {"records", "Records", true, panel_ == Panel::records}}; }
void TableView::run_command(std::string_view id) {
    for (const games::GameCommand& cmd : commands()) {
        if (cmd.id == id && cmd.enabled) { action(cmd.checked ? "close" : std::string(id)); return; }
    }
}
}
