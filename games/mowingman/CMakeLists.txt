# Mowing's own headless build: the ambient and coverage engines, the garden,
# its tests and the preview tool. The PlaySuite window is built by the application.
#
#   cmake -S . -B build && cmake --build build -j 2 && ctest --test-dir build --output-on-failure
#   build/mm_preview shot.png 1100 700 120
cmake_minimum_required(VERSION 3.25)
project(MowingMan VERSION 0.1.0 LANGUAGES C CXX)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/StandaloneGame.cmake")
