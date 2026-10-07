# Shared engines

A kit game normally owns everything it uses: it copies what it borrows into its own
folder and namespace ([primitives](07-primitives.md)). That keeps games independent,
but it has a cost once a family of games is expected to share a large piece of
machinery: each copy diverges, and an improvement reaches only the game it was made
in. The owner's rule for that case: an engine anticipated for a large collection, or
shared between games, is promoted ("upstaged") to collection infrastructure.

A shared engine lives once, in `shared/<id>/`, and games declare that they build on
it. The card and puzzle engines in `src/` were the first shared engines; `shared/`
is where new ones go, discovered like games, with no edits to the shell.

## When to make one

Promote code to an engine when all of these hold:

- Two or more games use it now, or a planned series of games will (a set of ambient
  scenes, a family of card games). One game is not a series; copy instead.
- It is machinery, not content. Rules, art, words, sounds and tuning stay in the
  games. An engine takes them as data or through a small interface.
- It can be tested without any game: its own tests run in its folder.
- Someone will look after it: an engine change is a change to every game on it.

Otherwise, borrow by copying, as before.

## The folder

```
shared/<id>/
  ENGINE.json          id, namespace, title, summary, build file, file lists
  README.md            what it does, its interface, its costs, its limits
  build.cmake          its core library, interface target and tests; ENGINE_DIR is set
  src/                 the portable core (no GUI.Forms, no clock, no threads)
  ui/                  optional: GUI.Forms code compiled only when a game uses the engine
  tests/               its own tests, registered with CTest
```

`ENGINE.json`:

| Field | Meaning |
|---|---|
| `id`, `namespace` | The folder name and the C++ namespace. Both are reserved for every game. |
| `title`, `summary` | What it is, in a phrase and a sentence. |
| `build` | CMake included once, before the games, with `ENGINE_DIR` set. Guard it with `if(TARGET <core>) return() endif()`. |
| `libraries` | The core targets a game links (for example `ambient_core`). |
| `ui_sources`, `ui_libraries` | Interface code added to the application's module library, and the interface target that carries its headers, only when an enabled game uses the engine. |
| `source_directories`, `ui_directories` | Include directories, used by the gate's syntax check. |
| `tests` | The engine's test sources, for reference; `build.cmake` registers them. |

`tools/game_catalog.py` validates engines before games: unique ids and namespaces,
files inside the folder, and that every engine a game names exists.

## Building a game on an engine

In `GAME.json`:

```json
"engines": ["ambient"],
"libraries": ["sw_core"]
```

and link the engine's core from the game's own core in `build.cmake`
(`target_link_libraries(sw_core PUBLIC ambient_core)`). The game's
`CMakeLists.txt` includes `cmake/StandaloneGame.cmake`, which builds the engines the
manifest names before the game, so the game's own headless build and tests still
run on their own.

The gate (`check_game.py`) knows engines: a game may use the namespaces of the
engines it declares and no other game's; the contract check reads the engine's view
as well as the game's; the syntax check adds the engine's include directories. A
game whose sounds the engine plays may omit `audio_adapter`.

## Looking after an engine

- Keep the core portable and deterministic, like a game's rules. Interface code is
  the only place for GUI.Forms, clocks and worker threads.
- Never break a game on it. Change the engine and every game that uses it in the
  same change, and run each game's tests. CI builds all of them.
- Keep data formats versioned. An engine that reads files (archives, levels) checks
  them completely and refuses a damaged one without disturbing what it had.
- Write down what it costs. An engine that animates owns its processor budget and
  says in its README how it keeps it.
- Follow the [house style](10-house-style.md); `scripts/check-style.py` checks
  `shared/` by default.

## The engines

| Engine | For | Games |
|---|---|---|
| [`ambient`](../../shared/ambient/README.md) | Living scenes drawn on the processor alone: retained 3D scenery, swaying foliage, creatures, animated light, all under a measured CPU budget | Stillwater |
