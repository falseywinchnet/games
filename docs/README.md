# docs

The living documents of the collection. Records of the 2026 port and the 0.4
integrations were retired on 2026-10-11; git history keeps them.

| Document | What it is |
|---|---|
| [BUILDING.md](BUILDING.md) | How the complete collection is built, tested, packaged and released on each platform; the GUI.Forms pin and its LLVM 22 contract; test reuse in CI. |
| [PERFORMANCE.md](PERFORMANCE.md) | The rule that a settled view does no work, how the shell and the games keep it, and how to measure idle and animating cost in the application. |
| [RENDER_ENGINE.md](RENDER_ENGINE.md) | The plan behind `shared/render`: do less work, not the same work faster; what was taken from other engines; the order the games moved over. |
| [PORTABLE_TEXT_REQUIREMENTS.md](PORTABLE_TEXT_REQUIREMENTS.md) | The contract the games' text masks hold the toolkit's text service to, and the font fixtures. |
| [TOOLKIT_REQUESTS.md](TOOLKIT_REQUESTS.md) | What PlaySuite has asked of GUI.Forms, found by using it, each with the local stand-in it would replace. |
| [ACCESSIBILITY_TOOLKIT_REQUEST.md](ACCESSIBILITY_TOOLKIT_REQUEST.md) | The request for spoken, keyboard-complete play for blind players: what belongs in the toolkit and what stays here. |

Everything about making a game is in [`new-games/`](../new-games/README.md) and its
[guides](../new-games/guide/README.md); everything about an engine is in its README
under [`shared/`](../shared). The root [AGENTS.md](../AGENTS.md) is the working
agreement for anyone, person or model, changing the collection.
