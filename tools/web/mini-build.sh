#!/bin/sh
# tools/web/build.sh with the Mini's PlaysuiteDependencies layout (m4web runs it there).
set -eu
deps="$HOME/Developer/PlaysuiteDependencies"
export GAMES_TOOLKIT_SOURCE_DIR="${GAMES_TOOLKIT_SOURCE_DIR:-$deps/web/gui_forms}"
export GAMES_RUNTIME_ASSET_DIR="${GAMES_RUNTIME_ASSET_DIR:-$deps/runtime-modular}"
exec sh tools/web/build.sh "$@"
