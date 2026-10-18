# The game's own build: its rules, its tests and its preview tool, with nothing but
# a C++20 compiler. It does not build the PlaySuite window; that happens in the
# repository's application build, where cmake/Application.cmake compiles the view.
#
#   cmake -S . -B build && cmake --build build -j 2 && ctest --test-dir build --output-on-failure
#   build/sc_preview shot.png 600 370
cmake_minimum_required(VERSION 3.25)
project(SpecimenCabinet VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# The kit lives in the repository's new-games folder. In the template that is two
# levels up; in games/<game> it is ../../new-games/kit.
set(SC_KIT_DIR "" CACHE PATH "The repository's new-games/kit directory")
if(NOT SC_KIT_DIR)
  foreach(candidate "${CMAKE_CURRENT_SOURCE_DIR}/../kit" "${CMAKE_CURRENT_SOURCE_DIR}/../../new-games/kit")
    if(EXISTS "${candidate}/headless_text.hpp")
      get_filename_component(SC_KIT_DIR "${candidate}" ABSOLUTE)
      break()
    endif()
  endforeach()
endif()
if(NOT EXISTS "${SC_KIT_DIR}/headless_text.hpp")
  message(FATAL_ERROR "Set SC_KIT_DIR to the repository's new-games/kit directory")
endif()
get_filename_component(SC_FONT_DIR "${SC_KIT_DIR}/../../assets/fonts" ABSOLUTE)

# PlaySuite's game API for saves and assets (src/game_data.hpp), which a game uses
# instead of opening files itself.
get_filename_component(SC_PLAYSUITE_SRC "${SC_KIT_DIR}/../../src" ABSOLUTE)
if(NOT TARGET game_paths)
  add_library(game_paths STATIC "${SC_PLAYSUITE_SRC}/runtime_paths.cpp" "${SC_PLAYSUITE_SRC}/game_data.cpp")
  target_include_directories(game_paths PUBLIC "${SC_PLAYSUITE_SRC}")
endif()

# Pure game: rules, save, layout. This is what PlaySuite's portable-core profile builds.
add_library(sc_core STATIC src/platform/raster.cpp src/rules.cpp src/save.cpp src/stage.cpp)
target_include_directories(sc_core PUBLIC src)
target_link_libraries(sc_core PUBLIC game_paths)
target_compile_definitions(sc_core PRIVATE _USE_MATH_DEFINES)
if(NOT MSVC)
  target_compile_options(sc_core PRIVATE -Wall -Wextra)
endif()

# The scene with headless text and silent audio, for previews.
add_library(sc_headless STATIC src/scene.cpp dev/text_headless.cpp dev/audio_silent.cpp
  "${SC_KIT_DIR}/headless_text.cpp")
target_include_directories(sc_headless PUBLIC dev "${SC_KIT_DIR}")
target_compile_definitions(sc_headless PRIVATE KIT_FONT_DIR="${SC_FONT_DIR}")
target_link_libraries(sc_headless PUBLIC sc_core)

enable_testing()
add_executable(sc_rules_tests tests/rules_tests.cpp)
target_link_libraries(sc_rules_tests PRIVATE sc_core)
add_test(NAME sc_rules COMMAND sc_rules_tests)

add_executable(sc_preview tools/preview.cpp)
target_link_libraries(sc_preview PRIVATE sc_headless)
