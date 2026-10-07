#pragma once
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
    void switch_game(Kind kind);
    std::array<std::shared_ptr<gf::Button>, 22> buttons_{};
    std::vector<gf::SubscriptionToken> subscriptions_;
    std::array<gf::ImageId, 52> faces_{};
    std::array<gf::ImageId, 4> backs_{};
    gf::ImageId felt_{};
    gf::ImageId shadow_{};
    // Lighting and texture laid over every card, rendered at the cards' device size.
    gf::ImageId finish_{};
    int finish_width_ = 0, finish_height_ = 0;
    void refresh_finish();
    std::array<Sprite, 104> sprites_{};
    std::array<gf::Rect, 20> slots_{};
    std::vector<int> pass_cards_;
    std::unique_ptr<gf::Timer> timer_;
    gf::FrameTime animation_start_{}, ai_due_{};
    bool pointer_down_ = false, dealing_ = false, waiting_ai_ = false;
    int keyboard_card_ = 0;
    int pending_from_ = -1, pending_index_ = -1, pending_to_ = -1;
    bool animating_ = false, dragging_ = false, reduced_ = false, sound_ = true, music_ = true;
    int selection_ = -1, selected_index_ = -1, hover_ = -1, back_ = 0, panel_ = 0, topic_ = 0,
        keyboard_slot_ = 0;
    double card_w_ = 105, card_h_ = 147, spread_ = 29;
    gf::Point press_{}, pointer_{};
    gf::Rect popup_{};
    std::uint32_t next_seed_ = 0;
    // Easy, Medium or Hard for Solitaire, Spider and FreeCell. Every deal is drawn from the
    // verified winnable tables (deal_tables.cpp); `picks_` walks each table. Both are kept
    // beside the cabinet in card-levels.txt.
    std::array<int, 3> levels_{1, 1, 1};
    std::array<std::uint32_t, 3> picks_{};
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
    int hit_slot(gf::Point point) const;
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
    void start_cascade();
    void step_cascade();
    void finish_cascade();
};
} // namespace games
