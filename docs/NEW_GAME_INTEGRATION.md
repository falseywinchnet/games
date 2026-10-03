# Four additional games in PlaySuite 0.4

Koi-Koi, The Parrot's Table, Liar's Dice and Pen the Sheep join the existing thirteen games. Their persisted `Entry` values are appended; every earlier entry and `PuzzleKind` keeps its value. The delivered `incoming/game-*-01` packages remain unchanged. Editable copies live under `vendor/koikoi`, `vendor/parrots`, `vendor/liarsdice` and `vendor/penthesheep`.

The shared shelf and command capsule host all four games. Help, sets, crew, records and meadow selection use each game's own panels through `CommandSource`. Card choices, bids, parrot marks and fence placement stay on the boards. The capsule owns music, sound and reduced motion in hosted mode. Hidden games stop audio and pause their simulation; returning resumes the existing position. A wrapping capsule expands the reserved rail so its buttons cannot overlap a live game surface.

## Platform adapters

All four views publish BGRA frames through GUI.Forms `LiveSurface`, derive backing scale from their attached window, and convert window pointer positions to local coordinates. Retained repaints draw the latest surface as well. The delivered Objective-C++ text, audio and presenter implementations are not compiled by PlaySuite. Their standalone authoring projects remain in the vendor copies as reference.

Text uses the existing GUI.Forms text-mask service and bundled fonts. `PortableMaskCache` bridges the delivered synchronous layout interface: a first miss waits for the worker with a two-second deadline, then keeps a stable cached mask. This compatibility cost is limited to misses; it is not a claim of fully asynchronous layout.

Audio uses the existing shared Vorbis decoder and scene players. Liar's Dice uses the same real bar transport as Four Pegs: both arrangements have 1,570,896 frames at 48 kHz, with 65,454-frame bars. A sample-by-sample test checks the actual transition and outgoing fade against independently decoded reference clips. All 392 source audio files are prepared, decoded and verified.

Koi-Koi PNGs are converted at build-resource preparation time to bounded premultiplied BGRA files. The runtime reader validates the header, dimensions and exact payload length. The shared felt renderer is linked once. The 48 new woodblock faces retain deterministic month and type labels; see [artwork provenance](../authoring/hanafuda/README.md).

No changes to GUI.Forms or new Apple framework adapter code were required. The only no-op seam is the standalone deliveries' developer-only window resize command; normal application resizing remains native and functional.

## Saves and checks

The new files are `koikoi-v1.txt`, `parrots_table-v1.txt`, `liars_dice-v1.txt` and `pen_the_sheep-v1.txt` under the existing platform state directory. `GAMES_STATE_DIR` isolates tests. Existing Atom Probe, Four Pegs and Switchbox v1 and v2 files are not migrated or overwritten by this integration.

The complete application has 31 CTest cases. New coverage includes the four rule suites, capsule panel toggling, complete game/help surfaces, immutable retained frames, 150% display scale and compact surfaces below the expanded capsule. Pen the Sheep's frame test waits for a playable generated meadow rather than accepting its loading screen. The existing minimum-window test visits every shelf entry at 600 × 420. Each release platform runs these checks independently, then installs and launches its package.
