# Games development

The complete application now builds from pinned GUI.Forms source using `GAMES_TOOLKIT_SOURCE_DIR` and prepared `GAMES_RUNTIME_ASSET_DIR`. Follow `docs/BUILDING.md` and `.github/workflows/applications.yml` for current native application and packaging commands. The full Windows profile passes 22 tests and a packaged native-window smoke; earlier subset commands below remain useful but are not the complete application recipe. Do not modify the shared toolkit checkout to fix application builds.

Preserve the complete current game collection and its behavior. Do not remove games, substitute simpler implementations, or disable tests to obtain a successful platform build. Read docs/WINDOWS_HANDOFF.md for roster and reservation boundaries, platform seams, and validation requirements.

The user requires most reusable cross-platform capabilities needed by Games to become GUI.Forms enhancements so other applications benefit. Inspect existing toolkit APIs first. Coordinate additions with the GUI.Forms owner; keep game-specific rules, assets, and Sudoku generation policy in Games, using thin integration with shared services. Preserve existing frozen SDKs and unrelated active work. Validate a new SDK before adopting it here.

On Shadow, use C:/Users/Shadow/games as the source checkout. Keep builds in a separate ignored build directory. Existing toolchains may be borrowed read-only; never overwrite another application's frozen SDK. Record the actual supported platform configure/build/test commands in this file as they are established. Keep compile parallelism at two jobs while sharing the host with other application work.

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

Atom Probe now uses `vendor/atomprobe` and `atom_probe-v2.txt`. Four Pegs uses the editable `vendor/fourpegs` copy and reserves `four_pegs-v2.txt`; Switchbox uses `vendor/switchbox` and `switchbox-v2.txt`. Keep their incoming packages and v1 saves unchanged. Collection source selects the new views in slots 5, 6 and 7 while retaining their enum values. Full UI linking, shared prepared-text wrapping, native audio playback, native cursor interaction and release packaging remain separate validation requirements.

Four Pegs bar scheduling has a separate development consumer test against GUI.Forms source, because the draft transport is not exported in an installed SDK:

```powershell
cmake -S tests/portable_audio -B .build/portable-audio-transport -G Ninja -DCMAKE_BUILD_TYPE=Release -DGAMES_AUDIO_SOURCE_DIR=<GUIForms-source-root> -DGAMES_AUDIO_ASSET_DIR=<prepared-runtime-assets>
cmake --build .build/portable-audio-transport -j 2
ctest --test-dir .build/portable-audio-transport --output-on-failure --timeout 30
```

This source profile tests real decoded score transitions and the complete 298-file audio corpus; it is not a full application or release SDK. The installed Audio SDK profile continues to test the ordinary adapters independently. Portable text requirements and exact font fixture provenance are in `docs/PORTABLE_TEXT_REQUIREMENTS.md` and `assets/fonts/manifest.json`.

The optional `audio_device_smoke` target in that source profile plays a 14-second score exercise through the real device. Invoke it explicitly with the prepared asset directory; it is deliberately absent from CTest. Its native callback receipts and teardown checks establish device processing, while sound quality still requires listening.

The portable core can be configured without a GUI.Forms SDK. This profile builds every current portable game core and all thirteen rules, storage, simulation, actor and raster tests; it does not build the graphical application:

```sh
cmake -S . -B .build/portable-core -G Ninja -DCMAKE_BUILD_TYPE=Release -DGAMES_BUILD_APPLICATION=OFF
cmake --build .build/portable-core --parallel 2
ctest --test-dir .build/portable-core --output-on-failure --timeout 120
```

The `Native game core checks` workflow runs that profile on native Windows x64, macOS arm64, Linux x64 and Linux arm64 runners. Test reports and scene previews are diagnostic artifacts, not application packages.
