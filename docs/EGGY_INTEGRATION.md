# Eggy cabinet integration — 2026-10-01

The delivered `game-eggy-01` package is integrated into the Games collection as Eggy, in collection slot 9. The full title remains **Eggy and the Very, Very Tall Mountain**. The original incoming package is unchanged: all **217/217** entries in `SHA256.json` were rechecked successfully on the M4.

## Source and assets

`games/eggy/src/` is the cabinet's working copy; the separate `eggy_core` and `eggy_ui` libraries link into `game_ui`. The standalone `main.cpp` is preserved but is not linked into Games. The author's original handoff is copied to `games/eggy/INTEGRATION_HANDOFF.txt`. No GUI.Forms or sibling checkout was edited.

Delivered assets were copied without alteration or name collisions into `assets/audio/` and `assets/lines/`: 44 AAC files, five WAV ambience beds, and 17,918 lines in 53 categories. The original WAV music masters remain on the Neo. The cabinet bundle contains the runtime assets.

## Cabinet-specific changes

- Nested pointer coordinates are converted with `point_from_window()` before game-space scaling, so the bottom toolbar and mouse guidance work beneath the cabinet header.
- A visibility subscription immediately hides the Core Animation layer, releases held input, saves progress, and gates all Eggy audio channels. Hidden simulation ticks run once per second without composing frames or requesting new audio.
- Cabinet Music/Sound masters gate Eggy's own local switches. Cabinet quiet motion combines with Eggy's saved motion preference without rewriting that preference. Cabinet music stops when Eggy opens.
- `GAMES_STATE_DIR` also redirects Eggy's separate `eggy-v1.txt` (or developer save), permitting tests that never touch the real climb. The normal save location and format remain as delivered.
- Speech is omitted behind modal panels, preventing cached speech text from appearing over Help or Scores.
- A fresh climb writes its first save only after Begin. Merely constructing the cabinet does not create an offline climb or consume the base-camp introduction. Existing climbs continue as before.
- The native layer is detached when its control leaves the window.

## Presentation and limits

The delivered Core Animation presenter remains in use on macOS, confined to the Eggy control below the cabinet toolbar. The `EGGY_NO_NATIVE=1` LiveSurface fallback remains available. The handoff's reported ~20 ms LiveSurface CPU cost is author evidence; this task does not claim a GUI.Forms renderer fix or a new measurement of that path.

The original simulation tests pass in the combined build. Native cabinet checks cover title/start, base camp, lower-toolbar input, Help, hide/show, and save/resume. All 49 delivered audio files fully decode through AVFoundation. No multi-year endurance run, complete subjective listening review, or Windows/Linux cabinet validation is claimed.
