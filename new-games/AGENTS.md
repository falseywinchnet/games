# Building a PlaySuite game

Read the repository's root `AGENTS.md` and this procedure first. A game is one
folder, `games/<id>/`. Its manifest, build rules, factory, cover, help, assets,
tests and handoff live there. CMake discovers the folder. Adding a game requires
no edits to the collection, shelf, shared help, asset inventory or release version.

Preserve the person's objective, the game's complete rules and presentation,
and the behavior of every existing game. Follow the person's existing decisions
and publication authorization; do not ask them to approve the same action again.
Use [the house style](guide/10-house-style.md) for newly authored C++.

**Routine author path: create the folder → build and dogfood that game → commit
and push the folder to main. GitHub tests and publishes the revision.** The
sections below describe game development, not extra release administration.
Do not make an author maintain a roster, count, help index, asset list, version,
platform build matrix, package, release notes or release tag. Do not require a
pull request or a new approval when the owner already authorized direct publication.

## 1. Settle the idea and write the brief

Read [what belongs](guide/01-what-belongs.md), [the brief](guide/02-the-brief.md)
and [look, sound and words](guide/06-look-sound-words.md). Resolve material gaps
with the person while doing independent work. A sufficiently detailed request
already authorizes implementation. Keep optional choices reversible and record
assumptions in the brief and handoff.

Create the folder:

```sh
python3 new-games/tools/new_game.py --id tidepools --namespace tp --title "Tide Pools" \
  --kind "Shore puzzle" --blurb "One sentence that makes someone pick up the box."
```

The tool assigns the next unused permanent `entry_id`, including disabled
reservations. Never change an existing ID. Resolve a collision between concurrent
new submissions before merging. IDs need not be contiguous and have no 32-game
limit. Use `--directory /path/to/tidepools --entry-id 1001` to prototype outside
the repository; add it to a local suite using `GAMES_EXTRA_GAME_DIRS`.

Write `README.md` as the brief. Choose the name, rules, session length, artwork,
characters, input, save semantics and sounds. Preserve an imported game's full
behavior; document its provenance and intentional changes.

## 2. Prove the core independently

Read [anatomy](guide/03-anatomy.md). Keep rules deterministic, independent of
GUI.Forms and operating-system APIs. Pass time and input as values. Prove
solvability by replaying witnesses through the actual rules. Test saves, damaged
input, outcomes, layout and animation transitions.

```sh
cmake -S games/<id> -B .build/new-games/<id> -DCMAKE_BUILD_TYPE=Release
cmake --build .build/new-games/<id> --parallel 2
ctest --test-dir .build/new-games/<id> --output-on-failure --timeout 120
```

For an external kit folder, supply `-D<NS>_KIT_DIR=<repo>/new-games/kit`.
Run CPU-heavy builds through the root `AGENTS.md` host instructions when present.

## 3. Build and inspect the presentation

Read [primitives](guide/07-primitives.md), [the shell](guide/04-the-shell.md)
and [performance](guide/05-performance.md). Put state/layout below the GUI
adapter. Keep the original renderer's detail, actors and animation when importing.
Make previews of start, active play, every outcome and panel. Inspect them at
1100 × 760, 600 × 370 and 600 × 320, and scales 1, 1.5 and 2. Keep the inspected
PNG files in `screens/`, including a filename containing `600x370`.

A game built on a shared engine (for example a living scene on `shared/ambient`)
supplies the engine's data and interface instead of writing that machinery; read
the engine's README and [shared engines](guide/11-shared-engines.md).

Implement `module.cpp` using `games::HostedGameInstance<View>` or a module-owned
`GameInstance` adapter. The factory receives `ModuleContext` with `dev`, `hosted`
and shared-engine storage. Implement the cover in `cover.cpp`, and full player
instructions in `help.md`. Additional help sections belong in `help_topics` in
`GAME.json`. Keep `build.cmake` and the manifest's source lists current.

Generate audio into the game's `assets/audio/`; use unique prefixed names.
Keep synthesis scripts in `audio_src/` and exact 48 kHz music loop bounds in an
`*_audio_manifest.json`. Other resources go under the game's `assets/` tree.
Packaging discovers them and installs resources under the game ID automatically.

## 4. Exercise the actual standalone window

A preview and syntax check do not prove native behavior. Build the full native
application using [BUILDING.md](../docs/BUILDING.md) and the supported local host
recipe in the root `AGENTS.md`. An external folder is included with
`-DGAMES_EXTRA_GAME_DIRS=/absolute/path/to/tidepools` at configure time.

```sh
<games-executable> --list-games
<games-executable> --game tidepools --standalone --dev
<games-executable> --game tidepools --standalone --dev --script /path/to/play.script
```

`--dev` uses isolated temporary saves unless `GAMES_STATE_DIR` explicitly names a
test directory. A finite script is chronological milliseconds followed by an
operation; its final operation must be `quit`:

```text
100 key enter
600 click 300 200
1000 help
1500 key escape
2000 resize 600 420
3000 quit
```

Operations are `key`, `click`, `resize`, `help`, `command <id>`, module-defined
`action <value>`, `capture <file.ppm>` (the complete native renderer output), and
`quit`. In hosted mode, `shelf` and `open <id>` exercise
navigation. `resize` changes the toolkit client layout; manually resize the native
window as well to verify the platform frame. Use `--help` for the current syntax.
Scripts require `--dev`. Add explicit module actions for reproducible difficult
states, and refuse them in production and while hidden.

Check keyboard, pointer, focus, timing, help, all dialogs, save/reopen, masters,
minimum size and DPI. Look at captures of the real window, not only previews: a
contact sheet of every state at a glance catches what one picture at a time hides. Run the native view-contract test. Observe sound quality by
listening when possible; measurements alone do not establish that it sounds good.
If native dependencies are unavailable, record exactly what is blocked. A native
validation requirement remains open until tested locally or with a CI artifact.

## 5. Check the discovered suite

Read [on the shelf](guide/08-on-the-shelf.md). This command validates discovery;
it does not edit files:

```sh
python3 new-games/tools/wire_shelf.py <id>
python3 new-games/tools/check_game.py <id> --fetch-toolkit --application <games-executable> --script /path/to/play.script
```

Launch the hosted application with `--game <id> --dev --script
/path/to/hosted.script`. Check the new game's cover, help, commands and transitions
between the game and shelf. Its local rules and view-contract tests remain the
author's responsibility. GitHub runs the full suite, style, catalog and platform
checks automatically; do not manually repeat that matrix for a folder-only game
addition. Changes to shared infrastructure need focused regression testing of
that infrastructure. The generic catalog UI test visits every discovered module.

## 6. Deliver and publish

Give the game its front-page entry: `about.md` (two or three sentences on what
makes it worth playing) and `screens/readme.jpg` (one 800-pixel-wide picture of it in
play, from a `capture` in a `--dev` script). The README's game list is regenerated
from them after the push; nothing else on the front page needs editing.

Keep `HANDOFF.md` brief: tests actually run, real limitations, and non-obvious
provenance or design decisions. Do not copy CI logs or write a platform/release
report. Fix gate failures and explain remaining warnings. Keep build output and
personal saves out of git. A new game's commit normally changes only its folder.

For the owner's authorized direct-write workflow, commit that folder and push to
`main`; this automatically starts deployment. A feature-branch push alone does
not deploy. Use a pull request only when requested or required by repository
permissions, then merge it to trigger the same pipeline. See
[delivery](guide/09-deliver.md) for the short command sequence.

CI owns resource preparation, discovery, whole-suite regression tests, the four
platform builds, installer launch checks, version selection, tagging and release
publication. An author may hand off once pushed: say "submitted; automatic release
is running" and link the workflow. No polling, artifact download or manual
release work is part of routine game authorship. Return to fix a reported failure.
If the person specifically requests confirmation of a published release, verify
publication before claiming that outcome.

## Contracts to preserve

- One game folder and namespace; no dependency on another game's private files.
  Shared engines are collection infrastructure in `shared/<id>/`: the card and
  puzzle engines the original games build on, and engines a game declares in
  `GAME.json` (`"engines": [...]`). Machinery anticipated for a series of games, or shared by
  several, is promoted to an engine rather than copied into each; content stays in
  the games. See [shared engines](guide/11-shared-engines.md).
- No network, accounts or operating-system code inside game modules.
- Stable IDs and save formats; committed progress persists immediately. Document
  any original game's restart-at-level semantics accurately.
- Settled scenes schedule no frames. Deliberate moving game actors may continue
  while visible. Hidden games stop timers, workers and sound.
- The shell owns hosted navigation, help and Music/Sound/Motion masters. Reduced
  motion preserves game rules and meaningful actors.
- Preserve all games, rendering quality and tests. Never simplify to make a port
  or performance check pass. Request shared capabilities from GUI.Forms through
  the established toolkit workflow.
- Respect original/public-domain asset provenance and the repository license.
- Reports describe observed evidence. A successful core test is not native UI,
  auditory or cross-platform validation.
