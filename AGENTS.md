# Games development

Preserve the complete current game collection and its behavior. Do not remove games, substitute simpler implementations, or disable tests to obtain a successful platform build. Read docs/WINDOWS_HANDOFF.md for roster and reservation boundaries, platform seams, and validation requirements.

The user requires most reusable cross-platform capabilities needed by Games to become GUI.Forms enhancements so other applications benefit. Inspect existing toolkit APIs first. Coordinate additions with the GUI.Forms owner; keep game-specific rules, assets, and Sudoku generation policy in Games, using thin integration with shared services. Preserve existing frozen SDKs and unrelated active work. Validate a new SDK before adopting it here.

On Shadow, use C:/Users/Shadow/games as the source checkout. Keep builds in a separate ignored build directory. Existing toolchains may be borrowed read-only; never overwrite another application's frozen SDK. Record the actual supported platform configure/build/test commands in this file as they are established. Keep compile parallelism at two jobs while sharing the host with other application work.

Personal save data is not repository content. Preserve the GAMES_STATE_DIR override and isolate test saves. Original source fingerprint and macOS validation records describe the pre-port baseline; they do not establish Windows success. Complete native application, audio, Sudoku parity, storage, and packaged-launch validation before claiming the Windows port is complete.
