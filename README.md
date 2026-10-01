# Games

One native C++20 / GUI.Forms application with twelve playable games: Solitaire, Spider, FreeCell, Hearts, Sudoku, Gems, Nature Cube, Untangle, Atom Probe, Four Pegs, Puzzle Solve, and Eggy and the Very, Very Tall Mountain.

The collection uses the GUI.Forms File Manager house composition: a pearl command area, subdued watercolor identity band, category navigation, a white game field, and a selection/details pane. Single click selects; double click or Play / Continue opens the game. Game views retain their individual boards within the shared shell. Card tables use Plan Paint's actual fiber-rendered billiard felt, original cream card faces, and four coordinated backs. The cube has three visible and playable 4×4 faces, pronounced mouse-follow tilt, reflective environment sampling, and surface picking; gems are animated faceted meshes.

## Build and launch

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/Users/joshuahkuttenkuler/Developer/Projects/gui-forms-investigation/paint-port-sdk
cmake --build build -j3
ctest --test-dir build --output-on-failure
open build/games.app
```

The bundle includes artwork, the GUI.Forms runtime, fonts, the offline Sudoku engine, audio, and notices. No external Node installation or server is required. This build is verified on macOS; Windows and Linux have not been tested. Distribution signing and notarization are separate from the local ad-hoc signature.

## Current games and scores

Every game saves committed changes, keeps its own position when switching games, resumes on reopening, and has an explicit New game action. Saves live in `~/Library/Application Support/Rainstar/Games/`; `GAMES_STATE_DIR` overrides this for isolated tests. Files use bounded, checksummed envelopes and atomic replacement. The existing version-one card cabinet migrates on load. Undo history is session-local; current boards, Sudoku notes and mistakes, puzzle progress, and named scores persist.

The card and logic games have no running score HUD or statistics system. Eggy retains its authored achievement HUD (altitude reached, collected stars, elapsed climbing time, and breath), its separate save, and its time-and-stars top-ten list. Terminal results open a named arcade-style top-score window. Only the best ten results per game/rules profile are retained. A saved submission latch prevents the same result from being entered twice. Sudoku ranks by fewest mistakes; Gems and Klondike by points; Hearts by lowest final penalty total; other puzzles and card profiles use their documented move/probe ranking. Remaining guesses and current Sudoku mistakes are gameplay information, not lifetime statistics.

Use Collection to choose a game; Card tables contains the four card games. Music, Sound, and Motion are shared master controls. Eggy also retains its own local music/sound switches; the shared masters gate its separate audio engine. Switching away immediately hides its native layer and silences music, effects, and ambience while its climb continues. Each game has its own Help and Top scores controls. Card Options selects draw count, Spider suits, and card backs. Sudoku's difficulty selector applies to the next New game.

## Architecture and references

- `src/game.*`, `scores.*`, `storage.*`: card rules, profile rankings, and cabinet persistence.
- `src/sudoku.*`, `sudoku_bridge.mm`: native state and the canonical handed-off generator running in JavaScriptCore on a background job.
- `src/puzzles.*`: six active puzzle models and retained legacy support, seeded generation, witnesses, rankings, and persistence.
- `src/table.*`, `sudoku_view.*`, `puzzle_view.*`: native presentation, input, animation, help, and score entry.
- `src/presentation.*`, `collection.*`: custom controls and the shared collection shell.
- `src/puzzle_render.*`, `puzzle_image.mm`: CPU triangle rasterization, z buffer, picking, and nature environment decoding.
- `vendor/eggy/`: cabinet copy of the delivered Eggy control, simulation, renderer, audio, and tests; the original package remains unchanged in `incoming/game-eggy-01/`.
- `src/audio.mm`: native audio decode, sample-counted looping, effects, and completion stingers.

See [the game catalog](docs/GAME_CATALOG.md) for exact rules and provenance, [validation](docs/VALIDATION.md) for evidence and limitations, [audio integration](docs/AUDIO_INTEGRATION.md) for the delivered audio batches, and [source references](docs/SOURCE_REFERENCES.md). Catching Thieves remains a separately requested planned module; it is not one of the twelve playable games. Rendezvous Riders is excluded.

Switchbox is reserved for the user's replacement module; its previous logic and collection entry have been removed. Sticks & Stones is retired. Existing assets and saves are retained. Nature Cube and Puzzle Solve now use separate `-v2.txt` saves, copying prior player names and scores without overwriting their old boards. See [the owner handoff](docs/OWNER_MODULE_HANDOFF.md) and [Eggy integration](docs/EGGY_INTEGRATION.md).
