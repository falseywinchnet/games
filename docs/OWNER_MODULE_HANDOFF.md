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
