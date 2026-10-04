# Building a new PlaySuite game

You are an AI model working with a person who has a game idea. This file is the
whole procedure for turning that idea into a game on the PlaySuite shelf, delivered
as a pull request. It is written for any model (Claude, Codex or another) and
assumes only a fresh clone of this repository, CMake 3.25 or later, a C++20
compiler, Python 3 and git. Two steps use the network once each: fetching the
toolkit's headers for the syntax check, and the repository's own configure, which
downloads one pinned dependency.

Read this file to the end before you start. Read each guide when its step tells
you to; they are short, and the rules in them are checked.

## What you can and cannot do here

You can do everything except compile the PlaySuite application itself. That build
needs the GUI.Forms toolkit, its text stack and (on macOS and Linux) a Skia build;
the repository's CI does it on four platforms when the branch is pushed.

So you work from four things, all available on a fresh clone:

| You have | What it proves |
|---|---|
| The game's own headless build and tests (`vendor/<id>/CMakeLists.txt`) | The rules, the save, the layout at every window size, that animation settles |
| `<ns>_preview`, which renders any state at any size to a PNG with the real fonts | What the game looks like. Open the PNGs and look at them. |
| `check_game.py --fetch-toolkit`, which syntax-checks the view and the wired shell against the pinned GUI.Forms headers | That the hosted code compiles |
| `tools/check_game.py`, the gate | That the collection's rules are kept |

What that leaves unproven is how the game feels in the window: timing, sound
levels, the pointer. You must say so in the handoff. Never write that something
works when you only believe it does.

## The procedure

### 1. Decide whether it belongs, with the person

Read [guide/01-what-belongs.md](guide/01-what-belongs.md). Then talk with the
person before building anything. Ask about their idea until you could explain it
to someone else, and tell them plainly if it runs against the shelf's taste: too
long, too demanding, already covered, or a cliché. Offer the nearest version that
would fit. The person decides.

Do not start building on a first description. The owner of this collection works
the same way: discuss, settle, then begin.

If the person cannot be reached, make each choice you would have brought to them,
list those choices in the brief under "Decided without the person" and again in
`HANDOFF.md`, and keep them easy to reverse.

### 2. Write the brief

Read [guide/02-the-brief.md](guide/02-the-brief.md). The brief covers the menu,
the look and the sound, so read [guide/04-the-shell.md](guide/04-the-shell.md) and
[guide/06-look-sound-words.md](guide/06-look-sound-words.md) now as well. Agree on
a name, an id and a namespace, then create the game (the title is at most 28
characters, the kind 24, the blurb 90):

```sh
python3 new-games/tools/new_game.py --id tidepools --namespace tp --title "Tide Pools" \
    --kind "Shore puzzle" --blurb "One sentence that makes someone pick up the box."
```

This creates `vendor/<id>/` as a complete, building copy of the kit's template
game under your names. Write the brief into `vendor/<id>/README.md` and show it to
the person. Build nothing more until they agree with it.

### 3. Build the rules first

Read [guide/03-anatomy.md](guide/03-anatomy.md) and
[guide/10-house-style.md](guide/10-house-style.md).

Replace `src/rules.*` with your game, pure and deterministic, and its tests in
`tests/rules_tests.cpp`. If the game generates puzzles, a test must prove every
generated puzzle can be finished. Keep the save envelope in `src/save.*`.

```sh
cmake -S vendor/<id> -B .build/new-games/<id> && cmake --build .build/new-games/<id> --parallel 2
ctest --test-dir .build/new-games/<id> --output-on-failure
```

### 4. Build the picture

Read [guide/06-look-sound-words.md](guide/06-look-sound-words.md) and
[guide/07-primitives.md](guide/07-primitives.md).

Put layout and animation state in `src/stage.*` (pure, tested across every window
size) and drawing in `src/scene.*`. Extend `tools/preview.cpp` so it can render
every state the game has: the start, mid-game, each ending, every panel.

```sh
.build/new-games/<id>/<ns>_preview shot.png 1100 760 1 mid
.build/new-games/<id>/<ns>_preview small.png 600 370 1 mid
.build/new-games/<id>/<ns>_preview help.png 600 320 2 help
```

Look at every picture you render. Then show the person. Expect several rounds;
that is the work, and it is where the game becomes good.

`<ns>_preview box.png 0 0 1 box` draws a mock of the game's box on the shelf.
Prototype the emblem there (`draw_emblem` in `tools/preview.cpp`) before step 7.

### 5. Build the view

Read [guide/04-the-shell.md](guide/04-the-shell.md) and
[guide/05-performance.md](guide/05-performance.md).

`src/<id>_view.*` is the only file that touches GUI.Forms and the shell. Keep the
template's structure: input becomes moves, `request_frame()` wakes the timer, the
timer stops when the picture has settled, `set_cabinet` silences and stops
everything when the shelf is showing. Offer commands through `commands()` and
`run_command()`. You cannot run this file here, so change its plumbing as little
as you can and keep logic in the files you can test.

### 6. Make the sounds

All audio is synthesized from scripts kept in `vendor/<id>/audio_src/`. Render
into `assets/audio/` with your prefix. A sound with no file stays silent, so this
step can come late. See [guide/06-look-sound-words.md](guide/06-look-sound-words.md).

### 7. Put it on the shelf

Read [guide/08-on-the-shelf.md](guide/08-on-the-shelf.md).

```sh
python3 new-games/tools/wire_shelf.py <id>
```

Then draw the box emblem in `src/suite.cpp`, and add the game to the roster
sentences in `README.md` and `docs/GAME_CATALOG.md`. Run `wire_shelf.py` again
whenever you add a source file to `GAME.json` or a sound to `assets/audio/`.

### 8. Pass the gate

```sh
python3 new-games/tools/check_game.py <id> --fetch-toolkit
```

Fix every FAIL. For each WARN, either fix it or record the decision in
`HANDOFF.md`. Then build and run the repository's portable-core profile once, as
its CI will, and the repository's own style check:

```sh
cmake -S . -B .build/portable-core -DCMAKE_BUILD_TYPE=Release -DGAMES_BUILD_APPLICATION=OFF
cmake --build .build/portable-core --parallel 2
ctest --test-dir .build/portable-core --output-on-failure --timeout 120
python3 scripts/check-style.py
```

### 9. Deliver

Read [guide/09-deliver.md](guide/09-deliver.md). Save the screenshots you looked
at into `vendor/<id>/screens/` and compress them with `tools/shrink_png.py`, write `vendor/<id>/HANDOFF.md` honestly, and
commit on a branch. **Push and open the pull request only when the person says
to.** After CI builds the application, download the build, and tell the person
how to play their game in it.

## Rules that are not negotiable

These are checked by the gate, by CI, or by the maintainer.

1. **Standalone.** The game is one folder in one namespace. It includes nothing
   from another game's folder and uses no other game's namespace. What you borrow,
   you copy. It needs no network, no account, no other program, and no code that
   exists for only one operating system.
2. **Deterministic.** The same seed gives the same game on every platform. Use
   the seeded integer generator pattern in the template's `rules.cpp`. Do not use
   `rand`, `std::random_device`, library distributions, or fast-math.
3. **Idle means idle.** A game nobody is touching schedules no timer and
   publishes no frame. A game hidden behind the shelf does nothing at all.
4. **It fits.** The layout reflows from 600 x 370 points (and survives 600 x 320)
   to a full screen, at display scales of 1, 1.5 and 2.
5. **It saves.** Every committed change is saved at once, in a versioned,
   checksummed file, replaced atomically. Reopening resumes exactly. A damaged
   save starts a fresh game without complaint.
6. **The shell owns the menu.** Commands live in the capsule. Music, Sound and
   Motion are the shell's master switches; the game obeys them.
7. **Existing entries never move.** `Entry` values are persisted in players'
   saves. Append only.
8. **Nothing existing is weakened.** Do not remove a game, simplify one, disable a
   test, or edit the toolkit to make something pass.
9. **House style** for every file you write.
10. **Everything is original or public domain.** No brand names, logos, or
    characters and music that belong to someone else.
11. **Report honestly.** `HANDOFF.md` separates what you verified from what you
    did not.

## Working with the person

- Their plain complaints are specifications. "It feels janky" means find out
  what is janky and fix it; do not explain why it is that way.
- When they report a bug you cannot see in your picture, believe them and build
  a way to see it (a marked test object, a labelled preview). They have been
  right about this before.
- Bring them decisions, with a recommendation, one at a time. Do not bring them
  build output.
- When they say the game is not finished, it is not finished. The collection's
  bar is polish.
- Use the name they chose, and use it everywhere from the start. A rename after
  delivery touches the save file, the shelf entry and the audio prefix.

## If something here is wrong

The shell changes. If a tool's anchor is missing, a header has moved, or a guide
disagrees with the source, the source wins: read `src/collection.cpp`,
`src/suite.hpp` and the newest game under `vendor/`, do the step by hand, and
note the mismatch in `HANDOFF.md` so the kit can be corrected.

The repository's root `AGENTS.md` also applies to you. Where it speaks of
`incoming/` packages and vendor copies, that describes games delivered from
another machine for porting; a kit game is written portable from the start and
lives only in `vendor/<id>/`.
