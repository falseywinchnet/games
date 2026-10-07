# Automatic shelf discovery

A game joins the suite when its complete folder is placed in `games/<id>/`.
CMake discovers `GAME.json` on configure and regenerates the catalog in the build
directory. No generated file belongs in the source tree.

```sh
python3 new-games/tools/wire_shelf.py <id>
python3 tools/game_catalog.py --help
```

`wire_shelf.py` validates the manifest and asset inventory. Its legacy `--dry-run`
option is also read-only. It never patches the shell.

## The module contract

| Folder file or field | Responsibility |
|---|---|
| `GAME.json` | Permanent integer `entry_id`, unique slug/namespace, title, kind, blurb, three RGB `colors`, file lists and optional help topics |
| `module.cpp` | Factory returning `GameInstance`; receives the host/development context |
| `cover.cpp` | Proportional GUI.Forms `Painter` emblem; shared drawing helpers are in `shelf_art.hpp` |
| `help.md` | Full instructions used by the shared help document |
| `build.cmake` | Core libraries, independent rule tests and hosted contract tests; use `GAME_MODULE_DIR` for local paths |
| `CMakeLists.txt` | Independent headless configure/build for this game |
| `assets/` | Prefixed audio and namespaced resources; no central count or copy rule |
| `HANDOFF.md`, `screens/` | Honest validation evidence and inspected presentation |

`ui_sources`, `audio_sources` and `libraries` are read from the manifest.
`engines` lists the shared engines (`shared/<id>/`) the game builds on; they are
validated and built before the games ([shared engines](11-shared-engines.md)).
`help_topics` contains `{ "id": "sets", "title": "Card sets", "file": "help-sets.md" }`
objects. `module`, `cover`, `help` and `build` name files within this folder.
See the template for a complete manifest and factory.

Order is ascending permanent `entry_id`. IDs are explicit, sparse and never
renumbered. Disabled manifests (`enabled: false`) reserve their IDs, slugs,
namespaces and symbols. The suite migrates older bitmask state to an extensible
list of opened IDs and retains unknown IDs if a game is temporarily absent.

The shelf derives its count and boxes from the catalog. Its native vertical
scrollbar, keyboard focus reveal and shared help contents grow with the catalog.
There is no per-game edit to the collection, shelf, help source or asset scripts.

## Before copying a prototype into vendor

Build its core independently, then configure the native suite with:

```sh
cmake -S . -B <native-build> -DGAMES_EXTRA_GAME_DIRS=/absolute/path/to/game <platform-options>
```

Use semicolon-separated paths for multiple external folders. Run the game with
`--game <id> --standalone --dev`, then in hosted mode. The same factory, render
code and assets are exercised. Prepare runtime assets with `--extra-game-dir <folder>`
for external prototypes; check the asset tool's `--help` for syntax.

After copying that folder to `games/<id>/`, clear `GAMES_EXTRA_GAME_DIRS` in the
build cache and configure again. Build and run all tests. Check the hosted box,
help, masters, input, persistence and minimum window size. Publishing after the
normal merge uses the shared workflow's generated version and tested artifacts.
No central registration or version edit is part of adding a game.
