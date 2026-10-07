# Stillwater's own headless build: the ambient engine, this scene's look, its tests
# and the preview tool. The PlaySuite window is built by the repository's application.
#
#   cmake -S . -B build && cmake --build build -j 2 && ctest --test-dir build --output-on-failure
#   build/sw_preview assets/scene/riverscape.ambient shot.png 590 380
cmake_minimum_required(VERSION 3.25)
project(Stillwater VERSION 0.1.0 LANGUAGES C CXX)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/StandaloneGame.cmake")
