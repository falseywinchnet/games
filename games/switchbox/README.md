# Switchbox

A girl lives in a toy box with six switches on top. The box knows a secret
order for all six. Flip the right switch next and it stays up with its lamp
lit while she frets. Flip a wrong one and every lamp goes out, and she pops
out of her hatch to push down the wrong switch and everything that was lit.
Light all six and she claps, beams, pushes everything back down, stretches,
and lies down again, and the box makes a new combination. Score is
the number of flips taken to crack a combination; fewer is better.

Native C++20 on GUI.Forms, using the techniques Eggy proved out (small software
3D renderer, pixel-art presentation through a Core Animation layer, crisp
text sprites, synthesized audio), with additions for her: two-tone toon light,
ink outlines, a painted swappable face, IK arms and springy hair.

## Play

```sh
open build/Switchbox.app
```

Click a switch (or press 1 to 6). F1 help, T top scores, M music, N sound,
Esc closes a dialog. Things she does not like:

- **Holding a switch down.** Her hand rests on your pointer, patiently at
  first, then crossly, then in a full meltdown. Hold out long enough and one
  hand lifts your pointer away while the other pushes the switch down. When
  she drops it, your real cursor is moved to where she dropped it.
- **Flipping the same switch over and over.** On the fourth flip in a row
  she steals it. Click its empty socket and she pops up to taunt you with it,
  then slots it back in from inside the box. If you never ask, she comes out
  to gloat after about 15 seconds.
- **Bonking her head.** Twenty clicks take her from "eep" through cross,
  teary, shouting and dizzy. Then she flees, and for 30 seconds the switches
  pop up and down like whack-a-mole. Whack at least 40 and she has something
  special to tell you. Saves go to
`~/Library/Application Support/Rainstar/Games/switchbox-v2.txt` (`GAMES_STATE_DIR` overrides the folder). The older `switchbox-v1.txt` belongs to the removed earlier Switchbox and is never touched.

`--dev` uses a separate save. With `--dev`, `SBX_SCRIPT="1.0:2,1.1:4,3.5:-1,6:7"`
replays timed inputs (`switch`, `-1` head click, `7` the next correct switch, `8` whack, `9` a wrong switch, `10` the stolen switch's hole)
for repeatable captures; `SBX_PRINT_WID=1` prints the window id for
`screencapture -l`.

## Build

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=$PWD/../eggy/sdk && cmake --build build -j 8
ctest --test-dir build                         # rules and patience ladder
./build/preview faces out.ppm                  # every expression on one sheet
./build/preview anim out.ppm 13 .25 5.5 "0.4:n,1.5:w"   # scripted session as a contact sheet (n next correct, w wrong, h head)
python3 audio_src/make_sfx.py                  # regenerate clap and babble
```

## How it works

| Part | Files |
|---|---|
| Rules: hidden order, lamps, steps, her patience ladder (point, "you forgot", scold, rip out, restore; stuck scold and grumbles) | `puzzle.*` |
| Behaviour: springs on every body channel; popping up, peeking and ducking; a two-hand job queue with reach and press; lean solving; pointing hints; the celebration; holding, carrying the pointer, theft, taunting and returning; the bonk ladder; moods, blinking and gaze; cartoon marks (anger, steam, sweat, hearts, sparkles) | `actor.*` |
| Her rig: chibi body, oversized sweater, IK arms, strand hair with an angel ring, ahoge, star clips | `girl.*` |
| Her face: painted expression textures (10 eye shapes, 4 brows, 11 mouths, blush levels, gaze) | `face.*` |
| Box, switches, lamps, lid, room, camera, picking | `stage.*` |
| Whack-a-mole timing and scoring | `mole.*` |
| Window, input, HUD, speech bubbles (muffled from inside the box), babble, the hidden and moved system cursor, top scores, autosave | `switchbox_view.*`, `save.*`, `platform/present.mm` |
| Borrowed from Eggy (renderer gained `toon` and `inverted` outline materials) | `platform/` |

Music and most effects are the existing "Peekaboo Switches" batch from
`../outputs`. The clap and her babble syllables are synthesized in
`audio_src/make_sfx.py`. The line bank in `assets/lines/` has 206 lines in 30 categories, written for this game.

## Status

Everything in the brief is in: the combination with lamps, her patience ladder
(pointing, "you forgot", scolding, ripping the switches out and putting them
back), holding, stealing and taunting, the twenty-bonk fluster ladder,
whack-a-mole with the special message, top scores and saves.

Checked with scripted runs: frame renders of each behaviour, and the real app
driven by `SBX_SCRIPT` (bonks through whack-a-mole to the special message).
Rules and whack-a-mole reachability are covered by `ctest`.

Not yet verified:
- **Holding with a real mouse:** the hide-and-move-the-cursor part of the
  holding gag has not been tried by hand; only the scripted render has been
  seen.
- **Listening:** nobody has listened to the clap or babble yet.
- **Reduced motion:** not implemented.
