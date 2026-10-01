#include "sudoku_view.hpp"
#include "audio.hpp"
#include "gui_forms/window.hpp"
#include "presentation.hpp"
#include "storage.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
namespace games {
static std::filesystem::path sudoku_path() {
    return cabinet_path().parent_path() / "sudoku-v1.txt";
}
SudokuView::SudokuView(gf::StableId id) : Control(std::move(id)) {
    set_focusable(true);
    set_accessible_name("Sudoku. Choose a digit; click a square to set it, right click to make a "
                        "note. Arrow keys move; N toggles notes; Z undoes.");
    game.load(sudoku_path());
    set_theme_override(games_theme(game.dark ? ButtonSkin::slate : ButtonSkin::blue));
    difficulty_ = game.difficulty;
}
void SudokuView::initialize_control_tree() {
    const char* labels[] = {"New game", "Undo",  "Notes",      "Light / dark",
                            "Easy",     "Help",  "Top scores", "1",
                            "2",        "3",     "4",          "5",
                            "6",        "7",     "8",          "9",
                            "Erase",    "Close", "Save name",  "New game"};
    for (int i = 0; i < 20; ++i) {
        buttons_[i] =
            gf::make_control<GameButton>(gf::StableId("sudoku." + std::to_string(i)), labels[i]);
        (*buttons_[i]).set_accessible_name(labels[i]);
        (*std::static_pointer_cast<GameButton>(buttons_[i]))
            .set_skin(game.dark ? ButtonSkin::slate : ButtonSkin::blue);
        add_child(buttons_[i]);
        subscriptions_.push_back(
            (*buttons_[i])
                .clicked()
                .subscribe(
                    *this,
                    gf::Delegate<gf::ButtonBase&>::bind<SudokuView, &SudokuView::action>(*this)));
        if (i >= 17)
            (*buttons_[i]).set_visible(false);
    }
    const char* levels[] = {"Easy", "Medium", "Hard"};
    (*buttons_[4]).set_text(levels[difficulty_]);
    name_ = gf::make_control<gf::TextBox>(gf::StableId("sudoku.name"), game.player_name);
    (*name_).set_maximum_length(24);
    (*name_).set_accessible_name("Your name for the top scores");
    (*name_).set_visible(false);
    add_child(name_);
}
void SudokuView::on_attached_to_window() {
    timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::milliseconds(16));
    subscriptions_.push_back((*timer_).tick().subscribe(
        *this, gf::Delegate<>::bind<SudokuView, &SudokuView::tick>(*this)));
}
void SudokuView::on_detaching_from_window(gf::Window&) noexcept {
    persist();
    if (timer_)
        (*timer_).stop();
    timer_.reset();
}
void SudokuView::activate() {
    Cabinet preferences;
    if (load_cabinet(cabinet_path(), preferences)) {
        sound_ = preferences.sound;
        music_ = preferences.music;
        reduced_ = preferences.reduced;
    }
    music_play(game.dark ? "sudoku_night" : "sudoku_day", music_);
    if (game.seed.empty() && !busy_)
        new_game();
    if (game.over && !game.recorded)
        panel(2);
    if (attached_window())
        static_cast<void>((*attached_window()).request_focus(shared_from_this()));
}
void SudokuView::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    double x = bounds.width - 780;
    const double widths[] = {103, 70, 83, 111, 103, 65, 115};
    for (int i = 0; i < 7; ++i) {
        set_child_layout(buttons_[i], {x, 17, widths[i], 34});
        x += widths[i] + 10;
    }
    cell_ = std::min(62.0, (bounds.height - 186) / 9);
    board_ = {(bounds.width - cell_ * 9) * .5, 91, cell_ * 9, cell_ * 9};
    for (int i = 7; i < 17; ++i)
        set_child_layout(buttons_[i], {bounds.width * .5 - 285 + (i - 7) * 57,
                                       board_.y + board_.height + 19, i == 16 ? 67.0 : 47.0, 38});
    popup_ = {bounds.width * .5 - 310, 104, 620, std::min(515.0, bounds.height - 122)};
    set_child_layout(buttons_[17], {popup_.x + 512, popup_.y + 18, 85, 32});
    set_child_layout(name_, {popup_.x + 28, popup_.y + 111, 255, 34});
    set_child_layout(buttons_[18], {popup_.x + 300, popup_.y + 111, 110, 34});
    set_child_layout(buttons_[19], {popup_.x + 475, popup_.y + popup_.height - 44, 120, 30});
}
void SudokuView::text(gf::Painter& p, double x, double y, const std::string& value, double size,
                      gf::Color color) {
    p.draw_text_utf8({x, y}, value, {gf::FontRole::control, size, 400, false}, color);
}
void SudokuView::on_paint(gf::Painter& p, gf::Rect) {
    gf::Rect b = client_rectangle();
    gf::Color background = game.dark ? gf::Color::rgba(16, 27, 45) : gf::Color::rgba(233, 243, 252);
    gf::Color paper = game.dark ? gf::Color::rgba(27, 43, 64) : gf::Color::rgba(255, 255, 255);
    gf::Color ink = game.dark ? gf::Color::rgba(222, 235, 250) : gf::Color::rgba(32, 56, 80);
    gf::Color blue = game.dark ? gf::Color::rgba(114, 192, 255) : gf::Color::rgba(22, 114, 188);
    p.fill_rect(b, background);
    paint_emblem(p, {28, 18, 34, 34}, 1);
    p.draw_text_utf8({80, 42}, "Sudoku", {gf::FontRole::control, 28, 700, false}, ink);
    p.draw_line({80, 54}, {120, 54}, blue, 2);
    p.draw_box_shadow(board_, 3, {0, 6}, 20, 0, gf::Color::rgba(20, 54, 87, 30));
    p.fill_rect(board_, paper);
    int focus = hover_ >= 0 ? hover_ : selected_;
    int match = focus >= 0 ? game.grid.values[focus] : 0;
    double elapsed =
        std::chrono::duration<double>(gf::FrameClock::now() - celebration_start_).count();
    for (int i = 0; i < 81; ++i) {
        gf::Rect r{board_.x + (i % 9) * cell_, board_.y + (i / 9) * cell_, cell_, cell_};
        bool same = match && game.grid.values[i] == match;
        bool cross = focus >= 0 && (i / 9 == focus / 9 || i % 9 == focus % 9);
        if (cross || same)
            p.fill_rect(r, game.dark ? gf::Color::rgba(61, 133, 191, same ? 115 : 45)
                                     : gf::Color::rgba(77, 155, 219, same ? 90 : 24));
        bool celebrate = false;
        for (int unit : celebrated_)
            celebrate = celebrate || (unit < 9    ? i / 9 == unit
                                      : unit < 18 ? i % 9 == unit - 9
                                                  : (i / 27) * 3 + (i % 9) / 3 == unit - 18);
        if (celebrate && !reduced_ && elapsed < .8) {
            double wave = std::max(0.0, 1 - std::abs(elapsed * 12 - (i % 9 + i / 9) * .4 - 1));
            p.fill_rect(r, gf::Color::rgba(96, 197, 243, static_cast<unsigned char>(wave * 150)));
        }
        int value = game.grid.values[i];
        if (value) {
            bool wrong = value != game.solution[i];
            text(p, r.x + cell_ * .34, r.y + cell_ * .66, std::to_string(value), cell_ * .47,
                 wrong            ? gf::Color::rgba(216, 74, 87)
                 : game.puzzle[i] ? ink
                                  : blue);
            if (wrong)
                p.draw_line({r.x + cell_ * .31, r.y + cell_ * .79},
                            {r.x + cell_ * .68, r.y + cell_ * .79}, gf::Color::rgba(216, 74, 87),
                            2);
        } else {
            for (int n = 1; n <= 9; ++n)
                if (game.grid.notes[i] & (1 << (n - 1)))
                    text(p, r.x + cell_ * (.11 + (n - 1) % 3 * .3),
                         r.y + cell_ * (.23 + (n - 1) / 3 * .3), std::to_string(n), cell_ * .20,
                         blue);
        }
    }
    for (int i = 0; i <= 9; ++i) {
        gf::Color line = i % 3 == 0  ? blue
                         : game.dark ? gf::Color::rgba(71, 94, 119)
                                     : gf::Color::rgba(191, 211, 231);
        p.draw_line({board_.x + i * cell_, board_.y},
                    {board_.x + i * cell_, board_.y + board_.height}, line, i % 3 == 0 ? 2 : 1);
        p.draw_line({board_.x, board_.y + i * cell_},
                    {board_.x + board_.width, board_.y + i * cell_}, line, i % 3 == 0 ? 2 : 1);
    }
    if (selected_ >= 0)
        p.stroke_rect({board_.x + (selected_ % 9) * cell_ + 2,
                       board_.y + (selected_ / 9) * cell_ + 2, cell_ - 4, cell_ - 4},
                      blue, 2);
    text(p, board_.x, 66, "Mistakes: " + std::to_string(game.errors), 13, ink);
    text(p, board_.x + board_.width - 184, 66,
         std::string(notes_ ? "Notes" : "Set digit") + " · " + std::to_string(digit_), 13, blue);
    text(p, 28, b.height - 27, busy_ ? "Generating a unique puzzle offline…" : game.message, 13,
         ink);
    if (!panel_)
        return;
    p.fill_rect(b, gf::Color::rgba(6, 18, 34, 135));
    p.fill_rounded_rect(popup_, 12, paper);
    p.draw_text_utf8({popup_.x + 28, popup_.y + 42},
                    panel_ == 1 ? "A quiet place to think" : "TOP SCORES",
                    {gf::FontRole::control, 25, 700, false}, ink);
    p.draw_line({popup_.x + 28, popup_.y + 54}, {popup_.x + 140, popup_.y + 54}, blue, 2);
    if (panel_ == 1) {
        const char* lines[] = {"Fill each row, column, and 3 × 3 box with digits 1–9.",
                               "Choose a digit below the board. Click to set; right click to note.",
                               "Keyboard: arrows move; type a digit to set it; N toggles notes.",
                               "Backspace erases. Z undoes. Mistakes stay counted after undo.",
                               "Hover a filled square to highlight its digit, row, and column.",
                               "Your puzzle saves after each change and resumes on reopening.",
                               "Difficulty selects the next puzzle; New game creates it offline.",
                               "Top scores keep ten names per difficulty, with fewest mistakes."};
        for (int i = 0; i < 8; ++i)
            text(p, popup_.x + 28, popup_.y + 89 + i * 39, lines[i], 14, ink);
    } else {
        const char* levels[] = {"Easy", "Medium", "Hard"};
        text(p, popup_.x + 28, popup_.y + 72,
             std::string(levels[game.difficulty]) + " · Fewest mistakes" +
                 (game.over ? " · Your result: " + std::to_string(game.errors) : ""),
             16, blue);
        if (!game.qualifies())
            text(p, popup_.x + 28, popup_.y + 120,
                 game.recorded ? "Your name is saved." : "Complete a puzzle to enter your name.",
                 14, ink);
        const std::vector<TopScore>& list = game.scores[game.difficulty];
        for (int i = 0; i < static_cast<int>(list.size()); ++i) {
            text(p, popup_.x + 28, popup_.y + 169 + i * 27, std::to_string(i + 1), 16, blue);
            text(p, popup_.x + 73, popup_.y + 169 + i * 27, list[i].name, 16, ink);
            text(p, popup_.x + 530, popup_.y + 169 + i * 27, std::to_string(list[i].value), 16,
                 blue);
        }
    }
}
void SudokuView::panel(int kind) {
    panel_ = kind;
    for (int i = 0; i < 17; ++i)
        (*buttons_[i]).set_enabled(!kind && !busy_);
    (*buttons_[17]).set_visible(kind != 0);
    (*buttons_[18]).set_visible(kind == 2 && game.qualifies());
    (*buttons_[19]).set_visible(kind == 2 && game.over);
    (*name_).set_visible(kind == 2 && game.qualifies());
    if (kind == 2)
        (*name_).set_text(game.player_name);
    if (attached_window())
        static_cast<void>(
            (*attached_window()).request_focus(kind ? buttons_[17] : shared_from_this()));
    invalidate(gf::Dirty::paint);
}
void SudokuView::new_game() {
    if (busy_)
        return;
    busy_ = true;
    std::string seed = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    generation_ = std::async(std::launch::async, SudokuJob{asset_directory(), seed, difficulty_});
    panel(0);
    if (timer_)
        (*timer_).start();
}
void SudokuView::tick() {
    if (busy_ && generation_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            Sudoku result = generation_.get();
            result.scores = game.scores;
            result.player_name = game.player_name;
            result.dark = game.dark;
            game = std::move(result);
            selected_ = 0;
            hover_ = -1;
            celebrated_.clear();
            persist();
            if (effectively_visible())
                music_play(game.dark ? "sudoku_night" : "sudoku_day", music_);
        } catch (const std::exception& error) {
            game.message = std::string("Could not generate this difficulty: ") + error.what();
        }
        busy_ = false;
        panel(0);
    }
    if (!celebrated_.empty() &&
        std::chrono::duration<double>(gf::FrameClock::now() - celebration_start_).count() >= .8)
        celebrated_.clear();
    if (!busy_ && celebrated_.empty() && timer_)
        (*timer_).stop();
    invalidate(board_);
}
void SudokuView::persist() {
    if (!game.seed.empty() && !game.save(sudoku_path()))
        game.message = "Your puzzle is in memory; the local save could not be written.";
}
void SudokuView::edit(int digit, bool note) {
    if (busy_ || panel_ || game.seed.empty())
        return;
    std::array<bool, 27> before{};
    for (int i = 0; i < 27; ++i)
        before[i] = game.complete_unit(i);
    int errors_before = game.errors;
    int note_before = game.grid.notes[selected_];
    if (!game.set(selected_, digit, note))
        return;
    celebrated_.clear();
    for (int i = 0; i < 27; ++i)
        if (!before[i] && game.complete_unit(i))
            celebrated_.push_back(i);
    celebration_start_ = gf::FrameClock::now();
    if (!reduced_ && !celebrated_.empty() && timer_)
        (*timer_).start();
    persist();
    if (game.over) {
        sound_play("stinger_win_sudoku", sound_);
        panel(2);
    } else if (game.errors > errors_before)
        sound_play("sudoku_error", sound_);
    else if (!celebrated_.empty())
        sound_play(celebrated_.front() < 9    ? "sudoku_row_complete"
                   : celebrated_.front() < 18 ? "sudoku_column_complete"
                                              : "sudoku_box_complete",
                   sound_);
    else
        sound_play(digit == 0 ? "sudoku_erase"
                   : note     ? (game.grid.notes[selected_] < note_before ? "sudoku_note_remove"
                                                                          : "sudoku_note_place")
                              : "sudoku_digit_place",
                   sound_);
    invalidate(gf::Dirty::paint);
}
void SudokuView::action(gf::ButtonBase& button) {
    int index = std::stoi(std::string(button.stable_id().value().substr(7)));
    if (index == 0 || index == 19)
        new_game();
    if (index == 1) {
        game.undo();
        persist();
    }
    if (index == 2) {
        notes_ = !notes_;
        (*buttons_[2]).set_text(notes_ ? "Notes on" : "Notes");
    }
    if (index == 3) {
        game.dark = !game.dark;
        set_theme_override(games_theme(game.dark ? ButtonSkin::slate : ButtonSkin::blue));
        for (const std::shared_ptr<gf::Button>& button : buttons_)
            (*std::static_pointer_cast<GameButton>(button))
                .set_skin(game.dark ? ButtonSkin::slate : ButtonSkin::blue);
        persist();
        music_play(game.dark ? "sudoku_night" : "sudoku_day", music_);
    }
    if (index == 4) {
        difficulty_ = (difficulty_ + 1) % 3;
        const char* levels[] = {"Easy", "Medium", "Hard"};
        (*buttons_[4]).set_text(levels[difficulty_]);
    }
    if (index == 5)
        panel(1);
    if (index == 6)
        panel(2);
    if (index >= 7 && index <= 15)
        digit_ = index - 6;
    if (index == 16)
        edit(0, false);
    if (index == 17)
        panel(0);
    if (index == 18) {
        if (game.record(std::string((*name_).text()))) {
            persist();
            panel(2);
        }
    }
    if (!panel_ && attached_window())
        static_cast<void>((*attached_window()).request_focus(shared_from_this()));
    invalidate(gf::Dirty::paint);
}
void SudokuView::on_pointer(gf::PointerEvent& e) {
    const gf::Point local_position = point_from_window(e.position);
    if (panel_ || busy_)
        return;
    int cell = -1;
    if (local_position.x >= board_.x && local_position.y >= board_.y &&
        local_position.x < board_.x + board_.width && local_position.y < board_.y + board_.height)
        cell = static_cast<int>((local_position.x - board_.x) / cell_) +
               9 * static_cast<int>((local_position.y - board_.y) / cell_);
    if (e.action == gf::PointerAction::move) {
        if (hover_ != cell) {
            hover_ = cell;
            invalidate(board_);
        }
    }
    if (e.action == gf::PointerAction::down && cell >= 0) {
        selected_ = cell;
        if (attached_window())
            static_cast<void>((*attached_window()).request_focus(shared_from_this()));
        edit(digit_, notes_ || e.button == gf::PointerButton::secondary);
        invalidate(board_);
        e.handled = true;
    }
}
void SudokuView::on_key_bubble(gf::KeyEvent& e) {
    on_key(e);
}
void SudokuView::on_key(gf::KeyEvent& e) {
    if (e.handled || e.action != gf::KeyAction::down)
        return;
    if (e.physical_key == gf::PhysicalKey::escape) {
        panel(0);
        e.handled = true;
        return;
    }
    if (panel_ || busy_)
        return;
    if (e.physical_key == gf::PhysicalKey::left)
        selected_ = std::max(0, selected_ - 1);
    else if (e.physical_key == gf::PhysicalKey::right)
        selected_ = std::min(80, selected_ + 1);
    else if (e.physical_key == gf::PhysicalKey::up)
        selected_ = std::max(0, selected_ - 9);
    else if (e.physical_key == gf::PhysicalKey::down)
        selected_ = std::min(80, selected_ + 9);
    else if (e.physical_key == gf::PhysicalKey::n)
        notes_ = !notes_;
    else if (e.physical_key == gf::PhysicalKey::z) {
        game.undo();
        persist();
    } else if (e.physical_key == gf::PhysicalKey::backspace ||
               e.physical_key == gf::PhysicalKey::delete_forward)
        edit(0, false);
    else
        return;
    hover_ = -1;
    invalidate(gf::Dirty::paint);
    e.handled = true;
}
void SudokuView::on_text_input(gf::TextInputEvent& e) {
    if (e.text_utf8.size() == 1 && e.text_utf8[0] >= '1' && e.text_utf8[0] <= '9' && !panel_) {
        digit_ = e.text_utf8[0] - '0';
        edit(digit_, notes_);
        e.handled = true;
    }
}
} // namespace games
