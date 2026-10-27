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

1. **Fork** [falseywinchnet/games](https://github.com/falseywinchnet/games) and
   clone your fork.
2. **Start from the template.** `python3 new-games/tools/new_game.py --id <id>
   --namespace <ns> --title "<Title>"` creates `games/<id>/` as a complete,
   building, tested game to replace piece by piece. With an AI coding model, open
   the repository and say: "Read `new-games/AGENTS.md`. Build this game: …".
   Without one, follow the same file.
3. **Run the submission check until it passes:**

   ```sh
   python3 new-games/tools/check_game.py <id> --fetch-toolkit
   ```

   It runs the same checks as the pull request, apart from the two that need the
   pull request itself: that only `games/<id>/` changed, and the 16 MB limit on
   what ships. The table below says how to fix what it reports.
4. **Commit only `games/<id>/`** and push it to your fork.
5. **Open a pull request** against `main`. The template asks what the reviewer
   needs: whether the game is a classic or new (rule 2), how it is replayable
   (rule 4), what its passive mode does if requirement 4 applies, and why, if the
   game needs a construct the checks flag for review.

### After you open the pull request

- **A maintainer starts the checks.** GitHub runs a first-time contributor's
  workflows only after a maintainer approves them, so the checks may show as
  waiting at first.
- **The checks** are the *Build installable PlaySuite* workflow. Its *prepare* job
  runs *Check contributed games* (one folder, size, build, house style, plain code)
  and *Check each game's shipped size*. Each platform's *application* job runs
  *Check games' shipped code* (what the compiled game can reach), then builds,
  tests, packages and launches PlaySuite with your game. Open a failed step on the
  pull request's *Checks* tab to read exactly what it found; each line names the
  file and line.
- **Push fixes to the same branch.** The checks run again on every push.
- **Review** reads the game against the rules. Anything the checks flagged for
  review is decided there; the reasons come with every decision.
- **When it is merged**, the next release includes it.

### Check failures and how to fix them

| The check says | Fix |
|---|---|
| `breaks the house style (explicit-type)` | Write the type instead of `auto`. |
| `breaks the house style (arrow-or-trailing-return)` | Write `(*p).member` instead of `p->member`; put return types first. |
| `breaks the house style (lambda-review)` | Make the lambda a named function, method or functor. |
| `breaks the house style (coroutine / ranges-or-views / defaulted-comparison / std::any)` | Write the loop, the comparison or the explicit type out. |
| `uses reinterpret_cast` (or another construct) `, which needs a maintainer's review` | Find another way; if there is none, say why in the pull request. |
| `uses platform or file/thread headers` | Use PlaySuite's API: `games::save_game_data`, `games::load_game_data`, `games::load_game_asset`, `gui_forms::Worker`. |
| `needs the 'files' capability` | Save and load through `src/game_data.hpp`, as the template does. |
| `needs the 'threads' capability` | Run background work on `gui_forms::Worker` instead of `std::thread`. |
| `needs the 'environment' capability` | Remove `getenv`; PlaySuite already places saves and assets. |
| `calls …, which no game capability covers` | The game calls something outside computation, GUI.Forms and PlaySuite; remove it. |
| `contains a system-call instruction` | Remove the assembly or intrinsic that produced it. |
| `MB of source exceeds the 64 MB limit` | Keep fewer or smaller source files; generated files need not be committed if a script in the folder makes them. |
| `MB prepared exceeds the 16 MB limit` | Shorten or compress audio and images. |
| `prebuilt binaries are not allowed` | Commit source only; remove libraries, executables and object files. |
| `<call>() is not allowed in a game's build` or `links …, which a game may not link` | Keep `build.cmake` to the template's form. |
| `changes only its own games/<id>/ folder` | Remove changes outside your game's folder from the pull request. |

By submitting, you agree that the game is distributed under the repository's
[licence](../LICENSE), and that the maintainer may change it.
