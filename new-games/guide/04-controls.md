# Controls

How PlaySuite writes GUI.Forms controls: the canon set, the concise authoring layer the
toolkit added in October 2026 (owner-held handlers, bound values and commands,
item-reporting groups), and the shell's own skins. A kit game draws its own surface and
needs little of this; the shell, a game's panels drawn with ordinary controls (the
cards, Sudoku, the puzzles), and anything built for the Settings screen use it daily.

The source of truth is the pinned toolkit (`.github/workflows/applications.yml`):
`docs/CURRENT_API_REFERENCE.md` and `docs/CONCISE_STATE_AND_COLLECTIONS.md` in
`falseywinchnet/gui_forms`. If they disagree with this page, they are right.

## The canon

| Control | Header | Use it for |
|---|---|---|
| `Control` | `gui_forms/control.hpp` | The base: `arrange`, `on_paint`, `on_pointer`, `on_key`, `on_key_bubble`, `on_attached_to_window`, `on_detaching_from_window`, `invalidate`, `set_paint_plane`, `set_accessible_name`. Anything owner-drawn. |
| `Panel`, `GroupBox`, `ScaledPanel`, `Card` | `container_controls.hpp` | Grouping; a card with an elevation. |
| `FlowLayoutPanel`, `TableLayoutPanel` | `container_controls.hpp` | Layout containers. The shell lays out by hand in `arrange`, which is shorter until the declarative grid below exists. |
| `ScrollableControl` | `controls/scrollable_control/` | A scrolling body: `set_auto_scroll`, `arrange_scroll_viewport`, `scroll_to`, `scroll_by`, `scroll_position`. The help pages and the Settings page are these. |
| `Label` | `basic_controls.hpp` | Text: `set_font`, `set_foreground`, `set_text_wrapping`, `set_alignment`, `measure`. |
| `Button`, `CheckBox`, `RadioButton`, `LinkLabel` | `basic_controls.hpp` | Buttons and their kin, all on `ButtonBase`: `clicked`, `set_checked`, `set_enabled`, `set_expanded_state` (a disclosure), `hovered_visual`, `focus_cue_visible`. |
| `TextBox` | `input_controls.hpp` | Text entry (`set_maximum_length`; multiline scrolls). The score name entry on the card table. |
| `TrackBar`, `ProgressBar` | `range_controls.hpp` | A `RangeControl` with `value_changed` for every change and `scroll` for user input only. |
| `ListBox` | `collection_controls.hpp` | A list: `selection_changed`, `item_activated(std::size_t)`. |
| `ChoiceGroup`, `CommandBar`, `ExpandableSections` | `collection_controls.hpp` | Groups that report the item: a radio row or chips, a toolbar of commands, disclosure sections. |
| `MenuStrip`, `ContextMenu` | `menu_controls.hpp` | Menus driven by `Command`s. |
| `Timer` | `timer.hpp` | `start`, `stop`, `set_interval`, a `tick()` event. The only clock a control uses. |
| `LiveSurface` | `live_surface.hpp` | A game's own pixels (see [primitives](03-primitives.md)). |
| `Command`, `Value<T>` | `commands.hpp`, `value.hpp` | Application-owned state that controls bind to. |

A control is made with `gf::make_control<T>(gf::StableId("where.what"), ...)` and
added with `add_child`. The `StableId` is mandatory: tests, automation, saved state and
the designer all find controls by it. Name ids by place and part (`help.close`,
`settings.page`, `capsule.cmd.undo`).

## One handler per control: `gf::on`

A click, a tick or a change goes to a named member function. The owner must derive from
`gf::Component` (every control does); the subscription lives in the owner and is
revoked when the owner is disposed. No token vectors, no pointer searches, no lambdas.

```cpp
gf::on((*back_).clicked(), *this, &CommandCapsule::on_back);
gf::on((*timer_).tick(), *this, &ShelfView::tick);

void CommandCapsule::on_back() {        // unused trailing event arguments may be omitted
    if (back)
        back();
}
```

**Which one?** Controls made in a loop bind a value, up to two trivially copyable
values of at most 16 bytes in all. Pass indices, never strings or pointers to things
that may move.

```cpp
for (const GameCommand& c : commands_) {
    std::shared_ptr<SuiteButton> b = gf::make_control<SuiteButton>(gf::StableId("capsule.cmd." + c.id), c.label);
    gf::on((*b).clicked(), *this, &CommandCapsule::on_command, static_cast<int>(buttons_.size()));
    buttons_.push_back(std::move(b));
}
void CommandCapsule::on_command(int index);          // bound values come first
void SettingsPage::chose(int row, int choice);       // two values
void SettingsPage::chose(int row, int choice, gf::ButtonBase& sender);  // then event arguments, if wanted
```

Rules:

- State events (`value_changed`, `checked_changed`, `changed(int)`) report every actual
  change, including programmatic ones; input events (`scroll`, `clicked`,
  `activated(int)`) report only what the user did. Setting an equal value does nothing.
- Register, emit and dispose on the UI thread. A handler that deletes its own owner, or
  emits from a destructor, calls `dispose()` first.
- `subscribe(...)` with a caller-held token still exists for a connection that must be
  cancelled independently; it is the exception, not the pattern.
- Overloaded member names need an explicit member-pointer cast.

## Shared state: `Command` and `Value<T>`

A switch that appears in two places (Music on the shelf and in the capsule), a slider
and a check box that show one setting, a choice row that a key also changes: the
application owns the state and the controls bind to it, so nothing copies `set_checked`
around and a programmatic change never re-fires the user handler.

```cpp
class SuiteModel final : public gf::Component {
  public:
    gf::Command music{"suite.music", "Music"};          // checked while music plays
    gf::Value<double> music_volume{100};                // percent
    gf::Value<int> card_back{0};
};

button.bind(model.music);                // enabled follows; checked projects to selected
check_box.bind(model.music);             // the box's changes update the command
track_bar.bind(model.music_volume);      // two-way; the model wins at bind time
choice_group.bind(model.card_back);      // selection as an int, -1 for none
gf::on(model.music_volume.changed(), *this, &SuiteModel::volume_moved);
```

- `Value<T>` is for arithmetic and enum scalars. `get`, `set`, `changed`, `validating`.
  Floating values must be finite; a validator may throw to reject. A different write
  during a `changed` notification is a `logic_error`: defer it.
- `Command` is the authority behind buttons, menus, shortcuts and tests: `set_enabled`,
  `set_checked` / `clear_checked`, `set_text`, `invoked`. `button.bind(command)` borrows
  it; clicking invokes it once. `command.bind_shortcut(window, gesture)` returns a token
  to keep.
- Binding may allocate; propagation does not. Controls borrow the model; a destroyed
  model is never invoked through a surviving control.
- The shell's `SuiteModel` (`src/suite_model.*`) is the worked example: the masters as
  commands and values, pulled from and pushed to the `SettingsStore`.

## Groups that report the item

| Group | Items | Reports | Use |
|---|---|---|---|
| `ChoiceGroup` | text, glyph or image items | `changed(int)`, `activated(int)`; binds `Value<int>` | A row of chips (a game's choice setting), a segmented control, image tiles (card backs). Arrow keys move the selection; accessibility is a radio group. |
| `CommandBar` | `CommandItem` records: id, label, glyph, accessible name, shortcut, borrowed `Command` | `invoked(const CommandItem&)` | A toolbar; one roving tab stop. |
| `ExpandableSections` | id, heading, owned content, optional `Value<bool>` | `toggled(int, bool)` | Disclosure sections, single-open or several. |

One subscription on the group survives `set_items`; ids must be unique; same-id items
keep their controls across a replacement. The named parts (`item`, `header`, `body`)
are ordinary controls, so a skin recipe applies to them.

## Skins: the shell's look

The shell does not restyle the toolkit; it subclasses a control and paints it, keeping
the control's behaviour, keyboard handling and accessibility:

| Shell class | On | Adds |
|---|---|---|
| `SuiteButton` (`src/shelf.hpp`) | `Button` | Gloss tones, glyphs, round or pill radius. The capsule, the shelf arrows, the master switches. |
| `GameButton` (`src/presentation.hpp`) | `Button` | Four `ButtonSkin`s (slate, ivory, blue, mint) for game panels. |
| `SettingSlider`, `SettingCheck`, `CardBackChoice` (`src/settings_sheet.hpp`) | `TrackBar`, `CheckBox`, `Button` | The gold groove and knob, the gold box, a card back as it looks on the table. |
| `HelpGlyph`, `TopicHeading` (`src/help_book.*`) | `Button`, `Button` | The close mark, a disclosure heading. |
| `BusinessCard` (`src/business_card.*`) | `Control` | Rainstar Software's card, drawn by `shared/cards/held_card.*`: on the help page a small still card that opens on a click; in the hand, over the whole window on black, turned by dragging or the arrows, its name the link to the website, Escape to put it away. |

`games_theme(ButtonSkin)` (`src/presentation.hpp`) is the palette a sheet applies with
`set_theme_override`. The help and Settings sheets are printed on the paper engine's
tile (`src/paper_sheet.*`): made once per window scale when the sheet attaches, drawn
with `fill_image_pattern`.

A painted control uses `hovered_visual()` and `focus_cue_visible()` (the toolkit shows
the focus cue only when the keyboard moved focus) and `client_rectangle()`; it draws
text with `draw_text_utf8` at a baseline it measures with `measure_text_utf8`.

## Lifecycle, in short

1. The constructor sets fonts, names and planes. With
   `static constexpr bool initialize_tree_after_construction = true;` the children are
   made in `initialize_control_tree()`, where `gf::on` wiring goes.
2. `on_attached_to_window()` loads images (`window.load_png`,
   `window.load_bgra32_premultiplied`), starts timers, makes the paper.
   `on_detaching_from_window(window) noexcept` removes the images and stops the timers.
3. `arrange(bounds)`: `arrange_self(bounds)`, then `set_child_layout(child, rect)` for
   each child, in points.
4. `on_paint(painter, damage)` draws only this control's own pixels; children paint
   themselves. `invalidate(gf::Dirty::paint)` after a state change,
   `gf::Dirty::layout | gf::Dirty::paint` after a size change.
5. Keys bubble: a sheet forwards `on_key_bubble` to its page; a control sets
   `event.handled = true` only for keys it used.
6. A game's single-key commands also work while its view lacks the keyboard (help
   open, or a capsule button focused): `CommandSource::key_command(key)` names the
   command, and the shell runs it through `run_command`.

## Why it is written this way

In October 2026 PlaySuite measured its own shell against the WinForms it is meant to
beat. Instantiation was at parity; custom drawing and performance were far better;
layout and events were worse. Wiring a button took a `Delegate::bind` long enough that
the capsule bound one shared handler for every button and found out which was clicked
by comparing the sender against each pointer, and the Settings page did the same for
its rows and chips, with a quiet-flag `TrackBar` subclass to stop a feedback loop. The
case was put to the toolkit owner in five steps; two have shipped and are what this
page teaches:

| Step | State | What it replaced in the shell |
|---|---|---|
| Named handlers: `gf::on` with owner-held tokens | Shipped (GUI.Forms f81ec0c) | Every `subscriptions_` vector and `Delegate::bind` |
| Bound values, shared `Command` and `Value<T>` state, item-reporting groups | Shipped (5bb7a8b) | The capsule's and Settings page's pointer searches, the quiet slider subclass, manual `set_checked` copies between the shelf's and the capsule's switches |
| One declarative grid (rows and columns as `"34 auto * 34"`, cells, dock and anchor) | Not yet | The hand-written `arrange` code in the capsule and shelf |
| A form as a plain struct of controls built from `Options` records, attached in one call | Not yet | The constructors' `make_control` and setter runs |
| Skins that draw named parts; the HTML subset and the browser designer that produce the struct | Not yet | `SuiteButton`'s gloss and glyphs becoming a skin |

Until the grid and form records exist, write controls as the shipped steps allow:
one named handler per control, state bound rather than copied, groups that report
the item, and `arrange` by hand.

## What not to do

- No `subscriptions_` vectors, `Delegate::bind` chains or handlers that compare a sender
  pointer against every button. `gf::on` with a bound index replaces all three.
- No quiet-flag subclasses to suppress a feedback loop: bind a `Value`, or observe the
  input event (`scroll`) instead of the state event.
- No manual `set_checked` copies between two controls that show one state: bind both to
  one `Command`.
- No lambdas, `auto` or `->` anywhere (the [house style](07-house-style.md)).
- Nothing drawn over a live surface by another control; nothing that polls. A control
  that is not animating schedules no work.
