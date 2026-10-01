# Owner-maintained modules

Switchbox is reserved for the user's implementation. **The owner's replacement has been delivered as `incoming/game-switchbox-01`** (2026-10-01). It is not integrated yet. Integrate that package into kind 5 / slot 7 by following its `INTEGRATION_HANDOFF.txt`, through a `vendor/switchbox` working copy; leave the incoming package unchanged. It uses a new save, `switchbox-v2.txt`, so the old `switchbox-v1.txt` below stays untouched. The rest of this section describes the removed predecessor.

 The previous five-switch secret-order model, reset animation, painted character arm, input handling, help, and associated rule tests have been removed. The active collection does not construct or offer that view. No new Switchbox artwork or animation is being integrated by this task.

Existing artwork (`assets/switchbox-girl.png`), delivered audio, and `switchbox-v1.txt` saves remain available. The historical numeric kind (5) and collection slot (7) remain reserved so existing unrelated game IDs do not move. No new logic is prescribed for the replacement. The shared native control, persistence, audio, and presentation interfaces can be used when the owner supplies the module.

Sticks & Stones is deprecated and is no longer constructed or offered by the collection. Its original numeric puzzle kind (7), files, and saves are retained. Its former collection slot (9) now opens the delivered Eggy control; no other active collection IDs changed. Eggy is integrated from `incoming/game-eggy-01` through the separate `vendor/eggy` cabinet copy.
