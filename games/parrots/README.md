# The Parrot's Table

Tea in the parlor, and something has happened: the last cracker is gone, the
teapot is on its side, someone pecked the cake, the kettle has learned rude
songs. Four to seven parrots sit in a row behind the table. Each is either
**honest** (everything they say is true) or a **liar** (everything they say is
false), and the host tells you exactly how many are lying. One of them did it.

Each parrot says their piece in turn, in their own manner:

- the posh macaw ("One hates to gossip, but...")
- the salty sea-dog ("Shiver me feathers...")
- the nervous budgie ("...sorry.")
- the gossip ("You didn't hear it from me, but...")
- the grump ("Now leave me alone.")
- the scholar ("Q.E.D.")
- the drama queen ("I shall never recover!")
- the sunny one ("Isn't this fun?")

The wording is drawn fresh each time, but every phrasing means exactly its
logic and nothing more.

## Play

- **Mark beaks:** click a parrot's beak (or press 1–7) to mark it honest
  (green), a liar (red), or unmarked again.
- **Contradictions:** the notebook at the bottom keeps every line. A line turns
  red the moment your marks make it impossible, either on its own or alongside
  another line. Only worlds with the right number of liars count. The speaker
  shakes their head as it happens.
- **Silent parrots:** some birds won't volunteer anything. You get one or two
  questions, chosen from a short list of yes/no questions. At least one
  question on offer settles the case. At least one doesn't: you can ask it
  truthfully and still be left guessing. Liars answer falsely, of course.
- **Accusation:** pick the bird, and lines that can't hold if it did turn red.
  Then confirm.
  - **Right:** the culprit confesses, the table cheers and confetti falls.
  - **Wrong:** the wrongly accused is outraged, the culprit gloats and flies
    out of the window, and every beak shows its true colour.
- **Records:** tables solved, the current streak and the best streak.

## The tables

- **Generation:** every table comes from a generator with a hidden truth: who
  lies, and who did it.
  - Each speaking parrot gets a statement that is true if they're honest and
    false if they lie, drawn from sixteen kinds: X is a liar, X and Y are both
    honest, exactly one of X and Y lies, X and Y are the same sort, it was X or
    Y, if X is honest then Y did it, whoever did it is a liar, exactly N of us
    lie, one of my neighbours did it, I didn't do it...
- **Solvability:** every table is proven by enumerating every possible world.
  - Without silent birds, exactly one world fits.
  - With them, two to six fit until the right question is asked, and the
    choice must matter: never every question settles it.
- **Difficulty:** a human-style grader rates each table by its single
  inference steps and suppositions. The game picks, among several candidates,
  the one whose difficulty suits the table number.
- **Progression:** tables grow from 4 parrots to 7, and the statement kinds
  get subtler. Silent birds appear from table 4, two at a time from table 12.
  Twins (who look alike) turn up now and then.

## Sound

- **Music:** "Tea for Seven", a small chamber waltz.
  - Clarinet tune, harp "pah-pah", pizzicato "oom", a flute answering in a
    tiptoeing minor B section, and celesta sparkles.
  - A seamless 32-bar loop.
- **Voices:** a squawky babble while each bubble types out, pitched by
  species.
- **Effects:** a table bell, marks, a disapproving "hmm" for a contradiction,
  the gavel, wings, and a cheering table.
- **Synthesized:** everything (`audio_src/`).

## Build

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=$PWD/../eggy/sdk && cmake --build build -j8
```

- `build/ParrotsTable.app` is the game.
- `build/logic_tests` checks:
  - the statement semantics against an independent reference
  - that hundreds of generated tables keep their promises
  - that the voices are varied
- The window resizes freely down to 600 x 370 points (the cabinet's 600 x 420
  less its rail) and the layout reflows to fit.
- `--dev` uses a separate save and allows `PT_SCRIPT` (including `size600x370`
  to resize the window mid-run) and `PT_WINDOW=WxH` to open at a given size.
- Saves are `parrots_table-v1.txt` under `GAMES_STATE_DIR`, versioned,
  checksummed and atomically replaced.
