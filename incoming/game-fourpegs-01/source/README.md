# Four Pegs

A composed gentleman, as formal as a butler, with a monocle and an old
duelling scar, has locked tonight's plan behind a code of four pegs in six
colours (colours may repeat). Each new game resets the console while he makes
the stakes plain, with impeccable manners: "Should you fail to name my code
within ten attempts, I shall fold the Moon into a small paper swan, while a
full orchestra plays." His schemes are generated from grand deeds, absurd
targets and dramatic flourishes, tens of thousands of them.

He leans in to inspect every guess. Then the pins light (gold for exact,
white rings for elsewhere) and he tells you the counts in his own words. He
steeples his fingers while you think, strokes his chin, straightens his tie,
and consults his pocket watch as your attempts run out, reminding you now
and then what failure will cost. If a guess is no better than your best so
far, he adds a jab at your intelligence, ability, determination or patience,
sharper the longer you fail to improve ("Are you guessing, or simply pressing
things?"). Crack it and he loses his composure entirely in a generated
"curses, foiled again": "Rats! Rats, rats, RATS!" ... "I polished the console
for this!" ... "You have not seen the last of me!". Run out and he keeps his
promise: he rises, arms flung wide, laughing maniacally, while the lair
trembles, cracks and falls apart around him, until the screen goes dark on
GAME OVER, what he went on to do, the code, and Try again.

The score is "The Mastermind's Console", one theme in three tiers: Scheming,
Rising, Doom clock. It cuts to the next tier on a bar line as your attempts
dwindle.

Native C++20 on GUI.Forms, using the techniques Eggy and Switchbox proved out:
a software 3D renderer (now with perspective), toon light and ink outlines,
painted expression textures, IK arms and pixel-art presentation.

## Play

```sh
open build/FourPegs.app
```

Drag pegs into the sockets, or click a peg to fill the next socket. Click a
placed peg to remove it, then press the big red CHECK button. Every attempt
goes on the test sheet at the right. Keys: 1-6 place a peg, Backspace
removes one, Enter checks. T top scores, M music, S sound, F1 help.

Saves go to `~/Library/Application Support/Rainstar/Games/four_pegs-v2.txt`
(`GAMES_STATE_DIR` overrides the folder). The earlier `four_pegs-v1.txt` is
never touched. With `--dev` (separate save), `FP_SCRIPT="9:w,9.6:c"` replays
inputs: `n` new game, `w` a guess, `x` three exact, `s` the answer, `c` check,
`p` poke. `FP_PRINT_WID=1` prints the window id, and `FP_AUDIO_LOG=1` logs
each bar-line music cut.

## Build

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=$PWD/../eggy/sdk && cmake --build build -j 8
ctest --test-dir build                                  # rules; the evaluator against all 1,679,616 pairs
./build/preview faces|pose|anim out.ppm ...             # expressions, a still, a scripted session
python3 audio_src/villain_music.py && python3 audio_src/make_sfx.py   # score, stingers, voice, effects
```

## How it works

| Part | Files |
|---|---|
| Rules: secret, draft, exact/elsewhere evaluation, save state | `board.*` |
| His lines: generated schemes and the stakes, orations, verdicts with the counts, "foiled again" and triumph | `script.*` |
| His performance: 16 gestures blended by springs, inspection, then verdict, endings, idle business, gaze, talking | `vactor.*` |
| His figure: tailcoat, waistcoat and watch chain, wing collar and bow tie, gloves that steeple, point and hold a pocket watch | `villain.*` |
| His face: painted expression textures | `vface.*` |
| The lair: porthole, red alert lighting, the console (sockets, palette, the red CHECK button, readout, pins, the rising secret), picking | `lair.*` |
| Window, input, the test sheet, speech and voice, adaptive music, dialogs, autosave | `fourpegs_view.*`, `save.*` |
| Shared platform (perspective added to the renderer; bar-line music cuts added to audio) | `platform/` |

All audio is synthesized (`audio_src/`): three music tiers (116/128/144 BPM,
32 bars each), three stingers, his eight voice syllables with a "hm" and a
chuckle, and the console effects. The pins' sounds say only what the pins
show.

## Not yet verified

- **Listening:** nobody has listened to the score, the voice or the effects
  yet. The tier cuts are confirmed to land on bar lines (logged), but not by ear.
- **Reduced motion:** not implemented.
- **Cabinet integration and other platforms:** not done.
