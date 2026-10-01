#include "collection.hpp"
#include "audio.hpp"
#include "gui_forms/window.hpp"
#include <fstream>
namespace games {
Collection::Collection(gf::StableId id) : Control(std::move(id)) {
    std::ifstream saved(cabinet_path().parent_path() / "current-game.txt");
    int active = 0;
    saved >> active;
    if (saved && active >= 0 && active <= 9)
        active_ = active;
    else
        choosing_ = true;
    if (!std::filesystem::exists(cabinet_path())) {
        Cabinet initial;
        initial.games[0].deal(Kind::solitaire, 1);
        initial.started[0] = true;
        save_cabinet(cabinet_path(), initial);
    }
    selected_ = active_;
    set_theme_override(games_theme(ButtonSkin::ivory));
}
void Collection::initialize_control_tree() {
    cards_ = gf::make_control<Table>(gf::StableId("collection.cards"));
    sudoku_ = gf::make_control<SudokuView>(gf::StableId("collection.sudoku"));
    add_child(cards_);
    add_child(sudoku_);
    eggy_ = gf::make_control<eggy::EggyView>(gf::StableId("collection.eggy"), eggy::Options{});
    add_child(eggy_);
    switchbox_ = gf::make_control<sbx::SwitchboxView>(gf::StableId("switchbox.view"), sbx::Options{});
    add_child(switchbox_);
    fourpegs_ = gf::make_control<fp::FourPegsView>(gf::StableId("fourpegs.view"), fp::Options{});
    add_child(fourpegs_);
    for (int i = 0; i < 8; ++i) {
        if (i == 4 || i == 5 || i == 7)
            continue; // Four Pegs and Switchbox have their own controls; Sticks & Stones is retired.
        puzzles_[i] = gf::make_control<PuzzleView>(
            gf::StableId("collection.puzzle." + std::to_string(i)), static_cast<PuzzleKind>(i));
        add_child(puzzles_[i]);
    }
    library_ = gf::make_control<LibrarySurface>(gf::StableId("collection.library"));
    add_child(library_);
    for (int i = 0; i < 10; ++i) {
        tiles_[i] = gf::make_control<GameTile>(gf::StableId("collection." + std::to_string(i)), i);
        (*library_).add_child(tiles_[i]);
        subscriptions_.push_back(
            (*tiles_[i])
                .clicked()
                .subscribe(
                    *this,
                    gf::Delegate<gf::ButtonBase&>::bind<Collection, &Collection::choose>(*this)));
    }
    (*library_).selection = selected_;
    (*tiles_[selected_]).set_selected(true);
    play_ = gf::make_control<GameButton>(gf::StableId("collection.open"), "Play / Continue");
    (*play_).set_skin(ButtonSkin::blue);
    (*library_).add_child(play_);
    subscriptions_.push_back((*play_).clicked().subscribe(
        *this, gf::Delegate<gf::ButtonBase&>::bind<Collection, &Collection::open_selected>(*this)));
    const char* groups[] = {"All games", "Card games", "Puzzles", "Long climb"};
    for (int i = 0; i < 4; ++i) {
        categories_[i] = gf::make_control<GameButton>(
            gf::StableId("collection.category." + std::to_string(i)), groups[i]);
        (*library_).add_child(categories_[i]);
        subscriptions_.push_back(
            (*categories_[i])
                .clicked()
                .subscribe(
                    *this,
                    gf::Delegate<gf::ButtonBase&>::bind<Collection, &Collection::category>(*this)));
    }
    (*categories_[0]).set_selected(true);
    const char* labels[] = {"Collection", "Music on", "Sound on", "Full motion"};
    for (int i = 0; i < 4; ++i) {
        controls_[i] = gf::make_control<GameButton>(
            gf::StableId("collection.command." + std::to_string(i)), labels[i]);
        add_child(controls_[i]);
        subscriptions_.push_back(
            (*controls_[i])
                .clicked()
                .subscribe(
                    *this,
                    gf::Delegate<gf::ButtonBase&>::bind<Collection, &Collection::command>(*this)));
    }
    preferences();
    visibility();
}
void Collection::on_attached_to_window() {
    audio_timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::milliseconds(16));
    subscriptions_.push_back((*audio_timer_).tick().subscribe(
        *this, gf::Delegate<>::bind<Collection, &Collection::poll_audio>(*this)));
    (*audio_timer_).start();
}
void Collection::on_detaching_from_window(gf::Window&) noexcept {
    if (audio_timer_) { (*audio_timer_).stop(); }
    audio_timer_.reset();
}
void Collection::poll_audio() { audio_poll(); }
void Collection::visibility() {
    (*cards_).set_visible(!choosing_ && active_ == 0);
    (*eggy_).set_visible(!choosing_ && active_ == 9);
    (*switchbox_).set_visible(!choosing_ && active_ == 7);
    (*fourpegs_).set_visible(!choosing_ && active_ == 6);
    (*sudoku_).set_visible(!choosing_ && active_ == 1);
    for (int i = 0; i < 8; ++i)
        if (puzzles_[i])
            (*puzzles_[i]).set_visible(!choosing_ && active_ == i + 2);
    (*library_).set_visible(choosing_);
    (*controls_[0]).set_selected(choosing_);
    preferences();
}
void Collection::arrange(gf::Rect b) {
    arrange_self(b);
    gf::Rect content{0, 52, b.width, b.height - 52};
    set_child_layout(cards_, content);
    set_child_layout(sudoku_, content);
    set_child_layout(eggy_, content);
    set_child_layout(switchbox_, content);
    set_child_layout(fourpegs_, content);
    for (int i = 0; i < 8; ++i)
        if (puzzles_[i])
            set_child_layout(puzzles_[i], content);
    set_child_layout(library_, content);
    set_child_layout(controls_[0], {153, 9, 132, 33});
    for (int i = 1; i < 4; ++i)
        set_child_layout(controls_[i], {b.width - 367 + (i - 1) * 119.0, 9, 108, 33});
}
void Collection::on_paint(gf::Painter& p, gf::Rect) {
    gf::Rect b = client_rectangle();
    const gf::GradientStop stops[] = {{0, gf::Color::rgba(254, 254, 251)},
                                      {1, gf::Color::rgba(226, 231, 229)}};
    p.fill_linear_gradient({0, 0, b.width, 52}, {0, 0}, {0, 52}, stops);
    const gf::GradientStop brand[] = {{0, gf::Color::rgba(206, 227, 224)},
                                      {1, gf::Color::rgba(221, 228, 238)}};
    p.fill_linear_gradient({0, 0, 140, 51}, {0, 0}, {140, 51}, brand);
    p.draw_text_utf8({24, 33}, "Games", {gf::FontRole::control, 26, 600, false, .1},
                     gf::Color::rgba(52, 76, 87));
    p.draw_line({140, 0}, {140, 51}, gf::Color::rgba(169, 184, 189), 1);
    p.draw_line({0, 51}, {b.width, 51}, gf::Color::rgba(138, 154, 161), 1);
    if (!choosing_)
        p.draw_text_utf8({305, 32}, collection_title(active_),
                         {gf::FontRole::content, 14, 400, false}, gf::Color::rgba(75, 90, 98));
}
void Collection::preferences() {
    Cabinet cabinet;
    if (!load_cabinet(cabinet_path(), cabinet))
        return;
    (*eggy_).set_cabinet_preferences(cabinet.music, cabinet.sound, cabinet.reduced);
    (*switchbox_).set_cabinet(!choosing_ && active_ == 7, cabinet.music, cabinet.sound, cabinet.reduced);
    (*fourpegs_).set_cabinet(!choosing_ && active_ == 6, cabinet.music, cabinet.sound, cabinet.reduced);
    (*controls_[1]).set_text(cabinet.music ? "Music on" : "Music off");
    (*controls_[2]).set_text(cabinet.sound ? "Sound on" : "Sound off");
    (*controls_[3]).set_text(cabinet.reduced ? "Quiet motion" : "Full motion");
}
void Collection::activate() {
    preferences();
    if (choosing_) {
        Cabinet cabinet;
        load_cabinet(cabinet_path(), cabinet);
        music_play("menu", cabinet.music);
        return;
    }
    if (active_ == 9) {
        music_play("", false);
        (*eggy_).activate();
    } else if (active_ == 7) {
        music_play("", false);
        (*switchbox_).activate();
    } else if (active_ == 6) {
        music_play("", false);
        (*fourpegs_).activate();
    } else if (active_ == 0)
        (*cards_).activate();
    else if (active_ == 1)
        (*sudoku_).activate();
    else
        (*puzzles_[active_ - 2]).activate();
}
void Collection::choose(gf::ButtonBase& button) {
    selected_ = std::stoi(std::string(button.stable_id().value().substr(11)));
    (*library_).selection = selected_;
    for (int i = 0; i < 10; ++i)
        if (tiles_[i])
            (*tiles_[i]).set_selected(i == selected_);
    (*library_).invalidate(gf::Dirty::paint);
    const GameTile* tile = dynamic_cast<GameTile*>(&button);
    if (tile && (*tile).open_requested())
        start_selected();
}
void Collection::open_selected(gf::ButtonBase&) {
    start_selected();
}
void Collection::category(gf::ButtonBase& button) {
    int c = std::stoi(std::string(button.stable_id().value().substr(20)));
    (*library_).category = c;
    bool matches = c == 0 || (c == 1 && selected_ == 0) ||
                   (c == 2 && selected_ > 0 && selected_ < 9) || (c == 3 && selected_ == 9);
    if (!matches) {
        selected_ = c == 1 ? 0 : c == 3 ? 9 : 1;
        (*library_).selection = selected_;
        for (int i = 0; i < 10; ++i)
            if (tiles_[i])
                (*tiles_[i]).set_selected(i == selected_);
    }
    for (int i = 0; i < 4; ++i)
        (*categories_[i]).set_selected(i == c);
    (*library_).invalidate(gf::Dirty::layout | gf::Dirty::paint);
}
void Collection::start_selected() {
    active_ = selected_;
    choosing_ = false;
    visibility();
    std::filesystem::path path = cabinet_path().parent_path() / "current-game.txt",
                          temporary = path;
    temporary += ".tmp";
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream saved(temporary);
    saved << active_ << '\n';
    saved.close();
    if (saved)
        std::filesystem::rename(temporary, path, error);
    activate();
    invalidate(gf::Dirty::paint);
}
void Collection::command(gf::ButtonBase& button) {
    std::string id(button.stable_id().value());
    int index = std::stoi(id.substr(id.rfind('.') + 1));
    if (index == 0) {
        choosing_ = !choosing_;
        visibility();
        activate();
        invalidate(gf::Dirty::paint);
        return;
    }
    Cabinet cabinet;
    if (!load_cabinet(cabinet_path(), cabinet))
        return;
    if (index == 1)
        cabinet.music = !cabinet.music;
    if (index == 2)
        cabinet.sound = !cabinet.sound;
    if (index == 3)
        cabinet.reduced = !cabinet.reduced;
    if (save_cabinet(cabinet_path(), cabinet)) {
        (*cards_).reload_preferences();
        preferences();
        activate();
    }
}
} // namespace games
