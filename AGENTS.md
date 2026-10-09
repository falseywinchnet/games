# Games (PlaySuite) development

The product is now named PlaySuite. Internal identifiers (the `games` executable and CMake project, the `Rainstar/Games` save folders, the Linux package id and the Windows uninstall key) keep their names so builds, saves and installs carry over. The shell is the shelf (`src/shelf.*`) plus the command capsule (`src/capsule.*`); games expose their commands through `CommandSource` (`src/suite.hpp`) or, for vendor views, `host_command`/`host_panel`. The Music, Sound and Motion masters, their volumes and the card back live in one `SettingsStore` (`src/suite_settings.*`, file `playsuite-settings-v1.txt`, migrated from and mirrored into the card cabinet); every switch observes it. Games declare persistent choices through `CommandSource::settings()`, shown on the shared Settings screen (`src/settings_sheet.*`); volumes are applied to every voice by `PcmPlayer` buses. Toolkit gaps found during the rework are listed in `docs/TOOLKIT_REQUESTS.md` for the GUI.Forms owner. The window minimum is 600 × 420; check new layouts at that size. Solitaire, Spider and FreeCell deal only from the verified, graded tables in `shared/cards/deal_tables.cpp`: if their rules in `game.cpp`, `shared/cards/solitaire_solver.cpp` or its grading limits change, regenerate the tables (`cmake --build .build/portable-core --target deal_grader -j 1`, then `.build/portable-core/deal_grader generate shared/cards/deal_tables.cpp 500 12`, about 21 minutes single-threaded) or the `solver` test fails. Card handling sounds are rendered by `playsuite_music sfx` (see `authoring/playsuite_music/README.md`).

The complete application now builds from pinned GUI.Forms source using `GAMES_TOOLKIT_SOURCE_DIR` and prepared `GAMES_RUNTIME_ASSET_DIR`. Follow `docs/BUILDING.md` and `.github/workflows/applications.yml` for current native application and packaging commands. The full Windows profile passes 39 tests and a packaged native-window smoke; earlier subset commands below remain useful but are not the complete application recipe. Do not modify the shared toolkit checkout to fix application builds.

Preserve the complete current game collection and its behavior. Do not remove games, substitute simpler implementations, or disable tests to obtain a successful platform build. Read docs/WINDOWS_HANDOFF.md for roster and reservation boundaries, platform seams, and validation requirements.

CI tests only what is nondeterministic or depends on the platform (windows, drawing, audio, files, threads, the shared engines). Deterministic game mechanics (rules, solvers, simulations, generators) are checked at design time: their tests carry the CTest label `design`, CI runs `ctest -LE design`, and whoever changes a game's rules runs `ctest -L design` on the M4. Label a new game's mechanics tests `design` when adding them.

The user requires most reusable cross-platform capabilities needed by Games to become GUI.Forms enhancements so other applications benefit. Inspect existing toolkit APIs first. Coordinate additions with the GUI.Forms owner; keep game-specific rules, assets, and Sudoku generation policy in Games, using thin integration with shared services. Preserve existing frozen SDKs and unrelated active work. Validate a new SDK before adopting it here.

On Shadow, use C:/Users/Shadow/games as the source checkout. Keep builds in a separate ignored build directory. Existing toolchains may be borrowed read-only; never overwrite another application's frozen SDK. Record the actual supported platform configure/build/test commands in this file as they are established. Keep compile parallelism at two jobs while sharing the host with other application work. GitHub-hosted CI runners are dedicated, so the workflows compile four at a time.

The current Shadow development build is `.build/application-dev`, configured against the isolated `.build/toolkit-input-fairness/gui_forms` source at `d58f530ad5ceaa8f4ab1e5afdc12027aff90d16d` and the prepared runtime `.build/runtime-040`. That runtime includes the PlaySuite v2 music; `authoring/playsuite_music/README.md` describes how to rebuild it. Put `C:/Users/Shadow/plan-paint/build-deps/msys64/mingw64/bin` on PATH, then:

```sh
cmake -S . -B .build/application-dev -G Ninja -DCMAKE_BUILD_TYPE=Release -DGAMES_TOOLKIT_SOURCE_DIR=C:/Users/Shadow/games/.build/toolkit-input-fairness/gui_forms -DGAMES_RUNTIME_ASSET_DIR=C:/Users/Shadow/games/.build/runtime-040
cmake --build .build/application-dev -j 2
ctest --test-dir .build/application-dev --output-on-failure --timeout 180
```

Personal save data is not repository content. Preserve the GAMES_STATE_DIR override and isolate test saves. Original source fingerprint and macOS validation records describe the pre-port baseline; they do not establish Windows success. Complete native application, audio, Sudoku parity, storage, and packaged-launch validation before claiming the Windows port is complete.

Validated Windows development subsets use CMake/Ninja with GCC 16.2 and two compile jobs. These commands do not build or validate the complete application:

```powershell
cmake -S . -B .build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Windows-GUIForms-Application-SDK>
cmake --build .build/windows --target rules_tests storage_tests sudoku_tests puzzle_tests eggy_sim_tests switchbox_rules_tests switchbox_storage_tests switchbox_actor_tests fourpegs_rules_tests fourpegs_storage_tests atomprobe_rules_tests atomprobe_storage_tests -j 2
ctest --test-dir .build/windows --output-on-failure -R '^(rules|storage|sudoku|puzzles|eggy_sim|switchbox_rules|switchbox_storage|switchbox_actor|fourpegs_rules|fourpegs_storage|atomprobe_rules|atomprobe_storage)$'
```

The portable audio consumer is independently configurable against an installed GUI.Forms Audio SDK with Vorbis support. Prepare assets using Python/Pillow and an authoring-only FFmpeg executable, then test actual decoding and offline playback:

```powershell
python3 tools/prepare_portable_assets.py --output .build/runtime-vorbis --ffmpeg <ffmpeg-executable> --audio-format vorbis
cmake -S tests/portable_audio -B .build/portable-audio -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<GUIForms-Audio-SDK> -DGAMES_AUDIO_ASSET_DIR=<absolute-runtime-vorbis-directory>
cmake --build .build/portable-audio -j 2
ctest --test-dir .build/portable-audio --output-on-failure
```

Atom Probe now uses `games/atomprobe` and `atom_probe-v2.txt`. Four Pegs uses the editable `games/fourpegs` copy and reserves `four_pegs-v2.txt`; Switchbox uses `games/switchbox` and `switchbox-v2.txt`. Keep their incoming packages and v1 saves unchanged. Collection source selects the new views in slots 5, 6 and 7 while retaining their enum values. Full UI linking, shared prepared-text wrapping, native audio playback, native cursor interaction and release packaging remain separate validation requirements.

Four Pegs bar scheduling has a separate development consumer test against GUI.Forms source, because the draft transport is not exported in an installed SDK:

```powershell
cmake -S tests/portable_audio -B .build/portable-audio-transport -G Ninja -DCMAKE_BUILD_TYPE=Release -DGAMES_AUDIO_SOURCE_DIR=<GUIForms-source-root> -DGAMES_AUDIO_ASSET_DIR=<prepared-runtime-assets>
cmake --build .build/portable-audio-transport -j 2
ctest --test-dir .build/portable-audio-transport --output-on-failure --timeout 30
```

This source profile tests real decoded score transitions and the complete 392-file audio corpus; it is not a full application or release SDK. The installed Audio SDK profile continues to test the ordinary adapters independently. Portable text requirements and exact font fixture provenance are in `docs/PORTABLE_TEXT_REQUIREMENTS.md` and `assets/fonts/manifest.json`.

The optional `audio_device_smoke` target in that source profile plays a 14-second score exercise through the real device. Invoke it explicitly with the prepared asset directory; it is deliberately absent from CTest. Its native callback receipts and teardown checks establish device processing, while sound quality still requires listening.

The portable core can be configured without a GUI.Forms SDK. This profile builds every current portable game core and all eighteen rules, solver, storage, simulation, actor and raster tests; it does not build the graphical application:

```sh
cmake -S . -B .build/portable-core -G Ninja -DCMAKE_BUILD_TYPE=Release -DGAMES_BUILD_APPLICATION=OFF
cmake --build .build/portable-core --parallel 2
ctest --test-dir .build/portable-core --output-on-failure --timeout 120
```

The `Native game core checks` workflow runs that profile on native Windows x64, macOS arm64, Linux x64 and Linux arm64 runners. Test reports and scene previews are diagnostic artifacts, not application packages.

Every game lives in its own folder, `games/<id>/`, discovered from its `GAME.json`. Code several games use lives in `shared/` (`cards`, `puzzles`, `ambient`, `coverage`, `felt`); `src/` is only the PlaySuite shell and its services. The README's game list is generated from each game's `GAME.json`, `about.md` and `screens/readme.jpg` by `tools/make_readme.py`, which the `readme.yml` workflow runs after every push to `main`; edit those files, not the generated list.

New games are built with the kit in `new-games/`; a model starting one reads `new-games/AGENTS.md` first. Shared engines live in `shared/<id>/` (see `new-games/guide/11-shared-engines.md`); games declare them in `GAME.json`. `shared/ambient` draws living 3D scenes on the processor alone under a measured CPU budget; Stillwater is its first scene. Kit games are written portable from the start, live only in `games/<id>/`, and are discovered automatically from `GAME.json`; `new-games/tools/wire_shelf.py` validates without changing files; they have no `incoming/` package. `new-games/tools/check_game.py <id> --fetch-toolkit` is their gate, and each adds a `<id>_view_contract` application test.

The 0.4 integrations and adapter boundaries are documented in docs/NEW_GAME_INTEGRATION.md. Run `python3 scripts/check-style.py` before publishing. New incoming packages remain unchanged; vendor copies are the integration surface.

Preserve demand-driven rendering and lazy game creation. See docs/PERFORMANCE.md
for the idle profiling protocol and remaining toolkit boundaries. Do not trade
away animation, image fidelity, game behavior or saved state to lower a metric.

## Rock Stack startup caches

Rock construction runs concurrently. Keep shared mesh and stone-texture caches
immutable after synchronized initialization; do not serialize the whole rock
factory or remove parallel generation. The cold-start test covers both mesh
levels and compares concurrent results with serial construction.

From the authoritative local checkout, validate on the M4 Mini with:

```sh
/Users/ultimussecundai/.local/bin/m4build -- sh -c \
  '/opt/homebrew/bin/cmake -S . -B /tmp/games-rockstack -DCMAKE_BUILD_TYPE=Release -DGAMES_BUILD_APPLICATION=OFF && \
   /opt/homebrew/bin/cmake --build /tmp/games-rockstack --target rockstack_cache_tests rockstack_rules_tests rockstack_physics_tests -j2 && \
   /opt/homebrew/bin/ctest --test-dir /tmp/games-rockstack -R "^rockstack_" --output-on-failure --timeout 180'
```

## Modular application checks on the M4 Mini

The authoritative checkout is `/Users/ultimussecundai/Developer/playsuite-games`.
Use `m4build` from this tree; it chooses the route with `m4host`. The remote mirror
is disposable build input. Do not make source edits there. The current native
build uses the reviewed toolkit at
`/Users/joshuahkuttenkuler/Developer/PlaysuiteDependencies/toolkit-d58f530/gui_forms`,
runtime assets at `.../PlaysuiteDependencies/runtime-modular`, and the verified
Skia CPU output at
`/Users/joshuahkuttenkuler/Developer/Projects/gui-forms-investigation/skia-cpu`.
Release CI pins GUI.Forms `5bb7a8ba74ccb358f11d1b9056831e0130bdd8d0` from its own repository, `falseywinchnet/gui_forms`, and follows its LLVM 22.1.8 contract on every platform (macOS 14.0 Apple silicon with GUI.Forms' bundled LLVM runtime, Windows 10, Ubuntu 24.04), restoring that revision's published compiler cache and, on Windows, its published MSYS2 CLANG64 toolchain (`tools/restore_windows_toolchain.py`; setup-msys2 only as a fallback); see docs/BUILDING.md. Ogg Vorbis is decoded by GUI.Forms' stx_vorbis (BFFT synthesis); packages carry its stx, BFFT and threadpool notices. On the M4, `~/Developer/PlaysuiteDependencies/toolkit-dogfood` holds this pin's sources (they equal 0d6c397's; 5bb7a8b changed only GUI.Forms' Windows toolchain packaging), and `toolkit-4e126d6` the pin before 56e194f, its macOS 14 runtime (`toolchain/llvm-22.1.8-macos14`) and Skia (`skia-out`). Threads use GUI.Forms' `Worker`/`AtomicThreadPool` and cancellation its `CancellationFlag`, not `std::thread`/`std::stop_token`; link `GUIForms::Audio` or `GUIForms::Application`, never also static `GUIForms::Core`. The `toolkit-f57d0fa`, `toolkit-e589142`, `toolkit-f81ec0c`, `toolkit-opaque` and `toolkit-d58f530` paths above are earlier pins.
The development compiler is Homebrew LLVM 22.1.8, as release CI now uses.
This developer build does not establish release packaging with a different LLVM.

```sh
/Users/ultimussecundai/.local/bin/m4build -- sh -c '
  export PATH=/opt/homebrew/bin:$PATH
  cmake --build .build/modular-app -j2 &&
  ctest --test-dir .build/modular-app --output-on-failure --timeout 180'
```

For a fresh configure on that host, use the absolute directories above and:

```sh
export CC=/opt/homebrew/opt/llvm/bin/clang
export CXX=/opt/homebrew/opt/llvm/bin/clang++ OBJCXX=/opt/homebrew/opt/llvm/bin/clang++
export CXXFLAGS="-nostdinc++ -isystem /opt/homebrew/opt/llvm/include/c++/v1"
export OBJCXXFLAGS="$CXXFLAGS"
export LDFLAGS="-L/opt/homebrew/opt/llvm/lib/c++ -Wl,-rpath,/opt/homebrew/opt/llvm/lib/c++ -L/opt/homebrew/opt/llvm/lib/unwind -Wl,-rpath,/opt/homebrew/opt/llvm/lib/unwind -lunwind"
cmake -S . -B .build/modular-app -DCMAKE_BUILD_TYPE=Release \
  -DGAMES_TOOLKIT_SOURCE_DIR=/Users/joshuahkuttenkuler/Developer/PlaysuiteDependencies/toolkit-d58f530/gui_forms \
  -DGAMES_RUNTIME_ASSET_DIR=/Users/joshuahkuttenkuler/Developer/PlaysuiteDependencies/runtime-modular \
  -DGUI_FORMS_SKIA_PREBUILT=ON \
  -DGUI_FORMS_SKIA_OUT=/Users/joshuahkuttenkuler/Developer/Projects/gui-forms-investigation/skia-cpu
```

The native executable is `.build/modular-app/games.app/Contents/MacOS/games`.
Use `--list-games`, `--game catchingthieves --standalone --dev`, or
`--game maze --dev --script <finite-script>` for isolated native validation.
`GAMES_EXTRA_GAME_DIRS` allows a complete external game folder to be tested before
copying it into `games/`. Clear that cache option after the experiment. Never
commit fixture games or generated catalog files. Test module addition/removal,
sparse permanent IDs, help, scrolling and old save migration without editing the
shell. Run the full native suite after finalizing the catalog.
