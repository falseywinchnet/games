# The ambient scene engine. Included once by the generated registry (and by a game's
# own standalone build) with ENGINE_DIR set to this folder.
if(TARGET ambient_core)
  return()
endif()
# The renderer scans the engine's triangles (shared/render, r3d_scan.hpp) and shows its
# frames (PixelSurface); it is built first.
if(NOT TARGET render_core)
  set(AMBIENT_ENGINE_DIR "${ENGINE_DIR}")
  get_filename_component(ENGINE_DIR "${AMBIENT_ENGINE_DIR}/../render" ABSOLUTE)
  include("${ENGINE_DIR}/build.cmake")
  set(ENGINE_DIR "${AMBIENT_ENGINE_DIR}")
endif()
add_library(ambient_core STATIC
  "${ENGINE_DIR}/src/archive.cpp"
  "${ENGINE_DIR}/src/cadence.cpp"
  "${ENGINE_DIR}/src/caustics.cpp"
  "${ENGINE_DIR}/src/inflate.cpp"
  "${ENGINE_DIR}/src/motion.cpp"
  "${ENGINE_DIR}/src/present.cpp"
  "${ENGINE_DIR}/src/settings.cpp"
  "${ENGINE_DIR}/src/stage.cpp")
target_include_directories(ambient_core PUBLIC "${ENGINE_DIR}/src")
target_link_libraries(ambient_core PUBLIC render_core)
target_compile_features(ambient_core PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(ambient_core PRIVATE /utf-8 /W4 /fp:precise)
else()
  target_compile_options(ambient_core PRIVATE -Wall -Wextra -ffp-contract=off)
endif()

# The engine's view is compiled into the application's module library when a game
# uses the engine; this target carries its headers (and the renderer's) to that game's code.
add_library(ambient_ui INTERFACE)
target_include_directories(ambient_ui INTERFACE "${ENGINE_DIR}/ui")
target_link_libraries(ambient_ui INTERFACE ambient_core render_ui)

include(CTest)
add_executable(ambient_tests "${ENGINE_DIR}/tests/ambient_tests.cpp")
target_link_libraries(ambient_tests PRIVATE ambient_core)
add_test(NAME ambient_engine COMMAND ambient_tests)
set_tests_properties(ambient_engine PROPERTIES TIMEOUT 60)
