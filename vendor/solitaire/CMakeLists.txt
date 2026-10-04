cmake_minimum_required(VERSION 3.25)
project(solitaire VERSION 0.1.0 LANGUAGES C CXX)
set(GAME_SHARED_ENGINE cards)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/StandaloneGame.cmake")
