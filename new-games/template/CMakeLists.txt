# The game's own build: its rules, its tests and its preview tool, with nothing but
# a C++20 compiler. It does not build the PlaySuite window; that happens in the
# repository's application build, where cmake/Application.cmake compiles the view.
#
#   cmake -S . -B build && cmake --build build -j 2 && ctest --test-dir build --output-on-failure
#   build/tg_preview shot.png 600 370
cmake_minimum_required(VERSION 3.25)
project(TemplateGame VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# The kit lives in the repository's new-games folder. In the template that is two
# levels up; in games/<game> it is ../../new-games/kit.
set(TG_KIT_DIR "" CACHE PATH "The repository's new-games/kit directory")
if(NOT TG_KIT_DIR)
  foreach(candidate "${CMAKE_CURRENT_SOURCE_DIR}/../kit" "${CMAKE_CURRENT_SOURCE_DIR}/../../new-games/kit")
    if(EXISTS "${candidate}/headless_text.hpp")
      get_filename_component(TG_KIT_DIR "${candidate}" ABSOLUTE)
      break()
    endif()
  endforeach()
endif()
if(NOT EXISTS "${TG_KIT_DIR}/headless_text.hpp")
  message(FATAL_ERROR "Set TG_KIT_DIR to the repository's new-games/kit directory")
endif()
get_filename_component(TG_FONT_DIR "${TG_KIT_DIR}/../../assets/fonts" ABSOLUTE)

# Pure game: rules, save, layout. This is what PlaySuite's portable-core profile builds.
add_library(tg_core STATIC src/platform/raster.cpp src/rules.cpp src/save.cpp src/stage.cpp)
target_include_directories(tg_core PUBLIC src)
target_compile_definitions(tg_core PRIVATE _USE_MATH_DEFINES)
if(NOT MSVC)
  target_compile_options(tg_core PRIVATE -Wall -Wextra)
endif()

# The scene with headless text and silent audio, for previews.
add_library(tg_headless STATIC src/scene.cpp dev/text_headless.cpp dev/audio_silent.cpp
  "${TG_KIT_DIR}/headless_text.cpp")
target_include_directories(tg_headless PUBLIC dev "${TG_KIT_DIR}")
target_compile_definitions(tg_headless PRIVATE KIT_FONT_DIR="${TG_FONT_DIR}")
target_link_libraries(tg_headless PUBLIC tg_core)

enable_testing()
add_executable(tg_rules_tests tests/rules_tests.cpp)
target_link_libraries(tg_rules_tests PRIVATE tg_core)
add_test(NAME tg_rules COMMAND tg_rules_tests)

add_executable(tg_preview tools/preview.cpp)
target_link_libraries(tg_preview PRIVATE tg_headless)
