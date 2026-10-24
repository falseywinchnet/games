# PlaySuite in the browser

The whole application (shelf, every game, GUI.Forms, Skia CPU, HarfBuzz/FreeType,
miniaudio, stx_vorbis) builds with Emscripten into one `games.wasm` through
GUI.Forms' web host (`gui_forms/src/host/web/`). The page is `web/`; the build is
`cmake/Web.cmake`.

## Build and run

- On the Mini: `tools/web/m4web -- sh tools/web/mini-build.sh` (emsdk 6.0.13 at
  `~/Developer/PlaysuiteDependencies/emsdk`). Pass
  `GAMES_WEB_CMAKE_ARGS=-DGAMES_WEB_DEBUG=ON` for function names and assertions.
- Copy `.build/web/web/` back into `web-out/` and serve it with
  `python3 tools/web/serve.py web-out`; the page needs its COOP/COEP headers.
- The page is silent unless opened with `?sound`. Test without it.
- Test programs link for Node: `node .build/web/<test>.js ...` (main runs on a worker).

## Status

| Area | State |
|---|---|
| GUI.Forms web host, Skia wasm, text | Running: the shelf renders and takes input |
| Audio decode (stx_vorbis) | `audio_tests` passes under Node, 485/485 files |
| Audio output | miniaudio ScriptProcessor (main thread); a non-blocking AudioWorklet start is still to do |
| Saves | IndexedDB at /state, synchronised every 4 s and on page hide |
| Assets | One 156 MB preload; per-game packs are still to do |
| Four Pegs | Runs |
| Every other game | Builds; not yet opened in a browser |

## Known web differences

- stx_vorbis' stb adapter allocates from the heap on wasm32: Emscripten's libc++
  `unsynchronized_pool_resource` faults in `release()` there (reproduced by replaying
  a recorded allocation sequence). `Limits` still caps decoder memory.
- miniaudio's AudioWorklet path blocks in `emscripten_sleep` and needs Asyncify,
  which wasm exceptions exclude.
- Native dialogs and window resizing are unavailable to a page.
