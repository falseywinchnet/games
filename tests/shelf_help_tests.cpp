#include "game_module.hpp"
#include "gui_forms/window.hpp"
#include "help_book.hpp"
#include "help_content.hpp"
#include "shelf.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace games;

static std::shared_ptr<gf::Control> find_control(gf::Control& root, const std::string& id) {
    for (const std::shared_ptr<gf::Control>& child : root.children()) {
        if ((*child).stable_id().value() == id)
            return child;
        const std::shared_ptr<gf::Control> nested = find_control(*child, id);
        if (nested)
            return nested;
    }
    return {};
}
static std::shared_ptr<gf::Control> box(gf::Control& root, Entry entry) {
    return find_control(root, "shelf.box." + std::to_string(static_cast<int>(entry)));
}
static void key(gf::Window& window, std::uint32_t physical) {
    gf::KeyEvent event;
    event.action = gf::KeyAction::down;
    event.physical_key = physical;
    assert(window.dispatch_key(event));
    window.perform_layout();
}
static void assert_revealed(gf::Control& control, const ShelfRows& rows) {
    const gf::Rect bounds = control.committed_arranged_bounds();
    const gf::Rect viewport = rows.client_rectangle();
    assert(bounds.y >= -.01);
    assert(bounds.bottom() <= viewport.height + .01);
}
int main() {
    TextSprites sprites;
    const gf::Size sizes[] = {{600, 420}, {1280, 900}};
    for (gf::Size size : sizes) {
        std::shared_ptr<ShelfView> shelf =
            gf::make_control<ShelfView>(gf::StableId("test.shelf"), sprites);
        (*shelf).set_reduced(true);
        gf::Window window(shelf, size);
        window.perform_layout();
        const std::shared_ptr<ShelfRows> rows =
            std::dynamic_pointer_cast<ShelfRows>(find_control(*shelf, "shelf.rows"));
        assert(rows);
        assert((*rows).children().size() == entries.size() + 2); // every box and two arrows
        // The credits live in help now; the shelf gives their room to the games.
        assert(!find_control(*shelf, "shelf.credits"));
        const std::shared_ptr<gf::Control> music = find_control(*shelf, "shelf.Music");
        assert(music);
        const gf::Rect music_bounds = (*music).committed_arranged_bounds();
        for (Entry entry : entries) {
            const std::shared_ptr<gf::Control> item = box(*shelf, entry);
            assert(item && (*item).committed_arranged_bounds().width >= 112);
            (*shelf).select(entry);
            (*shelf).focus_selection();
            window.perform_layout();
            assert((*shelf).selection() == entry);
            assert_revealed(*item, *rows);
        }
        assert((*music).committed_arranged_bounds() == music_bounds);
        // The keys follow the shelves' order: by category, then by title.
        assert((*rows).count() == entry_count);
        const Entry first = (*rows).entry_at(0);
        const Entry last = (*rows).entry_at(entry_count - 1);
        for (int i = 1; i < entry_count; ++i) {
            const EntryInfo& before = entry_info((*rows).entry_at(i - 1));
            const EntryInfo& after = entry_info((*rows).entry_at(i));
            assert(before.category <= after.category);
        }
        key(window, gf::PhysicalKey::home);
        assert((*shelf).selection() == first);
        assert_revealed(*box(*shelf, first), *rows);
        if (entries.size() > 1) {
            key(window, gf::PhysicalKey::right);
            assert((*shelf).selection() == (*rows).entry_at(1));
            key(window, gf::PhysicalKey::left);
            assert((*shelf).selection() == first);
            // Down: to the shelf below, the box nearest across; up again comes back.
            key(window, gf::PhysicalKey::down);
            const gf::Rect top = (*box(*shelf, first)).committed_arranged_bounds();
            const gf::Rect below = (*box(*shelf, (*shelf).selection())).committed_arranged_bounds();
            assert((*shelf).selection() == last || below.y > top.y);
            assert_revealed(*box(*shelf, (*shelf).selection()), *rows);
            key(window, gf::PhysicalKey::up);
            assert((*shelf).selection() == first);
        }
        key(window, gf::PhysicalKey::end);
        assert((*shelf).selection() == last);
        assert_revealed(*box(*shelf, last), *rows);
        key(window, gf::PhysicalKey::page_up);
        assert_revealed(*box(*shelf, (*shelf).selection()), *rows);
        key(window, gf::PhysicalKey::page_down);
        assert_revealed(*box(*shelf, (*shelf).selection()), *rows);
        // Tab focus also reveals a control independently of the selected index.
        assert(window.request_focus(box(*shelf, first)));
        window.perform_layout();
        assert((*shelf).selection() == first);
        assert_revealed(*box(*shelf, first), *rows);
        // The side arrows: dimmed toward an end with no more shelf; a press moves a shelf.
        const std::shared_ptr<gf::Button> up =
            std::dynamic_pointer_cast<gf::Button>(find_control(*shelf, "shelf.rows.up"));
        const std::shared_ptr<gf::Button> down =
            std::dynamic_pointer_cast<gf::Button>(find_control(*shelf, "shelf.rows.down"));
        assert(up && down && (*up).visible() && (*down).visible() && !(*up).enabled());
        assert((*down).enabled() == (*rows).scrollable());
        if ((*rows).scrollable()) {
            assert((*down).perform_click());
            while ((*rows).step(1.0 / 60, false)) {
            }
            window.perform_layout();
            assert((*rows).scroll_offset() > 0 && (*up).enabled());
            assert((*up).perform_click());
            while ((*rows).step(1.0 / 60, false)) {
            }
            window.perform_layout();
            assert((*rows).scroll_offset() == 0 && !(*up).enabled() && (*down).enabled());
        }
        if ((*rows).scrollable()) {
            gf::PointerEvent wheel;
            wheel.action = gf::PointerAction::wheel;
            wheel.wheel_delta = {0, -3};
            (*rows).on_pointer(wheel);
            window.perform_layout();
            assert(wheel.handled && (*rows).scroll_offset() > 0);
        }
    }
    std::shared_ptr<HelpPages> pages = gf::make_control<HelpPages>(gf::StableId("test.help"));
    gf::Window window(pages, {600, 420});
    window.perform_layout();
    std::size_t topic_count = 2;
    double first_topic_y = -1;
    for (Entry entry : entries) {
        const std::string prefix = "help.body." + std::to_string(static_cast<int>(entry)) + ".";
        (*pages).select(static_cast<int>(entry), "rules");
        window.perform_layout();
        const std::shared_ptr<gf::Control> body = find_control(*pages, prefix + "rules");
        assert(body && (*body).visible());
        assert(!help_text(entry).empty());
        const GameDescriptor& descriptor = game_descriptor(entry);
        topic_count += 1 + descriptor.help_topics.size();
        for (const HelpTopic& topic : descriptor.help_topics) {
            (*pages).select(static_cast<int>(entry), topic.id);
            window.perform_layout();
            const std::shared_ptr<gf::Control> extra = find_control(*pages, prefix + topic.id);
            assert(extra && (*extra).visible() && !(*body).visible());
            const std::shared_ptr<gf::Control> heading = find_control(
                *pages, "help.topic." + std::to_string(static_cast<int>(entry)) + "." + topic.id);
            // The chosen topic opens first, straight under the dedication and credits (and
            // the business card beside them, which the book lays over the pages).
            const gf::Rect credits =
                (*find_control(*pages, "help.front.credits")).committed_arranged_bounds();
            assert(heading && (*heading).committed_arranged_bounds().y >= credits.bottom() + 18);
            if (first_topic_y < 0)
                first_topic_y = (*heading).committed_arranged_bounds().y;
            assert(std::abs((*heading).committed_arranged_bounds().y - first_topic_y) < .01);
        }
        (*pages).select(static_cast<int>(entry), "missing-topic");
        assert((*body).visible());
        // A game's help holds the suite's two pages and that game's own, under the two
        // shared credit lines; no other game's.
        assert((*pages).children().size() == (2 + 1 + descriptor.help_topics.size()) * 2 + 2);
    }
    (*pages).select(-1, "about");
    // On the shelf the book holds every game's pages.
    assert((*pages).children().size() == topic_count * 2 + 2); // and two credit lines
    assert((*find_control(*pages, "help.body.-1.about")).visible());
    assert(help_about().find(std::to_string(entry_count) + " games") != std::string::npos);
    // The dedication and credits head the document, above every topic, whatever is selected.
    const std::shared_ptr<gf::Control> dedication = find_control(*pages, "help.front.dedication");
    const std::shared_ptr<gf::Control> credits = find_control(*pages, "help.front.credits");
    assert(dedication && credits);
    // The sponsor's address and website are on the business card now, not in the text.
    assert(!find_control(*pages, "help.front.contact") && !find_control(*pages, "help.front.website"));
    window.perform_layout();
    const double first_heading = (*find_control(*pages, "help.topic.-1.about")).committed_arranged_bounds().y;
    assert((*dedication).committed_arranged_bounds().y < first_heading);
    assert((*credits).committed_arranged_bounds().y < first_heading);
    (*pages).select(-123, "missing-topic");
    assert((*find_control(*pages, "help.body.-1.rules")).visible());
    std::cout << "Native shelves and descriptor-owned help passed\n";
}
