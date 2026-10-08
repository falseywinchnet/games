#include "help_book.hpp"
#include "help_content.hpp"
#include "game_module.hpp"
#include "open_link.hpp"
#include "presentation.hpp"
#include <algorithm>
namespace games {
namespace {
std::shared_ptr<gf::Label> front_line(const std::string& id, std::string_view text, std::uint16_t weight) {
    std::shared_ptr<gf::Label> label =
        gf::make_control<gf::Label>(gf::StableId("help.front." + id), std::string(text));
    (*label).set_font({gf::FontRole::content, 16, weight, false});
    (*label).set_foreground(gf::Color::rgba(42, 38, 24));
    (*label).set_text_wrapping(gf::TextWrapping::word);
    return label;
}
// A web link whose underline is measured from the text it draws.
class WebLink final : public gf::LinkLabel {
  public:
    WebLink(gf::StableId id, std::string text) : LinkLabel(std::move(id), std::move(text)) {}
    void on_paint(gf::Painter& p, gf::Rect) override {
        const gf::Rect r = client_rectangle();
        const gf::Color ink = hovered_visual() ? gf::Color::rgba(20, 60, 150)
                              : visited()      ? gf::Color::rgba(92, 52, 140)
                                               : gf::Color::rgba(28, 82, 176);
        const double width = p.measure_text_utf8(text(), font()).width;
        const double baseline = (r.height + font().size) * .5 - 1;
        p.draw_text_utf8({2, baseline}, text(), font(), ink);
        p.draw_line({2, baseline + 2}, {2 + width, baseline + 2}, ink, 1);
        if (focus_cue_visible())
            p.stroke_rect({0, 0, r.width, r.height}, ink, 1);
    }
};
class TopicHeading final : public gf::Button {
  public:
    TopicHeading(gf::StableId id, std::string title) : Button(std::move(id), std::move(title)) {
        set_font({gf::FontRole::content, 18, 600, false});
        set_accessible_name(text());
        // A disclosure: assistive technology hears whether its topic is open.
        set_expanded_state(false);
    }
    [[nodiscard]] bool expanded() const {
        return expanded_state().value_or(false);
    }
    void on_paint(gf::Painter& p, gf::Rect) override {
        const gf::Rect r = client_rectangle();
        p.fill_rect(r, hovered_visual() ? gf::Color::rgba(236, 226, 156)
                                        : gf::Color::rgba(244, 235, 175));
        p.draw_text_utf8({9, 25}, expanded() ? "−" : "+", font(), gf::Color::rgba(66, 58, 33));
        p.draw_text_utf8({32, 25}, text(), font(), gf::Color::rgba(42, 38, 24));
        if (focus_cue_visible())
            p.stroke_rect({1, 1, r.width - 2, r.height - 2}, gf::Color::rgba(110, 91, 37), 1);
    }
};
} // namespace
HelpGlyph::HelpGlyph(gf::StableId id, std::string glyph) : Button(std::move(id), std::move(glyph)) {
    set_font({gf::FontRole::content, 26, 600, false});
}
void HelpGlyph::set_ink(gf::Color ink) {
    ink_ = ink;
    invalidate(gf::Dirty::paint);
}
void HelpGlyph::on_paint(gf::Painter& p, gf::Rect) {
    const gf::Rect r = client_rectangle();
    const gf::Size size = p.measure_text_utf8(text(), font());
    const gf::Point at{(r.width - size.width) * .5, (r.height - size.height) * .5 + size.height * .8};
    // A soft dark edge keeps the glyph readable over bright game art.
    p.draw_text_utf8({at.x + 1, at.y + 1.5}, text(), font(), gf::Color::rgba(0, 0, 0, 110));
    p.draw_text_utf8(at, text(), font(), hovered_visual() ? gf::Color::rgba(255, 224, 140) : ink_);
    if (hovered_visual() || focus_cue_visible())
        p.draw_line({8, r.height - 3}, {r.width - 8, r.height - 3}, ink_, 1);
}
HelpPages::HelpPages(gf::StableId id) : ScrollableControl(std::move(id)) {
    set_auto_scroll(true);
    set_paint_plane(gf::PaintPlane::overlay);
}
void HelpPages::add_topic(int entry, std::string topic, std::string title,
                          std::string_view body) {
    const int index = static_cast<int>(topics_.size());
    const std::string key = std::to_string(entry) + "." + (topic.empty() ? "rules" : topic);
    std::shared_ptr<TopicHeading> heading =
        gf::make_control<TopicHeading>(gf::StableId("help.topic." + key), std::move(title));
    std::shared_ptr<gf::Label> label = gf::make_control<gf::Label>(
        gf::StableId("help.body." + key), std::string(body));
    (*label).set_font({gf::FontRole::content, 17, 400, false});
    (*label).set_foreground(gf::Color::rgba(42, 38, 24));
    (*label).set_text_wrapping(gf::TextWrapping::word);
    (*label).set_visible(false);
    (*heading).set_paint_plane(gf::PaintPlane::overlay);
    (*label).set_paint_plane(gf::PaintPlane::overlay);
    gf::on((*heading).clicked(), *this, &HelpPages::toggle, index);
    topics_.push_back({entry, std::move(topic)});
    headings_.push_back(heading);
    bodies_.push_back(label);
    order_.push_back(index);
    add_child(heading);
    add_child(label);
}
void HelpPages::initialize_control_tree() {
    dedication_ = front_line("dedication", dedication, 600);
    credits_ = front_line("credits", creators, 400);
    contact_ = front_line("contact", "Contact: " + std::string(contact) + "  ·", 400);
    sponsor_ = front_line("sponsor", sponsor, 400);
    website_ = gf::make_control<WebLink>(gf::StableId("help.front.website"),
                                         std::string(website));
    (*website_).set_font({gf::FontRole::content, 16, 400, false});
    (*website_).set_accessible_name("Open " + std::string(website) + " in the browser");
    gf::on((*website_).clicked(), *this, &HelpPages::visit_website);
    for (const std::shared_ptr<gf::Control>& line :
         {std::shared_ptr<gf::Control>(dedication_), std::shared_ptr<gf::Control>(credits_),
          std::shared_ptr<gf::Control>(contact_), std::shared_ptr<gf::Control>(website_),
          std::shared_ptr<gf::Control>(sponsor_)}) {
        (*line).set_paint_plane(gf::PaintPlane::overlay);
        add_child(line);
    }
    add_topic(-1, "", "Using PlaySuite", help_welcome);
    for (Entry entry : entries) {
        const GameDescriptor& descriptor = game_descriptor(entry);
        add_topic(static_cast<int>(entry), "", entry_info(entry).title, help_text(entry));
        for (const HelpTopic& topic : descriptor.help_topics)
            add_topic(static_cast<int>(entry), topic.id, topic.title, topic.text);
    }
    add_topic(-1, "about", "About PlaySuite", help_about());
}
void HelpPages::visit_website() {
    open_link("https://" + std::string(website) + "/");
}
void HelpPages::select(int entry, std::string_view topic) {
    int selected = 0;
    const bool about = topic == "about";
    const std::string_view requested = topic == "rules" ? "" : topic;
    // Missing modules and unknown topic ids fall back to their game's main help,
    // then to the welcome page. Numeric Entry values are persistent identifiers.
    for (std::size_t i = 0; i < topics_.size(); ++i) {
        const TopicKey& key = topics_[i];
        if (!about && key.entry == entry && key.topic.empty())
            selected = static_cast<int>(i);
        if ((about && key.entry == -1 && key.topic == "about") ||
            (!about && key.entry == entry && key.topic == requested)) {
            selected = static_cast<int>(i);
            break;
        }
    }
    order_.clear();
    order_.push_back(selected);
    for (int i = 0; i < static_cast<int>(bodies_.size()); ++i) {
        (*bodies_[i]).set_visible(i == selected);
        (*headings_[i]).set_expanded_state(i == selected);
        (*headings_[i]).invalidate(gf::Dirty::paint);
        if (i != selected)
            order_.push_back(i);
    }
    static_cast<void>(scroll_to({0, 0}));
    invalidate(gf::Dirty::layout | gf::Dirty::paint);
}
void HelpPages::toggle(int index) {
    const std::size_t i = static_cast<std::size_t>(index);
    const bool expanded = !(*bodies_[i]).visible();
    (*bodies_[i]).set_visible(expanded);
    (*headings_[i]).set_expanded_state(expanded);
    (*headings_[i]).invalidate(gf::Dirty::paint);
    invalidate(gf::Dirty::layout | gf::Dirty::paint);
}
void HelpPages::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    const double width = std::max(1.0, bounds.width - 24);
    std::vector<std::pair<std::shared_ptr<gf::Control>, gf::Rect>> layout;
    double y = 0;
    for (const std::shared_ptr<gf::Label>& line : {dedication_, credits_}) {
        const double height = (*line).measure({width - 16, 100000}).height;
        layout.push_back({line, {8, y, width - 16, height}});
        y += height + 2;
    }
    const double contact_width = (*contact_).measure({width - 16, 100000}).width;
    const double line_height = (*contact_).measure({width - 16, 100000}).height;
    layout.push_back({contact_, {8, y, contact_width, line_height}});
    layout.push_back({website_, {8 + contact_width + 2, y, 180, line_height}});
    y += line_height + 2;
    layout.push_back({sponsor_, {8, y, width - 16, line_height}});
    y += line_height + 18;
    for (int index : order_) {
        layout.push_back({headings_[index], {0, y, width, 36}});
        y += 44;
        if ((*bodies_[index]).visible()) {
            const double height =
                std::max(24.0, (*bodies_[index]).measure({width - 16, 100000}).height);
            layout.push_back({bodies_[index], {8, y, width - 16, height}});
            y += height + 20;
        }
    }
    arrange_scroll_viewport({bounds.width, bounds.height}, {width, y});
    for (const std::shared_ptr<gf::Control>& child : children())
        (*child).set_paint_plane(gf::PaintPlane::overlay);
    const gf::Point offset = scroll_position();
    for (const std::pair<std::shared_ptr<gf::Control>, gf::Rect>& item : layout) {
        gf::Rect r = item.second;
        r.y -= offset.y;
        set_child_layout(item.first, r);
    }
}
void HelpPages::on_key_bubble(gf::KeyEvent& e) {
    if (e.handled || e.action != gf::KeyAction::down)
        return;
    const double page = std::max(40.0, client_rectangle().height - 40);
    if (e.physical_key == gf::PhysicalKey::page_down ||
        e.physical_key == gf::PhysicalKey::page_up) {
        static_cast<void>(
            scroll_by({0, e.physical_key == gf::PhysicalKey::page_down ? page : -page}));
        e.handled = true;
    }
}
HelpBook::HelpBook(gf::StableId id) : Control(std::move(id)) {
    set_paint_plane(gf::PaintPlane::overlay);
    set_theme_override(games_theme(ButtonSkin::ivory));
}
void HelpBook::initialize_control_tree() {
    pages_ = gf::make_control<HelpPages>(gf::StableId("help.pages"));
    close_ = gf::make_control<HelpGlyph>(gf::StableId("help.close"), "×");
    (*close_).set_paint_plane(gf::PaintPlane::overlay);
    (*close_).set_accessible_name("Close PlaySuite help (H or Escape)");
    gf::on((*close_).clicked(), *this, &HelpBook::clicked_close);
    add_child(pages_);
    add_child(close_);
}
void HelpBook::on_key_bubble(gf::KeyEvent& event) {
    (*pages_).on_key_bubble(event);
}
void HelpBook::clicked_close() {
    if (close)
        close();
}
void HelpBook::select(int entry, std::string_view topic) {
    (*pages_).select(entry, topic);
}
void HelpBook::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    set_child_layout(close_, {bounds.width - 48, 9, 36, 36});
    set_child_layout(pages_, {20, 62, bounds.width - 36, bounds.height - 82});
}
void HelpBook::on_paint(gf::Painter& p, gf::Rect) {
    const gf::Rect b = client_rectangle();
    p.fill_rect(b, gf::Color::rgba(255, 252, 215));
    p.stroke_rect(b, gf::Color::rgba(153, 137, 78), 1);
    p.draw_text_utf8({20, 36}, "PlaySuite · Help", {gf::FontRole::content, 24, 600, false},
                     gf::Color::rgba(42, 38, 24));
    p.draw_line({20, 50}, {b.width - 20, 50}, gf::Color::rgba(207, 193, 133), 1);
}
} // namespace games
