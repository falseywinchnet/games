# Games

One native C++20 / GUI.Forms application whose collection contains: Solitaire, Spider, FreeCell, Hearts, Sudoku, Gems, Nature Cube, Untangle, Atom Probe, Four Pegs, Switchbox, Puzzle Solve, and Eggy and the Very, Very Tall Mountain.

The collection uses the GUI.Forms File Manager house composition: a pearl command area, subdued watercolor identity band, category navigation, a white game field, and a selection/details pane. Single click selects; double click or Play / Continue opens the game. Game views retain their individual boards within the shared shell. Card tables use Plan Paint's actual fiber-rendered billiard felt, original cream card faces, and four coordinated backs. The cube has three visible and playable 4Ã—4 faces, pronounced mouse-follow tilt, reflective environment sampling, and surface picking; gems are animated faceted meshes.

## Build status and downloads

The [Build installable Games workflow](https://github.com/falseywinchnet/games/actions/workflows/applications.yml) builds the complete collection on native Windows x64, macOS arm64, Linux x64 and Linux arm64 runners. Successful runs provide installers and portable archives. Version tags publish tested packages to [Releases](https://github.com/falseywinchnet/games/releases).

The complete application passes all 22 tests on each of those four native platforms. The [validated application run](https://github.com/falseywinchnet/games/actions/runs/36953930003) also installs and launches the Windows, macOS and Linux packages with isolated saves. Download installers and portable archives from [the latest release](https://github.com/falseywinchnet/games/releases/latest). macOS packages target Apple silicon and macOS 15 or later and are signed ad hoc, without Developer ID notarization. Linux packages target Ubuntu 24.04 or compatible systems and require X11.

Games links reviewed GUI.Forms source for native windows, portable text and compact audio; no development SDK is installed or exported. CMake fetches pinned QuickJS for offline Sudoku. No external JavaScript runtime or server is needed. FFmpeg and Pillow prepare release assets and are not runtime requirements. See [complete build instructions](docs/BUILDING.md).

The smaller portable-core profile remains useful for rules and numerical work:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGAMES_BUILD_APPLICATION=OFF
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure --timeout 120
```

## Current games and scores

Every game saves committed changes, keeps its own position when switching games, resumes on reopening, and has an explicit New game action. Saves live in `%APPDATA%/Rainstar/Games` on Windows, `~/Library/Application Support/Rainstar/Games/` on macOS, and `$XDG_STATE_HOME/rainstar/games` (default `~/.local/state/rainstar/games`) on Linux. `GAMES_STATE_DIR` overrides this for isolated tests. Files use bounded, checksummed envelopes and atomic replacement. The existing version-one card cabinet migrates on load. Undo history is session-local; current boards, Sudoku notes and mistakes, puzzle progress, and named scores persist.

The card and logic games have no running score HUD or statistics system. Eggy retains its authored achievement HUD (altitude reached, collected stars, elapsed climbing time, and breath), its separate save, and its time-and-stars top-ten list. Terminal results open a named arcade-style top-score window. Only the best ten results per game/rules profile are retained. A saved submission latch prevents the same result from being entered twice. Sudoku ranks by fewest mistakes; Gems and Klondike by points; Hearts by lowest final penalty total; other puzzles and card profiles use their documented move/probe ranking. Remaining guesses and current Sudoku mistakes are gameplay information, not lifetime statistics.

Use Collection to choose a game; Card tables contains the four card games. Music, Sound, and Motion are shared master controls. Eggy also retains its own local music/sound switches; the shared masters gate its separate audio engine. Switching away immediately hides its drawing surface and silences music, effects, and ambience while its climb continues. Each game has its own Help and Top scores controls. Card Options selects draw count, Spider suits, and card backs. Sudoku's difficulty selector applies to the next New game.

## Architecture and references

- `src/game.*`, `scores.*`, `storage.*`: card rules, profile rankings, and cabinet persistence.
- `src/sudoku.*`, `sudoku_bridge.cpp`: native state and the canonical handed-off generator running in the pinned QuickJS engine on a background job.
- `src/puzzles.*`: six active puzzle models and retained legacy support, seeded generation, witnesses, rankings, and persistence.
- `src/table.*`, `sudoku_view.*`, `puzzle_view.*`: native presentation, input, animation, help, and score entry.
- `src/presentation.*`, `collection.*`: custom controls and the shared collection shell.
- `src/puzzle_render.*`, `puzzle_image.cpp`: CPU triangle rasterization, z buffer, picking, and prepared environment pixels.
- `vendor/eggy/`: cabinet copy of the delivered Eggy control, simulation, renderer, audio, and tests; the original package remains unchanged in `incoming/game-eggy-01/`.
- `src/audio.cpp`, `scene_audio.*`, `fourpegs_audio.*`: portable cabinet audio policy, effects, and score transitions through GUI.Forms Audio.

See [the game catalog](docs/GAME_CATALOG.md) for exact rules and provenance, [validation](docs/VALIDATION.md) for evidence and limitations, [audio integration](docs/AUDIO_INTEGRATION.md) for the delivered audio batches, and [source references](docs/SOURCE_REFERENCES.md). Catching Thieves remains a separately requested planned module; it is not one of the thirteen playable games. Rendezvous Riders is excluded.

Atom Probe, Four Pegs and Switchbox now select the owner's replacement controls in collection source. All three use shared portable text, audio and presentation services and retain separate v2 saves. Sticks & Stones is retired. Existing assets and saves are retained. Nature Cube and Puzzle Solve now use separate `-v2.txt` saves, copying prior player names and scores without overwriting their old boards. See [the owner handoff](docs/OWNER_MODULE_HANDOFF.md) and [Eggy integration](docs/EGGY_INTEGRATION.md).
