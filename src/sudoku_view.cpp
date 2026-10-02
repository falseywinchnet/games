#include "sudoku_view.hpp"
#include "audio.hpp"
#include "gui_forms/window.hpp"
#include "presentation.hpp"
#include "storage.hpp"
#include "suite.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
namespace games {
// The PlaySuite capsule floats above the board.
static constexpr double kTop = 54;
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
        std::shared_ptr<SuiteButton> button =
            gf::make_control<SuiteButton>(gf::StableId("sudoku." + std::to_string(i)), labels[i],
                                          game.dark ? GlossTone::smoke : GlossTone::chrome);
        (*button).set_radius(i >= 7 && i <= 16 ? 6 : 4);
        (*button).set_font({gf::FontRole::content, i >= 7 && i <= 15 ? 18.0 : 14.0, 700, false});
        buttons_[i] = button;
        (*buttons_[i]).set_accessible_name(labels[i]);
        add_child(buttons_[i]);
        subscriptions_.push_back(
            (*buttons_[i])
                .clicked()
                .subscribe(
                    *this,
                    gf::Delegate<gf::ButtonBase&>::bind<SudokuView, &SudokuView::action>(*this)));
        // New game, Undo, Notes, theme, difficulty, Help and Top scores run from the capsule.
        if (i >= 17 || i < 7)
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
    refresh_pad();
    if (attached_window())
        static_cast<void>((*attached_window()).request_focus(shared_from_this()));
}
void SudokuView::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    // The board takes what the window allows below the capsule; the digit pad scales with it.
    const double pad_h = std::clamp(bounds.height * .06, 30.0, 38.0);
    cell_ = std::clamp(std::min((bounds.height - kTop - pad_h - 62) / 9, (bounds.width - 24) / 9),
                       22.0, 62.0);
    board_ = {std::round((bounds.width - cell_ * 9) * .5), kTop + 22, cell_ * 9, cell_ * 9};
    const double pad_w = std::min(580.0, bounds.width - 16), unit = pad_w / 10.2;
    for (int i = 7; i < 17; ++i)
        set_child_layout(buttons_[i], {(bounds.width - pad_w) * .5 + (i - 7) * unit,
                                       board_.y + board_.height + 10,
                                       i == 16 ? unit * 1.18 : unit * .84, pad_h});
    popup_ = {std::max(8.0, bounds.width * .5 - 310), kTop + 6, std::min(620.0, bounds.width - 16),
              std::min(515.0, bounds.height - kTop - 14)};
    set_child_layout(buttons_[17], {popup_.x + 512, popup_.y + 18, 85, 32});
    set_child_layout(name_, {popup_.x + 28, popup_.y + 111, 255, 34});
    set_child_layout(buttons_[18], {popup_.x + 300, popup_.y + 111, 110, 34});
    set_child_layout(buttons_[19], {popup_.x + 475, popup_.y + popup_.height - 44, 120, 30});
}
void SudokuView::text(gf::Painter& p, double x, double y, const std::string& value, double size,
                      gf::Color color) {
    p.draw_text_utf8({x, y}, value, {gf::FontRole::content, size, 600, false}, color);
}
// Finished digits are greyed on the pad; the chosen digit is gold.
void SudokuView::refresh_pad() {
    std::array<int, 10> placed{};
    for (int i = 0; i < 81; ++i)
        if (game.grid.values[i] && game.grid.values[i] == game.solution[i])
            ++placed[game.grid.values[i]];
    for (int n = 1; n <= 9; ++n) {
        std::shared_ptr<SuiteButton> button =
            std::static_pointer_cast<SuiteButton>(buttons_[static_cast<std::size_t>(n + 6)]);
        (*button).set_checked(n == digit_);
        const bool usable = placed[n] < 9 && !panel_ && !busy_;
        if ((*button).enabled() != usable)
            (*button).set_enabled(usable);
    }
}
void SudokuView::on_paint(gf::Painter& p, gf::Rect) {
    gf::Rect b = client_rectangle();
    const bool dark = game.dark;
    gf::Color paper = dark ? gf::Color::rgba(24, 36, 58) : gf::Color::rgba(255, 255, 253);
    gf::Color ink = dark ? gf::Color::rgba(226, 236, 250) : gf::Color::rgba(28, 40, 62);
    gf::Color blue = dark ? gf::Color::rgba(120, 196, 255) : gf::Color::rgba(28, 98, 190);
    // Day: blue-grey writing paper. Night: deep navy with a faint glow behind the board.
    fill_vertical(p, b, dark ? gf::Color::rgba(14, 22, 40) : gf::Color::rgba(226, 236, 248),
                  dark ? gf::Color::rgba(6, 10, 22) : gf::Color::rgba(196, 212, 232));
    const gf::GradientStop halo[] = {
        {0, dark ? gf::Color::rgba(60, 110, 180, 90) : gf::Color::rgba(255, 255, 255, 200)},
        {1, gf::Color::rgba(255, 255, 255, 0)}};
    p.fill_radial_gradient(b, {board_.x + board_.width * .5, board_.y + board_.height * .5},
                           {board_.width * .9, board_.height * .9}, halo);
    const gf::Rect card{board_.x - 8, board_.y - 8, board_.width + 16, board_.height + 16};
    p.draw_box_shadow(card, 8, {0, 8}, 24, 0, gf::Color::rgba(10, 30, 60, dark ? 150 : 70));
    p.fill_rounded_rect(card, 8, paper);
    p.stroke_rounded_rect(
        card, 8, dark ? gf::Color::rgba(80, 120, 170, 120) : gf::Color::rgba(160, 182, 210), 1);
    int focus = hover_ >= 0 ? hover_ : selected_;
    int match = focus >= 0 ? game.grid.values[focus] : 0;
    double elapsed =
        std::chrono::duration<double>(gf::FrameClock::now() - celebration_start_).count();
    for (int i = 0; i < 81; ++i) {
        gf::Rect r{board_.x + (i % 9) * cell_, board_.y + (i / 9) * cell_, cell_, cell_};
        // Alternate boxes carry a faint tint so the 3 × 3 structure reads at a glance.
        if (((i / 27) + (i % 9) / 3) % 2 == 1)
            p.fill_rect(r, dark ? gf::Color::rgba(255, 255, 255, 8)
                                : gf::Color::rgba(28, 98, 190, 10));
        bool same = match && game.grid.values[i] == match;
        bool cross = focus >= 0 && (i / 9 == focus / 9 || i % 9 == focus % 9 ||
                                    (i / 27 == focus / 27 && (i % 9) / 3 == (focus % 9) / 3));
        if (cross)
            p.fill_rect(r, dark ? gf::Color::rgba(90, 150, 220, 30)
                                : gf::Color::rgba(28, 98, 190, 18));
        if (same)
            p.fill_rect(r, dark ? gf::Color::rgba(90, 170, 255, 80)
                                : gf::Color::rgba(28, 98, 190, 46));
        if (i == selected_)
            p.fill_rect(r, dark ? gf::Color::rgba(255, 210, 122, 60)
                                : gf::Color::rgba(255, 214, 120, 110));
        bool celebrate = false;
        for (int unit : celebrated_)
            celebrate = celebrate || (unit < 9    ? i / 9 == unit
                                      : unit < 18 ? i % 9 == unit - 9
                                                  : (i / 27) * 3 + (i % 9) / 3 == unit - 18);
        if (celebrate && !reduced_ && elapsed < .8) {
            double wave = std::max(0.0, 1 - std::abs(elapsed * 12 - (i % 9 + i / 9) * .4 - 1));
            p.fill_rect(r, gf::Color::rgba(255, 214, 120, static_cast<unsigned char>(wave * 170)));
        }
        int value = game.grid.values[i];
        if (value) {
            bool wrong = value != game.solution[i];
            const gf::FontSpec f{gf::FontRole::content, cell_ * .54,
                                 static_cast<std::uint16_t>(game.puzzle[i] ? 700 : 600), false};
            const std::string digit = std::to_string(value);
            const gf::Size m = p.measure_text_utf8(digit, f);
            p.draw_text_utf8({r.x + (cell_ - m.width) * .5, r.y + cell_ * .7}, digit, f,
                             wrong            ? gf::Color::rgba(214, 64, 78)
                             : game.puzzle[i] ? ink
                                              : blue);
            if (wrong)
                p.draw_line({r.x + cell_ * .3, r.y + cell_ * .8},
                            {r.x + cell_ * .7, r.y + cell_ * .8}, gf::Color::rgba(214, 64, 78), 2);
        } else {
            const gf::FontSpec small{gf::FontRole::content, cell_ * .22, 600, false};
            for (int n = 1; n <= 9; ++n)
                if (game.grid.notes[i] & (1 << (n - 1)))
                    p.draw_text_utf8({r.x + cell_ * (.12 + (n - 1) % 3 * .3),
                                      r.y + cell_ * (.29 + (n - 1) / 3 * .3)},
                                     std::to_string(n), small,
                                     n == match ? blue
                                     : dark     ? gf::Color::rgba(150, 176, 210)
                                                : gf::Color::rgba(110, 128, 156));
        }
    }
    for (int i = 0; i <= 9; ++i) {
        const bool major = i % 3 == 0;
        gf::Color line =
            major ? (dark ? gf::Color::rgba(140, 190, 240) : gf::Color::rgba(34, 58, 96))
                  : (dark ? gf::Color::rgba(64, 86, 116) : gf::Color::rgba(196, 210, 228));
        p.draw_line({board_.x + i * cell_, board_.y},
                    {board_.x + i * cell_, board_.y + board_.height}, line, major ? 2.2 : 1);
        p.draw_line({board_.x, board_.y + i * cell_},
                    {board_.x + board_.width, board_.y + i * cell_}, line, major ? 2.2 : 1);
    }
    if (selected_ >= 0)
        p.stroke_rect({board_.x + (selected_ % 9) * cell_ + 1.5,
                       board_.y + (selected_ / 9) * cell_ + 1.5, cell_ - 3, cell_ - 3},
                      dark ? gf::Color::rgba(255, 210, 122) : gf::Color::rgba(200, 140, 30), 2);
    // Status pills above the board.
    const gf::FontSpec pill{gf::FontRole::content, 12, 700, false};
    const char* levels[] = {"EASY", "MEDIUM", "HARD"};
    const std::string left = std::string(levels[std::clamp(game.difficulty, 0, 2)]) +
                             "  ·  MISTAKES " + std::to_string(game.errors);
    p.draw_text_utf8({board_.x, board_.y - 16}, left, pill,
                     game.errors ? gf::Color::rgba(214, 64, 78) : ink);
    const std::string right =
        notes_ ? "NOTES ON  ·  DIGIT " + std::to_string(digit_) : "DIGIT " + std::to_string(digit_);
    const gf::Size rm = p.measure_text_utf8(right, pill);
    p.draw_text_utf8({board_.x + board_.width - rm.width, board_.y - 16}, right, pill, blue);
    p.draw_text_utf8({16, b.height - 10},
                     busy_ ? "Generating a unique puzzle offline…" : game.message,
                     {gf::FontRole::content, 13, 400, false}, ink);
    if (!panel_)
        return;
    p.fill_rect(b, gf::Color::rgba(6, 18, 34, 135));
    p.fill_rounded_rect(popup_, 12, paper);
    p.draw_text_utf8({popup_.x + 28, popup_.y + 42},
                     panel_ == 1 ? "A quiet place to think" : "TOP SCORES",
                     {gf::FontRole::content, 25, 700, false}, ink);
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
    refresh_pad();
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
    refresh_pad();
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
std::vector<GameCommand> SudokuView::commands() const {
    const char* levels[] = {"Easy", "Medium", "Hard"};
    return {{"new", "New game", !busy_, false, true},
            {"undo", "Undo", panel_ == 0},
            {"notes", "Notes", panel_ == 0, notes_},
            {"level", std::string("Next: ") + levels[difficulty_]},
            {"theme", game.dark ? "Day" : "Night"},
            {"help", "Help", true, panel_ == 1},
            {"scores", "Top scores", true, panel_ == 2}};
}
void SudokuView::run_command(std::string_view id) {
    const char* ids[] = {"new", "undo", "notes", "theme", "level", "help", "scores"};
    for (int i = 0; i < 7; ++i)
        if (id == ids[i]) {
            if ((i == 5 && panel_ == 1) || (i == 6 && panel_ == 2)) {
                panel(0);
                return;
            }
            if (i < 2)
                panel(0);
            action(*buttons_[static_cast<std::size_t>(i)]);
            return;
        }
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
            (*std::static_pointer_cast<SuiteButton>(button))
                .set_tone(game.dark ? GlossTone::smoke : GlossTone::chrome);
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
    refresh_pad();
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
