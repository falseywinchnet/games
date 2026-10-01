# Portable audio development build

The audio consumer tests build independently of the graphical application. They decode the complete game audio corpus, exercise ordinary playback policy, and check Four Pegs score changes against the shared sample clock. The source profile also runs the toolkit's service and loop-transport tests when `GUI_FORMS_BUILD_TESTS=ON`. It does not install or export the development transport as an SDK.

The repository workflow pins GUI.Forms source to `c4e4d917f7f759d5dba4863ff5eae427cf7c201d` from `falseywinchnet/file_manager`. It prepares the runtime data once and uses the same archive on Windows x64, macOS arm64, Linux x64 and Linux arm64.

## Compiler requirements

The audio cancellation API requires C++20 `std::stop_token` and `std::stop_source`. Merely accepting `-std=c++20` does not establish library support. Configuration compiles and links a cancellation probe and fails with a specific diagnostic if the selected library lacks it.

The default Apple compiler/library on the macOS 15 runner did not provide these types. The audio job uses Homebrew LLVM 20 with its matching libc++ headers and runtime. LLVM documents this feature as complete in libc++ 20; earlier implementations required experimental-library flags. See the [libc++ C++20 status](https://libcxx.llvm.org/Status/Cxx20.html) and [Homebrew LLVM 20 formula](https://formulae.brew.sh/formula/llvm@20).

For a fresh macOS audio build directory:

```sh
brew install ninja llvm@20
llvm_prefix="$(brew --prefix llvm@20)"
export CXX="$llvm_prefix/bin/clang++"
export CXXFLAGS="-nostdinc++ -isystem $llvm_prefix/include/c++/v1"
export LDFLAGS="-L$llvm_prefix/lib/c++ -Wl,-rpath,$llvm_prefix/lib/c++ -L$llvm_prefix/lib/unwind -Wl,-rpath,$llvm_prefix/lib/unwind -lunwind"
cmake -S tests/portable_audio -B build/audio -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DGUI_FORMS_BUILD_TESTS=ON \
  -DGAMES_AUDIO_SOURCE_DIR="$PWD/toolkit/gui_forms" \
  -DGAMES_AUDIO_ASSET_DIR="$PWD/build/runtime"
cmake --build build/audio --parallel 2
ctest --test-dir build/audio --output-on-failure --timeout 120
```

`toolkit/gui_forms` must contain the pinned source, and `build/runtime` must contain the prepared assets. These paths are examples relative to the checkout, not installed dependencies.

These tests render audio offline. They do not establish that a hosted runner produced audible sound. The explicit `audio_device_smoke` executable is available for local listening. A future macOS application package must include and validate any non-system compiler runtime dependencies, with their license notices, alongside the toolkit and game data.
