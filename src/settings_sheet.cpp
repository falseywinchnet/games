#include "settings_sheet.hpp"
#include "presentation.hpp"
#include "runtime_paths.hpp"
#include "gui_forms/window.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
namespace games {
namespace {
constexpr gf::Color rgb(int r, int g, int b, int a = 255) {
    return gf::Color::rgba(static_cast<unsigned char>(r), static_cast<unsigned char>(g),
                           static_cast<unsigned char>(b), static_cast<unsigned char>(a));
}
const gf::Color ink = rgb(42, 38, 24);
const gf::Color brown = rgb(140, 101, 34);
const char* const back_names[card_back_count] = {"Sapphire clubs", "Ruby diamonds",
                                                 "Emerald hearts", "Amethyst spades"};
// Master rows use these ids; a game's ids never start with "suite.".
const std::string music_id = "suite.music", sound_id = "suite.sound",
                  reduced_id = "suite.reduced", music_volume_id = "suite.music_volume",
                  sound_volume_id = "suite.sound_volume", backs_id = "suite.card_back";
constexpr double kRow = 34, kGap = 8;
} // namespace

SettingSlider::SettingSlider(gf::StableId id, std::string setting)
    : TrackBar(std::move(id)), setting_(std::move(setting)) {
    set_show_ticks(false);
    set_visual_style(gf::TrackBarVisualStyle::filled);
    set_paint_plane(gf::PaintPlane::overlay);
}
void SettingSlider::initialize_control_tree() {
    subscription_ = value_changed().subscribe(
        *this, gf::Delegate<double>::bind<SettingSlider, &SettingSlider::changed>(*this));
}
void SettingSlider::show_value(double value) {
    if (value == TrackBar::value())
        return;
    quiet_ = true;
    set_value(value);
    quiet_ = false;
}
void SettingSlider::on_paint(gf::Painter& p, gf::Rect) {
    const gf::Rect b = client_rectangle();
    // TrackBar maps the pointer across its width less ten points at each end.
    constexpr double inset = 10;
    const double width = std::max(0.0, b.width - inset * 2), cy = b.height * .5;
    const double ratio = normalized_value();
    const gf::Rect groove{inset - 3, cy - 3, width + 6, 6};
    p.fill_rounded_rect(groove, 3, rgb(226, 214, 166));
    p.stroke_rounded_rect(groove, 3, rgb(173, 151, 88), 1);
    if (ratio > 0)
        p.fill_rounded_rect({groove.x, groove.y, 6 + width * ratio, 6}, 3,
                            enabled() ? rgb(201, 146, 40) : rgb(190, 180, 150));
    const double x = inset + width * ratio, r = 9;
    if (focused_)
        p.stroke_rounded_rect({x - r - 3, cy - r - 3, 2 * r + 6, 2 * r + 6}, r + 3,
                              rgb(184, 137, 42), 2);
    p.draw_box_shadow({x - r, cy - r, 2 * r, 2 * r}, r, {0, 1}, 3, 0, rgb(0, 0, 0, 80));
    paint_gloss(p, {x - r, cy - r, 2 * r, 2 * r}, r, GlossTone::gold,
                {false, false, enabled(), false});
}
void SettingSlider::on_focus_changed(bool focused) {
    TrackBar::on_focus_changed(focused);
    focused_ = focused;
    invalidate(gf::Dirty::paint);
}
void SettingSlider::changed(double value) {
    if (!quiet_ && moved)
        moved(setting_, value);
}

CardBackChoice::CardBackChoice(gf::StableId id, std::string name, int back)
    : Button(std::move(id), std::move(name)), back_(back) {
    set_accessible_name(text());
    set_paint_plane(gf::PaintPlane::overlay);
}
void CardBackChoice::set_image(gf::ImageId image) {
    image_ = image;
    invalidate(gf::Dirty::paint);
}
void CardBackChoice::set_chosen(bool chosen) {
    if (chosen_ == chosen)
        return;
    chosen_ = chosen;
    set_accessible_name(text() + (chosen ? ", chosen" : ""));
    invalidate(gf::Dirty::paint);
}
void CardBackChoice::on_paint(gf::Painter& p, gf::Rect) {
    const gf::Rect b = client_rectangle();
    const gf::Rect card{5, 5, b.width - 10, b.height - 10};
    if (chosen_ || hovered_visual() || focus_cue_visible())
        p.stroke_rounded_rect({2, 2, b.width - 4, b.height - 4}, 8,
                              chosen_ ? rgb(214, 160, 40) : rgb(184, 137, 42, 150), chosen_ ? 3 : 2);
    p.draw_box_shadow(card, 5, {0, 2}, 4, 0, rgb(0, 0, 0, 70));
    if (image_.value)
        p.draw_image(image_, card);
    else
        p.fill_rounded_rect(card, 5, rgb(40, 60, 110));
    if (focus_cue_visible())
        p.stroke_rounded_rect({0, 0, b.width, b.height}, 10, rgb(110, 91, 37), 1);
}

SettingsPage::SettingsPage(gf::StableId id) : ScrollableControl(std::move(id)) {
    set_auto_scroll(true);
    set_paint_plane(gf::PaintPlane::overlay);
}
std::shared_ptr<gf::Label> SettingsPage::text_label(const std::string& text, double size,
                                                    int weight) {
    std::shared_ptr<gf::Label> label = gf::make_control<gf::Label>(
        gf::StableId("settings." + std::to_string(built_) + ".label." +
                     std::to_string(children().size())),
        text);
    (*label).set_font({gf::FontRole::content, size, static_cast<std::uint16_t>(weight), false});
    (*label).set_foreground(ink);
    (*label).set_vertical_alignment(gf::VerticalAlignment::center);
    (*label).set_text_wrapping(gf::TextWrapping::word);
    add_child(label);
    return label;
}
std::shared_ptr<gf::Label> SettingsPage::heading(const std::string& text) {
    std::shared_ptr<gf::Label> label = text_label(text, 13, 700);
    (*label).set_foreground(brown);
    (*label).set_text_case_transform(gf::TextCaseTransform::uppercase_ascii);
    return label;
}
std::shared_ptr<gf::CheckBox> SettingsPage::check(const std::string& key, const std::string& text) {
    std::shared_ptr<gf::CheckBox> box = gf::make_control<gf::CheckBox>(
        gf::StableId("settings." + std::to_string(built_) + ".check." + key), text);
    (*box).set_auto_check(false); // the store or the game decides; refresh() shows it
    (*box).set_font({gf::FontRole::content, 16, 600, false});
    (*box).set_accessible_name(text);
    (*box).set_paint_plane(gf::PaintPlane::overlay);
    subscriptions_.push_back((*box).clicked().subscribe(
        *this, gf::Delegate<gf::ButtonBase&>::bind<SettingsPage, &SettingsPage::clicked>(*this)));
    add_child(box);
    return box;
}
std::shared_ptr<SettingSlider> SettingsPage::slider(const std::string& key,
                                                    const std::string& setting,
                                                    const std::string& name, double minimum,
                                                    double maximum, double step) {
    std::shared_ptr<SettingSlider> bar = gf::make_control<SettingSlider>(
        gf::StableId("settings." + std::to_string(built_) + ".slider." + key), setting);
    (*bar).set_range(minimum, maximum);
    (*bar).set_small_change(step);
    (*bar).set_large_change(std::max(step, (maximum - minimum) / 5));
    (*bar).set_accessible_name(name);
    (*bar).moved = std::bind_front(&SettingsPage::slid, this);
    add_child(bar);
    return bar;
}
void SettingsPage::add_row(Row row) {
    rows_.push_back(std::move(row));
}
void SettingsPage::build(CommandSource* game, const std::string& game_title) {
    for (const Row& row : rows_) {
        for (const std::shared_ptr<gf::Control>& item : row.items)
            static_cast<void>(remove_child((*item).runtime_id()));
        for (const std::shared_ptr<gf::Control>& part :
             {row.lead, std::static_pointer_cast<gf::Control>(row.readout),
              std::static_pointer_cast<gf::Control>(row.note)})
            if (part)
                static_cast<void>(remove_child((*part).runtime_id()));
    }
    rows_.clear();
    subscriptions_.clear();
    ++built_;
    game_ = game;
    game_title_ = game_title;
    shape_ = game ? (*game).settings() : std::vector<GameSetting>{};

    add_row({Shape::heading, "", heading("Sound and music"), {}, nullptr, nullptr});
    struct Master {
        const std::string& toggle;
        const std::string& volume;
        const char* name;
        const char* key;
    };
    const Master masters[] = {{music_id, music_volume_id, "Music", "music"},
                              {sound_id, sound_volume_id, "Sound", "sound"}};
    for (const Master& m : masters) {
        Row row{Shape::volume, m.toggle, check(m.key, m.name), {}, nullptr, nullptr};
        row.items.push_back(slider(m.key, m.volume, std::string(m.name) + " volume", 0, 100, 5));
        row.readout = text_label("", 15, 600);
        (*row.readout).set_alignment(gf::HorizontalAlignment::far);
        add_row(std::move(row));
    }
    add_row({Shape::heading, "", heading("Motion"), {}, nullptr, nullptr});
    add_row({Shape::check, reduced_id, check("reduced", "Reduce motion"), {}, nullptr,
             text_label("Shortens or removes animation wherever a game offers that.", 13, 400)});
    if (game && (*game).uses_card_backs()) {
        add_row({Shape::heading, "", heading("Card back"), {}, nullptr, nullptr});
        Row row{Shape::backs, backs_id, nullptr, {}, nullptr, nullptr};
        for (int i = 0; i < card_back_count; ++i) {
            std::shared_ptr<CardBackChoice> choice = gf::make_control<CardBackChoice>(
                gf::StableId("settings." + std::to_string(built_) + ".back." + std::to_string(i)),
                back_names[i], i);
            (*choice).set_image(back_images_[static_cast<std::size_t>(i)]);
            subscriptions_.push_back((*choice).clicked().subscribe(
                *this,
                gf::Delegate<gf::ButtonBase&>::bind<SettingsPage, &SettingsPage::clicked>(*this)));
            add_child(choice);
            row.items.push_back(choice);
        }
        add_row(std::move(row));
    }
    if (!shape_.empty()) {
        add_row({Shape::heading, "", heading(game_title), {}, nullptr, nullptr});
        for (const GameSetting& s : shape_) {
            const std::string key = "game." + s.id;
            Row row{Shape::check, s.id, nullptr, {}, nullptr, nullptr};
            if (s.kind == GameSetting::Kind::toggle) {
                row.lead = check(key, s.label);
            } else if (s.kind == GameSetting::Kind::choice) {
                row.shape = Shape::chips;
                row.lead = text_label(s.label, 16, 600);
                for (std::size_t k = 0; k < s.choices.size(); ++k) {
                    std::shared_ptr<SuiteButton> chip = gf::make_control<SuiteButton>(
                        gf::StableId("settings." + std::to_string(built_) + ".chip." + key + "." +
                                     std::to_string(k)),
                        s.choices[k], GlossTone::chrome);
                    (*chip).set_radius(15);
                    (*chip).set_accessible_name(s.label + ": " + s.choices[k]);
                    (*chip).set_paint_plane(gf::PaintPlane::overlay);
                    subscriptions_.push_back((*chip).clicked().subscribe(
                        *this, gf::Delegate<gf::ButtonBase&>::bind<SettingsPage,
                                                                   &SettingsPage::clicked>(*this)));
                    add_child(chip);
                    row.items.push_back(chip);
                }
            } else {
                row.shape = Shape::slider;
                row.lead = text_label(s.label, 16, 600);
                row.items.push_back(slider(key, s.id, s.label, s.minimum, s.maximum, s.step));
                row.readout = text_label("", 15, 600);
                (*row.readout).set_alignment(gf::HorizontalAlignment::far);
            }
            if (!s.note.empty())
                row.note = text_label(s.note, 13, 400);
            add_row(std::move(row));
        }
    }
    refresh();
    static_cast<void>(scroll_to({0, 0}));
    invalidate(gf::Dirty::layout | gf::Dirty::paint);
}
bool SettingsPage::same_shape(const std::vector<GameSetting>& a, const std::vector<GameSetting>& b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].id != b[i].id || a[i].label != b[i].label || a[i].kind != b[i].kind ||
            a[i].choices != b[i].choices || a[i].note != b[i].note ||
            a[i].minimum != b[i].minimum || a[i].maximum != b[i].maximum)
            return false;
    return true;
}
std::string SettingsPage::percent(double value) {
    return std::to_string(static_cast<int>(std::lround(value))) + "%";
}
std::string SettingsPage::slider_text(const GameSetting& setting, double value) {
    const double span = setting.maximum - setting.minimum;
    if (span == 100 || span == 1)
        return percent((value - setting.minimum) * (span == 1 ? 100 : 1));
    return std::to_string(static_cast<int>(std::lround(value)));
}
void SettingsPage::refresh() {
    std::vector<GameSetting> current;
    if (game_) {
        current = (*game_).settings();
        if (!same_shape(current, shape_)) {
            build(game_, game_title_);
            return;
        }
    }
    const SuiteSettings& masters = SettingsStore::shared().values();
    for (const Row& row : rows_) {
        if (row.setting == music_id || row.setting == sound_id) {
            const bool music = row.setting == music_id;
            const bool on = music ? masters.music : masters.sound;
            const double volume = (music ? masters.music_volume : masters.sound_volume) * 100;
            (*std::static_pointer_cast<gf::CheckBox>(row.lead)).set_checked(on);
            (*std::static_pointer_cast<gf::CheckBox>(row.lead))
                .set_accessible_name(std::string(music ? "Music" : "Sound") + (on ? " on" : " off"));
            (*std::static_pointer_cast<SettingSlider>(row.items.front())).show_value(volume);
            (*row.readout).set_text(percent(volume));
        } else if (row.setting == reduced_id) {
            (*std::static_pointer_cast<gf::CheckBox>(row.lead)).set_checked(masters.reduced);
        } else if (row.setting == backs_id) {
            for (const std::shared_ptr<gf::Control>& item : row.items) {
                CardBackChoice& choice = *std::static_pointer_cast<CardBackChoice>(item);
                choice.set_chosen(choice.back() == masters.card_back);
            }
        } else if (!row.setting.empty()) {
            for (const GameSetting& s : current) {
                if (s.id != row.setting)
                    continue;
                if (row.shape == Shape::check)
                    (*std::static_pointer_cast<gf::CheckBox>(row.lead)).set_checked(s.value != 0);
                else if (row.shape == Shape::chips)
                    for (std::size_t k = 0; k < row.items.size(); ++k)
                        (*std::static_pointer_cast<SuiteButton>(row.items[k]))
                            .set_checked(static_cast<std::size_t>(std::lround(s.value)) == k);
                else if (row.shape == Shape::slider) {
                    (*std::static_pointer_cast<SettingSlider>(row.items.front())).show_value(s.value);
                    (*row.readout).set_text(slider_text(s, s.value));
                }
            }
        }
    }
}
void SettingsPage::set_card_back_images(const std::array<gf::ImageId, card_back_count>& images) {
    back_images_ = images;
    for (const Row& row : rows_)
        if (row.setting == backs_id)
            for (const std::shared_ptr<gf::Control>& item : row.items) {
                CardBackChoice& choice = *std::static_pointer_cast<CardBackChoice>(item);
                choice.set_image(back_images_[static_cast<std::size_t>(choice.back())]);
            }
}
void SettingsPage::clicked(gf::ButtonBase& button) {
    SettingsStore& store = SettingsStore::shared();
    for (const Row& row : rows_) {
        if (row.lead.get() == &button) {
            if (row.setting == music_id)
                store.toggle_music();
            else if (row.setting == sound_id)
                store.toggle_sound();
            else if (row.setting == reduced_id)
                store.toggle_reduced();
            else if (game_)
                (*game_).change_setting(
                    row.setting, (*std::static_pointer_cast<gf::CheckBox>(row.lead)).checked() ? 0 : 1);
            refresh();
            return;
        }
        for (std::size_t k = 0; k < row.items.size(); ++k) {
            if (row.items[k].get() != &button)
                continue;
            if (row.setting == backs_id) {
                SuiteSettings next = store.values();
                next.card_back = static_cast<int>(k);
                store.set(next);
            } else if (game_)
                (*game_).change_setting(row.setting, static_cast<double>(k));
            refresh();
            return;
        }
    }
}
void SettingsPage::slid(const std::string& setting, double value) {
    SettingsStore& store = SettingsStore::shared();
    if (setting == music_volume_id || setting == sound_volume_id) {
        SuiteSettings next = store.values();
        (setting == music_volume_id ? next.music_volume : next.sound_volume) = value / 100;
        store.set(next);
    } else if (game_)
        (*game_).change_setting(setting, value);
    refresh();
}
double SettingsPage::content_height(double width) const {
    Placement ignored;
    return plan(std::max(1.0, width - 18), ignored);
}
double SettingsPage::plan(double w, Placement& layout) const {
    const double lead = std::clamp(w * .3, 104.0, 168.0);
    const double right = lead + kGap;
    double y = 0;
    for (const Row& row : rows_) {
        switch (row.shape) {
        case Shape::heading:
            y += y > 0 ? 10 : 0;
            layout.push_back({row.lead, {0, y, w, 24}});
            y += 28;
            break;
        case Shape::volume:
        case Shape::slider:
            layout.push_back({row.lead, {0, y, lead, kRow}});
            layout.push_back({row.items.front(), {right, y, std::max(40.0, w - right - 56), kRow}});
            layout.push_back({row.readout, {w - 50, y, 50, kRow}});
            y += kRow + kGap;
            break;
        case Shape::check:
            layout.push_back({row.lead, {0, y, w, kRow}});
            y += kRow + (row.note ? 0 : kGap);
            break;
        case Shape::chips: {
            layout.push_back({row.lead, {0, y, lead, kRow}});
            double x = right;
            for (const std::shared_ptr<gf::Control>& item : row.items) {
                const double cw = (*std::static_pointer_cast<SuiteButton>(item)).preferred_width() + 8;
                if (x > right && x + cw > w) {
                    x = right;
                    y += kRow + 4;
                }
                layout.push_back({item, {x, y, cw, kRow}});
                x += cw + 6;
            }
            y += kRow + kGap;
            break;
        }
        case Shape::backs: {
            const double cw = 66, ch = 92;
            double x = 0;
            for (const std::shared_ptr<gf::Control>& item : row.items) {
                if (x > 0 && x + cw > w) {
                    x = 0;
                    y += ch + 6;
                }
                layout.push_back({item, {x, y, cw, ch}});
                x += cw + 10;
            }
            y += ch + kGap;
            break;
        }
        }
        if (row.note) {
            const double indent = row.shape == Shape::check ? 28 : right;
            const double height =
                std::max(18.0, (*row.note).measure({w - indent, 10000}).height);
            layout.push_back({row.note, {indent, y - 2, w - indent, height}});
            y += height + kGap;
        }
    }
    return y;
}
void SettingsPage::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    const double w = std::max(1.0, bounds.width - 18);
    Placement layout;
    const double y = plan(w, layout);
    arrange_scroll_viewport({bounds.width, bounds.height}, {w, y});
    for (const std::shared_ptr<gf::Control>& child : children())
        (*child).set_paint_plane(gf::PaintPlane::overlay);
    const gf::Point offset = scroll_position();
    for (const std::pair<std::shared_ptr<gf::Control>, gf::Rect>& item : layout) {
        gf::Rect r = item.second;
        r.y -= offset.y;
        set_child_layout(item.first, r);
    }
}
void SettingsPage::on_key_bubble(gf::KeyEvent& e) {
    if (e.handled || e.action != gf::KeyAction::down)
        return;
    const double page = std::max(40.0, client_rectangle().height - 40);
    if (e.physical_key == gf::PhysicalKey::page_down || e.physical_key == gf::PhysicalKey::page_up) {
        static_cast<void>(
            scroll_by({0, e.physical_key == gf::PhysicalKey::page_down ? page : -page}));
        e.handled = true;
    }
}

SettingsSheet::SettingsSheet(gf::StableId id) : Control(std::move(id)) {
    set_paint_plane(gf::PaintPlane::overlay);
    set_theme_override(games_theme(ButtonSkin::ivory));
    set_accessible_name("PlaySuite settings");
}
void SettingsSheet::initialize_control_tree() {
    page_ = gf::make_control<SettingsPage>(gf::StableId("settings.page"));
    close_ = gf::make_control<HelpGlyph>(gf::StableId("settings.close"), "×");
    (*close_).set_paint_plane(gf::PaintPlane::overlay);
    (*close_).set_accessible_name("Close settings (Escape)");
    close_subscription_ = (*close_).clicked().subscribe(
        *this,
        gf::Delegate<gf::ButtonBase&>::bind<SettingsSheet, &SettingsSheet::clicked_close>(*this));
    add_child(page_);
    add_child(close_);
}
void SettingsSheet::on_attached_to_window() {
    gf::Window& window = *attached_window();
    for (int i = 0; i < card_back_count; ++i) {
        const std::filesystem::path path =
            std::filesystem::path(asset_directory()) / ("back_" + std::to_string(i) + ".png");
        std::ifstream file(path, std::ios::binary);
        if (!file)
            continue;
        const std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
        const std::span<const std::byte> encoded(reinterpret_cast<const std::byte*>(bytes.data()),
                                                 bytes.size());
        gf::ImageLoadResult loaded = window.load_png(encoded);
        if (loaded)
            backs_[static_cast<std::size_t>(i)] = loaded.image;
    }
    (*page_).set_card_back_images(backs_);
}
void SettingsSheet::on_detaching_from_window(gf::Window& window) noexcept {
    for (gf::ImageId& image : backs_)
        if (image.value) {
            static_cast<void>(window.remove_image(image));
            image = {};
        }
    (*page_).set_card_back_images(backs_);
}
void SettingsSheet::show_for(const std::string& title, CommandSource* game) {
    title_ = title;
    (*page_).build(game, title);
    invalidate(gf::Dirty::layout | gf::Dirty::paint);
}
void SettingsSheet::refresh() {
    (*page_).refresh();
}
std::shared_ptr<gf::Control> SettingsSheet::first_control() const {
    for (const std::shared_ptr<gf::Control>& child : (*page_).children())
        if ((*child).focusable() && (*child).visible())
            return child;
    return close_;
}
double SettingsSheet::preferred_height(double width, double limit) const {
    return std::min(limit, std::ceil(74 + (*page_).content_height(width - 32)));
}
void SettingsSheet::on_key_bubble(gf::KeyEvent& event) {
    (*page_).on_key_bubble(event);
}
void SettingsSheet::clicked_close(gf::ButtonBase&) {
    if (close)
        close();
}
void SettingsSheet::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    set_child_layout(close_, {bounds.width - 48, 9, 36, 36});
    set_child_layout(page_, {20, 62, bounds.width - 32, bounds.height - 74});
}
void SettingsSheet::on_paint(gf::Painter& p, gf::Rect) {
    const gf::Rect b = client_rectangle();
    p.draw_box_shadow(b, 4, {0, 8}, 24, 0, rgb(0, 0, 0, 110));
    p.fill_rect(b, rgb(255, 252, 215));
    p.stroke_rect(b, rgb(153, 137, 78), 1);
    const std::string heading = (title_.empty() ? std::string("PlaySuite") : title_) + " · Settings";
    p.draw_text_utf8({20, 36}, heading, {gf::FontRole::content, 24, 600, false}, ink);
    p.draw_line({20, 50}, {b.width - 20, 50}, rgb(207, 193, 133), 1);
}
} // namespace games
