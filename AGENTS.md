# PlaySuite development

PlaySuite is one native C++20 application on GUI.Forms holding a collection of games.
The product is named PlaySuite; internal identifiers keep their old names so builds,
saves and installs carry over: the `games` executable and CMake project, the
`Rainstar/Games` save folders, the Linux package id and the Windows uninstall key.

This file is the working agreement for anyone, person or model, changing the
collection. Read it first; then the document for the work in hand:
[docs/README.md](docs/README.md) indexes the rest.

## Where things live

- `games/<id>/`: one folder per game, discovered from its `GAME.json`. Adding a game
  edits nothing outside its folder. The README's game list is generated from each
  game's `GAME.json`, `about.md` and `screens/readme.jpg` by `tools/make_readme.py`;
  edit those files, run it, and commit the list with them (the `readme.yml` check
  fails a pull request whose list is out of date). The Pocket Library, once a box on
  the shelf, is its own application in `malkuth-software/books`; its permanent id,
  24, stays retired here.
- `shared/<id>/`: engines several games build on, discovered from `ENGINE.json` and
  declared in a game's `GAME.json`: `render` (the 2D canvas, r3d and the surfaces),
  `ambient` (living 3D scenes), `cards` and `puzzles` (the card and puzzle engines),
  `grass`, `soil` and `paper` (materials), `coverage` (the mowing brain), `felt`
  (cloth, linked once by the suite). Each README is its guide;
  [new-games/guide/05-shared-engines.md](new-games/guide/05-shared-engines.md) indexes them.
- `src/`: only the shell and its services. The shelf (`shelf.*`) and the command
  capsule (`capsule.*`) are the shell; games expose commands through `CommandSource`
  (`suite.hpp`). The Music, Sound and Motion masters, their volumes and the card back
  live in one `SettingsStore` (`suite_settings.*`, file `playsuite-settings-v1.txt`),
  mirrored into GUI.Forms `Command`s and `Value`s by `SuiteModel`; every switch binds
  to it. Games declare persistent choices through `CommandSource::settings()`, shown
  on the shared Settings screen (`settings_sheet.*`). The help book and the Settings
  screen are printed on the paper engine's tile (`paper_sheet.*`). Volumes are applied
  to every voice by `PcmPlayer` buses. Live music is one engine, `src/music`: every
  game's score is a style there (`repertoire.hpp` names them, `live_composer.hpp` plays
  one on the device, `music_render <style>` renders one to a file), and a new score
  forks a style rather than starting a generator in its game. Controls are written as
  [new-games/guide/04-controls.md](new-games/guide/04-controls.md) says.
- `new-games/`: the kit. A model starting a game reads `new-games/AGENTS.md` first.
  Kit games are portable from the start, live only in `games/<id>/`, declare their
  engines in `GAME.json`, have no `incoming/` package, and add a `<id>_view_contract`
  test. `new-games/tools/check_game.py <id> --fetch-toolkit` is the gate.
- `authoring/`: offline production of music, sounds and card art
  (`authoring/playsuite_music/README.md` rebuilds the music and the card sounds).
- `incoming/`: delivered game packages, kept unchanged; `games/<id>/` holds the
  editable copy. `evidence/` and `review/` are records.

## Rules that do not bend

- Preserve the complete collection and its behaviour. Never remove a game,
  substitute a simpler implementation, lower image or audio fidelity, drop
  animation or saved state, or disable or weaken a test to get a green build or a
  lower number. See [docs/PERFORMANCE.md](docs/PERFORMANCE.md) for what may be
  traded and what may not.
- Game and entry ids, enum values and save formats are permanent. Personal save data
  is not repository content; `GAMES_STATE_DIR` isolates test saves. Atom Probe, Four
  Pegs and Switchbox keep their `-v1.txt` saves untouched and use `-v2.txt`.
- Do not modify the GUI.Forms checkout to fix an application build. A reusable
  cross-platform capability becomes a GUI.Forms enhancement, coordinated with its
  owner and recorded in [docs/TOOLKIT_REQUESTS.md](docs/TOOLKIT_REQUESTS.md) until it
  lands; game-specific rules, assets and Sudoku generation policy stay here. Validate
  a new toolkit pin before adopting it.
- Threads use GUI.Forms' `Worker` and `AtomicThreadPool` and cancellation its
  `CancellationFlag`, never `std::thread` or `std::stop_token`. Link
  `GUIForms::Audio` or `GUIForms::Application`, never also static `GUIForms::Core`.
- Preserve demand-driven rendering and lazy game creation: a settled scene schedules
  no frames, a hidden game stops its timers, workers and sound, and a game is created
  the first time its box is opened.
- Rock Stack's rock construction runs concurrently: keep its shared mesh and
  stone-texture caches immutable after synchronized initialization; do not serialize
  the factory or remove parallel generation. Its cold-start test covers both mesh
  levels and compares concurrent with serial construction.
- Solitaire, Spider and FreeCell deal only what `games::generate_deal`
  (`shared/cards/solitaire_solver.cpp`, "Dealing") makes as it is wanted: winnable,
  proven by a line replayed through `Game`, and Easy, Medium or Hard by the reference
  player's wins in 20 games (19-20, 14-18, 6 or fewer). `shared/cards/dealer.*` keeps the
  next deals ready in the background. If the rules in `game.cpp` or the reference player
  change, run `ctest -L design` (the `solver` test) and re-measure the level shares
  quoted in the solver's "Dealing" note.
- Write new C++ in the house style
  ([new-games/guide/07-house-style.md](new-games/guide/07-house-style.md)) and run
  `python3 scripts/check-style.py` before publishing. Keep compile parallelism at two
  jobs when sharing a host with other work.
- Check every new layout at the window minimum, 600 × 420, and at scales 1 to 2.
- A new generated material or look is shown to the owner as labelled candidates,
  with sample text or play, before it goes into the shell or a game.

## Tests

CI tests only what is nondeterministic or depends on the platform: windows, drawing,
audio, files, threads and the shared engines. Deterministic game mechanics (rules,
solvers, simulations, generators) are checked at design time: their tests carry the
CTest label `design`, CI runs `ctest -LE design`, and whoever changes a game's rules
runs `ctest -L design` on the M4. Label a new game's mechanics tests `design`.

Build every target before running CTest; a build of `games` alone leaves stale test
executables that pass falsely.

## Building

[docs/BUILDING.md](docs/BUILDING.md) and `.github/workflows/applications.yml` are the
recipe for the complete application and its packages. Release CI pins GUI.Forms
`6fd0071a0f25a8d4d13661dfe61d1c93c5552e00` from `falseywinchnet/gui_forms` and follows
its LLVM 22.1.8 contract on every platform (macOS 14.0 Apple silicon with GUI.Forms'
bundled runtime, Windows 10, Ubuntu 24.04), restoring that revision's published
compiler cache. Ogg Vorbis is decoded by GUI.Forms' stx_vorbis; packages carry its
notices.

Every change reaches `main` through a pull request; nobody pushes to `main`, people
and workflows alike. A pull request merges only when every check on its final head is
green. Each push to `main` publishes a release (`tools/publication_version.py`:
`GAMES_BASE_VERSION` major.minor, patch = baseline patch + run number), so merge only
what is ready to ship on its own.

The portable core configures without GUI.Forms and builds every game core, engine
and their tests, but not the graphical application:

```sh
cmake -S . -B .build/portable-core -G Ninja -DCMAKE_BUILD_TYPE=Release -DGAMES_BUILD_APPLICATION=OFF
cmake --build .build/portable-core --parallel 2
ctest --test-dir .build/portable-core --output-on-failure --timeout 120
```

The `Native game core checks` workflow runs that profile on Windows x64, macOS
arm64, Linux x64 and Linux arm64. The portable text and audio adapters have their
own workflows (`native-text.yml`, `runtime-assets.yml`), which run only when their
inputs change; `tests/portable_text` and `tests/portable_audio` are their consumers.

### The M4 Mini

The authoritative checkout is `/Users/ultimussecundai/Developer/playsuite-games`.
`m4build` syncs the checkout it is run from to its own mirror under
`~/Developer/CodexBuilds/` on the M4 and runs a command there; the mirror is
disposable build input and takes no source edits. Build in the mirror of the checkout
being edited, never in another checkout's. The current native build is
`.build/app-dogfood`, against `~/Developer/PlaysuiteDependencies/toolkit-dogfood`
(the pinned GUI.Forms with its macOS 14 runtime and prebuilt CPU Skia) and the
prepared runtime `.build/runtime-shell`. The development compiler is Homebrew LLVM
22.1.8, as release CI uses. Every configure passes the ccache launchers; a cold
toolkit build without them takes about twenty minutes.

```sh
/Users/ultimussecundai/.local/bin/m4build -- sh -c '
  D=$HOME/Developer/PlaysuiteDependencies/toolkit-dogfood
  export PATH=/opt/homebrew/opt/llvm@22/bin:/opt/homebrew/bin:$PATH
  export CC=clang CXX=clang++ OBJCXX=clang++ CCACHE_BASEDIR=$PWD CCACHE_COMPILERCHECK=content
  export GUI_FORMS_LLVM_RUNTIME=$D/toolchain/llvm-22.1.8-macos14
  cmake -S . -B .build/app-dogfood -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=$D/gui_forms/cmake/llvm22.cmake \
    -DGAMES_TOOLKIT_SOURCE_DIR=$D/gui_forms -DGAMES_RUNTIME_ASSET_DIR=$PWD/.build/runtime-shell \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_OBJCXX_COMPILER_LAUNCHER=ccache \
    -DGUI_FORMS_SKIA_PREBUILT=ON -DGUI_FORMS_SKIA_OUT=$D/skia-out &&
  cmake --build .build/app-dogfood --target all -j2 &&
  ctest --test-dir .build/app-dogfood -LE design --output-on-failure --timeout 300'
```

The native executable is `.build/app-dogfood/games.app/Contents/MacOS/games`. Use
`--list-games`, `--game <id> --standalone --dev`, or `--dev --script <file>` for
isolated native checks; a script is lines of `<milliseconds> <verb>` ending in
`quit`, with the verbs `help`, `shelf`, `open <id>`, `command <id>`, `action <code>`,
`key <name>`, `click X Y`, `hover X Y`, `resize W H` and `capture <file.ppm>`.
`--profile-idle` measures a settled game. `GAMES_EXTRA_GAME_DIRS` lets a complete
external game folder be tested before it is copied into `games/`; clear it after the
experiment, and never commit fixture games or generated catalog files. Test module
addition and removal, sparse permanent ids, help, scrolling and old-save migration
without editing the shell, and run the full native suite after finalizing a catalog
change.

Report what was observed. Passing rules tests is not native, audio or
cross-platform validation; audio is judged by listening, and the person is sent the
files.
