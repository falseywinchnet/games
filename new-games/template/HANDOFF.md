# Template Game: handoff

Written by the model that built the game, for the maintainer who will build and
release it. State only what you did. A check you did not run goes under NOT
VERIFIED; that section is expected to have entries.

- Game: Template Game (`templategame`, namespace `tg`, view `tg::TemplateView`)
- Save: `template_game-v1.txt`
- Built by: TODO (model and version), with TODO (the person whose game this is)
- Date: TODO
- Proposed release-note sentence: TODO

## VERIFIED

TODO: each line names the command or the act, and its result. For example:
- `python3 new-games/tools/check_game.py templategame --fetch-toolkit`: no failures, 0 warnings.
- Rules tests: N checks pass in S seconds (`ctest --test-dir .build/new-games/templategame`).
- Looked at previews at 1100 x 760, 600 x 370 and 600 x 320, at scales 1, 1.5 and 2: listed in `screens/`.
- Portable-core profile of the whole repository: built and passed, or not run.

## NOT VERIFIED

TODO: always includes, unless you really did them:
- The PlaySuite application was not built here. The view was syntax-checked against the
  pinned GUI.Forms headers only; the hosted contract test has not run.
- Nobody has played it in the window. Timing, feel and sound levels are unjudged.
- Sounds were checked by measurement, not by listening.
- Windows, Linux and macOS behaviour beyond this machine's headless tests.

## DECISIONS

TODO: choices the maintainer should know about or might reverse: anything borrowed
and from where, any warning from the gate you chose to accept and why, any place
the game departs from the guide, and anything the person asked for that you did
differently.

## FILES OUTSIDE THIS FOLDER

None are required. `GAME.json`, `module.cpp`, `cover.cpp`, `help.md`, `build.cmake`,
and `assets/` contain the integration. CMake discovers the folder and generates
the registry in its build directory. Record any intentional exception here.
