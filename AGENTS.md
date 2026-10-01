# Games development

Preserve the complete current game collection and its behavior. Do not remove games, substitute simpler implementations, or disable tests to obtain a successful platform build. Read docs/WINDOWS_HANDOFF.md for roster and reservation boundaries, platform seams, and validation requirements.

The user requires most reusable cross-platform capabilities needed by Games to become GUI.Forms enhancements so other applications benefit. Inspect existing toolkit APIs first. Coordinate additions with the GUI.Forms owner; keep game-specific rules, assets, and Sudoku generation policy in Games, using thin integration with shared services. Preserve existing frozen SDKs and unrelated active work. Validate a new SDK before adopting it here.

On Shadow, use C:/Users/Shadow/games as the source checkout. Keep builds in a separate ignored build directory. Existing toolchains may be borrowed read-only; never overwrite another application's frozen SDK. Record the actual supported platform configure/build/test commands in this file as they are established. Keep compile parallelism at two jobs while sharing the host with other application work.

Personal save data is not repository content. Preserve the GAMES_STATE_DIR override and isolate test saves. Original source fingerprint and macOS validation records describe the pre-port baseline; they do not establish Windows success. Complete native application, audio, Sudoku parity, storage, and packaged-launch validation before claiming the Windows port is complete.

Validated Windows development subsets use CMake/Ninja with GCC 16.2 and two compile jobs. These commands do not build or validate the complete application:

```powershell
cmake -S . -B .build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Windows-GUIForms-Application-SDK>
cmake --build .build/windows --target rules_tests storage_tests sudoku_tests puzzle_tests eggy_sim_tests switchbox_rules_tests switchbox_storage_tests switchbox_actor_tests fourpegs_rules_tests fourpegs_storage_tests -j 2
ctest --test-dir .build/windows --output-on-failure -R '^(rules|storage|sudoku|puzzles|eggy_sim|switchbox_rules|switchbox_storage|switchbox_actor|fourpegs_rules|fourpegs_storage)$'
```

The portable audio consumer is independently configurable against an installed GUI.Forms Audio SDK with Vorbis support. Prepare assets using Python/Pillow and an authoring-only FFmpeg executable, then test actual decoding and offline playback:

```powershell
python3 tools/prepare_portable_assets.py --output .build/runtime-vorbis --ffmpeg <ffmpeg-executable> --audio-format vorbis
cmake -S tests/portable_audio -B .build/portable-audio -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<GUIForms-Audio-SDK> -DGAMES_AUDIO_ASSET_DIR=<absolute-runtime-vorbis-directory>
cmake --build .build/portable-audio -j 2
ctest --test-dir .build/portable-audio --output-on-failure
```

Four Pegs now uses the editable `vendor/fourpegs` copy and reserves `four_pegs-v2.txt`; Switchbox uses `vendor/switchbox` and `switchbox-v2.txt`. Keep their incoming packages and v1 saves unchanged. Collection source selects the new views in slots 6 and 7 while retaining their enum values. Full UI linking, shared prepared-text wrapping, bar-synced Four Pegs audio, native cursor interaction and release packaging remain separate validation requirements.
