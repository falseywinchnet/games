# Deliver

A game is delivered as a pull request containing the game's folder, its sounds,
and the shelf edits. This guide is the last stretch: checking, the handoff
record, and what happens after you push.

## Before you call it done

**Play it in your head, then in pictures.** Walk through a whole session using
the preview tool: the first frame, a few moves, a mistake, the win, the loss,
help, every panel, a new game. Render each at 1100 x 760 and at 600 x 370, and
the busiest states at 600 x 320 and at scales 1.5 and 2. Open every image and
look at it. Things to look for:

- text that touches an edge, overlaps, or is too small to read;
- a board that became tiny while empty space surrounds it;
- anything cut off at the small sizes;
- a character or a banner covering something the player needs;
- colours that vanish against their background;
- a state that looks unfinished beside the others.

**Show the person.** Send them the pictures. Fix what they say. Repeat until
they say it is right. This loop is the main part of the work, and it is where
these games got good.

**Save the pictures you looked at** in `vendor/<id>/screens/`, named for what
they show and their size (`mid-1100x760.png`, `help-600x370.png`). At least one
must be at 600 x 370. Do not save a picture you did not look at. Then compress
them; the harness writes them uncompressed:

```sh
python3 new-games/tools/shrink_png.py vendor/<id>/screens
```

A very large picture may be scaled down by whatever shows it to you, and the
scaling can add banding or blur that is not in the file. Judge fine detail at
moderate sizes, or crop.

**Run the gate.**

```sh
python3 new-games/tools/check_game.py <id> --fetch-toolkit
```

Every FAIL is fixed. Every WARN is fixed or explained in `HANDOFF.md`.

**Run the repository's core profile once**, the way CI will:

```sh
cmake -S . -B .build/portable-core -DCMAKE_BUILD_TYPE=Release -DGAMES_BUILD_APPLICATION=OFF
cmake --build .build/portable-core --parallel 2
ctest --test-dir .build/portable-core --output-on-failure --timeout 120
python3 scripts/check-style.py
```

The first configure downloads one pinned dependency (QuickJS, for Sudoku). All
existing tests must still pass; the solver test alone takes a while.

## The handoff

`vendor/<id>/HANDOFF.md` is your report to the maintainer, who will build and
play the game without you. The template has the headings. The rule is simple:
**say what you did, and say what you did not.**

Under VERIFIED goes only what you ran or saw, with the command and its result.

Under NOT VERIFIED goes everything else that matters. On a fresh clone this
always includes:

- The application was not built. The view was syntax-checked only; the hosted
  contract test has not run.
- Nobody has played it in a window. Timing, feel, pointer behaviour and idle CPU
  in the application are unjudged.
- Sounds were checked by measurement, not by ear.
- Platforms other than the one you ran the headless tests on.

If the person did play a build (after CI, below), move the matching lines up to
VERIFIED and say who played what.

Under DECISIONS goes anything the maintainer might want to reverse: what you
borrowed and from where, a gate warning you accepted, a departure from these
guides, something the person asked for that you did differently and why, and
anything you would want from the toolkit.

Do not write that something works because it should. A report that says less and
is true is worth more to the maintainer than one that sounds finished. The
collection's handoffs have always had a NOT VERIFIED section with real entries
in it.

## Commit

Work on a branch named for the game.

```sh
git checkout -b game/<id>
git add vendor/<id> assets/audio/<prefix>* src cmake CMakeLists.txt tests tools README.md docs/GAME_CATALOG.md
git status        # read it: nothing you did not mean to add
git commit -m "Add <Title> to the shelf"
```

Check `git status` for strays: build folders, preview images outside `screens/`,
editor files. `.build/` is ignored already. Never commit a save file.

## Push when the person says so

Pushing publishes the work. Ask the person first, in so many words, and wait for
a yes. Then:

```sh
git push -u origin game/<id>
gh pr create --title "Add <Title> to the shelf" --body-file vendor/<id>/HANDOFF.md
```

A contributor without write access forks the repository first and opens the pull
request from the fork. The repository's [license](../../LICENSE) permits forks
for exactly this purpose and takes contributions as part of PlaySuite.

## After the push: the real build

Two workflows run on the pull request:

- **Native game core checks** builds every game's core and runs every rules test
  on Windows, macOS and two Linux architectures. Your `<id>_rules` test is among
  them.
- **Build installable PlaySuite** builds the whole application on the same four
  platforms, runs every application test (including your `<id>_view_contract`
  and the shell test that opens your game and clicks its Help), packages it, and
  attaches the packages to the run as artifacts named `games-<platform>`.

On a pull request from a fork, a maintainer may have to approve the workflows
before they run. When they finish:

1. If a job failed, read its log (`gh run view --log-failed`), fix the cause in
   your game, and push again. Do not weaken a test to pass. A failure in a
   different game or in the toolkit is not yours to fix; report it.
2. If they passed, download the artifact for the person's platform
   (`gh run download`) and tell them how to open it. Their game is on the shelf.
   Now they can play it properly.
3. Bring what they say back into the game: timing, sound levels, anything that
   feels wrong in the hand. Push the fixes. Update `HANDOFF.md`, moving what has
   now been played into VERIFIED.

The maintainer plays it too, decides whether it joins the shelf, and handles the
version, the release notes and the release.

## If it is turned down

Some good games will not suit this shelf. The reasons will be the ones in
[what belongs](01-what-belongs.md). The person's game still exists: their fork
builds it, and they can play it there. The license does not permit distributing
a modified PlaySuite, so a declined game stays a private build unless it is
reshaped and offered again.
