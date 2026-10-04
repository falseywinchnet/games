# On the shelf

Putting a game on the shelf is a fixed set of edits to the shell. One command
makes them:

```sh
python3 new-games/tools/wire_shelf.py <id> --dry-run   # see the edits
python3 new-games/tools/wire_shelf.py <id>             # make them
```

It reads `vendor/<id>/GAME.json`. Run it again whenever you add a source file to
`GAME.json` or a sound to `assets/audio/`; finished edits are skipped and the
build lists and audio counts are brought up to date.

Three things remain for you:

1. **Draw the emblem.** In `src/suite.cpp`, find `TODO(<id>)` in
   `paint_entry_emblem` and replace the placeholder. See
   [the shell](04-the-shell.md) for what an emblem is.
2. **Add the game where the documents list the games**: the opening sentence of
   `README.md`, the opening of `docs/GAME_CATALOG.md`, and a paragraph for it
   under "Implemented rule details" in the catalog (the rules, how boards are
   generated, what is saved). Those lists can lag behind the shelf; if the count
   you find is already wrong, correct it while you are there and say so in
   `HANDOFF.md`.
3. **Leave the version and `packaging/RELEASE_NOTES.md` alone.** Releases are the
   maintainer's. Put a proposed release-note sentence in your `HANDOFF.md`.

Then run the gate with `--fetch-toolkit`. It syntax-checks `src/collection.cpp`,
`src/suite.cpp` and `src/shelf.cpp` with your game wired in.

## What the edits are

If the tool stops because the shell has changed and an anchor is gone, make that
edit by hand. This is the full list, with the reason for each. The commit that
added Rock Stack (`git log --oneline -- src/suite.hpp`) is a worked example.

### `src/suite.hpp`

Append the game to `enum class Entry` and add one to `entry_count`.

**Append only.** `Entry` values are written into players' saved state. An entry
that moves makes every existing player's shelf open the wrong box. If two games
are being added at once, the maintainer settles the order when merging.

The shell remembers which boxes have been opened in a 32-bit mask, so the shelf
holds at most 31 entries until that is widened. The tool refuses past that.

### `src/suite.cpp`

- Add a row to `entries`: title, kind, blurb, cover top, cover bottom, accent.
  The row's position must match the enum.
- Add `case Entry::<id>:` to the switch in `paint_entry_emblem`.

### `src/collection.hpp`

- Include the view's header.
- Add `std::shared_ptr<ns::View> <id>_;` to `Collection`.

### `src/collection.cpp`

Seven places. Search for the newest game's entry to find each.

| Function | Edit | Why |
|---|---|---|
| `ensure_view` | `else if (entry == Entry::<id>) <id>_ = gf::make_control<ns::View>(gf::StableId("<id>.view"), ns::Options{.hosted = true});` | Created on first visit. The code after the chain adds it behind the shelf and capsule; leave that as it is. |
| `uses_rail` | add `entry == Entry::<id>` | The game presents a live surface, so the capsule gets a rail |
| `view` | `case Entry::<id>: return <id>_;` | |
| `source` | `if (entry == Entry::<id>) return <id>_.get();` | Its commands reach the capsule |
| `arrange` | add `child == <id>_` to `railed` | Laid out below the rail |
| `preferences` | `if (<id>_) (*<id>_).set_cabinet(!shelf_open_ && active_ == Entry::<id>, cabinet.music, cabinet.sound, cabinet.reduced);` | Foreground state and the master switches |
| `activate` | a branch that calls `music_play("", false);` then `(*<id>_).activate();` | Stops the shell's music and gives the game the keyboard |

### `src/shelf.cpp`

The sign under the name counts the games in words ("EIGHTEEN GAMES"). Update the
word. If the game changes what the collection is, the rest of that line is the
maintainer's to rewrite.

### `CMakeLists.txt`

Before `if(GAMES_BUILD_APPLICATION)`, a marked block:

```cmake
# >>> new-games: <id>
add_library(<ns>_core STATIC vendor/<id>/src/platform/raster.cpp vendor/<id>/src/rules.cpp ...)
target_include_directories(<ns>_core PUBLIC vendor/<id>/src)
target_link_libraries(<ns>_core PUBLIC game_paths)
target_compile_definitions(<ns>_core PRIVATE _USE_MATH_DEFINES)
add_executable(<id>_rules_tests vendor/<id>/tests/rules_tests.cpp)
target_link_libraries(<id>_rules_tests PRIVATE <ns>_core)
add_test(NAME <id>_rules COMMAND <id>_rules_tests)
# <<< new-games: <id>
```

This is what the portable-core CI builds and tests on four platforms.

### `cmake/Application.cmake`

At the end, a marked block:

```cmake
# >>> new-games: <id>
target_sources(game_audio_adapters PRIVATE vendor/<id>/src/platform/audio.cpp)
target_sources(vendor_game_ui PRIVATE vendor/<id>/src/<id>_view.cpp vendor/<id>/src/scene.cpp vendor/<id>/src/platform/text.cpp)
target_link_libraries(vendor_game_ui PUBLIC <ns>_core)
add_executable(<id>_contract_tests vendor/<id>/tests/view_contract_tests.cpp)
target_include_directories(<id>_contract_tests PRIVATE new-games/kit)
target_link_libraries(<id>_contract_tests PRIVATE vendor_game_ui)
add_test(NAME <id>_view_contract COMMAND <id>_contract_tests)
set_tests_properties(<id>_view_contract PROPERTIES TIMEOUT 90
    ENVIRONMENT "GAMES_ASSET_DIR=${GAMES_RUNTIME_ASSET_DIR}")
# <<< new-games: <id>
```

The `dev/` folder and `tools/preview.cpp` are never listed here. They are the
headless harness and do not ship.

### `tests/collection_ui_tests.cpp`

Add `Entry::<id>` to the loop that opens each hosted game, opens the capsule and
clicks its Help command. Your `help` command must then report `checked`.

### Audio inventory

If the game adds sound files, three places state the exact count of source
sounds in `assets/audio/`, and all three must match:

- `tools/prepare_portable_assets.py` (the check and its message),
- `tools/verify_portable_assets.py` (two comparisons),
- `tests/audio_tests.cpp` (the check and its message).

If the game has looping music, add its manifest's file name to the tuple in
`loop_bounds` in `tools/prepare_portable_assets.py`.

## What this does not touch

- No other game's code.
- No test is removed or loosened.
- The toolkit.
- `incoming/`. That folder holds games delivered from another machine for
  porting. A kit game is portable from its first line and has no package there.
