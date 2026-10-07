#include "collection.hpp"
#include "audio.hpp"
#include "pcm_player.hpp"
#include "storage.hpp"
#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include <sstream>
#include "gui_forms/window.hpp"
#include <fstream>
namespace games {
namespace {
std::filesystem::path entry_path() {
    return cabinet_path().parent_path() / "playsuite-entry.txt";
}
// Earlier versions remembered one of ten collection slots; card games shared slot 0.
Entry from_old_slot(int slot, int card_kind) {
    const int slots[] = {0,4,5,6,7,8,9,10,11,12};
    return static_cast<Entry>(slot == 0 ? std::clamp(card_kind,0,3) : slots[std::clamp(slot,0,9)]);
}
} // namespace

Collection::Collection(gf::StableId id, bool dev) : Control(std::move(id)) {
    modules_.dev = dev;
    Cabinet cabinet;
    const bool have_cabinet =
        std::filesystem::exists(cabinet_path()) && load_cabinet(cabinet_path(), cabinet);
    if (!std::filesystem::exists(cabinet_path())) {
        Cabinet initial;
        initial.games[0].deal(Kind::solitaire, 1);
        initial.started[0] = true;
        save_cabinet(cabinet_path(), initial);
    }
    std::ifstream saved(entry_path());
    std::string marker;
    int entry = -1, in_game = 0;
    bool restored = false;
    if (saved >> marker) {
        if (marker == "v2") {
            if (saved >> entry >> in_game) {
                int opened = -1;
                while (saved >> opened) if (opened >= 0) opened_.insert(opened);
                restored = true;
            }
        } else {
            std::istringstream first(marker);
            std::uint32_t mask = 0;
            if (first >> entry && saved >> in_game >> mask) {
                for (int i=0;i<32;++i) if ((mask >> i) & 1u) opened_.insert(i);
                restored = true;
            }
        }
    }
    if (restored && valid_entry(static_cast<Entry>(entry))) {
        active_ = static_cast<Entry>(entry);
        shelf_open_ = in_game == 0;
    } else {
        std::ifstream old(cabinet_path().parent_path() / "current-game.txt");
        int slot = 0;
        if (!restored && old >> slot && slot >= 0 && slot <= 9) {
            const Entry previous = from_old_slot(slot, have_cabinet ? cabinet.active : 0);
            if (valid_entry(previous)) active_ = previous;
            for (int i=0;i<18;++i) opened_.insert(i);
        }
        shelf_open_ = true;
    }
    set_theme_override(games_theme(ButtonSkin::ivory));
}
Collection::~Collection() = default;
void Collection::initialize_control_tree() {
    sprites_.request_update = std::bind_front(&Collection::wake, this);
    shelf_ = gf::make_control<ShelfView>(gf::StableId("collection.shelf"), sprites_);
    add_child(shelf_);
    (*shelf_).open = std::bind_front(&Collection::open_entry, this);
    (*shelf_).bind_masters(model_);
    (*shelf_).select(active_);
    capsule_ = gf::make_control<CommandCapsule>(gf::StableId("collection.capsule"), sprites_);
    add_child(capsule_);
    (*capsule_).back = std::bind_front(&Collection::show_shelf, this);
    (*capsule_).command = std::bind_front(&Collection::run_command, this);
    (*capsule_).bind_masters(model_);
    gf::on(model_.music.invoked(), *this, &Collection::toggle, 0);
    gf::on(model_.sound.invoked(), *this, &Collection::toggle, 1);
    gf::on(model_.reduced.invoked(), *this, &Collection::toggle, 2);
    gf::on(model_.settings.invoked(), *this, &Collection::toggle, 3);
    help_link_ = gf::make_control<HelpGlyph>(gf::StableId("collection.help"), "?");
    (*help_link_).set_accessible_name("PlaySuite help (H)");
    (*help_link_).set_paint_plane(gf::PaintPlane::overlay);
    gf::on((*help_link_).clicked(), *this, &Collection::clicked_help);
    help_ = gf::make_control<HelpBook>(gf::StableId("collection.help-book"));
    (*help_).close = std::bind_front(&Collection::close_help, this);
    (*help_).set_visible(false);
    settings_ = gf::make_control<SettingsSheet>(gf::StableId("collection.settings"));
    (*settings_).bind_masters(model_);
    (*settings_).close = std::bind_front(&Collection::close_settings, this);
    (*settings_).set_visible(false);
    add_child(help_link_);
    add_child(help_);
    add_child(settings_);
    masters_ = SettingsStore::shared().observe(std::bind_front(&Collection::masters_changed, this));
    const SuiteSettings& masters = SettingsStore::shared().values();
    music_shown_ = masters.music;
    sound_shown_ = masters.sound;
    reduced_shown_ = masters.reduced;
    for (Entry entry : entries)
        (*shelf_).set_progress(entry, opened_.contains(static_cast<int>(entry)));
    if (!shelf_open_)
        ensure_view(active_);
    preferences();
    visibility();
    refresh_commands();
}
void Collection::ensure_view(Entry entry) {
    if (view(entry)) return;
    std::unique_ptr<GameInstance> instance = game_descriptor(entry).create(modules_);
    const std::shared_ptr<gf::Control> created = (*instance).control();
    games_[entry] = std::move(instance);
    // Card modules intentionally share a control and cabinet. Attach it only once.
    if (std::find(children().begin(), children().end(), created) == children().end()) {
        (*created).set_visible(false);
        add_child(created);
        static_cast<void>(set_child_index((*created).runtime_id(), children().size()-1));
    }
}
void Collection::on_attached_to_window() {
    timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::milliseconds(16));
    gf::on((*timer_).tick(), *this, &Collection::tick);
    last_tick_ = std::chrono::steady_clock::now();
    (*timer_).start();
}
void Collection::on_detaching_from_window(gf::Window& window) noexcept {
    if (timer_)
        (*timer_).stop();
    timer_.reset();
    sprites_.release(window);
}
bool Collection::uses_rail(Entry entry) const { return game_descriptor(entry).rail; }
std::shared_ptr<gf::Control> Collection::view(Entry entry) const {
    const std::map<Entry,std::unique_ptr<GameInstance>>::const_iterator found = games_.find(entry);
    return found == games_.end() ? nullptr : (*(*found).second).control();
}
CommandSource* Collection::source(Entry entry) const {
    const std::map<Entry,std::unique_ptr<GameInstance>>::const_iterator found = games_.find(entry);
    return found == games_.end() ? nullptr : (*found).second.get();
}
void Collection::wake() {
    if (!timer_)
        return;
    (*timer_).set_interval(std::chrono::milliseconds(16));
    (*timer_).start();
}
void Collection::tick() {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const double dt = std::clamp(std::chrono::duration<double>(now - last_tick_).count(), 0.0, .25);
    last_tick_ = now;
    audio_poll();
    if (gf::Window* window = attached_window())
        set_polygon_scale((*window).scale());
    if (gf::Window* window = attached_window(); window && sprites_.update(*window)) {
        (*shelf_).invalidate(gf::Dirty::paint);
        for (const std::shared_ptr<gf::Control>& child : (*shelf_).children())
            (*child).invalidate(gf::Dirty::paint);
        (*capsule_).set_maximum_width(capsule_width_limit_);
        (*capsule_).invalidate(gf::Dirty::paint);
        invalidate(gf::Dirty::layout);
    }
    if (shelf_open_) {
        if (timer_ && !sprites_.waiting() && !audio_pending())
            (*timer_).stop();
        return;
    }
    const gf::Rect area{0, 0, client_rectangle().width - 48,
                        uses_rail(active_) ? current_rail_height_ : 60.0};
    gf::Rect r = (*capsule_).placement(area);
    // The placement includes the shadow margin, which doubles as a forgiving hover border.
    const bool inside = pointer_.x >= r.x && pointer_.x <= r.x + r.width && pointer_.y >= r.y - 8 &&
                        pointer_.y <= r.y + r.height;
    if ((*capsule_).step(dt, inside, reduced_))
        invalidate(gf::Dirty::layout);
    refresh_ += dt;
    if (refresh_ > .2) {
        refresh_ = 0;
        refresh_commands();
    }
    // Commands can change after an asynchronous game action. Poll that cheap
    // model at 5 Hz; only hover animation, text and audio preparation need 60 Hz.
    if (timer_)
        (*timer_).set_interval(std::chrono::milliseconds(
            (*capsule_).unsettled(inside) || sprites_.waiting() || audio_pending() ? 16 : 200));
}
void Collection::on_pointer_preview(gf::PointerEvent& e) {
    if (settings_open()) {
        const gf::Point local = (*settings_).point_from_window(e.position);
        const gf::Rect paper = (*settings_).paper();
        if (local.x < paper.x || local.y < paper.y || local.x >= paper.x + paper.width ||
            local.y >= paper.y + paper.height) {
            // A press on the dimmed background closes the screen, like Escape.
            if (e.action == gf::PointerAction::down && e.button == gf::PointerButton::primary)
                close_settings();
            e.handled = true;
            return;
        }
    }
    if (help_open()) {
        const gf::Point local = (*help_).point_from_window(e.position);
        const gf::Rect paper = (*help_).client_rectangle();
        if (local.x < 0 || local.y < 0 || local.x >= paper.width || local.y >= paper.height) {
            e.handled = true;
            return;
        }
    }
    wake();
    pointer_ = point_from_window(e.position);
    if (e.action == gf::PointerAction::leave && (pointer_.x < 0 || pointer_.y < 0))
        pointer_ = {-1000, -1000};
    gf::Control::on_pointer_preview(e);
}
void Collection::on_key_preview(gf::KeyEvent& e) {
    wake();
    const std::shared_ptr<gf::Control> focus =
        attached_window() ? (*attached_window()).focused_control() : nullptr;
    const bool entering_text = dynamic_cast<gf::TextBox*>(focus.get()) != nullptr ||
        (!help_open() && !shelf_open_ && (*games_.at(active_)).editing_name());
    if (e.action == gf::KeyAction::down && e.modifiers == gf::Modifier::none &&
        (!entering_text || e.physical_key == gf::PhysicalKey::f1)) {
        const bool help_key =
            e.physical_key == gf::PhysicalKey::h || e.physical_key == gf::PhysicalKey::f1;
        const bool escape = e.physical_key == gf::PhysicalKey::escape;
        if (help_key || e.physical_key == gf::PhysicalKey::m ||
            ((help_open() || settings_open()) && escape)) {
            if (!e.repeat) {
                if (e.physical_key == gf::PhysicalKey::m)
                    toggle(0);
                else if (settings_open() && escape)
                    close_settings();
                else if (help_open())
                    close_help();
                else
                    show_help();
            }
            e.handled = true;
            return;
        }
    }
    if (help_open() || settings_open())
        return;
    if (!shelf_open_ && e.action == gf::KeyAction::down && e.physical_key == gf::PhysicalKey::f2 &&
        source(active_)) {
        run_command("new");
        e.handled = true;
        return;
    }
    gf::Control::on_key_preview(e);
}
void Collection::refresh_commands() {
    if (!view(active_))
        return;
    const EntryInfo& info = entry_info(active_);
    std::vector<GameCommand> commands;
    if (CommandSource* s = source(active_))
        commands = (*s).commands();
    struct Explanation {
        const std::vector<HelpTopic>& topics;
        bool operator()(const GameCommand& command) const {
            for (const HelpTopic& topic : topics) if (command.id == topic.id) return true;
            return command.id == "help" || command.id == "rules";
        }
    };
    std::erase_if(commands, Explanation{game_descriptor(active_).help_topics});
    struct Reserved {
        bool operator()(const GameCommand& command) const { return command.id == "settings"; }
    };
    std::erase_if(commands, Reserved{});
    (*capsule_).set_game(info.title, std::move(commands));
    // A game's values can change under the screen (a key, a command); show them.
    if (settings_open())
        (*settings_).refresh();
}
void Collection::run_command(const std::string& id) {
    // "settings" is the shell's own, like Help: every game and the shelf have it.
    if (id == "settings") {
        settings_open() ? close_settings() : show_settings();
        return;
    }
    bool explanation = id == "help" || id == "rules";
    for (const HelpTopic& topic : game_descriptor(active_).help_topics) explanation = explanation || id == topic.id;
    if (explanation) {
        show_help(id);
        return;
    }
    if (CommandSource* s = source(active_))
        (*s).run_command(id);
    refresh_commands();
    // Return keyboard focus to the game after a capsule click.
    if (attached_window() && view(active_))
        static_cast<void>((*attached_window()).request_focus(view(active_)));
}
bool Collection::overlay(const std::shared_ptr<gf::Control>& child) const {
    return child == shelf_ || child == capsule_ || child == help_ || child == help_link_ ||
           child == settings_;
}
void Collection::visibility() {
    for (const std::shared_ptr<gf::Control>& child : children())
        if (!overlay(child))
            (*child).set_visible(false);
    if (!shelf_open_)
        (*view(active_)).set_visible(true);
    (*shelf_).set_visible(shelf_open_);
    (*capsule_).set_visible(!shelf_open_);
    preferences();
    invalidate(gf::Dirty::layout | gf::Dirty::paint);
}
void Collection::arrange(gf::Rect b) {
    arrange_self(b);
    const gf::Rect full{0, 0, b.width, b.height};
    if (capsule_width_limit_ != b.width - 64) {
        capsule_width_limit_ = b.width - 64;
        (*capsule_).set_maximum_width(capsule_width_limit_);
    }
    const gf::Rect capsule_bounds = (*capsule_).placement({0, 0, b.width - 48, rail_height});
    current_rail_height_ = std::max(rail_height, capsule_bounds.y + capsule_bounds.height);
    const gf::Rect below{0, current_rail_height_, b.width,
                         std::max(0.0, b.height - current_rail_height_)};
    // Each view is laid out once per pass; live-surface games sit below the rail.
    for (const std::shared_ptr<gf::Control>& child : children())
        if (!overlay(child) && (*child).visible()) {
            const bool railed = uses_rail(active_);
            set_child_layout(child, railed ? below : full);
        }
    set_child_layout(shelf_, full);
    const gf::Rect area{0, 0, b.width - 48, uses_rail(active_) ? current_rail_height_ : 60.0};
    gf::Rect placed = (*capsule_).placement(area);
    // Over a live-surface game the capsule stays within the rail; nothing may overlap
    // the game's presented surface.
    if (!shelf_open_ && uses_rail(active_))
        placed.height = std::min(placed.height, current_rail_height_ - placed.y);
    set_child_layout(capsule_, placed);
    set_child_layout(help_link_, {b.width - 44, 7, 36, 36});
    const double paper_width = std::min(780.0, b.width - 32);
    set_child_layout(help_, {b.width - paper_width - 16, 16, paper_width, b.height - 32});
    set_child_layout(settings_, full);
}
void Collection::on_paint(gf::Painter& p, gf::Rect) {
    if (shelf_open_ || !uses_rail(active_))
        return;
    // The rail above live-surface games: graphite with a gold hairline.
    const gf::Rect b = client_rectangle();
    fill_vertical(p, {0, 0, b.width, current_rail_height_}, gf::Color::rgba(49, 58, 73),
                  gf::Color::rgba(26, 31, 42));
    p.draw_line({0, current_rail_height_ - 1}, {b.width, current_rail_height_ - 1},
                gf::Color::rgba(255, 210, 122, 150), 1);
}
void Collection::preferences() {
    const SuiteSettings masters = SettingsStore::shared().values();
    reduced_ = masters.reduced;
    set_bus_gain(AudioBus::music, audio_gain(masters.music_volume));
    set_bus_gain(AudioBus::sound, audio_gain(masters.sound_volume));
    for (const std::pair<const Entry,std::unique_ptr<GameInstance>>& game : games_)
        (*game.second).preferences(!shelf_open_ && active_ == game.first,
                                   masters.music, masters.sound, masters.reduced);
    (*shelf_).set_reduced(masters.reduced);
    if (settings_open())
        (*settings_).refresh();
}
void Collection::masters_changed() {
    const SuiteSettings masters = SettingsStore::shared().values();
    preferences();
    // Volumes and the card back need nothing more. A switch restarts or stops music
    // and lets the open game take up the new masters.
    if (masters.music == music_shown_ && masters.sound == sound_shown_ &&
        masters.reduced == reduced_shown_)
        return;
    music_shown_ = masters.music;
    sound_shown_ = masters.sound;
    reduced_shown_ = masters.reduced;
    // Music changes must not take keyboard focus out of help or settings.
    if (help_open() || settings_open()) {
        if (shelf_open_)
            music_play("menu", masters.music);
        else if (!uses_rail(active_))
            (*games_.at(active_)).activate();
    } else
        activate();
}
void Collection::persist() const {
    std::filesystem::path path = entry_path(), temporary = path;
    temporary += ".tmp";
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    {
        std::ofstream saved(temporary);
        saved << "v2 " << static_cast<int>(active_) << ' ' << (shelf_open_ ? 0 : 1);
        for (int entry : opened_) saved << ' ' << entry;
        saved << '\n';
        saved.close();
        if (!saved)
            return;
    }
    std::filesystem::rename(temporary, path, error);
}
void Collection::activate() {
    wake();
    preferences();
    if (shelf_open_) {
        music_play("menu", SettingsStore::shared().values().music);
        (*shelf_).focus_selection();
        return;
    }
    if (uses_rail(active_)) music_play("", false);
    (*games_.at(active_)).activate();
}
void Collection::open_entry(Entry entry) {
    if (!valid_entry(entry)) return;
    close_help();
    close_settings();
    wake();
    ensure_view(entry);
    active_ = entry;
    shelf_open_ = false;
    opened_.insert(static_cast<int>(entry));
    (*shelf_).select(entry);
    (*shelf_).set_progress(entry, true);
    (*capsule_).fold();
    visibility();
    refresh_commands();
    persist();
    activate();
}
void Collection::show_shelf() {
    close_help();
    close_settings();
    shelf_open_ = true;
    (*shelf_).select(active_);
    visibility();
    persist();
    activate();
}
void Collection::toggle(int which) {
    // The store saves and notifies; masters_changed() and every switch follow it.
    SettingsStore& store = SettingsStore::shared();
    if (which == 0)
        store.toggle_music();
    else if (which == 1)
        store.toggle_sound();
    else if (which == 2)
        store.toggle_reduced();
    else if (which == 3)
        settings_open() ? close_settings() : show_settings();
}
void Collection::show_settings() {
    close_help();
    const bool was_open = settings_open();
    (*settings_).show_for(shelf_open_ ? std::string() : std::string(entry_info(active_).title),
                          shelf_open_ ? nullptr : source(active_));
    (*settings_).set_visible(true);
    model_.settings.set_checked(true);
    if (!was_open && attached_window()) {
        settings_focus_ = (*attached_window()).begin_focus_scope(settings_);
        static_cast<void>((*attached_window()).request_focus((*settings_).first_control()));
    }
    invalidate(gf::Dirty::layout | gf::Dirty::paint);
}
void Collection::close_settings() {
    if (!settings_open())
        return;
    if (attached_window() && settings_focus_)
        static_cast<void>((*attached_window()).end_focus_scope(settings_focus_));
    settings_focus_ = {};
    (*settings_).set_visible(false);
    model_.settings.set_checked(false);
    if (!shelf_open_)
        refresh_commands();
    invalidate(gf::Dirty::paint);
}
void Collection::clicked_help() {
    show_help();
}
void Collection::show_help(std::string_view topic) {
    close_settings();
    const bool was_open = help_open();
    (*help_).select(shelf_open_ ? -1 : static_cast<int>(active_), topic);
    (*help_).set_visible(true);
    if (!was_open && attached_window())
        help_focus_ = (*attached_window()).begin_focus_scope(help_);
    invalidate(gf::Dirty::layout | gf::Dirty::paint);
}
void Collection::close_help() {
    if (!help_open())
        return;
    if (attached_window() && help_focus_)
        static_cast<void>((*attached_window()).end_focus_scope(help_focus_));
    help_focus_ = {};
    (*help_).set_visible(false);
    invalidate(gf::Dirty::paint);
}
} // namespace games
