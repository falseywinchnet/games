# Proposal: making GUI.Forms controls less work than WinForms

Status: proposal for the GUI.Forms owner, written 2026-10-06 from PlaySuite dogfooding.
Toolkit revision examined: `2ac5dcd` (falseywinchnet/file_manager `main`).

## Why

GUI.Forms has four goals, all of which matter:

1. **Rich visual design without a separate graphics framework**, including real
   skinning (XP-era Royale/Watercolor like skins and a handful of default themes, like KDE 	themes in addition to our current default).
2. **WinForms-like efficiency.** Nothing runs at idle, and memory and code stay light.
3. **The exact design you want, with little effort.** A reduced HTML subset, and later
   a browser-based grid designer, generates native code.
4. **Clean, hand-editable code** that follows the house style. Interaction logic sits in
   one place and is only needed to override default behaviour. Controls are declared in
   a concise, readable structure (small structs, not XML) that people and models can
   edit easily.

The overall aim is "like WinForms, only better": common behaviours come for free,
and controls can be added just as quickly in code (with autocomplete) or in a designer.

Goal 2 is met and has improved through dogfooding: demand-driven painting, opaque and
native-order live surfaces, and live audio generators. The other three share the same
gaps.

## What it costs today

This is PlaySuite's command capsule (`src/capsule.cpp`) at `2ac5dcd`.

**Instantiation** is about the same as what the WinForms designer generates:

```cpp
more_ = gf::make_control<SuiteButton>(gf::StableId("capsule.more"), "", GlossTone::smoke);
(*more_).set_glyph(Glyph::more);
(*more_).set_radius(15);
(*more_).set_accessible_name("Keep the commands open");
add_child(more_);
```

**Events** are wired once for all the buttons, because binding is long-winded:

```cpp
for (const std::shared_ptr<SuiteButton>& b : {back_, more_, switches_[0], switches_[1], switches_[2]})
    subscriptions_.push_back((*b).clicked().subscribe(
        *this, gf::Delegate<gf::ButtonBase&>::bind<CommandCapsule, &CommandCapsule::clicked>(*this)));
```

and then dispatched by comparing pointers:

```cpp
void CommandCapsule::clicked(gf::ButtonBase& button) {
    if (&button == back_.get()) { ... }
    if (&button == more_.get()) { ... }
    for (int i = 0; i < 3; ++i) if (&button == switches_[i].get()) { ... }
    ...
}
```

**Layout** is hand-written in an arrange override, with a local functor and named
constants:

```cpp
PlaceSlot place{slots_, max_width_, x, y, right};
place(back_, kIcon, true);
x += title_width_ + kGap;
...
place(more_, kIcon, true);
```

GUI.Forms already has `TableLayoutPanel`, `FlowLayoutPanel`, `ScaledPanel` and
per-control `Margin`/`Padding`, but PlaySuite's shell does not use them. Its arrange
code is shorter than setting those containers up, which is the problem this proposal
targets.

**Theming** is a palette (`BasicControlStyle` colours), not skins. Controls draw their
own parts, and the same part can be drawn differently in two places:

- `ScrollableControl::paint_axis` draws the scroll-up arrow as the text glyph "▲" in an
  8-point font, positioned by fixed pixel offsets. It has no pressed state, no hover
  and no auto-repeat.
- `ScrollBar::paint_button` draws the same arrow as two 1.5-pixel lines, with a pressed
  state and repeat timing.

So the toolkit has two scroll bars that look and behave differently.

**Verdict:** roughly at parity with WinForms for instantiation, more work for layout
and events, much less work for custom drawing and performance. It is not yet strictly
less work than WinForms.

## The proposal

There are five steps. Each makes the next possible; steps 1–3 are the core of it.

### Step 1: one declarative grid in the core

A single layout model that hand-written code, the HTML subset and the designer all
target. Without it, a generator can only emit fixed coordinates.

- **Rows and columns** with fixed, auto (sized to content) or proportional (`*`, `2*`)
  sizes, plus minimum and maximum sizes.
- **Cells** with row, column, row span, column span, horizontal and vertical alignment
  (start, centre, end, stretch) and the control's existing `Margin`.
- **Dock and Anchor** as cell or control options, matching WinForms meanings.
- **A compact text form** for row and column definitions, which is the same syntax the
  HTML subset will use:

  ```cpp
  gf::Grid layout{{.columns = "34 auto * 34", .rows = "34", .gap = 6}};
  layout.place(back, {.column = 0});
  layout.place(title, {.column = 1});
  layout.place(more, {.column = 3});
  ```

- **Performance stays the same:** layout runs in the existing measure/arrange passes,
  only when marked dirty, with no allocation in steady state.
- `TableLayoutPanel` and `FlowLayoutPanel` stay as compatibility controls, implemented
  on top of the grid (flow as a grid that wraps). There should be one layout engine,
  not three.

Acceptance: PlaySuite's capsule and shelf lose their hand-written arrange code and
look the same, pixel for pixel, at the supported sizes (including 600 × 420).

### Step 2: one handler per control, in one line, without lambdas

The house style forbids lambdas, `auto` and `->`. The existing
`Delegate::bind<Class, &Class::method>` meets that rule but is long enough that code
falls back to a shared handler plus pointer comparisons. A short helper fixes that:

```cpp
// In the constructor or the form's wire-up function:
gf::on((*more_).clicked(), *this, &CommandCapsule::on_more);

void CommandCapsule::on_more(gf::ButtonBase&) {
    pinned_ = !pinned_;
    (*more_).set_checked(pinned_);
}
```

- `gf::on(event, owner, &Owner::method)` subscribes and stores the token in `owner`,
  so it is revoked automatically when the owner is disposed. The caller keeps no
  `subscriptions_` vector.
- The `Owner` and argument types are deduced from the event and the member-function
  pointer, so autocomplete offers the right handler signature.
- The same helper works for every `Event<…>` (clicked, text changed, value changed,
  timer tick).

Status: delivered in GUI.Forms PR #57 (owner-held tokens in `Component`). PlaySuite
adopts it when the toolkit pin next moves, after GUI.Forms is split into its own
repository and proven on File Manager.

Acceptance: PlaySuite's capsule replaces its shared `clicked` dispatcher with one method
per button, and has no `subscriptions_` bookkeeping.

### Step 2, continued: which item, and state

`gf::on` gives each fixed control its own handler. It does not remove the worst code in
the shell: the loops that compare a clicked pointer against every button in a row
(`CommandCapsule::clicked`, `SettingsPage::clicked`). Those buttons are made in loops and
the handler needs to know *which* one was clicked. WinForms answers this with
`sender.Tag`, or with containers that report the item (`ToolStrip.ItemClicked`,
`SelectedIndexChanged`); WPF adds commands that own their state. GUI.Forms needs all of
these, in the house style:

1. **One handler per fixed control** (`gf::on`). Done.
2. **Bound values:** `gf::on(event, owner, &Owner::method, value)`, a typed `Tag`. A
   handler may also omit event arguments it does not use (`void on_back()`).
3. **State that controls maintain and share:** every control keeps its own checked,
   selected, value and enabled state, and can bind it to an application-owned
   `gf::Command` or `gf::Value<T>`. Several controls bound to one value stay in step
   (the shelf's and the capsule's Music switches), and a programmatic change never
   re-fires the user handler.
4. **Collection controls that report the item:** a command bar
   (`invoked(const CommandItem&)`), a choice group (`changed(int)`, selection bound to a
   `gf::Value<int>`), and expandable sections. One subscription per group; replacing the
   items keeps it.

| PlaySuite code | Becomes |
|---|---|
| `CommandCapsule::clicked` pointer search | `on_back`, `on_more`, a bound `on_toggle(int)`, a command bar's `on_command` |
| `SettingsPage::clicked` row and chip search | one choice group per row, bound to its setting |
| `SettingSlider` subclass and its `quiet_` flag | a track bar bound to a `gf::Value<double>` |
| `masters_changed` and manual `set_checked` in shelf and capsule | switches bound to shared commands |
| `HelpPages::toggle` | expandable sections |

Acceptance: none of those pointer searches, subclasses or manual state copies remain,
and idle and paint costs are unchanged.

### Step 3: a form is a plain struct

Controls become members built from small option structs with designated initialisers,
followed by one layout declaration. This is the shape that hand-written code, models
and the generator all produce:

```cpp
struct CapsuleForm {
    gf::Button back{{.id = "capsule.back", .glyph = Glyph::back, .radius = 15, .name = "Back to the shelf"}};
    gf::Label title{{.id = "capsule.title", .font = {.size = 15, .weight = 700}}};
    gf::Button more{{.id = "capsule.more", .glyph = Glyph::more, .radius = 15, .name = "Keep the commands open"}};
    gf::Grid layout{{.columns = "34 auto * 34", .rows = "34", .gap = 6,
                     .cells = {{&back, {.column = 0}}, {&title, {.column = 1}}, {&more, {.column = 3}}}}};
};
```

- **Every built-in control has an `Options` struct.** Its fields have defaults, are
  documented in the header, and are listed in the API reference. This is what
  autocomplete shows.
- **The `StableId` comes from `.id`.** It stays mandatory, because tests, automation,
  saved state and the designer all rely on it.
- **One `attach` call** adds the struct's controls to a parent:
  `attach(*this, form_.layout)`. Ownership follows the existing retained-tree rules.
- Interaction logic stays in the owning class's handler methods (step 2), so the struct
  holds no behaviour.

Acceptance: PlaySuite's capsule and shelf are expressed this way and the code is
noticeably shorter. Two tests check it:

- a model given only the API reference can add a button with a handler correctly;
- a person can rearrange the capsule by editing only the struct.

### Step 4: skins draw named parts

- Every built-in control draws only through named theme parts, for example
  `scroll.up_button`, `scroll.thumb`, `scroll.track`, `button.face`,
  `check.box`, with states (normal, hover, pressed, disabled, focused, checked).
- A part is drawn by the active theme. It may be vector (the default themes), nine-slice
  images (XP-era skins) or a callback. A skin is data plus at most a small plug-in, not
  a fork of the controls.
- One implementation per behaviour: `ScrollableControl` uses the `ScrollBar` control's
  logic and parts, removing the second scroll bar.
- The default themes ship as data; the current palette (`BasicControlStyle`) becomes
  one of them.

Acceptance: switching between two shipped skins changes every control, the scroll
arrows included, with no control code changed. Idle and paint costs stay where they are.

### Step 5: the HTML subset and the browser designer

Both produce exactly step 3's struct:

- **The HTML subset** covers a small fixed set of elements mapped to controls, attributes
  mapped to `Options` fields, CSS-grid-style row and column definitions mapped to
  `gf::Grid`, and `id` mapped to `StableId`. Anything else is rejected with a clear
  message, not silently approximated.
- **The browser designer** edits the same model on a grid: drag and drop, live
  resizing of tracks, and the controls working as they will in the app. Its output is
  the struct; its input can be the struct too, so going back and forth between the
  designer and hand-edited code stays clean.
- The generated code obeys the house style, and `scripts/check-style.py` passes on it.

## Order of work and what it replaces in PlaySuite

| Step | Toolkit work | PlaySuite proving ground | Goals served |
|---|---|---|---|
| 1. Grid | layout engine, Dock and Anchor, row and column syntax | capsule and shelf arrange code | 3, 4 |
| 2. `gf::on` | one helper over `Event` and `Delegate` | capsule and shelf dispatchers, subscription vectors | 4 |
| 2, continued | bound values, shared state, collection controls | capsule and settings lookups, `SettingSlider`, manual `set_checked` | 2, 4 |
| 3. Form structs | `Options` per control, `attach` | capsule and shelf constructors | 3, 4 |
| 4. Skin parts | part vocabulary, theme data, one scroll bar | `SuiteButton` gloss and glyphs become a skin | 1 |
| 5. HTML and designer | generator, browser tool | a new game's settings panel built in the designer | 3 |

Every step keeps goal 2: no idle work, no extra allocation in steady state, and paint
and idle costs measured before and after with the existing profiling (`--profile-idle`,
the toolkit benchmarks).

## Open questions for the toolkit owner

- **Grid syntax:** should rows and columns use a compact string (`"34 auto * 34"`) or
  typed lists? A string is shorter and matches HTML; typed lists catch typos at
  compile time. One option is both, with the string parsed at construction and checked
  in debug builds.
- **Value or pointer members:** should form structs hold controls by value (simpler to
  read, fixed lifetime) or as `std::shared_ptr` (the current retained-tree ownership)?
  The examples above assume by value, with the tree holding non-owning references.
- **Two-way designer edits:** how much hand-written code inside a form struct should the
  designer preserve? The minimum is that it must not discard anything it does not
  understand.
