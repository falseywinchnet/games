# Canopy Falls' own headless build: the particle and ambient engines, the scene, its
# tests and its tools. The PlaySuite window is built by the application.
#
#   cmake -S . -B build && cmake --build build -j 2 && ctest --test-dir build --output-on-failure
#   build/cf_live_preview out 550 380
cmake_minimum_required(VERSION 3.25)
project(CanopyFalls VERSION 0.1.0 LANGUAGES C CXX)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/StandaloneGame.cmake")
