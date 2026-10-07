# Building a PlaySuite game

Read the repository's root `AGENTS.md`, then [README.md](README.md): its rules and
requirements are the acceptance criteria for everything below. A game is one
folder, `games/<id>/`, discovered from its `GAME.json`. Adding a game edits nothing
outside that folder.

Follow the person's decisions and their existing publication authorization. Ask
only about gaps that change the game. Write new C++ in
[the house style](guide/10-house-style.md).

## 1. Brief

Decide, with the person, the title (at most 28 characters), kind (at most 24), blurb
(at most 90), id (3 to 20 lowercase letters and digits) and namespace (2 to 4
lowercase letters, unused). These are permanent. Then create the folder:

```sh
python3 new-games/tools/new_game.py --id tidepools --namespace tp --title "Tide Pools" \
  --kind "Shore puzzle" --blurb "One sentence that makes someone want a turn."
```

The tool assigns the next unused `entry_id`. Never change an ID. To prototype
outside the repository, add `--directory <path> --entry-id <n>` and include the
folder in a local suite with `GAMES_EXTRA_GAME_DIRS`.

Write `README.md` in the game folder as the brief, before any code, under these
headings. The gate refuses a brief containing `TODO`.

- **The game.** Two sentences.
- **Rule 2.** Classic or new. For a classic, its source and any intentional
  changes. For a new game, why its decisions stay interesting.
- **A session.** The first minute, the length of one game, and what the player
  finds on returning mid-move.
- **Replayability.** What varies between games, the difficulty levels, and the
  tutorial levels if any.
- **Passive mode.** For a long real-time game (README requirement 4), how the
  engine plays and how the player takes over; otherwise, why none is needed.
- **Rules.** Complete enough for a stranger to referee. How generated content is
  proven finishable. How computer players decide, without cheating.
- **Look, sound and words.** The style (README rule 3), the scene, the palette,
  the music's character, the voice of any character with three sample lines.
- **Controls.** Pointer, keyboard, and the command bar's commands.
- **Not included.** Each addition considered and declined, with the reason.

The first version is the smallest complete game: one mode, one scene, sound, help,
saves, and the passive mode if the game needs one.

## 2. Rules and tests

Read [anatomy](guide/03-anatomy.md). Rules are deterministic and independent of
GUI.Forms and the operating system; time and input arrive as values. Prove every
generated level finishable by replaying a witness through the real rules.

```sh
cmake -S games/<id> -B .build/new-games/<id> -DCMAKE_BUILD_TYPE=Release
cmake --build .build/new-games/<id> --parallel 2
ctest --test-dir .build/new-games/<id> --output-on-failure --timeout 120
```

For a folder outside the repository, add `-D<NS>_KIT_DIR=<repo>/new-games/kit`.
Run heavy builds through the root `AGENTS.md` host instructions.

## 3. Presentation

Read [the shell](guide/04-the-shell.md), [primitives](guide/07-primitives.md) and
[performance](guide/05-performance.md). A game built on a shared engine supplies
that engine's data and interface; read its README and
[shared engines](guide/11-shared-engines.md).

- `module.cpp`: the factory, using `games::HostedGameInstance<View>` or a
  module-owned `GameInstance`.
- `cover.cpp`: the box emblem.
- `help.md`: goal, moves, how a game ends, keys. More sections go in
  `help_topics` in `GAME.json`.
- `assets/`: audio named with the game's prefix, loop bounds in
  `assets/audio/<id>_audio_manifest.json`, other resources beside them. Synthesis
  scripts go in `audio_src/`.

Render every state (start, play, each outcome, each panel) at 1100 × 760,
600 × 370 and 600 × 320, at scales 1, 1.5 and 2. Look at every picture. Keep them
in `screens/`.

## 4. The native window

Previews do not prove native behaviour. Build the application
([BUILDING.md](../docs/BUILDING.md) and the root host recipe) and run:

```sh
<games> --game <id> --standalone --dev
<games> --game <id> --standalone --dev --script play.script
<games> --game <id> --dev --script hosted.script
```

A script is lines of `<milliseconds> <operation>`, ending in `quit`. Operations:
`key`, `click X Y`, `resize W H`, `help`, `command <id>`, `action <value>`,
`capture <file.ppm>`, and, when hosted, `shelf` and `open <id>`. `--dev` keeps
saves temporary. Add module actions to reach hard states; refuse them outside
`--dev`.

Check keyboard, pointer, focus, help, dialogs, save and reopen, the masters, the
minimum size and each scale. Measure idle and animating cost with
`--profile-idle`. Lay captures of every state side by side and inspect them
together.

Audio cannot be judged by measurement. Send the person the files and record in
`HANDOFF.md` what has and has not been listened to.

## 5. Check and submit

```sh
python3 new-games/tools/check_game.py <id> --fetch-toolkit --application <games> --script play.script
```

Add `about.md` (two or three sentences on what makes the game worth playing) and
`screens/readme.jpg` (800 pixels wide, captured in play). The repository's front
page is generated from them.

Keep `HANDOFF.md` short: tests run, known limits, provenance, and decisions a
reader would not guess. Commit only the game folder.

- **The owner, or someone with their authorization:**
  `git add games/<id> && git commit -m "Add <Title> to the shelf" && git push origin HEAD:main`.
- **Anyone else:** a pull request adding only the folder, as in
  [README.md](README.md).

CI prepares assets, discovers the game, runs every test, builds Windows, macOS and
both Linux architectures, tests the installers and publishes a release. Do not edit
counts, catalogs, versions or packaging, and do not repeat the platform matrix
locally. After pushing, link the
[workflow](https://github.com/falseywinchnet/games/actions/workflows/applications.yml),
and return only to fix a failure it reports.

## Craft

These standards apply to every game.

**Look**
- The game happens in a place. Decide the place before drawing the board, and
  avoid the obvious one.
- Text is always sharp, whatever the scene's pixel size.
- Reuse the collection's materials where they fit: card faces, felt, wood, paper.
  Anything new must sit beside them as an equal.
- Nothing passes through anything else. Actors face where they move. A camera, if
  any, moves gently. Nothing animated covers a control.
- Four or five colours and their shades, with one accent for what matters now.
  Check contrast on every panel.
- At the smallest window the board is still the largest thing on screen. Nothing is
  clipped or overlapped at any size.
- With reduced motion, changes appear at once and the game still looks finished.
- Generated things vary as real things do. Draw from reference, not memory.

**Sound**
- Synthesize every sound in the game's own code. Play it live where synthesis is
  faster than real time; music may be a composed loop or a live, improvising
  generator.
- Study what the real thing sounds like before synthesizing it.
- Every action has a short, soft sound that says what happened. Vary the pitch of
  repeated sounds. Fanfares are for wins only. Sound never reveals hidden
  information.
- Background sounds are faint. Music can play all afternoon without wearing.
- Leave headroom; normalise filters; keep every part in balance. Fade out over a
  tenth of a second rather than cutting.
- Give layered audio a way to play one part alone. When a sound is a matter of
  taste, render several labelled candidates for the person to choose.

**Words**
- Labels name the thing plainly. English, sentence case, fitting the smallest
  window.
- Help is short and complete: goal, moves, ending, keys.
- A character has one consistent voice, enough lines that a session does not
  repeat, reactions that follow what just happened, and an escalation for
  repeated mistakes. No mockery of the player or of anyone real.

## Contracts

- One folder and namespace; no use of another game's private files. Machinery
  several games need belongs in `shared/<id>/` as an engine the game declares in
  `GAME.json`.
- No network, accounts or operating-system code inside a game.
- IDs and save formats are permanent. Committed progress is saved at once.
- Settled scenes schedule no frames. Hidden games stop timers, workers and sound.
- The shell owns navigation, help, the Settings screen and the Music, Sound and
  Motion masters and volumes. A hosted game shows no switch or entry of its own
  for them.
- Commands are actions. A choice that persists (level, size, look, detail) is a
  `GameSetting` from `settings()`, not a command.
- Never remove a game, lower its quality or weaken a test to make a check pass.
  Toolkit capabilities go to GUI.Forms as a reviewed pull request.
- Reports state what was observed. Passing rules tests is not native, audio or
  cross-platform validation.
