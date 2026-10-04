# PlaySuite

PlaySuite (formerly Rainstar Games) is one native C++20 / GUI.Forms application with eighteen games: Solitaire, Spider, FreeCell, Hearts, Sudoku, Gems, Nature Cube, Untangle, Atom Probe, Four Pegs, Switchbox, Puzzle Solve, Eggy and the Very, Very Tall Mountain, Koi-Koi, The Parrot's Table, Liar's Dice, Pen the Sheep, and Rock Stack.

The main menu is a shelf of boxed games in a walnut-panelled shop, in the manner of a 1990s games collection. Every game, including each card game, has its own box; there are no categories. Click a box to open it with a short box-to-window transition; arrow keys select a box and Enter opens it. Inside a game, a floating command capsule at the top holds the way back to the shelf, the game's name and New game; hovering opens it to the game's other commands (Undo, Hint, Options, Help, Top scores and so on) and the master Music, Sound and Motion switches. The `···` button keeps it open. Games that present their own live surfaces (Atom Probe, Four Pegs, Switchbox, Eggy and the four new games) sit below a slim rail that carries the capsule. The window resizes down to 600 × 420 and every view lays itself out for small screens.

Card tables use Plan Paint's fiber-rendered billiard felt, original cream card faces and four coordinated backs under a lit, air-cushion card finish, soft paper-on-felt handling sounds, and a finished patience game ends with the classic bouncing-card cascade. Solitaire, Spider and FreeCell deal only verified winnable games at Easy, Medium or Hard. Hearts opponents are named for US presidents, track the cards played and guess at hidden hands; each hand one is sharp and one is forgetful. The Nature Cube is a block of dark glass with three playable faces of mirrored tiles, wrap-safe reflections of the lake panorama and paths that fold across its edges. Gems are faceted stones that pour in, breathe, follow the pointer when dragged and shatter, with bomb shockwaves, star beams, hypercube lightning, score pop-ups and cascade call-outs. Nature Cube and Untangle have Easy, Medium and Hard levels; Untangle is yarn on a tufted cushion with colored thread layers, frozen pegs and a cat who helps, hinders and finally naps in the finished web.

The [four new integrations](docs/NEW_GAME_INTEGRATION.md) use the same shelf, command capsule and master preferences. Koi-Koi includes 48 new traditional woodblock-style faces, with month and card-type labels.

## Make a game for the shelf

Anyone with an idea for a small game can build one with an AI model and offer it for the shelf. [new-games/](new-games/README.md) explains what kind of game belongs here and holds the whole kit: a template game, a headless harness that renders a game's frames without the application, the tools that put a box on the shelf, and the procedure a model follows ([new-games/AGENTS.md](new-games/AGENTS.md)).

## Build status and downloads

The [Build installable PlaySuite workflow](https://github.com/falseywinchnet/games/actions/workflows/applications.yml) builds the complete collection on native Windows x64, macOS arm64, Linux x64 and Linux arm64 runners. Successful runs provide installers and portable archives. Version tags publish tested packages to [Releases](https://github.com/falseywinchnet/games/releases).

The release workflow requires all 31 application tests on each of those four native platforms, then installs and launches the Windows, macOS and Linux packages with isolated saves before publishing. Tests include the command bar at the minimum 600 × 420 window size for every game. Download installers and portable archives from [the latest release](https://github.com/falseywinchnet/games/releases/latest). macOS packages target Apple silicon and macOS 15 or later and are signed ad hoc, without Developer ID notarization. Linux packages target Ubuntu 24.04 or compatible systems and require X11.

Games links reviewed GUI.Forms source for native windows, portable text and compact audio; no development SDK is installed or exported. CMake fetches pinned QuickJS for offline Sudoku. No external JavaScript runtime or server is needed. FFmpeg and Pillow prepare release assets and are not runtime requirements. See [complete build instructions](docs/BUILDING.md).

The smaller portable-core profile remains useful for rules and numerical work:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGAMES_BUILD_APPLICATION=OFF
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure --timeout 120
```

## Current games and scores

Every game saves committed changes, keeps its own position when switching games, resumes on reopening, and offers its own restart, next-round or new-game controls. Saves live in `%APPDATA%/Rainstar/Games` on Windows, `~/Library/Application Support/Rainstar/Games/` on macOS, and `$XDG_STATE_HOME/rainstar/games` (default `~/.local/state/rainstar/games`) on Linux. These folders keep their earlier names so existing saves carry over to PlaySuite; the first PlaySuite start keeps the previously chosen game selected on the shelf. `GAMES_STATE_DIR` overrides this for isolated tests. Files use bounded, checksummed envelopes and atomic replacement. The existing version-one card cabinet migrates on load. Undo history is session-local; current boards, Sudoku notes and mistakes, puzzle progress, and named scores persist.

The original card and logic games use terminal top-score tables. Koi-Koi keeps a twelve-round match scoreboard, The Parrot's Table tracks solved tables and streaks, Liar's Dice keeps the Keeper's ledger and crew records, and Pen the Sheep records meadow stars. Eggy retains its authored achievement HUD (altitude reached, collected stars, elapsed climbing time, and breath), its separate save, and its time-and-stars top-ten list. Terminal results open a named arcade-style top-score window. Only the best ten results per game/rules profile are retained. A saved submission latch prevents the same result from being entered twice. Sudoku ranks by fewest mistakes; Gems and Klondike by points; Hearts by lowest final penalty total; other puzzles and card profiles use their documented move/probe ranking. Remaining guesses and current Sudoku mistakes are gameplay information, not lifetime statistics.

Choose a game from the shelf; each card game has its own box and resumes its own deal. Music, Sound, and Motion are shared master controls on the shelf sign and in every game's capsule. Eggy also retains its own local music/sound switches; the shared masters gate its separate audio engine. Switching away immediately hides its drawing surface and silences music, effects, and ambience while its climb continues. Each game has its own Help and Top scores controls. Card Options selects draw count, Spider suits, and card backs. Sudoku's, the patience games', Nature Cube's and Untangle's "Next:" level control applies to the next New game (an untouched deal is replaced at once).

## Architecture and references

- `src/game.*`, `scores.*`, `storage.*`: card rules, profile rankings, and cabinet persistence.
- `src/sudoku.*`, `sudoku_bridge.cpp`: native state and the canonical handed-off generator running in the pinned QuickJS engine on a background job.
- `src/puzzles.*`: six active puzzle models and retained legacy support, seeded generation, witnesses, rankings, and persistence.
- `src/table.*`, `sudoku_view.*`, `puzzle_view.*`: native presentation, input, animation, help, and score entry.
- `src/collection.*`, `shelf.*`, `capsule.*`, `suite.*`: the PlaySuite shell (shelf, boxes, launch transition, command capsule, house gloss controls and box art).
- `src/text_sprites.*`: display lettering in the game fonts for ordinary painted controls.
- `src/presentation.*`: shared game buttons, dialogs and polygon filling.
- `src/puzzle_render.*`, `puzzle_image.cpp`: CPU triangle rasterization, z buffer, picking, and prepared environment pixels.
- `vendor/eggy/`: cabinet copy of the delivered Eggy control, simulation, renderer, audio, and tests; the original package remains unchanged in `incoming/game-eggy-01/`.
- `src/audio.cpp`, `scene_audio.*`, `fourpegs_audio.*`: portable cabinet audio policy, effects, and score transitions through GUI.Forms Audio.
- `authoring/playsuite_music/`: the C++ synthesizer and scores for the PlaySuite menu theme and the card, Sudoku, Gems, Nature Cube, Untangle and Puzzle Solve music.

See [the game catalog](docs/GAME_CATALOG.md) for exact rules and provenance, [validation](docs/VALIDATION.md) for evidence and limitations, [audio integration](docs/AUDIO_INTEGRATION.md) for the delivered audio batches, and [source references](docs/SOURCE_REFERENCES.md). Catching Thieves remains a separately requested planned module; it is not one of the eighteen playable games. Rendezvous Riders is excluded.

Atom Probe, Four Pegs and Switchbox now select the owner's replacement controls in collection source. All three use shared portable text, audio and presentation services and retain separate v2 saves. Sticks & Stones is retired. Existing assets and saves are retained. Nature Cube and Puzzle Solve now use separate `-v2.txt` saves, copying prior player names and scores without overwriting their old boards. See [the owner handoff](docs/OWNER_MODULE_HANDOFF.md) and [Eggy integration](docs/EGGY_INTEGRATION.md).
