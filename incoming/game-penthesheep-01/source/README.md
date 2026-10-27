# Pen the Sheep

A sheep stands in the middle of a hex meadow and wants out. Click a patch of
grass to put up a fence; every fence joins the fences beside it with rails,
so walls build themselves as you go. The sheep then trots one patch toward the edge,
turning unhurried to face the patch it means to go to next. The moment it
reaches any edge patch, from any direction, it walks straight off the field
the way it was heading and is gone. Fence it in so no way leads out and it
sits down and sulks, then dozes off: you win. You get three fences before it
starts moving. There's no fence round the meadow itself; old, weathered
fence panels already stand here and there, and yours join onto them.

## Sheep and meadows

- **Three kinds of sheep:** a dozy sheep dawdles now and then; a clever one
  takes the shortest way with the most routes onward; a cunning one asks of
  every step where you'd put your next fence.
- **Clover:** any sheep will stop for clover. It steps onto a patch beside it
  and spends a turn munching, which gives you a free fence.
- **The meadows:** endless, made in the background as you play.
  - They run from 9x9 meadows with plenty of old fences and dozy sheep to
    11x11 meadows with fewer and cunning sheep.
  - Clover appears from meadow 5.
- **Proven winnable:** the sheep is deterministic, so a meadow is only
  accepted once the game's own shepherd, a solver bot, has penned its sheep.
  - The bot judges positions by the fewest fences that would still wall the
    sheep in (a max-flow cut) against how soon it can reach the edge.
  - Its fence count is the par: three stars at or under par, two within three
    fences, one for any win. Taking a hint caps the meadow at two stars.
- **Tools:** undo, restart, and a hint. Hovering Hint traces the sheep's
  shortest way out.
- **The Meadows map:** shows your stars and lets you replay any meadow.

## Music

Studied closely from the original hex-pen puzzle's soundtrack (never sampled
or shipped). The original has:

- **No melody and no beat:** a shimmering wash of soft, nearly pure tones
  swelling in at random about twice a second.
- **Harmony:** C6/9 over a held low C, chorused, very wide, with birdsong.

The game's main track, "Pasture Haze", follows that recipe with new material,
and keeps it heavenly rather than uneasy:

- **Gentle chorus:** each tone is five voices spread about ±14 cents, drifting
  slowly.
- **Consonant:** pentatonic only (C D E G A), with octave and fifth doublings
  over a soft C and G bass.
- **Space:** an airy reverb and a wide stereo image.

It matches the original's frequency balance, stereo width and mix of intervals.
"Long Grass", an earlier, more melodic piece in the same key and spirit, plays
on every even meadow. Everything is synthesized (`audio_src/`).

## Build

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=$PWD/../eggy/sdk && cmake --build build -j8
```

- `build/PenTheSheep.app` is the game.
- `build/sheep_tests` checks:
  - the hex neighbourhoods and the wall measure
  - the head start, the walk-off at the edge, and clover
  - that the sheep is deterministic
  - that 36 campaign meadows each replay their proof to a penned sheep
  - that cleverer sheep are harder
- `build/levelstats` prints the difficulty curve.
- The window resizes freely down to 600 x 370 points (the cabinet's 600 x 420
  less its rail) and the layout reflows to fit.
- `--dev` uses a separate save and allows `SH_SCRIPT` (including `size600x370`
  to resize the window mid-run) and `SH_WINDOW=WxH` to open at a given size.
- Saves are `pen_the_sheep-v1.txt` under `GAMES_STATE_DIR`: the meadow you're
  on with its fences, stars per meadow, versioned and checksummed.
