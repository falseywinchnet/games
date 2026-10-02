#include "collection.hpp"
#include "audio.hpp"
#include "gui_forms/window.hpp"
#include <fstream>
namespace games {
namespace {
std::filesystem::path entry_path() {
    return cabinet_path().parent_path() / "playsuite-entry.txt";
}
// Earlier versions remembered one of ten collection slots; card games shared slot 0.
Entry from_old_slot(int slot, int card_kind) {
    const Entry slots[] = {Entry::solitaire, Entry::sudoku, Entry::gems, Entry::cube,
                           Entry::untangle,  Entry::atom,   Entry::pegs, Entry::switchbox,
                           Entry::solve,     Entry::eggy};
    if (slot == 0)
        return static_cast<Entry>(std::clamp(card_kind, 0, 3));
    return slots[std::clamp(slot, 0, 9)];
}
int puzzle_index(Entry entry) {
    switch (entry) {
    case Entry::gems:
        return static_cast<int>(PuzzleKind::gems);
    case Entry::cube:
        return static_cast<int>(PuzzleKind::cube);
    case Entry::untangle:
        return static_cast<int>(PuzzleKind::untangle);
    case Entry::solve:
        return static_cast<int>(PuzzleKind::solve);
    default:
        return -1;
    }
}
bool is_cards(Entry entry) {
    return static_cast<int>(entry) <= static_cast<int>(Entry::hearts);
}
} // namespace

Collection::Collection(gf::StableId id) : Control(std::move(id)) {
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
    int entry = -1, in_game = 0;
    std::uint32_t opened = 0;
    if (saved >> entry >> in_game >> opened && entry >= 0 && entry < entry_count) {
        active_ = static_cast<Entry>(entry);
        shelf_open_ = in_game == 0;
        opened_ = opened;
    } else {
        // First PlaySuite start: keep the old selection, and open on the shelf.
        std::ifstream old(cabinet_path().parent_path() / "current-game.txt");
        int slot = 0;
        if (old >> slot && slot >= 0 && slot <= 9) {
            active_ = from_old_slot(slot, have_cabinet ? cabinet.active : 0);
            opened_ = (1u << entry_count) - 1;
        }
        shelf_open_ = true;
    }
    set_theme_override(games_theme(ButtonSkin::ivory));
}
Collection::~Collection() = default;
void Collection::initialize_control_tree() {
    cards_ = gf::make_control<Table>(gf::StableId("collection.cards"));
    sudoku_ = gf::make_control<SudokuView>(gf::StableId("collection.sudoku"));
    add_child(cards_);
    add_child(sudoku_);
    eggy_ = gf::make_control<eggy::EggyView>(gf::StableId("collection.eggy"), eggy::Options{});
    add_child(eggy_);
    switchbox_ =
        gf::make_control<sbx::SwitchboxView>(gf::StableId("switchbox.view"), sbx::Options{});
    add_child(switchbox_);
    fp::Options pegs;
    pegs.hosted = true;
    fourpegs_ = gf::make_control<fp::FourPegsView>(gf::StableId("fourpegs.view"), pegs);
    add_child(fourpegs_);
    ap::Options atom;
    atom.hosted = true;
    atomprobe_ = gf::make_control<ap::AtomProbeView>(gf::StableId("atomprobe.view"), atom);
    add_child(atomprobe_);
    for (Entry e : {Entry::gems, Entry::cube, Entry::untangle, Entry::solve}) {
        const int i = puzzle_index(e);
        puzzles_[i] = gf::make_control<PuzzleView>(
            gf::StableId("collection.puzzle." + std::to_string(i)), static_cast<PuzzleKind>(i));
        add_child(puzzles_[i]);
    }
    shelf_ = gf::make_control<ShelfView>(gf::StableId("collection.shelf"), sprites_);
    add_child(shelf_);
    (*shelf_).open = std::bind_front(&Collection::open_entry, this);
    (*shelf_).toggle = std::bind_front(&Collection::toggle, this);
    (*shelf_).select(active_);
    capsule_ = gf::make_control<CommandCapsule>(gf::StableId("collection.capsule"), sprites_);
    add_child(capsule_);
    (*capsule_).back = std::bind_front(&Collection::show_shelf, this);
    (*capsule_).command = std::bind_front(&Collection::run_command, this);
    (*capsule_).toggle = std::bind_front(&Collection::toggle, this);
    for (int i = 0; i < entry_count; ++i)
        (*shelf_).set_progress(static_cast<Entry>(i), (opened_ >> i) & 1u);
    if (is_cards(active_))
        (*cards_).show_kind(static_cast<Kind>(active_));
    preferences();
    visibility();
    refresh_commands();
}
void Collection::on_attached_to_window() {
    timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::milliseconds(16));
    subscriptions_.push_back((*timer_).tick().subscribe(
        *this, gf::Delegate<>::bind<Collection, &Collection::tick>(*this)));
    last_tick_ = std::chrono::steady_clock::now();
    (*timer_).start();
}
void Collection::on_detaching_from_window(gf::Window& window) noexcept {
    if (timer_)
        (*timer_).stop();
    timer_.reset();
    sprites_.release(window);
}
bool Collection::uses_rail(Entry entry) const {
    // These games present their own live surfaces, which nothing may float above.
    return entry == Entry::atom || entry == Entry::pegs || entry == Entry::switchbox ||
           entry == Entry::eggy;
}
std::shared_ptr<gf::Control> Collection::view(Entry entry) const {
    if (is_cards(entry))
        return cards_;
    switch (entry) {
    case Entry::sudoku:
        return sudoku_;
    case Entry::atom:
        return atomprobe_;
    case Entry::pegs:
        return fourpegs_;
    case Entry::switchbox:
        return switchbox_;
    case Entry::eggy:
        return eggy_;
    default:
        return puzzles_[static_cast<std::size_t>(puzzle_index(entry))];
    }
}
CommandSource* Collection::source(Entry entry) const {
    if (is_cards(entry))
        return cards_.get();
    if (entry == Entry::sudoku)
        return sudoku_.get();
    const int i = puzzle_index(entry);
    return i >= 0 ? puzzles_[static_cast<std::size_t>(i)].get() : nullptr;
}
void Collection::tick() {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const double dt = std::clamp(std::chrono::duration<double>(now - last_tick_).count(), 0.0, .1);
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
    if (shelf_open_)
        return;
    const gf::Rect area{0, 0, client_rectangle().width, uses_rail(active_) ? rail_height : 60.0};
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
}
void Collection::on_pointer_preview(gf::PointerEvent& e) {
    pointer_ = point_from_window(e.position);
    if (e.action == gf::PointerAction::leave && (pointer_.x < 0 || pointer_.y < 0))
        pointer_ = {-1000, -1000};
    gf::Control::on_pointer_preview(e);
}
void Collection::on_key_preview(gf::KeyEvent& e) {
    if (!shelf_open_ && e.action == gf::KeyAction::down && e.physical_key == gf::PhysicalKey::f2 &&
        active_ != Entry::eggy && active_ != Entry::switchbox) {
        run_command("new");
        e.handled = true;
        return;
    }
    gf::Control::on_key_preview(e);
}
void Collection::refresh_commands() {
    const EntryInfo& info = entry_info(active_);
    std::vector<GameCommand> commands;
    if (CommandSource* s = source(active_))
        commands = (*s).commands();
    else if (active_ == Entry::pegs || active_ == Entry::atom) {
        const std::string panel =
            active_ == Entry::pegs ? (*fourpegs_).host_panel() : (*atomprobe_).host_panel();
        commands = {{"new", active_ == Entry::pegs ? "New game" : "New box", true, false, true},
                    {"help", "Help", true, panel == "help"},
                    {"scores", "Top scores", true, panel == "scores"}};
    }
    (*capsule_).set_game(info.title, std::move(commands));
}
void Collection::run_command(const std::string& id) {
    if (CommandSource* s = source(active_))
        (*s).run_command(id);
    else if (active_ == Entry::pegs)
        (*fourpegs_).host_command(id);
    else if (active_ == Entry::atom)
        (*atomprobe_).host_command(id);
    refresh_commands();
    // Return keyboard focus to the game after a capsule click.
    if (attached_window() && view(active_))
        static_cast<void>((*attached_window()).request_focus(view(active_)));
}
void Collection::visibility() {
    for (const std::shared_ptr<gf::Control>& child : children())
        if (child != shelf_ && child != capsule_)
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
    const gf::Rect below{0, rail_height, b.width, std::max(0.0, b.height - rail_height)};
    // Each view is laid out once per pass; live-surface games sit below the rail.
    for (const std::shared_ptr<gf::Control>& child : children())
        if (child != shelf_ && child != capsule_) {
            const bool railed =
                child == atomprobe_ || child == fourpegs_ || child == switchbox_ || child == eggy_;
            set_child_layout(child, railed ? below : full);
        }
    set_child_layout(shelf_, full);
    if (capsule_width_limit_ != b.width - 16) {
        capsule_width_limit_ = b.width - 16;
        (*capsule_).set_maximum_width(capsule_width_limit_);
    }
    const gf::Rect area{0, 0, b.width, uses_rail(active_) ? rail_height : 60.0};
    gf::Rect placed = (*capsule_).placement(area);
    // Over a live-surface game the capsule stays within the rail; nothing may overlap
    // the game's presented surface.
    if (!shelf_open_ && uses_rail(active_))
        placed.height = std::min(placed.height, rail_height - placed.y);
    set_child_layout(capsule_, placed);
}
void Collection::on_paint(gf::Painter& p, gf::Rect) {
    if (shelf_open_ || !uses_rail(active_))
        return;
    // The rail above live-surface games: graphite with a gold hairline.
    const gf::Rect b = client_rectangle();
    fill_vertical(p, {0, 0, b.width, rail_height}, gf::Color::rgba(49, 58, 73),
                  gf::Color::rgba(26, 31, 42));
    p.draw_line({0, rail_height - 1}, {b.width, rail_height - 1},
                gf::Color::rgba(255, 210, 122, 150), 1);
}
void Collection::preferences() {
    Cabinet cabinet;
    if (!load_cabinet(cabinet_path(), cabinet))
        return;
    reduced_ = cabinet.reduced;
    (*eggy_).set_cabinet_preferences(cabinet.music, cabinet.sound, cabinet.reduced);
    (*switchbox_)
        .set_cabinet(!shelf_open_ && active_ == Entry::switchbox, cabinet.music, cabinet.sound,
                     cabinet.reduced);
    (*fourpegs_)
        .set_cabinet(!shelf_open_ && active_ == Entry::pegs, cabinet.music, cabinet.sound,
                     cabinet.reduced);
    (*atomprobe_)
        .set_cabinet(!shelf_open_ && active_ == Entry::atom, cabinet.music, cabinet.sound,
                     cabinet.reduced);
    (*shelf_).set_preferences(cabinet.music, cabinet.sound, cabinet.reduced);
    (*capsule_).set_preferences(cabinet.music, cabinet.sound, cabinet.reduced);
}
void Collection::persist() const {
    std::filesystem::path path = entry_path(), temporary = path;
    temporary += ".tmp";
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    {
        std::ofstream saved(temporary);
        saved << static_cast<int>(active_) << ' ' << (shelf_open_ ? 0 : 1) << ' ' << opened_
              << '\n';
        if (!saved)
            return;
    }
    std::filesystem::rename(temporary, path, error);
}
void Collection::activate() {
    preferences();
    if (shelf_open_) {
        Cabinet cabinet;
        load_cabinet(cabinet_path(), cabinet);
        music_play("menu", cabinet.music);
        (*shelf_).focus_selection();
        return;
    }
    if (is_cards(active_)) {
        (*cards_).show_kind(static_cast<Kind>(active_));
        (*cards_).activate();
    } else if (active_ == Entry::sudoku)
        (*sudoku_).activate();
    else if (active_ == Entry::eggy) {
        music_play("", false);
        (*eggy_).activate();
    } else if (active_ == Entry::switchbox) {
        music_play("", false);
        (*switchbox_).activate();
    } else if (active_ == Entry::pegs) {
        music_play("", false);
        (*fourpegs_).activate();
    } else if (active_ == Entry::atom) {
        music_play("", false);
        (*atomprobe_).activate();
    } else
        (*puzzles_[static_cast<std::size_t>(puzzle_index(active_))]).activate();
}
void Collection::open_entry(Entry entry) {
    active_ = entry;
    shelf_open_ = false;
    opened_ |= 1u << static_cast<int>(entry);
    (*shelf_).select(entry);
    (*shelf_).set_progress(entry, true);
    (*capsule_).fold();
    visibility();
    refresh_commands();
    persist();
    activate();
}
void Collection::show_shelf() {
    shelf_open_ = true;
    (*shelf_).select(active_);
    visibility();
    persist();
    activate();
}
void Collection::toggle(int which) {
    Cabinet cabinet;
    if (!load_cabinet(cabinet_path(), cabinet))
        return;
    if (which == 0)
        cabinet.music = !cabinet.music;
    if (which == 1)
        cabinet.sound = !cabinet.sound;
    if (which == 2)
        cabinet.reduced = !cabinet.reduced;
    if (save_cabinet(cabinet_path(), cabinet)) {
        (*cards_).reload_preferences();
        preferences();
        activate();
    }
}
} // namespace games
