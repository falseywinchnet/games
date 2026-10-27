# The guides

Read them in this order the first time; afterwards each stands alone. The procedure
that uses them is [`new-games/AGENTS.md`](../AGENTS.md); the rules a game must meet are
in [`new-games/README.md`](../README.md).

| | Guide | What it settles |
|---|---|---|
| 1 | [Anatomy of a game](01-anatomy.md) | The folder, the three layers (rules, platform, view), standalone builds, saves, and the tests every game carries. |
| 2 | [The shell](02-the-shell.md) | What the shelf and the command capsule give a game and expect back: the box, commands, settings, help, scores, the rail and the surface, keyboard, pointer, accessibility. |
| 3 | [Primitives](03-primitives.md) | What a game can draw, write and play with: the canvas, text, audio, saves, randomness, the shared engines, the headless harness, what to borrow and what GUI.Forms provides. |
| 4 | [Controls](04-controls.md) | The canon GUI.Forms controls and the concise way to write them: `gf::on`, bound `Command` and `Value` state, item-reporting groups, the shell's skins. |
| 5 | [Shared engines](05-shared-engines.md) | When machinery becomes an engine, how one is laid out, and the index of every reusable system: renderer, scenes, materials, brains, music. |
| 6 | [Performance](06-performance.md) | Idle means idle, hidden means stopped, start fast, draw simply then cheaply, hot paths, threads, memory, measuring in the application. |
| 7 | [House style](07-house-style.md) | The C++ every file is written in, what the checker enforces, and what it cannot see. |

Each engine's README under [`shared/`](../../shared) is the guide to that engine.
