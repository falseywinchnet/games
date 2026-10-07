# Build the complete collection

Use CMake 3.25 or later, Ninja and a C++20 compiler. The repository's `Build installable PlaySuite` workflow is the executable build recipe for each released platform. GUI.Forms now has its own repository, `falseywinchnet/gui_forms` (split from `file_manager`'s `gui_forms/` subtree on 2026-10-07). The workflow checks it out at `2cb2fc53bb26ff6c6cb2a856a60b0a6b9c854558` into `gui_forms/`, fetches its pinned text dependencies, builds its CPU renderer where required, and prepares the game resources. This revision adds owner-held event handlers (`gf::on`), application-owned `Command` and `Value` state with control binding, and the collection controls the shell now uses; it requires a full rebuild of every consumer. Linux builds with the LLVM 22 series, because Ubuntu's Clang 18 rejects it.

GUI.Forms publishes a compiler cache from each tested main build as the prerelease `build-<revision>`. `tools/restore_toolkit_cache.py <platform>` downloads the one for the pin, checks it against the digest recorded there and the revision in its manifest, and unpacks it into `.ccache`. The workflow then builds in `.build/native/app` with `CCACHE_BASEDIR` set to the workspace, matching GUI.Forms' own layout so cached toolkit objects can match. A miss only compiles from source. Update the pin, the digests and the package revision in `tools/package_common.py` together.

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

On Windows, a MinGW64 GCC toolchain supplies CMake and Ninja. No Skia build is required. With absolute paths for the two resource directories:

```sh
cmake -S . -B build/app -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DGAMES_TOOLKIT_SOURCE_DIR=<absolute-gui_forms-checkout> \
  -DGAMES_RUNTIME_ASSET_DIR=<absolute-prepared-runtime-directory>
cmake --build build/app --parallel 2
ctest --test-dir build/app --output-on-failure --timeout 180
python3 scripts/check-style.py
```

On macOS, use Homebrew LLVM 20 with its matching libc++ and libunwind, including the Objective-C++ compiler used internally by GUI.Forms. On Linux, install Clang 22 (apt.llvm.org), GN, Ninja, X11/Xcursor and ATK development packages. Both platforms build the toolkit's pinned CPU Skia archive and pass `GUI_FORMS_SKIA_PREBUILT=ON` and `GUI_FORMS_SKIA_OUT` to the complete application configure. See the workflow for the exact compiler flags and renderer commands.

Packaging scripts stage into a new output directory and refuse to overwrite an existing application folder:

```sh
python tools/package_windows.py --build build/app --toolkit gui_forms --runtime-dir <mingw64-bin> --installer
python3 tools/package_macos.py --build build/app --toolkit gui_forms --llvm <llvm-20-prefix>
python3 tools/package_linux.py --build build/app --toolkit gui_forms
```

Windows installer creation requires NSIS. Linux packaging requires patchelf and Debian packaging tools; its DEB records the actual shared-library dependencies. The Windows and macOS scripts copy and validate the native dependency closure. All packages include game data, toolkit fonts, game fonts, dependency notices and a resource manifest.

`tools/smoke_package.py <packaged-executable>` creates temporary isolated saves, removes asset/library overrides, launches the real native window, checks a presented frame and closes through the UI scheduler. Linux CI runs it inside Xvfb. It has a 60-second timeout. This complements CTest and manual visual/gameplay checks.

To publish a release, push a `v<version>` tag matching `project(Games VERSION ...)` after reviewing the platform runs. The workflow publishes installers, archives and SHA-256 checksums only after all four native application jobs pass.

Author: Astra
Sponsor: Rainstar
