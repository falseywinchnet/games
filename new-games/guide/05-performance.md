# Performance

The rule, from `docs/PERFORMANCE.md`: *a settled view reuses its last output.
Animation, input, layout changes, asynchronous results and timed game actions are
reasons to do work; the passage of another display interval alone is not.*

PlaySuite sits open on a work machine all day. The owner's standard for an open,
untouched game is "basically nearly no CPU use". When this was measured on the
collection, a settled Koi-Koi table was using 72.8 percent of a core redrawing an
identical picture. After the fix it used 0.3 percent. The shelf itself went from
135 MiB to 34 MiB by not creating games nobody had opened. Those two failures,
drawing when nothing changed and allocating before being asked, are the ones to
design against.

## Idle means idle

A kit game's view is built around one function and one flag:

```cpp
void request_frame();   // something changed: make sure a tick is coming
bool render_dirty_;     // the picture is out of date
```

- Every input, command, resize, scale change and return from the shelf calls
  `request_frame()`. It marks the picture dirty and starts the timer if stopped.
- Each tick advances the stage by the elapsed time. `advance()` returns whether
  anything is still moving.
- A frame is drawn and published only if the picture is dirty or something
  moved.
- When nothing is moving and nothing is dirty, the timer **stops**. It is slowed
  to 50 ms instead while a sound is still loading or fading
  (`audio_needs_tick()`), and stops after.

So an untouched game has no timer, draws nothing and publishes nothing. The
hosted contract test checks exactly this: after the opening settles, the window
has no wake scheduled, and a simulated second later the published frame count
has not changed.

For this to work, animation must actually end:

- Ease toward a target, and when the remaining gap cannot be seen, **set the
  value exactly to the target**. An exponential ease never arrives on its own.
  The template lands when the gap is under 0.004; Koi-Koi lands a card when it is
  within a hundredth of a device pixel.
- `advance()` must return false once everything has landed. Test it
  (`test_settles` in the template).
- Timed events (a computer opponent thinking, a banner that leaves after two
  seconds) keep the timer running only until they fire.

**Reacting to a pause.** A character who remarks on a long silence does not need
a running timer to notice it. The view records when the player last did
something, and once the picture has settled it sets the timer's interval to the
time remaining and sleeps: one wake, twenty-five seconds later, no frames in
between. The template does this for its waiting remark (`seconds_until_remark`,
`update_remark` and `note_player`); in a real window, twenty-eight untouched
seconds cost one timer callback and one frame. After its remark the game
schedules nothing. The contract test allows this: a settled game may hold one
wake that is a second or more away. Keep such reactions to one or two remarks and
then quiet. Do not put a countdown inside `advance()`; anything that makes it
return true redraws at sixty frames a second.

**Ambient motion** (a character breathing, water moving) keeps a game from ever
settling. Prefer a character who moves when something happens and is still
otherwise. If the game truly needs ambient life, give it a low cadence (10 to 15
frames a second is plenty for breathing), stop it when the window is not active,
stop it under reduced motion, and set `"settles_when_untouched": false` in
`GAME.json` and in the contract test, with the reason in `HANDOFF.md`. Expect the
maintainer to ask whether it is worth its cost.

## Hidden means stopped

When the shelf or another game is in front, `set_cabinet(false, ...)` arrives.
From then until `set_cabinet(true, ...)`:

- no timer callbacks;
- no frames;
- no sound (the audio adapter releases its buffers);
- no simulation. The position is saved, and returning resumes it exactly.

The contract test counts timer callbacks while the game is hidden and requires
zero.

## Start fast

The shell creates your game when its box is first opened, and the player is
watching.

- The first frame must be the game, not a blank or a loading card. Background
  textures and static scenery are part of that first frame; a card table whose
  felt arrived late was a reported bug.
- Do in the constructor only what the first frame needs. Build caches when first
  used.
- If generating a puzzle can take longer than about a tenth of a second, do it
  off the UI thread and show the scene meanwhile. The owner's question about a
  slow generator was "can't we do this in 300 ms?", and the answer was to make
  it fast, then move it off the thread.
- Do not decode or allocate for states the player may never reach.

## Draw simply, then cheaply

A full redraw of the frame is the normal way to draw. Do not build a
dirty-rectangle system; the owner turned one down as "too much bookkeeping" when
a whole-buffer redraw on demand is cheaper and always correct. Since frames are
only drawn when something changes, the cost of one frame matters while things
move, and then it matters a great deal: sixty of them a second.

The kit's targets, at a 1180 x 800 point surface at scale 2. They are chosen to
leave headroom on an ordinary laptop; they are goals to design for, and what you
measured goes in `HANDOFF.md`:

| | Target |
|---|---|
| One frame while animating | Under 8 ms, so sixty a second leaves room |
| One frame of a heavy 3D scene | Under 16 ms; run it at thirty a second if it cannot do better |
| A move's rule computation | Under 1 ms |
| Opening the game to first frame | Under 300 ms |
| Tests | A few seconds |

Ways to get there, in the order to try them:

1. **Draw the static part once.** Render the background, the board and anything
   that does not change into a canvas kept by the view, and start each frame by
   copying its pixels (`frame.px = backdrop.px` when the sizes match;
   `draw_canvas` blends pixel by pixel and is for layers with transparency). Rebuild it on resize, scale change or a new game. Pen the Sheep
   caches its landscape this way; Rock Stack keeps the shadows of everything
   that is not moving.
2. **Draw at lower resolution where the style allows.** The collection's 3D
   games render their scene at one scene pixel per 1.25 to 2 points and scale it
   up with nearest-neighbour or dithering, then draw text and interface at full
   resolution on top. "That means some pixelation," the owner said, and chose it
   deliberately: efficiency is part of the style. Crisp text over a chunky scene
   is the house look.
3. **Draw fewer, larger shapes.** A gradient fill is one path. A thousand small
   circles are a thousand.
4. **Keep text masks.** `text_mask` caches by content, so the same words cost
   nothing the second time. Text that changes every frame (a counter racing
   upward) makes a new mask each time; change it in steps.
5. **Measure before anything cleverer.** Add a small `tools/bench.cpp` that
   renders a hundred frames and prints the mean, and put the numbers in
   `HANDOFF.md`.

Do not trade away animation, image quality, behaviour or saved state to improve
a number. `AGENTS.md` at the repository root says this in so many words.

## Hot paths

The house style has a section on repeated kernels. In short, inside a
per-pixel, per-cell or per-body loop:

- no allocation, no container growth, no string building, no logging;
- decide modes and formats before the loop, and run one plain loop per case;
- index contiguous storage directly, innermost over the contiguous dimension;
- read fixed parameters into locals first and publish results after.

Allocate workspaces once, on their owner, and reuse them. A `std::vector`
member cleared and refilled each frame is fine; a `std::vector` constructed
inside the frame loop is a finding.

## Threads

The core has no threads; the gate enforces it. A view may use one background
task for generation (`std::async` returning a value, checked with `wait_for(0)`
on a tick) and must wait for it in the destructor.

Do not add worker threads to rendering without a measured need. Rock Stack's
banded renderer did, and delivered an intermittent crash on ARM64 from mesh and
texture caches shared between workers without synchronization. If you must:

- workers read immutable inputs and write disjoint outputs only;
- every cache a worker can reach is filled before the workers start, or is
  owned by one worker;
- the output is identical, bit for bit, to the single-threaded result, and a
  test says so.

## Memory

- Keep one representation of each asset. Do not hold both a source image and
  its prepared pixels.
- Size caches by need and give them a trim (`text_cache_trim` runs each frame).
- The frame canvas is width x height x 4 bytes in device pixels; a low-resolution
  scene buffer plus one static layer is the normal ceiling.
- Sound files are prepared once for the whole suite; a game's audio should be a
  few megabytes of source at most. Loops of 60 to 120 seconds, effects under a
  second or two.

## Measuring in the application

Build the native application with the pinned toolkit using `docs/BUILDING.md`
and the root host instructions. The executable takes `--profile-idle N` (N is
the permanent game ID from `--list-games`, or -1 for the shelf): it waits five
seconds, samples ten, and reports window activity. `tools/profile_windows.py`
adds process CPU sampling. Measure visible, inactive and hidden states. Deliberate
live actors are reported separately from settled scenes; do not delete animation
to lower the metric. Record the actual command, build, state and numbers in
`HANDOFF.md`; say what remains unmeasured.
