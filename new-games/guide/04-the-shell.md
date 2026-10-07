# The shell

PlaySuite's shell is the shelf (the main menu), the command capsule (the menu
inside every game) and the collection that owns both. A game is a guest. This
guide is what the shell gives a game and what it expects back.

The source of truth is `src/game_module.hpp`, `src/suite.hpp`, `src/collection.cpp`, `src/capsule.hpp`
and `src/shelf.hpp`. If they disagree with this page, they are right.

## The shelf

The main menu is a wall of walnut shelves holding every game as a box, in the
manner of a 1990s games collection. There are no categories. One click opens a
box with a short box-to-window transition; arrow keys choose a box and Enter
opens it. A ticket describes the box under the pointer.

Your game's box is generated from `GAME.json` and its own `cover.cpp`:

| Field | Shown | Guidance |
|---|---|---|
| `title` | On the box, condensed capitals | At most 28 characters |
| `kind` | Small line under the title | Two or three words: "Pasture puzzle", "Bluffing" |
| `blurb` | The ticket | One sentence, at most 90 characters, an invitation: "Place fences before a clever sheep finds its way out." |
| `colors[0]`, `colors[1]` | The box's vertical gradient | Two related colours from the game's own palette, dark enough that cream lettering reads |
| `colors[2]` | Glow behind the emblem and trim | The game's brightest colour |
| The emblem | The picture on the box | Drawn by you in the module's cover function |

**The emblem** is painted with GUI.Forms `Painter` calls inside a square of size
`s` centred at `(cx, cy)`. It is redrawn at every box size, so draw it from
proportions of `s`, with a few bold shapes: `disc(p, x, y, r, colour)`,
`p.fill_rounded_rect`, `p.draw_line`, `paint_polygon`. Look at the existing
cases for the level of detail: a sheep is six discs, a head and two legs; Rock
Stack is four rounded stones. It must read at 40 pixels. It should be the game's
character or its central object, in the game's colours. Replace the template
emblem in `cover.cpp` and inspect it on the actual shelf.

Look at the other modules' `GAME.json` colours and choose a palette distinct
from the neighboring boxes. The shelf orders games by permanent `entry_id`.

Prototype the emblem with the canvas, then inspect it in the native shell:
`draw_emblem` in the game's `tools/preview.cpp`, shown on mock boxes by
`<ns>_preview box.png 0 0 1 box`. Then transcribe it. The calls correspond one to
one:

| Canvas (prototype) | Painter (in `cover.cpp`) |
|---|---|
| `canvas.fill_circle(x, y, r, colour)` | `disc(p, x, y, r, colour)` |
| `canvas.begin(); canvas.rrect(x, y, w, h, r); canvas.fill(colour)` | `p.fill_rounded_rect({x, y, w, h}, r, colour)` |
| `canvas.stroke_line(x0, y0, x1, y1, colour, width)` | `p.draw_line({x0, y0}, {x1, y1}, colour, width)` |
| a closed path of straight lines, filled | `paint_polygon(p, {{x0, y0}, {x1, y1}, ...}, colour)` |
| `tg::rgb(r, g, b)` | `rgb(r, g, b)` |

Keep to those shapes in the prototype so nothing is lost in the transcription.

## The command capsule

Inside a game, a floating pill at the top of the window is the whole menu.

- **Folded**, it shows the way back to the shelf, the game's name, and the
  game's *primary* commands.
- **On hover** (or pinned open with `···`), it opens to the game's other
  commands and the three master switches: Music, Sound and Motion.
- When the window is narrow, an open capsule wraps onto more rows.

A game supplies its commands by implementing `games::CommandSource`:

```cpp
struct GameCommand {
    std::string id;       // stable, lowercase: "new", "undo", "help"
    std::string label;    // what the button says
    bool enabled = true;
    bool checked = false; // shown held down: a panel that is open, a mode that is on
    bool primary = false; // stays visible on the folded capsule
};
virtual std::vector<GameCommand> commands() const = 0;
virtual void run_command(std::string_view id) = 0;
```

The shell asks for `commands()` five times a second while your game is open and
after every command, so the list may change with the game's state (Undo disabled
when there is nothing to undo). Keep the function cheap and free of side effects.

### The menu a game should have

| Id | Label | Notes |
|---|---|---|
| `new` | "New game", or the game's own word: "New board", "New meadow" | **Primary.** Starts another. A game in progress is abandoned without a dialog; it was saved, and New is one click. If abandoning loses something the player built over a long time, make the command a held press or a two-step, as Rock Stack's "Start over" is. |
| `undo` | "Undo" | When the rules allow taking a move back. Disabled when there is nothing to undo. |
| `hint` | "Hint" | When a hint is possible and fair. |
| a level control | "Next: Medium" | Cycles the difficulty or size for the *next* new game. An untouched game is replaced at once. |
| the game's panels | "Meadows", "Crew", "Records" | Each opens a panel the game draws on its own surface. `checked` while open. |
| `scores` | "Top scores" | When the game keeps a top-ten table. |
| `help` | "Help" | Keep the command for standalone testing. In PlaySuite, the shell routes it to the shared help document and omits it from the capsule. |

Rules:

- One or two primary commands, no more. The folded capsule must stay small.
- Labels are plain words in sentence case. They name what happens.
- A command that opens a panel is a toggle: running it again closes the panel.
- Do not offer Music, Sound, Motion or Back. Those are the shell's.
- Do not draw your own menu bar, back button or music switch on the game's
  surface. Controls that are part of play (the cards, a Check button beside the
  board, a bid) stay on the board, because that is where the player's eyes are.
  A control on the board must look like one; a Check button that players could
  not find was moved and made "a red button that depresses".

### Help

In PlaySuite, the shell owns one paper-style help document. The plain `?` in the
top-right corner, H or F1 opens it at the active game's section. H or Escape closes
it. M toggles the suite's music. Reserve H and M for those functions; a game's Hint
action stays visible in its command menu.

Write the game's goal, moves, ending and controls in its `help.md`. Keep the prose short and in the game's voice. The shared
view wraps and scrolls the text at the minimum window size.

The template's own help card remains a standalone/headless preview fallback.
Its `set_help(true)` calls `games::route_help(*this)` first, so hosted games never
open a second explanation panel. Actual game decisions, results and score entry
remain on the game surface.

### Scores

A game does not need a score table; a single best, or stars per level, is often
right. There is no shared top-ten helper for kit games yet: a game that wants the
full table draws its own panel and name entry (Atom Probe's `scores` panel in
`games/atomprobe` is the model).

The collection's policy: running scores are not shown as statistics. When a game
ends with a result worth keeping, it may enter a named, arcade-style top-ten for
that game and rules profile; only the best ten are kept, and a saved latch stops
the same result being entered twice. Simpler records (a best, stars per level, a
streak in play) are the game's own to show. There are no win rates, histories or
lifetime totals.

## The rail and the surface

A kit game draws its own pixels into a `LiveSurface`. The shell reserves a slim
rail, 50 points high, across the top for the capsule, and lays the game out below
it. The shared help document uses the toolkit overlay plane. Keep the template's
`on_paint` surface replay: ordinary exposes must restore the latest complete
frame even when no animation is publishing.

- The window's minimum is 600 x 420 points, so your smallest normal surface is
  **600 x 370**.
- When the capsule is open and has wrapped, the rail grows and the surface can
  shrink to about **600 x 320**. The game must still draw correctly.
- The surface's size in device pixels is its size in points times the window's
  scale (1, 1.5, 2, ...). Take the scale from `attached_window()` in `arrange`.
- Pointer events arrive in window coordinates. Convert with
  `point_from_window(event.position)`; the result is in points, relative to your
  surface.

## What the shell calls

```cpp
MyView(gf::StableId id, Options{.hosted = true});   // on the first visit, not at startup
void activate();                                    // shown: take keyboard focus
void set_cabinet(bool foreground, bool music, bool sound, bool reduced);
```

**Construction is lazy.** The shell creates a game the first time its box is
opened. Load the save and build the first position in the constructor, quickly.
Anything slow (a large generation) runs in the background while the game shows
its scene, never a blank surface.

**`set_cabinet(foreground, ...)`** is called whenever the visible game or a
master switch changes.

- `foreground == false`: the shelf or another game is showing. Stop the timer,
  silence all sound, drop hover and any held keys or drags. Keep the position;
  it is already saved. When the player returns, everything is exactly as left.
- `foreground == true`: resume. Reset your time origin so the first tick does
  not see a huge elapsed time. Request a frame.
- `music` and `sound` are the master switches. Pass them to the audio adapter
  and gate every sound on them.
- `reduced` is the Motion switch. When true, state changes appear at once: no
  easing, no shakes, no ambient motion. The game stays fully playable and looks
  finished.

**The shell silences its own music** when your game opens. Your music is yours
to start.

## Keyboard

- H or F1: shared Help. M: master music. Escape: close the open panel.
- Enter or Space: confirm, or continue after a result.
- Z, U or Backspace: Undo, where there is one. N: new game, where that is safe.
- Arrow keys move a visible cursor, so the game can be played without a mouse
  wherever its rules allow.
- `gf::PhysicalKey` names letters, arrows, Enter, Space, Escape, Backspace and
  the function keys. It has no names for the digit row; those are the HID usage
  codes `0x1E` for 1 through `0x26` for 9, and `0x27` for 0, compared against
  `event.physical_key` as Four Pegs and Switchbox do.
- Set `event.handled = true` only for keys you used. Unhandled keys belong to
  the shell.
- Keys act on the game's own directions, never the camera's: if a view can be
  rotated, "left" must not change meaning when it is.

## Pointer

- Set the hand cursor over anything clickable, the arrow elsewhere.
- Show hover: the thing under the pointer should look ready.
- A button drawn on the surface acts on release, and a press that begins on it
  and is released elsewhere does nothing.
- A piece on the board (a lamp, a card, a tile) may act on the press itself, and
  the first press on a draggable thing starts the drag.
- A hidden surprise shows no hand cursor; that is what keeps it hidden.
- The board stays the largest thing on the surface. At small sizes a character
  gives way first: a bust instead of a figure, a corner instead of a side.
- Targets are at least 36 points across at the smallest window.

## Accessibility

Call `set_accessible_name` with the game's name and its goal in one sentence.
Text contrast should be comfortable on every panel. Nothing that matters is
conveyed by colour alone: a lit lamp is also brighter and haloed; a suit has a
shape.

## What integrators had to fix

Every item here was a change made to a delivered game before it could go on the
shelf. A kit game starts with them done; do not undo them.

- Hand-built save paths, replaced with `games::state_directory()`.
- No `commands()` and `run_command()`; in-game menu buttons still drawn when hosted.
- The Motion switch ignored.
- Timers, audio and held keys still live behind the shelf.
- A frame rendered on every tick whether or not anything changed.
- Scale assumed to be 2; pointer positions used without conversion.
- A native presenter instead of `LiveSurface`.
- A working title in the code after the real name was chosen.
- A game created above the capsule in the window's order, covering the menu
  (the shell now places every game behind it; do not change child order).
