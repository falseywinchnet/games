# Included by the generated registry, with GAME_MODULE_DIR set to this folder.
set(TS_CORE_SOURCES
  "${GAME_MODULE_DIR}/src/rules.cpp"
  "${GAME_MODULE_DIR}/src/save.cpp"
  "${GAME_MODULE_DIR}/src/stage.cpp"
  "${GAME_MODULE_DIR}/src/shapes.cpp"
  "${GAME_MODULE_DIR}/src/wood_volume.cpp"
  "${GAME_MODULE_DIR}/src/shell_volume.cpp"
  "${GAME_MODULE_DIR}/src/minerals.cpp"
  "${GAME_MODULE_DIR}/src/room.cpp"
  "${GAME_MODULE_DIR}/src/stone_render.cpp"
  "${GAME_MODULE_DIR}/src/tray.cpp")
add_library(ts_core STATIC ${TS_CORE_SOURCES} "${GAME_MODULE_DIR}/src/room_bake.cpp")
target_include_directories(ts_core PUBLIC "${GAME_MODULE_DIR}/src")
# Saves and assets come through PlaySuite (src/game_data.hpp); the stones and the tray
# are drawn with the render engine's rasteriser (shared/render).
target_link_libraries(ts_core PUBLIC game_paths render_core)
target_compile_definitions(ts_core PRIVATE _USE_MATH_DEFINES)
if(MSVC)
  target_compile_options(ts_core PRIVATE /utf-8 /W4 /fp:precise)
else()
  target_compile_options(ts_core PRIVATE -Wall -Wextra -ffp-contract=off)
endif()
include(CTest)
add_executable(tumbledstones_rules_tests "${GAME_MODULE_DIR}/tests/rules_tests.cpp")
target_link_libraries(tumbledstones_rules_tests PRIVATE ts_core)
add_test(NAME tumbledstones_rules COMMAND tumbledstones_rules_tests)
set_tests_properties(tumbledstones_rules PROPERTIES LABELS design)
if(GAMES_BUILD_APPLICATION)
  add_executable(tumbledstones_contract_tests "${GAME_MODULE_DIR}/tests/view_contract_tests.cpp")
  target_include_directories(tumbledstones_contract_tests PRIVATE "${CMAKE_SOURCE_DIR}/new-games/kit")
  target_link_libraries(tumbledstones_contract_tests PRIVATE vendor_game_ui)
  add_test(NAME tumbledstones_view_contract COMMAND tumbledstones_contract_tests)
  set_tests_properties(tumbledstones_view_contract PROPERTIES TIMEOUT 90
    ENVIRONMENT "GAMES_ASSET_DIR=${GAMES_RUNTIME_ASSET_DIR}")
else()
  # Development tools, only in the game's own build: the room's bake and the candidates.
  add_executable(ts_bake_room "${GAME_MODULE_DIR}/tools/bake_room.cpp")
  target_compile_options(ts_bake_room PRIVATE -O2)
  set(TS_KIT_DIR "${PLAYSUITE_SOURCE_DIR}/new-games/kit")
  add_library(ts_headless_text STATIC "${TS_KIT_DIR}/headless_text.cpp")
  target_include_directories(ts_headless_text PUBLIC "${TS_KIT_DIR}")
  target_compile_definitions(ts_headless_text PRIVATE KIT_FONT_DIR="${PLAYSUITE_SOURCE_DIR}/assets/fonts")
  # The scene with headless text and silent audio, for previews.
  add_library(ts_headless STATIC "${GAME_MODULE_DIR}/src/scene.cpp" "${GAME_MODULE_DIR}/dev/text_headless.cpp"
    "${GAME_MODULE_DIR}/dev/audio_silent.cpp")
  target_include_directories(ts_headless PUBLIC "${GAME_MODULE_DIR}/dev" "${GAME_MODULE_DIR}/src/platform")
  target_link_libraries(ts_headless PUBLIC ts_core ts_headless_text)
  add_executable(ts_preview "${GAME_MODULE_DIR}/tools/preview.cpp")
  target_link_libraries(ts_preview PRIVATE ts_headless)
  # Candidates under another room: TS_ROOMS names bakes beside the build (room_<name>.cpp).
  set(TS_ROOMS "" CACHE STRING "Room bakes to build candidate tools for")
  foreach(room IN LISTS TS_ROOMS)
    add_library(ts_core_${room} STATIC ${TS_CORE_SOURCES} "${CMAKE_BINARY_DIR}/room_${room}.cpp")
    target_include_directories(ts_core_${room} PUBLIC "${GAME_MODULE_DIR}/src")
    target_link_libraries(ts_core_${room} PUBLIC game_paths render_core)
    add_executable(ts_candidates_${room} "${GAME_MODULE_DIR}/tools/candidates.cpp")
    target_link_libraries(ts_candidates_${room} PRIVATE ts_core_${room} ts_headless_text)
  endforeach()
endif()
