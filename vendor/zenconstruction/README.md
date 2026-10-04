# Zen Construction

Stack river rocks as tall as they'll stand, with a little yellow toy crane on a
rock bank by a brook. A rubber duck rides in the truck's cab; worksite feedback
appears as short status messages. Score is the height of the stack. Every site is saved with its seed and
its company's name on a sign, and you can flip between them.

## Layout

    phys/        the rigid-body engine (namespace zc::phys): convex hulls, a
                 Newton contact solve, sleeping islands, a hold constraint for
                 the crane; its own acceptance tests (phys/tests). See
                 PHYSICS_SPEC.md and phys/NOTES.md.
    src/         the game (namespace zc)
      rocks.*        rock generation: five classes, superquadric bases, noise
      stones.*       eight procedural stone textures
      run.*          the run: the bowl, the crane, the stack's rules, saves
      site.*         the worksite drawn (software 3D, sun shadows)
      terrain.*      shared rendered ground geometry and indexed collision surface
      crane_model.*  the crane and the duck
      shapes.*       mesh builders
      zen_view.*     the window: input, camera, HUD, panels, sound
      platform/      the shared software renderer (raster, r3d, mesh) and
                     the macOS adapters (text.mm, audio.mm, present.mm)
    tests/       zen_tests: pour, fetch, place, stack membership, save/load
    tools/       bench (timings), site_preview and preview (headless stills),
                 rockstats
    audio_src/   the synthesized sound and music (make_sfx.py, stone_garden.py)

## Build

    cmake -S . -B build -DCMAKE_PREFIX_PATH=<GUI.Forms SDK> && cmake --build build -j 8
    ctest --test-dir build

The app reads its sounds from assets/ next to this file (copy the package's
assets/ here first).

## Controls

Click a rock in the bowl and the crane fetches it and lifts it clear. Then
up/down arrows telescope the boom out and in, left/right swing it, W/S lower
and raise the line, Q/E turn the rock, R/F tip it toward or away from the
crane, Z/C roll it, Shift for fine work, Space lets go, B carries it back to
the bowl. Drag to orbit the camera, scroll to zoom.
