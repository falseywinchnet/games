# Included by the generated registry (and by CMakeLists.txt), with GAME_MODULE_DIR set
# to this folder. The ambient engine (engines/ambient) is built first.
add_library(sw_core STATIC
  "${GAME_MODULE_DIR}/src/riverscape_look.cpp"
  "${GAME_MODULE_DIR}/src/treasure.cpp"
  "${GAME_MODULE_DIR}/src/shanty_voice.cpp"
  "${GAME_MODULE_DIR}/src/tank_voice.cpp")
target_include_directories(sw_core PUBLIC "${GAME_MODULE_DIR}/src")
target_link_libraries(sw_core PUBLIC ambient_core)
target_compile_definitions(sw_core PRIVATE _USE_MATH_DEFINES)
if(MSVC)
  target_compile_options(sw_core PRIVATE /utf-8 /W4 /fp:precise)
else()
  target_compile_options(sw_core PRIVATE -Wall -Wextra -ffp-contract=off)
endif()
include(CTest)
add_executable(stillwater_rules_tests "${GAME_MODULE_DIR}/tests/stillwater_tests.cpp")
target_link_libraries(stillwater_rules_tests PRIVATE sw_core)
add_test(NAME stillwater_rules COMMAND stillwater_rules_tests "${GAME_MODULE_DIR}/assets/scene/riverscape.ambient")
set_tests_properties(stillwater_rules PROPERTIES TIMEOUT 120)

# The live music and water, rendered to WAV: the fallback loops and listening copies.
add_executable(sw_music_render "${GAME_MODULE_DIR}/tools/music_render.cpp")
target_link_libraries(sw_music_render PRIVATE sw_core)

# Headless frames and timings (tools/preview.cpp); needs the kit's PNG writer.
get_filename_component(SW_KIT_DIR "${GAME_MODULE_DIR}/../../new-games/kit" ABSOLUTE)
if(EXISTS "${SW_KIT_DIR}/png_writer.hpp")
  add_executable(sw_preview "${GAME_MODULE_DIR}/tools/preview.cpp")
  target_include_directories(sw_preview PRIVATE "${SW_KIT_DIR}")
  target_link_libraries(sw_preview PRIVATE sw_core)
endif()

if(GAMES_BUILD_APPLICATION)
  add_executable(stillwater_view_contract_tests "${GAME_MODULE_DIR}/tests/view_contract_tests.cpp")
  target_include_directories(stillwater_view_contract_tests PRIVATE "${PROJECT_SOURCE_DIR}/new-games/kit")
  target_link_libraries(stillwater_view_contract_tests PRIVATE vendor_game_ui)
  add_test(NAME stillwater_view_contract COMMAND stillwater_view_contract_tests)
  set_tests_properties(stillwater_view_contract PROPERTIES TIMEOUT 90
    ENVIRONMENT "GAMES_ASSET_DIR=${GAMES_RUNTIME_ASSET_DIR}")
endif()
