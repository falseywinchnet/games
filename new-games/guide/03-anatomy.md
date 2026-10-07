# Anatomy of a game

A kit game is one folder, `games/<id>/`, in one C++ namespace. This guide
explains what is in the folder, why it is split the way it is, and what
"standalone" requires.

## The folder

```
games/<id>/
  GAME.json                 names, colours and file lists the tools read
  README.md                 the brief
  HANDOFF.md                what was verified and what was not
  CMakeLists.txt            the game's own headless build
  build.cmake              core, native and test targets used by discovery
  module.cpp               the GameInstance factory
  cover.cpp                the shelf emblem
  help.md                  player instructions
  assets/                  prefixed audio and game resources
  src/
    rules.hpp  rules.cpp    the game: pure, deterministic, no pixels
    save.hpp   save.cpp     the save envelope
    stage.hpp  stage.cpp    layout and animation state: pure geometry and easing
    scene.hpp  scene.cpp    draws one frame into a canvas
    <id>_view.hpp  .cpp     the GUI.Forms control: input, timer, publishing, commands
    platform/
      raster.hpp  .cpp      the shared 2D rasteriser, copied into your namespace
      text.hpp   text.cpp   text masks (hosted implementation)
      audio.hpp  audio.cpp  music and effects (hosted implementation)
  dev/                      headless stand-ins for text and audio; never shipped
  tests/
    rules_tests.cpp         rules, save, layout sweep, settling
    view_contract_tests.cpp the hosted contract, built with the application
  tools/preview.cpp         renders any state to a PNG
  audio_src/                the scripts that synthesize the game's sounds
  screens/                  the pictures you looked at
```

Sounds render into this game's `assets/audio/` with a unique prefix. Everything
authored for the module stays in its folder; discovery supplies the shell wiring.

## Three layers

The split exists so that as much of the game as possible can be run and proved
on a machine that cannot build the PlaySuite window.

**The core** (`rules`, `save`, `stage`, `platform/raster`) includes nothing but
the standard library. It has no clock, no file paths of its own, no threads, no
text and no sound. Time comes in as a parameter. The repository's portable-core
profile builds it and runs its tests on Windows, macOS and two Linux
architectures, so these tests are the ones you can rely on.

**The scene** (`scene`) turns the core's state into pixels. It calls the text
interface, which has two implementations: the application's and the headless
harness's. The same scene code draws the frame the player sees and the PNG you
look at.

**The view** (`<id>_view`) is the adapter between the game and PlaySuite. It is
tested in the native standalone and hosted windows. It holds little logic: it
converts input to calls on the core, advances the stage, asks the scene for a
frame and publishes it.

When you are unsure where something goes, put it as low as it can go. A rule
about which moves are legal is core. Where a button sits is stage. What colour
it is belongs to the scene. That a click reached it is the view.

`GAME.json` lists which files are core (`core_sources`) and which are compiled
only into the application (`ui_sources`, `audio_adapter`). When you add a file,
add it to the right manifest list and the module-owned build targets. CMake
regenerates the collection registry on configure. `wire_shelf.py` validates it.

## Standalone

The owner's instruction for this collection was that nothing in it should use
"any os-specific anything", and that a game must never depend on a server. For a
kit game that means:

**One namespace, one folder.** Everything is inside `namespace <ns>`. The game
includes no header from another game's folder and names no other game's
namespace. This is why `raster.cpp` is copied into each game instead of shared:
a game can be read, built, changed and removed without touching any other.

**Borrow by copying.** Eggy, Switchbox and Pen the Sheep are a platform to
borrow from, and you should: the 3D renderer, the mesh helpers, a cloth or felt
routine, a synthesizer. Copy the file into your folder, change its namespace to
yours, list it under `borrowed` in `GAME.json`, and say where it came from in
`HANDOFF.md`. Borrowed files are exempt from the house-style check as long as
you leave them unchanged.

**Portable C++20 only.** No Objective-C, no platform headers, no `#ifdef` for an
operating system. The only platform-specific code in the collection is inside
GUI.Forms. If your game seems to need something from the operating system,
what it needs is a toolkit feature; describe it in `HANDOFF.md` for the
maintainer and find another way for now.

**No network, no other programs.** No sockets, no HTTP, no spawning a process,
no scripting runtime. A puzzle generator runs in the game. If a generator needs
a difficulty rating, the game computes it.

**No new dependencies.** The standard library and GUI.Forms are what the game
may use. If you need an algorithm, write it; that is the tradition here (Rock
Stack has its own rigid-body engine because "it must be our own engine").
Development scripts may use Python with the standard library; sound scripts may
use NumPy.

**Deterministic.** The same seed and the same moves give the same game on every
platform, bit for bit. That is what makes a save file a seed plus a list of
moves, and what makes a bug report reproducible.

- Random numbers come from an integer generator you seed yourself. The template
  uses splitmix64 (`next_random` in `rules.cpp`). `rand`, `std::random_device`,
  `std::mt19937` with library distributions, and the clock are not sources of
  game randomness: the standard does not fix what distributions return, so
  results differ between compilers.
- Reduce a random integer with `%` or integer arithmetic you write, never with
  `std::uniform_int_distribution`.
- Floating-point rules are allowed, in `double`, without fast-math. If results
  must match across platforms (physics, generated geometry), build that code with
  contraction off, as Rock Stack does (`-ffp-contract=off`, `/fp:precise`), and
  avoid library functions whose last bit varies (`std::pow`, `std::sin`) inside
  decisions that affect the game's state.
- A new game's seed may come from the clock. Everything after that follows from
  the seed.

## Saves

Every game keeps one file in the collection's state folder, named
`<snake_title>-v1.txt`. The view finds the folder with `games::state_directory()`
(which honours `GAMES_STATE_DIR`, used by tests to isolate saves). Never build a
path from `HOME` or an environment variable yourself.

What a save must do:

- **Hold the position, not a picture of memory.** Text, `key=value` lines. A
  seed and the moves made, where the game allows; otherwise the full state in
  readable form.
- **Be versioned.** The first line is a magic word ending in the format's
  number (`TIDEPOOLS1`). When the format changes incompatibly, write a new file
  name (`-v2.txt`) and leave the old file untouched.
- **Be checked.** A checksum line ends the file. A file that fails the check, is
  too large, or describes an impossible position is ignored and the game starts
  fresh. Loading validates every field and rebuilds the position through the
  rules, so a save can never produce a state the rules could not.
- **Be replaced atomically.** Write beside the target and rename. A crash leaves
  the previous save.
- **Be bounded.** State the maximum size and refuse to read more.
- **Be written at once.** Save on every committed change (a move, a new game, a
  setting). Do not wait for the window to close; do not restart music or
  animation because a save happened.

Also saved: the player's choices for the game (level, size, which site), bests
and top scores. Not saved: undo history beyond what the move list gives you,
anything about the window, and anything personal.

`save.hpp` in the template does all of this. Keep it, change the magic word (the
scaffold does), and write your own `encode` and `decode`.

A `--dev` build of the view (`Options{.dev = true}`) uses a separate
`-dev-v1.txt` file, so tests and experiments never touch a player's game.

## Tests that belong in every game

The template's `tests/rules_tests.cpp` shows each of these. Keep them and add
your own.

| Test | Why |
|---|---|
| The same seed gives the same game; a known seed's output is pinned | Saves and bug reports depend on it |
| Every generated puzzle can be finished, shown by an independent solver | Fairness is a promise |
| Illegal moves are refused and change nothing | The view can pass anything |
| Undo restores the exact position | |
| Save round-trips; one flipped bit, a foreign file, an oversized file and an impossible position are all refused; a refused load leaves the game untouched | |
| The layout stays on the surface and does not overlap itself, swept from below the minimum to a large display | The sweep complements native window resizing |
| Animation settles in bounded time and then reports "not moving" | Idle depends on it |
| Reduced motion applies changes at once | |

A new game's tests must be deterministic and quick: the whole file in a few
seconds (some of the collection's older solver tests run longer; do not add to
that). Do not assert on wall-clock time: Rock Stack's timing test failed on a busy machine
and had to be moved out of the required set. Measure speed with a separate tool
and report the numbers in `HANDOFF.md`.
