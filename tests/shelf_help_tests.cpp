#include "game_module.hpp"
#include "gui_forms/window.hpp"
#include "help_book.hpp"
#include "help_content.hpp"
#include "shelf.hpp"
#include <cassert>
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
    const gf::Rect viewport = rows.viewport_rectangle();
    assert(bounds.y >= -.01);
    assert(bounds.bottom() <= viewport.height + .01);
}
int main() {
    TextSprites sprites;
    const gf::Size sizes[] = {{600, 420}, {1280, 900}};
    for (gf::Size size : sizes) {
        std::shared_ptr<ShelfView> shelf =
            gf::make_control<ShelfView>(gf::StableId("test.shelf"), sprites);
        (*shelf).set_preferences(false, false, true);
        gf::Window window(shelf, size);
        window.perform_layout();
        const std::shared_ptr<ShelfRows> rows =
            std::dynamic_pointer_cast<ShelfRows>(find_control(*shelf, "shelf.rows"));
        assert(rows && !(*rows).hscroll());
        assert((*rows).children().size() == entries.size());
        const std::shared_ptr<gf::Control> credits = find_control(*shelf, "shelf.credits");
        const std::shared_ptr<gf::Control> music = find_control(*shelf, "shelf.Music");
        assert(credits && music);
        const gf::Rect credits_bounds = (*credits).committed_arranged_bounds();
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
        assert((*credits).committed_arranged_bounds() == credits_bounds);
        assert((*music).committed_arranged_bounds() == music_bounds);
        key(window, gf::PhysicalKey::home);
        assert((*shelf).selection() == entries.front());
        assert_revealed(*box(*shelf, entries.front()), *rows);
        if (entries.size() > 1) {
            key(window, gf::PhysicalKey::right);
            assert((*shelf).selection() == entries[1]);
            key(window, gf::PhysicalKey::left);
            assert((*shelf).selection() == entries.front());
            key(window, gf::PhysicalKey::down);
            const int row_end = std::min(entry_count - 1, (*rows).columns());
            assert((*shelf).selection() == entries[static_cast<std::size_t>(row_end)]);
            assert_revealed(*box(*shelf, (*shelf).selection()), *rows);
            key(window, gf::PhysicalKey::up);
            assert((*shelf).selection() == entries.front());
        }
        key(window, gf::PhysicalKey::end);
        assert((*shelf).selection() == entries.back());
        assert_revealed(*box(*shelf, entries.back()), *rows);
        key(window, gf::PhysicalKey::page_up);
        assert_revealed(*box(*shelf, (*shelf).selection()), *rows);
        key(window, gf::PhysicalKey::page_down);
        assert_revealed(*box(*shelf, (*shelf).selection()), *rows);
        // Tab focus also reveals a control independently of the selected index.
        assert(window.request_focus(box(*shelf, entries.front())));
        window.perform_layout();
        assert((*shelf).selection() == entries.front());
        assert_revealed(*box(*shelf, entries.front()), *rows);
        if ((*rows).vscroll()) {
            gf::PointerEvent wheel;
            wheel.action = gf::PointerAction::wheel;
            wheel.wheel_delta = {0, -3};
            (*rows).on_pointer(wheel);
            window.perform_layout();
            assert(wheel.handled && (*rows).scroll_position().y > 0);
        }
    }
    std::shared_ptr<HelpPages> pages = gf::make_control<HelpPages>(gf::StableId("test.help"));
    gf::Window window(pages, {600, 420});
    window.perform_layout();
    std::size_t topic_count = 2;
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
            assert(heading && (*heading).committed_arranged_bounds().y == 0);
        }
        (*pages).select(static_cast<int>(entry), "missing-topic");
        assert((*body).visible());
    }
    assert((*pages).children().size() == topic_count * 2);
    (*pages).select(-1, "about");
    assert((*find_control(*pages, "help.body.-1.about")).visible());
    assert(help_about().find(std::to_string(entry_count) + " games") != std::string::npos);
    (*pages).select(-123, "missing-topic");
    assert((*find_control(*pages, "help.body.-1.rules")).visible());
    std::cout << "Native shelves and descriptor-owned help passed\n";
}
