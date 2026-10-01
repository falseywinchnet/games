# Owner-maintained modules

Switchbox is reserved for the user's implementation. **The owner's replacement has been delivered as `incoming/game-switchbox-01`** (2026-10-01). It is not integrated yet. Integrate that package into kind 5 / slot 7 by following its `INTEGRATION_HANDOFF.txt`, through a `vendor/switchbox` working copy; leave the incoming package unchanged. It uses a new save, `switchbox-v2.txt`, so the old `switchbox-v1.txt` below stays untouched. The rest of this section describes the removed predecessor.

 The previous five-switch secret-order model, reset animation, painted character arm, input handling, help, and associated rule tests have been removed. The active collection does not construct or offer that view. No new Switchbox artwork or animation is being integrated by this task.

Existing artwork (`assets/switchbox-girl.png`), delivered audio, and `switchbox-v1.txt` saves remain available. The historical numeric kind (5) and collection slot (7) remain reserved so existing unrelated game IDs do not move. No new logic is prescribed for the replacement. The shared native control, persistence, audio, and presentation interfaces can be used when the owner supplies the module.

**Four Pegs has an owner replacement too**: `incoming/game-fourpegs-01` (2026-10-01), approved by the user and not yet integrated. It replaces the current Four Pegs view in slot 6 (`PuzzleKind::pegs`) with `fp::FourPegsView` through a `vendor/fourpegs` working copy; its `INTEGRATION_HANDOFF.txt` has the details. It saves to `four_pegs-v2.txt` and starts fresh; `four_pegs-v1.txt` is left untouched.

**Atom Probe has an owner replacement too**: `incoming/game-atomprobe-01` (2026-10-01), approved by the user and not yet integrated. It replaces the current 4×4 Atom Probe in slot 5 (`PuzzleKind::atom`) with `ap::AtomProbeView`, a classic 8×8 Black Box with four atoms, through a `vendor/atomprobe` working copy; its `INTEGRATION_HANDOFF.txt` has the details. It saves to `atom_probe-v2.txt` and starts fresh; `atom_probe-v1.txt` is left untouched.

Sticks & Stones is deprecated and is no longer constructed or offered by the collection. Its original numeric puzzle kind (7), files, and saves are retained. Its former collection slot (9) now opens the delivered Eggy control; no other active collection IDs changed. Eggy is integrated from `incoming/game-eggy-01` through the separate `vendor/eggy` cabinet copy.
