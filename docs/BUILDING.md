# Build the complete collection

Use CMake 3.25 or later, Ninja and LLVM 22.1.8. The repository's `Build installable PlaySuite` workflow is the executable build recipe for each released platform. GUI.Forms has its own repository, `falseywinchnet/gui_forms` (split from `file_manager`'s `gui_forms/` subtree on 2026-10-07). The workflow checks it out at `5bb7a8ba74ccb358f11d1b9056831e0130bdd8d0` into `gui_forms/` (on Windows it first restores the MSYS2 CLANG64 tree GUI.Forms published with that revision's cache, to `D:\a\_temp\msys64`, so no job depends on the MSYS2 mirrors and the compiler matches the cache byte for byte), fetches its pinned text dependencies, builds its CPU renderer where required, and prepares the game resources. This revision adds owner-held event handlers (`gf::on`), application-owned `Command` and `Value` state with control binding, the collection controls the shell uses, the `AtomicThreadPool`/`Worker` threading utilities (the shared `GUIForms::Threading` library, which `GUIForms::Audio` and `GUIForms::Application` both carry: never also link static Core), a view-bound display link on macOS, and stx_vorbis, GUI.Forms' own Vorbis decoder with bounded BFFT synthesis, which replaces stb_vorbis (`GUI_FORMS_AUDIO_VORBIS_BACKEND=stx`, forced in our CMake). Audio loads are cancelled with `gui_forms::CancellationFlag`. It requires a full rebuild of every consumer. It also presents a live surface under floating controls without repainting the window around them on every frame, copies only the damage a live surface publishes (`LiveSurfaceWriteLease::publish(damage)` promises every pixel outside it equals the previous publication), and keeps shaped text and blurred shadows with each control's recorded paint.

Every platform follows GUI.Forms' LLVM 22 contract (`gui_forms/docs/COMPILER_CACHE.md`): one compiler, LLVM 22.1.8, named `clang`/`clang++` (`CC`, `CXX` and, on macOS, `OBJCXX`), and `-DCMAKE_TOOLCHAIN_FILE=<workspace>/gui_forms/cmake/llvm22.cmake`.

| Platform | Compiler | Minimum |
|---|---|---|
| macOS | Homebrew `llvm@22` | macOS 14.0, Apple silicon only |
| Linux x64 and arm64 | apt.llvm.org `llvm-toolchain-noble-22` | Ubuntu 24.04 |
| Windows | MSYS2 CLANG64 clang 22.1.8 (with winpthreads) | Windows 10 |

On macOS the compiler is Homebrew's, but the C++ runtime is not: Homebrew's libc++ requires macOS 26. GUI.Forms builds libc++, libc++abi and libunwind from pinned LLVM 22.1.8 source for macOS 14 (`gui_forms/tools/build_macos_runtimes.py`); set `GUI_FORMS_LLVM_RUNTIME` to that folder. The toolchain file adds its headers, libraries and the 14.0 minimum, and `tools/package_macos.py --runtime "$GUI_FORMS_LLVM_RUNTIME"` ships those three libraries (about 1.6 MB) with their notices and audits the app's minimum.

GUI.Forms publishes a compiler cache from each tested main build as the prerelease `build-<revision>`, for macOS arm64, Linux x64, Linux arm64 and Windows x64; the macOS archive also carries the runtime. `tools/restore_toolkit_cache.py <platform>` downloads the one for the pin, checks it against the digest recorded there and the revision in its manifest, and unpacks it; on macOS it then verifies the runtime against the local compiler and SDK and rebuilds it from source if they differ. The workflow builds in `.build/native/app` with `CCACHE_BASEDIR` at the workspace, matching GUI.Forms' layout so cached toolkit objects match. A miss only compiles from source. Update the pin, the digests and the package revision in `tools/package_common.py` together.

The source integration uses public GUI.Forms APIs for native windows, input, presentation, text masks and audio. The selected development text and bar-transport APIs are source-only; an older installed SDK cannot replace this source dependency. Games does not install or export an SDK. Apple framework adaptation is confined to GUI.Forms; Games compiles its application and game adapters as C++.

Prepare resources once with Python, Pillow and FFmpeg:

```sh
python3 tools/prepare_portable_assets.py --output build/runtime --ffmpeg ffmpeg
python3 tools/verify_portable_assets.py --runtime build/runtime
```

Fetch the toolkit text dependencies:

```sh
sh gui_forms/third_party/fetch_text_stack.sh
sh gui_forms/third_party/fetch_linebreak.sh
```

On Windows, MSYS2 CLANG64 supplies the compiler, CMake and Ninja. No Skia build is required. With absolute paths for the two resource directories:

```sh
cmake -S . -B build/app -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DGAMES_TOOLKIT_SOURCE_DIR=<absolute-gui_forms-checkout> \
  -DGAMES_RUNTIME_ASSET_DIR=<absolute-prepared-runtime-directory>
cmake --build build/app --parallel 2
ctest --test-dir build/app --output-on-failure --timeout 180
python3 scripts/check-style.py
```

On Linux, also install GN, Ninja, X11/Xcursor and ATK development packages. macOS and Linux build the toolkit's pinned CPU Skia archive (`gui_forms/third_party/build_skia_cpu.sh` or `build_skia_cpu_linux.sh`, which apply the same compiler, runtime and minimum) and pass `GUI_FORMS_SKIA_PREBUILT=ON` and `GUI_FORMS_SKIA_OUT` to the complete application configure. See the workflow for the exact commands.

Packaging scripts stage into a new output directory and refuse to overwrite an existing application folder:

```sh
python tools/package_windows.py --build build/app --toolkit gui_forms --runtime-dir <mingw64-bin> --installer
python3 tools/package_macos.py --build build/app --toolkit gui_forms --runtime <macos-14-llvm-runtime>
python3 tools/package_linux.py --build build/app --toolkit gui_forms
```

Windows installer creation requires NSIS. Linux packaging requires patchelf and Debian packaging tools; its DEB records the actual shared-library dependencies. The Windows and macOS scripts copy and validate the native dependency closure. All packages include game data, toolkit fonts, game fonts, dependency notices and a resource manifest.

`tools/smoke_package.py <packaged-executable>` creates temporary isolated saves, removes asset/library overrides, launches the real native window, checks a presented frame and closes through the UI scheduler. Linux CI runs it inside Xvfb. It has a 60-second timeout. This complements CTest and manual visual/gameplay checks.

To publish a release, push a `v<version>` tag matching `project(Games VERSION ...)` after reviewing the platform runs. The workflow publishes installers, archives and SHA-256 checksums only after all four native application jobs pass.

Author: Astra
Sponsor: Rainstar

## Test reuse in CI

The application and game-core workflows do not rerun a test whose inputs are unchanged
since it last passed (`tools/test_reuse.py`). Each test's key covers its platform and
profile, command line and properties, the compiler, and the git tree hashes of what it
depends on: a game's tests its own folder, the engines its `GAME.json` lists and any
`shared/` folder its build files name; an engine's tests that engine; the shell's tests
every game and engine; all of them the build files, `src/`, `tests/`, the kit, `assets/`
and the pinning and asset tools. A one-game change therefore runs that game's tests and
the shell's, nothing else.

Every release carries the results beside its packages, one small file per profile and
platform (`test-results-app-<platform>.json`, `test-results-core-<platform>.json`), so
they never expire. A run starts from the newest release's file and merges in the
results of the same workflow's last run for the same pull request; on main, the pull
request just merged. A re-pushed pull request, and the merge itself, therefore repeat
no test whose inputs are unchanged. When the git trees, the CTest listing or the
results cannot be read, every test runs.

What runs for a change:

- **Pull request:** the application jobs (all four platforms) and the game-core checks
  run the tests whose inputs changed. The text and audio workflows run only when their
  own code, tests, the four legacy games they exercise or the GUI.Forms pin change;
  assets and new games never trigger them (the application jobs already decode every
  prepared asset).
- **Main, after the merge:** the application jobs build, package and publish; their tests
  are reused from the pull request. The game-core checks do not run on main: the release
  takes their results from the merged pull request's run. The text and audio workflows,
  when their paths changed, skip entirely if the pull request's run passed on the same
  tree (`tools/test_reuse.py same-tree`).

Adding a game therefore runs, in its pull request, that game's tests and the shell's;
after the merge, only the packaging.
