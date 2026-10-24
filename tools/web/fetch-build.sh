#!/bin/zsh
# Copies this checkout's Mini build (tools/web/m4web) back into web-out/. The
# asset pack is not copied: it is cloned (APFS, no extra space) from the main
# web-port checkout's web-out, or GAMES_WEB_ASSET_SOURCE.
set -euo pipefail
games=${0:A:h:h:h}
name=${PLAYSUITE_WEB_NAME:-playsuite-games}
host="$($HOME/.local/bin/m4host)"
source_dir=${GAMES_WEB_ASSET_SOURCE:-$HOME/Developer/playsuite-games-web/web-out}
mkdir -p "$games/web-out"
rsync -az --exclude='assets.*' "$host:Developer/PlaysuiteDependencies/web/$name/.build/web/web/" "$games/web-out/"
for pack in assets.data assets.js; do
    [[ -e "$games/web-out/$pack" ]] || cp -c "$source_dir/$pack" "$games/web-out/$pack"
done
print "$games/web-out"
