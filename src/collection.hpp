#pragma once
#include "capsule.hpp"
#include "game_module.hpp"
#include "gui_forms/timer.hpp"
#include "gui_forms/window.hpp"
#include "help_book.hpp"
#include "help_route.hpp"
#include "presentation.hpp"
#include "shelf.hpp"
#include <set>
#include "text_sprites.hpp"
namespace games {

// The PlaySuite shell: the shelf of boxed games, every game view, and the command
// capsule that floats over whichever game is open.
class Collection final : public gf::Control, public HelpHost {
  public:
    explicit Collection(gf::StableId id, bool dev = false);
    ~Collection() override;
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
    void on_pointer_preview(gf::PointerEvent& event) override;
    void on_key_preview(gf::KeyEvent& event) override;
    void activate();
    void open_entry(Entry entry);
    void show_shelf();
    void show_help(std::string_view topic = {}) override;
    void close_help();
    bool dispatch_action(std::string_view action) { return !shelf_open_ && (*games_.at(active_)).scripted_action(action); }
    void dispatch_command(const std::string& id) { run_command(id); }
    [[nodiscard]] bool help_open() const {
        return help_ && (*help_).visible();
    }
    [[nodiscard]] bool shelf_open() const {
        return shelf_open_;
    }
    [[nodiscard]] Entry active() const {
        return active_;
    }
    // Space reserved above games that present their own live surfaces.
    static constexpr double rail_height = 50;

  private:
    TextSprites sprites_;
    std::shared_ptr<ShelfView> shelf_;
    std::shared_ptr<CommandCapsule> capsule_;
    std::shared_ptr<HelpGlyph> help_link_;
    std::shared_ptr<HelpBook> help_;
    gf::FocusScopeId help_focus_{};
    ModuleContext modules_;
    std::map<Entry,std::unique_ptr<GameInstance>> games_;
    std::vector<gf::SubscriptionToken> subscriptions_;
    std::unique_ptr<gf::Timer> timer_{};
    std::chrono::steady_clock::time_point last_tick_{};
    double refresh_ = 0;
    Entry active_ = entries.front();
    bool shelf_open_ = true, reduced_ = false;
    std::set<int> opened_; // permanent IDs, including temporarily absent modules
    gf::Point pointer_{-1000, -1000};
    double capsule_width_limit_ = 0;
    double current_rail_height_ = rail_height;
    void on_attached_to_window() override;
    void on_detaching_from_window(gf::Window& window) noexcept override;
    void tick();
    void wake();
    void ensure_view(Entry entry);
    void visibility();
    void preferences();
    void persist() const;
    void refresh_commands();
    void run_command(const std::string& id);
    void toggle(int which);
    void clicked_help(gf::ButtonBase& button);
    [[nodiscard]] bool uses_rail(Entry entry) const;
    [[nodiscard]] std::shared_ptr<gf::Control> view(Entry entry) const;
    [[nodiscard]] CommandSource* source(Entry entry) const;
};
} // namespace games
