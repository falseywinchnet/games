# Liar's Dice

You drowned. You owe Davy Jones' locker a hundred years. The only way to win
them back is at the dice table in the captain's cabin of a long-sunk ship,
where the drowned play under a swinging lantern. Behind the players, a great
stern window shows the Keeper of the locker: an enormous anglerfish whose lure
glows when he speaks.

## The wagers

Before every game the Keeper lays three wagers on the barrel, from a friendly
hand to everything on the barrel. Each card names:

- **The company:** who sits down with you: two easy marks, or three of the sharpest.
- **The stakes:** how many years a loss adds and a win strikes off, from 1 year
  up to 100.
- **Something stranger:** lose, and he keeps your shadow, your singing voice or
  your sense of direction in his jar. Win, and you take home a pearl the size
  of your fist, a ship in a bottle with a real crew, or one of your lost things
  back.

The wagers are generated, and the same three stay on offer until you take
one: quitting won't reshuffle them. When your debt reaches zero you're free,
and you may play on for trinkets. Lose, and the years come back.

## The game

- **Dice and bids:** everyone has five dice under a leather cup. In turn, each
  player raises the bid ("seven fours": at least seven dice on the table show a
  four), or calls the last bid a lie.
- **Ones are wild,** and nobody bids on them.
- **The reveal:** on a call every cup comes up, the dice that count glow one by
  one with a running tally, and whoever was wrong loses a die into the
  Keeper's jar.
- **Winning:** lose all your dice and you lose the wager; be the last with dice
  and you win it.
- **Your panel:** your dice, a composer (number, face, Bid) and a big Liar!
  button. It also tells you how many of the chosen face you hold and how many
  dice are hidden.
- **Atmosphere:** the room darkens and the music turns to a heartbeat when the
  bids climb past what's likely.

## The crew

- **Who they are:** a fixed cast of 32, generated once from their names, so
  they're the same every time. There are four each of crab, octopus, skeleton,
  grouper, eel, turtle, shark and ghost, each with their own colours, hat and
  voice.
- **Their habits:** each has a bluff rate, a nerve for calling, greed, skill at
  reading odds and other players' bids, and a speed at catching on to *your*
  bluffing. The crew remember how you bid across games.
- **Their tells:** each has a tell, distinct among the 32: a glance down at
  their own cup, a flush of colour, a double blink, leaning back, a tap, a few
  bubbles, a sway, or a twitch.
  - It shows on about half their bluffs and about one honest bid in eleven.
  - It is faint, and theirs alone.
- **The logbook:** keeps your record against each crew member and how often
  you've seen them bluff. A tell is written in once you've caught it three
  times.
- **Danger ratings:** measured, not guessed. `tools/ladder` plays 40,000
  three-handed games among the crew. The strongest win about 40% of the time,
  the weakest about 23%, against a fair share of 33%.

## Sound

- **Music:** "The Locker Waltz", a drowned sailors' waltz in D minor, in two
  arrangements of the same bars, so the game cuts between them on the beat:
  - **Calm:** concertina tune, banjo pluck, tuba oom-pah, glassy shimmer.
  - **Tense:** heartbeat kick, low pizzicato, a trembling glass.
- **Stingers:** "Liar!", the bid stands or falls, a win, a loss, and freedom.
- **Effects:** dice rattling in leather cups, a die plunking into the jar,
  voices for each kind of creature and for the Keeper.
- **Synthesized:** everything (`audio_src/`).

## Build

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=$PWD/../eggy/sdk && cmake --build build -j8
```

- `build/LiarsDice.app` is the game.
- `build/dice_tests` checks:
  - the rules
  - the cast (32 fixed, every kind-and-tell pair unique)
  - the odds model against simulation
  - 1,500 crew matches: always legal; skill shows; tells correlate with bluffs
  - adaptation to a heavy bluffer
  - the wagers
  - the ledger round trip
- `build/preview` renders the cabin headless.
- The window resizes freely down to 600 x 370 points (the cabinet's 600 x 420
  less its rail) and the layout reflows to fit.
- `--dev` uses a separate save and allows `LD_SCRIPT` (including `size600x370`
  to resize the window mid-run) and `LD_WINDOW=WxH` to open at a given size.
- Saves are `liars_dice-v1.txt` under `GAMES_STATE_DIR`, versioned,
  checksummed and atomically replaced, with any game in progress (dice and
  all) kept.
