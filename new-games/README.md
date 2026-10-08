# Submitting a game to PlaySuite

PlaySuite is a collection of small games for Windows, macOS and Linux. It runs on
any computer made since about 2015 and is played by anyone, anywhere. A game joins
when it meets every rule and requirement below. Submissions are judged against
these and nothing else, and every decision comes with its reasons.

## Rules

1. **Casual.** A player can begin without instruction, stop at any moment, and
   return to exactly where they stopped. A game that depends on sustained
   attention, punishes a pause, or asks for a long investment of time or effort
   does not qualify. Adventure and combat games do not qualify.

2. **A classic or something new.** The game is either a classic, presented with
   care and its complete rules, or an original game that nobody has made before.
   An original game has depth: its decisions stay interesting after many plays. A
   single reflex or a single repeated action is a demo, not a game.

3. **Style.** The game uses one of three looks:
   - isometric;
   - the house 3D style: a low-resolution, software-rendered 3D scene, textured
     and lit, enlarged with visible pixels, under sharp text;
   - polished 2D: vector drawing with depth, shading and texture.

   It is finished at every window size and stands beside the games already in the
   collection without looking unfinished.

4. **Replayable.** The game stays worth playing for a long time, through generated
   content, chosen difficulty, or both. If it needs teaching, the teaching is its
   easiest difficulty: short levels that each show one mechanism, chosen at random.
   There is no fixed tutorial sequence.

5. **Positive.** Content suits every age and any public place. Characters are
   warm, humour is aimed at no one, and stakes are theatrical.

6. **Self-contained and original.** No network, accounts, advertising, purchases,
   streaks or notifications. Every character, image, sound and line of text is
   original to the game or licensed for redistribution under the repository's
   licence. No brands, and nothing taken from another work.

## Requirements

These are checked when the game is built and reviewed.

1. **Standard controls.** The game uses the collection's controls: the command
   bar with New game, shared help (H or F1), the Music and Sound masters (M toggles
   music), the shared Settings screen for choices that persist, Escape to close a panel, Enter or Space to confirm, Z or Backspace to
   undo where undo exists, and the arrow keys wherever the rules allow keyboard
   play.
2. **Saves.** A game with state saves it, through the collection's save format,
   after every committed change, and resumes it on reopening.
3. **Compute.** When nothing moves, the game does no work. While it animates, it
   uses less than 30% of one core. When hidden, it stops entirely.
4. **Passive mode for long real-time games.** A game played with continuous,
   real-time control over a long session, such as a long climb, offers a passive
   mode: its own engine plays, and the player can watch and take over at any
   moment. Puzzles, card games and other turn-based games do not need one.
5. **Size.** The game's folder holds at most 64 MB, so source audio and artwork
   can be kept at full quality, and what ships after PlaySuite transcodes its
   audio and images is at most 16 MB. Most games ship far less.
6. **Windows and sizes.** The game works on all three systems, in a window as
   small as 600 × 420, at display scales 1 to 2.
7. **Sound.** Music follows the Music master and volume, everything else the
   Sound master and volume. All audio is made by the game's own code.
8. **Tests.** Rules, saves and layout are tested, and the game passes the
   submission check.
9. **Only what a game needs.** The game is ordinary C++20 that computes, draws
   through GUI.Forms and the kit, and plays sound through PlaySuite. It saves and
   loads through PlaySuite (`src/game_data.hpp`: `games::save_game_data`,
   `games::load_game_data`, `games::load_game_asset`) and runs background work on
   `gui_forms::Worker`. It does not open files, read the environment, start its
   own threads, use the network, start programs or load libraries. The template
   already works this way.

## What is checked automatically

Every pull request runs `tools/check_contribution.py`. A pull request from a fork
must pass all of it; a failure says what was found and where.

- **One folder.** The pull request changes only `games/<id>/`, for one game.
- **Size.** The folder is at most 64 MB, and what ships at most 16 MB.
- **Source only.** No compiled files (libraries, executables, object files), no
  Objective-C or assembly sources, no symbolic links.
- **A plain build.** `build.cmake` declares the game's libraries, tests and tools
  with `add_library`, `add_executable`, `target_*` and `add_test`. It links only
  its own targets and PlaySuite's (`vendor_game_ui`, `game_paths` and the like),
  and does not download, run commands, include other files or install anything.
- **The house style.** All of the game's C++, tests included, keeps the house
  style's mechanical rules: no `auto`, no `->`, no lambdas, no coroutines, no
  ranges or views pipelines, no defaulted comparisons and no `std::any`. Nothing
  waives these. `python3 scripts/check-style.py games/<id>` shows them.
- **Code that reads plainly.** Shipped code (everything outside `tests/`,
  `tools/` and `dev/`) avoids constructs a game has no ordinary use for:
  `reinterpret_cast`, `const_cast`, C-style pointer casts, `uintptr_t`, unions,
  `setjmp`, `extern "C"`, compiler attributes and builtins, `#pragma` other than
  `#pragma once`, `volatile`, `goto`, `alloca`, inline assembly, and platform,
  file or thread headers. If the game truly needs one, say why in the pull
  request; a maintainer reviews it.
- **What the code can reach.** The compiled game is read on every platform:
  everything it calls from outside itself must be computation, the C++ standard
  library apart from files and threads, GUI.Forms or PlaySuite, and it may contain
  no system-call instructions.

PlaySuite keeps the list of approved exceptions in `tools/game_approvals.json`,
outside every game folder. A contribution cannot change it. Review still reads
every submission; the checks make sure nothing unusual goes unnoticed.

## How to submit

1. Fork [falseywinchnet/games](https://github.com/falseywinchnet/games).
2. Build the game in one folder, `games/<id>/`. With an AI coding model, open the
   repository and say: "Read `new-games/AGENTS.md`. Build this game: …". Without
   one, follow the same file.
3. Run the submission check until it passes:

   ```sh
   python3 new-games/tools/check_game.py <id> --fetch-toolkit
   ```

4. Open a pull request that adds only `games/<id>/`. In its description, say
   whether the game is a classic or new (rule 2), how it is replayable (rule 4),
   and, if requirement 4 applies, what its passive mode does.

The pull request is checked (above), built and tested on all three systems. If it
is accepted and merged, the next release includes it.

By submitting, you agree that the game is distributed under the repository's
[licence](../LICENSE), and that the maintainer may change it.
