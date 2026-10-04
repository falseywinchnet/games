# Build the complete collection

Use CMake 3.25 or later, Ninja and a C++20 compiler. The repository's `Build installable PlaySuite` workflow is the executable build recipe for each released platform. It checks out GUI.Forms at `c3b9d10d0ed708da2527fcb2697b29b8b2cd9007`, fetches its pinned text dependencies, builds its CPU renderer where required, and prepares the game resources. This revision adds private Windows input scheduling and complete native repaint bounds to the `3f75e37` brush-cache backport. It retains the `7b260cf` public interface without later API changes. The unchanged Skia dependency archive retains its existing cache key.

The source integration uses public GUI.Forms APIs for native windows, input, presentation, text masks and audio. The selected development text and bar-transport APIs are source-only; an older installed SDK cannot replace this source dependency. Games does not install or export an SDK. Apple framework adaptation is confined to GUI.Forms; Games compiles its application and game adapters as C++.

Prepare resources once with Python, Pillow and FFmpeg:

```sh
python3 tools/prepare_portable_assets.py --output build/runtime --ffmpeg ffmpeg
python3 tools/verify_portable_assets.py --runtime build/runtime
```

Fetch the toolkit text dependencies:

```sh
sh toolkit/gui_forms/third_party/fetch_text_stack.sh
sh toolkit/gui_forms/third_party/fetch_linebreak.sh
```

On Windows, a MinGW64 GCC toolchain supplies CMake and Ninja. No Skia build is required. With absolute paths for the two resource directories:

```sh
cmake -S . -B build/app -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DGAMES_TOOLKIT_SOURCE_DIR=<absolute-toolkit-gui_forms-directory> \
  -DGAMES_RUNTIME_ASSET_DIR=<absolute-prepared-runtime-directory>
cmake --build build/app --parallel 2
ctest --test-dir build/app --output-on-failure --timeout 180
python3 scripts/check-style.py
```

On macOS, use Homebrew LLVM 20 with its matching libc++ and libunwind, including the Objective-C++ compiler used internally by GUI.Forms. On Linux, install Clang, GN, Ninja, X11/Xcursor and ATK development packages. Both platforms build the toolkit's pinned CPU Skia archive and pass `GUI_FORMS_SKIA_PREBUILT=ON` and `GUI_FORMS_SKIA_OUT` to the complete application configure. See the workflow for the exact compiler flags and renderer commands.

Packaging scripts stage into a new output directory and refuse to overwrite an existing application folder:

```sh
python tools/package_windows.py --build build/app --toolkit toolkit/gui_forms --runtime-dir <mingw64-bin> --installer
python3 tools/package_macos.py --build build/app --toolkit toolkit/gui_forms --llvm <llvm-20-prefix>
python3 tools/package_linux.py --build build/app --toolkit toolkit/gui_forms
```

Windows installer creation requires NSIS. Linux packaging requires patchelf and Debian packaging tools; its DEB records the actual shared-library dependencies. The Windows and macOS scripts copy and validate the native dependency closure. All packages include game data, toolkit fonts, game fonts, dependency notices and a resource manifest.

`tools/smoke_package.py <packaged-executable>` creates temporary isolated saves, removes asset/library overrides, launches the real native window, checks a presented frame and closes through the UI scheduler. Linux CI runs it inside Xvfb. It has a 60-second timeout. This complements CTest and manual visual/gameplay checks.

To publish a release, push a `v<version>` tag matching `project(Games VERSION ...)` after reviewing the platform runs. The workflow publishes installers, archives and SHA-256 checksums only after all four native application jobs pass.

Author: Astra
Sponsor: Rainstar
