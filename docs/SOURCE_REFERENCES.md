# Source and design references

## Current user instructions

Build one native Games app containing Windows 7-style Solitaire, Spider Solitaire, FreeCell, and Hearts. Share the card system and a small fast 2D game foundation; avoid a speculative general-purpose engine. Prioritize smooth play, visible selections, several card backs, original soft sounds and per-game music, consistently constructed HD card faces, and in-game help. Use Plan Paint's GUI.Forms implementation and felt logic as references. macOS verification is sufficient for this stage.

The attached `programming-house-style (2).md` is a coding reference: explicit types, named callbacks, visible ownership, ordinary control flow, and reusable storage. It is not a separate product request and does not replace the user's task.

## Inspected and reused source

- Plan Paint source: `/Users/joshuahkuttenkuler/Developer/Projects/rainstar-paint`.
- Actual felt mapping: `src/canvas_backing.cpp`; billiard green maps to carpet preset 3.
- Fiber and lighting renderer: `src/carpet.cpp`, `src/carpet.hpp`, `src/image.cpp`, `src/image.hpp`. These four files were copied unchanged into `vendor/paint` with the original MIT license. No sibling checkout was edited.
- GUI.Forms texture publication/pattern fill reference: Paint's `src/forms/surface.cpp`.
- In-app help reference: Paint's `src/forms/help.cpp` and `src/forms/help.hpp`. Games implements its own warm paper popup with game-specific rules, controls, and about pages; it does not depend on the Paint application.
- Installed GUI.Forms SDK: `/Users/joshuahkuttenkuler/Developer/Projects/gui-forms-investigation/paint-port-sdk`.
- Historical game interviews: `/Users/joshuahkuttenkuler/Developer/Projects/gui-forms-investigation/source/file_manager/games/planning`.

The history's proposed architecture and gating notes are source evidence, not new approval requirements. The current user explicitly requested implementation. The expanded roster and distinction between accepted intentions and open rules live in `GAME_CATALOG.md`.

## Art provenance

`tools/make_cards.py` draws all 52 faces using one geometry/layout specification, four original patterned backs, and the app icon. Faces are 360 × 504 pixels, over three times the current maximum logical card width; pips and court art use deterministic geometry. No image generator was used for card faces. Georgia is used by the development generator on macOS to render rank glyphs into the original images; no Georgia font file is redistributed. Runtime GUI fonts are supplied by GUI.Forms with their license/attribution files.

Microsoft artwork, original Windows executables, Kyodai assets, Crazy8 samples, and extracted Catching Thieves audio are not shipping assets. Those works establish reference behavior or artistic direction only.

## Audio coordination

The user started the Neo coordination chat `01a0f5bc-87ed-7913-9a8f-da805e7a641d`. Its reported Claude Opus session is “Solitaire music and sound effects,” working under `/Users/ultimussecundai/solitaire_sounds`. The coordinator owns interaction with Claude and delivery of original audio. Both audio batches have now been delivered and integrated; see `AUDIO_INTEGRATION.md` for package hashes, native validation, and provenance limits.

## October 1 refinement references

- The actual Smart Games Puzzle Challenge 2 video, [DeathSandals](https://www.youtube.com/watch?v=AbENhChkW1E), was inspected at the PicPax segment around 15:34. The relevant geometry includes diagonal triangles, squares, and parallelograms with blue/yellow regions. Puzzle Solve now uses those geometric families, with original layouts and artwork; it is not an exact reconstruction of PicPax's piece set.
- [The Mastermind Game by TheSwain](https://armorgames.com/play/2410/the-mastermind-game) was played in its official Armor Games page. Its illustrated opponent, foreground peg console, horizontal palette, and compact side history informed Four Pegs. `assets/four-pegs-curator.png` is newly generated original artwork, not a copied character or scene.
- Current File Manager house composition was checked against `STYLE_FAMILY_ATLAS_004.md` and `DESIGN_DNA_VERDICTS_007.md` under `gui-forms-investigation/source/file_manager/frontend/planning/visual/`, including the rendered frontend reference. The collection applies the pearl command field, watercolor identity, medium graphite dividers, white object field, and warm details pane to the whole window.
- Eggy's original author handoff and 217-file checksum manifest are preserved under `incoming/game-eggy-01/`. See `EGGY_INTEGRATION.md` for the precise cabinet changes.
