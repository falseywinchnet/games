#pragma once
#include "card_print.hpp"
#include "dealer.hpp"
#include "game.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include "gui_forms/timer.hpp"
#include "storage.hpp"
#include "suite.hpp"
#include "suite_settings.hpp"
#include <array>
#include <chrono>
namespace games {
namespace gf = gui_forms;
struct Sprite {
    gf::Rect rect{}, start{}, target{};
    Card card{};
    int pile = -1, index = -1;
    bool visible = false, prior_up = false, flipping = false, moving = false;
    double delay = 0, duration = .38, flip_progress = 1;
    double lift = 0;  // 0 on the table .. 1 at the top of its arc, as it moves
    double arc = .45; // how high a move that does not turn the card rises (a short one barely)
};
class Table final : public gf::Control, public CommandSource {
  public:
    explicit Table(gf::StableId id);
    static constexpr bool initialize_tree_after_construction = true;

    void initialize_control_tree();
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
    void on_pointer(gf::PointerEvent& event) override;
    void on_key(gf::KeyEvent& event) override;
    void on_key_preview(gf::KeyEvent& event) override;
    void on_key_bubble(gf::KeyEvent& event) override;
    Game game;
    void activate();
    void reload_preferences();
    void load_back();
    // Shows one of the four card games, resuming its saved deal.
    void show_kind(Kind kind);
    [[nodiscard]] std::vector<GameCommand> commands() const override;
    void run_command(std::string_view id) override;
    // Draw (Solitaire), Suits (Spider) and the deal level, on the Settings screen.
    // Motion, Sound, Music and the card back are the PlaySuite masters.
    [[nodiscard]] std::vector<GameSetting> settings() const override;
    void change_setting(std::string_view id, double value) override;
    [[nodiscard]] bool uses_card_backs() const override {
        return true;
    }

  private:
    Cabinet cabinet_;
    std::shared_ptr<gf::TextBox> score_name_;
    bool pending_score() const;
    void show_result();
    void persist();
    // A change to keep: written once the cards are at rest (or the table is put away), so
    // a slow first write (a cold Windows disk, a virus scan of the new file) never holds a
    // card in the air.
    void save_soon();
    void switch_game(Kind kind);
    std::array<std::shared_ptr<gf::Button>, 23> buttons_{};
    std::vector<gf::SubscriptionToken> subscriptions_;
    std::array<gf::ImageId, 52> faces_{};      // the classic faces (authoring/cards1902)
    std::array<gf::ImageId, card_back_count> backs_{};
    gf::ImageId felt_{};
    gf::ImageId shadow_{};
    // The paper (card_print.hpp) at the cards' device size, and laid over a face drawn the old
    // way while its picture is not made yet.
    CardPaper paper_;
    gf::ImageId finish_{};
    double scale_ = 1;  // device pixels to a point
    // The room's light on a card's face, by where it lies (table_light.hpp); made again only
    // when the table or its cards change size.
    gf::ImageId light_{};
    TableLight light_grid_;
    double light_width_ = 0, light_height_ = 0, light_card_ = 0;
    void refresh_light();
    int finish_width_ = 0, finish_height_ = 0;
    void refresh_finish();
    // Each card's finished picture (card_print.hpp): printed at rest where it lies, at whole
    // device pixels, and copied to the screen as it is; moved as it is while the card is in
    // motion, and printed again where it lands.
    struct Printed {
        gf::ImageId image{};
        int width = 0, height = 0;  // device pixels
        int x = 0, y = 0;           // where it was printed, device pixels in the table
        int card = -1;              // the card's id
        bool up = false;
        int design = -1;            // the back it shows, face down
        int generation = -1;        // print_generation_ when printed
    };
    std::array<Printed, 104> printed_{};
    Printed glinted_{};           // the card under the pointer, glinting
    int print_generation_ = 0;    // counts changes of the paper and the light
    std::array<CardArt, 52> face_art_{};  // at the paper's size, made as wanted
    std::array<bool, 52> face_failed_{};
    CardArt back_art_, shadow_art_;
    int back_art_design_ = -1;
    bool back_failed_ = false;
    // The shadow at the cards' device size, at rest and in the hand.
    gf::ImageId rest_shadow_{}, lifted_shadow_{};
    gf::FrameTime resized_at_{};  // when the cards last changed size (printed once it settles)
    [[nodiscard]] bool picture_shows(const Printed& printed, const Card& card, bool up) const;
    // Prints every card whose picture is out of date (the card under the pointer glinting),
    // every core lending a hand: a move's few cards in a few milliseconds, a deck in tens.
    void print_stale();
    void keep_print(Printed& printed, const Sprite& sprite, std::span<const std::byte> pixels);
    void forget_pictures(gf::Window& window);
    std::array<Sprite, 104> sprites_{};
    std::array<gf::Rect, 20> slots_{};
    std::vector<int> pass_cards_;
    std::unique_ptr<gf::Timer> timer_;
    gf::FrameTime animation_start_{}, ai_due_{};
    bool pointer_down_ = false, dealing_ = false, waiting_ai_ = false;
    int keyboard_card_ = 0;
    int pending_from_ = -1, pending_index_ = -1, pending_to_ = -1;
    bool animating_ = false, dragging_ = false, reduced_ = false, sound_ = true, music_ = true;
    bool save_due_ = false;  // a change not yet written (save_soon)
    int selection_ = -1, selected_index_ = -1, hover_ = -1, back_ = 0, panel_ = 0, topic_ = 0,
        keyboard_slot_ = 0;
    double card_w_ = 105, card_h_ = 147, spread_ = 29;
    gf::Point press_{}, pointer_{};
    gf::Rect popup_{};
    std::uint32_t next_seed_ = 0;
    // Easy, Medium or Hard for Solitaire, Spider and FreeCell, kept beside the cabinet in
    // card-levels.txt. Every deal is made fresh by the dealer, winnable and of its level
    // (solitaire_solver.hpp); while one is being made, `waiting_deal_` is the game waiting.
    std::array<int, 3> levels_{1, 1, 1};
    Dealer dealer_{cabinet_path().parent_path() / "card-deals.txt"};
    int waiting_deal_ = -1;
    void want_deals();
    void load_levels();
    void set_level(int level);
    void set_deal_option(int option);
    void save_levels() const;
    [[nodiscard]] std::uint32_t deal_seed(Kind kind, int option);
    [[nodiscard]] std::string level_name() const;
    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void action(gf::ButtonBase& button);
    void tick();
    void layout_cards(bool animate);
    void changed(const char* sound);
    void new_game(Kind kind);
    void select_or_move(int pile, int index);
    void heart_card(int index);
    void heart_continue();
    void open_panel(int panel);
    void draw_card(gf::Painter& painter, const Sprite& sprite, bool selected);
    void draw_panel(gf::Painter& painter);
    void draw_text(gf::Painter& painter, double x, double y, const std::string& text, double size,
                   gf::Color color);
    int hit_card(gf::Point point) const;
    // The run under the pointer, when it can go somewhere: its cards faintly brighten.
    int lift_pile_ = -1, lift_index_ = -1;      // a card that can move, under the pointer (with its run)
    int glint_card_ = -1;                        // any face-up card under the pointer, by id
    bool move_glow_ = false, glint_ = true;     // SuiteSettings::card_move_glow, card_glint
    // SuiteSettings::card_emboss, card_emboss_size, card_sparkle, card_grain, card_glare
    double emboss_ = .4, emboss_size_ = .45, sparkle_ = 1, grain_ = .4, glare_ = .7;
    // Face-up cards in play that cannot move now (by card id): drawn dimmed. Worked out
    // at each paint, so a computer's turn or a new deal is always reflected.
    [[nodiscard]] bool movable(int pile, int index) const;
    void track_hover(gf::Point point);
    int hit_slot(gf::Point point) const;
    // Where the run in the hand would go if let go now: the legal pile it overlaps most.
    [[nodiscard]] int drop_target() const;
    [[nodiscard]] gf::Rect landing(int pile) const;
    // How far apart Spider's waiting deals lie, each a small stack of backs.
    [[nodiscard]] double deal_step() const {
        return card_w_ * .14;
    }
    void request_tick();
    struct Bouncer {
        Card card;
        double x = 0, y = 0, vx = 0, vy = 0, sample = 0;
        std::array<gf::Point, 9> trail{};
    };
    std::vector<Bouncer> bouncers_;
    std::vector<Card> launch_queue_;
    std::array<bool, 104> launched_{};
    bool cascading_ = false;
    double launch_timer_ = 0;
    std::chrono::steady_clock::time_point cascade_last_{};
    // A game whose win is certain: nothing face down, nothing left in the stock (and in
    // FreeCell every column already in order), and the solver finds the line at once.
    // "Solve?" then plays that line out, a move every tenth of a second.
    bool solvable_ = false;
    std::vector<SolverStep> autoplay_;
    std::size_t autoplay_next_ = 0;
    gf::FrameTime autoplay_due_{};
    void update_solvable();
    void show_solve();
    void start_cascade();
    void step_cascade();
    void finish_cascade();
};
} // namespace games
