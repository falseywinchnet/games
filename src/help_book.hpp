#pragma once
#include "gui_forms/controls/scrollable_control/scrollable_control.hpp"
#include "suite.hpp"
#include <functional>
namespace games {
// Visually a text link; retains keyboard and accessibility semantics.
class HelpGlyph final : public gf::Button {
  public:
    HelpGlyph(gf::StableId id, std::string glyph);
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
};
class HelpPages final : public gf::ScrollableControl {
  public:
    explicit HelpPages(gf::StableId id);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void arrange(gf::Rect bounds) override;
    void on_key_bubble(gf::KeyEvent& event) override;
    void select(int entry, std::string_view topic);

  private:
    std::vector<std::shared_ptr<gf::Button>> headings_;
    std::vector<std::shared_ptr<gf::Label>> bodies_;
    std::vector<gf::SubscriptionToken> subscriptions_;
    struct TopicKey {
        int entry;
        std::string topic;
    };
    std::vector<TopicKey> topics_;
    std::vector<int> order_;
    void add_topic(int entry, std::string topic, std::string title, std::string_view body);
    void toggle(gf::ButtonBase& button);
};
class HelpBook final : public gf::Control {
  public:
    explicit HelpBook(gf::StableId id);
    static constexpr bool initialize_tree_after_construction = true;
    void initialize_control_tree();
    void arrange(gf::Rect bounds) override;
    void on_paint(gf::Painter& painter, gf::Rect damage) override;
    void select(int entry, std::string_view topic);
    void on_key_bubble(gf::KeyEvent& event) override;
    std::function<void()> close;

  private:
    std::shared_ptr<HelpPages> pages_;
    std::shared_ptr<HelpGlyph> close_;
    gf::SubscriptionToken close_subscription_;
    void clicked_close(gf::ButtonBase& button);
};
} // namespace games
