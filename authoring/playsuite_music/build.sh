#!/bin/sh
# Builds the PlaySuite music renderer (single g++ process; the host is shared).
# Usage: sh build.sh [output-exe]   (default ../../.build/music-v2/bin/playsuite_music.exe)
set -e
here="$(cd "$(dirname "$0")" && pwd)"
out="${1:-$here/../../.build/music-v2/bin/playsuite_music.exe}"
mkdir -p "$(dirname "$out")"
${CXX:-g++} -std=c++20 -O2 -Wall -Wextra -Wno-misleading-indentation -static -o "$out" "$here"/src/*.cpp
echo "built $out"
