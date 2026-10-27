# The polished stones. Included once by the generated registry (and by a game's own
# standalone build) with ENGINE_DIR set to this folder.
if(TARGET stones_core)
  return()
endif()
# The stones are rasterised by the renderer (shared/render); it is built first.
if(NOT TARGET render_core)
  set(STONES_ENGINE_DIR "${ENGINE_DIR}")
  get_filename_component(ENGINE_DIR "${STONES_ENGINE_DIR}/../render" ABSOLUTE)
  include("${ENGINE_DIR}/build.cmake")
  set(ENGINE_DIR "${STONES_ENGINE_DIR}")
endif()
add_library(stones_core STATIC
  "${ENGINE_DIR}/src/shapes.cpp"
  "${ENGINE_DIR}/src/wood_volume.cpp"
  "${ENGINE_DIR}/src/shell_volume.cpp"
  "${ENGINE_DIR}/src/candy_volume.cpp"
  "${ENGINE_DIR}/src/flake_volume.cpp"
  "${ENGINE_DIR}/src/minerals.cpp"
  "${ENGINE_DIR}/src/room.cpp"
  "${ENGINE_DIR}/src/room_bake.cpp"
  "${ENGINE_DIR}/src/stone_render.cpp"
  "${ENGINE_DIR}/src/tray.cpp")
target_include_directories(stones_core PUBLIC "${ENGINE_DIR}/src")
target_link_libraries(stones_core PUBLIC render_core)
target_compile_features(stones_core PUBLIC cxx_std_20)
target_compile_definitions(stones_core PRIVATE _USE_MATH_DEFINES)
if(MSVC)
  target_compile_options(stones_core PRIVATE /utf-8 /W4 /fp:precise)
else()
  target_compile_options(stones_core PRIVATE -Wall -Wextra -ffp-contract=off)
endif()

include(CTest)
add_executable(stones_tests "${ENGINE_DIR}/tests/stones_tests.cpp")
target_link_libraries(stones_tests PRIVATE stones_core)
add_test(NAME stones_engine COMMAND stones_tests)
set_tests_properties(stones_engine PROPERTIES TIMEOUT 120)

# The room's bake: path traces the room once and writes room_bake.cpp.
#   stones_bake_room <sun|window> <samples per texel> <out.cpp>
add_executable(stones_bake_room "${ENGINE_DIR}/tools/bake_room.cpp")
