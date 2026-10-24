#!/bin/sh
# Configures and builds the browser PlaySuite (cmake/Web.cmake) with emsdk on
# PATH. Run from the repository root:
#   GAMES_TOOLKIT_SOURCE_DIR=<gui_forms> GAMES_RUNTIME_ASSET_DIR=<assets> tools/web/build.sh [target]
# The toolkit's third_party/skia, harfbuzz and freetype must be fetched, and
# Skia's wasm archive built with third_party/build_skia_cpu_wasm.sh.
set -eu
: "${GAMES_TOOLKIT_SOURCE_DIR:?set GAMES_TOOLKIT_SOURCE_DIR}"
: "${GAMES_RUNTIME_ASSET_DIR:?set GAMES_RUNTIME_ASSET_DIR}"
build=${GAMES_WEB_BUILD_DIR:-.build/web}
skia_out=${GAMES_WEB_SKIA_OUT:-"$GAMES_TOOLKIT_SOURCE_DIR/third_party/skia/out/gui_forms-wasm-cpu"}
if [ ! -f "$skia_out/libskia.wasm.a" ]; then
    (cd "$GAMES_TOOLKIT_SOURCE_DIR/third_party" && sh build_skia_cpu_wasm.sh)
fi
emcmake cmake -S . -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DGAMES_TOOLKIT_SOURCE_DIR="$GAMES_TOOLKIT_SOURCE_DIR" \
    -DGAMES_RUNTIME_ASSET_DIR="$GAMES_RUNTIME_ASSET_DIR" \
    -DGUI_FORMS_SKIA_PREBUILT=ON \
    -DGUI_FORMS_SKIA_OUT="$skia_out" \
    ${GAMES_WEB_CMAKE_ARGS:-}
cmake --build "$build" --target "${1:-games}" -j "${BUILD_JOBS:-10}"
