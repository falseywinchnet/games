# Owner game replacements

The delivered replacement sources are integrated through editable vendor copies. Their complete Windows application link, native interaction and release validation remain in progress. Do not treat the source integration or headless tests as a released application.

| Incoming package | Working copy | Collection slot | Preserved enum | New save |
| --- | --- | --- | --- | --- |
| `game-atomprobe-01` | `vendor/atomprobe` | 5 | `PuzzleKind::atom` (3) | `atom_probe-v2.txt` |
| `game-fourpegs-01` | `vendor/fourpegs` | 6 | `PuzzleKind::pegs` (4) | `four_pegs-v2.txt` |
| `game-switchbox-01` | `vendor/switchbox` | 7 | Switchbox kind 5 | `switchbox-v2.txt` |

Keep each incoming package unchanged. Its `INTEGRATION_HANDOFF.txt`, validation record and fingerprints describe the delivered baseline. Keep every corresponding `-v1.txt` save untouched. Development saves use separate `-dev-v2.txt` names, and `GAMES_STATE_DIR` isolates tests.

Collection source selects `ap::AtomProbeView`, `fp::FourPegsView` and `sbx::SwitchboxView` in these slots. The predecessor Atom Probe and Four Pegs models remain available to legacy rule tests, but their old controls are not constructed in the collection. Sticks & Stones remains retired.

The Windows development checks cover the replacement game rules, v2 storage and rendering previews. Atom Probe retains its complete eight-by-eight, four-atom deduction rules and reveal. Four Pegs retains its villain, narrative and code-breaking rules; its portable audio consumer verifies the actual score changes on musical bar boundaries through the shared GUI.Forms transport. Switchbox uses the shared window-owned cursor interaction API. These controls use LiveSurface presentation instead of separate platform presenters.

Shared prepared-text wrapping and monochrome masks remain required for the complete application. See [portable text requirements](PORTABLE_TEXT_REQUIREMENTS.md). Full UI linking, native sound listening, input and DPI checks, and independent platform packaging must pass before publishing release binaries. Build and subset-test commands are in the repository's `AGENTS.md`.

## New games awaiting integration

Four new games from the owner's 2026-10-02 brief (Parrot's Table, Liar's Dice, Block the Pig, Koi-Koi, Spice Wars) are delivered as pristine packages. They replace nothing: each needs a new collection entry, except Koi-Koi, which joins the card shelf. Each package's `INTEGRATION_HANDOFF.txt` gives the control to host, the Windows seams, the save file and what was and wasn't verified.

| Incoming package | Game | Namespace | Hosted control | Save |
| --- | --- | --- | --- | --- |
| `game-parrotstable-01` | The Parrot's Table (logic puzzle) | `pt` | `pt::TableView` | `parrots_table-v1.txt` |
| `game-liarsdice-01` | Liar's Dice | `ld` | `ld::DiceView` | `liars_dice-v1.txt` |
| `game-penthesheep-01` | Pen the Sheep (the hex trapping puzzle) | `sh` | `sh::SheepView` | `pen_the_sheep-v1.txt` |
| `game-koikoi-01` | Koi-Koi, the fifth card game | `games::koi` rules, `kk` table | `kk::KoiView` | `koikoi-v1.txt` |

Keep each package unchanged and work from vendor copies, as above. All four reflow from 600 x 370 points (the window minimum less the rail) to full screen. Koi-Koi's package carries verbatim copies of `vendor/paint` and of the card table's `card_finish`; link the cabinet's own `vendor/paint` instead of the copy. These packages' `platform/raster.cpp` includes a fix to `accumulate()` (an intermittent one-float overread); the Switchbox, Four Pegs and Atom Probe vendor copies predate it.

### Zen Construction (delivered 2026-10-03)

`incoming/game-zenconstruction-01` is a new game (a new collection entry; it replaces nothing): stacking river stones with a toy truck crane, with its own deterministic rigid-body engine (`phys/`, namespace `zc::phys`). Namespace `zc`; hosted control `zc::ZenView`; save `zen_construction-v1.txt`. It reflows from 600 x 370 points. Its audio adds one seam the other games don't have, a looping bed with eased gain and playback rate (`audio_bed`); see its `INTEGRATION_HANDOFF.txt`. Its renderer copy fills in bands on a small `std::thread` pool and keeps static shadows (pixel-identical output).
