#!/bin/sh
# tools/web/build.sh with the Mini's PlaysuiteDependencies layout (m4web runs it there).
# A mirror other than playsuite-games (a parallel worker's) reuses the main
# mirror's fetched dependencies and asset pack.
set -eu
deps="$HOME/Developer/PlaysuiteDependencies"
main="$deps/web/playsuite-games/.build/web"
export GAMES_TOOLKIT_SOURCE_DIR="${GAMES_TOOLKIT_SOURCE_DIR:-$deps/web/gui_forms}"
export GAMES_RUNTIME_ASSET_DIR="${GAMES_RUNTIME_ASSET_DIR:-$deps/runtime-modular}"
shared=""
if [ "${PLAYSUITE_WEB_NAME:-playsuite-games}" != playsuite-games ]; then
    shared="$shared -DFETCHCONTENT_SOURCE_DIR_QUICKJS=$main/_deps/quickjs-src"
    shared="$shared -DFETCHCONTENT_SOURCE_DIR_GUI_FORMS_MINIAUDIO=$main/_deps/gui_forms_miniaudio-src"
    shared="$shared -DGAMES_WEB_SHARED_ASSETS=$main/web"
fi
export GAMES_WEB_CMAKE_ARGS="$shared ${GAMES_WEB_CMAKE_ARGS:-}"
exec sh tools/web/build.sh "$@"
